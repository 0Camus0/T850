#!/usr/bin/env python3
"""Emit Backyard_Ball.glb: a unit sphere used as the obstacle dropped into the
pool. Written directly as glTF 2.0 binary so the asset can be regenerated without
launching Blender.

The geometry is a unit-radius sphere at the origin with an identity transform:
the runtime picks the radius and the position from the scene values, so the asset
itself carries no placement.

The sphere is a spherified cube, not a UV sphere: the balls never rotate and the
camera looks down on them, so a UV sphere's pinched pole would sit exactly where
the viewer looks. Each cube face carries a full 0..1 UV square, and the four side
faces join seamlessly for tiling textures.

The material declares base colour, normal and metallic-roughness maps (1x1
placeholders that reproduce the old red rubber look). Declaring them is what makes
the engine build the textured shader permutation and generate MikkTSpace tangents;
BackyardScene then swaps in a palette set per ball (W-25), which is why every
factor is 1.0.

Run: python scripts/models/build_ball.py
"""

import json
import math
import os
import struct
import zlib

SUBDIVISIONS = 16   # quads along each cube-face edge: 64 segments around the equator
OUT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "Assets", "Models", "Backyard", "Backyard_Ball.glb"))

# (outward normal, u axis, v-up axis) with u x up == normal, so quads wind outward.
# Side faces walk around +Y in the same direction: each face's u=1 edge is the next
# face's u=0 edge, so a tiling texture is continuous around the equator.
FACES = (
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
    ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
    ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
    ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
)

# Placeholders, RGBA8. Base colour is stored sRGB (the shader decodes with 2.2):
# linear (0.85, 0.10, 0.12) -> (237, 89, 97). ORM: R = AO 1, G = roughness 0.35, B = metal 0.
PLACEHOLDER_COLOR = (237, 89, 97, 255)
PLACEHOLDER_NORMAL = (128, 128, 255, 255)
PLACEHOLDER_ORM = (255, 89, 0, 255)


def spherify(x, y, z):
    # Area-balancing cube-to-sphere map: texel density varies far less than a plain
    # normalize, so a face's texture is not stretched toward the cube corners.
    x2, y2, z2 = x * x, y * y, z * z
    return (x * math.sqrt(1.0 - y2 / 2.0 - z2 / 2.0 + y2 * z2 / 3.0),
            y * math.sqrt(1.0 - z2 / 2.0 - x2 / 2.0 + z2 * x2 / 3.0),
            z * math.sqrt(1.0 - x2 / 2.0 - y2 / 2.0 + x2 * y2 / 3.0))


def build_sphere():
    positions, uvs, indices = [], [], []
    n = SUBDIVISIONS
    for normal, right, up in FACES:
        base = len(positions)
        for j in range(n + 1):
            b = -1.0 + 2.0 * j / n
            for i in range(n + 1):
                a = -1.0 + 2.0 * i / n
                cube = tuple(normal[k] + a * right[k] + b * up[k] for k in range(3))
                positions.append(spherify(*cube))
                # glTF V runs down the image, so the face's "up" edge is v = 0.
                uvs.append(((a + 1.0) * 0.5, (1.0 - b) * 0.5))
        for j in range(n):
            for i in range(n):
                v00 = base + j * (n + 1) + i
                v10 = v00 + 1
                v01 = v00 + (n + 1)
                v11 = v01 + 1
                indices.extend([v00, v10, v11, v00, v11, v01])

    for t in range(0, len(indices), 3):
        p0, p1, p2 = (positions[indices[t + k]] for k in range(3))
        e1 = [p1[k] - p0[k] for k in range(3)]
        e2 = [p2[k] - p0[k] for k in range(3)]
        cross = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if sum(cross[k] * p0[k] for k in range(3)) <= 0.0:
            raise SystemExit("triangle %d winds inward; check FACES" % (t // 3))

    normals = list(positions)   # unit sphere: the normal is the position
    return positions, normals, uvs, indices


def png_rgba_1x1(rgba):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(b"\x00" + bytes(rgba))) + chunk(b"IEND", b""))


def pad4(data, fill=b"\x00"):
    return data + fill * ((-len(data)) % 4)


def main():
    positions, normals, uvs, indices = build_sphere()
    chunks = [
        (b"".join(struct.pack("<3f", *p) for p in positions), 34962),
        (b"".join(struct.pack("<3f", *v) for v in normals), 34962),
        (b"".join(struct.pack("<2f", *uv) for uv in uvs), 34962),
        (b"".join(struct.pack("<I", i) for i in indices), 34963),
        (png_rgba_1x1(PLACEHOLDER_COLOR), None),
        (png_rgba_1x1(PLACEHOLDER_NORMAL), None),
        (png_rgba_1x1(PLACEHOLDER_ORM), None),
    ]
    blob = b""
    buffer_views = []
    for data, target in chunks:
        view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        buffer_views.append(view)
        blob += pad4(data)

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
                "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                "indices": 3,
                "material": 0,
            }],
        }],
        "materials": [{
            "name": "BallPalette",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "metallicRoughnessTexture": {"index": 2},
                "metallicFactor": 1.0,
                "roughnessFactor": 1.0,
            },
            "normalTexture": {"index": 1},
        }],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": [
            {"name": "BallColor", "bufferView": 4, "mimeType": "image/png"},
            {"name": "BallNormal", "bufferView": 5, "mimeType": "image/png"},
            {"name": "BallORM", "bufferView": 6, "mimeType": "image/png"},
        ],
        "textures": [{"sampler": 0, "source": 0}, {"sampler": 0, "source": 1}, {"sampler": 0, "source": 2}],
        "bufferViews": buffer_views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]},
            {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        ],
        "buffers": [{"byteLength": len(blob)}],
    }

    json_bytes = pad4(json.dumps(gltf, separators=(",", ":")).encode("utf-8"), b" ")
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
