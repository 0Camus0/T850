---
name: t850-api-frame-comparison
description: "Use when T850 must dump the same frame on D3D11, D3D12, Vulkan, and OpenGL; compare render targets across APIs; diagnose a white overlay, black frame, lighting mismatch, shadow mismatch, or first divergent pass; or prove cross-backend visual parity."
argument-hint: "State scene/index, configuration, frame or fixed time, APIs, resolution, and reference API."
---

# T850 Cross-API Frame Dump and Comparison

Use this workflow to compare one deterministic scene state across graphics APIs. Work from early render targets toward the backbuffer; do not patch the final image before finding the first divergent pass.

## 1. Establish Paths and Preserve State

```powershell
$RepoRoot = git rev-parse --show-toplevel
$SourceRoot = Join-Path $RepoRoot 'T850'
$Output = Join-Path $SourceRoot 'bin\x64\Release'
$Exe = Join-Path $Output 'DayScene.exe'
```

Before building or capturing:

```powershell
git status --short
git diff --check
```

Do not revert local `T850/config.json`, `T850/Librerias/vcpkg`, scene saves, or unrelated worktree changes. Build outputs and dump directories are generated evidence, not source changes.

## 2. Build Once

```powershell
Set-Location $SourceRoot
.\scripts\build.ps1 -Config Release -Platform x64 -Action Build
```

Use the same executable for every API. If source changes between captures, rebuild and recapture the entire comparison set.

## 3. Choose a Deterministic Case

Current scene indices:

```text
0 Sandbox
1 DayScene
2 Quake3Mock
3 RagdollEditor
4 SceneTemplate
5 VoxelScene
6 Minecraft
```

Prefer a fixed frame and fixed delta:

```powershell
$CommonArgs = @(
  '--scene','6',
  '--width','1280','--height','720',
  '--regressionFixedDt','0.0166666667',
  '--dump-frame','61',
  '--logLevel','info'
)
```

Use the same scene file, camera, profile, resolution, frame, and asset set for every backend. A snapshot replay is useful when camera or runtime state cannot otherwise be reproduced:

```powershell
.\DayScene.exe --api d3d12 --scene 6 --replaySnapshot PATH\snapshot.json
```

Snapshot replay restores captured camera/light/render state, not a complete gameplay save state.

## 4. Capture All Four APIs Sequentially

Run from the executable output directory so assets and DLLs resolve:

```powershell
Set-Location $Output
$Before = @(Get-ChildItem -Directory -Filter 'dumps_*' | Select-Object -ExpandProperty FullName)

$Results = foreach ($Api in @('d3d11','d3d12','vulkan','gl')) {
  & $Exe --api $Api @CommonArgs
  [pscustomobject]@{ Api = $Api; ExitCode = $LASTEXITCODE }
}
$Results | Format-Table -AutoSize
```

Never run GPU captures concurrently. Require exit code 0 for every API.

A successful capture creates:

```text
dumps_<api>_f<frame>_<YYYYMMDD_HHMMSS>/
  RT_Dump_BackBuffer.ppm
  RT_Dump_<render-target>.ppm
  snapshot.json
```

The exact render-target list is scene-owned. Minecraft currently dumps all GBuffer attachments, depth, shadow atlas/accumulation, deferred output, and backbuffer.

## 5. Validate Each Capture Before Comparing

For each new dump require:

- exactly one new directory for that run;
- expected frame and API in the directory name;
- `RT_Dump_BackBuffer.ppm` and `snapshot.json` exist;
- dimensions match requested width/height;
- image is nonblank and nonuniform;
- engine log has no `[ERROR]`, device removal/loss, failed submit, allocation failure, or fatal line.

Do not call a uniform image a pass merely because the process exited 0. White, black, and fallback-texture frames are invalid evidence.

Frame dumps generally capture render targets before the runtime ImGui gameplay/developer overlay. For HUD validation, capture the composed application window separately; do not infer HUD correctness from `RT_Dump_BackBuffer.ppm`.

## 6. Compare Dumps

Use D3D12 as the usual diagnostic reference on Windows:

```powershell
Set-Location $SourceRoot
python .\scripts\compare_dumps.py `
  "$Output\dumps_d3d12_f61_REFERENCE" `
  "$Output\dumps_vulkan_f61_CANDIDATE" `
  --tolerance 2 --json
```

Generate heatmaps and HTML when needed:

```powershell
python .\scripts\compare_dumps.py REF_DIR CAND_DIR `
  --tolerance 2 --report REPORT_DIR
```

Repeat D3D12 against D3D11, Vulkan, and GL. Cross-API images are not expected to be byte-identical because rasterization, filtering, and precision differ. Evaluate:

- average channel delta;
- maximum delta;
- changed-pixel percentage;
- average luminance delta;
- spatial shape in heatmaps;
- whether scene content and composition visibly align.

## 7. Find the First Divergent Pass

Compare in this order:

1. GBuffer albedo/specular;
2. normals/roughness;
3. PBR/material attachments;
4. depth;
5. shadow atlas;
6. shadow accumulation before/after filtering;
7. deferred HDR target;
8. bloom/DOF/tone mapping;
9. backbuffer.

Interpret common patterns:

| Pattern | Likely owner |
|---|---|
| GBuffer already differs | mesh shader, vertex layout, material/texture binding |
| Atlas matches, ShadowAccum differs | CSM sampling, depth convention, atlas origin, PCF/SSAO |
| Deferred is uniform white | unbound texture fallback, additive pass, invalid light/material arithmetic |
| Deferred matches but Extra16F is uniform white | one-input pass incorrectly using two-input `LIGHT_ADD`; use slot 0 + `FSQUAD_1_TEX` |
| Intermediates match, backbuffer differs | present/copy/color conversion or late overlay |
| Only GL shadow rows differ | top-left authored atlas versus bottom-left GL viewport mapping |
| Broad low-amplitude drift | precision, filtering, quantization |
| Sparse depth-edge differences | raster/depth thresholds |

Use a reversible constant-color or single-texture shader probe only after the first divergent pass is known. Remove every probe before final validation.

## 8. Manifest Regression Gate

For maintained reference/candidate evidence, use the existing skill and scripts:

```powershell
Set-Location $SourceRoot
.\scripts\CaptureVisualBaselines.ps1 -RunSet candidate -Force -ContinueOnError
.\scripts\CompareVisualBaselines.ps1 -Tolerance 2 `
  -OutputPath .\VisualBaselines\final-comparison.json
```

Do not overwrite an accepted `VisualBaselines/reference` during an ordinary bug fix.

## 9. Final Proof and Reporting

After the fix:

1. rebuild once;
2. recapture all affected APIs from the same binary;
3. validate logs and image statistics;
4. rerun comparison/heatmaps;
5. run `DayScene.exe --game-selftest`;
6. run `git diff --check`;
7. confirm no diagnostic probe remains.

Report:

- scene, frame/time, dimensions, executable/configuration;
- reference and candidate dump directories;
- first divergent render target;
- root cause and files changed;
- before/after image metrics;
- all API exit codes;
- remaining measured backend variance;
- tests and any unavailable evidence.

Related workflow: `t850-visual-regression`.

Related documentation:

- `documentation/debug/visual-regression.md`
- `documentation/debug/diagnostics.md`
- `documentation/rendering/render-graph.md`
- `documentation/testing/verification.md`
