#ifndef T800_SHADOW_SYSTEM_H
#define T800_SHADOW_SYSTEM_H

#include <string>
#include <vector>
#include <unordered_map>
#include <scene/ShadowDescriptor.h>
#include <scene/RenderGraphDescriptor.h>
#include <utils/Camera.h>
#include <utils/xMaths.h>

struct SceneProps;  // forward declaration (global namespace, defined in SceneProp.h)

namespace t850 {

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

    static bool BuildDirectionalCascades(
      ShadowProjectionRuntime& projection,
      const SceneProps& props,
      const Camera& mainCamera,
      int tileResolution,
      std::string* error);

    static bool BuildPointCubeViews(ShadowProjectionRuntime&, const SceneProps&, const Camera&, int, std::string* error);
    static bool BuildDualParaboloidViews(ShadowProjectionRuntime&, const SceneProps&, const Camera&, int, std::string* error);

    static int ResolveLightIndex(const SceneProps& props, const ShadowProjectionDesc& desc);
    static void ComputeAtlasLayout(int viewCount, int& columns, int& rows);
    static ShadowTechnique ResolveTechnique(const std::string& name);
  };

} // namespace t850

#endif // T800_SHADOW_SYSTEM_H
