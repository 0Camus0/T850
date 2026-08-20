# Frame Dumps and Visual Regression

Status: verified against FrameDumper and comparison scripts on 2026-08-19.

T850 has two related workflows:

1. **raw FrameDumper output** for diagnosing one run and inspecting all render targets;
2. **manifest-backed reference/candidate baselines** for repeatable same-scene, same-API acceptance.

A raw screenshot is not automatically a valid regression baseline.

## Tools

| Tool | Purpose |
|---|---|
| `Framework/include/debug/FrameDumper.h` / `src/debug/FrameDumper*.cpp` | trigger raw RT/backbuffer dumps and save/replay `snapshot.json` |
| `scripts/CaptureVisualBaselines.ps1` | launch fixed cases/APIs, validate output, retain curated artifacts, write manifest |
| `scripts/CompareVisualBaselines.ps1` | compare matching reference/candidate backbuffers and enforce gates |
| `scripts/compare_dumps.py` | compare arbitrary PPM dump directories; JSON/HTML/heatmap output |
| `scripts/t850_snapshot_mcp.py` | PPM comparison/report engine used by the Python front end |

Generated dumps and `VisualBaselines/` are ignored by Git. Archive accepted reference/candidate/report artifacts externally or in CI when they must survive workspace cleanup.

## Raw FrameDumper Workflow

Build Release x64, then run from its output directory:

```powershell
Set-Location F:\T850\T850
.\scripts\build.ps1 -Config Release -Platform x64
Set-Location .\bin\x64\Release

.\DayScene.exe `
  --api d3d11 `
  --scene 4 `
  --sceneFile Scenes/DayScene.t8scene `
  --width 1280 --height 720 `
  --dumpSnapshot-seconds 5 `
  --logLevel info `
  --logFile logs/raw-dump.log
```

Frame-number trigger:

```powershell
.\DayScene.exe --api d3d12 --scene 1 --dump-frame 300
```

Aliases: `--dumpFrame` and `--dumpSnapshot-frame`.

Manual request mode:

```powershell
.\DayScene.exe --api d3d11 --scene 1 --debugFrames --keepRunning
```

A normal dump sets `ShouldExit`; `--keepRunning` resets the request state and continues.

### Raw Output

The directory name is:

```text
dumps_<api>_f<frame>_<YYYYMMDD_HHMMSS>/
```

Contents depend on the scene/render graph:

```text
RT_Dump_BackBuffer.ppm
RT_Dump_<registered-target>.ppm
snapshot.json
trace.json            # only with T850_RENDER_TRACE
```

`FrameDumper` logs camera/light/matrix/property state and writes a JSON snapshot containing render state; skinned scenes can include bone matrices and RGBA32F bone texture data.

### Replay

Replay a snapshot with the same scene/assets:

```powershell
.\DayScene.exe `
  --api d3d11 `
  --scene 4 `
  --sceneFile Scenes/DayScene.t8scene `
  --replaySnapshot .\dumps_d3d11_f300_TIMESTAMP\snapshot.json
```

Replay:

- restores camera, light camera, lights, and captured `SceneProps`;
- applies optional matrices/omni/skinned data;
- suppresses camera updates;
- warms up three frames;
- triggers a new dump.

It is not a complete gameplay/world save. It does not recreate arbitrary component/event/physics/nav runtime state.

### Raw Dump Acceptance

Before using a raw dump as evidence, require:

- process exit 0;
- one newly created dump directory;
- `RT_Dump_BackBuffer.ppm` exists;
- expected dimensions;
- image is nonblank/nonuniform;
- `snapshot.json` exists;
- no `[ERROR]`, fatal error, device lost, or failed submit line.

The baseline harness automates these checks.

## Deterministic Baseline Mode

`CaptureVisualBaselines.ps1` passes:

```text
--regressionFixedDt 0.0166666666666667
```

This mode:

- uses a fixed 1/60 update delta with real-time pacing;
- fixes process RNG seed;
- suppresses live keyboard/mouse/gamepad input;
- disables relative mouse capture;
- keeps unattended cameras stable.

At default five seconds, the timed dump occurs at deterministic frame 300. This is distinct from benchmark fixed delta and should not be used for ordinary interactive play.

## Baseline Matrix

| Case | Runtime input | Coverage/skip rule |
|---|---|---|
| `sandbox` | Sandbox + `DamagedHelmet.glb` + fixed orbit yaw | four APIs |
| `day` | DayScene | four APIs |
| `quake3` | Quake3Mock + static DamageHelmet | four APIs |
| `ragdoll-editor` | RagdollEditor + Tyrant | four APIs |
| `scene-template-q3-jolt` | SceneTemplate + Q3 Jolt `.t8scene` | Vulkan requires >=4 GiB VRAM |
| `scene-template-q3` | SceneTemplate + non-Jolt Q3 `.t8scene` | Vulkan requires >=4 GiB VRAM |
| `scene-template-day` | SceneTemplate + DayScene `.t8scene` | four APIs |
| `scene-template-nexus` | SceneTemplate + Nexus | skip when two source models are missing |
| `voxel-streaming` | VoxelScene + generated mutable chunks | four APIs; persisted user edits are disabled in regression mode |

The 2 GiB Quadro P620 audit machine records two Q3 Vulkan cases as `skipped_hardware_limit`. Nexus records four `skipped_missing_assets` entries for `nexus_wars_terrain.glb` and `marine.glb`.

A skip is not a pass.

## Create a Reference

Only create/replace a reference before a reviewed change or after explicit rebaseline approval:

```powershell
Set-Location F:\T850\T850
.\scripts\build.ps1 -Config Release -Platform x64
.\scripts\CaptureVisualBaselines.ps1 `
  -RunSet reference `
  -Force `
  -ContinueOnError
```

Default output:

```text
VisualBaselines/reference/
  manifest.json
  <case>/<api>/
    RT_Dump_BackBuffer.ppm
    snapshot.json
    capture.json
    engine.log
    stdout.log
    stderr.log
```

Full raw dump directories are removed unless `-KeepRawDumps` is used.

## Capture a Candidate

Full matrix:

```powershell
.\scripts\CaptureVisualBaselines.ps1 `
  -RunSet candidate `
  -Force `
  -ContinueOnError
```

Focused examples:

```powershell
.\scripts\CaptureVisualBaselines.ps1 `
  -RunSet candidate `
  -Cases scene-template-day `
  -Apis d3d11,d3d12 `
  -Force

.\scripts\CaptureVisualBaselines.ps1 `
  -RunSet candidate `
  -Cases sandbox,day `
  -Apis vulkan `
  -Force `
  -ContinueOnError
```

Focused reruns merge untouched existing manifest entries.

### Capture Parameters

| Parameter | Default/meaning |
|---|---|
| `-RunSet reference|candidate` | `reference` |
| `-Apis` | all four APIs |
| `-Cases` | all case definitions |
| `-Width`, `-Height` | 1280x720 |
| `-DumpSeconds` | 5.0 |
| `-FixedDeltaSeconds` | 1/60; must be >0 and <=1 |
| `-TimeoutSeconds` | 240 per process |
| `-ExePath` | x64 Release DayScene |
| `-OutputRoot` | `VisualBaselines` |
| `-ReplayFromRunSet` | use matching case/API snapshot instead of timed dump |
| `-Force` | replace existing selected captures |
| `-KeepRawDumps` | retain full `dumps_*` directory |
| `-ContinueOnError` | finish matrix and report all failures |

Unknown case IDs fail before launch.

## Capture Validation and Manifest

Each capture checks:

- timeout and process exit;
- a newly created API dump directory;
- required `RT_Dump_BackBuffer.ppm`;
- exact requested dimensions from P6 header;
- channel standard deviation >=1.0;
- logs for `[ERROR]`, `fatal error`, `device lost`, or `vkQueueSubmit failed`;
- SHA-256 of retained backbuffer.

Invalid output becomes `invalid_capture`, `failed`, or `timeout`, increments failure count, and makes the script exit 1.

Manifest records:

- schema/run set/time;
- Git commit/dirty status;
- executable path/SHA-256;
- dimensions/time/fixed delta/replay source;
- APIs and GPU info;
- every capture status, args, duration, image stats/hash, logs/errors, source dump, and skip details.

## Compare Reference and Candidate

Exact default:

```powershell
.\scripts\CompareVisualBaselines.ps1
```

Established final-tree gate:

```powershell
.\scripts\CompareVisualBaselines.ps1 `
  -Tolerance 2 `
  -OutputPath .\VisualBaselines\final-comparison.json
```

Focused:

```powershell
.\scripts\CompareVisualBaselines.ps1 `
  -Cases scene-template-day `
  -Apis d3d11,d3d12 `
  -Tolerance 2
```

Parameters:

| Parameter | Meaning |
|---|---|
| `-ReferenceRoot`, `-CandidateRoot` | roots containing manifests |
| `-Tolerance` | ignore a pixel when its maximum channel delta is <=N |
| `-MaximumDiffPercent` | maximum percentage of pixels above tolerance |
| `-MaximumAverageChannelDelta` | average raw channel-delta gate when pixels exceed tolerance |
| `-Cases`, `-Apis` | filters |
| `-OutputPath` | JSON report path |

A candidate is missing when no matching captured case/API exists. Reference skips become `not_comparable` with their reference status.

Success is `Comparison failures=0` and exit 0.

The 2026-08-19 final report has:

- 26 comparable captures;
- 23 byte-exact;
- 3 D3D11 captures within maximum 1-2 channel levels;
- 6 documented non-comparable skips;
- 0 failures.

D3D12, GL, and Vulkan accepted captures are exact. Do not raise tolerance or update reference merely to make a failure disappear.

## Compare Arbitrary Dump Directories

```powershell
python .\scripts\compare_dumps.py REF_DIR CAND_DIR
python .\scripts\compare_dumps.py REF_DIR CAND_DIR --tolerance 2 --json
python .\scripts\compare_dumps.py REF_DIR CAND_DIR --tolerance 2 --report REPORT_DIR
```

The Python engine compares common `.ppm` targets and reports:

- total/changed pixels and percent;
- maximum channel delta;
- average changed-pixel delta;
- average channel and BT.709 luminance delta;
- per-RGB average delta;
- missing targets and size mismatches.

HTML report mode adds PPM heatmaps.

Cross-API comparison can diagnose backend differences, but acceptance compares each API only to the same API.

## Difference Diagnosis

1. Check process/log errors first.
2. Compare manifest executable hashes, dimensions, args, capture mode, frame, and GPU.
3. Compare snapshot camera, lights, matrices, scene properties, and skin data.
4. Re-run twice from the same unchanged binary to detect intrinsic nondeterminism.
5. Retain raw dumps and find the first differing RT/pass.
6. Generate a report/heatmap.
7. Classify the pattern:
   - depth-edge differences: projection/depth/raster state;
   - normal/TBN differences: normal map/tangent basis;
   - broad low-amplitude differences: precision/quantization;
   - sparse high-amplitude differences: raster/depth threshold;
   - final-backbuffer-only differences: exposure, tone mapping, bloom, composition.
8. Fix deterministic inputs/state before considering tolerance.

## T8ditor Dumps

T8ditor supports frame-based dumps:

```powershell
$log = 'F:\T850\T850\editor-dump.log'
$args = @('--api','d3d11','--sceneFile','Scenes/DayScene.t8scene','--dump-frame','30','--logFile',$log)
$p = Start-Process .\T8ditor.exe -ArgumentList $args -WorkingDirectory $PWD -PassThru
$p.WaitForExit()
```

Because it is a GUI subsystem process, wait explicitly. Validate the log and generated `dumps_<api>_f30_*` directory.

## Cleanup

Generated artifacts are ignored. Remove only artifacts from the current investigation:

```powershell
Remove-Item .\bin\x64\Release\dumps_* -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item .\bin\x64\Release\logs -Recurse -Force -ErrorAction SilentlyContinue
```

Preserve accepted `VisualBaselines/reference`; retain candidate/report only when requested as evidence.

## Related Documents

- [Debug and diagnostics](diagnostics.md)
- [Verification gates](../testing/verification.md)
- [Runtime configuration](../development/runtime-configuration.md)
- [Runtime hosts](../runtime/runtime-hosts.md)
- [Render graph](../rendering/render-graph.md)
