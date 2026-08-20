---
name: t850-engine
description: "Use when working on T850 engine code, gameplay, rendering, scenes, T8ditor, assets, documentation, debugging, or when deciding which T850 build/deploy/visual workflow to run."
argument-hint: "Describe the subsystem, bug, feature, scene, backend, or workflow."
---

# T850 Engine Router

Use this skill to choose the smallest authoritative context and gate. Do not load the entire repository documentation.

## Roots

First determine roots from Git, not from the current terminal directory:

```powershell
$RepoRoot = git rev-parse --show-toplevel
$SourceRoot = Join-Path $RepoRoot 'T850'
```

Repository root contains `documentation/` and `LaunchSolution.bat`. Source root contains `T850.sln`, `Framework/`, `DayScene/`, `T8ditor/`, `Assets/`, and `scripts/`.

## Route the Task

| Task | Load/use |
|---|---|
| build, run, test, config, assets | `t850-build-run` skill |
| native crash, assert, call stack, CDB, dump analysis | `t850-crash-debugging` skill |
| voxel terrain, mutable mesh, chunks, streaming, block edits | `t850-voxel-terrain` skill |
| screenshots, RT dumps, replay, visual baselines, image comparison | `t850-visual-regression` skill |
| Android, Steam Deck, install, deploy, package, release | `t850-platform-deploy` skill |
| current implementation status | `documentation/current-status-and-roadmap.md` |
| subsystem owner/dependencies | `documentation/dependency-map.md` then one owning document |
| runtime scene choice | `documentation/runtime/runtime-hosts.md` |
| documentation maintenance | `documentation/stage-plan.md` and `doc-conventions.md` |

Use only the indexed documentation tree; superseded documents belong in Git history. Implementation prompts are completed maintenance contracts, not current status.

## Subsystem Routing

| Area | Read first | Source owner |
|---|---|---|
| platform/window loop | `architecture/platform-event-loop.md` | `Framework/src/core/` |
| architecture/lifecycle | `architecture/main-architecture.md` | Framework core + app/scene hosts |
| resource/cache paths | `architecture/resource-locator.md` | `ResourceLocator.*` |
| input/cameras | `input/camera-and-controls.md` | platform input + `CameraProfiles.*` |
| geometry/glTF | `geometry/loading-geometry.md` | `Framework/src/utils/gltf/`, mesh creation |
| shaders/PSOs | `rendering/shader-management.md` | descriptors, shader/cache/backend classes |
| render passes/RTs | `rendering/render-graph.md` | `RenderGraph.*`, JSON graphs |
| draw path | `rendering/geometry-rendering-flow.md` | `PrimitiveInst`, mesh/skinned draw |
| textures/IBL | `rendering/textures-and-ibl.md` | textures, `IBLResources`, material slots |
| animation | `animation/animation-system.md` | `AnimationController`, `RenderSkinnedMesh` |
| physics | `physics/jolt-physics.md` | `Framework/*/physics/` |
| navigation | `navigation/navmesh-detour.md` | `Framework/*/navigation/` |
| gameplay | `game/game-entity-system-spec.md` | `Framework/*/game/`, SceneTemplate |
| voxel terrain | `terrain/voxel-terrain.md` | `Framework/*/terrain/`, MutableMesh, VoxelScene |
| editor | `editor/editor-overview.md` | `T8ditor/` |
| runtime ImGui | `editor/imgui-system.md` | `FrameworkImGui/` |
| scene schema/load | `scenes/scene-format-and-runtime.md` | `EditorSceneFile`, SceneTemplate |
| legacy runtime descriptors | `scenes/scene-setup-descriptors.md` | `SceneDescriptor`, `SceneSetup` |
| diagnostics | `debug/diagnostics.md` | `Framework/*/debug/` |

## Local Change Procedure

1. Start from one file, symbol, failing command, scene, backend, or visible behavior.
2. Read the owning doc and one nearby call site/test.
3. State one falsifiable hypothesis and one cheap check.
4. Make the smallest edit.
5. Run the focused check immediately.
6. Broaden builds/tests according to blast radius.
7. Update the owning doc when behavior, API, schema, command, output, or limitation changed.

## Non-Negotiable Architecture Rules

- Windows primary build is MSBuild/v143; keep CMake in parity for Android/Steam Deck.
- Every new Framework `.cpp`: add to `Framework.vcxproj`, filters, and `Framework/CMakeLists.txt`.
- Use `ResourceLocator` for portable runtime resources.
- Render-pass order belongs in render-graph JSON.
- Treat D3D11, D3D12, OpenGL, and Vulkan as peer backends.
- Keep Framework independent of T8ditor/editor-only UI.
- Keep gameplay core independent of `game/examples`, T8ditor, and ImGui.
- Preserve fixed-tick phase ordering, deferred mutation, stable IDs, validation, and service boundaries.
- DetourCrowd is not the gameplay navigation path.
- Match the existing lifecycle hook spelling `OnDestoryScene`.
- Unknown JSON keys are ignored; schema edits require migration and validation.
- Never loosen/rebaseline visual acceptance merely to hide a failure.

## Validation Routing

| Change | Gate |
|---|---|
| local implementation | focused test/compile |
| Framework/game/schema/physics/nav | x64 Debug + ARM64 Debug + self-test when applicable |
| renderer/shared scene | Release + focused visual comparison |
| final/release | x64/ARM64 Debug+Release + Release self-test + full visual + platforms |
| docs | relative-link audit + `git diff --check` |

Exact commands live in the focused skills and `documentation/testing/verification.md`.

## Completion Report

State:

- behavior/files changed;
- build metadata added;
- commands run and result;
- visual cases/tolerance/skips;
- docs updated;
- unrun or environment-blocked platforms;
- remaining risks.

Do not claim an unrun gate passed. Do not classify an environment prerequisite as a source failure.
