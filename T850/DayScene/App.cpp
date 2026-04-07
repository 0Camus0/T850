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

#include <T8_descriptors.h>

std::vector<std::string> g_args;

// RT Dump configuration (set via command-line)
bool   g_dumpEnabled = false;
bool   g_dumpByFrame = false;     // true = frame-based, false = time-based
int    g_dumpFrame   = -1;        // frame number to dump at
float  g_dumpSeconds = -1.0f;     // seconds to dump at
bool   g_debugFrames = false;     // --debugFrames: spacebar pauses + dumps + exits
bool   g_debugDumpRequested = false; // set by spacebar press
std::string g_feedMatricesPath;      // --feedMatrices: path to matrices.json to replay
bool   g_keepRunning = false;        // --keepRunning: don't exit after dump

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
    else if (a == "--dump-seconds" && i + 1 < arg) {
      g_dumpEnabled = true;
      g_dumpByFrame = false;
      g_dumpSeconds = std::stof(args[++i]);
    }
    else if (a == "--debugFrames") {
      g_debugFrames = true;
    }
    else if (a == "--feedMatrices" && i + 1 < arg) {
      g_feedMatricesPath = args[++i];
      // Strip surrounding quotes if present (from launcher)
      if (g_feedMatricesPath.size() >= 2 && g_feedMatricesPath.front() == '"' && g_feedMatricesPath.back() == '"') {
        g_feedMatricesPath = g_feedMatricesPath.substr(1, g_feedMatricesPath.size() - 2);
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

    return 0;
}

