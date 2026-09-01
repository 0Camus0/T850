# Current Status and Remaining Work

Status: verified against source, local builds, deterministic rendering captures, and PR CI on 2026-08-30.

This is the single source of truth for implementation maturity, verified gates, known limits, and remaining engineering work. Subsystem documents own behavior and commands; Git history preserves completed plans and superseded reviews.

## Engine Status

| Area | State |
|---|---|
| D3D11, D3D12, OpenGL, Vulkan | Implemented peer backends on Windows |
| Graphics backend dispatch | Implemented: shared callers use `BaseDriver` capabilities; ImGui and GPU profiling use per-API strategies |
| JSON render graph/deferred/PBR/post processing | Implemented |
| Texture atlas framework | Implemented: managed memory textures, stable IDs, immutable rectangular atlas metadata, validated half-texel UVs, explicit pixelation |
| Minecraft production atlas | Implemented: classic `terrain.png`, named scene mapping, nearest sampling, blue translucent water tile |
| glTF/GLB, Draco, `.x`, mesh/material caches | Implemented |
| Animation/GPU bone texture/ragdolls | Implemented |
| Jolt physics/gameplay layers/queries | Implemented |
| Recast/Detour build/query/off-mesh links | Implemented |
| Gameplay v1 (P0-P14) | Implemented |
| Mutable voxel terrain reference | Implemented: chunks, atlas-aware meshes, streaming, FPS collision, edits, persistence |
| T8ditor game authoring/validation/overlays | Implemented |
| Fidelity Play through temporary `.t8scene` | Implemented |
| Fast in-memory Play | Not implemented; optional future work |
| Gameplay hot reload/live Play editing | Not implemented |
| Visual state-machine graph | Not implemented; table authoring is supported |
| Cross-scene persistent gameplay entities | Not implemented; out of scope for v1 |
| 100/1,000-entity benchmark scenes | Not implemented; budgets remain unmeasured |

## Verified Evidence

| Gate | Result |
|---|---|
| PR #33 build registration | Passed |
| Windows Win32/x64/ARM64 Debug and Release | Passed, including DayScene and T8ditor |
| Android arm64-v8a and x86_64 Release | Passed in CI |
| Android local development/production Debug | Passed |
| Steam Deck/SteamRT | Passed in CI |
| Gameplay/terrain/atlas self-tests | 43/43 passed |
| Vulkan validation | Zero errors in deterministic Minecraft capture |
| Atlas redesign visual preservation | Old accepted D3D12 versus redesigned D3D12 exact across all 12 render targets |
| Polymorphism refactor visual preservation | D3D12 exact against atlas-only baseline; D3D11/Vulkan/OpenGL variance unchanged |
| Deterministic API captures | D3D11, D3D12, Vulkan, and OpenGL exited 0 with complete nonuniform 1280x720 frame-61 dumps |
| Documentation and source diff | `git diff --check` passed before each pushed change |

Successful full CI run for the backend refactor and ARM64 fix: [GitHub Actions run 33325073153](https://github.com/0Camus0/T850/actions/runs/33325073153).

## Graphics Backend Architecture

Shared application, scene, editor, and diagnostic code does not downcast `BaseDriver` to perform backend work.

- `ImGuiRendererBackend` has D3D11, D3D12, OpenGL, and Vulkan implementations for platform/renderer initialization, frame hooks, draw submission, preview texture IDs, descriptor ownership, Android native-window rebinding, and shutdown.
- `ProfilerGpuBackend` has per-API timestamp-query strategies. `Profiler` retains API-neutral CPU timing, scope accounting, and reporting.
- `BaseDriver` virtual capabilities own API tags, shader dialect, UV origin, deferred-rendering support, render-target mip support, pre-present overlays, late-present sources, and native-surface suspend/resume.
- API switches remain at composition boundaries only: driver/backend factories, configuration parsing, API selection UI, and benchmark scheduling.

## Texture Atlas and Materials

`BaseDriver::CreateTextureFromMemory(key, ...)` registers memory-backed textures in the same owned texture registry as file resources. `TextureAtlas` is immutable metadata over a stable managed texture ID and supports rectangular images/tiles, exact grid validation, content identity, and half-texel UV regions.

`TextureAtlasDesc::pixelationFactor` is explicit. The Framework default preserves source detail; Minecraft authors `atlas_pixelation_factor: 2` to preserve its accepted pixel-art appearance.

`MaterialAssetCache::AcquireTextureVariant()` creates an immutable cached variant. Callers must not mutate texture pointers or IDs on an acquired material.

## Gameplay and Terrain

Gameplay v1 includes schema v2, stable IDs, fixed tick, component lifecycle, event/state systems, player/AI control, Jolt queries, navigation facade, groups, health/weapons, runtime DevGui, telemetry, editor authoring, validation, undo snapshots, and self-tests.

The generic `VoxelScene` remains a generated finite streamed terrain reference. Minecraft is a separate authored block-world integration using the Framework atlas, async chunk generation/remeshing, voxel-native A* navigation, collision-authoritative mob locomotion, gameplay HUD, and render graph. Recast remains an optional Minecraft diagnostic overlay and the production navigation path for mesh scenes.

## Build and Release State

- Windows primary toolchain: Visual Studio 2022/MSBuild/v143.
- Android and Steam Deck: CMake through platform wrappers.
- GitHub Actions: registration gate; Windows Win32/x64/ARM64 Debug+Release; Android arm64-v8a/x86_64; SteamRT; tagged-release packaging.
- `v*` tags package Windows ZIPs, Android APKs, Steam Deck tarball, and launcher into a GitHub Release.

Use:

- [Windows build and run](development/windows-build-and-run.md)
- [Android build and deployment](platform/android.md)
- [Steam Deck build and deployment](platform/steam-deck.md)
- [Verification gates](testing/verification.md)

## Remaining Engineering Work

1. Add Markdown-link and command-snippet validation to CI.
2. Add GPU-equipped retained visual reports as CI artifacts.
3. Create reviewed 100/1,000-entity Release benchmark scenes and measure budgets.
4. Expand headless success-path physics/navigation/editor save-reload tests.
5. Add release-package extraction/install smoke tests.
6. Add render-graph resource-lifetime validation and shader-cache operation tooling.
7. Add named atlas-region descriptors and mip-safe edge extrusion for filtered atlases.
8. Add voxel sunlight/emissive propagation, ambient occlusion, fluid simulation, and transparent sorting when required.
9. Add asynchronous chunk collision cooking, hierarchical voxel path regions for large crowds, LOD, indirect drawing, and floating origin based on measured project needs.
10. Add hot reload, cross-scene persistence, or granular editor commands only when a project requires them.

## Known Limits

- Heavy Q3 Vulkan scenes need more VRAM than the 2 GiB audit GPU for every retained visual case.
- Nexus visual cases require models not present in the public runtime manifest.
- Runtime telemetry flushes on normal shutdown; direct process termination can lose output.
- Android device install/launch and on-device performance still require equipped hardware evidence even though local and CI builds pass.
- Steam Deck runtime performance still requires device evidence even though SteamRT CI builds pass.
- JSON readers ignore unknown keys; scene/config spelling and validation remain important.
- Minecraft water currently uses a static authored atlas frame; animated fluids and simulation are not implemented.
- Generic `VoxelScene` still uses a generated atlas; Minecraft demonstrates the production file-backed atlas path.

## Related Documents

- [Documentation index](README.md)
- [Main architecture](architecture/main-architecture.md)
- [Dependency map](dependency-map.md)
- [Texture and IBL resources](rendering/textures-and-ibl.md)
- [FrameworkImGui](editor/imgui-system.md)
- [Diagnostics](debug/diagnostics.md)
- [Mutable voxel terrain](terrain/voxel-terrain.md)
- [Visual regression](debug/visual-regression.md)
