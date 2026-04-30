/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanRT.h: Render Target
*********************************************************/

#ifndef T800_VULKANRT_H
#define T800_VULKANRT_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <vector>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  Vulkan Render Target
  // ══════════════════════════════════════════════════════
  class VulkanRT : public BaseRT {
  public:
    bool LoadAPIRT() override;
    void DestroyAPIRT() override;
    void Set(const DeviceContext& context) override;
    void SetLoad(const DeviceContext& context) override;
    void ChangeCubeDepthTexture(int i) override;

    VkRenderPass    m_renderPass = VK_NULL_HANDLE;
    VkRenderPass    m_renderPassLoad = VK_NULL_HANDLE;
    VkFramebuffer   m_framebuffer = VK_NULL_HANDLE;

  private:
    void SetInternal(const DeviceContext& context, bool preserve);

  public:

    // Color attachments
    std::vector<VkImage>        vColorImages;
    std::vector<VmaAllocation>  vColorAllocations;
    std::vector<VkImageView>    vColorImageViews;
    std::vector<VkImageLayout>  vColorLayouts;
    VkFormat                    m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    std::vector<VkFormat>       m_colorFormats;

    // Depth attachment
    VkImage         m_depthImage = VK_NULL_HANDLE;
    VmaAllocation   m_depthAllocation = VK_NULL_HANDLE;
    VkImageView     m_depthImageView = VK_NULL_HANDLE;
    VkFormat        m_depthFormat = VK_FORMAT_D32_SFLOAT;

    bool            m_isCubeDepth = false;
    VkImageView     m_cubeFaceViews[6] = {};
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANRT_H
