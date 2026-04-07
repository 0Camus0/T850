#!/usr/bin/env python3
"""Compare PPM dumps from D3D11 and GL backends pixel-by-pixel."""
import os, sys

def read_ppm(path):
    with open(path, 'rb') as f:
        magic = f.readline().strip()
        while True:
            line = f.readline().strip()
            if not line.startswith(b'#'):
                break
        w, h = map(int, line.split())
        maxv = int(f.readline().strip())
        data = f.read()
    return w, h, data

def compare(d3d_dir, gl_dir):
    d3d_files = sorted([f for f in os.listdir(d3d_dir) if f.endswith('.ppm')])
    gl_files  = sorted([f for f in os.listdir(gl_dir)  if f.endswith('.ppm')])
    common = sorted(set(d3d_files) & set(gl_files))

    hdr = f"{'RT Name':35s} {'TotalPx':>10s} {'DiffPx':>10s} {'Diff%':>8s} {'Max':>5s} {'AvgDiff':>8s}"
    print(hdr)
    print('-' * len(hdr))

    for name in common:
        w1, h1, d1 = read_ppm(os.path.join(d3d_dir, name))
        w2, h2, d2 = read_ppm(os.path.join(gl_dir, name))
        if (w1, h1) != (w2, h2):
            print(f"{name:35s} SIZE MISMATCH {w1}x{h1} vs {w2}x{h2}")
            continue
        total = w1 * h1
        diff_count = 0
        max_diff = 0
        total_diff = 0
        nbytes = min(len(d1), len(d2))
        for i in range(0, nbytes, 3):
            r1, g1, b1 = d1[i], d1[i+1], d1[i+2]
            r2, g2, b2 = d2[i], d2[i+1], d2[i+2]
            d = max(abs(r1-r2), abs(g1-g2), abs(b1-b2))
            if d > 0:
                diff_count += 1
                total_diff += d
                if d > max_diff:
                    max_diff = d
        avg = total_diff / diff_count if diff_count else 0
        pct = 100.0 * diff_count / total
        print(f"{name:35s} {total:10d} {diff_count:10d} {pct:7.2f}% {max_diff:5d} {avg:8.2f}")

if __name__ == '__main__':
    if len(sys.argv) == 3:
        compare(sys.argv[1], sys.argv[2])
    else:
        print("Usage: compare_dumps.py <d3d11_dir> <gl_dir>")
