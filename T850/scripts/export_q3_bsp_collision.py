#!/usr/bin/env python3
"""Export Quake 3 BSP solid/player-clip brushes and patch facets into T850 character collision files."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

from convert_q3_bsp_to_glb import (
    FACE_PATCH,
    Vertex,
    archive_roots,
    bezier_vertex,
    build_archive_index,
    list_pk3_files,
    parse_bsp_vertices,
    read_archive_file,
)
from export_q3_bsp_lights_to_t8scene import (
    DEFAULT_MAPS,
    DEFAULT_UNIT_SCALE,
    parse_entities,
    parse_vec3,
    q3_bounds_to_scene,
    q3_to_scene,
    vec3,
)


Q3_BSP_LUMP_TEXTURES = 1
Q3_BSP_LUMP_PLANES = 2
Q3_BSP_LUMP_MODELS = 7
Q3_BSP_LUMP_BRUSHES = 8
Q3_BSP_LUMP_BRUSHSIDES = 9

Q3_AAS_LUMP_AREASETTINGS = 8
Q3_AAS_LUMP_REACHABILITY = 9

Q3_CONTENTS_SOLID = 0x00000001
Q3_CONTENTS_PLAYERCLIP = 0x00010000
Q3_PLAYER_COLLISION_CONTENTS = Q3_CONTENTS_SOLID | Q3_CONTENTS_PLAYERCLIP
Q3_DEFAULT_GRAVITY = 800.0

Q3_AAS_IDENT = (ord("S") << 24) + (ord("A") << 16) + (ord("A") << 8) + ord("E")
Q3_AAS_VERSION = 5
Q3_AAS_REACHABILITY_SIZE = 44
Q3_AAS_AREA_SETTINGS_SIZE = 28

TRAVELTYPE_MASK = 0xFFFFFF
TRAVEL_TYPE_NAMES = {
    1: "invalid",
    2: "walk",
    3: "crouch",
    4: "barrier_jump",
    5: "jump",
    6: "ladder",
    7: "walk_off_ledge",
    8: "swim",
    9: "water_jump",
    10: "teleport",
    11: "elevator",
    12: "rocket_jump",
    13: "bfg_jump",
    14: "grapple_hook",
    15: "double_jump",
    16: "ramp_jump",
    17: "strafe_jump",
    18: "jump_pad",
    19: "func_bob",
}


def q3_normal_to_scene(value: tuple[float, float, float]) -> tuple[float, float, float]:
    return (value[0], value[2], value[1])


def dot3(lhs: tuple[float, float, float], rhs: tuple[float, float, float]) -> float:
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2]


def sub3(lhs: tuple[float, float, float], rhs: tuple[float, float, float]) -> tuple[float, float, float]:
    return (lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2])


def cross3(lhs: tuple[float, float, float], rhs: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0],
    )


def normalize3(value: tuple[float, float, float]) -> tuple[float, float, float] | None:
    length = math.sqrt(dot3(value, value))
    if length <= 0.000001:
        return None
    return (value[0] / length, value[1] / length, value[2] / length)


def plane_from_normal_point(
    normal: tuple[float, float, float],
    point: tuple[float, float, float],
) -> dict[str, object]:
    return {
        "normal": vec3(*normal),
        "dist": round(float(dot3(normal, point)), 6),
    }


def q3_velocity_to_scene(value: tuple[float, float, float], unit_scale: float) -> tuple[float, float, float]:
    return (value[0] * unit_scale, value[2] * unit_scale, value[1] * unit_scale)


def parse_bsp_models(bsp_data: bytes) -> list[dict[str, object]]:
    lumps = [struct.unpack_from("<II", bsp_data, 8 + lump_index * 8) for lump_index in range(17)]
    model_offset, model_length = lumps[Q3_BSP_LUMP_MODELS]
    models: list[dict[str, object]] = []
    for model_index in range(model_length // 40):
        offset = model_offset + model_index * 40
        mins = struct.unpack_from("<3f", bsp_data, offset)
        maxs = struct.unpack_from("<3f", bsp_data, offset + 12)
        first_face, num_faces, first_brush, num_brushes = struct.unpack_from("<4i", bsp_data, offset + 24)
        models.append({
            "mins": mins,
            "maxs": maxs,
            "first_face": first_face,
            "num_faces": num_faces,
            "first_brush": first_brush,
            "num_brushes": num_brushes,
        })
    return models


def inline_model_index(model_name: str | None) -> int | None:
    if not model_name or not model_name.startswith("*"):
        return None
    try:
        return int(model_name[1:])
    except ValueError:
        return None


def trigger_brush_indices(bsp_data: bytes) -> set[int]:
    entities = parse_entities(bsp_data)
    models = parse_bsp_models(bsp_data)
    excluded: set[int] = set()
    for entity in entities:
        classname = entity.get("classname", "")
        if not classname.startswith("trigger_"):
            continue

        model_index = inline_model_index(entity.get("model"))
        if model_index is None or model_index < 0 or model_index >= len(models):
            continue

        model = models[model_index]
        first_brush = int(model["first_brush"])
        num_brushes = int(model["num_brushes"])
        excluded.update(range(first_brush, first_brush + num_brushes))
    return excluded


def jump_pad_velocity_q3(
    origin: tuple[float, float, float],
    target: tuple[float, float, float],
    gravity: float = Q3_DEFAULT_GRAVITY,
) -> tuple[float, float, float] | None:
    height = target[2] - origin[2]
    if height <= 0.0 or gravity <= 0.0:
        return None

    time = math.sqrt(height / (0.5 * gravity))
    if time <= 0.0:
        return None

    dx = target[0] - origin[0]
    dy = target[1] - origin[1]
    horizontal_dist = math.hypot(dx, dy)
    if horizontal_dist <= 0.000001:
        return (0.0, 0.0, time * gravity)

    horizontal_speed = horizontal_dist / time
    return (
        dx / horizontal_dist * horizontal_speed,
        dy / horizontal_dist * horizontal_speed,
        time * gravity,
    )


def parse_jump_pads(bsp_data: bytes, unit_scale: float) -> list[dict[str, object]]:
    entities = parse_entities(bsp_data)
    models = parse_bsp_models(bsp_data)
    targets = {
        entity["targetname"]: parse_vec3(entity.get("origin"), (0.0, 0.0, 0.0))
        for entity in entities
        if entity.get("targetname") and entity.get("origin")
    }

    jump_pads: list[dict[str, object]] = []
    for entity_id, entity in enumerate(entities, start=1):
        if entity.get("classname") != "trigger_push":
            continue

        model_index = inline_model_index(entity.get("model"))
        target_name = entity.get("target")
        target = targets.get(target_name or "")
        if model_index is None or model_index < 0 or model_index >= len(models) or target is None:
            continue

        model = models[model_index]
        entity_origin = parse_vec3(entity.get("origin"), (0.0, 0.0, 0.0))
        q3_mins = tuple(float(model["mins"][axis]) + entity_origin[axis] for axis in range(3))
        q3_maxs = tuple(float(model["maxs"][axis]) + entity_origin[axis] for axis in range(3))
        q3_center = tuple((q3_mins[axis] + q3_maxs[axis]) * 0.5 for axis in range(3))
        q3_velocity = jump_pad_velocity_q3(q3_center, target)
        if q3_velocity is None:
            continue

        scene_mins, scene_maxs = q3_bounds_to_scene(q3_mins, q3_maxs, unit_scale)
        jump_pads.append({
            "entity_id": entity_id,
            "model": entity.get("model", ""),
            "target": target_name or "",
            "mins": vec3(*scene_mins),
            "maxs": vec3(*scene_maxs),
            "target_position": vec3(*q3_to_scene(target, unit_scale)),
            "velocity": vec3(*q3_velocity_to_scene(q3_velocity, unit_scale)),
        })
    return jump_pads


def parse_aas_reachabilities(aas_data: bytes, unit_scale: float) -> list[dict[str, object]]:
    if len(aas_data) < 12 + 14 * 8:
        raise ValueError("AAS data is too small")

    header_size = 12 + 14 * 8
    header = bytearray(aas_data[:header_size])
    ident, version = struct.unpack_from("<II", header, 0)
    if ident != Q3_AAS_IDENT:
        raise ValueError(f"unexpected AAS ident 0x{ident:08x}")
    if version != Q3_AAS_VERSION:
        raise ValueError(f"unsupported AAS version {version}")

    for index in range(8, header_size):
        header[index] ^= ((index - 8) * 119) & 0xFF

    lumps = [struct.unpack_from("<II", header, 12 + lump_index * 8) for lump_index in range(14)]
    area_settings_offset, area_settings_length = lumps[Q3_AAS_LUMP_AREASETTINGS]
    reachability_offset, reachability_length = lumps[Q3_AAS_LUMP_REACHABILITY]

    reachability_source_areas: dict[int, int] = {}
    area_count = area_settings_length // Q3_AAS_AREA_SETTINGS_SIZE
    for area_index in range(area_count):
        offset = area_settings_offset + area_index * Q3_AAS_AREA_SETTINGS_SIZE
        if offset + Q3_AAS_AREA_SETTINGS_SIZE > len(aas_data):
            break
        (
            _contents,
            _area_flags,
            _presence_type,
            _cluster,
            _cluster_area_num,
            num_reachable_areas,
            first_reachable_area,
        ) = struct.unpack_from("<7i", aas_data, offset)
        for reach_index in range(first_reachable_area, first_reachable_area + num_reachable_areas):
            reachability_source_areas[reach_index] = area_index

    reachabilities: list[dict[str, object]] = []
    reachability_count = reachability_length // Q3_AAS_REACHABILITY_SIZE
    for reach_index in range(reachability_count):
        offset = reachability_offset + reach_index * Q3_AAS_REACHABILITY_SIZE
        if offset + Q3_AAS_REACHABILITY_SIZE > len(aas_data):
            break
        (
            target_area,
            face_num,
            edge_num,
            start_x,
            start_y,
            start_z,
            end_x,
            end_y,
            end_z,
            travel_type,
            travel_time,
        ) = struct.unpack_from("<3i3f3fiHxx", aas_data, offset)

        travel_type_id = travel_type & TRAVELTYPE_MASK
        travel_type_name = TRAVEL_TYPE_NAMES.get(travel_type_id, f"unknown_{travel_type_id}")
        start = q3_to_scene((start_x, start_y, start_z), unit_scale)
        end = q3_to_scene((end_x, end_y, end_z), unit_scale)
        reachabilities.append({
            "source_area": reachability_source_areas.get(reach_index, 0),
            "target_area": target_area,
            "face": face_num,
            "edge": edge_num,
            "start": vec3(*start),
            "end": vec3(*end),
            "travel_type": travel_type_name,
            "travel_type_id": travel_type_id,
            "travel_flags": travel_type & ~TRAVELTYPE_MASK,
            "travel_time": int(travel_time),
        })

    return reachabilities


def parse_bsp_collision(
    bsp_data: bytes,
    unit_scale: float,
    excluded_brush_indices: set[int] | None = None,
) -> list[dict[str, object]]:
    lumps = [struct.unpack_from("<II", bsp_data, 8 + lump_index * 8) for lump_index in range(17)]
    excluded_brush_indices = excluded_brush_indices or set()

    texture_offset, texture_length = lumps[Q3_BSP_LUMP_TEXTURES]
    texture_contents: list[int] = []
    for texture_index in range(texture_length // 72):
        offset = texture_offset + texture_index * 72
        _, _, contents = struct.unpack_from("<64sii", bsp_data, offset)
        texture_contents.append(contents)

    plane_offset, plane_length = lumps[Q3_BSP_LUMP_PLANES]
    planes: list[tuple[tuple[float, float, float], float]] = []
    for plane_index in range(plane_length // 16):
        offset = plane_offset + plane_index * 16
        normal = struct.unpack_from("<3f", bsp_data, offset)
        dist = struct.unpack_from("<f", bsp_data, offset + 12)[0]
        planes.append((q3_normal_to_scene(normal), dist * unit_scale))

    brushside_offset, brushside_length = lumps[Q3_BSP_LUMP_BRUSHSIDES]
    brushsides: list[tuple[int, int]] = []
    for side_index in range(brushside_length // 8):
        offset = brushside_offset + side_index * 8
        brushsides.append(struct.unpack_from("<ii", bsp_data, offset))

    brush_offset, brush_length = lumps[Q3_BSP_LUMP_BRUSHES]
    brushes: list[dict[str, object]] = []
    for brush_index in range(brush_length // 12):
        if brush_index in excluded_brush_indices:
            continue

        offset = brush_offset + brush_index * 12
        first_side, num_sides, texture_index = struct.unpack_from("<iii", bsp_data, offset)
        if num_sides < 4 or texture_index < 0 or texture_index >= len(texture_contents):
            continue

        contents = texture_contents[texture_index]
        if (contents & Q3_PLAYER_COLLISION_CONTENTS) == 0:
            continue

        brush_planes: list[dict[str, object]] = []
        for side_index in range(first_side, first_side + num_sides):
            if side_index < 0 or side_index >= len(brushsides):
                continue
            plane_index, _ = brushsides[side_index]
            if plane_index < 0 or plane_index >= len(planes):
                continue
            normal, dist = planes[plane_index]
            brush_planes.append({
                "normal": vec3(*normal),
                "dist": round(float(dist), 6),
            })

        if len(brush_planes) >= 4:
            brushes.append({
                "contents": contents,
                "planes": brush_planes,
            })

    return brushes


def make_patch_facet(
    first: Vertex,
    second: Vertex,
    third: Vertex,
    unit_scale: float,
) -> dict[str, object] | None:
    points = [
        q3_to_scene(first.position, unit_scale),
        q3_to_scene(second.position, unit_scale),
        q3_to_scene(third.position, unit_scale),
    ]

    normal = normalize3(cross3(sub3(points[1], points[0]), sub3(points[2], points[0])))
    if normal is None:
        return None

    averaged_normal = normalize3(tuple(
        q3_normal_to_scene(first.normal)[axis] +
        q3_normal_to_scene(second.normal)[axis] +
        q3_normal_to_scene(third.normal)[axis]
        for axis in range(3)
    ))
    if averaged_normal is not None and dot3(normal, averaged_normal) < 0.0:
        points[1], points[2] = points[2], points[1]
        normal = (-normal[0], -normal[1], -normal[2])

    borders: list[dict[str, object]] = []
    for edge_index in range(3):
        a = points[edge_index]
        b = points[(edge_index + 1) % 3]
        inside = points[(edge_index + 2) % 3]
        edge = sub3(b, a)
        border_normal = normalize3(cross3(edge, normal))
        if border_normal is None:
            return None
        border_dist = dot3(border_normal, a)
        if dot3(border_normal, inside) > border_dist:
            border_normal = (-border_normal[0], -border_normal[1], -border_normal[2])
        borders.append(plane_from_normal_point(border_normal, a))

    mins = tuple(min(point[axis] for point in points) for axis in range(3))
    maxs = tuple(max(point[axis] for point in points) for axis in range(3))
    return {
        "surface": plane_from_normal_point(normal, points[0]),
        "borders": borders,
        "mins": vec3(*mins),
        "maxs": vec3(*maxs),
    }


def parse_bsp_patch_facets(
    bsp_data: bytes,
    unit_scale: float,
    patch_subdivisions: int,
) -> list[dict[str, object]]:
    shader_names, source_vertices, _, faces = parse_bsp_vertices(bsp_data)
    lumps = [struct.unpack_from("<II", bsp_data, 8 + lump_index * 8) for lump_index in range(17)]

    texture_offset, texture_length = lumps[Q3_BSP_LUMP_TEXTURES]
    texture_contents: list[int] = []
    for texture_index in range(texture_length // 72):
        offset = texture_offset + texture_index * 72
        _, _, contents = struct.unpack_from("<64sii", bsp_data, offset)
        texture_contents.append(contents)

    subdivisions = max(1, int(patch_subdivisions))
    facets: list[dict[str, object]] = []
    for face in faces:
        shader_index = face[0]
        face_type = face[2]
        vertex_start = face[3]
        vertex_count = face[4]
        patch_width = face[-2]
        patch_height = face[-1]
        if face_type != FACE_PATCH or patch_width < 3 or patch_height < 3:
            continue
        if shader_index < 0 or shader_index >= len(shader_names) or shader_index >= len(texture_contents):
            continue
        if (texture_contents[shader_index] & Q3_PLAYER_COLLISION_CONTENTS) == 0:
            continue

        control = source_vertices[vertex_start:vertex_start + vertex_count]
        if len(control) < patch_width * patch_height:
            continue

        for y_base in range(0, patch_height - 2, 2):
            for x_base in range(0, patch_width - 2, 2):
                grid: list[list[Vertex]] = []
                for row_index in range(subdivisions + 1):
                    v_factor = row_index / subdivisions
                    row: list[Vertex] = []
                    for column_index in range(subdivisions + 1):
                        u_factor = column_index / subdivisions
                        row.append(bezier_vertex(control, patch_width, x_base, y_base, u_factor, v_factor))
                    grid.append(row)

                for row_index in range(subdivisions):
                    for column_index in range(subdivisions):
                        upper_left = grid[row_index][column_index]
                        upper_right = grid[row_index][column_index + 1]
                        lower_left = grid[row_index + 1][column_index]
                        lower_right = grid[row_index + 1][column_index + 1]
                        for triangle in (
                            (upper_left, lower_left, upper_right),
                            (upper_right, lower_left, lower_right),
                        ):
                            facet = make_patch_facet(*triangle, unit_scale)
                            if facet is not None:
                                facets.append(facet)

    return facets


def export_collision(args: argparse.Namespace) -> None:
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
        aas_path = f"maps/{map_name}.aas".lower()
        reachabilities: list[dict[str, object]] = []
        if not args.no_aas and aas_path in archive_index:
            try:
                reachabilities = parse_aas_reachabilities(read_archive_file(archive_index, aas_path), args.unit_scale)
            except ValueError as exc:
                print(f"warning: skipped {aas_path}: {exc}", file=sys.stderr)
        excluded_trigger_brushes = trigger_brush_indices(bsp_data)
        brushes = parse_bsp_collision(bsp_data, args.unit_scale, excluded_trigger_brushes)
        patch_facets = parse_bsp_patch_facets(bsp_data, args.unit_scale, args.patch_subdivisions)
        jump_pads = parse_jump_pads(bsp_data, args.unit_scale)
        clip = {
            "version": 3,
            "source": bsp_path,
            "aas_source": aas_path if reachabilities else "",
            "unit_scale": args.unit_scale,
            "contents_mask": Q3_PLAYER_COLLISION_CONTENTS,
            "brushes": brushes,
            "patch_facets": patch_facets,
            "jump_pads": jump_pads,
            "reachabilities": reachabilities,
        }
        output_path = output_dir / f"{map_name}.t8q3clip"
        output_path.write_text(json.dumps(clip, indent=2), encoding="utf-8")
        summary.append({
            "map": map_name,
            "status": "ok",
            "file": str(output_path),
            "brushes": len(brushes),
            "patch_facets": len(patch_facets),
            "patch_subdivisions": int(args.patch_subdivisions),
            "excluded_trigger_brushes": len(excluded_trigger_brushes),
            "jump_pads": len(jump_pads),
            "reachabilities": len(reachabilities),
        })
        print(
            f"wrote {output_path} brushes={len(brushes)} patch_facets={len(patch_facets)} "
            f"excluded_trigger_brushes={len(excluded_trigger_brushes)} jump_pads={len(jump_pads)} "
            f"reachabilities={len(reachabilities)}"
        )

    summary_path = output_dir / "_q3_bsp_collision_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"summary={summary_path}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Export Quake 3 BSP player collision brushes and patches to T850 .t8q3clip files")
    parser.add_argument("--q3-root", required=True, help="Quake 3 Arena install root")
    parser.add_argument("--rtx-root", help="Optional RTX mod root for mod-supplied BSPs such as nv15")
    parser.add_argument("--output-dir", required=True, help="Directory for generated .t8q3clip files")
    parser.add_argument("--maps", nargs="+", default=DEFAULT_MAPS, help="Map names without .bsp")
    parser.add_argument(
        "--patch-subdivisions",
        type=int,
        default=6,
        help="Uniform subdivisions per 3x3 Bezier patch cell for exported patch collision facets",
    )
    parser.add_argument(
        "--unit-scale",
        type=float,
        default=DEFAULT_UNIT_SCALE,
        help="Scale Q3 map units into engine scene units; default maps 32 Q3 units to 1 engine unit",
    )
    parser.add_argument(
        "--no-aas",
        action="store_true",
        help="Do not embed .aas reachability records even if maps/<map>.aas exists",
    )
    return parser


def main() -> None:
    export_collision(build_parser().parse_args())


if __name__ == "__main__":
    main()
