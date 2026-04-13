#ifndef T800_SCENEDESCRIPTOR_H
#define T800_SCENEDESCRIPTOR_H

#include <string>
#include <vector>
#include <array>

namespace t800 {

  // JSON-serializable scene description structures.
  // These mirror what SC_Day/SC_Night/SC_Tech currently hardcode,
  // allowing scenes to be loaded from JSON files via glaze.

  struct CameraDesc {
    std::array<float, 3> position = {0, 0, 0};
    std::array<float, 3> eye = {0, 0, 0};
    float pitch = 0;
    float roll = 0;
    float yaw = 0;
    float speed = 10.0f;

    // Perspective params
    float fov = 0.817f;  // ~46.8 degrees
    float aspect = 1.7778f; // 16:9
    // Ortho params
    float width = 130.0f;
    float height = 130.0f;
    // Shared
    float near_plane = 2.0f;
    float far_plane = 12000.0f;
    bool ortho = false;
    bool left_handed = true;
  };

  struct LightDesc {
    std::string type = "point";  // "directional" or "point"
    std::array<float, 3> position = {0, 0, 0};
    std::array<float, 3> direction = {0, -1, 0};  // for directional lights
    std::array<float, 3> color = {1, 1, 1};
    float radius = 100.0f;
    float intensity = 1.0f;
    bool enabled = true;
  };

  struct GaussFilterDesc {
    int kernel_size = 4;
    float radius = 1.0f;
    float sigma = 1.0f;
  };

  struct SplinePointDesc {
    std::array<float, 3> position = {0, 0, 0};
    float velocity = 7.0f;
  };

  struct SplineDesc {
    std::vector<SplinePointDesc> points;
    bool looped = false;
    float agent_velocity = 15.0f;
    int agent_offset = 0;
    int attached_camera = 0;  // index into cameras[]
  };

  struct QualityDesc {
    float shadow_map_resolution = 2048.0f;
    float god_rays_resolution = 0.0f;
    float pcf_scale = 1.5f;
    float pcf_samples = 3.0f;
    float parallax_low_samples = 20.0f;
    float parallax_high_samples = 30.0f;
    float parallax_height = 0.02f;
    float light_volume_steps = 256.0f;
    int   ssao_kernel_size = 32;
    float ssao_radius = 1.5f;
    float dof_near_samples = 1.0f;
    float dof_far_samples = 3.0f;
  };

  struct SceneSettingsDesc {
    float exposure = 0.0f;
    float bloom_factor = 0.35f;
    float bloom_threshold = 2.0f;
    float tone_map_white_level = 4.0f;
    float luminance_tau = 1.1f;
    float aperture = 120.0f;
    float focal_length = 50.0f;
    float max_coc = 2.5f;
    std::array<float, 3> ambient_color = {0.8f, 0.8f, 0.8f};
    int active_lights = 5;
    bool shadow_enabled = true;
    bool ssao_enabled = true;
    bool auto_focus = true;
    int debug_mode = 0;
  };

  struct SliderDesc {
    std::string name;
    std::string label;
    float min_val = 0.0f;
    float max_val = 1.0f;
    float step = 0.1f;
    float default_val = 0.5f;
  };

  struct CheckboxDesc {
    std::string name;
    std::string label;
    bool default_val = false;
  };

  struct SelectorDesc {
    std::string name;
    std::string label;
    std::vector<std::string> options;
    int default_index = 0;
  };

  struct SceneDescriptor {
    std::string name;
    std::vector<CameraDesc> cameras;
    std::vector<CameraDesc> light_cameras;
    std::vector<LightDesc> lights;
    std::vector<GaussFilterDesc> gauss_filters;  // shadow, bloom, dof
    std::vector<SplineDesc> splines;
    std::vector<std::string> meshes;  // model file paths
    std::string environment_map;
    QualityDesc quality;
    SceneSettingsDesc settings;
    std::vector<SliderDesc> sliders;
    std::vector<CheckboxDesc> checkboxes;
    std::vector<SelectorDesc> selectors;
  };

  // Load a SceneDescriptor from a JSON file.
  // Returns true on success, fills 'desc'. On failure, prints error and returns false.
  bool LoadSceneDescriptor(const std::string& path, SceneDescriptor& desc);

  // Save a SceneDescriptor to a JSON file (for authoring/export).
  bool SaveSceneDescriptor(const std::string& path, const SceneDescriptor& desc);

} // namespace t800

#endif
