"""Fabricate a seamless tiling water normal map.

Every wave uses integer frequencies across the tile, so the field is exactly
periodic in both axes and the map tiles without a visible seam. Heights and
gradients are evaluated analytically, which avoids the finite-difference noise
you get from differencing a rendered height field.

Output is tangent-space, OpenGL convention (+Y up). The engine flips green for
its own convention, so the stored value is the conventional one.

Usage: python scripts/textures/build_water_normal.py [size] [out_dir]
"""

import os
import sys
import math

import numpy as np
from PIL import Image

# One entry per octave: (cycles across the tile, amplitude weight). The weight
# falls off as 1 / cycles^1.15, which is close to the slope spectrum of real
# wind-driven capillary waves: steeper small detail, gentler large swell.
OCTAVE_CYCLES = [2, 4, 8, 16, 32, 64]
OCTAVE_PHASE = [0.0, math.pi / 3.0, 2.0 * math.pi / 3.0]

# Directions spread around the circle so no single axis dominates and the
# pattern reads as isotropic chop rather than stripes.
DIRECTIONS = [
    (math.cos(math.radians(18.0)), math.sin(math.radians(18.0))),
    (math.cos(math.radians(101.0)), math.sin(math.radians(101.0))),
    (math.cos(math.radians(214.0)), math.sin(math.radians(214.0))),
]

# Gradient magnitude at the 99th percentile after normalisation. 0.5 is a slope
# of about 27 degrees, which is choppy water without turning into static.
TARGET_P99_SLOPE = 0.5


def build_waves():
    waves = []
    for index, cycles in enumerate(OCTAVE_CYCLES):
        amplitude = 1.0 / (cycles ** 1.15)
        for dir_index, (dx, dy) in enumerate(DIRECTIONS):
            fx = int(round(cycles * dx))
            fy = int(round(cycles * dy))
            if fx == 0 and fy == 0:
                continue
            phase = OCTAVE_PHASE[dir_index] + index * 0.7
            waves.append((fx, fy, amplitude, phase))
    return waves


def evaluate(size, waves):
    coords = np.arange(size, dtype=np.float64) / size
    gx, gy = np.meshgrid(coords, coords, indexing="xy")

    grad_x = np.zeros((size, size), dtype=np.float64)
    grad_y = np.zeros((size, size), dtype=np.float64)
    for fx, fy, amplitude, phase in waves:
        angle = 2.0 * math.pi * (fx * gx + fy * gy) + phase
        scale = 2.0 * math.pi * amplitude
        grad_x += scale * fx * np.cos(angle)
        grad_y += scale * fy * np.cos(angle)
    return grad_x, grad_y


def main():
    size = int(sys.argv[1]) if len(sys.argv) > 1 else 512
    script_dir = os.path.dirname(os.path.abspath(__file__))
    source_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(source_root, "Assets", "Textures", "Backyard")
    os.makedirs(out_dir, exist_ok=True)

    waves = build_waves()
    grad_x, grad_y = evaluate(size, waves)

    magnitude = np.sqrt(grad_x * grad_x + grad_y * grad_y)
    p99 = float(np.percentile(magnitude, 99.0))
    gain = TARGET_P99_SLOPE / p99 if p99 > 0.0 else 1.0
    grad_x *= gain
    grad_y *= gain

    # Tangent-space normal from the gradient of a height field.
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

    # Seam check: every wave uses integer frequencies, so the field must be
    # exactly periodic. Compare the analytic gradient at x=0/x=1 and y=0/y=1
    # rather than neighbouring texels (which are adjacent samples, not a seam).
    def gradient_at(px, py):
        gx = np.zeros_like(px, dtype=np.float64)
        gy = np.zeros_like(py, dtype=np.float64)
        for fx, fy, amplitude, phase in waves:
            angle = 2.0 * math.pi * (fx * px + fy * py) + phase
            scale = 2.0 * math.pi * amplitude
            gx += scale * fx * np.cos(angle)
            gy += scale * fy * np.cos(angle)
        return gx * gain, gy * gain

    probe_x = np.array([0.0, 1.0])
    probe_y = np.array([0.0, 0.0])
    seam_x = float(np.abs(gradient_at(probe_x, probe_y)[0][0] - gradient_at(probe_x, probe_y)[0][1]))
    probe_x = np.array([0.0, 0.0])
    probe_y = np.array([0.0, 1.0])
    seam_y = float(np.abs(gradient_at(probe_x, probe_y)[1][0] - gradient_at(probe_x, probe_y)[1][1]))
    print(f"waves={len(waves)} size={size} p99={p99:.4f} gain={gain:.4f} "
          f"maxSlope={float(magnitude.max() * gain):.3f} seamX={seam_x:.2e} seamY={seam_y:.2e}")
    print(f"wrote {path}")


if __name__ == "__main__":
    main()
