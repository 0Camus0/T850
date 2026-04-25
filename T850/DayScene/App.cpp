#ifndef NOMINMAX
#define NOMINMAX
#endif

/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include <Config.h>
#ifdef OS_LINUX
    #include <core/LinuxFramework.h>
#elif defined(OS_WINDOWS)
	#include <core/windows/Win32Framework.h>
#endif

#include "Application.h"

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <algorithm>
#include <cctype>

#include <T8_descriptors.h>
#include <core/t8config.h>
#include <utils/Log.h>
#include <utils/GUIAtlasGenerator.h>
#include <debug/T8_Profiler.h>
#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFAccessor.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4267)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

std::vector<std::string> g_args;

t800::AppBase		  *pApp = 0;
t800::RootFramework *pFrameWork = 0;

namespace t850_config {

struct ReplaySnapshotJson {
  std::optional<bool> enabled;
  std::optional<std::string> path;
};

struct DumpJson {
  std::optional<bool> enabled;
  std::optional<std::string> trigger;
  std::optional<int> frame;
  std::optional<float> seconds;
};

struct DisplayJson {
  std::optional<int> width;
  std::optional<int> height;
  std::optional<bool> fullscreen;
  std::optional<int> scene;
  std::optional<std::string> model;
  std::optional<std::string> title;
};

struct DevToolsJson {
  std::optional<bool> gui;
  std::optional<bool> guiScreenshot;
  std::optional<std::string> guiScreenshotPath;
  std::optional<bool> guiEdit;
  std::optional<bool> guiSnap;
  std::optional<bool> guiControlEdit;
  std::optional<std::string> guiControlTarget;
  std::optional<std::string> logLevel;
  std::optional<int> logLevelValue;
  std::optional<bool> logToFile;
  std::optional<std::string> logFile;
  std::optional<bool> d3d12Debug;
  std::optional<bool> testGui;
  std::optional<bool> createAtlas;
  std::optional<int> atlasMaxSprite;
  std::optional<bool> profile;
  std::optional<int> profileFrames;
  std::optional<bool> dumpMatrices;
  std::optional<int> dumpMatricesFrames;
};

struct RuntimeConfigJson {
  std::optional<std::string> api;
  std::optional<int> width;
  std::optional<int> height;
  std::optional<bool> fullscreen;
  std::optional<int> scene;
  std::optional<std::string> title;
  std::optional<std::string> model;
  std::optional<bool> debugFrames;
  std::optional<bool> keepRunning;
  std::optional<std::string> replaySnapshotPath;
  std::optional<bool> dumpEnabled;
  std::optional<bool> dumpByFrame;
  std::optional<int> dumpFrame;
  std::optional<float> dumpSeconds;
  std::optional<bool> gui;
  std::optional<bool> guiScreenshot;
  std::optional<std::string> guiScreenshotPath;
  std::optional<bool> guiEdit;
  std::optional<bool> guiSnap;
  std::optional<bool> guiControlEdit;
  std::optional<std::string> guiControlTarget;
  std::optional<std::string> logLevel;
  std::optional<int> logLevelValue;
  std::optional<std::string> logFile;
  std::optional<bool> d3d12Debug;
  std::optional<bool> testGui;
  std::optional<bool> createAtlas;
  std::optional<int> atlasMaxSprite;
  std::optional<bool> profile;
  std::optional<int> profileFrames;
  std::optional<bool> dumpMatrices;
  std::optional<int> dumpMatricesFrames;

  std::optional<DisplayJson> display;
  std::optional<ReplaySnapshotJson> replaySnapshot;
  std::optional<DumpJson> dump;
  std::optional<DevToolsJson> devTools;
};

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string StripQuotes(std::string value) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

int ParseLogLevel(const std::string& value, int fallback) {
  std::string v = ToLower(value);
  if (v == "error" || v == "0") return 0;
  if (v == "info" || v == "1") return 1;
  if (v == "debug" || v == "2") return 2;
  if (v == "verbose" || v == "3") return 3;
  if (v == "trace" || v == "4") return 4;
  return fallback;
}

t800::GRAPHICS_API::E ParseGraphicsApi(const std::string& value, t800::GRAPHICS_API::E fallback) {
  std::string v = ToLower(value);
  if (v == "gl" || v == "opengl") return t800::GRAPHICS_API::OPENGL;
  if (v == "d3d12" || v == "dx12") return t800::GRAPHICS_API::D3D12;
  if (v == "d3d11" || v == "dx11") return t800::GRAPHICS_API::D3D11;
  if (v == "vulkan" || v == "vk") return t800::GRAPHICS_API::VULKAN;
  return fallback;
}

const char* ApiTag(t800::GRAPHICS_API::E api) {
  return (api == t800::GRAPHICS_API::OPENGL) ? "gl"
       : (api == t800::GRAPHICS_API::D3D12)  ? "d3d12"
       : (api == t800::GRAPHICS_API::VULKAN) ? "vulkan"
       : "d3d11";
}

void ApplyConfigJson(const RuntimeConfigJson& json, t800::t8config& cfg) {
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
    const DisplayJson& d = *json.display;
    if (d.width) cfg.width = *d.width;
    if (d.height) cfg.height = *d.height;
    if (d.fullscreen) cfg.flags.fullscreen = *d.fullscreen;
    if (d.scene) cfg.startScene = *d.scene;
    if (d.model) cfg.modelPath = *d.model;
    if (d.title) cfg.title = *d.title;
  }

  if (json.replaySnapshot) {
    const ReplaySnapshotJson& r = *json.replaySnapshot;
    if (r.enabled && !*r.enabled) cfg.replaySnapshotPath.clear();
    if ((!r.enabled || *r.enabled) && r.path) cfg.replaySnapshotPath = StripQuotes(*r.path);
  }

  if (json.dump) {
    const DumpJson& d = *json.dump;
    if (d.enabled) cfg.flags.dumpEnabled = *d.enabled;
    if (d.trigger) cfg.flags.dumpByFrame = (ToLower(*d.trigger) == "frame");
    if (d.frame) cfg.dumpFrame = *d.frame;
    if (d.seconds) cfg.dumpSeconds = *d.seconds;
  }

  if (json.devTools) {
    const DevToolsJson& d = *json.devTools;
    if (d.gui) cfg.flags.guiOnStart = *d.gui;
    if (d.guiScreenshot) {
      cfg.flags.guiScreenshot = *d.guiScreenshot;
      if (*d.guiScreenshot) cfg.flags.guiOnStart = true;
    }
    if (d.guiScreenshotPath) cfg.guiScreenshotPath = *d.guiScreenshotPath;
    if (d.guiEdit) {
      cfg.flags.guiEdit = *d.guiEdit;
      if (*d.guiEdit) cfg.flags.guiOnStart = true;
    }
    if (d.guiSnap) cfg.flags.guiSnap = *d.guiSnap;
    if (d.guiControlEdit) {
      cfg.flags.guiControlEdit = *d.guiControlEdit;
      if (*d.guiControlEdit) cfg.flags.guiOnStart = true;
    }
    if (d.guiControlTarget) cfg.guiControlTarget = *d.guiControlTarget;
    if (d.logLevel) cfg.logLevel = ParseLogLevel(*d.logLevel, cfg.logLevel);
    if (d.logLevelValue) cfg.logLevel = *d.logLevelValue;
    if (d.logFile) cfg.logFile = *d.logFile;
    if (d.logToFile && *d.logToFile && cfg.logFile.empty()) cfg.logFile = "logs/T850.log";
    if (d.d3d12Debug) cfg.flags.d3d12Debug = *d.d3d12Debug;
    if (d.testGui) cfg.flags.testGui = *d.testGui;
    if (d.createAtlas) cfg.flags.createAtlas = *d.createAtlas;
    if (d.atlasMaxSprite) cfg.atlasMaxSprite = *d.atlasMaxSprite;
    if (d.profile) cfg.flags.profile = *d.profile;
    if (d.profileFrames) cfg.profileFrames = *d.profileFrames;
    if (d.dumpMatrices) cfg.flags.dumpMatrices = *d.dumpMatrices;
    if (d.dumpMatricesFrames) cfg.dumpMatricesFrames = *d.dumpMatricesFrames;
  }
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

bool LoadRuntimeConfig(const std::filesystem::path& path, t800::t8config& cfg) {
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

void ApplyCommandLine(int argc, char** argv, t800::t8config& cfg) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--config" && i + 1 < argc) {
      ++i;
    }
    else if (a == "--api" && i + 1 < argc) {
      cfg.api = argv[++i];
    }
    else if (a == "--dump-frame" && i + 1 < argc) {
      cfg.flags.dumpEnabled = true;
      cfg.flags.dumpByFrame = true;
      cfg.dumpFrame = std::stoi(argv[++i]);
    }
    else if (a == "--dumpSnapshot-seconds" && i + 1 < argc) {
      cfg.flags.dumpEnabled = true;
      cfg.flags.dumpByFrame = false;
      cfg.dumpSeconds = std::stof(argv[++i]);
    }
    else if (a == "--debugFrames") {
      cfg.flags.debugFrames = true;
      cfg.flags.dumpEnabled = true;
    }
    else if (a == "--replaySnapshot" && i + 1 < argc) {
      cfg.replaySnapshotPath = StripQuotes(argv[++i]);
    }
    else if (a == "--keepRunning") {
      cfg.flags.keepRunning = true;
    }
    else if (a == "--width" && i + 1 < argc) {
      cfg.width = std::stoi(argv[++i]);
    }
    else if (a == "--height" && i + 1 < argc) {
      cfg.height = std::stoi(argv[++i]);
    }
    else if (a == "--scene" && i + 1 < argc) {
      cfg.startScene = std::stoi(argv[++i]);
    }
    else if (a == "--fullscreen") {
      cfg.flags.fullscreen = true;
    }
    else if (a == "--gui") {
      cfg.flags.guiOnStart = true;
    }
    else if (a == "--guiScreenshot" && i + 1 < argc) {
      cfg.flags.guiScreenshot = true;
      cfg.flags.guiOnStart = true;
      cfg.guiScreenshotPath = argv[++i];
    }
    else if (a == "--guiScreenshot") {
      cfg.flags.guiScreenshot = true;
      cfg.flags.guiOnStart = true;
    }
    else if (a == "--guiEdit") {
      cfg.flags.guiEdit = true;
      cfg.flags.guiOnStart = true;
    }
    else if (a == "--guiSnap") {
      cfg.flags.guiSnap = true;
    }
    else if (a == "--guiControlEdit") {
      cfg.flags.guiControlEdit = true;
      cfg.flags.guiOnStart = true;
    }
    else if (a == "--guiControlTarget" && i + 1 < argc) {
      cfg.guiControlTarget = argv[++i];
    }
    else if (a == "--logLevel" && i + 1 < argc) {
      cfg.logLevel = ParseLogLevel(argv[++i], cfg.logLevel);
    }
    else if (a == "--logFile" && i + 1 < argc) {
      cfg.logFile = argv[++i];
    }
    else if (a == "--d3d12debug") {
      cfg.flags.d3d12Debug = true;
    }
    else if (a == "--testGui") {
      cfg.flags.testGui = true;
    }
    else if (a == "--createAtlas" || a == "--updateAtlas") {
      cfg.flags.createAtlas = true;
    }
    else if (a == "--atlasMaxSprite" && i + 1 < argc) {
      cfg.atlasMaxSprite = std::stoi(argv[++i]);
    }
#ifdef T8_ENABLE_PROFILER
    else if (a == "--profile") {
      cfg.flags.profile = true;
    }
    else if (a == "--profileFrames" && i + 1 < argc) {
      cfg.profileFrames = std::stoi(argv[++i]);
    }
#endif
    else if (a == "--model" && i + 1 < argc) {
      cfg.modelPath = argv[++i];
    }
    else if (a == "--dumpMatrices" && i + 1 < argc) {
      cfg.flags.dumpMatrices = true;
      cfg.dumpMatricesFrames = std::stoi(argv[++i]);
    }
  }
}

void ConfigureApplicationDesc(const t800::t8config& cfg, t800::ApplicationDesc& desc) {
  desc.api = ParseGraphicsApi(cfg.api, t800::GRAPHICS_API::D3D11);
  desc.height = cfg.height;
  desc.width = cfg.width;
  desc.videoMode = cfg.flags.fullscreen ? t800::T8_VIDEO_MODE::FULLSCREEN : t800::T8_VIDEO_MODE::WINDOWED;
  desc.title = cfg.title.c_str();
}

} // namespace t850_config

using namespace t850_config;

int main(int arg,char ** args){
  t800::t8config defaultConfig;
  t800::g_t8config = defaultConfig;

    for(int i=0;i<arg;i++){
        g_args.push_back( std::string( args[i] ) );
    }

  if (HasHelpArgument(arg, args)) {
    PrintHelp();
    return 0;
  }

  if (auto configPath = FindConfigArgument(arg, args)) {
    LoadRuntimeConfig(*configPath, t800::g_t8config);
  }

  ApplyCommandLine(arg, args, t800::g_t8config);

  t800::ApplicationDesc desc;
  ConfigureApplicationDesc(t800::g_t8config, desc);

  // Parse command-line arguments that intentionally exit before renderer startup.
  for (int i = 1; i < arg; i++) {
    std::string a = args[i];
    if (a == "--config" && i + 1 < arg) {
      ++i;
    }
    else if (a == "--validateGltf" && i + 1 < arg) {
      // Offline parser conformance harness (plan §8). Loads a .gltf or
      // .glb file and prints a structural summary plus the first vertex
      // of each primitive. Exits without booting the renderer, so it
      // works on systems with no graphics device available (e.g. CI).
      std::string path = args[++i];
      // Strip surrounding quotes if present (from launcher).
      if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        path = path.substr(1, path.size() - 2);
      }
      // Bring up logging early enough to surface loader messages.
      t800::Log::Init(t800::Log::LVL_INFO,
                      t800::Log::T8_LOG_BACKEND_CONSOLE, nullptr);

      t800::gltf::Document doc;
      if (!t800::gltf::LoadGLTF(path, doc)) {
        std::cerr << "[validateGltf] FAILED to load '" << path << "'\n";
        t800::Log::Shutdown();
        return 1;
      }
      std::cout << "[validateGltf] " << path << "\n"
                << "  asset.version       : " << doc.asset.version << "\n"
                << "  scenes / nodes      : " << doc.scenes.size()
                << " / " << doc.nodes.size() << "\n"
                << "  meshes / accessors  : " << doc.meshes.size()
                << " / " << doc.accessors.size() << "\n"
                << "  materials / textures: " << doc.materials.size()
                << " / " << doc.textures.size() << "\n"
                << "  images / samplers   : " << doc.images.size()
                << " / " << doc.samplers.size() << "\n"
                << "  skins / animations  : " << doc.skins.size()
                << " / " << doc.animations.size() << "\n"
                << "  buffers / bufViews  : " << doc.buffers.size()
                << " / " << doc.bufferViews.size() << "\n";

      std::size_t primCount = 0;
      for (std::size_t mi = 0; mi < doc.meshes.size(); ++mi) {
        const auto& m = doc.meshes[mi];
        for (std::size_t pi = 0; pi < m.primitives.size(); ++pi, ++primCount) {
          const auto& p = m.primitives[pi];
          std::cout << "  mesh[" << mi << "].prim[" << pi << "]"
                    << " mode=" << p.mode
                    << " mat=" << (p.material ? *p.material : -1);
          if (p.attributes.POSITION >= 0) {
            std::vector<float> pos; int e = 0;
            if (t800::gltf::ReadAccessorFloats(doc, p.attributes.POSITION, pos, &e)
                && e == 3 && pos.size() >= 3) {
              std::cout << " v0=(" << pos[0] << "," << pos[1] << "," << pos[2] << ")";
            }
          }
          std::cout << "\n";
        }
      }
      std::cout << "[validateGltf] OK: " << primCount << " primitives total\n";
      t800::Log::Shutdown();
      return 0;
    }
  }

  // Create log directory if a file path was requested
  if (!t800::g_t8config.logFile.empty()) {
    auto parent = std::filesystem::path(t800::g_t8config.logFile).parent_path();
    if (!parent.empty())
      std::filesystem::create_directories(parent);
  }

  // Determine API display name for session tag
  const char* apiTag = ApiTag(desc.api);

  // Initialize logging
  uint32_t logBackends = t800::Log::T8_LOG_BACKEND_CONSOLE;
#ifdef OS_WINDOWS
  logBackends |= t800::Log::T8_LOG_BACKEND_DEBUG_OUTPUT;  // also emit to VS Output window
#endif
  if (!t800::g_t8config.logFile.empty())
    logBackends |= t800::Log::T8_LOG_BACKEND_FILE;

  t800::Log::Init(
    static_cast<t800::Log::Level>(t800::g_t8config.logLevel),
    logBackends,
    t800::g_t8config.logFile.empty() ? nullptr : t800::g_t8config.logFile.c_str()
  );
  t800::Log::SetSessionTag(apiTag);

  // --createAtlas / --updateAtlas: generate atlas and exit (no window needed)
  if (t800::g_t8config.flags.createAtlas) {
    t800::GUIAtlasGenerator gen;

    // Source textures in Assets/Layouts/Textures/
    const char* layoutDir = "Assets/Layouts/Textures/";
    struct TexDef { const char* name; const char* file; };
    TexDef texDefs[] = {
      {"SliderBar",              "SliderBar.png"},
      {"SliderKnob",             "SliderKnob.png"},
      {"GUI_CheckBox_Box",       "GUI_CheckBox_Box.png"},
      {"GUI_Checkbox_Check",     "GUI_Checkbox_Check.png"},
      {"GUI_DropBar",            "GUI_DropBar.png"},
      {"GUI_DropNonPressedLeft",  "GUI_DropNonPressedLeft.png"},
      {"GUI_DropNonPressedRight", "GUI_DropNonPressedRight.png"},
      {"GUI_DropPressedLeft",     "GUI_DropPressedLeft.png"},
      {"GUI_DropPressedRight",    "GUI_DropPressedRight.png"},
      {"PopupBackground",        "PopupBackground.png"},
      {"PopUpOKNonPressed",      "PopUpOKNonPressed.png"},
      {"PopUpOkPressed",         "PopUpOkPressed.png"},
      {"PopUpCancelNonPressed",  "PopUpCancelNonPressed.png"},
      {"PopUpCancelPressed",     "PopUpCancelPressed.png"},
    };
    for (auto& td : texDefs) {
      gen.AddImage(td.name, std::string(layoutDir) + td.file);
    }

    if (gen.Generate(4096, t800::g_t8config.atlasMaxSprite, 2)) {
      gen.Save("Assets/Layouts/gui_atlas.png", "Assets/Layouts/gui_atlas.json");
      printf("[createAtlas] Atlas generated successfully (%dx%d)\n",
             gen.GetWidth(), gen.GetHeight());
    } else {
      fprintf(stderr, "[createAtlas] Atlas generation failed\n");
    }
    t800::Log::Shutdown();
    return 0;
  }
	pApp = new App;
#ifdef OS_LINUX
    pFrameWork = new t800::LinuxFramework((t800::AppBase*)pApp);
    pFrameWork->InitGlobalVars();
	pFrameWork->OnCreateApplication(desc);
#elif defined(OS_WINDOWS)
	pFrameWork = new t800::Win32Framework((t800::AppBase*)pApp);
	pFrameWork->InitGlobalVars();
	pFrameWork->OnCreateApplication(desc);
	pFrameWork->UpdateApplication();
	pFrameWork->OnDestroyApplication();
#endif

	delete pFrameWork;
	delete pApp;

	t800::Log::Shutdown();

    return 0;
}

