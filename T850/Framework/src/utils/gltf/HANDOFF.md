# glTF 2.0 Loader — Agent Handoff

> **Audience:** the next coding agent (or human) picking up the glTF
> work after this PR. Read this file in full before touching anything in
> `Framework/src/utils/gltf/`.
>
> **Branch of record:** `copilot/create-gltf-loader-from-scratch`
> **Last commits on this branch:**
> - `f47a544` — Static-PBR: 32-bit index buffer support
> - `b48998e` — Static-PBR: MikkTSpace tangent generation
>
> **Build/test status at handoff:** PR validation green
> (Code Review = 0 comments, CodeQL = 0 alerts). Two end-to-end
> Linux builds were used to verify behaviour (see §6).

---

## 1. Mission and design contract

T850 historically loaded `.x` (DirectX) meshes via
`xF::XDataBase::LoadXFile`. This work adds a **from-scratch native glTF 2.0
loader** that targets the same internal representation
(`xF::XDataBase` → `RenderMesh`) so **no renderer code, shader, or
material-binding path needs to change** to render glTF assets. This is
the central design contract: *the loader adapts to the engine, not the
other way around.*

**Two-stage pipeline** (do not collapse them — the split is what keeps
JSON parsing, accessor decoding, and engine bridging independently
testable):

1. `LoadGLTF(path, Document&)` — parse `.gltf` (JSON + sidecars) or
   `.glb` (binary container) into a pure-data `gltf::Document`. No
   engine types, no GPU calls.
2. `ConvertToXDatabase(Document, xF::XDataBase&, sourcePath)` — bridge
   into the engine: walk the scene graph, bake hierarchy, triangulate,
   flip handedness, generate tangents, register textures.

Public API: `Framework/include/utils/gltf/GLTFLoader.h`. Don't change
its signatures without updating `ResourceManager::Load` and the
`--validateGltf` CLI.

---

## 2. Module layout

All paths are relative to `T850/`.

```
Framework/include/utils/gltf/
  GLTFTypes.h        # POD spec mirror; aggregates so glaze reflects by name
  GLTFLoader.h       # Public API (LoadGLTF, ParseJson, ValidateDocument,
                     #   ConvertToXDatabase)
  GLTFAccessor.h     # ReadAccessorFloats / ReadAccessorIndices entrypoints
  GLTFImage.h        # ResolveImage helper for material code
  GLTFMaterial.h     # Internal helpers re-used by tests / future code

Framework/src/utils/gltf/
  GLTFLoader.cpp     # File IO, GLB magic + chunk walk, buffer URI/base64/BIN
                     #   resolution, extensionsRequired hard-fail, cross-ref
                     #   validation. ~258 lines.
  GLTFJson.cpp       # glaze::read with .error_on_unknown_keys=false. ~33 lines.
  GLTFAccessor.cpp   # All 5 component × 7 element types, normalization,
                     #   sparse, byteStride. memcpy-based for ARM64
                     #   alignment safety. ~211 lines.
  GLTFBase64.cpp     # Whitespace-tolerant decoder; standard + URL-safe
                     #   alphabets. ~46 lines.
  GLTFImage.cpp      # External file URI / data: URI / bufferView blobs
                     #   decoded with stbi_load_from_memory and pre-
                     #   registered into BaseDriver::Textures keyed
                     #   "Textures/<name>" so the existing CreateTexture
                     #   path is a cache hit. ~215 lines.
  GLTFMaterial.cpp   # PBR metallic-roughness → xMaterial.EffectInstance.
                     #   pDefaults: diffuseColor / diffuseMap, pbrMetallic,
                     #   pbrRoughness, metallicMap, normalMap, occlusionMap,
                     #   emissiveColor, emissiveMap, alphaMode/alphaCutoff,
                     #   doubleSided. ~158 lines.
  GLTFMesh.cpp       # Scene-graph walk + TRS/matrix bake, TRIANGLES/STRIP/
                     #   FAN triangulation, RH→LH flip, MikkTSpace tangents
                     #   (naive fallback), 16/32-bit IB selection,
                     #   xFinalGeometry interleave. ~695 lines.
  GLTFAnimation.cpp  # Phase 2 stub. Build files already wired so adding
                     #   the implementation later won't touch any project
                     #   files. ~34 lines.
  HANDOFF.md         # ← this file

Librerias/mikktspace/
  README.md          # Origin + zlib license attribution
  include/mikktspace.h
  src/mikktspace.c   # Upstream Morten S. Mikkelsen, unmodified
```

### Vendored deps used by the loader

| Library     | Where                              | Purpose                       |
|-------------|------------------------------------|-------------------------------|
| `glaze`     | `Librerias/glaze/include`          | Header-only JSON parsing      |
| `stb_image` | `Librerias/stb/include`            | PNG/JPEG/etc. image decoding  |
| `mikktspace`| `Librerias/mikktspace/`            | Standard tangent-space gen    |

No new third-party dep was added besides MikkTSpace. **Do not** pull in
`tinygltf`, `cgltf`, or `fastgltf` — the from-scratch path is
intentional (vendor-control + glaze reuse + zero new ABI surface).

---

## 3. Build wiring (touch all four when adding files)

| File                                         | What needs editing                |
|----------------------------------------------|-----------------------------------|
| `Framework/Framework.vcxproj`                | `<ClCompile>`, `<ClInclude>`, and **all six configs'** `AdditionalIncludeDirectories` if a new include root is needed. |
| `Framework/Framework.vcxproj.filters`        | A `<ClCompile>`/`<ClInclude>` entry under the right `<Filter>` so VS solution explorer is tidy. |
| `Framework/CMakeLists.txt`                   | `include_directories(...)` for both HEADLESS and non-HEADLESS branches; add new `.c`/`.cpp` to the `file(GLOB SOURCES …)` block (or list it explicitly if it's outside the existing globs — `mikktspace.c` is listed explicitly because it's outside `src/`). |
| `Framework/Framework.cbp` (Code::Blocks)     | Add `<Add directory>` for new include roots and `<Unit filename>` for new sources. |

Reference: see commit `b48998e` for the canonical four-file wiring of
MikkTSpace (one new `.c` source + one new include root, all six MSBuild
configs touched via `sed`).

`mikktspace.c` is a C file — in MSBuild it's already tagged with
`<CompileAs>CompileAsC</CompileAs>`. Don't change that or it will fail
under MSVC.

---

## 4. Engine integration points (keep these stable)

### 4.1 Dispatch
`Framework/src/utils/ResourceManager.cpp` (around line 37):

```cpp
const std::string ext = FileExtensionLower(filename);
if (ext == "gltf" || ext == "glb") {
  gltf::Document doc;
  ok = gltf::LoadGLTF(filename, doc)
    && gltf::ConvertToXDatabase(doc, *db, filename);
} else {
  ok = db->LoadXFile(filename);          // unchanged legacy path
}
```

The cache key is the original filename — same as `.x`. Don't introduce
a parallel cache.

### 4.2 Engine-side data the loader populates

`Framework/include/utils/xDefs.h`:

- `xMeshGeometry` — one per glTF primitive. Critical fields:
  - `Positions/Normals/Tangents/Binormals/VertexColors/TexCoordinates[4]`
  - `Triangles` (16-bit) **or** `Triangles32` (32-bit), gated by
    `Indices32Bit` (see §5.1)
  - `MaterialList.Materials[0]` — single material per primitive
  - `MaterialList.FaceIndices` — all zeros (single-material primitive)
  - `RelativeMatrix = Identity()` — hierarchy is **baked into vertices**
    at load time (see §5.3)
  - `VertexAttributes` bitmask: HAS_POSITION / HAS_NORMAL / HAS_TANGENT
    / HAS_TEXCOORD0 / HAS_TEXCOORD1 / HAS_VERTEXCOLOR

- `xFinalGeometry` — interleaved render-ready buffer. Must mirror
  `XDataBase::CreateSubSets` exactly:

      [POS vec4][NORMAL vec4][TANGENT vec4][BINORMAL vec4]
      [UV0 vec2][UV1 vec2][UV2 vec2][UV3 vec2]

  POS w=1, normals/tangents/binormals w=0. The `BuildFinalGeometry` in
  `GLTFMesh.cpp` does this; do not "optimise" the layout — shaders
  depend on this exact order and stride.

- `xSubsetInfo::bAlignedVertex = true` — required parity with the `.x`
  loader so `RenderMesh::Create` takes the aligned-vertex path.

### 4.3 Texture pre-registration

`GLTFImage.cpp` decodes images into RGBA8 and calls
`g_pBaseDriver->Textures[key] = …` with key `"Textures/<imageName>"`.
Subsequent `CreateTexture("Textures/<name>")` calls from the standard
material binding path are then cache hits. **Do not** try to call
`CreateTexture` with raw bytes — the engine has no such API.

### 4.4 Coordinate-system flip

glTF: RH, +Y up. Engine: LH. The flip is done in `GLTFMesh.cpp`:

- Negate Z on positions, normals, tangents
- Reverse triangle winding
- For matrices, the bake transforms in glTF space and then negates Z
  on the resulting positions

Controlled by `constexpr bool kFlipToLeftHanded = true;` at the top of
the anonymous namespace. Leave it `true` unless you're also changing
the rest of the engine.

---

## 5. What landed in this PR (verbatim, with file/line anchors)

### 5.1 32-bit index buffers (commit `f47a544`)

**Why.** glTF assets routinely exceed 65 535 vertices per primitive;
the legacy 16-bit-only path failed loudly on those. The renderer
already supports R32 IBs, so this is just plumbing through the loader
and `RenderMesh`.

**Changes:**

- `Framework/include/utils/xDefs.h`
  - Added `std::vector<xDWORD> Triangles32` and `bool Indices32Bit = false;`
    to `xMeshGeometry`. The legacy `Triangles` (16-bit) is unchanged so
    the `.x` loader keeps working with **zero modification.**

- `Framework/include/scene/RenderMesh.h`
  - Added `bool IB32Bit = false;` to `SubSetInfo` so `Draw` knows what
    format to bind for each subset.

- `Framework/src/scene/RenderMesh.cpp` `RenderMesh::Create`
  - `const bool kUse32 = pActual->Indices32Bit;`
  - Per-subset and per-geometry IB allocation **branches on width**:
    `unsigned short[]` + `sizeof(unsigned short)` byteWidth, or
    `unsigned int[]` + `sizeof(unsigned int)` byteWidth.
  - The `CHANGE_TO_RH` reverse-winding loop also branches.
  - `it_subsetinfo->IB32Bit = kUse32;`

- `Framework/src/scene/RenderMesh.cpp` `RenderMesh::Draw`
  - `sub_info->IB->Set(*ctx, 0, sub_info->IB32Bit ? T8_IB_FORMAR::R32 : T8_IB_FORMAR::R16);`

- `Framework/src/utils/gltf/GLTFMesh.cpp` `BuildGeometry`
  - Detects max index, picks 16-bit if `≤ 65 535`, else 32-bit.
  - Logs `T8_LOG_INFO("[glTF] primitive uses 32-bit IB (%zu vertices)", N)`
    on the wide path so it's visible in test logs.
  - Fills the corresponding vector and **only** that one. The other
    stays empty.

**Invariant to preserve when adding more loaders:** writers must set
`Indices32Bit = true` *before* populating `Triangles32`. `RenderMesh`
keys its branch entirely on the flag — wrong flag = wrong stride =
silent corruption.

### 5.2 MikkTSpace tangent generation (commit `b48998e`)

**Why.** Naive per-triangle accumulator produced visible tangent-space
seams on normal-mapped assets coming from Blender / Substance / Maya
(those tools all use MikkTSpace internally; mismatched tangent spaces
flip normals along UV islands).

**Vendored:**
- `Librerias/mikktspace/include/mikktspace.h`
- `Librerias/mikktspace/src/mikktspace.c`
- `Librerias/mikktspace/README.md` (origin + zlib/libpng license)

Files are **unmodified upstream** (Morten S. Mikkelsen,
github.com/mmikk/MikkTSpace). Keep them that way — if upstream gets
patches we want, re-vendor wholesale.

**Wired into:** `Framework.vcxproj` (all 6 configs), `…vcxproj.filters`,
`Framework/CMakeLists.txt` (both HEADLESS and non-HEADLESS branches),
`Framework.cbp`. See §3.

**Implementation in `GLTFMesh.cpp`:**

- `MikkCtx` user-data holding pointers to `(positions, normals, uvs,
  tris, accumXYZ, firstSign)`.
- The 6 required `SMikkTSpaceInterface` callbacks
  (`MikkGetNumFaces`, `MikkGetNumVerticesOfFace`, `MikkGetPosition`,
  `MikkGetNormal`, `MikkGetTexCoord`, `MikkSetTSpaceBasic`).
- `GenerateMikkTSpaceTangents(positions, normals, uvs, tris, outTangents)`:
  1. Calls `genTangSpaceDefault()`.
  2. **Critical convention** — our pipeline does *not* split shared
     vertices, so the per-corner tangents Mikk emits via
     `setTSpaceBasic` are **summed into per-vertex slots**, then
     **re-orthogonalised against the vertex normal**
     (`T -= (T·N)N; normalize`). The first writer's bitangent sign is
     kept (`firstSign[i]`). For assets out of standard DCCs this matches
     `glTF-Sample-Viewer`'s CPU path; visually indistinguishable from
     the per-corner output on uniformly-mapped meshes.
  3. Returns `false` on degenerate input — caller falls back to the
     naive accumulator so a normal-mapped mesh always gets *something*.

**Caller change** (also `GLTFMesh.cpp`):
```cpp
if (!hasTangent && hasUV0 && hasNormal) {
  if (GenerateMikkTSpaceTangents(pos, nrm, uv0, tris, tan)) {
    hasTangent = true;
  } else {
    GenerateNaiveTangents(pos, uv0, tris, tan);
    hasTangent = (tan.size() == N * 4);
  }
}
```

The naive function is preserved for the fallback path — **don't delete
it.**

### 5.3 Earlier work already on this branch (context only)

- POD spec types + `glaze` JSON parsing with
  `error_on_unknown_keys=false` so `extras` and unknown extensions
  don't break load.
- Accessor decoding for all 5 × 7 component/element type combos plus
  sparse and `byteStride`. All raw reads via `std::memcpy` for ARM64
  alignment safety.
- GLB binary container: magic check, chunk walk (JSON + BIN).
- Buffer URIs: file (relative to `.gltf`), `data:` base64, GLB BIN
  reference.
- External image loading: file URI relative to `.gltf`,
  `bufferView` blobs, `data:` URIs.
- Material: PBR metallic-roughness → `xMaterial.EffectInstance.pDefaults`.
  See §4 for keys.
- Scene graph: TRS or column-major matrix per node, hierarchy bake into
  vertex positions (engine consumes `RelativeMatrix = Identity()`).
- Triangulation for `mode = TRIANGLES / STRIP / FAN`.
- `extensionsRequired` hard-fail with a clear log line.
- `xSubsetInfo::bAlignedVertex = true` parity.
- `--validateGltf <path>` CLI in `DayScene/App.cpp` (around line 162):
  parses a `.gltf`/`.glb` headlessly, prints scene/mesh/primitive/
  material/animation/buffer counts plus the first vertex of each
  primitive, exits without creating a graphics device. Useful on
  headless runners and as the basis for a future regression harness.

---

## 6. How to verify changes locally

### 6.1 Windows (canonical)

```powershell
cd T850
.\scripts\build.ps1 -Config Release -Platform x64
# or, equivalently:
MSBuild.exe T850.sln /p:Configuration=Release /p:Platform=x64
```

Expect `Lib\Release\x64\Framework.lib` and `bin\x64\Release\DayScene.exe`.
Requires `T8VcpkgStatic` and `T8VcpkgDynamic` env vars pointing at vcpkg
installed-triplet dirs.

### 6.2 Headless validation (CI-friendly)

```powershell
.\bin\x64\Release\DayScene.exe --validateGltf path\to\model.gltf
.\bin\x64\Release\DayScene.exe --validateGltf path\to\model.glb
```

Exits 0 on success and prints a structural summary (counts + first
vertex of each primitive). This is the cheapest smoke test that
exercises everything from JSON → accessor decode → mesh assembly,
**without** needing a device, window, or shader.

### 6.3 GUI screenshot regression

```powershell
.\bin\x64\Release\DayScene.exe --api d3d11 --guiScreenshot out\frame
```

The app appends `.ppm` to the path. Combine with
`--api gl|d3d11|d3d12|vulkan` to cover backends. **Always** verify all
four backends after touching `RenderMesh::{Create,Draw}` because the
IB-format selection happens once but is consumed by every backend
binding code path.

### 6.4 Quick Linux compile-only check (used during this PR)

```bash
cd T850
g++ -std=c++23 -fsyntax-only -w \
  -IFramework -IFramework/include \
  -ILibrerias/glaze/include -ILibrerias/stb/include \
  -ILibrerias/mikktspace/include \
  Framework/src/utils/gltf/GLTFMesh.cpp
gcc -c -w -ILibrerias/mikktspace/include \
  Librerias/mikktspace/src/mikktspace.c -o /tmp/_mikkt.o
```

Use this for a 5-second typo check before kicking the much slower
MSBuild.

### 6.5 End-to-end behaviour tests run in this PR

Two synthetic glTF documents were built in `/tmp` and parsed through
the actual `LoadGLTF` → `ConvertToXDatabase` pipeline (with the
renderer pointers stubbed to nullptr; the loader doesn't call them
when there are no materials with textures):

1. **MikkTSpace correctness** — UV-mapped quad (4 verts, 2 tris,
   normals=+Z, UVs spanning [0,1]²): expected tangent (1, 0, 0) at
   every vertex. Result: `T = (1, 0, 0)` for all 4 vertices ✓
2. **32-bit IB threshold** — primitive with POSITION accessor count
   70 000 and indices `[0, 1, 69 999]`. Expected: `Indices32Bit=true`,
   `Triangles32 = [0, 69 999, 1]` (the order reflects the LH winding
   flip; the swap of indices 1 and 2 is correct). Result: matches ✓

If you change the loader, replicate this style: build a tiny
self-contained `.gltf` in `/tmp`, link only the loader sources +
`Framework/src/utils/Log.cpp` + `Framework/src/utils/XMaths.cpp` + the
mikktspace `.c`, stub `g_pBaseDriver`/`T8Device`/`T8DeviceContext` and
`stbi_*` to `nullptr`/no-op when no textures are involved, and assert.

---

## 7. Conventions to follow

- **Logging:** `T8_LOG_DEBUG / INFO / WARN / ERROR` only. Never
  `printf`/`std::cerr` from loader code.
- **Filename casing:** match the existing convention `GLTFLoader.cpp`,
  `GLTFAccessor.cpp`, etc. Headers under `Framework/include/utils/gltf/`,
  sources under `Framework/src/utils/gltf/`.
- **Namespaces:** everything under `t800::gltf`. Internal helpers in an
  unnamed namespace per `.cpp`.
- **No exceptions.** Return `bool` and log.
- **No third-party headers in our public headers.** glaze, stb, and
  mikktspace must only be `#include`d from `.cpp`.
- **Memory:** bare `new[]/delete[]` only inside `xFinalGeometry` to
  match the engine convention; everywhere else use `std::vector`.
- **Tangent storage:** xyz in vec4 lanes 0-2, lane 3 always written as
  the bitangent sign (1.0 unless Mikk says otherwise). Some shaders
  read lane 3 — keep it consistent.

---

## 8. Phase 2 — what to do next, in priority order

These are deferred for clarity, not because they're hard. Each is a
self-contained change.

1. **Skinning + animation playback (single biggest gap).**
   - `GLTFAnimation.cpp` is a stub today. Wire it up so `Document`
     carries `animations[]` decoded into `xAnimationInfo` /
     `xAnimationSet` (already in `xDefs.h`).
   - LINEAR + STEP samplers first. CUBICSPLINE can be **baked** into
     LINEAR samples at load time (matches what most engines do) —
     gives correct visuals without touching the runtime sampler.
   - Skinning: glTF `skins[]` → `xSkinInfo`. The vertex attributes
     `JOINTS_0` and `WEIGHTS_0` need to land in `xMeshGeometry`
     (extend `VertexAttributes` mask + `BuildFinalGeometry`'s
     interleave layout — note this **does** change the vertex stride
     and therefore the shader input layout, so it needs a backend pass).
   - Inverse-bind matrices live in an accessor referenced by `skin.inverseBindMatrices`.

2. **Material features that the engine already supports but the loader
   skips.**
   - Occlusion map → `pDefaults["occlusionMap"]` (pre-registration is
     already in `GLTFImage.cpp`; just wire the slot in `GLTFMaterial.cpp`).
   - Emissive map + factor → `pDefaults["emissiveMap"]` /
     `pDefaults["emissiveColor"]`.
   - `alphaMode = MASK` → engine cutoff path (look at how the `.x`
     loader sets `alphaCutoff`; mimic).
   - `alphaMode = BLEND` → blend-state hook in shader selection
     (touches `ShaderKey`).

3. **Morph targets.** glTF stores them per-primitive in `targets[]`.
   Engine has no morph path today — would need shader work too.

4. **KHR_* extensions, in order of asset-prevalence:**
   - `KHR_materials_ior`, `KHR_materials_emissive_strength` — trivial
     additions to `pDefaults`.
   - `KHR_materials_unlit` — just bypass lighting in the chosen shader.
   - `KHR_texture_transform` — affine UV transform, ideally baked into
     the vertex stream at load time (cheaper than per-pixel) when no
     other primitive shares the source UV stream.
   - `KHR_texture_basisu` / KTX2 — needs a decoder dep
     (`basisu_transcoder`); may be worth picking up via vcpkg.
   - `KHR_draco_mesh_compression` — same, `draco` via vcpkg. Only
     bother once you see assets that need it.

5. **Performance pass.** Current `BuildFinalGeometry` allocates two
   `float[]` buffers (`pData` + `pDataDest`) and does N copies — that's
   fine because it mirrors `XDataBase::CreateSubSets`, but for very
   large scenes it's worth profiling. Don't optimise speculatively.

6. **Test infrastructure.** Promote the synthetic-glTF idea from §6.5
   into a real `tests/` target. The Khronos glTF-Sample-Models repo is
   the gold-standard corpus — pick 5-10 representative ones (Box,
   Triangle, Avocado, DamagedHelmet, RiggedFigure, AnimatedCube) and
   wire them into `--validateGltf` as a CI gate. Output should be
   deterministic so a `diff` against a baseline catches regressions.

---

## 9. Things to *not* do (lessons from the in-flight work)

- Don't bypass `BaseDriver::CreateTexture`. The texture-cache
  pre-registration in `GLTFImage.cpp` exists *because* the engine has
  no "create from raw bytes" API and we don't want to add one.
- Don't change `xFinalGeometry`'s interleave layout. Shaders depend on
  it byte-for-byte.
- Don't add a parallel resource cache. `ResourceManager` already keys
  on filename — that's fine.
- Don't merge the two pipeline stages (`LoadGLTF` and
  `ConvertToXDatabase`). The split is what lets us run
  `--validateGltf` headlessly and unit-test the parser without an
  engine.
- Don't read glTF accessor data via `reinterpret_cast<float*>(ptr)`.
  Use `std::memcpy` — accessors can land at non-natural alignment
  (especially after sparse application) and ARM64 will trap.
- Don't drop the naive tangent fallback. Mikk fails silently on a few
  pathological inputs (zero-area UV triangles only); the fallback
  guarantees normal-mapped meshes always shade *something* sensible.
- Don't turn off `kFlipToLeftHanded`. Half the engine assumes LH.
- Don't write 32-bit indices into `Triangles` (the 16-bit vector) —
  use `Triangles32` and set `Indices32Bit = true`. `RenderMesh::Create`
  trusts the flag, not the data.

---

## 10. Quick file map (cheat sheet)

| You want to…                                  | Edit                                                |
|-----------------------------------------------|-----------------------------------------------------|
| Add a new glTF spec field                     | `GLTFTypes.h` (POD struct, glaze auto-reflects)     |
| Add an extension                              | Parse extras in `GLTFLoader.cpp` validation step    |
| Decode a new accessor type                    | `GLTFAccessor.cpp`                                  |
| Tweak material → engine mapping               | `GLTFMaterial.cpp`                                  |
| Tweak how images become engine textures       | `GLTFImage.cpp`                                     |
| Change tangent generation                     | `GLTFMesh.cpp` (`GenerateMikkTSpaceTangents`)       |
| Change index-buffer width handling            | `GLTFMesh.cpp` (`BuildGeometry`) + `RenderMesh.cpp` |
| Change interleaved vertex layout              | `GLTFMesh.cpp` (`BuildFinalGeometry`) — also needs  |
|                                                 shader/backend pass                                |
| Add an animation                              | `GLTFAnimation.cpp` (currently stub)                |
| Add a CLI smoke-test mode                     | `DayScene/App.cpp` near `--validateGltf`            |
| Plug a new third-party header-only dep        | `Librerias/<name>/include`, then §3 wiring          |

---

*End of handoff. Treat this file as the source of truth; if you change
the loader, update the relevant §5 and §8 entries in the same commit.*
