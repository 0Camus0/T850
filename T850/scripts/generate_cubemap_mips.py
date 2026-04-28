#!/usr/bin/env python3
"""Generate offline mip chains for DDS cubemaps.

The engine uploads source DDS mips directly when they exist. This utility keeps
startup work out of the renderer by adding missing DDS mip levels ahead of time.
It supports legacy DDS cubemaps in uncompressed 24/32-bit color and legacy
RGBA16F HDR (D3DFMT_A16B16G16R16F). DXT/BC cubemaps are reported and skipped;
those require a texture compressor if a source file ever lacks mips.
"""

from __future__ import annotations

import argparse
import math
import shutil
import struct
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


DDS_MAGIC = b"DDS "
DDSD_MIPMAPCOUNT = 0x00020000
DDSCAPS_COMPLEX = 0x00000008
DDSCAPS_MIPMAP = 0x00400000
DDSCAPS_TEXTURE = 0x00001000
DDSCAPS2_CUBEMAP_ALL_FACES = 0x0000FE00
DDPF_FOURCC = 0x00000004
DDPF_RGB = 0x00000040
DDPF_ALPHAPIXELS = 0x00000001
D3DFMT_A16B16G16R16F = 113


@dataclass(frozen=True)
class DDSInfo:
    path: Path
    width: int
    height: int
    mip_count: int
    flags: int
    pitch_or_linear_size: int
    pixel_format_flags: int
    fourcc: bytes
    rgb_bit_count: int
    caps: int
    caps2: int
    header_size: int

    @property
    def is_cubemap(self) -> bool:
        return (self.caps2 & DDSCAPS2_CUBEMAP_ALL_FACES) == DDSCAPS2_CUBEMAP_ALL_FACES

    @property
    def face_count(self) -> int:
        return 6 if self.is_cubemap else 1

    @property
    def fourcc_int(self) -> int:
        return struct.unpack("<I", self.fourcc)[0]

    @property
    def is_compressed(self) -> bool:
        return self.fourcc in {b"DXT1", b"DXT3", b"DXT5"}

    @property
    def is_rgba16f(self) -> bool:
        return self.fourcc_int == D3DFMT_A16B16G16R16F

    @property
    def is_uncompressed_color(self) -> bool:
        return bool(self.pixel_format_flags & DDPF_RGB) and self.fourcc_int == 0 and self.rgb_bit_count in {24, 32}

    @property
    def bytes_per_pixel(self) -> int:
        if self.is_rgba16f:
            return 8
        if self.is_uncompressed_color:
            return self.rgb_bit_count // 8
        raise ValueError(f"Unsupported bytes-per-pixel format for {self.path.name}")

    @property
    def format_name(self) -> str:
        if self.fourcc == b"DXT1":
            return "BC1/DXT1"
        if self.fourcc == b"DXT3":
            return "BC2/DXT3"
        if self.fourcc == b"DXT5":
            return "BC3/DXT5"
        if self.is_rgba16f:
            return "RGBA16F"
        if self.is_uncompressed_color:
            alpha = "A" if self.pixel_format_flags & DDPF_ALPHAPIXELS else ""
            return f"RGB{alpha}{self.rgb_bit_count}"
        return f"FOURCC_{self.fourcc_int}"


def parse_dds(path: Path) -> DDSInfo:
    data = path.read_bytes()
    if len(data) < 128 or data[:4] != DDS_MAGIC:
        raise ValueError(f"{path} is not a DDS file")

    header = data[4:128]
    header_size, flags, height, width, pitch_or_linear_size, _depth, mip_count = struct.unpack_from("<7I", header, 0)
    if header_size != 124:
        raise ValueError(f"{path} has an unsupported DDS header size: {header_size}")

    pixel_format = header[72:104]
    _pf_size, pf_flags, fourcc, rgb_bit_count, _r_mask, _g_mask, _b_mask, _a_mask = struct.unpack("<II4sIIIII", pixel_format)
    caps, caps2 = struct.unpack_from("<II", header, 104)
    header_bytes = 148 if fourcc == b"DX10" else 128

    return DDSInfo(
        path=path,
        width=width,
        height=height,
        mip_count=mip_count or 1,
        flags=flags,
        pitch_or_linear_size=pitch_or_linear_size,
        pixel_format_flags=pf_flags,
        fourcc=fourcc,
        rgb_bit_count=rgb_bit_count,
        caps=caps,
        caps2=caps2,
        header_size=header_bytes,
    )


def full_mip_count(width: int, height: int) -> int:
    return int(math.floor(math.log2(max(width, height)))) + 1


def mip_dimensions(width: int, height: int, level: int) -> tuple[int, int]:
    return max(1, width >> level), max(1, height >> level)


def level_size(info: DDSInfo, width: int, height: int) -> int:
    if info.fourcc == b"DXT1":
        return max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * 8
    if info.fourcc in {b"DXT3", b"DXT5"}:
        return max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * 16
    return width * height * info.bytes_per_pixel


def face_stride(info: DDSInfo, mip_count: int) -> int:
    return sum(level_size(info, *mip_dimensions(info.width, info.height, level)) for level in range(mip_count))


def downsample_unorm(prev: bytes, width: int, height: int, bytes_per_pixel: int) -> tuple[bytes, int, int]:
    next_width = max(1, width // 2)
    next_height = max(1, height // 2)
    out = bytearray(next_width * next_height * bytes_per_pixel)

    for y in range(next_height):
        for x in range(next_width):
            sums = [0] * bytes_per_pixel
            count = 0
            for dy in range(2):
                source_y = min(height - 1, y * 2 + dy)
                for dx in range(2):
                    source_x = min(width - 1, x * 2 + dx)
                    base = (source_y * width + source_x) * bytes_per_pixel
                    for channel in range(bytes_per_pixel):
                        sums[channel] += prev[base + channel]
                    count += 1

            dest = (y * next_width + x) * bytes_per_pixel
            for channel in range(bytes_per_pixel):
                out[dest + channel] = (sums[channel] + count // 2) // count

    return bytes(out), next_width, next_height


def downsample_rgba16f(prev: bytes, width: int, height: int) -> tuple[bytes, int, int]:
    next_width = max(1, width // 2)
    next_height = max(1, height // 2)
    out = bytearray(next_width * next_height * 8)

    for y in range(next_height):
        for x in range(next_width):
            sums = [0.0, 0.0, 0.0, 0.0]
            count = 0
            for dy in range(2):
                source_y = min(height - 1, y * 2 + dy)
                for dx in range(2):
                    source_x = min(width - 1, x * 2 + dx)
                    base = (source_y * width + source_x) * 8
                    values = struct.unpack_from("<4e", prev, base)
                    for channel in range(4):
                        sums[channel] += values[channel]
                    count += 1

            dest = (y * next_width + x) * 8
            struct.pack_into("<4e", out, dest, *(value / count for value in sums))

    return bytes(out), next_width, next_height


def build_mips_for_face(base_level: bytes, info: DDSInfo, target_mips: int) -> list[bytes]:
    levels = [base_level]
    width, height = info.width, info.height
    current = base_level
    for _level in range(1, target_mips):
        if info.is_rgba16f:
            current, width, height = downsample_rgba16f(current, width, height)
        else:
            current, width, height = downsample_unorm(current, width, height, info.bytes_per_pixel)
        levels.append(current)
    return levels


def update_header(data: bytearray, info: DDSInfo, mip_count: int) -> None:
    flags = info.flags | DDSD_MIPMAPCOUNT
    caps = info.caps | DDSCAPS_TEXTURE | DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    struct.pack_into("<I", data, 4 + 4, flags)
    struct.pack_into("<I", data, 4 + 24, mip_count)
    struct.pack_into("<I", data, 4 + 104, caps)


def rewrite_with_mips(path: Path, info: DDSInfo, backup_dir: Path, dry_run: bool) -> str:
    target_mips = full_mip_count(info.width, info.height)
    if info.mip_count >= target_mips:
        return f"skip full-mip {path.name}: {info.format_name} {info.width}x{info.height} mips={info.mip_count}"

    if info.is_compressed:
        return f"skip compressed {path.name}: {info.format_name} needs a BC/DXT compressor to add mips"
    if not (info.is_uncompressed_color or info.is_rgba16f):
        return f"skip unsupported {path.name}: {info.format_name}"

    original = path.read_bytes()
    data_start = info.header_size
    base_size = level_size(info, info.width, info.height)
    source_stride = face_stride(info, info.mip_count)
    required = data_start + source_stride * info.face_count
    if len(original) < required:
        return f"skip truncated {path.name}: expected at least {required} bytes, got {len(original)}"

    if dry_run:
        return f"would update {path.name}: {info.format_name} {info.width}x{info.height} mips {info.mip_count}->{target_mips}"

    backup_dir.mkdir(parents=True, exist_ok=True)
    backup_path = backup_dir / path.name
    if not backup_path.exists():
        shutil.copy2(path, backup_path)

    output = bytearray(original[:data_start])
    update_header(output, info, target_mips)

    for face in range(info.face_count):
        face_offset = data_start + face * source_stride
        base_level = original[face_offset:face_offset + base_size]
        output.extend(b"".join(build_mips_for_face(base_level, info, target_mips)))

    path.write_bytes(output)
    return f"updated {path.name}: {info.format_name} {info.width}x{info.height} mips {info.mip_count}->{target_mips}; backup={backup_path}"


def iter_dds_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    return sorted(path.glob("*.dds"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate offline mip chains for DDS cubemaps.")
    parser.add_argument("path", nargs="?", default="Assets/Textures/sky", help="DDS file or directory to process")
    parser.add_argument("--backup-dir", type=Path, help="Backup directory. Defaults to <path>/_backup_mips_<timestamp>.")
    parser.add_argument("--dry-run", action="store_true", help="Report work without modifying files")
    args = parser.parse_args()

    target = Path(args.path).resolve()
    if not target.exists():
        raise SystemExit(f"Path does not exist: {target}")

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_dir = args.backup_dir or ((target.parent if target.is_file() else target) / f"_backup_mips_{stamp}")

    changed = 0
    for dds_path in iter_dds_files(target):
        try:
            info = parse_dds(dds_path)
        except ValueError as exc:
            print(f"skip {dds_path.name}: {exc}")
            continue

        if not info.is_cubemap:
            print(f"skip non-cubemap {dds_path.name}")
            continue

        message = rewrite_with_mips(dds_path, info, backup_dir, args.dry_run)
        print(message)
        if message.startswith("updated ") or message.startswith("would update "):
            changed += 1

    if changed == 0:
        print("No cubemaps needed mip generation.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())