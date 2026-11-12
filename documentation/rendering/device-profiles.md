# Device profiles: data-driven per-platform tuning

Status: **planned, not started.** Scope: Android first (tablet and phone); Steam Deck, desktop and
web keep today's values until a measurement says otherwise. Everything is driven by JSON; a profile
carries only the values that differ from the scene/water JSON, so those files stay the single source
of truth for desktop.

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
| Fluid solver | ≈6.3 | 100×22×50, **60** iterations (Android is capped at compile time, `kMobilePressureIterations`; the tablet logs `60 pressure iterations (mobile/web cap)`), 2.3 substeps/frame |
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
| `Assets/Scenes/BackyardWater.json` | `pressureIterations` (120), `maxSubstepsPerFrame` (4), `adaptiveSubsteps`, … The file lists **overrides only** (30 of 115 keys); `ssrSteps` (48) and `ssrEnabled` take the `WaterTunables` defaults | no |
| `Assets/Scenes/BackyardScene_RenderGraph.json` `shadow_projections[]` | `cascade_count`, `resolution`, `split_lambda`, distances | **override path exists**: `RenderGraph::MergeShadowOverrides` (base + runtime `ShadowProjectionOverrideDesc`, validated 1..6 cascades) |
| `Assets/Scenes/DayScene.json`, `MinecraftScene.json` → `profiles[]` | scene sandbox sliders, matched on `name`/`platform`/`architecture`/`gpu_family` | mechanism exists, one `shared` entry, Backyard has none |

### What is hardcoded

| Location | Constant | Effect |
|---|---|---|
| `Framework/src/scene/RenderGraph.cpp` (`kAndroidScreenRenderScale`) | `0.5f` | every screen-sized render target on Android |
| `Framework/include/scene/WaterTunables.h` (`kMobilePressureIterations`) | `60` | iteration cap on Android (compile time), phone/tablet/unclassified browsers (`--webDevice`, run time) and Steam Deck (run time, `DeviceInfo`) |
| `Framework/src/utils/RuntimeProfile.cpp` (`RecommendedProfile`) | `"android/mali-g700"`, `"pc/x64"`, … | **scene sandbox profile** names matched by `DayScene.json`/`MinecraftScene.json` `profiles[]`; not a device-quality profile |
| `Framework/src/utils/RuntimeProfile.cpp` (`DetectPlatform`) | `android`/`windows`/`linux`, else `unknown` | **the web build reports `unknown`**: there is no `OS_WEB` branch |
| `Assets/Scenes/BackyardScene.json` `quality.light_volume_steps` | `256` | 5.45 ms pass |

## 3. Design

### 3.1 File and location

`T850/Assets/Config/DeviceProfiles.json` — inside `Assets/` because gradle only syncs `Assets` into
the APK; the repository-root `config.json` never reaches Android or the web bundle.

```json
{
  "version": 1,
  "select": [
    { "profile": "steam-deck/default", "when": { "steam_deck": true } },
    { "profile": "android/default",    "when": { "platform": "android" } },
    { "profile": "web/default",        "when": { "platform": "web" } },
    { "profile": "desktop/default",    "when": { "platform": ["windows", "linux"] } }
  ],
  "profiles": {
    "desktop/default": {},
    "steam-deck/default": {
      "extends": "desktop/default",
      "water":  { "pressure_iterations": 60 }
    },
    "web/default": {
      "extends": "desktop/default",
      "water":  { "pressure_iterations": 60 }
    },
    "android/default": {
      "extends": "desktop/default",
      "render": { "screen_scale": 0.45 },
      "graph":  { "shadow_cascades": 2, "shadow_accum_scale": 0.5, "shadow_blur": false,
                  "light_volume_steps": 32 },
      "water":  { "pressure_iterations": 60, "ssr_steps": 16 },
      "assets": { "backyard_water_mesh": "Models/Backyard/Backyard_Water_Low.glb" }
    }
  }
}
```

Why it looks like this:

- **`desktop/default` is empty.** Copying 120 sweeps, 4 substeps, 48 SSR steps, 256 light steps and
  4 cascades into the profile would make it a second source of truth: an edit to
  `BackyardWater.json` or the scene JSON would be silently overridden on desktop.
- **The Deck extends desktop, not Android.** Extending Android would hand it 2 cascades, no shadow
  blur, 32 light steps and the low water mesh, which contradicts "Deck parity" in section 5. Its only
  delta is 60 sweeps, the owner's 2026-11-02 decision. `steam_deck` matches on Windows too.
- **Web extends desktop.** The browser measured 116–140 fps at 1262×532 with today's quality
  (staged plan, section 4). Cutting it to the Android set needs a measurement first.
- **Platform `web`, not `webgpu`.** `webgpu` is an API: native Windows WebGPU must stay desktop.
  `DetectPlatform()` gets an `OS_WEB` branch returning `"web"` (DP-01).
- **No `max_substeps` on Android** (see section 7).

On the shipped file, desktop, Deck and web behave exactly as today.

### 3.2 Semantics

- **Optional overrides.** A field absent from a profile leaves whatever the scene JSON, water JSON
  or `config.json` already said.
- **The file is required where it changes anything.** Android and web get the file through
  packaging that filters assets (the APK's `--asset-profile`, the web Cloud-mode pruning). A file
  silently missing there would put the tablet on desktop quality: full resolution and no cuts, about
  10 fps.
  - So on Android and web, a missing or invalid file logs an `[ERROR]` naming the path.
  - The engine then falls back to the two compiled safe values that exist today: render scale 0.5
    on Android, 60 sweeps on Android and web.
  - Those constants stay, renamed as fallbacks; they are not deleted.
  - On desktop a missing file is an `[INFO]` and changes nothing.
- **`extends`** merges parent → child, child wins; cycles are a validation error.
- **Precedence:** engine defaults → scene JSON (`quality`, `settings`) → water JSON → device profile →
  CLI flags (`--deviceProfile`, `--renderScale`, `--backyard-iterations`).
- **Selection:** first matching rule wins; `when` supports exact strings, arrays (any-of) and `*`
  wildcards.
  - The match inputs are data the engine already has: `RuntimeProfileInfo` (`platform`,
    `architecture`, `gpuName`, `gpuVendor`, `gpuFamily`, `deviceModel`), `DeviceInfo.steamDeck`,
    and the backbuffer size. RAM and core count are optional additions.
  - **Device profiles are not scene sandbox profiles.** `RecommendedProfile()` returns names like
    `pc/x64` and `android/mali-g700`, which `DayScene.json`/`MinecraftScene.json` `profiles[]` and
    `GetProfileTargets()` match against. Device profiles get their own names and selector and leave
    `RecommendedProfile()` alone.
- **Audit line.** On startup, one INFO line names the profile, the matching rule index and every value
  that differs from the scene JSON, e.g.
  `[Profiles] android/default (rule 2: platform=android) render_scale 1.00->0.45, cascades 4->2,
  light_volume_steps 256->32, water iterations 120->60, ssr 48->16, water mesh ->_Low.glb`.

### 3.3 Field map

| Profile field | Feeds | Replaces |
|---|---|---|
| `render.screen_scale` | render targets sized from the backbuffer | `kAndroidScreenRenderScale` (kept as the Android fallback) |
| `graph.shadow_cascades` | the **existing** runtime `ShadowProjectionOverrideDesc` for `sun-shadow` (`cascade_count`; `resolution` is available the same way) | — (no new mechanism) |
| `graph.shadow_accum_scale` | new `scale` field on render targets (`ShadowAccum`) | — |
| `graph.shadow_blur` | disables **both** `Shadow Blur V` (`ShadowAccum` → `ExtraHelper`) and `Shadow Blur H` (`ExtraHelper` → `ShadowAccum`) | — |
| `graph.light_volume_steps` | `SceneProp.LightVolumeSteps` | fixed 256 |
| `water.pressure_iterations` | `WaterTunables::EffectivePressureIterations` | `kMobilePressureIterations` (kept as the fallback) |
| `water.max_substeps` | `WaterTunables::maxSubstepsPerFrame` | — |
| `water.ssr` / `water.ssr_steps` | `WaterMisc0.z` / `WaterMisc0.x` | — |
| `assets.backyard_water_mesh` | Backyard water asset entry | single GLB |

The blur passes write each other's input, so the pair is one switch. A disabled pass whose output
an enabled pass reads is a validation error (DP-05, DP-10) unless it renders in place.

## 4. Plan

| ID | Task | Files | Acceptance |
|---|---|---|---|
| **DP-01** | `DeviceProfile` loader, selector, resolver (`extends`, wildcards, precedence), typed accessors; `DetectPlatform()` gains `OS_WEB` → `"web"` | new `Framework/include/core/DeviceProfile.h`, `Framework/src/core/DeviceProfile.cpp`, glaze schema, `utils/RuntimeProfile.cpp` | Unit test resolves each shipped profile; a missing file on Android/web logs `[ERROR]` and uses the fallbacks |
| **DP-02** | Keep `RecommendedProfile()` unchanged (scene sandbox profiles); document both concepts in `runtime-configuration.md` | documentation | DayScene/Minecraft pick the same `profiles[]` entry as today |
| **DP-03** | `Assets/Config/DeviceProfiles.json` with the four profiles above. Package it: the APK asset profiles (`--asset-profile backyard` and the default), the web export (Embedded and Cloud modes) | new asset, `scripts/android/*`, `scripts/BuildWeb.ps1`/`web/prepare-pages.mjs`, `ValidateWebReleasePackage.ps1` | The file is in the APK and in both web bundles; the package validators fail without it |
| **DP-04** | Startup audit line + `--deviceProfile`, `--deviceProfileList`, `--dumpDeviceProfile` | `Config`, `App.cpp`, `ConfigRuntime.cpp` | Forced profile beats the rules; dump prints resolved JSON |
| **DP-05** | RenderGraph: `screen_scale` from the profile (Android fallback 0.5 when no profile loads); generic `scale` on targets and `enabled` on passes, with the read-after-disabled validation | `RenderGraph.cpp/.h`, `RenderGraphDescriptor.h` | Desktop RT sizes unchanged; Android RT = round(surface × scale) |
| **DP-06** | Apply `shadow_cascades` through the runtime `ShadowProjectionOverrideDesc` list; `shadow_accum_scale`; `shadow_blur` as the V+H pair | `RenderGraph.cpp` | Tablet profile yields 2 cascades and no blur passes; a 2-cascade frame dump shows no cascade seam |
| **DP-07** | Apply `graph.light_volume_steps` before RT creation | `BackyardScene.cpp`, `SceneSetup` | 32 in the tablet log, 256 on desktop |
| **DP-08** | Water overlay: iterations, substeps, SSR. `kMobilePressureIterations` becomes the no-profile fallback; the log reason becomes the profile name (`60 pressure iterations (profile android/default)`) | `WaterTunables.h`, `BackyardScene.cpp` | Desktop 120/48; tablet 60/16; Deck 60 |
| **DP-09** | Low-poly water asset `Backyard_Water_Low.glb` (144×72) selected by the profile. Publish it like the other Backyard meshes: R2 upload, model manifest entry with its hash (the cloud asset contract), Android asset profile list | `scripts/blender/build_backyard.py`, `BackyardScene.cpp`, cloud manifest, `scripts/android/*` | Tablet log names the low mesh; 20,736 tris instead of 82,944; `WaterUVMatchesSolver` passes; a fresh install downloads it |
| **DP-10** | `ValidateDeviceProfiles.ps1` (schema, unknown keys, `extends` cycles, asset existence, one profile per platform), wired into `validate-build-registration` | `scripts/`, `.github/workflows/build.yml` | Fails CI on a broken profile file |
| **DP-11** | Documentation: this file, `runtime-configuration.md`, `platform/android.md` | documentation | New keys documented with defaults |

## 5. Verification

1. **Desktop parity (blocks merge).** Scene 7 on D3D12 and Vulkan, `desktop/default`: screenshots and
   the visual baseline comparison must be identical before and after P1/P2/P3.
2. **Deck parity.** `steam-deck/default` must resolve to 60 iterations, full render scale, and pass
   Backyard gates 1–13 as today.
3. **Tablet.** Per-pass GPU timestamps before/after for the passes in §1, plus FPS at native panel
   size, plus a 20-minute run to confirm the thermal drift improves.
4. **Gates.** `--backyard-selftest` gates 1–13 must pass under the Android profile, forced on desktop
   with `--deviceProfile android/default`, not only under the desktop one.
   - The gates count simulated time, so a substep budget that cannot cover a step must not shorten a
     gate silently. Gate 13's log has to show the budget never limited it.
5. **Web.** `web/default` resolves and the bundle still passes the browser compute self-test.
6. **Validation gate.** `ValidateDeviceProfiles.ps1` green in CI.

## 6. Expected result

| Knob | Now | Tablet profile | Expected |
|---|---|---|---|
| `screen_scale` | 0.50 | 0.45 | ×0.81 on GBuffer, Opaque Copy, Deferred, HDR, BackBuffer |
| Shadow accumulation | 7.25 ms, 4 cascades, blur | 0.5 scale, 2 cascades, no blur | ≈2.0 ms |
| Light volumes | 5.45 ms, 256 steps | 32 steps | ≈1.2 ms |
| Water solver | ≈6.3 ms at 2.3 substeps/frame | unchanged settings (60 sweeps already apply) | ≈2.7 ms: at ~55 fps a frame needs 1 substep instead of 2.3, so the cost per *simulated second* stays the same and the cost per *frame* drops with the frame time |
| Water pass | 4.60 ms, 48-step SSR, 83k tris | 16-step SSR, 21k tris | ≈3.0 ms |
| **Frame** | **41.7 ms (24 fps)** | | **≈16–18 ms (55–60 fps)** |

Numbers are projections from the measured per-pass costs, not promises; they will be replaced with
measurements when P2/P3 land.

## 7. Open items

- `screen_scale` floor: 0.45 vs 0.40 — measure both on the tablet and pick from screenshots plus FPS.
- `web/default`: it extends desktop now. Cut it only after a browser per-pass measurement shows a
  pass worth cutting.
- RAM/core-count probes for matching are optional and only added if a phone profile needs them.
- **`max_substeps` on Android: keep 4 unless measured.** The solver's cost per simulated second is
  fixed, so the budget saves nothing once the frame rate rises; it only decides when the water falls
  into slow motion.
  - At 24 fps a splash needs ~3 substeps per frame, and a budget of 2 would bring back exactly the
    slow motion that W-31 removed (`sim/wall` ≥ 0.95 is its acceptance).
  - Decide from a tablet A/B of `sim/wall` and fps at budgets 2 and 4.
- The Steam Deck keeps 4 substeps; revisit if its log shows budget limiting.
- No ball cap in any profile: the owner's decision (`dc4f06e6`, re-confirmed 2026-11-06).

## 8. Rollback

The profile file is additive. Deleting `Assets/Config/DeviceProfiles.json` puts desktop, Deck and web
back on the scene JSON, and Android back on the compiled fallbacks (scale 0.5, 60 sweeps), which is
today's behaviour. Every stage is a separate commit, so a single revert removes the Android tuning
without touching the desktop path.
