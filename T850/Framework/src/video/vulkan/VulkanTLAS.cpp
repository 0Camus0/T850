#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Ray Tracing
 * VulkanTLAS.cpp: Top-level acceleration structure
 *********************************************************/

#include <video/vulkan/VulkanDriver.h>

#if defined(OS_WINDOWS)

#include <utils/Log.h>
#include <cstring>

namespace t850 {

  static VulkanDriver* GetVkDrv()   { return static_cast<VulkanDriver*>(g_pBaseDriver); }
  static VulkanRTFunctions& GetRT() { return GetVkDrv()->m_rtFuncs; }

  // ─────────────────────────────────────────────────────
  void VulkanTLAS::EnsureBuffers(uint32_t instanceCount) {
    auto* drv = GetVkDrv();
    VkDevice device = drv->GetDevice();
    VmaAllocator allocator = drv->GetAllocator();
    auto& rt = GetRT();

    VkAccelerationStructureGeometryKHR geom = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                      VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geom;

    uint32_t maxPrim = m_maxInstances;
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    rt.vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &buildInfo, &maxPrim, &sizeInfo);

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
      asCI.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
      rt.vkCreateAccelerationStructureKHR(device, &asCI, nullptr, &m_as);

      VkAccelerationStructureDeviceAddressInfoKHR addrInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
      addrInfo.accelerationStructure = m_as;
      m_deviceAddress = rt.vkGetAccelerationStructureDeviceAddressKHR(device, &addrInfo);
    }
    if (!m_scratchBuffer) {
      VkBufferCreateInfo scratchCI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      scratchCI.size = sizeInfo.buildScratchSize;
      scratchCI.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      VmaAllocationCreateInfo scratchAllocCI = {}; scratchAllocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
      vmaCreateBuffer(allocator, &scratchCI, &scratchAllocCI, &m_scratchBuffer, &m_scratchAlloc, nullptr);
    }
    // Instance buffer (host-visible, written each frame)
    if (!m_instanceBuffer) {
      VkBufferCreateInfo instCI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      instCI.size = sizeof(VkAccelerationStructureInstanceKHR) * m_maxInstances;
      instCI.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      VmaAllocationCreateInfo instAllocCI = {};
      instAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
      instAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
      VmaAllocationInfo instInfo = {};
      vmaCreateBuffer(allocator, &instCI, &instAllocCI, &m_instanceBuffer, &m_instanceAlloc, &instInfo);
      m_instanceMapped = instInfo.pMappedData;
    }
    m_initialized = true;
  }

  void VulkanTLAS::Build(const RTInstanceDesc* instances, uint32_t instanceCount) {
    if (instanceCount > m_maxInstances) instanceCount = m_maxInstances;
    EnsureBuffers(instanceCount);
    if (!m_initialized) return;

    auto* drv = GetVkDrv();
    VkDevice device = drv->GetDevice();
    auto& rt = GetRT();

    // Convert RTInstanceDesc → VkAccelerationStructureInstanceKHR
    // The layout is compatible for the transform (3x4 float) and flags, but
    // blasGPUAddress goes into accelerationStructureReference.
    auto* vkInsts = static_cast<VkAccelerationStructureInstanceKHR*>(m_instanceMapped);
    for (uint32_t i = 0; i < instanceCount; i++) {
      const auto& src = instances[i];
      auto& dst = vkInsts[i];
      std::memcpy(dst.transform.matrix, src.transform, sizeof(src.transform));
      dst.instanceCustomIndex              = src.instanceID;
      dst.mask                             = src.instanceMask;
      dst.instanceShaderBindingTableRecordOffset = src.instanceContribToHitGroupIndex;
      dst.flags                            = src.flags;
      dst.accelerationStructureReference  = src.blasGPUAddress;
    }

    // Get device address of instance buffer
    VkBufferDeviceAddressInfo addrInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    addrInfo.buffer = m_instanceBuffer;
    VkDeviceAddress instAddr = vkGetBufferDeviceAddress(device, &addrInfo);

    VkBufferDeviceAddressInfo scratchAddrInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    scratchAddrInfo.buffer = m_scratchBuffer;
    VkDeviceAddress scratchAddr = vkGetBufferDeviceAddress(device, &scratchAddrInfo);

    VkAccelerationStructureGeometryKHR geom = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geom.geometry.instances.data.deviceAddress = instAddr;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                      VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = m_as;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geom;
    buildInfo.scratchData.deviceAddress = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
    rangeInfo.primitiveCount = instanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;

    VkCommandBuffer cmd = drv->GetCmdBuffer();
    rt.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange);

    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
      0, 1, &barrier, 0, nullptr, 0, nullptr);

    T8_LOG_TRACE("[VkTLAS] Built (%u instances)", instanceCount);
  }

  void VulkanTLAS::Destroy() {
    auto* drv = GetVkDrv();
    VkDevice device = drv->GetDevice();
    VmaAllocator allocator = drv->GetAllocator();
    auto& rt = GetRT();
    if (m_as)             { rt.vkDestroyAccelerationStructureKHR(device, m_as, nullptr); m_as = VK_NULL_HANDLE; }
    if (m_resultBuffer)   { vmaDestroyBuffer(allocator, m_resultBuffer, m_resultAlloc); m_resultBuffer = VK_NULL_HANDLE; }
    if (m_scratchBuffer)  { vmaDestroyBuffer(allocator, m_scratchBuffer, m_scratchAlloc); m_scratchBuffer = VK_NULL_HANDLE; }
    if (m_instanceBuffer) { vmaDestroyBuffer(allocator, m_instanceBuffer, m_instanceAlloc); m_instanceBuffer = VK_NULL_HANDLE; }
    m_deviceAddress = 0;
    m_initialized = false;
  }

  uint64_t VulkanTLAS::GetGPUAddress() const { return m_deviceAddress; }

} // namespace t850

#endif // OS_WINDOWS
