/*********************************************************
 * T850 Engine — Vulkan Ray Tracing
 *
 * VulkanBLAS.h: Bottom-level acceleration structure (VK_KHR_ray_tracing_pipeline)
 *********************************************************/

#ifndef T800_VULKANBLAS_H
#define T800_VULKANBLAS_H

#include <Config.h>
#include <video/AccelStructure.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <cstdint>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  VulkanBLAS — one triangle-geometry BLAS per mesh subset
  // ══════════════════════════════════════════════════════
  class VulkanBLAS : public BLAS {
  public:
    uint64_t vertexDeviceAddress = 0;   // VkBuffer device address (vkGetBufferDeviceAddress)
    uint64_t indexDeviceAddress  = 0;
    uint32_t vertexCount  = 0;
    uint32_t indexCount   = 0;
    uint32_t vertexStride = 0;
    bool     is32BitIndex = true;

    void Build(bool allowUpdate = false) override;
    void Refit() override;
    void Destroy() override;
    uint64_t GetGPUAddress() const override;

  private:
    VkAccelerationStructureKHR m_as = VK_NULL_HANDLE;
    VkBuffer     m_resultBuffer     = VK_NULL_HANDLE;
    VmaAllocation m_resultAlloc     = VK_NULL_HANDLE;
    VkBuffer     m_scratchBuffer    = VK_NULL_HANDLE;
    VmaAllocation m_scratchAlloc    = VK_NULL_HANDLE;
    uint64_t     m_deviceAddress    = 0;  // vkGetAccelerationStructureDeviceAddressKHR result
    VkBuildAccelerationStructureFlagsKHR m_buildFlags = 0;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANBLAS_H
