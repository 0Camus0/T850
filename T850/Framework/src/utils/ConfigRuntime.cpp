#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <pch.h>

#include <utils/ConfigRuntime.h>
#include <utils/ResourceLocator.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4267)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace t850::config {

namespace {

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool ReadTextFile(const std::filesystem::path& path, std::string& content) {
  return ResourceLocator::Instance().ReadText(path.string(), content);
}

std::filesystem::path ResolveConfigPath(const std::string& value, const char* executablePath) {
  std::filesystem::path requested = StripQuotes(value);
  if (requested.is_absolute() || std::filesystem::exists(requested)) return requested;

  std::filesystem::path exeDir = std::filesystem::path(executablePath).parent_path();
  if (!exeDir.empty()) {
    std::filesystem::path fromExe = exeDir / requested;
    if (std::filesystem::exists(fromExe)) return fromExe;
  }
  return requested;
}

bool IsKnownGraphicsApi(const std::string& value) {
  std::string lowered = ToLower(value);
  return lowered == "gl" || lowered == "opengl"
      || lowered == "d3d12" || lowered == "dx12"
      || lowered == "d3d11" || lowered == "dx11"
      || lowered == "vulkan" || lowered == "vk";
}

bool TryParseInt(const char* rawValue, int& out) {
  std::string value = StripQuotes(rawValue ? rawValue : "");
  if (value.empty()) return false;
  char* end = nullptr;
  errno = 0;
  long parsed = std::strtol(value.c_str(), &end, 10);
  if (errno == ERANGE || end == value.c_str() || *end != '\0') return false;
  if (parsed < (std::numeric_limits<int>::min)() || parsed > (std::numeric_limits<int>::max)()) return false;
  out = static_cast<int>(parsed);
  return true;
}

bool TryParseFloat(const char* rawValue, float& out) {
  std::string value = StripQuotes(rawValue ? rawValue : "");
  if (value.empty()) return false;
  char* end = nullptr;
  errno = 0;
  float parsed = std::strtof(value.c_str(), &end);
  if (errno == ERANGE || end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
  out = parsed;
  return true;
}

bool ReadIntArgument(const std::string& arg, int argc, char** argv, int& index, int& out) {
  if (index + 1 >= argc) {
    std::cerr << "[config] Missing value for " << arg << "; ignoring option.\n";
    return false;
  }
  const char* value = argv[++index];
  if (!TryParseInt(value, out)) {
    std::cerr << "[config] Invalid integer for " << arg << ": '" << value << "'; ignoring option.\n";
    return false;
  }
  return true;
}

bool ReadFloatArgument(const std::string& arg, int argc, char** argv, int& index, float& out) {
  if (index + 1 >= argc) {
    std::cerr << "[config] Missing value for " << arg << "; ignoring option.\n";
    return false;
  }
  const char* value = argv[++index];
  if (!TryParseFloat(value, out)) {
    std::cerr << "[config] Invalid number for " << arg << ": '" << value << "'; ignoring option.\n";
    return false;
  }
  return true;
}

void WarnConfigAdjusted(const std::string& field, const std::string& reason) {
  std::cerr << "[config] Adjusted " << field << ": " << reason << "\n";
}

} // namespace

std::string StripQuotes(std::string value) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

int ParseLogLevel(const std::string& value, int fallback) {
  std::string lowered = ToLower(value);
  if (lowered == "error" || lowered == "0") return 0;
  if (lowered == "info" || lowered == "1") return 1;
  if (lowered == "debug" || lowered == "2") return 2;
  if (lowered == "verbose" || lowered == "3") return 3;
  if (lowered == "trace" || lowered == "4") return 4;
  return fallback;
}

Config::GLOffscreenFlushMode ParseGLOffscreenFlushMode(const std::string& value, Config::GLOffscreenFlushMode fallback) {
  std::string lowered = ToLower(value);
  if (lowered == "frame" || lowered == "current" || lowered == "0") return Config::GLOffscreenFlushMode::Frame;
  if (lowered == "wait" || lowered == "reuse" || lowered == "1") return Config::GLOffscreenFlushMode::Wait;
  if (lowered == "none" || lowered == "off" || lowered == "2") return Config::GLOffscreenFlushMode::None;
  return fallback;
}

Config::CullingLoadMode ParseCullingLoadMode(const std::string& value, Config::CullingLoadMode fallback) {
  std::string lowered = ToLower(value);
  if (lowered == "enabled" || lowered == "enable" || lowered == "full" || lowered == "fullonload" || lowered == "on" || lowered == "1") {
    return Config::CullingLoadMode::FullOnLoad;
  }
  if (lowered == "lazy" || lowered == "deferred" || lowered == "2") return Config::CullingLoadMode::Lazy;
  if (lowered == "disabled" || lowered == "disable" || lowered == "off" || lowered == "none" || lowered == "0") {
    return Config::CullingLoadMode::Disabled;
  }
  return fallback;
}

GraphicsApi::E ParseGraphicsApi(const std::string& value, GraphicsApi::E fallback) {
  std::string lowered = ToLower(value);
  if (lowered == "gl" || lowered == "opengl") return GraphicsApi::OPENGL;
  if (lowered == "d3d12" || lowered == "dx12") return GraphicsApi::D3D12;
  if (lowered == "d3d11" || lowered == "dx11") return GraphicsApi::D3D11;
  if (lowered == "vulkan" || lowered == "vk") return GraphicsApi::VULKAN;
  return fallback;
}

const char* ApiTag(GraphicsApi::E api) {
  return (api == GraphicsApi::OPENGL) ? "gl"
       : (api == GraphicsApi::D3D12)  ? "d3d12"
       : (api == GraphicsApi::VULKAN) ? "vulkan"
       : "d3d11";
}

const char* CullingLoadModeTag(Config::CullingLoadMode mode) {
  return mode == Config::CullingLoadMode::FullOnLoad ? "full"
       : mode == Config::CullingLoadMode::Lazy       ? "lazy"
       : "disabled";
}

void ApplyConfigJson(const RuntimeConfigJson& json, Config& cfg) {
  if (json.api) cfg.api = *json.api;
  if (json.width) cfg.width = *json.width;
  if (json.height) cfg.height = *json.height;
  if (json.fullscreen) cfg.flags.fullscreen = *json.fullscreen;
  if (json.scene) cfg.startScene = *json.scene;
  if (json.title) cfg.title = *json.title;
  if (json.model) cfg.modelPath = *json.model;
  if (json.sceneFile) cfg.sceneFilePath = StripQuotes(*json.sceneFile);
  if (json.sceneProfile) cfg.sceneProfile = StripQuotes(*json.sceneProfile);
  if (json.debugFrames) {
    cfg.flags.debugFrames = *json.debugFrames;
    if (*json.debugFrames) cfg.flags.dumpEnabled = true;
  }
  if (json.keepRunning) cfg.flags.keepRunning = *json.keepRunning;
  if (json.replaySnapshotPath) cfg.replaySnapshotPath = StripQuotes(*json.replaySnapshotPath);
  if (json.dumpEnabled) cfg.flags.dumpEnabled = *json.dumpEnabled;
  if (json.dumpByFrame) cfg.flags.dumpByFrame = *json.dumpByFrame;
  if (json.dumpFrame) cfg.dumpFrame = *json.dumpFrame;
  if (json.dumpSeconds) cfg.dumpSeconds = *json.dumpSeconds;
  if (json.gui) cfg.flags.guiOnStart = *json.gui;
  if (json.logLevel) cfg.logLevel = ParseLogLevel(*json.logLevel, cfg.logLevel);
  if (json.logLevelValue) cfg.logLevel = *json.logLevelValue;
  if (json.logFile) cfg.logFile = *json.logFile;
  if (json.d3d12Debug) cfg.flags.d3d12Debug = *json.d3d12Debug;
  if (json.profile) cfg.flags.profile = *json.profile;
  if (json.profileFrames) cfg.profileFrames = *json.profileFrames;
  if (json.autoStartRagdoll) cfg.flags.autoStartRagdoll = *json.autoStartRagdoll;
  if (json.dumpMatrices) cfg.flags.dumpMatrices = *json.dumpMatrices;
  if (json.dumpMatricesFrames) cfg.dumpMatricesFrames = *json.dumpMatricesFrames;
  if (json.benchmark) cfg.flags.benchmark = *json.benchmark;
  if (json.benchmarkMatrix) { cfg.flags.benchmarkMatrix = *json.benchmarkMatrix; if (*json.benchmarkMatrix) cfg.flags.benchmark = true; }
  if (json.cullDisabled && *json.cullDisabled) cfg.cullingLoadMode = Config::CullingLoadMode::Disabled;
  if (json.cullingMode) cfg.cullingLoadMode = ParseCullingLoadMode(*json.cullingMode, cfg.cullingLoadMode);
  cfg.flags.cullDisabled = cfg.cullingLoadMode == Config::CullingLoadMode::Disabled;
  if (json.benchmarkOutputPath) cfg.benchmarkOutputPath = *json.benchmarkOutputPath;
  if (json.benchmarkReportPath) cfg.benchmarkReportPath = *json.benchmarkReportPath;
  if (json.benchmarkFinalFrameDump) cfg.flags.benchmarkFinalFrameDump = *json.benchmarkFinalFrameDump;
  if (json.benchmarkFinalFrameDir) cfg.benchmarkFinalFrameDir = StripQuotes(*json.benchmarkFinalFrameDir);
  if (json.benchmarkSeconds) cfg.benchmarkDurationSeconds = *json.benchmarkSeconds;
  if (json.benchmarkDurationSeconds) cfg.benchmarkDurationSeconds = *json.benchmarkDurationSeconds;
  if (json.benchmarkFrameLimit) cfg.benchmarkFrameLimit = *json.benchmarkFrameLimit;
  if (json.benchmarkFixedDt) cfg.benchmarkFixedDt = *json.benchmarkFixedDt;
  if (json.offscreen) cfg.flags.offscreen = *json.offscreen;
  if (json.offscreenDebug) cfg.flags.offscreenDebug = *json.offscreenDebug;
  if (json.glOffscreenFlushMode) cfg.glOffscreenFlushMode = ParseGLOffscreenFlushMode(*json.glOffscreenFlushMode, cfg.glOffscreenFlushMode);
  if (json.dumpShaderPermutations) cfg.flags.dumpShaderPermutations = *json.dumpShaderPermutations;
  if (json.shaderPermutationOutput) cfg.shaderPermutationOutputPath = StripQuotes(*json.shaderPermutationOutput);
  if (json.runtimeTelemetry) cfg.flags.runtimeTelemetry = *json.runtimeTelemetry;
  if (json.runtimeTelemetryFrequencyFrames) cfg.runtimeTelemetryFrequencyFrames = *json.runtimeTelemetryFrequencyFrames;
  if (json.runtimeTelemetryOutputPath) cfg.runtimeTelemetryOutputPath = StripQuotes(*json.runtimeTelemetryOutputPath);
  if (json.orbitYaw) {
    cfg.orbitYawOverride = true;
    cfg.orbitYaw = *json.orbitYaw;
  }

  if (json.display) {
    const DisplayJson& display = *json.display;
    if (display.width) cfg.width = *display.width;
    if (display.height) cfg.height = *display.height;
    if (display.fullscreen) cfg.flags.fullscreen = *display.fullscreen;
    if (display.scene) cfg.startScene = *display.scene;
    if (display.model) cfg.modelPath = *display.model;
    if (display.sceneFile) cfg.sceneFilePath = StripQuotes(*display.sceneFile);
    if (display.sceneProfile) cfg.sceneProfile = StripQuotes(*display.sceneProfile);
    if (display.title) cfg.title = *display.title;
  }

  if (json.replaySnapshot) {
    const ReplaySnapshotJson& replay = *json.replaySnapshot;
    if (replay.enabled && !*replay.enabled) cfg.replaySnapshotPath.clear();
    if ((!replay.enabled || *replay.enabled) && replay.path) cfg.replaySnapshotPath = StripQuotes(*replay.path);
  }

  if (json.dump) {
    const DumpJson& dump = *json.dump;
    if (dump.enabled) cfg.flags.dumpEnabled = *dump.enabled;
    if (dump.trigger) cfg.flags.dumpByFrame = (ToLower(*dump.trigger) == "frame");
    if (dump.frame) cfg.dumpFrame = *dump.frame;
    if (dump.seconds) cfg.dumpSeconds = *dump.seconds;
  }

  if (json.devTools) {
    const DevToolsJson& devTools = *json.devTools;
    if (devTools.gui) cfg.flags.guiOnStart = *devTools.gui;
    if (devTools.logLevel) cfg.logLevel = ParseLogLevel(*devTools.logLevel, cfg.logLevel);
    if (devTools.logLevelValue) cfg.logLevel = *devTools.logLevelValue;
    if (devTools.logFile) cfg.logFile = *devTools.logFile;
    if (devTools.logToFile && *devTools.logToFile && cfg.logFile.empty()) cfg.logFile = "logs/T850.log";
    if (devTools.d3d12Debug) cfg.flags.d3d12Debug = *devTools.d3d12Debug;
    if (devTools.profile) cfg.flags.profile = *devTools.profile;
    if (devTools.profileFrames) cfg.profileFrames = *devTools.profileFrames;
    if (devTools.autoStartRagdoll) cfg.flags.autoStartRagdoll = *devTools.autoStartRagdoll;
    if (devTools.dumpMatrices) cfg.flags.dumpMatrices = *devTools.dumpMatrices;
    if (devTools.dumpMatricesFrames) cfg.dumpMatricesFrames = *devTools.dumpMatricesFrames;
    if (devTools.benchmark) cfg.flags.benchmark = *devTools.benchmark;
    if (devTools.benchmarkMatrix) { cfg.flags.benchmarkMatrix = *devTools.benchmarkMatrix; if (*devTools.benchmarkMatrix) cfg.flags.benchmark = true; }
    if (devTools.cullDisabled && *devTools.cullDisabled) cfg.cullingLoadMode = Config::CullingLoadMode::Disabled;
    if (devTools.cullingMode) cfg.cullingLoadMode = ParseCullingLoadMode(*devTools.cullingMode, cfg.cullingLoadMode);
    cfg.flags.cullDisabled = cfg.cullingLoadMode == Config::CullingLoadMode::Disabled;
    if (devTools.benchmarkOutputPath) cfg.benchmarkOutputPath = *devTools.benchmarkOutputPath;
    if (devTools.benchmarkReportPath) cfg.benchmarkReportPath = *devTools.benchmarkReportPath;
    if (devTools.benchmarkFinalFrameDump) cfg.flags.benchmarkFinalFrameDump = *devTools.benchmarkFinalFrameDump;
    if (devTools.benchmarkFinalFrameDir) cfg.benchmarkFinalFrameDir = StripQuotes(*devTools.benchmarkFinalFrameDir);
    if (devTools.benchmarkSeconds) cfg.benchmarkDurationSeconds = *devTools.benchmarkSeconds;
    if (devTools.benchmarkDurationSeconds) cfg.benchmarkDurationSeconds = *devTools.benchmarkDurationSeconds;
    if (devTools.benchmarkFrameLimit) cfg.benchmarkFrameLimit = *devTools.benchmarkFrameLimit;
    if (devTools.benchmarkFixedDt) cfg.benchmarkFixedDt = *devTools.benchmarkFixedDt;
    if (devTools.offscreen) cfg.flags.offscreen = *devTools.offscreen;
    if (devTools.offscreenDebug) cfg.flags.offscreenDebug = *devTools.offscreenDebug;
    if (devTools.glOffscreenFlushMode) cfg.glOffscreenFlushMode = ParseGLOffscreenFlushMode(*devTools.glOffscreenFlushMode, cfg.glOffscreenFlushMode);
    if (devTools.dumpShaderPermutations) cfg.flags.dumpShaderPermutations = *devTools.dumpShaderPermutations;
    if (devTools.shaderPermutationOutput) cfg.shaderPermutationOutputPath = StripQuotes(*devTools.shaderPermutationOutput);
  }

  if (json.telemetry) {
    const RuntimeTelemetryJson& telemetry = *json.telemetry;
    if (telemetry.enabled) cfg.flags.runtimeTelemetry = *telemetry.enabled;
    if (telemetry.frequencyFrames) cfg.runtimeTelemetryFrequencyFrames = *telemetry.frequencyFrames;
    if (telemetry.outputPath) cfg.runtimeTelemetryOutputPath = StripQuotes(*telemetry.outputPath);
    if (telemetry.output) cfg.runtimeTelemetryOutputPath = StripQuotes(*telemetry.output);
  }
}

bool LoadRuntimeConfig(const std::filesystem::path& path, Config& cfg) {
  std::string content;
  if (!ReadTextFile(path, content)) {
    std::cerr << "[config] Could not open '" << path.string() << "'; using defaults/CLI.\n";
    return false;
  }

  RuntimeConfigJson json;
  auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(json, content);
  if (err) {
    std::cerr << "[config] Invalid JSON in '" << path.string() << "': "
              << glz::format_error(err, content) << "\n";
    return false;
  }

  ApplyConfigJson(json, cfg);
  return true;
}

bool ValidateConfig(Config& cfg) {
  bool valid = true;
  const Config defaults;
  constexpr int kMaxDimension = 16384;

  if (!IsKnownGraphicsApi(cfg.api)) {
    WarnConfigAdjusted("api", "unsupported value '" + cfg.api + "', using '" + defaults.api + "'");
    cfg.api = defaults.api;
    valid = false;
  } else {
    cfg.api = ApiTag(ParseGraphicsApi(cfg.api, GraphicsApi::D3D11));
  }

  if (cfg.width <= 0) {
    WarnConfigAdjusted("width", "must be positive, using " + std::to_string(defaults.width));
    cfg.width = defaults.width;
    valid = false;
  } else if (cfg.width > kMaxDimension) {
    WarnConfigAdjusted("width", "too large, clamping to " + std::to_string(kMaxDimension));
    cfg.width = kMaxDimension;
    valid = false;
  }

  if (cfg.height <= 0) {
    WarnConfigAdjusted("height", "must be positive, using " + std::to_string(defaults.height));
    cfg.height = defaults.height;
    valid = false;
  } else if (cfg.height > kMaxDimension) {
    WarnConfigAdjusted("height", "too large, clamping to " + std::to_string(kMaxDimension));
    cfg.height = kMaxDimension;
    valid = false;
  }

  if (cfg.startScene < 0) {
    WarnConfigAdjusted("startScene", "must be non-negative, using 0");
    cfg.startScene = 0;
    valid = false;
  }

  if (cfg.title.empty()) {
    WarnConfigAdjusted("title", "must not be empty, using default title");
    cfg.title = defaults.title;
    valid = false;
  }

  cfg.sceneFilePath = StripQuotes(cfg.sceneFilePath);
  if (!cfg.sceneFilePath.empty()) {
    cfg.modelPath.clear();
  }

  if (cfg.modelPath.empty() && cfg.sceneFilePath.empty()) {
    WarnConfigAdjusted("modelPath", "must not be empty, using '" + defaults.modelPath + "'");
    cfg.modelPath = defaults.modelPath;
    valid = false;
  } else if (!cfg.modelPath.empty()) {
    cfg.modelPath = StripQuotes(cfg.modelPath);
  }

  if (cfg.logLevel < 0 || cfg.logLevel > 4) {
    int clamped = (std::max)(0, (std::min)(4, cfg.logLevel));
    WarnConfigAdjusted("logLevel", "must be 0..4, clamping to " + std::to_string(clamped));
    cfg.logLevel = clamped;
    valid = false;
  }

  if (cfg.profileFrames <= 0) {
    WarnConfigAdjusted("profileFrames", "must be positive, using " + std::to_string(defaults.profileFrames));
    cfg.profileFrames = defaults.profileFrames;
    valid = false;
  }

  if (cfg.dumpMatricesFrames < 0) {
    WarnConfigAdjusted("dumpMatricesFrames", "must be non-negative, using 0");
    cfg.dumpMatricesFrames = 0;
    valid = false;
  }

  if (cfg.runtimeTelemetryFrequencyFrames < 0) {
    WarnConfigAdjusted("runtimeTelemetryFrequencyFrames", "must be non-negative, using 60");
    cfg.runtimeTelemetryFrequencyFrames = defaults.runtimeTelemetryFrequencyFrames;
    valid = false;
  }

  if (cfg.benchmarkDurationSeconds < 0) {
    WarnConfigAdjusted("benchmarkDurationSeconds", "must be non-negative, using 0");
    cfg.benchmarkDurationSeconds = 0;
    valid = false;
  }

  if (cfg.benchmarkFrameLimit < 0) {
    WarnConfigAdjusted("benchmarkFrameLimit", "must be non-negative, using 0");
    cfg.benchmarkFrameLimit = 0;
    valid = false;
  }

  if (cfg.benchmarkFixedDt < 0.0f || !std::isfinite(cfg.benchmarkFixedDt)) {
    WarnConfigAdjusted("benchmarkFixedDt", "must be finite and non-negative, using 0");
    cfg.benchmarkFixedDt = 0.0f;
    valid = false;
  }

  if (cfg.flags.dumpEnabled && cfg.flags.dumpByFrame && cfg.dumpFrame < 0) {
    cfg.flags.dumpEnabled = false;
    valid = false;
  }

  if (cfg.flags.dumpEnabled && !cfg.flags.dumpByFrame && !cfg.flags.debugFrames && cfg.dumpSeconds < 0.0f) {
    WarnConfigAdjusted("dumpSeconds", "timed dump requested with negative seconds, disabling dump");
    cfg.flags.dumpEnabled = false;
    valid = false;
  }

  if (cfg.dumpSeconds >= 0.0f && !std::isfinite(cfg.dumpSeconds)) {
    WarnConfigAdjusted("dumpSeconds", "must be finite, using default");
    cfg.dumpSeconds = defaults.dumpSeconds;
    valid = false;
  }

  if (cfg.orbitYawOverride && !std::isfinite(cfg.orbitYaw)) {
    WarnConfigAdjusted("orbitYaw", "must be finite, clearing override");
    cfg.orbitYawOverride = false;
    cfg.orbitYaw = defaults.orbitYaw;
    valid = false;
  }

  if (cfg.flags.offscreenDebug && !cfg.flags.offscreen) {
    WarnConfigAdjusted("offscreenDebug", "requires offscreen mode, disabling debug dumps");
    cfg.flags.offscreenDebug = false;
    valid = false;
  }

  cfg.logFile = StripQuotes(cfg.logFile);
  cfg.replaySnapshotPath = StripQuotes(cfg.replaySnapshotPath);
  cfg.benchmarkOutputPath = StripQuotes(cfg.benchmarkOutputPath);
  cfg.benchmarkReportPath = StripQuotes(cfg.benchmarkReportPath);
  cfg.benchmarkFinalFrameDir = StripQuotes(cfg.benchmarkFinalFrameDir);
  if (cfg.flags.benchmarkMatrix) {
    cfg.flags.benchmark = true;
    cfg.startScene = 1;
  }
  cfg.runtimeTelemetryOutputPath = StripQuotes(cfg.runtimeTelemetryOutputPath);
  if (cfg.flags.runtimeTelemetry && cfg.runtimeTelemetryOutputPath.empty()) {
    cfg.runtimeTelemetryOutputPath = defaults.runtimeTelemetryOutputPath;
  }
  cfg.flags.cullDisabled = cfg.cullingLoadMode == Config::CullingLoadMode::Disabled;
  return valid;
}

std::optional<std::filesystem::path> FindConfigArgument(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      return ResolveConfigPath(argv[i + 1], argv[0]);
    }
  }
  return std::nullopt;
}

bool HasHelpArgument(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") return true;
  }
  return false;
}

void ApplyCommandLine(int argc, char** argv, Config& cfg) {
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      ++i;
    }
    else if (arg == "--api" && i + 1 < argc) {
      cfg.api = argv[++i];
    }
    else if (arg == "--dump-frame" || arg == "--dumpFrame" || arg == "--dumpSnapshot-frame") {
      int value = 0;
      if (ReadIntArgument(arg, argc, argv, i, value)) {
        cfg.flags.dumpEnabled = true;
        cfg.flags.dumpByFrame = true;
        cfg.dumpFrame = value;
      }
    }
    else if (arg == "--dumpSnapshot-seconds") {
      float value = 0.0f;
      if (ReadFloatArgument(arg, argc, argv, i, value)) {
        cfg.flags.dumpEnabled = true;
        cfg.flags.dumpByFrame = false;
        cfg.dumpSeconds = value;
      }
    }
    else if (arg == "--debugFrames") {
      cfg.flags.debugFrames = true;
      cfg.flags.dumpEnabled = true;
    }
    else if (arg == "--replaySnapshot" && i + 1 < argc) {
      cfg.replaySnapshotPath = StripQuotes(argv[++i]);
    }
    else if (arg == "--keepRunning") {
      cfg.flags.keepRunning = true;
    }
    else if (arg == "--width") {
      int value = 0;
      if (ReadIntArgument(arg, argc, argv, i, value)) cfg.width = value;
    }
    else if (arg == "--height") {
      int value = 0;
      if (ReadIntArgument(arg, argc, argv, i, value)) cfg.height = value;
    }
    else if (arg == "--scene") {
      int value = 0;
      if (ReadIntArgument(arg, argc, argv, i, value)) cfg.startScene = value;
    }
    else if (arg == "--fullscreen") {
      cfg.flags.fullscreen = true;
    }
    else if (arg == "--gui") {
      cfg.flags.guiOnStart = true;
    }
    else if (arg == "--logLevel" && i + 1 < argc) {
      cfg.logLevel = ParseLogLevel(argv[++i], cfg.logLevel);
    }
    else if (arg == "--logFile" && i + 1 < argc) {
      cfg.logFile = argv[++i];
    }
    else if (arg == "--d3d12debug") {
      cfg.flags.d3d12Debug = true;
    }
    else if (arg == "--profile") {
      cfg.flags.profile = true;
    }
    else if (arg == "--profileFrames") {
      int value = 0;
      if (ReadIntArgument(arg, argc, argv, i, value)) cfg.profileFrames = value;
    }
    else if (arg == "--telemetry" || arg == "--runtimeTelemetry") {
      cfg.flags.runtimeTelemetry = true;
    }
    else if (arg == "--telemetryFrequencyFrames" || arg == "--runtimeTelemetryFrequencyFrames") {
      int value = 0;
      if (ReadIntArgument(arg, argc, argv, i, value)) cfg.runtimeTelemetryFrequencyFrames = value;
    }
    else if ((arg == "--telemetryOutput" || arg == "--runtimeTelemetryOutput") && i + 1 < argc) {
      cfg.runtimeTelemetryOutputPath = StripQuotes(argv[++i]);
    }
    else if (arg == "--autoStartRagdoll") {
      cfg.flags.autoStartRagdoll = true;
    }
    else if (arg == "--model" && i + 1 < argc) {
      cfg.modelPath = argv[++i];
      cfg.sceneFilePath.clear();
    }
    else if ((arg == "--sceneFile" || arg == "--t8scene") && i + 1 < argc) {
      cfg.sceneFilePath = StripQuotes(argv[++i]);
      cfg.modelPath.clear();
    }
    else if (arg == "--sceneProfile" && i + 1 < argc) {
      cfg.sceneProfile = StripQuotes(argv[++i]);
    }
    else if (arg == "--orbitYaw") {
      float value = 0.0f;
      if (ReadFloatArgument(arg, argc, argv, i, value)) {
        cfg.orbitYawOverride = true;
        cfg.orbitYaw = value;
      }
    }
    else if (arg == "--dumpMatrices") {
      int value = 0;
      if (ReadIntArgument(arg, argc, argv, i, value)) {
        cfg.flags.dumpMatrices = true;
        cfg.dumpMatricesFrames = value;
      }
    }
    else if (arg == "--benchmark") {
      cfg.flags.benchmark = true;
    }
    else if (arg == "--benchmarkMatrix") {
      cfg.flags.benchmark = true;
      cfg.flags.benchmarkMatrix = true;
    }
    else if (arg == "--benchmarkOutput" && i + 1 < argc) {
      cfg.flags.benchmark = true;
      cfg.benchmarkOutputPath = argv[++i];
    }
    else if (arg == "--benchmarkReport" && i + 1 < argc) {
      cfg.flags.benchmark = true;
      cfg.flags.benchmarkMatrix = true;
      cfg.benchmarkReportPath = argv[++i];
    }
    else if (arg == "--benchmarkFinalFrameDump" || arg == "--benchmarkDumpFinalFrame" || arg == "--benchmarkCaptureFinalFrame") {
      cfg.flags.benchmark = true;
      cfg.flags.benchmarkFinalFrameDump = true;
    }
    else if ((arg == "--benchmarkFinalFrameDir" || arg == "--benchmarkFinalFramePath") && i + 1 < argc) {
      cfg.flags.benchmark = true;
      cfg.flags.benchmarkFinalFrameDump = true;
      cfg.benchmarkFinalFrameDir = StripQuotes(argv[++i]);
    }
    else if (arg == "--benchmarkSeconds" || arg == "--benchmarkDuration" || arg == "--benchmarkDurationSeconds") {
      int value = 0;
      if (ReadIntArgument(arg, argc, argv, i, value)) {
        cfg.flags.benchmark = true;
        cfg.benchmarkDurationSeconds = value;
      }
    }
    else if (arg == "--benchmarkFrames" || arg == "--benchmarkFrameLimit") {
      int value = 0;
      if (ReadIntArgument(arg, argc, argv, i, value)) {
        cfg.flags.benchmark = true;
        cfg.benchmarkFrameLimit = value;
      }
    }
    else if (arg == "--benchmarkFixedDt" || arg == "--benchmarkFixedDelta") {
      float value = 0.0f;
      if (ReadFloatArgument(arg, argc, argv, i, value)) {
        cfg.flags.benchmark = true;
        cfg.benchmarkFixedDt = value;
      }
    }
    else if (arg == "--cullDisabled") {
      cfg.cullingLoadMode = Config::CullingLoadMode::Disabled;
      cfg.flags.cullDisabled = true;
    }
    else if ((arg == "--culling" || arg == "--cullingMode") && i + 1 < argc) {
      cfg.cullingLoadMode = ParseCullingLoadMode(argv[++i], cfg.cullingLoadMode);
      cfg.flags.cullDisabled = cfg.cullingLoadMode == Config::CullingLoadMode::Disabled;
    }
    else if (arg == "--offscreen") {
      cfg.flags.offscreen = true;
    }
    else if (arg == "--offscreenDebug") {
      cfg.flags.offscreenDebug = true;
    }
    else if (arg == "--glOffscreenFlushMode" && i + 1 < argc) {
      cfg.glOffscreenFlushMode = ParseGLOffscreenFlushMode(argv[++i], cfg.glOffscreenFlushMode);
    }
    else if (arg == "-dumpShaderPermutations" || arg == "--dumpShaderPermutations") {
      cfg.flags.dumpShaderPermutations = true;
    }
    else if ((arg == "-shaderPermutationOutput" || arg == "--shaderPermutationOutput") && i + 1 < argc) {
      cfg.shaderPermutationOutputPath = StripQuotes(argv[++i]);
    }
  }
}

void ConfigureApplicationDesc(const Config& cfg, ApplicationDesc& desc) {
  desc.api = ParseGraphicsApi(cfg.api, GraphicsApi::D3D11);
  desc.height = cfg.height;
  desc.width = cfg.width;
  desc.videoMode = cfg.flags.fullscreen ? VideoMode::FULLSCREEN : VideoMode::WINDOWED;
  desc.title = cfg.title.c_str();
}

void PrintHelp() {
  std::cout
    << "T850 DayScene\n"
    << "Usage: DayScene [options]\n\n"
    << "Configuration:\n"
    << "  -h, --help                         Print this help and exit\n"
    << "  --config <path>                    Load JSON config before applying CLI overrides\n\n"
    << "Renderer/window:\n"
    << "  --api <d3d11|d3d12|vulkan|gl>      Select graphics backend\n"
    << "  --width <pixels>                   Window width\n"
    << "  --height <pixels>                  Window height\n"
    << "  --fullscreen                       Launch fullscreen\n"
    << "  --scene <index>                    Starting scene index\n"
    << "  --model <path>                     glTF model for Sandbox\n"
    << "  --sceneFile <path>                 T8ditor .t8scene file for Sandbox\n"
    << "  --sceneProfile <name>              Override runtime scene profile selection\n\n"
    << "  --orbitYaw <radians>               Override Sandbox orbit yaw after model fit\n\n"
    << "Capture/debug:\n"
    << "  --dump-frame <frame>               Dump render targets at frame\n"
    << "  --dumpFrame <frame>                Alias for --dump-frame\n"
    << "  --dumpSnapshot-frame <frame>       Legacy alias for --dump-frame\n"
    << "  --dumpSnapshot-seconds <seconds>   Dump render targets after elapsed seconds\n"
    << "  --debugFrames                      Enable spacebar dump/exit debug flow\n"
    << "  --replaySnapshot <path>            Replay a snapshot JSON\n"
    << "  --keepRunning                      Keep running after dump\n"
    << "  --dumpMatrices <frames>            Write matrix_dump.csv for N frames\n"
    << "  --benchmark                        Run DayScene tour, write benchmark JSON, then exit\n"
    << "  --benchmarkMatrix                  Run DayScene API/resolution/onscreen-offscreen matrix in process\n"
    << "  --benchmarkOutput <path>            Benchmark JSON output path\n"
    << "  --benchmarkReport <path>            Benchmark matrix Markdown report path\n"
    << "  --benchmarkFinalFrameDump           Save the final offscreen benchmark output RT as PPM\n"
    << "  --benchmarkFinalFrameDir <dir>      Directory for --benchmarkFinalFrameDump captures\n"
    << "  --benchmarkSeconds <seconds>        Run benchmark unthrottled for this many seconds\n"
    << "  --benchmarkFrames <N>               End benchmark after N update frames instead of duration\n"
    << "  --benchmarkFixedDt <seconds>        Use a fixed dt for benchmark updates\n"
    << "  --culling <full|lazy|disabled>      Culling metadata load policy\n"
    << "  --cullDisabled                      Legacy alias for --culling disabled\n"
    << "  --offscreen                         Render the default target to rotating offscreen RTs instead of presenting\n"
    << "  --offscreenDebug                    With --offscreen, dump the offscreen color RT roughly once per second\n"
    << "  --glOffscreenFlushMode <frame|wait|none>  GL offscreen submission pacing mode\n"
    << "  -dumpShaderPermutations, --dumpShaderPermutations\n"
    << "                                      Write requested ShaderKey permutations, then exit after startup\n"
    << "  --shaderPermutationOutput <path>   JSON dictionary path for --dumpShaderPermutations\n"
    << "  --validateGltf <path>              Validate and summarize glTF/GLB, then exit\n\n"
    << "GUI/tools:\n"
    << "  --gui                              Show GUI on startup\n"
    << "\n"
    << "Logging/profiling:\n"
    << "  --logLevel <error|info|debug|verbose|trace|0..4>\n"
    << "  --logFile <path>                   Write log to file\n"
    << "  --d3d12debug                       Enable D3D12 debug layer\n"
    << "  --profile                          Enable GPU+CPU profiling\n"
    << "  --profileFrames <frames>           Frames to profile before report\n"
    << "  --telemetry                        Enable lightweight sampled runtime telemetry\n"
    << "  --telemetryFrequencyFrames <N>     Sample every N frames (0 = every frame)\n"
    << "  --telemetryOutput <path>           Runtime telemetry JSON output path\n"
    << "  --autoStartRagdoll                 Start ragdoll simulation as soon as it is ready\n"
    ;
}

} // namespace t850::config
