#!/usr/bin/env python3
"""Compare GBuffer normals between D3D11 and GL at specific pixel locations."""
import os, sys
from PIL import Image
import numpy as np

d3d_dir = sys.argv[1]
gl_dir  = sys.argv[2]

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

# Read normals (RT1)
w_d, h_d, nd = read_ppm(os.path.join(d3d_dir, 'RT_Dump_GBuffer_Normals.ppm'))
w_g, h_g, ng = read_ppm(os.path.join(gl_dir,  'RT_Dump_GBuffer_Normals.ppm'))

# Read color (RT0) for reference
_, _, cd = read_ppm(os.path.join(d3d_dir, 'RT_Dump_GBuffer_Color0.ppm'))
_, _, cg = read_ppm(os.path.join(gl_dir,  'RT_Dump_GBuffer_Color0.ppm'))

# Wall/floor brick mortar areas
test_coords = [
    (640, 200, "wall brick"),
    (660, 200, "wall mortar-h"),
    (640, 210, "wall mortar-v"),
    (700, 300, "wall brick2"),
    (720, 300, "wall mortar2-h"),
    (700, 310, "wall mortar2-v"),
    (500, 500, "floor brick"),
    (520, 500, "floor mortar-h"),
    (500, 510, "floor mortar-v"),
    (300, 400, "column"),
    (100, 300, "curtain"),
]

hdr = "{:<18s} {:>8s} {:>8s} {:>8s}   {:>8s} {:>8s} {:>8s}   {:>8s} {:>8s} {:>8s}"
row = "{:<18s} {:8.4f} {:8.4f} {:8.4f}   {:8.4f} {:8.4f} {:8.4f}   {:8.4f} {:8.4f} {:8.4f}"
print(hdr.format("Location", "D3D:Nx", "D3D:Ny", "D3D:Nz", "GL:Nx", "GL:Ny", "GL:Nz", "dNx", "dNy", "dNz"))
print("-" * 120)

for x, y, label in test_coords:
    idx = (y * w_d + x) * 3
    # Normals stored as n*0.5+0.5
    d_nx = (nd[idx]/255.0 - 0.5) * 2.0
    d_ny = (nd[idx+1]/255.0 - 0.5) * 2.0
    d_nz = (nd[idx+2]/255.0 - 0.5) * 2.0
    g_nx = (ng[idx]/255.0 - 0.5) * 2.0
    g_ny = (ng[idx+1]/255.0 - 0.5) * 2.0
    g_nz = (ng[idx+2]/255.0 - 0.5) * 2.0
    print(row.format(label, d_nx, d_ny, d_nz, g_nx, g_ny, g_nz, d_nx-g_nx, d_ny-g_ny, d_nz-g_nz))

# Generate diff images of normals
print("\nGenerating normal diff visualization...")
outdir = os.path.dirname(d3d_dir)

# Create difference image of normals (amplified 10x)
nbytes = min(len(nd), len(ng))
diff_data = bytearray(nbytes)
for i in range(0, nbytes, 3):
    for c in range(3):
        d = abs(int(nd[i+c]) - int(ng[i+c]))
        diff_data[i+c] = min(255, d * 10)

with open(os.path.join(outdir, 'diff_normals_10x.ppm'), 'wb') as f:
    f.write(f'P6\n{w_d} {h_d}\n255\n'.encode())
    f.write(bytes(diff_data))

# Convert to PNG
img = Image.open(os.path.join(outdir, 'diff_normals_10x.ppm'))
img.save(os.path.join(outdir, 'diff_normals_10x.png'))
print("Saved diff_normals_10x.png")

# Also create signed diff (centered at 128): where GL normal > D3D normal, channel is brighter
signed_data = bytearray(nbytes)
for i in range(0, nbytes, 3):
    for c in range(3):
        d = int(ng[i+c]) - int(nd[i+c])
        signed_data[i+c] = max(0, min(255, 128 + d * 5))

with open(os.path.join(outdir, 'diff_normals_signed.ppm'), 'wb') as f:
    f.write(f'P6\n{w_d} {h_d}\n255\n'.encode())
    f.write(bytes(signed_data))
img = Image.open(os.path.join(outdir, 'diff_normals_signed.ppm'))
img.save(os.path.join(outdir, 'diff_normals_signed.png'))
print("Saved diff_normals_signed.png")
