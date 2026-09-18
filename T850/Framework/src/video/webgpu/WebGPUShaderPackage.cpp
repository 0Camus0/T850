#include <pch.h>
#include <video/webgpu/WebGPUShaderCompiler.h>

#if (defined(_WIN32) && defined(_M_X64)) || defined(__EMSCRIPTEN__)
#include <utils/ResourceLocator.h>
#include <utils/ShaderDiskCache.h>
#include <glaze/glaze.hpp>
#include <filesystem>

namespace t850::webgpu {
struct ShaderPackageRecord {
  uint32_t version = 3;
  ShaderFlow flow = ShaderFlow::Auto;
  ShaderSourceLanguage sourceLanguage = ShaderSourceLanguage::Hlsl;
  std::string requestHash;
  std::string artifactHash;
  ShaderArtifact artifact;
};

namespace {
std::string RequestHash(const ShaderRequest& request, ShaderFlow flow) {
  auto normalized = request;
  normalized.name = std::filesystem::path(request.name).lexically_normal().generic_string();
  return ShaderDiskCache::ContentHash(glz::write_json(std::pair(normalized, flow)).value());
}
}

bool WriteShaderPackage(const ShaderRequest& request, const ShaderArtifact& artifact,
                        ShaderFlow flow, const ShaderFlowReport& report,
                        const std::string& directory, std::string& diagnostic) {
  ShaderPackageRecord record;
  record.flow = flow;
  record.sourceLanguage = report.attempts.empty() ? ShaderSourceLanguage::Hlsl : report.attempts.back().sourceLanguage;
  if ((flow == ShaderFlow::Wgsl && record.sourceLanguage != ShaderSourceLanguage::Wgsl) ||
      (flow == ShaderFlow::Spirv && record.sourceLanguage != ShaderSourceLanguage::Hlsl)) {
    diagnostic = "Shader package source does not match the requested strict flow";
    return false;
  }
  record.requestHash = RequestHash(request, flow);
  record.artifact = artifact;
  record.artifact.cacheHit = false;
  record.artifact.cacheStored = false;
  record.artifact.translationMilliseconds = 0;
  record.artifactHash = ShaderDiskCache::ContentHash(glz::write_json(record.artifact).value());
  const auto text = glz::write_json(record).value();
  const auto path = std::filesystem::absolute(std::filesystem::path(directory) / (record.requestHash + ".json"));
  if (!ResourceLocator::Instance().WriteBinaryAtomic(path.generic_string(),
      std::span(reinterpret_cast<const unsigned char*>(text.data()), text.size()))) {
    diagnostic = "Cannot write browser shader package: " + path.generic_string();
    return false;
  }
  return true;
}

bool ReadShaderPackage(const ShaderRequest& request, ShaderArtifact& artifact,
                       ShaderFlow flow, ShaderFlowReport& report, std::string& diagnostic,
                       const std::string& directory) {
  artifact = {};
  report = {};
  report.requestedFlow = flow;
  const auto hash = RequestHash(request, flow);
  const auto path = (std::filesystem::path(directory) / (hash + ".json")).generic_string();
  std::string text;
  ShaderPackageRecord record;
  if (!ResourceLocator::Instance().ReadText(path, text) ||
      glz::read_json(record, text) || record.version != 3 || record.requestHash != hash || record.flow != flow ||
      (flow == ShaderFlow::Wgsl && record.sourceLanguage != ShaderSourceLanguage::Wgsl) ||
      (flow == ShaderFlow::Spirv && record.sourceLanguage != ShaderSourceLanguage::Hlsl) ||
      record.artifact.wgsl.empty() || record.artifactHash !=
        ShaderDiskCache::ContentHash(glz::write_json(record.artifact).value())) {
    diagnostic = "Missing or invalid browser shader package for " + request.name + ": " + path;
    return false;
  }
  artifact = std::move(record.artifact);
  artifact.cacheHit = true;
  ShaderFlowAttempt attempt;
  attempt.sourceLanguage = record.sourceLanguage;
  attempt.sourceName = request.name;
  attempt.succeeded = true;
  attempt.cacheHit = true;
  report.attempts.push_back(std::move(attempt));
  return true;
}
}
#endif