#!/usr/bin/env python3
"""Install the cloud asset set for a T850 runtime tree (Steam Deck and Linux).

The runtime contract, shared with the Android downloader and the web catalog
(documentation/development/cloud-assets.md):

  * Two manifests, published by scripts/PushCloudModels.ps1 (models) and
    scripts/PushCloudTextures.ps1 (the union manifest, which holds models too).
    Every entry carries ``localRelativePath`` (relative to ``Assets/``), ``url``,
    ``size`` and, for anything published since 2026-11, ``sha256``.
  * An asset is READY when the file exists, has the declared size, and matches the
    declared sha256 when the manifest publishes one.
  * Downloads are atomic: write a temp file beside the target, verify it, rename.
  * A failed manifest fetch or download is a WARNING, not a hard stop, as long as
    the stamped verification still shows a complete tree.
  * One status line, identical on every platform:
        [T850] Cloud assets ready=N missing=M total=T

Why a stamp: this script runs on every launch (steamdeck/T850.sh), and re-hashing
16.7 MB plus hitting two manifests each time made a launch wait on the network. The
stamp (``Assets/.t850-cloud-assets.json``) records the sha256 of both manifests and
when the tree was last verified, so:

  * within ``--fresh-seconds`` of a complete verification: no network at all;
  * manifests unchanged since the stamp: sizes are re-checked (cheap, catches a
    deleted file) but contents are not re-hashed;
  * manifests changed: full verify, including hashes.

Exit codes: 0 complete (including the offline-with-a-complete-stamp case), 1 could
not converge, 2 ``--check-only`` and something is missing.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_MODEL_MANIFEST_URL = "https://pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev/runtime_assets.json"
DEFAULT_TEXTURE_MANIFEST_URL = "https://pub-ef5de729f9044220aa32f0601d99faa8.r2.dev/manifest.json"
REQUEST_HEADERS = {"User-Agent": "T850-SteamDeck-AssetDownloader/2.0"}
STAMP_NAME = ".t850-cloud-assets.json"
READ_TIMEOUT = 60
MANIFEST_TIMEOUT = 30
DEFAULT_FRESH_SECONDS = 300


def log(message):
    print(f"[T850] {message}", flush=True)


def warn(message):
    print(f"[T850] WARNING: {message}", file=sys.stderr, flush=True)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def fetch_bytes(url, timeout):
    request = urllib.request.Request(url, headers=REQUEST_HEADERS)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def load_manifest(url):
    """Parsed manifest and the sha256 of its bytes, or raise."""
    raw = fetch_bytes(url, MANIFEST_TIMEOUT)
    return json.loads(raw.decode("utf-8")), sha256_bytes(raw)


def normalize_resource_path(path):
    value = (path or "").replace("\\", "/").strip()
    while value.startswith("/"):
        value = value[1:]
    if value.lower().startswith("assets/"):
        value = value[7:]
    return value


def make_entry(asset, default_root):
    resource = asset.get("localRelativePath") or asset.get("key") or asset.get("path") or ""
    resource = normalize_resource_path(resource)
    if not resource:
        return None
    if not resource.lower().startswith(default_root.lower() + "/"):
        resource = f"{default_root}/{resource}"
    return {
        "path": resource,
        "url": asset.get("url"),
        "size": int(asset.get("size") or 0),
        "sha256": (asset.get("sha256") or "").lower(),
    }


def model_entries(manifest):
    entries = []
    for asset in manifest.get("models") or manifest.get("assets") or []:
        kind = str(asset.get("kind", "model"))
        if kind and kind.lower() != "model":
            continue
        entry = make_entry(asset, "Models")
        if not entry or entry["path"].lower().startswith("textures/"):
            continue
        entries.append(entry)
    return entries


def texture_entries(manifest):
    entries = []
    for asset in manifest.get("textures") or manifest.get("assets") or []:
        resource = normalize_resource_path(
            asset.get("localRelativePath") or asset.get("key") or asset.get("path") or "")
        kind = str(asset.get("kind", ""))
        if not (kind.lower() == "texture" or resource.lower().startswith("textures/")):
            continue
        entry = make_entry(asset, "Textures")
        if entry:
            entries.append(entry)
    return entries


def is_ready(asset_root, entry, verify_hash):
    path = asset_root / entry["path"]
    try:
        stat = path.stat()
    except OSError:
        return False
    if not path.is_file() or stat.st_size <= 0:
        return False
    if entry["size"] > 0 and stat.st_size != entry["size"]:
        return False
    if verify_hash and entry["sha256"]:
        return sha256_file(path) == entry["sha256"]
    return True


def download_one(asset_root, entry):
    target = asset_root / entry["path"]
    url = entry.get("url")
    if not url:
        raise RuntimeError("no URL in the manifest")
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=target.name + ".", suffix=".download", dir=str(target.parent))
    os.close(fd)
    tmp = Path(tmp_name)
    try:
        request = urllib.request.Request(url, headers=REQUEST_HEADERS)
        with urllib.request.urlopen(request, timeout=READ_TIMEOUT) as response, tmp.open("wb") as out:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                out.write(chunk)
        actual = tmp.stat().st_size
        if entry["size"] > 0 and actual != entry["size"]:
            raise RuntimeError(f"size {actual} != expected {entry['size']}")
        if entry["sha256"] and sha256_file(tmp) != entry["sha256"]:
            raise RuntimeError("sha256 mismatch")
        tmp.replace(target)
        return entry["path"]
    except Exception:
        try:
            tmp.unlink(missing_ok=True)
        except OSError:
            pass
        raise


def read_stamp(asset_root):
    try:
        with (asset_root / STAMP_NAME).open("rb") as handle:
            stamp = json.loads(handle.read().decode("utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(stamp, dict) or stamp.get("version") != 1:
        return None
    return stamp


def write_stamp(asset_root, manifests, ready, total, entries):
    """Record what was verified, including the entry list.

    The entry list matters: it lets the fresh path (below) re-check every file's size with 158
    `stat` calls and no network, so a file deleted a minute after a verified run is still
    restored on the next launch instead of waiting out the freshness window.
    """
    stamp = {
        "version": 1,
        "manifests": manifests,
        "ready": ready,
        "total": total,
        "checkedAt": int(time.time()),
        "entries": [{"path": e["path"], "size": e["size"], "sha256": e["sha256"]} for e in entries],
    }
    try:
        fd, tmp_name = tempfile.mkstemp(prefix=STAMP_NAME, dir=str(asset_root))
        os.close(fd)
        tmp = Path(tmp_name)
        tmp.write_text(json.dumps(stamp, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        tmp.replace(asset_root / STAMP_NAME)
    except OSError as error:
        warn(f"could not write {STAMP_NAME}: {error}")


def main():
    parser = argparse.ArgumentParser(description="Download T850 cloud assets for the Steam Deck / Linux runtime.")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[1]), help="T850 root directory")
    parser.add_argument("--model-manifest-url", default=DEFAULT_MODEL_MANIFEST_URL)
    parser.add_argument("--texture-manifest-url", default=DEFAULT_TEXTURE_MANIFEST_URL)
    parser.add_argument("--jobs", type=int, default=7)
    parser.add_argument("--check-only", action="store_true", help="verify and report, never download")
    parser.add_argument("--fresh-seconds", type=int, default=DEFAULT_FRESH_SECONDS,
                        help="skip the network when the tree was fully verified this recently (0 disables)")
    parser.add_argument("--no-stamp", action="store_true", help="ignore and do not write the verification stamp")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    asset_root = root / "Assets"
    asset_root.mkdir(parents=True, exist_ok=True)
    stamp = None if args.no_stamp else read_stamp(asset_root)

    # 1. Fresh stamp: the tree was complete a moment ago, so do not touch the network. This is what
    #    stops a launch from waiting on two manifests every single time.
    if (not args.check_only and stamp and args.fresh_seconds > 0
            and stamp.get("ready") == stamp.get("total") and stamp.get("total")
            and stamp.get("entries")):
        age = time.time() - float(stamp.get("checkedAt") or 0)
        stamped = [{"path": e.get("path", ""), "size": int(e.get("size") or 0),
                    "sha256": (e.get("sha256") or "").lower(), "url": None}
                   for e in stamp["entries"] if e.get("path")]
        gone = [entry for entry in stamped if not is_ready(asset_root, entry, False)]
        if 0 <= age < args.fresh_seconds and not gone:
            total = int(stamp["total"])
            log(f"Cloud assets ready={total} missing=0 total={total} (verified {int(age)}s ago)")
            return 0

    # 2. Manifests. Failure here is only fatal when nothing verified is on disk.
    manifests = {}
    entries = []
    try:
        model_manifest, model_hash = load_manifest(args.model_manifest_url)
        manifests["models"] = model_hash
        entries.extend(model_entries(model_manifest))
        texture_manifest, texture_hash = load_manifest(args.texture_manifest_url)
        manifests["textures"] = texture_hash
        entries.extend(texture_entries(texture_manifest))
    except (urllib.error.URLError, OSError, ValueError) as error:
        if stamp and stamp.get("ready") == stamp.get("total") and stamp.get("total"):
            warn(f"manifests unavailable ({error}); using the last verified tree "
                 f"({stamp['ready']}/{stamp['total']} assets)")
            return 0
        warn(f"manifests unavailable ({error}) and no verified tree on disk")
        return 1

    if not entries:
        warn("the manifests contain no runtime assets")
        return 1

    # 3. Hashes are only re-checked when the manifests moved: 158 stats are cheap, 16.7 MB of
    #    hashing on every launch is not.
    unchanged = bool(stamp) and stamp.get("manifests") == manifests
    missing = [entry for entry in entries if not is_ready(asset_root, entry, not unchanged)]
    ready = len(entries) - len(missing)
    log(f"Cloud assets ready={ready} missing={len(missing)} total={len(entries)}")

    if args.check_only:
        return 0 if not missing else 2

    if missing and unchanged:
        # The stamp matched, yet files are gone: fall back to content verification.
        missing = [entry for entry in entries if not is_ready(asset_root, entry, True)]
        ready = len(entries) - len(missing)

    if not missing:
        if not args.no_stamp:
            write_stamp(asset_root, manifests, ready, len(entries), entries)
        log(f"Cloud assets ready ({ready} already present).")
        return 0

    # 4. Download what is missing, atomically and verified.
    errors = []
    downloaded = 0
    workers = max(1, min(args.jobs, len(missing)))
    log(f"Downloading {len(missing)} asset(s) with {workers} worker(s)...")
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        futures = {executor.submit(download_one, asset_root, entry): entry for entry in missing}
        for future in concurrent.futures.as_completed(futures):
            entry = futures[future]
            try:
                path = future.result()
                downloaded += 1
                log(f"Downloaded {downloaded}/{len(missing)}: {path}")
            except Exception as error:  # report every failure, never abort the batch
                errors.append(f"{entry['path']}: {error}")
                warn(f"{entry['path']}: {error}")

    if errors:
        warn(f"{len(errors)} of {len(missing)} asset(s) failed; the game still runs with what is installed")
        if stamp:
            # No longer fully verified, so the next launch must not trust the stamp.
            write_stamp(asset_root, manifests, ready + downloaded, len(entries), entries)
        return 1

    if not args.no_stamp:
        write_stamp(asset_root, manifests, len(entries), len(entries), entries)
    log(f"Cloud assets ready ({downloaded} downloaded, {ready} already present).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())