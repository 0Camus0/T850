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

- Touch only the files your work order owns. Commit only them, with a pathspec:
  `git commit -m "WO-n: ..." -- <paths>`. Do not stage or commit anybody else's files, do not
  amend or rebase, never push.
- Commit subject starts with `WO-n:`; the body lists what you ran and the exact results.
- Build: `cd T850 && cmake --build build/linux --target DayScene -j$(nproc)`. Shaders compile at
  runtime (no rebuild for `.hlsl` edits).
- Run: from `T850/bin/SteamDeck/Release`, always `--unattended`, with
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

Keep the cbuffer and the bindings exactly as they are. Update the header comment.

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
| WO-1 engine plumbing | qwen-5090 | ready | | |
| WO-2 shaders | qwen-local | ready | | |
| WO-3 scene + GUI | qwen-5090 | after WO-1 | | |
| WO-4 integration report | qwen-local | after WO-1..3 | | |
| Claude review and tuning | Claude | after WO-4 | | |
| WO-5 docs | qwen-5090 | after Claude's sign-off | | |

## Notes from Claude

(Claude appends reviews and tuning instructions here.)

## Requests to Claude

(append here)
