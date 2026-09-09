# Embeddable Editor and Static Extensions

Status: first implementation, 2026-09-09. This is a source-level static SDK, not
a stable binary plugin ABI or the complete editor/runtime world extraction.

## Ownership

`T8ditorCore` contains the existing editor and its legacy default scene hosts.
Stock `T8ditor` is now a small executable that calls `RunEditor`. A private project
can link the same library and supply its own registrations without copying editor
sources. Framework remains independent of editor headers and ImGui callbacks.

Public API: [EditorHost.h](../../T850/T8ditor/include/t8ditor/EditorHost.h).
Working consumer: [EditorExtension](../../examples/EditorExtension/main.cpp).

```cpp
#include <t8ditor/EditorHost.h>

int main(int argc, char** argv) {
  t8ditor::EditorHostDesc host;
  host.title = "Project Editor";
  host.projectRoot = ".";
  host.cacheRoot = "Cache";
  host.startupScene = "Scenes/Map.t8scene";
  host.requireKnownComponentsForPlay = true;
  host.registerRuntime = RegisterProjectComponents;
  host.registerEditor = RegisterProjectTools;
  return t8ditor::RunEditor(argc, argv, std::move(host));
}
```

`RunEditor` owns application/platform setup, command-line parsing, logging, and
shutdown. Use it once per process; concurrent or sequential editor sessions in
one process are not supported because legacy engine/editor globals remain.
CLI scene selection overrides `startupScene`. Empty host paths preserve stock
lookup behavior. A nonempty project root becomes the working directory and the
ResourceLocator base path; relative cache paths are resolved after that change.
Provide the project's complete resource tree, including engine shaders/defaults
it uses. There is no project/engine mount-overlay resolver in this slice.

## Registrations and Lifetime

- `registerRuntime` receives a registry containing movement and the existing
  health/path-follow/weapon examples. `Register` returns false for empty names,
  null factories, or duplicate names and never replaces an existing factory.
  Check the return value and report registration failures. `Types()` is sorted.
- `registerEditor` receives `EditorRegistry`. Register namespaced panels, commands,
  component inspectors, and scene validators. Invalid/duplicate registration
  throws `std::invalid_argument`; duplicates are scoped to each registry category.
- Panel callbacks run inside an editor-owned ImGui window. Commands appear in
  the Extensions menu; panels can be reopened there. Do not call `Begin`/`End`
  for the panel's own window. Additional nested tools must balance ImGui stacks.
- Inspectors receive a component descriptor copy. Return true when edited. The
  editor queues the accepted change; callbacks must preserve component identity.
- Validators append issues with entity/component IDs to the shared report.
  They run for scene validation, proposed gameplay edits, and before Play.
- Registrations are made at startup and remain alive until editor shutdown.
  Captured objects must outlive the run. No registration removal, DLL unload,
  hot reload, worker-thread editor callbacks, or raw GPU access is supported.

The component picker enumerates registered implementations, rather than offering
placeholder names that cannot execute. Existing unknown descriptors remain editable
through the generic fields and are preserved on save. The legacy no-registry
validation overload retains its historical recognized-name list for compatibility;
editor and GameLogicSystem validation use the actual registry.

## Gameplay Edit Transactions

`EditorContext::ReadScene()` returns an owned snapshot with a revision token.
`SubmitGameplayEdit()` queues replacement gameplay entities, groups, and settings.
Set `expectedRevision` from that snapshot and supply an undo label. Do not retain
references to callback arguments or use editor object vector indices as identities.

Application happens at the next update boundary, outside ImGui drawing. The host:

1. rejects stale tokens, unnamed edits, and edits while a hosted window owns play;
2. validates the candidate before touching authoring state;
3. applies gameplay descriptors and preserves selected gameplay identity;
4. records one whole-scene undo entry and updates the revision/status.

The token tracks gameplay content and document identity, not camera/render state.
Explicit reload and undo restoration invalidate prior tokens. Multiple edits from
one revision do not silently overwrite one another: after the first changes the
document, later requests are stale. Read again and resubmit deliberately. Inspect
`LastEditStatus()` and the validation panel for results.

This API does not edit terrain, meshes, cameras, or external definition files.
Those still use built-in editor tools. Whole-scene undo retains its existing cost;
continuous sliders are not coalesced into a single gesture transaction yet.
There is no new atomic multi-file save or dirty-document service in this slice.

## Runtime and Default Play

Hosted Play copies the editor's component registry into a fresh `SceneTemplate`
before gameplay loading. `requireKnownComponentsForPlay` makes unavailable enabled
component types errors, while authoring keeps them as warnings. Disabled unknown
components may remain inert. Failed gameplay initialization is now a Play failure,
not a successful session containing silently missing behavior.

For a standalone runtime, use the same registration function with
`GameLogicSystem::Factories()` after `Initialize()` and before `LoadFromScene()`.
Initialization resets the registry to built-ins; register the host's types again
on each initialization rather than relying on factories from a previous session.
The new final `requireKnownTypes` argument defaults to false for compatibility.
A host using `SceneTemplate` directly can supply `SetComponentFactories()` and
check `GameLogicReady()`. Editor-only validators do not automatically run in a
standalone game: put domain validation in a shared private function and invoke it
from both hosts before loading.

Component `params`/`config_json` require no new scene fields. Version, parse, and
validate project data explicitly. Arbitrary unknown root JSON fields are not an
extension storage contract and may be discarded by serialization.

## Build from Another Repository

Prerequisites: VS 2022/v143, Windows SDK, the matching T850 vcpkg packages, and
runtime assets for GUI checks. Use [build/run](../development/windows-build-and-run.md).
Do not mix CRTs or build one output configuration with MSBuild and CMake concurrently.

An external MSBuild project imports `EditorHost.props`, declares its sources, then
imports `EditorHost.targets`. See [the sample project](../../examples/EditorExtension/EditorExtension.vcxproj).
These imports derive engine paths from their own location, build T8ditorCore and
Framework dependencies, and stage runtime DLLs. They do not use the consumer's
`SolutionDir`. `T850StageEngineAssets=true` optionally stages the stock asset tree;
leave it unset for privately managed assets. Outputs default to the consumer's
`bin/<Platform>/<Configuration>` and `obj` folders. Use the same platform and
configuration for all private libraries.

From the T850 repository root, in a VS developer shell:

```powershell
msbuild examples/EditorExtension/EditorExtension.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CL_MPCount=4 /m:1
& ./examples/EditorExtension/bin/x64/Debug/EditorExtension.exe --selftest
```

CMake supports `add_subdirectory` plus `target_link_libraries(MyEditor PRIVATE
T850::Editor)`. Call `t850_stage_editor_runtime(MyEditor)` for Windows DLLs; add
`ENGINE_ASSETS` for stock assets. There is no installed `find_package` SDK yet.
Set the external target's MSVC runtime to `MultiThreaded$<$<CONFIG:Debug>:Debug>`.

```powershell
cmake -S examples/EditorExtension -B examples/EditorExtension/build -G "Visual Studio 17 2022" -A x64 -DT850_AUTO_INSTALL_VCPKG_DEPS=OFF
cmake --build examples/EditorExtension/build --config Debug --target EditorExtension --parallel 4
& ./examples/EditorExtension/build/Debug/EditorExtension.exe --selftest
```

## Verification

From the repository root, after building the MSBuild sample:

```powershell
& ./T850/scripts/ValidateBuildRegistration.ps1 -SourceRoot (Resolve-Path ./T850).Path
& ./T850/bin/x64/Debug/DayScene.exe --game-selftest
& ./T850/scripts/TestTerrainEditor.ps1 -Extensions -Apis d3d11,d3d12,vulkan,gl
```

The sample self-test checks duplicate editor registration, validation, opaque
configuration round-trip, real component lifecycle, strict missing-type rejection,
and retaining active gameplay after rejected input. Framework `T-EXTENSION-01`
checks registry enumeration and non-overwriting registration.

The native gate uses the real frame loop, deferred edits, invalid/stale rejection,
UndoStack, reload, and two toolbar-requested hosted Play sessions. It verifies
that the external component is not an UnknownComponent and that simulation ticks.
It requires a PASS marker, exit 0, no engine errors, and nonuniform 1440x900 captures.
The test's intentionally rejected authoring edit logs validation counts, not an
engine error. Tests use temporary scene files and do not modify the input scene.

Verified locally: x64 Debug/Release and ARM64 Debug/Release solution builds;
53 gameplay tests on x64 Debug/Release; external MSBuild Debug/Release self-tests;
external CMake Debug build, DLL staging, and self-test; all four Debug desktop
native extension cases; Release extension and stock terrain workflows on D3D12
and Vulkan. ARM64 was cross-compiled, not executed.

These are workflow smoke tests, not pixel-baseline parity, exhaustive widget
automation, or platform-release acceptance. Win32, Android, Steam Deck execution,
and the full historical visual-baseline matrix were not run for this slice.

## Remaining Interface Work

- Replaceable `IPlaySession` with explicit input/render/resize/lifetime contracts.
  The current default still uses SceneTemplate privately.
- General viewport tools/overlays and selection services using stable handles.
- Full document transactions, undo gesture coalescing, atomic save, and project
  resource mounts. No mutable EditorWorld or GPU pointers will be exposed as a shortcut.
- Shared SceneWorld assembly and stable IDs for all world-object links.
- Installed SDK packaging and automated external-consumer CI across platforms.

Private game code belongs in its owning repository. This upstream sample is
genre-neutral; no proprietary game assets, factions, or mechanics belong in T850.