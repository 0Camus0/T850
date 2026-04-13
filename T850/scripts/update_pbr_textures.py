"""
Update SponzaEsc.X materials to reference PBR textures.
1. Copy PBR textures from SponzaPBR_textures/textures_pbr/ to Assets/Textures/
2. Update .X material blocks: replace diffuse/normal/roughness texture names
3. Add missing normalMap entries where PBR normals are available
"""
import os
import re
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PBR_SRC = os.path.join(os.path.dirname(ROOT), "SponzaPBR_textures", "textures_pbr")
TEX_DST = os.path.join(ROOT, "Assets", "Textures")
X_FILE  = os.path.join(ROOT, "Assets", "Models", "SponzaEsc.X")

# --- Mapping from old diffuse texture -> (PBR_diffuse, PBR_normal, PBR_roughness) ---
MATERIAL_MAP = {
    "sponza_column_c_diff.tga":     ("Sponza_Column_c_diffuse.tga",    "Sponza_Column_c_normal.tga",    "Sponza_Column_c_roughness.tga"),
    "sponza_column_a_diff.tga":     ("Sponza_Column_a_diffuse.tga",    "Sponza_Column_a_normal.tga",    "Sponza_Column_a_roughness.tga"),
    "sponza_column_b_diff.tga":     ("Sponza_Column_b_diffuse.tga",    "Sponza_Column_b_normal.tga",    "Sponza_Column_b_roughness.tga"),
    "sponza_details_diff.tga":      ("Sponza_Details_diffuse.tga",     "Sponza_Details_normal.tga",     "Sponza_Details_roughness.tga"),
    "sponza_arch_diff.tga":         ("Sponza_Arch_diffuse.tga",        "Sponza_Arch_normal.tga",        "Sponza_Arch_roughness.tga"),
    "sponza_ceiling_a_diff.tga":    ("Sponza_Ceiling_diffuse.tga",     "Sponza_Ceiling_normal.tga",     "Sponza_Ceiling_roughness.tga"),
    "spnza_bricks_a_diff.tga":      ("Sponza_Bricks_a_Albedo.tga",    "Sponza_Bricks_a_Normal.tga",    "Sponza_Bricks_a_Roughness.tga"),
    "sponza_flagpole_diff.tga":     ("Sponza_FlagPole_diffuse.tga",    "Sponza_FlagPole_normal.tga",    "Sponza_FlagPole_roughness.tga"),
    "sponza_fabric_blue_diff.tga":  ("Sponza_Fabric_Blue_diffuse.tga", "Sponza_Fabric_Blue_normal.tga", "Sponza_Fabric_roughness.tga"),
    "sponza_fabric_green_diff.tga": ("Sponza_Fabric_Green_diffuse.tga","Sponza_Fabric_Green_normal.tga","Sponza_Fabric_roughness.tga"),
    "sponza_fabric_diff.tga":       ("Sponza_Fabric_Red_diffuse.tga",  "Sponza_Fabric_Red_normal.tga",  "Sponza_Fabric_roughness.tga"),
    "sponza_curtain_diff.tga":      ("Sponza_Curtain_Red_diffuse.tga", "Sponza_Curtain_Red_normal.tga", "Sponza_Curtain_roughness.tga"),
    "sponza_curtain_blue_diff.tga": ("Sponza_Curtain_Blue_diffuse.tga","Sponza_Curtain_Blue_normal.tga","Sponza_Curtain_roughness.tga"),
    "sponza_curtain_green_diff.tga":("Sponza_Curtain_Green_diffuse.tga","Sponza_Curtain_Green_normal.tga","Sponza_Curtain_roughness.tga"),
    "vase_dif.tga":                 ("Vase_diffuse.tga",               "Vase_normal.tga",               "Vase_roughness.tga"),
    "lion.tga":                     ("Lion_Albedo.tga",                "Lion_Normal.tga",               "Lion_Roughness.tga"),
    "background.tga":               ("Background_Albedo.tga",          "Background_Normal.tga",         "Background_roughness.tga"),
    "sponza_roof_diff.tga":         ("Sponza_Roof_diffuse.tga",        "Sponza_Roof_normal.tga",        "Sponza_Roof_roughness.tga"),
    "sponza_floor_a_diff.tga":      ("Sponza_Floor_diffuse.tga",       "Sponza_Floor_normal.tga",       "Sponza_Floor_roughness.tga"),
}

# Old normal map names -> PBR normal map names
NORMAL_REMAP = {
    "sponza_column_c_ddn.tga": "Sponza_Column_c_normal.tga",
    "sponza_column_a_ddn.tga": "Sponza_Column_a_normal.tga",
    "sponza_column_b_ddn.tga": "Sponza_Column_b_normal.tga",
    "spnza_bricks_a_ddn.tga":  "Sponza_Bricks_a_Normal.tga",
    "vase_ddn.tga":             "Vase_normal.tga",
    "lion2_ddn.tga":            "Lion_Normal.tga",
    "background_ddn.tga":       "Background_Normal.tga",
}

# Old roughness/gloss names -> PBR roughness names
ROUGHNESS_REMAP = {
    "sponza_column_c_roughness.tga":  "Sponza_Column_c_roughness.tga",
    "sponza_column_a_roughness.tga":  "Sponza_Column_a_roughness.tga",
    "sponza_column_b_roughness.tga":  "Sponza_Column_b_roughness.tga",
    "sponza_details_roughness.tga":   "Sponza_Details_roughness.tga",
    "sponza_ceiling_a_roughness.tga": "Sponza_Ceiling_roughness.tga",
    "spnza_bricks_a_roughness.tga":   "Sponza_Bricks_a_Roughness.tga",
    "sponza_floor_a_roughness.tga":   "Sponza_Floor_roughness.tga",
    "sponza_flagpole_roughness.tga":  "Sponza_FlagPole_roughness.tga",
}

# Old diffuse names used in TextureFilename blocks
DIFFUSE_REMAP = {old: pbr[0] for old, pbr in MATERIAL_MAP.items()}


def copy_pbr_textures():
    """Copy all PBR textures from source to Assets/Textures."""
    if not os.path.isdir(PBR_SRC):
        print(f"ERROR: PBR source not found: {PBR_SRC}")
        return 0
    copied = 0
    for fname in os.listdir(PBR_SRC):
        if fname.lower().endswith(".tga"):
            src = os.path.join(PBR_SRC, fname)
            dst = os.path.join(TEX_DST, fname)
            if not os.path.exists(dst):
                shutil.copy2(src, dst)
                print(f"  COPY {fname}")
                copied += 1
            else:
                # Overwrite with PBR version (better quality)
                src_size = os.path.getsize(src)
                dst_size = os.path.getsize(dst)
                if src_size != dst_size:
                    shutil.copy2(src, dst)
                    print(f"  UPDATE {fname} ({dst_size} -> {src_size} bytes)")
                    copied += 1
                else:
                    print(f"  SKIP {fname} (same size)")
    return copied


def update_x_file():
    """Update the .X file material blocks with PBR texture references."""
    print(f"\nReading {X_FILE}...")
    with open(X_FILE, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    
    total = len(lines)
    print(f"  {total} lines loaded")
    
    # State machine
    current_diffuse = None       # current material's diffuse texture
    current_pbr = None           # (pbr_diffuse, pbr_normal, pbr_rough) for current material
    in_effect_param = False      # inside an EffectParamString/DWord block
    param_name = None            # name of current parameter
    last_param_name = None       # previous parameter name
    has_normal_map_string = False # whether current material has normalMap string
    pending_normal_inject = None # (line_index, normal_texture) to inject
    
    modifications = 0
    injections = 0
    
    # Track material boundaries and gather info
    # We'll do two passes:
    # Pass 1: identify material blocks, their diffuseMap, and whether they have normalMap strings
    # Pass 2: apply modifications
    
    # Actually, let's do a single pass with look-ahead for normalMap injection
    
    i = 0
    output = []
    
    while i < total:
        line = lines[i]
        stripped = line.strip()
        
        # Track when we enter a new material block
        if stripped.startswith("Material "):
            current_diffuse = None
            current_pbr = None
            has_normal_map_string = False
        
        # Detect EffectParamString blocks
        if stripped == "EffectParamString {":
            # Next line is the param name, line after is the value
            if i + 2 < total:
                name_line = lines[i + 1].strip().strip('"').rstrip(';').strip('"')
                val_line = lines[i + 2].strip().strip('"').rstrip(';').strip('"')
                
                if name_line == "diffuseMap":
                    current_diffuse = val_line
                    if val_line in MATERIAL_MAP:
                        current_pbr = MATERIAL_MAP[val_line]
                        new_val = current_pbr[0]
                        if new_val != val_line:
                            # Replace the value line
                            old_indent = lines[i + 2][:len(lines[i + 2]) - len(lines[i + 2].lstrip())]
                            lines[i + 2] = f'{old_indent}"{new_val}";\n'
                            modifications += 1
                            print(f"  L{i+3}: diffuseMap: {val_line} -> {new_val}")
                    else:
                        current_pbr = None
                
                elif name_line == "normalMap":
                    has_normal_map_string = True
                    if val_line in NORMAL_REMAP:
                        new_val = NORMAL_REMAP[val_line]
                        if new_val != val_line:
                            old_indent = lines[i + 2][:len(lines[i + 2]) - len(lines[i + 2].lstrip())]
                            lines[i + 2] = f'{old_indent}"{new_val}";\n'
                            modifications += 1
                            print(f"  L{i+3}: normalMap: {val_line} -> {new_val}")
                    elif current_pbr:
                        # Replace with PBR normal regardless
                        new_val = current_pbr[1]
                        if new_val != val_line:
                            old_indent = lines[i + 2][:len(lines[i + 2]) - len(lines[i + 2].lstrip())]
                            lines[i + 2] = f'{old_indent}"{new_val}";\n'
                            modifications += 1
                            print(f"  L{i+3}: normalMap: {val_line} -> {new_val}")
                
                elif name_line == "glossMap":
                    if val_line in ROUGHNESS_REMAP:
                        new_val = ROUGHNESS_REMAP[val_line]
                        if new_val != val_line:
                            old_indent = lines[i + 2][:len(lines[i + 2]) - len(lines[i + 2].lstrip())]
                            lines[i + 2] = f'{old_indent}"{new_val}";\n'
                            modifications += 1
                            print(f"  L{i+3}: glossMap: {val_line} -> {new_val}")
                    elif current_pbr:
                        new_val = current_pbr[2]
                        if new_val != val_line:
                            old_indent = lines[i + 2][:len(lines[i + 2]) - len(lines[i + 2].lstrip())]
                            lines[i + 2] = f'{old_indent}"{new_val}";\n'
                            modifications += 1
                            print(f"  L{i+3}: glossMap: {val_line} -> {new_val}")
        
        # Detect bUseNormalMap = 0 where we need to set it to 1
        if stripped == '"bUseNormalMap";' and current_pbr:
            # Check next line for the value
            if i + 1 < total:
                val_stripped = lines[i + 1].strip()
                if val_stripped == "0;":
                    # We need to change this to 1 - but only if we'll inject a normalMap
                    # Mark for potential update (we'll confirm when we reach normalMapmapChannel without normalMap)
                    bUseNormalMap_line = i + 1
        
        # Detect the pattern where normalMap string is missing:
        # After "bUseObjectNormals" block, if next is "normalMapmapChannel" (not "normalMap" string),
        # it means normalMap string is absent -> inject it
        if stripped == '"normalMapmapChannel";' and current_pbr and not has_normal_map_string:
            # We need to inject normalMap EffectParamString before this DWord block
            # The EffectParamDWord block starts 2 lines before (at "EffectParamDWord {")
            # We need to find the start of the current EffectParamDWord block
            inject_before = i
            # Walk back to find "EffectParamDWord {" for normalMapmapChannel
            for back in range(i - 1, max(i - 5, 0), -1):
                if lines[back].strip() == "EffectParamDWord {":
                    inject_before = back
                    break
            
            # Get indentation from surrounding lines
            indent = lines[inject_before][:len(lines[inject_before]) - len(lines[inject_before].lstrip())]
            inner_indent = indent + " "
            
            normal_tex = current_pbr[1]
            inject_block = (
                f"\n"
                f"{indent}EffectParamString {{\n"
                f"{inner_indent}\"normalMap\";\n"
                f"{inner_indent}\"{normal_tex}\";\n"
                f"{indent}}}\n"
            )
            
            # Insert the block
            lines.insert(inject_before, inject_block)
            total += 1  # file grew by one "virtual line" (multi-line string)
            i += 1  # skip past inserted content
            injections += 1
            print(f"  L{inject_before+1}: INJECT normalMap: {normal_tex} (for diffuse: {current_diffuse})")
            
            # Also set bUseNormalMap to 1
            # Search backwards for "bUseNormalMap" value line
            for back in range(inject_before - 1, max(inject_before - 20, 0), -1):
                if '"bUseNormalMap";' in lines[back]:
                    # Next line should be the value
                    if back + 1 < total and lines[back + 1].strip() == "0;":
                        old_indent = lines[back + 1][:len(lines[back + 1]) - len(lines[back + 1].lstrip())]
                        lines[back + 1] = f"{old_indent}1;\n"
                        modifications += 1
                        print(f"  L{back+2}: bUseNormalMap: 0 -> 1")
                    break
        
        # Also handle TextureFilename Diffuse blocks
        if stripped.startswith("TextureFilename Diffuse"):
            if i + 1 < total:
                tex_line = lines[i + 1].strip().strip('"').rstrip(';').strip('"')
                if tex_line in DIFFUSE_REMAP:
                    new_val = DIFFUSE_REMAP[tex_line]
                    if new_val != tex_line:
                        old_indent = lines[i + 1][:len(lines[i + 1]) - len(lines[i + 1].lstrip())]
                        lines[i + 1] = f'{old_indent}"{new_val}";\n'
                        modifications += 1
                        print(f"  L{i+2}: TextureFilename: {tex_line} -> {new_val}")
        
        i += 1
    
    print(f"\n  Total modifications: {modifications}")
    print(f"  Total normalMap injections: {injections}")
    
    # Write output
    backup = X_FILE + ".bak"
    if not os.path.exists(backup):
        shutil.copy2(X_FILE, backup)
        print(f"\n  Backup saved to: {backup}")
    
    print(f"  Writing modified file...")
    with open(X_FILE, "w", encoding="utf-8") as f:
        f.writelines(lines)
    print(f"  Done!")


if __name__ == "__main__":
    print("=== PBR Texture Upgrade for SponzaEsc.X ===\n")
    
    print("Step 1: Copying PBR textures to Assets/Textures/")
    n = copy_pbr_textures()
    print(f"  {n} files copied/updated\n")
    
    print("Step 2: Updating .X file material references")
    update_x_file()
    
    print("\n=== Complete ===")
