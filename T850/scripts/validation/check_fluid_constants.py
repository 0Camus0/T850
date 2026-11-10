#!/usr/bin/env python3
"""Guard the duplicated fluid kernel cbuffers (W-14) and their registered constant counts.

The compute compile path has no `#include` handler:

  * D3D12 hands DXC `nullptr` as the include handler (`D3D12Shader.cpp`, `compiler->Compile`),
  * Vulkan/GL compile one source string per kernel through glslang,
  * WebGPU preprocesses the same string before Tint.

So `FluidConstants` (20 DWORDs) is copy-pasted into 20 kernels, `ObjectConstants`/`StampConstants`
into several more, and a change to one copy silently diverges from the others. This script fails
when:

  1. a fluid kernel's cbuffer has a different **layout** (member sizes) from the other copies —
     names may differ on purpose, see the alias note below;
  2. a registered `constant_count` in compute_kernels.json does not match the DWORD size of the
     cbuffer declared at that register.

(2) is the failure mode recorded in the implementation doc: a cbuffer with two floats is padded to
four DWORDs, the driver rejects the pipeline, and nothing is logged.

**Deliberate alias.** Word 15 is `Padding0` everywhere except `CS_FluidForces3D.hlsl`, which calls
it `ForcingAmplitude`; `BuildConstants(paddingOverride)` passes the SOR factor to the pressure
kernel, the compression strength to alpha advection and the forcing gain to forces. Same layout,
different meaning per dispatch, which is why this compares sizes and reports name differences as
notes.

It also fails when two `SamplerState` declarations in one HLSL file share a register. D3D resolves
both names to the same slot, but glslang's auto-mapping gives the second one a fresh binding
(`FluidNormalSS : register(s0)` became binding 67), which WebGPU rejects at bind time.

Usage:
    python scripts/validation/check_fluid_constants.py [--shaders DIR]
Exit code 0 when every check passes.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Scalar sizes in bytes. Vectors (float3, uint4, ...) and arrays follow HLSL cbuffer packing: a
# vector never straddles a 16-byte register and every array element starts a new register.
SIZES = {"bool": 4, "int": 4, "uint": 4, "float": 4, "dword": 4}
VECTOR_RE = re.compile(r"^(bool|int|uint|float|dword)([1-4])$")
CBUFFER_RE = re.compile(r"cbuffer\s+(\w+)\s*(?::\s*register\(\s*b(\d+)\s*\))?\s*\{(.*?)\}\s*;", re.S)
MEMBER_RE = re.compile(r"^\s*(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;")
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def member_bytes(type_name: str) -> int | None:
    if type_name in SIZES:
        return SIZES[type_name]
    vector = VECTOR_RE.match(type_name)
    return SIZES[vector.group(1)] * int(vector.group(2)) if vector else None


def cbuffer_layout(body: str) -> tuple[int, list[tuple[str, str, int, int]], list[str]]:
    """(DWORD size, [(type, name, offset, bytes)] per member, members this check cannot size)."""
    members, unknown = [], []
    offset = 0
    for line in COMMENT_RE.sub("", body).splitlines():
        match = MEMBER_RE.match(line)
        if not match:
            continue
        type_name, name = match.group(1).lower(), match.group(2)
        count = int(match.group(3)) if match.group(3) else 1
        size = member_bytes(type_name)
        if size is None:
            unknown.append(f"{type_name} {name}")
            continue
        if count > 1 or offset // 16 != (offset + size - 1) // 16:
            offset = (offset + 15) // 16 * 16
        total = 16 * (count - 1) + size
        members.append((type_name, name, offset, total))
        offset += total
    words = ((offset + 15) // 16) * 4 if offset else 0
    return words, members, unknown


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--shaders", default=None, help="Assets/Shaders directory")
    args = parser.parse_args()
    shader_dir = (Path(args.shaders) if args.shaders
                  else Path(__file__).resolve().parents[2] / "Assets" / "Shaders")
    registry_path = shader_dir / "compute_kernels.json"
    if not registry_path.exists():
        print(f"[constants] FAIL: no compute_kernels.json under {shader_dir}")
        return 1

    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    kernels = registry.get("kernels", registry if isinstance(registry, list) else [])
    declared: dict[str, dict[int, int]] = {}
    for kernel in kernels:
        for binding in kernel.get("bindings", []):
            if binding.get("type") == "constants":
                declared.setdefault(kernel["source"], {})[int(binding["shader_register"])] = int(
                    binding["constant_count"])

    failures: list[str] = []
    canonical: dict[str, tuple[str, tuple, list[str]]] = {}
    aliases: list[str] = []
    checked_counts = 0
    files = sorted(list(shader_dir.glob("CS_Fluid*.hlsl")) + list(shader_dir.glob("CS_Particle*.hlsl")))

    for path in files:
        text = COMMENT_RE.sub("", path.read_text(encoding="utf-8", errors="replace"))
        for name, register, body in CBUFFER_RE.findall(text):
            words, members, unknown = cbuffer_layout(body)
            if not members:
                continue
            if unknown:
                failures.append(f"{path.name}: cbuffer {name} has members this check cannot size: "
                                f"{', '.join(unknown)}")
                continue
            layout = tuple((offset, size) for _, _, offset, size in members)
            names = [n for _, n, _, _ in members]
            first = canonical.get(name)
            if first is None:
                canonical[name] = (path.name, layout, names)
            elif first[1] != layout:
                failures.append(
                    f"{name}: {path.name} has a different layout ({len(layout)} members) than "
                    f"{first[0]} ({len(first[1])})")
            elif first[2] != names:
                aliases.append(f"{name}: {path.name} names the members differently from {first[0]}")
            if register == "":
                continue
            expected = declared.get(path.name, {}).get(int(register))
            if expected is None:
                continue
            checked_counts += 1
            if expected != words:
                failures.append(
                    f"{path.name}: cbuffer {name} at b{register} is {words} DWORDs but "
                    f"compute_kernels.json declares constant_count {expected}")

    for source, registers in sorted(declared.items()):
        for register, count in sorted(registers.items()):
            if count % 4:
                failures.append(f"{source}: b{register} constant_count {count} is not a DWORD count")

    # Two samplers on one register: legal HLSL (D3D resolves both names to the slot) but glslang's
    # auto-mapping gives the second a fresh binding, which WebGPU then rejects at bind time.
    sampler_re = re.compile(r"SamplerState\s+(\w+)\s*:\s*register\(\s*s(\d+)\s*\)")
    for path in sorted(shader_dir.glob("*.hlsl")):
        registers: dict[str, list[str]] = {}
        for name, register in sampler_re.findall(COMMENT_RE.sub("", path.read_text(encoding="utf-8", errors="replace"))):
            registers.setdefault(register, []).append(name)
        for register, names in sorted(registers.items()):
            if len(names) > 1:
                failures.append(f"{path.name}: samplers s{register} declared twice ({', '.join(names)}); "
                                "WebGPU gets a different binding for the second")

    if failures:
        print(f"[constants] FAIL: {len(failures)} problem(s)")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print(f"[constants] PASS: {len(canonical)} cbuffer name(s) across {len(files)} fluid kernel(s), "
          f"{checked_counts} registered constant_count(s) match")
    for alias in aliases:
        print(f"  note: {alias}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
