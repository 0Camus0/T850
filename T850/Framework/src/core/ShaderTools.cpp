#include <pch.h>
#include <core/ShaderTools.h>
#include <core/Config.h>
#include <core/Core.h>
#include <utils/ConfigRuntime.h>
#include <utils/ShaderPrecompiler.h>
#include <utils/ShaderPermutationDump.h>

#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace t850 {
namespace {

class ShaderToolApplication final : public AppBase {
public:
  explicit ShaderToolApplication(const Config& config) {
    request.manifestPath = config.shaderPermutationInputPath;
    request.cancelRequested = [path = config.shaderCompileCancelFile] {
      return !path.empty() && std::filesystem::exists(path);
    };
    request.onProgress = [](const ShaderPrecompileProgress& progress) {
      auto& output = progress.error.empty() ? std::cout : std::cerr;
      output << "[ShaderPrecompile] " << progress.completed << '/' << progress.total
             << (progress.error.empty() ? " OK " : " FAILED ") << progress.key;
      if (!progress.error.empty()) output << ": " << progress.error;
      output << std::endl;
    };
  }

  void CreateAssets() override {
    result = PrecompileShaders(*pFramework->pVideoDriver, request);
    if (result.cancelled)
      std::cout << "[ShaderPrecompile] cancelled after " << result.succeeded + result.failed
                << " permutations" << std::endl;
    else
      std::cout << "[ShaderPrecompile] complete: " << result.succeeded << " succeeded, "
                << result.failed << " failed" << std::endl;
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
  ShaderPrecompileResult result;

private:
  ShaderPrecompileRequest request;
};

}

std::optional<int> RunShaderPrecompileCommand(const Config& config) {
  if (!config.flags.compileShaders) return std::nullopt;
#ifdef OS_WINDOWS
  ShaderToolApplication application(config);
  Win32Framework framework(&application);
  ApplicationDesc descriptor;
  config::ConfigureApplicationDesc(config, descriptor);
  framework.InitGlobalVars();
  try {
    framework.OnCreateApplication(descriptor);
  } catch (...) {
    if (framework.pVideoDriver) framework.OnDestroyApplication();
    throw;
  }
  framework.OnDestroyApplication();
  return application.result.Succeeded() ? 0 : 1;
#else
  throw std::invalid_argument("--compileShaders currently requires the Windows runtime; Android uses offline APK compilation.");
#endif
}

void BeginShaderPermutationRecording(const Config& config) {
  if (config.flags.dumpShaderPermutations || config.flags.recordShaderPermutations)
    ShaderPermutationDump::Begin(config.shaderPermutationOutputPath);
}

}