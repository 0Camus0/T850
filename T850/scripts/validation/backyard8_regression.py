#!/usr/bin/env python3
r"""Backyard 8 capture + water-level regression gate.

Runs the nine captures (screen space, water/splat, spheres at frames 24/90/300) and the
two 1800-frame water-level checks, then compares against
scripts/validation/backyard8_references.json.

Example:
  cd T850
  LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu SDL_VIDEO_DRIVER=x11 \
    python3 scripts/validation/backyard8_regression.py

Windows:
  python scripts\validation\backyard8_regression.py --api d3d12 ^
    --day-scene bin\x64\Release\DayScene.exe
"""
import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]          # T850
REPO = PROJECT.parent
DEFAULT_EXE = PROJECT / "bin" / "SteamDeck" / "Release" / "DayScene"
DEFAULT_ASSETS = PROJECT / "Assets" / "Scenes" / "Backyard8Fluid.json"
DEFAULT_REFS = HERE / "backyard8_references.json"
PREFIX = "dumps_"
CHANNELS = 3


def read_ppm(path):
    data = path.read_bytes()
    tokens = []
    i = 0
    # Read and discard the magic (P6/P3).
    while i < len(data) and data[i:i + 1].isspace():
        i += 1
    j = i
    while j < len(data) and not data[j:j + 1].isspace():
        j += 1
    magic = data[i:j]
    if magic not in (b"P6", b"P3"):
        raise ValueError(f"{path}: unsupported PPM magic {magic!r}")
    i = j
    while len(tokens) < 3:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while i < len(data) and data[i:i + 1] not in (b"\n", b"\r"):
                i += 1
            continue
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        tokens.append(data[i:j])
        i = j
    width, height, maxval = int(tokens[0]), int(tokens[1]), int(tokens[2])
    if maxval > 255:
        raise ValueError(f"{path}: only 8-bit PPM is supported")
    # A binary PPM has exactly one whitespace separator after maxval. Do not skip all
    # whitespace here: the first pixel can legitimately be 9/10/13/32.
    if data[i:i + 2] == b"\r\n":
        i += 2
    elif i < len(data):
        i += 1
    payload = data[i:]
    channels = len(payload) // (width * height)
    if channels not in (3, 4):
        raise ValueError(f"{path}: {width}x{height} payload has {len(payload)} bytes")
    return width, height, channels, payload


def stats(path):
    width, height, channels, payload = read_ppm(path)
    n = 0
    sx = sy = sz = 0
    minx, maxx = width, -1
    miny, maxy = height, -1
    step = channels
    for i in range(0, width * height * channels, step):
        r, g, b = payload[i], payload[i + 1], payload[i + 2]
        if r == 0 and g == 0 and b == 0:
            continue
        n += 1
        sx += r
        sy += g
        sz += b
        x = (i // channels) % width
        y = (i // channels) // width
        if x < minx: minx = x
        if x > maxx: maxx = x
        if y < miny: miny = y
        if y > maxy: maxy = y
    if n == 0:
        return 0, [0, 0, 0, 0], [0.0, 0.0, 0.0]
    return n, [minx, maxx, miny, maxy], [sx / n, sy / n, sz / n]


def environment():
    env = os.environ.copy()
    if sys.platform.startswith("linux") and "SDL_VIDEO_DRIVER" not in env:
        env["SDL_VIDEO_DRIVER"] = "x11"
    return env


def find_dump(cwd, api, frame):
    dirs = [d for d in Path(cwd).glob(f"{PREFIX}{api}_f{frame}_*") if d.is_dir()]
    dirs.sort(key=lambda p: p.stat().st_mtime)
    return dirs[-1] if dirs else None


def run_capture(exe, assets, api, mode, frame, timeout):
    cwd = exe.parent
    for old in cwd.glob(f"{PREFIX}{api}_f{frame}_*"):
        if old.is_dir():
            shutil.rmtree(old, ignore_errors=True)
    args = [str(exe), "--unattended", "--api", api, "--scene", "8",
            "--backyard-no-hud", "--backyard-start", "water",
            "--width", "960", "--height", "540",
            "--regressionFixedDt", "0.0166667", "--postProcessMode", "compute",
            "--dump-frame", str(frame), "--logLevel", "warn"]
    if mode == "spheres":
        cfg = json.loads(assets.read_text(encoding="utf-8"))
        cfg["particles"]["renderMode"] = 0
        cfg["particles"]["style"] = 3
        cfg_path = Path(cwd) / f"backyard8_spheres_f{frame}.json"
        cfg_path.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
        args += ["--fluid-config", str(cfg_path)]
    else:
        args += ["--fluid-render", mode]
    result = subprocess.run(args, cwd=str(cwd), env=environment(),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=timeout)
    if result.returncode != 0:
        return None, f"exit {result.returncode}: {result.stdout[-500:]}"
    dump = find_dump(cwd, api, frame)
    if dump is None:
        return None, f"no {PREFIX}{api}_f{frame}_* directory"
    ppm = dump / "RT_Dump_FluidParticles.ppm"
    if not ppm.exists():
        return None, f"{dump.name}: no RT_Dump_FluidParticles.ppm"
    try:
        result_stats = stats(ppm)
    finally:
        shutil.rmtree(dump, ignore_errors=True)
    return result_stats, None


def compare(mode, frame, actual, reference, tolerances):
    pixels, bbox, mean = actual
    rpixels = reference["pixels"]
    rbbox = reference["bbox"]
    rmean = reference["mean"]
    tol = tolerances[str(frame)]
    dpx = abs(pixels - rpixels) / rpixels * 100.0
    dbox = max(abs(bbox[i] - rbbox[i]) for i in range(4))
    dmean = max(abs(mean[i] - rmean[i]) for i in range(3))
    ok = (dpx <= tol["pixel_percent"] and dbox <= tol["bbox_px"]
          and dmean <= tol["mean_levels"])
    detail = (f"{mode:12s} f{frame:<3d} px={pixels:6d} ({dpx:6.3f}%) "
              f"bbox={bbox} (dbox={dbox}) mean=({mean[0]:5.1f},{mean[1]:5.1f},"
              f"{mean[2]:5.1f}) (dmean={dmean:4.2f})")
    return ok, detail


def run_level(exe, api, start, timeout):
    with tempfile.TemporaryDirectory(prefix=f"b8level_{start}_") as tmp:
        env = environment()
        env["T850_PBF_DUMP"] = "1800"
        env["T850_PBF_DUMP_DIR"] = tmp
        args = [str(exe), "--unattended", "--api", api, "--scene", "8",
                "--backyard-no-hud", "--width", "640", "--height", "360",
                "--regressionFixedDt", "0.0166667", "--postProcessMode", "compute",
                "--dump-frame", "1800", "--logLevel", "warn"]
        if start == "water":
            args += ["--backyard-start", "water"]
        result = subprocess.run(args, cwd=str(exe.parent), env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=timeout)
        if result.returncode != 0:
            return None, f"exit {result.returncode}: {result.stdout[-500:]}"
        csv_path = Path(tmp) / "pbf_f1800.csv"
        if not csv_path.exists():
            return None, f"missing {csv_path}"
        import pbf_water_level
        return pbf_water_level.measure(str(csv_path)), None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--day-scene", default=str(DEFAULT_EXE),
                        help="path to DayScene(.exe)")
    parser.add_argument("--api", default="vulkan")
    parser.add_argument("--assets", default=str(DEFAULT_ASSETS))
    parser.add_argument("--references", default=str(DEFAULT_REFS))
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--skip-captures", action="store_true")
    parser.add_argument("--skip-level", action="store_true")
    args = parser.parse_args()

    exe = Path(args.day_scene).resolve()
    assets = Path(args.assets).resolve()
    if not exe.exists():
        print(f"ERROR: DayScene not found: {exe}")
        return 2
    refs = json.loads(Path(args.references).read_text(encoding="utf-8"))
    sys.path.insert(0, str(HERE))
    failures = []

    if not args.skip_captures:
        print(f"Captures: api={args.api}, {refs['capture']['width']}x"
              f"{refs['capture']['height']}, start={refs['capture']['start']}")
        for mode in ("screen-space", "splat", "spheres"):
            for frame in refs["capture"]["frames"]:
                actual, error = run_capture(exe, assets, args.api, mode, frame,
                                            args.timeout)
                if error:
                    print(f"FAIL {mode} f{frame}: {error}")
                    failures.append(f"{mode} f{frame}")
                    continue
                ok, detail = compare(mode, frame, actual,
                                     refs["references"][mode][str(frame)],
                                     refs["tolerances"])
                print(("PASS " if ok else "FAIL ") + detail)
                if not ok:
                    failures.append(f"{mode} f{frame}")

    if not args.skip_level:
        target = refs["water_level"]["target_m"]
        tolerance = refs["water_level"]["tolerance_m"]
        print(f"Water level: target {target:+.3f} +/- {tolerance:.3f} m")
        for start in ("lake", "water"):
            level, error = run_level(exe, args.api, start, args.timeout)
            if error:
                print(f"FAIL level {start}: {error}")
                failures.append(f"level {start}")
                continue
            ok = abs(level - target) <= tolerance
            print(("PASS " if ok else "FAIL ")
                  + f"level {start:5s}: {level:+.4f} m")
            if not ok:
                failures.append(f"level {start}")

    if failures:
        print(f"RESULT: {len(failures)} failure(s): {', '.join(failures)}")
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
