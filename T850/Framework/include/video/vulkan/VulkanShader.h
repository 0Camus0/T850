/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanShader.h: Shader
*********************************************************/

#ifndef T800_VULKANSHADER_H
#define T800_VULKANSHADER_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  Vulkan Shader
  // ══════════════════════════════════════════════════════
  class VulkanShader : public ShaderBase {
  public:
    static constexpr int kMaxTextureSlots = 32;
    static constexpr int kMaxCBufferSlots = 8;

    bool CreateShaderAPI(std::string src_vs, std::string src_fs,
                         const std::string& vs_name = "", const std::string& fs_name = "") override;
    void Set(const DeviceContext& deviceContext) override;
    void DestroyAPIShader() override;

    VkShaderModule              m_vertModule = VK_NULL_HANDLE;
    VkShaderModule              m_fragModule = VK_NULL_HANDLE;
    VkPipelineLayout            m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout       m_descriptorSetLayout = VK_NULL_HANDLE;

    int vertexStride = 0;
    std::vector<VkVertexInputAttributeDescription> m_vertexAttributes;
    VkVertexInputBindingDescription                m_vertexBinding = {};

    // Descriptor binding indices (resolved from SPIR-V reflection)
    int cbvBinding = -1;
    int cbvBindings[kMaxCBufferSlots] = {}; // logical cbuffer slot -> descriptor binding
    int srvBindings[kMaxTextureSlots] = {}; // slot -> binding index
    bool srvIsCubemap[kMaxTextureSlots] = {};  // true if slot expects a cubemap view
    int maxBinding = 0;  // highest binding number in the layout
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANSHADER_H
