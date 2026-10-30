#!/usr/bin/env python3
"""Prepares the CC0 PBR palette the Backyard balls are rendered with.

Source: https://ambientcg.com -- CC0 1.0 Universal (public domain). The raw files may
be shipped inside a project; no credit is required. See
https://docs.ambientcg.com/license/. Only the 1K-JPG archives are downloaded, which is
plenty for a ball that covers a few hundred pixels.

This script is an AUTHORING step: it runs on the machine that publishes the assets.
Players do not run it -- they download the packed files through the cloud texture
manifest with scripts\\DownloadTextures.ps1, exactly like the sky cubemaps. After a run,
publish with scripts\\PushCloudTextures.ps1.

For every material in Assets/Scenes/BackyardBallMaterials.json it writes, at 512 px:

    Assets/Textures/Ball/<id>/Color.jpg    base colour   (sRGB, JPEG)
    Assets/Textures/Ball/<id>/Normal.png   NormalGL      (OpenGL convention, PNG: no JPEG
                                                          ringing, which shows up as
                                                          specular noise on a ball)
    Assets/Textures/Ball/<id>/ORM.png      packed glTF metallic-roughness + AO in R,
                                                          roughness in G, metalness in B

The engine's METALLIC_MAP path is `metallic = factor.x * sample.b` and
`roughness = factor.y * sample.g`, so the packed map carries both. NormalDX ships in the
same archive and is NEVER extracted: using it inverts every bump.

The archive sha256 is pinned per material. To re-pin an entry deliberately, set its hash
to "" and the script records (and prints) the current one on the next run.

Run: python scripts/textures/fetch_ball_materials.py [--resolution 512] [--force]
"""

import argparse
import hashlib
import io
import json
import os
import sys
import urllib.request
import zipfile

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: python -m pip install pillow")

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
T850_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))
CACHE_DIR = os.path.join(T850_DIR, "build", "texture-cache")
ASSET_DIR = os.path.join(T850_DIR, "Assets", "Textures", "Ball")
PALETTE = os.path.join(T850_DIR, "Assets", "Scenes", "BackyardBallMaterials.json")

DOWNLOAD = "https://ambientcg.com/get?file=%s_1K-JPG.zip"
# ambientCG answers 403 to clients with no user agent, which is what urllib sends by
# default. Any ordinary UA works; this one just says what it is.
USER_AGENT = "t850-backyard-ball-materials/1.0 (+https://ambientcg.com/license/)"

# Pinned archive hashes: a changed upstream file fails loudly instead of silently
# changing the look of the palette. Re-pin deliberately by setting a hash to "".
PINNED = {
    "Chainmail001": "63f572ae3c2298057624606a3c12bba83d1c9f7bb096c9c742a9a226275b4c84",
    "Concrete047A": "94e04bdf80c4fa377f8c736183963cdd911131a26673e2462495e45bf679102f",
    "Cork001": "54e845caae513c86d244fefe794e27140317eb91642f88937afedfded2032c82",
    "CorrugatedSteel005": "4198faced790c75c551750d323ef384ea1ab353463b2067f44f8e5f5ff2d1a09",
    "DiamondPlate001": "6db1acd4576c5433aea0bb9391e8a5a0285b45b4be4beeb6f98cb6ff27283c13",
    "Leather008": "4a4357afdeb082d8c1751d45f4b281bca580a0b327fa07a9f0ef9963e1019a18",
    "Marble001": "76468a81047917a4f4d6c8c789c828e6e39c8a4e9b71e904787d79752e0fa34e",
    "Marble002": "6ac9604f331d5b58b98c648c7c95629f442dd633a336b33f39073170733e07d0",
    "Marble003": "57865842dfa08f2363e32c505aa12faebaab9701339d83198472883c9b62927c",
    "Marble012": "b04384c6d49ce73baeb70db7ea4abfe9948fc9b74b9fd92c3766970662bf2c43",
    "Marble016": "3740ddfd7d355b8bd5d89a36a854070b9fdf5780e11a7c2c35b5d13c56ff64ee",
    "Metal009": "1b1970219b40fe707e7194b28702f1a65b0d227bbea56da69dfbc8d8b5cfcb5e",
    "Metal027": "12b2d9996d532cf8a31820d24f2ea83e3217b89da060c4fb29c3c5177535ac72",
    "Metal032": "9e4f363905a647958c273154fa125d2a6440892d0e583f9b3af24bb08b5afc3c",
    "Metal038": "804a0e45cc923c71277f89b7e1a7b95bd8786899254e43692eb781b8c3981aae",
    "Metal043A": "78c9c68dd032f250d9aa000e498fb1facbeb4f993cb18bde2bc676258d57b55f",
    "Metal043B": "c2149c1fc032223f8e361409cde09dc19424bc3c05d58b86a94d2cc084d162dd",
    "Metal046A": "9b6ce754393bc7dd7899ce3874576ad504d33aa380098aa2751ce1f6e4981b37",
    "Metal046B": "b4fc855a414f7a1e415af6edaef24c0dc114f7802b3879a3db88e6ba1c97cda0",
    "Metal048A": "3915edefd3c8daaa3deb8be4070af19ff5663977ec01dbc677f1fa9bd3c07e5d",
    "Metal049A": "4f6a79e535261ab55cc6feefed0037124885720ce7b781fc2efa3fa22792af42",
    "MetalPlates006": "0b7d3736bf76103f158bc2f550e28b54a2ea9ba553c9253ac172eca3bafb02b5",
    "Onyx001": "bed3440d710da38b72c782f983e232c2a1fc3c1a8acf6615ad79f450f4aa057c",
    "Rock030": "2fd0bc2d214e2487c19062ed0798a85dd135b41cfc12bbaed42d5acae238f989",
    "Terrazzo001": "461e2867399393c1d49334202c931627867e83d7df9ec144dc62d430f10d3bee",
    "Terrazzo005": "d8b28aa460137005f1942399985d70f459c898653fd0ed3a683192c28844ec5a",
    "Tiles139": "0ac4f6e094874948027a1477ad4b9e4986d48ccede608ae59874672ab46265db",
    "Travertine001": "a5362ed94bc017fd232a986f1e4cc47c91037590b4cd5b0154b6719e619cf5fc",
    "Wood001": "3b8687ccd50252ff7ce5e2be5ecd0b684010708fda1a8941fd1473d8043159b4",
    "Wood021": "e4a7741c154314390a45acf946e3ca41c77137269c95ef520334243d7999fc82",
    "Wood035": "f9372bb1a6e854778c4c04f4a1d87374018ab300956f53c11011186e64ed4693",
    "WoodFloor043": "0bd1309b36c9a0add6fd9b4108f8cba73ba475afb6fb824c42bb05080c01403b",
}

# The maps to keep, as (suffix in the archive, destination file).
COLOUR_SUFFIX = "_Color.jpg"
NORMAL_SUFFIX = "_NormalGL.jpg"
ROUGHNESS_SUFFIX = "_Roughness.jpg"
AO_SUFFIX = "_AmbientOcclusion.jpg"


def sha256_of_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_of_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(asset_id, allow_repin):
    """Fetch the 1K archive, verifying the pinned sha256. Returns the archive bytes."""
    cache_path = os.path.join(CACHE_DIR, "%s_1K-JPG.zip" % asset_id)
    if os.path.exists(cache_path):
        data = open(cache_path, "rb").read()
    else:
        request = urllib.request.Request(DOWNLOAD % asset_id, headers={"User-Agent": USER_AGENT})
        data = urllib.request.urlopen(request, timeout=120).read()
        os.makedirs(CACHE_DIR, exist_ok=True)
        with open(cache_path, "wb") as handle:
            handle.write(data)

    actual = sha256_of_bytes(data)
    pinned = PINNED.get(asset_id)
    if pinned and actual != pinned:
        sys.exit("sha256 mismatch for %s: pinned %s, downloaded %s" % (asset_id, pinned, actual))
    if not pinned and allow_repin:
        print("  pin %-18s %s" % (asset_id, actual))
    return data


def read_map_from_zip(archive, suffix):
    with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
        for name in bundle.namelist():
            if name.lower().endswith(suffix.lower()):
                return Image.open(io.BytesIO(bundle.read(name))).convert("RGB")
    return None


def make_orm(roughness, metal, size):
    """Packs R = AO, G = roughness, B = metalness (glTF metallic-roughness layout)."""
    if roughness is None:
        roughness = Image.new("RGB", (size, size), (200, 200, 200))
    roughness = roughness.resize((size, size), Image.LANCZOS)
    grey = roughness.convert("L")
    channel = Image.new("L", (size, size), int(round(metal * 255.0)))
    white = Image.new("L", (size, size), 255)
    return Image.merge("RGB", (white, grey, channel))


def write_set(asset_id, metal, size, allow_repin, force):
    out_dir = os.path.join(ASSET_DIR, asset_id)
    marker = os.path.join(out_dir, ".complete")
    if os.path.exists(marker) and not force:
        print("  %-18s up to date" % asset_id)
        return False

    archive = download(asset_id, allow_repin)
    colour = read_map_from_zip(archive, COLOUR_SUFFIX)
    normal = read_map_from_zip(archive, NORMAL_SUFFIX)
    roughness = read_map_from_zip(archive, ROUGHNESS_SUFFIX)
    if colour is None or normal is None:
        sys.exit("%s: archive is missing Color or NormalGL" % asset_id)

    os.makedirs(out_dir, exist_ok=True)
    colour.resize((size, size), Image.LANCZOS).save(
        os.path.join(out_dir, "Color.jpg"), quality=92, optimize=True, progressive=False)
    normal.resize((size, size), Image.LANCZOS).save(
        os.path.join(out_dir, "Normal.png"), optimize=True)
    make_orm(roughness, metal, size).save(os.path.join(out_dir, "ORM.png"), optimize=True)
    with open(marker, "w") as handle:
        handle.write("%s %d\n" % (sha256_of_bytes(archive), size))
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resolution", type=int, default=None,
                        help="override the palette's default resolution (256/512/1024)")
    parser.add_argument("--force", action="store_true",
                        help="repack even when the marker file says the set is current")
    parser.add_argument("--pins", action="store_true",
                        help="record the upstream sha256 of every archive into PINNED output")
    args = parser.parse_args()

    palette = json.load(open(PALETTE, "r"))
    size = args.resolution or int(palette.get("resolution", 512))
    materials = palette["materials"]
    print("[ball-materials] %d material(s) at %dx%d -> %s" % (len(materials), size, size, ASSET_DIR))

    written = 0
    for entry in materials:
        asset_id = entry["id"]
        if entry.get("metal") is None:
            sys.exit("%s: palette entry has no metal value" % asset_id)
        if write_set(asset_id, float(entry["metal"]), size, args.pins, args.force):
            written += 1

    print("[ball-materials] wrote %d set(s)" % written)
    print("[ball-materials] publish with: scripts\\PushCloudTextures.ps1 -Prefix Textures\\Ball -Apply")


if __name__ == "__main__":
    main()
