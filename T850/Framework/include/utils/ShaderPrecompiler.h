#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace t850 {

class BaseDriver;

struct ShaderPrecompileProgress {
  size_t completed = 0;
  size_t total = 0;
  std::string key;
  std::string error;
};

struct ShaderPrecompileRequest {
  std::string manifestPath = "Shaders/shader_permutations.json";
  std::string sourceDirectory = "Shaders";
  std::function<bool()> cancelRequested;
  std::function<void(const ShaderPrecompileProgress&)> onProgress;
};

struct ShaderPrecompileResult {
  size_t succeeded = 0;
  size_t failed = 0;
  bool cancelled = false;
  bool Succeeded() const { return !cancelled && failed == 0; }
};

ShaderPrecompileResult PrecompileShaders(BaseDriver& driver, const ShaderPrecompileRequest& request);

}