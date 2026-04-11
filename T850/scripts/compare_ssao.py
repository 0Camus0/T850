"""Compare two PPM files and report differences."""
import sys

def read_ppm(path):
    with open(path, 'rb') as f:
        magic = f.readline().strip()
        # skip comments
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        w, h = map(int, line.split())
        maxval = int(f.readline().strip())
        data = f.read()
    return w, h, maxval, data

d3d_path = sys.argv[1]
gl_path = sys.argv[2]

w1, h1, m1, d1 = read_ppm(d3d_path)
w2, h2, m2, d2 = read_ppm(gl_path)

print(f"D3D11: {w1}x{h1} maxval={m1} bytes={len(d1)}")
print(f"GL:    {w2}x{h2} maxval={m2} bytes={len(d2)}")

if w1 != w2 or h1 != h2:
    print("ERROR: Different dimensions!")
    sys.exit(1)

total_pixels = w1 * h1
diff_pixels = 0
total_diff = 0
max_diff = 0
d3d_brighter = 0
gl_brighter = 0

# Sample some specific pixels for debugging
sample_rows = [0, h1//4, h1//2, 3*h1//4, h1-1]
sample_cols = [0, w1//4, w1//2, 3*w1//4, w1-1]

for y in range(h1):
    for x in range(w1):
        idx = (y * w1 + x) * 3
        r1, g1, b1 = d1[idx], d1[idx+1], d1[idx+2]
        r2, g2, b2 = d2[idx], d2[idx+1], d2[idx+2]
        dr = abs(r1 - r2)
        dg = abs(g1 - g2)
        db = abs(b1 - b2)
        d_max = max(dr, dg, db)
        if d_max > 0:
            diff_pixels += 1
            total_diff += d_max
            max_diff = max(max_diff, d_max)
            if r1 > r2:
                d3d_brighter += 1
            elif r2 > r1:
                gl_brighter += 1
        
        if y in sample_rows and x in sample_cols:
            print(f"  pixel({x:4d},{y:4d}): D3D=({r1:3d},{g1:3d},{b1:3d}) GL=({r2:3d},{g2:3d},{b2:3d}) diff=({dr},{dg},{db})")

print(f"\nTotal pixels: {total_pixels}")
print(f"Different pixels: {diff_pixels} ({100*diff_pixels/total_pixels:.1f}%)")
print(f"Max diff: {max_diff}")
print(f"Avg diff (of different): {total_diff/max(diff_pixels,1):.2f}")
print(f"D3D brighter: {d3d_brighter}, GL brighter: {gl_brighter}")

# Distribution of differences
buckets = [0]*11  # 0, 1-5, 6-10, 11-20, 21-30, 31-50, 51-70, 71-100, 101-150, 151-200, 201+
for y in range(h1):
    for x in range(w1):
        idx = (y * w1 + x) * 3
        d_max = max(abs(d1[idx]-d2[idx]), abs(d1[idx+1]-d2[idx+1]), abs(d1[idx+2]-d2[idx+2]))
        if d_max == 0: buckets[0] += 1
        elif d_max <= 5: buckets[1] += 1
        elif d_max <= 10: buckets[2] += 1
        elif d_max <= 20: buckets[3] += 1
        elif d_max <= 30: buckets[4] += 1
        elif d_max <= 50: buckets[5] += 1
        elif d_max <= 70: buckets[6] += 1
        elif d_max <= 100: buckets[7] += 1
        elif d_max <= 150: buckets[8] += 1
        elif d_max <= 200: buckets[9] += 1
        else: buckets[10] += 1

labels = ["0","1-5","6-10","11-20","21-30","31-50","51-70","71-100","101-150","151-200","201+"]
print("\nDifference distribution:")
for l, b in zip(labels, buckets):
    pct = 100*b/total_pixels
    bar = '#' * int(pct)
    print(f"  {l:>7s}: {b:7d} ({pct:5.1f}%) {bar}")
