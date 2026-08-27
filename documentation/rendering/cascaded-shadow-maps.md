# Cascaded Shadow Maps Detailed Implementation Specification

**Status:** Implementation specification
**Scope:** Directional CSM design and integration for D3D11, D3D12, OpenGL, and Vulkan
**Shader split:** HLSL for D3D11, D3D12, and Vulkan; dedicated GLSL for OpenGL
**Primary audience:** Implementation agent; this document is intended to remove design guesswork
**Implementation state:** Specification only; the source, shaders, assets, and schemas are not yet changed

## 1. Intent

This document replaces the prior proposal and gap-analysis accumulation with one implementation-ready plan.
It resolves contradictory branches and records one authoritative path.

Primary objective:
- Ship directional CSM first.

Default and range:
- Default cascade count: 4.
- Configurable cascade count: 1 to 6.

## 2. Scope and non-goals

Out of scope for this proposal:

- Omni shadow redesign.
- Dual-paraboloid work.
- Multiple simultaneous CSM directional lights.
- Texture-array based shadow storage.

These items are deferred intentionally.

## 3. Authoritative design decisions

### 3.1 Shadow storage

Use a plain 2D depth atlas with F32 depth.
Do not use texture arrays.
Do not treat atlas tiles as array slices.

ShadowMapResolution means tile resolution.
Atlas dimensions are derived from tile resolution and tile grid.

### 3.2 Atlas layout table

| Cascades | Grid | Tile index range |
|---|---|---|
| 1 | 1x1 | 0 |
| 2 | 2x1 | 0..1 |
| 3 | 2x2 | 0..2 |
| 4 | 2x2 | 0..3 |
| 5 | 3x2 | 0..4 |
| 6 | 3x2 | 0..5 |

Runtime computes per-cascade atlas scale and bias.
Shaders consume uploaded scale and bias.
Do not use bitwise quadrant mapping formulas.

### 3.3 Render graph top-level descriptor

Add a top-level `shadow_projections` descriptor list.
The first implementation supports zero or one directional projection, but the descriptor
shape also represents future cube and dual-paraboloid projections.

Directional descriptor fields:
- `id`
- `light_id`
- `legacy_light_index`
- `technique`
- `target`
- `enabled`
- `resolution`
- `cascade_count`
- `split_lambda`
- `near_distance`
- `far_distance`
- `caster_depth_padding`
- `blend_fraction`

Render target descriptor adds a `shadow_projection` string reference.
`shadow_projection` references the projection used for sizing and validation.
Sizing still produces an ordinary 2D depth target.

### 3.4 Source of truth and override order

Pass topology source of truth is effective render-graph data after profile overrides.
Required order before RT creation:

1. Load base graph data.
2. Apply profile defaults.
3. Apply persisted profile overrides.
4. Resolve effective `shadow_projections` descriptors.
5. Expand generated directional passes.
6. Create RTs and finalized pass instances.

No topology-affecting override is allowed after step 6.

### 3.5 Pass expansion and metadata

RenderGraph expands one directional projection into N internal depth passes before BuildGraph.
RenderPassDesc gains generated metadata:

- pass kind enum
- shadow map index
- shadow view index
- viewport rectangle

BuildGraph consumes only expanded pass list.

### 3.6 Pass-kind based control flow

Replace exact pass-name checks with pass kind checks.
Generated pass names are descriptive only.
Behavioral logic must not depend on exact name text.

### 3.7 Push, pop, and clear policy

For directional groups with N greater than 1:

- Pass 0: push true, pop false.
- Pass 1..N-2: push false, pop false.
- Pass N-1: push false, pop true.

Only one clear is valid for the grouped atlas sequence.
PushRT clears on all backends.
Therefore clear must occur on first generated pass only.

Viewport ordering rule:

1. Bind RT.
2. Set viewport.
3. Draw.

Setting viewport before RT bind is invalid.

## 4. Camera ownership and runtime state

### 4.1 Ownership requirement

Generated cascade cameras are separate runtime-owned state.
Do not repurpose pLightCameras.
Do not mutate ActiveLightCamera to route generated cascade passes.

### 4.2 Required runtime state payload

Directional CSM runtime state must include:

- Effective cascade count.
- Split boundaries storage for up to five boundaries.
- Light VP matrices for up to six cascades.
- Atlas scale and bias for up to six cascades.
- World-space cascade corners as [6][8].

### 4.3 Generated pass camera binding

For each generated directional depth pass:

1. Resolve cascade camera from dedicated runtime state.
2. SetPrimaryCamera before mesh draw.
3. Draw pass geometry.
4. ScopedPrimaryCameraOverride restores prior camera after draw.

This prevents global camera side effects.

## 5. Constant buffer and shader interface plan

### 5.1 Fixed max-six shadow payload

Use fixed max-six cascade payload for shadow composition path.
Do not use cascade-count shader-key bits.
Do not generate cascade-count shader permutations.

Rationale:

- Macro-sized matrix arrays shift later CB field offsets.
- Offset drift increases cross-backend binding fragility.
- Existing GL parser array handling is numeric and atoi based.
- Symbolic bounds are not reliable in that path.

### 5.2 Binding model by API family

HLSL path:
- Add dedicated CascadeCB at b2.
- Use fixed numeric array lengths of six.

GLSL path:
- Use fixed numeric loose-uniform arrays of six.
- Keep dedicated GLSL implementation aligned with HLSL semantics.

GL upload behavior:
- GL uses monolithic loose-uniform upload path.
- Append equivalent cascade fields to RenderQuad CBuffer payload used by GL upload.

Pass scoping:
- HLSL binds and reads CascadeCB only for SHADOW_COMP.
- Non-shadow passes do not consume cascade payload.

### 5.3 CascadeCB fields

CascadeCB contains:

- LightVP[6]
- SplitDepths[2] as float4, enough for at least five boundaries
- AtlasScaleBias[6]
- Params float4 with:
  - x: cascade count
  - y: atlas width
  - z: atlas height
  - w: blend fraction

## 6. Cascade math and fit details

### 6.1 Input clamping

Inputs:

- Main camera near and far.
- Main camera projection mode.
- split_lambda.
- near_distance and far_distance.
- caster_depth_padding.

Clamp effective near and far to main-camera range.
Reject invalid intervals where far is not greater than near.

### 6.2 Split generation

For N cascades, compute N minus 1 boundaries using PSSM log and linear blend:

- t = i / N
- logSplit = near * pow(far / near, t)
- linSplit = near + (far - near) * t
- split = lerp(linSplit, logSplit, lambda)

Store boundaries in SplitDepths.

### 6.3 Frustum corner construction

Build corners from main camera basis vectors.
Support both perspective and orthographic modes.
Do not rely on reversed-NDC assumptions for corner construction.

For each cascade interval:

1. Build near-plane corners in world space.
2. Build far-plane corners in world space.
3. Store eight corners in runtime corners array.

### 6.4 Stable light-space fit

For each cascade:

1. Compute receiver bounds in light space from the 8 corners.
2. Expand depth extent with caster_depth_padding.
3. Enforce positive near and far with valid order.
4. Use square XY extent for stability.
5. Use fallback up vector when directional vector is near parallel to world up.
6. Apply texel snapping to reduce shimmer.

Receiver versus caster coverage policy:

- Receiver bounds define visible shading area.
- Caster depth padding protects shadow contribution from off-screen casters.

### 6.5 Atlas transform data

For each cascade tile:

- Scale equals tile size divided by atlas size.
- Bias equals tile origin divided by atlas size.

Upload these values per cascade.

## 7. Shadow composition behavior

### 7.1 Main-camera depth semantics

SHADOW_COMP explicitly uses main-camera depth semantics.
Populate CameraInfo in SHADOW_COMP setup.
Use existing reversed-depth-aware LinearizeDepth path.

### 7.2 Cascade selection algorithm

Use fixed max loop over boundary slots.
Increment selected cascade for each crossed boundary.
Clamp final index to count minus one.

Out-of-range policy:

- Depth before first boundary selects cascade 0.
- Depth beyond last boundary selects final cascade.

### 7.3 UV transform and validity checks

Sampling flow:

1. Transform world position by selected LightVP.
2. Perspective divide.
3. Convert to local UV.
4. Reject invalid local UV outside tile domain.
5. Apply selected atlas scale and bias.

### 7.4 PCF domain and clamping

PCF texel size is computed from atlas width and height.
Clamp taps to selected tile interior using half-texel margin.
This prevents cross-tile bleeding.

Depth filtering policy:
- Keep depth target nearest-filtered.
- Perform filtering explicitly in shader.

### 7.5 Boundary blending status

Boundary blending is deferred to optional Phase 4.
It requires overlap policy and two shadow evaluations near boundaries.
It is not a trivial scalar interpolation patch.

## 8. Backend integration notes

### 8.1 Capability table

| Topic | D3D11 | D3D12 | Vulkan | OpenGL |
|---|---|---|---|---|
| 2D F32 depth atlas target | Existing path, atlas size input | Existing path, atlas size input | Existing path, atlas size input | Existing path, atlas size input |
| Per-pass viewport | Existing SetViewport path | Existing SetViewport path | Existing viewport path | Existing viewport path |
| Scissor behavior with generated passes | Existing behavior | Validate per-pass scissor update | Validate per-pass scissor update | Existing behavior |
| Shadow composition payload bind | CB bind | Root-bound CB bind | UBO bind | Loose uniform upload |
| Texture-array requirement | None | None | None | None |

No texture-array work is required by this proposal.

### 8.2 Backend checks required

D3D12:
- Validate root reflection after adding CascadeCB b2.
- Verify non-shadow passes remain binding-stable.

Vulkan:
- Validate UBO binding index changes if layout shifts.
- Verify per-pass viewport and scissor updates.

OpenGL:
- Ensure numeric array declarations remain parser-compatible.
- Verify monolithic loose-uniform upload includes all new fields.

## 9. Editor controls and rebuild semantics

### 9.1 Exposed controls

Expose and persist:

- cascade count
- split lambda
- near distance
- far distance
- caster depth padding
- blend fraction
- ShadowMapResolution

### 9.2 Rebuild versus recompute policy

Regenerate generated passes and recreate RT:

- cascade count changes
- ShadowMapResolution changes

Only recompute runtime cascade state:

- split lambda
- near distance
- far distance
- caster depth padding
- blend fraction

### 9.3 Persistence ordering

Persist profile overrides before descriptor expansion.
Build graph and create RT from effective values only.

### 9.4 Legacy behavior guarantees

Existing graphs with no `shadow_projections` keep the legacy single-directional flow.
Generated flow with cascade count 1 is expected equivalent to legacy output.

## 10. Validation and test matrix

### 10.1 Descriptor validation

Validate:

- cascade_count in 1..6
- target reference exists
- near and far interval valid
- viewport bounds valid after expansion

### 10.2 Runtime debug views

Provide debug outputs for:

- selected cascade index
- atlas tile occupancy
- per-cascade frustum wireframe
- split boundary readout

### 10.3 Matrix coverage

Run across APIs and counts 1..6.
Include non-square atlas grids 2x1 and 3x2.

Required scenarios:

- all APIs with each count from 1 through 6
- shadows disabled
- resize while shadows enabled
- API switch with persisted profile
- PCF edge tests near tile borders
- frame replay and capture validation

## 11. Implementation phases

### Phase 0: schema and expansion

Deliver:

- `shadow_projections` parsing and validation
- `shadow_projection` target sizing reference
- pass kind and shadow map metadata
- descriptor expansion before BuildGraph
- grouped push and pop clear semantics

Exit criteria:

- generated passes are correct for counts 1..6
- BuildGraph sees expanded list only

### Phase 1: runtime state and cameras

Deliver:

- dedicated directional cascade runtime state
- split generation and corners [6][8]
- stable light-space fit and texel snapping
- generated-pass camera resolve and scoped restore

Exit criteria:

- atlas depth content valid for all counts
- no mutation of legacy directional camera containers

### Phase 2: shader and payload integration

Deliver:

- CascadeCB binding for HLSL SHADOW_COMP path
- GLSL fixed-size loose-uniform equivalent
- SHADOW_COMP CameraInfo population from main camera
- atlas transform sampling and tile-interior PCF clamp

Exit criteria:

- correct cascade selection and sampling
- no tile bleed artifacts
- count-1 parity with legacy behavior

### Phase 3: backend and editor hardening

Deliver:

- D3D12 root reflection audit
- Vulkan UBO binding audit
- OpenGL upload audit
- editor controls with rebuild and recompute semantics
- complete matrix test run

Exit criteria:

- matrix pass across APIs and counts
- no regression in shadow-off and resize paths

### Phase 4: optional boundary blending

Deliver:

- overlap policy
- two-evaluation blend implementation
- tuning for blend_fraction

Exit criteria:

- smooth transitions with bounded cost

## 12. Resolved and remaining decisions

Resolved:

- Directional CSM first.
- Default 4, configurable 1..6.
- 2D F32 atlas only.
- Top-level `shadow_projections` descriptors drive generated passes.
- Pass-kind based behavior.
- Dedicated cascade runtime camera state.
- Fixed max-six shadow payload.
- No cascade-count permutation path.
- SHADOW_COMP uses main camera depth context.
- Boundary blending deferred to optional Phase 4.

Remaining:

- Final default tuning values per shipped scene profile.
- Exact editor UX for debug overlays.
- Blend cost budget if phase 4 is enabled.

## 13. Deferred omni statement

General omni model changes are deferred.
Current Vulkan cube ChangeCubeDepthTexture TODO remains open.
Current HLSL and GLSL omni paths are asymmetric.

Therefore omni cannot be claimed unchanged and cross-backend-ready by this document.

## 14. Pseudocode appendix

Split boundaries:

```text
for i in 1..N-1:
  t = i / N
  logSplit = near * pow(far / near, t)
  linSplit = near + (far - near) * t
  boundary[i] = lerp(linSplit, logSplit, lambda)
```

Cascade index selection:

```text
idx = 0
for b in 0..4:
  if b >= boundaryCount:
    break
  if viewDepth > boundary[b]:
    idx = idx + 1
idx = clamp(idx, 0, cascadeCount - 1)
```

Atlas mapping:

```text
localUV = clip.xy / clip.w * 0.5 + 0.5
atlasUV = localUV * atlasScale[idx] + atlasBias[idx]
```

## 15. Scalable shadow architecture

This section is normative. It separates concepts that must not be conflated when cube and
dual-paraboloid shadows are added.

### 15.1 Terminology

**Light:** An authored or runtime `Light` that may cast a shadow. A light has a stable ID
and a current runtime index.

**Shadow projection:** The algorithm that maps world space into shadow space. Examples are
one directional orthographic projection, CSM, six point-light cube projections, and two
paraboloid hemispheres.

**Shadow resource:** The GPU depth texture referenced by a render-target descriptor. It is
one resource even when it has multiple faces or atlas tiles.

**Shadow view:** One rendering of scene depth into a region or subresource of the shadow
resource. CSM has N atlas-tile views, a cube has 6 face views, and dual paraboloid has 2
atlas-tile views.

**Shadow sampling mode:** The shader path and sampler dimension used while lighting. A 2D
atlas and cubemap require different shader resource declarations and shader permutations.

Resource count, view count, and light count are independent. A cubemap is one resource,
six views, and normally one light. Do not encode it as six shadow maps.

### 15.2 Technique enum and derived behavior

Add shared enums to a new `Framework/include/scene/ShadowDescriptor.h`. Both
`RenderGraphDescriptor.h` and `SceneDescriptor.h` include this header so render-graph and
profile types do not depend on each other.

```cpp
enum class ShadowTechnique : uint8_t {
  DirectionalSingle,
  DirectionalCascaded,
  PointCube,
  PointDualParaboloid
};
```

| JSON | Enum | Resource | Derived views | Sampling |
|---|---|---|---:|---|
| `directional` | `DirectionalSingle` | 2D `F32` | 1 | `Texture2D` / `sampler2D` |
| `csm` | `DirectionalCascaded` | 2D `F32` atlas | `cascade_count` | `Texture2D` / `sampler2D` |
| `point_cube` | `PointCube` | `CUBE_F32` | 6 | `TextureCube` / `samplerCube` |
| `point_dual_paraboloid` | `PointDualParaboloid` | 2D `F32` atlas | 2 | `Texture2D` / `sampler2D` |

The loader derives view count from technique. Only CSM accepts `cascade_count`. Reject that
field on other techniques rather than silently changing its meaning.

### 15.3 Descriptor structures

Put `ShadowProjectionDesc`, `ShadowProjectionOverrideDesc`, `ShadowTechnique`, and
`ShadowViewKind` in `ShadowDescriptor.h`. Render-graph-only pass metadata remains in
`RenderGraphDescriptor.h`.

```cpp
struct ShadowProjectionDesc {
  std::string id;
  std::string light_id;
  int legacy_light_index = -1;
  ShadowTechnique technique = ShadowTechnique::DirectionalSingle;
  std::string target;
  bool enabled = true;

  // Zero means use SceneProps::ShadowMapResolution.
  int resolution = 0;

  // Directional CSM only.
  int cascade_count = 1;
  float split_lambda = 0.6f;
  float near_distance = 0.1f;
  float far_distance = 250.0f;
  float caster_depth_padding = 50.0f;
  float blend_fraction = 0.0f;

  // Point techniques. A non-positive far distance uses the light radius.
  float point_near_distance = 0.1f;
  float point_far_distance = 0.0f;
};

enum class RenderPassKind : uint8_t { Normal, ShadowDepth };
enum class ShadowViewKind : uint8_t { WholeTexture2D, AtlasTile, CubeFace };

struct RenderPassDesc {
  // Existing fields remain.
  std::string shadow_projection; // source JSON ID; empty for ordinary passes
  RenderPassKind kind = RenderPassKind::Normal;
  int shadow_projection_index = -1;
  int shadow_view_index = -1;
  ShadowViewKind shadow_view_kind = ShadowViewKind::WholeTexture2D;
  int shadow_subresource = -1;
  std::array<int, 4> viewport = {-1, -1, -1, -1};
};

struct RTDesc {
  // Existing fields remain.
  std::string shadow_projection;
};

struct RenderGraphDesc {
  std::vector<ShadowProjectionDesc> shadow_projections;
  std::vector<RTDesc> render_targets;
  std::vector<RenderPassDesc> passes;
};
```

Keep JSON-facing enum values as strings through Glaze mappings or map strings during graph
validation, following the existing format/signature mapping style.

### 15.4 Render graph example

```json
{
  "shadow_projections": [
    {
      "id": "sun-shadow",
      "light_id": "sun",
      "legacy_light_index": 0,
      "technique": "csm",
      "target": "DepthPass",
      "cascade_count": 4,
      "resolution": 0,
      "split_lambda": 0.6,
      "near_distance": 0.1,
      "far_distance": 250.0,
      "caster_depth_padding": 50.0,
      "blend_fraction": 0.0
    }
  ],
  "render_targets": [
    {
      "name": "DepthPass",
      "color_count": 0,
      "color_format": "NONE",
      "depth_format": "F32",
      "size": [0, 0],
      "linear_filter": false,
      "shadow_projection": "sun-shadow"
    }
  ],
  "passes": [
    {
      "name": "Shadow Depth",
      "kind": "shadow_depth",
      "shadow_projection": "sun-shadow",
      "target": "DepthPass",
      "state": { "depth_stencil": "READ_WRITE", "cull_face": "BACK_FACES" },
      "draws": [
        { "type": "mesh", "mesh_indices": [], "signature": "SHADOW_MAP_PASS" }
      ],
      "post_state": { "cull_face": "FRONT_FACES" }
    }
  ]
}
```

The graph retains the existing GBuffer, Shadow Accumulation, blur, deferred, and
post-process passes. Do not author individual CSM passes in JSON; expansion generates them.

### 15.5 Runtime structures

Create `Framework/include/scene/ShadowSystem.h` and
`Framework/src/scene/ShadowSystem.cpp`. Keep fitting and layout code out of scenes.

```cpp
constexpr int kMaxShadowViewsPerProjection = 6;
constexpr int kMaxCascadeBoundaries = 5;

struct ShadowViewport {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct ShadowAtlasTransform {
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  float biasX = 0.0f;
  float biasY = 0.0f;
};

struct ShadowViewRuntime {
  ShadowViewKind kind = ShadowViewKind::WholeTexture2D;
  int subresource = -1;
  ShadowViewport viewport;
  Camera camera;
  XMATRIX44 viewProjection;
  ShadowAtlasTransform atlasScaleBias;
  XVECTOR3 frustumCorners[8];
};

struct ShadowProjectionRuntime {
  ShadowProjectionDesc resolvedDesc;
  int resolvedLightIndex = -1;
  int viewCount = 0;
  int atlasColumns = 1;
  int atlasRows = 1;
  int atlasWidth = 0;
  int atlasHeight = 0;
  float splitBoundaries[kMaxCascadeBoundaries] = {};
  ShadowViewRuntime views[kMaxShadowViewsPerProjection];
};

struct ShadowRuntimeState {
  std::vector<ShadowProjectionRuntime> projections;
  std::unordered_map<std::string, int> projectionById;
  void Reset();
};
```

Do not store the runtime atlas transform in `XVECTOR3`. Its current copy constructor resets
`w` to `1.0f`; copying a `ShadowProjectionRuntime` would therefore change `biasY` to 1 and
force all atlas V coordinates outside the texture. The GPU `XVECTOR3`/`float4` payload is
still valid, but populate all four components explicitly during upload.

`SceneProps` owns `ShadowRuntimeState Shadows`. Generated cameras are values inside runtime
state. They are not appended to `pLightCameras` and are not serialized.

### 15.6 Common ShadowSystem API

```cpp
class ShadowSystem {
public:
    static bool ResolveDescriptors(
      const RenderGraphDesc& graph,
      SceneProps& props,
      ShadowRuntimeState& runtime,
      std::string* error);

    static bool ResolveLightBindings(
      SceneProps& props,
      ShadowRuntimeState& runtime,
      std::string* error);

  static bool UpdateProjection(
      ShadowProjectionRuntime& projection,
      const SceneProps& props,
      const Camera& mainCamera,
      int tileResolution,
      std::string* error);

  static bool BuildDirectionalCascades(...);
  static bool BuildPointCubeViews(...);       // future
  static bool BuildDualParaboloidViews(...);  // future
  static int ResolveLightIndex(const SceneProps&, const ShadowProjectionDesc&);
  static void ComputeAtlasLayout(int viewCount, int& columns, int& rows);
};
```

For the CSM milestone, future builders return `unsupported technique`. Do not silently
execute a legacy path through this API.

## 16. Stable light identity and migration

### 16.1 Why indices are insufficient

`SceneProps::Lights`, `SandboxLightOverrideDesc`, and editor paths currently use vector
indices. Reordering or inserting lights changes those indices. That is unsafe for persistent
shadow-to-light associations.

### 16.2 Schema additions

```cpp
// SceneDescriptor.h
struct LightDesc {
  std::string id;
  std::string name;
  // Existing fields...
};

struct SandboxLightOverrideDesc {
  std::string light_id;
  int index = -1; // legacy fallback
  // Existing optionals...
};

// EditorSceneFile.h
struct SceneLightDesc {
  std::string id;
  std::string name = "Light";
  // Existing fields...
};

struct SceneLightCameraDesc {
  std::string attached_light_id;
  int attached_light = 0; // legacy fallback
  // Existing fields...
};

// SceneProp.h, Light
std::string Id;
std::string Name;
```

Resolution order everywhere:

1. Match non-empty stable ID.
2. If no ID or no match, use a valid legacy index.
3. Otherwise disable the association and log projection ID and light reference.

### 16.3 Deterministic migration

When loading an old `.t8scene` with an empty light ID, create a deterministic ID from the
normalized light name and occurrence number, such as `sun`, `light`, `light-2`. If the name
is empty, use `light-<original-index>`. Store it in editor state and write it on next save.
Do not generate a new random UUID on each load.

For legacy `SceneDescriptor` lights with neither ID nor name, use
`descriptor-light-<index>`. Existing index-based profile overrides continue to resolve.
Duplicating a light creates a new unique ID. Renaming changes only its display name.

## 17. SceneDescriptor, profiles, and `.t8scene`

### 17.1 Responsibility split

| Data | Owner | Reason |
|---|---|---|
| Technique, target, baseline view topology | Render graph JSON | Controls RT type, size, and pass expansion |
| Authored light identity and transform | `.t8scene` or `SceneDescriptor` | Scene content |
| Sparse platform/quality overrides | `SandboxProfileDesc` | Profile matching runs before RT creation |
| Generated cameras, splits, matrices, handles | `SceneProps::Shadows` | Runtime-only derived data |

Do not add cascade topology to both `QualityDesc` and the render graph.
`QualityDesc::shadow_map_resolution` remains the compatibility fallback tile resolution.

### 17.2 Typed profile overrides

```cpp
struct ShadowProjectionOverrideDesc {
  std::string projection_id;
  std::optional<bool> enabled;
  std::optional<int> resolution;
  std::optional<int> cascade_count;
  std::optional<float> split_lambda;
  std::optional<float> near_distance;
  std::optional<float> far_distance;
  std::optional<float> caster_depth_padding;
  std::optional<float> blend_fraction;
  bool operator==(const ShadowProjectionOverrideDesc&) const = default;
};

struct SandboxProfileDesc {
  // Existing fields...
  std::vector<ShadowProjectionOverrideDesc> shadow_projections;
};
```

Merge by `projection_id`. Base profile applies first and best runtime profile second.
Missing optionals preserve prior values. Reject technique or target changes in profiles.
Count and resolution changes are legal only because merge occurs before expansion and RT
creation. Clamp after the complete merge.

### 17.3 `.t8scene` schema and versioning

The root keeps `render_graph`; do not add a duplicate root shadow block. Shadow data reaches
`.t8scene` through:

1. `lights[].id` for stable association.
2. `profiles[].shadow_projections` for sparse overrides.
3. `render_graph` for baseline topology.

Increment `EditorSceneFile::version` to 2 when stable IDs are saved. Continue accepting
version 1 and migrate in memory. Unknown keys remain ignored by Glaze, but unresolved light
associations require explicit logs.

### 17.4 Editor snapshot, restore, and Play Scene

Update `T8ditor/EditorApp.cpp`:

- `BuildEditorSceneSnapshot` copies each light ID and typed profile overrides.
- Scene load and undo restore preserve IDs and migrate missing IDs once.
- Rebuild the ID-to-runtime-index map after all lights load.
- `UpsertEditorSceneProfile` captures typed shadow overrides.
- `ApplyEditorSceneProfile` merges overrides into pending effective graph configuration.
- Light duplication assigns a new unique ID.
- Light deletion leaves unresolved projections disabled and visible as validation errors; it
  must not retarget another light at the same index.

Play Scene already exports `BuildEditorSceneSnapshot`. Verify its temporary `.t8scene`
contains the selected `render_graph`, migrated light IDs, and typed projection overrides.
`SceneTemplate` then follows the normal startup path.

### 17.5 SceneSetup behavior

`SceneSetup::Apply` transfers light ID/name into runtime lights. Do not make
`ApplyQualityAndSettings` authoritative for cascade count. It continues to set global
fallback resolution, PCF settings, enable, and bias. `SaveState` saves light identity and
existing quality values. It does not currently save profiles, so typed overrides remain in
the existing profile save paths.

## 18. Exact startup and rebuild ordering

### 18.1 SceneTemplate

Refactor `SceneTemplate::CreateAssets` to this order:

1. Load `Scenes/SceneTemplate.json` control metadata if needed.
2. Pre-read the active `.t8scene`.
3. Select its `render_graph` path or the default graph.
4. Select base and best runtime profiles, retaining both profile objects.
5. Apply ordinary profile values to `SceneProps`, including fallback tile resolution.
6. Load render graph JSON.
7. Merge typed shadow overrides into graph projection baselines.
8. Validate structural projection data and expand generated passes. At this stage retain
  light IDs even if runtime lights are not instantiated yet.
9. Create targets from effective projection dimensions.
10. Cache RT handles and configure pass toggles.
11. Load remaining scene assets, including runtime lights.
12. Resolve projection-to-light associations by ID then legacy index. Rendering is not
  allowed until this binding succeeds.
13. Before each graph execution, update generated views from the active main camera and
    current light transforms.

The current code applies profiles before graph loading. Keep that for ordinary values, but
retain typed projection overrides and merge them after graph load and before RT creation.

### 18.2 Shared configuration API

Apply the same contract in `DayScene.cpp`, `SandboxScene.cpp`, `Quake3Mock.cpp`,
`RagdollEditor.cpp`, `T8ditor/EditorApp.cpp`, and `RenderContainer.cpp`. Do not duplicate
merge or expansion logic. Add:

```cpp
bool RenderGraph::Configure(
  SceneProps& props,
    const std::vector<ShadowProjectionOverrideDesc>& baseOverrides,
    const std::vector<ShadowProjectionOverrideDesc>& runtimeOverrides,
    std::string* error);
```

`Configure` merges overrides, validates structural data, expands passes, and initializes
unbound runtime projection records. `ShadowSystem::ResolveLightBindings` performs the
second phase after runtime lights exist. Calling
`CreateRenderTargets` before successful configuration is an error for a graph containing
`shadow_projections`.

### 18.3 Runtime editor changes

| Change | Required action |
|---|---|
| enabled | Regenerate passes; recreate only if target ownership changes |
| resolution | Recompute dimensions and recreate target |
| cascade count | Regenerate passes and recreate target |
| technique or target | Graph/content edit, not runtime profile control |
| lambda, near/far, padding | Recompute generated views only |
| blend fraction | Update constants; recompute overlap when implemented |
| associated light transform | Recompute generated views only |

Perform recreation at the editor's existing safe frame boundary. Never destroy an RT while
a hosted viewport is rendering it.

## 19. RenderGraph implementation details

### 19.1 Descriptor ownership

Keep both baseline and effective descriptors:

```cpp
RenderGraphDesc m_sourceDesc;    // exact parsed JSON
RenderGraphDesc m_effectiveDesc; // overrides applied and passes expanded
```

Repeated editor reconfiguration always restarts from `m_sourceDesc`. Do not mutate parsed
source data in place. Split `GetDescriptor()` into `GetSourceDescriptor()` and
`GetEffectiveDescriptor()`, or document that it returns only the effective descriptor.

### 19.2 Expansion algorithm

For each enabled effective projection:

1. Validate projection ID uniqueness and target existence.
2. Validate that the projection has an ID reference or valid legacy-index syntax. Runtime
  light binding is completed after scene lights are instantiated.
3. Validate target depth format against technique.
4. Derive view count, tile resolution, and atlas layout.
5. Find an authored shadow-depth template pass for the projection, or consume the first
   legacy `Shadow Depth` pass for compatibility.
6. Copy template state, draws, and post-state into generated passes.
7. Set typed projection/view metadata and viewport/subresource.
8. Insert generated passes at the template's original position.
9. Remove only the consumed template. Never remove unrelated passes by name prefix.
10. Call `BuildGraph` after all expansion is complete.

CSM view `i` uses:

```text
columns, rows = AtlasLayout(cascadeCount)
tileX = i % columns
tileY = i / columns
x = tileX * resolution
y = tileY * resolution
w = resolution
h = resolution
```

Views stay contiguous. Pass 0 binds and clears; middle passes continue; the final pass pops.
Do not call `PushRTLoad` for middle views unless another pass interrupted the group, which
must itself be treated as an invalid expanded graph.

### 19.3 Target sizing

When `RTDesc::shadow_projection` is set:

1. Find the effective projection by ID.
2. Resolve tile resolution from projection override, then graph value, then
   `SceneProps::ShadowMapResolution`.
3. For directional single use `R x R`.
4. For CSM use `columns*R x rows*R`.
5. For point cube use `R x R` with `CUBE_F32`.
6. For dual paraboloid use `2R x R` with `F32`.
7. Validate dimensions against `BaseDriver`/device texture limits before `CreateRT`.

The CSM target is still one ordinary 2D `F32` texture. Do not pass view count to the HAL as
an array layer count.

### 19.4 ExecutePass camera and viewport

Generated shadow camera selection happens before ordinary `camera == "light"` handling:

```cpp
if (pass.kind == RenderPassKind::ShadowDepth) {
  auto& projection = props.Shadows.projections.at(pass.shadow_projection_index);
  auto& view = projection.views[pass.shadow_view_index];
  props.SetPrimaryCamera(&view.camera);
} else if (pass.camera == "light" && lightCam) {
  props.SetPrimaryCamera(lightCam);
} else if (pass.camera == "main" && mainCam) {
  props.SetPrimaryCamera(mainCam);
}
```

`ScopedPrimaryCameraOverride` restores the preceding camera when the pass exits. Generated
passes do not mutate `ActiveLightCamera`.

`ScopedPrimaryCameraOverride` already exists in `SceneProp.h` / `SceneProp.cpp`; reuse it.
Do not add a second camera-scope helper.

Apply viewport after binding or continuing the target:

```cpp
if (pass.viewport[2] > 0 && pass.viewport[3] > 0) {
  driver->SetViewport(x, y, w, h);
  driver->SetScissorRect(x, y, w, h);
}
```

Every target bind sets full-target viewport/scissor. Every tile pass overrides both. Replace
the exact `name == "Shadow Depth"` skip with `pass.kind == ShadowDepth`.

### 19.5 Camera API migration

Keep the singular `lightCam` and `omniCams` arguments to `RenderGraph::Execute` for legacy
graphs during CSM migration. Do not add a raw `cascadeCams` pointer. New projections use
`SceneProps::Shadows`; future cube migration removes dependence on `omniCams`.

## 20. CSM fitting implementation details

### 20.1 Update policy

Before graph execution, update CSM when these inputs change:

- main camera pose, near/far, FOV, aspect, handedness, or projection mode,
- associated directional-light direction,
- count, lambda, configured range, padding, or blend fraction,
- tile resolution or atlas layout.

The first implementation may recompute every frame. Add input caching only after all APIs
are correct.

### 20.2 Split boundaries

For N cascades, store N minus 1 selection boundaries:

$$
p_i = \frac{i}{N},\quad i \in [1,N-1]
$$

$$
d_{log} = n\left(\frac{f}{n}\right)^{p_i}
$$

$$
d_{lin} = n + (f-n)p_i
$$

$$
s_i = \lambda d_{log} + (1-\lambda)d_{lin}
$$

Use `near = max(configNear, camera.NPlane)` and
`far = min(configFar, camera.FPlane)`. Reject non-finite input and `far <= near`.

### 20.3 World-space corners

Use camera basis instead of inverse-NDC reconstruction because this engine uses reversed
depth and supports left/right handed cameras.

Perspective slice plane at distance `d`:

```text
center = Eye + Look * d
halfHeight = tan(Fov * 0.5) * d
halfWidth = halfHeight * AspectRatio
corners = center +/- Right * halfWidth +/- Up * halfHeight
```

Orthographic slices use `Width*0.5` and `Height*0.5` at both planes. Normalize and
orthogonalize the camera basis first. Preserve all eight receiver corners for debug output.

### 20.4 Light basis and stable extent

For each cascade:

1. Normalize light direction and reject a near-zero vector.
2. Use world up `(0,1,0)` unless `abs(dot(direction, up)) > 0.99`; then use `(0,0,1)`.
3. Build an orthonormal light basis.
4. Compute receiver center and bounding-sphere radius from all eight corners.
5. Round radius upward to a small fixed increment to prevent extent oscillation.
6. Use a square XY extent `[-radius,+radius]` around the light-space center.
7. Transform corners to light space and derive receiver min/max Z.
8. Expand toward the light by `caster_depth_padding`; add a smaller away-from-light safety
   expansion if needed by testing.
9. Position the generated camera so near is positive and far covers the expanded interval.
10. Initialize/update the generated orthographic `Camera` and save its VP.

Never pass a negative near plane to `Camera::InitOrtho`.

### 20.5 Texel snapping

```text
extent = 2 * radius
worldUnitsPerTexel = extent / tileResolution
centerLS.x = round(centerLS.x / worldUnitsPerTexel) * worldUnitsPerTexel
centerLS.y = round(centerLS.y / worldUnitsPerTexel) * worldUnitsPerTexel
```

Build the final view/projection from the snapped center. Do not patch matrix translation
without respecting this engine's matrix convention.

### 20.6 Culling and caster coverage

`RenderMesh::Draw` uses the current primary camera for shadow-pass culling. The generated
light camera must include depth padding or valid off-screen casters are culled. Receiver
fitting does not replace caster fitting. Phase 1 renders all meshes accepted by the expanded
light frustum. Per-cascade caster lists are a later conservative optimization.

## 21. Constant buffers and shaders

### 21.1 CPU layout

Add to `RenderQuad.h`:

```cpp
struct ShadowSamplingCBuffer {
  XMATRIX44 ViewProjection[6];
  XVECTOR3 SplitDepths[2];
  XVECTOR3 AtlasScaleBias[6];
  XVECTOR3 Params0; // x=viewCount, y=atlasWidth, z=atlasHeight, w=technique
  XVECTOR3 Params1; // x=farDistance, y=blendFraction, z=shadowBias, w=shadowMin
};
```

`XVECTOR3` carries four floats. Add `static_assert`s for 16-byte alignment and expecteda
size. With 64-byte `XMATRIX44` and 16-byte `XVECTOR3`, the expected size is 544 bytes:

```cpp
static_assert(sizeof(XMATRIX44) == 64);
static_assert(sizeof(XVECTOR3) == 16);
static_assert(sizeof(ShadowSamplingCBuffer) == 544);
```

The logical payload is 544 bytes. `D3D12ConstantBuffer::Create` already rounds
`descriptor.byteWidth` to a 256-byte allocation/CBV size, so this payload allocates 768
bytes on D3D12 while uploads still copy 544 bytes. Do not add manual padding to the shared
CPU struct solely for D3D12. Verify the ring allocator used by `Set` applies the same
constant-buffer alignment rule.

Zero all unused entries on each upload so count reductions cannot expose stale data.

Add `ShadowSamplingCBGPU` and a zero-initialized CPU instance. Create/destroy them with the
existing frame/pass buffers. Bind slot 2 only for directional/CSM `SHADOW_COMP` sampling.

### 21.2 HLSL declaration

Add only to `FS_Quad.hlsl`:

```hlsl
#define MAX_SHADOW_VIEWS 6

cbuffer ShadowSamplingCB : register(b2) {
  float4x4 ShadowViewProjection[MAX_SHADOW_VIEWS];
  float4 ShadowSplitDepths[2];
  float4 ShadowAtlasScaleBias[MAX_SHADOW_VIEWS];
  float4 ShadowParams0;
  float4 ShadowParams1;
}
```

Keep `QuadFrameCB` unchanged. Existing `WVPLight` remains for legacy passes until migration
is complete.

### 21.3 GLSL declaration and upload

Add numeric bounds to `FS_Quad.glsl`:

```glsl
uniform highp mat4 ShadowViewProjection[6];
uniform highp vec4 ShadowSplitDepths[2];
uniform highp vec4 ShadowAtlasScaleBias[6];
uniform highp vec4 ShadowParams0;
uniform highp vec4 ShadowParams1;
```

Do not use symbolic array bounds because `GLSL_Parser::DetermineArrayNum` uses `atoi`.
Append equivalent fields to the monolithic `RenderQuad::CBuffer` in the order used by GL
uniform byte positioning. Verify reflected counts 6, 2, and 6 and the corresponding
`glUniformMatrix4fv` count. In a GL debug test, require valid locations for
`ShadowViewProjection[0]`, `ShadowSplitDepths[0]`, and
`ShadowAtlasScaleBias[0]`; log the parser count and queried location before the first draw.

### 21.4 Main-camera depth constants

The `SHADOW_COMP` pass reconstructs GBuffer depth, so `RenderQuad::Draw` must use the main
camera for `WVPInverse`, `WorldView`, `Projection`, and `CameraInfo`. Populate `CameraInfo`
in the `SHADOW_COMP` branch before calling `LinearizeDepth`. Never use a generated shadow
camera to reconstruct main-camera depth.

The existing shader expects `CameraInfo.x = near` and `CameraInfo.y = far`. Populate the
complete value as:

```cpp
CnstBuffer.CameraInfo = XVECTOR3(
  mainCamera->NPlane,
  mainCamera->FPlane,
  mainCamera->Fov,
  1.0f);
```

The deferred pass currently uses `.w` for packed-light count. `SHADOW_COMP` does not; set
`.w` to `1.0f` so stale deferred data cannot affect future shader changes.

### 21.5 Cascade selection

Use a fixed loop and runtime predicate in HLSL and GLSL:

```hlsl
int GetCascadeIndex(float viewDepth)
{
  int viewCount = clamp((int)ShadowParams0.x, 1, MAX_SHADOW_VIEWS);
  int result = 0;
  [unroll]
  for (int boundary = 0; boundary < MAX_SHADOW_VIEWS - 1; ++boundary) {
    if (boundary < viewCount - 1 && viewDepth > GetShadowSplit(boundary))
      ++result;
  }
  return min(result, viewCount - 1);
}
```

`GetShadowSplit` explicitly selects components from the two float4 values. Only components
0 through 4 are valid boundaries.

### 21.6 Atlas sampling and PCF

1. Transform reconstructed world position by selected view projection.
2. Perspective divide.
3. Convert to local UV and apply current vertical convention.
4. Reject local UV/depth outside valid shadow domain.
5. Apply per-view atlas scale/bias.
6. Compute texel size from full atlas width/height.
7. Clamp every PCF tap to selected tile interior:

```text
tileMin = bias + 0.5 * atlasTexel
tileMax = bias + scale - 0.5 * atlasTexel
```

Keep the depth target nearest-filtered. Optional guard bands may follow for large kernels;
half-texel clamp is mandatory.

### 21.7 Permutations by resource dimension

Cascade count does not need a permutation. Resource dimension does:

- Directional, CSM, and dual paraboloid use `Texture2D` / `sampler2D`.
- Cube uses `TextureCube` / `samplerCube`.

Keep `ShaderKey::OMNI_SHADOWS` initially or replace it with a coordinated sampling-mode
field. Add dual-paraboloid define only with its implementation. The current GLSL file has a
cube branch, while the HLSL shadow-composition section uses a 2D texture. HLSL cube sampling
is required before cube shadows are supported on D3D11, D3D12, or Vulkan.

## 22. HAL and render-target view contract

### 22.1 CSM requirements

CSM uses one ordinary 2D depth view. Existing backend clear/load, viewport, and scissor
operations are sufficient when called in the correct order. No texture-array work is needed.

### 22.2 Generalized future view binding

`BaseRT::ChangeCubeDepthTexture` is too narrow and Vulkan leaves it TODO. Before cube work,
replace or wrap it with:

```cpp
enum class RTSubresourceKind : uint8_t { WholeResource, CubeFace };
enum class RTLoadAction : uint8_t { Clear, Load };

struct RTViewBinding {
  RTSubresourceKind kind = RTSubresourceKind::WholeResource;
  int subresource = -1;
  RTLoadAction loadAction = RTLoadAction::Clear;
};

virtual bool BaseRT::SetView(
    const DeviceContext& context,
    const RTViewBinding& binding) = 0;
```

Existing `Set`/`SetLoad` may wrap whole-resource clear/load. Add
`BaseDriver::PushRTView`. RenderGraph uses typed view metadata and never handles DSV/image
view/FBO details.

### 22.3 Cube backend requirements

**D3D11:** One typeless cube depth texture and SRV, one DSV per face. Bind and clear/load
only the selected DSV.

**D3D12:** One DSV descriptor per face, correct depth-write/shader-read transitions, face
binding and selected-face clear. Transition for sampling after final face.

**OpenGL:** Attach `GL_TEXTURE_CUBE_MAP_POSITIVE_X + face` with
`glFramebufferTexture2D`, validate FBO completeness in debug, and clear selected face only.

**Vulkan:** Create cube-compatible image, cube sampling view, six 2D face views, and six
framebuffers, or adopt dynamic rendering consistently. End active pass, transition, and
begin a clear/load pass on the selected face. Remove the current TODO only after all faces
render and sample correctly.

### 22.4 Dual-paraboloid rendering

Dual paraboloid uses an ordinary 2D `F32` 2x1 atlas, so no new HAL texture type is needed.
It requires a dedicated depth-generation mesh shader:

1. Compute direction and radial distance from point light to vertex.
2. Select front/back hemisphere for the generated view.
3. Project direction onto paraboloid XY.
4. Write normalized radial depth.
5. Clip or conservatively handle geometry crossing the hemisphere seam.

Sampling selects hemisphere from fragment-to-light direction, reconstructs UV, applies tile
scale/bias, and compares radial depth. Add seam overlap and bias tests. Do not reuse the
directional orthographic mesh shadow shader unchanged.

## 23. Lighting integration and multiple shadowed lights

The current `ShadowAccum` texture is one screen-space factor consumed by deferred lighting.
It naturally supports one primary directional shadow. It cannot correctly represent several
independently shadowed point lights because one scalar factor would shadow unrelated lights.

### 23.1 CSM milestone contract

- Support one enabled `directional` or `csm` projection.
- Produce the existing `ShadowAccum` factor.
- Leave blur and deferred consumption unchanged.
- Reject a second enabled directional projection with a validation error.
- Reject point techniques in the common projection path until per-light integration exists.

### 23.2 Future point-light contract

Cube and dual-paraboloid shadows are associated with one point light. Preferred integration:

1. Render each shadowed point light through the existing deferred light-volume path.
2. Bind that light's shadow resource and projection constants for its light-volume draw.
3. Evaluate cube or dual-paraboloid shadow inside that light's shader path.
4. Additively accumulate the already-shadowed light contribution.

This avoids indexing arbitrary cube textures inside the fullscreen 128-light loop and avoids
multiplying every light by one global shadow factor. Until this exists, point descriptors
fail configuration instead of pretending to contribute through `ShadowAccum`.

### 23.3 Future resource budget

Define configurable limits with point shadows:

- maximum shadowed point lights per frame,
- maximum total shadow texels,
- update frequency and priority,
- distance/visibility eviction policy.

Select deterministically, with stable light ID as tie-breaker. Record selected and dropped
counts in diagnostics. Resource caching keys include stable light ID, technique, resolution,
and backend/API generation so API switches cannot reuse invalid handles.

## 24. Debug, capture, validation, and failure behavior

### 24.1 Frame capture/replay

Extend the frame-dumper schema and capture/restore paths with:

- effective projection IDs and techniques,
- resolved light IDs and indices,
- effective count, resolution, splits, and range,
- atlas dimensions and per-view scale/bias,
- all generated view-projection matrices.

Replay either restores captured derived state exactly or recomputes and compares within a
tolerance. Do not capture only `ActiveLightCamera`; generated CSM does not use it.

### 24.2 Configuration failures

Fail before RT creation for:

- duplicate projection ID,
- missing or duplicate stable light match before first execution,
- invalid fallback index,
- technique incompatible with light type,
- missing target or incompatible shared target,
- incompatible depth format,
- cascade count outside 1 through 6,
- non-finite or invalid near/far/lambda/padding,
- atlas dimensions above backend limits,
- viewport outside target dimensions,
- missing shadow-depth template pass,
- unsupported future technique.

Log graph path, projection ID, offending field, and effective value. Never silently retarget
another light or downgrade CSM. A graph with no projection descriptor follows the explicit
legacy path.

### 24.3 Runtime diagnostics

Expose:

- effective projection and generated-view counts,
- atlas width, height, and texel count,
- current split distances,
- projection update CPU time,
- meshes drawn/culled per view,
- invalid light associations,
- future point-shadow lights selected/dropped by budget.

Debug rendering includes atlas occupancy, cascade index color, receiver frustums, fitted
light volumes, and split readout. Use editor controls rather than adding an unreviewed global
key binding.

Keep the two debug concepts distinct:

- **Cascade regions** reconstruct each visible scene position from GBuffer depth and use the
  production split boundaries to select exactly one cascade. This view cannot overlap and is
  the authoritative visualization for cascade selection and split continuity. Sky pixels
  have zero alpha and remain untouched.
- **Fitted light bounds** use each generated orthographic camera's width, height, and
  near/far range. They can overlap substantially because each encloses a rotated receiver
  slice and includes caster padding. This is correct and does not imply overlapping cascade
  selection.

Minecraft exposes `Cascade regions`, `Light bounds (overlap expected)`, and `Both`. Cascade
regions are the authored default. Do not draw transparent receiver-frustum shells through
the generating camera: even disjoint frustum slices project as nested viewport rectangles
and visually resemble overlapping overlays.

Render cascade regions through `CASCADE_DEBUG_PASS`, a fullscreen pass immediately after
`Forward Transparent`. It reads `GBuffer:DEPTH`, reuses the fixed six-view shadow sampling
buffer at slot `b2`, and traverses the same split boundaries as `SHADOW_COMP_PASS`. Blend its
single selected color into `Deferred` using authored opacity. The pass remains in the graph
and returns zero alpha when disabled, avoiding runtime graph mutation.

When rendering from a spectator or light camera, reconstruct the world position with that
active camera, then transform it through the player/culling camera. Use player-view `z` for
split selection and player clip coordinates for frustum containment. Do not linearize the
active camera's depth and compare it to player-camera split distances; that incorrectly makes
both production shadow tile selection and debug colors move with the spectator.

Render fitted light-bound geometry through a named render-graph callback, not as a post-graph
overlay. `RenderGraph::Execute` accepts an optional callback dispatcher, and a draw entry
with `type: "callback"` invokes it before shader-signature resolution. The Minecraft graph
dispatches `cascade_debug_volumes` into the existing `Deferred` target with `push: false`.
Use alpha blending, depth read with no depth write, and single-sided rasterization. This makes
opaque scene depth occlude debug geometry and leaves later light-add and post-processing
passes unchanged. Dispatch this callback only for `Light bounds` and `Both` modes.

Build filled boxes from one dynamic position buffer and shared 16-bit line and triangle
index buffers. Allocate them lazily and update only the vertex contents each frame; creating
GPU buffers per frame can cause device removal. Draw translucent boxes far-to-near, then
draw their outlines with stronger alpha. The flat RGBA shader is shared by line-list and
triangle-list draws; blend, depth, and cull state belong to the render-graph pass.

Store `show_cascade_debug`, `cascade_debug_mode`, and `cascade_debug_opacity` in the
`.t8scene` voxel settings. Clamp loaded mode to 0 through 2 and opacity to 0.01 through 0.75,
and write edited values back through scene serialization. Do not hide these defaults in
Minecraft runtime code or bind the visualization to an unowned global key.

Store the six cascade colors in `cascade_debug_colors`. Upload one authored palette through
the quad pass constants so fullscreen cascade regions and callback-drawn fitted bounds cannot
drift to different hardcoded color tables.

The generated orthographic cascade cameras follow the attached directional light every frame.
Minecraft's passive Light view follows the automatic Sun trajectory, but only the explicit
`Move light camera` edit state may write the authored light camera. Manual editing pauses the
trajectory and drives the directional light from that camera; saving a paused trajectory
preserves the manual light direction used to regenerate CSM views on the next launch.

### 24.4 Pure-logic tests

Add tests for code that does not need a GPU:

1. Atlas layout for view counts 1 through 6.
2. Scale/bias and viewport for 1x1, 2x1, 2x2, and 3x2.
3. Split monotonicity and boundary count for lambda 0, 0.6, and 1.
4. Perspective and orthographic corner construction.
5. Near/far clamping and non-finite rejection.
6. Stable ID migration and ID-first/index-fallback resolution.
7. Base/runtime override merge by projection ID.
8. Generated pass order and push/pop values.
9. Source descriptor remains unexpanded and reusable.

### 24.5 Cross-API visual matrix

Run on D3D11, D3D12, OpenGL, and Vulkan:

1. Legacy one-map graph.
2. Generated counts 1 through 6.
3. Counts 2 and 5 for non-square atlases.
4. PCF at every tile edge.
5. Camera movement for shimmer detection.
6. Orthographic main camera.
7. Off-screen caster entering a visible slice.
8. Shadow disable/enable.
9. Resize, hosted viewport resize, and API switch.
10. T8ditor save/load, undo/redo, and Play Scene export.
11. Frame capture/replay.

Cube and dual-paraboloid receive separate test matrices only after their milestones exist.

## 25. File-by-file implementation checklist

This list is the implementation agent's authoritative work queue.

### 25.1 Framework schema and runtime

**`Framework/include/scene/ShadowDescriptor.h` (new)**

- Add shared technique/view enums, projection descriptor, and typed override descriptor.

**`Framework/include/scene/RenderGraphDescriptor.h`**

- Include `ShadowDescriptor.h`; add render-pass-kind enum.
- Add `ShadowProjectionDesc` and `RenderGraphDesc::shadow_projections`.
- Add `RTDesc::shadow_projection`.
- Add generated shadow metadata and viewport to `RenderPassDesc`.

**`Framework/include/scene/ShadowSystem.h` (new)**

- Add runtime structures and API from section 15.

**`Framework/src/scene/ShadowSystem.cpp` (new)**

- Implement validation, light resolution, atlas layout, splits, corners, stable fit,
  padding, texel snapping, and runtime update.

**`Framework/include/scene/SceneProp.h` / `Framework/src/scene/SceneProp.cpp`**

- Add stable ID/name to `Light`.
- Own `ShadowRuntimeState`.
- Preserve authored light-camera semantics.

### 25.2 Render graph

**`Framework/include/scene/RenderGraph.h`**

- Store source/effective descriptors.
- Add `Configure` and expansion helpers.
- Keep legacy camera arguments during migration.

**`Framework/src/scene/RenderGraph.cpp`**

- Parse/map new enums.
- Merge typed overrides and validate.
- Size shadow-referenced targets.
- Expand passes before `BuildGraph`.
- Use pass kind for skipping.
- Select generated camera.
- Set viewport/scissor after binding.
- Preserve one-clear atlas semantics.

**`Framework/src/scene/RenderContainer.cpp`**

- Call `Configure` before target creation.
- Reconfigure on topology changes.

### 25.3 Scene formats and controls

**`Framework/include/scene/SceneDescriptor.h`**

- Add light ID/name.
- Add `light_id` to light overrides.
- Add typed projection overrides to profiles.

**`Framework/src/scene/SceneSetup.cpp`**

- Transfer light identity to runtime lights.
- Keep resolution/PCF as fallbacks.
- Save light identity.
- Do not add a second cascade-count authority.

**`Framework/include/scene/EditorSceneFile.h`**

- Add light IDs and attached-light ID fallback.
- Default to version 2 after migration works.

**`Framework/src/scene/EditorSceneFile.cpp`**

- Migrate missing IDs deterministically.
- Validate duplicates and preserve version-1 loading.

### 25.4 Scene and editor call sites

**`DayScene/SceneTemplate.cpp`**

- Implement section 18.1 startup order.
- Capture/apply typed projection overrides.
- Update projections before graph execution.

**`DayScene/DayScene.cpp`**, **`DayScene/SandboxScene.cpp`**,
**`DayScene/Quake3Mock.cpp`**, **`DayScene/RagdollEditor.cpp`**

- Configure before target creation.
- Apply descriptor/profile overrides.
- Update before execution.
- Preserve legacy graphs.

**`T8ditor/EditorWorld.h` / `T8ditor/EditorWorld.cpp`**

- Store stable ID on editor lights and pending projection overrides.

**`T8ditor/EditorApp.cpp`**

- Snapshot/restore IDs and typed overrides.
- Add controls and safe recreation.
- Preserve IDs through duplicate, undo, save/load, and Play Scene.
- Display validation errors and cascade debug views.

### 25.5 Quad constants and shaders

**`Framework/include/scene/RenderQuad.h`**

- Add fixed `ShadowSamplingCBuffer`, GPU buffer, CPU instance, and GL payload fields.

**`Framework/src/scene/RenderQuad.cpp`**

- Create/destroy/upload slot-2 buffer.
- Populate main-camera constants in `SHADOW_COMP`.
- Upload effective projection runtime data.
- Keep cascade count out of `ShaderKey`.

**`Assets/Shaders/FS_Quad.hlsl`**

- Add fixed slot-2 constants.
- Implement split lookup, selection, atlas mapping, and clamped PCF.
- Preserve legacy path until compatibility removal is approved.

**`Assets/Shaders/FS_Quad.glsl`**

- Mirror HLSL with numeric array bounds.
- Preserve current GL vertical convention.

**`Assets/Shaders/VS_Quad.hlsl` / `VS_Quad.glsl`**

- No CSM sampling change unless declarations are deliberately shared.

### 25.6 HAL and reflection

**D3D11 files under `Framework/src/video/d3d11/`**

- CSM: validate slot-2 reflection/upload and viewport.
- Future cube: typed face binding.

**D3D12 files under `Framework/src/video/d3d12/`**

- Validate `b2` root CBV discovery and root budget.
- Keep viewport/scissor paired.
- Future cube: per-face DSVs/transitions.

**`Framework/src/video/gl/GLSLParser.cpp` / `GLConstantBuffer.cpp`**

- Validate fixed numeric arrays and byte bounds.
- Do not rely on symbolic array parsing.

**`Framework/src/video/vulkan/VulkanShader.cpp` /
`Framework/src/utils/SPIRVReflection.cpp`**

- Validate shifted slot-2 UBO binding and collision absence.

**`Framework/src/video/vulkan/VulkanRT.cpp`**

- CSM: validate tile viewport/scissor in one open pass.
- Future cube: face views/framebuffers and remove TODO.

### 25.7 Assets, capture, and docs

**`Assets/Scenes/*_RenderGraph.json`**

- Convert one representative scene first.
- Keep at least one legacy graph for compatibility.
- Convert others only after four-API acceptance.

**`Assets/Scenes/*.json` and `*.t8scene`**

- Add stable light IDs when touched.
- Add typed overrides only for scene-specific tuning.

**`Framework/src/debug/FrameDumper.cpp` and its schema/header**

- Capture/restore section 24.1 state.

**Documentation**

- Update render graph, scene format, SceneSetup, editor, shader management, and backend docs
  after implementation.

## 26. Implementation milestones and stop conditions

### Milestone A: schemas and identity

- Add descriptors, typed overrides, IDs, migration, and pure tests.
- Do not change rendering yet.
- Stop if old `.t8scene` or SceneDescriptor files no longer load identically.

### Milestone B: runtime and graph expansion

- Add ShadowSystem, effective descriptor, sizing, generated passes, and cameras.
- Render atlas depth debug output before changing sampling.
- Stop if a tile is cleared, empty, uses wrong camera, or exceeds bounds.

### Milestone C: CSM sampling

- Add fixed constants and both shader implementations.
- Validate count-1 parity before counts 2 through 6.
- Stop on GL uniform mismatch, D3D12 root mismatch, Vulkan descriptor collision, or
  cross-tile reads.

### Milestone D: editor and scene round-trip

- Add controls, typed persistence, migration, and Play Scene checks.
- Stop if rebuild happens after drawing or light rename/reorder changes association.

### Milestone E: cross-API acceptance

- Complete section 24.5.
- CSM is complete only after all four APIs pass.

### Future Milestone F: point cube

- Implement typed cube-face views on every HAL.
- Implement HLSL cube sampling and per-light deferred-volume integration.
- Add budgeting and association tests.
- Do not claim support while Vulkan face binding remains TODO.

### Future Milestone G: dual paraboloid

- Implement two-view radial depth generation and sampling.
- Add seam overlap, bias, and cube comparison tests.
- Reuse common projection/runtime/pass model, not CSM-specific branches.

## 27. Build and acceptance order

1. Build Framework and DayScene using the normal Windows x64 build.
2. Run one-cascade legacy and generated compatibility scenes.
3. Run four-cascade CSM and inspect all atlas tiles.
4. Build/run D3D12, D3D11, OpenGL, and Vulkan.
5. Build T8ditor and test save/load/undo/Play Scene.
6. Run counts 1 through 6 on all APIs.
7. Test resize, API switch, capture/replay, and shadow disable.

Do not update every graph at once. Land one representative CSM graph, retain legacy
coverage, and migrate other scenes after the shared implementation passes.

## 28. References

- Microsoft, Cascaded Shadow Maps: https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps
- Alex Tardif, Shadow Mapping Notes: https://alextardif.com/shadowmapping.html
- T850 rendering docs index: ../README.md