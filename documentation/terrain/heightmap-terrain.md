# Authored Heightmap Terrain

Status: live editor tools implemented and verified on 2026-09-07.

## Purpose and Ownership

An ordinary scene object can use an image as its geometry source instead of a
model file. Framework owns native 16-bit decoding, sculpt/material brushes, mutable
rendering, render LOD generation, and collision/navigation commit semantics.
T8ditor owns input, panels, picking overlays, and undo history. Edits remain scene
data and load through the same Framework path in the editor and runtime.

| File | Responsibility |
|---|---|
| [HeightmapTerrain.h](../../T850/Framework/include/terrain/HeightmapTerrain.h) | Portable CPU image/generation API |
| [HeightmapTerrain.cpp](../../T850/Framework/src/terrain/HeightmapTerrain.cpp) | Validation, resource reading, bilinear sampling, normals, bounds |
| [HeightmapMesh.cpp](../../T850/Framework/src/terrain/HeightmapMesh.cpp) | Replaceable buffers, LOD, full-resolution CPU geometry, shared edit transaction |
| [MeshDatabaseBuilder.cpp](../../T850/Framework/src/scene/MeshDatabaseBuilder.cpp) | CPU snapshot to renderer/collision/navigation database |
| [PrimitiveManager.cpp](../../T850/Framework/src/scene/PrimitiveManager.cpp) | Shared `CreateSceneObject` factory |
| [EditorSceneFile.h](../../T850/Framework/include/scene/EditorSceneFile.h) | `SceneHeightmapDesc` and optional object `heightmap` field |
| [HeightmapExample.t8scene](../../T850/Assets/Scenes/HeightmapExample.t8scene) | Authored terrain, collision, navigation zones, camera, and light |

## Data Model

```json
{
  "name": "Terrain",
  "heightmap": {
    "image": "Textures/Terrain/HeightmapExample.bmp",
    "size_x": 64,
    "size_z": 64,
    "height_scale": 10,
    "height_offset": -1.5,
    "samples_x": 65,
    "samples_z": 65,
    "uv_scale": 1,
    "base_color": {"x": 0.28, "y": 0.48, "z": 0.21}
  },
  "position": {"x": -32, "y": 0, "z": -32},
  "navigation": {"include": true, "walkable": true, "static_object": true}
}
```

`mesh` must be empty or omitted when `heightmap` is supplied. Ambiguous objects
are rejected by the factory. Existing mesh-only objects need no migration.

Local terrain extends from `(0, height, 0)` to `(size_x, height, size_z)`.
Object position, rotation, and scale are applied normally. The first decoded image
row maps to local Z=0 and columns increase toward +X. Height is
`height_offset + normalized_sample * height_scale`; no sRGB/gamma conversion is
applied to height data. RGB images are converted to grayscale by the bundled decoder.

Image pixels and terrain grid samples are independent: bilinear interpolation
resamples the image to `samples_x` by `samples_z`. Normals are accumulated from the
generated triangles, normalized, and face upward for a flat image. UVs span
`0..uv_scale`. Material color is linear RGB; the current import has one untextured
PBR material by default. Height images are not automatically surface color textures.

Additional authored fields:

| Field | Meaning |
|---|---|
| `elevations` | Optional full-resolution row-major local-Y values. Overrides image decoding and elevation scale/offset after sculpting. |
| `materials` | Up to 16 named layers with linear RGB `color`, `roughness`, `metallic`, and optional resource-relative `texture`. |
| `cell_materials` | Row-major palette indices, one per grid cell. Empty means material zero. |
| `lod_levels` | 1..5 prebuilt render levels; default 1. |
| `lod_distance` | Positive world-distance threshold, doubled for each successive level. |
| `placement_grid` | Optional square-cell measurement/buildability settings, independent of terrain samples. |
| `placements` | Stable-ID static building/unit-marker footprints and colored box descriptors. |

Painted cells use normal PBR material sections and optional base-color textures.
This is discrete material assignment, not blended multi-texture splat mapping.
Deleting a palette entry remaps its cells to material zero and adjusts remaining
indices. Sculpting never overwrites the source image.

An empty image path creates flat terrain. A missing nonempty path fails instead
of silently creating a flat mesh. Embedded elevations let edited terrain reload
without the original import image; painted textures still require packaging.

## Bounds and Failure Behavior

- Source image: 2..4096 pixels per axis; LDR formats accepted by the terrain stb
  decoder, including PNG, BMP, TGA, and JPEG. Prefer lossless PNG/BMP for elevations.
- Generated grid: 2..1025 samples per axis. Invalid dimensions are rejected, not
  silently clamped. This is an allocation safety limit, not a target grid size.
- World sizes and UV scale must be positive, finite, and at most 1,000,000.
  Elevation scale/offset must be finite with absolute values at most 1,000,000.
- Color components and CPU height samples must be finite and in `[0,1]`.
- A generation or decoding failure leaves an existing output snapshot unchanged.
- Resource loading uses `ResourceLocator::ReadBinary`, not desktop-only file IO.

The isolated [terrain decoder](../../T850/Librerias/terrain-stb/README.md) preserves
**native 16-bit PNG** values. Its test distinguishes adjacent samples 32768 and
32769, verifies orientation, and checks both endpoints. Eight-bit images retain
their normalized heights. The renderer's legacy stb copy is unchanged. HDR is
still unsupported.

Elevation arrays must match grid dimensions exactly. Material maps must match
`(samples_x - 1) * (samples_z - 1)` and reference valid palette entries. Invalid
brush, palette, and LOD parameters are rejected before publication.

## Editor Workflow

For a hands-on RTS workflow, see the separate [tutorial series](../tutorials/README.md).
For footprints and flat-only construction, see [placement grids](placement-grid.md).

1. Choose **File > Import Heightmap**.
2. Select an image, or enable **Flat Terrain**, and set dimensions, elevation, grid samples, and color.
3. Import; resource creation occurs at the next frame boundary. Failures are logged
   in the editor console and no scene object is added.
4. Position the terrain using normal object tools. The imported object already
   participates in static, walkable navigation source geometry.
5. Open **View > Terrain Editor**, choose the terrain, and enable **Sculpt / Paint**.
  These controls also appear in the object's Properties inspector.
6. Choose Raise, Lower, Flatten, Smooth, or Paint Material and drag on the terrain.
  Radius uses terrain-local XZ units; strength controls change per second.
  Hardness controls sculpt falloff. Flatten Height is a local-Y value.
  Shift temporarily changes Raise to Lower; Alt retains camera interaction.
7. Add materials, choose colors/textures and PBR settings, select a palette entry,
  then use Paint Material. Keep textures in project assets for portable scenes.
8. Set render detail under **Geometry and LOD**. **Reload Source Image** discards
  sculpted elevations and reloads the image, or flat data if no image is set.
  This operation is undoable.
9. Use existing physics authoring to create static terrain collision. Existing
  collision bodies are replaced on each accepted geometry revision.
10. Create the scene NavMesh and author include/exclude/area-cost boxes. Navigation
   is invalidated during changes and rebuilt when the stroke ends. A failed
   rebuild leaves it dirty; Play's gate retries or blocks stale navigation.
11. Save the `.t8scene`. Edits, material maps, LOD settings, and regions persist in
   the document. Unedited terrain still requires its image; material textures
   always need to be included in the packaged assets.

The descriptor survives save/load, cloning, whole-scene undo restoration, and the
temporary scene exported for Play. Image import is included in the undo stack.
Missing/invalid terrain objects are preserved as unloaded descriptors on editor
scene load so saving does not silently discard their authoring data.

Live strokes commit at most roughly ten times per second to bound rebuild work.
A completed stroke is one undo entry. Smooth reads a stable neighborhood snapshot.
Frozen/invisible objects and hosted Play cannot receive brushes. Clones have
independent mutable geometry, so editing one does not modify another.

## Runtime and Lifetime

`PrimitiveManager::CreateSceneObject` chooses the authored geometry source.
`LoadHeightmapTerrain` produces a validated `MutableMeshSnapshot`.
`HeightmapMesh` retains full-resolution `XDataBase` geometry for physics, navigation,
fitting, and picking. Drawing uses independent `MutableMesh` buffers and existing
PBR shaders. Replaced buffers retire through the driver; strokes do not append to
the immutable mesh pools.

`CommitTerrainRevision` is the shared Framework edit operation. Call it on the
render thread outside draw recording, passing collision bindings and any active
`GameNavigationService`. It prepares collision, drains pending paths, attempts
replacement render/CPU geometry, then replaces body handles and clears navigation.
Failure retires prepared resources and keeps the previous geometry/body handles.
A failed late upload may cancel paths without changing geometry; retry those queries.

The editor prepares picking geometry, calls this API, publishes the descriptor,
marks navigation dirty, and records undo. Runtime callers can reuse the same API,
then rebuild navigation and request new paths. Collision/navmesh remain at full
resolution even when rendering uses a reduced LOD.

The generated geometry identity includes vertices, indices, sections, material
values, and texture paths.
Runtime navigation hashes this identity when no mesh file path exists, so image or
descriptor changes do not reuse a stale heightmap navmesh. Physics receives the
same source identity and triangle geometry through the existing cook path.

Navigation exclusion boxes do not remove terrain triangles from rendering or
collision. Area-cost boxes affect navigation cost, not gameplay ownership or build
placement. [Tagged regions](../scenes/scene-regions.md) are a separate Framework
system with editor authoring and gameplay queries.

## LOD and Performance Limits

Render LOD resamples elevations and material cells while preserving the local XZ
extent. Sculpt mode forces full detail. This is whole-terrain LOD, not chunked
streaming, geomorphing, holes, or seamless neighbor stitching. Transitions can pop
and palette boundaries can simplify. Collision/navigation/picking do not simplify.

Use 129..257 grids as a starting point for live editing and measure larger grids.
Generation, collision cooking, and whole-scene undo have costs proportional to
terrain size. The 1025 cap is a safety limit, not a responsiveness guarantee.

## Run the Example

From the source root, the directory containing the solution:

```powershell
.\scripts\build.ps1 -Config Release -Platform x64 -Action Build
Set-Location .\bin\x64\Release
.\T8ditor.exe --api d3d12 --sceneFile Scenes/HeightmapExample.t8scene
.\DayScene.exe --api d3d11 --scene 4 --sceneFile Scenes/HeightmapExample.t8scene
```

The example includes a small bitmap. Regenerate it from the source root with
[GenerateHeightmapExample.ps1](../../T850/scripts/GenerateHeightmapExample.ps1)
using `-Force` only when intentionally replacing the example image.

The example has 4,225 vertices, 8,192 triangles, static collision, an excluded
center region, a mud-cost region, and authored camera/light data. Automatic jumps
and drops are disabled in this scene's data, not in Framework.

### Play Camera

**Play Scene** opens a separate hosted window using the authored Overview camera.
The example has no player entity: it starts as a free-fly preview, not a grounded
character. Focus the Play window and use WASD to move, Q/E for vertical movement,
Shift for faster movement, and the mouse to look. G shows runtime controls; Escape
or Stop returns to the editor. To start on the ground with collision and gravity,
author a `physics_entities` entry of type `player` and its spawn in the scene.

Fixed on 2026-09-07: the runtime previously applied the Quake FPS controller even
without an authored player. The overview camera, outside the terrain bounds, would
fall away from the map despite a successful scene load. No-player scenes now use
the existing Framework free-fly controller by default. Explicit scene profiles
and authored player settings can still select grounded behavior.

## Verification

`DayScene.exe --game-selftest` includes `T-HEIGHTMAP-01`, `T-HEIGHTMAP-NAV-01`,
`T-TERRAIN-EDIT-01`, `T-TERRAIN-16BIT-01`, `T-REGION-01`,
`T-SCENE-CONVERSIONS-01`, and `T-SCENE-ISOLATION-01`. Tests cover BMP/16-bit PNG decode,
CPU geometry, collision extraction, navmesh construction/pathing, camera projection,
conversion round trips, and transactional scene loading.

Run the editor lifecycle gate from the source root:

```powershell
.\scripts\TestTerrainEditor.ps1 -Config Debug
.\scripts\TestTerrainEditor.ps1 -Config Debug -Apis d3d12 -Width 1024 -Height 768
```

It launches `--terrain-editor-selftest` and exercises real commit methods,
synthetic viewport brush input, collision, undo/redo, textured painting, clone,
save/reload, toolbar-requested hosted Play, LOD selection, and Play shutdown/restoration.
The window must initialize its runtime automatically; the test no longer calls
the runtime loader directly. It also verifies two seconds of idle camera stability
and movement forwarded through the editor's Play input path. It checks
exit status, fresh error logs, completion markers, image dimensions, and nonuniform
pixels, then generates PNGs beside the dumps. Test scenes are temporary; source
examples and Nexus are not overwritten.

The six-cell Windows matrix (Win32/x64/ARM64 Debug/Release) passes, with 51 self-tests
in each Win32/x64 configuration. All four Debug and Release backend editor workflows
and the compact Debug D3D12 viewport passed. At 1024 pixels, existing saved dock
proportions can leave a narrow viewport; resize/collapse side panels as needed.
Android and Steam Deck execution were not run; their source registration is updated.
These are
editor integration tests, not exhaustive OS-level widget clicking or pixel-parity
acceptance. The gate exposed and verified a fix for Vulkan viewport/scissor calls
outside command recording during Play shutdown. Existing backend brightness
differences remain a separate rendering concern.

## Related Documents

- [Architecture review](../editor/architecture-review.md)
- [Editor overview](../editor/editor-overview.md)
- [Scene format](../scenes/scene-format-and-runtime.md)
- [Navigation](../navigation/navmesh-detour.md)
- [Voxel terrain](voxel-terrain.md)
- [Placement grid](placement-grid.md)
- [Editor tutorials](../tutorials/README.md)