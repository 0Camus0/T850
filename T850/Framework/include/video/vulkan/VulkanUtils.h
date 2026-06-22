/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanUtils.h: Shared helper functions for Vulkan backend
*********************************************************/

#ifndef T800_VULKANUTILS_H
#define T800_VULKANUTILS_H

#include <Config.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX)

#if defined(OS_WINDOWS)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(OS_ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
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

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANUTILS_H
