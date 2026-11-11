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

The screen-space whitewater kernels (WO-2) are checked more strictly because no alias is
sanctioned there:

  * `PbfWhitewaterConstants` is shared by `CS_PbfWhitewater`, `CS_PbfDiffuse` and `CS_PbfFields`;
    all three copies must have an identical member **name and layout** list (a divergence is a
    failure, unlike the `FluidConstants` alias), must be 48 DWORDs, and must match the C++
    mirror `struct PbfWhitewaterConstants` in `Framework/src/simulation/PbfFluid.cpp` and the
    manifest `constant_count` of the three `pbf_*` kernels.
  * `ParticleShadeConstants` in `CS_ParticleShade.hlsl` must be 100 DWORDs and match the C++
    mirror `struct ParticleShadeConstants` in `Framework/src/utils/ComputeKernelRegistry.cpp`
    (400 bytes via its `static_assert`) and manifest `particle_shade`.
  * `WhitewaterSplatConstants` in `CS_FluidWhitewater.hlsl` must be 28 DWORDs and match the C++
    mirror `struct WhitewaterSplatConstants` (112 bytes) and manifest `particle_whitewater`.

The C++<->HLSL comparison applies the same packing rules (a vector never straddles a 16-byte row,
array elements start a new row). The C++ mirrors spell HLSL vectors as scalar arrays, and both
spellings are treated as the same layout:

  * an HLSL `float4 Name` vector and a C++ `float name[4]` array are both 16 bytes with the same
    row alignment, so they are equivalent (likewise `uint4` vs `uint32_t[4]`);
  * an HLSL `float4 Name[N]` vector array and a C++ `float name[N*4]` flat array occupy the same
    bytes: each 16-byte vector element starts a new row, which is identical to 4 scalars per row.
    (This is the `WhitewaterSplatConstants` `ViewProjection` case.)

To make both spellings pack identically, the C++ mirror is packed element-by-element where a C++
scalar array of 2/4 elements counts as one 8/16-byte vector element (matching the HLSL vector the
mirror was written for); other C++ types pack as plain scalars.

Member names are compared case-insensitively (C++ uses camelCase, HLSL PascalCase — the project's
convention; any real rename shows up as a name mismatch, which is reported, not silently accepted).

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

# The C++ mirrors of the whitewater cbuffers (WO-2), relative to Framework/src.
CPP_MIRRORS = {
    "PbfWhitewaterConstants": "simulation/PbfFluid.cpp",
    "ParticleShadeConstants": "utils/ComputeKernelRegistry.cpp",
    "WhitewaterSplatConstants": "utils/ComputeKernelRegistry.cpp",
}
# Kernel source file -> the cbuffer (and C++ struct) it must cross-check against. Sources without
# an entry (e.g. CS_PbfSolve.hlsl declaring PbfConstants) keep the generic layout/count checks.
CROSS_CHECKS = {
    "CS_PbfWhitewater.hlsl": "PbfWhitewaterConstants",
    "CS_PbfDiffuse.hlsl": "PbfWhitewaterConstants",
    "CS_PbfFields.hlsl": "PbfWhitewaterConstants",
    "CS_ParticleShade.hlsl": "ParticleShadeConstants",
    "CS_FluidWhitewater.hlsl": "WhitewaterSplatConstants",
}
CPP_TYPES = {"uint32_t": 4, "int32_t": 4, "uint": 4, "int": 4, "float": 4}


def member_bytes(type_name: str) -> int | None:
    if type_name in SIZES:
        return SIZES[type_name]
    vector = VECTOR_RE.match(type_name)
    return SIZES[vector.group(1)] * int(vector.group(2)) if vector else None


def pack_members(members: list[tuple[str, str, int]],
                 sizes: dict[str, int] | None = None) -> tuple[int, list[tuple[str, str, int, int]]]:
    """cbuffer packing over (type, name, element_count) members.

    Returns (total bytes, [(type, name, offset, bytes)]). An N-element member occupies
    16*(N-1) + element_size bytes: each element starts a new 16-byte row and a vector (8/12/16
    bytes) never straddles a row. `sizes` is a type-name -> bytes lookup; `None` sizes types as
    HLSL (so `float4` is 16 bytes and `float4[4]` four rows). The C++ mirror passes `CPP_TYPES`,
    where every type is a 4-byte scalar, and a scalar array of 2/4 elements is re-parsed as one
    8/16-byte vector element (see `cpp_element_size`): C++ `float x[4]` then packs as one 16-byte
    row like HLSL `float4 x`, and C++ `float x[16]` as four rows like HLSL `float4 x[4]`. A type
    absent from `sizes` is silently dropped, so callers must validate types first.
    """
    rows: list[tuple[str, str, int, int]] = []
    offset = 0
    for type_name, name, count in members:
        size = member_bytes(type_name) if sizes is None else sizes.get(type_name)
        if not size:
            continue
        if sizes is not None:
            size, count = cpp_element_size(type_name, count)
        if count > 1 or offset // 16 != (offset + size - 1) // 16:
            offset = (offset + 15) // 16 * 16
        rows.append((type_name, name, offset, 16 * (count - 1) + size))
        offset += 16 * (count - 1) + size
    return offset, rows


def cpp_element_size(type_name: str, count: int) -> tuple[int, int]:
    """(element bytes, element count) of a C++ mirror member, using HLSL vector packing.

    The mirrors spell an HLSL `float4`/`uint4` as `float[4]`/`uint32_t[4]` and a `float4[N]` as
    `float[N*4]`. A scalar array whose element count is a multiple of 4 is re-parsed as count/4
    elements of 16 bytes (i.e. `float[N*4]` → `float4[N]`); a 2-element array becomes one 8-byte
    vector. Other counts stay as plain scalars (4 bytes each, each on its own row).
    """
    size = 4
    if count % 4 == 0 and count > 0:
        return 16, count // 4
    if count == 2:
        return 8, 1
    return size, count


def cbuffer_layout(body: str) -> tuple[int, list[tuple[str, str, int, int]], list[str]]:
    """(DWORD size, [(type, name, offset, bytes)] per member, members this check cannot size)."""
    members, unknown = [], []
    for line in COMMENT_RE.sub("", body).splitlines():
        match = MEMBER_RE.match(line)
        if not match:
            continue
        type_name, name = match.group(1).lower(), match.group(2)
        count = int(match.group(3)) if match.group(3) else 1
        if member_bytes(type_name) is None:
            unknown.append(f"{type_name} {name}")
            continue
        members.append((type_name, name, count))
    offset, rows = pack_members(members)
    words = ((offset + 15) // 16) * 4 if offset else 0
    return words, rows, unknown


CPP_STRUCT_RE = re.compile(
    r"struct\s+(\w+)\s*\{(.*?)\}\s*;\s*(?:static_assert\s*\(\s*sizeof\s*\(\s*\1\s*\)\s*==\s*(\d+)\s*,[^;]*\)\s*;\s*)?", re.S)
CPP_MEMBER_RE = re.compile(r"^\s*(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;")
STATIC_ASSERT_RE = re.compile(r"static_assert\s*\(\s*sizeof\s*\(\s*(\w+)\s*\)\s*==\s*(\d+)\s*[,)]", re.S)


def parse_cpp_struct(name: str, text: str) -> tuple[tuple[int, list[tuple[str, str, int, int]]], int | None, list[str]]:
    """(packed layout of `struct <name>`, its static_assert byte size, members this check cannot size).

    The member lines use the same shape as HLSL (`type name[N];`); a C++ scalar array is packed
    element-per-element, so a mirror of an HLSL vector uses `float4 x` -> `float x[4]`.
    """
    assert_size: int | None = None
    members, unknown = [], []
    for line in COMMENT_RE.sub("", text).splitlines():
        match = STATIC_ASSERT_RE.search(line)
        if match and match.group(1) == name:
            assert_size = int(match.group(2))
    for struct_name, body, inline_assert in CPP_STRUCT_RE.findall(text):
        if struct_name != name:
            continue
        if inline_assert:
            assert_size = int(inline_assert)
        for line in COMMENT_RE.sub("", body).splitlines():
            match = CPP_MEMBER_RE.match(line)
            if not match:
                continue
            type_name, member = match.group(1).lower(), match.group(2)
            count = int(match.group(3)) if match.group(3) else 1
            if type_name not in CPP_TYPES:
                unknown.append(f"{type_name} {member}")
                continue
            members.append((type_name, member, count))
    offset, rows = pack_members(members, CPP_TYPES)
    return (offset, rows), assert_size, unknown


def fmt_layout(rows: list[tuple[str, str, int, int]]) -> str:
    return ", ".join(f"{name}@{(offset // 16) * 4}" for _, name, offset, _ in rows)


def compare_layouts(hlsl_rows: list[tuple[str, str, int, int]],
                    cpp_rows: list[tuple[str, str, int, int]]) -> list[str]:
    """Byte offsets then member names (lowercased); each difference becomes one failure line."""
    failures: list[str] = []
    for hlsl_row, cpp_row in zip(hlsl_rows, cpp_rows):
        if hlsl_row[2] != cpp_row[2] or hlsl_row[3] != cpp_row[3]:
            failures.append(
                f"{hlsl_row[1]}: offset differs (HLSL byte {hlsl_row[2]} / DWORD {hlsl_row[2] // 4} "
                f"vs C++ byte {cpp_row[2]} / DWORD {cpp_row[2] // 4})")
    if len(hlsl_rows) != len(cpp_rows):
        failures.append(
            f"member count differs (HLSL {len(hlsl_rows)}: {fmt_layout(hlsl_rows)} vs C++ "
            f"{len(cpp_rows)}: {fmt_layout(cpp_rows)})")
    for hlsl_row, cpp_row in zip(hlsl_rows, cpp_rows):
        if hlsl_row[1].lower() != cpp_row[1].lower():
            failures.append(
                f"{hlsl_row[1]} (HLSL) does not match C++ member {cpp_row[1]} "
                f"at byte {max(hlsl_row[2], cpp_row[2])} / DWORD {max(hlsl_row[2], cpp_row[2]) // 4}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--shaders", default=None, help="Assets/Shaders directory")
    parser.add_argument("--framework-src", default=None,
                        help="Framework/src directory (default: derived from --shaders or the repo root)")
    args = parser.parse_args()
    shader_dir = (Path(args.shaders) if args.shaders
                  else Path(__file__).resolve().parents[2] / "Assets" / "Shaders")
    registry_path = shader_dir / "compute_kernels.json"
    if not registry_path.exists():
        print(f"[constants] FAIL: no compute_kernels.json under {shader_dir}")
        return 1

    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    kernels = registry.get("kernels", registry if isinstance(registry, list) else [])
    kernel_ids: dict[str, str] = {}
    declared: dict[str, dict[int, int]] = {}
    for kernel in kernels:
        source = kernel.get("source")
        if not source:
            continue
        if kernel.get("id"):
            kernel_ids[source] = str(kernel["id"])
        for binding in kernel.get("bindings", []):
            if binding.get("type") == "constants":
                declared.setdefault(source, {})[int(binding["shader_register"])] = int(
                    binding["constant_count"])

    failures: list[str] = []
    canonical: dict[str, tuple[str, tuple, list[str]]] = {}
    aliases: list[str] = []
    strict: dict[str, list[tuple[str, tuple, list[str]]]] = {}
    checked_counts = 0
    checked_structs = 0
    files = sorted(list(shader_dir.glob("CS_Fluid*.hlsl")) + list(shader_dir.glob("CS_Particle*.hlsl"))
                   + list(shader_dir.glob("CS_Pbf*.hlsl")))

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
            # WO-2: the whitewater cbuffers have no sanctioned alias — any name divergence is a
            # failure, reported with the exact member that disagrees.
            if name in CROSS_CHECKS.values() and first is not None and path.name != first[0]:
                if layout != first[1]:
                    pass  # the generic layout check above already reported this pair
                elif names != first[2]:
                    strict.setdefault(name, []).append((path.name, layout, names))
            if register == "":
                continue
            expected = declared.get(path.name, {}).get(int(register))
            if expected is None:
                continue
            checked_counts += 1
            if expected != words:
                failures.append(
                    f"{path.name}: cbuffer {name} at b{register} is {words} DWORDs but "
                    f"compute_kernels.json ({kernel_ids.get(path.name, path.name)}) declares "
                    f"constant_count {expected}")

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

    # WO-2: cross-check the whitewater cbuffers against each other (strict member lists, no alias)
    # and against their C++ mirrors in Framework/src.
    if args.framework_src:
        framework_src = Path(args.framework_src)
    else:
        framework_src = shader_dir.resolve().parent.parent / "Framework" / "src"
    cpp_texts: dict[str, str] = {}
    for struct_name, relative in sorted(set(CPP_MIRRORS.items()), key=lambda kv: kv[0]):
        if struct_name in cpp_texts:
            continue
        cpp_path = framework_src / relative.removeprefix("Framework/src/")
        if not cpp_path.exists():
            failures.append(f"missing C++ mirror file {cpp_path} for struct {struct_name}")
            cpp_texts[struct_name] = ""
        else:
            cpp_texts[struct_name] = cpp_path.read_text(encoding="utf-8", errors="replace")

    for struct_name, copies in sorted(strict.items()):
        first = copies[0]
        for file_name, layout, names in copies[1:]:
            if layout != first[1]:
                failures.append(
                    f"{struct_name}: {file_name} has a different layout ({len(layout)} members) "
                    f"than {first[0]} ({len(first[1])})")
            else:
                for mine, theirs in zip(names, first[2]):
                    if mine != theirs:
                        failures.append(
                            f"{struct_name}: {file_name} names a member {mine} where {first[0]} "
                            f"names it {theirs}")
                        break

    for path in files:
        text = COMMENT_RE.sub("", path.read_text(encoding="utf-8", errors="replace"))
        for name, register, body in CBUFFER_RE.findall(text):
            if name not in CROSS_CHECKS.values():
                continue
            words, members, unknown = cbuffer_layout(body)
            if not members or unknown:
                continue
            struct_name = CROSS_CHECKS.get(path.name, name)
            relative = CPP_MIRRORS[struct_name]
            (offset, cpp_rows), assert_size, cpp_unknown = parse_cpp_struct(struct_name, cpp_texts[struct_name])
            if cpp_unknown:
                failures.append(f"{relative}: struct {struct_name} has members this check cannot size: "
                                f"{', '.join(cpp_unknown)}")
                continue
            checked_structs += 1
            cpp_words = ((offset + 15) // 16) * 4
            for message in compare_layouts(members, cpp_rows):
                failures.append(f"{path.name} cbuffer {struct_name} vs {relative} struct {struct_name}: {message}")
            if cpp_words != words:
                failures.append(
                    f"{path.name} cbuffer {struct_name} is {words} DWORDs but {relative} struct "
                    f"{struct_name} packs to {cpp_words} DWORDs ({offset} bytes)")
            if assert_size is not None and offset != assert_size:
                failures.append(
                    f"{relative}: static_assert(sizeof({struct_name}) == {assert_size}) disagrees "
                    f"with the packed layout of {offset} bytes")

    if failures:
        print(f"[constants] FAIL: {len(failures)} problem(s)")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    cross_checked = len(strict) + len(set(CROSS_CHECKS.values()))
    print(f"[constants] PASS: {len(canonical)} cbuffer name(s) across {len(files)} fluid kernel(s), "
          f"{checked_counts} registered constant_count(s) match, "
          f"{cross_checked} struct/kernel cross-check(s) (C++ mirror vs HLSL vs manifest)")
    for alias in aliases:
        print(f"  note: {alias}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
