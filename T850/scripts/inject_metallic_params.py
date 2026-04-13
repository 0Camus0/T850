"""
Inject pbrMetallic, pbrRoughness, and metallicMap params into SponzaEsc.X materials.

For each material block (identified by its EffectInstance), inserts:
  - EffectParamFloats "pbrMetallic"  (0.0 dielectric or 1.0 metal)
  - EffectParamFloats "pbrRoughness" (0.8 fallback)
  - EffectParamString "metallicMap"  (only for materials with per-pixel metallic)

Material mapping (by diffuseMap substring):
  - FlagPole  → pbrMetallic=1.0 (fully metallic, no map)
  - Details   → metallicMap = Sponza_Details_metallic.tga
  - Fabric    → metallicMap = Sponza_Fabric_metallic.tga
  - Curtain   → metallicMap = Sponza_Curtain_metallic.tga
  - All other → pbrMetallic=0.0 (dielectric, no map)
"""

import re
import sys
import os

X_FILE = os.path.join(os.path.dirname(__file__), "..", "Assets", "Models", "SponzaEsc.X")


def make_pbr_floats(name, value):
    """Generate an EffectParamFloats block with a single float."""
    return (
        f'     EffectParamFloats {{\n'
        f'      "{name}";\n'
        f'      1;\n'
        f'      {value:.6f};\n'
        f'     }}\n'
    )


def make_metallic_map(texture):
    """Generate EffectParamString + EffectParamDWord for metallicMap."""
    return (
        f'     EffectParamDWord {{\n'
        f'      "bUseMetallicMap";\n'
        f'      1;\n'
        f'     }}\n'
        f'     EffectParamString {{\n'
        f'      "metallicMap";\n'
        f'      "{texture}";\n'
        f'     }}\n'
    )


# Map: substring in diffuseMap → (metallic_value, metallic_texture or None)
METALLIC_MAP = {
    "FlagPole":  (1.0, None),
    "Details":   (0.0, "Sponza_Details_metallic.tga"),
    "Fabric":    (0.0, "Sponza_Fabric_metallic.tga"),
    "Curtain":   (0.0, "Sponza_Curtain_metallic.tga"),
}


def get_metallic_info(diffuse_name):
    """Return (metallic_value, metallic_texture_or_None) for a given diffuseMap name."""
    for key, (val, tex) in METALLIC_MAP.items():
        if key in diffuse_name:
            return val, tex
    return 0.0, None  # default: dielectric


def process():
    with open(X_FILE, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    total = len(lines)
    print(f"Read {total} lines from {X_FILE}")

    # Strategy: Find each EffectInstance block, extract its diffuseMap,
    # then inject PBR params before the closing "}" of the EffectInstance.
    #
    # We scan for 'EffectInstance Effect' to mark start,
    # find diffuseMap string within,
    # find the matching closing "}" and inject before it.

    # First pass: collect all material info
    materials = []  # list of (effect_start_line, effect_end_line, diffuse_name)

    i = 0
    while i < total:
        line = lines[i]
        if "EffectInstance Effect" in line:
            effect_start = i
            diffuse_name = None
            # Already has PBR params?
            has_pbr_metallic = False

            # Find closing } by tracking brace depth
            depth = 0
            j = i
            while j < total:
                depth += lines[j].count("{") - lines[j].count("}")
                if '"diffuseMap"' in lines[j]:
                    # Next non-empty line has the texture name
                    k = j + 1
                    while k < total:
                        m = re.search(r'"([^"]+)"', lines[k])
                        if m:
                            diffuse_name = m.group(1)
                            break
                        k += 1
                if '"pbrMetallic"' in lines[j]:
                    has_pbr_metallic = True
                if depth == 0:
                    effect_end = j
                    break
                j += 1
            else:
                effect_end = total - 1

            if diffuse_name and not has_pbr_metallic:
                materials.append((effect_start, effect_end, diffuse_name))
            i = effect_end + 1
        else:
            i += 1

    print(f"Found {len(materials)} materials to update")

    # Second pass: inject from bottom up (so line numbers stay valid)
    injected = 0
    for effect_start, effect_end, diffuse_name in reversed(materials):
        metallic_val, metallic_tex = get_metallic_info(diffuse_name)

        block = ""
        block += make_pbr_floats("pbrMetallic", metallic_val)
        block += make_pbr_floats("pbrRoughness", 0.800000)
        if metallic_tex:
            block += make_metallic_map(metallic_tex)

        # Insert before the closing } of the EffectInstance
        inject_lines = block.splitlines(True)
        for idx, il in enumerate(inject_lines):
            lines.insert(effect_end + idx, il)

        injected += 1
        tag = f"metallic={metallic_val}"
        if metallic_tex:
            tag += f", map={metallic_tex}"
        print(f"  [{injected}] {diffuse_name}: {tag}")

    # Write back
    with open(X_FILE, "w", encoding="utf-8", newline="") as f:
        f.writelines(lines)

    print(f"\nDone. Injected PBR params into {injected} materials.")
    print(f"File now has {len(lines)} lines (was {total}).")


if __name__ == "__main__":
    process()
