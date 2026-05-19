#!/usr/bin/env python3
"""Export Quake 3 BSP light entities into T8ditor scene files."""

from __future__ import annotations

import argparse
import json
import math
import re
import struct
import sys
from pathlib import Path

from convert_q3_bsp_to_glb import archive_roots, build_archive_index, list_pk3_files, parse_bsp_vertices, read_archive_file


Q3_BSP_LUMP_ENTITIES = 0
DEFAULT_MAPS = [f"q3dm{index}" for index in range(20)] + [f"q3tourney{index}" for index in range(1, 7)] + ["nv15"]
DEFAULT_UNIT_SCALE = 1.0 / 32.0

ENTITY_BLOCK_RE = re.compile(r"\{([^{}]*)\}", re.S)
ENTITY_KV_RE = re.compile(r'"([^"]*)"\s*"([^"]*)"')


def vec3(x: float, y: float, z: float) -> dict[str, float]:
    return {"x": round(float(x), 6), "y": round(float(y), 6), "z": round(float(z), 6)}


def q3_to_scene(value: tuple[float, float, float], unit_scale: float = 1.0) -> tuple[float, float, float]:
    return (value[0] * unit_scale, value[2] * unit_scale, value[1] * unit_scale)


def q3_direction_to_scene(value: tuple[float, float, float]) -> tuple[float, float, float]:
    return normalize_vector(q3_to_scene(value))


def q3_bounds_to_scene(
    bounds_min: tuple[float, float, float],
    bounds_max: tuple[float, float, float],
    unit_scale: float,
) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    corners = [
        q3_to_scene((x, y, z), unit_scale)
        for x in (bounds_min[0], bounds_max[0])
        for y in (bounds_min[1], bounds_max[1])
        for z in (bounds_min[2], bounds_max[2])
    ]
    scene_min = tuple(min(corner[axis] for corner in corners) for axis in range(3))
    scene_max = tuple(max(corner[axis] for corner in corners) for axis in range(3))
    return scene_min, scene_max  # type: ignore[return-value]


def parse_vec3(value: str | None, default: tuple[float, float, float]) -> tuple[float, float, float]:
    if not value:
        return default
    parts = value.replace(",", " ").split()
    if len(parts) < 3:
        return default
    try:
        return (float(parts[0]), float(parts[1]), float(parts[2]))
    except ValueError:
        return default


def normalize_vector(value: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(sum(component * component for component in value))
    if length <= 0.00001:
        return (0.0, -1.0, 0.0)
    return tuple(component / length for component in value)  # type: ignore[return-value]


def clamp(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(maximum, value))


def parse_entities(bsp_data: bytes) -> list[dict[str, str]]:
    offset, length = struct.unpack_from("<II", bsp_data, 8 + Q3_BSP_LUMP_ENTITIES * 8)
    text = bsp_data[offset:offset + length].decode("latin-1", errors="replace")
    return [dict(ENTITY_KV_RE.findall(match.group(1))) for match in ENTITY_BLOCK_RE.finditer(text)]


def light_direction(entity: dict[str, str], target_origins: dict[str, tuple[float, float, float]]) -> tuple[float, float, float]:
    origin = parse_vec3(entity.get("origin"), (0.0, 0.0, 0.0))
    target_name = entity.get("target")
    if target_name and target_name in target_origins:
        target = target_origins[target_name]
        return normalize_vector((target[0] - origin[0], target[1] - origin[1], target[2] - origin[2]))

    angle_text = entity.get("angle")
    if angle_text:
        try:
            angle = float(angle_text)
            if angle == -1:
                return (0.0, 0.0, 1.0)
            if angle == -2:
                return (0.0, 0.0, -1.0)
            radians = math.radians(angle)
            return normalize_vector((math.cos(radians), math.sin(radians), 0.0))
        except ValueError:
            pass

    return (0.0, -1.0, 0.0)


def entity_color(entity: dict[str, str]) -> tuple[float, float, float]:
    color = parse_vec3(entity.get("_color") or entity.get("color"), (1.0, 1.0, 1.0))
    if max(color) > 1.5:
        color = tuple(component / 255.0 for component in color)  # type: ignore[assignment]
    return tuple(clamp(component, 0.0, 1.0) for component in color)  # type: ignore[return-value]


def entity_brightness(entity: dict[str, str]) -> float:
    for key in ("light", "_light", "intensity"):
        value = entity.get(key)
        if value:
            try:
                return max(0.0, float(value.split()[0]))
            except ValueError:
                pass
    return 300.0


def entity_radius(entity: dict[str, str], brightness: float, radius_scale: float) -> float:
    value = entity.get("radius")
    if value:
        try:
            radius = float(value.split()[0])
            if radius > 0.0:
                return radius
        except ValueError:
            pass
    return max(8.0, math.sqrt(max(brightness, 1.0)) * radius_scale)


def scaled_radius(entity: dict[str, str], brightness: float, radius_scale: float, unit_scale: float) -> float:
    return entity_radius(entity, brightness, radius_scale) * unit_scale


def bsp_bounds(bsp_data: bytes) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    _, vertices, _, _ = parse_bsp_vertices(bsp_data)
    if not vertices:
        return ((0.0, 0.0, 0.0), (1.0, 1.0, 1.0))
    mins = [min(vertex.position[axis] for vertex in vertices) for axis in range(3)]
    maxs = [max(vertex.position[axis] for vertex in vertices) for axis in range(3)]
    return (tuple(mins), tuple(maxs))  # type: ignore[return-value]


def make_editor_state(bounds_min: tuple[float, float, float], bounds_max: tuple[float, float, float]) -> tuple[dict[str, object], dict[str, object]]:
    center = tuple((bounds_min[axis] + bounds_max[axis]) * 0.5 for axis in range(3))
    extent = tuple(bounds_max[axis] - bounds_min[axis] for axis in range(3))
    diagonal = math.sqrt(sum(component * component for component in extent))
    distance = max(200.0, diagonal * 0.6)
    camera_position = (center[0], center[1] - distance, center[2] + distance * 0.35)
    camera = {
        "name": "Overview Camera",
        "type": 0,
        "position": vec3(*camera_position),
        "target": vec3(*center),
        "fov_deg": 60.0,
        "ortho_w": max(20.0, max(extent[0], extent[1])),
        "ortho_h": max(15.0, extent[2]),
        "near_plane": 0.1,
        "far_plane": max(1000.0, diagonal * 3.0),
        "visible": True,
        "frozen": False,
    }
    editor = {
        "camera_target": vec3(*center),
        "camera_yaw": -0.75,
        "camera_pitch": 0.4,
        "camera_distance": round(distance, 6),
        "show_skybox": True,
        "show_wireframe": False,
    }
    return editor, camera


def make_light_desc(
    index: int,
    entity: dict[str, str],
    target_origins: dict[str, tuple[float, float, float]],
    intensity_scale: float,
    radius_scale: float,
    unit_scale: float,
) -> dict[str, object]:
    brightness = entity_brightness(entity)
    color = entity_color(entity)
    q3_position = parse_vec3(entity.get("origin"), (0.0, 0.0, 0.0))
    q3_direction = light_direction(entity, target_origins)
    position = q3_to_scene(q3_position, unit_scale)
    direction = q3_direction_to_scene(q3_direction)
    has_target = bool(entity.get("target"))
    label = "Spot" if has_target else "Omni"
    radius = scaled_radius(entity, brightness, radius_scale, unit_scale)
    result = {
        "name": f"Q3 {label} Light {index:03d}",
        "type": 1,
        "position": vec3(*position),
        "direction": vec3(*direction),
        "color": vec3(*color),
        "intensity": round(clamp(brightness / intensity_scale, 0.05, 20.0), 6),
        "radius": round(radius, 6),
        "enabled": True,
        "visible": True,
        "frozen": False,
    }
    result["q3"] = {
        "source": "bsp_entity_lump",
        "classname": entity.get("classname", "light"),
        "light": brightness,
        "radius": float(entity.get("radius", 0.0)) if entity.get("radius", "").replace(".", "", 1).isdigit() else None,
        "target": entity.get("target", ""),
        "targeted": has_target,
        "spawnflags": entity.get("spawnflags", ""),
        "angle": entity.get("angle", ""),
        "origin": entity.get("origin", ""),
        "color": entity.get("_color") or entity.get("color", ""),
    }
    return result


def make_scene(map_name: str, bsp_data: bytes, args: argparse.Namespace) -> tuple[dict[str, object], dict[str, int]]:
    entities = parse_entities(bsp_data)
    target_origins = {
        entity["targetname"]: parse_vec3(entity.get("origin"), (0.0, 0.0, 0.0))
        for entity in entities
        if entity.get("targetname") and entity.get("origin")
    }
    light_entities = [entity for entity in entities if entity.get("classname") == "light" and entity.get("origin")]
    lights = [
        make_light_desc(index, entity, target_origins, args.intensity_scale, args.radius_scale, args.unit_scale)
        for index, entity in enumerate(light_entities)
    ]
    q3_bounds_min, q3_bounds_max = bsp_bounds(bsp_data)
    bounds_min, bounds_max = q3_bounds_to_scene(q3_bounds_min, q3_bounds_max, args.unit_scale)
    editor, camera = make_editor_state(bounds_min, bounds_max)
    scene = {
        "version": 1,
        "collision": f"Scenes/Q3/{map_name}.t8q3clip",
        "editor": editor,
        "objects": [
            {
                "name": map_name,
                "mesh": f"Models/Q3/{map_name}.glb",
                "position": vec3(0.0, 0.0, 0.0),
                "rotation": vec3(0.0, 0.0, 0.0),
                "scale": vec3(args.unit_scale, args.unit_scale, args.unit_scale),
                "visible": True,
                "frozen": False,
                "show_wire": False,
            }
        ],
        "cameras": [camera],
        "lights": lights,
    }
    targeted = sum(1 for entity in light_entities if entity.get("target"))
    explicit_radius = sum(1 for entity in light_entities if entity.get("radius"))
    return scene, {
        "entities": len(entities),
        "lights": len(light_entities),
        "targeted_lights": targeted,
        "explicit_radius_lights": explicit_radius,
    }


def export_scenes(args: argparse.Namespace) -> None:
    q3_root = Path(args.q3_root).resolve()
    rtx_root = Path(args.rtx_root).resolve() if args.rtx_root else None
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    archive_index = build_archive_index(list_pk3_files(archive_roots(q3_root, rtx_root)))
    summary: list[dict[str, object]] = []

    for map_name in args.maps:
        bsp_path = f"maps/{map_name}.bsp".lower()
        if bsp_path not in archive_index:
            print(f"missing {bsp_path}", file=sys.stderr)
            summary.append({"map": map_name, "status": "missing"})
            continue
        bsp_data = read_archive_file(archive_index, bsp_path)
        scene, stats = make_scene(map_name, bsp_data, args)
        output_path = output_dir / f"{map_name}.t8scene"
        output_path.write_text(json.dumps(scene, indent=3), encoding="utf-8")
        record = {"map": map_name, "status": "ok", "file": str(output_path), **stats}
        summary.append(record)
        print(f"wrote {output_path} lights={stats['lights']} targeted={stats['targeted_lights']}")

    summary_path = output_dir / "_q3_bsp_light_scene_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"summary={summary_path}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Export Quake 3 BSP light entities to T8ditor scenes")
    parser.add_argument("--q3-root", required=True, help="Quake 3 Arena install root")
    parser.add_argument("--rtx-root", help="Optional RTX mod root for mod-supplied BSPs such as nv15")
    parser.add_argument("--output-dir", required=True, help="Directory for generated .t8scene files")
    parser.add_argument("--maps", nargs="+", default=DEFAULT_MAPS, help="Map names without .bsp")
    parser.add_argument("--intensity-scale", type=float, default=100.0, help="Q3 light value divided by this to produce T8ditor intensity")
    parser.add_argument("--radius-scale", type=float, default=8.0, help="Fallback radius multiplier for lights without an explicit radius")
    parser.add_argument(
        "--unit-scale",
        type=float,
        default=DEFAULT_UNIT_SCALE,
        help="Scale Q3 map units into engine scene units; default maps 32 Q3 units to 1 engine unit",
    )
    return parser


def main() -> None:
    export_scenes(build_parser().parse_args())


if __name__ == "__main__":
    main()
