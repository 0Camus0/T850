/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanDeviceContext.h: Device Context
*********************************************************/

#ifndef T800_VULKANDEVICECONTEXT_H
#define T800_VULKANDEVICECONTEXT_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID)

#if defined(OS_WINDOWS)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(OS_ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

namespace t850 {

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
    void SetPrimitiveTopology(Topology::E topology) override;
    void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) override;

    VkCommandBuffer GetCommandBuffer() const { return m_commandBuffer; }
    VkPrimitiveTopology GetTopology() const { return m_topology; }
    uint32_t GetVertexStride() const { return m_vertexStride; }
    void SetVertexStride(uint32_t stride) { m_vertexStride = stride; }

  private:
    friend class VulkanDriver;
    VkCommandBuffer     m_commandBuffer = VK_NULL_HANDLE;
    VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    uint32_t            m_vertexStride = 0;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANDEVICECONTEXT_H
