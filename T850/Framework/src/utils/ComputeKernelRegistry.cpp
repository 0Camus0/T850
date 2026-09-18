#include <pch.h>

#include <utils/ComputeKernelRegistry.h>
#include <scene/SceneProp.h>
#include <utils/Camera.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace t850 {
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
  for (size_t index = 0; index < kernel.bindingCount; ++index) {
    if (kernel.bindings[index].type == ComputeBindingType::Constants32)
      return kernel.bindings[index].constantCount;
  }
  return 0;
}

constexpr ComputeBindingLayoutDesc kArithmeticBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 4},
  {ComputeBindingType::ReadWriteBuffer, 0, 1, 0},
};

constexpr ComputeBindingLayoutDesc kImagePatternWriteBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 4},
  {ComputeBindingType::ReadWriteTexture, 0, 1, 0, ComputeStorageFormat::Rgba8Unorm},
};

constexpr ComputeBindingLayoutDesc kImagePatternReadBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 4},
  {ComputeBindingType::ReadOnlyTexture, 0, 1, 0, ComputeStorageFormat::Unspecified, true},
  {ComputeBindingType::ReadWriteBuffer, 0, 2, 0},
};

constexpr ComputeBindingLayoutDesc kGodRaysBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 52},
  {ComputeBindingType::ReadOnlyTexture, 0, 1, 0},
  {ComputeBindingType::ReadOnlyTexture, 1, 2, 0},
  {ComputeBindingType::Sampler, 0, 3, 0},
  {ComputeBindingType::Sampler, 1, 4, 0},
  {ComputeBindingType::ReadWriteTexture, 0, 5, 0, ComputeStorageFormat::Rgba8Unorm},
};

constexpr ComputeBindingLayoutDesc kBlurBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 32},
  {ComputeBindingType::ReadOnlyTexture, 0, 1, 0},
  {ComputeBindingType::Sampler, 0, 2, 0},
  {ComputeBindingType::ReadWriteTexture, 0, 3, 0, ComputeStorageFormat::Rgba8Unorm},
};

constexpr ComputeBindingLayoutDesc kBrightBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 8},
  {ComputeBindingType::ReadOnlyTexture, 0, 1, 0},
  {ComputeBindingType::ReadOnlyTexture, 1, 2, 0},
  {ComputeBindingType::Sampler, 0, 3, 0},
  {ComputeBindingType::Sampler, 1, 4, 0},
  {ComputeBindingType::ReadWriteTexture, 0, 5, 0, ComputeStorageFormat::Rgba8Unorm},
};

constexpr ComputeBindingLayoutDesc kHDRCompositeBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 8},
  {ComputeBindingType::ReadOnlyTexture, 0, 1, 0},
  {ComputeBindingType::ReadOnlyTexture, 1, 2, 0},
  {ComputeBindingType::ReadOnlyTexture, 2, 3, 0},
  {ComputeBindingType::Sampler, 0, 4, 0},
  {ComputeBindingType::Sampler, 1, 5, 0},
  {ComputeBindingType::Sampler, 2, 6, 0},
  {ComputeBindingType::ReadWriteTexture, 0, 7, 0, ComputeStorageFormat::Rgba8Unorm},
};

constexpr ComputeBindingLayoutDesc kTorchParticleBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 60},
  {ComputeBindingType::ReadWriteTexture, 0, 1, 0, ComputeStorageFormat::Rgba16Float},
  {ComputeBindingType::ReadOnlyTexture, 0, 2, 0, ComputeStorageFormat::Unspecified, true},
};

constexpr std::string_view kBasePermutation[] = {"base"};
constexpr std::string_view kArithmeticPermutations[] = {"base", "read-input"};
constexpr std::string_view kBlurPermutations[] = {"horizontal", "vertical"};

constexpr ComputeKernelDefinition kKernels[] = {
  {ComputeKernelId::Arithmetic, "CS_Arithmetic.hlsl", "CS",
   kArithmeticBindings, std::size(kArithmeticBindings),
    kArithmeticPermutations, std::size(kArithmeticPermutations)},
  {ComputeKernelId::ImagePatternWrite, "CS_ImagePatternWrite.hlsl", "CS",
   kImagePatternWriteBindings, std::size(kImagePatternWriteBindings),
   kBasePermutation, std::size(kBasePermutation)},
  {ComputeKernelId::ImagePatternRead, "CS_ImagePatternRead.hlsl", "CS",
   kImagePatternReadBindings, std::size(kImagePatternReadBindings),
   kBasePermutation, std::size(kBasePermutation)},
  {ComputeKernelId::GodRays, "CS_GodRays.hlsl", "CS",
   kGodRaysBindings, std::size(kGodRaysBindings),
   kBasePermutation, std::size(kBasePermutation)},
  {ComputeKernelId::Blur, "CS_Blur.hlsl", "CS",
   kBlurBindings, std::size(kBlurBindings),
   kBlurPermutations, std::size(kBlurPermutations)},
  {ComputeKernelId::Bright, "CS_Bright.hlsl", "CS",
   kBrightBindings, std::size(kBrightBindings),
   kBasePermutation, std::size(kBasePermutation)},
  {ComputeKernelId::HDRComposite, "CS_HDRComposite.hlsl", "CS",
   kHDRCompositeBindings, std::size(kHDRCompositeBindings),
   kBasePermutation, std::size(kBasePermutation)},
  {ComputeKernelId::TorchParticles, "CS_TorchParticles.hlsl", "CS",
   kTorchParticleBindings, std::size(kTorchParticleBindings),
   kBasePermutation, std::size(kBasePermutation)},
};

std::string_view FileName(std::string_view path) {
  const size_t separator = path.find_last_of("/\\");
  return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

} // namespace

const ComputeKernelDefinition* FindComputeKernel(std::string_view sourceName) {
  const std::string_view fileName = FileName(sourceName);
  for (const ComputeKernelDefinition& kernel : kKernels) {
    if (kernel.sourceName == fileName)
      return &kernel;
  }
  return nullptr;
}

bool SupportsComputePermutation(const ComputeKernelDefinition& kernel,
                                std::string_view permutation) {
  return std::find(kernel.permutations,
                   kernel.permutations + kernel.permutationCount,
                   permutation) != kernel.permutations + kernel.permutationCount;
}

void ConfigureComputePipelineDesc(const ComputeKernelDefinition& kernel,
                                  ComputePipelineDesc& pipeline) {
  pipeline.bindings.assign(kernel.bindings, kernel.bindings + kernel.bindingCount);
  if (kernel.id == ComputeKernelId::Arithmetic && pipeline.permutationName == "read-input")
    pipeline.bindings.push_back({ComputeBindingType::ReadOnlyBuffer, 0, 2, 0});
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

  SceneProps& props = *context.sceneProps;
  if (kernel.id == ComputeKernelId::GodRays) {
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
      static_cast<float>(context.outputWidth),
      static_cast<float>(context.outputHeight),
      (std::max)(props.LightVolumeSteps, 2.0f),
      props.ToogleGodRays && hasLightCamera ? 1.0f : 0.0f);
    CopyWords(value, constants);
  } else if (kernel.id == ComputeKernelId::Blur) {
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
    if (!kernelSize || kernelSize > kMaxBlurWeights ||
        gaussian.vGaussKernel.size() <= kernelSize) {
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
  } else if (kernel.id == ComputeKernelId::Bright ||
             kernel.id == ComputeKernelId::HDRComposite) {
    PostProcessComputeConstants value = {};
    value.outputWidth = static_cast<float>(context.outputWidth);
    value.outputHeight = static_cast<float>(context.outputHeight);
    value.parameter0 = kernel.id == ComputeKernelId::HDRComposite
      ? props.BloomFactor : props.BloomThreshold;
    value.parameter1 = props.Exposure;
    value.parameter2 = props.ToneMapWhiteLevel;
    CopyWords(value, constants);
  } else if (kernel.id == ComputeKernelId::TorchParticles) {
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
      static_cast<float>(context.outputWidth),
      static_cast<float>(context.outputHeight),
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
    value.intensity = XVECTOR3(props.ParticleFadeOutStart,
                   props.ParticleIntensity, 0.0f, 0.0f);
    CopyWords(value, constants);
  } else {
    error = "kernel does not expose scene-driven constants";
    return false;
  }

  if (constants.size() != ConstantWordCount(kernel)) {
    error = "packed constant count does not match the registered shader layout";
    constants.clear();
    return false;
  }
  return true;
}

} // namespace t850