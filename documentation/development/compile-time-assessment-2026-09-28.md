# Cross-Platform Build and Compile-Time Assessment

Status: measured on `microsoft_dawn_d3d12_cache_proposal` commit `1b0aa68e` on 2026-09-28. No compile-time optimization from this assessment is enabled in production yet.

## Purpose

This assessment closes the platform compilation audit and measures where clean C++ build time is spent. It distinguishes verified builds, measured compiler work, reversible experiments, and unmeasured estimates.

## Platform validation

| Platform | Configuration/architecture | Evidence | Result |
|---|---|---|---|
| Windows | Win32 Debug/Release | GitHub Actions run `36521043888` | PASS: build, output verification and native self-tests |
| Windows | x64 Debug/Release | GitHub Actions run `36521043888` | PASS: build, Dawn probes, native self-tests and WebAssembly Release build |
| Windows | ARM64 Debug/Release | GitHub Actions run `36521043888` on the VS 2026 ARM image with v143 compatibility | PASS: dependencies, Dawn probes, build, output verification and native self-tests |
| Android | `arm64-v8a` Release | GitHub Actions run `36521043888` | PASS: development/production APK build and upload |
| Android | `x86_64` Release | GitHub Actions run `36521043888` | PASS: development/production APK build and upload |
| Browser WebGPU | Native x64 and ARM64 Edge | GitHub Actions run `36521043888` | PASS: compute and recovery tests |
| SteamRT/Linux | Release runtime and T8ditor | GitHub Actions run `36521043888` | PASS: build, required outputs, package and upload |
| Physical Steam Deck | SteamRT Release runtime and T8ditor on `deck@10.0.0.225` | Dedicated checkout `/home/deck/Code/T850-pr49-validation`, exact commit `1b0aa68e` | PASS: official build and 78/78 wrapper self-tests |

The physical Deck official first build took 560 seconds, including container preparation and first-checkout dependency work. A dependency-warm clean application rebuild of DayScene and T8ditor took 198.860 seconds; a repeated clean rebuild took 188.765 seconds.

Android validation is compile/package evidence. No equipped Android device was attached for install, launch or GPU runtime testing in this pass.

## Windows measurement method

The supported MSBuild path was rebuilt as x64 Release with `CL=/Bt+` and four workers. The complete build passed with zero compiler/MSBuild warnings and zero errors.

| Metric | Result |
|---|---:|
| Wall time | 117.583 s |
| Compiler timing records | 438 |
| Translation units | 218 |
| Front-end `c1xx` work | 438.730 compiler-s |
| Back-end `c2` work | 47.573 compiler-s |
| Front-end share | 90.2% |
| Total compiler work | 486.303 compiler-s |
| Effective parallelism | 4.14x |

The result is front-end dominated. Header parsing, templates and large translation units matter more than optimizer/code-generation work.

### Worker scaling

| Workers | Clean x64 Release wall time | Difference from 31 workers |
|---:|---:|---:|
| 4 with `/Bt+` | 117.583 s | +4.9% |
| 16 | 118.956 s | +6.1% |
| 31, current default | 112.104 s | baseline |

Increasing parallelism beyond four workers provides only about a 5% improvement on this host. More workers do not solve the long-TU critical path.

## Measured target cost

| Target | Compiles | Front-end work | Average front end | Total compiler work |
|---|---:|---:|---:|---:|
| Framework | 174 | 216.014 s | 1.241 s | 218.103 s |
| T8ditor | 21 | 115.937 s | 5.521 s | 143.404 s |
| DayScene | 12 | 85.138 s | 7.095 s | 102.872 s |
| FrameworkImGui | 9 | 20.195 s | 2.244 s | 20.457 s |

Framework's existing PCH keeps its average low. DayScene, T8ditorCore and FrameworkImGui do not configure a project PCH, so their large source files dominate the critical path.

### Slowest translation units

| Translation unit | Total compiler work |
|---|---:|
| `T8ditor/EditorApp.cpp` | 48.877 s |
| `DayScene/SceneTemplate.cpp`, two target compiles combined | 22.493 s |
| `DayScene/RagdollEditor.cpp`, two target compiles combined | 21.341 s |
| `DayScene/Quake3Mock.cpp`, two target compiles combined | 20.674 s |
| `Framework/src/scene/EditorSceneFile.cpp` | 17.829 s |
| `Framework/src/scene/SceneDescriptor.cpp` | 8.905 s |
| `T8ditor/EditorTutorialCapture.cpp` | 8.888 s |
| `T8ditor/RagdollEditorPanel.cpp` | 8.757 s |
| `T8ditor/PlayScenePanel.cpp` | 8.343 s |
| `Framework/src/utils/gltf/GLTFJson.cpp` | 7.963 s |

`EditorApp.cpp` is 625 KB/12,741 lines with 65 direct includes. `SceneTemplate.cpp`, `Quake3Mock.cpp` and `RagdollEditor.cpp` are each 575-720 KB and 12,526-15,330 lines.

## Header dependency fan-out

The physical Deck Ninja dependency database contains:

| Metric | Result |
|---|---:|
| Object dependency records | 186 |
| Header dependency edges | 145,515 |
| Average dependencies per object | 782.3 |
| T850-controlled edges | 7,125 |
| Framework-header edges | 3,905 |
| Vendored-header edges | 2,408 |

Highest-fan-out project headers:

| Header | Objects including it |
|---|---:|
| `Framework/Config.h` | 179 |
| `Framework/include/utils/xMaths.h` | 177 |
| `Framework/include/utils/xDefs.h` | 164 |
| `Framework/pch.h` | 144 |
| `Framework/include/video/ShaderBase.h` | 107 |
| `Framework/Descriptors.h` | 107 |
| `tinyxml2.h` | 104 |
| `ShaderProgramCache.h` | 104 |
| `BaseDriver.h` | 103 |

The first three are foundational types already covered by Framework's PCH. Their fan-out is architectural debt, but changing them has high blast radius and is not the first optimization target.

## Reversible experiments

### Broad target PCH

A Deck-only reversible experiment added `Framework/pch.h` to DayScene, T8ditorCore and FrameworkImGui through `target_precompile_headers`. Source files were restored after each run.

| Run | Normal clean build | PCH clean build | Difference |
|---:|---:|---:|---:|
| 1 | 198.860 s | 187.557 s | -11.303 s |
| 2 | 188.765 s | 188.829 s | +0.064 s |
| Mean | 193.812 s | 188.193 s | -5.620 s / -2.9% |

The normal-run range is 10.095 seconds, larger than the mean saving. Broad PCH is therefore promising but inconclusive; do not land it based on these two samples. A five-run alternating experiment is required.

### CMake unity build

`CMAKE_UNITY_BUILD=ON`, batch size 8, did not compile. It exposed real unity collisions:

- SPIR-V enum redefinitions from `spirv.h`;
- ambiguous anonymous-namespace `NodeLocalMatrix` helpers across glTF translation units.

Unity is not a low-risk switch for this tree. It requires source cleanup and per-source `SKIP_UNITY_BUILD_INCLUSION` policy before timing claims.

## Prioritized opportunities

### 1. Split `EditorApp.cpp`

This is the largest measured critical-path unit: 48.877 compiler-seconds. Extract cohesive panels, loading/progress code, hosted viewport orchestration and scene import/export into independently compiled implementation files.

Expected effect: 10-25 seconds lower clean wall time on a highly parallel Windows host if the split removes the current critical path. Total compiler CPU may stay flat or rise slightly because headers are repeated. Validate with `/Bt+`; do not infer success from file size alone.

### 2. Compile shared scenes once

`SceneTemplate.cpp`, `Quake3Mock.cpp` and `RagdollEditor.cpp` are compiled into both DayScene and T8ditorCore. They contain no target-specific `_LIB`, `_CONSOLE`, `T8DITOR`, `EDITOR` or `DAYSCENE` preprocessor branches.

Compiling them once in a shared static scene-runtime target would remove approximately 32.254 compiler-CPU seconds per x64 Release rebuild. Expected wall saving is roughly 5-10 seconds, depending on scheduling and link order. This must preserve MSBuild/CMake/Android registration and Linux archive ordering.

### 3. Evaluate a Glaze-focused PCH

Sixteen production Framework/WebGPU translation units plus `EditorApp.cpp` include `glaze/glaze.hpp`; together they consumed about 136.196 compiler-seconds in the measured build. Glaze is already correctly isolated from public headers, so moving includes is not the answer. A secondary PCH for JSON-heavy translation units may reduce repeated template parsing, but template instantiation remains per TU.

Run a source-specific A/B before implementation. Plausible wall saving is 3-10 seconds; this is not yet measured.

### 4. Reduce public GL coupling

`RenderMesh.h` includes GLEW/`GLTexture.h` under OpenGL defines despite exposing no GL types, and `Utils.h` exposes GL-typed helper signatures only under `USING_GL_COMMON`. Moving implementation-only GL includes to `.cpp` files can reduce Windows parse fan-out through headers used by 29-38 objects.

Expected saving is small, likely 1-3 seconds on Windows and near zero on Vulkan-only SteamRT where OpenGL defines are absent. The architectural cleanup is still worthwhile.

### 5. Keep current worker default

The current `cores - 1` policy was fastest among measured worker counts. Do not lower it globally. Memory-constrained hosts may still set `T850_BUILD_WORKERS=4`.

## Expected practical saving

Do not add individual estimates linearly; several changes attack the same front-end work.

A realistic first target is 10-20 seconds (about 9-18%) from the 112-second Windows clean baseline by splitting `EditorApp.cpp` and compiling shared scenes once. A validated JSON PCH could extend that, while broad PCH alone currently supports no stronger claim than 0-6%.

For the Deck's dependency-warm 189-199 second application build, a reasonable target is 170-185 seconds after the same structural work. This remains a target, not a measured result.

## Recommended implementation sequence

1. Add a repeatable compile-timing script that captures wall time and MSVC `/Bt+` records without changing default builds.
2. Split `EditorApp.cpp`; run five alternating clean x64 Release builds and full platform gates.
3. Introduce one shared scene-runtime target in both MSBuild and CMake; verify Android/Steam link ordering.
4. Run a five-sample broad-PCH and JSON-PCH experiment; land only statistically clear wins.
5. Clean unity collisions only if unity remains desirable after steps 2-4.

## Related documents

- [Windows setup, build and run](windows-build-and-run.md)
- [Steam Deck build and deployment](../platform/steam-deck.md)
- [Android build and deployment](../platform/android.md)
- [Verification](../testing/verification.md)
- [Shader management](../rendering/shader-management.md)
