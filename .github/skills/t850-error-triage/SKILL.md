---
name: t850-error-triage
description: "Use when a T850 run, build, test or CI job reports an [ERROR], a compute pass falls back to graphics, a compute pipeline fails to create, a backend rejects a dispatch, MSBuild or SetupDawn reports a Dawn package problem, a link fails with undefined symbols, or a process exits with a native code. Maps each T850 message to the code that emits it and the check that proves the cause before any fix, rebuild or change to another machine."
argument-hint: "Paste the first error line, the exact command, the platform and API, and how the binary was built."
---

# T850 Error Triage

Find what produced a message before acting on what it seems to say. The costliest mistakes in T850 work came from
fixing the symptom a message named instead of the condition that emitted it:

- "Required Dawn package metadata is missing" came from a sync that wrote the project into the wrong folder; the
  package was intact.
- "Dawn setup changed" came from a copy that converted line endings.
- "compute dispatch failed" came from backends that rejected a cube texture without logging why.

## 1. Prove the premise

Before any fix, and always before an action that takes more than a few minutes or changes another machine
(`SetupDawn.ps1`, a vcpkg install, a full rebuild, deleting or moving folders):

1. Find the emitter. Grep the fixed part of the message,
   `grep -rn -F "<fixed text>" T850/Framework/src T850/DayScene T850/scripts T850/cmake`, and read the condition
   around it.
2. Check the inputs of that condition: the path it names exists where you think, the binary is the one you just
   built, and the API and scene are the ones you asked for (section 2).
3. Write the cause as one falsifiable sentence and run one cheap check that would disprove it.
4. Fix, rerun the same command, and require that the message is gone.

If a function fails without a message (a bare `return false`), add a temporary `T8_LOG_ERROR` before each
`return false`, rerun one case, read which one fires, and remove the logging before committing.

Compare APIs before reading code: a failure on every API points at the kernel, the manifest or the graph; a failure
on one API points at that backend.

## 2. Confirm what actually ran

| Question | Check |
|---|---|
| Which API? | The API tag in each log line (present once the driver exists) and the dump folder name `dumps_<api>_f<frame>_<time>`. Linux and Android are Vulkan-only: `--api gl`, `d3d11` or `d3d12` logs `... backend is Vulkan-only; forcing Vulkan` and runs Vulkan. D3D, GL and WebGPU evidence needs Windows (`t850-windows-remote-host`). |
| Which binary? | Its modification time is later than the build start. An incremental build after sources arrived with old timestamps keeps stale objects; rebuild when in doubt. |
| Which scene? | Pass `--scene N` explicitly on every run. |
| Did the run finish? | Exit code 0 and, for `--dump-frame`, `RT dump complete` in the log. A rare crash or hang right after the dump is the known `exit(0)` race in `DayScene.cpp`: rerun once before investigating. |
| Is a difference real? | Compare against two runs of the reference; the fluid solver is not deterministic (`t850-api-frame-comparison`). |

## 3. Message map

Grep the fixed part of a message to find its current line; line numbers drift.

### Compute

| Message | Emitter | Meaning and first check |
|---|---|---|
| `[RenderGraph] Pass '<p>' compute dispatch failed; using graphics fallback` | `RenderGraph.cpp`, after `ExecuteComputePass` returns false | The backend refused the dispatch, every frame. Read the lines just before it. With none, the backend's binding validation returned false silently: read and instrument the dispatch validation in `video/<api>/<Api>Compute.cpp` (WebGPU: `WebGPUDriver.cpp`). During the Windows port, D3D12, D3D11, GL and WebGPU each rejected the environment cube bound to a compute pass this way. |
| `[RenderGraph] Pass '<p>' failed to create compute pipeline` | `RenderGraph.cpp` | Compile, reflection or layout validation failed. The cause is logged just before it (`Shader compile failed`, `[ComputeReflection]`, `SetValidatedLayout failed`); on Vulkan it can be the only line. |
| `[D3D12]`, `[D3D11]` or `[GL][Compute] Shader compile failed for '<k>'` | backend compiler | Read the compiler text. Compute kernels cannot use `Sample`, `SampleBias`, `SampleCmp`, `ddx` or `ddy`; use `SampleLevel` or `SampleCmpLevelZero`. |
| `[GL][Compute] SetValidatedLayout failed for '<k>'`, after `reflected ...` and `declared ...` lines | `GLCompute.cpp` | The bindings glslang reflected from the GLSL twin differ from `compute_kernels.json`. Compare the two tables row by row (count, type, reg, idx, dim, fmt). Exact duplicate rows, such as a SPIRV-Cross `..._SPIRV_Cross_DummySampler` alias of a texture, are dropped before the check; any other difference means the twin is stale or the manifest is wrong. |
| `[ComputeReflection] Image <b> rejected: ...`, `Binding <b> rejected: ...` | `ComputeReflection.cpp` (Vulkan and GL pipeline creation) | Kernels may use 2D and 3D images and read-only cubes, in descriptor set 0, never arrays or MSAA. A sampled cube needs `"dimension": "cube"` in the manifest. |
| `[ComputeReflection] Storage image <b> is write-only` | `ComputeReflection.cpp` | Misleading wording: it fires when the storage image is *not* write-only, i.e. the kernel reads an `RWTexture`. Storage images must only be written. |
| `[ComputeReflection] Storage image <b> has unsupported format` | `ComputeReflection.cpp` | Storage formats are `rgba8`, `rgba16f` and `r32f`. |
| `[RenderGraph] Pass '<p>' does not declare the kernel's complete binding layout` | `RenderGraph.cpp` | The pass's `compute_resources` do not cover exactly the kernel's manifest bindings. Diff the two lists. |
| `[RenderGraph] Pass '<p>' storage format '<f>' does not match kernel '<g>'` | `RenderGraph.cpp` | The render target's format differs from the kernel's storage format. |
| `[RenderGraph] Pass '<p>' cannot build constants: <reason>` | constants builder, `ComputeKernelRegistry.cpp` | Read the reason; the builder is the manifest's `constants_builder`. |
| `[D3D12][Compute] Constants b<n> do not match reflected layout` | `D3D12Compute.cpp` | The builder's DWORD count differs from the HLSL cbuffer. Check the cbuffer, the C++ struct and its `static_assert`, and `constant_count`; run `check_fluid_constants.py`. Add new members at the end, padded to 16 bytes. |
| `[RenderGraph][DispatchTooLarge] ...` | `RenderGraph.cpp` | More than 65535 groups in one dimension; check the extent source. |
| `[<API>][Compute] Cube t<n> does not match: ...` | each backend's dispatch validation | The texture bound to a cube slot is not a six-face cube: check that the graph binds `@environment_map` and that the scene has a sky or IBL cube. |
| `[WebGPU][Compute] Dispatch failed: <Dawn text>` | `WebGPUDriver.cpp` | Dawn's text names the cause. `Binding size (N) ... larger than the maximum storage buffer binding size (M)`: the device already requests the adapter's limits, so the buffer is too large for this adapter. |
| `[GL][Compute] Dispatch failed with GL error 0x<e>` | `GLCompute.cpp` | A GL error after the dispatch; check image formats and texture units. |
| stderr `TINT_UNIMPLEMENTED unhandled SPIR-V instruction: OpIsNan`, exit `0xC0000409` (WebGPU) | Dawn's Tint compiler | `isfinite`, `isnan` or `isinf` in a kernel: use `(asuint(x) & 0x7F800000u) != 0x7F800000u`. Tint also aborts on `InterlockedAdd` on a vector component (`t850-compute-kernel`). The engine log has no line for this; read stderr. |
| `[RenderGraph] Pass '<p>' using graphics implementation by post-process mode` (INFO) | `RenderGraph.cpp` | Expected without `--postProcessMode compute`; not an error. |

### Build, link and Dawn

| Message | Emitter | Meaning and first check |
|---|---|---|
| `Required Dawn package metadata is missing. Run <path>scripts\SetupDawn.ps1 ...` | `cmake/DawnPackage.targets` | `<project>\build\dawn-package\Dawn.<Config>.props` (`dawn-package-arm64` on ARM64) does not exist. The message prints the project folder it checked (`Run <project>\scripts\SetupDawn.ps1`): confirm it is the folder that holds `T850.sln` and that the file is missing there. Only then is SetupDawn needed, and ask first: without a provisioned package it clones the pinned vcpkg and builds Dawn, a long build. |
| `MSB3073` from `DawnPackage.targets` running `SetupDawn.ps1 -Mode Check` | `SetupDawn.ps1` | Read the inner message (next rows). |
| `Dawn setup changed (<file>)` | `SetupDawn.ps1` | The raw bytes of a recipe file differ from `build\dawn-package\package-audit.json`. Recipe files are `cmake/vcpkg-overlays/**`, `cmake/dawn-package/*`, `scripts/SetupDawn.ps1` and the translator sources (`WebGPUShaderCompiler.*`, `ShaderPreprocessor.*`, `Librerias/simplecpp/simplecpp.*`). If nobody meant to change that file, compare its line endings and `git status` first: converting between CRLF and LF changes the hash, not the content; restore the bytes and compare the SHA-256 with the audit. After an intentional translator edit, rerunning setup is the documented step and does not rebuild unchanged packages (`documentation/development/windows-build-and-run.md`); a changed overlay rebuilds Dawn. |
| `Dawn package location or pin changed` | `SetupDawn.ps1` | The audit's source root, vcpkg pin or triplet differ: the project moved, or the vcpkg submodule pointer changed. |
| `Dawn package ABI changed` | `SetupDawn.ps1` | Files in the installed vcpkg tree changed. |
| `Dawn link metadata is missing`, `Generated Dawn linker properties are missing`, `Generated Dawn link properties changed` | `SetupDawn.ps1` | `package-audit.json` or the generated props under `build\dawn-package` were deleted or edited. |
| `Generated Dawn shader compiler identity is missing or stale` | `SetupDawn.ps1` | `T850DawnShaderConfig.h` does not match the package ABI and translator-source hashes: a translator source changed since setup ran (see `Dawn setup changed`). |
| `Dawn requires vcpkg <pin> at <root>; found <x>` | `SetupDawn.ps1` | The vcpkg checkout is at another commit. Do not update vcpkg; ask. |
| `ld.lld: error: undefined symbol: <Class>::<member>` (Android), `LNK2019` or `LNK2001` (MSVC), for code you added | linker | The new `.cpp` is missing from a build list. The vcxproj, the desktop `CMakeLists.txt` and `cmake/AndroidBuild.cmake` are separate lists. Run `check_change.py` (`t850-change-self-check`). |
| `<Owner> is missing <Entry>` (CI `validate-build-registration`) | `ValidateBuildRegistration.ps1` | Add the entry to that owner. For Framework and DayScene the script checks only `game/`, `terrain/` and a fixed list of files, so passing it does not prove the Android list is complete. |

### Native exit codes

| Code | Meaning |
|---|---|
| `0xC0000409` (-1073740791) | Fast-fail or abort. On WebGPU, first read stderr for a Tint compiler abort. Otherwise `t850-crash-debugging`. |
| `0xC0000005` | Access violation: `t850-crash-debugging`. |

## 4. Classify a visual difference

Dump every render target for the reference and the candidate, and find the first target that differs beyond the
run-to-run noise (order in `t850-api-frame-comparison`). A difference already present in `RT_Dump_GBuffer_Albedo`,
before any water or post pass, predates those passes: report it as pre-existing instead of fixing it inside an
unrelated change.

## 5. Report

The first error line, the emitter (file and condition), the cause and the check that proved it, the fix, and the rerun
that shows the message gone.
