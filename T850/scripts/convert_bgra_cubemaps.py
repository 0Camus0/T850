#!/usr/bin/env python3
"""
Convert BGRA DDS cubemap textures to RGBA in-place.

Detects BGRA layout by checking the DDS pixel format masks:
  - BGRA: R mask = 0x00FF0000, B mask = 0x000000FF  (standard D3D BGRA8)
  - RGBA: R mask = 0x000000FF, B mask = 0x00FF0000

Only converts uncompressed (no FourCC) textures where the masks indicate BGRA.
Compressed (DXT/BC) and floating-point (FourCC) textures are skipped.
"""

import struct
import sys
import os
import glob

# DDS header offsets (all relative to file start)
DDS_MAGIC = 0x20534444  # "DDS "
DDPF_FOURCC   = 0x4
DDPF_RGB      = 0x40
DDPF_ALPHAPIXELS = 0x1

# Pixel format starts at byte 76 in the DDS header
PF_OFFSET = 76
PF_SIZE   = 32  # dwSize of DDPIXELFORMAT

def read_dds_header(data):
    """Parse DDS header and pixel format. Returns dict or None on error."""
    if len(data) < 128:
        return None
    magic = struct.unpack_from('<I', data, 0)[0]
    if magic != DDS_MAGIC:
        return None

    header_size = struct.unpack_from('<I', data, 4)[0]
    flags       = struct.unpack_from('<I', data, 8)[0]
    height      = struct.unpack_from('<I', data, 12)[0]
    width       = struct.unpack_from('<I', data, 16)[0]
    pitch       = struct.unpack_from('<I', data, 20)[0]
    depth       = struct.unpack_from('<I', data, 24)[0]
    mip_count   = struct.unpack_from('<I', data, 28)[0]

    # Pixel format at offset 76
    pf_size     = struct.unpack_from('<I', data, PF_OFFSET)[0]
    pf_flags    = struct.unpack_from('<I', data, PF_OFFSET + 4)[0]
    pf_fourcc   = struct.unpack_from('<4s', data, PF_OFFSET + 8)[0]
    pf_bitcount = struct.unpack_from('<I', data, PF_OFFSET + 12)[0]
    pf_rmask    = struct.unpack_from('<I', data, PF_OFFSET + 16)[0]
    pf_gmask    = struct.unpack_from('<I', data, PF_OFFSET + 20)[0]
    pf_bmask    = struct.unpack_from('<I', data, PF_OFFSET + 24)[0]
    pf_amask    = struct.unpack_from('<I', data, PF_OFFSET + 28)[0]

    # Caps
    caps1 = struct.unpack_from('<I', data, 108)[0]
    caps2 = struct.unpack_from('<I', data, 112)[0]

    return {
        'width': width, 'height': height, 'depth': depth,
        'mip_count': max(mip_count, 1),
        'pf_flags': pf_flags, 'pf_fourcc': pf_fourcc,
        'pf_bitcount': pf_bitcount,
        'pf_rmask': pf_rmask, 'pf_gmask': pf_gmask,
        'pf_bmask': pf_bmask, 'pf_amask': pf_amask,
        'caps1': caps1, 'caps2': caps2,
        'header_size': 4 + header_size,  # magic + header
    }

def is_bgra(hdr):
    """Check if the pixel format indicates BGRA8888 layout."""
    pf = hdr['pf_flags']
    # Must be uncompressed RGB(A), not FourCC
    if pf & DDPF_FOURCC:
        return False
    if not (pf & DDPF_RGB):
        return False
    if hdr['pf_bitcount'] != 32:
        return False
    # BGRA: R=0x00FF0000, G=0x0000FF00, B=0x000000FF
    return (hdr['pf_rmask'] == 0x00FF0000 and
            hdr['pf_gmask'] == 0x0000FF00 and
            hdr['pf_bmask'] == 0x000000FF)

def swap_bgra_to_rgba(data, data_offset):
    """Swap B and R channels in-place for all pixels from data_offset onward."""
    arr = bytearray(data)
    length = len(arr)
    i = data_offset
    while i + 3 < length:
        # Swap B (offset+0) and R (offset+2) for BGRA→RGBA
        arr[i], arr[i+2] = arr[i+2], arr[i]
        i += 4
    return bytes(arr)

def fix_masks_to_rgba(data):
    """Rewrite the pixel format masks from BGRA to RGBA."""
    arr = bytearray(data)
    # R mask → 0x000000FF, G mask stays, B mask → 0x00FF0000
    struct.pack_into('<I', arr, PF_OFFSET + 16, 0x000000FF)  # R
    # G mask unchanged: 0x0000FF00
    struct.pack_into('<I', arr, PF_OFFSET + 24, 0x00FF0000)  # B
    return bytes(arr)

def process_file(filepath, dry_run=False):
    """Process a single DDS file. Returns True if converted."""
    with open(filepath, 'rb') as f:
        data = f.read()

    hdr = read_dds_header(data)
    if hdr is None:
        print(f"  SKIP (not a valid DDS): {filepath}")
        return False

    if not is_bgra(hdr):
        fourcc = hdr['pf_fourcc']
        bpp = hdr['pf_bitcount']
        rmask = hdr['pf_rmask']
        flags = hdr['pf_flags']
        if flags & DDPF_FOURCC:
            reason = f"FourCC={fourcc}"
        elif bpp != 32:
            reason = f"bpp={bpp}"
        elif rmask == 0x000000FF:
            reason = "already RGBA"
        else:
            reason = f"rmask=0x{rmask:08X}"
        print(f"  SKIP ({reason}): {os.path.basename(filepath)}")
        return False

    print(f"  CONVERT BGRA→RGBA: {os.path.basename(filepath)} "
          f"({hdr['width']}x{hdr['height']}, {hdr['mip_count']} mips)")

    if dry_run:
        return True

    # Swap pixel data
    data_offset = hdr['header_size']
    data = swap_bgra_to_rgba(data, data_offset)
    # Fix header masks
    data = fix_masks_to_rgba(data)

    with open(filepath, 'wb') as f:
        f.write(data)

    return True

def main():
    import argparse
    parser = argparse.ArgumentParser(description='Convert BGRA DDS cubemaps to RGBA')
    parser.add_argument('path', nargs='?',
                        default=os.path.join(os.path.dirname(__file__),
                                             '..', 'Assets', 'Textures', 'sky'),
                        help='Directory containing .dds files (default: Assets/Textures/sky)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Only report which files would be converted')
    args = parser.parse_args()

    search_dir = os.path.abspath(args.path)
    if not os.path.isdir(search_dir):
        print(f"ERROR: Directory not found: {search_dir}")
        sys.exit(1)

    dds_files = sorted(glob.glob(os.path.join(search_dir, '*.dds')))
    if not dds_files:
        print(f"No .dds files found in {search_dir}")
        sys.exit(0)

    print(f"Scanning {len(dds_files)} DDS files in {search_dir}:")
    converted = 0
    for f in dds_files:
        if process_file(f, dry_run=args.dry_run):
            converted += 1

    action = "would convert" if args.dry_run else "converted"
    print(f"\nDone: {action} {converted}/{len(dds_files)} files.")

if __name__ == '__main__':
    main()
