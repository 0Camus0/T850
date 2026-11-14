---
name: t850-windows-remote-host
description: "Use when building, syncing, running or comparing T850 on a Windows machine driven over SSH from Linux: windows_parity.py sync/build/run, GPU runs in the Windows console session, the shared Windows lock, the Backyard 8 five-API parity matrix and backyard8_parity_compare.py, or Python that generates PowerShell for the host."
argument-hint: "Say which revision to sync (HEAD or a working tree), what to run (smoke, full matrix, one script) and the output tag."
---

# T850 on a Remote Windows Host

Linux builds run Vulkan only. D3D11, D3D12, OpenGL and WebGPU evidence from a Linux workstation comes from one
Windows machine driven over SSH by `T850/scripts/validation/windows_parity.py`. Every rule below exists because
breaking it cost a run.

## Setup

- Set `T850_WIN_HOST` (`user@host`) and `T850_WIN_ROOT` (for example `D:\work`), or pass `--host` and `--root`.
  Never put their values in the repository, docs, commit messages or pasted logs; the repository is public.
- Host layout:

  ```text
  <root>\start_task.ps1     console-session launcher (copy scripts/start_task.ps1 from this skill)
  <root>\__parity\<tag>\    per-run scratch, fetched back to /tmp/winport/<tag>/
  <root>\T850\              the host's git checkout (its .git lives here)
  <root>\T850\T850\         the project: T850.sln, build\dawn-package\, bin\x64\Release\
  ```

- Host prerequisites: an OpenSSH server with key login, Visual Studio 2022 with v143, Python 3 on `PATH` (the
  Backyard 8 gate runs on the host), the Dawn package provisioned once (`LaunchSolution.bat --setup-only` runs
  `SetupDawn.ps1`; see `t850-build-run`), and the SSH user logged on to the console. A disconnected session still
  renders; a logged-off user does not.
- The host scripts refuse to write anywhere else: they print `ROOT_FAIL <path>` and stop when
  `<root>\T850\T850\T850.sln` is missing.

## Commands

Run from the repository root.

```bash
python3 T850/scripts/validation/windows_parity.py sync                     # HEAD; --tree <path> includes uncommitted changes
python3 T850/scripts/validation/windows_parity.py build                    # Release x64, Rebuild
python3 T850/scripts/validation/windows_parity.py run T850/scripts/validation/win/smoke_frame.ps1 smoke
python3 T850/scripts/validation/windows_parity.py run T850/scripts/validation/win/backyard8_parity.ps1 <tag>
python3 T850/scripts/validation/backyard8_parity_compare.py --ref win --win /tmp/winport/<tag>/out --out /tmp/winport/<tag>/compare
```

- `sync` copies changed and new tracked files only; `--prune` also deletes files the revision removed. It never
  touches `.git`, `bin`, `Librerias\vcpkg` or untracked assets.
- `build` runs `scripts\build.ps1 -Config Release -Platform x64` and prints
  `BUILD_EXIT=<n> ... exe_newer=<True|False>`. Accept it only with `BUILD_EXIT=0` and `exe_newer=True`. Use
  `--action Build` only right after a Rebuild of the same synced tree. `--setup-dawn` runs SetupDawn on failure;
  never use it without approval.
- `run <script> <tag>` copies the script to `<root>\__parity\<tag>\`, starts it in the console session through
  `start_task.ps1`, waits for `DONE.txt` (the wrapper writes it, with `ERROR.txt` on failure; `--timeout` seconds,
  default 6 hours) and fetches the folder to `/tmp/winport/<tag>/`. It passes `-Exe`, `-Out` and `-Root` to scripts
  that declare them, and nothing else; to change another parameter (for example `-Apis`), run a copy with different
  defaults.
- Every subcommand holds `/tmp/winport/windows.lock`. Never run Windows GPU work outside the tool or in parallel.

## Rules

| Rule | Why |
|---|---|
| The project is `<root>\T850\T850`; never write into `<root>\T850` itself. | That folder is the host checkout's root, beside its `.git`. A sync there built a copy without the Dawn package, which looked like a broken Dawn install. |
| Keep the host's bytes when only line endings differ. | Dawn's recipe check and `git status` hash raw bytes. Rewriting CRLF as LF made MSBuild demand a full Dawn rebuild. |
| Give changed files the current time; never stamp a fixed date. | MSBuild compares timestamps. Old dates on changed sources keep stale objects and a stale exe. |
| Rebuild by default and require `exe_newer=True`. | It proves the binary contains the synced source. |
| Run GPU work only through `run`. | SSH runs in session 0, where DXGI cannot create a swapchain. |
| Host scripts declare inputs in `param()` and run with `-NonInteractive`. | A missing mandatory parameter would prompt invisibly in the console session and hang while holding the lock. |
| Run SetupDawn only after diagnosing the message (`t850-error-triage`), and only with approval. | A changed overlay or vcpkg pin rebuilds Dawn, a long build, and a missing vcpkg checkout is cloned. After an intentional translator edit it only refreshes the audit. |
| No host details in the repository. | The repository is public. |
| Copy evidence you need out of `/tmp`. | `/tmp` is cleared at reboot on the Linux workstation. |

## Writing PowerShell from Python

- Keep host scripts as raw strings (`r'''...'''`) and pass values as parameters instead of formatting code. If you
  must format a template, double every PowerShell brace (`{{ }}`).
- Edit the tool with targeted changes, never a whole-file `sed`.
- After changing the tool or a host script, print the generated text once, then `sync` and run `smoke_frame.ps1`
  before a long matrix.
- Logs written by PowerShell redirection are UTF-16: read them with `iconv -f utf-16 -t utf-8` or
  `bytes.decode('utf-16')`. The matrix's `progress.txt` is UTF-8.

## Backyard 8 parity matrix

`win/backyard8_parity.ps1` runs, for each API (default `vulkan,d3d12,d3d11,gl,webgpu`): the Backyard 8 gate, eight
cells, the game self-test, one 3840x2160 cell and a log scan. Vulkan runs each cell twice (`_r1`, `_r2`) to measure
noise.

- Cells: renderer `ss` (`--fluid-render screen-space`) or `surfel` (`--fluid-render splat`); view `default` or
  `close` (`--backyard-camera 0 1.3 -10.0 0.32 0`); start `water150` (`--backyard-start water --dump-frame 150`) or
  `lake300` (`--dump-frame 300`). All at 1920x1080 with
  `--regressionFixedDt 0.0166667 --postProcessMode compute --backyard-no-hud`.
- `backyard8_parity_compare.py --ref win` compares `RT_Dump_HDR_Final.ppm` against Windows Vulkan `r1`, with `r1`
  against `r2` as the noise. A cell is `noise` when its mean, p99 and % of pixels above 4 are within 1.5x the noise
  (+1 point on the %). The default, `--ref linux`, needs Linux Vulkan references and a `noise.json` in `--linux`.
- Look at `sheet.png` and every `diff_<api>_<cell>.png`, and read `progress.txt`: any `ERRORS`, `BAD`,
  `DUMP_MISSING` or `LOG_MISSING` line fails the matrix. Each API needs `GATE <api> exit=0`,
  `SELFTEST <api> exit=0 PASS=78 FAIL=0` and code 0 on every `EXIT` line. In Nov 2026 every cell enabled 22 kernels
  and dispatched 14 (screen space) or 9 (surfels).
- Known residuals, recorded in `documentation/scenes/backyard8-windows-port-work-orders.md`: the calm `lake300` cells
  carry a small mean offset on every non-Vulkan API (0.44–1.05 against a run-to-run noise of 0.19–0.72), accepted as
  solver variance between shader compilers; a GL-only band on the far pool coping is already in the G-buffer albedo.

## Report

The synced revision, the build line (`BUILD_EXIT`, `exe_newer`), the matrix tag, per-API gate, self-test and log
results, the compare table, the images you looked at, and each remaining difference classified with
`t850-error-triage`. No host details.
