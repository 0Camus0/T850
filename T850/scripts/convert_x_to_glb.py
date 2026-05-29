import argparse
import json
import math
import re
import struct
import sys
import tempfile
from pathlib import Path

try:
    import bpy
except ImportError:  # Allows --help outside Blender.
    bpy = None


NUMBER_RE = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")
STRING_RE = re.compile(r'"([^"]*)"')


def parse_numbers(line):
    return [float(match.group(0)) for match in NUMBER_RE.finditer(line)]


def parse_ints(line):
    return [int(value) for value in parse_numbers(line)]


def sanitize_name(name):
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", name.strip())
    return cleaned or "unnamed"


class LineReader:
    def __init__(self, path):
        self.lines = Path(path).read_text(encoding="utf-8", errors="ignore").splitlines()
        self.index = 0

    def next(self):
        if self.index >= len(self.lines):
            return None
        line = self.lines[self.index]
        self.index += 1
        return line

    def peek(self):
        if self.index >= len(self.lines):
            return None
        return self.lines[self.index]


class XMaterial:
    def __init__(self, name):
        self.name = name or "Material"
        self.face_color = [1.0, 1.0, 1.0, 1.0]
        self.effect_floats = {}
        self.effect_dwords = {}
        self.effect_strings = {}
        self.fallback_texture = None

    def float_values(self, name, default):
        return self.effect_floats.get(name, default)

    def float_value(self, name, default):
        values = self.effect_floats.get(name)
        return values[0] if values else default

    def dword_value(self, name, default=0):
        return self.effect_dwords.get(name, default)

    def string_value(self, name):
        return self.effect_strings.get(name)


class XMesh:
    def __init__(self, name):
        self.name = name
        self.positions = []
        self.normals = []
        self.uv0 = []
        self.tangents = []
        self.binormals = []
        self.faces = []
        self.material_indices = []
        self.materials = []


def read_vector_lines(reader, count, components):
    values = []
    while len(values) < count:
        line = reader.next()
        if line is None:
            raise RuntimeError("Unexpected end of file while reading vector data")
        nums = parse_numbers(line)
        if len(nums) >= components:
            values.append(tuple(float(nums[i]) for i in range(components)))
    return values


def read_index_lines(reader, count):
    values = []
    while len(values) < count:
        line = reader.next()
        if line is None:
            raise RuntimeError("Unexpected end of file while reading indices")
        values.extend(parse_ints(line))
    return values[:count]


def skip_simple_block(reader):
    depth = 1
    while depth > 0:
        line = reader.next()
        if line is None:
            return
        depth += line.count("{")
        depth -= line.count("}")


def read_brace_block(reader, first_line):
    lines = [first_line]
    depth = first_line.count("{") - first_line.count("}")
    while depth > 0:
        line = reader.next()
        if line is None:
            raise RuntimeError("Unexpected end of file inside brace block")
        lines.append(line)
        depth += line.count("{")
        depth -= line.count("}")
    return "\n".join(lines)


def parse_material_block(block):
    first = block.splitlines()[0]
    name_match = re.match(r"\s*Material\s+([^\s{]+)", first)
    material = XMaterial(name_match.group(1) if name_match else "Material")

    body_after_open = block[block.find("{") + 1:]
    color_values = parse_numbers(body_after_open[:body_after_open.find("EffectInstance") if "EffectInstance" in body_after_open else 256])
    if len(color_values) >= 4:
        material.face_color = color_values[:4]

    for match in re.finditer(r'EffectParamFloats\s*\{\s*"([^"]+)"\s*;\s*(\d+)\s*;(?P<body>.*?)\}', block, re.S):
        values = parse_numbers(match.group("body"))
        expected = int(match.group(2))
        material.effect_floats[match.group(1)] = [float(v) for v in values[:expected]]

    for match in re.finditer(r'EffectParamDWord\s*\{\s*"([^"]+)"\s*;\s*([-+]?\d+)\s*;\s*\}', block, re.S):
        material.effect_dwords[match.group(1)] = int(match.group(2))

    for match in re.finditer(r'EffectParamString\s*\{\s*"([^"]+)"\s*;\s*"([^"]*)"\s*;\s*\}', block, re.S):
        material.effect_strings[match.group(1)] = match.group(2)

    tex_match = re.search(r'TextureFilename\s+[^\{]*\{\s*"([^"]+)"\s*;\s*\}', block, re.S)
    if tex_match:
        material.fallback_texture = tex_match.group(1)

    return material


def parse_normals(reader, mesh):
    count_line = reader.next()
    count = parse_ints(count_line)[0]
    mesh.normals = read_vector_lines(reader, count, 3)
    skip_simple_block(reader)


def parse_texcoords(reader, mesh):
    count_line = reader.next()
    count = parse_ints(count_line)[0]
    mesh.uv0 = read_vector_lines(reader, count, 2)
    skip_simple_block(reader)


def dword_to_float(value):
    return struct.unpack("<f", struct.pack("<I", value & 0xFFFFFFFF))[0]


def decl_type_float_count(decl_type):
    return {
        0: 1,  # FLOAT1
        1: 2,  # FLOAT2
        2: 3,  # FLOAT3
        3: 4,  # FLOAT4
    }.get(decl_type, 0)


def parse_decl_data(reader, mesh):
    element_count = parse_ints(reader.next())[0]
    elements = []
    for _ in range(element_count):
        values = parse_ints(reader.next())
        if len(values) >= 4:
            elements.append({"type": values[0], "method": values[1], "usage": values[2], "usage_index": values[3]})

    dword_count = parse_ints(reader.next())[0]
    dwords = read_index_lines(reader, dword_count)
    skip_simple_block(reader)

    stride = sum(decl_type_float_count(element["type"]) for element in elements)
    if not stride or not mesh.positions:
        return
    vertex_count = min(len(mesh.positions), len(dwords) // stride)
    cursor = 0
    tangents = []
    binormals = []
    for _ in range(vertex_count):
        tangent = None
        binormal = None
        for element in elements:
            count = decl_type_float_count(element["type"])
            values = [dword_to_float(dwords[cursor + i]) for i in range(count)]
            cursor += count
            if element["usage"] == 6 and count >= 3:
                tangent = tuple(values[:3])
            elif element["usage"] == 7 and count >= 3:
                binormal = tuple(values[:3])
        if tangent is not None:
            tangents.append(tangent)
        if binormal is not None:
            binormals.append(binormal)
    if len(tangents) == vertex_count:
        mesh.tangents = tangents
    if len(binormals) == vertex_count:
        mesh.binormals = binormals


def parse_material_list(reader, mesh):
    material_count = parse_ints(reader.next())[0]
    face_index_count = parse_ints(reader.next())[0]
    mesh.material_indices = read_index_lines(reader, face_index_count)

    materials = []
    while True:
        line = reader.peek()
        if line is None:
            break
        stripped = line.strip()
        if stripped == "}":
            reader.next()
            break
        if stripped.startswith("Material "):
            materials.append(parse_material_block(read_brace_block(reader, reader.next())))
        else:
            reader.next()
    if len(materials) != material_count:
        print(f"warning: {mesh.name}: expected {material_count} materials, parsed {len(materials)}")
    mesh.materials = materials


def parse_mesh(reader, first_line):
    name_match = re.match(r"\s*Mesh\s+([^\s{]+)", first_line)
    mesh = XMesh(name_match.group(1) if name_match else "Mesh")

    vertex_count = parse_ints(reader.next())[0]
    mesh.positions = read_vector_lines(reader, vertex_count, 3)

    face_count = parse_ints(reader.next())[0]
    faces = []
    while len(faces) < face_count:
        nums = parse_ints(reader.next())
        if not nums:
            continue
        sides = nums[0]
        indices = nums[1:1 + sides]
        if sides != 3:
            for i in range(1, len(indices) - 1):
                faces.append((indices[0], indices[i], indices[i + 1]))
        else:
            faces.append(tuple(indices))
    mesh.faces = faces

    while True:
        line = reader.next()
        if line is None:
            break
        stripped = line.strip()
        if stripped == "}":
            break
        if stripped.startswith("MeshNormals"):
            parse_normals(reader, mesh)
        elif stripped.startswith("MeshTextureCoords"):
            parse_texcoords(reader, mesh)
        elif stripped.startswith("DeclData"):
            parse_decl_data(reader, mesh)
        elif stripped.startswith("MeshMaterialList"):
            parse_material_list(reader, mesh)
        elif "{" in stripped:
            skip_simple_block(reader)

    if not mesh.material_indices:
        mesh.material_indices = [0] * len(mesh.faces)
    return mesh


def parse_x_file(path):
    reader = LineReader(path)
    meshes = []
    while True:
        line = reader.next()
        if line is None:
            break
        stripped = line.strip()
        if stripped.startswith("Mesh "):
            mesh = parse_mesh(reader, line)
            meshes.append(mesh)
            print(f"parsed {mesh.name}: {len(mesh.positions)} vertices, {len(mesh.faces)} triangles, {len(mesh.materials)} materials")
    return meshes


class GlbBuilder:
    def __init__(self, asset_root, temp_dir):
        self.asset_root = Path(asset_root)
        self.texture_root = self.asset_root / "Textures"
        self.temp_dir = Path(temp_dir)
        self.json = {
            "asset": {"version": "2.0", "generator": "T850 convert_x_to_glb.py"},
            "scene": 0,
            "scenes": [{"nodes": []}],
            "nodes": [],
            "meshes": [],
            "materials": [],
            "accessors": [],
            "bufferViews": [],
            "buffers": [{"byteLength": 0}],
            "images": [],
            "textures": [],
            "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        }
        self.bin = bytearray()
        self.image_cache = {}
        self.material_cache = {}
        self.extensions_used = set()

    def align(self, multiple=4):
        while len(self.bin) % multiple:
            self.bin.append(0)

    def add_bytes(self, data, target=None):
        self.align(4)
        offset = len(self.bin)
        self.bin.extend(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.json["bufferViews"].append(view)
        return len(self.json["bufferViews"]) - 1

    def add_accessor(self, data, component_type, accessor_type, count, target=None, mins=None, maxs=None):
        view = self.add_bytes(data, target)
        accessor = {"bufferView": view, "componentType": component_type, "count": count, "type": accessor_type}
        if mins is not None:
            accessor["min"] = mins
        if maxs is not None:
            accessor["max"] = maxs
        self.json["accessors"].append(accessor)
        return len(self.json["accessors"]) - 1

    def resolve_texture_path(self, name):
        if not name:
            return None
        clean = Path(name.replace("\\", "/")).name
        candidates = [self.texture_root / clean, self.asset_root / "Models" / clean, self.texture_root / "sky" / clean]
        for candidate in candidates:
            if candidate.exists():
                return candidate
        print(f"warning: texture '{name}' not found")
        return None

    def image_to_png_bytes(self, path, key):
        if path is None:
            return None
        ext = path.suffix.lower()
        if ext in (".png", ".jpg", ".jpeg"):
            return path.read_bytes(), "image/png" if ext == ".png" else "image/jpeg"
        if bpy is None:
            raise RuntimeError("Blender Python is required to convert non-glTF image formats")
        image = bpy.data.images.load(str(path), check_existing=True)
        _ = image.pixels[0]
        out_path = self.temp_dir / f"{sanitize_name(key)}.png"
        image.filepath_raw = str(out_path)
        image.file_format = "PNG"
        image.save()
        return out_path.read_bytes(), "image/png"

    def add_texture_from_file(self, texture_name, role):
        path = self.resolve_texture_path(texture_name)
        if path is None:
            return None
        cache_key = (str(path).lower(), role)
        if cache_key in self.image_cache:
            return self.image_cache[cache_key]
        data, mime_type = self.image_to_png_bytes(path, f"{path.stem}_{role}")
        view = self.add_bytes(data)
        self.json["images"].append({"name": path.stem, "bufferView": view, "mimeType": mime_type})
        image_index = len(self.json["images"]) - 1
        self.json["textures"].append({"sampler": 0, "source": image_index})
        texture_index = len(self.json["textures"]) - 1
        self.image_cache[cache_key] = texture_index
        return texture_index

    def make_metallic_roughness_texture(self, roughness_name, metallic_name, metallic_factor):
        rough_path = self.resolve_texture_path(roughness_name)
        metal_path = self.resolve_texture_path(metallic_name)
        if rough_path is None and metal_path is None:
            return None
        cache_key = (str(rough_path).lower() if rough_path else "", str(metal_path).lower() if metal_path else "", metallic_factor)
        if cache_key in self.image_cache:
            return self.image_cache[cache_key]
        if bpy is None:
            raise RuntimeError("Blender Python is required to generate packed metallic-roughness textures")

        src_path = rough_path or metal_path
        rough_img = bpy.data.images.load(str(rough_path), check_existing=True) if rough_path else None
        metal_img = bpy.data.images.load(str(metal_path), check_existing=True) if metal_path else None
        if rough_img:
            _ = rough_img.pixels[0]
        if metal_img:
            _ = metal_img.pixels[0]
        width, height = (rough_img or metal_img).size
        image = bpy.data.images.new(f"{src_path.stem}_metallicRoughness", width=width, height=height, alpha=True, float_buffer=False)
        rough_pixels = list(rough_img.pixels[:]) if rough_img else None
        metal_pixels = list(metal_img.pixels[:]) if metal_img else None
        out_pixels = [0.0] * (width * height * 4)
        for i in range(width * height):
            rough = rough_pixels[i * 4] if rough_pixels else 1.0
            metal = metal_pixels[i * 4] if metal_pixels else metallic_factor
            out_pixels[i * 4 + 0] = 1.0
            out_pixels[i * 4 + 1] = rough
            out_pixels[i * 4 + 2] = metal
            out_pixels[i * 4 + 3] = 1.0
        image.pixels[:] = out_pixels
        out_path = self.temp_dir / f"{sanitize_name(src_path.stem)}_metallicRoughness.png"
        image.filepath_raw = str(out_path)
        image.file_format = "PNG"
        image.save()
        view = self.add_bytes(out_path.read_bytes())
        self.json["images"].append({"name": out_path.stem, "bufferView": view, "mimeType": "image/png"})
        image_index = len(self.json["images"]) - 1
        self.json["textures"].append({"sampler": 0, "source": image_index})
        texture_index = len(self.json["textures"]) - 1
        self.image_cache[cache_key] = texture_index
        return texture_index

    def add_material(self, material):
        key = json.dumps({
            "name": material.name,
            "floats": material.effect_floats,
            "dwords": material.effect_dwords,
            "strings": material.effect_strings,
            "fallback": material.fallback_texture,
        }, sort_keys=True)
        if key in self.material_cache:
            return self.material_cache[key]

        diffuse = material.float_values("diffuseColor", material.face_color)
        if len(diffuse) < 4:
            diffuse = list(diffuse[:3]) + [1.0]
        metallic = material.float_value("pbrMetallic", 0.0)
        roughness = material.float_value("pbrRoughness", 0.8)
        specular_color = material.float_values("specularColor", [0.04, 0.04, 0.04, 1.0])
        if len(specular_color) < 4:
            specular_color = list(specular_color[:3]) + [1.0]

        gltf_material = {
            "name": material.name,
            "pbrMetallicRoughness": {
                "baseColorFactor": [float(v) for v in diffuse[:4]],
                "metallicFactor": float(metallic),
                "roughnessFactor": float(roughness),
            },
        }

        diffuse_map = material.string_value("diffuseMap") or material.fallback_texture
        diffuse_texture = self.add_texture_from_file(diffuse_map, "baseColor")
        if diffuse_texture is not None:
            gltf_material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": diffuse_texture, "texCoord": 0}

        mr_texture = self.make_metallic_roughness_texture(material.string_value("glossMap"), material.string_value("metallicMap"), float(metallic))
        if mr_texture is not None:
            gltf_material["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {"index": mr_texture, "texCoord": 0}

        normal_texture = self.add_texture_from_file(material.string_value("normalMap"), "normal")
        if normal_texture is not None:
            gltf_material["normalTexture"] = {"index": normal_texture, "texCoord": 0, "scale": material.float_value("normalScale", 1.0)}

        spec_texture = self.add_texture_from_file(material.string_value("specularMap"), "specularColor")
        extensions = {}
        if spec_texture is not None or specular_color[:3] != [0.04, 0.04, 0.04]:
            extensions["KHR_materials_specular"] = {
                "specularFactor": float(specular_color[3]),
                "specularColorFactor": [1.0, 1.0, 1.0],
            }
            if spec_texture is not None:
                extensions["KHR_materials_specular"]["specularColorTexture"] = {"index": spec_texture, "texCoord": 0}
            self.extensions_used.add("KHR_materials_specular")

        if material.dword_value("NoLighting", 0) != 0:
            extensions["KHR_materials_unlit"] = {}
            self.extensions_used.add("KHR_materials_unlit")

        if extensions:
            gltf_material["extensions"] = extensions

        if material.dword_value("AlphaMask", 0) != 0:
            gltf_material["alphaMode"] = "MASK"
        elif material.dword_value("AlphaGlass", 0) != 0 or material.dword_value("bUseAlpha", 0) != 0:
            gltf_material["alphaMode"] = "BLEND"

        self.json["materials"].append(gltf_material)
        index = len(self.json["materials"]) - 1
        self.material_cache[key] = index
        return index

    def add_mesh(self, mesh):
        positions = [(x, y, -z) for x, y, z in mesh.positions]
        normals = [(x, y, -z) for x, y, z in mesh.normals] if len(mesh.normals) == len(mesh.positions) else []
        uvs = mesh.uv0 if len(mesh.uv0) == len(mesh.positions) else []
        tangents = []
        if len(mesh.tangents) == len(mesh.positions) and len(mesh.binormals) == len(mesh.positions) and normals:
            for tangent, binormal, normal in zip(mesh.tangents, mesh.binormals, mesh.normals):
                tx, ty, tz = tangent
                nx, ny, nz = normal
                bx, by, bz = binormal
                # Source glTF is pre-flipped to RH. Engine flips Z and negates tangent.w again.
                rh_tangent = (tx, ty, -tz)
                rh_normal = (nx, ny, -nz)
                cross = (
                    rh_normal[1] * rh_tangent[2] - rh_normal[2] * rh_tangent[1],
                    rh_normal[2] * rh_tangent[0] - rh_normal[0] * rh_tangent[2],
                    rh_normal[0] * rh_tangent[1] - rh_normal[1] * rh_tangent[0],
                )
                rh_binormal = (bx, by, -bz)
                sign = 1.0 if sum(cross[i] * rh_binormal[i] for i in range(3)) >= 0.0 else -1.0
                tangents.append((rh_tangent[0], rh_tangent[1], rh_tangent[2], -sign))

        pos_flat = b"".join(struct.pack("<3f", *value) for value in positions)
        mins = [min(v[i] for v in positions) for i in range(3)]
        maxs = [max(v[i] for v in positions) for i in range(3)]
        pos_accessor = self.add_accessor(pos_flat, 5126, "VEC3", len(positions), 34962, mins, maxs)
        attributes = {"POSITION": pos_accessor}
        if normals:
            attributes["NORMAL"] = self.add_accessor(b"".join(struct.pack("<3f", *value) for value in normals), 5126, "VEC3", len(normals), 34962)
        if uvs:
            attributes["TEXCOORD_0"] = self.add_accessor(b"".join(struct.pack("<2f", *value) for value in uvs), 5126, "VEC2", len(uvs), 34962)
        if tangents:
            attributes["TANGENT"] = self.add_accessor(b"".join(struct.pack("<4f", *value) for value in tangents), 5126, "VEC4", len(tangents), 34962)

        primitives = []
        materials = mesh.materials or [XMaterial("Default")]
        for material_index, material in enumerate(materials):
            indices = []
            for face_index, face in enumerate(mesh.faces):
                source_material = mesh.material_indices[face_index] if face_index < len(mesh.material_indices) else 0
                if source_material == material_index:
                    a, b, c = face
                    indices.extend((a, c, b))
            if not indices:
                continue
            component_type = 5125 if max(indices) > 65535 else 5123
            fmt = "<I" if component_type == 5125 else "<H"
            index_data = b"".join(struct.pack(fmt, value) for value in indices)
            index_accessor = self.add_accessor(index_data, component_type, "SCALAR", len(indices), 34963, [min(indices)], [max(indices)])
            primitives.append({"attributes": attributes, "indices": index_accessor, "material": self.add_material(material), "mode": 4})

        self.json["meshes"].append({"name": mesh.name, "primitives": primitives})
        mesh_index = len(self.json["meshes"]) - 1
        self.json["nodes"].append({"name": mesh.name, "mesh": mesh_index})
        node_index = len(self.json["nodes"]) - 1
        self.json["scenes"][0]["nodes"].append(node_index)

    def write(self, output_path):
        if self.extensions_used:
            self.json["extensionsUsed"] = sorted(self.extensions_used)
        self.align(4)
        self.json["buffers"][0]["byteLength"] = len(self.bin)
        json_bytes = json.dumps(self.json, separators=(",", ":")).encode("utf-8")
        while len(json_bytes) % 4:
            json_bytes += b" "
        bin_bytes = bytes(self.bin)
        total_length = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
        with Path(output_path).open("wb") as out:
            out.write(struct.pack("<4sII", b"glTF", 2, total_length))
            out.write(struct.pack("<I4s", len(json_bytes), b"JSON"))
            out.write(json_bytes)
            out.write(struct.pack("<I4s", len(bin_bytes), b"BIN\0"))
            out.write(bin_bytes)


def convert_one(asset_root, input_path, output_path):
    meshes = parse_x_file(input_path)
    if not meshes:
        raise RuntimeError(f"No Mesh blocks found in {input_path}")
    with tempfile.TemporaryDirectory(prefix="t850_x_to_glb_") as temp:
        builder = GlbBuilder(asset_root, temp)
        for mesh in meshes:
            builder.add_mesh(mesh)
        builder.write(output_path)
    print(f"wrote {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Convert T850 ASCII .X assets to packed GLB files.")
    parser.add_argument("inputs", nargs="+", help="Input .X files")
    parser.add_argument("--asset-root", required=True, help="T850 Assets directory")
    parser.add_argument("--output-dir", help="Output directory; defaults to the input file directory")
    script_args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    args = parser.parse_args(script_args)

    for input_name in args.inputs:
        input_path = Path(input_name).resolve()
        output_dir = Path(args.output_dir).resolve() if args.output_dir else input_path.parent
        output_path = output_dir / f"{input_path.stem}.glb"
        convert_one(Path(args.asset_root).resolve(), input_path, output_path)


if __name__ == "__main__":
    main()