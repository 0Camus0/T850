/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanConstantBuffer.h: Constant Buffer
*********************************************************/

#ifndef T800_VULKANCONSTANTBUFFER_H
#define T800_VULKANCONSTANTBUFFER_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <cstdint>

namespace t800 {

  // Forward declarations
  class VulkanDevice;

  // ══════════════════════════════════════════════════════
  //  Vulkan Constant Buffer
  // ══════════════════════════════════════════════════════
  class VulkanConstantBuffer : public ConstantBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class VulkanDevice;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    VkBuffer        m_buffer = VK_NULL_HANDLE;
    VmaAllocation   m_allocation = VK_NULL_HANDLE;
    void*           m_mappedData = nullptr;   // persistently mapped
    uint32_t        m_alignedSize = 0;
  };

} // namespace t800

#endif // OS_WINDOWS
#endif // T800_VULKANCONSTANTBUFFER_H
