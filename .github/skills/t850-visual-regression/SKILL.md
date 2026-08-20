---
name: t850-visual-regression
description: "Use when asked to capture screenshots or render targets, create/replay frame dumps, generate visual baselines, compare PPM dumps, diagnose image differences, or verify D3D11/D3D12/GL/Vulkan rendering in T850."
argument-hint: "State case/scene, APIs, reference or candidate, dimensions, and whether exact comparison is required."
---

# T850 Visual Dump and Comparison Workflow

Use one of two workflows. Raw dumps diagnose one run; manifest baselines provide acceptance evidence.

## Roots and Prerequisite

```powershell
$RepoRoot = git rev-parse --show-toplevel
$SourceRoot = Join-Path $RepoRoot 'T850'
Set-Location $SourceRoot
.\scripts\build.ps1 -Config Release -Platform x64
```

Default capture executable is `bin\x64\Release\DayScene.exe`.

## A. Raw FrameDumper Diagnosis

Run from output directory:

```powershell
Set-Location .\bin\x64\Release
.\DayScene.exe `
  --api d3d11 `
  --scene 4 `
  --sceneFile Scenes/DayScene.t8scene `
  --width 1280 --height 720 `
  --dumpSnapshot-seconds 5 `
  --logLevel info --logFile logs/dump.log
```

Frame trigger alternative:

```powershell
.\DayScene.exe --api d3d12 --scene 1 --dump-frame 300
```

Output directory:

```text
dumps_<api>_f<frame>_<YYYYMMDD_HHMMSS>/
  RT_Dump_BackBuffer.ppm
  RT_Dump_<target>.ppm
  snapshot.json
  trace.json    # only when render tracing is compiled/enabled
```

A normal dump exits. Add `--keepRunning` to continue and allow repeated manual dumps.

### Replay

```powershell
.\DayScene.exe `
  --api d3d11 --scene 4 --sceneFile Scenes/DayScene.t8scene `
  --replaySnapshot .\dumps_d3d11_f300_TIMESTAMP\snapshot.json
```

Replay restores captured camera/light/render state, warms up three frames, then dumps. It is not complete gameplay save-state replay.

### Manual dump success check

Require:

- exit 0;
- one new dump directory;
- `RT_Dump_BackBuffer.ppm` exists and has expected dimensions;
- image is nonblank/nonuniform;
- snapshot exists;
- no `[ERROR]`, device lost, submit failure, or fatal line.

Do not rely on process exit alone.

## B. Manifest Visual Regression

### Reference

Create once before a reviewed change:

```powershell
Set-Location $SourceRoot
.\scripts\CaptureVisualBaselines.ps1 `
  -RunSet reference `
  -Force `
  -ContinueOnError
```

Do not overwrite an accepted reference during ordinary fixes.

### Candidate

```powershell
.\scripts\CaptureVisualBaselines.ps1 `
  -RunSet candidate `
  -Force `
  -ContinueOnError
```

Defaults:

- APIs: D3D11, D3D12, GL, Vulkan;
- 1280x720;
- five simulation seconds;
- fixed 1/60 delta with real-time pacing;
- timeout 240 seconds per case;
- output `VisualBaselines/<RunSet>`.

### Focused candidate

```powershell
.\scripts\CaptureVisualBaselines.ps1 `
  -RunSet candidate `
  -Cases scene-template-day `
  -Apis d3d11,d3d12 `
  -Force
```

Available cases:

```text
sandbox
day
quake3
ragdoll-editor
scene-template-q3-jolt
scene-template-q3
scene-template-day
scene-template-nexus
voxel-streaming
```

Focused runs preserve unrelated manifest entries.

### Capture parameters

```text
-RunSet reference|candidate
-Apis d3d11,d3d12,gl,vulkan
-Cases <ids>
-Width N -Height N
-DumpSeconds FLOAT
-FixedDeltaSeconds FLOAT
-TimeoutSeconds N
-ExePath PATH
-OutputRoot PATH
-ReplayFromRunSet reference|candidate
-Force
-KeepRawDumps
-ContinueOnError
```

`-ReplayFromRunSet` uses each matching case/API `snapshot.json` and changes mode to `snapshot_replay`.

## Capture Rejection Rules

The script fails or marks invalid when:

- executable is missing;
- process times out or exits nonzero;
- no new dump directory appears;
- backbuffer is missing;
- PPM dimensions differ;
- engine logs contain `[ERROR]`, fatal error, device lost, or failed Vulkan submit;
- image standard deviation is below 1.0 (uniform/black-like image).

It records executable SHA-256, image SHA-256/stats, Git state, GPU info, logs, snapshot, case args, status, and skip reason.

Skips are not passes:

- `skipped_hardware_limit`: Q3 Vulkan on adapters below 4 GiB;
- `skipped_missing_assets`: declared required files absent.

## Compare Full Matrix

Exact default:

```powershell
.\scripts\CompareVisualBaselines.ps1
```

Established final-tree D3D11 quantization allowance:

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

Other gates:

```text
-MaximumDiffPercent FLOAT
-MaximumAverageChannelDelta FLOAT
-ReferenceRoot PATH
-CandidateRoot PATH
```

Success: `Comparison failures=0`, exit 0. `not_comparable` entries preserve documented reference skips.

## Compare Arbitrary Dump Directories

```powershell
python .\scripts\compare_dumps.py REF_DIR CAND_DIR
python .\scripts\compare_dumps.py REF_DIR CAND_DIR --tolerance 2 --json
python .\scripts\compare_dumps.py REF_DIR CAND_DIR --tolerance 2 --report REPORT_DIR
```

Report mode writes HTML plus PPM heatmaps. Metrics include changed pixels/percent, maximum channel delta, average changed-pixel delta, average channel/luminance delta, and RGB averages.

Compare the same target, scene, resolution, frame/state, and API. Cross-API comparison is diagnostic, not the regression acceptance gate.

## Difference Diagnosis

1. Compare snapshot camera/frame/light/scene properties.
2. Confirm executable hashes and args in manifests.
3. Check logs before pixels.
4. Identify first differing render target when raw dumps are retained.
5. Use report heatmaps.
6. Classify pattern:
   - depth edges: projection/depth/raster state;
   - normal-dominant: TBN/normal map;
   - broad low-amplitude drift: precision/quantization;
   - sparse high-amplitude: raster/depth threshold;
   - final only: exposure/tone map/bloom/composition.
7. Reproduce twice from the same binary before blaming the code change.

Do not loosen tolerance until same-binary nondeterminism has been eliminated and the remaining bound is measured/reviewed.

## Cleanup

The harness removes raw dump directories unless `-KeepRawDumps`. It retains curated case directories/manifests.

Generated data is ignored. Remove only your temporary report/run sets. Preserve:

```text
VisualBaselines/reference/
VisualBaselines/candidate/   # when final evidence is requested
VisualBaselines/final-comparison.json
```

Detailed docs:

- `documentation/debug/visual-regression.md`
- `documentation/debug/diagnostics.md`
- `documentation/testing/verification.md`
