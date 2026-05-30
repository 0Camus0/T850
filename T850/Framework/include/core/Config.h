#pragma once

#include <string>

namespace t850 {

class Config {
public:
  enum class GLOffscreenFlushMode {
    Frame,
    Wait,
    None
  };

  enum class CullingLoadMode {
    FullOnLoad,
    Lazy,
    Disabled
  };

  struct BooleanFlags {
    bool dumpEnabled : 1 = false;
    bool dumpByFrame : 1 = false;
    bool debugFrames : 1 = false;
    bool debugDumpRequested : 1 = false;
    bool keepRunning : 1 = false;
    bool fullscreen : 1 = false;
    bool guiOnStart : 1 = false;
    bool d3d12Debug : 1 = false;
    bool profile : 1 = false;
    bool dumpMatrices : 1 = false;
    bool benchmark : 1 = false;
    bool cullDisabled : 1 = false;
    bool offscreen : 1 = false;
    bool offscreenDebug : 1 = false;
    bool dumpShaderPermutations : 1 = false;
    bool autoStartRagdoll : 1 = false;
    bool runtimeTelemetry : 1 = false;
  } flags;

  std::string api = "d3d11";
  int width = 1280;
  int height = 720;
  std::string title = "T850 Project";

  int dumpFrame = -1;
  float dumpSeconds = -1.0f;
  std::string replaySnapshotPath;
  int startScene = 0;

  int logLevel = 3;
  std::string logFile;

  int profileFrames = 300;
  int dumpMatricesFrames = 0;
  GLOffscreenFlushMode glOffscreenFlushMode = GLOffscreenFlushMode::Frame;
  CullingLoadMode cullingLoadMode = CullingLoadMode::FullOnLoad;
  std::string benchmarkOutputPath;
  std::string modelPath = "Models/DamagedHelmet.glb";
  std::string sceneFilePath;
  std::string sceneProfile;
  std::string shaderPermutationOutputPath = "shader_permutations.json";
  int runtimeTelemetryFrequencyFrames = 60;
  std::string runtimeTelemetryOutputPath = "logs/perf_telemetry.json";
  bool orbitYawOverride = false;
  float orbitYaw = 0.0f;
  int ragdollSimulationSpeedIndex = -1;
};

extern Config g_config;

} // namespace t850
