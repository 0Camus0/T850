# Tagged Scene Regions

Status: implemented and tested on 2026-09-07.

## Purpose

Regions are genre-neutral oriented boxes with stable IDs and tags. RTS code can
interpret tags as territory/buildability, while shooter code can use them for
objectives or trigger tests. Framework assigns no gameplay meaning to a tag.
Regions do not automatically alter navigation, collision, rendering, or ownership.

## Schema and Ownership

The `.t8scene` root accepts a `regions` array:

```json
{
  "regions": [{
    "id": "region_base",
    "name": "Base Area",
    "position": {"x": 0, "y": 5, "z": 0},
    "rotation": {"x": 0, "y": 30, "z": 0},
    "half_extents": {"x": 12, "y": 5, "z": 8},
    "tags": ["buildable", "objective"],
    "enabled": true
  }]
}
```

Positions/extents use world units; rotations use degrees in XYZ order. IDs must
be unique and nonempty, transforms finite, and extents positive. Load/save validate
these rules. Renaming does not change identity.

Framework owns [SceneRegions.h](../../T850/Framework/include/scene/SceneRegions.h)
and [SceneRegions.cpp](../../T850/Framework/src/scene/SceneRegions.cpp).
EditorWorld has an authoring copy. GameLogicSystem owns the loaded runtime copy
and clears it on shutdown.

## Editor and Runtime

Open **View > Regions**, or **Regions** in the terrain inspector. Add/remove
regions, edit transforms/tags, or frame a region. The panel docks with Properties.
Gold outlines indicate enabled regions; gray indicates disabled. Regions participate
in save/load, whole-scene undo, and Play export.

`RegionContainsPoint` includes boundaries and ignores disabled regions.
`QuerySceneRegions` returns matching IDs in document order, optionally filtered by
an exact, case-sensitive tag. Gameplay components can call:

```cpp
const auto regions = system.RegionsAt(worldPosition, "objective");
```

Queries currently use a linear scan. They do not synthesize enter/exit events,
overlap priority, teams, or economy rules. Game modules can compare membership
between ticks or add components using these queries. Spatial indexing is future
performance work.

## Compatibility and Verification

Existing scenes default to no regions. Updated editors are required for new data:
older builds ignore unknown JSON fields and may discard them when saving.

`T-REGION-01` checks rotation, containment, tags, disabled state, duplicate IDs,
runtime loading, and shutdown. The terrain editor gate verifies region persistence
through scene reload and Play export.

## Related Documents

- [Heightmap terrain](../terrain/heightmap-terrain.md)
- [Scene format](scene-format-and-runtime.md)
- [Architecture review](../editor/architecture-review.md)