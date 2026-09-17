/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanTexture.h: Texture
*********************************************************/

#ifndef T800_VULKANTEXTURE_H
#define T800_VULKANTEXTURE_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX)

#if defined(OS_WINDOWS)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(OS_ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>
#endif

#include <string>
#include <cstdint>
#include <utility>
#include <vector>

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
    void DestroySamplers(VkDevice device);
    void SetTextureParams() override;
    void GetFormatBpp(unsigned int& props, unsigned int& format, unsigned int& bpp) override;
    void Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void SetVS(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void SetSampler(const DeviceContext& deviceContext, unsigned int slot = 0) override;
    void UpdateFloatData(const DeviceContext& deviceContext, int w, int h, const float* data) override;

    VkImage         m_image = VK_NULL_HANDLE;
    VmaAllocation   m_allocation = VK_NULL_HANDLE;
    VkImageView     m_imageView = VK_NULL_HANDLE;
    VkSampler       m_sampler = VK_NULL_HANDLE;
    VkFormat        m_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageLayout GetLayout() const { return m_externalLayout ? *m_externalLayout : m_layout; }
    void SetLayout(VkImageLayout layout) { if (m_externalLayout) *m_externalLayout = layout; else m_layout = layout; }
    void SetExternalLayout(VkImageLayout* layout) { m_externalLayout = layout; }
    float           m_samplerMaxAnisotropy = 1.0f;
    bool            m_isFloatTex = false;  // true for CreateFloatTexture textures
  private:
    VkImageLayout m_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageLayout* m_externalLayout = nullptr;
    std::vector<std::pair<uint64_t, VkSampler>> m_samplerVariants;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANTEXTURE_H
