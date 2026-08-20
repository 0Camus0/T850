# Documentation Maintenance Plan

Status: verified and reset for ongoing maintenance on 2026-08-19.

The original Stages 0-24 created the subsystem documentation and implemented the game entity plan. Those stages are complete. This file now tracks current maintenance work instead of preserving an obsolete implementation checklist.

## Verified Baseline

The current documentation set covers:

- architecture, platform loops, resources, input, and runtime hosts;
- geometry, shaders, render graph, geometry rendering, textures, and IBL;
- animation, Jolt physics, Recast/Detour navigation, and gameplay;
- T8ditor, FrameworkImGui, `.t8scene`, and SceneSetup;
- Windows setup/build/run and runtime configuration;
- cloud asset acquisition;
- Android build/sign/install/deploy;
- Steam Deck build/run/package/deploy;
- diagnostics, raw frame dumps, visual regression, tests, and release gates.

All subsystem and operational guides were source-audited on 2026-08-19. Relative links and build-file registration passed their documented audits.

## Current Verification Evidence

| Gate | State |
|---|---|
| x64 Debug and Release | passed |
| ARM64 Debug and Release | passed |
| Gameplay/terrain Debug and Release self-tests | 39/39 passed |
| T8ditor authored Q3 smoke | passed with 0 errors and 1 expected runtime-camera warning |
| Visual matrix | 26 reference-comparable captures plus 4 voxel candidate captures, 0 failures, 6 documented skips |
| Build registration | all 28 new Framework/DayScene C++ sources registered in MSBuild, filters, and CMake |
| Documentation links | passed |
| SteamRT local configure | environment-blocked by missing Podman on the audit host |
| Android local Release | environment-blocked by missing Android SDK/NDK on the audit host |

CI separately defines Windows, Android, Steam Deck, and tagged-release jobs in `.github/workflows/build.yml`.

## Maintenance Rule

Every behavior change must update the owning document in the same change when it affects:

- public/runtime API;
- ownership or lifecycle;
- frame/tick phase ordering;
- scene or configuration schema;
- command-line flags;
- build, package, deploy, or signing commands;
- asset paths/manifests;
- test, dump, comparison, or acceptance behavior;
- known limits and supported platforms.

Remove superseded documentation instead of maintaining parallel historical copies. Git history preserves prior versions.

## Required Freshness Gate

Before declaring documentation current:

1. inspect the owning source and nearest call site;
2. run the command shown in the document when the host supports it;
3. check every relative Markdown link;
4. run `git diff --check`;
5. classify unrun platform commands as environment/toolchain gaps, not passes;
6. update the verification date only after those checks.

The executable commands are in [Verification and release gates](testing/verification.md).

## Ongoing Workstreams

### 1. CI Enforcement

Goal: turn manual freshness checks into repeatable CI failures.

Planned work:

- add a repository script for Markdown link validation;
- retain visual manifests/reports as CI artifacts on GPU-equipped runners;
- report configured versus skipped platform gates explicitly.

Implemented on 2026-08-19:

- `ValidateBuildRegistration.ps1` enforces MSBuild/filter/desktop-CMake/Android-CMake parity before platform jobs;
- `RunWindowsBuildMatrix.ps1` mirrors all six Windows CI build cells locally;
- CI builds the full solution for Win32/x64/ARM64 and runs Win32/x64 Debug/Release self-tests.

### 2. Equipped-Host Platform Revalidation

Goal: close local environment gaps without changing source merely to satisfy unavailable tools.

Required hosts:

- Podman-capable Linux/Steam Deck host for SteamRT Release build/package/run;
- Windows Android host with SDK 35, NDK 27.2.12479018, JDK 17, Vulkan SDK, and device/emulator.

On those hosts, execute the commands in:

- [Android build and deployment](platform/android.md)
- [Steam Deck build and deployment](platform/steam-deck.md)

Record exact tool versions, outputs, and artifact hashes.

### 3. Gameplay Performance Characterization

Goal: measure rather than assume the v1 gameplay budgets.

Planned work:

- create reviewed 100-entity and 1,000-entity benchmark scenes;
- capture `game.update` and component/nav/physics counters in Release;
- document hardware, scene composition, warmup, and sample distribution;
- change storage strategy only if profiling demonstrates a bottleneck.

Fast in-memory Play remains optional; serializer-backed Fidelity Play is the implemented path.

### 4. Runtime and Release Hardening

Potential work:

- render-graph resource lifetime validation;
- shader permutation/cache operations and invalidation tooling;
- richer gameplay component validation and examples;
- broader headless tests for physics/navigation success paths;
- automated release-package smoke tests after extraction/install.

These are targeted improvements, not a mandate for a renderer or ECS rewrite.

## Documentation Ownership Map

| Change area | Update first |
|---|---|
| setup, MSBuild, launcher, Windows output | [Windows build and run](development/windows-build-and-run.md) |
| config or CLI | [Runtime configuration](development/runtime-configuration.md) |
| cloud manifests/downloads | [Cloud assets](development/cloud-assets.md) |
| Android | [Android build and deployment](platform/android.md) |
| Steam Deck/Linux | [Steam Deck build and deployment](platform/steam-deck.md) |
| runtime scene choice | [Runtime hosts](runtime/runtime-hosts.md) |
| tests/CI/release gates | [Verification](testing/verification.md) |
| frame/image dumps and comparisons | [Visual regression](debug/visual-regression.md) |
| gameplay | [Game entity system](game/game-entity-system-spec.md) |
| subsystem internals | owning document linked from [README](README.md) |

## Completion Definition

Documentation is current when:

- indexed current docs have a source-verification date;
- commands match current script parameters;
- examples use valid working directories and resource-relative paths;
- implemented, optional, planned, skipped, and environment-blocked states are distinct;
- a small local model can choose a workflow, execute it, recognize success, and stop safely without reconstructing source behavior.

## Related Documents

- [Documentation index](README.md)
- [Current status](current-status-and-roadmap.md)
- [Documentation conventions](doc-conventions.md)
- [Review and remaining gaps](review-and-gaps.md)
- [T850 engine skill](../.github/skills/t850-engine/SKILL.md)
