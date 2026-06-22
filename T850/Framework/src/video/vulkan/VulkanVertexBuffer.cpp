#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanVertexBuffer.cpp: Vertex Buffer implementation
 *********************************************************/

#include <video/vulkan/VulkanVertexBuffer.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanDeviceContext.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX)

#include <utils/Log.h>
#include <debug/RenderTrace.h>
#include <cstring>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  Vulkan Buffers — Vertex Buffer
  // ══════════════════════════════════════════════════════

  void* VulkanVertexBuffer::GetAPIObject() const { return (void*)m_buffer; }
  void** VulkanVertexBuffer::GetAPIObjectReference() const { return nullptr; }

  void VulkanVertexBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    auto* driver = GetVkDriver();
    VmaAllocator allocator = driver->GetAllocator();

    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = desc.byteWidth;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo = {};
    VkResult res = vmaCreateBuffer(allocator, &bufInfo, &allocCI, &m_buffer, &m_allocation, &allocInfo);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] VB create failed res=%d", res);
      return;
    }
    m_mappedData = allocInfo.pMappedData;

    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      if (m_mappedData) memcpy(m_mappedData, initialData, desc.byteWidth);
    }
    T8_LOG_DEBUG("[Vulkan] VB created: %d bytes", desc.byteWidth);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && initialData) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->RecordBufferUpdate(bufId, initialData, desc.byteWidth, "vb", "");
    }
#endif
  }

  void VulkanVertexBuffer::Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) {
    const_cast<DeviceContext*>(&deviceContext)->actualVertexBuffer = (VertexBuffer*)this;
    auto* vkContext = static_cast<VulkanDeviceContext*>(const_cast<DeviceContext*>(&deviceContext));
    vkContext->SetVertexStride(stride);
    VkCommandBuffer cmd = vkContext->GetCommandBuffer();
    if (m_usesRing) {
      T8_LOG_TRACE("[Vulkan] VB::Set stride=%u ringOffset=%llu size=%u", stride, m_ringOffset, descriptor.byteWidth);
      VkDeviceSize off = m_ringOffset;
      vkCmdBindVertexBuffers(cmd, 0, 1, &m_ringBuffer, &off);
    } else {
      T8_LOG_TRACE("[Vulkan] VB::Set stride=%u offset=%u size=%u", stride, offset, descriptor.byteWidth);
      VkDeviceSize off = offset;
      vkCmdBindVertexBuffers(cmd, 0, 1, &m_buffer, &off);
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->EvBindVertexBufferRequest(bufId, stride, offset);
    }
#endif
  }

  void VulkanVertexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext) {
    if (m_mappedData && !sysMemCpy.empty())
      memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && !sysMemCpy.empty()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->RecordBufferUpdate(bufId, sysMemCpy.data(), (uint32_t)sysMemCpy.size(), "vb", "");
    }
#endif
  }

  void VulkanVertexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    // Allocate from per-frame ring buffer so each draw gets its own copy
    auto* driver = GetVkDriver();
    auto alloc = driver->AllocateVBRing(buffer, descriptor.byteWidth);
    if (alloc.valid) {
      m_ringBuffer = alloc.buffer;
      m_ringOffset = alloc.offset;
      m_usesRing = true;
      // Rebind immediately so the next DrawIndexed uses the new allocation
      VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&deviceContext)->GetCommandBuffer();
      vkCmdBindVertexBuffers(cmd, 0, 1, &m_ringBuffer, &alloc.offset);
    } else {
      if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
      m_usesRing = false;
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->RecordBufferUpdate(bufId, buffer, descriptor.byteWidth, "vb", "");
    }
#endif
  }

  void VulkanVertexBuffer::release() {
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
