/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanVertexBuffer.h: Vertex Buffer
*********************************************************/

#ifndef T800_VULKANVERTEXBUFFER_H
#define T800_VULKANVERTEXBUFFER_H

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
  //  Vulkan Vertex Buffer
  // ══════════════════════════════════════════════════════
  class VulkanVertexBuffer : public VertexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class VulkanDevice;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    VkBuffer        m_buffer = VK_NULL_HANDLE;
    VmaAllocation   m_allocation = VK_NULL_HANDLE;
    void*           m_mappedData = nullptr;
    // Ring-buffer suballocation for dynamic updates (GUI quads)
    VkBuffer        m_ringBuffer = VK_NULL_HANDLE;
    VkDeviceSize    m_ringOffset = 0;
    bool            m_usesRing = false;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANVERTEXBUFFER_H
