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

#include <T8_descriptors.h>
#include <utils/Log.h>

std::vector<std::string> g_args;

// RT Dump configuration (set via command-line)
bool   g_dumpEnabled = false;
bool   g_dumpByFrame = false;     // true = frame-based, false = time-based
int    g_dumpFrame   = -1;        // frame number to dump at
float  g_dumpSeconds = -1.0f;     // seconds to dump at
bool   g_debugFrames = false;     // --debugFrames: spacebar pauses + dumps + exits
bool   g_debugDumpRequested = false; // set by spacebar press
std::string g_replaySnapshotPath;    // --replaySnapshot: path to snapshot.json to replay
bool   g_keepRunning = false;        // --keepRunning: don't exit after dump
int    g_startScene  = 0;            // --scene: starting scene index (0=Day, 1=Night, 2=Tech)
bool   g_fullscreen  = false;        // --fullscreen: launch in fullscreen mode
bool   g_guiOnStart  = false;        // --gui: show GUI sliders on startup
bool   g_guiScreenshot = false;      // --guiScreenshot: render 1 frame with GUI, save screenshot, exit
std::string g_guiScreenshotPath = "gui_screenshot.ppm"; // output path for --guiScreenshot
bool   g_guiEdit     = false;        // --guiEdit: enable GUI edit mode (move/scale elements)
bool   g_guiSnap     = false;        // --guiSnap: snap elements to grid when moving
bool   g_guiControlEdit = false;     // --guiControlEdit: edit control internals (knob/buttons/check)
std::string g_guiControlTarget = "slider_knob"; // --guiControlTarget <slider_knob|selector_control|checkbox_mark>
int    g_logLevel    = 3;            // --logLevel: 0=Error,1=Info,2=Debug,3=Verbose
std::string g_logFile;              // --logFile <path>: write log to file (append, flush-per-entry)

t800::AppBase		  *pApp = 0;
t800::RootFramework *pFrameWork = 0;

int main(int arg,char ** args){
  t800::ApplicationDesc desc;
  desc.api = t800::GRAPHICS_API::D3D11;
  desc.height = 720;
  desc.width = 1280;
  desc.videoMode = t800::T8_VIDEO_MODE::WINDOWED;
  desc.title = "T800 Project";

    for(int i=0;i<arg;i++){
        g_args.push_back( std::string( args[i] ) );
    }

  // Parse command-line arguments
  for (int i = 1; i < arg; i++) {
    std::string a = args[i];
    if (a == "--api" && i + 1 < arg) {
      std::string val = args[++i];
      if (val == "gl" || val == "GL" || val == "opengl" || val == "OpenGL")
        desc.api = t800::GRAPHICS_API::OPENGL;
      else if (val == "d3d11" || val == "D3D11" || val == "dx11")
        desc.api = t800::GRAPHICS_API::D3D11;
    }
    else if (a == "--dump-frame" && i + 1 < arg) {
      g_dumpEnabled = true;
      g_dumpByFrame = true;
      g_dumpFrame = std::stoi(args[++i]);
    }
    else if (a == "--dumpSnapshot-seconds" && i + 1 < arg) {
      g_dumpEnabled = true;
      g_dumpByFrame = false;
      g_dumpSeconds = std::stof(args[++i]);
    }
    else if (a == "--debugFrames") {
      g_debugFrames = true;
    }
    else if (a == "--replaySnapshot" && i + 1 < arg) {
      g_replaySnapshotPath = args[++i];
      // Strip surrounding quotes if present (from launcher)
      if (g_replaySnapshotPath.size() >= 2 && g_replaySnapshotPath.front() == '"' && g_replaySnapshotPath.back() == '"') {
        g_replaySnapshotPath = g_replaySnapshotPath.substr(1, g_replaySnapshotPath.size() - 2);
      }
    }
    else if (a == "--keepRunning") {
      g_keepRunning = true;
    }
    else if (a == "--width" && i + 1 < arg) {
      desc.width = std::stoi(args[++i]);
    }
    else if (a == "--height" && i + 1 < arg) {
      desc.height = std::stoi(args[++i]);
    }
    else if (a == "--scene" && i + 1 < arg) {
      g_startScene = std::stoi(args[++i]);
    }
    else if (a == "--fullscreen") {
      g_fullscreen = true;
    }
    else if (a == "--gui") {
      g_guiOnStart = true;
    }
    else if (a == "--guiScreenshot" && i + 1 < arg) {
      g_guiScreenshot = true;
      g_guiOnStart = true;  // implies --gui
      g_guiScreenshotPath = args[++i];
    }
    else if (a == "--guiScreenshot") {
      g_guiScreenshot = true;
      g_guiOnStart = true;
    }
    else if (a == "--guiEdit") {
      g_guiEdit = true;
      g_guiOnStart = true;  // implies --gui
    }
    else if (a == "--guiSnap") {
      g_guiSnap = true;
    }
    else if (a == "--guiControlEdit") {
      g_guiControlEdit = true;
      g_guiOnStart = true; // implies --gui
    }
    else if (a == "--guiControlTarget" && i + 1 < arg) {
      g_guiControlTarget = args[++i];
    }
    else if (a == "--logLevel" && i + 1 < arg) {
      std::string val = args[++i];
      if (val == "error"   || val == "0") g_logLevel = 0;
      else if (val == "info"  || val == "1") g_logLevel = 1;
      else if (val == "debug" || val == "2") g_logLevel = 2;
      else if (val == "verbose" || val == "3") g_logLevel = 3;
    }
    else if (a == "--logFile" && i + 1 < arg) {
      g_logFile = args[++i];
    }
  }

  if (g_fullscreen)
    desc.videoMode = t800::T8_VIDEO_MODE::FULLSCREEN;

  // Create log directory if a file path was requested
  if (!g_logFile.empty()) {
    auto parent = std::filesystem::path(g_logFile).parent_path();
    if (!parent.empty())
      std::filesystem::create_directories(parent);
  }

  // Determine API display name for session tag
  const char* apiTag = (desc.api == t800::GRAPHICS_API::OPENGL) ? "gl" : "d3d11";

  // Initialize logging
  uint32_t logBackends = t800::Log::T8_LOG_BACKEND_CONSOLE;
#ifdef OS_WINDOWS
  logBackends |= t800::Log::T8_LOG_BACKEND_DEBUG_OUTPUT;  // also emit to VS Output window
#endif
  if (!g_logFile.empty())
    logBackends |= t800::Log::T8_LOG_BACKEND_FILE;

  t800::Log::Init(
    static_cast<t800::Log::Level>(g_logLevel),
    logBackends,
    g_logFile.empty() ? nullptr : g_logFile.c_str()
  );
  t800::Log::SetSessionTag(apiTag);

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

