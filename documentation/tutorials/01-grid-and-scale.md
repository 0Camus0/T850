# 1. Choose a Grid and Scale

Status: illustrated from the working editor on 2026-09-07.

## Recommendation

Start with square building footprints and continuous unit movement. Keep the
building grid separate from terrain triangles and navigation resolution. This is
a straightforward starting point for a base-building real-time strategy game.

An **isometric view** is a camera projection. A square viewed from an angle looks
like a diamond; it has not become a hexagon. You can change between an angled
perspective view and an orthographic view without changing the placement grid.
T850 currently implements square, axis-aligned placement, not hex placement.

## Squares Versus Hexes

| Consideration | Square cells | Hex cells |
|---|---|---|
| Rectangular buildings and walls | Simple width/depth footprints | Require irregular cell masks or approximate rectangles |
| Terrain/image addressing | Direct row/column mapping | Coordinate conversion and different tessellation |
| Tile-based movement | Four/eight neighbors; diagonal rules need care | Six equal-distance neighbors |
| Continuous RTS movement | Grid can be independent of movement | Also possible, but hex topology adds little if units are not tile-bound |
| Typical design reason to choose | Base layout, structures, roads, predictable measurements | Tile tactics where six-way adjacency is a game rule |

Choose the grid according to your game rules, not the camera angle. In this
editor, terrain elevation, placement occupancy, and navigation are separate data.
The tutorials use square placement footprints without constraining future moving
units to tile-by-tile movement.

## Step 1: Choose the Terrain Resolution

In **Terrain Editor > Geometry and LOD**, use a 64-by-64 terrain with 65-by-65
samples. The sample count controls elevation detail, not building size.

![Geometry and LOD showing a 64-by-64 terrain and Grid 65 x 65](images/flat-terrain.png)

**Check:** Terrain Width and Terrain Depth are `64`; the grid sample count is `65 x 65`.

## Step 2: Choose the Placement Measurement

For the tutorials, use **one placement cell = 2 world units per side**. A 64-by-64
terrain then contains 32-by-32 placement cells. This is a project scale convention;
the engine does not force a world unit to mean one meter.

![Placement Grid showing Cell Size 2 and a 32-by-32 square grid](images/grid.png)

**Check:** the panel reports `32 x 32 cells | 2.00 world units per cell`.

Keep these three resolutions distinct:

| Setting | Example | Purpose |
|---|---|---|
| Terrain samples | 65 x 65 across 64 x 64 world units | Elevation geometry, one-unit sample spacing |
| Placement cell | 2 world units | Building footprint and occupancy |
| Recast cell size | 0.25 world units | Navigation rasterization accuracy |

Changing camera zoom or render LOD must not change building occupancy. Making
terrain triangles finer must not make the same building occupy more cells.

## Step 3: Measure a Building Footprint

Choose Building and set **Footprint W / D** to `2, 2`.

![Two-by-two building footprint with Occupies 4 cells and green valid preview](images/footprint.png)

**Check:** the panel says **Occupies 4 cells**, not sixteen.

| Footprint width x depth | Occupied cells | Size with a 2-unit cell |
|---|---:|---|
| 1 x 1 | 1 | 2 x 2 world units |
| 2 x 1 | 2 | 4 x 2 world units |
| 3 x 1 | 3 | 6 x 2 world units |
| 2 x 2 | 4 | 4 x 4 world units |
| 5 x 1 | 5 | 10 x 2 world units |
| 3 x 3 | 9 | 6 x 6 world units |

"Four squares" means four occupied cells, not necessarily four cells on each
side. Use width and depth in your unit/building definitions. Odd cell counts are
rectangular strips in this implementation; L-shaped masks are not implemented.

## Step 4: Measure a Unit Marker

Choose **Unit Marker**. Use a `1, 1` footprint and height `1.5` as a scale reference.

![Unit Marker controls showing a one-by-one footprint occupying one cell](images/unit-marker.png)

**Check:** Blockout Type is Unit Marker and the panel says **Occupies 1 cells**.

A one-cell unit marker helps judge clearance. A real moving unit should use a
continuous position, collision radius, and agent clearance consistent with that
scale. It should not reserve every future movement cell as if it were a building.
An agent's diameter, steering space, and navigation clearance matter more than
the rendered model's bounding box alone.

Next: [Create RTS terrain](02-create-rts-terrain.md).