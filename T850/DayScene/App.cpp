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
#include <charconv>
#include <cstdlib>
#include <map>
#include <glaze/glaze.hpp>

#include <Descriptors.h>
#include <core/Config.h>
#include <utils/Log.h>
#include <utils/ConfigRuntime.h>
#include <utils/ShaderPermutationDump.h>
#include <utils/ResourceLocator.h>
#include <debug/Profiler.h>
#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFAccessor.h>
#include <game/GameSelfTest.h>
#include <debug/CrashDiagnostics.h>
#include <debug/GraphicsFixture.h>

std::vector<std::string> g_args;

t850::AppBase		  *pApp = 0;
t850::RootFramework *pFrameWork = 0;

namespace t850::shader_cache {
struct ShaderCacheEntry {
  std::string vertexShader;
  std::string fragmentShader;
};

struct ShaderCacheManifest {
  int version = 0;
  std::map<std::string, ShaderCacheEntry> permutations;
};

class ShaderCacheApp final : public t850::AppBase {
public:
  ShaderCacheApp(const std::string& path, std::string cancelPath) : cancelFile(std::move(cancelPath)) {
    std::string json;
    if (!t850::ResourceLocator::Instance().ReadText(path, json))
      throw std::runtime_error("Cannot read shader permutation manifest: " + path);
    if (glz::read<glz::opts{.error_on_unknown_keys = false}>(manifest, json)
        || manifest.version != 1 || manifest.permutations.empty())
      throw std::runtime_error("Invalid or empty shader permutation manifest: " + path);
  }

  void CreateAssets() override {
    size_t completed = 0;
    for (const auto& [hex, entry] : manifest.permutations) {
      if (!cancelFile.empty() && std::filesystem::exists(cancelFile)) {
        cancelled = true;
        std::cout << "[ShaderPrecompile] cancelled after " << completed << " permutations" << std::endl;
        break;
      }
      try {
        t850::ShaderKey key;
        if (hex.size() != 18 || hex.substr(0, 2) != "0x")
          throw std::runtime_error("Invalid permutation key");
        const auto parsed = std::from_chars(hex.data() + 2, hex.data() + hex.size(), key.bits, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size() || !key.isValid())
          throw std::runtime_error("Invalid permutation key");
        auto sourceName = [](const std::string& recorded) {
          if (recorded.empty()) throw std::runtime_error("Unnamed shader in permutation manifest");
          auto name = std::filesystem::path(recorded).filename();
          name.replace_extension(t850::g_pBaseDriver->UsesGLSL() ? ".glsl" : ".hlsl");
          return name.string();
        };
        const auto vertexName = sourceName(entry.vertexShader);
        const auto fragmentName = sourceName(entry.fragmentShader);
        std::string vertexSource;
        std::string fragmentSource;
        auto& resources = t850::ResourceLocator::Instance();
        if (!resources.ReadText("Shaders/" + vertexName, vertexSource)
            || !resources.ReadText("Shaders/" + fragmentName, fragmentSource))
          throw std::runtime_error("Missing shader source: " + vertexName + " / " + fragmentName);
        if (t850::g_pBaseDriver->CreateShader(vertexSource, fragmentSource, key, vertexName, fragmentName) < 0)
          throw std::runtime_error("Shader compilation failed");
        std::cout << "[ShaderPrecompile] " << ++completed << '/' << manifest.permutations.size()
                  << " OK " << hex << std::endl;
      } catch (const std::exception& error) {
        ++failures;
        std::cerr << "[ShaderPrecompile] " << ++completed << '/' << manifest.permutations.size()
                  << " FAILED " << hex << ": " << error.what() << std::endl;
      }
    }
    if (!cancelled)
      std::cout << "[ShaderPrecompile] complete: " << completed - failures << " succeeded, "
                << failures << " failed" << std::endl;
  }
  void InitVars() override {}
  void LoadAssets() override {}
  void DestroyAssets() override {}
  void OnUpdate() override {}
  void OnDraw() override {}
  void OnInput() override {}
  void OnPause() override {}
  void OnResume() override {}
  void OnReset() override {}
  void LoadScene(int) override {}
  bool AllowsMouseCapture() const override { return false; }
  size_t failures = 0;
  bool cancelled = false;
private:
  ShaderCacheManifest manifest;
  std::string cancelFile;
};
}

int main(int arg,char ** args) try {
  t850::InstallUnattendedCrtReportHook();
  t850::Config defaultConfig;
  t850::g_config = defaultConfig;
  bool recordShaderPermutations = false;
  bool compileShaders = false;
  std::string shaderPermutationInput = "Shaders/shader_permutations.json";
  std::string shaderCompileCancelFile;

    for(int i=0;i<arg;i++){
        g_args.push_back( std::string( args[i] ) );
    }

  for (int i = 1; i < arg; ++i) {
    if (std::string_view(args[i]) == "--compileShaders") compileShaders = true;
    if (std::string_view(args[i]) == "--shaderPermutationInput") {
      if (i + 1 >= arg || std::string_view(args[i + 1]).starts_with("--"))
        throw std::invalid_argument("--shaderPermutationInput requires a manifest path");
      shaderPermutationInput = args[++i];
    }
    if (std::string_view(args[i]) == "--shaderCompileCancelFile") {
      if (i + 1 >= arg || std::string_view(args[i + 1]).starts_with("--"))
        throw std::invalid_argument("--shaderCompileCancelFile requires a path");
      shaderCompileCancelFile = args[++i];
    }
    if (std::string_view(args[i]) == "--recordShaderPermutations") {
      recordShaderPermutations = true;
    }
      if (std::string_view(args[i]) == "--graphics-fixture") {
  #if defined(_WIN32) && defined(_M_X64)
    return t850::RunGraphicsFixture(arg, args);
  #else
    std::cerr << "The graphics fixture currently requires Windows x64.\n";
    return 1;
  #endif
      }
    if (std::string_view(args[i]) == "--game-selftest") {
      const int failures = t850::game::RunGameSelfTests();
      return failures == 0 ? 0 : 1;
    }
  }

  if (t850::config::HasHelpArgument(arg, args)) {
    t850::config::PrintHelp();
    std::cout << "Shader cache tools:\n"
              << "  --compileShaders                  Compile all recorded permutations, then exit\n"
              << "  --shaderPermutationInput <path>    Manifest to compile (default: Shaders/shader_permutations.json)\n"
              << "  --shaderCompileCancelFile <path>   Stop between permutations when this file exists\n"
              << "  --recordShaderPermutations         Record through runtime and flush at normal/snapshot exit\n"
              << "  --shaderPermutationOutput <path>   Manifest to merge recorded requests into\n";
    return 0;
  }

  if (auto configPath = t850::config::FindConfigArgument(arg, args)) {
    t850::config::LoadRuntimeConfig(*configPath, t850::g_config);
  }

  t850::config::ApplyCommandLine(arg, args, t850::g_config);
  t850::config::ValidateConfig(t850::g_config);
  if (compileShaders) {
#if !defined(OS_WINDOWS)
    throw std::invalid_argument("--compileShaders currently requires the Windows runtime; Android uses offline APK compilation.");
#endif
    t850::g_config.width = 64;
    t850::g_config.height = 64;
    t850::g_config.flags.offscreen = true;
    t850::g_config.flags.fullscreen = false;
    if (recordShaderPermutations || t850::g_config.flags.dumpShaderPermutations || t850::g_config.flags.benchmarkMatrix)
      throw std::invalid_argument("Shader compilation cannot be combined with recording or benchmark modes");
  }
  if (t850::g_config.flags.benchmarkMatrix) {
    if (t850::g_config.api == "webgpu") {
      std::cerr << "WebGPU benchmark-matrix integration is not implemented.\n";
      return 1;
    }
    t850::g_config.api = "d3d11";
    t850::g_config.width = 1920;
    t850::g_config.height = 1080;
    t850::g_config.flags.offscreen = false;
    t850::g_config.startScene = 1;
  }

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
  if (t850::g_config.flags.dumpShaderPermutations || recordShaderPermutations) {
    t850::ShaderPermutationDump::Begin(t850::g_config.shaderPermutationOutputPath);
  }
  if (recordShaderPermutations) {
    std::atexit([] {
      if (!t850::ShaderPermutationDump::Flush()) std::_Exit(EXIT_FAILURE);
    });
  }

  auto* shaderCacheApp = compileShaders ? new t850::shader_cache::ShaderCacheApp(shaderPermutationInput, shaderCompileCancelFile) : nullptr;
  pApp = shaderCacheApp ? static_cast<t850::AppBase*>(shaderCacheApp) : new App;
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
  } else if (!compileShaders) {
	  pFrameWork->UpdateApplication();
  }
	pFrameWork->OnDestroyApplication();
#endif

  const int exitCode = shaderCacheApp && (shaderCacheApp->failures || shaderCacheApp->cancelled) ? 1 : 0;
  delete pFrameWork;
	delete pApp;

  if (recordShaderPermutations && !t850::ShaderPermutationDump::Flush()) {
    t850::Log::Shutdown();
    return 1;
  }
	t850::Log::Shutdown();

    return exitCode;
} catch (const std::exception& error) {
  T8_LOG_ERROR("[App] Startup/runtime failure: %s", error.what());
  std::cerr << "Engine failure: " << error.what() << '\n';
  return 1;
}
