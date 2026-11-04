#!/usr/bin/env python3
"""Guard the cloud manifests' integrity fields.

Every downloader and the browser catalog verify an asset by `size` and, when the manifest publishes
one, by `sha256`. An entry without them silently degrades to "exists and is non-empty", which is how
a same-size corruption stayed invisible until 2026-11-04. This script is the CI-side check that the
fields are actually there, for the entries anything consumes.

What it checks:

  1. `runtime_assets.json` (t850-models): **every** entry has `size > 0` and a 64-hex `sha256`.
     This is the only model source any downloader reads.
  2. `manifest.json` (the union, identical in both buckets): every **texture** entry has
     `size > 0` and `sha256`. The union's model entries are inventory only — all four downloaders and
     the web catalog take models from `runtime_assets.json` — so a missing hash there is reported as
     a note, not a failure; hashing them would mean downloading the whole ~1 GB model library.
  3. Both buckets publish byte-identical union manifests.
  4. Every union entry's `url` points at the bucket its `bucket` field names.

Usage:
    python scripts/validation/check_cloud_manifests.py
    python scripts/validation/check_cloud_manifests.py --offline  # use saved copies
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
import urllib.request

MODEL_MANIFEST = "https://pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev/runtime_assets.json"
UNION_MANIFEST_TEXTURES = "https://pub-ef5de729f9044220aa32f0601d99faa8.r2.dev/manifest.json"
UNION_MANIFEST_MODELS = "https://pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev/manifest.json"
BUCKET_HOSTS = {
    "t850-models": "pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev",
    "t850-textures": "pub-ef5de729f9044220aa32f0601d99faa8.r2.dev",
}
HEADERS = {"User-Agent": "T850-ManifestCheck/1.0"}


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()


def load(url: str, path: str | None) -> tuple[dict, bytes]:
    raw = open(path, "rb").read() if path else fetch(url)
    return json.loads(raw.decode("utf-8")), raw


def is_hex64(value) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value.lower())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="read the manifests from --models/--textures")
    parser.add_argument("--models", default=None, help="saved runtime_assets.json")
    parser.add_argument("--textures", default=None, help="saved union manifest.json")
    args = parser.parse_args()

    problems: list[str] = []
    notes: list[str] = []

    models, _ = load(MODEL_MANIFEST, args.models if args.offline else None)
    model_entries = models.get("assets") or []
    if not model_entries:
        problems.append("runtime_assets.json has no assets")
    for entry in model_entries:
        key = entry.get("key") or entry.get("localRelativePath") or "?"
        if int(entry.get("size") or 0) <= 0:
            problems.append(f"runtime_assets.json: {key} has no size")
        if not is_hex64(entry.get("sha256")):
            problems.append(f"runtime_assets.json: {key} has no sha256")

    union, union_bytes = load(UNION_MANIFEST_TEXTURES, args.textures if args.offline else None)
    union_entries = union.get("assets") or []
    if not union_entries:
        problems.append("manifest.json has no assets")
    texture_missing_hash = 0
    model_missing_hash = 0
    for entry in union_entries:
        key = entry.get("key") or "?"
        kind = str(entry.get("kind") or "texture")
        bucket = str(entry.get("bucket") or "")
        size = int(entry.get("size") or 0)
        if size <= 0:
            problems.append(f"manifest.json: {key} has no size")
        if not is_hex64(entry.get("sha256")):
            if kind == "texture":
                texture_missing_hash += 1
                problems.append(f"manifest.json: texture {key} has no sha256")
            else:
                model_missing_hash += 1
        url = str(entry.get("url") or "")
        host = BUCKET_HOSTS.get(bucket)
        if host and host not in url:
            problems.append(f"manifest.json: {key} is in {bucket} but its url is {url}")

    if model_missing_hash:
        notes.append(f"{model_missing_hash} union model entr(ies) have no sha256; nothing consumes them "
                     f"(models come from runtime_assets.json), so they are inventory only")

    if not args.offline:
        try:
            _, other_bytes = load(UNION_MANIFEST_MODELS, None)
            if hashlib.sha256(other_bytes).hexdigest() != hashlib.sha256(union_bytes).hexdigest():
                problems.append("the two buckets publish different manifest.json copies")
        except Exception as error:  # noqa: BLE001 - report, do not crash
            problems.append(f"could not verify the second bucket's manifest.json: {error}")

    print(f"runtime_assets.json: {len(model_entries)} asset(s)")
    print(f"manifest.json:       {len(union_entries)} asset(s), {texture_missing_hash} texture(s) without a hash")
    for note in notes:
        print(f"  note: {note}")
    if problems:
        for problem in problems:
            print(f"  FAIL {problem}")
        print(f"FAIL: {len(problems)} problem(s)")
        return 1
    print("PASS: every consumed entry carries a size and a sha256")
    return 0


if __name__ == "__main__":
    sys.exit(main())
