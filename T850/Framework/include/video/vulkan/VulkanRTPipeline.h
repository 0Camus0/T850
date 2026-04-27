/*********************************************************
 * T850 Engine — Vulkan Ray Tracing
 *
 * VulkanRTPipeline.h: Vulkan RT pipeline + SBT
 *********************************************************/

#ifndef T800_VULKANRTPIPELINE_H
#define T800_VULKANRTPIPELINE_H

#include <Config.h>
#include <video/RTPipeline.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <cstdint>
#include <string>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  VulkanRTPipeline — vkCreateRayTracingPipelinesKHR + SBT
  // ══════════════════════════════════════════════════════
  class VulkanRTPipeline : public RTPipeline {
  public:
    bool Create(const char* raygenSrc, const char* missSrc, const char* closestHitSrc);
    void Destroy() override;

    // Shader binding table regions (consumed by vkCmdTraceRaysKHR).
    VkStridedDeviceAddressRegionKHR raygenRegion   = {};
    VkStridedDeviceAddressRegionKHR missRegion     = {};
    VkStridedDeviceAddressRegionKHR hitGroupRegion = {};
    VkStridedDeviceAddressRegionKHR callableRegion = {};

    VkPipeline       pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
    bool valid = false;

  private:
    VkBuffer      m_sbtBuffer = VK_NULL_HANDLE;
    VmaAllocation m_sbtAlloc  = VK_NULL_HANDLE;

    bool BuildDescriptorSetLayout();
    bool BuildPipelineLayout();
    bool BuildSBT(uint32_t handleSize, uint32_t handleAlignment);

    VkShaderModule CompileRTShader(const char* hlslPath, const char* entryPoint,
                                   const char* targetProfile);
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANRTPIPELINE_H
