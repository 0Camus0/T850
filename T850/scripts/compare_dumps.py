#!/usr/bin/env python3
"""Compare T850 PPM snapshot dumps.

The default two-argument mode keeps the original table output, but the script
now uses the same comparison engine as the MCP server so it can also emit JSON,
apply tolerances, and generate HTML reports with PPM heatmaps.
"""

import argparse
import json
import sys

from t850_snapshot_mcp import compare_snapshots, generate_visual_report


def print_table(result):
    hdr = (
        f"{'RT Name':35s} {'TotalPx':>10s} {'DiffPx':>10s} {'Diff%':>8s} "
        f"{'Max':>5s} {'AvgDiff':>8s} {'AvgLuma':>8s}"
    )
    print(hdr)
    print("-" * len(hdr))

    for item in result["comparisons"]:
        name = item["target"]
        if item["status"] == "size_mismatch":
            ref = item["reference_size"]
            cand = item["candidate_size"]
            print(f"{name:35s} SIZE MISMATCH {ref[0]}x{ref[1]} vs {cand[0]}x{cand[1]}")
            continue

        print(
            f"{name:35s} "
            f"{item['total_pixels']:10d} "
            f"{item['diff_pixels']:10d} "
            f"{item['diff_percent']:7.2f}% "
            f"{item['max_channel_delta']:5d} "
            f"{item['avg_changed_pixel_delta']:8.2f} "
            f"{item['avg_luminance_delta']:8.2f}"
        )

    missing_candidate = result.get("missing_in_candidate") or []
    missing_reference = result.get("missing_in_reference") or []
    if missing_candidate:
        print("\nMissing in candidate:")
        for name in missing_candidate:
            print(f"  {name}")
    if missing_reference:
        print("\nMissing in reference:")
        for name in missing_reference:
            print(f"  {name}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference_dir", help="Reference snapshot directory, such as a D3D11 dump.")
    parser.add_argument("candidate_dir", help="Candidate snapshot directory, such as a GL dump.")
    parser.add_argument("--tolerance", type=int, default=0, help="Ignore per-pixel max-channel deltas up to this value.")
    parser.add_argument("--json", action="store_true", help="Print full comparison metrics as JSON.")
    parser.add_argument("--report", help="Write an HTML report and PPM heatmaps to this directory.")
    args = parser.parse_args(argv)

    if args.report:
        result = generate_visual_report(args.reference_dir, args.candidate_dir, args.report, tolerance=args.tolerance)
    else:
        result = compare_snapshots(args.reference_dir, args.candidate_dir, tolerance=args.tolerance)

    if args.json or args.report:
        print(json.dumps(result, indent=2))
    else:
        print_table(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
