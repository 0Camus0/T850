# Session record — W-26, W-06 steps 3–4, W-07 analysis (2026-10-31, evening)

A record of what was done and **how**, written so the next agent can repeat or challenge any of it.
Companion documents: [remaining-work](backyard-water-remaining-work.md) (task catalogue) and
[staged-plan](backyard-water-staged-plan.md) (stage log). Nothing here was pushed; the branch is
`backyard_scene` and the work is local only.

Planned order was **W-26 → W-06 steps 3+4 → W-07 → W-16**. What actually happened:

| Item | Outcome |
|---|---|
| W-26 remove the trapped-air term | **Measured, not removed.** It is worth 4.8 mm of moat and D3D12 then misses gate 7 by 0.7 mm |
| W-06 step 3 body feels the current | **Shipped.** Annulus current + drag on relative velocity |
| W-06 step 4 retune heave damping | **Measured, kept at 0.15.** Lower values triple and sextuple the residual bob |
| `--backyard-ball-probe` | **New instrument**, unblocked both measurements above |
| W-07 attempt 2 displace instead of delete | **Analysed, not coded.** The flicker is the binary mask flip; the fix is a per-cell solid fraction |
| W-16 implementation record | **Partially done** (§7, §8.1, kernel table, readback contract). Sections 6 and 8 still stale |
| W-27 wind tunable is inert | **Found and filed**, not fixed |

---

## 1. The instrument that everything else needed

`--backyard-mass-audit N` averages the whole pool. A 2 cm moat around a 0.25 m ball is ~7 cells out
of 82 917, so the audit is blind to the only thing W-06 and W-07 are judged on — which is why W-07
attempt 1 could not tell whether it had helped. The first real deliverable was therefore a probe:

```powershell
cd D:\Code\QwenFlashT850\T850-backyard\T850\bin\x64\Release
.\DayScene.exe --scene 7 --api d3d12 --unattended --logLevel info `
  --backyard-ball-probe 60 --dump-frame 900
```

It logs two lines every N frames, from the **same surface texture the renderer draws** (via
`FluidSimulation::ReadSurface`, the call gate 7 uses):

```
[Backyard][ball] t=8.13 y=-0.059 vy=+0.096 level=-0.018 plane=-0.040 submerged=0.62 flow=(+0.054 +0.122) xz=(+0.098 -6.652) vxz=(+0.018 +0.018)
[Backyard][ball] far=+0.0012 ring-far=-0.0167 covered min-ring=-0.0133 rimPeak=+0.0543 moatMin=-0.1464 bands(cm): -2.4 -2.3 -2.4 -1.6 -1.3 -1.2 -1.2 -1.0
```

* `level` is the sampled water line and `plane` the authored one, so the coupling is visible directly.
* `ring−far` and `covered min−ring` are gate 7's two comparisons, recomputed per sample.
* `rimPeak` / `moatMin` are running extremes, so a transient crest or crater between samples is not
  lost. `bands` is the same profile in eight 10 cm radial bands — the shape that tells apart "moat"
  (dip in bands 0–3) from "crater" (a dip in band 0 and a crest in bands 1–3).
* `flow` is the annulus mean current from the GPU, `xz`/`vxz` the body's own position and velocity.

Implementation: [BackyardScene.cpp](../../T850/DayScene/BackyardScene.cpp) next to the mass audit,
`BackyardScene::SetBallProbeFrames` + `--backyard-ball-probe` in [App.cpp](../../T850/DayScene/App.cpp).
Means, not medians, so it is cheap; gate 7 keeps the medians.

## 2. W-26 — measured, term kept

**Method.** Do not delete the term and re-argue about the gate: make the term identically zero
(`TRAPPED_AIR_DEPTH_CELLS = 1000.0f`), run the self-test on two APIs, compare, restore. Shader edits
need no rebuild (assets load from the source tree), so the A/B costs two runs.

```powershell
.\DayScene.exe --backyard-selftest --scene 7 --api d3d12 --unattended --logLevel info
```

| | with the term | without it |
|---|---|---|
| `ring−far` D3D12 / Vulkan | −2.0 / −1.4 mm | −7.4 / −6.7 mm |
| `covered min−ring` D3D12 / Vulkan | −15.9 / −13.8 mm | **−20.7** / −19.1 mm (limit 20) |
| band profile vs far (cm) | −0.9 −1.0 −0.8 −0.2 | −1.8 −2.0 −1.7 −0.7 |
| float flicker | 0.0005 m | 0.0006 m |
| `submerged`, ball `y` | 0.60, −0.073 | 0.60, −0.073 |

**Decision.** Restore the term. It hides 4.8 mm of a 20 mm budget, and the verdict would be decided
by the ±0.8 mm spread between D3D12 and Vulkan — relaxing a gate on that basis is exactly what the
standing rules forbid. **What the numbers do establish** is that W-03's van Leer flux did not reduce
the moat (it sharpened the far field instead): the moat is water the *body deletes*, so it is W-07 by
definition. That table is now W-07's acceptance target.

**Risk kept on the books.** The term is a renderer-side compensation and will double count the day a
body displaces water properly (W-07) or bubbles are advected.

## 3. W-06 step 3 — the body feels the current

**Change.** [CS_FluidBodyLevel.hlsl](../../T850/Assets/Shaders/CS_FluidBodyLevel.hlsl) already scanned
the annulus to average the surface; it now also reads the velocity volume (new `sampled_texture:1`
binding, 3D) at the shallower of the column's waterline row and the body's centre row, and returns a
`uint4` per thread group: Σheight·1024, Σcount, Σu·1024, Σw·1024. The host divides by the count and
publishes `FluidSphere::flowX/flowZ`. `AdvanceOneSphere` then drags on the relative velocity and adds
the current back:

```cpp
const float relVx = s.vx - s.flowX;
const float relVz = s.vz - s.flowZ;
...
s.vx = s.flowX + relVx / dragFactor;
s.vz = s.flowZ + relVz / dragFactor;
```

Two traps in getting it to compile and register, worth knowing:

* `Texture3D::Load` takes `int4`, not `int3` (the DXC error names the line, the fix is one argument).
* A new sampled texture needs `"dimension": "3d"` in
  [compute_kernels.json](../../T850/Assets/Shaders/compute_kernels.json). Without it the pipeline is
  **silently rejected** (`SetValidatedLayout` compares the reflected dimension and returns false; the
  only symptom was `[Fluid] Solver resources are incomplete`).

**Verification, and why it was awkward.** 30 spawned balls produce a real current (`flow` reaches
+0.05…+0.13 m/s) and the body drifts with it, but that cannot prove the *sign*. The obvious test —
turning up the wind — does nothing (see W-27). So the direction was checked by forcing
`s.flowX = 0.5f; s.flowZ = -0.25f;` in the integrator for one build: the body crossed to
`x = +4.249` (the +X wall) and `z = −8.750` (the −Z wall), i.e. exactly the forced direction. The
override was removed before the final build.

**Cost.** Same-session A/B with the change stashed and the baseline rebuilt
(`git stash push -- <paths>`, build, `telemetry.ps1`, `git stash pop`, build, `telemetry.ps1`),
720p, 0 balls, 1 200 profiled frames:

| | before | after |
|---|---|---|
| D3D12 | 310.1 fps (`update` 2.392 ms) | **312.5 fps** (2.366 ms) |
| Vulkan | 291.9 fps (2.317 ms) | **295.5 fps** (2.232 ms) |

No regression; the difference is run variance.

## 4. W-06 step 4 — heave damping is measured

`sphereHeaveDamping` is a JSON tunable, so the A/B is three runs with no rebuild: insert
`"sphereHeaveDamping": <v>` into `Scenes/BackyardWater.json`, run the probe, parse the settled window.

| `sphereHeaveDamping` | y range t = 5…10 s | y min … max | max \|vy\| |
|---|---|---|---|
| **0.15 (shipped)** | **18 mm** | −0.083 … −0.065 m | 0.050 m/s |
| 0.05 | 128 mm | −0.145 … −0.017 m | 0.365 m/s |
| 0.00 | 307 mm | −0.225 … +0.082 m | 0.875 m/s |

**Kept at 0.15**, now justified rather than assumed: the coupling is one-way, so nothing radiates the
heave energy away and the ratio is the only sink; 0.15 also sits inside the 0.1–0.3 band a real
heaving sphere radiates at. The residual 18 mm is the body tracking the pool's remaining motion — the
sampled level is within 4 mm of the plane across the same window.

## 5. W-07 attempt 2 — analysed, deliberately not coded

The plan was "displace only on footprint change, distance-weighted, upward/velocity-biased". Analysis
(and a second opinion from the local Qwen model) says it cannot pass gate 8:

1. The gather already only sees cells that *just* became solid (body cells hold α = 0 afterwards), so
   attempt 1 was effectively "on change"; its 0.034 m flicker came from the **binary mask flip** —
   one full cell (0.73 L) changes hands in a single substep. 1/distance weighting reduces the peak
   per receiver only linearly.
2. Sub-cell coverage cannot be added to the displacement pass alone: the excluded volume is
   `α·(coverage(t) − coverage(t−dt))`, and coverage is only meaningful if the *mask* is a fraction.
   `IsSolid` in seven kernels, the `2u + k` body encoding and the transport's zeroing rule all assume
   a bit.

So the real task is a `solidFraction` volume (`freeVolume = 1 − solidFraction`, α per unit free
volume, analytic sphere–cube coverage), after which a 5 mm bob changes a cell by 5.5 % instead of
flipping it and gate 8 is satisfied by construction. Scoped in W-07; it needs its own stage.

## 6. W-27 — the wind tunable does nothing (found here)

`CS_FluidForces3D` applies wind only to rows above `SurfaceLevel · Height` = 0.96 × 22 = 21.1, but the
pool is filled to row ≈ 17, so wind touches **air only**. Measured: `windX` 0.6 → 5.0 (8×) left a
scene-7 run bit-identical in every logged value. Filed as W-27; not fixed, because the fix changes
the forcing path and needs its own still-pool check.

## 7. Verification on the final binary

```
--backyard-selftest --scene 7 --api d3d12   PASS (10 gates)
--backyard-selftest --scene 7 --api vulkan  PASS
--backyard-selftest --scene 7 --api d3d11   PASS (synchronous readback fallback path)
```

Float gate on the three APIs — `far` −2.4 / −2.8 / −2.3 mm, `ring−far` −2.0 / −1.4 / −2.1 mm,
`covered min−ring` −15.9 / −13.8 / −13.8 mm, `submerged` 0.60, ball `y` −0.073: unchanged from the
previous build, which is the point — the current channel had to be free.

Scene runs: 8 seeded balls on D3D12, Vulkan, D3D11, GL and WebGPU with 0 `[ERROR]` lines (checked
earlier in the session on the pre-change binary; the post-change binary was checked by the three
self-tests plus the probe runs above).

## 8. Files touched

| File | Change |
|---|---|
| `Assets/Shaders/CS_FluidBodyLevel.hlsl` | annulus current; 12-float constants; `uint4` slots |
| `Assets/Shaders/compute_kernels.json` | `fluid_body_level` gains a 3D texture at t1, constants 12 |
| `Framework/src/simulation/FluidSimulation.cpp` | flow readback, drag on `(v − flow)` |
| `Framework/include/simulation/FluidSimulation.h` | `FluidSphere::flowX`, `flowZ` |
| `DayScene/BackyardScene.{h,cpp}` | `--backyard-ball-probe` |
| `DayScene/App.cpp` | CLI hook |
| `documentation/scenes/*` | W-06, W-07, W-26, W-27, stage log, implementation record, this record |

`T850/config.json` was left uncommitted, as always.

## 9. Next, in order

1. **W-27** — small, measurable, and it unblocks verifying a current with a real wind-driven flow.
2. **W-14** — the duplicated `FluidConstants` (the body-level kernel is a fourth copy now).
3. **W-07 stage** — per-cell solid fraction. Big, but it is the last physics gap: it removes the moat,
   lets the trapped-air term go (W-26), and gives the body a two-way coupling so `heaveDamping` can
   come down.
4. **W-16 rest** — implementation record sections 6 and 8, and **W-17** platforms (Android, Linux,
   ARM64) which still have never run scene 7.
