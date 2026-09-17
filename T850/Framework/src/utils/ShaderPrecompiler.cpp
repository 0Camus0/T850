#include <pch.h>
#include <utils/ShaderPrecompiler.h>
#include <utils/ComputeKernelRegistry.h>
#include <utils/ResourceLocator.h>
#include <video/BaseDriver.h>
#include <glaze/glaze.hpp>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <map>
#include <stdexcept>

namespace t850::shader_precompiler {

struct Entry {
  std::string vertexShader;
  std::string fragmentShader;
};

struct ComputeEntry {
  std::string key;
  std::string kind;
  std::string computeShader;
  std::string entryPoint;
  std::string permutation;
  std::vector<std::string> defines;
};

struct Manifest {
  int version = 0;
  std::map<std::string, Entry> permutations;
  std::map<std::string, ComputeEntry> compute_permutations;
};

}

namespace t850 {

ShaderPrecompileResult PrecompileShaders(BaseDriver& driver, const ShaderPrecompileRequest& request) {
  std::string json;
  auto& resources = ResourceLocator::Instance();
  if (!resources.ReadText(request.manifestPath, json))
    throw std::runtime_error("Cannot read shader permutation manifest: " + request.manifestPath);
  shader_precompiler::Manifest manifest;
  if (glz::read<glz::opts{.error_on_unknown_keys = false}>(manifest, json)
      || (manifest.version != 1 && manifest.version != 2) ||
      (manifest.permutations.empty() && manifest.compute_permutations.empty()))
    throw std::runtime_error("Invalid or empty shader permutation manifest: " + request.manifestPath);

  ShaderPrecompileResult result;
  const size_t total = manifest.permutations.size() + manifest.compute_permutations.size();
  const auto reportProgress = [&result, &request](ShaderPrecompileProgress& progress) {
    progress.completed = result.succeeded + result.failed;
    if (request.onProgress) request.onProgress(progress);
  };
  for (const auto& [hex, entry] : manifest.permutations) {
    if (request.cancelRequested && request.cancelRequested()) {
      result.cancelled = true;
      break;
    }
    ShaderPrecompileProgress progress;
    progress.key = hex;
    progress.total = total;
    try {
      ShaderKey key;
      if (hex.size() != 18 || hex.substr(0, 2) != "0x")
        throw std::runtime_error("Invalid permutation key");
      const auto parsed = std::from_chars(hex.data() + 2, hex.data() + hex.size(), key.bits, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size() || !key.isValid())
        throw std::runtime_error("Invalid permutation key");
      const auto sourceName = [&driver](const std::string& recorded) {
        if (recorded.empty()) throw std::runtime_error("Unnamed shader in permutation manifest");
        auto name = std::filesystem::path(recorded).filename();
        name.replace_extension(driver.UsesGLSL() ? ".glsl" : ".hlsl");
        return name.string();
      };
      const auto vertexName = sourceName(entry.vertexShader);
      const auto fragmentName = sourceName(entry.fragmentShader);
      std::string vertexSource;
      std::string fragmentSource;
      const auto directory = std::filesystem::path(request.sourceDirectory);
      if (!resources.ReadText((directory / vertexName).generic_string(), vertexSource)
          || !resources.ReadText((directory / fragmentName).generic_string(), fragmentSource))
        throw std::runtime_error("Missing shader source: " + vertexName + " / " + fragmentName);
      if (driver.CreateShader(vertexSource, fragmentSource, key, vertexName, fragmentName) < 0)
        throw std::runtime_error("Shader compilation failed");
      ++result.succeeded;
    } catch (const std::exception& error) {
      ++result.failed;
      progress.error = error.what();
    }
    reportProgress(progress);
  }
  for (const auto& [identity, entry] : manifest.compute_permutations) {
    if (request.cancelRequested && request.cancelRequested()) {
      result.cancelled = true;
      break;
    }
    ShaderPrecompileProgress progress;
    progress.key = identity;
    progress.total = total;
    try {
      const std::filesystem::path recordedPath(entry.computeShader);
      const std::string shaderName = recordedPath.filename().string();
      const std::string expectedIdentity = shaderName + ":" + entry.entryPoint + ":" + entry.permutation;
      if (entry.kind != "compute" || entry.key != identity || identity != expectedIdentity ||
          shaderName.empty() || recordedPath.has_parent_path() ||
          !std::is_sorted(entry.defines.begin(), entry.defines.end()) ||
          std::adjacent_find(entry.defines.begin(), entry.defines.end()) != entry.defines.end()) {
        throw std::runtime_error("Invalid compute permutation manifest entry");
      }
      const ComputeKernelDefinition* kernel = FindComputeKernel(shaderName);
      if (!kernel || entry.entryPoint != kernel->entryPoint)
        throw std::runtime_error("Unknown compute kernel: " + shaderName + ":" + entry.entryPoint);
      if (!driver.SupportsComputeShaders())
        throw std::runtime_error("Compute shaders are unsupported by the selected API");
      std::string source;
      const auto sourcePath = (std::filesystem::path(request.sourceDirectory) / shaderName).generic_string();
      if (!resources.ReadText(sourcePath, source) || source.empty())
        throw std::runtime_error("Missing compute shader source: " + shaderName);
      ComputePipelineDesc pipeline;
      pipeline.source = std::move(source);
      pipeline.entryPoint = entry.entryPoint;
      pipeline.debugName = sourcePath;
      pipeline.permutationName = entry.permutation;
      pipeline.defines = entry.defines;
      ConfigureComputePipelineDesc(*kernel, pipeline);
      if (!driver.CreateComputePipeline(pipeline))
        throw std::runtime_error("Compute shader compilation failed");
      ++result.succeeded;
    } catch (const std::exception& error) {
      ++result.failed;
      progress.error = error.what();
    }
    reportProgress(progress);
  }
  return result;
}

}