#pragma once

#include <video/BaseDriver.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct SceneProps;

namespace t850 {

enum class ComputeKernelId {
  Arithmetic,
  ImagePatternWrite,
  ImagePatternRead,
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
  const std::string_view* permutations;
  size_t permutationCount;
};

struct ComputeKernelConstantsContext {
  ::SceneProps* sceneProps = nullptr;
  uint32_t outputWidth = 0;
  uint32_t outputHeight = 0;
  std::string_view permutation;
};

const ComputeKernelDefinition* FindComputeKernel(std::string_view sourceName);
bool SupportsComputePermutation(const ComputeKernelDefinition& kernel,
                                std::string_view permutation);
void ConfigureComputePipelineDesc(const ComputeKernelDefinition& kernel,
                                  ComputePipelineDesc& pipeline);
bool BuildComputeConstants(const ComputeKernelDefinition& kernel,
                           const ComputeKernelConstantsContext& context,
                           std::vector<uint32_t>& constants,
                           std::string& error);

} // namespace t850