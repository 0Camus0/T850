#include "pch.h"
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanDriver.cpp: Driver lifecycle, Device, DeviceContext,
 *                   Buffers (VB, IB, CB), PSO cache, Texture,
 *                   RenderTarget, Shader.
 *********************************************************/

#include <video/vulkan/VulkanDriver.h>

#if defined(OS_WINDOWS)

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <SDL3/SDL_vulkan.h>

#include <utils/Log.h>
#include <utils/SPIRVReflection.h>
#include <debug/T8_Profiler.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <cstring>
#include <algorithm>

namespace t800 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  Shared helpers
  // ══════════════════════════════════════════════════════
  static VulkanDriver* GetVkDriver() { return static_cast<VulkanDriver*>(g_pBaseDriver); }

  static void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                                    VkImageLayout oldLayout, VkImageLayout newLayout,
                                    VkImageAspectFlags aspectMask) {
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = 0;
    VkPipelineStageFlags dstStage = 0;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
      barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDeviceContext
  // ══════════════════════════════════════════════════════

  void* VulkanDeviceContext::GetAPIObject() const { return (void*)m_commandBuffer; }
  void** VulkanDeviceContext::GetAPIObjectReference() const { return nullptr; }
  void VulkanDeviceContext::release() { m_commandBuffer = VK_NULL_HANDLE; }

  void VulkanDeviceContext::SetPrimitiveTopology(T8_TOPOLOGY::E topology) {
    switch (topology) {
      case T8_TOPOLOGY::LINE_LIST:      m_topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;      break;
      case T8_TOPOLOGY::LINE_STRIP:     m_topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;     break;
      case T8_TOPOLOGY::POINT_LIST:     m_topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;     break;
      case T8_TOPOLOGY::TRIANGLE_STRIP: m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
      case T8_TOPOLOGY::TRIANLE_LIST:
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
    if (t800::g_profiler) t800::g_profiler->AddDrawCall(vertexCount);
#endif
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDevice
  // ══════════════════════════════════════════════════════

  void* VulkanDevice::GetAPIObject() const { return (void*)m_device; }
  void** VulkanDevice::GetAPIObjectReference() const { return nullptr; }
  void VulkanDevice::release() { m_device = VK_NULL_HANDLE; }

  Buffer* VulkanDevice::CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData) {
    T8_LOG_DEBUG("[Vulkan] CreateBuffer type=%d size=%d", bufferType, desc.byteWidth);
    Buffer* buf = nullptr;
    switch (bufferType) {
      case T8_BUFFER_TYPE::VERTEX:   buf = new VulkanVertexBuffer;   break;
      case T8_BUFFER_TYPE::INDEX:    buf = new VulkanIndexBuffer;    break;
      case T8_BUFFER_TYPE::CONSTANT: buf = new VulkanConstantBuffer; break;
    }
    if (buf) buf->Create(*this, desc, initialData);
    return buf;
  }

  ShaderBase* VulkanDevice::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key,
                                          const std::string& vs_name, const std::string& fs_name) {
    VulkanShader* sh = new VulkanShader();
    if (!sh->CreateShader(src_vs, src_fs, key, vs_name, fs_name)) {
      delete sh;
      return nullptr;
    }
    return sh;
  }

  Texture* VulkanDevice::CreateTexture(std::string path) {
    VulkanTexture* tex = new VulkanTexture;
    tex->LoadTexture(path.c_str());
    return tex;
  }

  Texture* VulkanDevice::CreateTextureFromMemory(const unsigned char* buff, int w, int h, int channels, std::string name) {
    VulkanTexture* tex = new VulkanTexture;
    tex->LoadFromMemory(buff, w, h, channels);
    return tex;
  }

  Texture* VulkanDevice::CreateCubeMap(const unsigned char* buff, int w, int h) {
    VulkanTexture* tex = new VulkanTexture;
    tex->CreateCubeMap(buff, w, h);
    return tex;
  }

  BaseRT* VulkanDevice::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips) {
    VulkanRT* rt = new VulkanRT;
    if (rt->LoadRT(nrt, cf, df, w, h, genMips)) return rt;
    delete rt;
    return nullptr;
  }

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
  }

  void VulkanVertexBuffer::Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) {
    const_cast<DeviceContext*>(&deviceContext)->actualVertexBuffer = (VertexBuffer*)this;
    VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&deviceContext)->GetCommandBuffer();
    if (m_usesRing) {
      T8_LOG_TRACE("[Vulkan] VB::Set stride=%u ringOffset=%llu size=%u", stride, m_ringOffset, descriptor.byteWidth);
      VkDeviceSize off = m_ringOffset;
      vkCmdBindVertexBuffers(cmd, 0, 1, &m_ringBuffer, &off);
    } else {
      T8_LOG_TRACE("[Vulkan] VB::Set stride=%u offset=%u size=%u", stride, offset, descriptor.byteWidth);
      VkDeviceSize off = offset;
      vkCmdBindVertexBuffers(cmd, 0, 1, &m_buffer, &off);
    }
  }

  void VulkanVertexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext) {
    if (m_mappedData && !sysMemCpy.empty())
      memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
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

  void VulkanIndexBuffer::Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format) {
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&deviceContext)->GetCommandBuffer();
    VkIndexType idxType = (format == T8_IB_FORMAR::R16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
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

  // ══════════════════════════════════════════════════════
  //  Vulkan Buffers — Constant Buffer
  // ══════════════════════════════════════════════════════

  void* VulkanConstantBuffer::GetAPIObject() const { return (void*)m_buffer; }
  void** VulkanConstantBuffer::GetAPIObjectReference() const { return nullptr; }

  void VulkanConstantBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    auto* driver = GetVkDriver();
    VmaAllocator allocator = driver->GetAllocator();

    // Align to minUniformBufferOffsetAlignment (typically 256)
    m_alignedSize = (desc.byteWidth + 255) & ~255u;

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

  // ══════════════════════════════════════════════════════
  //  VulkanTexture
  // ══════════════════════════════════════════════════════

  void VulkanTexture::LoadAPITexture(DeviceContext* context, unsigned char* buffer) {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    // Vulkan doesn't widely support RGB8 — always use RGBA8 for 3-channel textures
    unsigned char* uploadBuf = buffer;
    std::vector<unsigned char> rgbaTmp;
    int uploadChannels = m_channels;
    if (m_channels == 3) {
      uploadChannels = 4;
      rgbaTmp.resize((size_t)x * y * 4);
      for (unsigned int i = 0; i < x * y; i++) {
        rgbaTmp[i * 4 + 0] = buffer[i * 3 + 0];
        rgbaTmp[i * 4 + 1] = buffer[i * 3 + 1];
        rgbaTmp[i * 4 + 2] = buffer[i * 3 + 2];
        rgbaTmp[i * 4 + 3] = 255;
      }
      uploadBuf = rgbaTmp.data();
    }

    // Determine format and bytes per pixel
    bool isHalfFloat = (cil_props & CIL_HALF_FLOAT) != 0;
    int bytesPerPixel = isHalfFloat ? 8 : uploadChannels;

    if (isHalfFloat) {
      m_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    } else {
      switch (uploadChannels) {
        case 1:  m_format = VK_FORMAT_R8_UNORM;        break;
        case 4:
        default: m_format = VK_FORMAT_R8G8B8A8_UNORM;  break;
      }
    }

    VkDeviceSize imageSize = (VkDeviceSize)x * y * bytesPerPixel;
    bool isCube = (cil_props & CIL_CUBE_MAP) != 0;
    uint32_t layerCount = isCube ? 6 : 1;
    // For cubemaps, the DDS buffer stores all mip levels per face. Use
    // total size / 6 to correctly stride over each face's full mip chain.
    VkDeviceSize faceStride = isCube ? (this->size / layerCount) : imageSize;
    VkDeviceSize totalSize = isCube ? this->size : imageSize;

    // 1. Create VkImage
    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = m_format;
    imgCI.extent = { x, y, 1 };
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = layerCount;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (isCube) imgCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult res = vmaCreateImage(allocator, &imgCI, &allocCI, &m_image, &m_allocation, nullptr);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Texture image creation failed res=%d (%ux%u layers=%u cube=%d)", res, x, y, layerCount, (int)isCube);
      return;
    }
    if (isCube) {
      T8_LOG_INFO("[Vulkan] Cubemap image created: %ux%u x6 faces, fmt=%d", x, y, (int)m_format);
    }

    // 2. Create staging buffer and copy pixel data
    VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = totalSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCI = {};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingAllocInfo;
    vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
    memcpy(stagingAllocInfo.pMappedData, uploadBuf, totalSize);

    // 3. Record transient command buffer
    VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = driver->GetTransientCommandPool();
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // 3a. Transition UNDEFINED → TRANSFER_DST_OPTIMAL (all layers)
    {
      VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = m_image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = layerCount;
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // 3b. Copy staging buffer → image (one region per face/layer)
    std::vector<VkBufferImageCopy> regions(layerCount);
    for (uint32_t face = 0; face < layerCount; face++) {
      regions[face] = {};
      regions[face].bufferOffset = face * faceStride;
      regions[face].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      regions[face].imageSubresource.mipLevel = 0;
      regions[face].imageSubresource.baseArrayLayer = face;
      regions[face].imageSubresource.layerCount = 1;
      regions[face].imageExtent = { x, y, 1 };
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           layerCount, regions.data());

    // 3c. Transition TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL (all layers)
    {
      VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = m_image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = layerCount;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(cmd);

    // 4. Submit and wait
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(driver->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(driver->GetGraphicsQueue());

    vkFreeCommandBuffers(device, driver->GetTransientCommandPool(), 1, &cmd);

    // 5. Free staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // 6. Create VkImageView
    VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivCI.image = m_image;
    ivCI.viewType = isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = m_format;
    // R8 textures: replicate R to all channels (matches D3D11 behavior where R8 = (R,R,R,R))
    if (m_format == VK_FORMAT_R8_UNORM) {
      ivCI.components.r = VK_COMPONENT_SWIZZLE_R;
      ivCI.components.g = VK_COMPONENT_SWIZZLE_R;
      ivCI.components.b = VK_COMPONENT_SWIZZLE_R;
      ivCI.components.a = VK_COMPONENT_SWIZZLE_R;
    }
    ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivCI.subresourceRange.baseMipLevel = 0;
    ivCI.subresourceRange.levelCount = 1;
    ivCI.subresourceRange.baseArrayLayer = 0;
    ivCI.subresourceRange.layerCount = layerCount;

    res = vkCreateImageView(device, &ivCI, nullptr, &m_imageView);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Texture image view creation failed res=%d", res);
      return;
    }

    // 7. Create VkSampler
    SetTextureParams();

    T8_LOG_INFO("[Vulkan] LoadAPITexture OK (%ux%u ch=%u fmt=%d)", x, y, m_channels, m_format);
  }

  void VulkanTexture::LoadAPITextureCompressed(unsigned char* buffer) {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    VkFormat fmt = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    int blockSize = 8;
    if (cil_props & CIL_DXT3) { fmt = VK_FORMAT_BC2_UNORM_BLOCK; blockSize = 16; }
    else if (cil_props & CIL_DXT5) { fmt = VK_FORMAT_BC3_UNORM_BLOCK; blockSize = 16; }
    m_format = fmt;

    bool isCube = (cil_props & CIL_CUBE_MAP) != 0;
    uint32_t numFaces = isCube ? 6 : 1;
    uint32_t mipCount = (mipmaps > 0) ? mipmaps : 1;

    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = fmt;
    imgCI.extent = { x, y, 1 };
    imgCI.mipLevels = mipCount;
    imgCI.arrayLayers = numFaces;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (isCube) imgCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    VkResult res = vmaCreateImage(allocator, &imgCI, &allocCI, &m_image, &m_allocation, nullptr);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Compressed tex image failed res=%d (%ux%u)", res, x, y);
      return;
    }

    // Compute total upload size and build copy regions
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize totalSize = 0;
    unsigned char* pData = buffer;
    for (uint32_t face = 0; face < numFaces; face++) {
      uint32_t w = this->x, h = this->y;
      for (uint32_t mip = 0; mip < mipCount; mip++) {
        uint32_t wBlocks = (w + 3) / 4; if (wBlocks < 1) wBlocks = 1;
        uint32_t hBlocks = (h + 3) / 4; if (hBlocks < 1) hBlocks = 1;
        VkDeviceSize mipSize = (VkDeviceSize)wBlocks * hBlocks * blockSize;

        VkBufferImageCopy region = {};
        region.bufferOffset = totalSize;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mip;
        region.imageSubresource.baseArrayLayer = face;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { w, h, 1 };
        regions.push_back(region);

        totalSize += mipSize;
        w >>= 1; if (w < 1) w = 1;
        h >>= 1; if (h < 1) h = 1;
      }
    }

    // Staging buffer
    VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = totalSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo stagingAllocCI = {};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer stagingBuffer; VmaAllocation stagingAlloc; VmaAllocationInfo stagingAllocInfo;
    vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
    memcpy(stagingAllocInfo.pMappedData, buffer, totalSize);

    // Record copy
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = driver->GetTransientCommandPool();
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition all subresources to TRANSFER_DST
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, numFaces };
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           (uint32_t)regions.size(), regions.data());

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(driver->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(driver->GetGraphicsQueue());
    vkFreeCommandBuffers(device, driver->GetTransientCommandPool(), 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // Image view
    VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivCI.image = m_image;
    ivCI.viewType = isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = fmt;
    ivCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, numFaces };
    res = vkCreateImageView(device, &ivCI, nullptr, &m_imageView);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Compressed tex view failed res=%d", res);
      return;
    }

    // Sampler with mipmaps
    params |= MIPMAPS;
    SetTextureParams();

    T8_LOG_INFO("[Vulkan] Compressed texture OK: %ux%u fmt=%d mips=%u faces=%u cube=%d",
                x, y, (int)fmt, mipCount, numFaces, (int)isCube);
  }

  void VulkanTexture::DestroyAPITexture() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    if (m_sampler)   { vkDestroySampler(device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
    if (m_imageView) { vkDestroyImageView(device, m_imageView, nullptr); m_imageView = VK_NULL_HANDLE; }
    if (m_image)     { vmaDestroyImage(allocator, m_image, m_allocation); m_image = VK_NULL_HANDLE; }
  }

  void VulkanTexture::SetTextureParams() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();

    // Destroy old sampler if recreating
    if (m_sampler) {
      vkDestroySampler(device, m_sampler, nullptr);
      m_sampler = VK_NULL_HANDLE;
    }

    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (params & CLAMP_TO_EDGE)
      addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    else if (params & CLAMP_TO_BORDER)
      addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    VkFilter filter = VK_FILTER_LINEAR;
    if (params & NEAREST_FILTER)
      filter = VK_FILTER_NEAREST;

    VkSamplerCreateInfo samplerCI = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCI.magFilter = filter;
    samplerCI.minFilter = filter;
    samplerCI.addressModeU = addressMode;
    samplerCI.addressModeV = addressMode;
    samplerCI.addressModeW = addressMode;
    samplerCI.anisotropyEnable = VK_FALSE;
    samplerCI.maxAnisotropy = 1.0f;
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerCI.unnormalizedCoordinates = VK_FALSE;
    samplerCI.compareEnable = VK_FALSE;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.mipLodBias = 0.0f;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = (params & MIPMAPS) ? VK_LOD_CLAMP_NONE : 0.0f;

    VkResult res = vkCreateSampler(device, &samplerCI, nullptr, &m_sampler);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Sampler creation failed res=%d", res);
    }
  }

  void VulkanTexture::GetFormatBpp(unsigned int& props, unsigned int& format, unsigned int& bpp) {
    props = CH_RGBA;
    format = VK_FORMAT_R8G8B8A8_UNORM;
    bpp = 4;
  }

  void VulkanTexture::Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) {
    if (slot >= 8) return;
    auto* driver = GetVkDriver();
    driver->m_pendingTextures[slot].imageView = m_imageView;
    driver->m_pendingTextures[slot].sampler = m_sampler;
    T8_LOG_DEBUG("[Vulkan] Texture::Set slot=%u view=%p sampler=%p name=%s",
                slot, (void*)m_imageView, (void*)m_sampler, shaderTextureName.c_str());
  }

  void VulkanTexture::SetSampler(const DeviceContext& deviceContext, unsigned int slot) {
    // In Vulkan, samplers are part of the descriptor set, bound during shader Set().
  }

  // ══════════════════════════════════════════════════════
  //  VulkanRT
  // ══════════════════════════════════════════════════════

  bool VulkanRT::LoadAPIRT() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    // Helper to resolve BaseRT format enum to VkFormat
    auto resolveFormat = [](int fmt) -> VkFormat {
      switch (fmt) {
        case BaseRT::RGBA8:    return VK_FORMAT_R8G8B8A8_UNORM;
        case BaseRT::RGBA16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case BaseRT::F16:      return VK_FORMAT_R16_SFLOAT;
        case BaseRT::R8:       return VK_FORMAT_R8_UNORM;
        case BaseRT::F32:      return VK_FORMAT_R32_SFLOAT;
        default:               return VK_FORMAT_R8G8B8A8_UNORM;
      }
    };

    // Default color format (used when perColorFormats is empty)
    m_colorFormat = resolveFormat(color_format);

    // Per-attachment formats (mirrors D3D11/D3D12 behavior)
    std::vector<VkFormat> colorFormats(number_RT, m_colorFormat);
    if (!perColorFormats.empty()) {
      for (int i = 0; i < number_RT && i < (int)perColorFormats.size(); i++)
        colorFormats[i] = resolveFormat(perColorFormats[i]);
    }

    // ── 1. Color attachments ──
    vColorImages.resize(number_RT);
    vColorAllocations.resize(number_RT);
    vColorImageViews.resize(number_RT);
    vColorLayouts.resize(number_RT);
    vColorTextures.resize(number_RT);

    for (int i = 0; i < number_RT; i++) {
      VkFormat attachmentFormat = colorFormats[i];
      VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
      imgCI.imageType = VK_IMAGE_TYPE_2D;
      imgCI.format = attachmentFormat;
      imgCI.extent = { (uint32_t)w, (uint32_t)h, 1 };
      imgCI.mipLevels = 1;
      imgCI.arrayLayers = 1;
      imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
      imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
      imgCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

      VmaAllocationCreateInfo allocCI = {};
      allocCI.usage = VMA_MEMORY_USAGE_AUTO;

      VkResult res = vmaCreateImage(allocator, &imgCI, &allocCI, &vColorImages[i], &vColorAllocations[i], nullptr);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] RT color image[%d] creation failed res=%d", i, res);
        return false;
      }

      VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
      ivCI.image = vColorImages[i];
      ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivCI.format = attachmentFormat;
      ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      ivCI.subresourceRange.baseMipLevel = 0;
      ivCI.subresourceRange.levelCount = 1;
      ivCI.subresourceRange.baseArrayLayer = 0;
      ivCI.subresourceRange.layerCount = 1;

      res = vkCreateImageView(device, &ivCI, nullptr, &vColorImageViews[i]);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] RT color image view[%d] creation failed res=%d", i, res);
        return false;
      }

      vColorLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;

      // Create wrapper Texture for this color attachment
      VulkanTexture* tex = new VulkanTexture;
      tex->m_image = vColorImages[i];
      tex->m_imageView = vColorImageViews[i];
      tex->m_format = attachmentFormat;
      tex->x = (unsigned int)w;
      tex->y = (unsigned int)h;
      tex->m_channels = 4;
      tex->params = CLAMP_TO_EDGE;
      tex->SetTextureParams();
      vColorTextures[i] = tex;
    }

    // ── 2. Depth attachment ──
    bool hasDepth = (depth_format != BaseRT::NOTHING);
    if (hasDepth) {
      switch (depth_format) {
        case BaseRT::F32:  m_depthFormat = VK_FORMAT_D32_SFLOAT; break;
        case BaseRT::FD16: m_depthFormat = VK_FORMAT_D16_UNORM;  break;
        default:           m_depthFormat = VK_FORMAT_D32_SFLOAT; break;
      }

      VkImageCreateInfo depthImgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
      depthImgCI.imageType = VK_IMAGE_TYPE_2D;
      depthImgCI.format = m_depthFormat;
      depthImgCI.extent = { (uint32_t)w, (uint32_t)h, 1 };
      depthImgCI.mipLevels = 1;
      depthImgCI.arrayLayers = 1;
      depthImgCI.samples = VK_SAMPLE_COUNT_1_BIT;
      depthImgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
      depthImgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      depthImgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      depthImgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

      VmaAllocationCreateInfo depthAllocCI = {};
      depthAllocCI.usage = VMA_MEMORY_USAGE_AUTO;

      VkResult res = vmaCreateImage(allocator, &depthImgCI, &depthAllocCI, &m_depthImage, &m_depthAllocation, nullptr);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] RT depth image creation failed res=%d", res);
        return false;
      }

      VkImageViewCreateInfo divCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
      divCI.image = m_depthImage;
      divCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
      divCI.format = m_depthFormat;
      divCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      divCI.subresourceRange.baseMipLevel = 0;
      divCI.subresourceRange.levelCount = 1;
      divCI.subresourceRange.baseArrayLayer = 0;
      divCI.subresourceRange.layerCount = 1;

      res = vkCreateImageView(device, &divCI, nullptr, &m_depthImageView);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] RT depth image view creation failed res=%d", res);
        return false;
      }

      // Create wrapper Texture for depth attachment
      VulkanTexture* depthTex = new VulkanTexture;
      depthTex->m_image = m_depthImage;
      depthTex->m_imageView = m_depthImageView;
      depthTex->m_format = m_depthFormat;
      depthTex->x = (unsigned int)w;
      depthTex->y = (unsigned int)h;
      depthTex->m_channels = 1;
      depthTex->params = CLAMP_TO_EDGE;
      depthTex->SetTextureParams();
      pDepthTexture = depthTex;
    }

    // ── 3. VkRenderPass ──
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colorRefs;

    for (int i = 0; i < number_RT; i++) {
      VkAttachmentDescription colorAtt = {};
      colorAtt.format = colorFormats[i];
      colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
      colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      colorAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      attachments.push_back(colorAtt);

      VkAttachmentReference ref = {};
      ref.attachment = (uint32_t)i;
      ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      colorRefs.push_back(ref);
    }

    VkAttachmentReference depthRef = {};
    if (hasDepth) {
      VkAttachmentDescription depthAtt = {};
      depthAtt.format = m_depthFormat;
      depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
      depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      attachments.push_back(depthAtt);

      depthRef.attachment = (uint32_t)number_RT;
      depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = (uint32_t)number_RT;
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (hasDepth)
      dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpCI = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpCI.attachmentCount = (uint32_t)attachments.size();
    rpCI.pAttachments = attachments.data();
    rpCI.subpassCount = 1;
    rpCI.pSubpasses = &subpass;
    rpCI.dependencyCount = 1;
    rpCI.pDependencies = &dependency;

    VkResult res = vkCreateRenderPass(device, &rpCI, nullptr, &m_renderPass);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] RT render pass creation failed res=%d", res);
      return false;
    }

    // ── 4. VkFramebuffer ──
    std::vector<VkImageView> fbAttachments;
    for (int i = 0; i < number_RT; i++)
      fbAttachments.push_back(vColorImageViews[i]);
    if (hasDepth)
      fbAttachments.push_back(m_depthImageView);

    VkFramebufferCreateInfo fbCI = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbCI.renderPass = m_renderPass;
    fbCI.attachmentCount = (uint32_t)fbAttachments.size();
    fbCI.pAttachments = fbAttachments.data();
    fbCI.width = (uint32_t)w;
    fbCI.height = (uint32_t)h;
    fbCI.layers = 1;

    res = vkCreateFramebuffer(device, &fbCI, nullptr, &m_framebuffer);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] RT framebuffer creation failed res=%d", res);
      return false;
    }

    // Initialize color images to SHADER_READ_ONLY_OPTIMAL so they're valid when first sampled
    {
      VkCommandBuffer initCmd;
      VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      cmdAlloc.commandPool = driver->GetTransientCommandPool();
      cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmdAlloc.commandBufferCount = 1;
      vkAllocateCommandBuffers(device, &cmdAlloc, &initCmd);
      VkCommandBufferBeginInfo beginCI = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(initCmd, &beginCI);
      for (int i = 0; i < number_RT; i++) {
        TransitionImageLayout(initCmd, vColorImages[i],
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_ASPECT_COLOR_BIT);
        vColorLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }
      if (hasDepth && m_depthImage) {
        TransitionImageLayout(initCmd, m_depthImage,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_ASPECT_DEPTH_BIT);
      }
      vkEndCommandBuffer(initCmd);
      VkSubmitInfo initSubmit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      initSubmit.commandBufferCount = 1;
      initSubmit.pCommandBuffers = &initCmd;
      vkQueueSubmit(driver->GetGraphicsQueue(), 1, &initSubmit, VK_NULL_HANDLE);
      vkQueueWaitIdle(driver->GetGraphicsQueue());
      vkFreeCommandBuffers(device, driver->GetTransientCommandPool(), 1, &initCmd);
    }

    T8_LOG_INFO("[Vulkan] LoadAPIRT OK (%dx%d, %d color, depth=%s)", w, h, number_RT, hasDepth ? "yes" : "no");
    return true;
  }

  void VulkanRT::DestroyAPIRT() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    if (m_framebuffer) { vkDestroyFramebuffer(device, m_framebuffer, nullptr); m_framebuffer = VK_NULL_HANDLE; }
    if (m_renderPass)  { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }

    // Destroy sampler from color texture wrappers (image/view destroyed below separately)
    for (auto* tex : vColorTextures) {
      if (tex) {
        VulkanTexture* vt = static_cast<VulkanTexture*>(tex);
        if (vt->m_sampler) { vkDestroySampler(device, vt->m_sampler, nullptr); vt->m_sampler = VK_NULL_HANDLE; }
        vt->m_image = VK_NULL_HANDLE;      // prevent double-free
        vt->m_imageView = VK_NULL_HANDLE;
        delete vt;
      }
    }
    vColorTextures.clear();

    for (auto& view : vColorImageViews) vkDestroyImageView(device, view, nullptr);
    for (size_t i = 0; i < vColorImages.size(); i++) vmaDestroyImage(allocator, vColorImages[i], vColorAllocations[i]);
    vColorImageViews.clear();
    vColorImages.clear();
    vColorAllocations.clear();
    vColorLayouts.clear();

    // Destroy depth texture wrapper sampler (image/view destroyed below)
    if (pDepthTexture) {
      VulkanTexture* dt = static_cast<VulkanTexture*>(pDepthTexture);
      if (dt->m_sampler) { vkDestroySampler(device, dt->m_sampler, nullptr); dt->m_sampler = VK_NULL_HANDLE; }
      dt->m_image = VK_NULL_HANDLE;
      dt->m_imageView = VK_NULL_HANDLE;
      delete dt;
      pDepthTexture = nullptr;
    }

    if (m_depthImageView) { vkDestroyImageView(device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
    if (m_depthImage)     { vmaDestroyImage(allocator, m_depthImage, m_depthAllocation); m_depthImage = VK_NULL_HANDLE; }

    for (int i = 0; i < 6; i++) {
      if (m_cubeFaceViews[i]) { vkDestroyImageView(device, m_cubeFaceViews[i], nullptr); m_cubeFaceViews[i] = VK_NULL_HANDLE; }
    }
  }

  void VulkanRT::Set(const DeviceContext& context) {
    auto* driver = GetVkDriver();
    VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&context)->GetCommandBuffer();

    // End any active render pass before starting the RT pass
    driver->EndRenderPassIfActive(cmd);

    // Transition color images to COLOR_ATTACHMENT if needed
    for (int i = 0; i < number_RT; i++) {
      if (vColorLayouts[i] == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        TransitionImageLayout(cmd, vColorImages[i],
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT);
      }
      vColorLayouts[i] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Begin the RT's own render pass with clear values
    bool hasDepth = (depth_format != BaseRT::NOTHING);
    std::vector<VkClearValue> clearValues(number_RT);
    for (int i = 0; i < number_RT; i++)
      clearValues[i].color = { {0.0f, 0.0f, 0.0f, 0.0f} };

    if (hasDepth) {
      VkClearValue depthClear = {};
      depthClear.depthStencil = { 1.0f, 0 };
      clearValues.push_back(depthClear);
    }

    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = m_renderPass;
    rpBegin.framebuffer = m_framebuffer;
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = { (uint32_t)w, (uint32_t)h };
    rpBegin.clearValueCount = (uint32_t)clearValues.size();
    rpBegin.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    driver->SetActiveRenderPass(m_renderPass);
    driver->SetRenderPassActive(true);

    // Set viewport and scissor to RT dimensions (negative height for Y-flip)
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = (float)h;
    viewport.width = (float)w;
    viewport.height = -(float)h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = { (uint32_t)w, (uint32_t)h };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    T8_LOG_TRACE("[Vulkan] RT::Set %dx%d (%d attachments)", w, h, number_RT);
  }

  void VulkanRT::ChangeCubeDepthTexture(int i) {
    T8_LOG_TRACE("[Vulkan] TODO: ChangeCubeDepthTexture(%d)", i);
  }

  // ══════════════════════════════════════════════════════
  //  VulkanShader
  // ══════════════════════════════════════════════════════

  static bool s_glslangInitialized = false;

  static VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t codeSize) {
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = codeSize;
    ci.pCode = code;
    VkShaderModule mod = VK_NULL_HANDLE;
    VkResult res = vkCreateShaderModule(device, &ci, nullptr, &mod);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateShaderModule failed res=%d", res);
      return VK_NULL_HANDLE;
    }
    return mod;
  }

  static bool CompileHLSLToSPIRV(const std::string& source, glslang_stage_t stage,
                                   std::vector<uint32_t>& spirv, const std::string& debugName) {
    if (!s_glslangInitialized) {
      glslang_initialize_process();
      s_glslangInitialized = true;
    }

    const char* src = source.c_str();
    glslang_input_t input = {};
    input.language = GLSLANG_SOURCE_HLSL;
    input.stage = stage;
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_0;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_0;
    input.code = src;
    input.default_version = 100;
    input.default_profile = GLSLANG_NO_PROFILE;
    input.force_default_version_and_profile = false;
    input.forward_compatible = false;
    input.messages = (glslang_messages_t)(GLSLANG_MSG_DEFAULT_BIT | GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
    input.resource = glslang_default_resource();

    glslang_shader_t* shader = glslang_shader_create(&input);
    glslang_shader_set_options(shader, GLSLANG_SHADER_AUTO_MAP_BINDINGS | GLSLANG_SHADER_AUTO_MAP_LOCATIONS);
    // Set entry point for HLSL
    const char* entryPoint = (stage == GLSLANG_STAGE_VERTEX) ? "VS" : "FS";
    glslang_shader_set_entry_point(shader, entryPoint);
    // Shift UBO binding up to avoid collision with textures at binding 0-7
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_UBO, 16);
    // Shift texture/sampler bindings by 1 so register(t0)→binding 1 (binding 0 = UBO)
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_TEXTURE, 1);
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_SAMPLER, 1);
    if (!glslang_shader_preprocess(shader, &input)) {
      T8_LOG_ERROR("[Vulkan] Shader preprocess failed (%s): %s", debugName.c_str(), glslang_shader_get_info_log(shader));
      glslang_shader_delete(shader);
      return false;
    }
    if (!glslang_shader_parse(shader, &input)) {
      T8_LOG_ERROR("[Vulkan] Shader parse failed (%s): %s", debugName.c_str(), glslang_shader_get_info_log(shader));
      glslang_shader_delete(shader);
      return false;
    }

    glslang_program_t* program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
      T8_LOG_ERROR("[Vulkan] Program link failed (%s): %s", debugName.c_str(), glslang_program_get_info_log(program));
      glslang_program_delete(program);
      glslang_shader_delete(shader);
      return false;
    }

    glslang_program_SPIRV_generate(program, stage);

    size_t spirvSize = glslang_program_SPIRV_get_size(program);
    spirv.resize(spirvSize);
    glslang_program_SPIRV_get(program, spirv.data());

    const char* spirvMessages = glslang_program_SPIRV_get_messages(program);
    if (spirvMessages && spirvMessages[0]) {
      T8_LOG_INFO("[Vulkan] SPIR-V messages (%s): %s", debugName.c_str(), spirvMessages);
    }

    glslang_program_delete(program);
    glslang_shader_delete(shader);
    return true;
  }

  bool VulkanShader::CreateShaderAPI(std::string src_vs, std::string src_fs,
                                      const std::string& vs_name, const std::string& fs_name) {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();

    // Compile vertex shader (HLSL → SPIR-V)
    std::vector<uint32_t> vsSPIRV;
    if (!CompileHLSLToSPIRV(src_vs, GLSLANG_STAGE_VERTEX, vsSPIRV, vs_name.empty() ? "VS" : vs_name)) {
      return false;
    }
    // Patch SPIR-V: shift UBO bindings to avoid collision with textures at binding 0+
    SPIRVReflection::ShiftUBOBindings(vsSPIRV.data(), vsSPIRV.size(), 16);

    m_vertModule = CreateShaderModule(device, vsSPIRV.data(), vsSPIRV.size() * sizeof(uint32_t));
    if (!m_vertModule) return false;

    // Compile fragment shader (HLSL → SPIR-V)
    std::vector<uint32_t> fsSPIRV;
    if (!CompileHLSLToSPIRV(src_fs, GLSLANG_STAGE_FRAGMENT, fsSPIRV, fs_name.empty() ? "FS" : fs_name)) {
      return false;
    }
    SPIRVReflection::ShiftUBOBindings(fsSPIRV.data(), fsSPIRV.size(), 16);
    m_fragModule = CreateShaderModule(device, fsSPIRV.data(), fsSPIRV.size() * sizeof(uint32_t));
    if (!m_fragModule) return false;

    // ── SPIR-V Reflection ──
    SPIRVReflection vsRefl, fsRefl;
    vsRefl.Parse(vsSPIRV.data(), vsSPIRV.size());
    fsRefl.Parse(fsSPIRV.data(), fsSPIRV.size());

    // Build descriptor set layout from reflected bindings
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindingMap;

    // VS uniform buffers
    for (auto& ub : vsRefl.uniformBuffers) {
      auto& b = bindingMap[ub.binding];
      b.binding = ub.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
      cbvBinding = (int)ub.binding;
    }
    // FS uniform buffers
    for (auto& ub : fsRefl.uniformBuffers) {
      auto& b = bindingMap[ub.binding];
      b.binding = ub.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
      if (cbvBinding < 0) cbvBinding = (int)ub.binding;
    }
    // FS sampled images (textures) — derive engine slot from binding (undo +1 texture shift)
    for (int idx = 0; idx < (int)fsRefl.sampledImages.size(); idx++) {
      auto& si = fsRefl.sampledImages[idx];
      auto& b = bindingMap[si.binding];
      b.binding = si.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
      // Texture binding N maps to engine slot N (register(tN) → binding N)
      int engineSlot = (int)si.binding;
      if (engineSlot >= 0 && engineSlot < 8) {
        srvBindings[engineSlot] = (int)si.binding;
        srvIsCubemap[engineSlot] = si.isCubemap;
      }
    }
    // VS sampled images (if any)
    for (auto& si : vsRefl.sampledImages) {
      auto& b = bindingMap[si.binding];
      b.binding = si.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    }

    // Sort bindings and create layout
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (auto& [idx, b] : bindingMap) bindings.push_back(b);
    std::sort(bindings.begin(), bindings.end(),
      [](const auto& a, const auto& b) { return a.binding < b.binding; });

    // Track the max binding for descriptor writes
    maxBinding = 0;
    for (auto& b : bindings) {
      if (b.binding > (uint32_t)maxBinding) maxBinding = (int)b.binding;
    }

    VkDescriptorSetLayoutCreateInfo dslCI = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslCI.bindingCount = (uint32_t)bindings.size();
    dslCI.pBindings = bindings.data();
    VkResult res = vkCreateDescriptorSetLayout(device, &dslCI, nullptr, &m_descriptorSetLayout);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateDescriptorSetLayout failed res=%d", res);
      return false;
    }

    // Pipeline layout
    VkPipelineLayoutCreateInfo plCI = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plCI.setLayoutCount = 1;
    plCI.pSetLayouts = &m_descriptorSetLayout;
    res = vkCreatePipelineLayout(device, &plCI, nullptr, &m_pipelineLayout);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreatePipelineLayout failed res=%d", res);
      return false;
    }

    // ── Vertex input from VS reflection ──
    m_vertexAttributes.clear();
    uint32_t offset = 0;
    for (auto& inp : vsRefl.stageInputs) {
      VkVertexInputAttributeDescription attr = {};
      attr.location = inp.location;
      attr.binding = 0;
      attr.offset = offset;
      switch (inp.vecSize) {
        case 1: attr.format = VK_FORMAT_R32_SFLOAT;          offset += 4;  break;
        case 2: attr.format = VK_FORMAT_R32G32_SFLOAT;       offset += 8;  break;
        case 3: attr.format = VK_FORMAT_R32G32B32_SFLOAT;    offset += 12; break;
        default: attr.format = VK_FORMAT_R32G32B32A32_SFLOAT; offset += 16; break;
      }
      m_vertexAttributes.push_back(attr);
    }
    vertexStride = offset;
    m_vertexBinding.binding = 0;
    m_vertexBinding.stride = vertexStride;
    m_vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    T8_LOG_INFO("[Vulkan] Shader '%s'/'%s': %zu bindings (cbv=%d), %zu inputs (stride=%d)",
                vs_name.c_str(), fs_name.c_str(),
                bindings.size(), cbvBinding,
                m_vertexAttributes.size(), vertexStride);

#ifdef T8_DUMP_SHADER_REFLECTION
    T8_LOG_INFO("[VK_REFL] === key=0x%08X VS='%s' FS='%s' ===", key.bits, vs_name.c_str(), fs_name.c_str());
    T8_LOG_INFO("[VK_REFL] VS Inputs (%zu):", vsRefl.stageInputs.size());
    for (size_t idx = 0; idx < vsRefl.stageInputs.size(); idx++) {
      auto& inp = vsRefl.stageInputs[idx];
      T8_LOG_INFO("[VK_REFL]   [%zu] '%s'  location=%u  components=%u",
                  idx, inp.name.c_str(), inp.location, inp.vecSize);
    }
    T8_LOG_INFO("[VK_REFL] VS stride=%d", vertexStride);
    T8_LOG_INFO("[VK_REFL] VS UBOs (%zu):", vsRefl.uniformBuffers.size());
    for (auto& ub : vsRefl.uniformBuffers)
      T8_LOG_INFO("[VK_REFL]   '%s' set=%u binding=%u", ub.name.c_str(), ub.set, ub.binding);
    T8_LOG_INFO("[VK_REFL] VS Textures (%zu):", vsRefl.sampledImages.size());
    for (auto& si : vsRefl.sampledImages)
      T8_LOG_INFO("[VK_REFL]   '%s' set=%u binding=%u", si.name.c_str(), si.set, si.binding);
    T8_LOG_INFO("[VK_REFL] FS UBOs (%zu):", fsRefl.uniformBuffers.size());
    for (auto& ub : fsRefl.uniformBuffers)
      T8_LOG_INFO("[VK_REFL]   '%s' set=%u binding=%u", ub.name.c_str(), ub.set, ub.binding);
    T8_LOG_INFO("[VK_REFL] FS Textures (%zu):", fsRefl.sampledImages.size());
    for (auto& si : fsRefl.sampledImages)
      T8_LOG_INFO("[VK_REFL]   '%s' set=%u binding=%u", si.name.c_str(), si.set, si.binding);
    T8_LOG_INFO("[VK_REFL] Descriptor layout bindings:");
    for (auto& b : bindings)
      T8_LOG_INFO("[VK_REFL]   binding=%u type=%d stages=0x%X",
                  b.binding, b.descriptorType, b.stageFlags);
    T8_LOG_INFO("[VK_REFL] srvBindings: [%d,%d,%d,%d,%d,%d,%d,%d]",
                srvBindings[0], srvBindings[1], srvBindings[2], srvBindings[3],
                srvBindings[4], srvBindings[5], srvBindings[6], srvBindings[7]);
#endif
    return true;
  }

  void VulkanShader::Set(const DeviceContext& deviceContext) {
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = this;
    auto* driver = GetVkDriver();

    // Determine current render target format for pipeline creation
    uint8_t numColorAttachments = 1;
    VkFormat colorFormat = driver->m_swapChainFormat;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    if (driver->CurrentRT >= 0 && driver->CurrentRT < (int)driver->RTs.size()) {
      VulkanRT* rt = static_cast<VulkanRT*>(driver->RTs[driver->CurrentRT]);
      numColorAttachments = (uint8_t)rt->number_RT;
      colorFormat = rt->m_colorFormat;
      depthFormat = (rt->depth_format != BaseRT::NOTHING) ? rt->m_depthFormat : VK_FORMAT_UNDEFINED;
    }

    VkPipeline pipeline = driver->GetOrCreatePipeline(this, numColorAttachments, colorFormat, depthFormat);
    if (!pipeline) return;

    VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&deviceContext)->GetCommandBuffer();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  }

  void VulkanShader::DestroyAPIShader() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    if (m_pipelineLayout)       { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
    if (m_descriptorSetLayout)  { vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
    if (m_vertModule)           { vkDestroyShaderModule(device, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE; }
    if (m_fragModule)           { vkDestroyShaderModule(device, m_fragModule, nullptr); m_fragModule = VK_NULL_HANDLE; }
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — PSO Cache
  // ══════════════════════════════════════════════════════

  VkPipeline VulkanDriver::GetOrCreatePipeline(VulkanShader* shader, uint8_t numColorAttachments,
                                                VkFormat colorFormat, VkFormat depthFormat) {
    VulkanPipelineKey key = {};
    key.shaderPtr = reinterpret_cast<uintptr_t>(shader);
    key.blend = (uint8_t)m_currentBlend;
    key.depth = (uint8_t)m_currentDepth;
    key.cull = (uint8_t)m_currentCull;
    key.numColorAttachments = numColorAttachments;
    key.topology = (uint8_t)static_cast<VulkanDeviceContext*>(T8DeviceContext)->GetTopology();
    key.colorFormat = colorFormat;
    key.depthFormat = depthFormat;

    auto it = m_pipelineCache.find(key);
    if (it != m_pipelineCache.end()) {
      T8_LOG_TRACE("[Vulkan] Pipeline cache hit: shader=%p topo=%d blend=%d depth=%d",
                   shader, key.topology, key.blend, key.depth);
      return it->second;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader->m_vertModule;
    stages[0].pName = "VS";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader->m_fragModule;
    stages[1].pName = "FS";

    // Vertex input (use shader's reflection data or empty for now)
    VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    if (!shader->m_vertexAttributes.empty()) {
      vertexInput.vertexBindingDescriptionCount = 1;
      vertexInput.pVertexBindingDescriptions = &shader->m_vertexBinding;
      vertexInput.vertexAttributeDescriptionCount = (uint32_t)shader->m_vertexAttributes.size();
      vertexInput.pVertexAttributeDescriptions = shader->m_vertexAttributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = (VkPrimitiveTopology)key.topology;

    // Dynamic viewport/scissor
    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    switch (m_currentCull) {
      case FRONT_FACES:    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  break;
      case BACK_FACES:     rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; break;
      case FRONT_AND_BACK: rasterizer.cullMode = VK_CULL_MODE_NONE;      break;
      default:             rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  break;
    }
    // Negative viewport height flips winding, so use clockwise front face
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    switch (m_currentDepth) {
      case DEPTH_DEFAULT: case READ_WRITE:
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
      case READ:
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
      case NONE:
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        break;
    }

    // Color blend
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(numColorAttachments);
    for (auto& att : blendAttachments) {
      att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
      switch (m_currentBlend) {
        case BLEND_DEFAULT: case BLEND_OPAQUE:
          att.blendEnable = VK_FALSE;
          break;
        case ADDITIVE:
          att.blendEnable = VK_TRUE;
          att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
          att.colorBlendOp = VK_BLEND_OP_ADD;
          att.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
          att.alphaBlendOp = VK_BLEND_OP_ADD;
          break;
        case ALPHA_BLEND:
          att.blendEnable = VK_TRUE;
          att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
          att.colorBlendOp = VK_BLEND_OP_ADD;
          att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
          att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
          att.alphaBlendOp = VK_BLEND_OP_ADD;
          break;
        case NON_PREMULTIPLIED:
          att.blendEnable = VK_TRUE;
          att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
          att.colorBlendOp = VK_BLEND_OP_ADD;
          att.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
          att.alphaBlendOp = VK_BLEND_OP_ADD;
          break;
      }
    }

    VkPipelineColorBlendStateCreateInfo colorBlend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlend.attachmentCount = numColorAttachments;
    colorBlend.pAttachments = blendAttachments.data();

    // Dynamic states
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = stages;
    pipelineCI.pVertexInputState = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState = &multisampling;
    pipelineCI.pDepthStencilState = &depthStencil;
    pipelineCI.pColorBlendState = &colorBlend;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.layout = shader->m_pipelineLayout;
    // Use canonical backbuffer pass for pipeline creation when on backbuffer;
    // the LOAD variant is render-pass-compatible, so pipelines work with both.
    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size()) {
      VulkanRT* rt = static_cast<VulkanRT*>(RTs[CurrentRT]);
      pipelineCI.renderPass = rt->m_renderPass;
    } else {
      pipelineCI.renderPass = m_backbufferRenderPass;
    }
    pipelineCI.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult res = vkCreateGraphicsPipelines(m_device, m_vkPipelineCache, 1, &pipelineCI, nullptr, &pipeline);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateGraphicsPipelines failed res=%d shader=%p blend=%d depth=%d cull=%d",
                   res, shader, key.blend, key.depth, key.cull);
      return VK_NULL_HANDLE;
    }

    T8_LOG_DEBUG("[Vulkan] Pipeline created: shader=%p blend=%d depth=%d cull=%d topo=%d colors=%d renderPass=%p",
                 shader, key.blend, key.depth, key.cull, key.topology, key.numColorAttachments, pipelineCI.renderPass);
    m_pipelineCache[key] = pipeline;
    return pipeline;
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Core lifecycle
  // ══════════════════════════════════════════════════════

  void VulkanDriver::SetWindow(void* window) {
    m_hwnd = (HWND)window;
    if (!m_hwnd) m_hwnd = GetActiveWindow();
    // Surface creation is deferred to InitDriver where the VkInstance is available.
    // The window pointer is stored for use there.
  }

  void VulkanDriver::SetDimensions(int w, int h) { width = w; height = h; }

  void VulkanDriver::CreateInstance() {
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "T850 Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "T850";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // Get SDL-required extensions
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

    std::vector<const char*> extensions;
    for (uint32_t i = 0; i < sdlExtCount; i++)
      extensions.push_back(sdlExts[i]);

#ifdef T8_VULKAN_VALIDATION
    // Check if validation layer is available
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    bool validationAvailable = false;
    for (auto& lp : availableLayers) {
      if (strcmp(lp.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
        validationAvailable = true;
        break;
      }
    }
    const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    if (validationAvailable) {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      T8_LOG_INFO("[Vulkan] Validation layer available — enabling");
    } else {
      T8_LOG_INFO("[Vulkan] Validation layer NOT available — install Vulkan SDK for validation");
    }
#endif

    VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();

#ifdef T8_VULKAN_VALIDATION
    if (validationAvailable) {
      ci.enabledLayerCount = 1;
      ci.ppEnabledLayerNames = validationLayers;

      // Enable synchronization validation to catch barrier/layout hazards
      static VkValidationFeatureEnableEXT enabledFeatures[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
      };
      static VkValidationFeaturesEXT validationFeatures = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
      validationFeatures.enabledValidationFeatureCount = 2;
      validationFeatures.pEnabledValidationFeatures = enabledFeatures;
      ci.pNext = &validationFeatures;
      T8_LOG_INFO("[Vulkan] Synchronization validation + best practices ENABLED");
    }
#endif

    VkResult res = vkCreateInstance(&ci, nullptr, &m_instance);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateInstance failed res=%d", res);
      return;
    }
    T8_LOG_INFO("[Vulkan] Instance created (%u extensions)", (uint32_t)extensions.size());
  }

  void VulkanDriver::CreateDevice() {
    // Pick physical device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
      T8_LOG_ERROR("[Vulkan] No GPU with Vulkan support found");
      return;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Pick first discrete GPU, or fallback to first device
    m_physicalDevice = devices[0];
    for (auto& dev : devices) {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(dev, &props);
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        m_physicalDevice = dev;
        T8_LOG_INFO("[Vulkan] GPU: %s", props.deviceName);
        break;
      }
    }

    // Find graphics queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    m_graphicsQueueFamily = 0;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
      if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        m_graphicsQueueFamily = i;
        break;
      }
    }
    m_presentQueueFamily = m_graphicsQueueFamily;

    // Query features before creating device (best practice)
    VkPhysicalDeviceFeatures supportedFeatures = {};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supportedFeatures);

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCI = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueCI.queueFamilyIndex = m_graphicsQueueFamily;
    queueCI.queueCount = 1;
    queueCI.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      VK_KHR_MAINTENANCE1_EXTENSION_NAME,  // negative viewport height for Y-flip
    };

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo deviceCI = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceCI.queueCreateInfoCount = 1;
    deviceCI.pQueueCreateInfos = &queueCI;
    deviceCI.enabledExtensionCount = 2;
    deviceCI.ppEnabledExtensionNames = deviceExtensions;
    deviceCI.pEnabledFeatures = &deviceFeatures;

    VkResult res = vkCreateDevice(m_physicalDevice, &deviceCI, nullptr, &m_device);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateDevice failed res=%d", res);
      return;
    }
    static_cast<VulkanDevice*>(T8Device)->m_device = m_device;

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    m_presentQueue = m_graphicsQueue;
    T8_LOG_INFO("[Vulkan] Logical device created, graphics queue family=%u", m_graphicsQueueFamily);
  }

  void VulkanDriver::CreateAllocator() {
    VmaAllocatorCreateInfo allocCI = {};
    allocCI.physicalDevice = m_physicalDevice;
    allocCI.device = m_device;
    allocCI.instance = m_instance;
    allocCI.vulkanApiVersion = VK_API_VERSION_1_0;

    VkResult res = vmaCreateAllocator(&allocCI, &m_allocator);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vmaCreateAllocator failed res=%d", res);
      return;
    }
    T8_LOG_INFO("[Vulkan] VMA allocator created");
  }

  void VulkanDriver::CreateSwapChain() {
    // Query surface capabilities
    VkSurfaceCapabilitiesKHR surfCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfCaps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

    // Prefer B8G8R8A8_UNORM, SRGB_NONLINEAR
    m_swapChainFormat = formats[0].format;
    VkColorSpaceKHR colorSpace = formats[0].colorSpace;
    for (auto& fmt : formats) {
      if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        m_swapChainFormat = fmt.format;
        colorSpace = fmt.colorSpace;
        break;
      }
    }

    // Pick present mode: prefer MAILBOX, fallback to FIFO
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    VkPresentModeKHR chosenMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto mode : presentModes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) { chosenMode = mode; break; }
    }

    m_swapChainExtent = { (uint32_t)width, (uint32_t)height };
    if (surfCaps.currentExtent.width != UINT32_MAX)
      m_swapChainExtent = surfCaps.currentExtent;

    uint32_t imageCount = surfCaps.minImageCount + 1;
    if (surfCaps.maxImageCount > 0 && imageCount > surfCaps.maxImageCount)
      imageCount = surfCaps.maxImageCount;
    if (imageCount < kBackBufferCount) imageCount = kBackBufferCount;

    VkSwapchainCreateInfoKHR scCI = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    scCI.surface = m_surface;
    scCI.minImageCount = imageCount;
    scCI.imageFormat = m_swapChainFormat;
    scCI.imageColorSpace = colorSpace;
    scCI.imageExtent = m_swapChainExtent;
    scCI.imageArrayLayers = 1;
    scCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    scCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scCI.preTransform = surfCaps.currentTransform;
    scCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scCI.presentMode = chosenMode;
    scCI.clipped = VK_TRUE;

    VkResult res = vkCreateSwapchainKHR(m_device, &scCI, nullptr, &m_swapChain);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateSwapchainKHR failed res=%d", res);
      return;
    }
    T8_LOG_INFO("[Vulkan] Swap chain created (%ux%u, %u images, mode=%d)",
                m_swapChainExtent.width, m_swapChainExtent.height, imageCount, chosenMode);
  }

  void VulkanDriver::CreateBackBufferViews() {
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
      VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
      ivCI.image = m_swapChainImages[i];
      ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivCI.format = m_swapChainFormat;
      ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      ivCI.subresourceRange.levelCount = 1;
      ivCI.subresourceRange.layerCount = 1;
      vkCreateImageView(m_device, &ivCI, nullptr, &m_swapChainImageViews[i]);
    }
    T8_LOG_INFO("[Vulkan] Back buffer image views created (%u)", imageCount);
  }

  void VulkanDriver::CreateRenderPass() {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapChainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo rpCI = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpCI.attachmentCount = 2;
    rpCI.pAttachments = attachments;
    rpCI.subpassCount = 1;
    rpCI.pSubpasses = &subpass;
    rpCI.dependencyCount = 1;
    rpCI.pDependencies = &dependency;

    VkResult res = vkCreateRenderPass(m_device, &rpCI, nullptr, &m_backbufferRenderPass);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateRenderPass failed res=%d", res);
      return;
    }

    // Create a LOAD variant for restarting the backbuffer pass after RT pops
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[0] = colorAttachment;
    attachments[1] = depthAttachment;
    res = vkCreateRenderPass(m_device, &rpCI, nullptr, &m_backbufferRenderPassLoad);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateRenderPass (load) failed res=%d", res);
    }

    T8_LOG_INFO("[Vulkan] Backbuffer render passes created (clear + load)");
  }

  void VulkanDriver::CreateDepthBuffer() {
    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = VK_FORMAT_D32_SFLOAT;
    imgCI.extent = { (uint32_t)width, (uint32_t)height, 1 };
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult res = vmaCreateImage(m_allocator, &imgCI, &allocCI, &m_depthImage, &m_depthAllocation, nullptr);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Depth image creation failed res=%d", res);
      return;
    }

    VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivCI.image = m_depthImage;
    ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = VK_FORMAT_D32_SFLOAT;
    ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    ivCI.subresourceRange.levelCount = 1;
    ivCI.subresourceRange.layerCount = 1;

    res = vkCreateImageView(m_device, &ivCI, nullptr, &m_depthImageView);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Depth image view creation failed res=%d", res);
      return;
    }
    T8_LOG_INFO("[Vulkan] Depth buffer created (%dx%d)", width, height);
  }

  void VulkanDriver::CreateFramebuffers() {
    uint32_t imageCount = (uint32_t)m_swapChainImageViews.size();
    m_backbufferFramebuffers.resize(imageCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < imageCount; i++) {
      VkImageView attachments[] = { m_swapChainImageViews[i], m_depthImageView };

      VkFramebufferCreateInfo fbCI = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
      fbCI.renderPass = m_backbufferRenderPass;
      fbCI.attachmentCount = 2;
      fbCI.pAttachments = attachments;
      fbCI.width = m_swapChainExtent.width;
      fbCI.height = m_swapChainExtent.height;
      fbCI.layers = 1;

      VkResult res = vkCreateFramebuffer(m_device, &fbCI, nullptr, &m_backbufferFramebuffers[i]);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] vkCreateFramebuffer[%u] failed res=%d", i, res);
      }
    }
    T8_LOG_INFO("[Vulkan] Framebuffers created (%u)", (uint32_t)m_swapChainImageViews.size());
  }

  void VulkanDriver::CreateCommandInfrastructure() {
    // Main command pool
    VkCommandPoolCreateInfo poolCI = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolCI.queueFamilyIndex = m_graphicsQueueFamily;
    poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(m_device, &poolCI, nullptr, &m_commandPool);

    // Transient command pool for upload helpers
    VkCommandPoolCreateInfo transientPoolCI = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transientPoolCI.queueFamilyIndex = m_graphicsQueueFamily;
    transientPoolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    vkCreateCommandPool(m_device, &transientPoolCI, nullptr, &m_transientCommandPool);

    // Allocate per-frame command buffers
    VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kBackBufferCount;
    vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers);

    static_cast<VulkanDeviceContext*>(T8DeviceContext)->m_commandBuffer = m_commandBuffers[0];
    T8_LOG_INFO("[Vulkan] Command infrastructure created (%u command buffers)", kBackBufferCount);
  }

  void VulkanDriver::CreateSyncObjects() {
    VkSemaphoreCreateInfo semCI = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceCI = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      vkCreateSemaphore(m_device, &semCI, nullptr, &m_imageAvailableSemaphores[i]);
      vkCreateSemaphore(m_device, &semCI, nullptr, &m_renderFinishedSemaphores[i]);
      vkCreateFence(m_device, &fenceCI, nullptr, &m_inFlightFences[i]);
    }
    T8_LOG_INFO("[Vulkan] Sync objects created (%u frames in flight)", kBackBufferCount);
  }

  void VulkanDriver::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 },
    };

    VkDescriptorPoolCreateInfo dpCI = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpCI.maxSets = 4096;
    dpCI.poolSizeCount = 2;
    dpCI.pPoolSizes = poolSizes;
    dpCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      VkResult res = vkCreateDescriptorPool(m_device, &dpCI, nullptr, &m_descriptorPools[i]);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] vkCreateDescriptorPool[%u] failed res=%d", i, res);
      }
    }
    T8_LOG_INFO("[Vulkan] Descriptor pools created (%u)", kBackBufferCount);
  }

  void VulkanDriver::InitDriver() {
    T8Device = new VulkanDevice;
    T8DeviceContext = new VulkanDeviceContext;

    CreateInstance();

#ifdef T8_VULKAN_VALIDATION
    // Setup debug messenger for validation layer output
    {
      auto createFunc = (PFN_vkCreateDebugUtilsMessengerEXT)
          vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
      if (createFunc) {
        VkDebugUtilsMessengerCreateInfoEXT dbgCI = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        dbgCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        dbgCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCI.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                    VkDebugUtilsMessageTypeFlagsEXT,
                                    const VkDebugUtilsMessengerCallbackDataEXT* data,
                                    void*) -> VkBool32 {
          if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            T8_LOG_ERROR("[VK_VALIDATION] %s", data->pMessage);
          else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            T8_LOG_INFO("[VK_VALIDATION:WARN] %s", data->pMessage);
          else
            T8_LOG_DEBUG("[VK_VALIDATION] %s", data->pMessage);
          return VK_FALSE;
        };
        createFunc(m_instance, &dbgCI, nullptr, &m_debugMessenger);
        T8_LOG_INFO("[Vulkan] Validation layers enabled with debug messenger");
      }
    }
#endif

    // Create surface via SDL — m_hwnd holds the SDL_Window* passed through SetWindow()
    if (m_hwnd && m_instance) {
      SDL_Window* sdlWin = (SDL_Window*)m_hwnd;
      if (!SDL_Vulkan_CreateSurface(sdlWin, m_instance, nullptr, &m_surface)) {
        T8_LOG_ERROR("[Vulkan] SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
      } else {
        T8_LOG_INFO("[Vulkan] Surface created via SDL");
      }
    }

    CreateDevice();
    T8_LOG_INFO("[Vulkan] >> CreateAllocator...");
    CreateAllocator();
    T8_LOG_INFO("[Vulkan] >> CreateCommandInfrastructure...");
    CreateCommandInfrastructure();
    T8_LOG_INFO("[Vulkan] >> CreateSwapChain...");
    CreateSwapChain();
    T8_LOG_INFO("[Vulkan] >> CreateBackBufferViews...");
    CreateBackBufferViews();
    T8_LOG_INFO("[Vulkan] >> CreateRenderPass...");
    CreateRenderPass();
    T8_LOG_INFO("[Vulkan] >> CreateDepthBuffer...");
    CreateDepthBuffer();
    T8_LOG_INFO("[Vulkan] >> CreateFramebuffers...");
    CreateFramebuffers();
    T8_LOG_INFO("[Vulkan] >> CreateSyncObjects...");
    CreateSyncObjects();
    T8_LOG_INFO("[Vulkan] >> CreateDescriptorPool...");
    CreateDescriptorPool();

    // Pipeline cache
    VkPipelineCacheCreateInfo pcCI = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    vkCreatePipelineCache(m_device, &pcCI, nullptr, &m_vkPipelineCache);

    // Per-frame CB ring buffers
    {
      VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      bufInfo.size = kCBRingBufferSize;
      bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
      bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

      VmaAllocationCreateInfo allocCI = {};
      allocCI.usage = VMA_MEMORY_USAGE_AUTO;
      allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

      for (uint32_t i = 0; i < kBackBufferCount; i++) {
        VmaAllocationInfo allocInfo = {};
        vmaCreateBuffer(m_allocator, &bufInfo, &allocCI,
                        &m_cbRingBuffers[i], &m_cbRingAllocations[i], &allocInfo);
        m_cbRingMapped[i] = allocInfo.pMappedData;
      }
      T8_LOG_INFO("[Vulkan] CB ring buffers created (%u KB x %u)", kCBRingBufferSize / 1024, kBackBufferCount);
    }

    // Negative height flips Y to match D3D/GL NDC (requires VK_KHR_maintenance1)
    m_viewport = { 0.f, (float)height, (float)width, -(float)height, 0.f, 1.f };
    m_scissorRect = { {0, 0}, {(uint32_t)width, (uint32_t)height} };

    CreateDummyTexture();

    T8_LOG_INFO("[Vulkan] Driver initialized (%dx%d)", width, height);
  }

  void VulkanDriver::CreateSurfaces() {}
  void VulkanDriver::DestroySurfaces() {}
  void VulkanDriver::Update() {}

  bool VulkanDriver::ResizeSwapchain(int newW, int newH) {
    if (newW <= 0 || newH <= 0) return false;
    T8_LOG_INFO("[Vulkan] ResizeSwapchain %dx%d -> %dx%d", width, height, newW, newH);

    // Flush GPU: wait for idle AND reset command buffers/descriptor pools
    // so no stale references to resources we're about to destroy
    FlushGPUResources();

    // Destroy old framebuffers, depth, image views
    for (auto& fb : m_backbufferFramebuffers)
      if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_backbufferFramebuffers.clear();

    if (m_depthImageView) { vkDestroyImageView(m_device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
    if (m_depthImage) { vmaDestroyImage(m_allocator, m_depthImage, m_depthAllocation); m_depthImage = VK_NULL_HANDLE; }

    for (auto& iv : m_swapChainImageViews)
      vkDestroyImageView(m_device, iv, nullptr);
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();

    // Destroy old swap chain
    VkSwapchainKHR oldSwapChain = m_swapChain;
    m_swapChain = VK_NULL_HANDLE;
    if (oldSwapChain) vkDestroySwapchainKHR(m_device, oldSwapChain, nullptr);

    // Update dimensions
    width = newW;
    height = newH;

    // Recreate swap chain, views, depth, framebuffers
    CreateSwapChain();
    CreateBackBufferViews();
    CreateDepthBuffer();
    CreateFramebuffers();

    // Update viewport
    m_viewport = { 0.f, (float)height, (float)width, -(float)height, 0.f, 1.f };
    m_scissorRect = { {0, 0}, {(uint32_t)width, (uint32_t)height} };

    // Invalidate pipeline cache entries that reference the old backbuffer render pass
    // (render passes themselves are NOT recreated — same formats, same attachments)

    // Reset frame state
    m_currentFrame = 0;
    m_frameStarted = false;
    m_renderPassActive = false;

    T8_LOG_INFO("[Vulkan] Swapchain recreated (%dx%d)", width, height);
    return true;
  }

  void VulkanDriver::FlushGPUResources() {
    if (!m_device) return;
    WaitForGPU();
    // Reset all command buffers to release references to resources
    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_commandBuffers[i])
        vkResetCommandBuffer(m_commandBuffers[i], 0);
    }
    // Reset descriptor pools to release descriptor set references
    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_descriptorPools[i])
        vkResetDescriptorPool(m_device, m_descriptorPools[i], 0);
    }
    T8_LOG_INFO("[Vulkan] GPU flushed, command buffers and descriptor pools reset");
  }

  void VulkanDriver::DestroyDriver() {
    FlushGPUResources();

    DestroyShaders();
    DestroyRTs();
    DestroyTextures();

    // Destroy pipeline cache entries
    for (auto& pair : m_pipelineCache)
      vkDestroyPipeline(m_device, pair.second, nullptr);
    m_pipelineCache.clear();

    if (m_vkPipelineCache) { vkDestroyPipelineCache(m_device, m_vkPipelineCache, nullptr); m_vkPipelineCache = VK_NULL_HANDLE; }

    // Destroy dummy texture
    if (m_dummySampler)   { vkDestroySampler(m_device, m_dummySampler, nullptr); m_dummySampler = VK_NULL_HANDLE; }
    if (m_dummyImageView) { vkDestroyImageView(m_device, m_dummyImageView, nullptr); m_dummyImageView = VK_NULL_HANDLE; }
    if (m_dummyImage)     { vmaDestroyImage(m_allocator, m_dummyImage, m_dummyAllocation); m_dummyImage = VK_NULL_HANDLE; }
    if (m_dummyCubeImageView) { vkDestroyImageView(m_device, m_dummyCubeImageView, nullptr); m_dummyCubeImageView = VK_NULL_HANDLE; }
    if (m_dummyCubeImage) { vmaDestroyImage(m_allocator, m_dummyCubeImage, m_dummyCubeAllocation); m_dummyCubeImage = VK_NULL_HANDLE; }

    // Destroy CB ring buffers
    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_cbRingBuffers[i]) {
        vmaDestroyBuffer(m_allocator, m_cbRingBuffers[i], m_cbRingAllocations[i]);
        m_cbRingBuffers[i] = VK_NULL_HANDLE;
        m_cbRingMapped[i] = nullptr;
      }
    }

    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_descriptorPools[i]) { vkDestroyDescriptorPool(m_device, m_descriptorPools[i], nullptr); m_descriptorPools[i] = VK_NULL_HANDLE; }
    }

    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_imageAvailableSemaphores[i]) vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
      if (m_renderFinishedSemaphores[i]) vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
      if (m_inFlightFences[i])           vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    for (auto& fb : m_backbufferFramebuffers)
      if (fb) { vkDestroyFramebuffer(m_device, fb, nullptr); fb = VK_NULL_HANDLE; }
    m_backbufferFramebuffers.clear();

    if (m_depthImageView) { vkDestroyImageView(m_device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
    if (m_depthImage)     { vmaDestroyImage(m_allocator, m_depthImage, m_depthAllocation); m_depthImage = VK_NULL_HANDLE; }

    if (m_backbufferRenderPass) { vkDestroyRenderPass(m_device, m_backbufferRenderPass, nullptr); m_backbufferRenderPass = VK_NULL_HANDLE; }
    if (m_backbufferRenderPassLoad) { vkDestroyRenderPass(m_device, m_backbufferRenderPassLoad, nullptr); m_backbufferRenderPassLoad = VK_NULL_HANDLE; }

    for (auto& iv : m_swapChainImageViews)
      vkDestroyImageView(m_device, iv, nullptr);
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();

    if (m_swapChain) { vkDestroySwapchainKHR(m_device, m_swapChain, nullptr); m_swapChain = VK_NULL_HANDLE; }

    if (m_transientCommandPool) { vkDestroyCommandPool(m_device, m_transientCommandPool, nullptr); m_transientCommandPool = VK_NULL_HANDLE; }
    if (m_commandPool) { vkDestroyCommandPool(m_device, m_commandPool, nullptr); m_commandPool = VK_NULL_HANDLE; }

    if (m_allocator) { vmaDestroyAllocator(m_allocator); m_allocator = VK_NULL_HANDLE; }

    if (m_surface) { vkDestroySurfaceKHR(m_instance, m_surface, nullptr); m_surface = VK_NULL_HANDLE; }

    if (m_device) { vkDestroyDevice(m_device, nullptr); m_device = VK_NULL_HANDLE; }

#ifdef T8_VULKAN_VALIDATION
    if (m_debugMessenger) {
      auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
      if (func) func(m_instance, m_debugMessenger, nullptr);
      m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (m_instance) { vkDestroyInstance(m_instance, nullptr); m_instance = VK_NULL_HANDLE; }

    T8Device->release();
    T8DeviceContext->release();
    delete T8Device;   T8Device = nullptr;
    delete T8DeviceContext; T8DeviceContext = nullptr;

    T8_LOG_INFO("[Vulkan] Driver destroyed");
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Synchronization
  // ══════════════════════════════════════════════════════

  void VulkanDriver::WaitForFence(uint32_t frameIndex) {
    vkWaitForFences(m_device, 1, &m_inFlightFences[frameIndex], VK_TRUE, UINT64_MAX);
  }

  void VulkanDriver::WaitForGPU() {
    if (m_device) vkDeviceWaitIdle(m_device);
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Frame lifecycle
  // ══════════════════════════════════════════════════════

  void VulkanDriver::BeginFrame() {
    {
      T8_PROFILE_CPU_SCOPE(t800::g_profiler, "VK_FenceWait");
      WaitForFence(m_currentFrame);
      vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    }

    VkResult res = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
                                          m_imageAvailableSemaphores[m_currentFrame],
                                          VK_NULL_HANDLE, &m_imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_ERROR_SURFACE_LOST_KHR) {
      T8_LOG_INFO("[Vulkan] Swap chain out of date (res=%d), recreating...", res);
      ResizeSwapchain(width, height);
      // After recreation, fences are re-signaled by WaitForGPU inside ResizeSwapchain.
      // Reset the fence for the current frame before re-acquiring.
      WaitForFence(m_currentFrame);
      vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
      // Re-acquire after recreation
      res = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
                                  m_imageAvailableSemaphores[m_currentFrame],
                                  VK_NULL_HANDLE, &m_imageIndex);
      if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        T8_LOG_ERROR("[Vulkan] vkAcquireNextImageKHR failed after recreation res=%d", res);
        m_frameStarted = false;
        return;
      }
    } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
      T8_LOG_ERROR("[Vulkan] vkAcquireNextImageKHR failed res=%d", res);
      m_frameStarted = false;
      return;
    }

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    static_cast<VulkanDeviceContext*>(T8DeviceContext)->m_commandBuffer = cmd;

    // Flush profiler query pool reset (must happen before any render pass)
#ifdef T8_ENABLE_PROFILER
    if (t800::g_profiler) t800::g_profiler->FlushVulkanQueryReset(cmd);
#endif

    // Reset per-frame descriptor pool and pending state
    vkResetDescriptorPool(m_device, m_descriptorPools[m_currentFrame], 0);
    m_cbRingOffset = 0;
    m_cbDirty = false;
    memset(m_pendingTextures, 0, sizeof(m_pendingTextures));

    // Reserve a dummy CB region so draws without explicit CB still have valid descriptors
    m_pendingCB = {};
    m_pendingCB.buffer = m_cbRingBuffers[m_currentFrame];
    m_pendingCB.offset = 0;
    m_pendingCB.range  = 256;
    m_cbRingOffset = 256;

    m_lastPipeline = VK_NULL_HANDLE;
    m_lastPipelineLayout = VK_NULL_HANDLE;
    m_screenshotConsumedSemaphore = false;
    m_frameStarted = true;

    // Reset topology to triangle list at the start of each frame
    static_cast<VulkanDeviceContext*>(T8DeviceContext)->m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  }

  void VulkanDriver::EndFrame() {}

  void VulkanDriver::BuildPipelineObjects() {
    T8_LOG_INFO("[Vulkan] BuildPipelineObjects");
  }

  void VulkanDriver::Clear() {
    if (!m_frameStarted) {
      BeginFrame();
      m_frameStarted = true;
    }

    if (CurrentRT < 0) {
      VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

      // End any active render pass before starting a new one
      EndRenderPassIfActive(cmd);

      VkClearValue clearValues[2] = {};
      clearValues[0].color = { {0.9f, 0.9f, 0.9f, 1.0f} };
      clearValues[1].depthStencil = { 1.0f, 0 };

      VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
      rpBegin.renderPass = m_backbufferRenderPass;
      rpBegin.framebuffer = m_backbufferFramebuffers[m_imageIndex];
      rpBegin.renderArea.offset = { 0, 0 };
      rpBegin.renderArea.extent = m_swapChainExtent;
      rpBegin.clearValueCount = 2;
      rpBegin.pClearValues = clearValues;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      m_activeRenderPass = m_backbufferRenderPass;
      m_renderPassActive = true;

      vkCmdSetViewport(cmd, 0, 1, &m_viewport);
      vkCmdSetScissor(cmd, 0, 1, &m_scissorRect);
    }
  }

  void VulkanDriver::EnsureBackbufferRenderPass() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    if (!m_renderPassActive) {
      // Begin the backbuffer render pass with LOAD_OP_LOAD (preserve existing content)
      VkRenderPassBeginInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
      rpInfo.renderPass  = m_backbufferRenderPassLoad;
      rpInfo.framebuffer = m_backbufferFramebuffers[m_imageIndex];
      rpInfo.renderArea  = m_scissorRect;
      vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
      m_renderPassActive = true;
      m_activeRenderPass = m_backbufferRenderPassLoad;
      vkCmdSetViewport(cmd, 0, 1, &m_viewport);
      vkCmdSetScissor(cmd, 0, 1, &m_scissorRect);
    }
  }

  void VulkanDriver::SwapBuffers() {
    T8_LOG_TRACE("[Vulkan] SwapBuffers");
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // End the render pass if one is active
    if (m_renderPassActive) {
      vkCmdEndRenderPass(cmd);
      m_renderPassActive = false;
    }

    // Transition backbuffer from COLOR_ATTACHMENT → PRESENT_SRC for presentation
    TransitionImageLayout(cmd, m_swapChainImages[m_imageIndex],
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_ASPECT_COLOR_BIT);

    // End command buffer
    VkResult endRes = vkEndCommandBuffer(cmd);
    if (endRes != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkEndCommandBuffer failed res=%d", endRes);
    }

    // Submit
    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = m_screenshotConsumedSemaphore ? 0 : 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitRes = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (submitRes != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkQueueSubmit failed res=%d", submitRes);
      if (submitRes == VK_ERROR_DEVICE_LOST) {
        T8_LOG_ERROR("[Vulkan] Device lost! Waiting for idle...");
        vkDeviceWaitIdle(m_device);
      }
    }

    // Present
    VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_imageIndex;

    VkResult presentRes = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_ERROR_SURFACE_LOST_KHR ||
        presentRes == VK_SUBOPTIMAL_KHR) {
      T8_LOG_INFO("[Vulkan] Present: swap chain needs recreation (res=%d)", presentRes);
      // Swapchain will be recreated on next BeginFrame's acquire
    } else if (presentRes != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkQueuePresentKHR failed res=%d", presentRes);
    }

    m_currentFrame = (m_currentFrame + 1) % kBackBufferCount;
    m_frameStarted = false;
  }

  void VulkanDriver::SetBlendState(BLEND_STATES state) {
    T8_LOG_TRACE("[Vulkan] SetBlendState(%d)", state);
    m_currentBlend = state;
  }

  void VulkanDriver::SetDepthStencilState(DEPTH_STENCIL_STATES state) {
    T8_LOG_TRACE("[Vulkan] SetDepthStencilState(%d)", state);
    m_currentDepth = state;
  }

  void VulkanDriver::SetCullFace(FACE_CULLING state) {
    T8_LOG_TRACE("[Vulkan] SetCullFace(%d)", state);
    m_currentCull = state;
    m_FaceCulling = state;
  }

  void VulkanDriver::PopRT() {
    T8_LOG_TRACE("[Vulkan] PopRT (CurrentRT=%d)", CurrentRT);

    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size() && RTs[CurrentRT]) {
      VulkanRT* rt = static_cast<VulkanRT*>(RTs[CurrentRT]);
      VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

      // 1. End the current RT's render pass (guarded)
      if (m_renderPassActive) {
        vkCmdEndRenderPass(cmd);
        m_renderPassActive = false;
      }

      // 2. Transition color images from COLOR_ATTACHMENT → SHADER_READ_ONLY
      for (int i = 0; i < rt->number_RT; i++) {
        TransitionImageLayout(cmd, rt->vColorImages[i],
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT);
        rt->vColorLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }

      // 3. Transition depth from DEPTH_STENCIL_ATTACHMENT → SHADER_READ_ONLY
      bool hasDepth = (rt->depth_format != BaseRT::NOTHING);
      if (hasDepth && rt->m_depthImage) {
        TransitionImageLayout(cmd, rt->m_depthImage,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_ASPECT_DEPTH_BIT);
      }

      // 4. Restore the backbuffer render pass (LOAD variant to preserve content)
      VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
      rpBegin.renderPass = m_backbufferRenderPassLoad;
      rpBegin.framebuffer = m_backbufferFramebuffers[m_imageIndex];
      rpBegin.renderArea.offset = { 0, 0 };
      rpBegin.renderArea.extent = m_swapChainExtent;
      rpBegin.clearValueCount = 0;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      m_activeRenderPass = m_backbufferRenderPassLoad;
      m_renderPassActive = true;

      vkCmdSetViewport(cmd, 0, 1, &m_viewport);
      vkCmdSetScissor(cmd, 0, 1, &m_scissorRect);
    }

    CurrentRT = -1;
  }

  void VulkanDriver::SaveScreenshot(std::string path) {
    // End the current render pass if active
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    if (m_renderPassActive) {
      vkCmdEndRenderPass(cmd);
      m_renderPassActive = false;
    }

    // Close and submit current command buffer, wait for GPU
    // Must wait on image-available semaphore (first submit using this swapchain image)
    vkEndCommandBuffer(cmd);
    VkSemaphore waitSem[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStage[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = m_screenshotConsumedSemaphore ? 0 : 1;
    submitInfo.pWaitSemaphores = waitSem;
    submitInfo.pWaitDstStageMask = waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    m_screenshotConsumedSemaphore = true;

    // Get swap chain image
    VkImage srcImage = m_swapChainImages[m_imageIndex];
    uint32_t w = (uint32_t)width;
    uint32_t h = (uint32_t)height;

    // Create staging buffer for readback
    VkDeviceSize bufSize = w * h * 4;
    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = bufSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer stagingBuf;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingAllocInfo;
    vmaCreateBuffer(m_allocator, &bufInfo, &allocCI, &stagingBuf, &stagingAlloc, &stagingAllocInfo);

    // Record copy command
    VkCommandBuffer copyCmd;
    VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = m_transientCommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_device, &cmdAlloc, &copyCmd);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(copyCmd, &beginInfo);

    // Transition swapchain image to TRANSFER_SRC (image is in COLOR_ATTACHMENT after render pass)
    TransitionImageLayout(copyCmd, srcImage,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { w, h, 1 };
    vkCmdCopyImageToBuffer(copyCmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuf, 1, &region);

    // Transition back to COLOR_ATTACHMENT (we restart the render pass next)
    TransitionImageLayout(copyCmd, srcImage,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT);

    vkEndCommandBuffer(copyCmd);
    VkSubmitInfo copySubmit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    copySubmit.commandBufferCount = 1;
    copySubmit.pCommandBuffers = &copyCmd;
    vkQueueSubmit(m_graphicsQueue, 1, &copySubmit, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    // Read pixels and write PPM
    const uint8_t* pixels = (const uint8_t*)stagingAllocInfo.pMappedData;
    std::vector<uint8_t> rgb(w * h * 3);
    for (uint32_t i = 0; i < w * h; i++) {
      rgb[i * 3 + 0] = pixels[i * 4 + 0]; // R (or B if BGRA)
      rgb[i * 3 + 1] = pixels[i * 4 + 1]; // G
      rgb[i * 3 + 2] = pixels[i * 4 + 2]; // B (or R if BGRA)
    }

    // Check if format is BGRA and swap
    if (m_swapChainFormat == VK_FORMAT_B8G8R8A8_UNORM || m_swapChainFormat == VK_FORMAT_B8G8R8A8_SRGB) {
      for (uint32_t i = 0; i < w * h; i++) {
        std::swap(rgb[i * 3 + 0], rgb[i * 3 + 2]);
      }
    }

    std::ofstream out(path + ".ppm", std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    out.write((const char*)rgb.data(), rgb.size());
    T8_LOG_INFO("[Vulkan] Saved %s.ppm (%ux%u)", path.c_str(), w, h);

    vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &copyCmd);
    vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);

    // Reopen command buffer for continued rendering
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo);

    // Restart backbuffer render pass
    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = m_backbufferRenderPass;
    rpBegin.framebuffer = m_backbufferFramebuffers[m_imageIndex];
    rpBegin.renderArea.extent = { (uint32_t)width, (uint32_t)height };
    VkClearValue clears[2] = {};
    clears[0].color = {{0.9f, 0.9f, 0.9f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues = clears;
    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    m_renderPassActive = true;
    m_activeRenderPass = m_backbufferRenderPass;
  }

  void VulkanDriver::SaveRTToFile(int rtID, int attachment, std::string path) {
    if (rtID < 0 || rtID >= (int)RTs.size() || !RTs[rtID]) return;
    VulkanRT* rt = static_cast<VulkanRT*>(RTs[rtID]);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // End any active render pass before readback
    if (m_renderPassActive) {
      vkCmdEndRenderPass(cmd);
      m_renderPassActive = false;
    }

    // Flush command buffer
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    VkImage srcImage = VK_NULL_HANDLE;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t w = (uint32_t)rt->w;
    uint32_t h = (uint32_t)rt->h;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    if (attachment == DEPTH_ATTACHMENT) {
      if (!rt->m_depthImage) goto reopen;
      srcImage = rt->m_depthImage;
      fmt = rt->m_depthFormat;
      aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else if (attachment >= 0 && attachment < rt->number_RT) {
      srcImage = rt->vColorImages[attachment];
      fmt = rt->m_colorFormat;
    } else {
      goto reopen;
    }

    {
      // Determine bytes per pixel from format
      uint32_t bpp = 4;
      bool isFloat16 = (fmt == VK_FORMAT_R16_SFLOAT || fmt == VK_FORMAT_R16G16B16A16_SFLOAT);
      bool isFloat32 = (fmt == VK_FORMAT_R32_SFLOAT || fmt == VK_FORMAT_D32_SFLOAT);
      bool isR8 = (fmt == VK_FORMAT_R8_UNORM);
      if (isFloat16) bpp = (fmt == VK_FORMAT_R16_SFLOAT) ? 2 : 8;
      else if (isFloat32) bpp = 4;
      else if (isR8) bpp = 1;

      VkDeviceSize bufSize = (VkDeviceSize)w * h * bpp;
      VkBufferCreateInfo bufCI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      bufCI.size = bufSize;
      bufCI.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      VmaAllocationCreateInfo allocCI = {};
      allocCI.usage = VMA_MEMORY_USAGE_AUTO;
      allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
      VkBuffer stagingBuf;
      VmaAllocation stagingAlloc;
      VmaAllocationInfo stagingInfo;
      vmaCreateBuffer(m_allocator, &bufCI, &allocCI, &stagingBuf, &stagingAlloc, &stagingInfo);

      // Record copy
      VkCommandBuffer copyCmd;
      VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      cmdAlloc.commandPool = m_transientCommandPool;
      cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmdAlloc.commandBufferCount = 1;
      vkAllocateCommandBuffers(m_device, &cmdAlloc, &copyCmd);
      VkCommandBufferBeginInfo beginCI = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(copyCmd, &beginCI);

      TransitionImageLayout(copyCmd, srcImage, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, aspect);

      VkBufferImageCopy region = {};
      region.imageSubresource.aspectMask = aspect;
      region.imageSubresource.layerCount = 1;
      region.imageExtent = { w, h, 1 };
      vkCmdCopyImageToBuffer(copyCmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuf, 1, &region);

      TransitionImageLayout(copyCmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcLayout, aspect);

      vkEndCommandBuffer(copyCmd);
      VkSubmitInfo copySubmit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      copySubmit.commandBufferCount = 1;
      copySubmit.pCommandBuffers = &copyCmd;
      vkQueueSubmit(m_graphicsQueue, 1, &copySubmit, VK_NULL_HANDLE);
      vkQueueWaitIdle(m_graphicsQueue);

      // Convert to RGB PPM
      const uint8_t* pixels = (const uint8_t*)stagingInfo.pMappedData;
      std::vector<uint8_t> rgb(w * h * 3);
      for (uint32_t i = 0; i < w * h; i++) {
        if (bpp == 4 && !isFloat32) {
          rgb[i*3+0] = pixels[i*4+0];
          rgb[i*3+1] = pixels[i*4+1];
          rgb[i*3+2] = pixels[i*4+2];
        } else if (isFloat16 && bpp == 8) {
          // RGBA16F → uint8 via IEEE 754 half-float decode
          const uint16_t* fp = (const uint16_t*)(pixels + i * 8);
          auto h2f = [](uint16_t h) -> float {
            uint32_t sign = (h >> 15) & 1; uint32_t exp = (h >> 10) & 0x1F; uint32_t mant = h & 0x3FF;
            if (exp == 0) return sign ? -0.0f : 0.0f;
            if (exp == 31) return sign ? -1e30f : 1e30f;
            float f = ((float)mant / 1024.0f + 1.0f) * ldexpf(1.0f, (int)exp - 15);
            return sign ? -f : f;
          };
          for (int c = 0; c < 3; c++) {
            float v = h2f(fp[c]);
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            rgb[i*3+c] = (uint8_t)(v * 255.0f);
          }
        } else if (isR8) {
          rgb[i*3+0] = rgb[i*3+1] = rgb[i*3+2] = pixels[i];
        } else if (isFloat32) {
          const float* fp = (const float*)(pixels + i * 4);
          float v = *fp; v = v < 0 ? 0 : (v > 1 ? 1 : v);
          uint8_t b = (uint8_t)(v * 255.0f);
          rgb[i*3+0] = rgb[i*3+1] = rgb[i*3+2] = b;
        } else if (isFloat16 && bpp == 2) {
          const uint16_t* fp = (const uint16_t*)(pixels + i * 2);
          float v = (float)(*fp) / 65535.0f;
          uint8_t b = (uint8_t)(v * 255.0f);
          rgb[i*3+0] = rgb[i*3+1] = rgb[i*3+2] = b;
        }
      }

      // BGRA swap for backbuffer format
      if (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB) {
        for (uint32_t i = 0; i < w * h; i++)
          std::swap(rgb[i*3+0], rgb[i*3+2]);
      }

      std::ofstream out(path + ".ppm", std::ios::binary);
      out << "P6\n" << w << " " << h << "\n255\n";
      out.write((const char*)rgb.data(), rgb.size());
      T8_LOG_INFO("[Vulkan] Saved %s.ppm (%ux%u fmt=%d)", path.c_str(), w, h, (int)fmt);

      vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &copyCmd);
      vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);
    }

reopen:
    // Reopen command buffer
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo);

    // Restart backbuffer render pass (LOAD to preserve)
    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = m_backbufferRenderPassLoad;
    rpBegin.framebuffer = m_backbufferFramebuffers[m_imageIndex];
    rpBegin.renderArea.extent = m_swapChainExtent;
    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    m_renderPassActive = true;
    m_activeRenderPass = m_backbufferRenderPassLoad;

    vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &m_viewport);
    vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &m_scissorRect);
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Upload & ring buffer helpers
  // ══════════════════════════════════════════════════════

  void VulkanDriver::UploadBufferData(VkBuffer dest, const void* data, VkDeviceSize dataSize) {
    // Create staging buffer
    VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = dataSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCI = {};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingAllocInfo;
    vmaCreateBuffer(m_allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
    memcpy(stagingAllocInfo.pMappedData, data, dataSize);

    // Record and submit copy command
    VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = m_transientCommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion = {};
    copyRegion.size = dataSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, dest, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &cmd);
    vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAlloc);
  }

  VkDescriptorBufferInfo VulkanDriver::AllocateCBData(const void* data, uint32_t dataSize) {
    uint32_t alignedSize = (dataSize + 255) & ~255u;
    if (m_cbRingOffset + alignedSize > kCBRingBufferSize) {
      T8_LOG_ERROR("[Vulkan] CB ring buffer overflow! offset=%u + size=%u > %u", m_cbRingOffset, alignedSize, kCBRingBufferSize);
      m_cbRingOffset = 0;
    }

    uint32_t bufIdx = m_currentFrame;
    unsigned char* dst = (unsigned char*)m_cbRingMapped[bufIdx] + m_cbRingOffset;
    memcpy(dst, data, dataSize);

    VkDescriptorBufferInfo info = {};
    info.buffer = m_cbRingBuffers[bufIdx];
    info.offset = m_cbRingOffset;
    info.range = alignedSize;

    m_cbRingOffset += alignedSize;
    return info;
  }

  VulkanDriver::VBRingAlloc VulkanDriver::AllocateVBRing(const void* data, uint32_t size) {
    // Must align to 256 so subsequent UBO allocations from the same ring stay aligned
    uint32_t aligned = (size + 255) & ~255u;
    if (m_cbRingOffset + aligned > kCBRingBufferSize) {
      return { VK_NULL_HANDLE, 0, false };
    }
    uint32_t bufIdx = m_currentFrame;
    unsigned char* dst = (unsigned char*)m_cbRingMapped[bufIdx] + m_cbRingOffset;
    memcpy(dst, data, size);
    VBRingAlloc alloc;
    alloc.buffer = m_cbRingBuffers[bufIdx];
    alloc.offset = m_cbRingOffset;
    alloc.valid = true;
    m_cbRingOffset += aligned;
    return alloc;
  }

  void VulkanDriver::BindBackBufferNoDepth() {
    T8_LOG_TRACE("[Vulkan] BindBackBufferNoDepth");
    // In Vulkan, depth test is controlled by PSO state (NONE), so this is a no-op.
    // The depth attachment remains bound but the pipeline disables depth testing.
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Descriptor set allocation & binding
  // ══════════════════════════════════════════════════════

  VkDescriptorSet VulkanDriver::AllocateDescriptorSet(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = m_descriptorPools[m_currentFrame];
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet ds = VK_NULL_HANDLE;
    VkResult res = vkAllocateDescriptorSets(m_device, &allocInfo, &ds);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] AllocateDescriptorSets failed res=%d", res);
      return VK_NULL_HANDLE;
    }
    return ds;
  }

  void VulkanDriver::BindPendingDescriptors(VkCommandBuffer cmd, VulkanShader* shader) {
    VkDescriptorSet ds = AllocateDescriptorSet(shader->m_descriptorSetLayout);
    if (!ds) return;

    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> imageInfos;
    writes.reserve(16);
    imageInfos.reserve(8);

    // UBO binding
    if (shader->cbvBinding >= 0) {
      VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
      w.dstSet = ds;
      w.dstBinding = (uint32_t)shader->cbvBinding;
      w.descriptorCount = 1;
      w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w.pBufferInfo = &m_pendingCB;
      writes.push_back(w);
    }

    // Texture bindings (from reflected srv bindings)
    for (int slot = 0; slot < 8; slot++) {
      if (shader->srvBindings[slot] < 0) continue;

      VkDescriptorImageInfo imgInfo = {};
      imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      // Use cubemap dummy for slots that expect cubemap views
      VkImageView defaultView = shader->srvIsCubemap[slot] ? m_dummyCubeImageView : m_dummyImageView;
      imgInfo.imageView = m_pendingTextures[slot].imageView ? m_pendingTextures[slot].imageView : defaultView;
      imgInfo.sampler   = m_pendingTextures[slot].sampler   ? m_pendingTextures[slot].sampler   : m_dummySampler;

      imageInfos.push_back(imgInfo);

      VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
      w.dstSet = ds;
      w.dstBinding = (uint32_t)shader->srvBindings[slot];
      w.descriptorCount = 1;
      w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w.pImageInfo = &imageInfos.back();
      writes.push_back(w);
    }

    if (!writes.empty()) {
      vkUpdateDescriptorSets(m_device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            shader->m_pipelineLayout, 0, 1, &ds, 0, nullptr);
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Dummy texture for unbound slots
  // ══════════════════════════════════════════════════════

  void VulkanDriver::CreateDummyTexture() {
    // 1x1 white pixel
    uint8_t pixel[4] = { 255, 255, 255, 255 };

    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgCI.extent = { 1, 1, 1 };
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;

    vmaCreateImage(m_allocator, &imgCI, &allocCI, &m_dummyImage, &m_dummyAllocation, nullptr);

    // Upload pixel via staging buffer
    VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = 4;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCI = {};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingAllocInfo;
    vmaCreateBuffer(m_allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
    memcpy(stagingAllocInfo.pMappedData, pixel, 4);

    VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = m_transientCommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    TransitionImageLayout(cmd, m_dummyImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { 1, 1, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_dummyImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(cmd, m_dummyImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_ASPECT_COLOR_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &cmd);
    vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAlloc);

    // Image view
    VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivCI.image = m_dummyImage;
    ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivCI.subresourceRange.levelCount = 1;
    ivCI.subresourceRange.layerCount = 1;
    vkCreateImageView(m_device, &ivCI, nullptr, &m_dummyImageView);

    // Sampler
    VkSamplerCreateInfo sampCI = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampCI.magFilter = VK_FILTER_NEAREST;
    sampCI.minFilter = VK_FILTER_NEAREST;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(m_device, &sampCI, nullptr, &m_dummySampler);

    // Dummy cubemap (1x1x6 faces) for unbound cubemap slots (e.g. texEnv)
    VkImageCreateInfo cubeCI = imgCI;
    cubeCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cubeCI.arrayLayers = 6;
    VkImage dummyCubeImage = VK_NULL_HANDLE;
    VmaAllocation dummyCubeAlloc = VK_NULL_HANDLE;
    vmaCreateImage(m_allocator, &cubeCI, &allocCI, &dummyCubeImage, &dummyCubeAlloc, nullptr);

    // Transition cube to SHADER_READ_ONLY
    {
      VkCommandBuffer cubeCmd;
      vkAllocateCommandBuffers(m_device, &cmdAlloc, &cubeCmd);
      vkBeginCommandBuffer(cubeCmd, &beginInfo);
      VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.image = dummyCubeImage;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = 6;
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cubeCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                           0, nullptr, 0, nullptr, 1, &barrier);
      vkEndCommandBuffer(cubeCmd);
      VkSubmitInfo cubeSubmit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      cubeSubmit.commandBufferCount = 1;
      cubeSubmit.pCommandBuffers = &cubeCmd;
      vkQueueSubmit(m_graphicsQueue, 1, &cubeSubmit, VK_NULL_HANDLE);
      vkQueueWaitIdle(m_graphicsQueue);
      vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &cubeCmd);
    }

    VkImageViewCreateInfo cubeIvCI = ivCI;
    cubeIvCI.image = dummyCubeImage;
    cubeIvCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cubeIvCI.subresourceRange.layerCount = 6;
    vkCreateImageView(m_device, &cubeIvCI, nullptr, &m_dummyCubeImageView);

    m_dummyCubeImage = dummyCubeImage;
    m_dummyCubeAllocation = dummyCubeAlloc;

    T8_LOG_INFO("[Vulkan] Dummy textures created (2D + Cube)");
  }

} // namespace t800

#endif // OS_WINDOWS
