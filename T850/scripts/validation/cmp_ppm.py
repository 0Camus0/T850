import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts, pos = [], 0
    while len(parts) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        parts.append(data[start:pos])
    pos += 1
    w, h = int(parts[1]), int(parts[2])
    ch = 3 if parts[0] == b"P6" else 1
    pix = data[pos:pos + w * h * ch]
    # luminance-ish: first channel is enough for single-value targets; mean of 3 otherwise
    vals = [sum(pix[i * ch:(i + 1) * ch]) / ch for i in range(w * h)]
    return w, h, vals


a_path, b_path = sys.argv[1], sys.argv[2]
threshold = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
w, h, a = read_ppm(a_path)
w2, h2, b = read_ppm(b_path)
assert (w, h) == (w2, h2), "size mismatch"
diff = [i for i in range(w * h) if abs(a[i] - b[i]) > threshold]
nz_a = sum(1 for v in a if v > 0)
nz_b = sum(1 for v in b if v > 0)
print(f"{w}x{h}  nonzero A {nz_a}  B {nz_b}  mean A {sum(a)/len(a):.3f}  B {sum(b)/len(b):.3f}")
if not diff:
    print(f"0 pixels differ by > {threshold}")
    sys.exit(0)
xs = [i % w for i in diff]
ys = [i // w for i in diff]
deltas = [a[i] - b[i] for i in diff]
print(f"{len(diff)} pixels differ by > {threshold}  bbox x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)}  "
      f"mean(A-B) {sum(deltas)/len(deltas):+.2f}  max|A-B| {max(abs(d) for d in deltas):.1f}")
