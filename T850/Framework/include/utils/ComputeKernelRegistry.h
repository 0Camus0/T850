#pragma once

#include <video/BaseDriver.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct SceneProps;

namespace t850 {

struct ComputeKernelConstantsContext {
  ::SceneProps* sceneProps = nullptr;
  uint32_t outputWidth = 0;
  uint32_t outputHeight = 0;
  std::string_view permutation;
};

using ComputePipelineConfigurator = void (*)(ComputePipelineDesc&, std::string_view);
using ComputeConstantsBuilder = bool (*)(const ComputeKernelConstantsContext&,
                                         std::vector<uint32_t>&, std::string&);

struct ComputeKernelDefinition {
  std::string id;
  std::string sourceName;
  std::string entryPoint;
  std::vector<ComputeBindingLayoutDesc> bindings;
  std::vector<std::string> permutations;
  ComputePipelineConfigurator configurePipeline = nullptr;
  ComputeConstantsBuilder buildConstants = nullptr;
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