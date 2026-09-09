---
name: t850-editor-extensions
description: "Use when embedding T8ditorCore in another repository, building a private editor, adding static editor panels, commands, component inspectors, validators, deferred gameplay edits, or diagnosing external editor registration and Play behavior."
argument-hint: "State the host, component/tool ID, authoring data, and expected editor/Play behavior."
---

# T850 Static Editor Extensions

## Establish the Contract

Read [the SDK guide](../../../documentation/editor/editor-sdk.md) and
[the public API](../../../T850/T8ditor/include/t8ditor/EditorHost.h).
When T850 is nested as a submodule, resolve its root with `git -C <engine-path>
rev-parse --show-toplevel`; do not use the parent game's root as the engine root.

This is the first static SDK slice. Panels, commands, inspectors, validators,
gameplay edits, and default Play registration work. Replaceable Play sessions,
viewport tools, selection handles, full-world transactions, DLL plugins, and hot
reload are not implemented. Do not invent those APIs or expose EditorWorld to
simulate them. Propose a separate bounded implementation when they are required.

## Implement

1. Start from the external [sample](../../../examples/EditorExtension/main.cpp)
   and the nearest owning callback; avoid copying editor source files.
2. Register namespaced runtime types with ComponentFactoryRegistry. Check the
   Register result; invalid/duplicate entries return false without replacement.
3. Keep runtime registration independent of ImGui. Use the same recipe after
   GameLogicSystem initialization in the standalone host. Hosted default Play
   copies the editor's registered factories before loading components.
4. Register editor callbacks at startup. Inspector callbacks edit descriptor
   copies and return true; panel callbacks are already inside an ImGui window.
5. ReadScene, prepare GameplayEdit with its revision and undo label, then submit.
   Never mutate the live editor world, rebuild resources during drawing, or
   cache callback references. Stale edits require a fresh snapshot, not forced replay.
6. Domain validators must be shared with the standalone runtime explicitly;
   editor callbacks do not run in the game. Version/validate params/config_json.
7. Enable requireKnownComponentsForPlay in private hosts. Unknown preservation
   is an authoring feature, not proof that a component can execute.

One RunEditor per process is supported. Captured registration state must outlive
that run. Keep private code and assets outside the engine checkout.

## Build and Check

Windows uses MSBuild/v143. Consumer projects import EditorHost.props/targets;
CMake consumers link T850::Editor. Do not pass the game's SolutionDir to Framework
or duplicate T850 dependency lists. Public imports own engine path resolution.

From the engine repository root in a VS developer shell:

```powershell
msbuild examples/EditorExtension/EditorExtension.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CL_MPCount=4 /m:1
& ./examples/EditorExtension/bin/x64/Debug/EditorExtension.exe --selftest
& ./T850/scripts/TestTerrainEditor.ps1 -Extensions -Apis d3d11,d3d12,vulkan,gl
& ./T850/scripts/ValidateBuildRegistration.ps1 -SourceRoot (Resolve-Path ./T850).Path
```

The first focused validation should be the sample self-test for registry changes
or native extension regression for edits/Play. A successful build alone does not
prove undo, persistence, custom component execution, or hosted teardown.

Preserve MSBuild EditorSources.props/core filters and CMake parity. Framework
changes still require the gameplay self-tests and x64/ARM64 checks in the engine
router. Report smoke captures separately from visual-baseline acceptance. Update
the SDK guide/status and state remaining unsupported interfaces honestly.