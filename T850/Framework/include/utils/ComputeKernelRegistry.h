#pragma once

#include <video/BaseDriver.h>

#include <cstddef>
#include <string_view>

namespace t850 {

enum class ComputeKernelId {
  Arithmetic,
  GodRays,
  Blur,
  Bright,
  HDRComposite,
  TorchParticles
};

struct ComputeKernelDefinition {
  ComputeKernelId id;
  std::string_view sourceName;
  std::string_view entryPoint;
  const ComputeBindingLayoutDesc* bindings;
  size_t bindingCount;
};

const ComputeKernelDefinition* FindComputeKernel(std::string_view sourceName);
void ConfigureComputePipelineDesc(const ComputeKernelDefinition& kernel,
                                  ComputePipelineDesc& pipeline);

} // namespace t850