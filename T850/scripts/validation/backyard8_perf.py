#!/usr/bin/env python3
r"""Backyard 8 4K performance measurement (WO-P0).

Runs DayScene scene 8 on this machine's GPU and reports per-pass GPU times
(GPU mode) or real-time frame statistics (real-time mode), with a
contention guard so a run is only reported when no other process used the
GPU during it.

GPU mode (default), per view/start cell:
  DayScene --unattended --api vulkan --scene 8 --backyard-no-hud --width W --height H
           --postProcessMode compute --benchmarkFixedDt 0.0166667
           --profile --profileFrames 400 --logLevel info --logFile <tmp>
           [--backyard-start water] [--backyard-camera 0 1.3 -10.0 0.32 0]
  Parses the "[Profiler] <pass> CPU=<c>ms GPU=<g>ms" lines, prints one row
  per pass (passes at or above 0.03 ms in any column) with a sum row.

Real-time mode (--realtime), per view/start cell:
  Same command with the HUD on, no fixed dt, and
  --profile --profileCpuOnly --profileFrames 900 --telemetry
  --telemetryFrequencyFrames 1 --telemetryOutput <tmp>.json.
  The first 60 frames are skipped; reports the mean/median/p90/p99 of
  deltaMs, fps = 1000 / mean, and the mean CPU ms of gpu.gpu_wait,
  gpu.encode and render.pass.Deferred Light Volumes.

Contention guard (every mode, every run):
  - Before each run, wait until nvidia-smi reports GPU utilization below 10
    for 5 consecutive samples 2 s apart (give up after 15 minutes).
  - During the run, sample "nvidia-smi pmon -c 1 -s u" every 2 s. If any
    process other than DayScene shows sm >= 5, the run is contended and is
    repeated, up to 3 attempts. Cells that never got a clean run are marked
    "contended".

Usage:
  cd T850
  python3 scripts/validation/backyard8_perf.py
  python3 scripts/validation/backyard8_perf.py --realtime --views default --starts water
  python3 scripts/validation/backyard8_perf.py --width 1920 --height 1080 --runs 3 --output perf.json
"""
import argparse
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]          # T850
DEFAULT_EXE = PROJECT / "bin" / "SteamDeck" / "Release" / "DayScene"

VIEWS = ("default", "close")
STARTS = ("water", "lake")
CLOSE_CAMERA = ("0", "1.3", "-10.0", "0.32", "0")

GPU_UTIL_QUERY = ["nvidia-smi", "--query-gpu=utilization.gpu",
                  "--format=csv,noheader,nounits"]
PMON_CMD = ["nvidia-smi", "pmon", "-c", "1", "-s", "u"]

IDLE_THRESHOLD = 10       # GPU utilization % below which the GPU counts as idle
IDLE_SAMPLES = 5         # consecutive idle samples required before a run
SAMPLE_INTERVAL = 2.0     # seconds between samples
# Claude's steering: do not wait more than 10 minutes for an idle GPU; qwen-local
# generates on this GPU almost continuously, so the guarded wait will usually time
# out and the run falls back to --no-guard for functional checks.
IDLE_TIMEOUT = 10 * 60
SM_THRESHOLD = 5         # sm% at which another process counts as contending
MAX_ATTEMPTS = 3
RUN_TIMEOUT = 600        # seconds per DayScene attempt

PROFILER_RE = re.compile(r"\[Profiler\] +(.+?) CPU=([\d.]+)ms GPU=([\d.]+)ms")
PASS_KEEP_THRESHOLD = 0.03


def environment():
    env = os.environ.copy()
    if sys.platform.startswith("linux"):
        env.setdefault("SDL_VIDEO_DRIVER", "x11")
        env.setdefault("LD_LIBRARY_PATH", "/usr/lib/x86_64-linux-gnu")
    return env


def gpu_utilization():
    """Current GPU utilization in percent, or None when nvidia-smi is unavailable."""
    try:
        out = subprocess.run(GPU_UTIL_QUERY, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True, timeout=15)
        return int(out.stdout.strip())
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def other_dayscene_pids():
    """PIDs of DayScene processes that are not this process's own child.

    Any DayScene that is not ours is another worker's run and counts as
    contention: the GPU is shared, and a measurement taken while a second
    DayScene is rendering is not the game's own frame rate."""
    try:
        out = subprocess.run(["pgrep", "-x", "DayScene"], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True, timeout=15)
        return [int(line) for line in out.stdout.split() if line.strip()]
    except (OSError, ValueError, subprocess.SubprocessError):
        return []


def wait_for_idle(our_pid=None):
    """Wait until the GPU reads below IDLE_THRESHOLD for IDLE_SAMPLES samples
    SAMPLE_INTERVAL seconds apart, and no other DayScene is running.
    Returns True when idle, False on timeout."""
    deadline = time.monotonic() + IDLE_TIMEOUT
    consecutive = 0
    while time.monotonic() < deadline:
        if other_dayscene_pids():
            consecutive = 0
            time.sleep(SAMPLE_INTERVAL)
            continue
        util = gpu_utilization()
        if util is None:
            # No nvidia-smi: cannot guard, so do not block the run.
            print("[perf4k] nvidia-smi unavailable; contention guard disabled")
            return True
        if util < IDLE_THRESHOLD:
            consecutive += 1
            if consecutive >= IDLE_SAMPLES:
                return True
        else:
            consecutive = 0
        time.sleep(SAMPLE_INTERVAL)
    print(f"[perf4k] GPU still busy after {IDLE_TIMEOUT:.0f} s; giving up on the idle wait")
    return False


def parse_pmon_sample(sample, our_pid):
    """Return the highest sm% of any contending process, or -1.

    Contending means: a process other than this run's own DayScene child,
    with sm >= 0 (any use). Another worker's DayScene counts even at sm < 5,
    because it is rendering the same scene on the same GPU."""
    worst = -1
    for line in sample.splitlines():
        fields = line.split()
        # Header rows: "# gpu", "# Idx" and the column legend.
        if not fields or fields[0].startswith("#") or len(fields) < 10:
            continue
        try:
            pid = int(fields[1])
            sm = int(fields[3])
        except ValueError:
            continue
        name = fields[9]
        if pid == our_pid:
            continue
        if name == "DayScene":
            # Another worker's run: any sm counts as contention.
            worst = max(worst, sm)
            continue
        if sm >= SM_THRESHOLD:
            worst = max(worst, sm)
    return worst


def monitor_contention(stop_event, contended_event, our_pid):
    """Sample nvidia-smi pmon every SAMPLE_INTERVAL seconds while a run is in
    progress. Sets contended_event when another process (including another
    DayScene) shows sm >= 5, or when any other DayScene shows any sm."""
    while not stop_event.is_set():
        try:
            out = subprocess.run(PMON_CMD, stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL, text=True, timeout=30)
            worst = parse_pmon_sample(out.stdout, our_pid)
            if worst >= 0:
                contended_event.set()
        except (OSError, subprocess.SubprocessError):
            pass
        stop_event.wait(SAMPLE_INTERVAL)


def run_once(exe, args, log_path, timeout):
    """Run one DayScene attempt with the contention monitor active.
    Returns (clean, contended, exit_code)."""
    cmd = [str(exe)] + args
    env = environment()
    stop_event = threading.Event()
    contended_event = threading.Event()
    proc = None
    monitor = None
    try:
        with open(log_path, "w") as log:
            proc = subprocess.Popen(cmd, cwd=str(exe.parent), env=env,
                                    stdout=log, stderr=subprocess.STDOUT,
                                    preexec_fn=os.setsid)
            our_pid = proc.pid
            monitor = threading.Thread(target=monitor_contention,
                                       args=(stop_event, contended_event, our_pid),
                                       daemon=True)
            monitor.start()
            try:
                proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
                except OSError:
                    proc.kill()
                proc.wait()
                return False, contended_event.is_set(), 124
            code = proc.returncode
        return code == 0, contended_event.is_set(), code
    finally:
        stop_event.set()
        if monitor:
            monitor.join(timeout=35)


def parse_profiler(log_path):
    """GPU ms per pass from the [Profiler] report lines (last value wins)."""
    passes = {}
    order = []
    with open(log_path, errors="replace") as fh:
        for line in fh:
            m = PROFILER_RE.search(line)
            if not m:
                continue
            name = m.group(1).strip()
            gpu = float(m.group(3))
            if name not in passes:
                order.append(name)
            passes[name] = gpu
    return order, passes


def parse_telemetry(telemetry_path, skip=60):
    """deltaMs statistics and scope means from the telemetry JSON.

    Skips the first `skip` frames (startup transients), then reports the
    mean/median/p90/p99 of deltaMs, fps = 1000 / mean, and the mean CPU ms
    of gpu.gpu_wait, gpu.encode and render.pass.Deferred Light Volumes."""
    with open(telemetry_path) as fh:
        data = json.load(fh)
    frames = [f for f in data["frames"] if not f["startup"]][skip:]
    deltas = [f["deltaMs"] for f in frames if f["deltaMs"] > 0]

    def scope_mean(name):
        values = []
        for frame in frames:
            for scope in frame["scopes"]:
                if scope["name"] == name:
                    values.append(scope["totalMs"])
                    break
        return statistics.mean(values) if values else 0.0

    def quantile(values, p):
        ordered = sorted(values)
        return ordered[min(len(ordered) - 1, int(p * len(ordered)))]

    result = {
        "frames": len(deltas),
        "deltaMs": {
            "mean": statistics.mean(deltas),
            "median": statistics.median(deltas),
            "p90": quantile(deltas, 0.9),
            "p99": quantile(deltas, 0.99),
        },
        "fps": 1000.0 / statistics.mean(deltas),
        "scopeMeanMs": {
            "gpu.gpu_wait": scope_mean("gpu.gpu_wait"),
            "gpu.encode": scope_mean("gpu.encode"),
            "render.pass.Deferred Light Volumes":
                scope_mean("render.pass.Deferred Light Volumes"),
        },
    }
    return result


def build_args(exe, view, start, width, height, realtime, log_path, telemetry_path):
    args = ["--unattended", "--api", "vulkan", "--scene", "8",
            "--width", str(width), "--height", str(height),
            "--postProcessMode", "compute"]
    if view == "close":
        args += ["--backyard-camera"] + list(CLOSE_CAMERA)
    if start == "water":
        args += ["--backyard-start", "water"]
    if realtime:
        # HUD on (no --backyard-no-hud), no fixed dt: real-time frame pacing.
        # Telemetry frames are capped at 8192; profileFrames 900 is the WO spec.
        args += ["--profile", "--profileCpuOnly", "--profileFrames", "900",
                 "--telemetry", "--telemetryFrequencyFrames", "1",
                 "--telemetryOutput", str(telemetry_path)]
        # No --logFile: the telemetry JSON is the output; keep the console quiet.
    else:
        args += ["--backyard-no-hud", "--benchmarkFixedDt", "0.0166667",
                 "--profile", "--profileFrames", "400",
                 "--logLevel", "info", "--logFile", str(log_path)]
    return args


def measure_cell(exe, view, start, width, height, realtime, tmpdir, guard=True):
    """Run one cell with the contention guard. Returns (result, contended).

    With guard=False (--no-guard) there is no idle wait and no contention
    retries; the result is valid for a functional check only and must be
    labelled UNGUARDED in the report."""
    tag = f"{view}_{start}_{'rt' if realtime else 'gpu'}"
    for attempt in range(1, MAX_ATTEMPTS + 1):
        if guard and not wait_for_idle():
            return None, True
        log_path = Path(tmpdir) / f"{tag}_run{attempt}.log"
        telemetry_path = Path(tmpdir) / f"{tag}_run{attempt}.json"
        args = build_args(exe, view, start, width, height, realtime,
                           log_path, telemetry_path)
        clean, contended, code = run_once(exe, args, log_path, RUN_TIMEOUT)
        if guard and contended:
            print(f"[perf4k] {view}/{start} attempt {attempt}: contended, repeating")
            continue
        if not clean:
            print(f"[perf4k] {view}/{start} attempt {attempt}: exit {code}")
            continue
        if realtime:
            # The telemetry file is written with a timestamp suffix.
            candidates = sorted(Path(tmpdir).glob(f"{tag}_run{attempt}_*.json"))
            if not candidates:
                print(f"[perf4k] {view}/{start} attempt {attempt}: no telemetry output")
                continue
            try:
                return parse_telemetry(candidates[-1]), False
            except (OSError, json.JSONDecodeError, KeyError) as error:
                print(f"[perf4k] {view}/{start} attempt {attempt}: telemetry parse failed: {error}")
                continue
        try:
            order, passes = parse_profiler(log_path)
        except OSError as error:
            print(f"[perf4k] {view}/{start} attempt {attempt}: log read failed: {error}")
            continue
        if not passes:
            print(f"[perf4k] {view}/{start} attempt {attempt}: no [Profiler] lines in log")
            continue
        return {"order": order, "passes": passes}, False
    return None, True


def print_gpu_table(cells, views, starts, guarded=True):
    """cells: {(view, start): {"order": [...], "passes": {name: gpu_ms}} or None}"""
    if not guarded:
        print("UNGUARDED (not valid for comparison)")
    columns = [(v, s) for v in views for s in starts]
    header = f"{'pass':<28}" + "".join(f"{v + '/' + s:>16}" for v, s in columns)
    print(header)
    print("-" * len(header))
    order = []
    for v, s in columns:
        cell = cells.get((v, s))
        if cell:
            for name in cell["order"]:
                if name not in order:
                    order.append(name)
    totals = [0.0] * len(columns)
    for name in order:
        row = []
        for i, (v, s) in enumerate(columns):
            cell = cells.get((v, s))
            value = cell["passes"].get(name, 0.0) if cell else 0.0
            row.append(value)
            totals[i] += value
        if max(row) >= PASS_KEEP_THRESHOLD:
            print(f"{name[:28]:<28}" + "".join(f"{v:>16.3f}" for v in row))
    print(f"{'SUM (graph GPU)':<28}" + "".join(f"{v:>16.3f}" for v in totals))


def print_realtime(cells, views, starts, guarded=True):
    if not guarded:
        print("UNGUARDED (not valid for comparison)")
    columns = [(v, s) for v in views for s in starts]
    for v, s in columns:
        cell = cells.get((v, s))
        label = f"{v}/{s}"
        if cell is None:
            print(f"{label}: contended")
            continue
        d = cell["deltaMs"]
        m = cell["scopeMeanMs"]
        print(f"{label}: n={cell['frames']} delta mean={d['mean']:.2f} med={d['median']:.2f} "
              f"p90={d['p90']:.2f} p99={d['p99']:.2f} -> fps(mean)={cell['fps']:.0f}; "
              f"gpu_wait mean={m['gpu.gpu_wait']:.2f} encode={m['gpu.encode']:.2f} "
              f"Deferred Light Volumes={m['render.pass.Deferred Light Volumes']:.2f}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--views", default=",".join(VIEWS),
                        help="comma-separated views: default,close (default: both)")
    parser.add_argument("--starts", default=",".join(STARTS),
                        help="comma-separated starts: water,lake (default: both)")
    parser.add_argument("--width", type=int, default=3840)
    parser.add_argument("--height", type=int, default=2160)
    parser.add_argument("--runs", type=int, default=2,
                        help="runs per cell; the last run is reported (default: 2)")
    parser.add_argument("--realtime", action="store_true",
                        help="real-time mode: HUD on, no fixed dt, telemetry statistics")
    parser.add_argument("--no-guard", action="store_true",
                        help="skip the contention guard (no idle wait, no retries); "
                             "the output is labelled UNGUARDED and is not valid for comparison")
    parser.add_argument("--output", default=None,
                        help="write the results as JSON to this path")
    parser.add_argument("--day-scene", default=str(DEFAULT_EXE),
                        help="path to DayScene")
    args = parser.parse_args()

    views = [v.strip() for v in args.views.split(",") if v.strip()]
    starts = [s.strip() for s in args.starts.split(",") if s.strip()]
    for view in views:
        if view not in VIEWS:
            print(f"ERROR: unknown view '{view}' (expected one of {', '.join(VIEWS)})")
            return 2
    for start in starts:
        if start not in STARTS:
            print(f"ERROR: unknown start '{start}' (expected one of {', '.join(STARTS)})")
            return 2

    exe = Path(args.day_scene).resolve()
    if not exe.exists():
        print(f"ERROR: DayScene not found: {exe}")
        return 2

    guarded = not args.no_guard
    tmpdir = tempfile.mkdtemp(prefix="perf4k_")
    print(f"[perf4k] mode={'realtime' if args.realtime else 'gpu'} views={views} "
          f"starts={starts} {args.width}x{args.height} runs={args.runs} "
          f"guard={'on' if guarded else 'OFF (UNGUARDED)'}")
    try:
        cells = {}
        for view in views:
            for start in starts:
                result = None
                contended = False
                for run in range(1, args.runs + 1):
                    print(f"[perf4k] {view}/{start} run {run}/{args.runs}")
                    result, contended = measure_cell(exe, view, start,
                                                      args.width, args.height,
                                                      args.realtime, tmpdir,
                                                      guard=guarded)
                    if contended:
                        result = None
                    if result is None:
                        break
                cells[(view, start)] = result
                if result is None:
                    print(f"[perf4k] {view}/{start}: contended")
                else:
                    print(f"[perf4k] {view}/{start}: clean")

        print()
        if args.realtime:
            print_realtime(cells, views, starts, guarded=guarded)
        else:
            print_gpu_table(cells, views, starts, guarded=guarded)

        if args.output:
            payload = {
                "mode": "realtime" if args.realtime else "gpu",
                "views": views,
                "starts": starts,
                "width": args.width,
                "height": args.height,
                "runs": args.runs,
                "guarded": guarded,
                "cells": {},
            }
            for view in views:
                for start in starts:
                    cell = cells.get((view, start))
                    payload["cells"][f"{view}/{start}"] = (
                        cell if cell is not None else {"contended": True})
            with open(args.output, "w") as fh:
                json.dump(payload, fh, indent=2)
            print(f"[perf4k] wrote {args.output}")
        return 0
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
