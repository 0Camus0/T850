#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanTexture.cpp: Texture implementation
 *********************************************************/

#include <video/vulkan/VulkanTexture.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanShader.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID)

#include <utils/Log.h>
#include <debug/RenderTrace.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  VulkanTexture
  // ══════════════════════════════════════════════════════

  namespace {
    uint32_t CalculateFullMipCount(uint32_t width, uint32_t height) {
      uint32_t levels = 1;
      while (width > 1 || height > 1) {
        width = width > 1 ? (width >> 1) : 1;
        height = height > 1 ? (height >> 1) : 1;
        ++levels;
      }
      return levels;
    }

    void GenerateMipChain8(const unsigned char* src, uint32_t width, uint32_t height,
                           uint32_t faceCount, uint32_t bytesPerPixel,
                           std::vector<unsigned char>& outData) {
      const uint32_t mipCount = CalculateFullMipCount(width, height);
      size_t totalBytes = 0;
      for (uint32_t face = 0; face < faceCount; ++face) {
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
          totalBytes += static_cast<size_t>(mipWidth) * mipHeight * bytesPerPixel;
          mipWidth = mipWidth > 1 ? (mipWidth >> 1) : 1;
          mipHeight = mipHeight > 1 ? (mipHeight >> 1) : 1;
        }
      }

      outData.resize(totalBytes);
      size_t dstOffset = 0;
      const size_t baseFaceBytes = static_cast<size_t>(width) * height * bytesPerPixel;

      for (uint32_t face = 0; face < faceCount; ++face) {
        uint32_t prevWidth = width;
        uint32_t prevHeight = height;
        const unsigned char* prev = src + static_cast<size_t>(face) * baseFaceBytes;
        size_t prevBytes = baseFaceBytes;

        memcpy(outData.data() + dstOffset, prev, prevBytes);
        size_t prevOffset = dstOffset;
        dstOffset += prevBytes;

        for (uint32_t mip = 1; mip < mipCount; ++mip) {
          uint32_t mipWidth = prevWidth > 1 ? (prevWidth >> 1) : 1;
          uint32_t mipHeight = prevHeight > 1 ? (prevHeight >> 1) : 1;
          unsigned char* dst = outData.data() + dstOffset;
          const unsigned char* srcMip = outData.data() + prevOffset;

          for (uint32_t y = 0; y < mipHeight; ++y) {
            for (uint32_t x = 0; x < mipWidth; ++x) {
              uint32_t sx0 = x * 2;
              uint32_t sy0 = y * 2;
              uint32_t sx1 = (sx0 + 1 < prevWidth) ? sx0 + 1 : sx0;
              uint32_t sy1 = (sy0 + 1 < prevHeight) ? sy0 + 1 : sy0;
              for (uint32_t c = 0; c < bytesPerPixel; ++c) {
                uint32_t accum = 0;
                accum += srcMip[(static_cast<size_t>(sy0) * prevWidth + sx0) * bytesPerPixel + c];
                accum += srcMip[(static_cast<size_t>(sy0) * prevWidth + sx1) * bytesPerPixel + c];
                accum += srcMip[(static_cast<size_t>(sy1) * prevWidth + sx0) * bytesPerPixel + c];
                accum += srcMip[(static_cast<size_t>(sy1) * prevWidth + sx1) * bytesPerPixel + c];
                dst[(static_cast<size_t>(y) * mipWidth + x) * bytesPerPixel + c] =
                  static_cast<unsigned char>((accum + 2) / 4);
              }
            }
          }

          prevOffset = dstOffset;
          prevWidth = mipWidth;
          prevHeight = mipHeight;
          dstOffset += static_cast<size_t>(mipWidth) * mipHeight * bytesPerPixel;
        }
      }
    }

    void Decode565(uint16_t value, unsigned char* rgba) {
      const uint32_t r = (value >> 11) & 31;
      const uint32_t g = (value >> 5) & 63;
      const uint32_t b = value & 31;
      rgba[0] = static_cast<unsigned char>((r * 255 + 15) / 31);
      rgba[1] = static_cast<unsigned char>((g * 255 + 31) / 63);
      rgba[2] = static_cast<unsigned char>((b * 255 + 15) / 31);
      rgba[3] = 255;
    }

    void DecodeDXTColorBlock(const unsigned char* block, unsigned char colors[4][4]) {
      const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
      const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
      Decode565(c0, colors[0]);
      Decode565(c1, colors[1]);

      if (c0 > c1) {
        for (int c = 0; c < 3; ++c) {
          colors[2][c] = static_cast<unsigned char>((2 * colors[0][c] + colors[1][c] + 1) / 3);
          colors[3][c] = static_cast<unsigned char>((colors[0][c] + 2 * colors[1][c] + 1) / 3);
        }
        colors[2][3] = 255;
        colors[3][3] = 255;
      } else {
        for (int c = 0; c < 3; ++c) {
          colors[2][c] = static_cast<unsigned char>((colors[0][c] + colors[1][c] + 1) / 2);
          colors[3][c] = 0;
        }
        colors[2][3] = 255;
        colors[3][3] = 0;
      }
    }

    void DecodeDXT5AlphaBlock(const unsigned char* block, unsigned char alpha[16]) {
      unsigned char palette[8] = {};
      palette[0] = block[0];
      palette[1] = block[1];
      if (palette[0] > palette[1]) {
        palette[2] = static_cast<unsigned char>((6 * palette[0] + 1 * palette[1] + 3) / 7);
        palette[3] = static_cast<unsigned char>((5 * palette[0] + 2 * palette[1] + 3) / 7);
        palette[4] = static_cast<unsigned char>((4 * palette[0] + 3 * palette[1] + 3) / 7);
        palette[5] = static_cast<unsigned char>((3 * palette[0] + 4 * palette[1] + 3) / 7);
        palette[6] = static_cast<unsigned char>((2 * palette[0] + 5 * palette[1] + 3) / 7);
        palette[7] = static_cast<unsigned char>((1 * palette[0] + 6 * palette[1] + 3) / 7);
      } else {
        palette[2] = static_cast<unsigned char>((4 * palette[0] + 1 * palette[1] + 2) / 5);
        palette[3] = static_cast<unsigned char>((3 * palette[0] + 2 * palette[1] + 2) / 5);
        palette[4] = static_cast<unsigned char>((2 * palette[0] + 3 * palette[1] + 2) / 5);
        palette[5] = static_cast<unsigned char>((1 * palette[0] + 4 * palette[1] + 2) / 5);
        palette[6] = 0;
        palette[7] = 255;
      }

      uint64_t bits = 0;
      for (int i = 0; i < 6; ++i) {
        bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
      }
      for (int i = 0; i < 16; ++i) {
        alpha[i] = palette[(bits >> (3 * i)) & 0x7];
      }
    }

    bool DecompressDXTToRGBA(const unsigned char* src, uint32_t width, uint32_t height,
                             uint32_t mipCount, uint32_t faceCount, unsigned int cilProps,
                             std::vector<unsigned char>& outData) {
      if (!src || mipCount == 0 || faceCount == 0) return false;

      const bool isDXT1 = (cilProps & CIL_DXT1) != 0;
      const bool isDXT3 = (cilProps & CIL_DXT3) != 0;
      const bool isDXT5 = (cilProps & CIL_DXT5) != 0;
      const uint32_t blockSize = isDXT1 ? 8u : 16u;
      if (!isDXT1 && !isDXT3 && !isDXT5) return false;

      size_t totalBytes = 0;
      for (uint32_t face = 0; face < faceCount; ++face) {
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
          totalBytes += static_cast<size_t>(mipWidth) * mipHeight * 4;
          mipWidth = mipWidth > 1 ? (mipWidth >> 1) : 1;
          mipHeight = mipHeight > 1 ? (mipHeight >> 1) : 1;
        }
      }
      outData.assign(totalBytes, 0);

      size_t srcOffset = 0;
      size_t dstOffset = 0;
      for (uint32_t face = 0; face < faceCount; ++face) {
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
          const uint32_t blocksX = std::max(1u, (mipWidth + 3u) / 4u);
          const uint32_t blocksY = std::max(1u, (mipHeight + 3u) / 4u);
          unsigned char* dstMip = outData.data() + dstOffset;

          for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
              const unsigned char* block = src + srcOffset + static_cast<size_t>(by * blocksX + bx) * blockSize;
              unsigned char alpha[16];
              std::fill(std::begin(alpha), std::end(alpha), 255);

              const unsigned char* colorBlock = block;
              if (isDXT3) {
                for (int i = 0; i < 16; ++i) {
                  const unsigned char packed = block[i / 2];
                  const unsigned char nibble = (i & 1) ? (packed >> 4) : (packed & 0xF);
                  alpha[i] = static_cast<unsigned char>((nibble << 4) | nibble);
                }
                colorBlock = block + 8;
              } else if (isDXT5) {
                DecodeDXT5AlphaBlock(block, alpha);
                colorBlock = block + 8;
              }

              unsigned char colors[4][4] = {};
              DecodeDXTColorBlock(colorBlock, colors);
              const uint32_t code = colorBlock[4] |
                (static_cast<uint32_t>(colorBlock[5]) << 8) |
                (static_cast<uint32_t>(colorBlock[6]) << 16) |
                (static_cast<uint32_t>(colorBlock[7]) << 24);

              for (uint32_t py = 0; py < 4; ++py) {
                const uint32_t y = by * 4 + py;
                if (y >= mipHeight) continue;
                for (uint32_t px = 0; px < 4; ++px) {
                  const uint32_t x = bx * 4 + px;
                  if (x >= mipWidth) continue;
                  const uint32_t pixel = py * 4 + px;
                  const uint32_t colorIndex = (code >> (2 * pixel)) & 0x3;
                  unsigned char* dst = dstMip + (static_cast<size_t>(y) * mipWidth + x) * 4;
                  dst[0] = colors[colorIndex][0];
                  dst[1] = colors[colorIndex][1];
                  dst[2] = colors[colorIndex][2];
                  dst[3] = isDXT1 ? colors[colorIndex][3] : alpha[pixel];
                }
              }
            }
          }

          srcOffset += static_cast<size_t>(blocksX) * blocksY * blockSize;
          dstOffset += static_cast<size_t>(mipWidth) * mipHeight * 4;
          mipWidth = mipWidth > 1 ? (mipWidth >> 1) : 1;
          mipHeight = mipHeight > 1 ? (mipHeight >> 1) : 1;
        }
      }
      return true;
    }
  }

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

    bool isCube = (cil_props & CIL_CUBE_MAP) != 0;
    uint32_t layerCount = isCube ? 6 : 1;
    bool hasSourceMips = mipmaps > 1;
    uint32_t mipCount = hasSourceMips ? mipmaps : 1;
    std::vector<unsigned char> generatedMips;
    if (!hasSourceMips && !isHalfFloat && uploadBuf) {
      mipCount = CalculateFullMipCount(this->x, this->y);
      if (mipCount > 1) {
        GenerateMipChain8(uploadBuf, this->x, this->y, layerCount,
                          static_cast<uint32_t>(bytesPerPixel), generatedMips);
        uploadBuf = generatedMips.data();
      }
    }
    VkDeviceSize imageSize = (VkDeviceSize)x * y * bytesPerPixel;
    VkDeviceSize totalSize = !generatedMips.empty()
      ? static_cast<VkDeviceSize>(generatedMips.size())
      : ((mipCount > 1 && this->size > 0) ? this->size : imageSize * layerCount);

    // 1. Create VkImage
    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = m_format;
    imgCI.extent = { x, y, 1 };
    imgCI.mipLevels = mipCount;
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
    memcpy(stagingAllocInfo.pMappedData, uploadBuf, static_cast<size_t>(totalSize));

    // 3. Record transient command buffer
    const bool useUploadBatch = driver->IsResourceUploadBatchActive();
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (useUploadBatch) {
      cmd = driver->GetResourceUploadCommandBuffer();
    } else {
      VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      cmdAlloc.commandPool = driver->GetTransientCommandPool();
      cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmdAlloc.commandBufferCount = 1;
      vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);

      VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmd, &beginInfo);
    }
    if (cmd == VK_NULL_HANDLE) {
      vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
      vmaDestroyImage(allocator, m_image, m_allocation);
      m_image = VK_NULL_HANDLE; m_allocation = nullptr;
      return;
    }

    // 3a. Transition UNDEFINED → TRANSFER_DST_OPTIMAL (all layers)
    {
      VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = m_image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = mipCount;
      barrier.subresourceRange.layerCount = layerCount;
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // 3b. Copy staging buffer → image (one region per face/layer)
    std::vector<VkBufferImageCopy> regions(layerCount * mipCount);
    VkDeviceSize sourceOffset = 0;
    for (uint32_t face = 0; face < layerCount; face++) {
      uint32_t mipWidth = x;
      uint32_t mipHeight = y;
      for (uint32_t mip = 0; mip < mipCount; ++mip) {
        uint32_t regionIndex = face * mipCount + mip;
        regions[regionIndex] = {};
        regions[regionIndex].bufferOffset = sourceOffset;
        regions[regionIndex].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[regionIndex].imageSubresource.mipLevel = mip;
        regions[regionIndex].imageSubresource.baseArrayLayer = face;
        regions[regionIndex].imageSubresource.layerCount = 1;
        regions[regionIndex].imageExtent = { mipWidth, mipHeight, 1 };
        sourceOffset += VkDeviceSize(mipWidth) * VkDeviceSize(mipHeight) * VkDeviceSize(bytesPerPixel);
        mipWidth >>= 1; if (mipWidth < 1) mipWidth = 1;
        mipHeight >>= 1; if (mipHeight < 1) mipHeight = 1;
      }
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

    // 3c. Transition TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL (all layers)
    {
      VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = m_image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = mipCount;
      barrier.subresourceRange.layerCount = layerCount;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    if (useUploadBatch) {
      driver->KeepResourceUploadBuffer(stagingBuffer, stagingAlloc);
    } else {
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
    }

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
    ivCI.subresourceRange.levelCount = mipCount;
    ivCI.subresourceRange.baseArrayLayer = 0;
    ivCI.subresourceRange.layerCount = layerCount;

    res = vkCreateImageView(device, &ivCI, nullptr, &m_imageView);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Texture image view creation failed res=%d", res);
      return;
    }

    // 7. Create VkSampler
    this->mipmaps = mipCount;
    if (mipCount > 1)
      params |= MIPMAPS;
    SetTextureParams();

    T8_LOG_INFO("[Vulkan] LoadAPITexture OK (%ux%u ch=%u fmt=%d mips=%u cube=%d)", x, y, m_channels, m_format, mipCount, (int)isCube);
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
    VkPhysicalDeviceFeatures features = {};
    vkGetPhysicalDeviceFeatures(driver->GetPhysicalDevice(), &features);
    VkFormatProperties formatProps = {};
    vkGetPhysicalDeviceFormatProperties(driver->GetPhysicalDevice(), fmt, &formatProps);
    const bool supportsCompressed = features.textureCompressionBC == VK_TRUE &&
      (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    if (!supportsCompressed) {
      std::vector<unsigned char> decompressed;
      if (DecompressDXTToRGBA(buffer, x, y, mipCount, numFaces, cil_props, decompressed)) {
        T8_LOG_INFO("[Vulkan] BC/DXT format %d unsupported; decompressed %ux%u mips=%u faces=%u to RGBA8",
                    fmt, x, y, mipCount, numFaces);
        const unsigned int cubeFlag = cil_props & CIL_CUBE_MAP;
        cil_props = cubeFlag | CIL_RGBA | CIL_RAW;
        props = TextBasicFormat::CH_RGBA;
        m_channels = 4;
        size = static_cast<unsigned int>(decompressed.size());
        LoadAPITexture(T8DeviceContext, decompressed.data());
        return;
      }
      T8_LOG_ERROR("[Vulkan] BC/DXT format %d unsupported and CPU decompression failed (%ux%u)",
                   fmt, x, y);
      return;
    }

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
    memcpy(stagingAllocInfo.pMappedData, buffer, static_cast<size_t>(totalSize));

    // Record copy
    const bool useUploadBatch = driver->IsResourceUploadBatchActive();
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (useUploadBatch) {
      cmd = driver->GetResourceUploadCommandBuffer();
    } else {
      VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      cmdAlloc.commandPool = driver->GetTransientCommandPool();
      cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmdAlloc.commandBufferCount = 1;
      vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);
      VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmd, &beginInfo);
    }
    if (cmd == VK_NULL_HANDLE) {
      vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
      vmaDestroyImage(allocator, m_image, m_allocation);
      m_image = VK_NULL_HANDLE; m_allocation = nullptr;
      return;
    }

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

    if (useUploadBatch) {
      driver->KeepResourceUploadBuffer(stagingBuffer, stagingAlloc);
    } else {
      vkEndCommandBuffer(cmd);
      VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &cmd;
      vkQueueSubmit(driver->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
      vkQueueWaitIdle(driver->GetGraphicsQueue());
      vkFreeCommandBuffers(device, driver->GetTransientCommandPool(), 1, &cmd);
      vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
    }

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

    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (params & TILED)
      addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (params & CLAMP_TO_BORDER)
      addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    VkFilter filter = VK_FILTER_LINEAR;
    if (params & NEAREST_FILTER)
      filter = VK_FILTER_NEAREST;

    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (params & NEAREST_FILTER)
      mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    else if (params & LINEAR_FILTER)
      mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkPhysicalDeviceFeatures features = {};
    vkGetPhysicalDeviceFeatures(driver->GetPhysicalDevice(), &features);
    VkPhysicalDeviceProperties props = {};
    vkGetPhysicalDeviceProperties(driver->GetPhysicalDevice(), &props);
    m_samplerMaxAnisotropy = std::min(16.0f, props.limits.maxSamplerAnisotropy);
    bool useAnisotropy = features.samplerAnisotropy &&
                         !(cil_props & CIL_CUBE_MAP) &&
                         !(params & NEAREST_FILTER) &&
                         !(params & LINEAR_FILTER) &&
                         !(params & CLAMP_TO_BORDER) &&
                         m_samplerMaxAnisotropy > 1.0f;

    VkSamplerCreateInfo samplerCI = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCI.magFilter = filter;
    samplerCI.minFilter = filter;
    samplerCI.addressModeU = addressMode;
    samplerCI.addressModeV = addressMode;
    samplerCI.addressModeW = addressMode;
    samplerCI.anisotropyEnable = useAnisotropy ? VK_TRUE : VK_FALSE;
    samplerCI.maxAnisotropy = useAnisotropy ? m_samplerMaxAnisotropy : 1.0f;
    samplerCI.borderColor = (params & CLAMP_TO_BORDER) ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                                       : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerCI.unnormalizedCoordinates = VK_FALSE;
    samplerCI.compareEnable = VK_FALSE;
    samplerCI.mipmapMode = mipmapMode;
    samplerCI.mipLodBias = 0.0f;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = (params & (NEAREST_FILTER | LINEAR_FILTER)) ? 0.0f : VK_LOD_CLAMP_NONE;

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
    if (slot >= VulkanShader::kMaxTextureSlots) return;
    auto* driver = GetVkDriver();
    driver->m_pendingTextures[slot].imageView = m_imageView;
    driver->m_pendingTextures[slot].sampler = m_sampler;
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      auto& pending = driver->m_pendingTextures[slot];
      pending.tracerTexId = g_renderTracer->LookupTextureId(this);
      m_shaderTextureName = shaderTextureName;
      std::snprintf(pending.tracerName, sizeof(pending.tracerName), "%s", m_shaderTextureName.c_str());
      std::snprintf(pending.tracerStage, sizeof(pending.tracerStage), "%s", "ps");
      // Build a logical sampler signature from the same TextBasicParams bits
      // used to construct m_sampler (see VulkanTexture sampler creation
      // around line 388-432). All 4 backends use this helper, so equivalent
      // samplers across APIs hash to the same id.
      pending.tracerSamplerId =
        g_renderTracer->RegisterSampler(RenderTracer::MakeSamplerSigVulkan(params, m_samplerMaxAnisotropy, (cil_props & CIL_CUBE_MAP) != 0));
      g_renderTracer->EvBindTextureRequest(slot, pending.tracerTexId,
                                           shaderTextureName, "ps");
    }
#endif
    T8_LOG_TRACE("[Vulkan] Texture::Set slot=%u view=%p sampler=%p name=%s",
                slot, (void*)m_imageView, (void*)m_sampler, shaderTextureName.c_str());
  }

  void VulkanTexture::SetVS(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) {
    if (slot >= VulkanShader::kMaxTextureSlots) return;
    auto* driver = GetVkDriver();
    driver->m_pendingTextures[slot].imageView = m_imageView;
    driver->m_pendingTextures[slot].sampler = m_sampler;
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      auto& pending = driver->m_pendingTextures[slot];
      pending.tracerTexId = g_renderTracer->LookupTextureId(this);
      m_shaderTextureName = shaderTextureName;
      std::snprintf(pending.tracerName, sizeof(pending.tracerName), "%s", m_shaderTextureName.c_str());
      std::snprintf(pending.tracerStage, sizeof(pending.tracerStage), "%s", "vs");
      pending.tracerSamplerId =
        g_renderTracer->RegisterSampler(RenderTracer::MakeSamplerSigVulkan(params, m_samplerMaxAnisotropy, (cil_props & CIL_CUBE_MAP) != 0));
      g_renderTracer->EvBindTextureRequest(slot, pending.tracerTexId,
                                           shaderTextureName, "vs");
    }
#endif
    T8_LOG_TRACE("[Vulkan] Texture::SetVS slot=%u view=%p sampler=%p name=%s",
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
    memcpy(stagingAllocInfo.pMappedData, data, static_cast<size_t>(totalSize));

    // Record copy in current frame's command buffer
    VkCommandBuffer cmd = driver->GetCurrentCommandBuffer();

    // End any active render pass — copy commands are invalid inside a render pass
    driver->EndRenderPassIfActive(cmd);

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

} // namespace t850

#endif // OS_WINDOWS
