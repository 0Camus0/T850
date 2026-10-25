#ifndef T800_VULKANCOMPUTE_H
#define T800_VULKANCOMPUTE_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX)
#include <vulkan/vulkan.h>
#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>
#endif

namespace t850 {
  class VulkanDriver;

  class VulkanComputePipeline final : public ComputePipeline {
  public:
    ~VulkanComputePipeline() override;
    bool Create(VulkanDriver* driver, const ComputePipelineDesc& desc);
    const ComputeBindingLayoutDesc* Find(ComputeBindingType type, uint32_t shaderRegister) const;

    VulkanDriver* owner = nullptr;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::vector<ComputeBindingLayoutDesc> bindings;
  };

  class VulkanComputeBuffer final : public ComputeBuffer {
  public:
    ~VulkanComputeBuffer() override;
    bool Create(VulkanDriver* driver, const ComputeBufferDesc& desc, const void* initialData);

    VulkanDriver* owner = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
  };

  class VulkanComputeTexture3D final : public ComputeTexture3D {
  public:
    ~VulkanComputeTexture3D() override;
    bool Create(VulkanDriver* driver, const ComputeTexture3DDesc& desc, const void* initialData);

    VulkanDriver* owner = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageLayout GetLayout() const { return layout; }
    void SetLayout(VkImageLayout next) { layout = next; }
  };
}
#endif
#endif
