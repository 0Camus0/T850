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

#include <T8_descriptors.h>
#include <core/t8config.h>
#include <utils/Log.h>
#include <utils/T8ConfigRuntime.h>
#include <gui/GUIAtlas.h>
#include <debug/T8_Profiler.h>
#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFAccessor.h>

std::vector<std::string> g_args;

t800::AppBase		  *pApp = 0;
t800::RootFramework *pFrameWork = 0;

int main(int arg,char ** args){
  t800::t8config defaultConfig;
  t800::g_t8config = defaultConfig;

    for(int i=0;i<arg;i++){
        g_args.push_back( std::string( args[i] ) );
    }

  if (t800::config::HasHelpArgument(arg, args)) {
    t800::config::PrintHelp();
    return 0;
  }

  if (auto configPath = t800::config::FindConfigArgument(arg, args)) {
    t800::config::LoadRuntimeConfig(*configPath, t800::g_t8config);
  }

  t800::config::ApplyCommandLine(arg, args, t800::g_t8config);

  t800::ApplicationDesc desc;
  t800::config::ConfigureApplicationDesc(t800::g_t8config, desc);

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
        path = t800::config::StripQuotes(path);
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
  const char* apiTag = t800::config::ApiTag(desc.api);

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
    int atlasWidth = 0;
    int atlasHeight = 0;
    if (t800::GUIAtlas::RecreateDefault(t800::g_t8config.atlasMaxSprite, atlasWidth, atlasHeight)) {
      printf("[createAtlas] Atlas generated successfully (%dx%d)\n",
             atlasWidth, atlasHeight);
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

