"""Fabricate a seamless tiling water normal map from a real wave spectrum.

The previous version summed 18 directional sine waves, which is why the pool read
as "sine waves bouncing around": the visible normal detail literally was a sum of
sinusoids, and because its amplitude was tuned to be visible it dominated whatever
the fluid solver produced.

This version derives the surface from a Phillips wind-wave spectrum with random
phases, which gives the irregular, non-repeating chop real water has.

Every wave vector is an integer multiple of the tile frequency, so the field is
exactly periodic and the map tiles seamlessly. The gradient is accumulated
directly from the spectrum (d/dx of A*sin(k.x+p) is A*kx*cos(...)), which is exact
and avoids differencing a rendered height field.

Output is tangent-space, OpenGL convention (+Y up).

Usage: python scripts/textures/build_water_normal.py [size] [out_dir]
"""

import json
import math
import os
import sys

import numpy as np
from PIL import Image

# Physical size one tile represents, in metres. The wave vectors are converted
# from tile cycles to rad/m with this, so the spectrum is in real units.
TILE_SIZE_M = 0.40
# Dominant chop wavelength, in metres. 12 cm reads as wind chop on a pool; the
# Gaussian band below spreads energy either side of it.
PEAK_WAVELENGTH_M = 0.12
# Relative width of the band around the peak wavelength.
BAND_WIDTH = 1.1
# How strongly energy follows the wind direction (0 = isotropic chop).
DIRECTIONALITY = 0.55
# Wind direction, unit vector. Only the ratio matters via `directional`.
WIND_DIR = (1.0, 0.0)

# Frequency band in cycles across the tile. The lower bound sets the largest
# feature; the upper bound is where the map stops carrying detail.
MIN_CYCLES = 2
MAX_CYCLES = 128

# Gradient magnitude at the 99th percentile, roughly a 23 degree slope.
TARGET_P99_SLOPE = 0.42

SEED = 0x850


def spectrum(k):
    """Band-limited Phillips-style amplitude for wave number magnitude k (rad/m).

    A true Phillips spectrum is dominated by metre-scale gravity waves because its
    cutoff is g/U^2, which at pool scale would put every component outside the
    tile's frequency band. What a pool surface needs is a Gaussian band around a
    centimetre-scale peak wavelength instead, with the same directional weighting
    and a k^-2 falloff so short waves carry less slope.
    """
    if k <= 0.0:
        return 0.0
    k_peak = 2.0 * math.pi / PEAK_WAVELENGTH_M
    offset = (k - k_peak) / (k_peak * BAND_WIDTH)
    band = math.exp(-offset * offset)
    return band / (k * k)


def directional(kx, ky, k):
    kd = (kx * WIND_DIR[0] + ky * WIND_DIR[1]) / k
    return (1.0 - DIRECTIONALITY) + DIRECTIONALITY * kd * kd


def build(size):
    """Returns the (grad_x, grad_y) height-field gradient on a size x size grid."""
    rng = np.random.default_rng(SEED)
    xs = np.arange(size, dtype=np.float64) / size
    grad_x = np.zeros((size, size), dtype=np.float64)
    grad_y = np.zeros((size, size), dtype=np.float64)
    # Cycles per metre, so the spectrum is evaluated in physical units.
    cycles_per_metre = 1.0 / TILE_SIZE_M
    contributed = 0

    for cy in range(-MAX_CYCLES, MAX_CYCLES + 1):
        for cx in range(-MAX_CYCLES, MAX_CYCLES + 1):
            cycles = math.hypot(cx, cy)
            if cycles < MIN_CYCLES or cycles > MAX_CYCLES:
                continue
            # Wave vector in rad/m.
            kx = cx * 2.0 * math.pi * cycles_per_metre
            ky = cy * 2.0 * math.pi * cycles_per_metre
            k = math.hypot(kx, ky)
            amp = spectrum(k) * directional(kx, ky, k)
            if amp <= 0.0:
                continue
            contributed += 1
            phase = rng.uniform(0.0, 2.0 * math.pi)
            angle = 2.0 * math.pi * (cx * xs[:, None] + cy * xs[None, :]) + phase
            cos_angle = np.cos(angle)
            grad_x += cos_angle * (amp * kx)
            grad_y += cos_angle * (amp * ky)
    print(f"contributed {contributed} wave components")
    return grad_x, grad_y


def main():
    size = int(sys.argv[1]) if len(sys.argv) > 1 else 512
    script_dir = os.path.dirname(os.path.abspath(__file__))
    source_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        source_root, "Assets", "Textures", "Backyard")
    os.makedirs(out_dir, exist_ok=True)

    grad_x, grad_y = build(size)

    magnitude = np.sqrt(grad_x * grad_x + grad_y * grad_y)
    p99 = float(np.percentile(magnitude, 99.0))
    gain = TARGET_P99_SLOPE / p99 if p99 > 0.0 else 1.0
    grad_x *= gain
    grad_y *= gain

    length = np.sqrt(grad_x * grad_x + grad_y * grad_y + 1.0)
    normal_x = -grad_x / length
    normal_y = -grad_y / length
    normal_z = 1.0 / length

    image = np.empty((size, size, 3), dtype=np.uint8)
    image[..., 0] = np.clip((normal_x * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)
    image[..., 1] = np.clip((normal_y * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)
    image[..., 2] = np.clip((normal_z * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)

    path = os.path.join(out_dir, "WaterNormal.png")
    Image.fromarray(image).save(path)
    print(f"size={size} p99={p99:.4f} gain={gain:.4f} "
          f"maxSlope={float(magnitude.max() * gain):.3f}")
    print(f"wrote {path}")

    # Record what was baked into the layout, so the spectrum parameters live in
    # data next to the geometry rather than as magic numbers in two places.
    layout_path = os.path.join(source_root, "Assets", "Models", "Backyard",
                               "backyard_layout.json")
    if os.path.isfile(layout_path):
        with open(layout_path, "r", encoding="utf-8") as handle:
            layout = json.load(handle)
        layout.setdefault("waterDetail", {})
        layout["waterDetail"].update({
            "normalMap": "Backyard/WaterNormal.png",
            "tileSizeMetres": TILE_SIZE_M,
            "peakWavelengthMetres": PEAK_WAVELENGTH_M,
            "bandWidth": BAND_WIDTH,
            "directionality": DIRECTIONALITY,
            "minCycles": MIN_CYCLES,
            "maxCycles": MAX_CYCLES,
            "targetP99Slope": TARGET_P99_SLOPE,
        })
        with open(layout_path, "w", encoding="utf-8") as handle:
            json.dump(layout, handle, indent=2)
        print("updated", layout_path)


if __name__ == "__main__":
    main()
