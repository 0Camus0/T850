/*********************************************************
 * T850 Engine — Vulkan Ray Tracing
 *
 * VulkanTLAS.h: Top-level acceleration structure (VK_KHR_acceleration_structure)
 *********************************************************/

#ifndef T800_VULKANTLAS_H
#define T800_VULKANTLAS_H

#include <Config.h>
#include <video/AccelStructure.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <cstdint>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  VulkanTLAS — scene top-level acceleration structure
  // ══════════════════════════════════════════════════════
  class VulkanTLAS : public TLAS {
  public:
    explicit VulkanTLAS(uint32_t maxInstances) : m_maxInstances(maxInstances) {}

    void Build(const RTInstanceDesc* instances, uint32_t instanceCount) override;
    void Destroy() override;
    uint64_t GetGPUAddress() const override;

    // Vulkan descriptor info for binding the AS to a descriptor set.
    VkAccelerationStructureKHR GetHandle() const { return m_as; }

  private:
    uint32_t m_maxInstances = 0;

    VkAccelerationStructureKHR m_as          = VK_NULL_HANDLE;
    VkBuffer     m_resultBuffer               = VK_NULL_HANDLE;
    VmaAllocation m_resultAlloc               = VK_NULL_HANDLE;
    VkBuffer     m_scratchBuffer              = VK_NULL_HANDLE;
    VmaAllocation m_scratchAlloc              = VK_NULL_HANDLE;
    VkBuffer     m_instanceBuffer             = VK_NULL_HANDLE;
    VmaAllocation m_instanceAlloc             = VK_NULL_HANDLE;
    void*        m_instanceMapped             = nullptr;
    uint64_t     m_deviceAddress              = 0;
    bool         m_initialized               = false;

    void EnsureBuffers(uint32_t instanceCount);
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANTLAS_H
