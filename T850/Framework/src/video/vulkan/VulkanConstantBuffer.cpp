#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanConstantBuffer.cpp: Constant Buffer implementation
 *********************************************************/

#include <video/vulkan/VulkanConstantBuffer.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanDeviceContext.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS)

#include <utils/Log.h>
#include <cstring>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  Vulkan Buffers — Constant Buffer
  // ══════════════════════════════════════════════════════

  void* VulkanConstantBuffer::GetAPIObject() const { return (void*)m_buffer; }
  void** VulkanConstantBuffer::GetAPIObjectReference() const { return nullptr; }

  void VulkanConstantBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    auto* driver = GetVkDriver();
    VmaAllocator allocator = driver->GetAllocator();

    // Align to physical device's minUniformBufferOffsetAlignment (typically 64–256 on desktop GPUs)
    VkPhysicalDeviceProperties props = {};
    vkGetPhysicalDeviceProperties(driver->GetPhysicalDevice(), &props);
    VkDeviceSize uboAlign = props.limits.minUniformBufferOffsetAlignment;
    if (uboAlign == 0) uboAlign = 256;
    m_alignedSize = (uint32_t)((desc.byteWidth + (uboAlign - 1)) & ~(uboAlign - 1));

    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = m_alignedSize;
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo = {};
    VkResult res = vmaCreateBuffer(allocator, &bufInfo, &allocCI, &m_buffer, &m_allocation, &allocInfo);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CB create failed res=%d size=%u", res, m_alignedSize);
      return;
    }
    m_mappedData = allocInfo.pMappedData;

    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      if (m_mappedData) memcpy(m_mappedData, initialData, desc.byteWidth);
    }
    T8_LOG_DEBUG("[Vulkan] CB created: %d bytes (aligned=%u)", desc.byteWidth, m_alignedSize);
  }

  void VulkanConstantBuffer::Set(const DeviceContext& deviceContext) {
    const_cast<DeviceContext*>(&deviceContext)->actualConstantBuffer = (ConstantBuffer*)this;
    auto* driver = GetVkDriver();
    if (!sysMemCpy.empty()) {
      driver->m_pendingCB = driver->AllocateCBData(sysMemCpy.data(), (uint32_t)sysMemCpy.size());
      driver->m_cbDirty = true;
      T8_LOG_TRACE("[Vulkan] CB::Set offset=%llu dataSize=%u", driver->m_pendingCB.offset, (uint32_t)sysMemCpy.size());
    }
  }

  void VulkanConstantBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext) {
    if (m_mappedData && !sysMemCpy.empty())
      memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }

  void VulkanConstantBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }

  void VulkanConstantBuffer::release() {
    auto* driver = GetVkDriver();
    if (m_buffer && driver->GetAllocator()) {
      vmaDestroyBuffer(driver->GetAllocator(), m_buffer, m_allocation);
    }
    m_buffer = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_mappedData = nullptr;
    sysMemCpy.clear();
    delete this;
  }

} // namespace t850

#endif // OS_WINDOWS
