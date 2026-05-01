#!/usr/bin/env python3
"""T850 snapshot comparison CLI and minimal stdio MCP server.

The MCP server is intentionally dependency-free so an MCP host can launch it
directly with Python. It implements the JSON-RPC methods needed by MCP clients:
initialize, tools/list, and tools/call.
"""

import argparse
import json
import os
import sys
from dataclasses import dataclass
from html import escape
from typing import Any, Callable


MCP_PROTOCOL_VERSION = "2024-11-05"


@dataclass
class PpmImage:
    width: int
    height: int
    max_value: int
    data: bytes


def _read_ppm_token(handle: Any) -> bytes:
    token = bytearray()
    while True:
        ch = handle.read(1)
        if not ch:
            if token:
                return bytes(token)
            raise ValueError("Unexpected end of PPM header")
        if ch == b"#":
            handle.readline()
            if token:
                return bytes(token)
            continue
        if ch.isspace():
            if token:
                return bytes(token)
            continue
        token.extend(ch)


def read_ppm(path: str) -> PpmImage:
    with open(path, "rb") as handle:
        magic = _read_ppm_token(handle)
        if magic != b"P6":
            raise ValueError(f"{path}: expected binary PPM P6, got {magic!r}")
        width = int(_read_ppm_token(handle))
        height = int(_read_ppm_token(handle))
        max_value = int(_read_ppm_token(handle))
        if max_value <= 0 or max_value > 255:
            raise ValueError(f"{path}: unsupported PPM max value {max_value}")
        data = handle.read()

    expected = width * height * 3
    if len(data) < expected:
        raise ValueError(f"{path}: expected {expected} RGB bytes, got {len(data)}")
    return PpmImage(width, height, max_value, data[:expected])


def write_ppm(path: str, width: int, height: int, data: bytes) -> None:
    with open(path, "wb") as handle:
        handle.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        handle.write(data)


def list_snapshot_targets(directory: str) -> dict[str, Any]:
    if not os.path.isdir(directory):
        raise ValueError(f"Snapshot directory does not exist: {directory}")

    targets: list[dict[str, Any]] = []
    for name in sorted(os.listdir(directory)):
        if not name.lower().endswith(".ppm"):
            continue
        path = os.path.join(directory, name)
        try:
            image = read_ppm(path)
            targets.append(
                {
                    "name": name,
                    "path": path,
                    "width": image.width,
                    "height": image.height,
                    "bytes": len(image.data),
                }
            )
        except ValueError as exc:
            targets.append({"name": name, "path": path, "error": str(exc)})

    snapshot_json = os.path.join(directory, "snapshot.json")
    metadata = None
    if os.path.isfile(snapshot_json):
        with open(snapshot_json, "r", encoding="utf-8") as handle:
            metadata = json.load(handle)

    return {"directory": directory, "targets": targets, "metadata": metadata}


def _target_path(directory: str, target: str) -> str:
    safe_target = os.path.basename(target)
    path = os.path.join(directory, safe_target)
    if not safe_target.lower().endswith(".ppm"):
        path += ".ppm"
    return path


def compare_frame(reference_dir: str, candidate_dir: str, target: str, tolerance: int = 0) -> dict[str, Any]:
    reference_path = _target_path(reference_dir, target)
    candidate_path = _target_path(candidate_dir, target)
    reference = read_ppm(reference_path)
    candidate = read_ppm(candidate_path)

    result: dict[str, Any] = {
        "target": os.path.basename(reference_path),
        "reference": reference_path,
        "candidate": candidate_path,
        "tolerance": tolerance,
    }

    if (reference.width, reference.height) != (candidate.width, candidate.height):
        result.update(
            {
                "status": "size_mismatch",
                "reference_size": [reference.width, reference.height],
                "candidate_size": [candidate.width, candidate.height],
            }
        )
        return result

    total_pixels = reference.width * reference.height
    diff_pixels = 0
    max_channel_delta = 0
    total_channel_delta = 0
    total_pixel_delta = 0
    luminance_delta = 0.0
    channel_delta = [0, 0, 0]

    for offset in range(0, len(reference.data), 3):
        r0, g0, b0 = reference.data[offset], reference.data[offset + 1], reference.data[offset + 2]
        r1, g1, b1 = candidate.data[offset], candidate.data[offset + 1], candidate.data[offset + 2]
        deltas = [abs(r0 - r1), abs(g0 - g1), abs(b0 - b1)]
        pixel_delta = max(deltas)
        if pixel_delta > tolerance:
            diff_pixels += 1
            total_pixel_delta += pixel_delta
        max_channel_delta = max(max_channel_delta, pixel_delta)
        for index, delta in enumerate(deltas):
            channel_delta[index] += delta
            total_channel_delta += delta
        lum0 = (0.2126 * r0) + (0.7152 * g0) + (0.0722 * b0)
        lum1 = (0.2126 * r1) + (0.7152 * g1) + (0.0722 * b1)
        luminance_delta += abs(lum0 - lum1)

    total_channels = total_pixels * 3
    result.update(
        {
            "status": "different" if diff_pixels else "match",
            "width": reference.width,
            "height": reference.height,
            "total_pixels": total_pixels,
            "diff_pixels": diff_pixels,
            "diff_percent": (100.0 * diff_pixels / total_pixels) if total_pixels else 0.0,
            "max_channel_delta": max_channel_delta,
            "avg_changed_pixel_delta": (total_pixel_delta / diff_pixels) if diff_pixels else 0.0,
            "avg_channel_delta": (total_channel_delta / total_channels) if total_channels else 0.0,
            "avg_luminance_delta": (luminance_delta / total_pixels) if total_pixels else 0.0,
            "avg_channel_delta_rgb": [
                (channel_delta[index] / total_pixels) if total_pixels else 0.0 for index in range(3)
            ],
        }
    )
    return result


def compare_snapshots(reference_dir: str, candidate_dir: str, tolerance: int = 0) -> dict[str, Any]:
    reference_targets = {
        target["name"]
        for target in list_snapshot_targets(reference_dir)["targets"]
        if "error" not in target
    }
    candidate_targets = {
        target["name"]
        for target in list_snapshot_targets(candidate_dir)["targets"]
        if "error" not in target
    }

    common = sorted(reference_targets & candidate_targets)
    comparisons = [compare_frame(reference_dir, candidate_dir, target, tolerance) for target in common]
    return {
        "reference_dir": reference_dir,
        "candidate_dir": candidate_dir,
        "tolerance": tolerance,
        "common_targets": common,
        "missing_in_candidate": sorted(reference_targets - candidate_targets),
        "missing_in_reference": sorted(candidate_targets - reference_targets),
        "comparisons": comparisons,
        "summary": _summarize_comparisons(comparisons),
    }


def _summarize_comparisons(comparisons: list[dict[str, Any]]) -> dict[str, Any]:
    changed = [item for item in comparisons if item.get("diff_pixels", 0) > 0]
    worst = sorted(
        changed,
        key=lambda item: (item.get("diff_percent", 0.0), item.get("max_channel_delta", 0)),
        reverse=True,
    )[:5]
    return {
        "targets_compared": len(comparisons),
        "targets_changed": len(changed),
        "worst_targets": [
            {
                "target": item.get("target"),
                "diff_percent": item.get("diff_percent"),
                "max_channel_delta": item.get("max_channel_delta"),
                "avg_luminance_delta": item.get("avg_luminance_delta"),
            }
            for item in worst
        ],
    }


def suggest_likely_cause(comparison: dict[str, Any]) -> list[str]:
    target = str(comparison.get("target", "")).lower()
    diff_percent = float(comparison.get("diff_percent", 0.0) or 0.0)
    max_delta = int(comparison.get("max_channel_delta", 0) or 0)
    lum_delta = float(comparison.get("avg_luminance_delta", 0.0) or 0.0)
    rgb_delta = comparison.get("avg_channel_delta_rgb") or [0.0, 0.0, 0.0]
    suggestions: list[str] = []

    if comparison.get("status") == "size_mismatch":
        return ["Snapshot target dimensions differ; verify resolution, render scale, and dump timing."]
    if diff_percent <= 0.0:
        return ["No pixel differences above the selected tolerance."]
    if "normal" in target:
        suggestions.append("Normal target changed; inspect tangent space, normal map decode, or TBN basis changes.")
    if "depth" in target:
        suggestions.append("Depth target changed; inspect projection matrices, depth compare state, clears, or pass ordering.")
    if "albedo" in target or "color" in target:
        suggestions.append("Color target changed; inspect material textures, UV selection, color space, or sampler state.")
    if "pbr" in target or "rough" in target or "metal" in target:
        suggestions.append("PBR target changed; inspect metallic/roughness/occlusion material packing.")
    if "back" in target or "final" in target or "hdr" in target:
        suggestions.append("Final output changed; inspect lighting, exposure, tone mapping, bloom, or post-processing.")
    if diff_percent > 75.0 and max_delta < 8:
        suggestions.append("Large low-amplitude drift; likely precision, color conversion, or rounding differences.")
    if diff_percent < 5.0 and max_delta > 64:
        suggestions.append("Sparse high-amplitude differences; likely edge rasterization, depth precision, or missing draw calls.")
    if lum_delta > 12.0:
        suggestions.append("Average luminance moved significantly; inspect exposure, light intensity, or HDR/tone-map changes.")
    if len(rgb_delta) == 3 and max(rgb_delta) > (min(rgb_delta) * 2.0 + 1.0):
        suggestions.append("One color channel dominates the diff; inspect channel swizzles or normal-map green-channel handling.")

    if not suggestions:
        suggestions.append("Differences are present; inspect the changed render target first, then walk forward through dependent passes.")
    return suggestions


def analyze_artifacts(
    reference_dir: str,
    candidate_dir: str,
    target: str | None = None,
    tolerance: int = 0,
) -> dict[str, Any]:
    if target:
        comparisons = [compare_frame(reference_dir, candidate_dir, target, tolerance)]
    else:
        comparisons = compare_snapshots(reference_dir, candidate_dir, tolerance)["comparisons"]

    findings = []
    for comparison in comparisons:
        if comparison.get("diff_pixels", 0) <= 0 and comparison.get("status") != "size_mismatch":
            continue
        findings.append(
            {
                "target": comparison.get("target"),
                "status": comparison.get("status"),
                "diff_percent": comparison.get("diff_percent"),
                "max_channel_delta": comparison.get("max_channel_delta"),
                "suggestions": suggest_likely_cause(comparison),
            }
        )

    return {
        "reference_dir": reference_dir,
        "candidate_dir": candidate_dir,
        "tolerance": tolerance,
        "findings": findings,
    }


def _heatmap_name(target: str) -> str:
    base = os.path.splitext(os.path.basename(target))[0]
    safe = "".join(ch if ch.isalnum() or ch in ("-", "_") else "_" for ch in base)
    return f"{safe}_diff.ppm"


def _write_heatmap(reference_dir: str, candidate_dir: str, target: str, output_dir: str) -> str | None:
    reference = read_ppm(_target_path(reference_dir, target))
    candidate = read_ppm(_target_path(candidate_dir, target))
    if (reference.width, reference.height) != (candidate.width, candidate.height):
        return None
    heatmap = bytearray()
    for offset in range(0, len(reference.data), 3):
        delta = max(
            abs(reference.data[offset] - candidate.data[offset]),
            abs(reference.data[offset + 1] - candidate.data[offset + 1]),
            abs(reference.data[offset + 2] - candidate.data[offset + 2]),
        )
        heatmap.extend((delta, 0, 255 - delta))
    path = os.path.join(output_dir, _heatmap_name(target))
    write_ppm(path, reference.width, reference.height, bytes(heatmap))
    return path


def generate_visual_report(
    reference_dir: str,
    candidate_dir: str,
    output_dir: str,
    target: str | None = None,
    tolerance: int = 0,
) -> dict[str, Any]:
    os.makedirs(output_dir, exist_ok=True)
    if target:
        comparisons = [compare_frame(reference_dir, candidate_dir, target, tolerance)]
    else:
        comparisons = compare_snapshots(reference_dir, candidate_dir, tolerance)["comparisons"]

    heatmaps: dict[str, str] = {}
    for comparison in comparisons:
        if comparison.get("status") == "size_mismatch":
            continue
        heatmap_path = _write_heatmap(reference_dir, candidate_dir, str(comparison["target"]), output_dir)
        if heatmap_path:
            heatmaps[str(comparison["target"])] = heatmap_path

    report_path = os.path.join(output_dir, "snapshot_report.html")
    with open(report_path, "w", encoding="utf-8") as handle:
        handle.write("<!doctype html><meta charset=\"utf-8\"><title>T850 Snapshot Report</title>")
        handle.write("<style>body{font-family:sans-serif}table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:4px 8px}</style>")
        handle.write("<h1>T850 Snapshot Report</h1>")
        handle.write(f"<p><b>Reference:</b> {escape(reference_dir)}<br><b>Candidate:</b> {escape(candidate_dir)}</p>")
        handle.write("<table><tr><th>Target</th><th>Status</th><th>Diff %</th><th>Max Delta</th><th>Avg Luma</th><th>Heatmap</th><th>Likely cause</th></tr>")
        for comparison in comparisons:
            heatmap = heatmaps.get(str(comparison.get("target")), "")
            heatmap_link = f"<a href=\"{escape(os.path.basename(heatmap))}\">heatmap</a>" if heatmap else ""
            causes = "<br>".join(escape(item) for item in suggest_likely_cause(comparison))
            handle.write(
                "<tr>"
                f"<td>{escape(str(comparison.get('target', '')))}</td>"
                f"<td>{escape(str(comparison.get('status', '')))}</td>"
                f"<td>{float(comparison.get('diff_percent', 0.0) or 0.0):.3f}</td>"
                f"<td>{comparison.get('max_channel_delta', '')}</td>"
                f"<td>{float(comparison.get('avg_luminance_delta', 0.0) or 0.0):.3f}</td>"
                f"<td>{heatmap_link}</td>"
                f"<td>{causes}</td>"
                "</tr>"
            )
        handle.write("</table>")

    return {"report": report_path, "heatmaps": heatmaps, "comparisons": comparisons}


def _json_text(value: Any) -> dict[str, Any]:
    return {"content": [{"type": "text", "text": json.dumps(value, indent=2)}]}


def _tool_schemas() -> list[dict[str, Any]]:
    return [
        {
            "name": "list_snapshot_targets",
            "description": "List PPM render-target dumps and optional snapshot metadata in a T850 snapshot directory.",
            "inputSchema": {
                "type": "object",
                "properties": {"directory": {"type": "string"}},
                "required": ["directory"],
            },
        },
        {
            "name": "compare_frame",
            "description": "Compare one PPM render target between two snapshot directories.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "reference_dir": {"type": "string"},
                    "candidate_dir": {"type": "string"},
                    "target": {"type": "string"},
                    "tolerance": {"type": "integer", "default": 0},
                },
                "required": ["reference_dir", "candidate_dir", "target"],
            },
        },
        {
            "name": "compare_snapshots",
            "description": "Compare every matching PPM render target between two T850 snapshot directories.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "reference_dir": {"type": "string"},
                    "candidate_dir": {"type": "string"},
                    "tolerance": {"type": "integer", "default": 0},
                },
                "required": ["reference_dir", "candidate_dir"],
            },
        },
        {
            "name": "analyze_artifacts",
            "description": "Compare targets and return heuristic artifact findings with likely causes.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "reference_dir": {"type": "string"},
                    "candidate_dir": {"type": "string"},
                    "target": {"type": "string"},
                    "tolerance": {"type": "integer", "default": 0},
                },
                "required": ["reference_dir", "candidate_dir"],
            },
        },
        {
            "name": "generate_visual_report",
            "description": "Generate an HTML report and PPM diff heatmaps for snapshot comparisons.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "reference_dir": {"type": "string"},
                    "candidate_dir": {"type": "string"},
                    "output_dir": {"type": "string"},
                    "target": {"type": "string"},
                    "tolerance": {"type": "integer", "default": 0},
                },
                "required": ["reference_dir", "candidate_dir", "output_dir"],
            },
        },
        {
            "name": "suggest_likely_cause",
            "description": "Return likely causes from a compare_frame result object.",
            "inputSchema": {
                "type": "object",
                "properties": {"comparison": {"type": "object"}},
                "required": ["comparison"],
            },
        },
    ]


TOOLS: dict[str, Callable[..., Any]] = {
    "list_snapshot_targets": list_snapshot_targets,
    "compare_frame": compare_frame,
    "compare_snapshots": compare_snapshots,
    "analyze_artifacts": analyze_artifacts,
    "generate_visual_report": generate_visual_report,
    "suggest_likely_cause": suggest_likely_cause,
}


def _handle_request(request: dict[str, Any]) -> dict[str, Any] | None:
    request_id = request.get("id")
    method = request.get("method")
    params = request.get("params") or {}

    if request_id is None:
        return None

    try:
        if method == "initialize":
            result = {
                "protocolVersion": MCP_PROTOCOL_VERSION,
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "t850-snapshot", "version": "1.0.0"},
            }
        elif method == "tools/list":
            result = {"tools": _tool_schemas()}
        elif method == "tools/call":
            name = params.get("name")
            arguments = params.get("arguments") or {}
            if name not in TOOLS:
                raise ValueError(f"Unknown tool: {name}")
            result = _json_text(TOOLS[name](**arguments))
        else:
            return {
                "jsonrpc": "2.0",
                "id": request_id,
                "error": {"code": -32601, "message": f"Method not found: {method}"},
            }
        return {"jsonrpc": "2.0", "id": request_id, "result": result}
    except Exception as exc:
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "error": {"code": -32000, "message": str(exc)},
        }


def serve_stdio() -> None:
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            request = json.loads(line)
            response = _handle_request(request)
        except Exception as exc:
            response = {
                "jsonrpc": "2.0",
                "id": None,
                "error": {"code": -32700, "message": str(exc)},
            }
        if response is not None:
            sys.stdout.write(json.dumps(response, separators=(",", ":")) + "\n")
            sys.stdout.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("serve", help="Run as a stdio MCP server.")

    list_parser = subparsers.add_parser("list", help="List snapshot targets.")
    list_parser.add_argument("directory")

    compare_parser = subparsers.add_parser("compare-frame", help="Compare one render target.")
    compare_parser.add_argument("reference_dir")
    compare_parser.add_argument("candidate_dir")
    compare_parser.add_argument("target")
    compare_parser.add_argument("--tolerance", type=int, default=0)

    snapshots_parser = subparsers.add_parser("compare-snapshots", help="Compare all matching render targets.")
    snapshots_parser.add_argument("reference_dir")
    snapshots_parser.add_argument("candidate_dir")
    snapshots_parser.add_argument("--tolerance", type=int, default=0)

    analyze_parser = subparsers.add_parser("analyze-artifacts", help="Return heuristic artifact findings.")
    analyze_parser.add_argument("reference_dir")
    analyze_parser.add_argument("candidate_dir")
    analyze_parser.add_argument("--target")
    analyze_parser.add_argument("--tolerance", type=int, default=0)

    report_parser = subparsers.add_parser("generate-report", help="Generate HTML report and heatmaps.")
    report_parser.add_argument("reference_dir")
    report_parser.add_argument("candidate_dir")
    report_parser.add_argument("output_dir")
    report_parser.add_argument("--target")
    report_parser.add_argument("--tolerance", type=int, default=0)

    args = parser.parse_args()
    if args.command == "serve":
        serve_stdio()
        return 0
    if args.command == "list":
        result = list_snapshot_targets(args.directory)
    elif args.command == "compare-frame":
        result = compare_frame(args.reference_dir, args.candidate_dir, args.target, args.tolerance)
    elif args.command == "compare-snapshots":
        result = compare_snapshots(args.reference_dir, args.candidate_dir, args.tolerance)
    elif args.command == "analyze-artifacts":
        result = analyze_artifacts(args.reference_dir, args.candidate_dir, args.target, args.tolerance)
    elif args.command == "generate-report":
        result = generate_visual_report(args.reference_dir, args.candidate_dir, args.output_dir, args.target, args.tolerance)
    else:
        parser.error(f"Unknown command: {args.command}")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
