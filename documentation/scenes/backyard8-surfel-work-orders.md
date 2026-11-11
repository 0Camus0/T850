# Backyard 8: surfel splatting for the particle renderer, work orders

Branch `backyard8-surfels` (from `7c88f66`). Coordinator: the opencode orchestrator. Workers:
`qwen-5090`, `qwen-local`. Reviewer: Claude. Paths are relative to the repository root unless
they start with `T850/`-relative names inside a work order.

## Goal

The particle renderer ("splat", `particles.renderMode = 0`, water style 0) looks like jelly:
`CS_FluidSphereShade` uses the summed thickness as a fake height field, so its normals face the
camera. It has no real reflection, its refraction is a thickness-scaled offset, and it costs
2.2 ms vs 1.3 ms for screen space at 1080p.

Replace the water style with **surfel splatting** feeding the **existing water shade**
(`CS_ParticleShade`). Then every screen-space fix applies to the particle renderer:
- environment and screen-space reflections;
- hidden-surface refraction;
- G-buffer sun and caustics;
- foam, spray and bubbles;
- the wet deck.

Design:

1. The particle renderer's water style runs the screen-space pipeline with a different surface
   reconstruction. The scene maps (renderMode 0, blend 1, style 0) to a descriptor with
   `render_mode = ScreenSpace` and `screen_space.surfels = true`. Debug styles 1-3 keep the
   `Fluid Sphere Shade` path unchanged.
2. Surface reconstruction:
   1. `Fluid Splat` (unchanged) writes the front-most ellipsoid depth per pixel (half resolution).
   2. The new `Fluid Surfel Splat` pass (`CS_FluidSplat.hlsl`, `compute_stage` 2) scatters every
      particle again. A ray-ellipsoid hit within `SurfelBand` metres behind the front depth is
      blended into five per-pixel sums: weight, weighted depth offset, and weighted view-space
      normal. The normal is the ellipsoid's own quadric normal, `M (hit - c)`.
   3. The new `Fluid Resolve (surfels)` pass (`CS_FluidResolve.hlsl`, `compute_stage` 2) writes
      the blended depth to `FluidDepthA`, the normal to the new `FluidNormal` target, and the
      thickness as today. It then clears the sums.
   4. There are no smoothing passes.
3. The shade gets `Mode = 2` (screen space with surfel normals). It takes the macro normal from
   `FluidNormal` instead of from the depth derivative. Everything after the macro normal is
   unchanged.

## Rules (every work order)

- **Never `git stash`, `git checkout -- <path>`, `git restore` or `git reset` in this working
  tree.** Other agents and Claude have uncommitted edits in it, and a stash lost Claude's
  review notes once. To compare with an older commit, use
  `git worktree add /tmp/<name> <commit>` and remove it afterwards.
- Touch only the files your work order owns. Commit only them, with a pathspec:
  `git commit -m "WO-n: ..." -- <paths>`. Do not stage or commit anybody else's files, do not
  amend or rebase, never push.
- Commit subject starts with `WO-n:`; the body lists what you ran and the exact results.
- Build: `cd T850 && cmake --build build/linux --target DayScene -j$(nproc)`. Shaders compile at
  runtime (no rebuild for `.hlsl` edits).
- Run: from `T850/bin/SteamDeck/Release`, always `--unattended --scene 8` (without `--scene 8`
  the app starts its default scene, which tests nothing here), with
  `SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`. `--fluid-render splat`
  selects the particle renderer, `--backyard-start water` the splash start. Delete the
  `dumps_*` folders you create.
- Static shader check (each changed `.hlsl`):
  `glslangValidator -D -V -S comp -e CS --target-env vulkan1.0 -DT850_VULKAN=1 -DT850_SPIRV=1 <file> -o /tmp/x.spv && spirv-val --target-env vulkan1.0 /tmp/x.spv`.
- GL twins are generated, never hand-edited:
  `cd T850 && SPIRV_CROSS=$HOME/src/SPIRV-Cross/build/spirv-cross python3 scripts/shaders/generate_gl_compute_twins.py <Name>.hlsl`
  (the file name WITH `.hlsl`), then `python3 scripts/validation/check_gl_twins.py`.
- Validators: `python3 T850/scripts/validation/check_fluid_constants.py` must exit 0 (check the
  exit code itself, not through a pipe).
- Never re-freeze `T850/scripts/validation/backyard8_references.json`. Claude does it after a
  visual sign-off.
- Report numbers, not adjectives. Distinguish pass, fail and "not run".
- When blocked or when a design detail is wrong, append to "Requests to Claude" at the end and
  continue with what you can.

## WO-1: engine plumbing (owner: qwen-5090)

Files: `T850/Framework/include/scene/ParticleSystem.h`,
`T850/Framework/src/scene/ParticleSystem.cpp`, `T850/Framework/src/utils/ComputeKernelRegistry.cpp`,
`T850/Assets/Shaders/compute_kernels.json`, `T850/Assets/Scenes/Backyard8Scene_RenderGraph.json`.

1. `ParticleScreenSpaceDesc` (after `debug_view`):
   ```cpp
   // Surface reconstruction: false = smoothed screen-space depth (the smoothing passes); true =
   // surfel splatting, the front layer of anisotropic particles blended into depth and normal
   // in one pass (Backyard 8's particle renderer). The water shade is the same.
   bool surfels = false;
   float surfel_band = 1.0f;        // front-layer depth, in particle radii
   float surfel_sharpness = 2.0f;   // exponent on the ellipsoid's centre weight
   ```
   Validation in `ParticleSystem.cpp` (next to the `debug_view` check): reject `surfel_band`
   outside [0.1, 8] and `surfel_sharpness` outside [0.25, 8].
2. `ComputeKernelRegistry.cpp`:
   - `BuildParticleSplatConstants`: `value.iteration = context.permutation == "resolve" ? 1u :
     static_cast<uint32_t>((std::max)(context.passStage, 0));` (keep the comment accurate:
     `CS_ParticleSplat` uses permutations, `CS_FluidSplat`/`CS_FluidResolve` use stage 2 for
     surfels).
   - Same builder: `value.color[2][3]` = `SurfelBand` in metres
     (`system->screen_space.surfels ? system->screen_space.surfel_band * system->radius : 0.0f`),
     `value.color[3][3]` = `SurfelSharpness` (`system->screen_space.surfel_sharpness`). Both
     slots are padding in every kernel today; add a comment like the existing ones for
     `color[0][3]`/`color[1][3]`.
   - Shade builder (`value.mode = ...`, near line 632): `ScreenSpace` with `surfels` gives
     `2u`, otherwise as today. Update the comment.
3. `compute_kernels.json` (keep the existing bindings and their order; append):
   - `particle_fluid_splat`: `{"type": "read_write_buffer", "shader_register": 3, "binding_index": 8}`.
   - `particle_fluid_resolve`: `{"type": "read_write_buffer", "shader_register": 2, "binding_index": 5}`
     and `{"type": "storage_texture", "shader_register": 3, "binding_index": 6, "storage_format": "rgba16float"}`.
   - `particle_shade`: `{"type": "sampled_texture", "shader_register": 11, "binding_index": 15}`.
   Copy the exact key set and formatting of neighbouring bindings.
4. `Backyard8Scene_RenderGraph.json`:
   - Render target `FluidNormal`: like `FluidThickness` (`RGBA16F`, `size_ref` `$window_half`,
     `storage` true, `linear_filter` false).
   - Compute buffer `__fluid_surfel_bits_BackyardWater`: `structure_stride` 4, `element_count`
     5, `size_ref` `$window`, `access` `read_write`, `clear` **true** (zeroed at creation; the
     resolve clears it every frame).
   - Bind the new resources in the existing passes (every pass of a kernel must bind every
     manifest binding):
     - `Fluid Splat` and `Fluid Splat (splat mode)`: `__fluid_surfel_bits_BackyardWater`,
       `buffer_write`, register 3.
     - `Fluid Resolve`: `__fluid_surfel_bits_BackyardWater` (`buffer_write`, register 2) and
       `FluidNormal:COLOR0` (`storage_write`, register 3).
     - `Fluid Surface Shade`: `FluidNormal:COLOR0`, `sampled`, register 11.
   - New pass `Fluid Surfel Splat`, placed right after `Fluid Splat`: a copy of `Fluid Splat`
     (same target, extent and resources) with `"compute_stage": 2`.
   - New pass `Fluid Resolve (surfels)`, placed right after `Fluid Resolve`: a copy of
     `Fluid Resolve` with `"compute_stage": 2`.
   - Pass order is execution order; do not move other passes.

Acceptance: the build passes and `check_fluid_constants.py` exits 0. Commit even though the
runtime check needs WO-2: until the shaders declare the new bindings, the pipelines fail
validation, which is expected. Say so in the commit body.

## WO-2: shaders (owner: qwen-local)

Files: `T850/Assets/Shaders/CS_FluidSplat.hlsl`, `CS_FluidResolve.hlsl`, `CS_ParticleShade.hlsl`,
`CS_FluidSphereShade.hlsl` and their `.glsl` twins.

### CS_FluidSplat.hlsl

1. Rename the cbuffer member `Color2Pad` to `SurfelBand` (metres; 0 = off) and `Color3Pad` to
   `SurfelSharpness`. Do not change the layout.
2. Declare, after `AuxBits`:
   ```hlsl
   // Five per output pixel, written by the surfel pass (Iteration == 2) and read and cleared by
   // CS_FluidResolve: [0] sum w, [1] sum w * dz / SurfelBand, [2..4] sum w * n (view space,
   // two's complement), all in units of 1/4096.
   #if defined(T850_VULKAN) || defined(T850_SPIRV)
   [[vk::binding(8, 0)]]
   #endif
   RWStructuredBuffer<uint> SurfelBits : register(u3);
   #define SURFEL_SCALE 4096.0f
   ```
3. Add, before `CS`:
   ```hlsl
   // Surfel pass: blend this ellipsoid's front hit into the pixel when it lies in the front
   // layer, weighted toward the ellipsoid's centre line and toward the front.
   // `centreWeight` = 1 - s^2, s = the ray's distance from the centre in ellipsoid units.
   void AccumulateSurfel(uint pix, float3 r, float zFront, float3 c, float3x3 viewM, float centreWeight)
   {
       const uint bits = DepthBits[pix];
       if (bits >= 0x7F800000u) return;            // empty (cleared) pixel
       const float dz = max(zFront - asfloat(bits), 0.0f);
       if (dz > SurfelBand) return;                 // behind the front layer
       const float3 hit = r * zFront;               // view space (r.z == 1)
       float3 n = mul(viewM, hit - c);              // quadric gradient: the ellipsoid's normal
       const float len = length(n);
       if (len < 1.0e-6f) return;
       n /= len;
       if (dot(n, hit) > 0.0f) n = -n;              // face the camera
       const float front = 1.0f - dz / SurfelBand;
       const float w = pow(saturate(centreWeight), max(SurfelSharpness, 0.25f)) * front * front;
       if (w < 1.0e-3f) return;
       const uint base = pix * 5u;
       InterlockedAdd(SurfelBits[base + 0u], uint(w * SURFEL_SCALE + 0.5f));
       InterlockedAdd(SurfelBits[base + 1u], uint(w * (dz / SurfelBand) * SURFEL_SCALE + 0.5f));
       InterlockedAdd(SurfelBits[base + 2u], asuint(int(round(w * n.x * SURFEL_SCALE))));
       InterlockedAdd(SurfelBits[base + 3u], asuint(int(round(w * n.y * SURFEL_SCALE))));
       InterlockedAdd(SurfelBits[base + 4u], asuint(int(round(w * n.z * SURFEL_SCALE))));
   }
   ```
4. In `CS`, screen-space path (`surface == true`):
   - `const bool surfelPass = Iteration == 2u;`. Surfels run only in the screen-space path and
     only when `SurfelBand > 0`. Return early otherwise.
   - The thickness and whitewater block runs only when `!surfelPass`. The scatter must not
     add thickness twice.
   - Ellipsoid branch, after `lo`/`hi` are known, surfel pass only, as an early out:
     1. Read the front depth at the centre pixel and at the four corners of `[lo, hi]`. Empty
        pixels (bits >= `0x7F800000u`) count as infinitely far.
     2. If `c.z - bound` is beyond the farthest of these five plus `SurfelBand`, return. Most
        interior particles lie wholly behind the front layer.
   - In the pixel loop, after `zFront` passes its `<= 0.05f` test: the depth pass keeps its
     `InterlockedMin`, and the surfel pass calls `AccumulateSurfel(pix, r, zFront, c, viewM,
     disc / a)`. (`disc / a` is exactly `1 - s^2`, because `disc = b^2 - a * cc`.)
   - Sphere fallback (no anisotropy): the surfel pass returns before it. Those pixels keep the
     depth normal.
   - Update the header comment: one paragraph on the surfel pass.

### CS_FluidResolve.hlsl

1. Rename `Color2Pad` to `SurfelBand`. Declare
   `RWStructuredBuffer<uint> SurfelBits : register(u2)` (`[[vk::binding(5, 0)]]`) and
   `RWTexture2D<float4> NormalOut : register(u3)` (`[[vk::binding(6, 0)]]`, with the same
   `T850_SPIRV` storage attributes as `ThicknessOut`).
2. In `CS`, after computing `z`:
   ```hlsl
   // Surfel resolve (Iteration == 2): the blended front layer. Depth = front-most hit plus the
   // weighted mean offset; normal = normalised weighted sum, w = confidence. Read and clear.
   float4 normal = float4(0.0f, 0.0f, 0.0f, 0.0f);
   if (Iteration == 2u) {
       const uint base = pix * 5u;
       const uint weightBits = SurfelBits[base + 0u];
       if (z > 0.0f && weightBits > 4u) {
           z += SurfelBand * float(SurfelBits[base + 1u]) / float(weightBits);
           const float3 sum = float3(float(asint(SurfelBits[base + 2u])),
                                     float(asint(SurfelBits[base + 3u])),
                                     float(asint(SurfelBits[base + 4u])));
           const float len = length(sum);
           if (len > 1.0f) normal = float4(sum / len, saturate(float(weightBits) / 4096.0f));
       }
       SurfelBits[base + 0u] = 0u;
       SurfelBits[base + 1u] = 0u;
       SurfelBits[base + 2u] = 0u;
       SurfelBits[base + 3u] = 0u;
       SurfelBits[base + 4u] = 0u;
   }
   ```
   Always write `NormalOut[imagePixel] = normal;`, zero in stage 0. Update the header comment.

### CS_ParticleShade.hlsl

1. Declare `Texture2D<float4> FluidNormal : register(t11)` (`[[vk::binding(15, 0)]]`), next to
   the other fluid targets. Add `LoadFluidNormalTexel(int2 t)` like
   `LoadFluidThicknessTexel`.
2. Add `float4 SampleFluidNormal(float2 pixel)`, after `SampleFluid`. It repeats `SampleFluid`'s
   four-texel layer test (same `front`, same `valid` weights) and returns the `valid`-weighted
   mean of `FluidNormal`, or zero when no texel is valid.
3. In `CS`, right after
   `if (dot(macroNormal, toCamera) < 0.0f) macroNormal = -macroNormal;`:
   ```hlsl
   // Surfel splatting (Mode 2): the front layer's blended ellipsoid normal replaces the depth
   // derivative, which would show the unsmoothed surface's particle-scale bumps.
   if (Mode == 2u) {
       const float4 surfel = SampleFluidNormal(pixelCentre);
       const float len = length(surfel.xyz);
       if (len > 1.0e-3f) {
           float3 n = surfel.xyz / len;
           if (dot(n, toCamera) < 0.0f) n = -n;
           macroNormal = normalize(lerp(macroNormal, n, saturate(surfel.w * 4.0f)));
       }
   }
   ```
4. Update the `Mode` comment in the cbuffer (`0 splat, 1 screen space, 2 screen space with
   surfel normals`) and the file header (one sentence).

### CS_FluidSphereShade.hlsl (last step of WO-2)

Style 0 no longer reaches this kernel. Delete:
- the water style: `ShadeWater`, `ThicknessField`, `ThicknessGradient9`, the binomial tables,
  and `SkyEstimate` if nothing else uses it;
- its branch in `CS`.

Keep the cbuffer as it is. **Amended after review:** also delete the declarations the water
style alone used: `ThicknessBits` (t2), `SceneColor` (t3), `LinearSampler` (s3), `SceneDepth`
(t4), plus helpers only they used. Otherwise the compiler strips them, and
`ComputePipeline::SetValidatedLayout` rejects the pipeline, because the declared and reflected
binding counts must match. Do **not** keep them alive with dummy reads. Whether such reads
survive depends on the compiler version, and `x * 0.0f` is NaN when x is inf. WO-1's owner
removes the same four bindings (binding indices 4, 5, 6 and 7) from `particle_sphere_shade`
in `compute_kernels.json` and from the `Fluid Sphere Shade` pass in the graph. Update the
header comment.

### Twins and checks

Regenerate the four twins; `check_gl_twins.py` passes once WO-1's manifest is in.
`check_fluid_constants.py` exits 0. Run glslangValidator and spirv-val on all four files. If WO-1
is not committed yet, commit WO-2 with the twins and note in the body that the twin check
waits for WO-1. The orchestrator re-runs it.

## WO-3: scene, config and Dev GUI (owner: qwen-5090, after WO-1)

Files: `T850/DayScene/Backyard8FluidJson.h`, `T850/DayScene/Backyard8Scene.cpp`,
`T850/DayScene/Backyard8Scene.h`, `T850/Assets/Scenes/Backyard8Fluid.json`.

1. `Backyard8ParticleConfig`: `float surfelBand = 1.0f;` and `float surfelSharpness = 2.0f;`,
   commented like the neighbours. Add both keys to the `particles` section of the JSON with
   the same values (the schema rejects unknown keys and the JSON must equal the defaults).
2. Add `bool UsesWaterShade() const` (renderMode 1, or renderMode 0 with blend 1 and style 0)
   and `bool UsesSurfels() const` (the second case). Use them:
   - `BuildParticleDesc`: `d.render_mode = UsesWaterShade() ? ScreenSpace : Splat`;
     `d.screen_space.surfels = UsesSurfels()`; clamp the band to [0.1, 8] and the sharpness to
     [0.25, 8].
   - `ApplyRenderModeToGraph`:
     - `Fluid Clear`, `Fluid Splat`, `Whitewater Splat`, `Fluid Surface Shade`: on when the
       water shade is used.
     - `Fluid Surfel Splat`, `Fluid Resolve (surfels)`: surfels only.
     - `Fluid Resolve` and `Fluid Smooth 1..8`: screen space only (renderMode 1).
     - `Fluid Clear (splat mode)`, `Fluid Splat (splat mode)`, `Fluid Sphere Shade`: particle
       renderer with blend 1 and style != 0.
     - `Fluid Particles (resolve)`: as today with the new meaning
       (`(!UsesWaterShade() && !spheres) || fallback`).
   - The anisotropy compute (line ~524) and the whitewater compute (line ~542) must run
     whenever the water shade is used, not only for renderMode 1.
   - The registration log line names the surfel renderer.
3. Dev GUI:
   - Fluid Simulation renderer combo: `"particles (surfel splatting)"`,
     `"screen-space fluid (smoothed depth)"`.
   - Particles panel:
     - style 0 label `"water (surfel splatting)"`;
     - a "Surfels" section with sliders `surfel band (radii)` 0.1..8 and `surfel sharpness`
       0.25..8 (both `lookChanged`);
     - the text: the water style uses all shading settings of the Screen Space panel (Optics,
       Scene lighting, Whitewater, Debug), and not its smoothing settings.
   - Screen Space panel status line: active for both renderers, and say which surface
     reconstruction is active.
4. Acceptance:
   - Build.
   - A 300-frame run each, with zero `[ERROR]`:
     - `--fluid-render splat` lake;
     - `--fluid-render splat --backyard-start water`;
     - default screen space.
   - An unknown key under `particles` is rejected.
   - The log of the splat run shows `Fluid Surfel Splat` and `Fluid Resolve (surfels)`
     dispatching (`--logLevel info`). The screen-space run does not show them.

## WO-4: integration report (owner: qwen-local, after WO-1..3)

Run and report in the status table; change no source.

1. Build, `DayScene --unattended --game-selftest` (expect 78 PASS, exit 0), both validators.
2. `python3 scripts/validation/backyard8_regression.py` from `T850/` with the display
   environment. Expected: `screen-space` and `spheres` PASS unchanged, levels PASS, and `splat`
   FAIL (it is now a different renderer). Report every line. If `screen-space` or `spheres`
   fails, that is a bug: report it and stop.
3. Captures, 1920x1080, `--fluid-render splat`, `--dump-frame`:
   - lake f120;
   - water f45, f90, f240;
   - the same four with `screenSpace.debugView` 1 (normals), via a temporary `--fluid-config`
     copy.

   Keep the eight `RT_Dump_BackBuffer.ppm` files in `/tmp/surfel-review/`, named
   `<start>_f<frame>[_normals].ppm`.
4. GPU cost: `--profile --profileFrames 400 --benchmarkFixedDt 0.0166667` at 1920x1080, water
   start, `--fluid-render splat` and `--fluid-render screen-space`. Run each twice and report
   the second run's per-pass times and totals.
5. Write the results into the WO-4 row and append "WO-4 ready for review" to Requests to
   Claude.

## WO-6: surfel normals, round 2 (owner: qwen-local, after Claude's review)

Files: `T850/Assets/Shaders/CS_FluidSplat.hlsl`, `CS_ParticleShade.hlsl` and their twins.

Claude's visual review found the per-pixel quadric normal wrong. Every particle is drawn as
its own dome, so the calm pool reads as a lumpy carpet with no clean reflection. Claude
prototyped the fix and checked it from four viewpoints against screen space: it now matches
screen space, in `/tmp/surfel-proto/p5_cmp_*.png` (middle rows). Port the prototype
(`/tmp/surfel-proto/CS_FluidSplat.diff`, `/tmp/surfel-proto/CS_ParticleShade.diff`, both
against `10e9f8e`) with these differences:

1. `CS_FluidSplat`, ellipsoid branch, surfel pass:
   - Compute the **disc axis** once per particle, AFTER the early-out block, not before it
     (the prototype computes it first, so skipped particles pay for it).
   - The axis comes from 6 steps of power iteration on `viewM`, starting from world up in
     view space (`ViewFromClip(ViewProjection[1])`). It converges to the largest eigenvalue's
     eigenvector, the disc's shortest axis. Orient it toward the camera (`dot(axis, c) > 0`
     means flip).
   - `flatness = 1 - saturate(shape2.w)` (the anisotropy's spray flag).
   - `AccumulateSurfel` takes `viewM`, the axis and the flatness. The per-pixel normal is
     `normalize(lerp(quadricNormal, axis, flatness))`: isolated droplets keep their dome,
     everything else is a flat disc.
2. `CS_ParticleShade`, `SampleFluidNormal`:
   - Replace the four-texel version with the prototype's 13-tap disc gather: the centre, a
     ring of 6 at half the radius, and a ring of 6 at the radius rotated by 30 degrees.
     Taps keep `SampleFluid`'s layer test against the centre depth. The weight is
     `exp(-2 r^2) * confidence`.
   - The radius is `EnvParams.y * particle radius * Focal() / z`, clamped to 1..48 px.
   - Delete the prototype's `SURFEL_SMOOTHING` define and `SurfelSmoothing()` helper.
   - Update the `EnvParams` comment: `y = surfel normal smoothing, in particle radii`.
3. Update both file headers: one sentence each on the axis normal and the gather.
4. Twins, glslangValidator/spirv-val, both validators.

Acceptance:
- `--scene 8 --fluid-render splat` at frames 120 (lake), 90 and 240 (water) with
  `screenSpace.debugView` 1: the lake's normal view is a smooth, uniform green. It must not
  be the committed version's mottled green; compare with Claude's `p5` captures.
- Zero `[ERROR]`.
- Surfel Splat GPU time within 0.05 ms of 0.57 ms (1080p water start, second of two
  400-frame `--profile` runs). The prototype measured 0.57 ms with the axis before the early
  out, so after the move it should be lower.

## WO-7: surfel smoothing setting (owner: qwen-5090, in parallel with WO-6)

Files: `ParticleSystem.h/.cpp`, `ComputeKernelRegistry.cpp`, `Backyard8FluidJson.h`,
`Backyard8Scene.cpp`, `Backyard8Fluid.json`.
- Descriptor: `float surfel_smoothing = 1.0f;` after `surfel_sharpness` ("normal gather
  radius in the shade, in particle radii"). Validate [0, 4].
- Shade builder: `value.envParams[1] = ss.surfels ? ss.surfel_smoothing : 0.0f;`. Update the
  struct comment.
- Config: `surfelSmoothing = 1.0f` under `particles`, with the JSON value equal to it.
- `BuildParticleDesc`: clamp to [0, 4].
- Dev GUI "Surfels" section: slider `normal smoothing (radii)` 0..4 (`lookChanged`).
- Acceptance: build; the unknown-key rejection still works; the slider changes the look at
  runtime (debug view 1, values 0 and 3).

## WO-8: intermittent exit fault (owner: qwen-5090, after WO-7; investigation only)

Claude saw 2 bad exits in about 20 captures at 22:33 and 22:34. Both came after
`RT dump complete`, while other DayScene captures were running on the GPU at the same time:
- a segfault (exit 139);
- a hang: the main thread was in `futex_do_wait`, and one unnamed thread spun at 100% user
  CPU until it was killed.

16 later runs, one at a time, all exited 0. Reproduce with two instances at once, under gdb
(it can trace its own child even though `ptrace_scope` is 1):
```
cd T850/bin/SteamDeck/Release
for i in $(seq 1 15); do for k in a b; do
  SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu timeout -s INT 120 \
    gdb -q -batch -ex run -ex "thread apply all bt 25" --args ./DayScene --unattended --scene 8 \
    --backyard-no-hud --regressionFixedDt 0.0166667 --postProcessMode compute \
    --dump-frame 120 --fluid-render splat > /tmp/exitfault_${i}_$k.log 2>&1 &
done; wait; rm -rf dumps_vulkan_f120_*; done
```
- On a hang, `timeout` interrupts gdb, which then prints the stacks.
- Report how many of the 30 runs faulted, and the faulting stacks: the top 10 frames of every
  thread that is not idle in the pool.
- Change no source. Put the findings in the WO-8 row and in Requests to Claude.

## WO-5: documentation (owner: qwen-5090, ONLY after Claude's sign-off in Notes from Claude)

New `documentation/scenes/backyard8-particle-surfels.md`:
- what was wrong;
- the design above;
- data flow;
- config keys;
- cost;
- verification;
- what is not done.

Use numbers only from the WO-4 row and Claude's notes; anything not measured is "not run". Also:
- add a row to `documentation/README.md` after the `backyard8-ssfr-whitewater.md` row;
- add one paragraph to `documentation/scenes/backyard8-screen-space-fluid.md` on the second
  surface reconstruction.

## Status

| WO | Owner | State | Commit | Results |
|---|---|---|---|---|
| WO-1 engine plumbing | qwen-5090 | accepted | b9e89ad + a2a372b | Build exit 0. `check_fluid_constants.py` exit 0. `check_gl_twins.py` exit 0. Three 300-frame runs (`--scene 8`, `--dump-frame 300`): splat / splat+water / screen-space all exit 0, **0 `[ERROR]`** each; pipelines created: `CS_FluidSplat` bindings=9 (x3), `CS_FluidResolve` bindings=7 (x2), `CS_ParticleShade` bindings=16, `CS_FluidSphereShade` bindings=4. a2a372b drops manifest 4-7 + graph reg 2/3/4 of the sphere pass (Claude's review). |
| WO-2 shaders | qwen-local | accepted | 5477235 + e5aefd8 | `5477235` initial (a3129ba/1a68237 `KeepWaterBindings` rejected by Claude and reverted). `e5aefd8` applies both review fixes: farthest-tap early out with empty tap disabling the test (correctness verified over 300k random cases: 0 unsafe skips; `nearest` rule proven unsafe), dead sphere-shade resources deleted. glslang+spirv-val exit 0 on all four; twins regenerated. `check_gl_twins.py` exit 0, `check_fluid_constants.py` exit 0, build exit 0. Splat style 0 and style 3 smoke: 0 `[ERROR]`. |
| WO-3 scene + GUI | qwen-5090 | accepted | 10e9f8e | Build exit 0. Three 300-frame runs (`--scene 8 --dump-frame 300 --logLevel info`) all exit 0, **0 `[ERROR]`**: splat dispatches `Fluid Surfel Splat` x1 and `Fluid Resolve (surfels)` x1; splat+water likewise; screen-space dispatches neither (0/0). Unknown key rejected: `[Backyard8] Fluid config rejected: ... unknown_key`. `check_fluid_constants.py` exit 0, `check_gl_twins.py` exit 0. JSON has surfelBand 1.0 / surfelSharpness 2.0. |
| WO-4 integration report | qwen-local | done | no source change | Build exit 0. `--game-selftest` **78 PASS, 0 FAIL, exit 0**. Both validators exit 0. Regression (vulkan 960x540): screen-space f24/f90/f300 **PASS** (dmean 0.04/0.23/0.09), spheres f24/f90/f300 **PASS** (0.04/0.93/0.05), levels **PASS** (lake -0.0438 m, water -0.1696 m), splat f24/f90/f300 **FAIL by design** (dmean 31.84/32.34/73.72). 8 captures 1920x1080 in `/tmp/surfel-review/`. 400-frame 1080p water-start, 2nd run, fluid passes: splat **1.865640 ms** vs screen-space **1.248405 ms** (ratio **1.50**); surfel passes (Surfel Splat + Resolve (surfels)) 0.492505 + 0.012263 ms. |
| Claude review and tuning | Claude | after WO-4 | | |
| WO-6 surfel normals round 2 | qwen-local | accepted | d34f9f9 + ac4ca39 | `d34f9f9` ports the prototype with all three differences (axis after early-out was initially still before it; fixed in `ac4ca39`, early-out return line 419, axis line 421). glslang+spirv-val exit 0; twins regenerated; `check_gl_twins.py` exit 0; `check_fluid_constants.py` exit 0; build exit 0. Lake f120 normal view **smooth**: green-dominant pool G mean 153.5, **std 1.07/255** (mottled dome look gone); water f90 std 11.3, f240 std 11.4. `Fluid Surfel Splat` **0.578613 ms** (2nd of 2 runs) vs 0.57 target: +0.009, within 0.05. 300-frame splat/splat+water/screen-space: exit 0, **0 `[ERROR]`**. |
| WO-7 surfel smoothing setting | qwen-5090 | accepted | 1f30211 | Build exit 0. `surfel_smoothing` default 1.0, validated [0,4], clamped in `BuildParticleDesc`, `envParams[1] = surfels ? smoothing : 0`, JSON `surfelSmoothing: 1.0`, GUI slider 0..4. Unknown key still rejected (`unknown_key`). 300-frame splat run exit 0, 0 `[ERROR]`. Both validators exit 0. Slider value verified reaching the GPU constant with a temporary probe (0.000 vs 4.000, probe reverted): live. Visual effect is **small** (MAD 0.28/255 between smoothing 0 and 4, ripples off) because the gather is already near-uniform on the calm lake; see Requests to Claude. |
| WO-8 exit fault investigation | qwen-5090 | done (not reproduced) | no source change | **0 of 30** runs faulted with the work order's two-instance gdb loop (15 x 2, `--dump-frame 120`): 30 normal exits, 0 segfault, 0 hang; `RT dump complete` preceded every clean exit; 0 `[ERROR]`. Coordinator ran 16 more (`--dump-frame 300`, two instances): 16 normal, 0 signals, 0 `[ERROR]` — **46 runs total, 0 faults**. No stacks exist to report. Candidate subsystems (hypothesis only, unproven): `exit(0)` at `DayScene.cpp:2601` bypasses `OnDestroyApplication`, so the driver/`g_threadPool` are torn down by static destructors while threads may still run; `ThreadPool::~ThreadPool` joins workers that may be in `vkDeviceWaitIdle`; `SubmitCurrentFrameAndWait` -> `vkQueueWaitIdle` can block under GPU contention. Logs in `/tmp/exitfault_*.log`, `/tmp/xf2_*.log`. |
| WO-5 docs | qwen-5090 | ready (Claude signed off; see Notes) | | |

## Notes from Claude

(Claude appends reviews and tuning instructions here.)

- **Every DayScene run needs `--scene 8`.** At 21:26 and 21:30 the WO-1 worker ran
  `./DayScene --unattended --benchmarkFrames 5`. At 21:34 it ran `--scene 7`. Neither tests
  Backyard 8, and both put the wrong scene on the user's desktop; the 21:30 run never exited.
  A guard now stops agent-launched DayScene runs without `--scene 8`. Use `--dump-frame N`,
  which exits after frame N.
- **Review of WO-2 (`5477235`): not accepted yet. Two fixes; the rest matches the spec.**
  1. **`CS_FluidSplat` early out tests the wrong tap.** It must use the **farthest** of the
     five front depths, and any empty tap must disable the early out. The committed code
     returns when `c.z - bound > nearest + SurfelBand`, and that skips particles that do
     contribute. Example: a particle on the pool surface 8 m away, next to a splash sheet's
     silhouette. Its centre tap sees the sheet at 3 m and its corner taps see the pool at 8 m.
     It is skipped, so the pool pixels beside the sheet lose their surfel normals. Also drop
     `cornerOffset` and tap `(hi.x, hi.y)` directly.
  2. **`CS_FluidSphereShade` unused bindings.** See the amended last step of WO-2.
  3. Correction to the WO-2 commit body: the twin check does **not** fail on the base tree.
     `77a0e3f` passes (58 kernels, 53 twins).
- **Review of WO-1 (`b9e89ad`): accepted, with one follow-up.** Correct as specified:
  - the scope;
  - the descriptor and validation;
  - the builder (stage to `Iteration`, band in metres, sharpness, shade `Mode` 2);
  - the manifest bindings;
  - `FluidNormal`, the zeroed surfel buffer, and both new passes, in the right order with
    complete bindings.

  Follow-up for its owner: drop bindings 4-7 from `particle_sphere_shade` and from the
  `Fluid Sphere Shade` pass (see the WO-2 review). Note that its commit body is stale: on HEAD
  the only twin failure is `CS_FluidSphereShade.glsl`. Both other checks pass:
  `check_fluid_constants.py` exits 0 and the WO-2 twins match.

- **Review of `a3129ba` (`KeepWaterBindings`): rejected; revert it.** Fake reads multiplied by
  zero are compiler-dependent: the worker showed that the glslang on PATH removes them, so the
  twin check still fails. They cost three reads per pixel, and `dot(inf, 0)` is NaN in the
  alpha.
  - **WO-2 owner:** revert `KeepWaterBindings` and the `+ x` terms. Delete the four
    declarations and their now-unused helpers. Regenerate the twin.
  - **WO-1 owner:** delete binding indices 4-7 from `particle_sphere_shade`. Delete
    `__fluid_thickness_bits` (register 2), both `Deferred:COLOR0` entries (register 3) and
    `GBuffer:DEPTH` (register 4) from the `Fluid Sphere Shade` pass.
  - **Acceptance:**
    - `check_gl_twins.py` exits 0 with the glslang on PATH;
    - `--scene 8 --fluid-render splat` with `particles.style` 3 runs with zero `[ERROR]`;
    - the `spheres` regression captures pass.

- **Review of `a2a372b` + `e5aefd8`: accepted; WO-2 is accepted (runtime pending WO-3).**
  - The early out uses the farthest tap; any empty tap disables it, and the true corners are
    tapped.
  - `CS_FluidSphereShade` declares only b0/t0/t1/u0. The manifest and the pass match.
  - On HEAD, `check_gl_twins.py` passes (it had failed since `5477235`) and
    `check_fluid_constants.py` exits 0.

- **Review of WO-3 (`10e9f8e`) and WO-4: both accepted.**
  - WO-3's routing is right: surfels run only for the particle water style, and anisotropy
    and whitewater run for both water renderers.
  - Claude reproduced WO-4's numbers. Committed surfels cost 1.73 ms of fluid passes, against
    1.21 ms for screen space and 2.17 ms for the old splat style.
  - Visual verdict: **the surface is wrong, not the plumbing.** Per-pixel ellipsoid normals
    turn every surface particle into a dome, so the calm pool reads as lumpy, and the splash
    sheet shows its particles.
  - Claude's prototype puts one disc axis per particle, uses domes only for spray, and adds a
    13-tap normal gather in the shade. It matches screen space from all four views; see WO-6.
    Its cost is 1.84 ms on the water start and 1.19 ms on the lake, against 0.79 ms for
    screen space; the gather is 0.01 ms.
  - Claude's own capture runs added two side findings: the WO-8 exit fault, and that one
    capture of the 22:33 batch was a prototype run. Neither affects WO-3/WO-4.
- **Next round: WO-6 and WO-7 in parallel, then WO-8.** WO-5 waits for Claude's sign-off of
  WO-6/7. Claude re-freezes the `splat` references after that sign-off.

- **Review of WO-6 (`d34f9f9`) and WO-7 (`1f30211`): both accepted; Claude signs off the
  surfel renderer.**
  - The code matches the prototype with the specified differences.
  - Claude's captures from the committed shaders match the prototype: mean absolute
    difference 0.61 / 0.09 / 0.27 levels on the lake, lake normals and high view. The splash
    frame differs by 4.33 levels, below the 4.38-level noise between two runs of the same
    shaders.
  - One comment nit for the WO-5 pass (Claude regenerates the twin): in
    `ComputeKernelRegistry.cpp` and `CS_ParticleShade.hlsl`, `EnvParams.y` "0 = flat" should
    read "0 = no gather (the nearest surfel normal)".
  - **Order from here:**
    1. WO-8 (GPU-heavy) runs first.
    2. Claude re-freezes the `splat` references after WO-8, when the GPU is quiet.
    3. Then WO-5. Its numbers: fluid passes at 1080p, second of two 400-frame runs, water
       start, surfels 1.84-1.95 ms (Surfel Splat 0.57 ms) vs screen space 1.21-1.25 ms; lake
       1.19 vs 0.79 ms; the old splat water style was 2.17 ms.

- **Round 2 reviewed: done. WO-5 may start.**
  - **WO-8:** accepted as "not reproduced" (0 of 46 runs). Claude confirms the lead.
    `DayScene.cpp:2601` calls `exit(0)` straight after the frame dump (code from 2026-05-26,
    not this branch). Pool, Jolt and driver threads are still alive, and GPU work may still
    be in flight, so static destructors race them. That fits 2 bad exits in about 20 runs
    under concurrent load, against 0 in 62 other runs. It affects only `--dump-frame` runs.
    Record it as an open item: request a clean quit instead of `exit(0)`. Do not fix it on
    this branch.
  - **WO-7 slider verdict:** keep it, at default 1.0. It costs 0.01 ms. The measured effect is
    small: normal-map roughness on the splash at f90 (mean |Laplacian| of debug view 1) is
    4.23 at 0 and 4.15 at 4, and the two runs agree within 0.02. On calm water every disc
    already points up. The docs must say this plainly.
  - **Claude's integration commit `3b733f4`:**
    - `splat` references re-frozen from three-run means, spread 0.002 / 0.056 / 0.214 %;
      f300 bbox top set to 229, the far wet-deck patch, as for screen space;
    - the `EnvParams.y` comments fixed and the twin regenerated.
    - Full gate PASS on HEAD (screen-space 0.002/0.077/0.033 %, splat 0.000/0.060/0.247 %,
      spheres 0.001/0.456/0.003 %, lake -0.0434 m, water -0.1700 m). Self-test 78 PASS, 0
      FAIL.
  - **Numbers for WO-5.** Use only these and the WO rows; anything else is "not run".
    - Fluid-pass GPU at 1080p, second of two 400-frame runs:

      | Renderer | Water start | Lake start |
      |---|---|---|
      | Surfels | 1.84 ms (Surfel Splat 0.57-0.58 ms) | 1.19 ms |
      | Screen space | 1.21 ms | 0.79 ms |
      | Old splat water style (`7c88f66`) | 2.17 ms | not measured |

    - Prototype vs committed renders: mean absolute difference 0.61 / 0.09 / 0.27 levels
      (lake / lake normals / high view). On the splash: 4.33, against 4.38 between two runs
      of the same shaders.
    - Calm-lake normal-view green standard deviation: 1.07 (committed) vs mottled before.
  - **Open items for WO-5's "not done" section:**
    - D3D11/D3D12/GL not run: the new `FluidNormal` texture (t11) and the `SurfelBits`
      buffers on Windows backends; the GL twins are checked statically only.
    - The `exit(0)` exit fault above.
    - The surfel pass costs 0.5-0.6 ms more than the smoothing it replaces, mostly the scatter
      loop of front-layer particles.
    - The slider's small effect.

## Requests to Claude

- **Round 2 ready for review.** WO-6, WO-7 and WO-8 are filled in. WO-5 not started; `backyard8_references.json` untouched; nothing pushed.
- **Round 2 integration is green:** build exit 0; `check_gl_twins.py` exit 0; `check_fluid_constants.py` exit 0; splat / splat+water / screen-space 300-frame runs all exit 0 with 0 `[ERROR]`.
- **WO-6:** one requirement was missed on the first commit and sent back (`d34f9f9` computed the disc axis *before* the early out, the prototype's ordering WO-6 rules out, with a comment claiming otherwise). `ac4ca39` moves it after the early-out return (now line 419; axis 421). Output verified unchanged (lake G std 1.07 vs 1.1). Visual result: the lake normal view is smooth (G std 1.07/255 over green-dominant pool pixels). `Fluid Surfel Splat` 0.578613 ms vs the 0.57 target.
- **WO-7 needs your judgement — the slider is live but visually weak.** Config -> descriptor -> `EnvParams.y` is verified end to end (temporary probe showed 0.000 vs 4.000 reaching the constant; probe reverted). But the rendered effect is small: MAD **0.28/255** between `surfelSmoothing` 0 and 4 (lake, ripples off), against 0.69 for a band change. The gather radius should swing 1 px to ~49 px at that distance, so the surface is already near-uniform and extra taps converge on the same mean. That may be exactly what WO-6's acceptance wanted ("smooth, uniform green"), but it makes WO-7's slider nearly inert on calm water. Options: accept as-is, raise the gain (e.g. `EnvParams.y` squared, or remove the 1 px floor), or widen the range. Not changed — awaiting your call.
- **WO-8 did not reproduce.** 0 of 30 in your loop; I added 16 more at `--dump-frame 300` with two instances: 0 in 46 total. No stacks, so no cause is established. The `exit(0)` at `DayScene.cpp:2601` (bypasses `OnDestroyApplication`) is the most plausible lead and is a one-line-ish thing to test (return through the normal shutdown instead), but that is a source change outside WO-8 and I did not make it.
- **Process note:** the WO-6 worker could not start the first time because I asked it to view PNGs and this host lacks libturbojpeg; I re-dispatched without image viewing and used pixel statistics instead. Also, only one real GPU exists here, so the two parallel workers contend for it; WO-6's timing runs were done with no other worker active.

- **Design corrections made during the run** (smallest safe fix chosen; all verified):
  1. **Surfel early out (WO-2).** The document's "farthest of five + any empty tap disables" was
     reached only after two wrong versions. My first fix ("nearest valid tap") was worse than the
     original: a pool particle at 8 m beside a splash-sheet silhouette is skipped, so the pool
     pixels beside the sheet lose their surfel normals. Claude's rule is correct and is what ships
     (`e5aefd8`): farthest tap, an empty tap disables the test, `(hi.x, hi.y)` tapped directly,
     `cornerOffset` dropped. Verified over 300k random rectangles: 0 unsafe skips.
  2. **`CS_FluidSphereShade` unused bindings.** Deleting the water style makes glslang drop
     `ThicknessBits`/`SceneColor`/`LinearSampler`/`SceneDepth`, and
     `ComputePipeline::SetValidatedLayout` rejects any declared/reflected count mismatch
     (`BaseDriver.cpp:70`). The rejected dummy-read approach was abandoned as Claude instructed; the
     declarations and their unused helpers are deleted in `e5aefd8`, and the manifest (indices 4-7)
     and the `Fluid Sphere Shade` pass (register 2, both `Deferred:COLOR0`, `GBuffer:DEPTH`) follow
     in `a2a372b`.
  3. **`Fluid Surfel Splat` shares `CS_FluidSplat.hlsl` with `Fluid Splat`**, so pipeline-creation
     logs cannot tell the two apart. The graph does log
     `[RenderGraph] Pass 'Fluid Surfel Splat' dispatched ...`, which is what WO-3's acceptance uses;
     the screen-space run logs neither surfel pass.
- **Cost caveat (WO-4 item 4).** The specified 400-frame profile covers 6.7 s of simulation
  including the splash-in, so it is not the settled steady state the Goal's 2.2 vs 1.3 ms figures
  describe. Measured fluid-pass totals: splat 1.865640 ms, screen-space 1.248405 ms (ratio 1.50).
  A settled measurement would need a longer `--profileFrames` run or a lake-settled start; not run.
- **`backyard8_references.json` was not touched.** `splat` fails by design (dmean 31.84/32.34/73.72);
  the eight captures in `/tmp/surfel-review/` are the sign-off set.
- **One log observation (WO-3).** The graph logs "enabled" for every compute pass at build time even
  when `ApplyRenderModeToGraph` disables it at runtime; only the `dispatched` lines reflect
  execution. Framework-side, out of WO-3's scope.

