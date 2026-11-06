# Device profiles: data-driven per-platform tuning

Status: **planned, not started.** Scope: Android first (tablet and phone), Steam Deck and desktop
kept at today's values, web inheriting the Android baseline. Everything is driven by JSON; no
quality value may be hardcoded in C++ once this lands.

## 1. Why

Measured on the SM-X920 tablet (MediaTek MT6989, Mali-G720-Immortalis MC12, 2960×1848 panel) with
the engine's own Vulkan GPU timestamps, 240 frames per run, Backyard scene 7:

| Pass | GPU ms | Note |
|---|---|---|
| Shadow Accumulation | **7.25** | full-screen cascade blend, 4 cascades |
| Deferred Light Volumes | **5.45** | `light_volume_steps: 256` |
| Forward Transparent (water) | 4.60 | includes 82,944 water triangles/frame |
| GBuffer | 3.28 | |
| Opaque Scene Copy | 3.07 | |
| Deferred | 2.38 | |
| Everything else (shadow depth, blurs, HDR, bloom, composite, backbuffer) | ≈4.8 | |
| **GPU total** | **≈30.5** | |
| Fluid solver | ≈6.3 | 100×22×50, 120 iterations, 2.3 substeps/frame |
| **Frame** | **41.7 ms → 24 fps** | |

Same engine, same resolution, **no water (scene 6): 126 fps.** Dropping the display to 1480×924
scaled every full-screen pass by ~4×, so the scene is fill-rate bound, not solver bound. The tablet
also reached Android Thermal Status 2 (MODERATE, AP 46.8 °C) after ~40 minutes, dropping to 14–21 fps.

The desktop preset is being used unchanged on all platforms. Android needs its own numbers, and those
numbers belong in data, not in compiled constants.

## 2. Current state

### What is already JSON

| File | Controls | Per-platform today |
|---|---|---|
| `Assets/Scenes/BackyardScene.json` → `quality` | `shadow_map_resolution`, `light_volume_steps` (256), `ssao_kernel_size`, `pcf_*`, parallax, DOF samples | no |
| `Assets/Scenes/BackyardScene.json` → `settings` | `shadow_enabled`, `ssao_enabled`, `godrays_enabled`, … | no |
| `Assets/Scenes/BackyardScene_RenderGraph.json` | passes, targets, `cascade_count` (4), `size`/`size_ref` | no |
| `Assets/Scenes/BackyardWater.json` | `pressureIterations` (120), `maxSubstepsPerFrame`, `ssrSteps`, `ssrEnabled`, … | no |
| `Assets/Scenes/DayScene.json`, `MinecraftScene.json` → `profiles[]` | scene sandbox sliders, matched on `name`/`platform`/`architecture`/`gpu_family` | mechanism exists, one `shared` entry, Backyard has none |

### What is hardcoded

| Location | Constant | Effect |
|---|---|---|
| `Framework/src/scene/RenderGraph.cpp` (`kAndroidScreenRenderScale`) | `0.5f` | every screen-sized render target on Android |
| `Framework/include/scene/WaterTunables.h` (`kMobilePressureIterations`) | `60` | iteration cap on Android, web and Steam Deck |
| `Framework/src/utils/RuntimeProfile.cpp` (`RecommendedProfile`) | `"android/mali-g700"`, `"android/adreno-800"`, … | profile selection rules |
| `Assets/Scenes/BackyardScene.json` `quality.light_volume_steps` | `256` | 5.45 ms pass |

## 3. Design

### 3.1 File and location

`T850/Assets/Config/DeviceProfiles.json` — inside `Assets/` because gradle only syncs `Assets` into
the APK; the repository-root `config.json` never reaches Android or the web bundle.

```json
{
  "version": 1,
  "select": [
    { "profile": "steam-deck/default", "when": { "platform": "linux", "steam_deck": true } },
    { "profile": "android/default",    "when": { "platform": "android" } },
    { "profile": "web/default",        "when": { "platform": "webgpu" } },
    { "profile": "desktop/default",    "when": { "platform": ["windows", "linux"] } }
  ],
  "profiles": {
    "desktop/default": {
      "render": { "screen_scale": 1.0 },
      "graph":  { "shadow_cascades": 4, "shadow_accum_scale": 1.0, "shadow_blur": true,
                  "light_volume_steps": 256 },
      "water":  { "pressure_iterations": 120, "max_substeps": 4, "ssr": true, "ssr_steps": 48 },
      "assets": { "backyard_water_mesh": "Models/Backyard/Backyard_Water.glb" }
    },
    "android/default": {
      "extends": "desktop/default",
      "render": { "screen_scale": 0.45 },
      "graph":  { "shadow_cascades": 2, "shadow_accum_scale": 0.5, "shadow_blur": false,
                  "light_volume_steps": 32 },
      "water":  { "pressure_iterations": 60, "max_substeps": 2, "ssr_steps": 16 },
      "assets": { "backyard_water_mesh": "Models/Backyard/Backyard_Water_Low.glb" }
    },
    "steam-deck/default": {
      "extends": "android/default",
      "render": { "screen_scale": 1.0 },
      "water":  { "pressure_iterations": 60, "max_substeps": 4, "ssr_steps": 48 }
    },
    "web/default": { "extends": "android/default" }
  }
}
```

Every value above reproduces today's behaviour for desktop and Deck, so P1 ships with no visual or
performance change outside Android.

### 3.2 Semantics

- **Optional overrides.** A field absent from a profile leaves whatever the scene JSON, water JSON or
  `config.json` already said. Delete the profile file and the engine behaves exactly as today — no
  fallback constants are introduced in C++.
- **`extends`** merges parent → child, child wins; cycles are a validation error.
- **Precedence:** engine defaults → scene JSON (`quality`, `settings`) → water JSON → device profile →
  CLI flags (`--deviceProfile`, `--renderScale`, `--backyard-iterations`).
- **Selection:** first matching rule wins; `when` supports exact strings, arrays (any-of) and `*`
  wildcards. Match inputs come from data the engine already has: `RuntimeProfileInfo` (`platform`,
  `architecture`, `gpuName`, `gpuVendor`, `gpuFamily`, `deviceModel`) plus `DeviceInfo.steamDeck` and
  the backbuffer size. RAM and core count are optional additions.
- **Audit line.** On startup, one INFO line names the profile, the matching rule index and every value
  that differs from the scene JSON, e.g.
  `[Profiles] android/default (rule 2: platform=android) render_scale 1.00->0.45, cascades 4->2,
  light_volume_steps 256->32, water iterations 120->60, ssr 48->16, water mesh ->_Low.glb`.

### 3.3 Field map

| Profile field | Feeds | Replaces |
|---|---|---|
| `render.screen_scale` | render targets sized from the backbuffer | `kAndroidScreenRenderScale` |
| `graph.shadow_cascades` | `shadow_projections[].cascade_count` | fixed 4 in the graph JSON |
| `graph.shadow_accum_scale` | new `scale` field on render targets (`ShadowAccum`) | — |
| `graph.shadow_blur` | new `enabled` field on passes (`Shadow Blur V/H`) | — |
| `graph.light_volume_steps` | `SceneProp.LightVolumeSteps` | fixed 256 |
| `water.pressure_iterations` | `WaterTunables::EffectivePressureIterations` | `kMobilePressureIterations` |
| `water.max_substeps` | `WaterTunables::maxSubstepsPerFrame` | — |
| `water.ssr` / `water.ssr_steps` | `WaterMisc0.z` / `WaterMisc0.x` | — |
| `assets.backyard_water_mesh` | Backyard water asset entry | single GLB |

## 4. Plan

| ID | Task | Files | Acceptance |
|---|---|---|---|
| **DP-01** | `DeviceProfile` loader, selector, resolver (`extends`, wildcards, precedence), typed accessors | new `Framework/include/core/DeviceProfile.h`, `Framework/src/core/DeviceProfile.cpp`, glaze schema | Unit test resolves each shipped profile; deleting the file changes nothing |
| **DP-02** | Replace `RecommendedProfile()`'s if-chain with the JSON rules; keep the returned string for the existing scene-profile matcher | `utils/RuntimeProfile.cpp` | Same profile name returned for the tablet, Deck and desktop as today |
| **DP-03** | `Assets/Config/DeviceProfiles.json` with the four profiles above | new asset | Loads on desktop, APK and web bundle |
| **DP-04** | Startup audit line + `--deviceProfile`, `--deviceProfileList`, `--dumpDeviceProfile` | `Config`, `App.cpp`, `ConfigRuntime.cpp` | Forced profile beats the rules; dump prints resolved JSON |
| **DP-05** | RenderGraph: `screen_scale` from the profile; generic `scale` on targets and `enabled` on passes | `RenderGraph.cpp/.h`, `RenderGraphDescriptor.h` | Desktop RT sizes unchanged; Android RT = round(surface × scale) |
| **DP-06** | Apply `shadow_cascades`, `shadow_accum_scale`, `shadow_blur` | `RenderGraph.cpp` | Tablet profile yields 2 cascades and no blur passes |
| **DP-07** | Apply `graph.light_volume_steps` before RT creation | `BackyardScene.cpp`, `SceneSetup` | 32 in the tablet log, 256 on desktop |
| **DP-08** | Water overlay: iterations, substeps, SSR; delete `kMobilePressureIterations` and its reason strings | `WaterTunables.h`, `BackyardScene.cpp` | Desktop 120/48; tablet 60/2/16; Deck 60 |
| **DP-09** | Low-poly water asset `Backyard_Water_Low.glb` (144×72) selected by the profile | `scripts/blender/build_backyard.py`, `BackyardScene.cpp` | Tablet log names the low mesh; 20,736 tris instead of 82,944 |
| **DP-10** | `ValidateDeviceProfiles.ps1` (schema, unknown keys, `extends` cycles, asset existence, one profile per platform), wired into `validate-build-registration` | `scripts/`, `.github/workflows/build.yml` | Fails CI on a broken profile file |
| **DP-11** | Documentation: this file, `runtime-configuration.md`, `platform/android.md` | documentation | New keys documented with defaults |

## 5. Verification

1. **Desktop parity (blocks merge).** Scene 7 on D3D12 and Vulkan, `desktop/default`: screenshots and
   the visual baseline comparison must be identical before and after P1/P2/P3.
2. **Deck parity.** `steam-deck/default` must resolve to 60 iterations, full render scale, and pass
   Backyard gates 1–13 as today.
3. **Tablet.** Per-pass GPU timestamps before/after for the passes in §1, plus FPS at native panel
   size, plus a 20-minute run to confirm the thermal drift improves.
4. **Gates.** `--backyard-selftest` gates 1–13 must pass under the Android profile (60 iterations,
   2 substeps) — not only under the desktop one.
5. **Web.** `web/default` resolves and the bundle still passes the browser compute self-test.
6. **Validation gate.** `ValidateDeviceProfiles.ps1` green in CI.

## 6. Expected result

| Knob | Now | Tablet profile | Expected |
|---|---|---|---|
| `screen_scale` | 0.50 | 0.45 | ×0.81 on GBuffer, Opaque Copy, Deferred, HDR, BackBuffer |
| Shadow accumulation | 7.25 ms, 4 cascades, blur | 0.5 scale, 2 cascades, no blur | ≈2.0 ms |
| Light volumes | 5.45 ms, 256 steps | 32 steps | ≈1.2 ms |
| Water solver | ≈6.3 ms | 60 iterations, 2 substeps | ≈2.6 ms |
| Water pass | 4.60 ms, 48-step SSR, 83k tris | 16-step SSR, 21k tris | ≈3.0 ms |
| **Frame** | **41.7 ms (24 fps)** | | **≈16–18 ms (55–60 fps)** |

Numbers are projections from the measured per-pass costs, not promises; they will be replaced with
measurements when P2/P3 land.

## 7. Open items

- `screen_scale` floor: 0.45 vs 0.40 — measure both on the tablet and pick from screenshots plus FPS.
- `web/default` inheritance: confirm that 0.45 is acceptable in a browser tab, otherwise pin it.
- RAM/core-count probes for matching are optional and only added if a phone profile needs them.
- Steam Deck keeps 4 substeps for now; revisit if the Deck profile shows budget limiting.

## 8. Rollback

The profile file is additive: deleting `Assets/Config/DeviceProfiles.json` restores today's behaviour
once DP-05/06/07/08 read only from it (no compiled defaults). Every stage is a separate commit, so a
single revert removes the Android tuning without touching the desktop path.
