# Terrain Placement Grid

Status: implemented and locally tested on 2026-09-07.

## Purpose and Ownership

Framework supplies a square-cell measurement and occupancy layer over authored
heightmap terrain. Editor panels provide settings, a projected grid, candidate
feedback, and placement/removal commands. Terrain generation creates static colored
boxes from the records, so no external building models or map-specific C++ are needed.
An optional `visual` replaces a box's drawn surface with a static or skinned model;
the box remains the occupancy/collision/navigation proxy. Model loading, fitting,
part visibility and animation state belong to Framework, not the editor panel.

This is an authoring/blockout feature, not an RTS construction/economy system.
See [the tutorials](../tutorials/README.md) for step-by-step usage.

## Schema

Both fields belong to `objects[].heightmap`:

```json
{
  "placement_grid": {
    "enabled": true,
    "cell_size": 2,
    "flat_buildings_only": true,
    "max_height_difference": 0.02,
    "max_slope_degrees": 1
  },
  "placements": [{
    "id": "base-building",
    "name": "Base",
    "kind": "building",
    "cell_x": 4, "cell_z": 4,
    "width": 2, "depth": 2,
    "height": 3,
    "color": {"x": 0.2, "y": 0.6, "z": 0.9}
  }]
}
```

Grid dimensions are `floor(size_x / cell_size)` by `floor(size_z / cell_size)`.
The origin is terrain-local `(0,0)` in XZ. Footprints use a minimum-cell anchor and
occupy half-open rectangles `[cell_x, cell_x + width)` and `[cell_z, cell_z + depth)`.
Touching edges do not overlap. Width times depth is the occupied-cell count.

The cell size is independent of terrain samples, render LOD, and Recast cell size.
Grid terrain with placements must be unrotated at unit scale. Translation is
supported. Runtime creation rejects unsupported transforms; editor transform sync
rejects rotation/scaling of an occupied grid. Use terrain dimensions for size edits.

Defaults leave the grid disabled for generic/legacy terrain. Enabling it gives
flat-only buildings by default. The tutorial example enables it explicitly.
Disabled grids cannot contain placements. IDs are stable and unique per terrain.
Cloning terrain copies its local records but owns independent mutable geometry.

## Model Visuals

Each placement may contain:

```json
"visual": {
  "mesh": "Models/Building.glb",
  "hidden_geometry": [],
  "yaw_degrees": 0,
  "animation": "",
  "animate": true,
  "loop": true,
  "animation_speed": 1
}
```

Omitting `visual`, or using an empty mesh path, draws the original colored box.
`hidden_geometry` contains zero-based imported geometry indices; these are explicit
per-renderer settings, never changes to cached source geometry/materials. Reimporting
an asset with different part ordering requires reviewing these indices.

`FitPlacementVisual` rotates the visible reference AABB, then uniformly scales it by
`min(footprint_width / model_width, height / model_height, footprint_depth / model_depth)`.
It centers XZ and aligns minimum Y to the full-resolution supporting terrain height.
Reference bounds use the selected clip at time zero or bind pose; the transform stays
fixed as the animation runs. Animated excursions do not change physics or cells.

Each placement has its own `RenderMesh`/`RenderSkinnedMesh` and controller. Immutable
mesh/material resources remain cached. Terrain revisions reuse unchanged visuals;
changing Animate, Loop or Speed retains current time. Changing a clip/part/model
reinitializes the visual and fit. Clearing a model leaves occupancy intact.
Playback time is not persisted. Empty clip means bind pose. Unknown clips, missing
files, invalid part indices, entirely hidden models and unusable bounds fail the
transaction without replacing previous geometry. Playback speed is finite, 0..10.

`HeightmapMesh::UpdatePlacementAnimations(deltaSeconds)` is called once during host
update. `UploadPlacementBones()` runs before render passes, never from multi-pass
Draw. Replaced renderers use a GPU drain before releasing bone textures; geometry-only
terrain updates retain mutable-buffer retirement. Call revisions outside recording.
Editor/runtime both use these APIs. Source GLBs remain unchanged and are not embedded
inside the scene file. External absolute paths require relocation for packaging.

Use [Assign building models](../tutorials/05-assign-building-models.md) for the illustrated
workflow. Part/clip selection is not gameplay animation events or a prefab catalogue.

## Placement Rules

`TerrainPlacement.h/.cpp` owns the rules:

1. Require enabled, finite valid grid settings, with 1..1024 cells per axis.
2. Require positive integer footprint dimensions inside the grid.
3. Require kind `building` or `unit`, finite positive height, and RGB in `[0,1]`.
4. Reject overlap with any existing record, irrespective of color or kind.
5. Check full-resolution terrain elevations in all overlapping source quads.
6. For buildings with flat-only enabled, reject height range above the authored
   tolerance or either triangle's slope above the authored slope limit.

Flatness uses complete overlapping terrain quads, so boundary checks are
conservative. It catches interior bumps that a center/corner-only test would miss.
The default tolerances are 0.02 local height units and one degree. A non-flat
policy can be explicitly authored, but placement never modifies terrain itself.
The base rests at the highest supporting elevation. No foundation or terrain
leveling is synthesized.

Both static building and unit-marker blockouts reserve cells. Unit markers bypass
the building-flatness rule only. They are scale references, not moving agents.
There is a safety limit of 1024 blockouts per terrain; this is not a performance
claim. Arbitrary footprint masks, inter-terrain occupancy, imported-prop overlap,
region rules, and gameplay-unit collision checks are not part of this query.

## APIs

Source: [TerrainPlacement.h](../../T850/Framework/include/terrain/TerrainPlacement.h)
and [TerrainPlacement.cpp](../../T850/Framework/src/terrain/TerrainPlacement.cpp).

- `InitializeTerrainEditing`: prepare full-resolution local elevations once.
- `TerrainGridDimensions`: validate/compute cell counts.
- `TerrainSurfaceHeight`: sample the actual triangulated ground, not a render LOD.
- `CheckTerrainPlacement`: return allowed/reason/base height; no mutation.
- `AddTerrainPlacement`: validate and append a new stable-ID record.
- `RemoveTerrainPlacement`: release a record and its occupancy by ID.
- `ValidateTerrainPlacements`: validate existing records, including after terrain edits.
- `AppendTerrainBlockouts`: generate normals/triangles/materials from valid records.
- `FitPlacementVisual`: compute an aspect-preserving visual transform inside a valid footprint box.

These are usable from runtime game code. Callers prepare a descriptor copy, make
changes, and invoke `CommitTerrainRevision` on the render thread outside draw
recording, with their collision/navigation bindings. Retain the previous descriptor
if commit fails. Query/record APIs do not themselves publish a live world change.

## Rendering, Physics, and Navigation

`HeightmapMesh` retains full-resolution terrain/box CPU geometry and authored
placement data. Drawing uses mutable buffers. Each LOD keeps box geometry and base
elevation at full detail; only the surrounding ground mesh simplifies.

Existing terrain collision bodies are recooked on placement/removal through the
shared transaction. Blockout collision exists only if terrain collision is authored.
The editor invalidates navigation and rebuilds after the accepted deferred change.

Both `BuildGeometryFromNavSources` and `BuildGeometryFromPrimitiveInstances` append
footprint exclusion boxes from HeightmapMesh. They remove ground and roofs inside
the footprint from the Recast build. Existing centroid-based modifier semantics and
agent erosion still apply. The CPU database and source identity include generated
boxes, so runtime cached navigation changes when placement changes.

Standalone `BuildGeometryFromXDataBase` extracts triangles only; callers needing
placement exclusions must use a terrain instance/source or add the modifiers.
`ProjectPoint` is a nearest-polygon query, not an exact occupancy/containment test.

## Editor Lifecycle

Controls live under **Terrain Editor > Placement Grid**. Grid enablement/cell size
cannot change while records exist. Hovering computes a candidate; clicking queues
a descriptor edit for the next safe frame boundary. Numeric placement uses the
same path. Successful placement/removal is captured by whole-scene undo.
Saving, reload, clone, and temporary Play export all retain grid settings and IDs.

Sculpting that invalidates an occupied building pad fails the terrain commit and
keeps the prior surface. Remove or relocate the building before changing its pad.
Placement and sculpt modes are mutually exclusive. Overlays are editor-only and
do not become additional render/physics meshes. Large grids show at most roughly
128 guide intervals per axis while retaining the true measurement for placement.

## Verification and Limits

`T-PLACEMENT-01` covers cell dimensions, overlap/adjacency, bounds, interior slopes,
unit-marker policy, generated box topology, LOD preservation, IDs, and round trips.
`T-PLACEMENT-VISUAL-01` covers uniform rotated fitting, bottom alignment, empty
bounds, playback validation and full visual descriptor round trips. Optional
`model-*` tutorial captures exercise a locally supplied 92-joint/17-clip reference,
independent bones, helper-part visibility, failed transactions, undo/redo and Play.
The reference is not distributed and the fixture does not validate every model.
Inspected D3D11/D3D12/Vulkan captures show fitted textured models; the OpenGL editor
capture remains overexposed, so its lifecycle pass is not a visual pass.
The terrain editor integration gate exercises placement commit/undo/redo/removal,
rejection of invalid foundations/transforms, navigation exclusion, reload, and Play.

See [RtsBlockout.t8scene](../../T850/Assets/Scenes/RtsBlockout.t8scene) for a complete
data-driven example. Builds do not imply a finished RTS: dynamic actors, construction
commands, economy, gameplay animation integration, replication, and large-army performance remain
game-level work. Android/Steam Deck execution is not covered by local Windows gates.

## Related Documents

- [Heightmap terrain](heightmap-terrain.md)
- [Editor overview](../editor/editor-overview.md)
- [Navigation](../navigation/navmesh-detour.md)
- [Grid design lesson](../tutorials/01-grid-and-scale.md)