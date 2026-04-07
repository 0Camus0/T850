import sys, os

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

gl_dir = sys.argv[1]
d3d_dir = sys.argv[2]

w, h, gl_px = read_ppm(os.path.join(gl_dir, "RT_Dump_ShadowAccum.ppm"))
_, _, d3d_px = read_ppm(os.path.join(d3d_dir, "RT_Dump_ShadowAccum.ppm"))

# R = SHTC.x * 255 (clamped to [0,1])
# G = SHTC.y * 255 (clamped to [0,1]) - note: HLSL has Y-flip, GLSL doesn't
# B = wOk*0.5 + zOk*0.25 = {0.75 if both ok, 0.5 if w ok but z >= 1, 0.25 if w <= 0 but z ok, 0 if both fail}
#   in R8: 0.75*255=191, 0.5*255=128, 0.25*255=64, 0=0

# Analyze B channel (w/z flags)
print("=== GL B channel (w/z flags) ===")
gl_b = [p[2] for p in gl_px]
b_hist = {}
for b in gl_b:
    b_hist[b] = b_hist.get(b, 0) + 1
for b in sorted(b_hist.keys(), key=lambda x: -b_hist[x])[:10]:
    label = ""
    if b >= 188: label = "w>0 AND z<1 (both OK)"
    elif b >= 125: label = "w>0 BUT z>=1 (far plane!)"
    elif b >= 62: label = "w<=0 BUT z<1 (behind light!)"
    else: label = "BOTH BAD"
    print(f"  B={b:3d}  count={b_hist[b]:7d} ({100*b_hist[b]/len(gl_b):.1f}%)  => {label}")

print("\n=== D3D11 B channel (w/z flags) ===")
d3d_b = [p[2] for p in d3d_px]
b_hist = {}
for b in d3d_b:
    b_hist[b] = b_hist.get(b, 0) + 1
for b in sorted(b_hist.keys(), key=lambda x: -b_hist[x])[:10]:
    label = ""
    if b >= 188: label = "w>0 AND z<1 (both OK)"
    elif b >= 125: label = "w>0 BUT z>=1 (far plane!)"
    elif b >= 62: label = "w<=0 BUT z<1 (behind light!)"
    else: label = "BOTH BAD"
    print(f"  B={b:3d}  count={b_hist[b]:7d} ({100*b_hist[b]/len(d3d_b):.1f}%)  => {label}")

# Analyze SHTC.x (R channel)
print("\n=== SHTC.x (R channel) ranges ===")
for name, px in [("GL", gl_px), ("D3D11", d3d_px)]:
    rs = [p[0] for p in px]
    in_range = sum(1 for r in rs if 1 <= r <= 254)  # SHTC.x in (0.004, 0.996) 
    print(f"  {name}: min={min(rs)} max={max(rs)} mean={sum(rs)/len(rs):.1f} in_range(1-254)={in_range} ({100*in_range/len(rs):.1f}%)")

# Analyze SHTC.y (G channel) - remember HLSL has Y flip, GLSL doesn't
print("\n=== SHTC.y (G channel) ===")
for name, px in [("GL", gl_px), ("D3D11", d3d_px)]:
    gs = [p[1] for p in px]
    in_range = sum(1 for g in gs if 1 <= g <= 254)
    print(f"  {name}: min={min(gs)} max={max(gs)} mean={sum(gs)/len(gs):.1f} in_range(1-254)={in_range} ({100*in_range/len(gs):.1f}%)")

# Check: For SHTC to be in frustum, need both X and Y in (0,1) AND both w>0 and z<1
# Let's count how many pixels pass ALL conditions
print("\n=== Pixels passing all frustum conditions ===")
for name, px in [("GL", gl_px), ("D3D11", d3d_px)]:
    count = 0
    for p in px:
        r, g, b = p
        shtc_x_ok = (r >= 1 and r <= 254)  # SHTC.x in approximately (0.004, 0.996)
        shtc_y_ok = (g >= 1 and g <= 254)  # SHTC.y in approximately (0.004, 0.996) 
        wz_ok = (b >= 188)  # both w>0 and z<1
        if shtc_x_ok and shtc_y_ok and wz_ok:
            count += 1
    print(f"  {name}: {count} ({100*count/len(px):.1f}%)")

# Sample some pixels at a specific location to compare exact values
print("\n=== Sample pixels (center of image) ===")
cy, cx = h//2, w//2
for dy in range(-2, 3):
    for dx in range(-2, 3):
        idx = (cy+dy)*w + (cx+dx)
        gp = gl_px[idx]
        dp = d3d_px[idx]
        print(f"  ({cx+dx},{cy+dy}) GL=({gp[0]:3d},{gp[1]:3d},{gp[2]:3d}) D3D=({dp[0]:3d},{dp[1]:3d},{dp[2]:3d})")

# Sample at the boundary area (top-right)
print("\n=== Sample pixels (top-right, X=700, Y=150) ===")
cy, cx = 150, 700
for dy in range(-2, 3):
    for dx in range(-2, 3):
        idx = (cy+dy)*w + (cx+dx)
        gp = gl_px[idx]
        dp = d3d_px[idx]
        print(f"  ({cx+dx},{cy+dy}) GL=({gp[0]:3d},{gp[1]:3d},{gp[2]:3d}) D3D=({dp[0]:3d},{dp[1]:3d},{dp[2]:3d})")
