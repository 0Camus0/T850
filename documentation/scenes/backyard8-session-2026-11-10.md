# Backyard Scene 8 — Session Record (2026-11-10)

> **Superseded in part (2026-11-10, later):** the solver's wall top came from the layout's
> `freeboard` (an invisible wall 0.30 m above the real coping), which is why the water start
> needed `prefillLevel −0.27` to settle at −0.04. With the wall at the real coping the water
> start uses `prefillLevel −0.04` and settles at −0.170 m; the gate now has per-start targets.
> See [backyard8-ssfr-realism.md](backyard8-ssfr-realism.md).

This is the record of the work done **after** the frontier-model handoff document
[backyard8-fluid-summary-and-next-steps.md](backyard8-fluid-summary-and-next-steps.md).
The handoff started at commit `1544474`; the pre-work state is tagged
`backyard8-phase0-baseline`.

The session covered four things:

1. The remaining Phase 1 solver fixes (rest density, wall no-slip, settle-before-drop,
   determinism gate, water-loss measurement).
2. Native Windows verification of D3D12, D3D11 and GL, plus the Linux GL driver/backend
   check.
3. The 2×6 solver experiment and adoption, with re-frozen references.
4. The non-interactive cleanup items: re-seed debounce, small code fixes, and the automated
   capture/water-level regression gate.

Nothing was pushed. The working tree is clean except the generated
`T850/Assets/.t850-cloud-assets.json` cache.

---

## 1. Commits and revert points

| Ref | Contents |
|---|---|
| `backyard8-phase0-baseline` (tag, `1544474`) | Verified pre-work state |
| `6572247` | Phase 1 solver: rest density 0.907, wall no-slip, settle-before-drop, integer wind gate |
| `0e4e6d9` | Docs: water-loss sweep (3.5) |
| `7e28cef` | Docs: D3D12/D3D11 native Windows pass |
| `84bf60e` | Docs: Windows GL pass; Linux GL driver/backend split |
| `4e71223` | 2×6 solver, regression gate, 3.7/3.10 fixes, final references |

Revert the whole session:

```bash
git revert 6572247 0e4e6d9 7e28cef 84bf60e 4e71223
```

Or return to the pre-work state (destructive):

```bash
git reset --hard backyard8-phase0-baseline
```

---

## 2. Starting point and baseline checks

Before changing anything:

- Linux build succeeded; `--game-selftest` was 78 PASS / 0 FAIL.
- `check_gl_twins.py` and `check_fluid_constants.py` passed.
- The Vulkan frame-24 screen-space reference matched the handoff exactly:
  228,390 px, bbox `x[0,959] y[0,438]`, mean (66.7, 138.1, 150.2).
- The handoff's frame-300 water capture was within its documented GPU-atomic
  non-determinism.

The baseline was tagged so every later result can be A/B compared.

---

## 3. Phase 1 solver work

### 3.1 Free-surface compaction (3.4)

The pool was seeded at `restingLevel 0` and settled for 1800 frames (lake start). The
handoff suggested sweeping `restDensityScale` 0.98–0.94; that range was **not enough**.

| `restDensityScale` | Settled level |
|---|---|
| 1.000 | −0.191 m |
| 0.980 | −0.162 m |
| 0.960 | −0.129 m |
| 0.940 | −0.093 m |
| 0.920 | −0.055 m |
| 0.912 | −0.041 m |
| 0.911 | −0.039 m |
| **0.907 + wall no-slip 8** | **−0.043 m** |

`restDensityScale` was set to **0.907**; `restingLevel` became **0.0** at that point (later
−0.02 after 2×6; see §5). The 0.907 value is the relaxed packing of the seeded fluid, not a
hardcoded offset, and it survives in the final config.

### 3.2 Pool never still (3.6)

Physical air drag replaced `WindStrength * windSpeed * 1.0e-3`:

```hlsl
drag = WindStrength * 1.56e-6 * length(rel.xz) / ParticleRadius;
```

`windStrength` became a config key and GUI slider (0–50). Wind alone did **not** stop the
motion: `windStrength` 1 and 20 produced the same mean speed. The particle dump showed the
real cause: a persistent **wall-driven circulation** — about 0.335 m/s downward in the
one-particle wall layer, 0.130 m/s at the surface. The analytic Akinci boundary was
free-slip; it kept particles out of the wall but let the layer slide down it.

The missing no-slip half was added in `CS_PbfUpdateVelocity.hlsl`: the solid is a neighbour
with zero velocity and the same missing weight `D(d)` as the density pass:

```hlsl
vi *= max(0.0f, 1.0f - WallFriction * ViscosityC * boundary);
```

`wallFriction` is a config key and GUI slider (shipped 8; 1 = the fluid's XSPH coefficient).
Measured at 1800 frames:

| `wallFriction` | Surface \|v\| | Wall layer \|v\| | Lake level |
|---|---|---|---|
| 0 (free slip) | 0.130 | 0.335 | — |
| 1 | 0.066 | 0.181 | −0.0445 |
| 2 | 0.047 | 0.125 | −0.0472 |
| 4 | 0.034 | 0.079 | −0.0481 |
| 6 | 0.029 | 0.055 | −0.0499 |
| 8 | **0.023** | **0.040** | −0.0506 |
| 8 + `restDensityScale 0.907` | **0.024** | **0.040** | **−0.0430** |

The last row was shipped. Overall mean |v| fell from 0.117 to 0.019 m/s. Wind at 1 is
invisible; ripples return around 20.

### 3.3 Settle before the drop (3.3)

`PbfFluid::SeedParticles` now uploads only the pool and keeps the relaxed sphere in
`m_pendingSphere`. `Backyard8Scene::OnUpdate` settles for 3 s, then calls
`AppendPendingSphere`, re-publishes the buffers and re-registers the particle system. The
water start logs:

```
[PBF] Seeded 22124 pool particles up to y=-0.31; 15234 relaxed sphere particles pending
[PBF] Appended 15234 relaxed sphere particles after settling: 37358 total
[Backyard8] Pool settled (3 s simulated) in 253 ms
```

`prefillLevel` became **−0.27 m**. At 2×3 the water level was −0.0406 m; frame 6 showed a
flat settled pool with the sphere still in the air and no lattice rows.

### 3.4 Water lost over the rim (3.5) — measured, not changed

At the 2×3 Phase 1 config (`prefillLevel −0.27`), 1800 frames:

| Sphere | Culled | Alive | Level |
|---|---|---|---|
| h 3.08, r 2.00 (shipped) | 10,340 | 27,018 | −0.0406 m |
| h 2.00, r 2.00 | 8,698 | 28,660 | +0.0527 m |
| h 1.50, r 2.00 | 7,688 | 29,670 | +0.1096 m |
| h 3.08, r 1.80 | 7,403 | 25,846 | −0.1070 m |
| h 3.08, r 1.60 | 4,647 | 25,276 | −0.1397 m |

A lower sphere culls less but retains more water, so the level rises and `prefillLevel` has
to drop; a smaller sphere has less water and settles lower. No config-only combination
removes the loss without recycling, and the 3.08 m / 2 m drop is the intended visual.
Recycling remains optional.

### 3.5 Frame-24 determinism (3.8)

The wind gate moved from a float density threshold to the integer neighbour count
(`Lambda[i].w`). Measured on the settled lake: surface median 24 (p90 26), interior median
31; the gate uses `< 27`. The handoff's suggested `< 22` would have rejected most of the
surface.

Two frame-24 runs were still not bit-identical:

| Config | Differing pixels | Max channel delta |
|---|---|---|
| Phase-1 config | 4,915 / 518,400 | 171 |
| Original config | 9,255 / 518,400 | 218 |

The remaining source is the neighbour grid's atomic insertion order: the density sum sees
the same neighbours in a different order and float addition is not associative. The wind
gate was not the only cause. Bit-exactness needs deterministic ordering inside each grid
cell (sort the cell's indices before summing), which is a separate performance trade-off.
Regression therefore uses tolerances, not pixel identity.

### 3.6 Phase 1 verification and commits

- Linux build pass.
- `--game-selftest` 78 PASS / 0 FAIL.
- `check_gl_twins.py` PASS (53 kernels / 48 twins); `check_fluid_constants.py` PASS.
- Vulkan frame 24 screen-space matched the new table to the pixel; frame 300 within the
  documented GPU-atomic variation.
- Phase 1 commits: `6572247` (code/config/docs), `0e4e6d9` (3.5 measurements).

---

## 4. Backend port and verification

### 4.1 Windows host/session setup

The Windows host has an RTX 4080 Laptop GPU and the console session `1` is active
(2026-11-10; in the 2026-11-11 port of 6.3 session 1 was disconnected and rendering still
worked). The SSH process runs in session `0`, where DXGI cannot create a swapchain
(`CreateSwapChain failed hr=0x887A0022`). All GPU captures were therefore run in session 1
through a scheduled task with an interactive principal:

```powershell
$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
  -Argument '-NoProfile -ExecutionPolicy Bypass -File <capture-script>.ps1'
$principal = New-ScheduledTaskPrincipal -UserId 'CamusStrix\camus' `
  -LogonType Interactive -RunLevel Highest
Register-ScheduledTask -TaskName B8Captures -Action $action -Principal $principal -Force
Start-ScheduledTask -TaskName B8Captures
```

`$env:USERDOMAIN` is `WORKGROUP` over SSH; the principal must use
`$env:COMPUTERNAME\$env:USERNAME` (`CamusStrix\camus`). The captures use `--unattended`, so
windows do not steal focus.

The Windows checkout's git HEAD was unrelated/old. The source tree was synced from the Linux
HEAD by tarball (including `Assets/Shaders`, `Assets/Scenes` and the Backyard assets), not
pushed. The first build stopped on the Dawn package hash check because
`ShaderPreprocessor.h` changed; rerunning the sanctioned
`scripts\SetupDawn.ps1 -Architecture x64` refreshed the cached Dawn/vcpkg metadata. The build
then succeeded with `scripts\build.ps1 -Config Release -Platform x64 -Action Build`.

### 4.2 D3D12 and D3D11 (native Windows)

Nine captures per API (screen space, water, spheres at 24/90/300), 960×540, water start:

- Frame 24: 219,017–219,022 px vs Vulkan 219,007–219,014 (+0.01%); bbox exact; mean within
  1.3 levels.
- Frames 90/300: pixel delta 0.1–0.9%, mean within 1.3 levels.
- Frame-90 bbox is splash-dependent; see §4.5.
- 4K: `Fluid Clear` 480 × 270, no `DispatchTooLarge`.
- Selftest: 78 PASS / 0 FAIL on both APIs.
- D3D11 FXC compiled all ten PBF pipelines, including the new `SolidBoxes` bindings on
  density and update-velocity; no compile failure and no graphics fallback.
- No `Shader compile failed`, `using graphics implementation`, `compute dispatch failed`,
  `cannot build constants`, or incomplete-binding-layout messages.

D3D12/D3D11 docs commit: `7e28cef`.

### 4.3 GL on Windows

Native desktop **OpenGL 4.6, NVIDIA 576.52** (not ANGLE; the “ANGLE/GLES3” line in the log is
only the depth-export warning).

- Nine captures + 4K + selftest: all pass; every fluid pass dispatched.
- Frame 24: 219,095–219,102 px vs Vulkan 219,007–219,014 (+0.04%), bbox exact, mean
  0.04–1.9 levels.
- Frames 90/300: pixel delta 0.1–0.8%, mean ≤2.6 levels.
- 4K: `Fluid Clear` 480 × 270, no `DispatchTooLarge`.
- Selftest: 78 PASS / 0 FAIL.

GL docs commit: `84bf60e`.

### 4.4 GL on Linux

The OS driver is present:

```
GL_VENDOR:   NVIDIA Corporation
GL_RENDERER: NVIDIA RTX PRO 6000 Blackwell Workstation Edition/PCIe/SSE2
GL_VERSION:  4.6.0 NVIDIA 615.71.09
```

A minimal EGL pbuffer probe initialises and reports that renderer. `libGLX_nvidia.so.0`,
`libEGL_nvidia.so.0` and `libGL.so.1` are installed.

The **engine's Linux build has no GL backend**:

- `Framework/CMakeLists.txt` adds `src/video/gl/*` only under `if(WIN32)`.
- `LinuxFramework.cpp:417` unconditionally creates `VulkanDriver`.
- There is no `USING_OPENGL` define in the Linux build.
- `--api gl` logs a `[gl]` session tag, then runs Vulkan; the dump goes to `dumps_vulkan_*`.

The `GLDriver.cpp` `OS_LINUX` path is GBM/EGL **GLES3 surfaceless**, not the desktop GL 4.3
path the GLSL-430 twins need. Enabling Linux GL is a backend port: compile the GL sources on
Linux, add a windowed GLX/EGL context and GL driver selection in `LinuxFramework`, link
GL/GLEW/EGL, and implement swapchain/present. It is not a build flag.

### 4.5 Frame-90 splash bbox is not a port metric

The frame-90 splash top varies run to run even on the same backend:

| Backend | Frame-90 bbox y_min |
|---|---|
| Vulkan runs | 81, 115, 115 |
| D3D12/D3D11 | 78–119 |
| GL | 73–101 |

Pixel count and mean are stable; the bbox top is a chaotic splash statistic. The regression
gate uses **55 px** for frame 90 and tight gates for frames 24 and 300.

### 4.6 UTF-8 BOM pitfall

PowerShell 5.1 `Set-Content -Encoding UTF8` writes a BOM. The glaze parser rejects it with
`1:1: expected_brace` for `--fluid-config`. Configs generated for captures are written with
`[IO.File]::WriteAllText(..., New-Object System.Text.UTF8Encoding($false))`.

---

## 5. 2×6 solver experiment and adoption

### 5.1 Density-error versus cost

Measured on the settled lake at 1800 frames, mean `|ρ/0.907 − 1|`. Frame times are whole-frame
1080p splat medians and are noisy; use them as approximate.

| Config | Density error | p95 | Lake frame | Water frame |
|---|---|---|---|---|
| 2×3 (old) | 2.50% | 4.6% | 2.19 ms | 2.34 ms |
| **2×6 (shipped)** | **1.27%** | 2.3% | ~2.67 ms | ~3.18 ms |
| 2×9 | 0.87% | 1.6% | — | — |
| 2×12 | 0.66% | 1.2% | — | ~3.73 ms |
| 4×3 | 0.67% | 1.2% | ~2.65 ms | — |
| 4×4 | 0.51% | 1.0% | — | — |
| 6×3 | 0.31% | 0.6% | ~2.67 ms | — |
| 2×3, `stiffnessEps 0.02` | 2.49% | 4.6% | — | — |

`stiffnessEps` was not the lever. More substeps/iterations are.

### 5.2 Level retune

| Config | Lake seed | Lake level | Water seed | Water level |
|---|---|---|---|---|
| 2×3 old | `resting 0` | −0.0433 | `prefill −0.27` | −0.0406 |
| **2×6** | `resting −0.02` | **−0.0440** | `prefill −0.27` | **−0.0379** |
| 2×12 | `resting −0.03` | −0.0414 | `prefill −0.27` | −0.0375 |
| 4×3 | `resting −0.03` | −0.0420 | `prefill −0.41` | −0.0360 |
| 6×3 | — | — | `prefill −0.31` | +0.0622 (needs ~−0.55) |

4×3 and 6×3 were rejected because hitting the water level requires a visibly half-empty
pool before the sphere lands. 2×12 was rejected as the default because it costs about
+1.4 ms on the water start and needs the GUI iteration slider maximum raised from 10 to 12;
it remains a quality option.

**Shipped:** 2 substeps × 6 iterations, lake `restingLevel −0.02`, water `prefillLevel −0.27`,
`restDensityScale 0.907`, `wallFriction 8`, `windStrength 1`.

Final measured with the shipped JSON:

- Lake level: **−0.0438 m**.
- Water level: **−0.0398 m**.
- Mean density error: **1.27%**.
- Selftest: 78 PASS / 0 FAIL.

### 5.3 Why not IPBF yet

The IPBF paper (Diaz et al., SIGGRAPH Asia 2025) reaches ~0.1% density error with a
second-order per-particle Hessian/Newton solve. The 2×6 experiment shows a large part of the
PBF error was simply under-convergence: 2.50% → 1.27% for a small cost, and 0.66% at 2×12,
without changing the integrator or boundary model. A full IPBF rewrite is therefore not
needed for this scene; it remains the option if sub-0.5% error at fewer iterations or 1/30 s
stability becomes necessary. If implemented, the analytic Akinci boundary and no-slip term
must be carried into the Hessian.

---

## 6. Final references and all-backend verification

### 6.1 Canonical Vulkan references (2×6, 960×540, water start)

| Mode | Frame | Pixels | Bounding box | Mean RGB |
|---|---|---|---|---|
| screen space | 24 | 220,002 | x[0,959] y[0,436] | (70.9, 140.4, 150.3) |
| screen space | 90 | 217,145 | x[0,959] y[119,440] | (73.7, 128.1, 140.9) |
| screen space | 300 | 127,013 | x[0,959] y[278,440] | (69.4, 136.1, 155.5) |
| water | 24 | 220,009 | x[0,959] y[0,436] | (63.6, 132.8, 144.3) |
| water | 90 | 216,933 | x[0,959] y[84,440] | (70.9, 121.4, 134.1) |
| water | 300 | 126,665 | x[0,959] y[279,440] | (57.8, 131.1, 149.7) |
| spheres | 24 | 220,005 | x[0,959] y[0,436] | (213.9, 235.8, 237.8) |
| spheres | 90 | 215,438 | x[0,959] y[82,440] | (226.3, 244.2, 246.0) |
| spheres | 300 | 126,547 | x[0,959] y[279,440] | (239.4, 253.9, 254.5) |

These are also in `T850/scripts/validation/backyard8_references.json` and the port guide,
section 7.

### 6.2 Final Windows re-run (2×6)

All 27 captures pass (screen space, water, spheres at 24/90/300 for D3D12, D3D11, GL):

| API | Max pixel delta | Max mean delta | Frame-24 bbox |
|---|---|---|---|
| D3D12 | 0.64% | 2.91 levels | exact |
| D3D11 | 1.25% | 2.92 levels | exact |
| GL | 0.81% | 2.06 levels | exact |

Frame-24 fluid layer (Vulkan reference in parentheses):

| API | Screen space | Water | Spheres |
|---|---|---|---|
| D3D12 | 220,003 (220,002) | 220,001 (220,009) | 219,994 (220,005) |
| D3D11 | 220,006 (220,002) | 220,004 (220,009) | 220,001 (220,005) |
| GL | 220,044 (220,002) | 220,041 (220,009) | 220,044 (220,005) |

Also verified:

- 4K `Fluid Clear` 480 × 270 on all three Windows APIs; no `DispatchTooLarge`.
- Selftest 78 PASS / 0 FAIL on D3D12, D3D11 and GL.
- No shader compile failures, graphics fallbacks, dispatch failures, constant-build
  failures, or incomplete binding layouts.
- D3D11 FXC compiled all ten PBF pipelines, including the new `SolidBoxes` bindings.

### 6.3 Windows port, 2026-11-11 (five APIs)

The 83 commits after `7e28cef` had never run on Windows: whitewater, environment-cube
reflections sampled in a compute kernel, G-buffer lighting in the shade, caustics, the
surfel `FluidNormal` target and the coarse thickness grid. All five APIs were checked on the
RTX 4080 Laptop GPU at 1920x1080, `--regressionFixedDt 0.0166667 --postProcessMode compute`.

Method: eight cells (`ss|surfel` × `default|close` × `water150|lake300`) per API, and a cell
is judged against the **run-to-run noise of Windows Vulkan on the same machine** (r1 against
r2: mean absolute difference per channel, p99, % of pixels above 4), never against zero.
`scripts/validation/backyard8_parity_compare.py --ref win` produces the verdicts, the contact
sheet and per-cell diff images.

| API | Gate | Self-test | Water cells | Lake cells | Log findings |
|---|---|---|---|---|---|
| Vulkan | PASS | 78 PASS | reference | reference | 0 |
| D3D12 | PASS | 78 PASS | at noise | 0.45-0.64 above noise (same as Vulkan vs Linux) | 0 |
| D3D11 | PASS | 78 PASS | at noise | 0.50-0.72 above noise | 0 |
| GL | PASS | 78 PASS | at noise | 0.65-1.05 above noise; p99 11 (band below) | 0 |
| WebGPU | PASS | 78 PASS | at noise | 0.44-0.67 above noise | 0 |

The lake cells' residual is a cross-compiler solver offset that every API carries equally,
accepted as variance; all 16 water cells are at noise and the contact sheets for both
renderers look identical.

Fixes, one per root cause:

| Commit | Change |
|---|---|
| `a7f40b2` | D3D12: cube textures (`DepthOrArraySize == 6`) pass dispatch validation |
| `abaddb8` | D3D11: a `TEXTURECUBE` SRV is accepted for a cube layout |
| `29ed39f` | GL: drop duplicate combined-sampler aliases, accept and restore cubes |
| `0e2a332` | WebGPU: bit-pattern finiteness, because Tint aborts on `OpIsNan` |
| `5e04cac` | WebGPU: accept the 6-layer cube in dispatch validation |
| `6d601b2` | WebGPU: request the adapter's storage-buffer limits (4K is 165,888,000 B) |

The GL-only band at rows 582-584 is the coping's striped front face, shifted by about two
rows: it is already present in `RT_Dump_GBuffer_Albedo` with `FluidParticles` empty, so it is
a pre-existing GL scene difference, not the port.

Full numbers, the per-cell table and the evidence paths are in
[backyard8-windows-port-work-orders.md](backyard8-windows-port-work-orders.md) under
"Results (final)".

---

## 7. Non-interactive cleanup items

### 7.1 3.7 — re-seed debounce

Seeding sliders now schedule a re-seed **0.2 s after the drag stops** instead of rebuilding
the solver every frame:

- `Backyard8Scene.h`: `float m_reseedDelay = -1.0f;`
- `Backyard8Scene::DrawSolverPanel`: `if (seedChanged) m_reseedDelay = 0.2f;`
- `Backyard8Scene::OnUpdate`: decrements the delay and calls `ReinitializePbf()` when it
  expires.
- `ReinitializePbf()` cancels a pending delay so Restart/start-mode changes stay immediate.

Build and selftest pass. The drag itself still needs a GUI hand test.

### 7.2 3.10 — small items

- Removed the unused `const double k = ...` in `LatticeGradSq()`.
- `[ParticleProbe]` now looks up the system by the `__particle_projected_<name>` suffix via
  `ParticleSystemRegistry::Find` and scans only `min(elements, buffer_count)` instead of the
  full 131,072-entry capacity. Tiles still scan the tile count.
- Wireframe/flat (styles 1/2) moved to the nearest-particle `CS_FluidSphereShade` path:
  - style 1: anti-aliased ring where `rho2 > (1 − 2/r)²`, transparent inside;
  - style 2: hard flat disc, alpha = opacity.
  `Backyard8Scene::ApplyRenderModeToGraph` now enables the sphere-shade path for every
  alpha-blended splat style; the legacy per-particle resolve remains only for additive
  blending.
  `ComputeKernelRegistry.cpp` now passes `static_cast<uint32_t>(system->style)` instead of
  mapping every non-spheres style to 0 (water); this was required for styles 1/2 to reach
  their branches.
  Ring/flat visuals were checked on Vulkan; the GL twin was regenerated and compile-checked.

### 7.3 4.5 / 4.6 — automated regression

Added:

- `T850/scripts/validation/backyard8_references.json` — the 2×6 Vulkan references,
  tolerances, and water-level target.
- `T850/scripts/validation/backyard8_regression.py` — runs the nine captures, computes
  pixel/bbox/mean statistics, compares to the JSON, then runs lake and water to 1800 frames
  and checks the level. It is API-parameterised (`--api`) and cross-platform
  (`--day-scene` to point at the executable).
- `T850/scripts/validation/pbf_water_level.py` — standalone level checker.

Vulkan run:

```
PASS screen-space f24/f90/f300
PASS splat        f24/f90/f300
PASS spheres      f24/f90/f300
PASS level lake : -0.0435 m
PASS level water: -0.0379 m
RESULT: PASS
```

The runner was tested on Vulkan. The same capture matrix was verified on Windows via the
session-1 PowerShell scripts; the Python runner itself was not executed on Windows in this
session.

---

## 8. Final verification matrix

| Check | Result |
|---|---|
| Linux build (`cmake --build build/linux --target DayScene`) | pass |
| Vulkan `--game-selftest` | 78 PASS / 0 FAIL |
| `check_gl_twins.py` | PASS, 53 kernels / 48 twins |
| `check_fluid_constants.py` | PASS, 35 counts |
| `CS_FluidSphereShade.glsl` `glslangValidator` | pass |
| `backyard8_regression.py --api vulkan` | PASS |
| D3D12 final Windows captures | 9/9 pass |
| D3D11 final Windows captures | 9/9 pass |
| GL final Windows captures | 9/9 pass |
| Windows 4K, three APIs | 480×270 clear; no errors |
| Windows selftest, three APIs | 78 PASS / 0 FAIL each |
| D3D11 FXC | 10/10 PBF pipelines; no fallback |
| Linux GL driver | NVIDIA OpenGL 4.6; EGL probe pass |
| Linux GL engine backend | absent; `--api gl` falls back to Vulkan |

Note: the nine 960×540 captures per API are the reference set; the 4K and selftest runs are
separate. Across the three APIs the total is 27.

---

## 9. Remaining work

| Item | Status / reason |
|---|---|
| 3.2 Dev GUI by hand | Needs a person at the desktop; controls apply in code but no slider was dragged in a test |
| 3.5 recycling | Optional; final water start culls 10.7k, level correct; recycling needs a frame counter/ABI change and a visual pop check |
| 3.8 frame-24 bit-exactness | Not achieved; grid atomic insertion order changes the density sum. Needs deterministic cell ordering, a performance trade-off |
| 3.9 Steam Deck | No hardware; desktop 1080p 2×6 is ~2.2 ms lake / ~2.7 ms water, but Deck build/package and 1280×800 measurement remain |
| 4.1 save GUI state | Not implemented; needs a GUI test and a decision on writing source assets vs runtime copy |
| 4.2 ball coupling | Feature work; moving sphere boundary + reaction impulse |
| 4.3 half-res / IBL / curvature flow | Feature work; current SSFR is full-res a-trous + screen-space reflection |
| 4.4 spray/foam | Feature work; current foam is a screen-space spray fade |
| Linux GL | Backend port; OS driver exists, engine CMake/`LinuxFramework` do not compile/select GL |

---

## 10. Reproduction

### 10.1 Linux build and tests

```bash
cd /home/camus000/testBitso/T850
cmake --build build/linux --target DayScene --parallel 24

cd bin/SteamDeck/Release
SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu \
  ./DayScene --unattended --api vulkan --game-selftest
```

### 10.2 Automated regression

```bash
cd /home/camus000/testBitso/T850
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu SDL_VIDEO_DRIVER=x11 \
  python3 scripts/validation/backyard8_regression.py --api vulkan
```

Skip captures or levels:

```bash
python3 scripts/validation/backyard8_regression.py --skip-level
python3 scripts/validation/backyard8_regression.py --skip-captures
```

Standalone level:

```bash
python3 scripts/validation/pbf_water_level.py /tmp/b8/pbf_f1800.csv
```

### 10.3 Single Vulkan capture

From `T850/bin/SteamDeck/Release`:

```bash
SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu \
  ./DayScene --unattended --api vulkan --scene 8 --backyard-no-hud \
  --backyard-start water --width 960 --height 540 \
  --regressionFixedDt 0.0166667 --postProcessMode compute \
  --fluid-render screen-space --dump-frame 24 --logLevel info
```

For spheres, copy `Assets/Scenes/Backyard8Fluid.json` with
`particles.renderMode = 0` and `particles.style = 3`, then pass
`--fluid-config <absolute path>`.

### 10.4 Windows build and captures

1. Sync the source tree (the checkout's git HEAD is old; a tarball is sufficient).
2. From `T850\`:
   ```powershell
   .\scripts\SetupDawn.ps1 -Architecture x64   # only when the Dawn audit says so
   .\scripts\build.ps1 -Config Release -Platform x64 -Action Build
   ```
3. Run captures in the active console session, not the SSH session. Create an interactive
   scheduled task (principal `CamusStrix\camus`, `-LogonType Interactive`) that runs a
   PowerShell capture script; `--unattended` prevents focus stealing.
4. Use `--api d3d12`, `d3d11` or `gl`. The capture matrix is screen space / splat / spheres at
   frames 24/90/300, plus one 4K run and the selftest.
5. Compare `RT_Dump_FluidParticles.ppm` against the reference table or run the Python
   regression runner with `--day-scene <path>\DayScene.exe` if Python is available.

---

## 11. File map (this session)

| Area | Files |
|---|---|
| Solver | `Framework/src/simulation/PbfFluid.cpp`, `Framework/include/simulation/PbfFluid.h` |
| PBF kernels | `Assets/Shaders/CS_PbfIntegrate.hlsl`, `CS_PbfDensity.hlsl`, `CS_PbfSolve.hlsl`, `CS_PbfUpdateVelocity.hlsl`, `CS_PbfCull.hlsl` + GL twins |
| Constants/bindings | `Assets/Shaders/compute_kernels.json` |
| Scene/config | `DayScene/Backyard8Scene.cpp`, `.h`, `DayScene/Backyard8FluidJson.h`, `Assets/Scenes/Backyard8Fluid.json` |
| Renderer/style | `Assets/Shaders/CS_FluidSphereShade.hlsl` + `.glsl`, `Framework/src/utils/ComputeKernelRegistry.cpp`, `Framework/src/scene/RenderGraph.cpp` |
| Validation | `T850/scripts/validation/backyard8_references.json`, `backyard8_regression.py`, `pbf_water_level.py` |
| Documentation | `documentation/scenes/backyard8-fluid-summary-and-next-steps.md`, `backyard8-fluid-render-d3d-port.md`, this file |

---

## 12. Caveats

- Frame-time numbers are whole-frame wall-time medians at 1080p and are noisy (±0.2 ms);
  they are not GPU-timestamp measurements.
- The density-error metric is mean `|ρ/0.907 − 1|` from the particle dump. It is useful for
  relative comparisons but is not directly comparable to the IPBF paper's metric.
- Frame 24 is not bit-exact; frame 90 bbox is chaotic. The regression gate uses tolerances,
  not pixel identity.
- The Windows checkout was source-synced, not pushed; its git HEAD remains old. The Linux
  repository contains the commits listed above.
- GUI controls (seeding debounce, styles 1/2, save state) are code-complete but were not
  hand-dragged in this session.
