#include "pch.h"
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanTexture.cpp: Texture implementation
 *********************************************************/

#include <video/vulkan/VulkanTexture.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS)

#include <utils/Log.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace t800 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

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

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = nullptr;
    VmaAllocationInfo stagingAllocInfo = {};
    {
      VkResult sres = vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
      if (sres != VK_SUCCESS || !stagingAllocInfo.pMappedData) {
        T8_LOG_ERROR("[Vulkan] Texture staging buffer creation failed res=%d mapped=%p", sres, stagingAllocInfo.pMappedData);
        if (stagingBuffer) vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        vmaDestroyImage(allocator, m_image, m_allocation);
        m_image = VK_NULL_HANDLE; m_allocation = nullptr;
        return;
      }
    }
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
    VkBuffer stagingBuffer = VK_NULL_HANDLE; VmaAllocation stagingAlloc = nullptr; VmaAllocationInfo stagingAllocInfo = {};
    {
      VkResult sres = vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
      if (sres != VK_SUCCESS || !stagingAllocInfo.pMappedData) {
        T8_LOG_ERROR("[Vulkan] Compressed texture staging buffer creation failed res=%d mapped=%p", sres, stagingAllocInfo.pMappedData);
        if (stagingBuffer) vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        vmaDestroyImage(allocator, m_image, m_allocation);
        m_image = VK_NULL_HANDLE; m_allocation = nullptr;
        return;
      }
    }
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

  void VulkanTexture::UpdateFloatData(const DeviceContext& deviceContext, int w, int h, const float* data) {
    if (!m_image || !data || !m_isFloatTex) return;
    auto* driver = static_cast<VulkanDriver*>(g_pBaseDriver);
    VmaAllocator allocator = driver->GetAllocator();

    VkDeviceSize totalSize = (VkDeviceSize)w * h * 16;

    // Create staging buffer
    VkBuffer stagingBuffer; VmaAllocation stagingAlloc;
    VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = totalSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo stagingAllocCI = {};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo stagingAllocInfo;
    vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
    memcpy(stagingAllocInfo.pMappedData, data, totalSize);

    // Record copy in current frame's command buffer
    VkCommandBuffer cmd = driver->GetCurrentCommandBuffer();

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = m_image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Defer staging buffer cleanup to after frame completes
    driver->DeferCleanup(stagingBuffer, stagingAlloc);
  }

  // ══════════════════════════════════════════════════════
  //  VulkanRT
  // ══════════════════════════════════════════════════════

} // namespace t800

#endif // OS_WINDOWS
