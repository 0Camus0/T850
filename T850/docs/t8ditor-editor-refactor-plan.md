# T8ditor Editor Refactor — Handoff Plan

Status date: 2026-06-12
Repo: `D:\Code\Game\T850` (git root) · Branch: `feature-editor-refactor`

## Goal
Break up the editor "god file" `T850/T8ditor/EditorApp.cpp` (originally **11,701 lines**)
into cohesive translation units, introduce a real data model (`EditorWorld`), and remove
duplication — **without changing runtime behavior**. Work proceeds phase-by-phase, each
phase build-verified and **runtime-tested in the launcher** before continuing.

## Build & test workflow (do this every phase)
- Full-solution build (launcher-equivalent):
  `powershell T850\scripts\build.ps1 -Config Release -Platform x64`
  (MSBuild `/t:Rebuild` of `T850.sln`; builds both `DayScene.exe` and `T8ditor.exe`.)
- After a green build, **runtime-test the affected feature in the launcher** before moving on.
- Conventions: x64 Release full-solution build after changes; **do not commit/push unless
  explicitly asked**; every editor action must support **Undo (Ctrl+Z)**; authored character
  physics must stay attached to source mesh translate/rotate/scale.

## DONE — committed & runtime-verified (commit `e653dae`, Phases 0–4b)
`EditorApp.cpp`: 11,701 → **10,183** lines. New files (all in `T850/T8ditor/`):

| Phase | What | Files |
|---|---|---|
| 0 | Remove dead `main_fixed.cpp`; sync `.vcxproj.filters` + add `SceneGraph.h` | (build files) |
| 1 | Stateless helpers (NearlyEqual*, S*R*T builders, AABB merge, kRadToDeg/kDegToRad/kMinEditableScale; path/override helpers) | `EditorMath.{h,cpp}`, `EditorUtil.{h,cpp}` |
| 2 | Scene↔runtime conversions (navmesh build-settings/links, physics cook/quality) | `EditorSceneSerialization.{h,cpp}` |
| 3 | **EditorWorld data model** (objects, cameras, lights, physicsEntities, groups, selection, undo, scene-load state) via `GetEditorWorld()` | `EditorWorld.{h,cpp}` |
| 4a | Play Scene hosted-window methods | `PlayScenePanel.cpp` |
| 4b | Mesh Editor hosted-window methods; shared editor-private helpers | `MeshEditorPanel.cpp`, `EditorInternal.h` |

## DONE IN CURRENT WORKTREE — Phase 4c-only, build-green, runtime-verified
Phase 4c has been reconstructed without applying Phase 5's `OnDraw` split:
`RagdollEditorPanel.cpp` and `EditorRagdollSupport.h` are added, the ragdoll window methods are removed from
`EditorApp.cpp`, and `EditorApp.cpp` now has **7,658** lines. The x64 Release full-solution build succeeds.

Important safety change versus the broken stash: the ragdoll panel no longer binds `g_quads`,
`g_dummyWhiteTex`, or `g_dummyEnvMapIdx` as namespace-scope aliases during static initialization. It resolves
those deferred scratch resources inside `DrawRagdollEditorViewport()` right before use. Runtime-test **Mesh Edit**
and **Ragdoll Edit** in the launcher passed on 2026-06-12.

## CURRENT WIP — Phase 5, build-green, awaiting runtime test
Phase 5 has been applied on top of the committed Phase 4c baseline. `EditorApp.cpp` now has **7,669**
lines. The x64 Release full-solution build succeeds.

What changed:
- `OnUpdate()` now delegates the deferred scene-load block to `LoadPendingScene()`.
- `OnDraw()` now orchestrates frame begin/clear/frozen-frame capture/UI/frame dump/end-frame.
- Scene rendering moved to `RenderEditorSceneFrame()`.
- Editor-light synchronization moved to `SyncEditorSceneLights()`.
- ImGui/editor panel drawing moved to `DrawEditorUI()`.
- The old sub-editor fast-path `goto after_editor_imgui;` cases are `return;` statements inside
  `DrawEditorUI()`, while the frame dump and `EndFrame()` epilogue remain in `OnDraw()`.

Runtime-test **Mesh Edit** first, then normal editor scene rendering/loading, before marking Phase 5 done
or committing it.

### Technique (reuse this for remaining phases)
- **Partial class across TUs**: move a sub-app's `EditorApp::` methods byte-for-byte into a new
  `.cpp` (still `namespace t8ditor { ... }`, still `EditorApp::` methods). No new members in
  `EditorApp.h`; moved code uses members via `this`. Cross-TU member calls link fine.
- **Scene data access** in a panel TU: add local aliases at top of the file's anon namespace,
  e.g. `auto& g_objects = GetEditorWorld().objects;` — keeps moved code unchanged.
- **Shared file-local helpers** the moved code needs: either move stateless ones to
  `EditorMath`/`EditorUtil`, or **de-static** the helper in `EditorApp.cpp` and **declare** it in
  a shared header (`EditorInternal.h`). Never delete a definition that staying code still uses.
- **Large byte-identical moves**: use a PowerShell line-slice. A file-scope method ends at the
  first column-0 `}`. Validate boundaries before writing.

## REMAINING WORK
### Phase 4c — runtime verification
Complete in the current worktree. Commit this safe baseline before attempting Phase 5.

### Phase 5 — decompose `OnDraw` (1,959 lines) and the `OnUpdate` scene-loader
Code/build complete in the current worktree. Runtime-test Mesh Edit and scene loading/rendering before
marking done or committing.

### Phase 6 — remove duplication (the user's original ask)
- Replace the **3 hand-rolled orbit cameras** (main `EditorCamera` vs mesh-editor `m_meshEditorOrbit*`
  vs ragdoll-editor inline spherical math) with the existing `EditorCamera`.
- Unify the **inconsistent viewport-resize** debounce (mesh editor uses `RenderViewport::ShouldResize`;
  ragdoll editor hand-rolls a stable-frame counter).
- De-dup the copy-pasted RT-validity check + ImGui image blit across the hosted viewports.

### Optional
Extract `DrawNavMeshAuthoringPanel` and `DrawEditorRenderingPanel` into their own panel TUs.

## ⚠️ CRITICAL — Edit Mesh regression history
A first attempt at Phases 4c+5 is preserved in **git stash** `refactor-phase4c-5-wip-EDITMESH-REGRESSION`
(`git stash list`; includes untracked `RagdollEditorPanel.cpp` + `EditorRagdollSupport.h`). It built
green but **regressed the "Edit Mesh" window**: the mesh preview shows a purple / garbage material and
broken skinning ("model moving but looks like trash"). The committed 4b build renders Edit Mesh
**correctly**, and the DayScene sandbox renders the same model correctly.

What was ruled out (so the next agent doesn't repeat it):
- `MeshEditorPanel.cpp` is **byte-identical** to the working version (git diff confirms only
  `EditorApp.cpp` changed since `e653dae`).
- A normalized content diff proved **no logic lines were dropped** — the only behavioral delta is the
  equivalent `goto`→`return`. So the `OnDraw` decomposition (Phase 5) is behavior-preserving; **Phase 4c
  is the prime suspect.**

Design intent (from the user): *"Edit Mesh re-uses the existing scene as a container where the render
states of the Editor are 100% separate and isolated. Only the API objects (already-loaded resources,
shaders, textures) must be shared. The render states have to be separate, isolated."*

Likely cause — **shared mutable render state** that the refactor's TU split / init order exposed:
- `MeshEditorPanel::EnsureMeshEditorEmbeddedScene` calls
  `m_meshEditorScene->UseExternalMesh(obj.litInst, meshPath)` → shares the underlying
  `RenderSkinnedMesh` (`obj.litInst.pBase`), so **bone textures / skinning state are shared** between
  the main editor and the embedded preview.
- Phase 4c exposed the deferred-render scratch globals `g_quads`, `g_dummyWhiteTex`, `g_dummyEnvMapIdx`
  via accessors (`EditorDeferredQuads` etc.) so the extracted ragdoll viewport could use them — i.e.
  the editor's shared deferred quads are reused across viewports.

Current recommended next steps:
1. Commit the current Phase 4c-only baseline when the user asks.
2. Continue with Phase 5 separately and runtime-test Mesh Edit again before Phase 6.

## Other stored conventions
- ImGui layouts are global by default; per-scene docking only when "Allow Custom Layout" is checked.
- Persisted physics meshes live under `Assets\Models\Phx`, referenced from `.t8scene`.
- Android: build a signed Release APK with the existing scripts (not Debug/unsigned).
