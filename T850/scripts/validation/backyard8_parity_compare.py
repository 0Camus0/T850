#!/usr/bin/env python3
"""WO-W2b: Backyard 8 Windows/Linux parity compare tool.

Compares Windows render-graph dumps against the Linux reference dumps and
produces a verdict per (api, cell) plus a contact sheet and diff images.

Usage:
    python3 backyard8_parity_compare.py \
        --linux /tmp/winport/linux_ref \
        --win    /tmp/winport/win_out \
        --out    /tmp/winport/parity_out \
        --apis   vulkan,d3d12,d3d11,gl

Windows image layout (from win/backyard8_parity.ps1):
    <win>/<api>/cells/<cell>/RT_Dump_HDR_Final.ppm
Vulkan cells are repeated twice, so the ps1 writes them as <cell>_r1 and
<cell>_r2; for every other API the cell directory is named <cell> (no
suffix). This tool picks the r1 dump for vulkan (the repeat that matches
the Linux r1 reference) and the plain dump for the other APIs.

Linux reference layout:
    <linux>/<cell>_r1/dump/RT_Dump_HDR_Final.ppm

The eight cells and the metric definitions come from
/tmp/winport/linux_ref/analyze.py (mean absolute difference per channel,
p99 of the per-pixel max channel difference, and the percentage of pixels
whose max channel difference is above 4).

Verdicts:
    noise   - all three metrics within 1.5x the cell's r1-vs-r2 values in
              <linux>/noise.json (the % above 4 also allows +1 point)
    DIFF    - otherwise
    missing - the Windows PPM does not exist
    size    - the image dimensions differ from the Linux reference
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


def win_cell_dir(win_dir, api, cell):
    """The cell directory for an API, handling the Vulkan repeat naming."""
    name = f"{cell}_r1" if api == "vulkan" else cell
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


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--linux", default="/tmp/winport/linux_ref")
    ap.add_argument("--win", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--apis", default="vulkan,d3d12,d3d11,gl")
    args = ap.parse_args()

    apis = [a.strip() for a in args.apis.split(",") if a.strip()]
    os.makedirs(args.out, exist_ok=True)

    with open(os.path.join(args.linux, "noise.json")) as f:
        noise = json.load(f)

    results = {}
    linux_imgs = {}
    for cell in CELLS:
        p = os.path.join(args.linux, f"{cell}_r1", "dump", "RT_Dump_HDR_Final.ppm")
        linux_imgs[cell] = read_ppm(p).astype(np.float64)

    for api in apis:
        results[api] = {}
        for cell in CELLS:
            ref = linux_imgs[cell]
            win_path = os.path.join(win_cell_dir(args.win, api, cell), "RT_Dump_HDR_Final.ppm")
            if not os.path.exists(win_path):
                results[api][cell] = {"verdict": "missing"}
                continue
            img = read_ppm(win_path)
            if img.shape != ref.shape:
                results[api][cell] = {
                    "verdict": "size",
                    "linux_size": [int(ref.shape[0]), int(ref.shape[1])],
                    "win_size": [int(img.shape[0]), int(img.shape[1])],
                }
                continue
            m = metrics(ref, img.astype(np.float64))
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
    def fmt(entry):
        v = entry["verdict"]
        if v == "missing":
            return "missing"
        if v == "size":
            return "size"
        m = entry["mean_abs_diff_per_channel"]
        return f"{m[0]:.2f}/{m[1]:.2f}/{m[2]:.2f} {entry['p99_per_pixel_max_channel_diff']:.1f} {entry['pct_pixels_max_diff_above_4']:.2f} {v}"

    lines = ["| cell | " + " | ".join(apis) + " |",
             "|---|" + "---|" * len(apis)]
    for cell in CELLS:
        lines.append(f"| {cell} | " + " | ".join(fmt(results[api][cell]) for api in apis) + " |")
    with open(os.path.join(args.out, "compare.md"), "w") as f:
        f.write("\n".join(lines) + "\n")

    # sheet.png: one row per cell, columns [Linux | <apis...>].
    n_cols = 1 + len(apis)
    row_h = LABEL_H + TILE_H
    sheet = Image.new("RGB",
                      (n_cols * TILE_W + PAD * (n_cols + 1),
                       row_h * len(CELLS) + PAD * (len(CELLS) + 1)),
                      (30, 30, 30))
    draw = ImageDraw.Draw(sheet)
    for i, cell in enumerate(CELLS):
        y0 = PAD + i * row_h
        # First tile: Linux reference, labelled with the cell name.
        sheet.paste(tile_for(linux_imgs[cell].astype(np.uint8), cell), (PAD, y0))
        # API tiles.
        for j, api in enumerate(apis):
            x = PAD * (j + 2) + (j + 1) * TILE_W
            entry = results[api][cell]
            p = os.path.join(win_cell_dir(args.win, api, cell), "RT_Dump_HDR_Final.ppm")
            if entry["verdict"] == "missing" or not os.path.exists(p):
                tile = Image.new("RGB", (TILE_W, TILE_H + LABEL_H), MISSING_TILE)
                d = ImageDraw.Draw(tile)
                d.text((4, 4), f"{api} missing", fill=(200, 200, 200))
            else:
                tile = tile_for(read_ppm(p), api)
            sheet.paste(tile, (x, y0))
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
