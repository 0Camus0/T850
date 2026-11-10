# Backyard Scene 8 (PBF water): Work Summary and Next Steps

Date: 2026-11-09. Platform verified: **Vulkan on Linux only** (Ubuntu, RTX PRO 6000, driver
615). D3D12, D3D11 and OpenGL have **not** been run with any of this work.

This is the record of one working session on the particle-fluid scene. The commit that adds this
file also contains the diagnosis-3 implementation that other agents had left uncommitted on top
of `90f0550`, because the two are interleaved in the same files.

Detailed references:

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
- Updated: [backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md),
  [diagnosis-3-implementation.md](diagnosis-3-implementation.md) (superseded note),
  [../README.md](../README.md), `T850/BUILD-UBUNTU.md`.

## 2. Verification (Vulkan, Linux)

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

## 3. Next fixes (in priority order)

1. **Run D3D12, D3D11 and OpenGL** with the port guide (sections 5 to 8). Nothing here was run
   on Windows. The Linux build has no OpenGL path.
2. **Try the Dev GUI by hand.** Drag every seeding slider in each start mode and watch for
   stalls, validation errors or missing water.
3. **Settle the pool before the drop.** The water start does not settle its pool (the sphere
   would fall), so for the first half second the pool shows rows of particles along the near
   wall. Fix: settle the pool, read it back, append the relaxed sphere, re-upload.
4. **Free-surface compaction.** The seeded grid loses about one particle layer of height as it
   settles. That is why both levels need +0.13 to +0.16 m offsets. The likely root cause: the
   kernel is normalised against a cubic lattice, and free-surface particles lack neighbours
   under a one-sided constraint. Options: normalise against a relaxed glass, add a cohesion or
   surface-tension term, or correct surface density. With that fixed, the level offsets can go
   back towards 0.
5. **Water lost over the rim.** The water start culls a third of its particles. Options: recycle
   culled particles into the pool at rest, keep particles on the coping instead of culling them
   at 0.35 m, or reduce the drop height or sphere size.
6. **Pool never quite still.** The 8 m/s wind drag is strongly exaggerated (`WindStrength`), so
   the pool keeps ripples and a slow slosh of a few cm, and sloshing decays slowly. Options:
   realistic drag, a lower default wind, an optional velocity damping.
7. **Cheaper re-seeding while dragging.** Re-seed on slider release, or throttle, instead of
   every frame; seeding on the GPU would also help.
8. **Frame-24 determinism.** Two runs differ by up to 4 levels on under 1% of the pixels: the
   wind gate (density < 0.9) flips for interior particles near the threshold, decided by float
   summation order. Use a smoothed density or add hysteresis.
9. **Low-end performance.** Measure on a Steam Deck: the water style's normal reads 81 thickness
   values per fluid pixel, and the defaults now run 30k to 45k particles. If it is too slow, a
   group-shared separable blur is the next step.
10. **Small items.** An unused variable warning in `LatticeGradSq` (pre-existing). The
    `[ParticleProbe]` readback counts stale entries past the live count now that the table is
    oversized (debug output only). The legacy splat resolve (wireframe/flat styles) still loops
    over every particle per covered pixel.

## 4. Potential next steps

- **Save the GUI state.** Real-time changes are lost on restart; add a "save to
  Backyard8Fluid.json" button.
- **Couple the balls to PBF.** The `balls` start is only a resting pool; the balls keep their
  analytic model and their mesh is hidden.
- **Screen-space features still missing** (diagnosis-3 phases 4 and 6): half-resolution path,
  IBL cubemap reflections (off-screen reflections are sky only), exact curvature flow.
- **Secondary particles** for spray and foam, instead of whitewater discs.
- **Automated Vulkan regression:** run the section-7 captures and `scripts/compare_dumps.py`
  with the port guide's tolerances in CI.
- **Water-level check in the repo:** turn the measurement below into
  `scripts/validation/pbf_water_level.py` with a pass band around the water plane.

## 5. How to reproduce the measurements (Linux)

From `T850/bin/SteamDeck/Release`, prefix every run with
`SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` (centred window, system Vulkan
loader).

- **Captures:** `./DayScene --unattended --api vulkan --scene 8 --backyard-no-hud
  --backyard-start water --width 960 --height 540 --regressionFixedDt 0.0166667
  --postProcessMode compute --fluid-render screen-space --dump-frame 90`. The dumps go to
  `dumps_vulkan_f90_*`.
- **Water level:** add `T850_PBF_DUMP=900,1800 T850_PBF_DUMP_DIR=<dir>` to write
  `pbf_f<N>.csv` (positions, velocities, density). The level is the median, over 0.26 m cells of
  the pool footprint (steps excluded), of the highest particle centre plus half a spacing.
  `--pbf-diagnostics <frames>` logs counts, culled particles and density.
- **Frame time:** run with `--benchmarkFixedDt 0.0166667` (unpaced) to `--dump-frame 100` and
  `--dump-frame 700`. The time difference divided by 600 is the frame time.
  `--regressionFixedDt` paces at 60 Hz and does not measure cost.
- **4K:** the capture command at `--width 3840 --height 2160`. `Fluid Clear` must log
  `dispatched compute 480 x 270 x 1`.
