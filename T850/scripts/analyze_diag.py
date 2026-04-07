#!/usr/bin/env python3
"""Analyze height/viewDir/deltaTexCoords diagnostic from PPM dumps."""
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

d3d_dir = sys.argv[1]
gl_dir  = sys.argv[2]

w_d, h_d, data_d = read_ppm(os.path.join(d3d_dir, 'RT_Dump_GBuffer_Color0.ppm'))
w_g, h_g, data_g = read_ppm(os.path.join(gl_dir,  'RT_Dump_GBuffer_Color0.ppm'))

print(f"D3D11: {w_d}x{h_d}, GL: {w_g}x{h_g}")
print()

# R=height, G=viewDir.y sign, B=deltaTexCoords.y sign
test_coords = [
    (640, 200, "wall upper"),
    (700, 300, "wall mid"),
    (800, 250, "wall right"),
    (500, 500, "floor"),
    (600, 400, "floor edge"),
    (640, 150, "wall top"),
    (900, 350, "wall far right"),
]

fmt_hdr = "{:<20s} {:>8s} {:>5s} {:>5s}   {:>8s} {:>5s} {:>5s}   {:>8s}"
fmt_row = "{:<20s} {:8.4f} {:>5s} {:>5s}   {:8.4f} {:>5s} {:>5s}   {:8.4f}"
print(fmt_hdr.format("Location", "D3D:H", "vdY", "dtY", "GL:H", "vdY", "dtY", "H_diff"))
print("-" * 90)
for x, y, label in test_coords:
    idx = (y * w_d + x) * 3
    dr, dg, db = data_d[idx], data_d[idx+1], data_d[idx+2]
    gr, gg, gb = data_g[idx], data_g[idx+1], data_g[idx+2]
    h_d3d = dr / 255.0
    h_gl  = gr / 255.0
    vdy_d = "+" if dg > 127 else "-"
    vdy_g = "+" if gg > 127 else "-"
    dty_d = "+" if db > 127 else "-"
    dty_g = "+" if gb > 127 else "-"
    print(fmt_row.format(label, h_d3d, vdy_d, dty_d, h_gl, vdy_g, dty_g, abs(h_d3d-h_gl)))

# Count sign differences across all parallax pixels
vdy_same = vdy_diff = dty_same = dty_diff = 0
h_total_diff = h_count = 0
for i in range(0, min(len(data_d), len(data_g)), 3):
    dr, dg, db = data_d[i], data_d[i+1], data_d[i+2]
    gr, gg, gb = data_g[i], data_g[i+1], data_g[i+2]
    # Only parallax surfaces (G and B are binary 0 or 255)
    if dg in (0, 255) and gg in (0, 255):
        if dg == gg:
            vdy_same += 1
        else:
            vdy_diff += 1
        if db == gb:
            dty_same += 1
        else:
            dty_diff += 1
        h_total_diff += abs(dr - gr)
        h_count += 1

print()
print(f"Parallax surface pixels: {h_count}")
print(f"viewDir.y sign:        same={vdy_same}, diff={vdy_diff}")
print(f"deltaTexCoords.y sign: same={dty_same}, diff={dty_diff}")
if h_count > 0:
    print(f"Avg height diff: {h_total_diff/h_count:.4f} (out of 255)")
