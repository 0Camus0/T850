/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanUtils.h: Shared helper functions for Vulkan backend
*********************************************************/

#ifndef T800_VULKANUTILS_H
#define T800_VULKANUTILS_H

#include <Config.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

namespace t850 {

  // Forward declarations
  class VulkanDriver;

  // Get the global VulkanDriver instance
  VulkanDriver* GetVkDriver();

  // Transition image layout with appropriate pipeline barriers
  void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkImageAspectFlags aspectMask);

  // ──────────────────────────────────────────────────────
  //  VulkanRTFunctions — lazily loaded KHR ray tracing
  //  function pointers.  All are null until
  //  VulkanDriver::CreateDevice() loads them.
  // ──────────────────────────────────────────────────────
  struct VulkanRTFunctions {
    // Acceleration structure
    PFN_vkCreateAccelerationStructureKHR          vkCreateAccelerationStructureKHR          = nullptr;
    PFN_vkDestroyAccelerationStructureKHR         vkDestroyAccelerationStructureKHR         = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR   vkGetAccelerationStructureBuildSizesKHR   = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR       vkCmdBuildAccelerationStructuresKHR       = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    // Ray tracing pipeline
    PFN_vkCreateRayTracingPipelinesKHR            vkCreateRayTracingPipelinesKHR            = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR      vkGetRayTracingShaderGroupHandlesKHR      = nullptr;
    PFN_vkCmdTraceRaysKHR                         vkCmdTraceRaysKHR                         = nullptr;

    // Load all function pointers from a logical device.
    // Returns false if any required pointer could not be resolved.
    bool Load(VkDevice device);

    // Returns true when all required pointers are non-null.
    bool IsValid() const;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANUTILS_H
