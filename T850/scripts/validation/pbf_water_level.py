#!/usr/bin/env python3
"""Measure the settled PBF water level from a T850_PBF_DUMP CSV.

The level is the median column-top height plus half a particle spacing, excluding the
step region at the +X/-Z corner. Scene 7's plane is -0.040 m.
"""
import csv
import statistics
import sys

SPACING = 0.13
BASIN_MIN_X, BASIN_MAX_X = -4.5, 4.5
BASIN_MIN_Z, BASIN_MAX_Z = -9.0, -4.5


def measure(csv_path, spacing=SPACING):
    rows = [r for r in csv.DictReader(open(csv_path)) if float(r["alive"]) > 0.5]
    tops = {}
    for r in rows:
        x, y, z = float(r["x"]), float(r["y"]), float(r["z"])
        if not (BASIN_MIN_X <= x <= BASIN_MAX_X and BASIN_MIN_Z <= z <= BASIN_MAX_Z):
            continue
        # Exclude the submerged steps at the +X/-Z corner.
        if x > 3.3 and z > -7.1:
            continue
        key = (int((x - BASIN_MIN_X) // (2 * spacing)),
               int((z - BASIN_MIN_Z) // (2 * spacing)))
        tops[key] = max(tops.get(key, -9.0), y)
    if not tops:
        raise ValueError(f"no pool columns in {csv_path}")
    return statistics.median(tops.values()) + spacing / 2.0


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv")
    parser.add_argument("--target", type=float, default=-0.040)
    parser.add_argument("--tolerance", type=float, default=0.015)
    args = parser.parse_args()
    level = measure(args.csv)
    ok = abs(level - args.target) <= args.tolerance
    print(f"level {level:+.4f} m  target {args.target:+.3f} +/- {args.tolerance:.3f}  "
          f"{'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
