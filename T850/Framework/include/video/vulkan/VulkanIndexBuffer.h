/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanIndexBuffer.h: Index Buffer
*********************************************************/

#ifndef T800_VULKANINDEXBUFFER_H
#define T800_VULKANINDEXBUFFER_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

namespace t850 {

  // Forward declarations
  class VulkanDevice;

  // ══════════════════════════════════════════════════════
  //  Vulkan Index Buffer
  // ══════════════════════════════════════════════════════
  class VulkanIndexBuffer : public IndexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext, const unsigned offset, IndexBufferFormat::E format = IndexBufferFormat::R32) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class VulkanDevice;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    VkBuffer        m_buffer = VK_NULL_HANDLE;
    VmaAllocation   m_allocation = VK_NULL_HANDLE;
    void*           m_mappedData = nullptr;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANINDEXBUFFER_H
