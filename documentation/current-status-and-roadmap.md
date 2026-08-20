# Current Status and Roadmap

Status: verified against source, scripts, builds, tests, editor smoke, and visual artifacts on 2026-08-19.

This is the source of truth for implementation maturity. Subsystem documents explain behavior; operational documents explain commands.

## Engine Status

| Area | State |
|---|---|
| D3D11, D3D12, OpenGL, Vulkan | Implemented peer backends on Windows |
| JSON render graph/deferred/PBR/post processing | Implemented |
| glTF/GLB, Draco, `.x`, mesh/material caches | Implemented |
| Animation/GPU bone texture/ragdolls | Implemented |
| Jolt physics/gameplay layers/queries | Implemented |
| Recast/Detour build/query/off-mesh links | Implemented |
| Gameplay v1 (P0-P14) | Implemented |
| Mutable voxel terrain reference | Implemented: chunks, atlas mesh, streaming, FPS and per-chunk Jolt collision, edits, persistence |
| T8ditor game authoring/validation/overlays | Implemented |
| Fidelity Play through temporary `.t8scene` | Implemented |
| Fast in-memory Play | Not implemented; optional future work |
| Gameplay hot reload/live Play editing | Not implemented; pause, inspection, and force-transition controls exist |
| Visual state-machine graph | Not implemented; table authoring is the supported v1 path |
| Granular gameplay authoring undo commands | Not implemented; whole-scene snapshot undo is supported |
| Cross-scene persistent gameplay entities | Not implemented; out of scope for v1 |
| 100/1,000-entity benchmark scenes | Not implemented; performance budgets remain unmeasured targets |
| Windows local build/run | Verified |
| Android and Steam Deck source/CI workflows | Implemented; local audit host lacked required toolchains |

## Final Verification Evidence

| Gate | Result |
|---|---|
| x64 Debug solution | Passed |
| x64 Release solution | Passed |
| Win32 Debug solution | Passed, including T8ditor |
| Win32 Release solution | Passed, including T8ditor |
| ARM64 Debug solution | Passed |
| ARM64 Release solution | Passed |
| Win32 and x64 Debug/Release gameplay/terrain self-tests | 39/39 passed in all four runnable cells |
| T8ditor Q3 Jolt smoke | 2 entities, 0 validation errors, 1 expected missing runtime-camera warning, 10 RT files, 0 engine errors |
| Existing visual regression | 26 comparable captures, 0 failures; 23 exact, 3 D3D11 within 1-2 channel levels |
| Voxel visual case | 4/4 APIs captured at 1280x720 after 5 seconds; 0 engine errors/invalid captures; D3D11/D3D12 byte-identical |
| Visual skips | 2 Q3 Vulkan hardware skips on 2 GiB GPU; 4 Nexus missing-asset skips |
| Gameplay telemetry | all 12 required `game.*` counters emitted on normal shutdown |
| Source registration | MSBuild, filters, desktop/Steam CMake, and Android CMake parity passed and CI-enforced |
| CMake x64 Debug | Configure plus DayScene/T8ditor target builds passed |
| Release launcher | Compiled and staged into all six Windows output directories |
| Documentation links/diff | passed |
| SteamRT local configure | environment-blocked before CMake: Podman unavailable |
| Android local Release | environment-blocked before Gradle/CMake: SDK/NDK unavailable |

The retained visual report is `T850/VisualBaselines/final-comparison.json`; the directory is intentionally ignored and should be archived externally/CI when needed.

## Gameplay v1

Implemented under `Framework/include/game`, `Framework/src/game`, physics additions, SceneTemplate, and T8ditor:

- schema v2, stable entity/component/group IDs, migration, validation;
- fixed-tick scene-owned `GameLogicSystem` with bounded catch-up and pause;
- registry, component factories/lifecycle/deferred mutation;
- queued EventBus and compiled StateMachine;
- Player/AI control and MovementIntent;
- gameplay Jolt layers, filtered casts/overlap, physics command facade;
- async/batched navigation facade with NavMesh mutation barrier;
- path following and existing nav-agent authoring fields;
- groups, formation/flock targets, RTS commands, health and weapon examples;
- SceneTemplate loading, DevGui state/events/pause, telemetry;
- T8ditor entity/group inspectors, validation, overlays, whole-scene undo, Play validation;
- `--game-selftest` coverage.

The implementation prompts are completed acceptance contracts, not a current todo list.

## Mutable Voxel Terrain

Implemented under `Framework/include/terrain`, `Framework/src/terrain`, mutable scene rendering, and `DayScene/VoxelScene`:

- stable gameplay object addresses and generational/reusable physics handles;
- validated mutable mesh snapshots and cross-backend procedural rendering;
- fence-safe D3D12/Vulkan replacement-buffer retirement;
- stable generational render-instance add/remove;
- block registry, dense chunks, negative-safe world coordinates, and DDA targeting;
- neighbor-aware greedy meshing, atlas UVs, material sections, and a shared nearest-filtered block atlas;
- prioritized bounded worker streaming with cancellation/epochs and main-thread commits;
- grounded FPS voxel collision, one generated Jolt triangle body per active chunk, and place/remove interaction;
- versioned sparse `.t8vox` edits with hashed payload and atomic replacement;
- no-asset `voxel-streaming` visual case across all four APIs;
- unattended CDB assertion/crash workflow.

See [Mutable voxel terrain](terrain/voxel-terrain.md).

## Build and Release State

- Windows primary build: Visual Studio 2022/MSBuild/v143.
- Android and Steam Deck: CMake through their platform wrappers.
- GitHub Actions: source-registration gate; full-solution Windows Win32/x64/ARM64 Debug+Release with Win32/x64 self-tests; Android arm64-v8a/x86_64 Release; SteamRT Release/package.
- `v*` tags: package Windows ZIPs, Android APKs, Steam Deck tarball, and launcher into a GitHub Release.

Use:

- [Windows build and run](development/windows-build-and-run.md)
- [Android build and deployment](platform/android.md)
- [Steam Deck build and deployment](platform/steam-deck.md)
- [Verification gates](testing/verification.md)

## Documentation State

All subsystem and operational guides were source-audited on 2026-08-19. The old staged authoring checklist was replaced by [Documentation maintenance plan](stage-plan.md). Superseded documentation was removed; use Git history when historical context is required.

## Remaining Engineering Work

Highest-value next work:

1. run and record Android/Steam Deck local gates on equipped hosts;
2. automate Markdown-link validation in CI;
3. create reviewed 100/1,000-entity Release benchmark scenes and measure budgets;
4. expand headless success-path physics/navigation tests;
5. add release-package extraction/install smoke tests;
6. add render-graph resource-lifetime validation and shader/cache operations;
7. improve gameplay examples/editor ergonomics based on actual projects;
8. add hot reload, cross-scene persistence, or granular editor commands only when a project requires them.
9. profile the streamed voxel reference on Android/Steam Deck and add asynchronous chunk collision cooking or voxel NPC navigation only for projects that require them.

Do not begin a broad renderer abstraction or ECS rewrite without profiling evidence.

## Known Operational Limits

- Heavy Q3 Vulkan scenes need at least 4 GiB VRAM in the visual harness; the audit GPU has 2 GiB.
- Nexus visual cases need two models not present in the public runtime manifest.
- Runtime telemetry flushes on normal framework shutdown; direct dump `exit()` paths can lose telemetry.
- T8ditor launcher UI maps editor launches to D3D12/Vulkan on x64 and ARM64, and D3D11/Vulkan on Win32; direct T8ditor CLI accepts all four Windows APIs.
- Fast Android APK repack uses a debug keystore and is not a production release path.
- SteamRT official build requires Podman.
- `.t8scene` and config JSON ignore unknown keys, so explicit migration/validation and spelling discipline remain required.
- Voxel reference uses a generated shared atlas, voxel-grid player collision, static Jolt chunk bodies, and sparse edit persistence; production atlas assets, block lighting, asynchronous collision cooking, and voxel NPC navigation remain future work.

## Roadmap

See [Documentation maintenance plan](stage-plan.md) for ongoing documentation/CI/platform/performance work. Feature-specific plans should live beside their owning subsystem, not accumulate here.

## Related Documents

- [Documentation index](README.md)
- [Review and gaps](review-and-gaps.md)
- [Dependency map](dependency-map.md)
- [Game entity system](game/game-entity-system-spec.md)
- [Visual regression](debug/visual-regression.md)
