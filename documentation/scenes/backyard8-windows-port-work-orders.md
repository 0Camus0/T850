# Backyard 8: Windows port and parity work orders (D3D12, D3D11, GL)

Branch `backyard8-perf4k`. Claude assigns the work and reviews it. DeepSeek does the Windows port; qwen-5090
does bounded Linux tasks.

## Where we are

- The Nov 10 parity pass is recorded in [backyard8-session-2026-11-10.md](backyard8-session-2026-11-10.md),
  §4 and §6.2.
  - All 27 gate captures (screen space, water, spheres at frames 24/90/300) passed on D3D12, D3D11 and GL.
  - The self-test gave 78 PASS on each.
  - Commits `7e28cef` and `84bf60e`.
- Since then there are 83 commits and about 8,500 changed lines in `Assets/Shaders` and
  `Framework/src/video`. None of these features has run on D3D12, D3D11 or GL:
  - the SSFR realism shade (`76fd80b`);
  - whitewater (`CS_PbfWhitewater`, `CS_PbfDiffuse`, `CS_PbfFields`, `CS_FluidWhitewater`);
  - environment-cube reflections (a `TextureCube` sampled in a compute kernel);
  - G-buffer lighting in the shade;
  - the wet deck and the wetness map, caustics, and the refraction stand-in;
  - surfels: `SurfelBits`, the `FluidNormal` RGBA16F storage target, and `CS_FluidResolve` stage 2;
  - the coarse thickness grid (`ThicknessShift`, appended at the end of two cbuffers);
  - the buried marker in `CS_PbfAnisotropy`;
  - the `groupshared` sky term with a group barrier in `CS_ParticleShade`;
  - the engine-wide tiled-light change in `RenderQuad.cpp` (`LightCameraInfo.w` is now a variable width).
- Known leads:
  - `Framework/src/video/gl/GLCompute.cpp:415` rejects non-2D textures (`glTarget != GL_TEXTURE_2D`).
    Check how the shade's `EnvironmentMap` cube binds on GL.
  - D3D11 and D3D12 compute already handle `TEXTURECUBE` (`D3D11Compute.cpp:130`, `D3D12Compute.cpp:409`).
  - D3D11 compiles compute kernels at runtime with FXC `cs_5_0`. Use `SampleLevel` or `Load`, never `Sample`.
  - The D3D12 shade cbuffer (104 DWORDs) is bound as a root CBV.
- Windows host: Windows 11, an RTX 4080 Laptop GPU plus an Intel iGPU. Logs must show the NVIDIA adapter.
  - Python 3.11 is installed.
  - The source tree is `<root>\T850` (not a git checkout; it is synced by archive).
  - The executable is `<root>\T850\T850\bin\x64\Release\DayScene.exe`.
  - The SSH session is session 0, where DXGI cannot create a swapchain. GPU runs therefore go to session 1
    through an Interactive scheduled task, using `<root>\start_task.ps1 -Name <task> -Script <ps1>` (already on
    the host).
  - Session 1 is currently disconnected, but rendering works there: Claude made a D3D12 dump at 09:11 on
    2026-11-11.
  - The host, user and root are in Claude's dispatch message. Never commit them.
- Old capture scripts on the host, worth reusing: `<root>\run_all.ps1` (the gate cells) and
  `<root>\check_all.ps1` (log patterns).

## Rules (every work order)

- Git:
  - never `git stash`, `checkout`, `restore`, `reset`, `clean`, `rebase`, `push` or `commit --amend`;
  - commit only your own files, with a pathspec;
  - start the subject with `WO-Wn:`;
  - the body lists what you ran and the exact results.
- **The Windows machine is one shared resource.**
  - Run every sync, build and GPU run under the same Linux lock:
    `flock /tmp/winport/windows.lock <command>`.
  - Never start a scheduled GPU task while another is running.
  - Never run GPU work in session 0.
- On Windows, touch only `<root>`. Do not change settings, drivers, users or installed software; ask Claude
  instead.
- Every DayScene run, on either OS, uses `--unattended` and is bounded (`--dump-frame`, `--profileFrames`, the
  gate or `--game-selftest`).
  - Use `--scene 8`. Scene 1 is allowed only for the engine-wide tiled-light check.
  - On Linux, from `T850/bin/SteamDeck/Release`, with `SDL_VIDEO_DRIVER=x11 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`.
  - Never touch that folder's `config.json` or its asset links.
- Never re-freeze `backyard8_references.json`.
- Shared-code changes: any fix to HLSL kernels, the render graph JSON, `ComputeKernelRegistry.cpp`, the render
  graph or `RenderQuad.cpp` changes Vulkan too.
  - It must also pass the Linux gate:
    `cd T850 && LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu SDL_VIDEO_DRIVER=x11 python3 scripts/validation/backyard8_regression.py`.
  - Regenerate the GL twins (`generate_gl_compute_twins.py <Name>.hlsl`, then `check_gl_twins.py`).
  - Keep `check_fluid_constants.py` at exit 0.
- **Look at your images.** You can view PNG files. Build side-by-side sheets, look at them, and describe what
  you see next to the numbers.
- Report pass, fail and "not run" separately. When blocked, append to "Requests to Claude" and continue with
  what you can.

## Parity method

Comparisons at 1920x1080, with `--regressionFixedDt 0.0166667 --postProcessMode compute --backyard-no-hud`.

A cell is `<renderer>_<view>_<start>`:

| Part | Values | Flags |
|---|---|---|
| renderer | `ss` | `--fluid-render screen-space` |
| | `surfel` | `--fluid-render splat` (the particle renderer's water style) |
| view | `default` | no flag |
| | `close` | `--backyard-camera 0 1.3 -10.0 0.32 0` |
| start | `water150` | `--backyard-start water --dump-frame 150` |
| | `lake300` | no start flag, `--dump-frame 300` |

That gives 8 cells. Keep `RT_Dump_HDR_Final` and `RT_Dump_FluidParticles` (as PNG too) and the run log.

The solver is not deterministic from run to run, so judge a difference against noise:

- the noise is the difference between two runs of the same API (mean absolute difference per channel, p99,
  and the percentage of pixels above 4 levels);
- the lake cells are calm and therefore the sensitive check;
- the water cells use the gate tolerances and a visual review.

## WO-W1 (DeepSeek): Windows tooling, full parity matrix, triage and backend fixes

1. **Tooling (commit):**
   - `T850/scripts/validation/windows_parity.py`, run on Linux, with subcommands:
     - `sync`: archive the source and extract it into `<root>\T850`.
       - The default is HEAD.
       - `--tree <path>` takes a working tree or worktree including uncommitted changes, via `git ls-files`.
       - Include at least `Framework`, `FrameworkImGui`, `DayScene`, `T8ditor`, `scripts`, `Assets/Shaders`,
         `Assets/Scenes`, `Assets/Layouts`, `Assets/Fonts`, the `.sln`, `.props` and `.vcxproj` files, and the
         CMake files.
     - `build`: run `scripts\build.ps1 -Config Release -Platform x64 -Action Build` over SSH. If the Dawn
       package audit fails, run `scripts\SetupDawn.ps1 -Architecture x64` once and retry.
     - `run <script.ps1> <tag>`: copy the script, start it in session 1, poll for its `DONE` marker (with a
       timeout), and fetch its output folder to `/tmp/winport/<tag>/`.
     - Every subcommand holds the lock. The host and root come from `--host` and `--root` or the
       `T850_WIN_HOST` and `T850_WIN_ROOT` environment variables; no defaults in the repository.
   - `T850/scripts/validation/win/backyard8_parity.ps1`, run on Windows. For each API in a list (`vulkan`,
     `d3d12`, `d3d11`, `gl`):
     - the gate: `python scripts\validation\backyard8_regression.py --api <api> --day-scene <exe>`;
     - the 8 parity cells (above), two runs each for `vulkan`;
     - `--game-selftest`;
     - one 3840x2160 run (default view, water start f150);
     - a log scan of every run for:
       - the `check_all.ps1` patterns (`Shader compile failed`, `using graphics implementation`,
         `compute dispatch failed`, `DispatchTooLarge`, `cannot build constants`,
         `does not declare the kernel's complete binding layout`);
       - `[ERROR]`, and device removed or lost;
       - the counts of `enabled compute kernel` and `dispatched compute` lines;
     - the adapter name.
2. **Run it on HEAD.** Fetch the results to Linux and report:
   - per API: the gate result per cell, the log findings, and the kernel counts against Windows Vulkan;
   - each cell against Windows Vulkan, and Windows Vulkan against Linux Vulkan (WO-W2's references in
     `/tmp/winport/linux_ref/`), with the Vulkan run-to-run noise as the yardstick;
   - contact sheets, one row per cell: `[Linux Vulkan | Windows Vulkan | D3D12 | D3D11 | GL]`, for HDR_Final.
     Look at them.
3. **Triage and fix** every API that fails or looks different.
   - Find the first divergent target, following the `t850-api-frame-comparison` skill, §7.
   - For the fluid stages you may add temporary dumps of `FluidDepthA`, `FluidThickness` and `FluidNormal`;
     remove them before committing.
   - One commit per root cause (`WO-W1: <api>: <fix>`), with before/after numbers and sheets.
   - Backend-local fixes (`Framework/src/video/<api>/...`) can go straight in.
   - **Before you change shared code** (see Rules), append the diagnosis and the proposed change to "Requests
     to Claude" and wait for Claude's note.
4. Finish with a clean full matrix on the final HEAD: all four APIs, gate PASS, no log findings, and differences
   at noise level. Write the summary under "Results".

## WO-W2 (qwen-5090): Linux Vulkan references for the parity cells

Done on Linux only; no code changes and no commits.

- Run the 8 cells (Parity method) on Linux Vulkan, twice each, serially. Use `--api vulkan` and the Linux run
  rules.
- After each run, move the dump folder to `/tmp/winport/linux_ref/<cell>_r<n>/`.
  - Convert `RT_Dump_HDR_Final.ppm` and `RT_Dump_FluidParticles.ppm` to PNG in the same folder.
  - Keep the log as `run.log`.
  - Delete the `dumps_*` folders you create.
- Write `/tmp/winport/linux_ref/noise.json`: for each cell, r1 against r2 on HDR_Final (mean absolute
  difference per channel, p99 of the per-pixel max channel difference, the percentage of pixels above 4
  levels) and each run's `[ERROR]` count.
- Make `/tmp/winport/linux_ref/sheet.png`: one row per cell, r1 and r2 side by side, each image scaled to 640
  px wide, with the cell name printed. Look at it and report anything that looks wrong.
- Report the noise table and the exact commands you ran.

## WO-W2b (qwen-5090): parity compare tool and contact sheets

This work is Linux-only: take no Windows lock and run no DayScene. DeepSeek calls the tool after each matrix.

- Write `T850/scripts/validation/backyard8_parity_compare.py` (Python 3; numpy and Pillow are allowed).
  - Arguments: `--linux <dir>` (default `/tmp/winport/linux_ref`), `--win <dir>`, `--out <dir>` and
    `--apis` (default `vulkan,d3d12,d3d11,gl`).
  - Windows images are `<win>/<api>/cells/<cell>/RT_Dump_HDR_Final.ppm`. Read
    `T850/scripts/validation/win/backyard8_parity.ps1` for the exact names, including the Vulkan repeats.
  - The Linux reference is `<linux>/<cell>_r1/dump/RT_Dump_HDR_Final.ppm`. The eight cells are listed in
    `/tmp/winport/linux_ref/analyze.py`.
- For every (api, cell), compute the same three metrics as `analyze.py`, using its definitions:
  mean absolute difference per channel, p99, and % of pixels above 4. Compare against the Linux r1 image.
  - Verdict `noise` if all three are within 1.5x the cell's r1-against-r2 values in
    `/tmp/winport/linux_ref/noise.json`. For % above 4, also allow +1 point.
  - Otherwise `DIFF`. Use `missing` if the file is absent and `size` if the dimensions differ.
- Outputs in `--out`:
  - `compare.json`;
  - `compare.md`: a table with one row per cell and one column per API, each entry `mean/p99/%>4 verdict`;
  - `sheet.png`: one row per cell, columns `[Linux Vulkan r1 | <apis...>]`. Each tile is 480x270 and labelled
    (cell name on the first tile, API on the others). Use a grey tile marked `missing` when there is no image;
  - `diff_<api>_<cell>.png` for every `DIFF`: absolute difference x8, clamped.
- Test it under `/tmp/winport/w2b_test/`.
  - Make a fake Windows tree whose `vulkan` images are the Linux r2 dumps. Expect `noise` for all eight
    `vulkan` cells and `missing` for the other APIs.
  - Make one `d3d12` cell from r2 with a 200x200 black square drawn on it. Expect `DIFF` and a diff image
    showing the square.
  - Look at `sheet.png`. Report the table and the exact commands you ran.
- Commit only that file: `WO-W2b: parity compare tool`.

## WO-W2c (qwen-5090): compare against Windows Vulkan, and add WebGPU

This work is Linux-only, with the same rules as WO-W2b. Test data: `/tmp/winport/matrix1/out`, a real Windows matrix
(`vulkan`, `d3d12`, `d3d11` and `gl`, with Vulkan repeats `<cell>_r1` and `<cell>_r2`). Read it only.

- Make the default `--apis` `vulkan,d3d12,d3d11,gl,webgpu`.
- Add `--ref linux|win` (default `linux`, which keeps the current behaviour).
  - With `--ref win`, the reference image is `<win>/vulkan/cells/<cell>_r1/RT_Dump_HDR_Final.ppm`.
  - The noise is that image against `<win>/vulkan/cells/<cell>_r2/...`, computed with the same three metrics
    (it does not come from `noise.json`).
  - The verdict rule is unchanged (1.5x, and +1 point on % above 4).
  - The columns are `[Win Vulkan r1 | Win Vulkan r2 | <apis other than vulkan>]`.
    In `compare.md`, add a `noise` column showing the r1-against-r2 values.
  - If a Windows Vulkan r2 image is missing, print a clear error for that cell and mark the row `no-noise`.
- Test it on `/tmp/winport/matrix1/out` with `--ref win`. Expect `DIFF` for d3d12, d3d11 and gl in all 8 cells
  (the water shade is broken there), and `missing` for webgpu. Look at the sheet and one diff image, and describe them.
- Commit only that file: `WO-W2c: compare against Windows Vulkan, add webgpu`.

## WO-W3 (DeepSeek): documentation

WO-W1 is signed off (see "WO-W1 sign-off" in Notes from Claude). This work is docs only: no code, no host details.
Make one commit per file group, with subjects starting `WO-W3: docs -`.

1. `documentation/rendering/compute-shader-implementation.md`:
   - **WebGPU specifics, Tint limit 2**: make it say *any* `isfinite`/`isnan`/`isinf`, scalar or vector.
     They lower to `OpIsNan`/`OpIsInf`, and Dawn's Tint SPIR-V reader does not implement them:
     `TINT_UNIMPLEMENTED unhandled SPIR-V instruction: OpIsNan`, then the process aborts with `0xC0000409`.
     Cite `CS_PbfCull.hlsl` / `CS_PbfDiffuse.hlsl` (`b4a8a75`) beside `CS_FluidDiagnostics3D.hlsl`.
   - **Read-only cube textures in compute** (contract or "Portable guarantees"). A kernel may declare a read-only
     `"dimension": "cube"` texture in `compute_kernels.json`.
     - Supported on D3D11 (the texture's `TEXTURECUBE` SRV), D3D12 (`DepthOrArraySize == 6`, the texture's cube SRV),
       Vulkan, GL (`GL_TEXTURE_CUBE_MAP`, `samplerCube`) and WebGPU (a Cube view).
     - Not supported as storage (read-write).
     - Commits: `0bd825b`, `639e8a5`, `a5f6ba6`, `238be0f`.
   - **OpenGL behavior**: SPIRV-Cross emits one combined sampler per (texture, sampler) pair.
     - A texture that is both sampled and queried with `GetDimensions` gets a second
       `..._SPIRV_Cross_DummySampler` alias on the same binding.
     - `GLComputePipeline::Create` drops exactly duplicated reflected entries before `SetValidatedLayout`.
     - Cube bindings save and restore `GL_TEXTURE_BINDING_CUBE_MAP`.
   - **WebGPU/Dawn implementation**: the device requests the adapter's `maxStorageBufferBindingSize` and
     `maxBufferSize`. The WebGPU defaults (128 MiB and 256 MiB) reject full-screen per-pixel buffers at 4K:
     20 B/pixel at 3840x2160 is 165,888,000 B (`130e6b9`).
2. `.github/skills/t850-compute-kernel/SKILL.md`: add two short rules where they fit the existing structure.
   - Read-only cube textures are portable (declare `"dimension": "cube"`).
   - Never use `isfinite`/`isnan`/`isinf` in a compute kernel (WebGPU aborts); use the exponent-bits test.
   Keep the skill concise: two bullets, no history.
3. `documentation/scenes/backyard8-session-2026-11-10.md` §4: add a short "Windows port, 2026-11-11" subsection.
   - Five APIs, and the method in two lines (the noise yardstick and the compare tool).
   - The per-API result in one table (cells at noise and remaining variance).
   - The fix commits.
   - The GL coping band as a pre-existing scene difference.
   - A link to this doc's Results for the full numbers.
4. Set W3 to done in Status. Reply with the commit hashes.

## Status

| WO | Owner | State | Commits | Notes |
|---|---|---|---|---|
| W1 | DeepSeek | done (signed off by Claude) | caf9b2d 05abb78 8eb749f 64e527d 6046bec 1fe5ca5 14dd602 bc28091 3e2e6ac 0bd825b 639e8a5 a5f6ba6 b4a8a75 238be0f 130e6b9 1f37bfc | all 5 APIs: gate 0, 78 PASS, 0 log findings, water cells at noise; lake300 DIFF accepted as solver variance; GL coping band is pre-existing |
| W2 | qwen-5090 | done (accepted) | none (local evidence) | 16 runs, 0 errors; noise table below |
| W2b | qwen-5090 | done (accepted) | 67ea48b, 90acd92 | compare tool and contact sheets (Linux-only) |
| W2c | qwen-5090 | done (accepted) | 5f92c4e | `--ref win` (Windows Vulkan yardstick) and a webgpu column |
| W3 | DeepSeek | done | a887d9c bb2c471 2fe5031 plus this commit | docs: compute-shader doc, compute-kernel skill, session doc §6.3 |

## Results (final)

Final binary: HEAD `1f37bfc`, sync `rev:1f37bfc` (1343 files, 1 copied), `build` = Rebuild,
`BUILD_EXIT=0 ... exe_newer=True` (245 s). Matrix `matrix4` at 1920x1080 with
`--regressionFixedDt 0.0166667 --postProcessMode compute --backyard-no-hud`, plus one
3840x2160 water/f150 run per API. Compare tool: `backyard8_parity_compare.py --ref win` and
`--ref linux`, `--win /tmp/winport/matrix4/out`.

### Gate and self-test: PASS on all five APIs

| API | gate exit | captures | levels | self-test |
|---|---|---|---|---|
| vulkan | 0 (101 s) | 9/9 PASS | lake + water PASS | 78 PASS / 0 FAIL |
| d3d12 | 0 (103 s) | 9/9 PASS | lake + water PASS | 78 PASS / 0 FAIL |
| d3d11 | 0 (177 s) | 9/9 PASS | lake + water PASS | 78 PASS / 0 FAIL |
| gl | 0 (102 s) | 9/9 PASS | lake + water PASS | 78 PASS / 0 FAIL |
| webgpu | 0 (104 s) | 9/9 PASS | lake + water PASS | 78 PASS / 0 FAIL |

### Logs: 0 findings in every step

`progress.txt` for matrix4 has **no** `ERRORS`, `BAD`, `DUMP_MISSING` or `LOG_MISSING` line,
for 8 cells x 4 APIs (Vulkan twice) plus 5 4K runs: 0 `[ERROR]`, 0 `Shader compile failed`,
0 `compute dispatch failed`, 0 `DispatchTooLarge`, 0 `cannot build constants`, 0
`does not declare the kernel's complete binding layout`, 0 device removed/lost. Kernel
counts: 22 enabled everywhere; 14 dispatched (screen-space) / 9 (surfel) on every API;
D3D12, D3D11, GL and WebGPU now equal Windows Vulkan. All 4K runs exit 0 with 14 dispatched
(the D3D12/D3D11/GL 4K cells previously dispatched 13). Adapter: NVIDIA GeForce RTX 4080
Laptop GPU on every run.

### Per-API parity against Windows Vulkan (`--ref win`)

Format `mean R/G/B  p99  %pixels>4`, verdict against Windows Vulkan r1/r2 noise.

| cell | noise | d3d12 | d3d11 | gl | webgpu |
|---|---|---|---|---|---|
| ss_default_water150 | 1.38/1.67/1.94 25 15.2 | 1.52/1.81/2.07 26 15.6 noise | 1.57/1.86/2.12 26 16.0 noise | 1.64/1.95/2.24 27 15.7 noise | 1.51/1.79/2.03 26 15.8 noise |
| ss_default_lake300 | 0.19/0.23/0.28 6 1.37 | 0.45/0.47/0.48 7 1.49 DIFF | 0.50/0.53/0.55 8 1.69 DIFF | 0.65/0.74/0.84 11 2.49 DIFF | 0.44/0.47/0.49 7 1.62 DIFF |
| ss_close_water150 | 1.91/3.03/3.75 33 31.5 | 2.03/3.30/4.08 33 32.4 noise | 2.04/3.38/4.21 33 33.1 noise | 2.17/3.40/4.15 35 32.8 noise | 1.97/3.29/4.13 32 32.8 noise |
| ss_close_lake300 | 0.41/0.51/0.59 8 2.92 | 0.64/0.73/0.78 8 3.20 DIFF | 0.65/0.72/0.74 9 3.07 DIFF | 0.68/0.80/0.89 9 3.78 DIFF | 0.64/0.77/0.82 9 3.89 DIFF |
| surfel_default_water150 | 1.64/1.98/2.26 30 17.5 | 1.74/2.05/2.33 28 17.4 noise | 1.81/2.16/2.44 29 18.2 noise | 1.86/2.17/2.45 29 17.8 noise | 1.75/2.13/2.44 27 18.0 noise |
| surfel_default_lake300 | 0.44/0.52/0.61 15 4.25 | 0.67/0.72/0.76 15 4.31 DIFF | 0.72/0.78/0.84 15 4.49 DIFF | 0.90/1.04/1.19 17 5.40 DIFF | 0.67/0.74/0.80 14 4.14 DIFF |
| surfel_close_water150 | 2.32/3.75/4.63 36 36.0 | 2.48/3.97/4.87 36 37.5 noise | 2.44/3.95/4.85 35 37.7 noise | 2.47/3.90/4.76 35 37.0 noise | 2.48/3.78/4.53 36 35.5 noise |
| surfel_close_lake300 | 0.72/0.84/0.91 13 5.91 | 0.94/1.03/1.06 13 5.87 noise | 0.93/1.05/1.09 12 6.03 noise | 1.05/1.22/1.30 14 7.19 noise | 0.94/1.08/1.11 14 6.32 noise |

Vulkan itself is `0.00/0.00/0.00` in every cell by construction (it is the reference; r1
against r2 is `noise`). All 16 water cells and the two `surfel_*lake300` cells for
D3D12/D3D11/WebGPU are at noise; the lake cells are discussed below.

**All five sheets look identical.** `compare4_win/sheet.png` (Win Vulkan r1 | r2 | D3D12 |
D3D11 | GL | WebGPU) and `compare4_linux/sheet.png` (Linux | the five APIs): the same water
body, whitewater, foam line, coping reflection and lit window in every column, for both
renderers and both views. This replaces matrix1, where D3D12/D3D11 lost the water surface
and GL showed only the dry pool.

Against Linux Vulkan (`--ref linux`) the picture is the same: water cells at noise for all
five APIs; lake cells DIFF for all five including Windows Vulkan (Windows Vulkan vs Linux
r1: 0.64/0.78/0.93 in ss_default_lake300), i.e. the platform difference affects every API
equally and is not caused by a backend.

### Remaining variance: lake300 accepted as cross-compiler solver variance

The four calm cells (`*_lake300`) show a small, consistent mean offset on every API:
0.44-0.68 (d3d12), 0.50-0.93 (d3d11), 0.64-1.05 (gl), 0.44-0.94 (webgpu) against a
run-to-run noise of 0.19-0.72. p99 is equal to noise on D3D12, D3D11 and WebGPU; GL p99 is
11 against 6-8 in `ss_default_lake300`. Claude accepted this as cross-compiler solver
variance after viewing the rows (all four APIs carry the same offset, in the same cells).

### GL coping band: pre-existing scene difference, not the port

The GL-only band Claude located at rows 582-584 (columns 341-1578) was tested with one GL
and one Vulkan `ss_default_lake300` run that kept every dump. It is **already in the
G-buffer**, so it is a scene/material difference outside this port:

| row (x=900) | Vulkan GBuffer_Albedo | GL GBuffer_Albedo | columns with diff > 8 |
|---|---|---|---|
| 581 | 182,182,182 | 181,181,181 | 0 |
| 582 | 181,181,181 | **104,105,100** | 1238 |
| 583 | 180,180,180 | **116,118,115** | 1236 |
| 584 | **126,126,122** | 184,184,184 | 1236 |
| 585 | 111,113,112 | 110,113,112 | 0 |
| 586 | **102,106,104** | 181,181,181 | 1236 |

The same rows differ by the same amounts in `RT_Dump_Deferred` and persist unchanged in
`RT_Dump_HDR_Final`, while `FluidParticles` is empty there; `band_gbuf_zoom.png` shows the
coping's striped front face with the stripe sequence shifted by about two rows on GL. The
band is therefore a GL texture-orientation or rasterization difference in the static coping
material, present before any water pass. Recorded, not fixed, per Claude's instruction.

### Fixes, one commit per root cause

Tooling:
- `caf9b2d` windows_parity.py (sync/build/run under the shared flock), backyard8_parity.ps1, smoke_frame.ps1.
- `05abb78` sync stamps only changed files with the host's time; `build` defaults to Rebuild
  and refuses to continue unless `exe_newer=True`.
- `8eb749f` project is `<root>\T850\T850` in all three remote scripts, with a `ROOT_FAIL` guard
  on `<project>\T850.sln` (the 09:18 sync wrote a tree one level too high).
- `64e527d` BUILD_COMMAND really substitutes `-Root`/`-Action`.
- `6046bec` sync writes CRLF like the host's `core.autocrlf=true` checkout.
- `1fe5ca5` sync leaves a file alone when its text matches after CR-stripping (this keeps
  the Dawn recipe hashes intact and the checkout clean); `run` passes the script's declared
  `-Exe/-Out/-Root` and uses `-NonInteractive`.
- `14dd602`, `bc28091` parity matrix: absolute gate path (the gate ran from `bin`), UTF-8
  progress, and the project root three levels above the exe.
- `3e2e6ac` add webgpu to the default API list.

Backend fixes:
- `0bd825b` **d3d12**: accept the environment cube (`DepthOrArraySize == 6`, TEXTURE2D,
  read-only) in dispatch validation.
- `639e8a5` **d3d11**: accept a TEXTURECUBE SRV for a Cube layout.
- `a5f6ba6` **gl**: drop duplicate reflected bindings (two samplerCube aliases for the
  sampled+queried EnvironmentMap), accept `GL_TEXTURE_CUBE_MAP`, and save the previous
  binding from `GL_TEXTURE_BINDING_CUBE_MAP`.
- `b4a8a75` **webgpu** (shared HLSL): bit-pattern finiteness instead of `isfinite`, because
  Dawn's Tint SPIR-V reader aborts the process on `OpIsNan` (`CS_PbfCull.hlsl`,
  `CS_PbfDiffuse.hlsl`, twins regenerated; Linux gate PASS).
- `238be0f` **webgpu**: accept the 6-layer environment cube in dispatch validation.
- `130e6b9` **webgpu**: request the adapter's `maxStorageBufferBindingSize`/`maxBufferSize`
  (a 20 B/pixel `$window` buffer is 165,888,000 B at 4K, over the 128 MiB default).
- `1f37bfc` comment arithmetic fix.

### What was run

- Linux Vulkan gate on the shared-shader commit: RESULT PASS, 9/9 captures, both levels.
- `check_gl_twins.py` PASS (58 kernels, 53 twins), `check_fluid_constants.py` exit 0.
- Sync + build cycles between every fix; matrices 1 (4 APIs), 2 (5 APIs), 3 (5 APIs),
  4 (5 APIs, final binary); one WebGPU-only 4K confirmation pass.
- 8 parity cells and one 4K run per API, two Vulkan repeats, gate and self-test per API in
  every matrix.

Evidence on this Linux host: `/tmp/winport/matrix4/out`, `compare4_win/`,
`compare4_linux/`, `gbuf/out/`, `band_gbuf_zoom.png`.

## Notes from Claude

### WO-W2 accepted (Linux Vulkan references)

- The PNGs are in `/tmp/winport/linux_ref/<cell>_r<n>/dump/`
  (`RT_Dump_HDR_Final.png`, `RT_Dump_FluidParticles.png`, plus all PPMs), with `run.log` beside `dump/`.
  Also `noise.json` and `sheet.png`.
- Noise, r1 against r2 on HDR_Final (mean levels per channel / p99 / % of pixels above 4):

| Cell | Mean | p99 | > 4 |
|---|---|---|---|
| ss_default_water150 | 1.4 / 1.7 / 2.0 | 25 | 15.6 % |
| ss_default_lake300 | 0.21 / 0.25 / 0.29 | 8 | 1.5 % |
| ss_close_water150 | 1.9 / 3.3 / 4.1 | 34 | 34.3 % |
| ss_close_lake300 | 0.41 / 0.49 / 0.56 | 8 | 2.5 % |
| surfel_default_water150 | 1.7 / 2.0 / 2.3 | 28 | 17.8 % |
| surfel_default_lake300 | 0.51 / 0.58 / 0.67 | 14 | 4.2 % |
| surfel_close_water150 | 2.4 / 3.8 / 4.7 | 37 | 37.4 % |
| surfel_close_lake300 | 0.69 / 0.80 / 0.87 | 12 | 5.7 % |

- Claude viewed the sheet: water is present and shaded in every cell, and r1 against r2 differs only in
  wave phase.

### Answer to the build blocker: (c), the blocker is a root-path bug in the tool

Dawn is fine. The tool wrote the tree to the wrong place.

- `<root>` is the folder that holds `start_task.ps1` and `__parity` (as `run` and this doc use it).
  The source tree is `<root>\T850` and the project is `<root>\T850\T850`.
- `EXTRACT_SCRIPT`, `BUILD_SCRIPT` and `SETUP_DAWN_SCRIPT` all used `Join-Path $Root 'T850'` as the project.
  So the 09:18 sync wrote the whole project one level too high, into `<root>\T850\{Framework,Assets,scripts,...}`,
  beside the host checkout's `.git`.
- `build` then compiled that copy, which has no `build\dawn-package`. `SetupDawn` cloned a second vcpkg there.
- The real project was never synced. Its Dawn package (`<root>\T850\T850\build\dawn-package\Dawn.Release.props`,
  11/9) and vcpkg (11/7) are intact. No Dawn-related file has changed since the Nov 10 build
  (`git log 7e28cef..HEAD` on `cmake/`, `SetupDawn.ps1`, `vcpkg*.json` and the vcpkg gitlink is empty).
  `T850CheckDawnPackage` only checks that the props file exists.
- Claude removed the misplaced copies. These are the 16 top-level items in `<root>\T850` that the host git does not track
  and that were created today: `Assets`, `CMakeLists.txt`, `CMakePresets.json`, `DayScene`, `Directory.Build.props`,
  `Framework`, `FrameworkImGui`, `Librerias` (the second vcpkg), `T850.sln`, `T8ditor`, `android`, `cmake`, `scripts`,
  `steamdeck`, `web` and `config.json`. Nothing else was touched.

Fix, as one commit (`WO-W1: tooling: project is <root>\T850\T850`):

1. In `EXTRACT_SCRIPT`, set `$dstRoot = Join-Path $Root 'T850\T850'`. Keep `Substring(5)` and the `$src` path.
2. In `BUILD_SCRIPT` and `SETUP_DAWN_SCRIPT`, set `$proj` / `$project = Join-Path $Root 'T850\T850'`.
3. Add a guard to all three, before anything is written:
   if `<project>\T850.sln` is missing, print `ROOT_FAIL <path>` and `exit 1`. The Python side dies on `ROOT_FAIL`.
   The guard would have stopped the 09:18 sync.
4. `win/backyard8_parity.ps1`: remove the host paths from the `-Exe` and `-Out` defaults and make both mandatory.
   The Rules forbid committing host details. `run` passes them.
5. Do not run `SetupDawn.ps1` again without asking Claude.

Then:

1. `sync`. Check that `<root>\T850\T850\DayScene\Backyard8Scene.cpp` contains `DrawWaterShadingPanel` and that
   `<root>\T850\Framework` does not exist.
2. `build` (Rebuild). The result must show `BUILD_EXIT=0` and `exe_newer=True`, and `DayScene.exe --help` must list `--backyard-camera`.
3. Continue with milestone 1 (the full matrix).

### GL leads from code reading (confirm against the matrix before fixing)

Linux cannot test GL (`LinuxFramework.cpp` forces Vulkan), so these come from reading the code:

1. **Expected failure: GL rejects the cube texture in `CS_ParticleShade`.**
   `GLCompute.cpp` (~line 415) returns false for any texture whose `glTarget != GL_TEXTURE_2D`. `EnvironmentMap`
   (t7, `"dimension": "cube"` in `compute_kernels.json`) is a `GL_TEXTURE_CUBE_MAP`. Expect the screen-space shade
   to be refused on GL. The GLSL twin already declares `samplerCube` at binding 7, so no twin change is needed.
   Fix: for `ReadOnlyTexture` with `layout->dimension == ComputeTextureDimension::Cube`, require
   `GL_TEXTURE_CUBE_MAP`. Keep requiring `GL_TEXTURE_2D` for 2D layouts, and keep rejecting cubes for `ReadWriteTexture`.
2. **Same file, bind site (~line 503).** The previous binding is saved with `GL_TEXTURE_BINDING_2D` whatever the target.
   On restore, a 2D texture name would be bound to `GL_TEXTURE_CUBE_MAP`.
   Query `GL_TEXTURE_BINDING_CUBE_MAP` when `texture->glTarget == GL_TEXTURE_CUBE_MAP`.
3. **Possible GL-only difference.** Nothing in `video/gl` enables `GL_TEXTURE_CUBE_MAP_SEAMLESS`.
   D3D and Vulkan always filter cube maps seamlessly. If GL reflections show seams at cube-face edges, enable it once at
   GL device init. That is a backend-local change.

A static scan of the 13 kernels changed since Nov 10 found no FXC `cs_5_0` blockers.
No `Sample` or derivatives are used in compute, the `float4` UAVs are only written (no typed UAV loads), and no kernel
is near the 8-UAV limit. D3D11 compile failures would therefore be unexpected; check the log scan anyway.

### matrix1 review and fix list (Claude)

**Goal (from the user).** On Windows, every API (Vulkan, D3D12, D3D11, GL and **WebGPU**) must produce the same Backyard 8
output as Vulkan, for both the particle (surfel) renderer and the screen-space renderer. WebGPU is added to WO-W1.

Results (`backyard8_parity_compare.py --win /tmp/winport/matrix1/out`):

- **Windows Vulkan** has 0 errors.
  - The water150 cells are at noise against Linux.
  - The lake300 cells are 1.2x to 3x the Linux r1/r2 noise. That noise is very low (0.2 to 0.9) and this is another GPU
    (RTX 4080 Laptop against RTX PRO 6000).
  - So the yardstick for the other APIs is **Windows Vulkan on the same machine** (r1 against r2 is the noise), not Linux.
    WO-W2c adds `--ref win` to the compare tool.
- **D3D12 and D3D11**: `Pass 'Fluid Surface Shade' compute dispatch failed; using graphics fallback` on every frame.
  This is 100 lines per cell in all 9 cells, in both renderers, because both shade through `CS_ParticleShade`.
  The result is a DIFF of 10 to 25 mean levels everywhere. These are the dispatch errors the user saw on the laptop.
- **GL**: the `CS_ParticleShade` pipeline is never created (`SetValidatedLayout failed`), giving the same DIFF.

Root causes, all backend-local. Make one commit per API, `WO-W1: <api>: <fix>`:

1. **D3D12** (`D3D12Compute.cpp`, the dispatch validation at ~line 743).
   - The non-volume branch requires `DepthOrArraySize == 1`. `EnvironmentMap` (t4, layout `dimension == Cube`)
     is a cube with `DepthOrArraySize == 6`, so every dispatch returns false.
   - Reflection already accepts cubes (~line 409), and the texture's own SRV is already `TEXTURECUBE`
     (`D3D12Texture.cpp:126`, `:231`), so `srvGPU` can be bound as is.
   - Fix: when `layout->dimension == ComputeTextureDimension::Cube`, require `ReadOnlyTexture`,
     `Dimension == TEXTURE2D`, `DepthOrArraySize == 6` and `SampleDesc.Count == 1`. Keep the 2D rule otherwise.
2. **D3D11** (`D3D11Compute.cpp:287`).
   - The dispatch requires `ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D`, but the cube's `pSRVTex` is `TEXTURECUBE`
     (`D3D11Texture.cpp:159`).
   - Fix: when the layout is Cube, require `TEXTURECUBE` (`ReadOnlyTexture` only). Keep `TEXTURE2D` otherwise.
3. **GL** (`GLCompute.cpp`), three parts:
   - (a) The reflection has 17 entries against 16 declared: `idx=7` appears twice.
     - `CS_ParticleShade.hlsl:1122` calls `EnvironmentMap.GetDimensions` and also samples the map. SPIRV-Cross therefore
       emitted two `samplerCube` aliases on binding 7 (`...LinearSampler` and `...SPIRV_Cross_DummySampler`).
     - It is the only texture both sampled and queried. Every other `DummySampler` texture is load-only and has
       one alias.
     - `ReflectComputeBindings` does not merge aliases.
     - Fix in `GLComputePipeline::Create`: before `SetValidatedLayout`, drop reflected entries that exactly duplicate an
       earlier one (same type, bindingIndex, shaderRegister, dimension, storageFormat and constantCount). They are the
       same texture unit. Do not touch the shared `ComputeReflection.cpp`; Vulkan uses it.
   - (b) GL lead 1 above: accept `GL_TEXTURE_CUBE_MAP` for a Cube `ReadOnlyTexture` (~line 415).
   - (c) GL lead 2 above: save the previous binding with `GL_TEXTURE_BINDING_CUBE_MAP` for cube targets (~line 503).
4. **WebGPU**: add `webgpu` to the matrix (`-Apis` default in `backyard8_parity.ps1`). The compute bind group already
   builds cube views (`WebGPUDriver.cpp:1004-1006`). Run it, then triage what appears.

Then sync, Rebuild and run `matrix2` with all five APIs. Compare with `--ref win` (WO-W2c), look at the sheet and report.
Per cell and API, the target is `noise` against Windows Vulkan. Explain any remaining DIFF with images (diff heat maps).

### matrix2 review (Claude)

After `0bd825b`, `639e8a5`, `a5f6ba6` and `b4a8a75`, D3D12, D3D11 and GL log **0 errors** in every cell.
Self-test is 78 PASS and the gate exits 0 on every API.

`--ref win` (`/tmp/winport/matrix2/compare_win/compare.md`):

- **All four water150 cells**, in both renderers: d3d12, d3d11 and gl are at `noise` against Windows Vulkan.
- **lake300 cells**: some small `DIFF`s.
  - Their means are d3d12 0.43 to 0.45, d3d11 0.51 to 0.87, and gl 0.65 to 1.13, against noise of 0.20 to 0.91. That is
    1.5x to 3x a very low noise floor. p99 is equal to noise, except GL (11 against 7).
  - The heat maps show the same wave-pattern speckle as run-to-run noise, plus one structured GL defect (below).
  - The surface itself is not offset: on every API the best vertical shift of `FluidParticles` against Vulkan is 0.
- **GL-only band at the far coping.**
  - Location: rows 582 to 584 of the 1080p default view, columns 341 to 1578 (exactly the pool width).
  - Values: Vulkan `[68,66,63] [67,65,62] [26,26,23]`, GL `[24,24,20] [23,24,21] [64,62,60]`.
  - It is identical in the surfel and screen-space cells, and `FluidParticles` is empty there.
  - Zoomed (`/tmp/winport/band_zoom.png`), it is the fine striped detail on the far coping's front face. GL shows the
    stripe sequence shifted or mirrored by about 2 rows. It is not the water.
  - Likely a pre-existing GL texture-orientation or rasterization difference in the static scene.
  - To confirm, keep all dumps for one GL and one Vulkan `ss_default_lake300` run and compare
    `RT_Dump_GBuffer_Albedo` and `RT_Dump_Deferred` at rows 576 to 590. If the band is already in the G-buffer, it is a
    scene issue outside this port: report it, do not fix it here.
- **WebGPU**: `Fluid Surface Shade` dispatch failed every frame. `238be0f` fixes the cube validation; `matrix3` checks it.
  The 4K WebGPU run exited 1 after 2 s (22 errors). Read that log in `matrix3`.

### WO-W1 sign-off (Claude)

**Signed off** on `1f37bfc` / `matrix4`. Claude checked it independently, without relying on the Results text:

- **Logs**: 0 `[ERROR]` and 0 `[WARN ]` in all 53 runs (17 Vulkan, 9 each for d3d12, d3d11, gl and webgpu,
  including every 4K run).
- **Parity**: both compares were re-run (`--ref win` and `--ref linux`) and the tables match Results.
- **Images**: Claude viewed the close-view rows of the sheet. The wave crest, foam, refraction and colour are the same on
  all six columns, for both renderers.
- **GL coping band**: Claude confirmed it from `/tmp/winport/gbuf/out`.
  - `RT_Dump_GBuffer_Albedo` already differs at rows 582-586 (mean about 53).
  - `WaterDepth` and `FluidParticles` are identical there, so it is pre-existing scene albedo, outside the port.
- Answers to the requests:
  1. Signed off.
  2. The GL band is left for the user to decide on; it is a separate, pre-existing scene issue.
  3. The host path in an older doc is left as is; it is the user's content, and Claude reports it to the user.

WO-W3 is dispatched to DeepSeek.

## Requests to Claude

1. **WO-W1 sign-off (final binary `1f37bfc`, matrix4).** Every decision you gave is done:
   - lake300 recorded as cross-compiler solver variance, with numbers (Results).
   - the GL coping band was tested with the G-buffer dumps: it is already in
     `RT_Dump_GBuffer_Albedo` (rows 582/583/586, ~80 levels over 1236 columns) and persists
     to HDR, with `FluidParticles` empty; recorded as a pre-existing GL scene difference,
     not fixed.
   - matrix4 ran on a Rebuild of the final HEAD: 5 APIs, gate exit 0, self-test 78 PASS,
     **zero** `[ERROR]`, gate-pattern, `compute dispatch failed`, `failed to create compute
     pipeline` or `SetValidatedLayout failed` lines in any cell or the 4K runs; compared with
     both `--ref win` and `--ref linux`; both sheets viewed and identical across all columns.
   - the storage-size comment is corrected (`1f37bfc`).
   Request: sign off WO-W1 so WO-W3 (documentation) can start.

2. **One residual item for your call, not blocking:** GL is the only API whose calm cells
   sit above noise on p99 in `ss_default_lake300` (11 against 6-8), all of it in the coping
   band above. If you want it gone it needs a GL texture-orientation investigation in the
   static coping material — a separate, pre-existing issue, so I did not open it.

3. **Host paths in an older doc (pre-existing, for your call).** My WO-W1 files carry none
   (`git grep` for the host, root and user over `T850/scripts/validation` and this doc finds
   nothing), but `documentation/scenes/backyard8-particle-fluid-investigation.md:626`
   contains the Windows host name in backticks followed by the root in parentheses (an
   MSVC build note). It arrived with `6e41c94` on 2026-11-09, outside WO-W1. I left it
   alone; say the word and I will redact it and scan the remaining docs.
