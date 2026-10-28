#!/usr/bin/env python3
"""Emit Backyard_Ball.glb: a unit UV sphere used as the obstacle dropped into the
pool. Written directly as glTF 2.0 binary so the asset can be regenerated without
launching Blender.

The geometry is a unit-radius sphere at the origin with an identity transform:
the runtime picks the radius and the position from the scene values, so the asset
itself carries no placement.

Run: python scripts/models/build_ball.py
"""

import json
import math
import os
import struct

SEGMENTS = 48
RINGS = 32
OUT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "Assets", "Models", "Backyard", "Backyard_Ball.glb"))


def build_sphere():
    positions = [(0.0, 1.0, 0.0)]
    normals = [(0.0, 1.0, 0.0)]
    indices = []
    for ring in range(1, RINGS):
        phi = math.pi * ring / RINGS
        y = math.cos(phi)
        r = math.sin(phi)
        for segment in range(SEGMENTS):
            theta = 2.0 * math.pi * segment / SEGMENTS
            x = r * math.cos(theta)
            z = r * math.sin(theta)
            positions.append((x, y, z))
            normals.append((x, y, z))
    positions.append((0.0, -1.0, 0.0))
    normals.append((0.0, -1.0, 0.0))
    bottom = len(positions) - 1

    for segment in range(SEGMENTS):
        nxt = (segment + 1) % SEGMENTS
        indices.extend([0, 1 + nxt, 1 + segment])
    for ring in range(RINGS - 2):
        base = 1 + ring * SEGMENTS
        nbase = base + SEGMENTS
        for segment in range(SEGMENTS):
            nxt = (segment + 1) % SEGMENTS
            indices.extend([base + segment, base + nxt, nbase + nxt])
            indices.extend([base + segment, nbase + nxt, nbase + segment])
    last = 1 + (RINGS - 2) * SEGMENTS
    for segment in range(SEGMENTS):
        nxt = (segment + 1) % SEGMENTS
        indices.extend([last + segment, last + nxt, bottom])
    return positions, normals, indices


def pad4(data):
    return data + b"\x00" * ((-len(data)) % 4)


def main():
    positions, normals, indices = build_sphere()
    pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
    nrm_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
    idx_bytes = b"".join(struct.pack("<I", i) for i in indices)

    nrm_off = len(pos_bytes)
    idx_off = nrm_off + len(nrm_bytes)
    blob = pad4(pos_bytes + nrm_bytes + idx_bytes)

    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]

    gltf = {
        "asset": {"version": "2.0", "generator": "t850 build_ball.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Backyard_Ball", "mesh": 0}],
        "meshes": [{
            "name": "Backyard_Ball",
            "primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1},
                "indices": 2,
                "material": 0,
            }],
        }],
        "materials": [{
            "name": "BallRedRubber",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.85, 0.10, 0.12, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.35,
            },
        }],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pos_bytes), "target": 34962},
            {"buffer": 0, "byteOffset": nrm_off, "byteLength": len(nrm_bytes), "target": 34962},
            {"buffer": 0, "byteOffset": idx_off, "byteLength": len(idx_bytes), "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]},
            {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        ],
        "buffers": [{"byteLength": len(blob)}],
    }

    json_bytes = pad4(json.dumps(gltf, separators=(",", ":")).encode("utf-8"))
    total = 12 + 8 + len(json_bytes) + 8 + len(blob)
    out = bytearray()
    out += struct.pack("<4sII", b"glTF", 2, total)
    out += struct.pack("<I4s", len(json_bytes), b"JSON")
    out += json_bytes
    out += struct.pack("<I4s", len(blob), b"BIN\x00")
    out += blob

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as handle:
        handle.write(out)
    print("wrote %s (%d bytes, %d verts, %d tris)" % (OUT, len(out), len(positions), len(indices) // 3))


if __name__ == "__main__":
    main()
