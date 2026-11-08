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
  // Monotonic frame counter, incremented once per RenderGraph::Execute. Particle kernels use
  // it to stamp per-tile state so stale tiles from earlier frames are ignored without a clear.
  uint32_t frameIndex = 0;
  std::string_view permutation;
  // Set for particle passes so the splat constants builder knows which ParticleSystem this
  // dispatch draws (its count, radius, blend, colours). Empty for every other kernel.
  std::string_view particleSystem;
  // Numeric stage from the pass (RenderPassDesc::compute_stage). 0 for ordinary passes.
  int passStage = 0;
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