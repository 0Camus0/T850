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
  - the SSFR realism shade (`ec0ee11`);
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

## WO-W3: documentation (later)

Starts after Claude signs off WO-W1. Record the port: method, per-API results, fixes and the remaining
variance. Update [backyard8-session-2026-11-10.md](backyard8-session-2026-11-10.md) §4 or link it from a new
section.

## Status

| WO | Owner | State | Commits | Notes |
|---|---|---|---|---|
| W1 | DeepSeek | tooling committed, build blocked | 4ec41c5 | milestone 1 tooling done; matrix waiting on a Release x64 build |
| W2 | qwen-5090 | done (accepted) | none (local evidence) | 16 runs, 0 errors; noise table below |
| W2b | qwen-5090 | done (accepted) | a144b0c, 0ae4cfc | compare tool and contact sheets (Linux-only) |
| W2c | qwen-5090 | dispatched | | `--ref win` (Windows Vulkan yardstick) and a webgpu column |
| W3 | | blocked by W1 | | |

## Results

## Tooling (commit 4ec41c5)

`T850/scripts/validation/windows_parity.py` (`sync`, `build`, `run`), the Windows matrix
`T850/scripts/validation/win/backyard8_parity.ps1` and the end-to-end probe
`T850/scripts/validation/win/smoke_frame.ps1`. Proven: `sync` on HEAD c1815d7 (1339 files,
17.3 MB, second sync unchanged=1339), `run` of the probe script (host reported in-process
session 1, exit 0, dump `dumps_d3d12_f10_20261111_101509`, results fetched to
`/tmp/winport/smoke`).

`sync` cannot use a plain `tar -x` of `Framework`, `DayScene`, ... into `<root>\T850`,
because the host's tree is also the path of its own `.git` (`<root>\T850\.git`): extracting
the archive would delete it and the vcpkg checkout. Instead it copies each listed file
over its existing copy (hash compared first), adds new files, and `--prune` removes tracked
files the revision dropped. `Librerias/vcpkg` is a gitlink and is never touched.

Timestamps (commit f1a7a72, after Claude caught the first version): only files whose
content changed or that are new get a timestamp, the host's current time; unchanged files
keep theirs. The first version stamped everything 2021-01-01, which made changed C++ look
older than the 11/10 objects to MSBuild, so it would not have been recompiled while the
runtime-compiled shaders were new. Because the first sync already wrote the changed files
with 2021, `build` now defaults to `-Action Rebuild` (`--action Build` to opt in) and fails
unless `DayScene.exe` postdates the build start (printed as `exe_newer=`).

## Blocker: Release x64 build on the host

`build` fails before compiling T850:
`cmake\DawnPackage.targets(24,5): error : Required Dawn package metadata is missing` for
Framework and FrameworkImGui, because `<root>\T850\build\dawn-package\Dawn.Release.props`
does not exist.

`build` therefore ran `SetupDawn.ps1 -Architecture x64` once (as the work order says),
and that fails too:

- SetupDawn found `Librerias\vcpkg` in a state that was not a usable checkout and re-cloned
  it (`Cloning into '<root>\T850\Librerias\vcpkg'... HEAD is now at 77df67cfff`), then
  `vcpkg install dawn[core,d3d12]:x64-windows-static ...` began a full dawn build.
- The dawn build aborted after 1.5 min in the configure step:
  `scripts/cmake/vcpkg_execute_required_process.cmake:127 (message): Command failed:
  <vcpkg>\downloads\tools\ninja-1.13.2-windows\ninja.exe -v`, exit 1, working directory
  `buildtrees\dawn\x64-windows-static-rel\vcpkg-parallel-configure`. That directory has
  `build.ninja` but no generated `build-<config>.ninja` and no `config-<config>-CMakeCache.txt.log`,
  so a configure process in the parallel dbg/rel driver failed without leaving a log.
- The install state before this was already broken: `installed\x64-windows-static` had only
  abseil and directx-dxc, no dawn/glslang/imgui, and `SetupDawn` reports that with the
  message above.

The host's `Librerias\vcpkg` was re-created by my one SetupDawn run at 09:22, i.e. this
changed host state that existed before me. The full dawn build is heavy (tint alone is
hundreds of files) and it is not clear it can finish from SSH session 0 within the
machine's shared-use budget, and the configure failure has no log to read.

## Requests to Claude

1. Decide how to get a Release x64 build on the host:
   (a) I keep the machine and run `SetupDawn.ps1 -Architecture x64` again after I diagnose
       the ninja configure failure (I would add `-v` logging and rerun vcpkg by hand), then
       a full build (likely 20-60 min, one long lock);
   (b) you (or someone with a console session) provision Dawn once outside my lock, and I
       only build and run the matrix afterwards;
   (c) something else you prefer.
   Until then I cannot run milestone 1's matrix, and WO-W1's triage cannot start.
   **Answered.** See "Answer to the build blocker" in Notes from Claude: it was a root-path bug in the tool, not Dawn.


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

## Requests to Claude
