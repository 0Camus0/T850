#pragma once

#if (defined(_WIN32) && defined(_M_X64)) || defined(__EMSCRIPTEN__)
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace t850::webgpu {
enum class ShaderStage { Vertex, Fragment, Compute };
enum class ShaderSourceLanguage { Hlsl, Wgsl };
enum class ShaderFlow { Auto, Wgsl, Spirv };
enum class BindingLayout { GraphicsV1, BlurV1, ComputeV1 };
enum class ResourceKind {
  UniformBuffer,
  ReadOnlyStorageBuffer,
  ReadWriteStorageBuffer,
  SampledTexture,
  Sampler,
  WriteOnlyStorageTexture,
  DepthTexture
};
enum class TextureDimension { None, D1, D2, D2Array, D3, Cube, CubeArray };
enum class ShaderComponentType { Float, Unsigned, Signed, Half };

struct ShaderLocation {
  uint32_t location = 0;
  uint32_t components = 0;
  ShaderComponentType type = ShaderComponentType::Float;
  bool operator==(const ShaderLocation&) const = default;
};

struct ShaderBinding {
  ResourceKind kind;
  uint32_t group = 0;
  uint32_t binding = 0;
  uint64_t minimumBufferSize = 0;
  std::vector<uint32_t> uniformMemberOffsets;
  bool storageRgba8Unorm = false;
  bool storageRgba16Float = false;
  TextureDimension dimension = TextureDimension::None;
  ShaderComponentType sampledType = ShaderComponentType::Float;
  bool comparisonSampler = false;
  bool operator==(const ShaderBinding&) const = default;
};

struct ShaderRequest {
  ShaderStage stage = ShaderStage::Vertex;
  BindingLayout layout = BindingLayout::GraphicsV1;
  std::string name;
  std::string entryPoint;
  std::string source;
  std::string defines;
  uint64_t keyBits = 0;
  ShaderSourceLanguage sourceLanguage = ShaderSourceLanguage::Hlsl;
};

struct ShaderArtifact {
  std::string wgsl;
  std::vector<ShaderBinding> bindings;
  std::array<uint32_t, 3> workgroupSize{};
  std::vector<ShaderLocation> inputs;
  std::vector<ShaderLocation> outputs;
  bool writesDepth = false;
  bool cacheHit = false;
  bool cacheStored = false;
  double translationMilliseconds = 0;
};

struct ShaderFileRequest {
  ShaderStage stage = ShaderStage::Vertex;
  BindingLayout layout = BindingLayout::GraphicsV1;
  std::string name;
  std::string entryPoint;
  std::string defines;
  uint64_t keyBits = 0;
  ShaderFlow flow = ShaderFlow::Auto;
};

struct ShaderFlowAttempt {
  ShaderSourceLanguage sourceLanguage = ShaderSourceLanguage::Wgsl;
  std::string sourceName;
  bool succeeded = false;
  bool cacheHit = false;
  double preparationMilliseconds = 0;
  double elapsedMilliseconds = 0;
  std::string diagnostic;
};

struct ShaderFlowReport {
  ShaderFlow requestedFlow = ShaderFlow::Auto;
  bool fallbackAttempted = false;
  double elapsedMilliseconds = 0;
  std::vector<ShaderFlowAttempt> attempts;
};

inline const char* ShaderFlowName(ShaderFlow flow) {
  switch (flow) {
    case ShaderFlow::Auto: return "auto";
    case ShaderFlow::Wgsl: return "wgsl";
    case ShaderFlow::Spirv: return "spirv";
  }
  return "invalid";
}
inline bool ParseShaderFlow(const std::string& name, ShaderFlow& flow) {
  for (const auto candidate : {ShaderFlow::Auto, ShaderFlow::Wgsl, ShaderFlow::Spirv}) {
    if (name == ShaderFlowName(candidate)) {
      flow = candidate;
      return true;
    }
  }
  return false;
}
bool LoadShaderFiles(const ShaderFileRequest& request, ShaderArtifact& artifact,
                     ShaderFlowReport& report, std::string& diagnostic,
                     const std::string& specialization = {});
std::string CompilerSignature();
bool LoadOrTranslateShader(const ShaderRequest& request, ShaderArtifact& artifact, std::string& diagnostic,
                           const std::string& specialization = {});
bool TranslateShader(const ShaderRequest& request, ShaderArtifact& artifact, std::string& diagnostic);
bool ReflectShader(const ShaderRequest& request, ShaderArtifact& artifact, std::string& diagnostic);
bool WriteShaderPackage(const ShaderRequest& request, const ShaderArtifact& artifact,
                        ShaderFlow flow, const ShaderFlowReport& report,
                        const std::string& directory, std::string& diagnostic);
bool ReadShaderPackage(const ShaderRequest& request, ShaderArtifact& artifact,
                       ShaderFlow flow, ShaderFlowReport& report, std::string& diagnostic,
                       const std::string& directory = "WebShaders");
}
#endif