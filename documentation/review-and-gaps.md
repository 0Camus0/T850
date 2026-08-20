# Current Review and Gaps

Status: full source/documentation freshness pass completed on 2026-08-19.

This file lists current gaps and troubleshooting routes. Historical findings that were already resolved were removed; use Git history for old assessments.

## Audit Performed

The pass covered:

- all current subsystem documents;
- Windows setup/build/launcher/config/assets;
- Android setup/build/sign/install/deploy;
- Steam Deck SteamRT/SSH/run/package workflows;
- gameplay P0-P14 runtime/editor source;
- FrameDumper and visual comparison scripts;
- CI/release workflow;
- root README, documentation index, glossary, and skill routing.

Validation evidence is summarized in [Current status](current-status-and-roadmap.md).

## Documentation Coverage

Current authoritative coverage exists for:

- architecture and platform lifecycle;
- resources and cloud assets;
- runtime CLI/config;
- rendering, geometry, shaders, textures, animation;
- physics, navigation, gameplay;
- scenes, runtime hosts, T8ditor, FrameworkImGui;
- diagnostics, tests, image dumps, visual comparison;
- Windows, Android, Steam Deck, CI/release packaging.

The remaining gaps are engineering/automation/equipped-host evidence and advanced product features, not missing basic guides or mutable-terrain foundations.

## Remaining Gaps

### Remaining CI Documentation Enforcement

CI now fails on the maintained gameplay/terrain/mutable-mesh source-registration contract across MSBuild, filters, desktop/Steam CMake, and Android CMake. It also builds all six Windows cells and runs Win32/x64 self-tests. CI does not yet fail on:

- broken Markdown relative links;
- stale command snippets.

Add a repository Markdown validation script and invoke it from `.github/workflows/build.yml`.

### Equipped-Host Local Platform Evidence

The audit host lacked Podman and Android SDK/NDK, so local platform commands stopped at prerequisite checks. CI defines both platform builds, but an equipped local validation record should still capture:

- exact Podman/SteamRT build and package result;
- Android SDK/NDK/JDK/Vulkan versions;
- signed ARM64 APK install/launch on device;
- runtime logs and artifact hashes.

### Gameplay Performance Budgets

The gameplay spec defines 100/1,000-entity targets, but benchmark scenes and measured Release data are absent. Do not claim those budgets are met until reproducible scenes and telemetry reports exist.

### Test Depth

The 39-test suite is strong for deterministic core and terrain semantics, including generated Jolt triangle-body creation, but still lacks broad success-path integration for:

- real initialized Jolt overlap/hitscan with entity mapping;
- built NavMesh asynchronous path completion and mutation cancellation;
- GroupManager formation/flock output against live primitives;
- T8ditor save/reload/undo through a headless harness;
- extracted release package startup.

### Advanced Voxel Terrain

The Framework now supports a finite streamed mutable block world with greedy meshes, bounded jobs, voxel FPS collision, DDA, block edits, atomic sparse persistence, and four-backend rendering. Remaining advanced features are:

- named persisted block palettes and migration when registration order changes;
- production atlas assets, named tile metadata, mip-safe padding, and cutout/transparent reference materials;
- sunlight/emissive propagation, voxel ambient occlusion, water, and fluids;
- asynchronous/off-thread chunk collision cooking and incremental collision updates;
- voxel/grid NPC navigation or true tiled Detour integration;
- targeted changed-chunk plus boundary-neighbor remeshing instead of conservative all-loaded edit remeshing;
- device-local asynchronous D3D12/Vulkan mutable geometry uploads;
- LOD, occlusion culling, indirect draws, memory budgets, and floating origin;
- T8ditor voxel tools, inventory/crafting/drops, scripting, and multiplayer;
- Android and Steam Deck runtime performance evidence.

Implement these from measured project requirements. The current `VoxelScene` is the reference integration, not a complete game.

### Deferred Gameplay and Editor Capabilities

These suggestions from the pre-v1 assessments remain deliberately unimplemented:

- Fast in-memory Play; Fidelity Play remains authoritative and validates the serializer/loader.
- Gameplay config hot reload and general live component/event editing during Play. Runtime DevGui supports pause, inspection, recent events, and forced state transitions, but not arbitrary hot reload.
- A visual state-machine graph. The table editor is the supported v1 authoring path.
- Granular gameplay undo commands. Existing whole-scene snapshots provide undo/redo coverage.
- Cross-scene persistent entities, entity transfer, and gameplay-driven scene transitions. `GameLogicSystem` remains scene-owned.

These are product capabilities rather than missing P0-P14 foundations. Implement them from a concrete project requirement, not solely because they appeared in an old proposal.

### Root-Level Product Documentation

The root README is a concise project entry point, not a replacement for subsystem docs. New feature claims must link to an owning verified document and should not duplicate volatile implementation detail.

## Current Technical Limits

- `RenderQueue` is not the active mesh executor; `RenderMesh::Draw()` still owns the draw walk.
- D3D12/Vulkan PSOs depend on draw state and render-target formats, not just shader keys.
- Skinned bounds are conservative and not fully animation-aware.
- Recast builds whole meshes rather than streamed tiles.
- DetourCrowd is linked/detected but not the gameplay navigation path.
- Gameplay objects use stable `std::list` nodes and components use `unique_ptr`; profile before adding per-type arrays or an ECS/sparse set.
- Runtime JSON readers ignore unknown keys.
- Android packaged asset access differs from desktop filesystem access.
- Hosted editor windows require careful API resource/state isolation.
- Telemetry requires normal shutdown to flush.
- OpenGL voxel GBuffer albedo is byte-identical to D3D11 and normals are nearly identical, proving mutable mesh/atlas parity; deferred lighting produces a materially darker final image and needs renderer-level parity work before shared exact baselines.

## Troubleshooting Index

| Symptom/task | Start here |
|---|---|
| first build, MSBuild, outputs | [Windows build and run](development/windows-build-and-run.md) |
| launcher/config/CLI confusion | [Runtime configuration](development/runtime-configuration.md) |
| missing models/textures | [Cloud assets](development/cloud-assets.md) |
| Android setup/sign/install | [Android](platform/android.md) |
| Steam Deck setup/build/run/package | [Steam Deck](platform/steam-deck.md) |
| choose runtime scene owner | [Runtime hosts](runtime/runtime-hosts.md) |
| test or release gate | [Verification](testing/verification.md) |
| create/compare image dumps | [Visual regression](debug/visual-regression.md) |
| app/window/input/resize | [Platform event loop](architecture/platform-event-loop.md) |
| path/cache works on desktop only | [Resource locator](architecture/resource-locator.md) |
| mesh import/material | [Loading geometry](geometry/loading-geometry.md) |
| shader/permutation/PSO | [Shader management](rendering/shader-management.md) |
| pass/RT/post process | [Render graph](rendering/render-graph.md) |
| draw/binding/culling | [Geometry rendering](rendering/geometry-rendering-flow.md) |
| texture/sampler/IBL | [Textures and IBL](rendering/textures-and-ibl.md) |
| animation/skinning | [Animation](animation/animation-system.md) |
| body/cast/ragdoll/gameplay layer | [Jolt physics](physics/jolt-physics.md) |
| NavMesh/path/off-mesh links | [Navigation](navigation/navmesh-detour.md) |
| gameplay object/event/state/control | [Game entity system](game/game-entity-system-spec.md) |
| editor hierarchy/undo/Play | [Editor](editor/editor-overview.md) |
| `.t8scene` fields/runtime load | [Scene format](scenes/scene-format-and-runtime.md) |
| telemetry/profile/raw dump | [Diagnostics](debug/diagnostics.md) |

## Change Review Checklist

Before merging a cross-system change:

1. identify the owning document and source abstraction;
2. update both MSBuild and CMake for new Framework sources;
3. run the minimum gate from [Verification](testing/verification.md);
4. run a focused visual case when rendering can change;
5. keep `VisualBaselines/reference` unchanged unless a reviewed visual change requires rebaseline;
6. update known limits and commands in the same change;
7. run link audit and `git diff --check`;
8. report unrun platform gates explicitly.

## Related Documents

- [Documentation index](README.md)
- [Current status](current-status-and-roadmap.md)
- [Maintenance plan](stage-plan.md)
- [Dependency map](dependency-map.md)
