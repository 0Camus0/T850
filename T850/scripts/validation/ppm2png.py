"""Crop a binary PPM (P6) frame dump to a validated PNG.

usage: ppm2png.py <in.ppm> <out.png> [x0 y0 x1 y1] [scale]
"""
import struct
import sys
import zlib


def read_ppm(path):
    data = open(path, "rb").read()
    fields, i = [], 0
    while len(fields) < 4:
        while data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while data[i:i + 1] not in (b"\n", b""):
                i += 1
            continue
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        fields.append(data[i:j])
        i = j
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError(f"unsupported PPM {fields}")
    w, h = int(fields[1]), int(fields[2])
    pixels = data[i + 1:i + 1 + w * h * 3]
    if len(pixels) != w * h * 3:
        raise ValueError("truncated PPM")
    return w, h, pixels


def main():
    src, dst = sys.argv[1], sys.argv[2]
    w, h, px = read_ppm(src)
    x0, y0, x1, y1 = (int(v) for v in sys.argv[3:7]) if len(sys.argv) >= 7 else (0, 0, w, h)
    scale = int(sys.argv[7]) if len(sys.argv) >= 8 else 1
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(w, x1), min(h, y1)
    if x1 <= x0 or y1 <= y0:
        raise ValueError("empty crop")
    rows = []
    for y in range(y0, y1):
        line = px[(y * w + x0) * 3:(y * w + x1) * 3]
        if scale > 1:
            line = b"".join(line[k:k + 3] * scale for k in range(0, len(line), 3))
        rows.extend([b"\x00" + line] * scale)
    out_w, out_h = (x1 - x0) * scale, (y1 - y0) * scale
    raw = b"".join(rows)
    if len(raw) != out_h * (1 + out_w * 3) or out_w > 2000 or out_h > 2000:
        raise ValueError("refusing to write an invalid or oversized PNG")

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", out_w, out_h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    open(dst, "wb").write(png)
    print(f"ok {out_w}x{out_h} {len(png)} bytes")


if __name__ == "__main__":
    main()
