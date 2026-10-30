#!/usr/bin/env python3
"""Fetches the CC0 PBR textures the Backyard generator uses.

Source: https://ambientcg.com
License: CC0 1.0 Universal (public domain). No credit required; see
https://docs.ambientcg.com/license/. Only the 1K-JPG archives are downloaded,
which is plenty for a 9 m pool and a 18 m house seen from a few metres.

NormalGL is the OpenGL-convention normal map (+Y up), which is what glTF and this
engine expect. NormalDX ships in the same archive and is never extracted: using it
makes every bump read inverted.

Every archive's sha256 is pinned below, so a changed upstream file fails loudly
instead of silently changing the look of the scene. To re-pin an entry
deliberately, set its hash to "" and the script records (and prints) the current
one on the next run.

Output goes to an uncommitted cache, one directory per asset:

    T850/build/texture-cache/<ID>/Color.jpg
                                  NormalGL.jpg
                                  Roughness.jpg
                                  AmbientOcclusion.jpg   (when the archive has one)

No network access happens at runtime; the generator reads the cache.
"""

import hashlib
import os
import sys
import urllib.request
import zipfile

CACHE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "..", "build", "texture-cache")
DOWNLOAD = "https://ambientcg.com/get?file=%s_1K-JPG.zip"
# ambientCG answers 403 to clients with no user agent, which is what urllib sends by
# default. Any ordinary UA works; this one just says what it is.
USER_AGENT = "t850-backyard-texture-fetch/1.0 (+https://ambientcg.com/license/)"

# (material as named in build_backyard.py, ambientCG asset id, zip sha256)
TARGETS = [
    ("House_Stucco",     "Plaster001",        "944b4831016e42ace4a89422e4e7190912ca2f7fed6f561354c48ec7bb54d3a4"),
    ("House_RoofMetal",  "CorrugatedSteel005", "4198faced790c75c551750d323ef384ea1ab353463b2067f44f8e5f5ff2d1a09"),
    ("Ground_Patio",     "Concrete034",       "5839d284d94ffb8d2a56df742ec522b13dd311c52dbd42b8fd33f0409ceedb81"),
    ("Ground_Gravel",    "Gravel043",         "bca54835c7c71bea4b4b9f27fbef127d666fa1cf7de16f2b04f0ceb4294e784c"),
    ("Pool_Coping",      "PavingStones150",   "bcaffad6b5ae522394a740b698dbf1b478ff9daca465a3acca20ddbc45e1442f"),
    ("Wall_Adobe",       "Plaster007",        "75c7d96a8e60c4c5073767508e34e88c73677a3641e40bb8aaa0f1d6674391c0"),
]

# The maps to keep, in the order they appear in the archive name.
WANTED = [
    ("_Color.jpg", "Color.jpg"),
    ("_NormalGL.jpg", "NormalGL.jpg"),
    ("_Roughness.jpg", "Roughness.jpg"),
    ("_AmbientOcclusion.jpg", "AmbientOcclusion.jpg"),
]


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch(material, asset_id, pinned):
    out_dir = os.path.join(CACHE_DIR, asset_id)
    marker = os.path.join(out_dir, ".complete")
    if os.path.exists(marker):
        print("%-16s %-18s cached" % (material, asset_id))
        return True

    os.makedirs(out_dir, exist_ok=True)
    zip_path = os.path.join(CACHE_DIR, asset_id + "_1K-JPG.zip")
    if not os.path.exists(zip_path):
        url = DOWNLOAD % asset_id
        print("%-16s %-18s downloading %s" % (material, asset_id, url))
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=180) as response, \
                    open(zip_path, "wb") as handle:
                handle.write(response.read())
        except Exception as exc:                                   # noqa: BLE001
            print("  FAILED: %s" % exc)
            if os.path.exists(zip_path):
                os.remove(zip_path)
            return False

    digest = sha256_of(zip_path)
    if pinned and digest != pinned:
        print("  SHA256 MISMATCH for %s: pinned %s, got %s" % (asset_id, pinned, digest))
        return False
    if not pinned:
        print("  sha256 %s  <-- pin this in TARGETS" % digest)

    extracted = 0
    with zipfile.ZipFile(zip_path) as archive:
        names = archive.namelist()
        for suffix, friendly in WANTED:
            match = next((n for n in names if n.endswith(suffix)), None)
            if not match:
                continue                                            # AO is optional
            with archive.open(match) as src, open(os.path.join(out_dir, friendly), "wb") as dst:
                dst.write(src.read())
            extracted += 1

    if extracted < 3:
        print("  only %d of the expected maps found in %s" % (extracted, asset_id))
        return False
    with open(marker, "w", encoding="utf-8") as handle:
        handle.write(digest + "\n")
    print("%-16s %-18s %d maps" % (material, asset_id, extracted))
    return True


def main():
    os.makedirs(CACHE_DIR, exist_ok=True)
    failures = 0
    for material, asset_id, pinned in TARGETS:
        if not fetch(material, asset_id, pinned):
            failures += 1
    print("\n%d/%d textures ready in %s" % (len(TARGETS) - failures, len(TARGETS), CACHE_DIR))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
