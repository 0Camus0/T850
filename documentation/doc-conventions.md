# Documentation Conventions

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

## Cross-linking conventions

Add a “Related documents” section to every document.

Example:

```md
## Related documents

- [Geometry loading](../geometry/loading-geometry.md)
- [Shader management](shader-management.md)
- [Render graph](render-graph.md)
```

## Agent workflow for future stages

For each stage, narrow the agent context to the relevant files. The expected output is the Markdown document plus links from `README.md` and, when needed, `glossary.md`.

Each agent should report:

- Files inspected.
- Classes/functions documented.
- Gaps or uncertainties.
- Follow-up links needed.

