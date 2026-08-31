#!/usr/bin/env python3
"""Verify Minecraft block faces against the repository's classic terrain atlas."""

from __future__ import annotations

import hashlib
import json
import struct
import sys
from pathlib import Path


EXPECTED_ATLAS_SHA256 = "e9783b0bf01869cfacbf0e762f2bba03eee9362d7a5465304532203a9313f0db"
EXPECTED_TILES: dict[str, list[tuple[int, int]]] = {
    "air": [(0, 0)] * 6,
    "grass": [(3, 0), (3, 0), (1, 0), (2, 0), (3, 0), (3, 0)],
    "dirt": [(2, 0)] * 6,
    "stone": [(0, 0)] * 6,
    "sand": [(2, 1)] * 6,
    "water": [(13, 12)] * 6,
    "log": [(4, 1), (4, 1), (5, 1), (5, 1), (4, 1), (4, 1)],
    "leaves": [(4, 3)] * 6,
    "planks": [(4, 0)] * 6,
    "bedrock": [(1, 1)] * 6,
    "cobblestone": [(0, 1)] * 6,
    "gravel": [(3, 1)] * 6,
    "coal_ore": [(2, 2)] * 6,
    "iron_ore": [(1, 2)] * 6,
    "gold_ore": [(0, 2)] * 6,
    "diamond_ore": [(2, 3)] * 6,
    "brick": [(7, 0)] * 6,
    "glass": [(1, 3)] * 6,
    "snow": [(2, 4)] * 6,
    "stone_bricks": [(6, 3)] * 6,
}
EXPECTED_OPAQUE = {"air": False, "water": False, "leaves": False, "glass": False}
FACE_NAMES = ("+X", "-X", "+Y", "-Y", "+Z", "-Z")


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def png_dimensions(data: bytes) -> tuple[int, int]:
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError("terrain.png is not a valid PNG with an IHDR header")
    return struct.unpack(">II", data[16:24])


def tile_pairs(values: object, block_name: str, errors: list[str]) -> list[tuple[int, int]]:
    if not isinstance(values, list) or len(values) != 12 or not all(isinstance(value, int) for value in values):
        fail(errors, f"{block_name}: tiles must contain 12 integer values")
        return []
    return [(values[index], values[index + 1]) for index in range(0, 12, 2)]


def main() -> int:
    source_root = Path(__file__).resolve().parents[1]
    scene_path = source_root / "Assets" / "Scenes" / "Minecraft.t8scene"
    atlas_path = source_root / "Assets" / "Textures" / "terrain.png"
    errors: list[str] = []

    scene = json.loads(scene_path.read_text(encoding="utf-8-sig"))
    voxel = scene.get("voxel_world")
    if not isinstance(voxel, dict):
        print("FAIL: Minecraft.t8scene has no voxel_world object", file=sys.stderr)
        return 1

    atlas_data = atlas_path.read_bytes()
    try:
        width, height = png_dimensions(atlas_data)
    except ValueError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    atlas_hash = hashlib.sha256(atlas_data).hexdigest()
    if atlas_hash != EXPECTED_ATLAS_SHA256:
        fail(errors, f"terrain.png SHA-256 changed: {atlas_hash}; re-audit every expected tile")
    if (width, height) != (256, 256):
        fail(errors, f"terrain.png dimensions are {width}x{height}, expected 256x256")
    if voxel.get("atlas_texture") != "terrain.png":
        fail(errors, f"atlas_texture is {voxel.get('atlas_texture')!r}, expected 'terrain.png'")
    if voxel.get("atlas_tile_px") != 16 or voxel.get("atlas_tiles_per_axis") != 16:
        fail(errors, "atlas grid must remain 16x16 pixels per tile and 16 tiles per axis")

    block_entries = voxel.get("blocks")
    if not isinstance(block_entries, list):
        fail(errors, "voxel_world.blocks must be an array")
        block_entries = []
    blocks = {
        block.get("name"): block
        for block in block_entries
        if isinstance(block, dict) and isinstance(block.get("name"), str)
    }

    missing = sorted(set(EXPECTED_TILES) - set(blocks))
    extra = sorted(set(blocks) - set(EXPECTED_TILES))
    if missing:
        fail(errors, f"missing block definitions: {', '.join(missing)}")
    if extra:
        fail(errors, f"unreviewed block definitions: {', '.join(extra)}")

    for name, expected in EXPECTED_TILES.items():
        block = blocks.get(name)
        if block is None:
            continue
        actual = tile_pairs(block.get("tiles"), name, errors)
        if actual and actual != expected:
            for face, expected_tile, actual_tile in zip(FACE_NAMES, expected, actual):
                if actual_tile != expected_tile:
                    fail(errors, f"{name} {face}: tile {actual_tile}, expected {expected_tile}")
        for face, tile in zip(FACE_NAMES, actual):
            if not (0 <= tile[0] < 16 and 0 <= tile[1] < 16):
                fail(errors, f"{name} {face}: out-of-bounds tile {tile}")

        expected_opaque = EXPECTED_OPAQUE.get(name, name != "air")
        if block.get("opaque") is not expected_opaque:
            fail(errors, f"{name}: opaque={block.get('opaque')!r}, expected {expected_opaque}")

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(f"PASS: terrain.png {width}x{height} sha256={atlas_hash}")
    print(f"PASS: {len(EXPECTED_TILES)} blocks, {len(EXPECTED_TILES) * 6} face mappings verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())