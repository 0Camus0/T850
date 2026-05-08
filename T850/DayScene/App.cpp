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

#include <Application.h>

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include <Descriptors.h>
#include <core/Config.h>
#include <utils/Log.h>
#include <utils/ConfigRuntime.h>
#include <utils/ShaderPermutationDump.h>
#include <debug/Profiler.h>
#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFAccessor.h>

std::vector<std::string> g_args;

t850::AppBase		  *pApp = 0;
t850::RootFramework *pFrameWork = 0;

int main(int arg,char ** args){
  t850::Config defaultConfig;
  t850::g_config = defaultConfig;

    for(int i=0;i<arg;i++){
        g_args.push_back( std::string( args[i] ) );
    }

  if (t850::config::HasHelpArgument(arg, args)) {
    t850::config::PrintHelp();
    return 0;
  }

  if (auto configPath = t850::config::FindConfigArgument(arg, args)) {
    t850::config::LoadRuntimeConfig(*configPath, t850::g_config);
  }

  t850::config::ApplyCommandLine(arg, args, t850::g_config);
  t850::config::ValidateConfig(t850::g_config);

  t850::ApplicationDesc desc;
  t850::config::ConfigureApplicationDesc(t850::g_config, desc);

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
        path = t850::config::StripQuotes(path);
      }
      // Bring up logging early enough to surface loader messages.
      t850::Log::Init(t850::Log::LVL_INFO,
                      t850::Log::T8_LOG_BACKEND_CONSOLE, nullptr);

      t850::gltf::Document doc;
      if (!t850::gltf::LoadGLTF(path, doc)) {
        std::cerr << "[validateGltf] FAILED to load '" << path << "'\n";
        t850::Log::Shutdown();
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
            if (t850::gltf::ReadAccessorFloats(doc, p.attributes.POSITION, pos, &e)
                && e == 3 && pos.size() >= 3) {
              std::cout << " v0=(" << pos[0] << "," << pos[1] << "," << pos[2] << ")";
            }
          }
          std::cout << "\n";
        }
      }
      std::cout << "[validateGltf] OK: " << primCount << " primitives total\n";
      t850::Log::Shutdown();
      return 0;
    }
  }

  // Create log directory if a file path was requested
  if (!t850::g_config.logFile.empty()) {
    auto parent = std::filesystem::path(t850::g_config.logFile).parent_path();
    if (!parent.empty())
      std::filesystem::create_directories(parent);
  }

  // Determine API display name for session tag
  const char* apiTag = t850::config::ApiTag(desc.api);

  // Initialize logging
  uint32_t logBackends = t850::Log::T8_LOG_BACKEND_CONSOLE;
#ifdef OS_WINDOWS
  logBackends |= t850::Log::T8_LOG_BACKEND_DEBUG_OUTPUT;  // also emit to VS Output window
#endif
  if (!t850::g_config.logFile.empty())
    logBackends |= t850::Log::T8_LOG_BACKEND_FILE;

  t850::Log::Init(
    static_cast<t850::Log::Level>(t850::g_config.logLevel),
    logBackends,
    t850::g_config.logFile.empty() ? nullptr : t850::g_config.logFile.c_str()
  );
  t850::Log::SetSessionTag(apiTag);
  if (t850::g_config.flags.dumpShaderPermutations) {
    t850::ShaderPermutationDump::Begin(t850::g_config.shaderPermutationOutputPath);
  }

	pApp = new App;
#ifdef OS_LINUX
    pFrameWork = new t850::LinuxFramework((t850::AppBase*)pApp);
    pFrameWork->InitGlobalVars();
	pFrameWork->OnCreateApplication(desc);
#elif defined(OS_WINDOWS)
	pFrameWork = new t850::Win32Framework((t850::AppBase*)pApp);
	pFrameWork->InitGlobalVars();
	pFrameWork->OnCreateApplication(desc);
  if (t850::g_config.flags.dumpShaderPermutations) {
    t850::ShaderPermutationDump::Flush();
  } else {
	  pFrameWork->UpdateApplication();
  }
	pFrameWork->OnDestroyApplication();
#endif

	delete pFrameWork;
	delete pApp;

	t850::Log::Shutdown();

    return 0;
}

