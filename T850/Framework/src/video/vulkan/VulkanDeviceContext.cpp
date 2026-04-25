#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanDeviceContext.cpp: DeviceContext implementation
 *********************************************************/

#include <video/vulkan/VulkanDeviceContext.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanShader.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS)

#include <utils/Log.h>
#include <debug/Profiler.h>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  VulkanDeviceContext
  // ══════════════════════════════════════════════════════

  void* VulkanDeviceContext::GetAPIObject() const { return (void*)m_commandBuffer; }
  void** VulkanDeviceContext::GetAPIObjectReference() const { return nullptr; }
  void VulkanDeviceContext::release() { m_commandBuffer = VK_NULL_HANDLE; }

  void VulkanDeviceContext::SetPrimitiveTopology(Topology::E topology) {
    switch (topology) {
      case Topology::LINE_LIST:      m_topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;      break;
      case Topology::LINE_STRIP:     m_topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;     break;
      case Topology::POINT_LIST:     m_topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;     break;
      case Topology::TRIANGLE_STRIP: m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
      case Topology::TRIANLE_LIST:
      default:                          m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;   break;
    }
  }

  void VulkanDeviceContext::DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) {
    if (!m_commandBuffer) return;
    T8_LOG_TRACE("[Vulkan] DrawIndexed(%u, %u, %u)", vertexCount, startIndex, startVertex);

    auto* driver = GetVkDriver();
    VulkanShader* shader = static_cast<VulkanShader*>(actualShaderSet);
    if (shader && shader->m_descriptorSetLayout)
      driver->BindPendingDescriptors(m_commandBuffer, shader);

    vkCmdDrawIndexed(m_commandBuffer, vertexCount, 1, startIndex, (int32_t)startVertex, 0);
#ifdef T8_ENABLE_PROFILER
    if (t850::g_profiler) t850::g_profiler->AddDrawCall(vertexCount);
#endif
  }

} // namespace t850

#endif // OS_WINDOWS
