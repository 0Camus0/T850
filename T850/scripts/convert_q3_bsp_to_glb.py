#!/usr/bin/env python3
"""Convert a Quake 3 BSP map to GLB with RTX Remix PBR texture candidates.

Run in two phases:
  1. Normal Python prepares a manifest and PNG texture cache.
  2. Blender Python imports BSP geometry from the manifest and exports GLB.
"""

from __future__ import annotations

import argparse
import io
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path

try:
    import bpy  # type: ignore
except ImportError:
    bpy = None

try:
    from PIL import Image
    import numpy as np
except ImportError:
    Image = None
    np = None


Q3_BSP_LUMP_TEXTURES = 1
Q3_BSP_LUMP_VERTICES = 10
Q3_BSP_LUMP_MESHVERTS = 11
Q3_BSP_LUMP_FACES = 13
Q3_BSP_LUMP_LIGHTMAPS = 14

Q3_LIGHTMAP_SIZE = 128
Q3_LIGHTMAP_GUTTER = 2

FACE_POLYGON = 1
FACE_PATCH = 2
FACE_MESH = 3

IMAGE_EXTENSIONS = (".tga", ".jpg", ".jpeg", ".png")
SKIP_SHADER_PREFIXES = (
    "textures/common/caulk",
    "textures/common/clip",
    "textures/common/hint",
    "textures/common/nodraw",
    "textures/common/origin",
    "textures/common/trigger",
    "noshader",
    "flareshader",
)


@dataclass
class ArchiveFile:
    archive: Path
    name: str


@dataclass
class Vertex:
    position: tuple[float, float, float]
    texcoord: tuple[float, float]
    lightmap_texcoord: tuple[float, float]
    normal: tuple[float, float, float]
    color: tuple[int, int, int, int]


def sanitize_name(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value)
    return value.strip("_") or "material"


def archive_roots(q3_root: Path, rtx_root: Path | None) -> list[Path]:
    roots: list[Path] = [q3_root if q3_root.name.lower() == "baseq3" else q3_root / "baseq3"]
    if rtx_root is not None:
        roots.append(rtx_root if rtx_root.name.lower() == "baseq3" else rtx_root / "baseq3")
    return [root for root in roots if root.exists()]


def list_pk3_files(roots: list[Path]) -> list[Path]:
    pk3_files: list[Path] = []
    for root in roots:
        pk3_files.extend(sorted(root.glob("*.pk3")))
    return pk3_files


def build_archive_index(pk3_files: list[Path]) -> dict[str, ArchiveFile]:
    result: dict[str, ArchiveFile] = {}
    for pk3_file in pk3_files:
        with zipfile.ZipFile(pk3_file) as archive:
            for entry_name in archive.namelist():
                normalized = entry_name.replace("\\", "/").lower()
                result[normalized] = ArchiveFile(pk3_file, entry_name)
    return result


def read_archive_file(index: dict[str, ArchiveFile], path: str) -> bytes:
    archive_file = index[path.replace("\\", "/").lower()]
    with zipfile.ZipFile(archive_file.archive) as archive:
        return archive.read(archive_file.name)


def find_indexed_image(index: dict[str, ArchiveFile], shader_name: str) -> str | None:
    normalized = shader_name.replace("\\", "/").lower()
    if normalized.endswith(IMAGE_EXTENSIONS) and normalized in index:
        return normalized
    stem = normalized.rsplit(".", 1)[0] if normalized.endswith(IMAGE_EXTENSIONS) else normalized
    for extension in IMAGE_EXTENSIONS:
        candidate = stem + extension
        if candidate in index:
            return candidate
    return None


def parse_shader_scripts(index: dict[str, ArchiveFile]) -> dict[str, dict[str, object]]:
    shader_defs: dict[str, dict[str, object]] = {}
    script_paths = sorted(path for path in index if path.startswith("scripts/") and path.endswith(".shader"))
    for script_path in script_paths:
        try:
            text = read_archive_file(index, script_path).decode("latin-1", errors="ignore")
        except Exception:
            continue

        pending_name = ""
        current_name = ""
        block_lines: list[str] = []
        depth = 0
        for raw_line in text.splitlines():
            line = raw_line.split("//", 1)[0].strip()
            if not line:
                continue
            if depth == 0:
                if line == "{":
                    current_name = pending_name.lower()
                    block_lines = []
                    depth = 1
                else:
                    pending_name = line
                continue
            depth += line.count("{")
            depth -= line.count("}")
            if depth > 0:
                block_lines.append(line)
            elif current_name:
                shader_defs[current_name] = parse_shader_block(block_lines)
                current_name = ""
    return shader_defs


def parse_shader_block(lines: list[str]) -> dict[str, object]:
    editor_image = ""
    selected_map = ""
    selected_wrap = "repeat"
    selected_uv_scale = (1.0, 1.0)
    alpha_mode = "OPAQUE"
    alpha_cutoff = 0.5
    double_sided = False
    surfaceparms: list[str] = []
    q3_stages: list[dict[str, object]] = []

    def parse_float(value: str, default: float) -> float:
        try:
            return float(value)
        except ValueError:
            return default

    def parse_blend(tokens: list[str]) -> str:
        if len(tokens) < 2:
            return "custom"
        if len(tokens) == 2:
            shortcut = tokens[1].lower()
            if shortcut in ("add", "filter", "blend"):
                return shortcut
            return shortcut
        src = tokens[1].lower()
        dst = tokens[2].lower()
        if src in ("gl_one", "one") and dst in ("gl_one", "one"):
            return "add"
        if src in ("gl_dst_color", "dst_color") and dst in ("gl_zero", "zero"):
            return "filter"
        if src in ("gl_src_alpha", "src_alpha") and dst in ("gl_one_minus_src_alpha", "one_minus_src_alpha"):
            return "blend"
        return f"{src} {dst}"

    def parse_alpha_func(tokens: list[str]) -> tuple[str, float]:
        if len(tokens) < 2:
            return "MASK", 0.5
        mode = tokens[1].upper()
        if mode == "GT0":
            return "MASK", 0.01
        if mode in ("GE128", "LT128"):
            return "MASK", 0.5
        return "MASK", 0.5

    def stage_desc(stage_lines: list[str]) -> dict[str, object] | None:
        stage_map = ""
        stage_wrap = "repeat"
        uv_scale = [1.0, 1.0]
        stage: dict[str, object] = {"tcMods": []}
        for stage_line in stage_lines:
            tokens = stage_line.replace("(", " ").replace(")", " ").split()
            if not tokens:
                continue
            keyword = tokens[0].lower()
            if keyword in ("map", "clampmap") and len(tokens) >= 2 and not stage_map:
                candidate = tokens[1]
                if not candidate.startswith("$") and not candidate.startswith("*"):
                    stage_map = candidate
                    stage_wrap = "clamp" if keyword == "clampmap" else "repeat"
                    stage["map"] = stage_map
                    stage["wrap"] = stage_wrap
            elif keyword == "animmap" and len(tokens) >= 3 and not stage_map:
                stage["animFps"] = parse_float(tokens[1], 0.0)
                stage["animFrames"] = [candidate for candidate in tokens[2:] if not candidate.startswith("$") and not candidate.startswith("*")]
                for candidate in tokens[2:]:
                    if not candidate.startswith("$") and not candidate.startswith("*"):
                        stage_map = candidate
                        stage["map"] = stage_map
                        stage["wrap"] = stage_wrap
                        break
            elif keyword == "blendfunc":
                stage["blendFunc"] = tokens[1:]
                stage["blendMode"] = parse_blend(tokens)
            elif keyword == "alphafunc":
                stage["alphaFunc"] = tokens[1].upper() if len(tokens) > 1 else ""
            elif keyword in ("rgbgen", "alphagen", "tcgen") and len(tokens) >= 2:
                stage[keyword] = tokens[1:]
            elif keyword == "tcmod" and len(tokens) >= 2:
                tcmod = {"op": tokens[1].lower(), "args": tokens[2:]}
                tcmods = stage.get("tcMods")
                if isinstance(tcmods, list):
                    tcmods.append(tcmod)
                if len(tokens) >= 4 and tokens[1].lower() == "scale":
                    try:
                        uv_scale[0] *= float(tokens[2])
                        uv_scale[1] *= float(tokens[3])
                    except ValueError:
                        pass
        if not stage_map:
            return None
        stage["uvScale"] = [uv_scale[0], uv_scale[1]]
        return {"map": stage_map, "wrap": stage_wrap, "uvScale": tuple(uv_scale), "stage": stage}

    current_stage: list[str] | None = None
    for line in lines:
        tokens = line.replace("(", " ").replace(")", " ").split()
        if not tokens:
            continue
        if tokens[0] == "{":
            current_stage = []
            continue
        if tokens[0] == "}":
            # Track whether this stage is the base (first-selected) stage.
            # In Q3, stage 1 is the opaque base; later stages add glow/effects
            # with blendFunc on top.  alphaMode = BLEND only when the base stage
            # itself blends — not when a later additive overlay stage does so.
            is_base_stage = current_stage is not None and not selected_map
            if current_stage is not None and not selected_map:
                desc = stage_desc(current_stage)
                if desc is not None:
                    selected_map = str(desc["map"])
                    selected_wrap = str(desc["wrap"])
                    selected_uv_scale = desc["uvScale"]  # type: ignore[assignment]
            if current_stage is not None:
                desc = stage_desc(current_stage)
                if desc is not None:
                    stage = desc.get("stage", {})
                    if isinstance(stage, dict):
                        q3_stages.append(stage)
                    stage_alpha = None
                    for stage_line in current_stage:
                        stage_tokens = stage_line.replace("(", " ").replace(")", " ").split()
                        if not stage_tokens:
                            continue
                        stage_keyword = stage_tokens[0].lower()
                        if stage_keyword == "alphafunc":
                            stage_alpha = parse_alpha_func(stage_tokens)
                        elif stage_keyword == "blendfunc" and alpha_mode != "MASK" and is_base_stage:
                            # Only the base stage's blendFunc determines the material's
                            # overall alpha mode.  Overlay stages blending on top of an
                            # opaque base do not make the whole material transparent.
                            # GL_DST_COLOR/GL_ZERO is multiplicative (modifies dest colour)
                            # — the surface is still opaque, so don't flag it as BLEND.
                            bf = [t.lower() for t in stage_tokens[1:]]
                            is_multiply = bf in (
                                ["gl_dst_color", "gl_zero"],
                                ["gl_zero", "gl_src_color"],
                            )
                            if not is_multiply:
                                alpha_mode = "BLEND"
                    if stage_alpha is not None:
                        alpha_mode, alpha_cutoff = stage_alpha
            current_stage = None
            continue
        if current_stage is not None:
            current_stage.append(line)
            continue
        keyword = tokens[0].lower()
        if keyword == "qer_editorimage" and len(tokens) >= 2 and not editor_image:
            editor_image = tokens[1]
        elif keyword == "surfaceparm" and len(tokens) >= 2:
            surfaceparms.append(tokens[1].lower())
        elif keyword == "cull" and len(tokens) >= 2:
            cull_value = tokens[1].lower()
            if cull_value in ("disable", "none", "twosided", "two-sided"):
                double_sided = True
    q3_shader: dict[str, object] = {}
    if surfaceparms:
        q3_shader["surfaceparms"] = surfaceparms
    if q3_stages:
        q3_shader["stages"] = q3_stages
    return {
        "editorImage": editor_image,
        "map": selected_map,
        "wrap": selected_wrap,
        "uvScale": selected_uv_scale,
        "alphaMode": alpha_mode,
        "alphaCutoff": alpha_cutoff,
        "doubleSided": double_sided,
        "q3Shader": q3_shader,
    }


def resolve_shader_image(index: dict[str, ArchiveFile], shader_defs: dict[str, dict[str, object]], shader_name: str) -> str | None:
    normalized = shader_name.lower()
    shader_def = shader_defs.get(normalized, {})
    for key in ("map", "editorImage"):
        value = str(shader_def.get(key, ""))
        if value:
            image_path = find_indexed_image(index, value)
            if image_path:
                return image_path
    return find_indexed_image(index, shader_name)


def decode_bsp_textures(bsp_data: bytes) -> list[str]:
    lumps = [struct.unpack_from("<II", bsp_data, 8 + lump_index * 8) for lump_index in range(17)]
    texture_offset, texture_length = lumps[Q3_BSP_LUMP_TEXTURES]
    textures: list[str] = []
    for texture_index in range(texture_length // 72):
        offset = texture_offset + texture_index * 72
        raw_name = bsp_data[offset:offset + 64]
        name = raw_name.split(b"\0", 1)[0].decode("ascii", errors="replace")
        textures.append(name)
    return textures


def image_feature(image: "Image.Image") -> "np.ndarray":
    small = image.convert("RGB").resize((32, 32), Image.Resampling.LANCZOS)
    return np.asarray(small, dtype=np.float32).reshape(-1) / 255.0


def write_png(image: "Image.Image", output_path: Path) -> None:
    if output_path.exists():
        return
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path, "PNG")


def cached_source_output(texture_dir: Path, source_image_path: str) -> Path:
    source_stem = sanitize_name(source_image_path.rsplit(".", 1)[0])
    return texture_dir / "_q3" / source_stem / "base.png"


def cached_rtx_output(texture_dir: Path, material_hash: str, channel: str) -> Path:
    return texture_dir / "_rtx" / material_hash / f"{channel}.png"


def cached_rtx_sprite_frame_output(texture_dir: Path, material_hash: str, channel: str, frame_index: int) -> Path:
    return texture_dir / "_rtx" / material_hash / f"{channel}_frame_{frame_index:02d}.png"


def cached_metallic_roughness_output(texture_dir: Path, material_hash: str) -> Path:
    return texture_dir / "_rtx" / material_hash / "metallicRoughness.png"


def cached_lightmap_atlas_output(texture_dir: Path, map_name: str) -> Path:
    return texture_dir / "_q3_lightmaps" / sanitize_name(map_name) / "lightmap_atlas.png"


def write_lightmap_gutter(atlas: "Image.Image", tile: "Image.Image", x: int, y: int, gutter: int) -> None:
    if gutter <= 0:
        return
    size = Q3_LIGHTMAP_SIZE
    atlas.paste(tile.crop((0, 0, size, 1)).resize((size, gutter)), (x, y - gutter))
    atlas.paste(tile.crop((0, size - 1, size, size)).resize((size, gutter)), (x, y + size))
    atlas.paste(tile.crop((0, 0, 1, size)).resize((gutter, size)), (x - gutter, y))
    atlas.paste(tile.crop((size - 1, 0, size, size)).resize((gutter, size)), (x + size, y))
    atlas.paste(tile.crop((0, 0, 1, 1)).resize((gutter, gutter)), (x - gutter, y - gutter))
    atlas.paste(tile.crop((size - 1, 0, size, 1)).resize((gutter, gutter)), (x + size, y - gutter))
    atlas.paste(tile.crop((0, size - 1, 1, size)).resize((gutter, gutter)), (x - gutter, y + size))
    atlas.paste(tile.crop((size - 1, size - 1, size, size)).resize((gutter, gutter)), (x + size, y + size))


def write_lightmap_atlas(bsp_data: bytes, output_path: Path) -> dict[str, object] | None:
    if Image is None:
        raise RuntimeError("Pillow is required to export BSP lightmaps")
    lumps = [struct.unpack_from("<II", bsp_data, 8 + lump_index * 8) for lump_index in range(17)]
    lightmap_offset, lightmap_length = lumps[Q3_BSP_LUMP_LIGHTMAPS]
    lightmap_bytes = Q3_LIGHTMAP_SIZE * Q3_LIGHTMAP_SIZE * 3
    lightmap_count = lightmap_length // lightmap_bytes
    if lightmap_count <= 0:
        return None

    tile_count = lightmap_count + 1
    columns = math.ceil(math.sqrt(tile_count))
    rows = math.ceil(tile_count / columns)
    gutter = Q3_LIGHTMAP_GUTTER
    pitch = Q3_LIGHTMAP_SIZE + gutter * 2
    atlas = Image.new("RGB", (columns * pitch, rows * pitch), (255, 255, 255))
    for lightmap_index in range(lightmap_count):
        start = lightmap_offset + lightmap_index * lightmap_bytes
        tile = Image.frombytes("RGB", (Q3_LIGHTMAP_SIZE, Q3_LIGHTMAP_SIZE), bsp_data[start:start + lightmap_bytes])
        tile_x = (lightmap_index % columns) * pitch + gutter
        tile_y = (lightmap_index // columns) * pitch + gutter
        atlas.paste(tile, (tile_x, tile_y))
        write_lightmap_gutter(atlas, tile, tile_x, tile_y, gutter)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(output_path, "PNG")
    return {
        "texture": str(output_path),
        "extension": "MOZ_lightmap",
        "texCoord": 1,
        "intensity": 1.0,
        "count": lightmap_count,
        "tileSize": Q3_LIGHTMAP_SIZE,
        "gutter": gutter,
        "atlasColumns": columns,
        "atlasRows": rows,
        "atlasPitch": pitch,
        "whiteTile": lightmap_count,
    }


def collect_rtx_sets(rtx_mod_root: Path) -> dict[str, dict[str, Path]]:
    mods_root = rtx_mod_root / "rtx-remix" / "mods"
    ingested_dirs = [path for path in mods_root.glob("*/assets/ingested") if path.exists()]
    groups: dict[str, dict[str, Path]] = {}
    if not ingested_dirs:
        return groups
    channel_patterns = {
        "albedo": ("albedo", "diffuse"),
        "normal": ("normal",),
        "roughness": ("roughness",),
        "metallic": ("metallic",),
        "height": ("height",),
        "emissive": ("emissive", "emission"),
    }

    def add_texture(material_hash: str, texture_file: Path) -> None:
        lower_name = texture_file.name.lower()
        for channel, tokens in channel_patterns.items():
            if any(token in lower_name for token in tokens):
                groups.setdefault(material_hash.upper(), {}).setdefault(channel, texture_file)

    for ingested in ingested_dirs:
        for texture_file in ingested.rglob("*.dds"):
            match = re.match(r"^([0-9A-F]{15,16})(?=[_.-])", texture_file.name, re.IGNORECASE)
            if not match:
                continue
            add_texture(match.group(1), texture_file)

    # RTX Remix material definitions are authoritative. Some mods bind a
    # mat_<runtime hash> to texture files whose filename prefix is different
    # or truncated, so filename-only grouping misses valid material assets.
    for mod_file in mods_root.glob("*/mod.usda"):
        try:
            text = mod_file.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        material_positions = [
            (match.start(), match.group(1).upper())
            for match in re.finditer(r'over\s+"mat_([0-9A-Fa-f]{15,16})"', text)
        ]
        for index, (start, material_hash) in enumerate(material_positions):
            end = material_positions[index + 1][0] if index + 1 < len(material_positions) else len(text)
            block = text[start:end]
            for asset_ref in re.findall(r"@\./assets/ingested/([^@]+\.dds)@", block, re.IGNORECASE):
                texture_file = mod_file.parent / "assets" / "ingested" / Path(*asset_ref.split("/"))
                if texture_file.exists():
                    add_texture(material_hash, texture_file)
    return groups


def collect_rtx_sprite_sheets(rtx_mod_root: Path) -> dict[str, dict[str, int]]:
    mods_root = rtx_mod_root / "rtx-remix" / "mods"
    sprites: dict[str, dict[str, int]] = {}
    for mod_file in mods_root.glob("*/mod.usda"):
        try:
            text = mod_file.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        material_positions = [
            (match.start(), match.group(1).upper())
            for match in re.finditer(r'over\s+"mat_([0-9A-Fa-f]{15,16})"', text)
        ]
        for index, (start, material_hash) in enumerate(material_positions):
            end = material_positions[index + 1][0] if index + 1 < len(material_positions) else len(text)
            block = text[start:end]
            cols_match = re.search(r"custom\s+int\s+inputs:sprite_sheet_cols\s*=\s*(\d+)", block)
            rows_match = re.search(r"custom\s+int\s+inputs:sprite_sheet_rows\s*=\s*(\d+)", block)
            if not cols_match or not rows_match:
                continue
            fps_match = re.search(r"custom\s+int\s+inputs:sprite_sheet_fps\s*=\s*(\d+)", block)
            cols = int(cols_match.group(1))
            rows = int(rows_match.group(1))
            if cols <= 0 or rows <= 0:
                continue
            sprites[material_hash] = {
                "cols": cols,
                "rows": rows,
                "fps": int(fps_match.group(1)) if fps_match else 0,
            }
    return sprites


def normalized_vector(values: "np.ndarray") -> "np.ndarray":
    vector = np.asarray(values, dtype=np.float32).reshape(-1)
    return (vector - vector.mean()) / (vector.std() + 1.0e-5)


def structural_feature(image: "Image.Image") -> tuple[float, "np.ndarray", "np.ndarray", "np.ndarray"]:
    rgb = np.asarray(image.convert("RGB").resize((64, 64), Image.Resampling.LANCZOS), dtype=np.float32) / 255.0
    luminance = rgb[:, :, 0] * 0.2126 + rgb[:, :, 1] * 0.7152 + rgb[:, :, 2] * 0.0722
    dx = np.zeros_like(luminance)
    dy = np.zeros_like(luminance)
    dx[:, 1:-1] = luminance[:, 2:] - luminance[:, :-2]
    dy[1:-1, :] = luminance[2:, :] - luminance[:-2, :]
    gradient = np.sqrt(dx * dx + dy * dy)
    return (
        image.width / max(image.height, 1),
        normalized_vector(luminance),
        normalized_vector(gradient),
        rgb.reshape(-1).astype(np.float32),
    )


def rtx_feature_signature(albedo_files: list[tuple[str, Path]]) -> str:
    parts: list[str] = []
    for material_hash, path in albedo_files:
        stat = path.stat()
        parts.append(f"{material_hash}:{path.name}:{stat.st_size}:{stat.st_mtime_ns}")
    return "\n".join(parts)


def load_rtx_features(rtx_sets: dict[str, dict[str, Path]], cache_path: Path) -> dict[str, object]:
    albedo_files = [(material_hash, channels["albedo"]) for material_hash, channels in sorted(rtx_sets.items()) if "albedo" in channels]
    signature = rtx_feature_signature(albedo_files)
    if cache_path.exists():
        try:
            cached = np.load(cache_path, allow_pickle=False)
            if str(cached["signature"][0]) == signature:
                return {
                    "hashes": [str(value) for value in cached["hashes"]],
                    "aspects": cached["aspects"].astype(np.float32, copy=False),
                    "luminance": cached["luminance"].astype(np.float32, copy=False),
                    "gradient": cached["gradient"].astype(np.float32, copy=False),
                    "rgb": cached["rgb"].astype(np.float32, copy=False),
                }
        except Exception:
            pass

    hashes: list[str] = []
    aspects: list[float] = []
    luminance_features: list[np.ndarray] = []
    gradient_features: list[np.ndarray] = []
    rgb_features: list[np.ndarray] = []
    for material_hash, albedo in albedo_files:
        try:
            image = Image.open(albedo)
            image.load()
            aspect, luminance, gradient, rgb = structural_feature(image)
            hashes.append(material_hash)
            aspects.append(aspect)
            luminance_features.append(luminance)
            gradient_features.append(gradient)
            rgb_features.append(rgb)
        except Exception:
            continue
    aspect_array = np.asarray(aspects, dtype=np.float32)
    luminance_matrix = np.vstack(luminance_features).astype(np.float32) if luminance_features else np.empty((0, 64 * 64), dtype=np.float32)
    gradient_matrix = np.vstack(gradient_features).astype(np.float32) if gradient_features else np.empty((0, 64 * 64), dtype=np.float32)
    rgb_matrix = np.vstack(rgb_features).astype(np.float32) if rgb_features else np.empty((0, 64 * 64 * 3), dtype=np.float32)
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        cache_path,
        signature=np.array([signature]),
        hashes=np.array(hashes),
        aspects=aspect_array,
        luminance=luminance_matrix,
        gradient=gradient_matrix,
        rgb=rgb_matrix,
    )
    return {"hashes": hashes, "aspects": aspect_array, "luminance": luminance_matrix, "gradient": gradient_matrix, "rgb": rgb_matrix}


def choose_rtx_set(source_image: "Image.Image", rtx_features: dict[str, object]) -> tuple[str, float, str, float, float] | None:
    hashes = rtx_features["hashes"]
    aspects = rtx_features["aspects"]
    luminance = rtx_features["luminance"]
    gradient = rtx_features["gradient"]
    rgb = rtx_features["rgb"]
    if not isinstance(hashes, list) or not hashes or not isinstance(luminance, np.ndarray) or luminance.size == 0:
        return None
    source_aspect, source_luminance, source_gradient, source_rgb = structural_feature(source_image)
    aspect_delta = np.abs(np.log(np.maximum(aspects, 1.0e-6) / max(source_aspect, 1.0e-6)))
    luminance_corr = np.mean(luminance * source_luminance, axis=1)
    gradient_corr = np.mean(gradient * source_gradient, axis=1)
    rgb_error = np.mean((rgb - source_rgb) ** 2, axis=1)
    scores = 0.60 * luminance_corr + 0.35 * gradient_corr - 0.30 * rgb_error - 0.10 * aspect_delta
    scores = np.where(aspect_delta <= 0.35, scores, -999.0)
    best_index = int(np.argmax(scores))
    if len(scores) > 1:
        second_index = int(np.argpartition(scores, -2)[-2])
    else:
        second_index = best_index
    best_hash = hashes[best_index]
    best_score = float(scores[best_index])
    second_hash = hashes[second_index]
    second_score = float(scores[second_index])
    gap = best_score - second_score
    return best_hash, best_score, second_hash, second_score, gap


def load_runtime_capture_features(captures_root: Path, map_name: str) -> dict[str, object]:
    candidate_dirs = [
        captures_root / map_name / "capture" / "textures",
        captures_root / map_name / "textures",
    ]
    if captures_root.exists():
        run_dirs = [path for path in captures_root.iterdir() if path.is_dir()]
        run_dirs.sort(key=lambda path: path.stat().st_mtime, reverse=True)
        for run_dir in run_dirs:
            candidate_dirs.extend([
                run_dir / map_name / "capture" / "textures",
                run_dir / map_name / "textures",
            ])
    texture_dirs: list[Path] = []
    seen_dirs: set[Path] = set()
    for candidate_dir in candidate_dirs:
        if candidate_dir.exists() and candidate_dir not in seen_dirs:
            texture_dirs.append(candidate_dir)
            seen_dirs.add(candidate_dir)
    if not texture_dirs:
        return {"hashes": [], "paths": {}, "aspects": np.empty((0,), dtype=np.float32), "luminance": np.empty((0, 64 * 64), dtype=np.float32), "gradient": np.empty((0, 64 * 64), dtype=np.float32), "rgb": np.empty((0, 64 * 64 * 3), dtype=np.float32)}

    hashes: list[str] = []
    paths: dict[str, Path] = {}
    aspects: list[float] = []
    luminance_features: list[np.ndarray] = []
    gradient_features: list[np.ndarray] = []
    rgb_features: list[np.ndarray] = []
    for texture_dir in texture_dirs:
        for texture_file in sorted(texture_dir.glob("*.dds")):
            if not re.fullmatch(r"[0-9A-Fa-f]{16}", texture_file.stem):
                continue
            material_hash = texture_file.stem.upper()
            if material_hash in paths:
                continue
            try:
                image = Image.open(texture_file)
                image.load()
                aspect, luminance, gradient, rgb = structural_feature(image)
            except Exception:
                continue
            hashes.append(material_hash)
            paths[material_hash] = texture_file
            aspects.append(aspect)
            luminance_features.append(luminance)
            gradient_features.append(gradient)
            rgb_features.append(rgb)

    return {
        "hashes": hashes,
        "paths": paths,
        "aspects": np.asarray(aspects, dtype=np.float32),
        "luminance": np.vstack(luminance_features).astype(np.float32) if luminance_features else np.empty((0, 64 * 64), dtype=np.float32),
        "gradient": np.vstack(gradient_features).astype(np.float32) if gradient_features else np.empty((0, 64 * 64), dtype=np.float32),
        "rgb": np.vstack(rgb_features).astype(np.float32) if rgb_features else np.empty((0, 64 * 64 * 3), dtype=np.float32),
    }


def convert_rtx_channel(path: Path, output_path: Path) -> None:
    if output_path.exists():
        return
    try:
        image = Image.open(path)
        image.load()
        write_png(image.convert("RGBA"), output_path)
        return
    except NotImplementedError:
        pass

    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise RuntimeError(f"Could not decode {path}; Pillow lacks this DDS format and ffmpeg is not available")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(path), str(output_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"ffmpeg failed to decode {path}: {result.stderr.strip()}")


def crop_sprite_sheet_frame(source_path: Path, output_path: Path, cols: int, rows: int, frame_index: int = 0) -> None:
    if output_path.exists():
        return
    if cols <= 0 or rows <= 0:
        raise ValueError(f"Invalid sprite sheet grid {cols}x{rows} for {source_path}")
    image = Image.open(source_path)
    image.load()
    width, height = image.size
    original_cols, original_rows = cols, rows
    if width % cols != 0 and rows == 1 and height > 0 and width % height == 0:
        cols = max(1, width // height)
    if height % rows != 0 and cols == 1 and width > 0 and height % width == 0:
        rows = max(1, height // width)
    if width % cols != 0:
        cols = 1
    if height % rows != 0:
        rows = 1
    if (cols, rows) != (original_cols, original_rows):
        print(f"  [sprite sheet] adjusted invalid grid for {source_path.name}: "
              f"{original_cols}x{original_rows} -> {cols}x{rows} ({width}x{height})")
    frame_count = cols * rows
    frame_index = max(0, min(frame_index, frame_count - 1))
    frame_width = width // cols
    frame_height = height // rows
    frame_col = frame_index % cols
    frame_row = frame_index // cols
    left = frame_col * frame_width
    top = frame_row * frame_height
    frame = image.crop((left, top, left + frame_width, top + frame_height))
    write_png(frame.convert("RGBA"), output_path)


def cached_runtime_output(texture_dir: Path, material_hash: str) -> Path:
    return texture_dir / f"{material_hash.lower()}_runtime_basecolor.png"


def classify_blend_type(q3_shader: dict) -> str:
    """Classify the dominant blend type from Q3 shader stages.

    Returns one of: "additive", "multiply", "alpha", "opaque".
    "additive"  = GL_ONE/GL_ONE or 'add'  — dark pixels vanish, bright pixels show.
    "multiply"  = GL_DST_COLOR/GL_ZERO only — multiplies dest; render OPAQUE in glTF.
    "alpha"     = GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA or 'blend' shorthand.
    """
    stages = q3_shader.get("stages", [])
    has_additive = False
    has_multiply = False
    has_alpha = False
    for stage in stages:
        bf = [t.lower() for t in stage.get("blendFunc", [])]
        if not bf:
            continue
        if bf == ["add"] or bf == ["gl_one", "gl_one"]:
            has_additive = True
        elif bf in (["gl_dst_color", "gl_zero"], ["gl_zero", "gl_dst_color"]):
            has_multiply = True
        elif bf == ["blend"] or bf == ["gl_src_alpha", "gl_one_minus_src_alpha"]:
            has_alpha = True
    if has_additive:
        return "additive"
    if has_alpha:
        return "alpha"
    if has_multiply:
        return "multiply"
    return "opaque"


def make_additive_png(source_path: Path, output_path: Path) -> None:
    """Create a copy of source_path with alpha = max(R, G, B).

    This approximates additive (GL_ONE GL_ONE) blending in glTF BLEND mode:
    black pixels become fully transparent (add nothing to the background) while
    bright/coloured pixels remain visible proportional to their luminance.
    """
    if output_path.exists():
        return
    output_path.parent.mkdir(parents=True, exist_ok=True)
    img = Image.open(source_path).convert("RGBA")
    arr = np.array(img)
    arr[:, :, 3] = arr[:, :, :3].max(axis=2)
    Image.fromarray(arr, "RGBA").save(output_path, "PNG")


def bake_multistage_png(
    base_path: Path,
    output_path: Path,
    overlays: list[tuple[Path, float]],
) -> None:
    """Bake additive multi-stage overlays into a base texture PNG.

    Each entry in *overlays* is (overlay_path, intensity) where intensity is
    the fraction [0..1] of the overlay's peak contribution to bake (use 0.5
    for animations that oscillate between 0 and peak, giving the mid-cycle
    appearance).  The compositing formula is additive (GL_ONE GL_ONE):
        result = clamp(base + sum(overlay_i * intensity_i), 0, 1)
    """
    if output_path.exists():
        return
    output_path.parent.mkdir(parents=True, exist_ok=True)
    base = Image.open(base_path).convert("RGB")
    W, H = base.size
    result = np.array(base, dtype=np.float32) / 255.0
    for overlay_path, intensity in overlays:
        if not overlay_path.exists():
            continue
        ov = Image.open(overlay_path).convert("RGB").resize((W, H), Image.LANCZOS)
        result += np.array(ov, dtype=np.float32) / 255.0 * intensity
    Image.fromarray(np.clip(result, 0.0, 1.0).mul(255).astype(np.uint8) if False else
                    (np.clip(result, 0.0, 1.0) * 255).astype(np.uint8), "RGB").save(output_path, "PNG")


def pack_metallic_roughness_png(roughness_path: Path | None, metallic_path: Path | None, output_path: Path) -> None:
    if output_path.exists():
        return
    if roughness_path is None and metallic_path is None:
        return
    output_path.parent.mkdir(parents=True, exist_ok=True)

    roughness_img = Image.open(roughness_path).convert("L") if roughness_path is not None else None
    metallic_img = Image.open(metallic_path).convert("L") if metallic_path is not None else None
    width, height = (roughness_img or metallic_img).size
    if roughness_img is not None and roughness_img.size != (width, height):
        roughness_img = roughness_img.resize((width, height), Image.LANCZOS)
    if metallic_img is not None and metallic_img.size != (width, height):
        metallic_img = metallic_img.resize((width, height), Image.LANCZOS)

    roughness = np.array(roughness_img, dtype=np.uint8) if roughness_img is not None else np.full((height, width), 255, dtype=np.uint8)
    metallic = np.array(metallic_img, dtype=np.uint8) if metallic_img is not None else np.zeros((height, width), dtype=np.uint8)
    packed = np.empty((height, width, 4), dtype=np.uint8)
    packed[:, :, 0] = 255
    packed[:, :, 1] = roughness
    packed[:, :, 2] = metallic
    packed[:, :, 3] = 255
    Image.fromarray(packed, "RGBA").save(output_path, "PNG")


def prepare_manifest(args: argparse.Namespace) -> None:
    if Image is None or np is None:
        raise RuntimeError("prepare mode requires Pillow and numpy")

    q3_root = Path(args.q3_root).resolve()
    rtx_root = Path(args.rtx_root).resolve()
    output_dir = Path(args.output_dir).resolve()
    texture_dir = output_dir / "textures"
    output_dir.mkdir(parents=True, exist_ok=True)

    pk3_files = list_pk3_files(archive_roots(q3_root, rtx_root))
    index = build_archive_index(pk3_files)
    shader_defs = parse_shader_scripts(index)
    map_path = f"maps/{args.map}.bsp".lower()
    if map_path not in index:
        raise FileNotFoundError(f"Could not find {map_path} in Quake 3 archives")
    bsp_data = read_archive_file(index, map_path)
    shader_names = decode_bsp_textures(bsp_data)

    rtx_sets = collect_rtx_sets(rtx_root)
    rtx_sprites = collect_rtx_sprite_sheets(rtx_root)
    rtx_features = load_rtx_features(rtx_sets, output_dir.parent / "_rtx_feature_cache.npz")
    runtime_capture_features = None
    if args.runtime_captures_root:
        runtime_capture_features = load_runtime_capture_features(Path(args.runtime_captures_root).resolve(), args.map)

    materials: dict[str, dict[str, object]] = {}
    source_images: dict[str, Image.Image] = {}
    for shader_name in shader_names:
        normalized = shader_name.lower()
        skipped = normalized.startswith(SKIP_SHADER_PREFIXES)
        material_name = sanitize_name(normalized)
        material: dict[str, object] = {
            "shader": shader_name,
            "name": material_name,
            "skip": skipped,
            "textures": {},
        }
        if not skipped:
            shader_def = shader_defs.get(normalized, {})
            material["wrap"] = str(shader_def.get("wrap", "repeat"))
            material["alphaMode"] = str(shader_def.get("alphaMode", "OPAQUE"))
            material["alphaCutoff"] = float(shader_def.get("alphaCutoff", 0.5))
            material["doubleSided"] = bool(shader_def.get("doubleSided", False))
            q3_shader = shader_def.get("q3Shader", {})
            if isinstance(q3_shader, dict) and q3_shader:
                material["q3Shader"] = q3_shader
            uv_scale = shader_def.get("uvScale", (1.0, 1.0))
            if isinstance(uv_scale, (list, tuple)) and len(uv_scale) == 2:
                material["uvScale"] = [float(uv_scale[0]), float(uv_scale[1])]
            source_image_path = resolve_shader_image(index, shader_defs, shader_name)
            if source_image_path:
                raw_image = read_archive_file(index, source_image_path)
                source_image = Image.open(io.BytesIO(raw_image))
                source_image.load()
                fallback_output = cached_source_output(texture_dir, source_image_path)
                write_png(source_image.convert("RGBA"), fallback_output)
                material["sourceImage"] = source_image_path
                textures = material["textures"]
                assert isinstance(textures, dict)
                textures["baseColor"] = str(fallback_output)
                source_images.setdefault(source_image_path, source_image)

        materials[shader_name] = material

    source_matches: dict[str, dict[str, object]] = {}
    if runtime_capture_features is not None and runtime_capture_features["hashes"]:
        runtime_capture_paths = runtime_capture_features.get("paths", {})
        if not isinstance(runtime_capture_paths, dict):
            runtime_capture_paths = {}
        for source_image_path, source_image in source_images.items():
            match = choose_rtx_set(source_image, runtime_capture_features)
            if not match:
                continue
            material_hash, score, second_hash, second_score, score_gap = match
            runtime_match_good = score >= float(args.min_runtime_score)
            accepted = runtime_match_good and material_hash in rtx_sets
            runtime_capture_path = runtime_capture_paths.get(material_hash)
            reject_reason = ""
            if not accepted:
                reject_reason = "no-rtx-assets" if runtime_match_good else "below-runtime-threshold"
            source_matches[source_image_path] = {
                "rtxHash": material_hash,
                "rtxScore": score,
                "rtxSecondHash": second_hash,
                "rtxSecondScore": second_score,
                "rtxScoreGap": score_gap,
                "rtxMatchSource": "runtime-capture",
                "rtxAccepted": accepted,
                "rtxRejectReason": reject_reason,
                "runtimeCaptureAccepted": bool(runtime_match_good and runtime_capture_path),
                "runtimeCaptureTexture": str(runtime_capture_path) if runtime_capture_path else "",
            }
    else:
        for source_image_path, source_image in source_images.items():
            match = choose_rtx_set(source_image, rtx_features)
            if match:
                material_hash, score, second_hash, second_score, score_gap = match
                source_matches[source_image_path] = {
                    "rtxHash": material_hash,
                    "rtxScore": score,
                    "rtxSecondHash": second_hash,
                    "rtxSecondScore": second_score,
                    "rtxScoreGap": score_gap,
                    "rtxMatchSource": "structural-pbr",
                    "rtxAccepted": False,
                    "rtxRejectReason": "below-threshold",
                }

        eligible_by_hash: dict[str, list[tuple[float, float, str]]] = {}
        for source_image_path, match in source_matches.items():
            score = float(match["rtxScore"])
            score_gap = float(match["rtxScoreGap"])
            if score >= float(args.min_structural_score) and score_gap >= float(args.min_structural_gap):
                material_hash = str(match["rtxHash"])
                eligible_by_hash.setdefault(material_hash, []).append((score, score_gap, source_image_path))

        accepted_sources: set[str] = set()
        for entries in eligible_by_hash.values():
            entries.sort(key=lambda item: (item[0], item[1]), reverse=True)
            accepted_sources.add(entries[0][2])

        for source_image_path, match in source_matches.items():
            score = float(match["rtxScore"])
            score_gap = float(match["rtxScoreGap"])
            if source_image_path in accepted_sources:
                match["rtxAccepted"] = True
                match["rtxRejectReason"] = ""
            elif score >= float(args.min_structural_score) and score_gap >= float(args.min_structural_gap):
                match["rtxRejectReason"] = "pbr-hash-conflict"

    for material in materials.values():
        source_image_path = material.get("sourceImage")
        if not isinstance(source_image_path, str):
            continue
        match = source_matches.get(source_image_path)
        if not match:
            continue
        material.update(match)
        if not match.get("rtxAccepted"):
            runtime_capture_texture = match.get("runtimeCaptureTexture")
            if match.get("runtimeCaptureAccepted") and isinstance(runtime_capture_texture, str) and runtime_capture_texture:
                runtime_capture_path = Path(runtime_capture_texture)
                if runtime_capture_path.exists():
                    material_hash = str(match["rtxHash"])
                    channel_output = cached_runtime_output(texture_dir, material_hash)
                    convert_rtx_channel(runtime_capture_path, channel_output)
                    textures = material.get("textures", {})
                    if isinstance(textures, dict):
                        textures["baseColor"] = str(channel_output)
            continue
        material_hash = str(match["rtxHash"])
        channels = rtx_sets.get(material_hash, {})
        sprite_info = rtx_sprites.get(material_hash.upper())
        textures = material.get("textures", {})
        if not isinstance(textures, dict):
            continue
        for channel, source_path in channels.items():
            channel_output = cached_rtx_output(texture_dir, material_hash, channel)
            convert_rtx_channel(source_path, channel_output)
            channel_texture = channel_output
            if sprite_info is not None and (sprite_info["cols"] > 1 or sprite_info["rows"] > 1):
                channel_texture = cached_rtx_sprite_frame_output(texture_dir, material_hash, channel, 0)
                crop_sprite_sheet_frame(channel_output, channel_texture, sprite_info["cols"], sprite_info["rows"], 0)
            if channel == "albedo":
                # RTX Remix can bind sprite-sheet RTEX textures, with columns/rows stored
                # in mod.usda.  glTF has no equivalent RTX sprite-sheet material sampler,
                # so export a static frame crop for every PBR channel instead of mapping
                # the full animation sheet over TEXCOORD_0.
                use_albedo = channel_texture
                if sprite_info is not None and (sprite_info["cols"] > 1 or sprite_info["rows"] > 1):
                    print(f"  [sprite sheet] {material_hash}: RTX albedo "
                          f"{sprite_info['cols']}x{sprite_info['rows']} -> using frame 0")
                elif Image is not None:
                    try:
                        with Image.open(channel_texture) as _img:
                            _w, _h = _img.size
                            _arr = np.array(_img.convert("RGBA")).astype(np.float32) if np is not None else None
                        _is_nonsquare = _w > 0 and _h > 0 and (_w / _h > 2.0 or _h / _w > 2.0)
                        _is_square_atlas = False
                        if not _is_nonsquare and _arr is not None and _w >= 4 and _h >= 4:
                            # Detect square multi-frame atlases (e.g. 2×2 tiles at 1:1 ratio)
                            # by comparing left/right and top/bottom halves.  A genuine atlas
                            # packing N identical copies shows near-perfect half-correlation
                            # (>0.90), whereas organic tileable textures do not.
                            _hw, _hh = _w // 2, _h // 2
                            try:
                                _lr = float(np.corrcoef(_arr[:, :_hw, :].ravel(), _arr[:, _hw:_hw*2, :].ravel())[0, 1])
                                _tb = float(np.corrcoef(_arr[:_hh, :, :].ravel(), _arr[_hh:_hh*2, :, :].ravel())[0, 1])
                                _is_square_atlas = _lr > 0.88 and _tb > 0.88
                            except Exception:
                                pass
                        # Size-ratio fallback: if the RTX albedo is much larger than the Q3
                        # source texture the RTX pack has baked multiple surface textures into
                        # one large atlas quadrant-by-quadrant.  In that case the half-halves
                        # won't correlate (different textures per quadrant) so the square-atlas
                        # detector misses it.  Use the runtime capture when albedo is >=2x the
                        # Q3 source in either axis and a runtime capture exists.
                        _is_oversized = False
                        _qw, _qh = 0, 0
                        if not _is_nonsquare and not _is_square_atlas and _w >= 4 and _h >= 4:
                            try:
                                _q3_data = read_archive_file(index, source_image_path)
                                with Image.open(io.BytesIO(_q3_data)) as _qi:
                                    _qw, _qh = _qi.size
                                _is_oversized = (_w > _qw * 2) or (_h > _qh * 2)
                            except Exception:
                                pass

                        if _is_nonsquare or _is_square_atlas or _is_oversized:
                            rc_path = cached_runtime_output(texture_dir, material_hash)
                            if rc_path.exists():
                                use_albedo = rc_path
                                if _is_nonsquare:
                                    _reason = f"ratio {_w/_h:.1f}"
                                elif _is_square_atlas:
                                    _reason = f"square atlas corr={_lr:.2f}/{_tb:.2f}"
                                else:
                                    _reason = f"oversized {_w}x{_h} vs Q3 {_qw}x{_qh}"
                                print(f"  [atlas fallback] {material_hash}: RTX albedo {_w}x{_h} "
                                      f"({_reason}) -> using runtime_basecolor")
                    except Exception:
                        pass
                textures["baseColor"] = str(use_albedo)
            elif channel in ("normal", "roughness", "metallic", "emissive"):
                if channel == "normal":
                    textures["normal"] = str(channel_texture)
                elif channel == "roughness":
                    textures["roughness"] = str(channel_texture)
                elif channel == "metallic":
                    textures["metallic"] = str(channel_texture)
                elif channel == "emissive":
                    textures["emissive"] = str(channel_texture)

        roughness_path = textures.get("roughness")
        metallic_path = textures.get("metallic")
        roughness_file = Path(roughness_path) if isinstance(roughness_path, str) and roughness_path else None
        metallic_file = Path(metallic_path) if isinstance(metallic_path, str) and metallic_path else None
        if roughness_file is not None or metallic_file is not None:
            metallic_roughness_path = cached_metallic_roughness_output(texture_dir, material_hash)
            pack_metallic_roughness_png(roughness_file, metallic_file, metallic_roughness_path)
            if metallic_roughness_path.exists():
                textures["metallicRoughness"] = str(metallic_roughness_path)

    # Post-process BLEND materials: derive proper alpha from blend type.
    # Q3 additive shaders (GL_ONE GL_ONE) have no alpha in the source texture;
    # in glTF BLEND mode we approximate by setting alpha = max(R,G,B) so black
    # areas vanish and bright areas remain visible — a standard additive approx.
    # Pure multiplicative shaders (GL_DST_COLOR GL_ZERO) don't use transparency
    # in any viewer-meaningful way, so we demote them to OPAQUE.
    for material in materials.values():
        if material.get("alphaMode") != "BLEND":
            continue
        q3_shader = material.get("q3Shader", {})
        if not isinstance(q3_shader, dict):
            continue
        surfaceparms = q3_shader.get("surfaceparms", [])
        if "sky" in surfaceparms:
            continue  # sky uses additive cloud layers but must stay opaque
        blend_type = classify_blend_type(q3_shader)
        material["blendType"] = blend_type
        if blend_type == "multiply":
            material["alphaMode"] = "OPAQUE"
            print(f"  [blend] demoting multiply-only BLEND to OPAQUE: {material.get('shader', '')}")
            continue
        # additive or alpha-blend with no alpha in source → luminance alpha
        textures = material.get("textures", {})
        if not isinstance(textures, dict):
            continue
        base_color_path = textures.get("baseColor")
        if not base_color_path:
            continue
        bc_path = Path(base_color_path)
        if not bc_path.exists():
            continue
        additive_path = bc_path.with_name(bc_path.stem + "_additive.png")
        try:
            make_additive_png(bc_path, additive_path)
            textures["baseColor"] = str(additive_path)
            print(f"  [blend] additive alpha applied: {material.get('shader', '')}")
        except Exception as exc:
            print(f"  [blend] additive alpha failed for {material.get('shader', '')}: {exc}")

    # Post-process MASK materials: solid floor grates (trans but NOT nonsolid) must
    # stay in the opaque draw pass to avoid z-fighting with co-planar OPAQUE floor
    # geometry.  Many viewers incorrectly render MASK in the transparent pass without
    # depth testing, making solid-floor grates appear over the floor surface they sit
    # on.  Since the holes in a solid floor grate reveal nothing useful (just void),
    # we demote these to OPAQUE.  Non-solid grates (cage walls, fences) keep MASK
    # because their see-through quality is architecturally important.
    for material in materials.values():
        if material.get("alphaMode") != "MASK":
            continue
        q3_shader = material.get("q3Shader", {})
        if not isinstance(q3_shader, dict):
            continue
        surfaceparms = q3_shader.get("surfaceparms", [])
        # trans = marked transparent in Q3, nonsolid = players can walk through it.
        # solid + trans = floor/wall grate that clips BSP but was alpha-tested in Q3.
        if "trans" in surfaceparms and "nonsolid" not in surfaceparms:
            material["alphaMode"] = "OPAQUE"
            print(f"  [mask] demoting solid-floor MASK to OPAQUE: {material.get('shader', '')}")

    # Post-process bounce-pad shaders: bake additive multi-stage overlays into the
    # base texture so the glow pattern is visible in static glTF export.
    # Q3 bounce pad shaders have two extra additive stages on every bounce surface:
    #   stage N+1: bouncepad01b_layer1  (blendfunc add, rgbGen wave sin .5 .5 0 1.5)
    #   stage N+2: jumppadsmall         (blendfunc add, clampmap, tcMod stretch ...)
    # We bake both at 50% intensity (mid-cycle average of the sin/square animation).
    _overlay_names = [
        ("textures/sfx/bouncepad01b_layer1", 0.5),
        ("textures/sfx/jumppadsmall",        0.5),
    ]
    # Resolve overlay source images from PK3 archives
    _overlays: list[tuple[Path, float]] = []
    for _ov_name, _ov_int in _overlay_names:
        _ov_cached = texture_dir / "_q3" / _ov_name.replace("/", "_") / "base.png"
        if not _ov_cached.exists():
            _ov_cached.parent.mkdir(parents=True, exist_ok=True)
            _ov_raw = read_archive_file(index, _ov_name + ".jpg") or read_archive_file(index, _ov_name + ".tga")
            if _ov_raw:
                import io as _io
                Image.open(_io.BytesIO(_ov_raw)).convert("RGB").save(_ov_cached, "PNG")
        if _ov_cached.exists():
            _overlays.append((_ov_cached, _ov_int))

    if _overlays:
        for material in materials.values():
            q3s = material.get("q3Shader", {})
            if not isinstance(q3s, dict):
                continue
            # Only bounce pad shaders have the layer1/jumppadsmall stages
            stages = q3s.get("stages", [])
            has_layer1 = any("bouncepad01b_layer1" in s.get("map", "") for s in stages)
            has_jumppad = any("jumppadsmall" in s.get("map", "") for s in stages)
            if not (has_layer1 or has_jumppad):
                continue
            textures = material.get("textures", {})
            if not isinstance(textures, dict):
                continue
            bc_path_str = textures.get("baseColor", "")
            if not bc_path_str:
                continue
            bc_path = Path(bc_path_str)
            if not bc_path.exists():
                continue
            if bc_path.stem.endswith("_baked"):
                continue  # already baked
            baked_path = bc_path.with_name(bc_path.stem + "_baked.png")
            try:
                bake_multistage_png(bc_path, baked_path, _overlays)
                textures["baseColor"] = str(baked_path)
                print(f"  [bake] bounce-pad overlays baked: {material.get('shader', '')}")
            except Exception as exc:
                print(f"  [bake] failed for {material.get('shader', '')}: {exc}")

    lightmap_desc = None
    if bool(getattr(args, "include_lightmaps", False)):
        lightmap_desc = write_lightmap_atlas(bsp_data, cached_lightmap_atlas_output(texture_dir, args.map))

    manifest = {
        "map": args.map,
        "bspArchivePath": map_path,
        "pk3Files": [str(path) for path in pk3_files],
        "materials": materials,
        "patchSubdivisions": int(args.patch_subdivisions),
    }
    if lightmap_desc is not None:
        manifest["lightmap"] = lightmap_desc
    manifest_path = output_dir / f"{args.map}_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    matched = sum(
        1
        for value in materials.values()
        if value.get("rtxAccepted")
    )
    print(f"wrote {manifest_path}")
    runtime_candidates = len(runtime_capture_features["hashes"]) if runtime_capture_features is not None else 0
    print(f"materials: {len(materials)}, RTX candidates accepted: {matched}, RTX albedo candidates: {len(rtx_features['hashes'])}, runtime capture candidates: {runtime_candidates}")


def read_pk3_from_manifest(manifest: dict[str, object], path: str) -> bytes:
    normalized_path = path.lower()
    for pk3_name in reversed(manifest["pk3Files"]):  # type: ignore[arg-type]
        with zipfile.ZipFile(pk3_name) as archive:
            names = {name.replace("\\", "/").lower(): name for name in archive.namelist()}
            if normalized_path in names:
                return archive.read(names[normalized_path])
    raise FileNotFoundError(path)


def parse_bsp_vertices(bsp_data: bytes) -> tuple[list[str], list[Vertex], list[int], list[tuple[int, ...]]]:
    lumps = [struct.unpack_from("<II", bsp_data, 8 + lump_index * 8) for lump_index in range(17)]

    texture_offset, texture_length = lumps[Q3_BSP_LUMP_TEXTURES]
    shader_names: list[str] = []
    for texture_index in range(texture_length // 72):
        offset = texture_offset + texture_index * 72
        raw_name = bsp_data[offset:offset + 64]
        shader_names.append(raw_name.split(b"\0", 1)[0].decode("ascii", errors="replace"))

    vertex_offset, vertex_length = lumps[Q3_BSP_LUMP_VERTICES]
    vertices: list[Vertex] = []
    for vertex_index in range(vertex_length // 44):
        offset = vertex_offset + vertex_index * 44
        position = struct.unpack_from("<3f", bsp_data, offset)
        texcoord = struct.unpack_from("<2f", bsp_data, offset + 12)
        lightmap_texcoord = struct.unpack_from("<2f", bsp_data, offset + 20)
        normal = struct.unpack_from("<3f", bsp_data, offset + 28)
        color = struct.unpack_from("<4B", bsp_data, offset + 40)
        vertices.append(Vertex(position, texcoord, lightmap_texcoord, normal, color))

    meshvert_offset, meshvert_length = lumps[Q3_BSP_LUMP_MESHVERTS]
    meshverts = list(struct.unpack_from(f"<{meshvert_length // 4}i", bsp_data, meshvert_offset)) if meshvert_length else []

    face_offset, face_length = lumps[Q3_BSP_LUMP_FACES]
    faces: list[tuple[int, ...]] = []
    for face_index in range(face_length // 104):
        offset = face_offset + face_index * 104
        faces.append(struct.unpack_from("<12i3f6f3f2i", bsp_data, offset))
    return shader_names, vertices, meshverts, faces


def bezier2(first: float, second: float, third: float, factor: float) -> float:
    inv = 1.0 - factor
    return inv * inv * first + 2.0 * inv * factor * second + factor * factor * third


def bezier_vertex(control: list[Vertex], width: int, x_base: int, y_base: int, u_factor: float, v_factor: float) -> Vertex:
    row_values: list[Vertex] = []
    for row_offset in range(3):
        row = [control[(y_base + row_offset) * width + x_base + column_offset] for column_offset in range(3)]
        row_position = tuple(bezier2(row[0].position[axis], row[1].position[axis], row[2].position[axis], u_factor) for axis in range(3))
        row_texcoord = tuple(bezier2(row[0].texcoord[axis], row[1].texcoord[axis], row[2].texcoord[axis], u_factor) for axis in range(2))
        row_lightmap_texcoord = tuple(bezier2(row[0].lightmap_texcoord[axis], row[1].lightmap_texcoord[axis], row[2].lightmap_texcoord[axis], u_factor) for axis in range(2))
        row_normal = tuple(bezier2(row[0].normal[axis], row[1].normal[axis], row[2].normal[axis], u_factor) for axis in range(3))
        row_values.append(Vertex(row_position, row_texcoord, row_lightmap_texcoord, row_normal, (255, 255, 255, 255)))
    position = tuple(bezier2(row_values[0].position[axis], row_values[1].position[axis], row_values[2].position[axis], v_factor) for axis in range(3))
    texcoord = tuple(bezier2(row_values[0].texcoord[axis], row_values[1].texcoord[axis], row_values[2].texcoord[axis], v_factor) for axis in range(2))
    lightmap_texcoord = tuple(bezier2(row_values[0].lightmap_texcoord[axis], row_values[1].lightmap_texcoord[axis], row_values[2].lightmap_texcoord[axis], v_factor) for axis in range(2))
    normal = tuple(bezier2(row_values[0].normal[axis], row_values[1].normal[axis], row_values[2].normal[axis], v_factor) for axis in range(3))
    length = math.sqrt(sum(component * component for component in normal)) or 1.0
    return Vertex(position, texcoord, lightmap_texcoord, tuple(component / length for component in normal), (255, 255, 255, 255))


def packed_lightmap_uv(vertex: Vertex, lightmap_num: int, lightmap_desc: dict[str, object]) -> tuple[float, float]:
    lightmap_count = int(lightmap_desc.get("count", 0))
    tile_index = lightmap_num if 0 <= lightmap_num < lightmap_count else int(lightmap_desc.get("whiteTile", lightmap_count))
    tile_size = float(lightmap_desc.get("tileSize", Q3_LIGHTMAP_SIZE))
    gutter = float(lightmap_desc.get("gutter", Q3_LIGHTMAP_GUTTER))
    columns = int(lightmap_desc.get("atlasColumns", 1))
    rows = int(lightmap_desc.get("atlasRows", 1))
    pitch = float(lightmap_desc.get("atlasPitch", tile_size + gutter * 2.0))
    local_u, local_v = vertex.lightmap_texcoord if 0 <= lightmap_num < lightmap_count else (0.5, 0.5)
    atlas_u = ((tile_index % columns) * pitch + gutter + local_u * tile_size) / (columns * pitch)
    atlas_v = ((tile_index // columns) * pitch + gutter + local_v * tile_size) / (rows * pitch)
    return atlas_u, atlas_v


def make_material(material_desc: dict[str, object]):
    material = bpy.data.materials.new(str(material_desc.get("name", "material")))
    material.use_nodes = True
    material.diffuse_color = (1.0, 1.0, 1.0, 1.0)
    alpha_mode = str(material_desc.get("alphaMode", "OPAQUE")).upper()
    if alpha_mode not in ("OPAQUE", "MASK", "BLEND"):
        alpha_mode = "OPAQUE"
    alpha_cutoff = float(material_desc.get("alphaCutoff", 0.5))
    double_sided = bool(material_desc.get("doubleSided", False))
    material.use_backface_culling = not double_sided
    if hasattr(material, "blend_method"):
        material.blend_method = "CLIP" if alpha_mode == "MASK" else ("BLEND" if alpha_mode == "BLEND" else "OPAQUE")
    if hasattr(material, "alpha_threshold"):
        material.alpha_threshold = alpha_cutoff
    if hasattr(material, "surface_render_method"):
        material.surface_render_method = "BLENDED" if alpha_mode == "BLEND" else "DITHERED"
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    principled = nodes.get("Principled BSDF")
    textures = material_desc.get("textures", {})
    if not isinstance(textures, dict) or principled is None:
        return material

    wrap_mode = str(material_desc.get("wrap", "repeat"))

    def add_image_texture(path_value: object, color_space: str):
        if not path_value:
            return None
        image_path = str(path_value)
        if not Path(image_path).exists():
            return None
        image = bpy.data.images.load(image_path, check_existing=True)
        image.colorspace_settings.name = color_space
        texture_node = nodes.new("ShaderNodeTexImage")
        texture_node.image = image
        texture_node.extension = "EXTEND" if wrap_mode == "clamp" else "REPEAT"
        return texture_node

    base_node = add_image_texture(textures.get("baseColor"), "sRGB")
    if base_node:
        links.new(base_node.outputs["Color"], principled.inputs["Base Color"])
        if alpha_mode != "OPAQUE" and "Alpha" in base_node.outputs and "Alpha" in principled.inputs:
            links.new(base_node.outputs["Alpha"], principled.inputs["Alpha"])
            principled.inputs["Alpha"].default_value = 1.0

    normal_node = add_image_texture(textures.get("normal"), "Non-Color")
    if normal_node:
        normal_map = nodes.new("ShaderNodeNormalMap")
        links.new(normal_node.outputs["Color"], normal_map.inputs["Color"])
        links.new(normal_map.outputs["Normal"], principled.inputs["Normal"])

    metallic_roughness_node = add_image_texture(textures.get("metallicRoughness"), "Non-Color")
    if metallic_roughness_node:
        try:
            separate = nodes.new("ShaderNodeSeparateColor")
            color_input = separate.inputs.get("Color")
            green_output = separate.outputs.get("Green")
            blue_output = separate.outputs.get("Blue")
        except RuntimeError:
            separate = nodes.new("ShaderNodeSeparateRGB")
            color_input = separate.inputs.get("Image")
            green_output = separate.outputs.get("G")
            blue_output = separate.outputs.get("B")
        links.new(metallic_roughness_node.outputs["Color"], color_input)
        links.new(green_output, principled.inputs["Roughness"])
        links.new(blue_output, principled.inputs["Metallic"])
    else:
        roughness_node = add_image_texture(textures.get("roughness"), "Non-Color")
        if roughness_node:
            links.new(roughness_node.outputs["Color"], principled.inputs["Roughness"])
        else:
            principled.inputs["Roughness"].default_value = 0.8

        metallic_node = add_image_texture(textures.get("metallic"), "Non-Color")
        if metallic_node:
            links.new(metallic_node.outputs["Color"], principled.inputs["Metallic"])
        else:
            principled.inputs["Metallic"].default_value = 0.0

    emissive_node = add_image_texture(textures.get("emissive"), "sRGB")
    if emissive_node:
        links.new(emissive_node.outputs["Color"], principled.inputs["Emission Color"])
        principled.inputs["Emission Strength"].default_value = 2.0

    return material


def patch_glb_material_metadata(output_path: Path, material_descs: dict[str, dict[str, object]], lightmap_desc: dict[str, object] | None = None) -> None:
    data = output_path.read_bytes()
    if len(data) < 20:
        return
    magic, version, _ = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2:
        return

    offset = 12
    chunks: list[tuple[int, bytes]] = []
    while offset + 8 <= len(data):
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk_data = data[offset:offset + chunk_length]
        offset += chunk_length
        chunks.append((chunk_type, chunk_data))
    if not chunks or chunks[0][0] != 0x4E4F534A:
        return

    json_text = chunks[0][1].rstrip(b" \t\r\n\0").decode("utf-8")
    gltf = json.loads(json_text)
    by_name = {str(desc.get("name", "")): desc for desc in material_descs.values() if isinstance(desc, dict)}
    lightmap_texture_index = None

    if lightmap_desc is not None:
        lightmap_path = Path(str(lightmap_desc["texture"]))
        lightmap_bytes = lightmap_path.read_bytes()
        bin_index = next((index for index, (chunk_type, _) in enumerate(chunks) if chunk_type == 0x004E4942), None)
        if bin_index is None:
            raise RuntimeError("Cannot embed lightmap atlas in GLB without a BIN chunk")
        bin_chunk = chunks[bin_index][1]
        bin_offset = len(bin_chunk)
        buffer_views = gltf.setdefault("bufferViews", [])
        buffer_view_index = len(buffer_views)
        buffer_views.append({
            "buffer": 0,
            "byteOffset": bin_offset,
            "byteLength": len(lightmap_bytes),
        })
        buffers = gltf.setdefault("buffers", [{"byteLength": 0}])
        if not buffers:
            buffers.append({"byteLength": 0})
        updated_bin = bin_chunk + lightmap_bytes
        bin_padding = (4 - (len(updated_bin) % 4)) % 4
        if bin_padding:
            updated_bin += b"\0" * bin_padding
        buffers[0]["byteLength"] = len(updated_bin)
        chunks[bin_index] = (0x004E4942, updated_bin)

        samplers = gltf.setdefault("samplers", [])
        sampler_index = len(samplers)
        samplers.append({
            "magFilter": 9729,
            "minFilter": 9729,
            "wrapS": 33071,
            "wrapT": 33071,
        })
        images = gltf.setdefault("images", [])
        image_index = len(images)
        images.append({
            "name": f"{str(lightmap_desc.get('extension', 'q3'))}_atlas",
            "mimeType": "image/png",
            "bufferView": buffer_view_index,
        })
        textures = gltf.setdefault("textures", [])
        lightmap_texture_index = len(textures)
        textures.append({
            "name": "q3_bsp_lightmap_atlas",
            "sampler": sampler_index,
            "source": image_index,
        })
        extensions_used = gltf.setdefault("extensionsUsed", [])
        if "MOZ_lightmap" not in extensions_used:
            extensions_used.append("MOZ_lightmap")

    for material_json in gltf.get("materials", []):
        if not isinstance(material_json, dict):
            continue
        desc = by_name.get(str(material_json.get("name", "")))
        if not desc:
            continue
        alpha_mode = str(desc.get("alphaMode", "OPAQUE")).upper()
        if alpha_mode in ("MASK", "BLEND"):
            material_json["alphaMode"] = alpha_mode
            if alpha_mode == "MASK":
                material_json["alphaCutoff"] = float(desc.get("alphaCutoff", 0.5))
            else:
                material_json.pop("alphaCutoff", None)
        else:
            material_json.pop("alphaMode", None)
            material_json.pop("alphaCutoff", None)

        if bool(desc.get("doubleSided", False)):
            material_json["doubleSided"] = True
        else:
            material_json.pop("doubleSided", None)

        q3_shader = desc.get("q3Shader")
        if isinstance(q3_shader, dict) and q3_shader:
            extras = material_json.get("extras")
            if not isinstance(extras, dict):
                extras = {}
            extras.pop("extras", None)
            extras["q3Shader"] = q3_shader
            material_json["extras"] = extras

        if lightmap_texture_index is not None:
            extensions = material_json.get("extensions")
            if not isinstance(extensions, dict):
                extensions = {}
            moz_lightmap = {
                "index": lightmap_texture_index,
                "texCoord": int(lightmap_desc.get("texCoord", 1)) if lightmap_desc else 1,
            }
            intensity = float(lightmap_desc.get("intensity", 1.0)) if lightmap_desc else 1.0
            if intensity != 1.0:
                moz_lightmap["intensity"] = intensity
            extensions["MOZ_lightmap"] = moz_lightmap
            material_json["extensions"] = extensions

    json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    json_padding = (4 - (len(json_bytes) % 4)) % 4
    json_chunk = json_bytes + b" " * json_padding
    rebuilt_chunks = [(0x4E4F534A, json_chunk)] + chunks[1:]
    total_length = 12 + sum(8 + len(chunk_data) for _, chunk_data in rebuilt_chunks)
    rebuilt = bytearray(struct.pack("<III", magic, version, total_length))
    for chunk_type, chunk_data in rebuilt_chunks:
        rebuilt.extend(struct.pack("<II", len(chunk_data), chunk_type))
        rebuilt.extend(chunk_data)
    output_path.write_bytes(rebuilt)


def export_glb(args: argparse.Namespace) -> None:
    if bpy is None:
        raise RuntimeError("export mode must be run with Blender")

    manifest_path = Path(args.manifest).resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    bsp_data = read_pk3_from_manifest(manifest, manifest["bspArchivePath"])
    shader_names, source_vertices, meshverts, bsp_faces = parse_bsp_vertices(bsp_data)
    material_descs = manifest["materials"]
    patch_subdivisions = int(manifest.get("patchSubdivisions", 6))
    lightmap_desc = manifest.get("lightmap")
    if not isinstance(lightmap_desc, dict):
        lightmap_desc = None

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()

    blender_materials: dict[str, object] = {}
    for shader_name in shader_names:
        material_desc = material_descs.get(shader_name, {})
        if material_desc.get("skip"):
            continue
        blender_materials[shader_name] = make_material(material_desc)

    mesh_vertices: list[tuple[float, float, float]] = []
    mesh_normals: list[tuple[float, float, float]] = []
    mesh_uvs: list[tuple[float, float]] = []
    mesh_lightmap_uvs: list[tuple[float, float]] = []
    triangles: list[tuple[int, int, int]] = []
    triangle_materials: list[str] = []

    uv_scales: dict[str, tuple[float, float]] = {}
    for shader_name, material_desc in material_descs.items():
        uv_scale = material_desc.get("uvScale", [1.0, 1.0])
        if isinstance(uv_scale, list) and len(uv_scale) == 2:
            uv_scales[shader_name] = (float(uv_scale[0]), float(uv_scale[1]))
        else:
            uv_scales[shader_name] = (1.0, 1.0)

    def append_vertex(vertex: Vertex, shader_name: str, lightmap_num: int) -> int:
        uv_scale = uv_scales.get(shader_name, (1.0, 1.0))
        mesh_vertices.append(vertex.position)
        mesh_normals.append(vertex.normal)
        mesh_uvs.append((vertex.texcoord[0] * uv_scale[0], vertex.texcoord[1] * uv_scale[1]))
        if lightmap_desc is not None:
            mesh_lightmap_uvs.append(packed_lightmap_uv(vertex, lightmap_num, lightmap_desc))
        return len(mesh_vertices) - 1

    def append_triangle(first: Vertex, second: Vertex, third: Vertex, shader_name: str, lightmap_num: int) -> None:
        first_index = append_vertex(first, shader_name, lightmap_num)
        second_index = append_vertex(second, shader_name, lightmap_num)
        third_index = append_vertex(third, shader_name, lightmap_num)
        # Quake 3 BSP meshverts are opposite Blender/glTF's front-face winding.
        triangles.append((first_index, third_index, second_index))
        triangle_materials.append(shader_name)

    for face in bsp_faces:
        shader_index = face[0]
        face_type = face[2]
        vertex_start = face[3]
        vertex_count = face[4]
        meshvert_start = face[5]
        meshvert_count = face[6]
        lightmap_num = face[7]
        patch_width = face[-2]
        patch_height = face[-1]
        if shader_index < 0 or shader_index >= len(shader_names):
            continue
        shader_name = shader_names[shader_index]
        material_desc = material_descs.get(shader_name, {})
        if material_desc.get("skip") or shader_name not in blender_materials:
            continue

        if face_type in (FACE_POLYGON, FACE_MESH):
            for mesh_index in range(0, meshvert_count - 2, 3):
                first_index = vertex_start + meshverts[meshvert_start + mesh_index]
                second_index = vertex_start + meshverts[meshvert_start + mesh_index + 1]
                third_index = vertex_start + meshverts[meshvert_start + mesh_index + 2]
                if first_index < 0 or second_index < 0 or third_index < 0:
                    continue
                append_triangle(source_vertices[first_index], source_vertices[second_index], source_vertices[third_index], shader_name, lightmap_num)
        elif face_type == FACE_PATCH and patch_width >= 3 and patch_height >= 3:
            control = source_vertices[vertex_start:vertex_start + vertex_count]
            if len(control) < patch_width * patch_height:
                continue
            for y_base in range(0, patch_height - 2, 2):
                for x_base in range(0, patch_width - 2, 2):
                    grid: list[list[Vertex]] = []
                    for row_index in range(patch_subdivisions + 1):
                        v_factor = row_index / patch_subdivisions
                        row: list[Vertex] = []
                        for column_index in range(patch_subdivisions + 1):
                            u_factor = column_index / patch_subdivisions
                            row.append(bezier_vertex(control, patch_width, x_base, y_base, u_factor, v_factor))
                        grid.append(row)
                    for row_index in range(patch_subdivisions):
                        for column_index in range(patch_subdivisions):
                            upper_left = grid[row_index][column_index]
                            upper_right = grid[row_index][column_index + 1]
                            lower_left = grid[row_index + 1][column_index]
                            lower_right = grid[row_index + 1][column_index + 1]
                            append_triangle(upper_left, lower_left, upper_right, shader_name, lightmap_num)
                            append_triangle(upper_right, lower_left, lower_right, shader_name, lightmap_num)

    mesh = bpy.data.meshes.new(str(manifest.get("map", "q3map")))
    mesh.from_pydata(mesh_vertices, [], triangles)
    mesh.update()

    material_slots = list(blender_materials.keys())
    material_indices = {shader_name: index for index, shader_name in enumerate(material_slots)}
    for shader_name in material_slots:
        mesh.materials.append(blender_materials[shader_name])
    for polygon, shader_name in zip(mesh.polygons, triangle_materials):
        polygon.material_index = material_indices.get(shader_name, 0)

    uv_layer = mesh.uv_layers.new(name="UVMap")
    for polygon in mesh.polygons:
        for loop_index in polygon.loop_indices:
            vertex_index = mesh.loops[loop_index].vertex_index
            texcoord = mesh_uvs[vertex_index]
            uv_layer.data[loop_index].uv = (texcoord[0], 1.0 - texcoord[1])

    if lightmap_desc is not None:
        lightmap_uv_layer = mesh.uv_layers.new(name="LightmapUV")
        for polygon in mesh.polygons:
            for loop_index in polygon.loop_indices:
                vertex_index = mesh.loops[loop_index].vertex_index
                texcoord = mesh_lightmap_uvs[vertex_index]
                lightmap_uv_layer.data[loop_index].uv = (texcoord[0], 1.0 - texcoord[1])

    obj = bpy.data.objects.new(str(manifest.get("map", "q3map")), mesh)
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.shade_smooth()

    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(output_path),
        export_format="GLB",
        export_image_format="AUTO",
        export_materials="EXPORT",
        export_extras=True,
        export_texcoords=True,
        export_normals=True,
        export_tangents=True,
        export_apply=True,
    )
    patch_glb_material_metadata(output_path, material_descs, lightmap_desc)
    print(f"wrote {output_path}")
    print(f"triangles: {len(triangles)}, vertices: {len(mesh_vertices)}, materials: {len(material_slots)}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Convert a Q3 BSP to GLB with RTX Remix PBR textures")
    subparsers = parser.add_subparsers(dest="mode", required=True)

    prepare = subparsers.add_parser("prepare")
    prepare.add_argument("--q3-root", required=True)
    prepare.add_argument("--rtx-root", required=True)
    prepare.add_argument("--map", default="q3dm3")
    prepare.add_argument("--output-dir", required=True)
    prepare.add_argument("--max-match-score", type=float, default=0.015)
    prepare.add_argument("--max-match-ratio", type=float, default=0.95)
    prepare.add_argument("--min-structural-score", type=float, default=0.90)
    prepare.add_argument("--min-structural-gap", type=float, default=0.15)
    prepare.add_argument("--runtime-captures-root", default="")
    prepare.add_argument("--min-runtime-score", type=float, default=0.75)
    prepare.add_argument("--min-runtime-gap", type=float, default=0.03)
    prepare.add_argument("--patch-subdivisions", type=int, default=6)
    prepare.add_argument("--include-lightmaps", action="store_true", help="Embed original BSP RGB lightmaps using TEXCOORD_1 and MOZ_lightmap")

    export = subparsers.add_parser("export")
    export.add_argument("--manifest", required=True)
    export.add_argument("--output", required=True)
    return parser


def main() -> None:
    script_args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    parser = build_parser()
    args = parser.parse_args(script_args)
    if args.mode == "prepare":
        prepare_manifest(args)
    elif args.mode == "export":
        export_glb(args)


if __name__ == "__main__":
    main()
