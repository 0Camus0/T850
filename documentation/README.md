# T850 Engine Documentation

Status: index verified on 2026-08-30.

This tree documents the current T850 rendering/game engine, runtime hosts, editor, build/deploy workflows, diagnostics, and acceptance gates. Superseded documents are removed and remain available through Git history.

## Start Here

| Need | Read |
|---|---|
| current implementation and verified gates | [Current status](current-status-and-roadmap.md) |
| first Windows setup/build/run | [Windows setup, build, and run](development/windows-build-and-run.md) |
| CLI/config fields | [Runtime configuration](development/runtime-configuration.md) |
| choose Sandbox/Day/Q3/Ragdoll/SceneTemplate/VoxelScene | [Runtime hosts](runtime/runtime-hosts.md) |
| tests and merge/release gates | [Verification](testing/verification.md) |
| make or compare image dumps | [Visual regression](debug/visual-regression.md) |
| Android build/install/deploy | [Android](platform/android.md) |
| Steam Deck build/run/package/deploy | [Steam Deck](platform/steam-deck.md) |
| find a subsystem owner | [Dependency map](dependency-map.md) |
| local-agent workflow | [T850 engine router](../.github/skills/t850-engine/SKILL.md) |
| build/run/test skill | [T850 build/run skill](../.github/skills/t850-build-run/SKILL.md) |
| native crash/CDB skill | [T850 crash-debugging skill](../.github/skills/t850-crash-debugging/SKILL.md) |
| voxel terrain/streaming skill | [T850 voxel-terrain skill](../.github/skills/t850-voxel-terrain/SKILL.md) |
| image dump/compare skill | [T850 visual regression skill](../.github/skills/t850-visual-regression/SKILL.md) |
| platform deploy/package skill | [T850 platform deploy skill](../.github/skills/t850-platform-deploy/SKILL.md) |

## Operations

| Area | Document | Status |
|---|---|---|
| Windows setup/build/run/release packaging | [development/windows-build-and-run.md](development/windows-build-and-run.md) | Verified 2026-08-19 |
| Runtime JSON and CLI | [development/runtime-configuration.md](development/runtime-configuration.md) | Verified 2026-08-19 |
| Cloud models/textures | [development/cloud-assets.md](development/cloud-assets.md) | Verified 2026-08-19 |
| Verification, CI, self-tests, smoke gates | [testing/verification.md](testing/verification.md) | Verified 2026-08-30 |
| Raw dumps and visual baselines | [debug/visual-regression.md](debug/visual-regression.md) | Verified 2026-08-19 |
| Android | [platform/android.md](platform/android.md) | Verified 2026-08-30 |
| Steam Deck | [platform/steam-deck.md](platform/steam-deck.md) | Verified 2026-08-19 |
| Runtime host selection | [runtime/runtime-hosts.md](runtime/runtime-hosts.md) | Verified 2026-08-19 |

## Architecture and Shared Systems

| Area | Document | Status |
|---|---|---|
| Main architecture and ownership | [architecture/main-architecture.md](architecture/main-architecture.md) | Verified 2026-08-30 |
| Platform event loops/windows | [architecture/platform-event-loop.md](architecture/platform-event-loop.md) | Verified 2026-08-19 |
| Resource lookup/cache paths | [architecture/resource-locator.md](architecture/resource-locator.md) | Verified 2026-08-19 |
| Input/controllers/camera profiles | [input/camera-and-controls.md](input/camera-and-controls.md) | Verified 2026-08-19 |
| FrameworkImGui/runtime UI | [editor/imgui-system.md](editor/imgui-system.md) | Verified 2026-08-30 |
| Diagnostics/telemetry/profiler/dumps | [debug/diagnostics.md](debug/diagnostics.md) | Verified 2026-08-30 |
| Cross-system dependencies | [dependency-map.md](dependency-map.md) | Verified 2026-08-30 |

## Rendering and Assets

| Area | Document | Status |
|---|---|---|
| Geometry/glTF/.x loading | [geometry/loading-geometry.md](geometry/loading-geometry.md) | Verified 2026-08-19 |
| Shader keys/cache/reflection/PSOs | [rendering/shader-management.md](rendering/shader-management.md) | Verified 2026-08-19 |
| JSON render graph | [rendering/render-graph.md](rendering/render-graph.md) | Verified 2026-08-19 |
| Mesh draw path/state tracking | [rendering/geometry-rendering-flow.md](rendering/geometry-rendering-flow.md) | Verified 2026-08-19 |
| Textures/samplers/IBL/material slots | [rendering/textures-and-ibl.md](rendering/textures-and-ibl.md) | Verified 2026-08-30 |
| Animation/skinning/bone textures | [animation/animation-system.md](animation/animation-system.md) | Verified 2026-08-19 |

## Simulation, Gameplay, Editor, and Scenes

| Area | Document | Status |
|---|---|---|
| Jolt physics/gameplay layers/ragdolls | [physics/jolt-physics.md](physics/jolt-physics.md) | Verified 2026-08-19 |
| Recast/Detour/game navigation | [navigation/navmesh-detour.md](navigation/navmesh-detour.md) | Verified 2026-08-19 |
| Game entities/components/control/events | [game/game-entity-system-spec.md](game/game-entity-system-spec.md) | Implemented v1, verified 2026-08-19 |
| Mutable voxel terrain/chunk streaming | [terrain/voxel-terrain.md](terrain/voxel-terrain.md) | Implemented reference, verified 2026-08-30 |
| P0-P14 maintenance contracts | [game/game-entity-system-implementation-prompts.md](game/game-entity-system-implementation-prompts.md) | Executed; reference only |
| T8ditor | [editor/editor-overview.md](editor/editor-overview.md) | Verified 2026-08-19 |
| `.t8scene` and runtime loading | [scenes/scene-format-and-runtime.md](scenes/scene-format-and-runtime.md) | Verified 2026-08-30 |
| SceneDescriptor/SceneSetup | [scenes/scene-setup-descriptors.md](scenes/scene-setup-descriptors.md) | Verified 2026-08-19 |

## Governance

| Document | Purpose |
|---|---|
| [current-status-and-roadmap.md](current-status-and-roadmap.md) | implemented state, verification evidence, open work |
| [doc-conventions.md](doc-conventions.md) | writing/freshness requirements |
| [glossary.md](glossary.md) | engine terminology |

## Small-Model Reading Rule

Do not load every document. Use this sequence:

1. read [Current status](current-status-and-roadmap.md);
2. read one operational guide or one subsystem owner document;
3. inspect the exact script/function named there;
4. execute the smallest listed gate;
5. update the owning document if behavior changed.

Do not infer current status from implementation prompts; they are completed maintenance contracts.

## Documentation Contract

A current guide must state:

- working directory;
- exact command and prerequisites;
- expected output/artifact;
- success and failure conditions;
- ownership/lifetime or phase when relevant;
- what is implemented versus optional/planned;
- related documents.

See [Documentation conventions](doc-conventions.md). Current implementation, verified gates, and remaining work are maintained in [Current status](current-status-and-roadmap.md).
