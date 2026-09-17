#include <video/webgpu/WebGPUShaderCompiler.h>

#if defined(_WIN32) && defined(_M_X64)
#include <T850DawnShaderConfig.h>
#include <utils/ShaderDiskCache.h>
#include <utils/ShaderPreprocessor.h>
#include <utils/ResourceLocator.h>
#include <glaze/glaze.hpp>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <src/tint/api/tint.h>
#include <src/tint/lang/core/type/struct.h>
#include <src/tint/lang/wgsl/ast/identifier.h>
#include <src/tint/lang/wgsl/ast/module.h>
#include <src/tint/lang/wgsl/ast/variable.h>
#include <src/tint/lang/wgsl/inspector/inspector.h>
#include <src/tint/lang/wgsl/reader/reader.h>
#include <src/tint/lang/wgsl/sem/variable.h>

#include <mutex>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace t850::webgpu {
struct CacheRecord {
  uint32_t version = 1;
  std::string sourceKey;
  std::string checksum;
  std::string wgsl;
};

namespace {
void InitializeTint() {
  static std::once_flag initialized;
  std::call_once(initialized, [] { tint::Initialize(); });
}

const char* StageName(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex: return "vertex";
  case ShaderStage::Fragment: return "fragment";
  case ShaderStage::Compute: return "compute";
  }
  return "invalid";
}

EShLanguage Language(ShaderStage stage) {
  switch (stage) {
  case ShaderStage::Vertex: return EShLangVertex;
  case ShaderStage::Fragment: return EShLangFragment;
  case ShaderStage::Compute: return EShLangCompute;
  }
  throw std::runtime_error("Invalid shader stage");
}

std::string Context(const ShaderRequest& request, const std::string& reason) {
  std::ostringstream message;
  message << "[WebGPU shader] " << request.name << " entry=" << request.entryPoint
          << " key=0x" << std::hex << request.keyBits << ": " << reason
          << "\ndefines:\n" << request.defines;
  return message.str();
}
}

std::string CompilerSignature() {
  return std::string("T850_WGSL_V1;") + T850_DAWN_SHADER_ABI;
}

const char* ShaderFlowName(ShaderFlow flow) {
  switch (flow) {
  case ShaderFlow::Auto: return "auto";
  case ShaderFlow::Wgsl: return "wgsl";
  case ShaderFlow::Spirv: return "spirv";
  }
  return "invalid";
}

bool ParseShaderFlow(const std::string& name, ShaderFlow& flow) {
  for (const auto candidate : {ShaderFlow::Auto, ShaderFlow::Wgsl, ShaderFlow::Spirv}) {
    if (name == ShaderFlowName(candidate)) {
      flow = candidate;
      return true;
    }
  }
  return false;
}

bool LoadShaderFiles(const ShaderFileRequest& request, ShaderArtifact& artifact,
                     ShaderFlowReport& report, std::string& diagnostic, const std::string& specialization) {
  artifact = {};
  report = {};
  report.requestedFlow = request.flow;
  diagnostic.clear();
  const auto start = std::chrono::steady_clock::now();
  const auto elapsed = [](auto begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
  };
  ShaderRequest stageRequest;
  stageRequest.stage = request.stage;
  stageRequest.layout = request.layout;
  stageRequest.name = request.name;
  stageRequest.entryPoint = request.entryPoint;
  stageRequest.defines = request.defines;
  stageRequest.keyBits = request.keyBits;
  try {
    const std::filesystem::path family(request.name);
    const auto extension = family.extension().string();
    if (family.filename().empty() || (!extension.empty() && extension != ".hlsl" && extension != ".wgsl"))
      throw std::runtime_error("Shader file name must be a family stem, .wgsl or .hlsl path");
    if (request.entryPoint.empty()) throw std::runtime_error("Shader entry point is required");
    if (request.stage != ShaderStage::Vertex && request.stage != ShaderStage::Fragment && request.stage != ShaderStage::Compute)
      throw std::runtime_error("Invalid shader stage");
    if (request.layout != BindingLayout::GraphicsV1 &&
        request.layout != BindingLayout::BlurV1 &&
        request.layout != BindingLayout::ComputeV1)
      throw std::runtime_error("Invalid binding layout");
    const bool computeLayout = request.layout == BindingLayout::BlurV1 ||
                               request.layout == BindingLayout::ComputeV1;
    if ((request.stage == ShaderStage::Compute) != computeLayout)
      throw std::runtime_error("Shader stage and binding layout are incompatible");
    if (request.flow != ShaderFlow::Auto && request.flow != ShaderFlow::Wgsl && request.flow != ShaderFlow::Spirv)
      throw std::runtime_error("Invalid shader flow");
    const auto attempt = [&](ShaderSourceLanguage language) {
      ShaderFlowAttempt result;
      result.sourceLanguage = language;
      stageRequest.name = std::filesystem::path(family).replace_extension(language == ShaderSourceLanguage::Wgsl ? ".wgsl" : ".hlsl").generic_string();
      stageRequest.sourceLanguage = language;
      stageRequest.source.clear();
      result.sourceName = stageRequest.name;
      const auto attemptStart = std::chrono::steady_clock::now();
      ShaderArtifact candidate;
      try {
        if (!ResourceLocator::Instance().ReadText(stageRequest.name, stageRequest.source) || stageRequest.source.empty())
          result.diagnostic = Context(stageRequest, "Shader source is missing, unreadable or empty");
        else
          result.succeeded = LoadOrTranslateShader(stageRequest, candidate, result.diagnostic, specialization);
      } catch (const std::exception& error) {
        result.diagnostic = Context(stageRequest, error.what());
      }
      result.elapsedMilliseconds = elapsed(attemptStart);
      result.cacheHit = candidate.cacheHit;
      result.preparationMilliseconds = candidate.translationMilliseconds;
      if (result.succeeded) artifact = std::move(candidate);
      const bool succeeded = result.succeeded;
      report.attempts.push_back(std::move(result));
      return succeeded;
    };
    bool succeeded = false;
    if (request.flow != ShaderFlow::Spirv) succeeded = attempt(ShaderSourceLanguage::Wgsl);
    if (!succeeded && request.flow != ShaderFlow::Wgsl) {
      report.fallbackAttempted = request.flow == ShaderFlow::Auto;
      succeeded = attempt(ShaderSourceLanguage::Hlsl);
    }
    if (report.fallbackAttempted)
      diagnostic = succeeded ? "WGSL failed; using HLSL/SPIR-V fallback.\n" : "WGSL and HLSL/SPIR-V fallback both failed.\n";
    for (const auto& result : report.attempts) {
      if (!result.diagnostic.empty()) {
        if (!diagnostic.empty() && diagnostic.back() != '\n') diagnostic += '\n';
        diagnostic += result.diagnostic;
      }
    }
    report.elapsedMilliseconds = elapsed(start);
    return succeeded;
  } catch (const std::exception& error) {
    artifact = {};
    diagnostic = Context(stageRequest, error.what());
    report.elapsedMilliseconds = elapsed(start);
    return false;
  }
}

bool LoadOrTranslateShader(const ShaderRequest& request, ShaderArtifact& artifact, std::string& diagnostic,
                           const std::string& specialization) {
  static std::mutex cacheMutex;
  std::lock_guard<std::mutex> lock(cacheMutex);
  artifact = {};
  const std::string signature = CompilerSignature() + ";layout=" + std::to_string(static_cast<int>(request.layout))
    + ";language=" + std::to_string(static_cast<int>(request.sourceLanguage))
    + ";specialization=" + specialization;
  const auto key = ShaderDiskCache::MakeStageKey("webgpu", signature, request.keyBits, StageName(request.stage),
    request.entryPoint, request.name, request.defines + "\n" + request.source);
  try {
    std::vector<uint8_t> bytes;
    if (ShaderDiskCache::LoadArtifact(key, "stage.wgsl.json", bytes)) {
      CacheRecord record;
      const std::string json(bytes.begin(), bytes.end());
      if (!glz::read_json(record, json) && record.version == 1 && record.sourceKey == key.sha1
          && !record.wgsl.empty() && record.checksum == ShaderDiskCache::ContentHash(record.wgsl)) {
        artifact.wgsl = std::move(record.wgsl);
        if (ReflectShader(request, artifact, diagnostic)) {
          artifact.cacheHit = true;
          return true;
        }
      }
    }
  } catch (const std::filesystem::filesystem_error&) {
  }
  const auto start = std::chrono::steady_clock::now();
  const bool translated = TranslateShader(request, artifact, diagnostic);
  artifact.translationMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  if (!translated) return false;
  try {
    CacheRecord record{1, key.sha1, ShaderDiskCache::ContentHash(artifact.wgsl), artifact.wgsl};
    std::string json;
    if (!glz::write_json(record, json)) {
      artifact.cacheStored = ShaderDiskCache::StoreArtifact(key, "stage.wgsl.json", json.data(), json.size());
      if (artifact.cacheStored) ShaderDiskCache::WriteManifest(key, signature);
    }
  } catch (const std::filesystem::filesystem_error&) {
    artifact.cacheStored = false;
  }
  if (!artifact.cacheStored) diagnostic = Context(request, "Translation succeeded, but the disk cache could not be written");
  return true;
}

bool ReflectShader(const ShaderRequest& request, ShaderArtifact& artifact, std::string& diagnostic) {
  InitializeTint();
  artifact.bindings.clear();
  artifact.workgroupSize = {};
  artifact.inputs.clear();
  artifact.outputs.clear();
  artifact.writesDepth = false;
  tint::Source::File file(request.name, artifact.wgsl);
  tint::wgsl::reader::Options readerOptions;
  readerOptions.allowed_features.features.insert(tint::wgsl::LanguageFeature::kUnrestrictedPointerParameters);
  auto program = tint::wgsl::reader::Parse(&file, readerOptions);
  if (!program.IsValid()) {
    diagnostic = Context(request, program.Diagnostics().Str());
    return false;
  }
  tint::inspector::Inspector inspector(program);
  const auto entries = inspector.GetEntryPoints();
  if (entries.size() != 1 || entries[0].name != request.entryPoint) {
    diagnostic = Context(request, "Expected exactly the requested entry point");
    return false;
  }
  using PipelineStage = tint::inspector::PipelineStage;
  const auto expectedStage = request.stage == ShaderStage::Vertex ? PipelineStage::kVertex
    : request.stage == ShaderStage::Fragment ? PipelineStage::kFragment : PipelineStage::kCompute;
  if (entries[0].stage != expectedStage) {
    diagnostic = Context(request, "Entry-point stage mismatch");
    return false;
  }
  const auto locations = [](const std::vector<tint::inspector::StageVariable>& variables) {
    std::vector<ShaderLocation> result;
    for (const auto& variable : variables) {
      if (!variable.attributes.location) continue;
      ShaderLocation value;
      value.location = *variable.attributes.location;
      using Composition = tint::inspector::CompositionType;
      switch (variable.composition_type) {
      case Composition::kScalar: value.components = 1; break;
      case Composition::kVec2: value.components = 2; break;
      case Composition::kVec3: value.components = 3; break;
      case Composition::kVec4: value.components = 4; break;
      default: break;
      }
      using Component = tint::inspector::ComponentType;
      if (variable.component_type == Component::kU32) value.type = ShaderComponentType::Unsigned;
      if (variable.component_type == Component::kI32) value.type = ShaderComponentType::Signed;
      if (variable.component_type == Component::kF16) value.type = ShaderComponentType::Half;
      result.push_back(value);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.location < right.location; });
    return result;
  };
  artifact.inputs = locations(entries[0].input_variables);
  artifact.outputs = locations(entries[0].output_variables);
  artifact.writesDepth = entries[0].frag_depth_used;
  if (request.stage == ShaderStage::Compute) {
    if (!entries[0].workgroup_size) {
      diagnostic = Context(request, "Workgroup overrides must be specialized before compilation");
      return false;
    }
    const auto& workgroup = *entries[0].workgroup_size;
    artifact.workgroupSize = {workgroup.x, workgroup.y, workgroup.z};
  }
  using Binding = tint::inspector::ResourceBinding;
  for (const auto& resource : inspector.GetResourceBindings(request.entryPoint)) {
    ShaderBinding binding{};
    binding.group = resource.bind_group;
    binding.binding = resource.binding;
    switch (resource.resource_type) {
    case Binding::ResourceType::kUniformBuffer:
      binding.kind = ResourceKind::UniformBuffer;
      binding.minimumBufferSize = resource.size;
      for (const auto* variable : program.AST().GlobalVariables()) {
        if (variable->name->symbol.Name() != resource.variable_name) continue;
        const auto* global = program.Sem().Get<tint::sem::GlobalVariable>(variable);
        const auto* structure = global->Type()->UnwrapRef()->As<tint::core::type::Struct>();
        if (structure) {
          for (const auto* member : structure->Members()) binding.uniformMemberOffsets.push_back(member->Offset());
        }
      }
      break;
    case Binding::ResourceType::kReadOnlyStorageBuffer:
      binding.kind = ResourceKind::ReadOnlyStorageBuffer;
      binding.minimumBufferSize = resource.size;
      break;
    case Binding::ResourceType::kStorageBuffer:
      binding.kind = ResourceKind::ReadWriteStorageBuffer;
      binding.minimumBufferSize = resource.size;
      break;
    case Binding::ResourceType::kSampledTexture:
      binding.kind = ResourceKind::SampledTexture;
      break;
    case Binding::ResourceType::kSampler:
      binding.kind = ResourceKind::Sampler;
      binding.comparisonSampler = resource.sampler_type == Binding::SamplerType::kComparison;
      break;
    case Binding::ResourceType::kWriteOnlyStorageTexture:
      binding.kind = ResourceKind::WriteOnlyStorageTexture;
      binding.storageRgba8Unorm = resource.image_format == Binding::TexelFormat::kRgba8Unorm;
      binding.storageRgba16Float = resource.image_format == Binding::TexelFormat::kRgba16Float;
      break;
    default:
      diagnostic = Context(request, "Resource kind is not supported by the stage-two compiler");
      return false;
    }
    if (binding.kind == ResourceKind::SampledTexture || binding.kind == ResourceKind::WriteOnlyStorageTexture) {
      switch (resource.dim) {
      case Binding::TextureDimension::k1d: binding.dimension = TextureDimension::D1; break;
      case Binding::TextureDimension::k2d: binding.dimension = TextureDimension::D2; break;
      case Binding::TextureDimension::k2dArray: binding.dimension = TextureDimension::D2Array; break;
      case Binding::TextureDimension::k3d: binding.dimension = TextureDimension::D3; break;
      case Binding::TextureDimension::kCube: binding.dimension = TextureDimension::Cube; break;
      case Binding::TextureDimension::kCubeArray: binding.dimension = TextureDimension::CubeArray; break;
      default: break;
      }
    }
    if (binding.kind == ResourceKind::SampledTexture) {
      if (resource.sampled_kind == Binding::SampledKind::kUInt) binding.sampledType = ShaderComponentType::Unsigned;
      if (resource.sampled_kind == Binding::SampledKind::kSInt) binding.sampledType = ShaderComponentType::Signed;
    }
    artifact.bindings.push_back(binding);
  }
  if (inspector.has_error()) {
    diagnostic = Context(request, inspector.error());
    return false;
  }
  diagnostic.clear();
  return true;
}

bool TranslateShader(const ShaderRequest& request, ShaderArtifact& artifact, std::string& diagnostic) {
  artifact = {};
  try {
    if (request.sourceLanguage == ShaderSourceLanguage::Wgsl) {
      if (!PreprocessShader(request.source, request.name, request.defines, artifact.wgsl, diagnostic)) {
        diagnostic = Context(request, diagnostic);
        return false;
      }
      return ReflectShader(request, artifact, diagnostic);
    }
    static std::once_flag initialized;
    std::call_once(initialized, [] {
      if (!glslang::InitializeProcess()) throw std::runtime_error("glslang initialization failed");
    });
    InitializeTint();
    if (request.source.empty() || request.entryPoint.empty()) throw std::runtime_error("Canonical source and entry point are required");
    const bool computeLayout = request.layout == BindingLayout::BlurV1 ||
                               request.layout == BindingLayout::ComputeV1;
    if ((request.stage == ShaderStage::Compute) != computeLayout)
      throw std::runtime_error("Shader stage and binding layout are incompatible");
    const auto stage = Language(request.stage);
    glslang::TShader shader(stage);
    const std::string source = "#define T850_SPIRV 1\n" + request.defines + "\n" + request.source;
    const char* text = source.c_str();
    shader.setStrings(&text, 1);
    shader.setEntryPoint(request.entryPoint.c_str());
    shader.setSourceEntryPoint(request.entryPoint.c_str());
    shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);
    shader.setTextureSamplerTransformMode(EShTexSampTransKeep);
    const bool compute = request.stage == ShaderStage::Compute;
    const bool legacyBlur = request.layout == BindingLayout::BlurV1;
    shader.setShiftBinding(glslang::EResTexture, legacyBlur ? 1 : 0);
    shader.setShiftBinding(glslang::EResSampler, compute ? 0 : 32);
    shader.setShiftBinding(glslang::EResUbo, compute ? 0 : 64);
    shader.setShiftBinding(glslang::EResUav, legacyBlur ? 2 : (compute ? 0 : 96));
    shader.setShiftBinding(glslang::EResImage, legacyBlur ? 2 : (compute ? 0 : 96));
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);
    if (!shader.parse(GetDefaultResources(), 100, false, messages)) throw std::runtime_error(shader.getInfoLog());
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages) || !program.mapIO()) throw std::runtime_error(program.getInfoLog());
    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
    tint::wgsl::writer::Options writerOptions;
    writerOptions.allowed_features.features.insert(tint::wgsl::LanguageFeature::kUnrestrictedPointerParameters);
    auto result = tint::SpirvToWgsl(spirv, writerOptions);
    if (result != tint::Success) throw std::runtime_error(result.Failure().reason);
    artifact.wgsl = result.Get();
    return ReflectShader(request, artifact, diagnostic);
  } catch (const std::exception& error) {
    diagnostic = Context(request, error.what());
    return false;
  }
}
}
#endif