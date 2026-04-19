# T8ditor — Handoff Notes for the Next Agent

This file captures **everything currently shipped on the
`copilot/editor-development-options` branch** for the T8ditor sub-project,
along with the plan, design rationale, known gaps, and a concrete to-do
list ordered by priority. It is meant to be read cold by a fresh agent
who has never seen the branch — start here.

> Companion documents:
> - [`EDITOR.md`](EDITOR.md) — the original architecture / phase plan that
>   the user signed off on. The high-level "why" lives there. This file
>   is the operational "what's done / what's next".
> - [`README.md`](../README.md) — repo overview.

---

## 1. TL;DR — Where we are right now

We have a third executable, **`T8ditor.exe`**, that:

1. Opens its own window through the existing `Win32Framework` /
   `LinuxFramework` event loop (no fork of the framework).
2. Links `Framework.lib` exactly like `DayScene.exe` does.
3. Renders a recognisable **"Blender / 3dsMax empty start"** viewport:
   - XZ reference grid (red X axis, blue Z axis, neutral minor lines).
   - Three-axis transform gizmo on the active selection
     (translate / rotate / scale, RGB-coloured per axis, mode toggled
     with `W` / `E` / `R`).
   - Optional wireframe display of an `.x` mesh loaded from the CLI.
4. Is driven by an **orbit camera** (middle-drag = orbit, Shift+middle
   = pan, arrow keys orbit when no mouse, `+` / `-` zoom, `F` frame
   selection).
5. Lets the user **manipulate the loaded mesh** with the keyboard
   (`J`/`L` X, `U`/`O` Y, `I`/`K` Z, `[`/`]` rotate Y, `;` / `'` scale).

What we **do not** yet have: ImGui panels, mouse-drag interactive
gizmos, mouse picking, mouse-wheel zoom, PBR-lit mesh display, or a
scene-graph / multi-object selection model. Those are Phase 1c+.

---

## 2. Repository layout (only the editor-relevant bits)

```
T850/
├── T850.sln                              # 3 projects: Framework, DayScene, T8ditor
├── EDITOR.md                             # Phase plan, options analysis (authoritative)
├── HANDOFF.md                            # ← you are here
│
├── Assets/Shaders/
│   ├── VS_EditorLine.hlsl   FS_EditorLine.hlsl
│   └── VS_EditorLine.glsl   FS_EditorLine.glsl
│       # NEW — colour-uniform line shader pair used only by T8ditor.
│       # Existing VS_W / FS_W (used by SplineWireframe & WireframeArrow)
│       # are intentionally untouched.
│
├── Framework/                            # unchanged this slice
│   ├── Framework.vcxproj                 # builds Framework.lib
│   └── ...
│
├── DayScene/                             # unchanged this slice
│   └── DayScene.vcxproj                  # builds DayScene.exe (the runtime)
│
└── T8ditor/                              # NEW project (T8ditor.exe)
    ├── T8ditor.vcxproj                   # links Framework.lib
    ├── T8ditor.vcxproj.filters
    ├── T8ditor.rc
    ├── CMakeLists.txt                    # globs *.cpp; mirrors DayScene's link list
    ├── main.cpp                          # entry point + CLI parsing
    ├── EditorApp.{h,cpp}                 # AppBase subclass (host + per-frame loop)
    ├── EditorCamera.{h,cpp}              # orbit/pan/zoom around Target
    ├── EditorLineRenderer.{h,cpp}        # shared shader + CB + draw helper
    ├── EditorGrid.{h,cpp}                # XZ reference grid
    ├── EditorGizmo.{h,cpp}               # translate/rotate/scale visualisation
    └── EditorMesh.{h,cpp}                # .x file loader → wireframe
```

---

## 3. What every file does (read in this order)

### 3.1 `T8ditor/main.cpp`

- Mirrors `DayScene/App.cpp`'s `main()` pattern exactly.
- Parses CLI: `--api {gl|d3d11|d3d12}`, `--width N`, `--height N`,
  `--logFile PATH`, `--logLevel ...`, **`--mesh PATH`**.
- Defaults `meshPath` to `Models/SkyBox.X` if no `--mesh` flag is
  supplied and that file exists. (SkyBox.X is the smallest known-good
  asset that ships with the repo.)
- Calls `t8ditor::SetStartupMeshPath(meshPath)` *before* constructing
  `EditorApp`, so `EditorApp::CreateAssets()` knows what to load.
- Initialises `t800::Log` with session tag `"t8ditor"`.
- On Windows: `Win32Framework::OnCreateApplication(desc)` →
  `UpdateApplication()` → `OnDestroyApplication()`.
- On Linux: `LinuxFramework::OnCreateApplication(desc)` (Linux drives
  its own loop inside that call).

### 3.2 `T8ditor/EditorApp.{h,cpp}`

`t8ditor::EditorApp : public t800::AppBase`. Owns the four editor
sub-systems as members:

```cpp
EditorCamera        m_camera;
EditorLineRenderer  m_lines;
EditorGrid          m_grid;
EditorGizmo         m_gizmo;
EditorMesh          m_mesh;
```

Per-frame order in `OnUpdate()` → `OnInput()` → `OnDraw()`:

1. `OnInput()` — `W`/`E`/`R` toggle gizmo mode (single-press semantics
   via `IManager.PressedOnceKey`). Then `m_camera.Update(dt, IManager)`,
   then `ProcessSelectionInput()` (keyboard manipulation of the mesh's
   T/R/S).
2. `OnDraw()` — `BeginFrame` → `Clear` → grid → mesh wireframe → gizmo
   (always last so it renders on top) → `SwapBuffers` → `EndFrame`.

The gizmo is drawn at the **mesh's translation only** (not the full
TRS), so it doesn't visually inherit the mesh's rotation or scale.

`g_startupMeshPath` is a file-static `std::string` set by
`SetStartupMeshPath()` (called from `main.cpp`).

### 3.3 `T8ditor/EditorCamera.{h,cpp}`

Spherical orbit around `m_target` (yaw, pitch, distance). Wraps a
`::Camera` for VP composition. Key facts:

- `Init()` calls `m_cam.InitPerspective()` with placeholder eye, then
  `RecomputeEye()` and `m_cam.Update(0)`.
- `Update(dt, im)`:
  - SDL **middle button = index 1**, **right = index 2** (per
    `Framework/src/core/windows/Win32Framework.cpp`).
  - `Shift + middle` = pan: shifts `m_target` along
    `m_cam.Right` and `m_cam.Up`, scaled by `m_distance` so panning
    feels constant across zoom.
  - Otherwise middle/right = orbit (yaw/pitch += mouse delta *
    `OrbitSpeed`).
  - Arrow keys = orbit fallback (no mouse needed).
  - `+`/`-` = zoom (no mouse-wheel routing in Win32Framework yet —
    see § 5).
  - `F` = `Frame()` (resets distance to `FrameDistance`).
- Pitch is clamped to `[MinPitch, MaxPitch]` to avoid gimbal flip at
  the poles.
- `RecomputeEye()` builds `dir = (sin(yaw)cos(pitch), sin(pitch),
  cos(yaw)cos(pitch))` and sets `Eye = target + dir * distance`,
  `Look = target`, then `m_cam.SetLookAt(target)`.

### 3.4 `T8ditor/EditorLineRenderer.{h,cpp}`

Shared GPU plumbing for every editor overlay (grid, gizmo, mesh
wireframe).

- Owns one `ShaderBase*` (the editor-line shader pair) and one
  `ConstantBuffer*` (CB struct = `XMATRIX44 WVP; XVECTOR3 LineColor;`).
- `DrawLines(world, vp, rgba, vb, ib, indexCount, stride, ibFormat)`:
  - Composes `WVP = world * vp`.
  - Uploads the full `CBuffer` struct to `m_cb` (NOT just the matrix —
    that was a review-fix; see § 8 lessons learned).
  - Sets primitive topology to `LINE_LIST` and issues `DrawIndexed`.
- Static helpers `CreatePositionVB(positionsXYZW, n)` and
  `CreateIndexBuffer16(indices, n)` so callers don't have to know the
  `BufferDesc` boilerplate.

#### Why an editor-only shader?

The existing `VS_W` / `FS_W` (used by `SplineWireframe` and
`WireframeArrow`) **hard-code the colour to magenta in the fragment
shader**. We need per-axis colour (red X, green Y, blue Z), and we
absolutely don't want to retro-fit a colour uniform onto `FS_W` and
risk regressing those callers. So we ship a parallel pair under
`Assets/Shaders/{VS,FS}_EditorLine.{hlsl,glsl}`.

### 3.5 `Assets/Shaders/{VS,FS}_EditorLine.{hlsl,glsl}` — gotcha

**HLSL** is straightforward: `cbuffer { float4x4 WVP; float4 LineColor; }`,
VS returns `SV_POSITION`, FS returns `LineColor`.

**GLSL has a non-obvious trap** — see the stored memory
`GL constant buffer reflection`: the GL backend
(`Framework/src/video/GLShader.cpp:73-130`) walks reflected uniforms
**positionally** and assigns each one a byte offset into the CB. If
you declare `LineColor` as a uniform in **both** VS and FS, the
parser sees it twice and the byte offsets shift out of sync with the
actual CB layout — drawing breaks silently.

**Solution shipped:** declare `LineColor` only in
`VS_EditorLine.glsl`, and forward it to the FS via a `vColor`
varying. `FS_EditorLine.glsl` reads `vColor`, never `LineColor`. The
file headers explain this.

### 3.6 `T8ditor/EditorGrid.{h,cpp}`

- `Create(halfExtent=10, spacing=1.0f)` builds two GPU resources:
  - One VB+IB with all the **minor** grid lines (skipping the two
    centre axis lines).
  - One VB shared by both **principal axes**, with **two separate IBs**
    — `m_xAxisIB` references the X endpoints, `m_zAxisIB` the Z
    endpoints — so each axis gets its own colour in `Draw()`.
- `Draw(lines, vp)` calls `lines.DrawLines(...)` three times: minor
  grid (gray), X axis (red), Z axis (blue). World matrix is identity.

### 3.7 `T8ditor/EditorGizmo.{h,cpp}`

Three modes, each with its own VB and three per-axis IBs. Geometry
is built in unit-length local space; `Draw()` pre-multiplies a
`Size`-scale matrix so the gizmo size is **independent of the
selection's own scale**.

- **Translate (`W`):** `BuildArrow(axis)` — shaft + 4 arrowhead barbs
  at 0.85 along the axis.
- **Rotate (`E`):** `BuildCircle(axis, segments=48)` — closed
  line-loop in the plane perpendicular to that axis.
- **Scale (`R`):** `BuildScaleAxis(axis)` — shaft + small wireframe
  cube (12 edges) at the tip.

`Draw(lines, vp, world)` switches on `m_mode`, issues 3 coloured
`DrawLines` calls (X red, Y green, Z blue).

> **Visualisation only this PR.** No mouse-drag interaction yet —
> see § 7 next steps.

### 3.8 `T8ditor/EditorMesh.{h,cpp}`

Loads `.x` via `xF::XDataBase::LoadXFile(path)` (the same loader
`PrimitiveManager::CreateMesh` uses in `DayScene`). Then walks every
`xMeshContainer*` in `XMeshDataBase` and every `xMeshGeometry` in
`container->Geometry`:

- Appends `geom.Positions` to a flat `float[xyzw]` buffer.
- Walks `geom.Triangles` (a flat `xWORD` triplet list — confirmed
  against `Framework/src/utils/XDataBase.cpp:738` and
  `Framework/src/scene/RenderMesh.cpp`) and emits **3 line segments
  per triangle** (a→b, b→c, c→a) into a 16-bit IB.
- Computes the local-space AABB centre (`m_localCenter`) for the
  camera's "frame selection".

Hard limits / behaviours worth knowing:

- **65535 vertex cap.** If the merged VB exceeds that, we log an
  error and refuse to load (rather than silently overflowing 16-bit
  indices). 32-bit IB path is a follow-up — see § 7.
- **No subset / material handling.** This bypasses Framework's PBR
  `RenderMesh` pipeline entirely — the editor only needs to *see*
  the geometry, not light it. PBR display is a follow-up.
- **TRS composition.** `BuildWorld()` returns
  `S * Rx * Ry * Rz * T`, matching the engine convention used by
  `PrimitiveInst::Update` (row-vector / row-major, D3DX-style; see
  the stored memory `matrix conventions`).

### 3.9 Project files

- **`T8ditor/T8ditor.vcxproj` + `.vcxproj.filters`** — register all
  five `.cpp` / `.h` pairs. Layout / configs / `T8VcpkgStatic` and
  `T8VcpkgDynamic` references are copied verbatim from
  `DayScene.vcxproj`. The vcxproj has a `ProjectReference` on
  `Framework.vcxproj` so MSBuild builds it transitively.
- **`T8ditor/CMakeLists.txt`** — already globs `*.cpp`, so new files
  are picked up automatically. Linux-side links the same library set
  as DayScene.
- **`T850.sln`** — already includes `T8ditor.vcxproj` from Phase 1a
  (no changes needed this slice).

---

## 4. How to build & run

### Windows (the primary target)

Prereqs (per stored memories):

- VS 2022 + MSBuild.
- Environment variables `T8VcpkgStatic` and `T8VcpkgDynamic` pointing
  at vcpkg installed-triplet dirs (referenced by
  `Framework.vcxproj:459`).

Commands:

```powershell
# From T850/ :
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "T850.sln" /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal

# Or, the wrapper script:
.\scripts\build.ps1 -Config Release -Platform x64
```

Outputs:

- `Lib\<Config>\x64\Framework.lib`
- `bin\x64\<Config>\DayScene.exe`
- `bin\x64\<Config>\T8ditor.exe`  ← the editor

Run:

```powershell
# From T850/ :
.\bin\x64\Release\T8ditor.exe                     # default: Models/SkyBox.X
.\bin\x64\Release\T8ditor.exe --mesh Models\SponzaEsc.X
.\bin\x64\Release\T8ditor.exe --api gl --mesh Models\SkyBox.X
.\bin\x64\Release\T8ditor.exe --logFile logs\editor.log --logLevel debug
```

### Linux (CI / parity)

```bash
cd T850 && mkdir -p build && cd build && cmake .. && make T8ditor -j
```

### Lightweight syntax check (no build infra)

The agent sandbox can syntax-check without the full toolchain:

```bash
cd /home/runner/work/T850/T850/T850
g++ -fsyntax-only -std=c++17 -DOS_LINUX -include cstdint \
    -I Framework -I Framework/include \
    -I Librerias/SDL-1.2.14/include \
    -I Librerias/glew-2.0.0/include \
    -I Librerias/OGLES2/amd/include \
    -I Librerias/stb/include \
    -I Librerias/tinyxml2/include \
    T8ditor/Editor*.cpp
```

`main.cpp` will fail this check because it includes `LinuxFramework.h`
(needs Wayland/EGL headers) — that is expected and unrelated to the
editor sources.

---

## 5. Controls reference (user-facing)

### Camera

| Action                | Binding                              |
| --------------------- | ------------------------------------ |
| Orbit                 | Middle-drag, or Right-drag, or Arrow keys |
| Pan                   | Shift + Middle-drag                  |
| Zoom in / out         | `+` / `-` (`=` also works for `+`)   |
| Frame selection       | `F`                                  |

### Gizmo mode

| Action                | Binding |
| --------------------- | ------- |
| Translate (arrows)    | `W`     |
| Rotate (circles)      | `E`     |
| Scale (cube tips)     | `R`     |

### Selection manipulation (keyboard, while a mesh is loaded)

| Action                | Binding   |
| --------------------- | --------- |
| Translate +X / -X     | `L` / `J` |
| Translate +Y / -Y     | `U` / `O` |
| Translate +Z / -Z     | `I` / `K` |
| Rotate around Y       | `[` / `]` |
| Uniform scale up/down | `'` / `;` |

> Mouse-drag interaction on the gizmo handles themselves is not
> wired up yet — keyboard is the only way to manipulate the
> selection in this slice.

---

## 6. Design rationale (why it's built this way)

### 6.1 Why a separate `EditorLineRenderer` instead of reusing `SplineWireframe` / `WireframeArrow`?

Both existing classes are **fused units**: they own their geometry
*and* their shader *and* their constant buffer, all hard-coded to a
single magenta colour. The editor needs:

- Multiple coloured passes (R/G/B per axis, gray for the grid).
- Multiple geometries sharing one shader+CB (grid, three gizmos,
  one mesh).

Forking each callsite to add a colour parameter would have churned
shipped runtime code with no gain for DayScene. Instead we shipped a
parallel renderer + shader for the editor.

### 6.2 Why bypass `RenderMesh` for `.x` display?

`RenderMesh::Create` walks the material list and compiles a shader
per `ShaderKey` permutation, expects `SceneProps*` (lights, cameras,
ambient, exposure, ...) to be wired up, and binds 8 textures per
subset. None of that is meaningful in an empty editor scene.

`EditorMesh` does the absolute minimum: positions VB + line-list IB.
You can recognise the model. Lighting, materials, and PBR display are
follow-up work — see § 7.

### 6.3 Why no ImGui yet?

Phase 1a explicitly deferred the ImGui dependency until the project
scaffolding was proven on Windows. Phase 1b (this slice) keeps the
"no new third-party dependency" promise while still delivering a
useful editor-101 viewport. ImGui + ImGuizmo land in Phase 1c.

### 6.4 Why keyboard manipulation of the selection?

Without ImGui's input filtering and without mouse-drag gizmo handles,
a keyboard editor lets the user *prove the gizmo is updating
correctly* (you can watch the arrows move with the mesh). It's
explicitly a stop-gap for Phase 1c.

---

## 7. Next steps — ordered by priority

The following is the **recommended order** for the next agent. Each
item is sized so it can land as a single PR without blocking the
others.

### P0 — make the current slice production-quality on Windows

These aren't "new features" — they're loose ends that we couldn't
verify from the Linux sandbox.

1. **Smoke-test on Windows.**
   - Build with the MSBuild command in § 4.
   - Run `T8ditor.exe --api d3d11`, `--api d3d12`, `--api gl`.
   - Verify: window opens, grid appears, gizmo appears, mesh loads.
   - If GL fails to render, check the GLSL CB byte-offset trap
     described in § 3.5 — likeliest culprit.
2. **Wire SDL `MOUSEWHEEL` events into `InputManager`.**
   - Currently `Win32Framework.cpp` polls cursor position via
     `GetCursorPos` and never sees wheel events.
   - Add an `InputManager::wheelDelta` field and a poll in the SDL
     event pump.
   - Then in `EditorCamera::Update`, multiply distance by
     `pow(ZoomSpeed, -wheelDelta)` (or similar). `ZoomSpeed` already
     exists as a tunable.
3. **Resize handling.**
   - `Win32Framework` currently rebuilds the swapchain on resize but
     doesn't notify the app. Add an `EditorCamera::SetViewportSize`
     call from `EditorApp` when the framework's `aplicationDescriptor`
     dimensions change between frames (cheap polling).

### P1 — make the gizmo *do* something

4. **Mouse picking on the gizmo handles.**
   - Implement a ray-vs-cylinder (translate shaft) /
     ray-vs-torus-segment (rotate ring) / ray-vs-cube (scale tip)
     hit test in screen space.
   - On `MouseDown` over a handle, store `m_activeAxis` and the
     hit point in world space.
   - On `MouseDrag`, project the cursor onto the active axis line
     (translate) / the rotation plane (rotate) / the axis scalar
     (scale) and apply the delta to the mesh's TRS.
   - Reference: ImGuizmo's `ComputeTripodAxisAndVisibility` &
     `HandleTranslation`. We don't need ImGuizmo itself for this
     slice — a 200-line bespoke implementation is enough.

### P2 — bring in ImGui

This is the official "Phase 1c" hand-off point.

5. **Add ImGui (docking branch) + ImGui_ImplSDL3 + an ImGui backend
   per renderer.**
   - Vcpkg manifest update — add `imgui[docking-experimental,sdl3-binding,dx11-binding,dx12-binding,opengl3-binding]`.
   - Initialise in `EditorApp::CreateAssets` after the driver is up.
   - Minimal panels for first cut: **Hierarchy** (lists "Scene Root
     → mesh"), **Inspector** (T/R/S sliders for the selection),
     **Console** (mirrors `T8_LOG` output), **Viewport** (the
     existing render, optionally inside an `ImGui::Image` of an
     offscreen RT).
6. **Add ImGuizmo for proper interactive gizmos.**
   - Replaces `EditorGizmo`'s draw-only path (or wraps it).
   - Bind to the same `m_mesh` transform that the keyboard editor
     uses, so both input paths stay coherent.

### P3 — proper rendering of `.x` files

7. **Optional: PBR-lit display through `RenderMesh`.**
   - Stand up a minimal `SceneProps` (one directional light, one
     camera, default exposure) inside `EditorApp`.
   - Use `PrimitiveManager::CreateMesh(path)` instead of the
     bespoke wireframe loader.
   - Keep wireframe as a viewport-overlay toggle (Maya-style
     "wireframe on shaded").
8. **Multi-mesh scene + selection model.**
   - Replace the single `EditorMesh m_mesh` with
     `std::vector<EditorMesh>` and an `int m_selectedIndex`.
   - Selection driven by Hierarchy panel clicks (P2).
9. **Save / load the editor scene to JSON.**
   - One JSON per scene listing each mesh's path + TRS.
   - Reuse the JSON library already used for `gui_atlas.json`.

### P4 — nice-to-haves

- 32-bit index path for `.x` meshes >65 535 verts.
- Distance-scaled gizmo (constant pixel size on screen).
- Camera presets (top / front / side / persp) on numpad keys.
- "Look through camera" (use a scene camera as the editor camera).

---

## 8. Lessons learned / hidden gotchas

These are the non-obvious things that already bit us once. Re-read
before touching the editor:

### 8.1 GLSL constant-buffer reflection

The GL backend assigns each declared uniform a byte offset based on
**the order it's reflected**. If `LineColor` appears in both VS and
FS, it gets reflected twice and shifts every subsequent uniform's
offset. **Always declare CB-mapped uniforms in only one stage, and
forward via varyings.** See stored memory
`GL constant buffer reflection`.

### 8.2 Matrix conventions

xMaths is row-vector / row-major (D3DX-style). `v * M` is the
transform; reading the multiplication left-to-right reads as
application order. Compose TRS as
`Scale * Rx * Ry * Rz * Translation`, **not** the OpenGL-textbook
`T * R * S`. See stored memory `matrix conventions` and
`Framework/src/scene/PrimitiveInstance.cpp:71`.

### 8.3 `xMeshGeometry::Triangles` index width

Now that `Indices32Bit` + `Triangles32` exist (per stored memory
`index buffers`), `EditorMesh` *does not yet handle the 32-bit
case* — it only reads the 16-bit `Triangles` field. For very large
`.x` meshes, that's a silent skip. Fix when adding the 32-bit IB
path (P4).

### 8.4 `ConstantBuffer::UpdateFromBuffer` expects the whole struct

The first cut passed `&cb.WVP[0]` (only the matrix). On D3D11 with
default-usage CBs that happens to upload the right number of bytes
(`descriptor.byteWidth`) but reads past the matrix into stack memory
for the colour. Fixed by passing `&cb` directly. **Always pass the
struct head, not a sub-field.**

### 8.5 Mouse button indices

SDL middle = **1**, right = **2** (verified in
`Framework/src/core/windows/Win32Framework.cpp:113`-onwards). Do not
guess — it's not the same as Win32 `VK_*` ordering.

### 8.6 `T800K_*` keys are SDL3-style

`InputManager.h` enumerates the legacy SDL1 codes, and
`SDL3KeyToSTDKEY()` maps SDL3's larger keycodes back. ASCII keys
work directly (`T800K_w == 'w' == 119`). Function keys, modifiers,
and arrows go through the mapping table. If you add a key binding,
test it on Windows — the Linux mapping isn't necessarily complete.

---

## 9. Quick sanity checklist for the next PR

Before you push:

- [ ] Did you touch `VS_W` / `FS_W` or any `Framework/` shader? If
      yes, run `DayScene.exe` to make sure `SplineWireframe` and
      `WireframeArrow` still draw.
- [ ] Did you add a uniform to `VS_EditorLine.glsl` or
      `FS_EditorLine.glsl`? Re-read § 8.1.
- [ ] Did you touch the CB struct? Make sure HLSL field order,
      GLSL uniform order, and the C++ struct order all still match.
- [ ] Did you compose a TRS matrix? Re-read § 8.2.
- [ ] Did you add `.cpp` / `.h` files to `T8ditor/`? Update
      `T8ditor.vcxproj` **and** `T8ditor.vcxproj.filters` (CMake
      globs `*.cpp` so it's automatic there).
- [ ] Did you change `Framework.lib`'s public API? Make sure
      `DayScene.exe` still builds and runs.
- [ ] Run `parallel_validation` and address Code Review feedback
      before reporting "done".

---

## 10. People & references

- The high-level user request that started this work is verbatim:
  > "The very basic few controls that we need, is to have something
  > similar to what 3dsmax or blender shows at the start, some kind
  > of grid, the basic camera rotation around the origin or around
  > the selected object, gizmos to move the geometry, gizmos to
  > rotate the geometry, gizmos to scale the geometry. We should be
  > able to load the .x files and show it in the editor, that would
  > be a 101 start."
- The architecture decisions ("ImGui inside a Win32/SDL host") were
  signed off in [`EDITOR.md`](EDITOR.md) § Recommendation.
- Branch: `copilot/editor-development-options`.
- Existing PR description (auto-generated from `report_progress`) on
  the GitHub PR page covers the per-commit checklist.

If anything in this file disagrees with the actual code on `HEAD`,
**the code wins** — please update this file in the same PR that
introduces the divergence.
