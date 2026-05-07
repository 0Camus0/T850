#pragma once

#include <string>
#include <vector>
#include <array>
#include <optional>

// Forward declarations (no engine headers needed in this header)
class Camera;
struct SceneProps;
struct XVECTOR3;

namespace t850 {
  class BaseDriver;
}

namespace t850 {

  // ── JSON-clean data structs (only std types, no engine deps) ──

  struct SnapshotCamJson {
    std::array<float, 3> eye = {0, 0, 0};
    float pitch = 0, roll = 0, yaw = 0;
    float speed = 10.0f;
    float fov = 0, aspect_ratio = 0, near_plane = 0, far_plane = 0;
  };

  struct SnapshotLightJson {
    std::array<float, 3> position = {0, 0, 0};
    std::array<float, 3> color = {1, 1, 1};
    float radius = 100.0f;
  };

  using Mat4Json = std::array<std::array<float, 4>, 4>;

  struct SnapshotMatricesJson {
    Mat4Json camView = {};
    Mat4Json camProjection = {};
    Mat4Json camVP = {};
    Mat4Json lightCamView = {};
    Mat4Json lightCamProjection = {};
    Mat4Json lightCamVP = {};
  };

  // Scene-specific rendering settings
  struct SnapshotScenePropsJson {
    float exposure = 0.3f;
    float bloom_factor = 0.35f;
    std::optional<float> bloom_threshold;
    float tone_map_white_level = 4.0f;
    float luminance_tau = 1.1f;
    float shadow_map_resolution = 2048.0f;
    float pcf_scale = 2.1f;
    float pcf_samples = 3.0f;
    float parallax_low = 10.0f;
    float parallax_high = 18.0f;
    float parallax_height = 0.02f;
    float light_volume_steps = 96.0f;
    float aperture = 120.0f;
    float focal_length = 50.0f;
    float focus_depth = 0.0f;
    float max_coc = 2.5f;
    int active_lights = 1;
    int active_light_camera = 0;
    int toggle_shadow = 1;
    int toggle_ssao = 1;
    int debug_mode = 0;
    std::optional<std::array<float, 3>> ambient_color;
    std::optional<int> toggle_dof;
    std::optional<int> toggle_parallax;
    std::optional<int> toggle_parallax_shadow;
    std::optional<int> toggle_godrays;
    std::optional<bool> auto_focus;
    std::optional<float> shadow_bias;
    std::optional<float> shadow_min;
    std::optional<float> env_factor;
    std::optional<float> ibl_factor;
    std::optional<float> ibl_mip_count;
    std::optional<float> ibl_diffuse_mip_level;
    std::optional<float> ibl_brdf_lut_enabled;
    std::optional<float> godrays_factor;
    std::optional<float> material_emissive_intensity;
    std::optional<float> material_transmission_multiplier;
    std::optional<float> material_refraction_strength;
    std::optional<float> parallax_shadow_min_layers;
    std::optional<float> parallax_shadow_max_layers;
    std::optional<float> parallax_shadow_softness;
    std::optional<float> parallax_shadow_strength;
  };

  // Night scene extra state: omni light cameras + omni position
  struct SnapshotOmniJson {
    std::array<float, 3> omni_light_pos = {0, 0, 0};
    std::vector<SnapshotCamJson> omni_cameras;  // 6 face cameras
  };

  struct SnapshotSkinnedJson {
    bool has_skin = false;
    bool playing = false;
    bool looping = true;
    bool use_slerp = true;
    bool use_quat_skinning = false;
    bool keyframe_mode = false;
    bool wireframe_visible = false;
    bool skeleton_visible = false;
    float animation_speed = 1.0f;
    float local_time = 0.0f;
    float tick_time = 0.0f;
    float ticks_per_second = 0.0f;
    int current_anim_set = 0;
    int num_anim_sets = 0;
    int current_keyframe = 0;
    int total_keyframes = 0;
    int num_bones = 0;
    int bone_texture_width = 0;
    std::vector<Mat4Json> bone_matrices;
    std::vector<float> bone_texture_rgba32f;
  };

  // The complete snapshot file
  struct SnapshotJson {
    int frame = 0;
    int scene = 0;           // 0=Day, 1=Night, 2=Tech
    std::string api;
    float dt = 0;
    SnapshotCamJson cam;
    SnapshotCamJson light_cam;
    std::vector<SnapshotLightJson> lights;
    SnapshotScenePropsJson scene_props;
    std::optional<SnapshotMatricesJson> matrices;
    std::optional<SnapshotOmniJson> omni;  // Night scene only
    std::optional<SnapshotSkinnedJson> skinned;
  };

  // JSON I/O (glaze-based, implemented in FrameDumperIO.cpp)
  bool LoadSnapshot(const std::string& path, SnapshotJson& data);
  bool SaveSnapshot(const std::string& path, const SnapshotJson& data);

  // ── Configuration (mirrors command-line flags) ──

  struct FrameDumperConfig {
    bool  dumpEnabled     = false;
    bool  dumpByFrame     = false;
    int   dumpFrame       = -1;
    float dumpSeconds     = -1.0f;
    bool  debugFrames     = false;   // --debugFrames: spacebar dumps
    bool  keepRunning     = false;   // --keepRunning: don't exit after dump
    std::string replaySnapshotPath;  // --replaySnapshot: path to snapshot.json
    int   sceneIndex      = 0;       // which scene we're in (for snapshot metadata)
  };

  // ── RT dump entry (scene provides the list of RTs to dump) ──

  struct RTDumpEntry {
    int         rtID;
    int         attachment;
    std::string name;
  };

  // ── FrameDumper: snapshot loading, state replay, frame dumping ──

  class FrameDumper {
  public:
    FrameDumper() = default;

    void Init(const FrameDumperConfig& config);

    // ── Replay snapshot ──
    bool HasPendingReplay() const;
    bool LoadReplaySnapshot();

    // Apply snapshot to scene cameras, lights, and props.
    // omniCams/omniLightPos are optional (Night scene only).
    void ApplySnapshot(Camera& cam, Camera& lightCam, SceneProps& props,
                       Camera* omniCams = nullptr, XVECTOR3* omniLightPos = nullptr);
    void UpdateReplayState();
    bool IsReplayActive() const;

    // ── Dump control ──
    void RequestDump();                     // spacebar
    bool ShouldDump(float dt);              // call every frame
    void DumpFrame(BaseDriver* driver,
                   Camera& cam, Camera& lightCam,
                   const SceneProps& props,
                   const std::vector<RTDumpEntry>& rts,
                   float dt,
                   Camera* omniCams = nullptr,
                   const XVECTOR3* omniLightPos = nullptr,
                   const SnapshotSkinnedJson* skinned = nullptr);
    bool ShouldExit() const;

    // ── Query ──
    bool SkipCameraUpdates() const;
    const SnapshotSkinnedJson* GetReplaySkinnedState() const;

  private:
    FrameDumperConfig config_;

    // Replay state
    SnapshotJson replayData_;
    bool hasReplayData_     = false;
    int  replayState_       = 0;     // 0=pending, 1=warmup, 2=done
    int  replayWarmup_      = 0;
    static const int WARMUP_FRAMES = 3;

    // Dump state
    float dumpTimer_        = 0.0f;
    int   dumpFrameCounter_ = 0;
    bool  dumped_           = false;
    bool  debugDumpRequested_ = false;
    bool  shouldExit_       = false;

    // Helpers
    std::string BuildDumpDir(const std::string& apiName);
    void WriteSnapshot(const std::string& path,
                       Camera& cam, Camera& lightCam,
                       const SceneProps& props,
                       int frame, const std::string& apiName, float dt,
                       Camera* omniCams, const XVECTOR3* omniLightPos,
                       const SnapshotSkinnedJson* skinned);
    void LogCameraState(Camera& cam, Camera& lightCam,
                        const SceneProps& props,
                        int frame, const std::string& apiName, float dt);
  };

} // namespace t850
