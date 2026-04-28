#ifndef T800_SCENESETUP_H
#define T800_SCENESETUP_H

#include <string>
#include <vector>
#include <scene/SceneDescriptor.h>
#include <scene/SceneProp.h>
#include <utils/Camera.h>
#include <utils/Spline.h>

namespace t850 {

  class SceneBase;  // forward

  // SceneSetup: loads a JSON scene descriptor and builds all scene objects.
  // Owns cameras, light cameras, gauss filters, splines, and spline agents.
  // Call Load() to parse and build, then Apply() to wire into a SceneProps.
  class SceneSetup {
  public:
    SceneSetup() = default;

    // Non-copyable (pointers into internal vectors would be invalidated)
    SceneSetup(const SceneSetup&) = delete;
    SceneSetup& operator=(const SceneSetup&) = delete;

    // Load scene from JSON and build all objects.
    bool Load(const std::string& jsonPath);

    // Wire built objects into a SceneProps instance.
    void Apply(SceneProps& props);

    // Write current runtime state back to descriptor and save to JSON.
    void SaveState(SceneBase* scene, const std::string& jsonPath);

    // ── Built objects (owned by SceneSetup) ──
    std::vector<Camera>       cameras;
    std::vector<Camera>       lightCameras;
    std::vector<GaussFilter>  gaussFilters;
    std::vector<Spline>       splines;
    std::vector<SplineAgent>  agents;

    // Convenience accessors (bounds-checked, return nullptr on out-of-range)
    Camera*      GetCamera(int index = 0);
    Camera*      GetLightCamera(int index = 0);
    GaussFilter* GetGaussFilter(int index = 0);
    Spline*      GetSpline(int index = 0);
    SplineAgent* GetAgent(int index = 0);

    // The parsed descriptor (kept for introspection / re-export)
    SceneDescriptor descriptor;

    // Asset paths from JSON (scenes use these in CreateAssets)
    std::vector<std::string> meshPaths;
    std::string environmentMap;
    std::string environmentDiffuseIBL;
    std::string environmentSpecularIBL;
    std::string environmentBrdfLUT;
    std::string environmentSheenIBL;
    std::string environmentCharlieLUT;
    std::string environmentSheenELUT;
    std::string name;
  };

} // namespace t850

#endif
