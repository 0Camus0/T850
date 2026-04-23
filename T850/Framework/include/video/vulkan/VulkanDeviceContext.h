/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanDeviceContext.h: Device Context
*********************************************************/

#ifndef T800_VULKANDEVICECONTEXT_H
#define T800_VULKANDEVICECONTEXT_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

namespace t800 {

  // Forward declaration
  class VulkanDriver;

  // ══════════════════════════════════════════════════════
  //  Vulkan Device Context
  // ══════════════════════════════════════════════════════
  class VulkanDeviceContext : public DeviceContext {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void release() override;
    void SetPrimitiveTopology(T8_TOPOLOGY::E topology) override;
    void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) override;

    VkCommandBuffer GetCommandBuffer() const { return m_commandBuffer; }
    VkPrimitiveTopology GetTopology() const { return m_topology; }

  private:
    friend class VulkanDriver;
    VkCommandBuffer     m_commandBuffer = VK_NULL_HANDLE;
    VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  };

} // namespace t800

#endif // OS_WINDOWS
#endif // T800_VULKANDEVICECONTEXT_H
