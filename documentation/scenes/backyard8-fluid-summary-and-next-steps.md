# Backyard Scene 8 (PBF water): Work Summary and Next Steps

Date: 2026-11-09, updated 2026-11-10. Platform verified: **Vulkan on Linux** (Ubuntu, RTX PRO 6000, driver 615);
**D3D12, D3D11 and GL natively on Windows** (RTX 4080 Laptop) were added later. **Linux GL is
not supported by the current Linux build** (Vulkan-only), although the OS driver is present.

This is the record of one working session on the particle-fluid scene. The commit that adds this
file also contains the diagnosis-3 implementation that other agents had left uncommitted on top
of `6658d56`, because the two are interleaved in the same files.

Detailed references:

- [backyard8-session-2026-11-10.md](backyard8-session-2026-11-10.md): everything done after this
  handoff — Phase 1 solver, Windows D3D12/D3D11/GL verification, 2×6 adoption, regression
  automation, and the remaining blockers.
- [backyard8-fluid-render-d3d-port.md](backyard8-fluid-render-d3d-port.md): what changed in the
  renderer, the kernel ABI, the pass gating, the D3D11/D3D12 port steps and the Vulkan reference
  numbers. **Start here for the port.**
- [backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md): pipeline and the gap to
  the curvature-flow paper.
- [diagnosis-3.md](diagnosis-3.md), [diagnosis-3-implementation.md](diagnosis-3-implementation.md):
  the plan and the earlier implementation record.
- `T850/BUILD-UBUNTU.md`: build, run, window placement, start modes.

## 1. What was done

### 1.1 Screen-space renderer (the default look)

The diagnosis-3 implementation rendered lumpy "cellophane" water. Fixed:

| Problem | Fix |
|---|---|
| Dark seams and glassy ridges: the smoothing averaged separate layers of water together | `CS_ParticleSmooth` is an edge-aware a-trous filter (spacing 1, 2, 4, 8, 16, then 1) with a fixed world radius (0.3 m) and a layer threshold (0.3 m) |
| Ghostly, half-transparent water: thickness was used as opacity | Alpha is silhouette coverage only |
| No visible surface on the settled pool: "reflection" was another refraction sample | Screen-space reflection marched against the scene depth, with the on-screen sky (then the frame's top row) as the miss colour |
| Refraction bent the wrong way and pulled the coping into the water | Snell refraction (eta 1/1.33) over `thickness * refraction` (max 2 m), rejecting samples in front of the water |
| Radial streaks and saturated cyan | Thickness blur, absorption multiplier 4 to 2 |
| Spray drawn as grey discs | Isolated droplets fade to lit whitewater (`foam`) |
| `check_fluid_constants.py` failed | It now sizes HLSL vectors/arrays correctly and scans `CS_Particle*` too |

### 1.2 Particle renderer (the "splat" mode)

- The old splat averaged every particle into one flat, unlit tint. It is replaced by
  `CS_FluidSphereShade`, which has two styles:
  - **water** (style 0, default): the particles blend into water through their summed thickness.
    The thickness acts as a height field for the normal, using a dense 9x9 binomial gradient,
    because sparse taps aliased the particle grid into moire. It reuses the screen-space optics
    (refraction, absorption, Fresnel sky reflection, sun glints), and the edges fade with
    thickness. Through deep water the refracted ray bends less, because there the gradient is
    mostly particle noise.
  - **spheres** (style 3, debug): one lit opaque sphere per particle, in depth order.
- The panel's style menu is: water (blended), wireframe, flat, spheres (lit particles).
- `CS_FluidSplat` sums thickness in particle mode as well.
- The sphere kernel has 56 constants and new bindings. On D3D12 it needs 66 root DWORDs, so its
  constants go in a root CBV (already supported).

### 1.3 Solver seed, start modes and water level

- **Relaxed sphere seed.** The falling sphere used to be a perfect cubic grid, whose rows showed
  as terraces, a serrated edge and moire until the impact. It is now jittered and relaxed on the
  CPU with the solver's own density projection (26 iterations, about 27 ms, compression 0.57 to
  0.002), so it falls cleanly and without a pop.
- **Why scene 8 was low.** The scene dropped a single 2 m sphere (33.5 m3) into an empty pool. The
  water plane needs about 60 m3; 8% splashed out and was culled. A sphere big enough does not
  fit: the pool is only 4.5 m wide. More particles at a finer spacing do not add water either:
  the sphere radius sets the volume.
- **Resting pool seed and start modes** (`PbfFluid::SeedParticles`, `scene.startMode`,
  `--backyard-start`):

  | Start | Seed | Particles | Settled level (15 s / 30 s) |
  |---|---|---|---|
  | `lake` (default), `balls` | pool to `restingLevel` +0.16 m, settled for 3 s before the first frame | 29,880 | -0.045 / -0.039 m |
  | `water` | pool to `prefillLevel` +0.13 m, plus the 2 m sphere | 44,573 (a third is culled) | -0.056 / -0.035 m |

  Scene 7's water plane is at -0.040 m. Both seeds sit above it on purpose: the seeded grid
  compacts by about one particle layer (~16 cm) as it settles. `PbfFluid::Settle` runs the
  resting pool with speeds capped at 0.3 m/s and no wind (about 200 ms at load), so you never
  see it sink. The default `maxParticles` is now 50,000.

### 1.4 Dev GUI: every setting applies at once

- All controls in the three "Backyard 8" panels apply immediately. The seeding controls rebuild
  the solver and re-seed: spacing, smoothing ratio (previously not wired at all), both pool
  levels, sphere radius and height, max particles, heightfield size. This takes about 30 ms per
  change, or about 250 ms in the resting starts because of the settle.
- The start-mode menu now changes the start; before, all three entries dropped the same sphere.
- "Apply & re-seed" is gone; "Restart (re-seed the water)" re-seeds.
- **Bug fixed:** the graph's projected-particle table was sized once at load, so raising the
  particle count while running would have overflowed it. `ParticleSystemDesc::buffer_capacity`
  now sizes it (scene 8: 131,072, the scatter key's 17-bit index range); the max-particles
  slider stops there.

### 1.5 4K and windowing

- **4K crash:** `Fluid Clear` dispatched one thread per pixel as a 1D grid: 129,600 groups at
  3840x2160, above the 65,535 limit. The graph now dispatches `"$window"` buffers as width x
  height, and the clear kernel uses 8x8 groups. Verified at 3840x2160: 480 x 270 groups, no
  errors.
- **Window placement (Linux):** windows are created centred on the primary display (the 4K
  monitor here). Native Wayland ignores positions. Automated captures therefore set
  `SDL_VIDEO_DRIVER=x11`. Forcing XWayland in the engine for `--unattended` runs was tried and
  reverted: at 1.25 scaling XWayland shows windows and ImGui at 0.625 size.

### 1.6 Documentation

- New: [backyard8-fluid-render-d3d-port.md](backyard8-fluid-render-d3d-port.md) and this file.
- New: `T850/scripts/shaders/generate_gl_compute_twins.py`, the Linux generator of the GL twins
  used for this work (same output and banner as the Windows `.ps1`; section 5.3).
- Updated: [backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md),
  [diagnosis-3-implementation.md](diagnosis-3-implementation.md) (superseded note),
  [../README.md](../README.md), `T850/BUILD-UBUNTU.md`.

## 2. Verification (Vulkan/Linux; D3D12/D3D11/GL Windows)

| Check | Result |
|---|---|
| `cmake --build build/linux --target DayScene` | pass |
| `DayScene --unattended --api vulkan --game-selftest` | 78 PASS, exit 0 |
| `scripts/validation/check_gl_twins.py --shaders Assets/Shaders` | PASS (53 kernels, 48 generated twins) |
| `scripts/validation/check_fluid_constants.py` | PASS (10 cbuffer names, 35 constant counts) |
| Captures, three renderers x frames 24/90/300, `--backyard-start water` | visually correct; numbers in the port guide, section 7 |
| 3840x2160, screen space and water | no `DispatchTooLarge`, no errors |
| Settled water level, both starts | -0.035 to -0.045 m (scene 7: -0.040 m) |
| Frame time, 1920x1080, unpaced | resting pool 2.7 ms, drop 3.2 ms (was 2.3 ms with 15k particles) |
| Real-time GUI controls | **code review only**; no slider was dragged in a test |
| D3D12 native Windows, 9 captures | PASS on pixels/mean; frame-90 bbox within Vulkan's own spread (port guide §10) |
| D3D11 native Windows, 9 captures | PASS on pixels/mean; frame-90 bbox within Vulkan's own spread (port guide §10) |
| D3D12/D3D11 4K | `Fluid Clear` 480 x 270, no `DispatchTooLarge` |
| D3D12/D3D11 selftest | 78 PASS / 0 FAIL |
| D3D11 FXC | 10/10 PBF pipelines, no compile failure or graphics fallback |
| GL native Windows, 9 captures | PASS on pixels/mean; frame-90 bbox within Vulkan's spread (port guide §10) |
| GL 4K / selftest | 480 x 270 clear; 78 PASS / 0 FAIL |
| Linux GL | OS driver present (NVIDIA OpenGL 4.6, 615.71.09); engine Linux build has no GL backend, `--api gl` falls back to Vulkan |

## 2.1 Phase 1 results (2026-11-09)

Baseline tagged `backyard8-phase0-baseline` (commit `6fe9aa3`) before any Phase 1 edit. All
numbers below are Vulkan on the RTX PRO 6000, fixed 1/60 s.

### 3.4 Free-surface compaction — done with `restDensityScale 0.907`

With `restingLevel: 0` and `startMode: lake`, the settled level against the rest-density target
(1800 frames, measured from the particle dump):

| `restDensityScale` | Settled level |
|---|---|
| 1.000 | −0.191 m |
| 0.980 | −0.162 m |
| 0.960 | −0.129 m |
| 0.940 | −0.093 m |
| 0.920 | −0.055 m |
| 0.912 | −0.041 m |
| 0.911 | −0.039 m |
| 0.907 + wall no-slip 8 | **−0.043 m** |

The suggested 0.98–0.94 range is not enough; the target is ~0.91. The shipped value is
**0.907**, which also covers the small level drop from the wall no-slip term. `restingLevel` is
now **0.0** in the JSON and the C++ defaults. Stability held across the sweep: `rhoMean` tracks
the target and `maxSpeed` stayed below 0.9 m/s. Experiment B (relaxed-block kernel
normalisation) was not needed because the splash at 0.907 matches the 1.0 reference at frames
24/90/300.

### 3.6 Pool never still — wind fixed, but wind was not the whole cause

`CS_PbfIntegrate` now uses physical air drag
(`WindStrength * 1.56e-6 * |rel.xz| / ParticleRadius`), `windStrength` is a config key and a
GUI slider (0–50). Wind by itself did **not** make the pool still: `windStrength` 1 and 20 gave
the same mean speed. The dump showed a persistent **wall-driven circulation** — ~0.335 m/s
downward in the one-particle wall layer, ~0.13 m/s at the surface — present at both rest
densities. The analytic boundary was free-slip: it kept particles out of the wall but let the
wall layer slide down it.

Added the missing no-slip half of the boundary model in `CS_PbfUpdateVelocity.hlsl`: the solid
is a neighbour with zero velocity and the same missing weight `D(d)` as the density pass, so
`v_i += c * D(d) * (0 - v_i)`. The strength is `solver.wallFriction` (1 = the fluid's XSPH
coefficient; 8 shipped). Measured on the settled lake (1800 frames):

| `wallFriction` | Surface \|v\| | Wall layer \|v\| | Lake level |
|---|---|---|---|
| 0 (free slip) | 0.130 | 0.335 | — |
| 1 | 0.066 | 0.181 | −0.0445 m |
| 2 | 0.047 | 0.125 | −0.0472 m |
| 4 | 0.034 | 0.079 | −0.0481 m |
| 6 | 0.029 | 0.055 | −0.0499 m |
| 8 | **0.023** | **0.040** | −0.0506 m |
| 8 + `restDensityScale` 0.907 | **0.024** | **0.040** | **−0.0430 m** |

The last row is shipped: overall mean \|v\| 0.019 m/s, surface 0.024 m/s (target <0.03), lake
level −0.043 m. Wind at 1 is invisible; ripples return around 20.

### 3.3 Settle the pool before the drop — done

`PbfFluid::SeedParticles` now uploads the pool only and keeps the relaxed sphere in
`m_pendingSphere`; `Backyard8Scene::OnUpdate` settles for 3 s and calls
`AppendPendingSphere`, then re-publishes and re-registers the particle system. The water start
logs:

```
[PBF] Seeded 22124 pool particles up to y=-0.31; 15234 relaxed sphere particles pending ...
[PBF] Appended 15234 relaxed sphere particles after settling: 37358 total
[Backyard8] Pool settled (3 s simulated) in 253 ms
```

`prefillLevel` is **−0.27 m** (the retained part of the drop brings the pool back to the
plane). Measured at 1800 frames: **level −0.0406 m**, 27,018 alive, 10,340 culled. Frame 6
shows a flat settled pool with the sphere still in the air, no lattice rows. The start-mode
combo also goes through `ReinitializePbf`, so it sets `m_settlePending` and works the same way.

### 3.8 Frame-24 determinism — gate fixed, bit-exactness not achieved

The wind gate now uses the integer neighbour count (`Lambda[i].w > 0.5 && < 27`), measured
from the settled lake (surface median 24, p90 26; interior median 31; the document's suggested
22 would reject most of the surface). Two frame-24 runs are **still not identical**: the new
config differs on 4,915 of 518,400 pixels (max 171 levels), and the original 1.0/prefill-0.13
config differs on 9,255 (max 218). The remaining source is the neighbour grid's atomic
insertion order: the density sum is over the same set in a different order, and float addition
is not associative. Wind gating was not the only cause. Bit-exactness needs a deterministic
order inside each grid cell (sort the cell's indices before summing), which is a separate
performance trade-off; regression must use tolerances for now.

### Verification after Phase 1

| Check | Result |
|---|---|
| `cmake --build build/linux --target DayScene` | pass |
| `--game-selftest` | 78 PASS, 0 FAIL |
| `check_gl_twins.py` | PASS, 53 kernels / 48 twins |
| `check_fluid_constants.py` | PASS, 35 counts |
| Vulkan references, screen space / water / spheres, frames 24/90/300 | re-measured; table in the port guide, section 7 |
| Lake level, 1800 frames | −0.043 m |
| Water level, 1800 frames | −0.0406 m |
| Frame 6, water start | settled pool, no lattice rows |

### Still open from this phase

- **3.5** was measured but not reduced. At the shipped `prefillLevel -0.27`, 1800 frames:

  | Sphere | Culled | Alive | Level |
  |---|---|---|---|
  | h 3.08, r 2.00 (shipped) | 10,340 | 27,018 | −0.0406 m |
  | h 2.00, r 2.00 | 8,698 | 28,660 | +0.0527 m |
  | h 1.50, r 2.00 | 7,688 | 29,670 | +0.1096 m |
  | h 3.08, r 1.80 | 7,403 | 25,846 | −0.1070 m |
  | h 3.08, r 1.60 | 4,647 | 25,276 | −0.1397 m |

  A lower sphere culls less but retains more water, so the level rises and `prefillLevel` has
  to drop further; a smaller sphere has less water and settles lower. None of the config-only
  combinations removes the loss without recycling, and the 3.08 m / 2 m drop is the intended
  visual (the 10k culled particles are water that went over the rim, which is the effect the
  scene is showing). Recycling remains optional.
- **3.7** re-seed debounce, **3.9** Deck budget, **3.10** small items remain.
- **3.1** D3D12/D3D11/GL still need the session-1 Windows run (the host has a console session;
  see the plan).

## 2.2 Final 2026-11-10 pass: 2×6, automation, small items

This section supersedes the earlier 2×3 numbers where they differ.

- **Solver is now 2 substeps × 6 iterations** (was 3). `solverIterations: 6`, lake
  `restingLevel: -0.02`, water `prefillLevel: -0.27`. Measured: mean density error **1.27%**
  (was 2.50%), lake level **−0.0438 m**, water level **−0.0398 m**, selftest 78/0. Cost is
  roughly +0.5 ms lake / +0.8 ms water at 1080p. The 2×12 option reached 0.66% but costs
  ~+1.4 ms and needs the GUI iteration limit raised; 4×3/6×3 were rejected because the water
  start needs a visibly low pre-drop pool.
- **Vulkan references re-frozen** at 960x540 for screen space / water / spheres at frames
  24/90/300. The port guide section 7 and `scripts/validation/backyard8_references.json` are
  the canonical tables.
- **D3D12, D3D11 and GL re-run** on native Windows against the new references: all 27 captures
  pass; max pixel delta 1.25%, max mean delta 2.92 levels, frame-24 bbox exact. Frame-90 bbox
  is chaotic on every backend (Vulkan/GL y_min 73–119), so the gate is 55 px there.
- **4K** `Fluid Clear` 480 x 270, no `DispatchTooLarge`, on all three Windows APIs; selftest
  78/0 each.
- **Automation added:** `scripts/validation/backyard8_regression.py` runs the nine captures and
  the two 1800-frame level checks; `pbf_water_level.py` is the standalone level checker;
  `backyard8_references.json` holds the references and tolerances. Vulkan run: PASS.
- **3.7** seeding sliders now re-seed 0.2 s after the drag stops instead of every frame.
- **3.10** unused `LatticeGradSq` variable removed; `[ParticleProbe]` scans only the live
  particle prefix; wireframe/flat (styles 1/2) moved to the nearest-particle sphere-shade path,
  leaving the legacy resolve only for additive blending.

Remaining: 3.2 needs a human at the GUI; 3.5 recycling is optional (10.7k culled, level
correct); 3.8 bit-exactness needs deterministic cell ordering; 3.9 needs Steam Deck hardware
(desktop 1080p is ~2.2–2.7 ms); 4.1–4.4 are features; Linux GL is a backend port (driver exists,
engine build is Vulkan-only).

## 3. Next fixes (in priority order)

Conventions for every step below:

- Paths are relative to the repository root (`testBitso/`); engine commands run from
  `T850/bin/SteamDeck/Release` on Linux, prefixed with
  `SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` (section 5).
- After **any** HLSL change: regenerate its GL twin (section 5.3), then run
  `python3 T850/scripts/validation/check_gl_twins.py --shaders T850/Assets/Shaders` and
  `python3 T850/scripts/validation/check_fluid_constants.py`. Both must print PASS.
- After any C++ change: `cd T850 && cmake --build build/linux --target DayScene --parallel 24`,
  and confirm the output contains `Linking CXX executable` (a build started right after an edit
  can miss it).
- Before committing: the selftest (`./DayScene --unattended --api vulkan --game-selftest`) must
  report 78 PASS. If captures change, update the reference table in the port guide, section 7.

### 3.1 Run D3D12, D3D11 and OpenGL

> **D3D12, D3D11 and GL done (native Windows):** Release x64 build succeeded; all nine captures
> per API pass; 4K clears 480 x 270; selftest 78 PASS / 0 FAIL; FXC compiled all ten PBF
> pipelines, and GL runs native OpenGL 4.6 (NVIDIA 576.52) with all GLSL twins. The Windows
> checkout was source-synced from this tree, not pushed. **Linux GL remains a port, not a run:**
> the OS driver exists (NVIDIA OpenGL 4.6), but the Linux build compiles GL sources only on
> Windows and `LinuxFramework` always creates Vulkan. See port guide §6.3.

1. Sync the tree to the Windows build checkout. Pushing is not required; a source tarball of
   `T850` plus `Assets/Shaders`, `Assets/Scenes` and the Backyard assets is sufficient.
2. From `T850\`: `.\scripts\build.ps1 -Config Release -Platform x64 -Action Build`. Expect
   `BUILD SUCCEEDED`.
3. From `T850\bin\x64\Release`, run the capture loop of the port guide, section 7, with
   `$api = 'd3d12'`. It already passes `--backyard-start water`.
4. In every `logs\d3d12-*.log`, check each fluid pass logs `enabled compute kernel` and
   `dispatched compute`, and that none of these appear: `Shader compile failed`,
   `using graphics implementation`, `compute dispatch failed`, `DispatchTooLarge`,
   `cannot build constants`, `does not declare the kernel's complete binding layout`.
5. Run the statistics snippet of the port guide, section 7, on each
   `dumps_d3d12_f<frame>_*\RT_Dump_FluidParticles.ppm` and compare with its Vulkan table. Frame
   24 must be within 0.5% pixels, 2 px bounding box, 3 levels per channel; frames 90 and 300
   within 2%, 5 px, 5 levels.
6. Capture once at `--width 3840 --height 2160`: `Fluid Clear` must log
   `dispatched compute 480 x 270 x 1`.
7. `.\DayScene.exe --unattended --api d3d12 --game-selftest`: 78 PASS, exit 0.
8. Repeat steps 3 to 7 with `d3d11` (if FXC reports X3511, change that loop's `[unroll]` to
   `[loop]`; see the port guide, section 6.1) and with `gl`.
9. Record pass/fail per API and frame in the port guide, section 10.

### 3.2 Try the Dev GUI by hand

The real-time controls were only checked by reading the code.

1. Start interactively (native Wayland, no `--unattended`):
   `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./DayScene --api vulkan --scene 8 --gui
   --width 1920 --height 1080 --logLevel info --logFile /tmp/b8-gui.log`. `G` toggles the
   panels.
2. Optional, to catch GPU hazards: `sudo apt install vulkan-validationlayers` and add
   `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` to the command. The loader enables it
   without a rebuild; watch stdout for `VUID`.
3. "Backyard 8 - Fluid Simulation", start mode: pick balls, water, lake in turn. Each must log
   `[Backyard8] Re-seeded: <N> particles` and `[PBF] Seeded ...` (26,993 pool particles for
   lake/balls; 22,124 pool + 15,234 sphere for water, then `Appended 15234 ... 37358 total`).
   All starts log `Pool settled (3 s simulated) in ~250 ms`.
4. Section "Particle seeding & count": drag each slider over its whole range: spacing
   (0.05 to 0.30), smoothing ratio, both pool levels, sphere radius and height, max particles
   (1,000 to 131,072), heightfield width and depth. The water must re-seed during the drag with
   no crash and no `[ERROR]`. `TRUNCATED at maxParticles` is expected only when the cap is below
   the seed's count.
5. Section "Simulation": gravity 0 (water floats), wind speed 20 (stronger ripples), iterations,
   substeps, viscosity: each must change the motion at once.
6. "Backyard 8 - Particles (Splat)" with renderer = splat: cycle the four styles and move the
   sliders. "Backyard 8 - Screen Space Fluid": cycle the debug views (normals, surface depth,
   thickness, coverage).
7. Done when every control visibly applies and `grep -E "ERROR|VUID" /tmp/b8-gui.log` is empty.
   Note any control that stalls for more than a second (see 3.7).

### 3.3 Settle the pool before the drop

> **Done in Phase 1:** pool-only upload, settle, then append the relaxed sphere; shipped
> `prefillLevel: -0.27`, measured level −0.0406 m, no lattice rows at frame 6. See section 2.1.

The water start cannot settle its pool (the sphere would fall), so for the first half second
the pool shows lattice rows along the near wall. Settle the pool alone, then add the sphere.

Files: `T850/Framework/src/simulation/PbfFluid.cpp`, `T850/Framework/include/simulation/PbfFluid.h`,
`T850/DayScene/Backyard8Scene.cpp`.

1. In `PbfFluid::SeedParticles`, move the six `driver->CreateComputeBuffer` calls
   (`m_positions`, `m_predicted`, `m_predictedNext`, `m_velocities`, `m_velocitiesNext`,
   `m_lambda`) into a new `bool CreateParticleBuffers(BaseDriver*, const std::vector<float>&
   positions, const std::vector<float>& velocities)`. Positions also seed both predicted buffers;
   lambda stays zero-filled (CS_PbfIntegrate reads it as "not measured yet").
2. Still in `SeedParticles`: when `dropSphere`, upload only the pool and keep the relaxed sphere
   in a new member `std::vector<float> m_pendingSphere`. Add `bool HasPendingSphere() const`.
3. Add `bool PbfFluid::AppendPendingSphere(BaseDriver*)`: `ReadParticleFields(driver, &pos,
   &vel, nullptr)`, append `m_pendingSphere` to `pos` and zeros to `vel`, call
   `CreateParticleBuffers`, set `m_particleCount`, clear `m_pendingSphere`.
4. In `Backyard8Scene::InitPbf` and `ReinitializePbf`, set `m_settlePending = true` for every
   start. In the settle block of `Backyard8Scene::OnUpdate`, after `Settle`:
   `if (m_pbf.HasPendingSphere() && m_pbf.AppendPendingSphere(driver)) {
   m_pbf.PublishParticleBuffers(m_config.particles.buffer, m_config.particles.velocityBuffer);
   RegisterParticleSystem(); }`.
5. Re-tune `solver.prefillLevel` in `T850/Assets/Scenes/Backyard8Fluid.json` (and the default in
   `T850/DayScene/Backyard8FluidJson.h`): measure the level at 1800 frames with
   `--backyard-start water` (section 5.2), and move `prefillLevel` by about
   (-0.040 - measured) / 0.43 per step (each +0.01 raised the final level by ~0.0043 m).
6. Verify: `--backyard-start water --dump-frame 6` shows no rows; the level at 1800 frames is
   -0.040 +/- 0.010 m; re-measure the port guide's reference table (section 7).

### 3.4 Free-surface compaction (why the levels need +0.13 to +0.16 m)

> **Done in Phase 1:** shipped `restDensityScale: 0.907`, `restingLevel: 0.0`; measured level
> −0.043 m. See section 2.1. The text below is the original experiment plan.

The seeded grid loses about one particle layer (~16 cm) as it settles: the free-surface
particles lack neighbours above them, read under-dense, get no pressure from the one-sided
constraint, and sink into the layer below. Experiment before changing code.

1. Baseline: a copy of `Backyard8Fluid.json` with `"startMode": 2` and `"restingLevel": 0.0`,
   run with `--fluid-config <abs path>` for 1800 frames, measure (section 5.2). Expected about
   -0.20 m.
2. Confirm the cause: in the same dump, histogram particle counts per 0.13 m layer down from the
   top (section 5.2 shows the snippet). The deficit should sit in the top two layers.
3. Experiment A, config only: `solver.restDensityScale` 0.98, 0.96, 0.94 (wired to the solver's
   rest density) with `restingLevel` 0. Measure the level, and check stability with
   `--pbf-diagnostics 300` (`rhoMean`, `maxSpeed` must stay steady). Lower rest density keeps the
   water less dense, so the level rises. The relaxed sphere already targets 0.995 x rest
   density, so it follows. A value that lands at -0.040 +/- 0.010 m with `restingLevel` 0
   replaces the offsets.
4. If A distorts the splash or the references: normalise the kernel against a relaxed glass
   instead of the cubic lattice. In `PbfFluid.cpp`, `LatticeRestDensity()` returns the cubic
   lattice's kernel sum (459.616 at h = 0.26 m); compute it instead as the mean Poly6 sum of a
   relaxed periodic block (reuse `RelaxSeed` on a 10x10x10 block with wrap-around neighbours),
   and recompute `LatticeGradSq` with the same block.
5. Last resort: cohesion and surface tension (Akinci et al. 2013) in `CS_PbfSolve.hlsl`.
6. Done when `restingLevel` and `prefillLevel` can be close to 0 and both starts settle at
   -0.040 +/- 0.010 m.

### 3.5 Water lost over the rim (water start)

> **Measured in Phase 1, not reduced:** 10,340 culled, level −0.0406 m. See section 2.1.

The water start culls 14,663 of 44,573 particles. Retirement is in
`T850/Assets/Shaders/CS_PbfCull.hlsl`: below the floor, more than 0.6 m outside the footprint,
or outside the footprint and below the wall top (`strandedOnSolid`).

1. Measure the loss: `--backyard-start water --pbf-diagnostics 1800 --dump-frame 1800`, read
   `dead=` in the log.
2. Cheapest: sweep `solver.sphereHeight` (3.08, 2.0, 1.5) and `solver.sphereRadius` (2.0, 1.8,
   1.6) with config copies. Record `dead=` and the 1800-frame level; for the chosen pair, re-tune
   `prefillLevel` (3.3, step 5) so the level stays -0.040 m.
3. If the drop must stay as is, recycle instead of discarding: in `CS_PbfCull.hlsl`, for
   `outsideXZ || strandedOnSolid`, move the particle to a hashed point inside the pool 0.3 m
   below `WaterPlaneY` (x in [BasinMinX + 0.2, BasinMaxX - 0.2], z likewise) with zero velocity,
   instead of parking it. The hash needs a frame counter: add `uint FrameIndex` to the
   `PbfConstants` struct in `PbfFluid.cpp` and to the cbuffer of every `CS_Pbf*.hlsl` (keep the
   16-byte rows and the `static_assert` size consistent; `check_fluid_constants.py` checks the
   layouts), and increment it in `PbfFluid::Step`. Then lower `prefillLevel`.
4. Verify: `dead=` near zero (or the target), the level, and frames 90 and 300 show no visible
   pops where particles reappear.

### 3.6 Pool never still (wind)

> **Done in Phase 1:** physical drag plus a no-slip boundary term; shipped `windStrength: 1`,
> `wallFriction: 8`. See section 2.1. The wind alone did not fix the motion.

The wind drag in `T850/Assets/Shaders/CS_PbfIntegrate.hlsl` (wind block, around line 188) is
`drag = WindStrength * windSpeed * 1.0e-3f`: about 80 times a real air drag at 8 m/s.
`WindStrength` is hard-coded to 1 in `Backyard8Scene::BuildSettings`.

1. Physical drag: compute `rel` first, then
   `const float drag = WindStrength * 1.56e-6f * length(rel.xz) / ParticleRadius;`
   (air density 1.2 x drag coefficient 1.3e-3 / water density 1000, acting on one particle
   layer). Keep `v += float3(rel.x, 0.0f, rel.z) * min(drag * DeltaTime, 0.5f);`.
2. Make the strength tunable: add `float windStrength = 1.0f;` to `Backyard8SolverConfig`
   (`Backyard8FluidJson.h`) and `"windStrength": 1.0` to `Backyard8Fluid.json`; in
   `BuildSettings` use `out.windStrength = c.windStrength;`; add
   `void SetWindStrength(float s)` to `PbfFluid.h` and a GUI slider (0 to 50) next to wind speed.
3. Optional damping: add `float Damping` (1/s) to `PbfConstants` (same rules as 3.5 step 3) and
   apply `v *= exp(-Damping * DeltaTime);` at the end of `CS_PbfUpdateVelocity.hlsl`; default 0,
   GUI slider 0 to 2.
4. Regenerate the GL twins of every changed kernel (section 5.3).
5. Verify on the lake start at 1800 frames (section 5.2): surface spread across X under 0.02 m
   and mean |v| under 0.03 m/s with `windStrength` 1; visible ripples come back around 20.

### 3.7 Cheaper re-seeding while dragging

> **Done 2026-11-10:** 0.2 s debounce, immediate Restart/start-mode. The drag itself still
> needs a GUI hand test.

Seeding sliders re-seed every frame of a drag (30 ms, or 250 ms with the settle).

1. Add `float m_reseedDelay = -1.0f;` to `T850/DayScene/Backyard8Scene.h`.
2. In `Backyard8Scene::DrawSolverPanel`, replace
   `if (seedChanged && !ReinitializePbf()) ...` with `if (seedChanged) m_reseedDelay = 0.2f;`.
   Leave the start-mode combo and the Restart button immediate.
3. At the top of `Backyard8Scene::OnUpdate`:
   `if (m_reseedDelay >= 0.0f && (m_reseedDelay -= deltaSeconds) < 0.0f && !ReinitializePbf())
   T8_LOG_ERROR("[Backyard8] Re-seed failed");`.
4. Alternative (re-seed on release): add `bool LastItemReleased() const` returning
   `ImGui::IsItemDeactivatedAfterEdit()` to `T850/FrameworkImGui/include/imgui/DevGuiContext.h`
   and `.cpp`, and call it right after each seeding slider.
5. Verify: dragging the spacing slider stays smooth, and the log shows one
   `[Backyard8] Re-seeded` about 0.2 s after the drag stops.

### 3.8 Frame-24 determinism

> **Partially done in Phase 1:** integer neighbour gate shipped; frame 24 still differs
> (4,915 px, max 171) because the grid's atomic insertion order changes the density sum. See
> section 2.1.

Two runs differ by up to 4 levels on under 1% of the pixels. The wind gate in
`CS_PbfIntegrate.hlsl` (around line 197),
`freeSurface = density > 1e-4f && density < 0.9f * RestDensity`, flips for interior particles
near 0.9, and float summation order decides which way.

1. Gate on the neighbour count instead, which is an integer (`Lambda[i].w`, written by
   `CS_PbfDensity.hlsl`), so summation order cannot change it:
   `const float neighbours = Lambda[i].w;`
   `const bool freeSurface = neighbours > 0.5f && neighbours < 22.0f;`
2. Check the threshold on data: dump a settled lake (`T850_PBF_DUMP=1800`) and compare the
   `neighbours` column of the top 0.13 m with the interior. A full lattice neighbourhood is 27
   including the particle itself; put the threshold between the two peaks.
3. Regenerate the GL twin; run both validators.
4. Verify: run `--backyard-start water --dump-frame 24` twice. `RT_Dump_FluidParticles` must be
   identical (`numpy` max abs diff 0). Then restore "deterministic" for frame 24 in the port
   guide, section 7.

### 3.9 Low-end performance (Steam Deck)

> **Not run:** no Steam Deck hardware in this session. Desktop 1080p with 2×6 is ~2.2 ms lake /
> ~2.7 ms water (whole frame, splat); a Deck measurement and packaging still need the device.

1. Build and package for the Deck with `T850/steamdeck/BuildSteamRuntime.sh` and
   `T850/steamdeck/PackageSteamDeckRelease.sh`, and run there at 1280x800.
2. Measure the frame time (section 5.4) for lake and water, with the screen-space and the
   splat (water style) renderers. The budget is 16.6 ms for the whole frame.
3. If over budget, in this order:
   - Spacing 0.15 m: about 0.65 x the particles for the same volume (lake about 19,500). The
     compaction offset is one layer, so re-measure `restingLevel` and `prefillLevel`.
   - Screen space: `smoothingIterations` 6 to 4 (Screen Space panel).
   - Water style: replace `ThicknessGradient9` (81 loads per fluid pixel) in
     `CS_FluidSphereShade.hlsl` with a 7x7 version (binomial 1 6 15 20 15 6 1; derivative
     taps -1 -4 -5 0 5 4 1 normalised the same way) and check the drop's frame 24 for moire.
4. Verify: under budget and the captures still look right.

### 3.10 Small items

> **Done 2026-11-10:** unused `LatticeGradSq` variable removed, `[ParticleProbe]` bounded to the
> live count, wireframe/flat moved to `CS_FluidSphereShade` (styles 1/2). The new debug styles
> show rings/flat discs on the nearest-particle path; the old per-particle resolve remains only
> for additive blending.

- `T850/Framework/src/simulation/PbfFluid.cpp`, `LatticeGradSq()`: delete the unused
  `const double k = ...` (compiler warning `-Wunused-variable`).
- `T850/Framework/src/scene/RenderGraph.cpp`, `[ParticleProbe]` block (around line 1404):
  limit the loop to the live count. Look up the system by the buffer name after
  `__particle_projected_` with `ParticleSystemRegistry::Instance().Find(...)` and use
  `min(elements, buffer_count)`.
- Legacy splat resolve (wireframe and flat styles loop over every particle per pixel): add
  `Style == 1` (ring where `rho2 > (1 - 2/r)^2`, transparent inside) and `Style == 2` (flat ramp
  colour, alpha = `Opacity`) to `CS_FluidSphereShade.hlsl`; in
  `Backyard8Scene::ApplyRenderModeToGraph` set `spheres = !screenSpace && blend == 1` for all
  styles, so the resolve runs only for additive blending. Regenerate the twin and compare styles
  1 and 2 with their old look.

## 4. Potential next steps

### 4.1 Save the GUI state

Real-time changes are lost on restart.

1. In `T850/DayScene/Backyard8FluidJson.cpp`, add `bool SaveBackyard8FluidConfig(const char*
   path, const Backyard8FluidConfig&, std::string& error)` using
   `glz::write<glz::opts{.prettify = true}>(config, text)`; keep `"version": 1`. Declare it in
   `Backyard8FluidJson.h` (glaze stays in the .cpp).
2. Add a "Save settings to Backyard8Fluid.json" button to `DrawSolverPanel` that writes
   `m_config` to `Scenes/Backyard8Fluid.json`. On Linux `bin/SteamDeck/Release/Scenes` is a
   symlink to `T850/Assets/Scenes`, so this updates the source file; on Windows check whether the
   assets are copied, and write to the source path if so.
3. Verify: change a slider, save, restart, the value persists; `LoadBackyard8FluidConfig` still
   accepts the file (it rejects unknown keys).

### 4.2 Couple the balls to the water

The `balls` start is only a resting pool: the balls keep their analytic model, and their mesh is
hidden in `Backyard8Scene::CreateAssets`.

1. Add moving sphere colliders to the solver, next to the step boxes: a `SolidSpheres` buffer
   (centre, radius) uploaded each step from the ball bodies in `BackyardScene`.
2. In `CS_PbfDensity.hlsl` and `CS_PbfSolve.hlsl`, add `BoundaryAccumulateSphere` and
   `BoundaryGradientSphere` modelled on the box versions.
3. Accumulate the reaction impulse per sphere (fixed-point `InterlockedAdd` into a small
   buffer), read it back one frame late, and apply it to the ball bodies instead of the analytic
   buoyancy. Show the ball mesh again.
4. Verify: a dropped ball floats at about half its radius and makes a splash.

### 4.3 Missing screen-space features

From [diagnosis-3.md](diagnosis-3.md), phases 4 and 6:

- **Half resolution:** create `FluidDepthA/B` and `FluidThickness` at half size in
  `Backyard8Scene_RenderGraph.json` and upsample in `CS_ParticleShade.hlsl` with a depth-aware
  bilateral filter. Measure the gain at 4K.
- **Reflections off screen:** bind the sky cubemap to `CS_ParticleShade` and use it instead of
  `SkyEstimate()` when the reflection march misses.
- **Curvature flow:** replace the a-trous passes with the paper's iterative curvature flow,
  using the corrected sign from diagnosis-3, and compare against the current look.

### 4.4 Spray and foam particles

1. Add a second particle system (for example `BackyardSpray`) with its own buffer and lifetime.
2. Add an emission kernel that reads the PBF velocities and densities and emits spray where fast
   particles leave the surface (Ihmsen et al. 2012, "Unified spray, foam and bubbles").
3. Draw it with additive blending (the legacy splat path), and reduce `screenSpace.foam`.

### 4.5 Automated Vulkan regression

> **Done 2026-11-10:** references in `scripts/validation/backyard8_references.json`; runner in
> `scripts/validation/backyard8_regression.py`; Vulkan run PASS (9 captures + 2 levels).

1. Store the port guide's section-7 table as `T850/scripts/validation/backyard8_references.json`.
2. Add `T850/scripts/validation/backyard8_regression.py`: run the nine captures (three
   renderers x frames 24, 90, 300, with `--backyard-start water`), compute the statistics, and
   compare with the tolerances of section 7. Add the water-level check (4.6) and exit non-zero
   on failure.
3. Run it in CI on a Vulkan-capable runner, or before every commit that touches scene 8.

### 4.6 Water-level check

> **Done 2026-11-10:** `scripts/validation/pbf_water_level.py <csv>` enforces
> −0.040 ± 0.015 m; the regression runner calls it for lake and water.

Turn the snippet in section 5.2 into `T850/scripts/validation/pbf_water_level.py <csv>` that
fails outside -0.040 +/- 0.015 m, and run it at frame 1800 for `lake` and `water`.

## 5. How to reproduce the measurements (Linux)

From `T850/bin/SteamDeck/Release`, prefix every run with
`SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` (centred window, system Vulkan
loader).

### 5.1 Captures

`./DayScene --unattended --api vulkan --scene 8 --backyard-no-hud --backyard-start water
--width 960 --height 540 --regressionFixedDt 0.0166667 --postProcessMode compute
--fluid-render screen-space --dump-frame 90 --logLevel info --logFile /tmp/b8.log`

The dumps go to `dumps_vulkan_f90_*`. Use `--fluid-render splat` for the water style, and
`--fluid-config <abs path>` with `"style": 3, "renderMode": 0` for the spheres style.

### 5.2 Water level

Add `T850_PBF_DUMP=1800 T850_PBF_DUMP_DIR=/tmp/b8` to the capture command (with
`--dump-frame 1800`) to write `/tmp/b8/pbf_f1800.csv` (positions, velocities, density,
neighbours). `--pbf-diagnostics <frames>` also logs counts, culled particles and density.

```python
import csv, statistics, sys
S = 0.13                                    # particle spacing
rows = [r for r in csv.DictReader(open(sys.argv[1])) if float(r["alive"]) > 0.5]
tops = {}
for r in rows:
    x, y, z = float(r["x"]), float(r["y"]), float(r["z"])
    if not (-4.5 <= x <= 4.5 and -9.0 <= z <= -4.5) or (x > 3.3 and z > -7.1):
        continue                            # outside the pool, or over the steps
    key = (int((x + 4.5) // (2 * S)), int((z + 9.0) // (2 * S)))
    tops[key] = max(tops.get(key, -9.0), y)
print(f"level {statistics.median(tops.values()) + S / 2:+.3f} m (scene 7: -0.040 m)")
# Per-layer profile for 3.4: counts in 0.13 m bins from the floor up.
for b in range(16):
    lo = -1.6 + b * S
    print(f"{lo:+.2f}: {sum(1 for r in rows if lo <= float(r['y']) < lo + S)}")
```

### 5.3 GL twins on Linux

`python3 T850/scripts/shaders/generate_gl_compute_twins.py CS_Name.hlsl [...]` regenerates the
twins with the same banner as the Windows generator (`--check` only compiles). It needs
`glslangValidator` (package `glslang-tools`) and a recent `spirv-cross`. Ubuntu's package is
from 2021; the committed twins were made with SPIRV-Cross `57e4efc` (2026-10-28):

```bash
git clone https://github.com/KhronosGroup/SPIRV-Cross ~/src/SPIRV-Cross
cmake -S ~/src/SPIRV-Cross -B ~/src/SPIRV-Cross/build -DCMAKE_BUILD_TYPE=Release
cmake --build ~/src/SPIRV-Cross/build -j
export SPIRV_CROSS=~/src/SPIRV-Cross/build/spirv-cross
```

### 5.4 Frame time

Run the capture command at `--width 1920 --height 1080` with `--benchmarkFixedDt 0.0166667`
(unpaced) instead of `--regressionFixedDt`, once to `--dump-frame 100` and once to
`--dump-frame 700`. The wall-time difference divided by 600 is the frame time.
`--regressionFixedDt` paces at 60 Hz and does not measure cost.

### 5.5 4K

The capture command at `--width 3840 --height 2160`. `Fluid Clear` must log
`dispatched compute 480 x 270 x 1`.
