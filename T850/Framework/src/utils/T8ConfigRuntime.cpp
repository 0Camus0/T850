#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <pch.h>

#include <utils/T8ConfigRuntime.h>

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

namespace t800::config {

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

GRAPHICS_API::E ParseGraphicsApi(const std::string& value, GRAPHICS_API::E fallback) {
  std::string lowered = ToLower(value);
  if (lowered == "gl" || lowered == "opengl") return GRAPHICS_API::OPENGL;
  if (lowered == "d3d12" || lowered == "dx12") return GRAPHICS_API::D3D12;
  if (lowered == "d3d11" || lowered == "dx11") return GRAPHICS_API::D3D11;
  if (lowered == "vulkan" || lowered == "vk") return GRAPHICS_API::VULKAN;
  return fallback;
}

const char* ApiTag(GRAPHICS_API::E api) {
  return (api == GRAPHICS_API::OPENGL) ? "gl"
       : (api == GRAPHICS_API::D3D12)  ? "d3d12"
       : (api == GRAPHICS_API::VULKAN) ? "vulkan"
       : "d3d11";
}

void ApplyConfigJson(const RuntimeConfigJson& json, t8config& cfg) {
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
  if (json.guiScreenshot) {
    cfg.flags.guiScreenshot = *json.guiScreenshot;
    if (*json.guiScreenshot) cfg.flags.guiOnStart = true;
  }
  if (json.guiScreenshotPath) cfg.guiScreenshotPath = *json.guiScreenshotPath;
  if (json.guiEdit) {
    cfg.flags.guiEdit = *json.guiEdit;
    if (*json.guiEdit) cfg.flags.guiOnStart = true;
  }
  if (json.guiSnap) cfg.flags.guiSnap = *json.guiSnap;
  if (json.guiControlEdit) {
    cfg.flags.guiControlEdit = *json.guiControlEdit;
    if (*json.guiControlEdit) cfg.flags.guiOnStart = true;
  }
  if (json.guiControlTarget) cfg.guiControlTarget = *json.guiControlTarget;
  if (json.logLevel) cfg.logLevel = ParseLogLevel(*json.logLevel, cfg.logLevel);
  if (json.logLevelValue) cfg.logLevel = *json.logLevelValue;
  if (json.logFile) cfg.logFile = *json.logFile;
  if (json.d3d12Debug) cfg.flags.d3d12Debug = *json.d3d12Debug;
  if (json.testGui) cfg.flags.testGui = *json.testGui;
  if (json.createAtlas) cfg.flags.createAtlas = *json.createAtlas;
  if (json.atlasMaxSprite) cfg.atlasMaxSprite = *json.atlasMaxSprite;
  if (json.profile) cfg.flags.profile = *json.profile;
  if (json.profileFrames) cfg.profileFrames = *json.profileFrames;
  if (json.dumpMatrices) cfg.flags.dumpMatrices = *json.dumpMatrices;
  if (json.dumpMatricesFrames) cfg.dumpMatricesFrames = *json.dumpMatricesFrames;

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
    if (devTools.guiScreenshot) {
      cfg.flags.guiScreenshot = *devTools.guiScreenshot;
      if (*devTools.guiScreenshot) cfg.flags.guiOnStart = true;
    }
    if (devTools.guiScreenshotPath) cfg.guiScreenshotPath = *devTools.guiScreenshotPath;
    if (devTools.guiEdit) {
      cfg.flags.guiEdit = *devTools.guiEdit;
      if (*devTools.guiEdit) cfg.flags.guiOnStart = true;
    }
    if (devTools.guiSnap) cfg.flags.guiSnap = *devTools.guiSnap;
    if (devTools.guiControlEdit) {
      cfg.flags.guiControlEdit = *devTools.guiControlEdit;
      if (*devTools.guiControlEdit) cfg.flags.guiOnStart = true;
    }
    if (devTools.guiControlTarget) cfg.guiControlTarget = *devTools.guiControlTarget;
    if (devTools.logLevel) cfg.logLevel = ParseLogLevel(*devTools.logLevel, cfg.logLevel);
    if (devTools.logLevelValue) cfg.logLevel = *devTools.logLevelValue;
    if (devTools.logFile) cfg.logFile = *devTools.logFile;
    if (devTools.logToFile && *devTools.logToFile && cfg.logFile.empty()) cfg.logFile = "logs/T850.log";
    if (devTools.d3d12Debug) cfg.flags.d3d12Debug = *devTools.d3d12Debug;
    if (devTools.testGui) cfg.flags.testGui = *devTools.testGui;
    if (devTools.createAtlas) cfg.flags.createAtlas = *devTools.createAtlas;
    if (devTools.atlasMaxSprite) cfg.atlasMaxSprite = *devTools.atlasMaxSprite;
    if (devTools.profile) cfg.flags.profile = *devTools.profile;
    if (devTools.profileFrames) cfg.profileFrames = *devTools.profileFrames;
    if (devTools.dumpMatrices) cfg.flags.dumpMatrices = *devTools.dumpMatrices;
    if (devTools.dumpMatricesFrames) cfg.dumpMatricesFrames = *devTools.dumpMatricesFrames;
  }
}

bool LoadRuntimeConfig(const std::filesystem::path& path, t8config& cfg) {
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

void ApplyCommandLine(int argc, char** argv, t8config& cfg) {
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
    else if (arg == "--guiScreenshot" && i + 1 < argc) {
      cfg.flags.guiScreenshot = true;
      cfg.flags.guiOnStart = true;
      cfg.guiScreenshotPath = argv[++i];
    }
    else if (arg == "--guiScreenshot") {
      cfg.flags.guiScreenshot = true;
      cfg.flags.guiOnStart = true;
    }
    else if (arg == "--guiEdit") {
      cfg.flags.guiEdit = true;
      cfg.flags.guiOnStart = true;
    }
    else if (arg == "--guiSnap") {
      cfg.flags.guiSnap = true;
    }
    else if (arg == "--guiControlEdit") {
      cfg.flags.guiControlEdit = true;
      cfg.flags.guiOnStart = true;
    }
    else if (arg == "--guiControlTarget" && i + 1 < argc) {
      cfg.guiControlTarget = argv[++i];
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
    else if (arg == "--testGui") {
      cfg.flags.testGui = true;
    }
    else if (arg == "--createAtlas" || arg == "--updateAtlas") {
      cfg.flags.createAtlas = true;
    }
    else if (arg == "--atlasMaxSprite" && i + 1 < argc) {
      cfg.atlasMaxSprite = std::stoi(argv[++i]);
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
    else if (arg == "--dumpMatrices" && i + 1 < argc) {
      cfg.flags.dumpMatrices = true;
      cfg.dumpMatricesFrames = std::stoi(argv[++i]);
    }
  }
}

void ConfigureApplicationDesc(const t8config& cfg, ApplicationDesc& desc) {
  desc.api = ParseGraphicsApi(cfg.api, GRAPHICS_API::D3D11);
  desc.height = cfg.height;
  desc.width = cfg.width;
  desc.videoMode = cfg.flags.fullscreen ? T8_VIDEO_MODE::FULLSCREEN : T8_VIDEO_MODE::WINDOWED;
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
    << "Capture/debug:\n"
    << "  --dump-frame <frame>               Dump render targets at frame\n"
    << "  --dumpSnapshot-seconds <seconds>   Dump render targets after elapsed seconds\n"
    << "  --debugFrames                      Enable spacebar dump/exit debug flow\n"
    << "  --replaySnapshot <path>            Replay a snapshot JSON\n"
    << "  --keepRunning                      Keep running after dump\n"
    << "  --dumpMatrices <frames>            Write matrix_dump.csv for N frames\n"
    << "  --validateGltf <path>              Validate and summarize glTF/GLB, then exit\n\n"
    << "GUI/tools:\n"
    << "  --gui                              Show GUI on startup\n"
    << "  --guiScreenshot [path]             Save GUI screenshot and exit\n"
    << "  --guiEdit                          Enable GUI edit mode\n"
    << "  --guiSnap                          Snap GUI edits to grid\n"
    << "  --guiControlEdit                   Enable GUI control internals edit mode\n"
    << "  --guiControlTarget <name>          slider_knob|selector_control|checkbox_mark\n"
    << "  --testGui                          Run minimal GUI rendering test\n"
    << "  --createAtlas, --updateAtlas       Generate GUI texture atlas and exit\n"
    << "  --atlasMaxSprite <pixels>          Max sprite dimension in atlas\n\n"
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

} // namespace t800::config