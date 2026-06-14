# DayScene Benchmark Determinism and D3D12 Performance Notes

## Problem

The DayScene benchmark matrix is intended to compare APIs by rendering equivalent work. The current time-based benchmark can make runs non-repeatable because each API/mode renders a different number of frames in the same wall-clock duration. That also means the final captured frame can be a different simulation frame for each run.

The D3D12 1080p matrix result also looked much slower than expected. In one matrix run, D3D12 1920x1080 onscreen reported ~60 FPS, similar to D3D11 at 4K, which is not a meaningful steady-state comparison.

## D3D12 performance finding

The bad D3D12 1080p matrix average is dominated by benchmark contamination from the first frame after the D3D11 -> D3D12 API transition.

Representative data from `benchmark_reports/perf_d3d12_20260613_151135`:

| Case | Avg all | Median | First frame | Max | Avg after first 20 frames |
|---|---:|---:|---:|---:|---:|
| Matrix D3D12 1080 onscreen | 16.58 ms / 60.3 FPS | 5.10 ms / 196 FPS | 2079.62 ms | 2079.62 ms | 5.17 ms / 193 FPS |
| Single-run D3D12 1080 onscreen | 1.45 ms / 691 FPS | 1.31 ms / 762 FPS | 16.67 ms | 29.01 ms | 1.32 ms / 758 FPS |
| Single-run Vulkan 1080 onscreen | 1.36 ms / 733 FPS | 1.33 ms / 753 FPS | 16.67 ms | 16.67 ms | 1.31 ms / 764 FPS |
| Single-run D3D11 1080 onscreen | 1.32 ms / 757 FPS | 1.17 ms / 855 FPS | 16.67 ms | 45.94 ms | 1.14 ms / 877 FPS |

Conclusion: D3D12 is not inherently running at 4K-D3D11 speed. The matrix is counting cold transition/setup work as part of the first D3D12 run. D3D12 steady-state in an isolated 1080p run is close to Vulkan and only moderately slower than D3D11.

The matrix should not include API-change cost, resource upload cost, PSO/descriptor warmup, or run-reset cost in measured frame samples. Those are separate startup metrics, not render-frame metrics.

## Deterministic benchmark model

DayScene benchmark mode should be deterministic by default:

1. Use a fixed simulation timestep.
   - Recommended: `dt = 1.0 / 60.0`.
   - Every benchmark update uses this dt, independent of real render time.

2. Use a fixed simulation frame count.
   - Debug equivalent of 10 seconds: `600` frames.
   - Full equivalent of 90 seconds: `5400` frames.
   - The report should display simulated duration as `frameCount * fixedDt`.

3. Separate simulation time from measured render time.
   - Simulation state advances by fixed dt.
   - FPS/frame-time stats must be measured from actual render/submit duration.
   - Do not use simulation dt as the measured frame time.

4. Run warm-up frames before measuring.
   - After every API change, resolution change, or same-API reset, render warm-up frames without recording benchmark samples.
   - Recommended: at least 30 frames, or enough to create PSOs/descriptors/resources and settle temporal render targets.
   - Reset benchmark counters after warm-up.

5. Capture deterministic final frames.
   - Final capture should always be taken at the same simulation frame index.
   - Store `simulationFrame`, `fixedDt`, `warmupFrames`, and `measuredFrames` in JSON/report output.

6. Seed/reset all nondeterministic state.
   - Fixed random seed for SSAO/noise/procedural data during benchmark.
   - Reset temporal render targets/history.
   - Reset camera spline agent, light rotation, exposure/adaptation, physics stats, and culling state.
   - Ignore runtime input while benchmark is active.

7. Report startup separately.
   - API switch duration, texture upload, PSO compilation, and scene reload should be logged as setup metrics.
   - They should not be part of FPS averages unless a separate "startup benchmark" is explicitly requested.

## Implemented default behavior

DayScene benchmark mode now uses fixed-step deterministic simulation by default. Unless overridden by CLI:

- Fixed simulation dt: `1.0 / 60.0`.
- Warmup frames: `30`.
- Measured frames: debug default equivalent of current 10 second benchmark (`600` frames).
- `--benchmarkFrames <N>` overrides measured frames.
- `--benchmarkFixedDt <seconds>` overrides fixed simulation dt.

Measured frame times are recorded from actual rendered frame duration in `Application`, not from fixed simulation dt. This keeps simulation deterministic while still measuring real render/submit performance.

Validation run:

- Command used `--benchmarkFrames 60` for a quick deterministic matrix.
- Report: `bin/x64/Release/benchmark_reports/deterministic3_20260613_152621/DayScene_Benchmark_Report.md`.
- Every matrix row reported exactly 60 measured frames.
- D3D12 1080p no longer showed the transition-spike-contaminated ~60 FPS value; the measured run reported ~698 FPS at 1080p onscreen.

## Benchmark flow

For DayScene matrix:

```text
for each API/resolution/mode:
  apply API/resolution/mode
  reset scene and render graph
  seed deterministic state
  run warmupFrames with fixedDt, no stats
  clear benchmark stats
  run measuredFrames with fixedDt, record real frame duration
  capture final frame at measuredFrames
  store JSON/report row
```

## Existing debug controls

The current code already has useful debug hooks:

- `--benchmarkFrames <N>`: end after a fixed number of update frames.
- `--benchmarkFixedDt <seconds>`: force simulation dt.
- `--benchmarkFinalFrameDump`: save final PPM captures.

These should become the basis of the default matrix behavior rather than optional debug-only controls.

## Implementation notes

- Add a real per-frame timer around render/submit for measured frame time. Do not derive benchmark FPS from `DtSecs` when `benchmarkFixedDt` is active.
- Add `benchmarkWarmupFrames` or a fixed internal warmup constant.
- In matrix mode, reset the app/frame timer after setup and warmup so the first measured sample cannot include API switch or resource upload work.
- Keep the current final-frame PPM path and report links; it is useful for validating deterministic rendering.
