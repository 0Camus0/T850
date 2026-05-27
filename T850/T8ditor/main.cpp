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

#include <Descriptors.h>
#include <core/Config.h>
#include <utils/Log.h>

#include <string>
#include <vector>
#include <filesystem>

#include "EditorApp.h"

// Global expected by Framework (RenderMesh.cpp uses extern reference).
t850::AppBase* pApp = nullptr;

namespace t8ditor {
  // Defined in EditorApp.cpp.
  void SetStartupMeshPath(const std::string& p);
}

static t850::AppBase*       g_pApp       = nullptr;
static t850::RootFramework* g_pFramework = nullptr;

int main(int argc, char** argv) {
  t850::ApplicationDesc desc;
  desc.api       = t850::GraphicsApi::D3D12;
  desc.height    = 720;
  desc.width     = 1280;
  desc.videoMode = t850::VideoMode::WINDOWED;
  desc.title     = "T8ditor";

  // Minimal CLI: --api {d3d12|d3d11|vulkan|gl}, --width N, --height N,
  // --logFile PATH, --logLevel {error|info|debug|verbose|trace|0..4}, --mesh PATH.
  // Default API is D3D12.
  int   logLevel = 3;
  std::string logFile;
  std::string meshPath;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--api" && i + 1 < argc) {
      std::string v = argv[++i];
      if      (v == "d3d12" || v == "D3D12" || v == "dx12")   desc.api = t850::GraphicsApi::D3D12;
      else if (v == "d3d11" || v == "D3D11" || v == "dx11")   desc.api = t850::GraphicsApi::D3D11;
      else if (v == "vulkan" || v == "Vulkan" || v == "vk")   desc.api = t850::GraphicsApi::VULKAN;
      else if (v == "gl" || v == "GL" || v == "opengl")       desc.api = t850::GraphicsApi::OPENGL;
    }
    else if (a == "--width"  && i + 1 < argc) desc.width  = std::stoi(argv[++i]);
    else if (a == "--height" && i + 1 < argc) desc.height = std::stoi(argv[++i]);
    else if (a == "--mesh"   && i + 1 < argc) meshPath = argv[++i];
    else if (a == "--logFile" && i + 1 < argc) logFile = argv[++i];
    else if (a == "--d3d12debug") t850::g_config.flags.d3d12Debug = true;
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
    t850::g_config.logFile = logFile;
    auto parent = std::filesystem::path(logFile).parent_path();
    if (!parent.empty())
      std::filesystem::create_directories(parent);
  }

  uint32_t logBackends = t850::Log::T8_LOG_BACKEND_CONSOLE;
#ifdef OS_WINDOWS
  logBackends |= t850::Log::T8_LOG_BACKEND_DEBUG_OUTPUT;
#endif
  if (!logFile.empty())
    logBackends |= t850::Log::T8_LOG_BACKEND_FILE;

  t850::Log::Init(
    static_cast<t850::Log::Level>(logLevel),
    logBackends,
    logFile.empty() ? nullptr : logFile.c_str()
  );
  t850::Log::SetSessionTag("t8ditor");

  // Default to a sample model if the user didn't pick one — Models/SkyBox.glb
  // ships with the repo and is loaded by DayScene, so it's known-good.
  if (meshPath.empty()) {
    if (std::filesystem::exists("Models/SkyBox.glb")) {
      meshPath = "Models/SkyBox.glb";
    }
  }
  t8ditor::SetStartupMeshPath(meshPath);

  g_pApp = new t8ditor::EditorApp();
  pApp   = g_pApp;  // RenderMesh::Load() uses this global

#ifdef OS_LINUX
  g_pFramework = new t850::LinuxFramework(g_pApp);
  g_pFramework->InitGlobalVars();
  g_pFramework->OnCreateApplication(desc);
  // LinuxFramework drives its own loop in OnCreateApplication today, matching
  // DayScene's pattern.
#elif defined(OS_WINDOWS)
  g_pFramework = new t850::Win32Framework(g_pApp);
  g_pFramework->InitGlobalVars();
  g_pFramework->OnCreateApplication(desc);
  g_pFramework->UpdateApplication();
  g_pFramework->OnDestroyApplication();
#endif

  delete g_pFramework;
  delete g_pApp;

  t850::Log::Shutdown();
  return 0;
}
