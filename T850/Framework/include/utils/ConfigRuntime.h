#pragma once

#include <Descriptors.h>
#include <core/Config.h>

#include <filesystem>
#include <optional>
#include <string>

namespace t850::config {

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
  std::optional<std::string> logLevel;
  std::optional<int> logLevelValue;
  std::optional<bool> logToFile;
  std::optional<std::string> logFile;
  std::optional<bool> d3d12Debug;
  std::optional<bool> profile;
  std::optional<int> profileFrames;
  std::optional<bool> dumpMatrices;
  std::optional<int> dumpMatricesFrames;
  std::optional<bool> benchmark;
  std::optional<bool> cullDisabled;
  std::optional<std::string> cullingMode;
  std::optional<std::string> benchmarkOutputPath;
  std::optional<bool> offscreen;
  std::optional<bool> offscreenDebug;
  std::optional<std::string> glOffscreenFlushMode;
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
  std::optional<std::string> logLevel;
  std::optional<int> logLevelValue;
  std::optional<std::string> logFile;
  std::optional<bool> d3d12Debug;
  std::optional<bool> profile;
  std::optional<int> profileFrames;
  std::optional<bool> dumpMatrices;
  std::optional<int> dumpMatricesFrames;
  std::optional<bool> benchmark;
  std::optional<bool> cullDisabled;
  std::optional<std::string> cullingMode;
  std::optional<std::string> benchmarkOutputPath;
  std::optional<bool> offscreen;
  std::optional<bool> offscreenDebug;
  std::optional<std::string> glOffscreenFlushMode;
  std::optional<float> orbitYaw;

  std::optional<DisplayJson> display;
  std::optional<ReplaySnapshotJson> replaySnapshot;
  std::optional<DumpJson> dump;
  std::optional<DevToolsJson> devTools;
};

std::string StripQuotes(std::string value);
int ParseLogLevel(const std::string& value, int fallback);
Config::GLOffscreenFlushMode ParseGLOffscreenFlushMode(const std::string& value, Config::GLOffscreenFlushMode fallback);
Config::CullingLoadMode ParseCullingLoadMode(const std::string& value, Config::CullingLoadMode fallback);
GraphicsApi::E ParseGraphicsApi(const std::string& value, GraphicsApi::E fallback);
const char* ApiTag(GraphicsApi::E api);
const char* CullingLoadModeTag(Config::CullingLoadMode mode);

void ApplyConfigJson(const RuntimeConfigJson& json, Config& cfg);
bool LoadRuntimeConfig(const std::filesystem::path& path, Config& cfg);
std::optional<std::filesystem::path> FindConfigArgument(int argc, char** argv);
bool HasHelpArgument(int argc, char** argv);
void ApplyCommandLine(int argc, char** argv, Config& cfg);
void ConfigureApplicationDesc(const Config& cfg, ApplicationDesc& desc);
void PrintHelp();

} // namespace t850::config