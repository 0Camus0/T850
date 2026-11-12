#!/usr/bin/env python3
"""WO-W2b/W2c: Backyard 8 Windows/Linux parity compare tool.

Compares Windows render-graph dumps against a reference and produces a
verdict per (api, cell) plus a contact sheet and diff images.

Usage:
    python3 backyard8_parity_compare.py \
        --linux /tmp/winport/linux_ref \
        --win    /tmp/winport/win_out \
        --out    /tmp/winport/parity_out \
        --apis   vulkan,d3d12,d3d11,gl,webgpu \
        --ref    linux

Windows image layout (from win/backyard8_parity.ps1):
    <win>/<api>/cells/<cell>/RT_Dump_HDR_Final.ppm
Vulkan cells are repeated twice, so the ps1 writes them as <cell>_r1 and
<cell>_r2; for every other API the cell directory is named <cell> (no
suffix). This tool picks the r1 dump for vulkan (the repeat that matches
the Linux r1 reference) and the plain dump for the other APIs.

Reference modes (--ref):
    linux (default) - reference is <linux>/<cell>_r1/dump/RT_Dump_HDR_Final.ppm
                      and the noise comes from <linux>/noise.json (the
                      r1-vs-r2 values). Sheet columns: [Linux | <apis...>].
    win             - reference is <win>/vulkan/cells/<cell>_r1/RT_Dump_HDR_Final.ppm
                      and the noise is that image against
                      <win>/vulkan/cells/<cell>_r2/... computed with the
                      same three metrics (it does not come from noise.json).
                      Sheet columns: [Win Vulkan r1 | Win Vulkan r2 |
                      <apis other than vulkan>]. If a Windows Vulkan r2
                      image is missing, the cell row is marked no-noise.

The eight cells and the metric definitions come from
/tmp/winport/linux_ref/analyze.py (mean absolute difference per channel,
p99 of the per-pixel max channel difference, and the percentage of pixels
whose max channel difference is above 4).

Verdicts:
    noise   - all three metrics within 1.5x the noise values (the % above 4
              also allows +1 point)
    DIFF    - otherwise
    missing - the Windows PPM does not exist
    size    - the image dimensions differ from the reference
    no-noise - (--ref win only) the Windows Vulkan r2 image is missing, so
               the noise values cannot be computed
"""

import argparse
import json
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

# The eight parity cells (same list as /tmp/winport/linux_ref/analyze.py).
CELLS = [
    "ss_default_water150", "ss_default_lake300",
    "ss_close_water150", "ss_close_lake300",
    "surfel_default_water150", "surfel_default_lake300",
    "surfel_close_water150", "surfel_close_lake300",
]

TILE_W, TILE_H = 480, 270
PAD = 8
LABEL_H = 24
MISSING_TILE = (64, 64, 64)


def read_ppm(path):
    """Read a binary P6 PPM into an (h, w, 3) uint8 array."""
    with open(path, "rb") as f:
        data = f.read()
    m = re.match(rb"^P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if m is None:
        raise ValueError(f"not a P6 PPM: {path}")
    w, h = int(m.group(1)), int(m.group(2))
    off = m.end()
    return np.frombuffer(data[off:off + w * h * 3], dtype=np.uint8).reshape(h, w, 3).copy()


def metrics(a, b):
    """The three analyze.py metrics between two float arrays."""
    diff = np.abs(a - b)
    per_pixel_max = diff.max(axis=2)
    return {
        "mean_abs_diff_per_channel": [float(v) for v in diff.mean(axis=(0, 1))],
        "p99_per_pixel_max_channel_diff": float(np.percentile(per_pixel_max, 99)),
        "pct_pixels_max_diff_above_4": float((per_pixel_max > 4).mean() * 100.0),
    }


def win_cell_dir(win_dir, api, cell, repeat=1):
    """The cell directory for an API, handling the Vulkan repeat naming."""
    name = f"{cell}_r{repeat}" if api == "vulkan" else cell
    return os.path.join(win_dir, api, "cells", name)


def verdict_for(m, noise_entry):
    """noise if every metric is within 1.5x the r1-vs-r2 noise values."""
    mean_ok = all(
        m["mean_abs_diff_per_channel"][i] <= 1.5 * noise_entry["mean_abs_diff_per_channel"][i]
        for i in range(3)
    )
    p99_ok = m["p99_per_pixel_max_channel_diff"] <= 1.5 * noise_entry["p99_per_pixel_max_channel_diff"]
    pct_ok = m["pct_pixels_max_diff_above_4"] <= 1.5 * noise_entry["pct_pixels_max_diff_above_4"] + 1.0
    return "noise" if (mean_ok and p99_ok and pct_ok) else "DIFF"


def tile_for(img, label):
    """Scale an image to a 480x270 tile with a label above it; return the tile."""
    im = Image.fromarray(img).convert("RGB").resize((TILE_W, TILE_H), Image.LANCZOS)
    tile = Image.new("RGB", (TILE_W, TILE_H + LABEL_H), (30, 30, 30))
    draw = ImageDraw.Draw(tile)
    draw.text((4, 4), label, fill=(255, 255, 255))
    tile.paste(im, (0, LABEL_H))
    return tile


def missing_tile(label):
    tile = Image.new("RGB", (TILE_W, TILE_H + LABEL_H), MISSING_TILE)
    d = ImageDraw.Draw(tile)
    d.text((4, 4), label, fill=(200, 200, 200))
    return tile


def fmt_metrics(m):
    c = m["mean_abs_diff_per_channel"]
    return f"{c[0]:.2f}/{c[1]:.2f}/{c[2]:.2f} {m['p99_per_pixel_max_channel_diff']:.1f} {m['pct_pixels_max_diff_above_4']:.2f}"


def fmt_entry(entry):
    v = entry["verdict"]
    if v in ("missing", "size", "no-noise"):
        return v
    return f"{fmt_metrics(entry)} {v}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--linux", default="/tmp/winport/linux_ref")
    ap.add_argument("--win", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--apis", default="vulkan,d3d12,d3d11,gl,webgpu")
    ap.add_argument("--ref", choices=["linux", "win"], default="linux")
    args = ap.parse_args()

    apis = [a.strip() for a in args.apis.split(",") if a.strip()]
    os.makedirs(args.out, exist_ok=True)

    ref_mode = args.ref
    ref_imgs = {}
    noise = {}

    if ref_mode == "linux":
        with open(os.path.join(args.linux, "noise.json")) as f:
            noise = json.load(f)
        for cell in CELLS:
            p = os.path.join(args.linux, f"{cell}_r1", "dump", "RT_Dump_HDR_Final.ppm")
            ref_imgs[cell] = read_ppm(p).astype(np.float64)
    else:
        # --ref win: reference is the Windows Vulkan r1 dump; the noise is
        # that image against the r2 dump, computed with the same metrics.
        for cell in CELLS:
            r1 = os.path.join(win_cell_dir(args.win, "vulkan", cell, 1), "RT_Dump_HDR_Final.ppm")
            r2 = os.path.join(win_cell_dir(args.win, "vulkan", cell, 2), "RT_Dump_HDR_Final.ppm")
            if not os.path.exists(r1):
                ref_imgs[cell] = None
                noise[cell] = None
                print(f"ERROR: missing Windows Vulkan r1 reference for {cell}: {r1}", file=sys.stderr)
                continue
            ref_imgs[cell] = read_ppm(r1).astype(np.float64)
            if not os.path.exists(r2):
                noise[cell] = None
                print(f"ERROR: missing Windows Vulkan r2 noise image for {cell}: {r2}", file=sys.stderr)
                continue
            noise[cell] = metrics(ref_imgs[cell], read_ppm(r2).astype(np.float64))

    results = {}
    for api in apis:
        results[api] = {}
        for cell in CELLS:
            ref = ref_imgs[cell]
            if ref is None:
                results[api][cell] = {"verdict": "no-noise"}
                continue
            win_path = os.path.join(win_cell_dir(args.win, api, cell), "RT_Dump_HDR_Final.ppm")
            if not os.path.exists(win_path):
                results[api][cell] = {"verdict": "missing"}
                continue
            img = read_ppm(win_path)
            if img.shape != ref.shape:
                results[api][cell] = {
                    "verdict": "size",
                    "ref_size": [int(ref.shape[0]), int(ref.shape[1])],
                    "win_size": [int(img.shape[0]), int(img.shape[1])],
                }
                continue
            m = metrics(ref, img.astype(np.float64))
            if noise[cell] is None:
                results[api][cell] = {"verdict": "no-noise", **m}
                continue
            verdict = verdict_for(m, noise[cell])
            results[api][cell] = {"verdict": verdict, **m}
            if verdict == "DIFF":
                diff = np.abs(ref - img.astype(np.float64))
                diff8 = np.clip(diff * 8.0, 0, 255).astype(np.uint8)
                Image.fromarray(diff8).save(os.path.join(args.out, f"diff_{api}_{cell}.png"))

    # compare.json
    with open(os.path.join(args.out, "compare.json"), "w") as f:
        json.dump(results, f, indent=2)

    # compare.md: one row per cell, one column per API.
    if ref_mode == "win":
        # Add a noise column showing the r1-against-r2 values.
        lines = ["| cell | noise | " + " | ".join(apis) + " |",
                 "|---|---|" + "---|" * len(apis)]
        for cell in CELLS:
            noise_cell = noise[cell]
            noise_str = fmt_metrics(noise_cell) if noise_cell is not None else "no-noise"
            lines.append(f"| {cell} | {noise_str} | "
                         + " | ".join(fmt_entry(results[api][cell]) for api in apis) + " |")
    else:
        lines = ["| cell | " + " | ".join(apis) + " |",
                 "|---|" + "---|" * len(apis)]
        for cell in CELLS:
            lines.append(f"| {cell} | " + " | ".join(fmt_entry(results[api][cell]) for api in apis) + " |")
    with open(os.path.join(args.out, "compare.md"), "w") as f:
        f.write("\n".join(lines) + "\n")

    # sheet.png: one row per cell.
    # --ref linux: columns [Linux | <apis...>]
    # --ref win:   columns [Win Vulkan r1 | Win Vulkan r2 | <apis other than vulkan>]
    if ref_mode == "linux":
        sheet_cols = [("linux", None)] + [(api, api) for api in apis]
    else:
        sheet_cols = [("Win Vulkan r1", "vulkan_r1"), ("Win Vulkan r2", "vulkan_r2")]
        sheet_cols += [(api, api) for api in apis if api != "vulkan"]
    n_cols = len(sheet_cols)
    row_h = LABEL_H + TILE_H
    sheet = Image.new("RGB",
                      (n_cols * TILE_W + PAD * (n_cols + 1),
                       row_h * len(CELLS) + PAD * (len(CELLS) + 1)),
                      (30, 30, 30))
    for i, cell in enumerate(CELLS):
        y0 = PAD + i * row_h
        for j, (label, key) in enumerate(sheet_cols):
            x = PAD * (j + 1) + j * TILE_W
            if key is None:
                # Linux reference tile, labelled with the cell name.
                sheet.paste(tile_for(ref_imgs[cell].astype(np.uint8), cell), (x, y0))
                continue
            if key == "vulkan_r1":
                p = os.path.join(win_cell_dir(args.win, "vulkan", cell, 1), "RT_Dump_HDR_Final.ppm")
                if os.path.exists(p):
                    sheet.paste(tile_for(read_ppm(p), label), (x, y0))
                else:
                    sheet.paste(missing_tile(f"{label} missing"), (x, y0))
            elif key == "vulkan_r2":
                p = os.path.join(win_cell_dir(args.win, "vulkan", cell, 2), "RT_Dump_HDR_Final.ppm")
                if os.path.exists(p):
                    sheet.paste(tile_for(read_ppm(p), label), (x, y0))
                else:
                    sheet.paste(missing_tile(f"{label} missing"), (x, y0))
            else:
                api = key
                entry = results[api][cell]
                p = os.path.join(win_cell_dir(args.win, api, cell), "RT_Dump_HDR_Final.ppm")
                if entry["verdict"] == "missing" or not os.path.exists(p):
                    sheet.paste(missing_tile(f"{api} missing"), (x, y0))
                else:
                    sheet.paste(tile_for(read_ppm(p), api), (x, y0))
    sheet.save(os.path.join(args.out, "sheet.png"))

    # Summary to stdout.
    for api in apis:
        counts = {}
        for cell in CELLS:
            v = results[api][cell]["verdict"]
            counts[v] = counts.get(v, 0) + 1
        print(f"{api}: " + " ".join(f"{k}={v}" for k, v in sorted(counts.items())))


if __name__ == "__main__":
    main()
