import struct, sys, os
import math

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
            pixels.append((data[i], data[i+1], data[i+2]))
        return w, h, pixels

def analyze(name, w, h, pixels):
    # ShadowAccum is R8, so R=G=B or only R matters
    vals = [p[0] for p in pixels]
    mean = sum(vals) / len(vals)
    variance = sum((v - mean)**2 for v in vals) / len(vals)
    std = math.sqrt(variance)
    
    # Histogram
    hist = [0]*256
    for v in vals:
        hist[v] += 1
    
    # Local variance (3x3 neighborhood)
    local_vars = []
    for y in range(1, h-1):
        for x in range(1, w-1):
            neighbors = []
            for dy in range(-1, 2):
                for dx in range(-1, 2):
                    neighbors.append(vals[(y+dy)*w + (x+dx)])
            local_mean = sum(neighbors) / 9.0
            local_var = sum((n - local_mean)**2 for n in neighbors) / 9.0
            local_vars.append(local_var)
    
    avg_local_var = sum(local_vars) / len(local_vars)
    max_local_var = max(local_vars)
    
    # Count "noisy" pixels (high local variance)
    noisy_threshold = 100  # variance threshold
    noisy_count = sum(1 for v in local_vars if v > noisy_threshold)
    
    print(f"\n=== {name} ===")
    print(f"  Resolution: {w}x{h}")
    print(f"  Mean: {mean:.2f}")
    print(f"  Std:  {std:.2f}")
    print(f"  Min:  {min(vals)}, Max: {max(vals)}")
    print(f"  Avg Local Variance (3x3): {avg_local_var:.2f}")
    print(f"  Max Local Variance (3x3): {max_local_var:.2f}")
    print(f"  Noisy pixels (local_var > {noisy_threshold}): {noisy_count} ({100.0*noisy_count/len(local_vars):.2f}%)")
    
    # Show top histogram entries
    top_vals = sorted(range(256), key=lambda i: hist[i], reverse=True)[:15]
    print(f"  Top 15 values:")
    for v in top_vals:
        print(f"    val={v:3d}  count={hist[v]:7d}  ({100.0*hist[v]/len(vals):.2f}%)")

gl_dir = sys.argv[1]
d3d_dir = sys.argv[2]

gl_path = os.path.join(gl_dir, "RT_Dump_ShadowAccum.ppm")
d3d_path = os.path.join(d3d_dir, "RT_Dump_ShadowAccum.ppm")

w1, h1, px1 = read_ppm(gl_path)
w2, h2, px2 = read_ppm(d3d_path)

analyze("GL ShadowAccum", w1, h1, px1)
analyze("D3D11 ShadowAccum", w2, h2, px2)

# Also compare per-pixel difference spatially
print("\n=== Spatial Analysis ===")
# Divide into quadrants
qw, qh = w1//2, h1//2
for qy, qname_y in enumerate(["Top", "Bottom"]):
    for qx, qname_x in enumerate(["Left", "Right"]):
        gl_vals = []
        d3d_vals = []
        diffs = []
        for y in range(qy*qh, (qy+1)*qh):
            for x in range(qx*qw, (qx+1)*qw):
                idx = y * w1 + x
                gv = px1[idx][0]
                dv = px2[idx][0]
                gl_vals.append(gv)
                d3d_vals.append(dv)
                diffs.append(abs(gv - dv))
        gl_mean = sum(gl_vals)/len(gl_vals)
        d3d_mean = sum(d3d_vals)/len(d3d_vals)
        diff_mean = sum(diffs)/len(diffs)
        diff_count = sum(1 for d in diffs if d > 0)
        print(f"  {qname_y}-{qname_x}: GL_mean={gl_mean:.1f} D3D_mean={d3d_mean:.1f} diff_mean={diff_mean:.2f} diff_px={diff_count} ({100*diff_count/len(diffs):.1f}%)")
