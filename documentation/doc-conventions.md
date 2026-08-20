# Documentation Conventions

Status: verified and extended for operational documentation on 2026-08-19.

This file defines how the `documentation/` tree should be written so every stage is consistent.

## File organization

- Use one Markdown file per major subsystem or flow.
- Keep cross-subsystem topics linked instead of duplicating long explanations.
- Use relative links between files.
- Keep diagrams close to the text they explain.
- Prefer code/file references over vague subsystem names.

## Standard document structure

Each subsystem document should follow this structure unless there is a good reason to deviate:

```md
# Subsystem Name

## Purpose
## Key files and classes
## Runtime ownership
## Initialization flow
## Per-frame flow
## Data model
## Important APIs
## Editor integration
## Runtime scene integration
## Diagrams
## Extension points
## Known limitations
## Debugging checklist
## Related documents
```

Operational guides should instead use:

```md
# Workflow Name

Status: verified against <scripts/source> on YYYY-MM-DD.

## Roots and prerequisites
## Exact commands
## Inputs/options
## Outputs/artifacts
## Success criteria
## Stop conditions and failure classification
## Cleanup
## Related documents
```

## Diagram conventions

Use Mermaid for diagrams.

Recommended diagram types:

- `flowchart TD` for build/load/update pipelines.
- `flowchart LR` for render/input/data paths.
- `sequenceDiagram` for frame lifecycle or async flows.
- `classDiagram` only when class dependency matters more than data flow.

Keep node names short, but add surrounding prose that names the exact files and classes.

## Naming conventions

- Use code formatting for class and function names: `RenderMesh`, `PrimitiveInst`, `SceneTemplate`.
- Use inline paths for files: `T850/Framework/src/scene/RenderMesh.cpp`.
- Use `.t8scene` for authored editor scenes.
- Use “Framework” for core reusable engine systems.
- Use “T8ditor” for the editor executable and editor-only code.
- Use “SceneTemplate” for the runtime scene that loads `.t8scene`.

## Required detail level

Each document should make it possible to answer:

- What owns this data?
- Who creates it?
- Who destroys it?
- What thread/frame phase runs it?
- How does it reach the GPU/physics/navigation/runtime system?
- What is editor-only versus runtime?
- What breaks when this subsystem is misconfigured?

For commands, also answer:

- What directory must the shell use?
- Which script is authoritative?
- What exit code/text/artifact proves success?
- Is a result passed, skipped, unsupported, or environment-blocked?
- Which generated files are safe to remove?

## Freshness Rules

- Put a source-verification date near the top of every current document.
- Do not promote a command to Verified unless its script and options were read and, when the host supports it, executed.
- Keep future design in an explicit Planned/Optional section; do not mix it into implemented API prose.
- When a specification becomes implemented, replace “new/proposed/required” language with current behavior and retain deferred items explicitly.
- Remove superseded documents instead of keeping a parallel legacy tree; Git history is the archive.
- Run a relative-link audit and `git diff --check` after documentation edits.

## Small-Model Writing Rules

- Use one canonical command first, then variants.
- State repository root versus source root before commands.
- Prefer tables for flags/outputs and short numbered workflows.
- Include expected output and a stop condition.
- Link to detail instead of duplicating volatile implementation.
- Avoid unexplained historical stage names in current docs.

## Cross-linking conventions

Add a “Related documents” section to every document.

Example:

```md
## Related documents

- [Dependency map](dependency-map.md)
- [Geometry loading](geometry/loading-geometry.md)
- [Shader management](rendering/shader-management.md)
- [Render graph](rendering/render-graph.md)
```

## Agent workflow for future stages

For each stage, narrow the agent context to the relevant files. The expected output is the Markdown document plus links from `README.md` and, when needed, `glossary.md`.

Each agent should report:

- Files inspected.
- Classes/functions documented.
- Gaps or uncertainties.
- Follow-up links needed.
