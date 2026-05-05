#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <pch.h>

#include <utils/ConfigRuntime.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
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
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  content = buffer.str();
  return true;
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
  if (json.dumpMatrices) cfg.flags.dumpMatrices = *json.dumpMatrices;
  if (json.dumpMatricesFrames) cfg.dumpMatricesFrames = *json.dumpMatricesFrames;
  if (json.benchmark) cfg.flags.benchmark = *json.benchmark;
  if (json.cullDisabled && *json.cullDisabled) cfg.cullingLoadMode = Config::CullingLoadMode::Disabled;
  if (json.cullingMode) cfg.cullingLoadMode = ParseCullingLoadMode(*json.cullingMode, cfg.cullingLoadMode);
  cfg.flags.cullDisabled = cfg.cullingLoadMode == Config::CullingLoadMode::Disabled;
  if (json.benchmarkOutputPath) cfg.benchmarkOutputPath = *json.benchmarkOutputPath;
  if (json.offscreen) cfg.flags.offscreen = *json.offscreen;
  if (json.offscreenDebug) cfg.flags.offscreenDebug = *json.offscreenDebug;
  if (json.glOffscreenFlushMode) cfg.glOffscreenFlushMode = ParseGLOffscreenFlushMode(*json.glOffscreenFlushMode, cfg.glOffscreenFlushMode);
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
    if (devTools.dumpMatrices) cfg.flags.dumpMatrices = *devTools.dumpMatrices;
    if (devTools.dumpMatricesFrames) cfg.dumpMatricesFrames = *devTools.dumpMatricesFrames;
    if (devTools.benchmark) cfg.flags.benchmark = *devTools.benchmark;
    if (devTools.cullDisabled && *devTools.cullDisabled) cfg.cullingLoadMode = Config::CullingLoadMode::Disabled;
    if (devTools.cullingMode) cfg.cullingLoadMode = ParseCullingLoadMode(*devTools.cullingMode, cfg.cullingLoadMode);
    cfg.flags.cullDisabled = cfg.cullingLoadMode == Config::CullingLoadMode::Disabled;
    if (devTools.benchmarkOutputPath) cfg.benchmarkOutputPath = *devTools.benchmarkOutputPath;
    if (devTools.offscreen) cfg.flags.offscreen = *devTools.offscreen;
    if (devTools.offscreenDebug) cfg.flags.offscreenDebug = *devTools.offscreenDebug;
    if (devTools.glOffscreenFlushMode) cfg.glOffscreenFlushMode = ParseGLOffscreenFlushMode(*devTools.glOffscreenFlushMode, cfg.glOffscreenFlushMode);
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
    else if (arg == "--dump-frame" && i + 1 < argc) {
      cfg.flags.dumpEnabled = true;
      cfg.flags.dumpByFrame = true;
      cfg.dumpFrame = std::stoi(argv[++i]);
    }
    else if (arg == "--dumpSnapshot-seconds" && i + 1 < argc) {
      cfg.flags.dumpEnabled = true;
      cfg.flags.dumpByFrame = false;
      cfg.dumpSeconds = std::stof(argv[++i]);
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
    else if (arg == "--width" && i + 1 < argc) {
      cfg.width = std::stoi(argv[++i]);
    }
    else if (arg == "--height" && i + 1 < argc) {
      cfg.height = std::stoi(argv[++i]);
    }
    else if (arg == "--scene" && i + 1 < argc) {
      cfg.startScene = std::stoi(argv[++i]);
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
#ifdef T8_ENABLE_PROFILER
    else if (arg == "--profile") {
      cfg.flags.profile = true;
    }
    else if (arg == "--profileFrames" && i + 1 < argc) {
      cfg.profileFrames = std::stoi(argv[++i]);
    }
#endif
    else if (arg == "--model" && i + 1 < argc) {
      cfg.modelPath = argv[++i];
    }
    else if (arg == "--orbitYaw" && i + 1 < argc) {
      cfg.orbitYawOverride = true;
      cfg.orbitYaw = std::stof(argv[++i]);
    }
    else if (arg == "--dumpMatrices" && i + 1 < argc) {
      cfg.flags.dumpMatrices = true;
      cfg.dumpMatricesFrames = std::stoi(argv[++i]);
    }
    else if (arg == "--benchmark") {
      cfg.flags.benchmark = true;
    }
    else if (arg == "--benchmarkOutput" && i + 1 < argc) {
      cfg.flags.benchmark = true;
      cfg.benchmarkOutputPath = argv[++i];
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
    << "  --model <path>                     glTF model for Sandbox\n\n"
    << "  --orbitYaw <radians>               Override Sandbox orbit yaw after model fit\n\n"
    << "Capture/debug:\n"
    << "  --dump-frame <frame>               Dump render targets at frame\n"
    << "  --dumpSnapshot-seconds <seconds>   Dump render targets after elapsed seconds\n"
    << "  --debugFrames                      Enable spacebar dump/exit debug flow\n"
    << "  --replaySnapshot <path>            Replay a snapshot JSON\n"
    << "  --keepRunning                      Keep running after dump\n"
    << "  --dumpMatrices <frames>            Write matrix_dump.csv for N frames\n"
    << "  --benchmark                        Run DayScene tour, write benchmark JSON, then exit\n"
    << "  --benchmarkOutput <path>            Benchmark JSON output path\n"
    << "  --culling <full|lazy|disabled>      Culling metadata load policy\n"
    << "  --cullDisabled                      Legacy alias for --culling disabled\n"
    << "  --offscreen                         Render the default target to rotating offscreen RTs instead of presenting\n"
    << "  --offscreenDebug                    With --offscreen, dump the offscreen color RT roughly once per second\n"
    << "  --glOffscreenFlushMode <frame|wait|none>  GL offscreen submission pacing mode\n"
    << "  --validateGltf <path>              Validate and summarize glTF/GLB, then exit\n\n"
    << "GUI/tools:\n"
    << "  --gui                              Show GUI on startup\n"
    << "\n"
    << "Logging/profiling:\n"
    << "  --logLevel <error|info|debug|verbose|trace|0..4>\n"
    << "  --logFile <path>                   Write log to file\n"
    << "  --d3d12debug                       Enable D3D12 debug layer\n"
#ifdef T8_ENABLE_PROFILER
    << "  --profile                          Enable GPU+CPU profiling\n"
    << "  --profileFrames <frames>           Frames to profile before report\n"
#endif
    ;
}

} // namespace t850::config