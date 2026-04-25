/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanDevice.h: Device
*********************************************************/

#ifndef T800_VULKANDEVICE_H
#define T800_VULKANDEVICE_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <string>

namespace t800 {

  // Forward declaration
  class VulkanDriver;

  // ══════════════════════════════════════════════════════
  //  Vulkan Device
  // ══════════════════════════════════════════════════════
  class VulkanDevice : public Device {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void release() override;

    Buffer*     CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData = nullptr) override;
    ShaderBase* CreateShader(std::string src_vs, std::string src_fs, ShaderKey key = ShaderKey(),
                             const std::string& vs_name = "", const std::string& fs_name = "") override;
    Texture*    CreateTexture(std::string path) override;
    Texture*    CreateTextureFromMemory(const unsigned char* buff, int w, int h, int channels, std::string name) override;
    Texture*    CreateCubeMap(const unsigned char* buff, int w, int h) override;
    Texture*    CreateFloatTexture(int w, int h, const float* data = nullptr) override;
    BaseRT*     CreateRT(int nrt, int cf, int df, int w, int h, bool genMips = false) override;

    VkDevice GetNativeDevice() const { return m_device; }

  private:
    friend class VulkanDriver;
    VkDevice m_device = VK_NULL_HANDLE;
  };

} // namespace t800

#endif // OS_WINDOWS
#endif // T800_VULKANDEVICE_H
