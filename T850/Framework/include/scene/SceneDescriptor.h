#ifndef T800_SCENEDESCRIPTOR_H
#define T800_SCENEDESCRIPTOR_H

#include <string>
#include <vector>
#include <array>
#include <optional>

namespace t850 {

  // JSON-serializable scene description structures.
  // These mirror what DayScene/SC_Night/SC_Tech currently hardcode,
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
    int luminance_mode = 0;
    float aperture = 120.0f;
    float focal_length = 50.0f;
    float max_coc = 2.5f;
    std::array<float, 3> ambient_color = {0.8f, 0.8f, 0.8f};
    int active_lights = 5;
    bool shadow_enabled = true;
    bool ssao_enabled = true;
    bool dof_enabled = true;
    bool parallax_enabled = true;
    bool godrays_enabled = true;
    bool auto_focus = true;
    int debug_mode = 0;
    float shadow_bias = 0.000005f;
    float shadow_min = 0.25f;
    float env_factor = 1.0f;
    float ibl_factor = 1.0f;
    float godrays_factor = 1.0f;
    float light_radius_scale = 1.0f;
    float light_intensity_scale = 1.0f;
    float material_emissive_intensity = 1.0f;
    float material_transmission_multiplier = 1.0f;
    float material_refraction_strength = 0.03f;
    float lightmap_intensity = 1.0f;
    std::optional<bool> point_lights_enabled;
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
    bool enabled = true;
  };

  struct SelectorDesc {
    std::string name;
    std::string label;
    std::vector<std::string> options;
    int default_index = 0;
  };

  struct FloatOverrideDesc {
    std::string name;
    float value = 0.0f;
    bool operator==(const FloatOverrideDesc&) const = default;
  };

  struct BoolOverrideDesc {
    std::string name;
    bool value = false;
    bool operator==(const BoolOverrideDesc&) const = default;
  };

  struct IntOverrideDesc {
    std::string name;
    int value = 0;
    bool operator==(const IntOverrideDesc&) const = default;
  };

  struct SandboxOrbitCameraDesc {
    std::array<float, 3> target = {0, 0, 0};
    std::array<float, 3> pan_offset = {0, 0, 0};
    std::array<float, 3> eye = {0, 0, 0};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 5.0f;
    bool operator==(const SandboxOrbitCameraDesc&) const = default;
  };

  struct SandboxCameraDesc {
    int profile = 0;
    std::array<float, 3> eye = {0, 0, 0};
    std::array<float, 3> look = {0, 0, 1};
    std::array<float, 3> up = {0, 1, 0};
    std::array<float, 3> right = {1, 0, 0};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float fov = 45.0f;
    float aspect_ratio = 1.0f;
    float near_plane = 0.01f;
    float far_plane = 1000.0f;
    bool ortho = false;
    float width = 1280.0f;
    float height = 720.0f;
    bool left_handed = true;
    std::optional<SandboxOrbitCameraDesc> orbit;
    bool operator==(const SandboxCameraDesc&) const = default;
  };

  struct SandboxLightOverrideDesc {
    int index = 0;
    std::optional<std::array<float, 3>> position;
    std::optional<std::array<float, 3>> direction;
    std::optional<std::array<float, 3>> color;
    std::optional<float> diameter;
    std::optional<float> intensity;
    std::optional<bool> attach_to_camera;
    bool operator==(const SandboxLightOverrideDesc&) const = default;
  };

  struct SandboxAnimationOverrideDesc {
    int index = 0;
    std::string mesh;
    float anim_speed = 1.0f;
    int anim_select = 0;
    int anim_mode = 0;
    std::optional<int> current_keyframe;
    bool operator==(const SandboxAnimationOverrideDesc&) const = default;
  };

  struct SandboxProfileDesc {
    std::string name;
    std::string platform;
    std::string architecture;
    std::string gpu_family;
    std::string gpu_name_contains;
    std::string model;
    std::vector<FloatOverrideDesc> sliders;
    std::vector<BoolOverrideDesc> checkboxes;
    std::vector<IntOverrideDesc> selectors;
    std::vector<SandboxLightOverrideDesc> lights;
    std::vector<SandboxAnimationOverrideDesc> animations;
    std::optional<std::string> cubemap_path;
    std::optional<SandboxCameraDesc> camera;
    std::optional<SandboxOrbitCameraDesc> orbit_camera;
    std::optional<bool> frustum_culling;
    std::optional<bool> show_culling_debug;
    std::optional<int> current_keyframe;
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
    std::string environment_diffuse_ibl;
    std::string environment_specular_ibl;
    std::string environment_brdf_lut;
    std::string environment_sheen_ibl;
    std::string environment_charlie_lut;
    std::string environment_sheen_e_lut;
    QualityDesc quality;
    SceneSettingsDesc settings;
    std::vector<SliderDesc> sliders;
    std::vector<CheckboxDesc> checkboxes;
    std::vector<SelectorDesc> selectors;
    std::vector<SandboxProfileDesc> profiles;
  };

  // Load a SceneDescriptor from a JSON file.
  // Returns true on success, fills 'desc'. On failure, prints error and returns false.
  bool LoadSceneDescriptor(const std::string& path, SceneDescriptor& desc);

  // Save a SceneDescriptor to a JSON file (for authoring/export).
  bool SaveSceneDescriptor(const std::string& path, const SceneDescriptor& desc);

} // namespace t850

#endif
