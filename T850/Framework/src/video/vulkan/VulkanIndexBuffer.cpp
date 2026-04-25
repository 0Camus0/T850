#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanIndexBuffer.cpp: Index Buffer implementation
 *********************************************************/

#include <video/vulkan/VulkanIndexBuffer.h>
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
  //  Vulkan Buffers — Index Buffer
  // ══════════════════════════════════════════════════════

  void* VulkanIndexBuffer::GetAPIObject() const { return (void*)m_buffer; }
  void** VulkanIndexBuffer::GetAPIObjectReference() const { return nullptr; }

  void VulkanIndexBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    auto* driver = GetVkDriver();
    VmaAllocator allocator = driver->GetAllocator();

    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = desc.byteWidth;
    bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo = {};
    VkResult res = vmaCreateBuffer(allocator, &bufInfo, &allocCI, &m_buffer, &m_allocation, &allocInfo);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] IB create failed res=%d", res);
      return;
    }
    m_mappedData = allocInfo.pMappedData;

    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      if (m_mappedData) memcpy(m_mappedData, initialData, desc.byteWidth);
    }
    T8_LOG_DEBUG("[Vulkan] IB created: %d bytes", desc.byteWidth);
  }

  void VulkanIndexBuffer::Set(const DeviceContext& deviceContext, const unsigned offset, IndexBufferFormat::E format) {
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&deviceContext)->GetCommandBuffer();
    VkIndexType idxType = (format == IndexBufferFormat::R16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd, m_buffer, (VkDeviceSize)offset, idxType);
  }

  void VulkanIndexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext) {
    if (m_mappedData && !sysMemCpy.empty())
      memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }

  void VulkanIndexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }

  void VulkanIndexBuffer::release() {
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
