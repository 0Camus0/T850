# T850 Editor — Design Plan

This document captures the editor strategy for T850. It is the authoritative
source for what the editor is, why we picked the approach we did, and how the
work is broken into phases. Each phase ships in its own PR.

## Goals

The runtime today is driven by a hand-coded immediate-mode GUI built on top
of `T8_GUI`. That worked while we were iterating on a single scene, but it
does not scale to authoring workflows: loading models, moving them around in
a viewport, placing cameras and lights, and saving the result back to JSON.

We need a real editor that:

1. Owns its own top-level window (Win32) and tool UI.
2. Hosts one or more child render surfaces (`HWND`s) that the engine renders
   into via the existing `BaseDriver` abstraction.
3. Works across every backend — **D3D11, D3D12, GL today; Vulkan once the
   feature branch lands**.
4. Reuses the existing `Framework.lib` without forking or duplicating engine
   code.

## Options considered

### A. Dear ImGui inside a native Win32/SDL host (chosen)

A small `T850Editor.exe` that links `Framework.lib`, owns the main window,
and uses ImGui (docking + multi-viewport branch) for all tool UI. The
viewport is either an offscreen RT shown via `ImGui::Image`, or a child
`HWND` with its own swapchain whose handle is fed into
`BaseDriver::SetWindowHandle`.

**Pros:** MIT license, same toolchain/debugger as the engine, identical UX
across D3D11/D3D12/GL/Vulkan, ImGuizmo for transform handles, very fast to
get a usable v1.

**Cons:** Less polished than retained widget toolkits for native menus,
modal dialogs, accessibility, complex tables. We will write more UI plumbing
ourselves.

### B. Qt 6 (Widgets) over LGPL DLL link

`QWidget::createWindowContainer(QWindow)` (or `winId()`) gives us an `HWND`
per viewport tab, which we hand to `SetWindowHandle`. Engine still owns
rendering.

**Licensing:** Qt LGPLv3 is legally fine for closed-source commercial use
**only if** we (a) dynamically link to the Qt DLLs (no static linking unless
we ship relinkable object files), (b) ship the Qt DLLs and license notices
with the app, (c) allow users to replace those DLLs (don't sign/lock them
in), and (d) do not modify Qt itself without releasing those modifications.
Some modules (Qt Charts historically, Qt for MCU) are GPL-only or
commercial-only and need to be checked module-by-module.

**Pros:** Best-in-class retained widgets, dialogs, docking, model/view, undo
framework, theming, accessibility, i18n. Cross-platform.

**Cons:** Real LGPL redistribution and packaging obligations. ~40–80 MB of
runtime DLLs. Adds `moc` / Qt SDK to the build pipeline. Some friction
routing input between Qt and the embedded engine HWND.

### C. C# (WPF or WinUI 3) + native interop

UI in C#, viewport via `HwndHost`, engine compiled as a DLL exposing a
stable C ABI invoked through P/Invoke or a C++/CLI bridge.

**Pros:** Fastest UI development for a Windows-only tool, huge ecosystem
(AvalonDock, PropertyTools, native dialogs, MVVM).

**Cons:** Two-language project — must design and maintain a C ABI / interop
layer, marshal scene data, and respect threading rules (UI thread vs render
thread, HwndHost airspace issues for overlays). Windows-only forever
(today the runtime also builds on Linux). Cross-boundary debugging is
painful. WinUI 3 deployment is still rougher than WPF; WPF would be the
safer pick within this option.

### D. Avalonia / wxWidgets / WinForms / Slint

- **Avalonia** — C#, MIT, cross-platform; younger ecosystem; HWND embedding
  via `NativeControlHost` is doable.
- **wxWidgets** — C++, permissive license, easy HWND embedding via
  `GetHWND()`. A serious contender if we ever want retained C++ UI without
  Qt's licensing strings.
- **Slint** — same GPL/commercial trap as Qt.
- **WinForms** — easy HWND embedding via `Panel.Handle`, but dated. Only
  for quick internal tools.

## Recommendation

Start with **Option A (ImGui)** behind a clean Editor API on the engine side.
Document **Qt LGPL (Option B)** as the escalation path for the day ImGui
hits its ceiling. Avoid C# unless Windows-only-forever is an explicit,
accepted constraint.

Reasoning:

1. The team is already comfortable with immediate-mode UI from `T8_GUI`.
2. Zero new licensing concerns.
3. Same `.sln`, same vcpkg setup, same compiler, same debugger session.
4. ImGuizmo + ImNodes + ImGuiFileDialog cover ~80% of editor visuals.
5. The engine-side decoupling we do for ImGui is **exactly** what a future
   Qt or WPF UI would need on top of `Framework.lib`.

## Phased roadmap

### Phase 0 — Decouple engine from window owner (this PR)

- `WindowHandle` typed/tagged window-handle struct
  (`Framework/include/video/WindowHandle.h`): can carry either an
  `SDL_Window*` or an `HWND`.
- `BaseDriver::SetWindowHandle(const WindowHandle&)` virtual method with a
  backward-compatible default implementation that delegates to the existing
  `SetWindow(void*)`. Existing `Win32Framework`/`LinuxFramework` callers are
  untouched.
- `D3D11Driver::SetWindowHandle` and `D3D12Driver::SetWindowHandle` now
  honor an explicit HWND when one is supplied (instead of unconditionally
  calling `GetActiveWindow()`), which is what an editor child-window host
  needs. They fall back to `GetActiveWindow()` when no HWND is provided so
  the existing SDL-driven flow behaves identically.
- `GLDriver::SetWindowHandle` mirrors the same pattern: explicit HWND wins
  on the EGL/ES Windows path; SDL window goes to `m_sdlWindow` for the
  desktop-GL path.

This PR does **not** add a new project, new dependency, or any UI. Its sole
job is to make the rest of the plan possible without touching engine
internals later.

### Phase 1 — Editor shell (ImGui)

- New `T850Editor` project in `T850.sln`, links `Framework.lib`.
- Win32 (or SDL2) main window + ImGui docking branch + multi-viewport.
- ImGui rendered through `BaseDriver` so it works identically on D3D11,
  D3D12, GL, and Vulkan — or use upstream per-API backends as a quick start
  and consolidate later.
- Default layout: menubar, scene hierarchy, inspector, asset browser, log
  console, and a viewport panel that renders to an offscreen RT shown via
  `ImGui::Image` (or to a child HWND swapchain via the Phase 0 plumbing).

**Status:** Phase 1a (project scaffold) landed. The new project lives at
`T850/T8ditor/` and is named **`T8ditor`** (the document still refers to
`T850Editor` above for historical continuity; both names refer to the same
project). `T8ditor.vcxproj` is added to `T850.sln` with a `ProjectReference`
on `Framework.vcxproj`, mirroring DayScene's toolchain (same configs,
`T8VcpkgStatic` / `T8VcpkgDynamic` env vars, `bin\<arch>\<config>\` output).
A matching `T8ditor/CMakeLists.txt` is wired into the top-level Linux build.

**Phase 1b — basic viewport controls (this PR).** Adds the "blender/3dsmax
empty start" feel without pulling in ImGui yet:

- **Editor camera** (`EditorCamera`): orbits a `Target` point with
  middle-drag (or right-drag for 3-button mice); shift+middle pans;
  arrow keys orbit when no mouse is connected; `+`/`-` zoom; `F` frames
  the selection. Wraps Framework's `Camera` for VP composition.
- **Reference grid** (`EditorGrid`): XZ-plane grid with neutral minor
  lines and red X / blue Z principal axes.
- **Three-axis transform gizmo** (`EditorGizmo`): drawn at the selection
  origin in red/green/blue. Modes toggled with `W` (translate, arrows),
  `E` (rotate, axis-aligned circles), `R` (scale, arrows w/ cube tips).
  Visualisation only — interactive mouse-drag dragging arrives with
  ImGuizmo in Phase 1c.
- **Wireframe `.x` viewer** (`EditorMesh`): loads a `.x` file via
  Framework's `xF::XDataBase` and draws its triangle edges as a line
  list. Bypasses Framework's full PBR `RenderMesh` pipeline so the
  editor doesn't have to set up RTs / SceneProps / shader keys this
  early. Pass via `--mesh <path>`; defaults to `Models/SkyBox.X` if
  no flag given.
- **Keyboard manipulation of the selection**: `J`/`L` (X), `U`/`O` (Y),
  `I`/`K` (Z) translate; `[`/`]` rotate around Y; `;` / `'` uniform
  scale.
- **Editor-only colour-uniform line shader** in `Assets/Shaders/`
  (`VS_/FS_EditorLine.{hlsl,glsl}`). Kept editor-side so the existing
  `VS_W` / `FS_W` shaders used by `SplineWireframe` and `WireframeArrow`
  are untouched.

**No ImGui dependency yet** — Phase 1c layers ImGui on top, replacing
the keyboard-driven selection editing with proper inspector panels and
ImGuizmo handles.

### Phase 2 — Core editor features

- Scene hierarchy tree with selection and drag-reparent.
- Inspector for transform, materials, lights, camera params.
- **ImGuizmo** translate/rotate/scale on selection.
- Mouse picking via a color-id RT or ray-vs-AABB walk over the scene.
- Gridded viewport, editor camera (orbit/fly), camera list, "look through
  camera" mode.
- Asset browser rooted at `Assets/`, reusing the existing atlas/JSON
  conventions.
- Save/load scene to JSON using the same library already used for
  `gui_atlas.json`.

### Phase 3 — Quality of life

- Undo/redo as a command stack on top of the Editor API.
- Per-API viewport switcher (D3D11/D3D12/GL/Vulkan dropdown that recreates
  the swapchain). Useful for cross-backend testing — leverages Phase 0
  HWND-handle plumbing.
- Hot-reload of shaders and textures.
- Play-in-editor: spin up the existing scene runtime in the same process.

### Phase 4 — Vulkan parity

- When the Vulkan branch lands, add an `HWND`-based surface path
  (`vkCreateWin32SurfaceKHR`) to `VulkanDriver::SetWindowHandle` next to
  the existing `SDL_Vulkan_CreateSurface` path.
- Verify the editor viewport works under Vulkan with multi-viewport.

### Phase 5 — (Optional) Promote UI to Qt LGPL

- Only if Phase 1–3 hit ImGui's ceiling.
- The Editor API from Phase 0/1 means the UI swap is contained to one
  project; engine, scene format, and tools logic stay untouched.

## Quick comparison

| Concern                       | ImGui (A)        | Qt LGPL (B)                        | C# WPF (C)                    |
|-------------------------------|------------------|------------------------------------|-------------------------------|
| License burden                | None (MIT)       | DLL ship + notices + replaceable   | None                          |
| Time to first usable editor   | Days–weeks       | Weeks–months                       | Weeks (plus interop design)   |
| Per-API HWND hosting          | Trivial          | Easy (`createWindowContainer`)     | Easy (`HwndHost`)             |
| Cross-platform later          | Yes              | Yes                                | No                            |
| UI polish ceiling             | Medium           | Very high                          | Very high                     |
| Toolchain complexity added    | Minimal          | `moc` + Qt SDK                     | C#/.NET + interop layer       |
| Risk if we change our mind    | Low              | Medium                             | High (rewrite engine bridge)  |

**TL;DR:** ImGui + docking now, behind a clean Editor API. Qt LGPL is the
documented escalation path. Skip C# unless Windows-only forever is an
explicit, accepted constraint.
