# T850 Engine Documentation

Status: full documentation and skill audit completed on 2026-09-26.

This tree documents the current T850 rendering/game engine, runtime hosts, editor, build/deploy workflows, diagnostics, and acceptance gates. Superseded documents are removed and remain available through Git history.

## Start Here

| Need | Read |
|---|---|
| learn terrain and RTS blockout authoring step by step | [Editor tutorials](tutorials/README.md) |
| current implementation and verified gates | [Current status](current-status-and-roadmap.md) |
| first Windows setup/build/run | [Windows setup, build, and run](development/windows-build-and-run.md) |
| CLI/config fields | [Runtime configuration](development/runtime-configuration.md) |
| choose Sandbox/Day/Q3/Ragdoll/SceneTemplate/VoxelScene/Minecraft | [Runtime hosts](runtime/runtime-hosts.md) |
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
| Windows setup/build/run/release packaging | [development/windows-build-and-run.md](development/windows-build-and-run.md) | Five-runtime-API scope refreshed 2026-09-26 |
| Runtime JSON and CLI | [development/runtime-configuration.md](development/runtime-configuration.md) | Strict config/recovery fields verified 2026-09-26 |
| Cloud models/textures | [development/cloud-assets.md](development/cloud-assets.md) | Verified 2026-08-19 |
| Verification, CI, self-tests, smoke gates | [testing/verification.md](testing/verification.md) | Current 78-test and release-package gates verified 2026-09-26 |
| Raw dumps and visual baselines | [debug/visual-regression.md](debug/visual-regression.md) | Verified 2026-08-19 |
| Android | [platform/android.md](platform/android.md) | Build/package contract audited 2026-09-26; device runtime still required |
| Steam Deck | [platform/steam-deck.md](platform/steam-deck.md) | Package contract audited 2026-09-26; device runtime still required |
| Browser/WebGPU deployment | [platform/browser.md](platform/browser.md) | Current packaged browser/runtime state and historical releases |
| Runtime host selection | [runtime/runtime-hosts.md](runtime/runtime-hosts.md) | Seven hosts verified 2026-09-26 |

## Architecture and Shared Systems

| Area | Document | Status |
|---|---|---|
| Main architecture and ownership | [architecture/main-architecture.md](architecture/main-architecture.md) | Render-thread ownership audited 2026-09-26 |
| Platform event loops/windows | [architecture/platform-event-loop.md](architecture/platform-event-loop.md) | Bounded WebGPU recovery audited 2026-09-26 |
| Resource lookup/cache paths | [architecture/resource-locator.md](architecture/resource-locator.md) | Compute-manifest packaging audited 2026-09-26 |
| Input/controllers/camera profiles | [input/camera-and-controls.md](input/camera-and-controls.md) | Verified 2026-08-19 |
| FrameworkImGui/runtime UI | [editor/imgui-system.md](editor/imgui-system.md) | Verified 2026-08-30 |
| Diagnostics/telemetry/profiler/dumps | [debug/diagnostics.md](debug/diagnostics.md) | Recovery/cache diagnostics audited 2026-09-26 |
| Cross-system dependencies | [dependency-map.md](dependency-map.md) | Compute and animation flows refreshed 2026-09-26 |

## Rendering and Assets

| Area | Document | Status |
|---|---|---|
| Geometry/glTF/.x loading | [geometry/loading-geometry.md](geometry/loading-geometry.md) | Animation ownership refreshed 2026-09-26 |
| Shader keys/cache/reflection/PSOs | [rendering/shader-management.md](rendering/shader-management.md) | Program-cache affinity and compute manifest refreshed 2026-09-26 |
| Current WebGPU architecture and support boundary | [rendering/proposal-webgpu.md](rendering/proposal-webgpu.md) | Bounded recovery status refreshed 2026-09-26 |
| WebGPU implementation/runtime handoff | [rendering/webgpu-runtime-summary.md](rendering/webgpu-runtime-summary.md) | Runtime, compute, profiling and recovery state refreshed 2026-09-26 |
| WebGPU port lessons | [rendering/webgpu-port-lessons.html](rendering/webgpu-port-lessons.html) | Narrative architecture/performance reference refreshed 2026-09-26 |
| Proposed Dawn D3D12 native pipeline persistence | [rendering/dawn-d3d12-native-pipeline-cache-proposal.md](rendering/dawn-d3d12-native-pipeline-cache-proposal.md) | PipelineLibrary/ShaderCacheSession design verified against Dawn and T850 source 2026-09-28 |
| Future WebGPU platform ports | [rendering/webgpu-platform-gaps.md](rendering/webgpu-platform-gaps.md) | Windows remains Dawn/D3D12; Android/Deck remain native Vulkan |
| Cross-backend compute shaders, manifest registry and 3D dispatch | [rendering/compute-shader-implementation.md](rendering/compute-shader-implementation.md) | Implementation verified 2026-09-26; multi-slice hardware gate pending |
| Reproducible Windows GPU/ETW profiling workflow | [rendering/gpu-performance-profiling-workflow.md](rendering/gpu-performance-profiling-workflow.md) | Verified x64/ARM64 2026-09-22 |
| Opt-in D3D12/Vulkan/WebGPU GPU timestamp profiling | [rendering/gpu-timestamp-profiling.md](rendering/gpu-timestamp-profiling.md) | Whole-frame and pass-level implementation verified 2026-09-22 |
| WebGPU/compute/browser remediation backlog | [rendering/webgpu-compute-remediation-plan.md](rendering/webgpu-compute-remediation-plan.md) | R18 complete; R19 implementation complete with hardware gate pending; revised 2026-09-26 |
| JSON render graph | [rendering/render-graph.md](rendering/render-graph.md) | Attachment lifetime/initialization verified 2026-09-26 |
| Cascaded shadow maps | [rendering/cascaded-shadow-maps.md](rendering/cascaded-shadow-maps.md) | Five-runtime-API scope refreshed 2026-09-26 |
| Mesh draw path/state tracking | [rendering/geometry-rendering-flow.md](rendering/geometry-rendering-flow.md) | Skinned pre-pass ownership refreshed 2026-09-26 |
| Textures/samplers/IBL/material slots | [rendering/textures-and-ibl.md](rendering/textures-and-ibl.md) | Verified 2026-08-30 |
| Animation/skinning/bone textures | [animation/animation-system.md](animation/animation-system.md) | Graph pre-pass upload verified 2026-09-26 |

## Simulation, Gameplay, Editor, and Scenes

| Area | Document | Status |
|---|---|---|
| Jolt physics/gameplay layers/ragdolls | [physics/jolt-physics.md](physics/jolt-physics.md) | Verified 2026-08-19 |
| Recast/Detour/game navigation | [navigation/navmesh-detour.md](navigation/navmesh-detour.md) | Request cancellation verified 2026-09-26 |
| Game entities/components/control/events | [game/game-entity-system-spec.md](game/game-entity-system-spec.md) | Implemented v1; strict schema/cancellation refreshed 2026-09-26 |
| Mutable voxel terrain/chunk streaming | [terrain/voxel-terrain.md](terrain/voxel-terrain.md) | Implemented reference, verified 2026-08-30 |
| Authored image-based terrain | [terrain/heightmap-terrain.md](terrain/heightmap-terrain.md) | Implemented, verified 2026-09-07 |
| Square-grid placement and colored building blockouts | [terrain/placement-grid.md](terrain/placement-grid.md) | Implemented, verified 2026-09-07 |
| Tagged gameplay regions | [scenes/scene-regions.md](scenes/scene-regions.md) | Implemented, verified 2026-09-07 |
| P0-P14 maintenance contracts | [game/game-entity-system-implementation-prompts.md](game/game-entity-system-implementation-prompts.md) | Executed; reference only |
| T8ditor | [editor/editor-overview.md](editor/editor-overview.md) | Strict scene loading refreshed 2026-09-26; WebGPU intentionally unsupported |
| Embeddable editor/static extensions | [editor/editor-sdk.md](editor/editor-sdk.md) | First implementation, 2026-09-09 |
| Editor/runtime architecture assessment | [editor/architecture-review.md](editor/architecture-review.md) | Review and first extraction, 2026-09-07 |
| `.t8scene` and runtime loading | [scenes/scene-format-and-runtime.md](scenes/scene-format-and-runtime.md) | Strict owned-schema behavior verified 2026-09-26 |
| SceneDescriptor/SceneSetup | [scenes/scene-setup-descriptors.md](scenes/scene-setup-descriptors.md) | Strict descriptor behavior verified 2026-09-26 |

## Governance

| Document | Purpose |
|---|---|
| [current-status-and-roadmap.md](current-status-and-roadmap.md) | implemented state, verification evidence, open work |
| [doc-conventions.md](doc-conventions.md) | writing/freshness requirements |
| [documentation-audit-2026-09-26.md](documentation-audit-2026-09-26.md) | full documentation/skill consistency audit for the current hardening changes |
| [glossary.md](glossary.md) | engine terminology |

## Historical Review Records

These dated documents preserve review context and are not the current status authority:

- [WebGPU branch ownership audit](architecture/webgpu-branch-ownership-audit.md)
- [Compute shader branch review](rendering/compute-shader-branch-review.md)
- [Compute flow assessment, 2026-09-17](rendering/compute-flow-assessment-2026-09-17.md)

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
