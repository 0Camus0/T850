---
name: t850-compute-kernel
description: "Use when adding, changing, debugging or validating a T850 compute kernel or a render-graph compute pass: new CS_*.hlsl kernel, compute_kernels.json entry, compute_resources / compute_buffers schema, storage format, binding registers, constants builder, dispatch extent, GLSL twin, or a compute pass that silently falls back to graphics."
argument-hint: "State the kernel name, its bindings (b/t/s/u registers), whether it needs a constants builder, its dispatch extent (render target or compute buffer), and the target backends."
---

# T850 Compute Kernel and Compute Pass

A compute kernel is not "done" when the shader compiles. Four independent registries must agree,
and each one fails in a different, quiet way. This workflow exists because getting them out of
sync costs a rebuild cycle per mistake.

Use this for any work that touches:
`Assets/Shaders/CS_*.hlsl`, `Assets/Shaders/compute_kernels.json`,
`compute_resources`/`compute_buffers` in `Assets/Scenes/*_RenderGraph.json`, or the
`pipeline_configurator` / `constants_builder` callbacks in `ComputeKernelRegistry.cpp`.

## The Four Registries That Must Agree

| # | Registry | File | Failure mode if wrong |
|---|---|---|---|
| 1 | Kernel source | `Assets/Shaders/CS_*.hlsl` | `[Vulkan][Compute] Shader parse failed` |
| 2 | Kernel manifest | `Assets/Shaders/compute_kernels.json` | `Kernel '<x>' is not registered` — and one bad entry aborts the **whole** manifest |
| 3 | Constants layout | `cbuffer` in HLSL vs `constant_count` in manifest vs host struct | `does not declare the kernel's complete binding layout`, or silent garbage |
| 4 | Graph pass | `Assets/Scenes/<Scene>_RenderGraph.json` | `using graphics implementation` fallback, with only one ERROR line |

## 1. Write the Kernel Against the Engine's Conventions

Read an existing kernel of the same shape first — do not invent the preamble.

- Texture-splat / per-pixel accumulate: `Assets/Shaders/CS_TorchParticles.hlsl`
- Volume + atomic reduce: `Assets/Shaders/CS_FluidVolumeReduce3D.hlsl`
- Particle buffer stages: `Assets/Shaders/CS_Pbf*.hlsl`

Required conventions:

```hlsl
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(N, 0)]]
#endif
```

- One constant buffer per kernel, at `b0`, matching the host struct **field for field**.
- `RWStructuredBuffer` only — scalar atomics only. `InterlockedAdd` on a vector component
  crashes Tint's SPIR-V reader (WebGPU) and has no fallback.
- Never use `isfinite`/`isnan`/`isinf`, scalar or vector: they lower to `OpIsNan`/`OpIsInf`
  and WebGPU's Tint reader aborts the process. Test the exponent bits —
  `(asuint(v) & 0x7F800000u) != 0x7F800000u` — which is false exactly for NaN and ±Inf.
- A `RWTexture2D` storage output needs **both** qualifiers, in this order, or
  `ComputeReflection` rejects it:

```hlsl
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16float]]   // must match the RT colour format
#endif
RWTexture2D<float4> OutputTexture : register(u0);
```

- `#include` is **not** used by any shipped kernel. Duplicate shared helpers instead of
  extracting a header; glslang include handling is unverified here.
- No grid-wide barriers exist. If a stage needs all particles written before it runs, split it
  into two kernels and dispatch them in order.

## 2. Register the Kernel in `compute_kernels.json`

The manifest is parsed strictly; **one invalid entry discards every kernel** and every compute
pass in every scene falls back to graphics.

Entry shape:

```json
{
  "id": "pbf_integrate",
  "source": "CS_PbfIntegrate.hlsl",
  "entry": "CS",
  "permutations": ["base"],
  "bindings": [
    { "type": "constants",         "shader_register": 0, "binding_index": 0, "constant_count": 32 },
    { "type": "read_write_buffer", "shader_register": 0, "binding_index": 1 },
    { "type": "read_only_buffer",  "shader_register": 1, "binding_index": 2 },
    { "type": "storage_texture",   "shader_register": 2, "binding_index": 3, "storage_format": "rgba16float" }
  ]
}
```

Rules that bite:

- `source` must be a **filename unique** across the manifest; duplicates abort loading.
- `constant_count` must equal the HLSL `cbuffer` size in DWORDs and be a **multiple of 4**.
  `scripts/validation/check_fluid_constants.py` cross-checks both and fails on a mismatch; pad
  the cbuffer and the host struct together (a 29-field struct becomes 32).
- `shader_register` is the HLSL `tN`/`sN`/`uN` index, **not** the descriptor index. The registry
  rejects a duplicate `shader_register` for the same binding type (two buffers at `u0`).
- `binding_index` must be unique within the entry (also registry-enforced).
- Do not declare the same sampler register twice. It is legal HLSL but glslang auto-mapping
  gives the second a fresh binding, which WebGPU rejects at bind time;
  `check_fluid_constants.py` fails it for you.
- `storage_format` is required for `storage_texture` and drives RT format validation.
- Read-only cube textures are portable: declare `"dimension": "cube"` on a `sampled_texture`
  and every backend binds the texture's own cube view. T850's compute abstraction has no
  writable cube binding: reflection assigns `Cube` only to sampled textures and the
  backends reject it for `storage_texture`.
- Only `type` values `constants | read_only_buffer | read_write_buffer | sampled_texture |
  storage_texture | sampler` are valid.

### Constants with no scene dependency

Omit `constants_builder`. The graph executor still needs *something* for `@kernel_constants`;
a kernel with no builder fails with:

```
cannot build constants: kernel does not expose scene-driven constants
```

Either add a builder (below) or use a kernel that has one.

### Constants from camera / SceneProps

1. Add fields to `Framework/include/scene/SceneProp.h`.
2. Add a packed struct + builder in `Framework/src/utils/ComputeKernelRegistry.cpp`
   (`static_assert` the exact byte size).
3. Register the name in `ResolveConstantsBuilder`.
4. Set `"constants_builder": "<name>"` in the manifest entry.

## 3. Add the Pass to the Render Graph

`compute_extent_from` selects the dispatch grid:

```json
{
  "name": "Simulate", "target": "", "execution": "compute_if_supported",
  "compute_shader": "Shaders/CS_PbfIntegrate.hlsl", "compute_entry": "CS",
  "compute_extent_from": "ParticlePositions",
  "compute_resources": [
    { "resource": "@kernel_constants", "access": "constants",    "shader_register": 0 },
    { "resource": "ParticlePositions", "access": "buffer_write", "shader_register": 0 }
  ],
  "draws": []
}
```

- A render target gives a 2D grid; a `compute_buffers[]` name gives a 1D grid over
  `element_count`.
- `compute_resources` must declare the kernel's **complete** binding set — an extra or missing
  entry is `does not declare the kernel's complete binding layout`.
- Access values: `constants`, `sampled`, `sampler`, `storage_write`, `buffer_read`,
  `buffer_write`.
- A `storage_write` target must have `"storage": true` in `render_targets` and a colour format
  matching the kernel's `storage_format`.
- One pass may bind the same **buffer** to several registers (including two write registers);
  read/write feedback on the *same* buffer in one dispatch is rejected.
- Dispatches above 65535 groups per dimension are rejected as `[DispatchTooLarge]`.

## 4. Keep the GL Twin and the Docs in Step

- Every kernel GL can load needs `<stem>.glsl` next to the `.hlsl`. GL is the only backend that
  reads the twin; Vulkan/D3D compile the HLSL.
- Generated twins carry `hlsl-sha256` in line 2. After editing the HLSL either regenerate with
  `scripts/shaders/generate_gl_compute_twins.ps1 -Force` (Windows) or, from `T850/` on Linux,
  `SPIRV_CROSS=<spirv-cross> python3 scripts/shaders/generate_gl_compute_twins.py CS_X.hlsl` (tools from
  `PATH` or `GLSLANG`/`SPIRV_CROSS`; `--check` verifies), or update the twin by hand **and** fix
  the hash, then check:

```bash
python3 T850/scripts/validation/check_gl_twins.py --shaders T850/Assets/Shaders
python3 T850/scripts/validation/check_fluid_constants.py
```

`check_fluid_constants.py` verifies cbuffer size against `constant_count`, the multiple-of-4
rule, and duplicate sampler registers. Note it only *scans* `CS_Fluid*.hlsl` by default — for a
new kernel family, confirm it is covered or check the counts by hand.

- Update `documentation/rendering/render-graph.md` (schema, validation rules) and
  `documentation/rendering/compute-shader-implementation.md` (kernel behaviour) in the same
  change. Record the **verification status per backend** — do not imply D3D/GL validation that
  has not run.

## 5. Validate

```bash
# Build (Linux)
cd T850
cmake --build build/linux --target DayScene --parallel $(nproc)

# Every maintained graph still loads strictly, plus the synthetic compute fixtures
bin/SteamDeck/Release/DayScene --unattended --api vulkan --game-selftest
# Expected: 78 PASS lines, exit 0. Any FAIL blocks completion.

# A scene that actually uses the pass, in compute mode
bin/SteamDeck/Release/DayScene --unattended --api vulkan --scene N \
  --postProcessMode compute --dump-frame 6 --logLevel info
```

Then prove the pass really dispatched, do not assume it:

```bash
grep -E "dispatched compute|enabled compute kernel|using graphics implementation|ERROR" log
```

- `enabled compute kernel '<x>'` + `dispatched compute N x M x Z` = working.
- `using graphics implementation by post-process mode` = compute is off; add
  `--postProcessMode compute`.
- `compute dispatch failed; using graphics fallback` = it ran and failed; read the preceding
  ERROR. With no ERROR before it, the backend's dispatch validation returned false silently;
  `t850-error-triage` lists the causes seen so far and how to instrument it.

Windows backends cannot be built on Linux (`MSVC` + `.vcxproj` + `windows-msvc-x64` triplets).
Proton runs Windows binaries; it does not produce them. Until `scripts/build.ps1` plus a
`--game-selftest` run has happened on Windows, D3D11/D3D12 are **unvalidated** — say so.
From Linux, `t850-windows-remote-host` builds and runs D3D11, D3D12, GL and WebGPU on a Windows machine.
