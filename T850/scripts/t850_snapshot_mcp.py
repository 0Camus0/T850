#!/usr/bin/env python3
"""T850 snapshot comparison CLI and minimal stdio MCP server.

The MCP server is intentionally dependency-free so an MCP host can launch it
directly with Python. It implements the JSON-RPC methods needed by MCP clients:
initialize, tools/list, and tools/call.
"""

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass
from html import escape
from typing import Any, Callable


MCP_PROTOCOL_VERSION = "2024-11-05"
BT709_LUMA = (0.2126, 0.7152, 0.0722)
# Treat one RGB channel as suspicious only when it is at least twice the least
# changed channel, with a small epsilon so near-zero noise does not dominate.
CHANNEL_DOMINANCE_RATIO = 2.0
CHANNEL_DOMINANCE_EPSILON = 1.0
REPORT_CSS = """
body { font-family: sans-serif; }
table { border-collapse: collapse; }
td, th { border: 1px solid #ccc; padding: 4px 8px; }
"""


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
        try:
            width = int(_read_ppm_token(handle))
            height = int(_read_ppm_token(handle))
            max_value = int(_read_ppm_token(handle))
        except ValueError as exc:
            raise ValueError(f"{path}: failed to parse PPM width, height, or max value") from exc
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
        lum0 = (BT709_LUMA[0] * r0) + (BT709_LUMA[1] * g0) + (BT709_LUMA[2] * b0)
        lum1 = (BT709_LUMA[0] * r1) + (BT709_LUMA[1] * g1) + (BT709_LUMA[2] * b1)
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
    max_rgb_delta = max(rgb_delta)
    channel_dominance_threshold = (min(rgb_delta) * CHANNEL_DOMINANCE_RATIO) + CHANNEL_DOMINANCE_EPSILON
    if max_rgb_delta > CHANNEL_DOMINANCE_EPSILON and max_rgb_delta > channel_dominance_threshold:
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
        handle.write("<!doctype html><html><head><meta charset=\"utf-8\"><title>T850 Snapshot Report</title>")
        handle.write(f"<style>{REPORT_CSS}</style></head><body>")
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
        handle.write("</table></body></html>")

    return {"report": report_path, "heatmaps": heatmaps, "comparisons": comparisons}


# ─────────────────────────────────────────────────────────────────────────────
# Render trace analysis (T850_RENDER_TRACE / T850_TRACE_GEOMETRY)
#
# trace.json schema (RenderTrace.h): a per-frame structured dump of the API
# state. Each tool below treats one snapshot directory as authoritative and
# the other as the candidate, mirroring the PPM compare contract above.
# ─────────────────────────────────────────────────────────────────────────────

TRACE_FILENAME = "trace.json"


def _load_trace(directory: str) -> dict[str, Any]:
    path = os.path.join(directory, TRACE_FILENAME)
    if not os.path.isfile(path):
        raise ValueError(f"No trace.json in {directory} (build with T850_RENDER_TRACE=ON)")
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _trace_event_histogram(trace: dict[str, Any]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for ev in trace.get("events", []):
        t = str(ev.get("type", ""))
        counts[t] = counts.get(t, 0) + 1
    return dict(sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])))


def _trace_shader_index(trace: dict[str, Any]) -> dict[int, dict[str, Any]]:
    return {int(s["id"]): s for s in trace.get("shaders", []) if "id" in s}


def _trace_buffer_index(trace: dict[str, Any]) -> dict[int, dict[str, Any]]:
    return {int(b["id"]): b for b in trace.get("buffers", []) if "id" in b}


def _trace_texture_index(trace: dict[str, Any]) -> dict[int, dict[str, Any]]:
    return {int(t["id"]): t for t in trace.get("textures", []) if "id" in t}


def _shader_label(shader: dict[str, Any] | None) -> str:
    if not shader:
        return "?"
    vs = str(shader.get("vs_name", ""))
    fs = str(shader.get("fs_name", ""))
    if vs and fs and vs != fs:
        return f"{vs}|{fs}"
    return vs or fs or f"shader#{shader.get('id')}"


def summarize_trace(directory: str) -> dict[str, Any]:
    """Top-level summary of one trace.json: counts, draws-by-shader, events."""
    trace = _load_trace(directory)
    draws = trace.get("draws", [])
    shaders = _trace_shader_index(trace)
    by_shader: dict[int, int] = {}
    for d in draws:
        sid = int(d.get("shader_id", -1))
        by_shader[sid] = by_shader.get(sid, 0) + 1
    draws_by_shader = sorted(
        ({"shader_id": sid, "count": cnt, "label": _shader_label(shaders.get(sid))}
         for sid, cnt in by_shader.items()),
        key=lambda x: (-x["count"], x["shader_id"]),
    )
    return {
        "directory": directory,
        "api": trace.get("api"),
        "frame": trace.get("frame"),
        "scene": trace.get("scene"),
        "timestamp": trace.get("timestamp"),
        "counts": {
            "textures": len(trace.get("textures", [])),
            "rts": len(trace.get("rts", [])),
            "shaders": len(trace.get("shaders", [])),
            "psos": len(trace.get("psos", [])),
            "buffers": len(trace.get("buffers", [])),
            "draws": len(draws),
            "events": len(trace.get("events", [])),
        },
        "draws_by_shader": draws_by_shader,
        "event_histogram": _trace_event_histogram(trace),
    }


def _draws_aligned(ref_trace: dict[str, Any], cand_trace: dict[str, Any]) -> tuple[list[dict[str, Any]], list[dict[str, Any]], int]:
    """Return (ref_draws, cand_draws, common_count). Alignment is positional —
    the engine drives identical draw orders across APIs because the snapshot
    is replayed deterministically from the same scene state."""
    ref = list(ref_trace.get("draws", []))
    cand = list(cand_trace.get("draws", []))
    return ref, cand, min(len(ref), len(cand))


def _shader_signature(trace: dict[str, Any]) -> dict[int, tuple]:
    """Map shader_id -> (pass, key_bits) so cross-API ids can be compared
    by logical identity instead of registration order."""
    out: dict[int, tuple] = {}
    for s in trace.get("shaders", []):
        sid = int(s.get("id", -1))
        if sid >= 0:
            out[sid] = (s.get("pass"), s.get("key_hex"))
    return out


def _texture_signature(trace: dict[str, Any]) -> dict[int, tuple]:
    """Map texture_id -> (name, width, height) for cross-API logical identity."""
    out: dict[int, tuple] = {}
    for t in trace.get("textures", []):
        tid = int(t.get("id", -1))
        if tid >= 0:
            out[tid] = (t.get("name"), t.get("width"), t.get("height"))
    return out


def _all_cb_hashes(trace: dict[str, Any]) -> set:
    """Collect every cbuffer hash bound across the entire trace. A draw that
    binds a CB whose hash appears in this set is using GPU-equivalent content
    even if the API rebound it at a different point in the frame."""
    out: set = set()
    for d in trace.get("draws", []):
        for c in d.get("cbuffers", []):
            h = c.get("hash")
            if h is not None:
                out.add(h)
    return out


def _cbs_present_in(draw: dict[str, Any], hashes: set) -> bool:
    """True iff every CB hash bound on this draw appears somewhere in `hashes`.
    Used to ask: 'did the ref API ever produce this content?' — which screens
    out cross-API binding-model noise (D3D12 root rebinds vs D3D11/Vulkan/GL
    slot reuse) from real divergences."""
    for c in draw.get("cbuffers", []):
        h = c.get("hash")
        if h is not None and h not in hashes:
            return False
    return True


def _normalized_tex_bindings(draw: dict[str, Any], tex_sig: dict[int, tuple]) -> frozenset:
    """Slot-agnostic set of (shader_name, stage, texture_signature) — captures
    'what shader-side variable saw which logical texture' across APIs.
    Ignores Vulkan's <dummy> placeholders (texture_id=-1) which exist only
    to satisfy descriptor-set fill requirements and never affect rendering."""
    return frozenset(
        (
            t.get("shader_name") or "",
            t.get("stage") or "",
            tex_sig.get(int(t.get("texture_id", -1)), ("?", 0, 0)),
        )
        for t in draw.get("textures", [])
        if int(t.get("texture_id", -1)) >= 0
    )


def _normalized_shader_label(label: str | None) -> str | None:
    """Strip language-specific filename extensions so HLSL and GLSL versions
    of the same logical shader pair compare equal. e.g.
    'VS_Mesh.hlsl|FS_Mesh.hlsl' and 'VS_Mesh.glsl|FS_Mesh.glsl' both
    normalize to 'VS_Mesh|FS_Mesh'."""
    if not label:
        return label
    parts = label.split("|")
    out = []
    for p in parts:
        for ext in (".hlsl", ".glsl", ".vert", ".frag", ".vsh", ".fsh"):
            if p.endswith(ext):
                p = p[: -len(ext)]
                break
        out.append(p)
    return "|".join(out)


def _summarize_buffer_diff(ref_buf: dict[str, Any] | None, cand_buf: dict[str, Any] | None) -> dict[str, Any]:
    if not ref_buf and not cand_buf:
        return {"present_in_ref": False, "present_in_cand": False}
    if not ref_buf:
        return {"present_in_ref": False, "present_in_cand": True}
    if not cand_buf:
        return {"present_in_ref": True, "present_in_cand": False}
    ref_updates = ref_buf.get("updates", [])
    cand_updates = cand_buf.get("updates", [])
    return {
        "kind": ref_buf.get("kind") or cand_buf.get("kind"),
        "ref_updates": len(ref_updates),
        "cand_updates": len(cand_updates),
        "ref_first_hash": ref_updates[0].get("hash") if ref_updates else None,
        "cand_first_hash": cand_updates[0].get("hash") if cand_updates else None,
        "ref_first_size": ref_updates[0].get("size") if ref_updates else None,
        "cand_first_size": cand_updates[0].get("size") if cand_updates else None,
        "first_hash_match": (
            ref_updates and cand_updates
            and ref_updates[0].get("hash") == cand_updates[0].get("hash")
        ),
    }


def compare_traces(reference_dir: str, candidate_dir: str) -> dict[str, Any]:
    """Structural comparison of two trace.json files. Reports counts, buffer
    hash matches, and a high-level per-draw divergence count without dumping
    every byte. Cross-API safe: shader_id and CB slots are normalized."""
    ref = _load_trace(reference_dir)
    cand = _load_trace(candidate_dir)
    ref_draws, cand_draws, common = _draws_aligned(ref, cand)
    ref_shaders = _shader_signature(ref)
    cand_shaders = _shader_signature(cand)
    ref_tex = _texture_signature(ref)
    cand_tex = _texture_signature(cand)

    diverging = 0
    diverging_by_kind: dict[str, int] = {}
    ref_all_hashes = _all_cb_hashes(ref)
    cand_all_hashes = _all_cb_hashes(cand)
    for i in range(common):
        rd, cd = ref_draws[i], cand_draws[i]
        kinds: list[str] = []
        rsig = ref_shaders.get(int(rd.get("shader_id", -1)), ("?", "?"))
        csig = cand_shaders.get(int(cd.get("shader_id", -1)), ("?", "?"))
        if rsig != csig:
            kinds.append("shader_logical")
        if int(rd.get("vertex_buffer_id", -1)) != int(cd.get("vertex_buffer_id", -1)):
            kinds.append("vb_id")
        if int(rd.get("index_buffer_id", -1)) != int(cd.get("index_buffer_id", -1)):
            kinds.append("ib_id")
        if int(rd.get("blend", -1)) != int(cd.get("blend", -1)):
            kinds.append("blend")
        if int(rd.get("depth", -1)) != int(cd.get("depth", -1)):
            kinds.append("depth")
        if int(rd.get("cull", -1)) != int(cd.get("cull", -1)):
            kinds.append("cull")
        # CB content: cumulative-set check. A CB hash bound on cand that never
        # appears anywhere in ref is a real divergence; otherwise the difference
        # is just per-API binding-model noise (D3D12 rebinds every draw via the
        # root signature; D3D11/Vulkan/GL keep state across draws).
        if not _cbs_present_in(cd, ref_all_hashes):
            kinds.append("cbuffer_content_unknown_to_ref")
        if not _cbs_present_in(rd, cand_all_hashes):
            kinds.append("cbuffer_content_unknown_to_cand")
        # Slot-agnostic texture compare: match by (shader_name, stage, tex sig).
        if _normalized_tex_bindings(rd, ref_tex) != _normalized_tex_bindings(cd, cand_tex):
            kinds.append("textures_logical")
        if int(rd.get("vertex_count", 0)) != int(cd.get("vertex_count", 0)):
            kinds.append("vertex_count")
        if int(rd.get("rt_id", -1)) != int(cd.get("rt_id", -1)):
            kinds.append("rt_id")
        if kinds:
            diverging += 1
            for k in kinds:
                diverging_by_kind[k] = diverging_by_kind.get(k, 0) + 1

    # Aggregate buffer hash matches (first update only — covers static pools).
    ref_buffers = _trace_buffer_index(ref)
    cand_buffers = _trace_buffer_index(cand)
    common_buffer_ids = sorted(set(ref_buffers) & set(cand_buffers))
    buffer_summary = []
    for bid in common_buffer_ids:
        diff = _summarize_buffer_diff(ref_buffers.get(bid), cand_buffers.get(bid))
        buffer_summary.append({"buffer_id": bid, **diff})

    return {
        "reference_dir": reference_dir,
        "candidate_dir": candidate_dir,
        "ref_api": ref.get("api"),
        "cand_api": cand.get("api"),
        "draws_ref": len(ref_draws),
        "draws_cand": len(cand_draws),
        "draws_common": common,
        "draws_diverging": diverging,
        "diverging_by_kind": dict(sorted(diverging_by_kind.items(), key=lambda kv: (-kv[1], kv[0]))),
        "buffers": buffer_summary,
    }


def _draw_state_dict(trace: dict[str, Any], draw: dict[str, Any], tex_sig: dict[int, tuple] | None = None) -> dict[str, Any]:
    """Build a normalized state view of one draw, hiding raw hex but keeping
    the addresses and hashes that matter for cross-API equivalence.
    Cross-API safe: shader_id is replaced by (pass, key); textures are sorted
    by (shader_name, stage); cbuffers are sorted by (size, hash) and the slot
    field is preserved as 'slot_in_api' for forensic visibility but ignored
    in equality (different APIs assign different binding slots/root indices)."""
    shaders = _trace_shader_index(trace)
    sid = int(draw.get("shader_id", -1))
    sh = shaders.get(sid, {})
    if tex_sig is None:
        tex_sig = _texture_signature(trace)
    sorted_tex = sorted(
        (t for t in draw.get("textures", []) if int(t.get("texture_id", -1)) >= 0),
        key=lambda t: (t.get("shader_name") or "", t.get("stage") or "", int(t.get("texture_id", -1))),
    )
    sorted_cbs = sorted(
        draw.get("cbuffers", []),
        key=lambda c: (int(c.get("size") or 0), c.get("hash") or 0),
    )
    return {
        "shader_pass": sh.get("pass"),
        "shader_key": sh.get("key_hex"),
        "shader_label": _normalized_shader_label(_shader_label(sh)),
        "rt_id": draw.get("rt_id"),
        "vertex_buffer_id": draw.get("vertex_buffer_id"),
        "vertex_buffer_version": draw.get("vertex_buffer_version"),
        "vb_stride": draw.get("vb_stride"),
        "index_buffer_id": draw.get("index_buffer_id"),
        "index_buffer_version": draw.get("index_buffer_version"),
        "ib_format": draw.get("ib_format"),
        "topology": draw.get("topology"),
        "blend": draw.get("blend"),
        "depth": draw.get("depth"),
        "cull": draw.get("cull"),
        "vertex_count": draw.get("vertex_count"),
        "start_index": draw.get("start_index"),
        "start_vertex": draw.get("start_vertex"),
        "context": {
            "mesh": draw.get("context_mesh"),
            "material": draw.get("context_material"),
            "entity": draw.get("context_entity"),
            "pass": draw.get("context_pass"),
        },
        "textures": [
            {
                "shader_name": t.get("shader_name"),
                "stage": t.get("stage"),
                "tex_sig": list(tex_sig.get(int(t.get("texture_id", -1)), ("?", 0, 0))),
            }
            for t in sorted_tex
        ],
        "cbuffers": [
            {
                "size": c.get("size"),
                "hash": c.get("hash"),
                "has_hex": bool(c.get("data_hex")),
            }
            for c in sorted_cbs
        ],
    }


def _diff_dicts(ref: dict[str, Any], cand: dict[str, Any], path: str = "") -> list[dict[str, Any]]:
    diffs: list[dict[str, Any]] = []
    for key in sorted(set(ref) | set(cand)):
        rv = ref.get(key)
        cv = cand.get(key)
        full = f"{path}.{key}" if path else key
        if isinstance(rv, dict) and isinstance(cv, dict):
            diffs.extend(_diff_dicts(rv, cv, full))
        elif isinstance(rv, list) and isinstance(cv, list):
            if len(rv) != len(cv):
                diffs.append({"path": full, "ref": f"<list len={len(rv)}>", "cand": f"<list len={len(cv)}>"})
            else:
                for i, (a, b) in enumerate(zip(rv, cv)):
                    if isinstance(a, dict) and isinstance(b, dict):
                        diffs.extend(_diff_dicts(a, b, f"{full}[{i}]"))
                    elif a != b:
                        diffs.append({"path": f"{full}[{i}]", "ref": a, "cand": b})
        elif rv != cv:
            diffs.append({"path": full, "ref": rv, "cand": cv})
    return diffs


def diff_draws(reference_dir: str, candidate_dir: str, max_results: int = 10,
               include_matching: bool = False) -> dict[str, Any]:
    """For each aligned draw, return a flat list of (path, ref, cand) tuples
    showing exactly which fields disagree. Use max_results to cap the noise
    when many draws diverge in similar ways."""
    ref = _load_trace(reference_dir)
    cand = _load_trace(candidate_dir)
    ref_draws, cand_draws, common = _draws_aligned(ref, cand)
    ref_tex = _texture_signature(ref)
    cand_tex = _texture_signature(cand)
    out: list[dict[str, Any]] = []
    for i in range(common):
        rd = _draw_state_dict(ref, ref_draws[i], ref_tex)
        cd = _draw_state_dict(cand, cand_draws[i], cand_tex)
        diffs = _diff_dicts(rd, cd)
        if diffs or include_matching:
            out.append({
                "draw_index": i,
                "ref_seq": ref_draws[i].get("seq"),
                "cand_seq": cand_draws[i].get("seq"),
                "shader": rd.get("shader_label"),
                "context": rd.get("context"),
                "diffs": diffs,
            })
            if len(out) >= max_results:
                break
    return {
        "reference_dir": reference_dir,
        "candidate_dir": candidate_dir,
        "ref_api": ref.get("api"),
        "cand_api": cand.get("api"),
        "draws_compared": common,
        "results": out,
        "truncated": len(out) >= max_results,
    }


def find_first_diverging_draw(reference_dir: str, candidate_dir: str,
                              start_at: int = 0) -> dict[str, Any]:
    """Walk aligned draws and return the first one whose normalized state
    differs. Use start_at to skip past known-good early draws and isolate
    the next regression."""
    ref = _load_trace(reference_dir)
    cand = _load_trace(candidate_dir)
    ref_draws, cand_draws, common = _draws_aligned(ref, cand)
    ref_tex = _texture_signature(ref)
    cand_tex = _texture_signature(cand)
    for i in range(start_at, common):
        rd = _draw_state_dict(ref, ref_draws[i], ref_tex)
        cd = _draw_state_dict(cand, cand_draws[i], cand_tex)
        diffs = _diff_dicts(rd, cd)
        if diffs:
            return {
                "reference_dir": reference_dir,
                "candidate_dir": candidate_dir,
                "draws_scanned": i + 1,
                "found": True,
                "draw_index": i,
                "shader": rd.get("shader_label"),
                "ref_seq": ref_draws[i].get("seq"),
                "cand_seq": cand_draws[i].get("seq"),
                "context": rd.get("context"),
                "diffs": diffs,
                "ref_state": rd,
                "cand_state": cd,
            }
    return {
        "reference_dir": reference_dir,
        "candidate_dir": candidate_dir,
        "draws_scanned": common,
        "found": False,
    }


def _hex_to_floats(hex_str: str) -> list[float]:
    """Decode tracer hex payload (lowercase, no separators) to float32 list.
    Trailing odd bytes are dropped."""
    raw = bytes.fromhex(hex_str)
    n = len(raw) // 4
    if n == 0:
        return []
    return list(struct.unpack(f"<{n}f", raw[:n * 4]))


def dump_cbuffer_hex(directory: str, draw_index: int, slot: int | None = None,
                     as_floats: bool = True) -> dict[str, Any]:
    """Decode the cbuffer slice the GPU saw for a specific draw. Requires
    T850_TRACE_GEOMETRY=ON at build time so data_hex is populated."""
    trace = _load_trace(directory)
    draws = trace.get("draws", [])
    if not (0 <= draw_index < len(draws)):
        raise ValueError(f"draw_index {draw_index} out of range [0,{len(draws)})")
    cbs = draws[draw_index].get("cbuffers", [])
    selected = [c for c in cbs if slot is None or int(c.get("slot", -1)) == slot]
    out = []
    for c in selected:
        hex_str = str(c.get("data_hex") or "")
        entry = {
            "slot": c.get("slot"),
            "buffer_id": c.get("buffer_id"),
            "size": c.get("size"),
            "version": c.get("update_version"),
            "hash": c.get("hash"),
            "has_hex": bool(hex_str),
            "byte_length": len(hex_str) // 2,
        }
        if hex_str:
            if as_floats:
                entry["floats"] = _hex_to_floats(hex_str)
            else:
                entry["hex"] = hex_str
        out.append(entry)
    return {
        "directory": directory,
        "draw_index": draw_index,
        "shader_id": draws[draw_index].get("shader_id"),
        "cbuffers": out,
    }


def diff_cbuffer_floats(reference_dir: str, candidate_dir: str, draw_index: int,
                        slot: int | None = None, max_diffs: int = 32,
                        epsilon: float = 1e-5) -> dict[str, Any]:
    """Float-level diff of a single draw's cbuffer slices between APIs.
    Each entry is (index, ref, cand, abs_delta) sorted by absolute delta
    descending so the largest divergences surface first.

    CBs are paired by SIZE (not slot) so that cross-API binding-model
    differences (D3D12 root signatures vs D3D11 slot reuse vs Vulkan
    descriptor sets) don't show up as false positives. Within a single
    draw, two CBs of different sizes are unambiguous; CBs of the same
    size are paired in sorted-hash order."""
    ref_dump = dump_cbuffer_hex(reference_dir, draw_index, slot, as_floats=True)
    cand_dump = dump_cbuffer_hex(candidate_dir, draw_index, slot, as_floats=True)

    # Pair CBs by exact hash first (true content equivalence), then by
    # size+order for any leftovers. This collapses cross-API binding-model
    # noise: D3D12's mandatory per-draw root rebinds vs D3D11/Vulkan's slot
    # reuse only show up where the content actually differs.
    ref_cbs = list(ref_dump["cbuffers"])
    cand_cbs = list(cand_dump["cbuffers"])
    pairs: list[tuple[dict | None, dict | None]] = []
    cand_used = [False] * len(cand_cbs)
    for rc in ref_cbs:
        rh = rc.get("hash")
        match_idx = None
        for j, cc in enumerate(cand_cbs):
            if not cand_used[j] and cc.get("hash") == rh and rh is not None:
                match_idx = j
                break
        if match_idx is not None:
            cand_used[match_idx] = True
            pairs.append((rc, cand_cbs[match_idx]))
        else:
            pairs.append((rc, None))
    for j, cc in enumerate(cand_cbs):
        if not cand_used[j]:
            pairs.append((None, cc))

    out_slots = []
    for i, (rc_or_none, cc_or_none) in enumerate(pairs):
        rc = rc_or_none or {}
        cc = cc_or_none or {}
        rfloats = rc.get("floats") or []
        cfloats = cc.get("floats") or []
        deltas = []
        n = min(len(rfloats), len(cfloats))
        for j in range(n):
            d = abs(rfloats[j] - cfloats[j])
            if d > epsilon:
                deltas.append({"index": j, "ref": rfloats[j], "cand": cfloats[j], "abs_delta": d})
        deltas.sort(key=lambda x: -x["abs_delta"])
        out_slots.append({
            "pair_index": i,
            "size": rc.get("size") or cc.get("size"),
            "ref_slot": rc.get("slot"),
            "cand_slot": cc.get("slot"),
            "slot": rc.get("slot") if rc_or_none else cc.get("slot"),
            "ref_hash": rc.get("hash"),
            "cand_hash": cc.get("hash"),
            "ref_floats": len(rfloats),
            "cand_floats": len(cfloats),
            "diff_count": len(deltas),
            "hash_match": rc.get("hash") == cc.get("hash") and rc.get("hash") is not None,
            "ref_present": rc_or_none is not None,
            "cand_present": cc_or_none is not None,
            "top_diffs": deltas[:max_diffs],
        })
    return {
        "reference_dir": reference_dir,
        "candidate_dir": candidate_dir,
        "draw_index": draw_index,
        "epsilon": epsilon,
        "slots": out_slots,
    }


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
                    "tolerance": {"type": "integer"},
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
                    "tolerance": {"type": "integer"},
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
                    "tolerance": {"type": "integer"},
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
                    "tolerance": {"type": "integer"},
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
        {
            "name": "summarize_trace",
            "description": "Summarize one trace.json (T850_RENDER_TRACE): counts, draws-by-shader, event histogram.",
            "inputSchema": {
                "type": "object",
                "properties": {"directory": {"type": "string"}},
                "required": ["directory"],
            },
        },
        {
            "name": "compare_traces",
            "description": "Structural comparison of two trace.json files: counts, buffer hashes, per-draw divergence histogram.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "reference_dir": {"type": "string"},
                    "candidate_dir": {"type": "string"},
                },
                "required": ["reference_dir", "candidate_dir"],
            },
        },
        {
            "name": "diff_draws",
            "description": "Per-draw aligned diff of two trace.json files. Returns up to max_results draws whose normalized state differs.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "reference_dir": {"type": "string"},
                    "candidate_dir": {"type": "string"},
                    "max_results": {"type": "integer"},
                    "include_matching": {"type": "boolean"},
                },
                "required": ["reference_dir", "candidate_dir"],
            },
        },
        {
            "name": "find_first_diverging_draw",
            "description": "Walk aligned draws and return the first divergent one. Use start_at to skip past known-good prefix and isolate the next regression.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "reference_dir": {"type": "string"},
                    "candidate_dir": {"type": "string"},
                    "start_at": {"type": "integer"},
                },
                "required": ["reference_dir", "candidate_dir"],
            },
        },
        {
            "name": "dump_cbuffer_hex",
            "description": "Decode the cbuffer slice the GPU saw at a specific draw (requires T850_TRACE_GEOMETRY=ON). Returns float32 view by default.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "directory": {"type": "string"},
                    "draw_index": {"type": "integer"},
                    "slot": {"type": "integer"},
                    "as_floats": {"type": "boolean"},
                },
                "required": ["directory", "draw_index"],
            },
        },
        {
            "name": "diff_cbuffer_floats",
            "description": "Float-level cross-API diff of one draw's cbuffer slices. Sorted by absolute delta descending; epsilon filters noise.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "reference_dir": {"type": "string"},
                    "candidate_dir": {"type": "string"},
                    "draw_index": {"type": "integer"},
                    "slot": {"type": "integer"},
                    "max_diffs": {"type": "integer"},
                    "epsilon": {"type": "number"},
                },
                "required": ["reference_dir", "candidate_dir", "draw_index"],
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
    "summarize_trace": summarize_trace,
    "compare_traces": compare_traces,
    "diff_draws": diff_draws,
    "find_first_diverging_draw": find_first_diverging_draw,
    "dump_cbuffer_hex": dump_cbuffer_hex,
    "diff_cbuffer_floats": diff_cbuffer_floats,
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

    summarize_parser = subparsers.add_parser("summarize-trace", help="Summarize one trace.json.")
    summarize_parser.add_argument("directory")

    compare_traces_parser = subparsers.add_parser("compare-traces", help="Structural comparison of two trace.json files.")
    compare_traces_parser.add_argument("reference_dir")
    compare_traces_parser.add_argument("candidate_dir")

    diff_draws_parser = subparsers.add_parser("diff-draws", help="Per-draw aligned diff of two trace.json files.")
    diff_draws_parser.add_argument("reference_dir")
    diff_draws_parser.add_argument("candidate_dir")
    diff_draws_parser.add_argument("--max-results", type=int, default=10)
    diff_draws_parser.add_argument("--include-matching", action="store_true")

    first_diverging_parser = subparsers.add_parser("first-diverging-draw", help="Find first divergent draw between two traces.")
    first_diverging_parser.add_argument("reference_dir")
    first_diverging_parser.add_argument("candidate_dir")
    first_diverging_parser.add_argument("--start-at", type=int, default=0)

    dump_cb_parser = subparsers.add_parser("dump-cbuffer", help="Decode cbuffer slice for a specific draw.")
    dump_cb_parser.add_argument("directory")
    dump_cb_parser.add_argument("draw_index", type=int)
    dump_cb_parser.add_argument("--slot", type=int)
    dump_cb_parser.add_argument("--hex", action="store_true", help="Emit raw hex instead of float32 view.")

    diff_cb_parser = subparsers.add_parser("diff-cbuffer", help="Float-level diff of one draw's cbuffer slices.")
    diff_cb_parser.add_argument("reference_dir")
    diff_cb_parser.add_argument("candidate_dir")
    diff_cb_parser.add_argument("draw_index", type=int)
    diff_cb_parser.add_argument("--slot", type=int)
    diff_cb_parser.add_argument("--max-diffs", type=int, default=32)
    diff_cb_parser.add_argument("--epsilon", type=float, default=1e-5)

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
        result = analyze_artifacts(args.reference_dir, args.candidate_dir, target=args.target, tolerance=args.tolerance)
    elif args.command == "generate-report":
        result = generate_visual_report(args.reference_dir, args.candidate_dir, args.output_dir, args.target, args.tolerance)
    elif args.command == "summarize-trace":
        result = summarize_trace(args.directory)
    elif args.command == "compare-traces":
        result = compare_traces(args.reference_dir, args.candidate_dir)
    elif args.command == "diff-draws":
        result = diff_draws(args.reference_dir, args.candidate_dir, args.max_results, args.include_matching)
    elif args.command == "first-diverging-draw":
        result = find_first_diverging_draw(args.reference_dir, args.candidate_dir, args.start_at)
    elif args.command == "dump-cbuffer":
        result = dump_cbuffer_hex(args.directory, args.draw_index, args.slot, as_floats=not args.hex)
    elif args.command == "diff-cbuffer":
        result = diff_cbuffer_floats(args.reference_dir, args.candidate_dir, args.draw_index,
                                     args.slot, args.max_diffs, args.epsilon)
    else:
        raise RuntimeError(f"Unhandled command: {args.command}")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
