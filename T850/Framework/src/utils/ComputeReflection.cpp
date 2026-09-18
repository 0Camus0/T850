#include <pch.h>
#include <utils/SPIRVReflection.h>
#include <video/BaseDriver.h>
#include "../../../Librerias/spirv-reflect/spirv_reflect.h"

namespace t850 {
bool ReflectComputeBindings(const std::vector<uint32_t>& spirv, const ComputePipelineDesc& desc,
    std::vector<ComputeBindingLayoutDesc>& bindings, std::array<uint32_t, 3>& groupSize,
    bool combinedSamplers) {
  SpvReflectShaderModule module{};
  if (spvReflectCreateShaderModule(spirv.size() * sizeof(uint32_t), spirv.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
    return false;
  struct ModuleGuard {
    SpvReflectShaderModule& module;
    ~ModuleGuard() { spvReflectDestroyShaderModule(&module); }
  } guard{module};
  const auto* entry = spvReflectGetEntryPoint(&module, combinedSamplers ? "main" : desc.entryPoint.c_str());
  if (!entry || entry->shader_stage != SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT) return false;
  groupSize = {entry->local_size.x, entry->local_size.y, entry->local_size.z};
  if (!groupSize[0] || !groupSize[1] || !groupSize[2]) return false;
  uint32_t count = 0;
  if (spvReflectEnumerateDescriptorBindings(&module, &count, nullptr) != SPV_REFLECT_RESULT_SUCCESS) return false;
  std::vector<SpvReflectDescriptorBinding*> resources(count);
  if (spvReflectEnumerateDescriptorBindings(&module, &count, resources.data()) != SPV_REFLECT_RESULT_SUCCESS) return false;
  bindings.clear();
  for (const auto* resource : resources) {
    if (resource->set != 0 || resource->count != 1 || resource->array.dims_count) return false;
    ComputeBindingLayoutDesc binding;
    binding.bindingIndex = resource->binding;
    switch (resource->descriptor_type) {
      case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        if (!resource->block.padded_size || resource->block.padded_size % 4) return false;
        binding.type = ComputeBindingType::Constants32;
        binding.constantCount = resource->block.padded_size / 4;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        binding.type = (resource->decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE)
          ? ComputeBindingType::ReadOnlyBuffer : ComputeBindingType::ReadWriteBuffer;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        binding.type = ComputeBindingType::ReadOnlyTexture;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
        binding.type = ComputeBindingType::Sampler;
        break;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        binding.type = ComputeBindingType::ReadWriteTexture;
        if (!(resource->decoration_flags & SPV_REFLECT_DECORATION_NON_READABLE)) return false;
        if (resource->image.image_format == SpvImageFormatRgba8) binding.storageFormat = ComputeStorageFormat::Rgba8Unorm;
        else if (resource->image.image_format == SpvImageFormatRgba16f) binding.storageFormat = ComputeStorageFormat::Rgba16Float;
        else return false;
        break;
      default: return false;
    }
    if ((binding.type == ComputeBindingType::ReadOnlyTexture || binding.type == ComputeBindingType::ReadWriteTexture) &&
        (resource->image.dim != SpvDim2D || resource->image.arrayed || resource->image.ms)) return false;
    bindings.push_back(binding);
    if (combinedSamplers && resource->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
      for (const auto& texture : desc.bindings) {
        if (texture.type != ComputeBindingType::ReadOnlyTexture || texture.bindingIndex != resource->binding) continue;
        for (const auto& sampler : desc.bindings)
          if (sampler.type == ComputeBindingType::Sampler && sampler.shaderRegister == texture.shaderRegister)
            bindings.push_back(sampler);
      }
    }
  }
  return true;
}
}