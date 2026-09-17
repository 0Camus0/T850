#include <pch.h>

#include <utils/ComputeKernelRegistry.h>

namespace t850 {
namespace {

constexpr ComputeBindingLayoutDesc kArithmeticBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 4},
  {ComputeBindingType::ReadWriteBuffer, 0, 1, 0},
};

constexpr ComputeBindingLayoutDesc kGodRaysBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 52},
  {ComputeBindingType::ReadOnlyTexture, 0, 1, 0},
  {ComputeBindingType::ReadOnlyTexture, 1, 2, 0},
  {ComputeBindingType::Sampler, 0, 3, 0},
  {ComputeBindingType::Sampler, 1, 4, 0},
  {ComputeBindingType::ReadWriteTexture, 0, 5, 0},
};

constexpr ComputeBindingLayoutDesc kBlurBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 32},
  {ComputeBindingType::ReadOnlyTexture, 0, 1, 0},
  {ComputeBindingType::Sampler, 0, 2, 0},
  {ComputeBindingType::ReadWriteTexture, 0, 3, 0},
};

constexpr ComputeBindingLayoutDesc kBrightBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 8},
  {ComputeBindingType::ReadOnlyTexture, 0, 1, 0},
  {ComputeBindingType::ReadOnlyTexture, 1, 2, 0},
  {ComputeBindingType::Sampler, 0, 3, 0},
  {ComputeBindingType::Sampler, 1, 4, 0},
  {ComputeBindingType::ReadWriteTexture, 0, 5, 0},
};

constexpr ComputeBindingLayoutDesc kHDRCompositeBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 8},
  {ComputeBindingType::ReadOnlyTexture, 0, 1, 0},
  {ComputeBindingType::ReadOnlyTexture, 1, 2, 0},
  {ComputeBindingType::ReadOnlyTexture, 2, 3, 0},
  {ComputeBindingType::Sampler, 0, 4, 0},
  {ComputeBindingType::Sampler, 1, 5, 0},
  {ComputeBindingType::Sampler, 2, 6, 0},
  {ComputeBindingType::ReadWriteTexture, 0, 7, 0},
};

constexpr ComputeBindingLayoutDesc kTorchParticleBindings[] = {
  {ComputeBindingType::Constants32, 0, 0, 28},
  {ComputeBindingType::ReadWriteTexture, 0, 1, 0},
};

constexpr ComputeKernelDefinition kKernels[] = {
  {ComputeKernelId::Arithmetic, "CS_Arithmetic.hlsl", "CS",
   kArithmeticBindings, std::size(kArithmeticBindings)},
  {ComputeKernelId::GodRays, "CS_GodRays.hlsl", "CS",
   kGodRaysBindings, std::size(kGodRaysBindings)},
  {ComputeKernelId::Blur, "CS_Blur.hlsl", "CS",
   kBlurBindings, std::size(kBlurBindings)},
  {ComputeKernelId::Bright, "CS_Bright.hlsl", "CS",
   kBrightBindings, std::size(kBrightBindings)},
  {ComputeKernelId::HDRComposite, "CS_HDRComposite.hlsl", "CS",
   kHDRCompositeBindings, std::size(kHDRCompositeBindings)},
  {ComputeKernelId::TorchParticles, "CS_TorchParticles.hlsl", "CS",
   kTorchParticleBindings, std::size(kTorchParticleBindings)},
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

void ConfigureComputePipelineDesc(const ComputeKernelDefinition& kernel,
                                  ComputePipelineDesc& pipeline) {
  pipeline.bindings.assign(kernel.bindings, kernel.bindings + kernel.bindingCount);
}

} // namespace t850