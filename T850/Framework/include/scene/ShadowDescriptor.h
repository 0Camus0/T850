#ifndef T800_SHADOW_DESCRIPTOR_H
#define T800_SHADOW_DESCRIPTOR_H

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace t850 {

  // ── Shadow technique ──
  // Shared between RenderGraphDescriptor.h and SceneDescriptor.h so the
  // render-graph and profile types do not depend on each other.
  enum class ShadowTechnique : uint8_t {
    DirectionalSingle,
    DirectionalCascaded,
    PointCube,
    PointDualParaboloid
  };

  // ── Shadow view kind ──
  // How a generated shadow pass maps into the shadow resource.
  enum class ShadowViewKind : uint8_t {
    WholeTexture2D,
    AtlasTile,
    CubeFace
  };

  // ── Shadow projection descriptor (render graph JSON) ──
  // Declares one shadow-casting projection. The first implementation supports
  // zero or one directional projection, but the shape also represents future
  // cube and dual-paraboloid projections.
  struct ShadowProjectionDesc {
    std::string id;
    std::string light_id;
    int legacy_light_index = -1;
    std::string technique = "directional";  // "directional", "csm", "point_cube", "point_dual_paraboloid"
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

  // ── Typed profile override (SandboxProfileDesc) ──
  // Sparse platform/quality overrides merged by projection_id.
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

} // namespace t850

#endif // T800_SHADOW_DESCRIPTOR_H
