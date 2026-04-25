#pragma once

#include <T8_descriptors.h>
#include <core/t8config.h>

#include <filesystem>
#include <optional>
#include <string>

namespace t800::config {

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

std::string StripQuotes(std::string value);
int ParseLogLevel(const std::string& value, int fallback);
GRAPHICS_API::E ParseGraphicsApi(const std::string& value, GRAPHICS_API::E fallback);
const char* ApiTag(GRAPHICS_API::E api);

void ApplyConfigJson(const RuntimeConfigJson& json, t8config& cfg);
bool LoadRuntimeConfig(const std::filesystem::path& path, t8config& cfg);
std::optional<std::filesystem::path> FindConfigArgument(int argc, char** argv);
bool HasHelpArgument(int argc, char** argv);
void ApplyCommandLine(int argc, char** argv, t8config& cfg);
void ConfigureApplicationDesc(const t8config& cfg, ApplicationDesc& desc);
void PrintHelp();

} // namespace t800::config