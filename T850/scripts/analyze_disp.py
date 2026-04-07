#!/usr/bin/env python3
"""Analyze parallax displacement diagnostic: R=(dispX*200+0.5), G=(dispY*200+0.5), B=height"""
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

# R=dispX*200+0.5, G=dispY*200+0.5, B=height
# disp = (channel/255.0 - 0.5) / 200.0

test_coords = [
    (640, 200, "wall upper"),
    (700, 300, "wall mid"),
    (800, 250, "wall right"),
    (500, 500, "floor center"),
    (600, 400, "floor edge"),
    (640, 360, "wall-floor"),
    (750, 180, "wall mortar"),
    (850, 280, "wall brick"),
]

hdr = "{:<16s} {:>10s} {:>10s} {:>8s}   {:>10s} {:>10s} {:>8s}   {:>10s} {:>10s}"
row = "{:<16s} {:10.6f} {:10.6f} {:8.4f}   {:10.6f} {:10.6f} {:8.4f}   {:10.6f} {:10.6f}"
print(hdr.format("Location", "D3D:dX", "D3D:dY", "D3D:H", "GL:dX", "GL:dY", "GL:H", "dX_diff", "dY_diff"))
print("-" * 120)
for x, y, label in test_coords:
    idx = (y * w_d + x) * 3
    dr, dg, db = data_d[idx], data_d[idx+1], data_d[idx+2]
    gr, gg, gb = data_g[idx], data_g[idx+1], data_g[idx+2]
    d3d_dx = (dr/255.0 - 0.5) / 200.0
    d3d_dy = (dg/255.0 - 0.5) / 200.0
    d3d_h  = db/255.0
    gl_dx  = (gr/255.0 - 0.5) / 200.0
    gl_dy  = (gg/255.0 - 0.5) / 200.0
    gl_h   = gb/255.0
    print(row.format(label, d3d_dx, d3d_dy, d3d_h, gl_dx, gl_dy, gl_h, d3d_dx-gl_dx, d3d_dy-gl_dy))

# Scan all parallax pixels for sign analysis
dx_same = dx_diff = dy_same = dy_diff = 0
dy_sum_d3d = dy_sum_gl = 0.0
count = 0
for i in range(0, min(len(data_d), len(data_g)), 3):
    dr, dg, db = data_d[i], data_d[i+1], data_d[i+2]
    gr, gg, gb = data_g[i], data_g[i+1], data_g[i+2]
    # Parallax pixels: displacement is non-zero (R or G not 128)
    if abs(dr - 128) > 2 or abs(dg - 128) > 2:
        d_dx = dr/255.0 - 0.5
        d_dy = dg/255.0 - 0.5
        g_dx = gr/255.0 - 0.5
        g_dy = gg/255.0 - 0.5
        if (d_dx > 0) == (g_dx > 0):
            dx_same += 1
        else:
            dx_diff += 1
        if (d_dy > 0) == (g_dy > 0):
            dy_same += 1
        else:
            dy_diff += 1
        dy_sum_d3d += d_dy
        dy_sum_gl += g_dy
        count += 1

print()
print(f"Parallax pixels with displacement: {count}")
print(f"dispX sign: same={dx_same}, different={dx_diff}")
print(f"dispY sign: same={dy_same}, different={dy_diff}")
if count > 0:
    print(f"Avg dispY: D3D={dy_sum_d3d/count:.6f}, GL={dy_sum_gl/count:.6f}")
    print(f"Avg dispY ratio: {dy_sum_d3d/dy_sum_gl:.4f}" if abs(dy_sum_gl) > 0.0001 else "GL dispY ~0")
