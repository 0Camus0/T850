#pragma once

#include <string>

namespace t850 {

class Config {
public:
  struct BooleanFlags {
    bool dumpEnabled : 1 = false;
    bool dumpByFrame : 1 = false;
    bool debugFrames : 1 = false;
    bool debugDumpRequested : 1 = false;
    bool keepRunning : 1 = false;
    bool fullscreen : 1 = false;
    bool guiOnStart : 1 = false;
    bool guiScreenshot : 1 = false;
    bool guiEdit : 1 = false;
    bool guiSnap : 1 = false;
    bool guiControlEdit : 1 = false;
    bool d3d12Debug : 1 = false;
    bool testGui : 1 = false;
    bool createAtlas : 1 = false;
    bool profile : 1 = false;
    bool dumpMatrices : 1 = false;
  } flags;

  std::string api = "d3d11";
  int width = 1280;
  int height = 720;
  std::string title = "T850 Project";

  int dumpFrame = -1;
  float dumpSeconds = -1.0f;
  std::string replaySnapshotPath;
  int startScene = 0;

  std::string guiScreenshotPath = "gui_screenshot.ppm";
  std::string guiControlTarget = "slider_knob";

  int logLevel = 3;
  std::string logFile;

  int atlasMaxSprite = 256;
  int profileFrames = 300;
  int dumpMatricesFrames = 0;
  std::string modelPath = "Models/DamagedHelmet.glb";
  bool orbitYawOverride = false;
  float orbitYaw = 0.0f;
};

extern Config g_config;

} // namespace t850