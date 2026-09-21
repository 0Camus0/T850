#include <pch.h>
#include <debug/RuntimeTelemetry.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanCompute.h>
#include <video/vulkan/VulkanTexture.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX)
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <utils/Log.h>
#include <utils/ShaderPermutationDump.h>
#include <utils/SPIRVReflection.h>

#include <cstring>
#include <sstream>
#include <unordered_set>

namespace t850 {
extern DeviceContext* T8DeviceContext;
namespace {
  bool ReadLocalSize(const std::vector<uint32_t>& spirv,
                     std::array<uint32_t, 3>& threadGroupSize) {
    constexpr uint16_t kOpExecutionMode = 16;
    constexpr uint32_t kExecutionModeLocalSize = 17;
    for (size_t word = 5; word < spirv.size();) {
      const uint16_t opcode = static_cast<uint16_t>(spirv[word] & 0xffffu);
      const uint16_t wordCount = static_cast<uint16_t>(spirv[word] >> 16u);
      if (!wordCount || word + wordCount > spirv.size())
        return false;
      if (opcode == kOpExecutionMode && wordCount >= 6 &&
          spirv[word + 2] == kExecutionModeLocalSize) {
        threadGroupSize = {spirv[word + 3], spirv[word + 4], spirv[word + 5]};
        return threadGroupSize[0] && threadGroupSize[1] && threadGroupSize[2];
      }
      word += wordCount;
    }
    return false;
  }

  bool CompileCompute(const ComputePipelineDesc& desc,
                      std::vector<uint32_t>& spirv,
                      std::array<uint32_t, 3>& threadGroupSize) {
    T8_TELEMETRY_SCOPE("shader.compile");
    T8_TELEMETRY_ADD("shader.cache.uncached", 1);
    static bool initialized = false;
    if (!initialized) { glslang::InitializeProcess(); initialized = true; }
    std::ostringstream combined;
    combined << "#define T850_VULKAN 1\n#define T850_SPIRV 1\n";
    for (const std::string& define : desc.defines)
      if (!define.empty()) combined << "#define " << define << '\n';
    combined << desc.source;
    const std::string source = combined.str();
    const char* text = source.c_str();
    EShMessages messages = static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);
    glslang::TShader shader(EShLangCompute);
    shader.setStrings(&text, 1);
    shader.setEntryPoint(desc.entryPoint.c_str());
    shader.setSourceEntryPoint(desc.entryPoint.c_str());
    shader.setEnvInput(glslang::EShSourceHlsl, EShLangCompute, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);
    if (!shader.parse(GetDefaultResources(), 100, false, messages)) {
      T8_LOG_ERROR("[Vulkan][Compute] Shader parse failed '%s': %s", desc.debugName.c_str(), shader.getInfoLog());
      return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
      T8_LOG_ERROR("[Vulkan][Compute] Program link failed '%s': %s", desc.debugName.c_str(), program.getInfoLog());
      return false;
    }
    const glslang::TIntermediate* intermediate = program.getIntermediate(EShLangCompute);
    if (!intermediate)
      return false;
    std::vector<unsigned int> generated;
    glslang::GlslangToSpv(*intermediate, generated);
    spirv.assign(generated.begin(), generated.end());
    if (!ReadLocalSize(spirv, threadGroupSize)) {
      T8_LOG_ERROR("[Vulkan][Compute] Shader '%s' has an invalid thread-group size",
                   desc.debugName.c_str());
      return false;
    }
    return !spirv.empty();
  }

  VkDescriptorType DescriptorType(ComputeBindingType type) {
    switch (type) {
      case ComputeBindingType::Constants32: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      case ComputeBindingType::ReadOnlyBuffer:
      case ComputeBindingType::ReadWriteBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      case ComputeBindingType::ReadOnlyTexture: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      case ComputeBindingType::ReadWriteTexture: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      case ComputeBindingType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
  }

  VkImageAspectFlags TextureAspect(const VulkanTexture& texture) {
    return texture.m_format == VK_FORMAT_D32_SFLOAT || texture.m_format == VK_FORMAT_D16_UNORM
      ? VK_IMAGE_ASPECT_DEPTH_BIT
      : VK_IMAGE_ASPECT_COLOR_BIT;
  }
}

VulkanComputePipeline::~VulkanComputePipeline() {
  if (!owner || !owner->GetDevice()) return;
  owner->RetireComputeResource([device = owner->GetDevice(), nativePipeline = pipeline,
      nativeLayout = pipelineLayout, nativeBindings = descriptorSetLayout] {
    if (nativePipeline) vkDestroyPipeline(device, nativePipeline, nullptr);
    if (nativeLayout) vkDestroyPipelineLayout(device, nativeLayout, nullptr);
    if (nativeBindings) vkDestroyDescriptorSetLayout(device, nativeBindings, nullptr);
  });
}

const ComputeBindingLayoutDesc* VulkanComputePipeline::Find(ComputeBindingType type, uint32_t shaderRegister) const {
  for (const auto& binding : bindings)
    if (binding.type == type && binding.shaderRegister == shaderRegister) return &binding;
  return nullptr;
}

bool VulkanComputePipeline::Create(VulkanDriver* driver, const ComputePipelineDesc& desc) {
  if (!driver || !driver->GetDevice() || desc.source.empty() || desc.bindings.empty()) return false;
  owner = driver;
  bindings = desc.bindings;
  std::unordered_set<uint32_t> descriptorBindings;
  std::unordered_set<uint64_t> logicalBindings;
  for (const ComputeBindingLayoutDesc& binding : bindings) {
    const uint64_t logicalKey =
      (static_cast<uint64_t>(binding.type) << 32u) | binding.shaderRegister;
    if (!descriptorBindings.insert(binding.bindingIndex).second ||
        !logicalBindings.insert(logicalKey).second ||
        (binding.type == ComputeBindingType::Constants32 && binding.constantCount == 0)) {
      T8_LOG_ERROR("[Vulkan][Compute] Invalid or duplicate binding in pipeline '%s'",
                   desc.debugName.c_str());
      return false;
    }
  }
  std::vector<uint32_t> spirv;
  if (!CompileCompute(desc, spirv, threadGroupSize)) return false;
  std::vector<ComputeBindingLayoutDesc> reflected;
  if (!ReflectComputeBindings(spirv, desc, reflected, threadGroupSize) ||
      !SetValidatedLayout(desc, reflected, true)) return false;
  VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
  moduleInfo.pCode = spirv.data();
  VkShaderModule module = VK_NULL_HANDLE;
  if (T8_TELEMETRY_CALL("shader.module.create", vkCreateShaderModule(driver->GetDevice(), &moduleInfo, nullptr, &module)) != VK_SUCCESS) return false;

  std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
  for (const auto& binding : bindings) {
    VkDescriptorSetLayoutBinding vkBinding{};
    vkBinding.binding = binding.bindingIndex;
    vkBinding.descriptorType = DescriptorType(binding.type);
    vkBinding.descriptorCount = 1;
    vkBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    layoutBindings.push_back(vkBinding);
  }
  VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  setInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
  setInfo.pBindings = layoutBindings.data();
  if (vkCreateDescriptorSetLayout(driver->GetDevice(), &setInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
    vkDestroyShaderModule(driver->GetDevice(), module, nullptr); return false;
  }
  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &descriptorSetLayout;
  if (vkCreatePipelineLayout(driver->GetDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
    vkDestroyShaderModule(driver->GetDevice(), module, nullptr); return false;
  }
  VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipelineInfo.stage.module = module;
  pipelineInfo.stage.pName = desc.entryPoint.c_str();
  pipelineInfo.layout = pipelineLayout;
  const VkResult result = T8_TELEMETRY_CALL("pipeline.create.compute", vkCreateComputePipelines(driver->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
  vkDestroyShaderModule(driver->GetDevice(), module, nullptr);
  if (result != VK_SUCCESS) return false;
  ShaderPermutationDump::RecordCompute(desc.debugName, desc.entryPoint, desc.permutationName, desc.defines);
  T8_LOG_INFO("[Vulkan][Compute] Pipeline '%s' created (bindings=%zu)", desc.debugName.c_str(), bindings.size());
  return true;
}

VulkanComputeBuffer::~VulkanComputeBuffer() {
  if (owner && buffer) owner->RetireComputeResource(
    [allocator = owner->GetAllocator(), nativeBuffer = buffer, nativeAllocation = allocation] {
      vmaDestroyBuffer(allocator, nativeBuffer, nativeAllocation);
    });
}

bool VulkanComputeBuffer::Create(VulkanDriver* driver, const ComputeBufferDesc& desc, const void* initialData) {
  if (!driver || !desc.byteWidth || !desc.structureStride || desc.byteWidth % desc.structureStride) return false;
  owner = driver;
  descriptor = desc;
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = desc.byteWidth;
  info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VmaAllocationCreateInfo allocationInfo{};
  allocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  if (vmaCreateBuffer(driver->GetAllocator(), &info, &allocationInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) return false;
  if (initialData) driver->UploadBufferData(buffer, initialData, desc.byteWidth);
  return true;
}

std::unique_ptr<ComputePipeline> VulkanDriver::CreateComputePipeline(const ComputePipelineDesc& desc) {
  auto pipeline = std::make_unique<VulkanComputePipeline>();
  return pipeline->Create(this, desc) ? std::move(pipeline) : nullptr;
}

std::unique_ptr<ComputeBuffer> VulkanDriver::CreateComputeBuffer(const ComputeBufferDesc& desc, const void* initialData) {
  auto buffer = std::make_unique<VulkanComputeBuffer>();
  return buffer->Create(this, desc, initialData) ? std::move(buffer) : nullptr;
}

bool VulkanDriver::DispatchCompute(ComputePipeline& pipelineBase, const std::vector<ComputeBindingDesc>& bindings,
                                   uint32_t gx, uint32_t gy, uint32_t gz) {
  auto* pipeline = dynamic_cast<VulkanComputePipeline*>(&pipelineBase);
  if (!pipeline || pipeline->owner != this || !pipeline->ValidateBindings(bindings) ||
      !gx || !gy || !gz || gx > 65535 || gy > 65535 || gz > 65535) return false;
  for (const auto& binding : bindings) {
    if (binding.buffer) {
      const auto* buffer = dynamic_cast<VulkanComputeBuffer*>(binding.buffer);
      if (!buffer || buffer->owner != this) return false;
    }
    if (binding.texture) {
      const auto* texture = dynamic_cast<VulkanTexture*>(binding.texture);
        if (!texture || !texture->m_imageView || !texture->m_image ||
          (texture->cil_props & CIL_CUBE_MAP)) return false;
      if (binding.type == ComputeBindingType::Sampler && !texture->m_sampler) return false;
      if (binding.type == ComputeBindingType::ReadWriteTexture) {
        if (!texture->m_storageUsage) return false;
        for (const auto& layout : pipeline->bindingLayout) {
          if (layout.type != binding.type || layout.shaderRegister != binding.shaderRegister) continue;
          const auto format = layout.storageFormat == ComputeStorageFormat::Rgba16Float
            ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
          if (texture->m_format != format) return false;
        }
      }
    }
  }
  BeginFrame(FrameTargetMode::Offscreen);
  VkCommandBuffer cmd = GetCmdBuffer();
  EndRenderPassIfActive(cmd);
  VkDescriptorSet set = AllocateDescriptorSet(pipeline->descriptorSetLayout);
  if (!set) return false;
  std::vector<VkDescriptorBufferInfo> infos;
  std::vector<VkDescriptorImageInfo> imageInfos;
  std::vector<VkWriteDescriptorSet> writes;
  std::vector<VulkanTexture*> writtenTextures;
  std::unordered_set<const ComputeBindingLayoutDesc*> boundLayouts;
  infos.reserve(bindings.size()); imageInfos.reserve(bindings.size()); writes.reserve(bindings.size());
  for (const auto& binding : bindings) {
    const auto* layout = pipeline->Find(binding.type, binding.shaderRegister);
    if (!layout || !boundLayouts.insert(layout).second) return false;
    VkDescriptorBufferInfo info{};
    if (binding.type == ComputeBindingType::Constants32) {
      if (!binding.constants || binding.constantCount != layout->constantCount) return false;
      info = AllocateCBData(binding.constants, binding.constantCount * sizeof(uint32_t));
    } else if (binding.type == ComputeBindingType::ReadOnlyBuffer || binding.type == ComputeBindingType::ReadWriteBuffer) {
      auto* buffer = dynamic_cast<VulkanComputeBuffer*>(binding.buffer);
        if (!buffer || (binding.type == ComputeBindingType::ReadWriteBuffer &&
          buffer->descriptor.access != ComputeBufferAccess::ReadWrite)) return false;
      info.buffer = buffer->buffer; info.offset = 0; info.range = buffer->descriptor.byteWidth;
    }
    if (binding.type == ComputeBindingType::Constants32 ||
      binding.type == ComputeBindingType::ReadOnlyBuffer ||
      binding.type == ComputeBindingType::ReadWriteBuffer) {
      infos.push_back(info);
      VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      write.dstSet = set; write.dstBinding = layout->bindingIndex; write.descriptorCount = 1;
      write.descriptorType = DescriptorType(binding.type); write.pBufferInfo = &infos.back();
      writes.push_back(write);
      continue;
    }

    auto* texture = dynamic_cast<VulkanTexture*>(binding.texture);
    if (!texture || !texture->m_image || !texture->m_imageView) return false;
    VkDescriptorImageInfo imageInfo{};
    if (binding.type == ComputeBindingType::ReadOnlyTexture) {
      if (texture->GetLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        TransitionImageLayout(cmd, texture->m_image, texture->GetLayout(),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, TextureAspect(*texture));
        texture->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
      imageInfo.imageView = texture->m_imageView;
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if (binding.type == ComputeBindingType::ReadWriteTexture) {
      if (texture->GetLayout() != VK_IMAGE_LAYOUT_GENERAL) {
        TransitionImageLayout(cmd, texture->m_image, texture->GetLayout(),
                              VK_IMAGE_LAYOUT_GENERAL, TextureAspect(*texture));
        texture->SetLayout(VK_IMAGE_LAYOUT_GENERAL);
      }
      imageInfo.imageView = texture->m_imageView;
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      writtenTextures.push_back(texture);
    } else if (binding.type == ComputeBindingType::Sampler) {
      if (!texture->m_sampler) return false;
      imageInfo.sampler = texture->m_sampler;
    } else {
      return false;
    }
    imageInfos.push_back(imageInfo);
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set; write.dstBinding = layout->bindingIndex; write.descriptorCount = 1;
    write.descriptorType = DescriptorType(binding.type); write.pImageInfo = &imageInfos.back();
    writes.push_back(write);
  }
  if (writes.size() != pipeline->bindings.size() ||
      boundLayouts.size() != pipeline->bindings.size()) return false;
  vkUpdateDescriptorSets(GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipelineLayout, 0, 1, &set, 0, nullptr);
  vkCmdDispatch(cmd, gx, gy, gz);
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                       0, 1, &barrier, 0, nullptr, 0, nullptr);
  for (VulkanTexture* texture : writtenTextures) {
    TransitionImageLayout(cmd, texture->m_image, texture->GetLayout(),
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, TextureAspect(*texture));
    texture->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  m_lastPipeline = VK_NULL_HANDLE;
  m_lastPipelineLayout = VK_NULL_HANDLE;
  return true;
}

bool VulkanDriver::ReadComputeBuffer(ComputeBuffer& bufferBase, void* destination, size_t byteCount) {
  auto* source = dynamic_cast<VulkanComputeBuffer*>(&bufferBase);
  if (!source || source->owner != this || !destination || !byteCount || byteCount % 4 || byteCount > source->descriptor.byteWidth) return false;
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = byteCount; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VmaAllocationCreateInfo allocationInfo{};
  allocationInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
  allocationInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
  VkBuffer staging = VK_NULL_HANDLE; VmaAllocation allocation = VK_NULL_HANDLE; VmaAllocationInfo mapped{};
  if (vmaCreateBuffer(GetAllocator(), &info, &allocationInfo, &staging, &allocation, &mapped) != VK_SUCCESS) return false;
  const bool frameOpen = m_frameStarted;
  const bool passOpen = m_renderPassActive;
  const int target = CurrentRT;
  const auto viewport = m_viewport;
  const auto scissor = m_scissorRect;
  VkCommandBuffer cmd = frameOpen ? GetCmdBuffer() : GetTransientCommandBuffer();
  if (frameOpen) EndRenderPassIfActive(cmd);
  VkMemoryBarrier beforeCopy{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  beforeCopy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  beforeCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &beforeCopy, 0, nullptr, 0, nullptr);
  VkBufferCopy copy{0, 0, byteCount};
  vkCmdCopyBuffer(cmd, source->buffer, staging, 1, &copy);
  VkMemoryBarrier hostRead{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
    0, 1, &hostRead, 0, nullptr, 0, nullptr);
  if (frameOpen) {
    vkEndCommandBuffer(cmd);
    SubmitCurrentFrameAndWait(cmd);
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    m_lastPipeline = VK_NULL_HANDLE;
    m_lastPipelineLayout = VK_NULL_HANDLE;
    T8DeviceContext->actualShaderSet = nullptr;
    T8DeviceContext->actualConstantBuffer = nullptr;
    T8DeviceContext->actualIndexBuffer = nullptr;
    T8DeviceContext->actualVertexBuffer = nullptr;
    if (passOpen) {
      if (target >= 0) PushRTLoad(target);
      else EnsureBackbufferRenderPass();
    }
    m_viewport = viewport;
    m_scissorRect = scissor;
    vkCmdSetViewport(cmd, 0, 1, &m_viewport);
    vkCmdSetScissor(cmd, 0, 1, &m_scissorRect);
  } else {
    SubmitTransientCommandBuffer(cmd);
  }
  const bool mappedHere = mapped.pMappedData == nullptr;
  if (mappedHere && vmaMapMemory(GetAllocator(), allocation, &mapped.pMappedData) != VK_SUCCESS) {
    vmaDestroyBuffer(GetAllocator(), staging, allocation);
    return false;
  }
  vmaInvalidateAllocation(GetAllocator(), allocation, 0, byteCount);
  std::memcpy(destination, mapped.pMappedData, byteCount);
  if (mappedHere) vmaUnmapMemory(GetAllocator(), allocation);
  vmaDestroyBuffer(GetAllocator(), staging, allocation);
  return true;
}
}
#endif
