/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanTexture.h: Texture
*********************************************************/

#ifndef T800_VULKANTEXTURE_H
#define T800_VULKANTEXTURE_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <string>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  Vulkan Texture
  // ══════════════════════════════════════════════════════
  class VulkanTexture : public Texture {
  public:
    VulkanTexture() {}

    void LoadAPITexture(DeviceContext* context, unsigned char* buffer) override;
    void LoadAPITextureCompressed(unsigned char* buffer) override;
    void DestroyAPITexture() override;
    void SetTextureParams() override;
    void GetFormatBpp(unsigned int& props, unsigned int& format, unsigned int& bpp) override;
    void Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void SetSampler(const DeviceContext& deviceContext, unsigned int slot = 0) override;
    void UpdateFloatData(const DeviceContext& deviceContext, int w, int h, const float* data) override;

    VkImage         m_image = VK_NULL_HANDLE;
    VmaAllocation   m_allocation = VK_NULL_HANDLE;
    VkImageView     m_imageView = VK_NULL_HANDLE;
    VkSampler       m_sampler = VK_NULL_HANDLE;
    VkFormat        m_format = VK_FORMAT_R8G8B8A8_UNORM;
    float           m_samplerMaxAnisotropy = 1.0f;
    bool            m_isFloatTex = false;  // true for CreateFloatTexture textures
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANTEXTURE_H
