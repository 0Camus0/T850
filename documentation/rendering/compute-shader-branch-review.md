# Compute Shader Branch Review

**Reviewed:** 2026-09-17
**Base:** `origin/microsoft_webgpu_branch` (`70327f3`)
**Decision:** Ready for pull request, subject to CI platform gates.

## Review Result

No unaddressed code defects were found in the complete branch diff. The review
covered the shared compute contract, all desktop backend implementations,
render-graph selection, shader permutation/precompile handling, Minecraft torch
particles, runtime configuration, launcher arguments, build registration, and
documentation.

The post-process selector has two explicit states: `compute` enables every
declared `compute_if_supported` graph pass, while `raster` selects each authored
graphics or clear fallback. The obsolete `auto` mode and `prefer_compute` graph
field were removed. Minecraft TorchParticles follows the same selector: it
dispatches in compute mode and produces its transparent fallback in raster mode.

Desktop OpenGL requests a 4.3 compatibility context and enables compute only
when `GLEW_VERSION_4_3` is available. WebGPU/Dawn uses the hardware D3D12 adapter
and its `auto` shader flow correctly falls back from missing generated WGSL to
HLSL through SPIR-V and Tint. The prior TorchParticles preparation failure was
caused by an outdated Debug executable; current Debug and Release builds prepare
and dispatch the pipeline successfully.

## Validation

- `ValidateBuildRegistration.ps1`: passed.
- Release `DayScene.exe --game-selftest`: passed all registered checks. Logged
  parse and manifest errors are intentional negative-path assertions.
- `TestLauncherWebGPU.ps1 -Ui`: passed for developer and portable launchers,
  including the persisted `compute|raster` selector and WebGPU shader-flow args.
- Compute self-test: passed on D3D11, D3D12, Vulkan, desktop GL 4.3+, WebGPU
  `auto`, and WebGPU `spirv`.
- Runtime matrix: 70/70 passed. It covered scenes 0-6 in both compute and
  raster modes on D3D11, D3D12, Vulkan, desktop GL, and WebGPU at 1023x577.
  Every run completed a deterministic render-target dump without an engine,
  validation, device-loss, or submit error. Compute runs dispatched; raster
  runs suppressed dispatch and selected their authored fallback.
- Final Release x64 build: passed.
- `git diff --check`: passed.

## Local Platform Limitation

The six-cell Windows build matrix is blocked locally because the checkout has
only `x64-windows` and `x64-windows-static` vcpkg installations. The required
`x86-windows` and `arm64-windows` Vulkan headers are absent, so the Win32/ARM64
cells cannot compile. This is an environment prerequisite, not a source failure.
The pull request's Windows CI matrix remains the required platform gate.

## Operational Notes

- The root `T850Launcher.exe` is the developer launcher built from
  `scripts/Launcher.ps1`; it resolves `bin/<architecture>/<configuration>` and
  exposes Windows/architecture selectors.
- `scripts/build_launcher_release.ps1` intentionally replaces the root launcher
  with the portable `Launcher_Release.ps1` only while staging release artifacts.
- Debug Dawn is substantially slower than Release because it uses unoptimized
  engine code and Dawn's debug library. This is expected and not a stale-binary
  or TorchParticles failure.
