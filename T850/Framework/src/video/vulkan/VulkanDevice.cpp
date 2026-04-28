#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanDevice.cpp: Device implementation
 *********************************************************/

#include <video/vulkan/VulkanDevice.h>
#include <video/vulkan/VulkanVertexBuffer.h>
#include <video/vulkan/VulkanIndexBuffer.h>
#include <video/vulkan/VulkanConstantBuffer.h>
#include <video/vulkan/VulkanTexture.h>
#include <video/vulkan/VulkanRT.h>
#include <video/vulkan/VulkanShader.h>
#include <video/vulkan/VulkanDriver.h>
#include <vector>

#if defined(OS_WINDOWS)

#include <utils/Log.h>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  VulkanDevice
  // ══════════════════════════════════════════════════════

  void* VulkanDevice::GetAPIObject() const { return (void*)m_device; }
  void** VulkanDevice::GetAPIObjectReference() const { return nullptr; }
  void VulkanDevice::release() { m_device = VK_NULL_HANDLE; }

  Buffer* VulkanDevice::CreateBuffer(BufferType::E bufferType, BufferDesc desc, void* initialData) {
    T8_LOG_DEBUG("[Vulkan] CreateBuffer type=%d size=%d", bufferType, desc.byteWidth);
    Buffer* buf = nullptr;
    switch (bufferType) {
      case BufferType::VERTEX:   buf = new VulkanVertexBuffer;   break;
      case BufferType::INDEX:    buf = new VulkanIndexBuffer;    break;
      case BufferType::CONSTANT: buf = new VulkanConstantBuffer; break;
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

  Texture* VulkanDevice::CreateFloatTexture(int w, int h, const float* data) {
    auto* driver = static_cast<VulkanDriver*>(g_pBaseDriver);
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    VulkanTexture* tex = new VulkanTexture;
    tex->m_format = VK_FORMAT_R32G32B32A32_SFLOAT;
    tex->m_isFloatTex = true;
    tex->x = w;
    tex->y = h;

    // Create VkImage
    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = tex->m_format;
    imgCI.extent = { (uint32_t)w, (uint32_t)h, 1 };
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator, &imgCI, &allocCI, &tex->m_image, &tex->m_allocation, nullptr) != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateFloatTexture: vmaCreateImage failed");
      delete tex; return nullptr;
    }

    // Create image view
    VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivCI.image = tex->m_image;
    ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = tex->m_format;
    ivCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(device, &ivCI, nullptr, &tex->m_imageView) != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateFloatTexture: vkCreateImageView failed");
      delete tex; return nullptr;
    }

    // Create sampler (NEAREST, no interpolation)
    VkSamplerCreateInfo sampCI = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampCI.magFilter = VK_FILTER_NEAREST;
    sampCI.minFilter = VK_FILTER_NEAREST;
    sampCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampCI.addressModeU = sampCI.addressModeV = sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.maxLod = 0.0f;
    if (vkCreateSampler(device, &sampCI, nullptr, &tex->m_sampler) != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateFloatTexture: vkCreateSampler failed");
      delete tex; return nullptr;
    }

    // Upload initial data if provided
    if (data) {
      VkDeviceSize totalSize = (VkDeviceSize)w * h * 16;
      VkBuffer stagingBuffer; VmaAllocation stagingAlloc;
      VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      stagingInfo.size = totalSize;
      stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      VmaAllocationCreateInfo stagingAllocCI = {};
      stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
      stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
      VmaAllocationInfo stagingAllocInfo;
      vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
      memcpy(stagingAllocInfo.pMappedData, data, static_cast<size_t>(totalSize));

      VkCommandBuffer cmd = driver->GetTransientCommandBuffer();
      VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      barrier.image = tex->m_image;
      barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
      barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);

      VkBufferImageCopy region = {};
      region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
      vkCmdCopyBufferToImage(cmd, stagingBuffer, tex->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);

      driver->SubmitTransientCommandBuffer(cmd);
      vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
    }

    T8_LOG_INFO("[Vulkan] CreateFloatTexture: %dx%d RGBA32F", w, h);
    return tex;
  }

  Texture* VulkanDevice::CreateFloatCubeMap(int size, int mipCount, const float* data) {
    if (size <= 0 || mipCount <= 0)
      return nullptr;

    auto* driver = static_cast<VulkanDriver*>(g_pBaseDriver);
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    VulkanTexture* tex = new VulkanTexture;
    tex->m_format = VK_FORMAT_R32G32B32A32_SFLOAT;
    tex->x = size;
    tex->y = size;
    tex->mipmaps = mipCount;
    tex->m_channels = 4;
    tex->props = TextBasicFormat::CH_RGBA;
    tex->cil_props = CIL_CUBE_MAP;
    tex->params = TextBasicParams::CLAMP_TO_EDGE | TextBasicParams::MIPMAPS;

    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = tex->m_format;
    imgCI.extent = { static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1 };
    imgCI.mipLevels = static_cast<uint32_t>(mipCount);
    imgCI.arrayLayers = 6;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator, &imgCI, &allocCI, &tex->m_image, &tex->m_allocation, nullptr) != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateFloatCubeMap: vmaCreateImage failed");
      delete tex; return nullptr;
    }

    size_t floatCount = 0;
    int mipSizeForCount = size;
    for (int mip = 0; mip < mipCount; ++mip) {
      floatCount += size_t(mipSizeForCount) * size_t(mipSizeForCount) * 4u * 6u;
      mipSizeForCount >>= 1; if (mipSizeForCount < 1) mipSizeForCount = 1;
    }
    std::vector<float> zeroData;
    const float* sourceFloats = data;
    if (!sourceFloats) {
      zeroData.assign(floatCount, 0.0f);
      sourceFloats = zeroData.data();
    }

    VkDeviceSize totalSize = static_cast<VkDeviceSize>(floatCount * sizeof(float));
    VkBuffer stagingBuffer; VmaAllocation stagingAlloc;
    VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = totalSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo stagingAllocCI = {};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo stagingAllocInfo;
    if (vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo) != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateFloatCubeMap: staging buffer failed");
      delete tex; return nullptr;
    }
    memcpy(stagingAllocInfo.pMappedData, sourceFloats, static_cast<size_t>(totalSize));

    VkCommandBuffer cmd = driver->GetTransientCommandBuffer();
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = tex->m_image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(mipCount), 0, 6 };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    std::vector<VkBufferImageCopy> regions(6 * mipCount);
    VkDeviceSize sourceOffset = 0;
    for (uint32_t face = 0; face < 6; ++face) {
      uint32_t mipSize = static_cast<uint32_t>(size);
      for (uint32_t mip = 0; mip < static_cast<uint32_t>(mipCount); ++mip) {
        uint32_t regionIndex = face * static_cast<uint32_t>(mipCount) + mip;
        regions[regionIndex] = {};
        regions[regionIndex].bufferOffset = sourceOffset;
        regions[regionIndex].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, face, 1 };
        regions[regionIndex].imageExtent = { mipSize, mipSize, 1 };
        sourceOffset += VkDeviceSize(mipSize) * VkDeviceSize(mipSize) * 16;
        mipSize >>= 1; if (mipSize < 1) mipSize = 1;
      }
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer, tex->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    driver->SubmitTransientCommandBuffer(cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivCI.image = tex->m_image;
    ivCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    ivCI.format = tex->m_format;
    ivCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(mipCount), 0, 6 };
    if (vkCreateImageView(device, &ivCI, nullptr, &tex->m_imageView) != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateFloatCubeMap: vkCreateImageView failed");
      delete tex; return nullptr;
    }

    tex->SetTextureParams();
    T8_LOG_INFO("[Vulkan] CreateFloatCubeMap: %dx%d mips=%d RGBA32F", size, size, mipCount);
    return tex;
  }

  BaseRT* VulkanDevice::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips) {
    VulkanRT* rt = new VulkanRT;
    if (rt->LoadRT(nrt, cf, df, w, h, genMips)) return rt;
    delete rt;
    return nullptr;
  }

} // namespace t850

#endif // OS_WINDOWS
