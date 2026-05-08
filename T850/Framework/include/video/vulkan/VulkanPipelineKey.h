/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanPipelineKey.h: Pipeline cache key structures
*********************************************************/

#ifndef T800_VULKANPIPELINEKEY_H
#define T800_VULKANPIPELINEKEY_H

#include <Config.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID)

#if defined(OS_WINDOWS)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(OS_ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  Vulkan Pipeline cache key
  // ══════════════════════════════════════════════════════
  inline uint64_t VulkanRenderPassKey(VkRenderPass renderPass) {
#if defined(VK_USE_64_BIT_PTR_DEFINES) && (VK_USE_64_BIT_PTR_DEFINES == 1)
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(renderPass));
#else
    return static_cast<uint64_t>(renderPass);
#endif
  }

  struct VulkanPipelineKey {
    uintptr_t shaderPtr;
    uint8_t   blend;
    uint8_t   depth;
    uint8_t   cull;
    uint8_t   numColorAttachments;
    uint8_t   topology;  // VkPrimitiveTopology truncated to 8-bit
    uint32_t  vertexStride;
    uint64_t  renderPass;
    VkFormat  colorFormat;
    VkFormat  depthFormat;
    bool operator==(const VulkanPipelineKey& o) const {
      return shaderPtr == o.shaderPtr && blend == o.blend &&
             depth == o.depth && cull == o.cull &&
             numColorAttachments == o.numColorAttachments &&
             topology == o.topology &&
             vertexStride == o.vertexStride &&
             renderPass == o.renderPass &&
             colorFormat == o.colorFormat && depthFormat == o.depthFormat;
    }
  };

  struct VulkanPipelineKeyHash {
    size_t operator()(const VulkanPipelineKey& k) const {
      size_t h = std::hash<uintptr_t>()(k.shaderPtr);
      h ^= std::hash<uint8_t>()(k.blend)    << 1;
      h ^= std::hash<uint8_t>()(k.depth)    << 2;
      h ^= std::hash<uint8_t>()(k.cull)     << 3;
      h ^= std::hash<uint8_t>()(k.numColorAttachments) << 4;
      h ^= std::hash<uint32_t>()(static_cast<uint32_t>(k.colorFormat)) << 5;
      h ^= std::hash<uint32_t>()(static_cast<uint32_t>(k.depthFormat)) << 6;
      h ^= std::hash<uint8_t>()(k.topology) << 7;
      h ^= std::hash<uint32_t>()(k.vertexStride) << 8;
      h ^= std::hash<uint64_t>()(k.renderPass) << 9;
      return h;
    }
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANPIPELINEKEY_H
