#!/usr/bin/env python3
import argparse
import concurrent.futures
import json
import os
import sys
import tempfile
import urllib.request
from pathlib import Path


DEFAULT_MODEL_MANIFEST_URL = "https://pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev/runtime_assets.json"
DEFAULT_TEXTURE_MANIFEST_URL = "https://pub-ef5de729f9044220aa32f0601d99faa8.r2.dev/manifest.json"
REQUEST_HEADERS = {"User-Agent": "T850-SteamDeck-AssetDownloader/1.0"}


def load_json_url(url):
    request = urllib.request.Request(url, headers=REQUEST_HEADERS)
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read().decode("utf-8"))


def normalize_resource_path(path):
    value = (path or "").replace("\\", "/").strip()
    while value.startswith("/"):
        value = value[1:]
    if value.lower().startswith("assets/"):
        value = value[7:]
    return value


def model_entries(manifest):
    entries = []
    for asset in manifest.get("models") or manifest.get("assets") or []:
        kind = str(asset.get("kind", "model"))
        if kind and kind.lower() != "model":
            continue
        resource = asset.get("localRelativePath") or asset.get("key") or asset.get("path") or ""
        resource = normalize_resource_path(resource)
        if not resource:
            continue
        if resource.lower().startswith("textures/"):
            continue
        if not resource.lower().startswith("models/"):
            resource = f"Models/{resource}"
        entries.append({
            "path": resource,
            "url": asset.get("url"),
            "size": int(asset.get("size") or 0),
        })
    return entries


def texture_entries(manifest):
    entries = []
    for asset in manifest.get("textures") or manifest.get("assets") or []:
        resource = asset.get("localRelativePath") or asset.get("key") or asset.get("path") or ""
        resource = normalize_resource_path(resource)
        kind = str(asset.get("kind", ""))
        is_texture = kind.lower() == "texture" or resource.lower().startswith("textures/")
        if not is_texture:
            continue
        if not resource:
            continue
        if not resource.lower().startswith("textures/"):
            resource = f"Textures/{resource}"
        entries.append({
            "path": resource,
            "url": asset.get("url"),
            "size": int(asset.get("size") or 0),
        })
    return entries


def is_ready(asset_root, entry):
    path = asset_root / entry["path"]
    if not path.exists() or not path.is_file():
        return False
    if entry["size"] > 0 and path.stat().st_size != entry["size"]:
        return False
    return path.stat().st_size > 0


def download_one(asset_root, entry):
    target = asset_root / entry["path"]
    url = entry.get("url")
    if not url:
        raise RuntimeError(f"No URL for {entry['path']}")
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=target.name + ".", suffix=".download", dir=str(target.parent))
    os.close(fd)
    tmp = Path(tmp_name)
    try:
        request = urllib.request.Request(url, headers=REQUEST_HEADERS)
        with urllib.request.urlopen(request, timeout=120) as response, tmp.open("wb") as out:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                out.write(chunk)
        if entry["size"] > 0 and tmp.stat().st_size != entry["size"]:
            raise RuntimeError(f"size mismatch for {entry['path']}: expected {entry['size']}, got {tmp.stat().st_size}")
        tmp.replace(target)
        return entry["path"]
    except Exception:
        try:
            tmp.unlink(missing_ok=True)
        except Exception:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description="Download T850 Steam Deck cloud assets.")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[1]), help="T850 root directory")
    parser.add_argument("--model-manifest-url", default=DEFAULT_MODEL_MANIFEST_URL)
    parser.add_argument("--texture-manifest-url", default=DEFAULT_TEXTURE_MANIFEST_URL)
    parser.add_argument("--jobs", type=int, default=7)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    asset_root = root / "Assets"
    asset_root.mkdir(parents=True, exist_ok=True)

    print("[T850] Loading model manifest...")
    models = model_entries(load_json_url(args.model_manifest_url))
    print("[T850] Loading texture manifest...")
    textures = texture_entries(load_json_url(args.texture_manifest_url))
    entries = models + textures

    missing = [entry for entry in entries if not is_ready(asset_root, entry)]
    ready = len(entries) - len(missing)
    print(f"[T850] Cloud assets ready={ready} missing={len(missing)} total={len(entries)}")
    if args.check_only or not missing:
        return 0 if not missing else 2

    errors = []
    downloaded = 0
    max_workers = max(1, min(args.jobs, len(missing)))
    print(f"[T850] Downloading {len(missing)} asset(s) with {max_workers} worker(s)...")
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        future_to_entry = {executor.submit(download_one, asset_root, entry): entry for entry in missing}
        for future in concurrent.futures.as_completed(future_to_entry):
            entry = future_to_entry[future]
            try:
                path = future.result()
                downloaded += 1
                print(f"[T850] Downloaded {downloaded}/{len(missing)}: {path}")
            except Exception as exc:
                errors.append(f"{entry['path']}: {exc}")
                print(f"[T850] ERROR {entry['path']}: {exc}", file=sys.stderr)

    if errors:
        print("[T850] Cloud asset download failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        return 1

    print(f"[T850] Cloud assets ready ({downloaded} downloaded, {ready} already present).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
