# Current Status and Remaining Work

Status: editor/terrain local-worktree update on 2026-09-07; historical CI/rendering
evidence below remains dated 2026-08-30 and does not validate later uncommitted work.

This is the single source of truth for implementation maturity, verified gates, known limits, and remaining engineering work. Subsystem documents own behavior and commands; Git history preserves completed plans and superseded reviews.

## Engine Status

2026-09-09 editor SDK update: [T8ditorCore/static extensions](editor/editor-sdk.md)
now supports an external host, panels/commands, component inspectors/validators,
revision-checked gameplay edits, and shared factory registration for default Play.
x64/ARM64 Debug/Release solution builds pass (ARM64 compile/link only). The x64
Debug/Release gameplay suite has 53 passing tests. External MSBuild Debug/Release
self-tests and CMake Debug build/staging/self-test pass. Native Debug extension
workflows pass on D3D11/D3D12/Vulkan/OpenGL; Release extension and stock terrain
workflows pass on D3D12/Vulkan. Win32, Android, Steam Deck, and the full historical
visual-baseline matrix were not run for this slice. This is an initial SDK slice,
not replaceable Play, full document transactions, or a completed SceneWorld refactor.

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
| Authored heightmap editing | Native 16-bit import, sculpt/material brushes, mutable rendering, LOD, scene persistence |
| Generic tagged regions | Stable-ID oriented boxes, authoring controls, runtime point/tag queries |
| Square-grid terrain blockouts | Cell metrics, footprint occupancy, flat-only buildings by default, colored boxes, nav exclusions |
| Placement model visuals | Uniform GLB fitting, per-instance parts/clip playback, box proxy retained for collision/navigation |
| Editor tutorials | Separate [hands-on series](tutorials/README.md), including grid choice, terrain, buildings, and Play |
| Fidelity Play through temporary `.t8scene` | Implemented |
| Fast in-memory Play | Not implemented; optional future work |
| Gameplay hot reload/live Play editing | Not implemented |
| Visual state-machine graph | Not implemented; table authoring is supported |
| Cross-scene persistent gameplay entities | Not implemented; out of scope for v1 |
| 100/1,000-entity benchmark scenes | Not implemented; budgets remain unmeasured |

## Current Editor Evidence

The local terrain/placement work has focused Framework and editor regression
coverage: native precision, sculpting, LOD, regions, flatness, occupied footprints,
blockout geometry, undo/removal, reload, and hosted Play. The editor test requires
automatic window-driven runtime loading and verifies camera stability/movement.
See [terrain](terrain/heightmap-terrain.md) and [placement](terrain/placement-grid.md)
for exact commands and current bounds. No later CI/platform pass is implied by
the historical table below.

The local model-assignment fixture imports one GLB with 92 joints and 17 clips.
Checks cover square/rectangle fitting, independent animated bones, mixed static/
skinned shader attributes, invalid asset/clip/part transactions, Clear Model
undo/redo, reload, and hosted Play. D3D11/D3D12/Vulkan images were inspected; Vulkan
validation is clean after retaining immutable sampler variants for shared textures.
OpenGL lifecycle checks run, but the close-up editor capture is overexposed and
does not pass visual acceptance. This is not a cross-API pixel-parity result.

All six Windows configurations build with 52 self-tests on Win32/x64 Debug/Release.
ARM64 is cross-compiled, not executed; its clean Debug/Release rebuild required
`PreferredToolArchitecture=x64`, four workers and `/FS` after mixed-host PDB/linker
failures. Release terrain workflow regressions pass on D3D12/Vulkan. Android, Steam
Deck and the full historical visual-baseline suite were not rerun for this change.
See the illustrated [model assignment lesson](tutorials/05-assign-building-models.md).

## Editor Build Delivery (2026-09-08)

Windows CI already builds and verifies both hosts in every Win32/x64/ARM64
Debug/Release cell, including T8ditor in Release artifacts. Builds now request the
x64-hosted MSVC tools. The Steam Deck job explicitly builds and packages the separate
Linux `T8ditor` binary alongside `DayScene`, and checks the archive entry. The SteamRT
`--with-editor` flag now selects both build targets; previously it only configured
the editor target. `T850.sh --editor` launches it with the shared Linux setup.

Local shell tests verify target routing, missing-editor rejection, packaging and
launch arguments. These are not Linux compilation/render evidence; GitHub Actions
results must be checked against the pushed `editor_refactor_4` commit. With PR #34
open, validation uses the PR trigger; the temporary branch-push trigger was removed
to avoid duplicate matrices. Master pushes and release tags retain their triggers.
The initial Linux runs stopped before CMake on the discontinued Bullseye security
feeds. The build now retires those entries inside the disposable Sniper SDK while
preserving APT verification and all unrelated feeds. See the
[SteamRT prerequisite notes](platform/steam-deck.md) for the EOL scope and limits.

## Historical Evidence (2026-08-30)

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
