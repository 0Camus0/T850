#!/usr/bin/env python3
"""Proves that every spawned ball really wears the palette material the scene assigned.

Two checks, one CPU-side and one on the GPU, and neither can pass on its own:

  1. Assignment. With a fixed `--backyard-spawn-seed`, the scene's spawn log must name a
     different material for every ball until the palette runs out (the shuffle bag
     guarantees this). This is the rule, but a log line proves nothing about pixels.
  2. Application. The same run is repeated with `Scenes/BackyardBallMaterials.json` moved
     aside, which makes the scene fall back to the GLB's own placeholder look. Each ball's
     screen position is projected with the engine's own `camVP` from `snapshot.json`, and
     the mean colour of a small patch must differ between the two dumps. If the
     per-instance texture override stops working, the two dumps match on every ball and
     this check fails; if only a few balls differ, the override is partially broken.

The dump is taken at frame 3, when the balls have barely moved from their logged spawn
positions (0.0066 s of fall), so a projection lands on the ball.

Usage (the engine must be built first):

    python scripts\\validation\\probe_ball_materials.py --api d3d12
    python scripts\\validation\\probe_ball_materials.py --api vulkan --balls 32
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: python -m pip install pillow")

T850_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_EXE_DIR = os.path.join(T850_DIR, "bin", "x64", "Release")
PALETTE = os.path.join(T850_DIR, "Assets", "Scenes", "BackyardBallMaterials.json")

SPAWN_RE = re.compile(
    r"Spawned ball (\d+): position \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)"
    r".*?material (.+?);")
SCRIPTED_RE = re.compile(r"Scripted ball material: (.+?)\s*$")

PATCH_DIFF = 6         # max channel difference between the palette and placeholder look
MAX_DISTINCT = 32      # the palette size; repeats start after this many bodies


def run_engine(exe_dir, api, balls, frame, seed, log_path):
    exe = os.path.join(exe_dir, "DayScene.exe")
    if not os.path.isfile(exe):
        sys.exit("DayScene.exe not found: %s" % exe)
    args = [exe, "--scene", "7", "--api", api, "--unattended",
            "--regressionFixedDt", "0.0033", "--backyard-spawn", str(balls),
            "--backyard-spawn-seed", str(seed),
            "--logLevel", "info", "--logFile", log_path, "--dump-frame", str(frame)]
    result = subprocess.run(args, cwd=exe_dir, capture_output=True, text=True, timeout=900)
    if result.returncode != 0:
        sys.exit("engine exited %d; see %s" % (result.returncode, log_path))


def newest_dump(exe_dir, api, frame, newer_than):
    pattern = os.path.join(exe_dir, "dumps_%s_f%d_*" % (api, frame))
    candidates = [path for path in glob.glob(pattern) if os.path.getmtime(path) >= newer_than]
    if not candidates:
        sys.exit("no dump directory matched %s" % pattern)
    return max(candidates, key=os.path.getmtime)


def read_spawns(log_path):
    spawned, scripted = [], None
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if "[ERROR" in line:
                sys.exit("engine logged an error: %s" % line.strip())
            match = SPAWN_RE.search(line)
            if match:
                spawned.append({
                    "index": int(match.group(1)),
                    "position": (float(match.group(2)), float(match.group(3)), float(match.group(4))),
                    "material": match.group(5),
                })
                continue
            match = SCRIPTED_RE.search(line)
            if match:
                scripted = match.group(1)
    return spawned, scripted


def project(vp, position, width, height):
    x, y, z = position
    v = (x, y, z, 1.0)
    # snapshot.json stores camVP row-major for the engine's row-vector convention:
    # clip_j = sum over i of v_i * M[i][j].
    clip = [sum(v[i] * vp[i][j] for i in range(4)) for j in range(4)]
    if abs(clip[3]) < 1e-9:
        return None
    ndc = (clip[0] / clip[3], clip[1] / clip[3])
    return ((ndc[0] * 0.5 + 0.5) * width, (1.0 - (ndc[1] * 0.5 + 0.5)) * height)


def patch_mean(pixels, width, height, sx, sy, radius=4):
    total, count = [0, 0, 0], 0
    for dx in range(-radius, radius + 1):
        for dy in range(-radius, radius + 1):
            x, y = sx + dx, sy + dy
            if 0 <= x < width and 0 <= y < height:
                r, g, b = pixels[x, y]
                total[0] += r
                total[1] += g
                total[2] += b
                count += 1
    return tuple(v // max(1, count) for v in total)


def load_frame(dump):
    snapshot = json.load(open(os.path.join(dump, "snapshot.json")))
    image = Image.open(os.path.join(dump, "RT_Dump_BackBuffer.ppm")).convert("RGB")
    return snapshot["matrices"]["camVP"], image


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--api", default="d3d12")
    parser.add_argument("--balls", type=int, default=32)
    parser.add_argument("--frame", type=int, default=3)
    parser.add_argument("--seed", type=int, default=12345)
    parser.add_argument("--exe-dir", default=DEFAULT_EXE_DIR)
    parser.add_argument("--log", default=None, help="keep the palette run's log at this path")
    args = parser.parse_args()

    if not os.path.isfile(PALETTE):
        sys.exit("palette file not found: %s (run scripts/textures/fetch_ball_materials.py)" % PALETTE)

    log_path = args.log or tempfile.mktemp(prefix="probe_ball_materials_", suffix=".log")
    hidden = PALETTE + ".hidden"

    # ── Run A: palette present ────────────────────────────────────────────────
    started = time.time() - 1.0
    run_engine(args.exe_dir, args.api, args.balls, args.frame, args.seed, log_path)
    dump_a = newest_dump(args.exe_dir, args.api, args.frame, started)
    spawned, scripted = read_spawns(log_path)
    if not spawned:
        sys.exit("no 'Spawned ball' lines in %s" % log_path)

    labels = [ball["material"] for ball in spawned]
    if scripted:
        labels.append(scripted)
    distinct = sorted(set(labels))
    expected_repeats = max(0, len(labels) - MAX_DISTINCT)
    repeats = len(labels) - len(distinct)

    # ── Run B: palette moved aside, so the balls fall back to the GLB look ────
    try:
        shutil.move(PALETTE, hidden)
        log_b = tempfile.mktemp(prefix="probe_ball_materials_placeholder_", suffix=".log")
        started = time.time() - 1.0
        run_engine(args.exe_dir, args.api, args.balls, args.frame, args.seed, log_b)
        dump_b = newest_dump(args.exe_dir, args.api, args.frame, started)
    finally:
        if os.path.exists(hidden):
            shutil.move(hidden, PALETTE)

    vp_a, image_a = load_frame(dump_a)
    _, image_b = load_frame(dump_b)
    if image_b.size != image_a.size:
        sys.exit("the two dumps differ in size: %s vs %s" % (image_a.size, image_b.size))
    width, height = image_a.size
    pixels_a, pixels_b = image_a.load(), image_b.load()

    failures = []
    rows = []
    for ball in spawned:
        screen = project(vp_a, ball["position"], width, height)
        if screen is None:
            failures.append("ball %d projects to w=0" % ball["index"])
            continue
        sx, sy = int(round(screen[0])), int(round(screen[1]))
        if not (0 <= sx < width and 0 <= sy < height):
            failures.append("ball %d projects off screen at (%d,%d)" % (ball["index"], sx, sy))
            continue
        colour_a = patch_mean(pixels_a, width, height, sx, sy)
        colour_b = patch_mean(pixels_b, width, height, sx, sy)
        diff = max(abs(colour_a[k] - colour_b[k]) for k in range(3))
        rows.append((ball["index"], ball["material"], (sx, sy), colour_a, colour_b, diff))
        if diff < PATCH_DIFF:
            failures.append("ball %d (%s) renders the same with and without the palette "
                            "(max channel diff %d at %d,%d)"
                            % (ball["index"], ball["material"], diff, sx, sy))

    print("\n%-4s %-24s %-11s %-15s %-15s %s"
          % ("ball", "material", "pixel", "palette rgb", "placeholder rgb", "diff"))
    for index, material, pixel, colour_a, colour_b, diff in rows:
        print("%-4d %-24s %-11s %-15s %-15s %d"
              % (index, material[:24], "%d,%d" % pixel, str(colour_a), str(colour_b), diff))

    total_diff = sum(row[5] for row in rows)
    print("\n[probe] %d ball(s), %d distinct material(s), %d repeat(s) (expected <= %d)"
          % (len(labels), len(distinct), repeats, expected_repeats))
    print("[probe] mean per-ball palette-vs-placeholder difference: %.1f"
          % (total_diff / max(1, len(rows))))
    print("[probe] dumps: %s | %s" % (os.path.basename(dump_a), os.path.basename(dump_b)))

    if len(distinct) < min(MAX_DISTINCT, len(spawned)):
        failures.append("only %d distinct materials for %d spawned balls" % (len(distinct), len(spawned)))
    if repeats > expected_repeats:
        failures.append("%d material repeats, expected at most %d" % (repeats, expected_repeats))

    if failures:
        print("\n[probe] FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("\n[probe] PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
