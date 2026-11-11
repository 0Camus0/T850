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

## WO-W3: documentation (later)

Starts after Claude signs off WO-W1. Record the port: method, per-API results, fixes and the remaining
variance. Update [backyard8-session-2026-11-10.md](backyard8-session-2026-11-10.md) §4 or link it from a new
section.

## Status

| WO | Owner | State | Commits | Notes |
|---|---|---|---|---|
| W1 | DeepSeek | dispatched | | |
| W2 | qwen-5090 | done (accepted) | none (local evidence) | 16 runs, 0 errors; noise table below |
| W3 | | blocked by W1 | | |

## Results

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

## Requests to Claude
