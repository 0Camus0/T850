# Documentation Audit - 2026-09-26

Scope: all 57 Markdown documents under `documentation/`, root `README.md`, `AGENTS.md`, all 15 repository skills, `documentation/rendering/webgpu-port-lessons.html`, and the standalone cache proxy report.

## Current contracts verified

- Seven DayScene runtime hosts and five supported Windows runtime graphics APIs.
- T8ditor remains limited to D3D11, D3D12, OpenGL, and Vulkan.
- The current engine self-test suite contains 78 checks.
- T850-owned config, scene, render-graph, and compute-manifest JSON rejects unknown keys; glTF extensions and opaque component payloads remain permissive.
- Navigation requests retain requester ownership and are canceled across queued, in-flight, and completed states.
- Render-graph dependencies and initialization are tracked per attachment; `initialized: true` requests a deterministic one-time zero clear.
- CPU animation pose evaluation remains scene-owned; visible bone texture uploads occur at the start of `RenderGraph::Execute()`.
- `Shaders/compute_kernels.json` is the strict packaged compute registry; `compute_depth` is the positive logical Z extent and defaults to one.
- WebGPU recovery uses a configurable consecutive-attempt budget, defaults to three, and has single-loss plus native repeated-loss stress commands.
- Driver resources and `ShaderProgramCache` are render-thread-affine.
- Tagged releases validate Windows ZIP, Android APK, and Steam tarball contents before publication and generate `SHA256SUMS.txt`.

## Updated documentation surfaces

- Public README, documentation index, current status, verification guide, glossary, dependency diagrams, architecture and platform lifecycle.
- Rendering, shader, compute, render-graph, texture, geometry, animation, navigation, scene, editor, diagnostics, Android, Steam Deck, and browser owner documents.
- All affected repository skills and AGENTS.md.
- WebGPU lessons HTML and the standalone Dawn/D3D12 cache report validation provenance.

## Validation

- All repository-relative links in 58 documentation Markdown files resolve.
- All 15 skills have valid frontmatter and relative links.
- Mermaid source blocks and updated API/animation/compute/recovery flows were reviewed for current ownership.
- Workflow YAML and `compute_kernels.json` parse successfully.
- HTML documents have no editor diagnostics; desktop/mobile overflow checks pass.
- `git diff --check` passes.

Historical benchmark values and dated test counts were retained where they describe a named earlier revision. They are labeled as historical and are not presented as current engine results.
