#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Ray Tracing
 * VulkanBLAS.cpp: Bottom-level acceleration structure
 *********************************************************/

#include <video/vulkan/VulkanDriver.h>

#if defined(OS_WINDOWS)

#include <utils/Log.h>

namespace t850 {

  // ─────────────────────────────────────────────────────
  //  Helpers
  // ─────────────────────────────────────────────────────
  static VulkanDriver* GetVkDrv()   { return static_cast<VulkanDriver*>(g_pBaseDriver); }
  static VulkanRTFunctions& GetRT() { return GetVkDrv()->m_rtFuncs; }

  // ─────────────────────────────────────────────────────
  void VulkanBLAS::Build(bool allowUpdate) {
    auto& rt = GetRT();
    if (!rt.IsValid()) return;

    auto* drv = GetVkDrv();
    VkDevice device = drv->GetDevice();
    VmaAllocator allocator = drv->GetAllocator();

    m_buildFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (allowUpdate) {
      m_buildFlags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                     VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    }

    VkAccelerationStructureGeometryKHR geom = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geom.geometry.triangles.vertexData.deviceAddress = vertexDeviceAddress;
    geom.geometry.triangles.vertexStride = vertexStride;
    geom.geometry.triangles.maxVertex = vertexCount - 1;
    geom.geometry.triangles.indexType = is32BitIndex ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    geom.geometry.triangles.indexData.deviceAddress = indexDeviceAddress;
    geom.geometry.triangles.transformData.deviceAddress = 0; // identity (no transform)

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = m_buildFlags;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geom;

    uint32_t primitiveCount = indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    rt.vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &buildInfo, &primitiveCount, &sizeInfo);

    // Create result buffer + AS object
    if (!m_resultBuffer) {
      VkBufferCreateInfo bufCI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      bufCI.size = sizeInfo.accelerationStructureSize;
      bufCI.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      VmaAllocationCreateInfo allocCI = {}; allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
      vmaCreateBuffer(allocator, &bufCI, &allocCI, &m_resultBuffer, &m_resultAlloc, nullptr);

      VkAccelerationStructureCreateInfoKHR asCI = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
      asCI.buffer = m_resultBuffer;
      asCI.size = sizeInfo.accelerationStructureSize;
      asCI.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      rt.vkCreateAccelerationStructureKHR(device, &asCI, nullptr, &m_as);

      VkAccelerationStructureDeviceAddressInfoKHR addrInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
      addrInfo.accelerationStructure = m_as;
      m_deviceAddress = rt.vkGetAccelerationStructureDeviceAddressKHR(device, &addrInfo);
    }

    // Create / reuse scratch buffer
    if (!m_scratchBuffer || true) { // always recreate scratch for simplicity
      if (m_scratchBuffer) { vmaDestroyBuffer(allocator, m_scratchBuffer, m_scratchAlloc); m_scratchBuffer = VK_NULL_HANDLE; }
      VkBufferCreateInfo scratchCI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      scratchCI.size = sizeInfo.buildScratchSize;
      scratchCI.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      VmaAllocationCreateInfo scratchAlloc = {}; scratchAlloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
      vmaCreateBuffer(allocator, &scratchCI, &scratchAlloc, &m_scratchBuffer, &m_scratchAlloc, nullptr);
    }

    // Get scratch device address
    VkBufferDeviceAddressInfo scratchAddrInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    scratchAddrInfo.buffer = m_scratchBuffer;
    VkDeviceAddress scratchAddress = vkGetBufferDeviceAddress(device, &scratchAddrInfo);

    buildInfo.dstAccelerationStructure = m_as;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
    rangeInfo.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    VkCommandBuffer cmd = drv->GetCmdBuffer();
    rt.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);

    // Barrier
    VkMemoryBarrier memBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    memBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    isBuilt = true;
    T8_LOG_DEBUG("[VkBLAS] Built (verts=%u, idx=%u)", vertexCount, indexCount);
  }

  void VulkanBLAS::Refit() {
    // For simplicity, full rebuild with update flag
    Build(true);
  }

  void VulkanBLAS::Destroy() {
    auto* drv = GetVkDrv();
    VkDevice device = drv->GetDevice();
    VmaAllocator allocator = drv->GetAllocator();
    if (m_as)            { GetRT().vkDestroyAccelerationStructureKHR(device, m_as, nullptr); m_as = VK_NULL_HANDLE; }
    if (m_resultBuffer)  { vmaDestroyBuffer(allocator, m_resultBuffer, m_resultAlloc); m_resultBuffer = VK_NULL_HANDLE; }
    if (m_scratchBuffer) { vmaDestroyBuffer(allocator, m_scratchBuffer, m_scratchAlloc); m_scratchBuffer = VK_NULL_HANDLE; }
    m_deviceAddress = 0;
    isBuilt = false;
  }

  uint64_t VulkanBLAS::GetGPUAddress() const { return m_deviceAddress; }

} // namespace t850

#endif // OS_WINDOWS
