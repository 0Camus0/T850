#!/usr/bin/env python3
"""Guard the generated OpenGL GLSL twins against HLSL drift (W-28 / A2 of the staged plan).

OpenGL has no runtime SPIRV-Cross: `GLCompute::LoadComputeSource` loads `<stem>.glsl` next to the
`.hlsl`, inserts the pipeline defines after `#version`, reflects that GLSL through glslang to get
the binding layout, and compiles the GLSL text with the driver. The twin is therefore a *copy* of
the kernel that CI cannot see is stale — the same failure mode the hand-maintained `.wgsl` twins
had (WebGPU drew flat water for weeks because `FS_Mesh.wgsl` predated `FLUID_SURFACE`).

Every generated twin carries a banner on line 2:

    // GENERATED from CS_FluidAirRise3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1,
    // do not edit; hlsl-sha256=<hash>; defines=T850_SPIRV=1,T850_VULKAN=1

This script fails when:

  1. a kernel the GL backend can load has no `<stem>.glsl` twin;
  2. a generated twin's `hlsl-sha256` no longer matches its `.hlsl`;
  3. a generated twin does not begin with `#version` on line 1 (the loader rejects it);
  4. a generated twin binds a different set of numbers from `compute_kernels.json`, which is what
     a lost `layout(binding = N)` looks like after SPIRV-Cross.

Hand-written twins (CS_Arithmetic, CS_Blur, CS_Bright, CS_GodRays, CS_HDRComposite,
CS_ImagePattern*, CS_TorchParticles) are only checked for presence: their bindings come from the
file, not from the generator, and 3 and 4 do not apply.

Usage:
    python scripts/validation/check_gl_twins.py [--shaders DIR]
Exit code 0 when every check passes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

BANNER_TAG = "GENERATED from"
SHA_RE = re.compile(r"hlsl-sha256=([0-9a-f]{64})")
BINDING_RE = re.compile(r"layout\s*\(\s*binding\s*=\s*(\d+)")

# Kernels the GL backend dispatches that are not in compute_kernels.json: the 3D case of
# `--compute-selftest` names its .hlsl directly. CS_SeparableBlur is not here, it only feeds the
# Dawn shader probe. Keep in sync with `$unregistered` in generate_gl_compute_twins.ps1.
UNREGISTERED = [
    "CS_VolumePatternWrite.hlsl",
    "CS_VolumePatternRead.hlsl",
    "CS_VolumeFloatPatternWrite.hlsl",
    "CS_VolumeFloatPatternRead.hlsl",
]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_header(path: Path) -> list[str]:
    with path.open("r", encoding="utf-8-sig", errors="replace") as handle:
        return [handle.readline().rstrip("\r\n") for _ in range(2)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shaders", default=None, help="Assets/Shaders directory")
    args = parser.parse_args()

    root = Path(args.shaders) if args.shaders else Path(__file__).resolve().parents[2] / "Assets" / "Shaders"
    if not root.is_dir():
        print(f"FAIL: no shader directory at {root}")
        return 1

    registry_path = root / "compute_kernels.json"
    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    problems: list[str] = []
    checked = 0

    kernels: list[tuple[str, str, dict[str, int]]] = []
    for kernel in registry["kernels"]:
        # GL has no separate sampler objects: SPIRV-Cross folds each SamplerState into the
        # texture it samples, so the combined `sampler2D` keeps the TEXTURE's binding index
        # and the sampler's own index disappears. Exclude sampler entries from the expected
        # set; otherwise every generated kernel with a sampler looks like a lost binding.
        bindings = {b["binding_index"] for b in kernel.get("bindings", [])
                    if b.get("type") != "sampler"}
        kernels.append((kernel["source"], kernel["entry"], bindings))
    for source in UNREGISTERED:
        kernels.append((source, "CS", set()))

    for source, _entry, registry_bindings in kernels:
        hlsl = root / source
        if not hlsl.is_file():
            problems.append(f"{source}: registered kernel has no HLSL")
            continue
        stem = hlsl.stem
        twins = sorted(root.glob(f"{stem}.glsl")) + sorted(root.glob(f"{stem}.*.glsl"))
        if not twins:
            problems.append(f"{source}: no GLSL twin ({stem}.glsl)")
            continue
        expected = sha256(hlsl)
        for twin in twins:
            header = read_header(twin)
            generated = len(header) >= 2 and BANNER_TAG in header[1]
            if not generated:
                continue
            checked += 1
            if twin.read_bytes()[:3] == b"\xef\xbb\xbf":
                problems.append(
                    f"{twin.name}: starts with a UTF-8 BOM; the GL loader requires #version on line 1"
                )
                continue
            if not header[0].startswith("#version"):
                problems.append(f"{twin.name}: line 1 must be #version, found {header[0][:40]!r}")
            match = SHA_RE.search(header[1])
            if not match:
                problems.append(f"{twin.name}: banner has no hlsl-sha256")
            elif match.group(1) != expected:
                problems.append(
                    f"{twin.name}: stale (HLSL {expected[:12]}..., twin {match.group(1)[:12]}...); "
                    f"run scripts/shaders/generate_gl_compute_twins.ps1"
                )
            if registry_bindings:
                found = {int(n) for n in BINDING_RE.findall(twin.read_text(encoding="utf-8", errors="replace"))}
                if found != registry_bindings:
                    problems.append(
                        f"{twin.name}: binds {sorted(found)}, compute_kernels.json says "
                        f"{sorted(registry_bindings)}"
                    )

    print(f"Checked {len(kernels)} kernels, {checked} generated twins under {root}")
    if problems:
        for problem in problems:
            print(f"  FAIL {problem}")
        print(f"FAIL: {len(problems)} problem(s)")
        return 1
    print("PASS: every generated GLSL twin matches its HLSL and the registered bindings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
