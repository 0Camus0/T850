# Particle Fluid — Diagnosis 2: Screen-Space Rendering, Cross-Backend Work and Remaining Difficulty

Status: written 2026-11-08 after the screen-space fluid implementation, the config/Dev-GUI
rework, and the OpenGL image-origin fix. Vulkan and OpenGL are verified. D3D11/D3D12 create
and dispatch the new chain but their screen-space result is not reproduced yet; they keep the
splat resolve underneath so the fluid stays visible.

This is the sequel to [diagnosis.md](diagnosis.md). That document fixed the projection,
falloff, dispatch, solver and D3D root-signature defects. This one records what happened when
the renderer moved from "draw the particles as splats" to "reconstruct a screen-space surface",
and which parts of that are still hard.

## 1. What is verified

| Check | Result |
|---|---|
| Linux/Vulkan `--game-selftest` | 78 PASS / 0 FAIL |
| Windows MSBuild (`build.ps1 -Config Release -Platform x64`) | BUILD SUCCEEDED |
| `ValidateBuildRegistration.ps1` | PASS |
| `check_gl_twins.py` | PASS, 50 kernels / 45 generated twins |
| PBF + particle GLSL twins with `glslangValidator` | 17/17 clean |
| Vulkan screen-space frame 45 | particle RT 46,824 px, bbox `x[186,454] y[85,290]` at 640x360 |
| Vulkan screen-space frame 300 | settled surface visible, waterline highlight |
| OpenGL frame 45 (960x540) | 105,507 px, bbox `x[279,681] y[127,436]`; scaled to 640x360 `x[186,454] y[85,291]`, matching Vulkan |
| OpenGL frame 300 | matches the Vulkan settled pool |
| Runtime switch | `--fluid-render splat\|screen-space`; Dev GUI combo switches live |

## 2. Challenges and how they were resolved

### 2.1 The OpenGL storage-image origin

**Symptom:** the GL particle body was at `y[6,267]` while D3D12 was at `y[92,290]`, and the
pool wall never occluded it, even though the stamp's projected UV/Z ranges and tile counts
were identical.

**Root cause:** `GLDriver::SetViewport` flips the viewport for framebuffer rendering
(`targetHeight - y - h`), which makes graphics passes agree with the engine's top-left
convention. Compute dispatches have no viewport. `imageStore` and `texelFetch` therefore
addressed images from the bottom-left, upside down relative to every render target the
composite reads. The depth `texelFetch` had the same problem, so the occlusion test compared
against the wrong row.

**Fix:** `glBindImageTexture` cannot flip. The generated GL twin now flips the image Y
coordinate (`T850_IMAGE_Y`) and the scene-depth fetch behind a `T850_GL` define that
`generate_gl_compute_twins.ps1` adds to its common set. D3D and Vulkan are untouched.

**Evidence:** at 960x540 GL is 105,507 px, bbox `x[279,681] y[127,436]`; at 640x360 Vulkan is
46,824 px, bbox `x[186,454] y[85,290]`. Scaling GL by 2/3 gives `x[186,454] y[85,291]`. The
frame-300 images are visually equivalent.

**Lesson:** every new compute kernel that writes a storage image or fetches a screen-space
texture needs the `T850_GL` flip. The helper is in `CS_ParticleSplat.hlsl`,
`CS_ParticleDepth.hlsl`, `CS_ParticleSmooth.hlsl` and `CS_ParticleShade.hlsl`.

### 2.2 Surface depth from a particle cloud

The paper renders point sprites with depth replacement and keeps the **minimum** depth. The
first implementation did exactly that: per pixel, the nearest sphere front. The depth debug
view looked plausible, but the reconstructed normals were noise: the minimum is the particle
lattice envelope, and a one-pixel finite difference turns every splat bump into a large
normal tilt.

Two changes were needed:

1. **Weighted surface depth.** The depth pass stores the kernel-weighted average of the
   sphere fronts (`Σ w·front / Σ w`), not the minimum. It is the same surface low-pass
   filtered by the splat kernel; the minimum is still used for the scene-depth occlusion
   test.
2. **A larger normal stencil.** `normalStep` (default 4 px) samples the smoothed depth at a
   distance instead of adjacent texels.

The remaining ripple pattern on a falling sphere is the particle shell showing through; it
is a quality item, not the old per-pixel noise.

### 2.3 The normal formula was 400x too small

The paper's Eq. 4 uses `Cx = Vx/(2Fx)`, `Cy = Vy/(2Fy)` with `Fx,Fy` in pixels. Applying that
literally to **pixel-space** derivatives `∂z/∂x` leaves the x/y slope terms ~400x too small
(`Cx ≈ 1.4` versus a focal length of ~337 px), so every surface reconstructs as
fronto-parallel. The physically derived form for our projection is:

```
P(x,y) = ((x - W/2)·z/Fx, (y - H/2)·z/Fy, z)
n ∝ (-Fx·∂z/∂x, -Fy·∂z/∂y, -z)
```

with `Fx = Fy = H·P11/2`. The shader uses that form (`CS_ParticleShade.hlsl`). This was the
single largest visual fix: before it, Fresnel was always low and the water looked like a pale
band of the pool wall.

### 2.4 Smoothing is doing more work than the paper suggests

A 3x3 Laplacian is useless on a particle splat: one splat is 10-20 pixels across. The first
attempt produced rainbow noise in the normals. The implementation now uses four ping-pong
passes of a **tolerance-guarded radial weighted average** (`smoothingRadius` default 10 px),
with the paper's silhouette boundary condition: neighbours whose view depth differs by more
than `depthTolerance` do not contribute. This is a stable approximation of curvature flow and
is cheap to reason about; it is not the explicit PDE.

The four passes always exist in the graph. `smoothingIterations` (0-4) selects how many do
work; the rest copy. That keeps the final depth in a fixed target (`DepthA`) regardless of the
runtime count.

### 2.5 Thickness and the pale-water problem

Thickness is the kernel-weighted sum of per-particle sphere chords along the ray, not the
paper's exact volume integral. It is enough to drive transparency and Beer-Lambert absorption,
but it is coupled to the splat radius and the occlusion band.

The water initially looked pale because it was physically plausible: the camera views the
surface at about 23 degrees, where water reflects only ~10% (Fresnel ≈ 0.1), and the refracted
background is a white pool wall. The fix was not "more reflection"; it was a stronger,
per-metre absorption tint, a darker scattering term, and a sky-coloured reflection blend for
rays that leave the screen. The surface now reads as water, but the exact look is a tuning
problem, which is why every value is in JSON and in the panel.

### 2.6 Screen-space reflection without SSR

True reflection would trace the reflected ray against the depth buffer or sample the
environment cubemap. The compute path has a scene colour texture and no cubemap binding, so the
implementation samples the scene at a normal-offset UV and blends toward a configurable sky
colour by the reflected ray's world Y. For a flat water plane that is closer to the desired
look than sampling the wall directly behind the pixel. The gap is listed in Section 4.

### 2.7 Runtime switching and shader dead-code elimination

The switch is a constant (`ParticleSystemDesc::render_mode`), read by the constants builders
every frame. The graph is static; no pipeline or graph rebuild is needed. The shade pass
early-outs in Splat mode and leaves the splat target untouched.

Two traps surfaced:

- **D3D11 validates the reflected layout against the manifest.** A forced early return in a
  diagnostic shader made FXC strip the now-unused sampled textures and sampler; pipeline
  creation then failed with "failed to create compute pipeline" and no compile error. Any
  diagnostic that returns early must keep every declared binding used.
- **The splat resolve cannot simply be skipped.** In screen-space mode it was initially
  skipped to save the O(particles) loop. When the D3D screen-space output did not appear, the
  target stayed empty. The final design keeps the splat resolve active as a fallback and the
  shade pass does **not** write zero on empty coverage, so a backend that fails to produce a
  screen-space surface still shows the fluid.

### 2.8 Config-driven scene and the Dev GUI

All scene-8 values moved to `Assets/Scenes/Backyard8Fluid.json` with strict unknown-key
rejection. Command-line flags override the file. The Dev GUI no longer calls the VOF panel; it
draws the particle-fluid panel and writes live edits back into the in-memory config.

Two ownership details matter:

- `PbfFluid::ResetToRest` rebuilds the particle buffers. The `ParticleBufferRegistry` must be
  re-published and the particle system re-registered after a reset, or the renderer points at
  freed buffers. `Backyard8Scene::ApplyStartMode` now does both.
- Changing spacing, sphere size, caps or heightfield size requires a full `TryInitialize`
  (buffers are sized at creation). The panel applies those on the explicit **Apply & re-seed**
  button, not mid-drag. The surface RT is destroyed and recreated with the new dimensions.

## 3. The unresolved D3D11/D3D12 screen-space output

This is the main open renderer defect.

What is known:

- All four new pipelines create and dispatch on both D3D11 and D3D12.
- D3D11 reflection reports the shade pipeline as `constants=1 textureSRVs=2
  textureUAVs=1[0] samplers=1`; the graph binds the same layout.
- The splat resolve writes the same `FluidParticles` target successfully at `u3`.
- The shade pass writes at `u0` in the repo shader. Moving it to `u3` did **not** make the
  output land.
- A forced magenta write with all declared bindings kept live still produced a zero target in
  one run. A pre-dimension forced write made FXC strip bindings and fail pipeline creation,
  so that probe is not conclusive.
- The final build keeps the splat underneath. D3D11 shows a visible fluid (a smooth blue body)
  rather than a black pool; whether that is the splat, a flat shade, or a mix is not proven.

Next diagnostics, in order:

1. Capture a D3D11 frame in RenderDoc (Windows host or a Windows VM) and inspect the shade
   dispatch's UAV binding and the `FluidParticles` texture contents.
2. Write a minimal `RWTexture2D` compute kernel with **no** sampled textures, bind it to
   `FluidParticles`, and confirm the write lands. Then add one sampled texture at a time.
3. Log the actual `ID3D11UnorderedAccessView*` and its resource pointer for the shade dispatch
   (the reflection log now exists; the binding pointer does not).
4. Confirm the depth/smoothing targets are non-zero on D3D independently of the shade, using a
   debug pass that copies `DepthA` to a dumped target.

Until that is done, treat D3D11/D3D12 as **splat fallback**, not SSFR.

## 4. What is still genuinely difficult

### 4.1 Exact curvature flow

The current radial low-pass removes the lattice, but it is not the paper's `∂z/∂t = H`. A
faithful implementation needs the finite-difference mean-curvature step (Eqs. 1-8), a stable
time step at the target resolution, and the same silhouette boundary condition. The stiff PDE
is the reason the paper suggests half-resolution and interpolation; getting this stable at
4K without a pyramid is the hard part.

### 4.2 Half-resolution processing

Depth and smoothing at half resolution, then upsample depth/normals for shading, is the main
performance lever. It requires either a second set of RTs at `size/2` (the graph currently
sizes RTs from the viewport or explicit pixels) or a compute-buffer pyramid. The normal
reconstruction must be aware of the scale, and the silhouette tolerance must scale too.

### 4.3 Perlin foam and advected noise

The paper assigns one noise octave per particle and sums it into the surface, with velocity
change marking "violent" particles. Our PBF solver publishes positions and velocities, not
per-particle age or a density/velocity-change flag, and the shade pass has no per-particle
loop. A faithful implementation needs either a third particle pass that writes noise/foam into
a screen-space target, or a per-particle attribute buffer. It is a real feature, not a shader
tweak.

### 4.4 Environment reflection and refraction

A cubemap reflection needs the environment map bound to the compute pass (the graph can bind
sampled 2D textures; cubemaps through compute reflection are not exercised yet), or a depth
ray-march SSR. Screen-space refraction currently offsets the scene-colour lookup; a physically
better version reconstructs the background position from the refracted ray and the scene
depth.

### 4.5 Spray separation

The paper thresholds low-density particles and renders them separately as spray. The PBF
`Lambda.y` buffer is the density signal, but it is not published to the renderer, and a
spray pass needs a different look (splat/points) from the coherent surface. The runtime switch
currently selects one renderer for the whole cloud.

### 4.6 D3D11/D3D12

Section 3. This is an engine/backend issue, not a paper issue, but it blocks cross-backend
parity.

### 4.7 Solver and scene lifecycle

Still open from the earlier plan:

- R5 durable scatter (the current scatter is rebuilt each substep).
- L1 re-seed use-after-free (mitigated in the scene by re-publishing, but the registry
  ownership model is still fragile).
- L2 registry ownership, L4 skip heightfield work while the mesh is hidden, L6 start modes,
  L7 shutdown ordering.

### 4.8 Performance and redundancy

The splat resolve stays active in screen-space mode as a fallback. That is deliberate for
correctness, but it means screen-space mode pays for both renderers. A graph-stage enable flag
(or a backend capability query) should skip the unused path once D3D parity is settled.

## 5. How to reproduce

```bash
# Build (Linux)
cmake -S T850 -B T850/build/linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DT850_PLATFORM_STEAM_DECK=ON \
  -DT850_BUILD_EDITOR=OFF -DT850_AUTO_INSTALL_VCPKG_DEPS=OFF
cmake --build T850/build/linux --target DayScene --parallel 24

# Gameplay gate
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu \
  T850/bin/SteamDeck/Release/DayScene --unattended --game-selftest

# Vulkan screen-space frame
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu \
  T850/bin/SteamDeck/Release/DayScene --unattended --api vulkan --scene 8 \
  --backyard-no-hud --width 960 --height 540 --regressionFixedDt 0.0166667 \
  --dump-frame 300 --postProcessMode compute --logLevel info

# Runtime switch (start-up); the panel's renderer combo does the same live
... --fluid-render splat
... --fluid-render screen-space

# GL twin guard
python3 T850/scripts/validation/check_gl_twins.py --shaders T850/Assets/Shaders
```

Windows: `T850/scripts/build.ps1 -Config Release -Platform x64 -Action Build`, then the GL
run under GE-Proton with `--api gl`. The twin generator needs the Windows host and the Vulkan
SDK (`scripts/shaders/generate_gl_compute_twins.ps1 -Force -SdkRoot <sdk>`).

## 6. File map

| Concern | File |
|---|---|
| Scene config | `T850/Assets/Scenes/Backyard8Fluid.json`, `DayScene/Backyard8FluidJson.{h,cpp}` |
| Scene lifecycle + panel | `DayScene/Backyard8Scene.{h,cpp}` |
| CLI | `DayScene/App.cpp` (`--fluid-render`, `--fluid-config`, `--pbf-*`) |
| Particle descriptor / modes | `Framework/include/scene/ParticleSystem.h` |
| Render-graph pass fields | `Framework/include/scene/RenderGraphDescriptor.h`, `Framework/src/scene/RenderGraph.cpp` |
| Constants builders | `Framework/src/utils/ComputeKernelRegistry.{h,cpp}` |
| Splat + GL flip | `T850/Assets/Shaders/CS_ParticleSplat.hlsl` |
| Surface depth | `T850/Assets/Shaders/CS_ParticleDepth.hlsl` |
| Smoothing | `T850/Assets/Shaders/CS_ParticleSmooth.hlsl` |
| Shading | `T850/Assets/Shaders/CS_ParticleShade.hlsl` |
| Kernel manifest | `T850/Assets/Shaders/compute_kernels.json` |
| Graph passes | `T850/Assets/Scenes/Backyard8Scene_RenderGraph.json` |
| GL twins | `T850/scripts/shaders/generate_gl_compute_twins.ps1`, `check_gl_twins.py` |
| Paper scope | [backyard8-screen-space-fluid.md](backyard8-screen-space-fluid.md) |

## 7. Honest assessment

The renderer now implements the paper's **main visual idea**: a particle cloud becomes a
smooth depth surface with depth-derived normals, Fresnel, refraction, thickness absorption and
a specular highlight, and it can be switched off live. On Vulkan and OpenGL it looks like
water instead of a flat splat.

It is **not** a complete implementation of the paper, and it is **not** cross-backend yet.
The largest fidelity gaps are exact curvature flow, half-resolution processing and Perlin
foam; the largest engineering gap is the D3D11/D3D12 shade output. The next honest milestone
is not "more knobs"; it is: exact smoothing step, half-res path, then D3D parity or an explicit
backend capability declaration that selects the splat on D3D until parity is fixed.
