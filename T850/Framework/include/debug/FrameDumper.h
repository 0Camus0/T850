#pragma once

#include <string>
#include <vector>
#include <array>
#include <optional>

// Forward declarations (no engine headers needed in this header)
class Camera;
struct SceneProps;

namespace t800 {
  class BaseDriver;
}

namespace t800 {

  // ── JSON-clean data structs (only std types, no engine deps) ──

  struct FeedCamJson {
    std::array<float, 3> eye = {0, 0, 0};
    float pitch = 0, roll = 0, yaw = 0;
  };

  struct FeedLightJson {
    std::array<float, 3> position = {0, 0, 0};
    std::array<float, 3> color = {1, 1, 1};
    float radius = 100.0f;
  };

  using Mat4Json = std::array<std::array<float, 4>, 4>;

  struct FeedMatricesJson {
    Mat4Json camView = {};
    Mat4Json camProjection = {};
    Mat4Json camVP = {};
    Mat4Json lightCamView = {};
    Mat4Json lightCamProjection = {};
    Mat4Json lightCamVP = {};
  };

  struct FeedFileJson {
    int frame = 0;
    std::string api;
    float dt = 0;
    FeedCamJson cam;
    FeedCamJson lightCam;
    std::vector<FeedLightJson> lights;
    std::optional<FeedMatricesJson> matrices;
  };

  // JSON I/O (glaze-based, implemented in FrameDumperIO.cpp)
  bool LoadFeedFile(const std::string& path, FeedFileJson& data);
  bool SaveFeedFile(const std::string& path, const FeedFileJson& data);

  // ── Configuration (mirrors command-line flags) ──

  struct FrameDumperConfig {
    bool  dumpEnabled     = false;
    bool  dumpByFrame     = false;
    int   dumpFrame       = -1;
    float dumpSeconds     = -1.0f;
    bool  debugFrames     = false;   // --debugFrames: spacebar dumps
    bool  keepRunning     = false;   // --keepRunning: don't exit after dump
    std::string feedMatricesPath;    // --feedMatrices: path to replay
  };

  // ── RT dump entry (scene provides the list of RTs to dump) ──

  struct RTDumpEntry {
    int         rtID;
    int         attachment;
    std::string name;
  };

  // ── FrameDumper: feed-matrix loading, camera replay, frame dumping ──

  class FrameDumper {
  public:
    FrameDumper() = default;

    void Init(const FrameDumperConfig& config);

    // ── Feed matrices ──
    bool HasPendingFeed() const;
    bool LoadFeedMatrices();
    void ApplyFeedState(Camera& cam, Camera& lightCam, SceneProps& props);
    void UpdateFeedState();
    bool IsFeedActive() const;

    // ── Dump control ──
    void RequestDump();                     // spacebar
    bool ShouldDump(float dt);              // call every frame
    void DumpFrame(BaseDriver* driver,
                   Camera& cam, Camera& lightCam,
                   const SceneProps& props,
                   const std::vector<RTDumpEntry>& rts,
                   float dt);
    bool ShouldExit() const;

    // ── Query ──
    bool SkipCameraUpdates() const;

  private:
    FrameDumperConfig config_;

    // Feed state
    FeedFileJson feedData_;
    bool hasFeedData_       = false;
    int  feedState_         = 0;     // 0=pending, 1=warmup, 2=done
    int  feedWarmup_        = 0;
    static const int WARMUP_FRAMES = 3;

    // Dump state
    float dumpTimer_        = 0.0f;
    int   dumpFrameCounter_ = 0;
    bool  dumped_           = false;
    bool  debugDumpRequested_ = false;
    bool  shouldExit_       = false;

    // Helpers
    std::string BuildDumpDir(const std::string& apiName);
    void WriteMatricesJson(const std::string& path,
                           Camera& cam, Camera& lightCam,
                           const SceneProps& props,
                           int frame, const std::string& apiName, float dt);
    void LogCameraState(Camera& cam, Camera& lightCam,
                        const SceneProps& props,
                        int frame, const std::string& apiName, float dt);
  };

} // namespace t800
