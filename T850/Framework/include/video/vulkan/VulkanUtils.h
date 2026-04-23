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

namespace t800 {

  // Forward declarations
  class VulkanDriver;

  // Get the global VulkanDriver instance
  VulkanDriver* GetVkDriver();

  // Transition image layout with appropriate pipeline barriers
  void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkImageAspectFlags aspectMask);

} // namespace t800

#endif // OS_WINDOWS
#endif // T800_VULKANUTILS_H
