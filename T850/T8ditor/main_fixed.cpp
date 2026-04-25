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

// T8ditor entry point. Mirrors DayScene/App.cpp's main() pattern so the
// editor reuses the existing Win32Framework / LinuxFramework loop instead
// of forking it. See EDITOR.md (Phase 1) for the design.

#include <Config.h>
#ifdef OS_LINUX
    #include <core/LinuxFramework.h>
#elif defined(OS_WINDOWS)
    #include <core/windows/Win32Framework.h>
#endif

#include <T8_descriptors.h>
#include <core/t8config.h>
#include <utils/Log.h>

#include <string>
#include <vector>
#include <filesystem>

#include "EditorApp.h"

// Global expected by Framework (RenderMesh.cpp uses extern reference).
t800::AppBase* pApp = nullptr;

namespace t8ditor {
  // Defined in EditorApp.cpp.
  void SetStartupMeshPath(const std::string& p);
}

static t800::AppBase*       g_pApp       = nullptr;
static t800::RootFramework* g_pFramework = nullptr;

int main(int argc, char** argv) {
  t800::ApplicationDesc desc;
  desc.api       = t800::GRAPHICS_API::D3D12;
  desc.height    = 720;
  desc.width     = 1280;
  desc.videoMode = t800::T8_VIDEO_MODE::WINDOWED;
  desc.title     = "T8ditor";

  // Minimal CLI: --api {gl|d3d11|d3d12}, --width N, --height N, --logFile PATH,
  // --logLevel {error|info|debug|verbose|trace|0..4}. Mirrors a subset of
  // DayScene's flags so existing dev workflows transfer.
  int   logLevel = 3;
  std::string logFile;
  std::string meshPath;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--api" && i + 1 < argc) {
      std::string v = argv[++i];
      if      (v == "gl"    || v == "GL"    || v == "opengl" || v == "OpenGL") desc.api = t800::GRAPHICS_API::OPENGL;
      else if (v == "d3d12" || v == "D3D12" || v == "dx12")                    desc.api = t800::GRAPHICS_API::D3D12;
      else if (v == "d3d11" || v == "D3D11" || v == "dx11")                    desc.api = t800::GRAPHICS_API::D3D12;
    }
    else if (a == "--width"  && i + 1 < argc) desc.width  = std::stoi(argv[++i]);
    else if (a == "--height" && i + 1 < argc) desc.height = std::stoi(argv[++i]);
    else if (a == "--mesh"   && i + 1 < argc) meshPath = argv[++i];
    else if (a == "--logFile" && i + 1 < argc) logFile = argv[++i];
    else if (a == "--d3d12debug") t800::g_t8config.flags.d3d12Debug = true;
    else if (a == "--logLevel" && i + 1 < argc) {
      std::string v = argv[++i];
      if      (v == "error"   || v == "0") logLevel = 0;
      else if (v == "info"    || v == "1") logLevel = 1;
      else if (v == "debug"   || v == "2") logLevel = 2;
      else if (v == "verbose" || v == "3") logLevel = 3;
      else if (v == "trace"   || v == "4") logLevel = 4;
    }
  }

  if (!logFile.empty()) {
    t800::g_t8config.logFile = logFile;
    auto parent = std::filesystem::path(logFile).parent_path();
    if (!parent.empty())
      std::filesystem::create_directories(parent);
  }

  uint32_t logBackends = t800::Log::T8_LOG_BACKEND_CONSOLE;
#ifdef OS_WINDOWS
  logBackends |= t800::Log::T8_LOG_BACKEND_DEBUG_OUTPUT;
#endif
  if (!logFile.empty())
    logBackends |= t800::Log::T8_LOG_BACKEND_FILE;

  t800::Log::Init(
    static_cast<t800::Log::Level>(logLevel),
    logBackends,
    logFile.empty() ? nullptr : logFile.c_str()
  );
  t800::Log::SetSessionTag("t8ditor");

  // Default to a sample model if the user didn't pick one — Models/SkyBox.X
  // ships with the repo and is loaded by DayScene, so it's known-good.
  if (meshPath.empty()) {
    if (std::filesystem::exists("Models/SkyBox.X")) {
      meshPath = "Models/SkyBox.X";
    }
  }
  t8ditor::SetStartupMeshPath(meshPath);

  g_pApp = new t8ditor::EditorApp();
  pApp   = g_pApp;  // RenderMesh::Load() uses this global

#ifdef OS_LINUX
  g_pFramework = new t800::LinuxFramework(g_pApp);
  g_pFramework->InitGlobalVars();
  g_pFramework->OnCreateApplication(desc);
  // LinuxFramework drives its own loop in OnCreateApplication today, matching
  // DayScene's pattern.
#elif defined(OS_WINDOWS)
  g_pFramework = new t800::Win32Framework(g_pApp);
  g_pFramework->InitGlobalVars();
  g_pFramework->OnCreateApplication(desc);
  g_pFramework->UpdateApplication();
  g_pFramework->OnDestroyApplication();
#endif

  delete g_pFramework;
  delete g_pApp;

  t800::Log::Shutdown();
  return 0;
}
