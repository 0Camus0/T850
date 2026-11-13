---
name: t850-change-self-check
description: "Use before committing any T850 change and when reviewing another agent's change: run check_change.py, apply the gates each changed path needs (kernels and GL twins, cbuffers, build lists including Android, scene and graph JSON, backends, host tooling), verify every claim in docs, UI text and commit messages against the code, and follow the shared-checkout, attribution and host-detail rules."
argument-hint: "Give the base ref (usually origin/master) and whether you are checking your own change or reviewing someone else's."
---

# T850 Change Self-Check

A passing build does not prove a change is complete. Misses that reached review or CI in this repository:

- a new source missing from the Android list;
- a cbuffer member inserted mid-struct;
- an optimization gate that never fired;
- a skip that left holes near the camera;
- UI text claiming which renderer reads a setting, without checking.

## 1. Run the script

```bash
python3 .github/skills/t850-change-self-check/scripts/check_change.py --base origin/master
```

It reads the commits since the merge base, plus staged, unstaged and untracked files, and exits 1 on any failure. It
never builds or runs anything.

| Check | Fails on |
|---|---|
| `protected-files` | committing `T850/config.json`, the `Librerias/vcpkg` pointer, `Assets/.t850-cloud-assets.json`, anything under `T850/bin/`, `dumps_*` or `VisualBaselines/` |
| `attribution` | a `Co-authored-by` trailer, author or committer naming Copilot (repository rule) |
| `host-details` | an added line with a private IPv4 address or the value of `T850_WIN_HOST` or `T850_WIN_ROOT` |
| `hlsl-portability` | an added `isfinite`, `isnan` or `isinf` (WebGPU aborts), or `Sample`, `SampleBias`, `SampleCmp`, `ddx` or `ddy` in a `CS_*.hlsl` kernel |
| `gl-twins`, `fluid-constants` | `check_gl_twins.py` or `check_fluid_constants.py` failing, when kernels, twins, the manifest or `ComputeKernelRegistry.cpp` changed |
| `build-lists` | a new `.cpp` missing from its vcxproj, its filters, the desktop `CMakeLists.txt` or `cmake/AndroidBuild.cmake`, or a deleted one still listed |
| `json`, `python` | changed JSON that does not parse, or changed Python that does not compile |
| `whitespace` | `git diff --check` findings, except in generated files (the CRLF GL twins) |

Warnings do not fail the run: locally modified machine files that are not staged, a re-frozen
`backyard8_references.json`, a changed Dawn recipe file, and scratch files left in the tree.

`build-lists` notes:

- A source that must not build on Android (Windows APIs, the WebGPU/Dawn backend, desktop tooling) needs
  `--desktop-only <path>`, and one sentence in the commit message saying why. Sources inside an `if(...)` block of
  the desktop `CMakeLists.txt` are already treated as platform-specific.
- For Framework and DayScene sources, `scripts/ValidateBuildRegistration.ps1` (Windows, CI) checks only `game/`,
  `terrain/`, a fixed file list and two scenes; this check covers every added source.
- DayScene has no live filters file: `App.vcxproj.filters` matches no project and is read only by
  `ValidateBuildRegistration.ps1`.

## 2. Gates by changed path

| Changed | Also required |
|---|---|
| `Assets/Shaders/CS_*.hlsl` | Regenerate the GL twin: `cd T850 && SPIRV_CROSS=<spirv-cross> python3 scripts/shaders/generate_gl_compute_twins.py CS_X.hlsl` (`--check` verifies). Run a scene that dispatches the kernel. New cbuffer members go at the end, padded to a 16-byte row, mirrored in the C++ struct and its `static_assert`, with `constant_count` updated (`t850-compute-kernel`). |
| `Assets/Shaders/compute_kernels.json`, `Assets/Scenes/*.json` | Strict JSON: unknown keys are rejected at load. Load a scene that uses it (`--unattended --scene N`, bounded with `timeout -k 10`) and read the log for `[ERROR]`. |
| `Framework/src/video/<api>/` | Backend-local. Linux runs only Vulkan; D3D11, D3D12, GL and WebGPU changes need a Windows run (`t850-windows-remote-host`). |
| `WebGPUShaderCompiler.*`, `ShaderPreprocessor.*`, `Librerias/simplecpp/`, `cmake/vcpkg-overlays/`, `cmake/dawn-package/`, `scripts/SetupDawn.ps1` | Dawn recipe files: every Windows x64/ARM64 checkout fails its build with `Dawn setup changed (<file>)` until setup runs again there (`t850-error-triage`). Say so in the commit message. |
| Backyard 8 water | `T850/scripts/validation/backyard8_regression.py` must pass. Re-freezing its references needs reviewer sign-off. |
| Tooling that runs on another machine | Print the generated script and run the smoke before relying on it (`t850-windows-remote-host`). |
| Docs, UI labels, commit messages | Section 3. |

## 3. Verify every claim

For each sentence you add that states what code does ("only the screen-space renderer reads X", "costs 0.4 ms",
"fixes the holes"), cite the code you read (file and function) or the run that measured it. If you cannot, delete the
sentence. UI labels and tooltips are claims too.

## 4. Repository rules

- Commit with a pathspec: `git commit -m "..." -- <paths>`. Never `git add -A` or `git add .`: other agents may be
  working in the same checkout.
- Never `git stash`, `reset`, `clean`, `rebase`, `checkout` or `restore` files you did not change, amend someone
  else's commit, or switch branches in a shared checkout; use `git worktree add` or a separate clone. Never push
  unless asked.
- Never edit `T850/config.json` or retarget the asset links in `T850/bin/<platform>/<config>/`; use command-line
  flags.
- Run DayScene with `--unattended` and an explicit `--scene N`, bounded with `timeout -k 10 <seconds>`. Do not run
  GPU work while someone measures performance; local model servers can share the GPU.
- No `Co-authored-by: Copilot` trailer in this repository.
- No host names, IP addresses, user names or host paths in commits, docs or pasted logs; use environment variables.
- Say in the commit message what you ran and the result.

## 5. Reviewing another agent's change

- Run the script yourself; do not trust a "PASS" in the commit message.
- Reproduce one claimed measurement on the same build.
- For every new gate, skip, early-out or optimization, measure with it on and off: the output must stay within noise
  and the cost must change. No cost change means the gate never fires.
- Look at the images, not only the statistics: a close camera (`--backyard-camera 0 1.3 -10.0 0.32 0` in Backyard 8)
  for culling and skips, and 3840x2160 for anything sized per pixel.
- Performance baselines: same binary and configuration, the second of two runs, nothing else on the GPU,
  `--benchmarkFixedDt` for GPU timing.
- Reject workarounds that satisfy a check instead of fixing the cause: fake reads that keep an unused binding alive
  (delete the binding), loosened tolerances, references re-frozen to pass.
- Verdict: accept, or list each defect with the file, the evidence and the required fix.
