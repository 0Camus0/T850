#include <pch.h>

#include <utils/ComputeKernelRegistry.h>
#include <scene/SceneProp.h>
#include <utils/Camera.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace t850 {

struct ComputeBindingManifest {
  std::string type;
  int shader_register = 0;
  int binding_index = 0;
  int constant_count = 0;
  std::string storage_format = "unspecified";
  bool match_output_extent = false;
};

struct ComputeKernelManifestEntry {
  std::string id;
  std::string source;
  std::string entry = "CS";
  std::vector<std::string> permutations;
  std::vector<ComputeBindingManifest> bindings;
  std::string pipeline_configurator;
  std::string constants_builder;
};

struct ComputeKernelManifest {
  int version = 0;
  std::vector<ComputeKernelManifestEntry> kernels;
};

namespace {

constexpr uint32_t kMaxBlurWeights = 24;

struct GodRaysComputeConstants {
  XMATRIX44 WVPInverse;
  XMATRIX44 WVPLight;
  XVECTOR3 CameraPosition;
  XVECTOR3 SunDirectionAndBias;
  XVECTOR3 VolumeCenterAndEnabled;
  XVECTOR3 VolumeHalfExtentsAndFactor;
  XVECTOR3 OutputSizeStepsAndEnabled;
};
static_assert(sizeof(GodRaysComputeConstants) == 208);

struct BlurComputeConstants {
  uint32_t outputWidth;
  uint32_t outputHeight;
  uint32_t kernelSize;
  uint32_t direction;
  float radius;
  float padding[3];
  float weights[kMaxBlurWeights];
};
static_assert(sizeof(BlurComputeConstants) == 128);

struct PostProcessComputeConstants {
  float outputWidth;
  float outputHeight;
  float parameter0;
  float parameter1;
  float parameter2;
  float padding[3];
};
static_assert(sizeof(PostProcessComputeConstants) == 32);

struct ParticleComputeConstants {
  XMATRIX44 viewProjection;
  XVECTOR3 emitterAndTime;
  XVECTOR3 additionalEmitterXZ;
  XVECTOR3 outputSizeCountEnabled;
  XVECTOR3 motion;
  XVECTOR3 color0;
  XVECTOR3 color1;
  XVECTOR3 color2;
  XVECTOR3 shape;
  XVECTOR3 wobble;
  XVECTOR3 fade;
  XVECTOR3 intensity;
};
static_assert(sizeof(ParticleComputeConstants) == 240);

template <typename T>
void CopyWords(const T& value, std::vector<uint32_t>& words) {
  static_assert(sizeof(T) % sizeof(uint32_t) == 0);
  words.resize(sizeof(T) / sizeof(uint32_t));
  std::memcpy(words.data(), &value, sizeof(T));
}

uint32_t ConstantWordCount(const ComputeKernelDefinition& kernel) {
  for (size_t index = 0; index < kernel.bindings.size(); ++index) {
    if (kernel.bindings[index].type == ComputeBindingType::Constants32)
      return kernel.bindings[index].constantCount;
  }
  return 0;
}

void ConfigureArithmeticPipeline(ComputePipelineDesc& pipeline, std::string_view permutation) {
  if (permutation == "read-input")
    pipeline.bindings.push_back({ComputeBindingType::ReadOnlyBuffer, 0, 2, 0});
}

bool BuildGodRaysConstants(const ComputeKernelConstantsContext& context,
                           std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "God Rays constants require a primary camera";
    return false;
  }
  GodRaysComputeConstants value = {};
  camera->VP.Inverse(&value.WVPInverse);
  value.CameraPosition = camera->Eye;
  XVECTOR3 sunDirection(0.0f, 1.0f, 0.0f, props.ShadowBias);
  const bool hasLightCamera = props.ActiveLightCamera >= 0 &&
    props.ActiveLightCamera < static_cast<int>(props.pLightCameras.size()) &&
    props.pLightCameras[props.ActiveLightCamera];
  if (hasLightCamera) {
    value.WVPLight = props.pLightCameras[props.ActiveLightCamera]->VP;
    XVECTOR3 direction = -props.pLightCameras[props.ActiveLightCamera]->Look;
    direction.Normalize();
    sunDirection.x = direction.x;
    sunDirection.y = direction.y;
    sunDirection.z = direction.z;
  }
  value.SunDirectionAndBias = sunDirection;
  value.VolumeCenterAndEnabled = props.GodRaysVolumeCenter;
  value.VolumeCenterAndEnabled.w = static_cast<float>(props.GodRaysVolumeEnabled);
  value.VolumeHalfExtentsAndFactor = props.GodRaysVolumeHalfExtents;
  value.VolumeHalfExtentsAndFactor.w = props.GodRaysFactor;
  value.OutputSizeStepsAndEnabled = XVECTOR3(
    static_cast<float>(context.outputWidth), static_cast<float>(context.outputHeight),
    (std::max)(props.LightVolumeSteps, 2.0f),
    props.ToogleGodRays && hasLightCamera ? 1.0f : 0.0f);
  CopyWords(value, constants);
  return true;
}

bool BuildBlurConstants(const ComputeKernelConstantsContext& context,
                        std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  const bool validKernel = props.ActiveGaussKernel >= 0 &&
    props.ActiveGaussKernel < static_cast<int>(props.pGaussKernels.size()) &&
    props.pGaussKernels[props.ActiveGaussKernel] &&
    !props.pGaussKernels[props.ActiveGaussKernel]->vGaussKernel.empty();
  if (!validKernel) {
    error = "Blur constants require the active Gaussian kernel";
    return false;
  }
  const GaussFilter& gaussian = *props.pGaussKernels[props.ActiveGaussKernel];
  const uint32_t kernelSize = static_cast<uint32_t>(gaussian.vGaussKernel[0].x);
  if (!kernelSize || kernelSize > kMaxBlurWeights || gaussian.vGaussKernel.size() <= kernelSize) {
    error = "Blur Gaussian kernel exceeds the supported weight count";
    return false;
  }
  BlurComputeConstants value = {};
  value.outputWidth = context.outputWidth;
  value.outputHeight = context.outputHeight;
  value.kernelSize = kernelSize;
  value.direction = context.permutation == "vertical" ? 1u : 0u;
  value.radius = gaussian.vGaussKernel[0].y;
  for (uint32_t index = 0; index < kernelSize; ++index) {
    const float weight = gaussian.vGaussKernel[index + 1].x;
    value.weights[index] = std::round(weight * 1000000.0f) / 1000000.0f;
  }
  CopyWords(value, constants);
  return true;
}

bool BuildPostProcessConstants(const ComputeKernelConstantsContext& context,
                               bool composite, std::vector<uint32_t>& constants) {
  SceneProps& props = *context.sceneProps;
  PostProcessComputeConstants value = {};
  value.outputWidth = static_cast<float>(context.outputWidth);
  value.outputHeight = static_cast<float>(context.outputHeight);
  value.parameter0 = composite ? props.BloomFactor : props.BloomThreshold;
  value.parameter1 = props.Exposure;
  value.parameter2 = props.ToneMapWhiteLevel;
  CopyWords(value, constants);
  return true;
}

bool BuildBrightConstants(const ComputeKernelConstantsContext& context,
                          std::vector<uint32_t>& constants, std::string&) {
  return BuildPostProcessConstants(context, false, constants);
}

bool BuildHDRCompositeConstants(const ComputeKernelConstantsContext& context,
                                std::vector<uint32_t>& constants, std::string&) {
  return BuildPostProcessConstants(context, true, constants);
}

bool BuildTorchParticleConstants(const ComputeKernelConstantsContext& context,
                                 std::vector<uint32_t>& constants, std::string& error) {
  SceneProps& props = *context.sceneProps;
  Camera* camera = props.GetPrimaryCamera();
  if (!camera) {
    error = "Torch particle constants require a primary camera";
    return false;
  }
  ParticleComputeConstants value = {};
  value.viewProjection = camera->VP;
  value.emitterAndTime = props.ParticleEmitterPosition;
  value.emitterAndTime.w = props.ParticleTimeSeconds;
  value.additionalEmitterXZ = XVECTOR3(
    props.ParticleEmitterPosition1.x, props.ParticleEmitterPosition1.z,
    props.ParticleEmitterPosition2.x, props.ParticleEmitterPosition2.z);
  value.outputSizeCountEnabled = XVECTOR3(
    static_cast<float>(context.outputWidth), static_cast<float>(context.outputHeight),
    static_cast<float>(props.ParticleCount),
    static_cast<float>((std::max)(0, (std::min)(3, props.ParticleEmitterEnabled))));
  value.motion = XVECTOR3(props.ParticleLifetime, props.ParticleRiseHeight,
                          props.ParticleSpread, props.ParticleSize);
  value.color0 = props.ParticleColor0;
  value.color1 = props.ParticleColor1;
  value.color2 = props.ParticleColor2;
  value.shape = props.ParticleShape;
  value.wobble = props.ParticleWobble;
  value.fade = props.ParticleFade;
  value.intensity = XVECTOR3(props.ParticleFadeOutStart, props.ParticleIntensity, 0.0f, 0.0f);
  CopyWords(value, constants);
  return true;
}

bool ResolveBindingType(std::string_view value, ComputeBindingType& type) {
  if (value == "constants") type = ComputeBindingType::Constants32;
  else if (value == "read_only_buffer") type = ComputeBindingType::ReadOnlyBuffer;
  else if (value == "read_write_buffer") type = ComputeBindingType::ReadWriteBuffer;
  else if (value == "sampled_texture") type = ComputeBindingType::ReadOnlyTexture;
  else if (value == "storage_texture") type = ComputeBindingType::ReadWriteTexture;
  else if (value == "sampler") type = ComputeBindingType::Sampler;
  else return false;
  return true;
}

bool ResolveStorageFormat(std::string_view value, ComputeStorageFormat& format) {
  if (value == "unspecified") format = ComputeStorageFormat::Unspecified;
  else if (value == "rgba8unorm") format = ComputeStorageFormat::Rgba8Unorm;
  else if (value == "rgba16float") format = ComputeStorageFormat::Rgba16Float;
  else return false;
  return true;
}

ComputePipelineConfigurator ResolvePipelineConfigurator(std::string_view value) {
  if (value.empty()) return nullptr;
  return value == "arithmetic" ? ConfigureArithmeticPipeline : nullptr;
}

ComputeConstantsBuilder ResolveConstantsBuilder(std::string_view value) {
  if (value.empty()) return nullptr;
  if (value == "god_rays") return BuildGodRaysConstants;
  if (value == "blur") return BuildBlurConstants;
  if (value == "bright") return BuildBrightConstants;
  if (value == "hdr_composite") return BuildHDRCompositeConstants;
  if (value == "torch_particles") return BuildTorchParticleConstants;
  return nullptr;
}

const std::vector<ComputeKernelDefinition>& Kernels() {
  static const std::vector<ComputeKernelDefinition> kernels = [] {
    std::vector<ComputeKernelDefinition> result;
    std::string json;
    if (!ResourceLocator::Instance().ReadText("Shaders/compute_kernels.json", json)) {
      T8_LOG_ERROR("[ComputeKernelRegistry] Cannot read Shaders/compute_kernels.json");
      return result;
    }
    ComputeKernelManifest manifest;
    const auto error = glz::read<glz::opts{.error_on_unknown_keys = true}>(manifest, json);
    if (error || manifest.version != 1 || manifest.kernels.empty()) {
      T8_LOG_ERROR("[ComputeKernelRegistry] Invalid manifest: %s",
        error ? glz::format_error(error, json).c_str() : "unsupported version or empty kernel list");
      return result;
    }
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> sources;
    result.reserve(manifest.kernels.size());
    for (const ComputeKernelManifestEntry& entry : manifest.kernels) {
      ComputeKernelDefinition kernel;
      if (entry.id.empty() || entry.source.empty() || entry.entry.empty() ||
          entry.permutations.empty() || entry.bindings.empty() ||
          !ids.insert(entry.id).second || !sources.insert(entry.source).second) {
        T8_LOG_ERROR("[ComputeKernelRegistry] Invalid or duplicate kernel '%s'", entry.source.c_str());
        return std::vector<ComputeKernelDefinition>{};
      }
      kernel.id = entry.id;
      kernel.sourceName = entry.source;
      kernel.entryPoint = entry.entry;
      kernel.permutations = entry.permutations;
      kernel.configurePipeline = ResolvePipelineConfigurator(entry.pipeline_configurator);
      kernel.buildConstants = ResolveConstantsBuilder(entry.constants_builder);
      if ((!entry.pipeline_configurator.empty() && !kernel.configurePipeline) ||
          (!entry.constants_builder.empty() && !kernel.buildConstants)) {
        T8_LOG_ERROR("[ComputeKernelRegistry] Kernel '%s' names an unknown callback", entry.source.c_str());
        return std::vector<ComputeKernelDefinition>{};
      }
      std::unordered_set<uint64_t> logicalBindings;
      std::unordered_set<uint32_t> nativeBindings;
      for (const ComputeBindingManifest& binding : entry.bindings) {
        ComputeBindingLayoutDesc layout;
        if (binding.shader_register < 0 || binding.binding_index < 0 || binding.constant_count < 0 ||
            !ResolveBindingType(binding.type, layout.type) ||
            !ResolveStorageFormat(binding.storage_format, layout.storageFormat)) {
          T8_LOG_ERROR("[ComputeKernelRegistry] Kernel '%s' has an invalid binding", entry.source.c_str());
          return std::vector<ComputeKernelDefinition>{};
        }
        layout.shaderRegister = static_cast<uint32_t>(binding.shader_register);
        layout.bindingIndex = static_cast<uint32_t>(binding.binding_index);
        layout.constantCount = static_cast<uint32_t>(binding.constant_count);
        layout.matchOutputExtent = binding.match_output_extent;
        const uint64_t logical = (static_cast<uint64_t>(layout.type) << 32u) | layout.shaderRegister;
        if (!logicalBindings.insert(logical).second || !nativeBindings.insert(layout.bindingIndex).second) {
          T8_LOG_ERROR("[ComputeKernelRegistry] Kernel '%s' has duplicate bindings", entry.source.c_str());
          return std::vector<ComputeKernelDefinition>{};
        }
        kernel.bindings.push_back(layout);
      }
      result.push_back(std::move(kernel));
    }
    T8_LOG_INFO("[ComputeKernelRegistry] Loaded %zu kernels from manifest", result.size());
    return result;
  }();
  return kernels;
}

std::string_view FileName(std::string_view path) {
  const size_t separator = path.find_last_of("/\\");
  return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

} // namespace

const ComputeKernelDefinition* FindComputeKernel(std::string_view sourceName) {
  const std::string_view fileName = FileName(sourceName);
  for (const ComputeKernelDefinition& kernel : Kernels()) {
    if (kernel.sourceName == fileName)
      return &kernel;
  }
  return nullptr;
}

bool SupportsComputePermutation(const ComputeKernelDefinition& kernel,
                                std::string_view permutation) {
  return std::find(kernel.permutations.begin(), kernel.permutations.end(), permutation) !=
         kernel.permutations.end();
}

void ConfigureComputePipelineDesc(const ComputeKernelDefinition& kernel,
                                  ComputePipelineDesc& pipeline) {
  pipeline.bindings = kernel.bindings;
  if (kernel.configurePipeline)
    kernel.configurePipeline(pipeline, pipeline.permutationName);
}

bool BuildComputeConstants(const ComputeKernelDefinition& kernel,
                           const ComputeKernelConstantsContext& context,
                           std::vector<uint32_t>& constants,
                           std::string& error) {
  constants.clear();
  if (!context.sceneProps || !context.outputWidth || !context.outputHeight) {
    error = "compute constants require scene properties and a nonzero output extent";
    return false;
  }
  if (!SupportsComputePermutation(kernel, context.permutation)) {
    error = "unsupported compute permutation '" + std::string(context.permutation) + "'";
    return false;
  }

  if (!kernel.buildConstants) {
    error = "kernel does not expose scene-driven constants";
    return false;
  }
  if (!kernel.buildConstants(context, constants, error)) return false;

  if (constants.size() != ConstantWordCount(kernel)) {
    error = "packed constant count does not match the registered shader layout";
    constants.clear();
    return false;
  }
  return true;
}

} // namespace t850