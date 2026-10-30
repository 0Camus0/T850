"""Parametric Backyard scene generator for T850.

Run headless:
    blender.exe --background --python build_backyard.py

Everything is authored in metres with Z up in Blender (the glTF exporter
converts to Y-up). The pool is emitted as separately named nodes so the runtime
fluid solver can rasterize its walls, floor, steps and coping into a boundary
mask instead of relying on hand-authored numbers.

Outputs (relative to the T850 source root):
    Assets/Models/Backyard/<Asset>.glb
    Assets/Models/Backyard/backyard_layout.json
"""

import json
import math
import os

import bpy

# ---------------------------------------------------------------------------
# Dimensions (metres). Derived from the reference photograph layout.
# ---------------------------------------------------------------------------
POOL = {
    "width": 9.0,
    "length": 4.5,
    "depth": 1.6,
    "wall_thickness": 0.25,
    "coping_width": 0.40,
    "coping_height": 0.08,
    "step_count": 3,
    "step_rise": 0.30,
    "step_run": 0.35,
    # Freeboard: metres of air the fluid solver keeps above the water plane so a
    # free surface has room to rise.
    "freeboard": 0.42,
}
POOL_MIN_X = -4.5
POOL_MIN_Y = -9.0
POOL_MAX_X = POOL_MIN_X + POOL["width"]
POOL_MAX_Y = POOL_MIN_Y + POOL["length"]
WATER_LINE = 0.0
# Engine-space Y of the still-water surface: 4 cm below the pool rim.
WATER_PLANE_Y = -0.04

HOUSE = {
    "width": 18.0,
    "depth": 12.0,
    "eave_height": 3.2,
    "ridge_height": 5.1,
    "wall_thickness": 0.35,
    "min_x": -9.0,
    "min_y": 4.0,
}
HOUSE_MAX_X = HOUSE["min_x"] + HOUSE["width"]
HOUSE_MAX_Y = HOUSE["min_y"] + HOUSE["depth"]

PATIO = {"min_x": -10.0, "max_x": 10.0, "min_y": -4.5, "max_y": 4.0, "height": 0.12}
GROUND = {"size": 44.0, "center": (0.0, -4.0)}
YARD_WALL = {"height": 2.1, "thickness": 0.25, "inset": 14.0, "min_y": -16.0, "max_y": 4.0}

ASSET_DIR = None


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0


def material(name, base_color, metallic=0.0, roughness=0.8, emission=None, emission_strength=0.0):
    mat = bpy.data.materials.get(name)
    if mat:
        return mat
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is None:
        return mat
    bsdf.inputs["Base Color"].default_value = (base_color[0], base_color[1], base_color[2], 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission is not None:
        if "Emission Color" in bsdf.inputs:
            bsdf.inputs["Emission Color"].default_value = (emission[0], emission[1], emission[2], 1.0)
        elif "Emission" in bsdf.inputs:
            bsdf.inputs["Emission"].default_value = (emission[0], emission[1], emission[2], 1.0)
        if "Emission Strength" in bsdf.inputs:
            bsdf.inputs["Emission Strength"].default_value = emission_strength
    return mat


def box(name, size, location, mat, rot=(0.0, 0.0, 0.0)):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location, rotation=rot)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (size[0], size[1], size[2])
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    return obj


def plane(name, size, location, mat):
    bpy.ops.mesh.primitive_plane_add(size=1.0, location=location)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (size[0], size[1], 1.0)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    return obj


def cylinder(name, radius, depth, location, mat, vertices=24, rot=(0.0, 0.0, 0.0)):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices, radius=radius, depth=depth, location=location, rotation=rot
    )
    obj = bpy.context.active_object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def uv_unwrap(obj, angle_limit=1.15):
    for other in bpy.context.selected_objects:
        other.select_set(False)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    try:
        bpy.ops.uv.smart_project(angle_limit=angle_limit, island_margin=0.02)
    except RuntimeError:
        bpy.ops.uv.cube_project(cube_size=1.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.select_set(False)


def bevel(obj, width=0.03, segments=2):
    mod = obj.modifiers.new("Bevel", "BEVEL")
    mod.width = width
    mod.segments = segments
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def gable(name, x_inner, x_outer, y_min, y_max, y_ridge, z_eave, z_ridge, mat, flip=False):
    """Closed triangular prism that caps a gable end.

    A triangle in the Y-Z plane (base on the eave line, apex at the ridge) extruded
    along X by `x_outer - x_inner`. The outer face is flush with the wall's outer
    face, so the roof overhang covers its top edge instead of leaving a ledge.

    Built with from_pydata rather than bpy.ops: a triangle cannot be made from the
    cube primitive, and the winding below is checked to keep the normals outward
    (the engine culls front faces, so inverted normals are visible).
    """
    verts = [
        (x_inner, y_min, z_eave),      # 0
        (x_inner, y_max, z_eave),      # 1
        (x_inner, y_ridge, z_ridge),   # 2
        (x_outer, y_min, z_eave),      # 3
        (x_outer, y_max, z_eave),      # 4
        (x_outer, y_ridge, z_ridge),   # 5
    ]
    faces = [
        (0, 1, 2),        # inner triangle, normal -X
        (5, 4, 3),        # outer triangle, normal +X
        (0, 3, 4, 1),     # bottom, normal -Z
        (1, 4, 5, 2),     # slope toward y_max
        (2, 5, 3, 0),     # slope toward y_min
    ]
    if flip:
        faces = [tuple(reversed(f)) for f in faces]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    return obj


def export_objects(objects, asset_name):
    for obj in bpy.context.scene.objects:
        obj.select_set(False)
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    path = os.path.join(ASSET_DIR, asset_name + ".glb")
    kwargs = dict(
        filepath=path,
        export_format="GLB",
        use_selection=True,
        export_apply=True,
        export_yup=True,
    )
    try:
        bpy.ops.export_scene.gltf(**kwargs)
    except TypeError:
        kwargs.pop("export_yup", None)
        bpy.ops.export_scene.gltf(**kwargs)
    print("exported", path)
    for obj in objects:
        obj.select_set(False)
    return path


# ---------------------------------------------------------------------------
# Pool: named nodes, each with a distinct material
# ---------------------------------------------------------------------------
def build_pool():
    plaster = material("Pool_Plaster", (0.62, 0.78, 0.86), roughness=0.45)
    coping = material("Pool_Coping", (0.66, 0.62, 0.56), roughness=0.65)
    tiles = material("Pool_TileLine", (0.35, 0.55, 0.70), roughness=0.35)

    w = POOL["width"]
    l = POOL["length"]
    t = POOL["wall_thickness"]
    d = POOL["depth"]
    cx = POOL_MIN_X + w * 0.5
    cy = POOL_MIN_Y + l * 0.5

    parts = []

    parts.append(box("Pool_Wall_XMin", (t, l + 2 * t, d), (POOL_MIN_X - t * 0.5, cy, -d * 0.5), plaster))
    parts.append(box("Pool_Wall_XMax", (t, l + 2 * t, d), (POOL_MAX_X + t * 0.5, cy, -d * 0.5), plaster))
    parts.append(box("Pool_Wall_YMin", (w, t, d), (cx, POOL_MIN_Y - t * 0.5, -d * 0.5), plaster))
    parts.append(box("Pool_Wall_YMax", (w, t, d), (cx, POOL_MAX_Y + t * 0.5, -d * 0.5), plaster))
    parts.append(box("Pool_Floor", (w, l, t * 0.6), (cx, cy, -d - t * 0.3), plaster))

    step_run = POOL["step_run"]
    step_rise = POOL["step_rise"]
    for i in range(POOL["step_count"]):
        top = WATER_LINE - step_rise * (i + 1)
        height = max(d + top, 0.02)
        # Deeper steps reach further into the pool, otherwise they nest inside step 1.
        width = step_run * (i + 1)
        parts.append(
            box(
                "Pool_Step_%d" % (i + 1),
                (width, l * 0.55, height),
                (POOL_MAX_X - width * 0.5, POOL_MAX_Y - l * 0.275, top - height * 0.5),
                plaster,
            )
        )

    cw = POOL["coping_width"]
    ch = POOL["coping_height"]
    parts.append(box("Pool_Coping_XMin", (cw, l + cw * 2, ch), (POOL_MIN_X - cw * 0.5, cy, ch * 0.5), coping))
    parts.append(box("Pool_Coping_XMax", (cw, l + cw * 2, ch), (POOL_MAX_X + cw * 0.5, cy, ch * 0.5), coping))
    parts.append(box("Pool_Coping_YMin", (w, cw, ch), (cx, POOL_MIN_Y - cw * 0.5, ch * 0.5), coping))
    parts.append(box("Pool_Coping_YMax", (w, cw, ch), (cx, POOL_MAX_Y + cw * 0.5, ch * 0.5), coping))

    band = 0.16
    band_inset = 0.01
    # Four thin segments hugging the inner walls. A single full-footprint box
    # would cap the pool at the water line and hide the water surface.
    parts.append(box("Pool_TileBand_XMin", (band_inset, l - 0.02, band),
                     (POOL_MIN_X + band_inset * 0.5, cy, -band * 0.5 - 0.02), tiles))
    parts.append(box("Pool_TileBand_XMax", (band_inset, l - 0.02, band),
                     (POOL_MAX_X - band_inset * 0.5, cy, -band * 0.5 - 0.02), tiles))
    parts.append(box("Pool_TileBand_YMin", (w - 0.02, band_inset, band),
                     (cx, POOL_MIN_Y + band_inset * 0.5, -band * 0.5 - 0.02), tiles))
    parts.append(box("Pool_TileBand_YMax", (w - 0.02, band_inset, band),
                     (cx, POOL_MAX_Y - band_inset * 0.5, -band * 0.5 - 0.02), tiles))

    for part in parts:
        uv_unwrap(part)
    return parts


def build_step_bounds():
    """Engine-space AABBs of the pool steps, in the same formulas build_pool uses.

    Blender axes are (x, y_up, z_depth) with the pool spanning y. The engine maps
    Blender (x, y, z) to engine (x, z, y), so an AABB emitted here is already in
    engine space: engine X is Blender x, engine Y is Blender z, engine Z is
    Blender y.
    """
    length = POOL["length"]
    depth = POOL["depth"]
    steps = []
    for i in range(POOL["step_count"]):
        top = WATER_LINE - POOL["step_rise"] * (i + 1)
        height = max(depth + top, 0.02)
        width = POOL["step_run"] * (i + 1)
        center_x = POOL_MAX_X - width * 0.5
        center_y = POOL_MAX_Y - length * 0.275
        center_z = top - height * 0.5
        steps.append({
            "name": "Pool_Step_%d" % (i + 1),
            "minX": round(center_x - width * 0.5, 4),
            "maxX": round(center_x + width * 0.5, 4),
            "minY": round(center_z - height * 0.5, 4),
            "maxY": round(center_z + height * 0.5, 4),
            "minZ": round(center_y - length * 0.275, 4),
            "maxZ": round(center_y + length * 0.275, 4),
        })
    return steps


# ---------------------------------------------------------------------------
# House: stucco walls, standing-seam metal roof, chimney, glazing, pergola
# ---------------------------------------------------------------------------
def build_house():
    stucco = material("House_Stucco", (0.55, 0.44, 0.34), roughness=0.85)
    metal = material("House_RoofMetal", (0.30, 0.33, 0.36), metallic=0.65, roughness=0.35)
    trim = material("House_Trim", (0.20, 0.18, 0.17), roughness=0.6)
    glass = material("House_Glass", (0.85, 0.72, 0.52), roughness=0.08, emission=(1.0, 0.72, 0.38), emission_strength=2.4)
    concrete = material("House_Concrete", (0.70, 0.68, 0.64), roughness=0.8)

    parts = []
    w = HOUSE["width"]
    d = HOUSE["depth"]
    h = HOUSE["eave_height"]
    t = HOUSE["wall_thickness"]
    cx = HOUSE["min_x"] + w * 0.5
    cy = HOUSE["min_y"] + d * 0.5

    parts.append(box("House_Wall_Back", (w, t, h), (cx, HOUSE_MAX_Y - t * 0.5, h * 0.5), stucco))
    parts.append(box("House_Wall_Left", (t, d, h), (HOUSE["min_x"] + t * 0.5, cy, h * 0.5), stucco))
    parts.append(box("House_Wall_Right", (t, d, h), (HOUSE_MAX_X - t * 0.5, cy, h * 0.5), stucco))
    parts.append(box("House_Wall_FrontL", (w * 0.28, t, h), (HOUSE["min_x"] + w * 0.14, HOUSE["min_y"] + t * 0.5, h * 0.5), stucco))
    parts.append(box("House_Wall_FrontR", (w * 0.22, t, h), (HOUSE_MAX_X - w * 0.11, HOUSE["min_y"] + t * 0.5, h * 0.5), stucco))
    parts.append(box("House_Wall_FrontMid", (w * 0.30, t, h * 0.28), (cx - w * 0.02, HOUSE["min_y"] + t * 0.5, h * 0.86), stucco))
    parts.append(box("House_Floor", (w, d, 0.15), (cx, cy, 0.075), concrete))

    ridge = HOUSE["ridge_height"]
    run = d * 0.5 + 0.5
    rise = ridge - h
    angle = math.atan2(rise, run)
    slope_len = math.hypot(run, rise)
    for sign, name in ((1.0, "House_Roof_Front"), (-1.0, "House_Roof_Back")):
        parts.append(
            box(
                name,
                (w + 0.8, slope_len, 0.10),
                (cx, cy + sign * run * 0.5, h + rise * 0.5),
                metal,
                # Rotating about X by +theta raises the +Y end of the slab. The front
                # slab's eave is its +Y end, so it needs the negative angle to drop the
                # eave and lift the ridge side; the back slab is the mirror. Using
                # +sign*angle instead gave a V roof with the ridge cap floating over a
                # valley (W-24).
                rot=(-sign * angle, 0.0, 0.0),
            )
        )
    parts.append(box("House_Ridge", (w + 0.8, 0.30, 0.12), (cx, cy, ridge), metal))

    # The pitched roof exposes the two gable ends: the walls stop at the eave height,
    # so without these the roof floats over an open triangle.
    parts.append(gable("House_Gable_Left", HOUSE["min_x"] + t, HOUSE["min_x"],
                       HOUSE["min_y"], HOUSE_MAX_Y, cy, h, ridge, stucco, flip=True))
    parts.append(gable("House_Gable_Right", HOUSE_MAX_X - t, HOUSE_MAX_X,
                       HOUSE["min_y"], HOUSE_MAX_Y, cy, h, ridge, stucco, flip=False))

    for name in ("House_Roof_Front", "House_Roof_Back"):
        slab = bpy.data.objects[name]
        zs = [(slab.matrix_world @ v.co).z for v in slab.data.vertices]
        print("[house] %s world Z %.3f .. %.3f m (eave %.1f, ridge %.1f)"
              % (name, min(zs), max(zs), h, ridge))

    seam_count = 26
    for i in range(seam_count):
        x = HOUSE["min_x"] - 0.4 + (w + 0.8) * (i + 0.5) / seam_count
        for sign, name in ((1.0, "House_Seam_Front"), (-1.0, "House_Seam_Back")):
            parts.append(
                box(
                    "%s_%02d" % (name, i),
                    (0.04, slope_len * 0.98, 0.04),
                    (x, cy + sign * run * 0.5, h + rise * 0.5 + 0.06),
                    metal,
                    rot=(-sign * angle, 0.0, 0.0),
                )
            )

    parts.append(box("House_Chimney", (1.15, 1.15, 2.4), (cx + 1.6, cy + 1.2, h + rise * 0.55 + 0.6), stucco))
    parts.append(box("House_ChimneyCap", (1.30, 1.30, 0.12), (cx + 1.6, cy + 1.2, h + rise * 0.55 + 1.86), trim))

    door_w = w * 0.36
    door_h = 2.35
    door_x = cx - w * 0.02
    parts.append(box("House_GlassDoor", (door_w, 0.08, door_h), (door_x, HOUSE["min_y"] + 0.09, door_h * 0.5 + 0.08), glass))
    for i, label in enumerate(("A", "B", "C", "D")):
        fx = door_x - door_w * 0.5 + door_w * (i + 0.5) / 4.0
        parts.append(box("House_DoorFrame_" + label, (0.06, 0.14, door_h), (fx, HOUSE["min_y"] + 0.10, door_h * 0.5 + 0.08), trim))
    parts.append(box("House_DoorHead", (door_w + 0.15, 0.16, 0.10), (door_x, HOUSE["min_y"] + 0.10, door_h + 0.13), trim))

    parts.append(box("House_Window", (1.1, 0.08, 1.0), (HOUSE["min_x"] + w * 0.14, HOUSE["min_y"] + 0.09, 1.65), glass))
    parts.append(box("House_WindowFrame", (1.2, 0.12, 1.1), (HOUSE["min_x"] + w * 0.14, HOUSE["min_y"] + 0.07, 1.65), trim))

    post_mat = material("House_Pergola", (0.72, 0.70, 0.66), roughness=0.55)
    for i, x in enumerate((-5.5, -2.2, 1.1, 4.4)):
        parts.append(box("House_PergolaPost_%d" % i, (0.14, 0.14, 3.0), (x, -3.6, 1.5), post_mat))
    parts.append(box("House_PergolaBeam", (10.4, 0.16, 0.20), (-0.55, -3.6, 3.02), post_mat))
    parts.append(box("House_PergolaJoist", (0.10, 8.0, 0.14), (-0.55, 0.2, 3.06), post_mat))

    for part in parts:
        uv_unwrap(part)
    return parts


# ---------------------------------------------------------------------------
# Ground, patio, perimeter wall, props
# ---------------------------------------------------------------------------
def build_water_surface():
    """Explicit grid at the water line with UV 0..1 across the pool footprint.

    The runtime displaces it with the solver's height texture, so vertices must
    line up with the simulation grid and UVs must be exact (no smart-project).
    """
    water = material("Water_Surface", (0.10, 0.34, 0.42), roughness=0.03)
    # Transparent so the renderer draws the surface in the forward pass, where
    # refraction is available and the displacement permutation resolves.
    water.blend_method = "BLEND"
    bsdf = water.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        if "Alpha" in bsdf.inputs:
            bsdf.inputs["Alpha"].default_value = 0.72
    width = POOL["width"]
    length = POOL["length"]
    # Vertex density, deliberately well above the solver grid (100 x 50). The
    # surface texture is a storage texture and cannot be sampled with hardware
    # bilinear filtering, so every displaced vertex lands exactly on a texel and
    # the water edge reads as 0.09 m blocks. Extra vertices cost nothing at runtime
    # and turn those blocks into a smooth ramp.
    nx = 288
    nz = 144
    z_level = WATER_LINE - 0.04

    verts = []
    for j in range(nz + 1):
        for i in range(nx + 1):
            verts.append((POOL_MIN_X + (i / nx) * width,
                          POOL_MIN_Y + (j / nz) * length,
                          z_level))
    faces = []
    for j in range(nz):
        for i in range(nx):
            a = j * (nx + 1) + i
            b = a + 1
            c = a + nx + 1
            d = c + 1
            faces.append((a, b, d, c))

    mesh = bpy.data.meshes.new("Water_Surface")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for poly in mesh.polygons:
        for loop_index in poly.loop_indices:
            vertex = mesh.loops[loop_index].vertex_index
            i = vertex % (nx + 1)
            j = vertex // (nx + 1)
            uv_layer.data[loop_index].uv = (i / nx, j / nz)

    obj = bpy.data.objects.new("Water_Surface", mesh)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(water)
    return [obj]


def build_ground():
    gravel = material("Ground_Gravel", (0.52, 0.48, 0.42), roughness=0.95)
    concrete = material("Ground_Patio", (0.68, 0.66, 0.62), roughness=0.8)

    half = GROUND["size"] * 0.5
    cx, cy = GROUND["center"]
    min_x = cx - half
    max_x = cx + half
    min_y = cy - half
    max_y = cy + half

    parts = []
    # The gravel is split into four strips so the pool opening stays hollow;
    # a single plane would hide the basin.
    strips = (
        ("Ground_Gravel_Front", (max_x - min_x, POOL_MIN_Y - min_y), (cx, (min_y + POOL_MIN_Y) * 0.5)),
        ("Ground_Gravel_Back", (max_x - min_x, max_y - POOL_MAX_Y), (cx, (POOL_MAX_Y + max_y) * 0.5)),
        ("Ground_Gravel_Left", (POOL_MIN_X - min_x, POOL_MAX_Y - POOL_MIN_Y), ((min_x + POOL_MIN_X) * 0.5, (POOL_MIN_Y + POOL_MAX_Y) * 0.5)),
        ("Ground_Gravel_Right", (max_x - POOL_MAX_X, POOL_MAX_Y - POOL_MIN_Y), ((POOL_MAX_X + max_x) * 0.5, (POOL_MIN_Y + POOL_MAX_Y) * 0.5)),
    )
    for name, size, location in strips:
        parts.append(plane(name, size, (location[0], location[1], 0.0), gravel))

    parts.append(
        box(
            "Ground_Patio",
            (PATIO["max_x"] - PATIO["min_x"], PATIO["max_y"] - PATIO["min_y"], PATIO["height"]),
            ((PATIO["min_x"] + PATIO["max_x"]) * 0.5, (PATIO["min_y"] + PATIO["max_y"]) * 0.5, PATIO["height"] * 0.5),
            concrete,
        )
    )
    for part in parts:
        uv_unwrap(part)
    return parts


def build_yard_wall():
    adobe = material("Wall_Adobe", (0.60, 0.47, 0.34), roughness=0.9)
    parts = []
    h = YARD_WALL["height"]
    t = YARD_WALL["thickness"]
    inset = YARD_WALL["inset"]
    min_y = YARD_WALL["min_y"]
    max_y = YARD_WALL["max_y"]
    span = max_y - min_y
    parts.append(box("Wall_Left", (t, span, h), (-inset, (min_y + max_y) * 0.5, h * 0.5), adobe))
    parts.append(box("Wall_Right", (t, span, h), (inset, (min_y + max_y) * 0.5, h * 0.5), adobe))
    parts.append(box("Wall_Back", (inset * 2.0, t, h), (0.0, min_y, h * 0.5), adobe))
    parts.append(box("Wall_Cap", (inset * 2.0 + t, 0.12, 0.10), (0.0, min_y, h + 0.05), adobe))
    for part in parts:
        uv_unwrap(part)
    return parts


def build_props():
    steel = material("Prop_Steel", (0.42, 0.44, 0.47), metallic=0.8, roughness=0.35)
    dark = material("Prop_Dark", (0.10, 0.10, 0.11), roughness=0.55)
    canvas = material("Prop_Canvas", (0.16, 0.32, 0.62), roughness=0.85)
    lamp = material("Prop_LampGlass", (1.0, 0.86, 0.62), roughness=0.15, emission=(1.0, 0.78, 0.45), emission_strength=4.0)
    leaf = material("Prop_Foliage", (0.16, 0.28, 0.14), roughness=0.9)

    parts = []
    parts.append(box("Prop_BBQ_Body", (1.1, 0.62, 0.75), (8.0, 1.2, 0.98), steel))
    parts.append(box("Prop_BBQ_Lid", (1.1, 0.62, 0.28), (8.0, 1.2, 1.5), dark))
    for i, dx in enumerate((-0.38, 0.38)):
        parts.append(box("Prop_BBQ_Leg_%d" % i, (0.07, 0.07, 0.62), (8.0 + dx, 0.98, 0.31), dark))

    parts.append(cylinder("Prop_CoverRoll", 0.22, 3.4, (6.4, -1.6, 0.26), canvas, vertices=20, rot=(0.0, math.radians(90.0), 0.0)))

    for i, (x, y) in enumerate(((-8.4, -15.6), (8.4, -15.6), (-13.6, -2.0), (13.6, -2.0))):
        parts.append(box("Prop_LanternArm_%d" % i, (0.06, 0.32, 0.06), (x, y + 0.16, 1.95), dark))
        parts.append(box("Prop_LanternGlass_%d" % i, (0.20, 0.20, 0.30), (x, y + 0.32, 1.78), lamp))
        parts.append(box("Prop_LanternCap_%d" % i, (0.26, 0.26, 0.06), (x, y + 0.32, 1.95), dark))

    for i, (x, y, r, hgt) in enumerate(((-11.5, -6.5, 1.5, 1.1), (-9.0, -11.5, 1.1, 0.9), (11.0, -12.0, 0.9, 0.8), (12.2, -5.0, 0.8, 2.4))):
        parts.append(cylinder("Prop_Shrub_%d" % i, r, hgt, (x, y, hgt * 0.5), leaf, vertices=18))

    for part in parts:
        uv_unwrap(part)
    return parts


# ---------------------------------------------------------------------------
# Layout metadata consumed by the runtime scene and the fluid boundary builder
# ---------------------------------------------------------------------------
def write_layout(pool_path, asset_paths):
    layout = {
        "units": "meters",
        "waterLine": WATER_LINE,
        # Absolute world Y of the still-water surface: the plane the water mesh is
        # authored at. The fluid solver derives its fill level from this, rather
        # than from a literal, so moving the pool in Blender moves the simulation.
        "waterPlaneY": WATER_PLANE_Y,
        "pool": {
            "minX": POOL_MIN_X,
            "minY": POOL_MIN_Y,
            "maxX": POOL_MAX_X,
            "maxY": POOL_MAX_Y,
            "width": POOL["width"],
            "length": POOL["length"],
            "depth": POOL["depth"],
            # Metres of air the solver volume keeps above the water plane. A free
            # surface cannot rise if the top of the domain is already full.
            "freeboard": POOL["freeboard"],
            "wallThickness": POOL["wall_thickness"],
            "copingWidth": POOL["coping_width"],
            "copingHeight": POOL["coping_height"],
            "stepCount": POOL["step_count"],
            "stepRise": POOL["step_rise"],
            "stepRun": POOL["step_run"],
            "mesh": os.path.basename(pool_path),
            # Exact engine-space bounds of every solid intrusion into the basin,
            # emitted from the same formulas that build the meshes above. The
            # fluid solver derives its wall mask from these, so a geometry change
            # cannot silently desync from the simulation.
            "steps": build_step_bounds(),
            "boundaryNodes": [
                "Pool_Wall_XMin",
                "Pool_Wall_XMax",
                "Pool_Wall_YMin",
                "Pool_Wall_YMax",
                "Pool_Floor",
                "Pool_Step_1",
                "Pool_Step_2",
                "Pool_Step_3",
            ],
            "surfaceNodes": [
                "Pool_Coping_XMin",
                "Pool_Coping_XMax",
                "Pool_Coping_YMin",
                "Pool_Coping_YMax",
            ],
        },
        "house": {
            "minX": HOUSE["min_x"],
            "minY": HOUSE["min_y"],
            "maxX": HOUSE_MAX_X,
            "maxY": HOUSE_MAX_Y,
            "eaveHeight": HOUSE["eave_height"],
            "ridgeHeight": HOUSE["ridge_height"],
        },
        "patio": PATIO,
        "ground": {"size": GROUND["size"], "centerX": GROUND["center"][0], "centerY": GROUND["center"][1]},
        "yardWall": YARD_WALL,
        "assets": {key: os.path.basename(value) for key, value in asset_paths.items()},
    }
    path = os.path.join(ASSET_DIR, "backyard_layout.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(layout, handle, indent=2)
    print("exported", path)


def main():
    global ASSET_DIR
    script_dir = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.getcwd()
    source_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    ASSET_DIR = os.path.join(source_root, "Assets", "Models", "Backyard")
    os.makedirs(ASSET_DIR, exist_ok=True)

    reset_scene()

    pool_parts = build_pool()
    pool_path = export_objects(pool_parts, "Backyard_Pool")

    house_parts = build_house()
    house_path = export_objects(house_parts, "Backyard_House")

    ground_parts = build_ground()
    ground_path = export_objects(ground_parts, "Backyard_Ground")

    wall_parts = build_yard_wall()
    wall_path = export_objects(wall_parts, "Backyard_Wall")

    prop_parts = build_props()
    prop_path = export_objects(prop_parts, "Backyard_Props")

    water_parts = build_water_surface()
    water_path = export_objects(water_parts, "Backyard_Water")

    write_layout(
        pool_path,
        {
            "pool": pool_path,
            "house": house_path,
            "ground": ground_path,
            "wall": wall_path,
            "props": prop_path,
            "water": water_path,
        },
    )
    print("Backyard asset generation complete")


if __name__ == "__main__":
    main()
