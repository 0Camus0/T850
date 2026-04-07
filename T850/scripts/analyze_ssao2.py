import struct, sys, os

def read_ppm(path):
    with open(path, "rb") as f:
        header = f.readline().decode().strip()
        assert header == "P6"
        while True:
            line = f.readline().decode().strip()
            if not line.startswith("#"):
                break
        w, h = map(int, line.split())
        maxval = int(f.readline().decode().strip())
        data = f.read()
        pixels = []
        for i in range(0, len(data), 3):
            pixels.append(data[i])
        return w, h, pixels

d3d_dir = sys.argv[1]
gl_dir = sys.argv[2]

w, h, d3d_px = read_ppm(os.path.join(d3d_dir, "RT_Dump_ShadowAccum.ppm"))
_, _, gl_px = read_ppm(os.path.join(gl_dir, "RT_Dump_ShadowAccum.ppm"))

# Find pixels where D3D11 value > 100 (clearly bright = shadow boundary issue)
bright_pixels = []
for y in range(h):
    for x in range(w):
        idx = y * w + x
        if d3d_px[idx] > 100:
            bright_pixels.append((x, y, d3d_px[idx], gl_px[idx]))

print(f"D3D11 pixels > 100: {len(bright_pixels)}")
if bright_pixels:
    xs = [p[0] for p in bright_pixels]
    ys = [p[1] for p in bright_pixels]
    print(f"  X range: {min(xs)}-{max(xs)}")
    print(f"  Y range: {min(ys)}-{max(ys)}")
    
    # Bounding box
    print(f"  Bounding box: ({min(xs)},{min(ys)}) to ({max(xs)},{max(ys)})")
    
    # What are GL values at these positions?
    gl_vals = [p[3] for p in bright_pixels]
    d3d_vals_b = [p[2] for p in bright_pixels]
    print(f"  GL vals at these pixels: min={min(gl_vals)} max={max(gl_vals)} mean={sum(gl_vals)/len(gl_vals):.1f}")
    print(f"  D3D vals at these pixels: min={min(d3d_vals_b)} max={max(d3d_vals_b)} mean={sum(d3d_vals_b)/len(d3d_vals_b):.1f}")

# Now check local variance EXCLUDING the shadow boundary (only pixels where D3D < 80)
print("\n=== Local variance excluding shadow boundary ===")
for name, px in [("GL", gl_px), ("D3D11", d3d_px)]:
    local_vars = []
    for y in range(1, h-1):
        for x in range(1, w-1):
            idx = y * w + x
            if d3d_px[idx] > 80:  # skip shadow boundary pixels
                continue
            neighbors = []
            for dy in range(-1, 2):
                for dx in range(-1, 2):
                    neighbors.append(px[(y+dy)*w + (x+dx)])
            local_mean = sum(neighbors) / 9.0
            local_var = sum((n - local_mean)**2 for n in neighbors) / 9.0
            local_vars.append(local_var)
    
    avg_lv = sum(local_vars)/len(local_vars) if local_vars else 0
    noisy = sum(1 for v in local_vars if v > 100)
    print(f"  {name}: pixels={len(local_vars)} avg_local_var={avg_lv:.2f} noisy(>100)={noisy} ({100*noisy/max(len(local_vars),1):.3f}%)")
