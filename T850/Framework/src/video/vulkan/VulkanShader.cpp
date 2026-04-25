#include "pch.h"
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanShader.cpp: Shader implementation
 *********************************************************/

#include <video/vulkan/VulkanShader.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS)

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <utils/Log.h>
#include <utils/SPIRVReflection.h>

namespace t800 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  Helpers (file-local)
  // ══════════════════════════════════════════════════════

  static bool s_glslangInitialized = false;

  static VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t codeSize) {
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = codeSize;
    ci.pCode = code;
    VkShaderModule mod = VK_NULL_HANDLE;
    VkResult res = vkCreateShaderModule(device, &ci, nullptr, &mod);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateShaderModule failed res=%d", res);
      return VK_NULL_HANDLE;
    }
    return mod;
  }

  static bool CompileHLSLToSPIRV(const std::string& source, glslang_stage_t stage,
                                   std::vector<uint32_t>& spirv, const std::string& debugName) {
    if (!s_glslangInitialized) {
      glslang_initialize_process();
      s_glslangInitialized = true;
    }

    const char* src = source.c_str();
    glslang_input_t input = {};
    input.language = GLSLANG_SOURCE_HLSL;
    input.stage = stage;
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_0;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_0;
    input.code = src;
    input.default_version = 100;
    input.default_profile = GLSLANG_NO_PROFILE;
    input.force_default_version_and_profile = false;
    input.forward_compatible = false;
    input.messages = (glslang_messages_t)(GLSLANG_MSG_DEFAULT_BIT | GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
    input.resource = glslang_default_resource();

    glslang_shader_t* shader = glslang_shader_create(&input);
    glslang_shader_set_options(shader, GLSLANG_SHADER_AUTO_MAP_BINDINGS | GLSLANG_SHADER_AUTO_MAP_LOCATIONS);
    // Set entry point for HLSL
    const char* entryPoint = (stage == GLSLANG_STAGE_VERTEX) ? "VS" : "FS";
    glslang_shader_set_entry_point(shader, entryPoint);
    // Shift UBO binding up to avoid collision with textures at binding 0-7
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_UBO, 16);
    // Shift texture/sampler bindings by 1 so register(t0)→binding 1 (binding 0 = UBO)
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_TEXTURE, 1);
    glslang_shader_shift_binding(shader, GLSLANG_RESOURCE_TYPE_SAMPLER, 1);
    if (!glslang_shader_preprocess(shader, &input)) {
      T8_LOG_ERROR("[Vulkan] Shader preprocess failed (%s): %s", debugName.c_str(), glslang_shader_get_info_log(shader));
      glslang_shader_delete(shader);
      return false;
    }
    if (!glslang_shader_parse(shader, &input)) {
      T8_LOG_ERROR("[Vulkan] Shader parse failed (%s): %s", debugName.c_str(), glslang_shader_get_info_log(shader));
      glslang_shader_delete(shader);
      return false;
    }

    glslang_program_t* program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
      T8_LOG_ERROR("[Vulkan] Program link failed (%s): %s", debugName.c_str(), glslang_program_get_info_log(program));
      glslang_program_delete(program);
      glslang_shader_delete(shader);
      return false;
    }

    glslang_program_SPIRV_generate(program, stage);

    size_t spirvSize = glslang_program_SPIRV_get_size(program);
    spirv.resize(spirvSize);
    glslang_program_SPIRV_get(program, spirv.data());

    const char* spirvMessages = glslang_program_SPIRV_get_messages(program);
    if (spirvMessages && spirvMessages[0]) {
      T8_LOG_INFO("[Vulkan] SPIR-V messages (%s): %s", debugName.c_str(), spirvMessages);
    }

    glslang_program_delete(program);
    glslang_shader_delete(shader);
    return true;
  }

  // ══════════════════════════════════════════════════════
  //  VulkanShader
  // ══════════════════════════════════════════════════════

  bool VulkanShader::CreateShaderAPI(std::string src_vs, std::string src_fs,
                                      const std::string& vs_name, const std::string& fs_name) {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();

    // Compile vertex shader (HLSL → SPIR-V)
    std::vector<uint32_t> vsSPIRV;
    if (!CompileHLSLToSPIRV(src_vs, GLSLANG_STAGE_VERTEX, vsSPIRV, vs_name.empty() ? "VS" : vs_name)) {
      return false;
    }
    // Patch SPIR-V: shift UBO bindings to avoid collision with textures at binding 0+
    SPIRVReflection::ShiftUBOBindings(vsSPIRV.data(), vsSPIRV.size(), 16);

    m_vertModule = CreateShaderModule(device, vsSPIRV.data(), vsSPIRV.size() * sizeof(uint32_t));
    if (!m_vertModule) return false;

    // Compile fragment shader (HLSL → SPIR-V)
    std::vector<uint32_t> fsSPIRV;
    if (!CompileHLSLToSPIRV(src_fs, GLSLANG_STAGE_FRAGMENT, fsSPIRV, fs_name.empty() ? "FS" : fs_name)) {
      return false;
    }
    SPIRVReflection::ShiftUBOBindings(fsSPIRV.data(), fsSPIRV.size(), 16);
    m_fragModule = CreateShaderModule(device, fsSPIRV.data(), fsSPIRV.size() * sizeof(uint32_t));
    if (!m_fragModule) return false;

    // ── SPIR-V Reflection ──
    SPIRVReflection vsRefl, fsRefl;
    vsRefl.Parse(vsSPIRV.data(), vsSPIRV.size());
    fsRefl.Parse(fsSPIRV.data(), fsSPIRV.size());

    // Build descriptor set layout from reflected bindings
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindingMap;

    // VS uniform buffers
    for (auto& ub : vsRefl.uniformBuffers) {
      auto& b = bindingMap[ub.binding];
      b.binding = ub.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
      cbvBinding = (int)ub.binding;
    }
    // FS uniform buffers
    for (auto& ub : fsRefl.uniformBuffers) {
      auto& b = bindingMap[ub.binding];
      b.binding = ub.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
      if (cbvBinding < 0) cbvBinding = (int)ub.binding;
    }
    // FS sampled images (textures) — derive engine slot from binding (undo +1 texture shift)
    for (int idx = 0; idx < (int)fsRefl.sampledImages.size(); idx++) {
      auto& si = fsRefl.sampledImages[idx];
      auto& b = bindingMap[si.binding];
      b.binding = si.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
      // Texture binding N maps to engine slot N (register(tN) → binding N)
      int engineSlot = (int)si.binding;
      if (engineSlot >= 0 && engineSlot < 8) {
        srvBindings[engineSlot] = (int)si.binding;
        srvIsCubemap[engineSlot] = si.isCubemap;
      }
    }
    // VS sampled images (if any — e.g., bone texture)
    for (auto& si : vsRefl.sampledImages) {
      auto& b = bindingMap[si.binding];
      b.binding = si.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
      int engineSlot = (int)si.binding;
      if (engineSlot >= 0 && engineSlot < 8) {
        srvBindings[engineSlot] = (int)si.binding;
      }
    }

    // Sort bindings and create layout
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (auto& [idx, b] : bindingMap) bindings.push_back(b);
    std::sort(bindings.begin(), bindings.end(),
      [](const auto& a, const auto& b) { return a.binding < b.binding; });

    // Track the max binding for descriptor writes
    maxBinding = 0;
    for (auto& b : bindings) {
      if (b.binding > (uint32_t)maxBinding) maxBinding = (int)b.binding;
    }

    VkDescriptorSetLayoutCreateInfo dslCI = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslCI.bindingCount = (uint32_t)bindings.size();
    dslCI.pBindings = bindings.data();
    VkResult res = vkCreateDescriptorSetLayout(device, &dslCI, nullptr, &m_descriptorSetLayout);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateDescriptorSetLayout failed res=%d", res);
      return false;
    }

    // Pipeline layout
    VkPipelineLayoutCreateInfo plCI = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plCI.setLayoutCount = 1;
    plCI.pSetLayouts = &m_descriptorSetLayout;
    res = vkCreatePipelineLayout(device, &plCI, nullptr, &m_pipelineLayout);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreatePipelineLayout failed res=%d", res);
      return false;
    }

    // ── Vertex input from VS reflection ──
    m_vertexAttributes.clear();
    uint32_t offset = 0;
    for (auto& inp : vsRefl.stageInputs) {
      VkVertexInputAttributeDescription attr = {};
      attr.location = inp.location;
      attr.binding = 0;
      attr.offset = offset;
      switch (inp.vecSize) {
        case 1: attr.format = VK_FORMAT_R32_SFLOAT;          offset += 4;  break;
        case 2: attr.format = VK_FORMAT_R32G32_SFLOAT;       offset += 8;  break;
        case 3: attr.format = VK_FORMAT_R32G32B32_SFLOAT;    offset += 12; break;
        default: attr.format = VK_FORMAT_R32G32B32A32_SFLOAT; offset += 16; break;
      }
      m_vertexAttributes.push_back(attr);
    }
    vertexStride = offset;
    m_vertexBinding.binding = 0;
    m_vertexBinding.stride = vertexStride;
    m_vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    T8_LOG_INFO("[Vulkan] Shader '%s'/'%s': %zu bindings (cbv=%d), %zu inputs (stride=%d)",
                vs_name.c_str(), fs_name.c_str(),
                bindings.size(), cbvBinding,
                m_vertexAttributes.size(), vertexStride);

#ifdef T8_DUMP_SHADER_REFLECTION
    T8_LOG_INFO("[VK_REFL] === key=0x%08X VS='%s' FS='%s' ===", key.bits, vs_name.c_str(), fs_name.c_str());
    T8_LOG_INFO("[VK_REFL] VS Inputs (%zu):", vsRefl.stageInputs.size());
    for (size_t idx = 0; idx < vsRefl.stageInputs.size(); idx++) {
      auto& inp = vsRefl.stageInputs[idx];
      T8_LOG_INFO("[VK_REFL]   [%zu] '%s'  location=%u  components=%u",
                  idx, inp.name.c_str(), inp.location, inp.vecSize);
    }
    T8_LOG_INFO("[VK_REFL] VS stride=%d", vertexStride);
    T8_LOG_INFO("[VK_REFL] VS UBOs (%zu):", vsRefl.uniformBuffers.size());
    for (auto& ub : vsRefl.uniformBuffers)
      T8_LOG_INFO("[VK_REFL]   '%s' set=%u binding=%u", ub.name.c_str(), ub.set, ub.binding);
    T8_LOG_INFO("[VK_REFL] VS Textures (%zu):", vsRefl.sampledImages.size());
    for (auto& si : vsRefl.sampledImages)
      T8_LOG_INFO("[VK_REFL]   '%s' set=%u binding=%u", si.name.c_str(), si.set, si.binding);
    T8_LOG_INFO("[VK_REFL] FS UBOs (%zu):", fsRefl.uniformBuffers.size());
    for (auto& ub : fsRefl.uniformBuffers)
      T8_LOG_INFO("[VK_REFL]   '%s' set=%u binding=%u", ub.name.c_str(), ub.set, ub.binding);
    T8_LOG_INFO("[VK_REFL] FS Textures (%zu):", fsRefl.sampledImages.size());
    for (auto& si : fsRefl.sampledImages)
      T8_LOG_INFO("[VK_REFL]   '%s' set=%u binding=%u", si.name.c_str(), si.set, si.binding);
    T8_LOG_INFO("[VK_REFL] Descriptor layout bindings:");
    for (auto& b : bindings)
      T8_LOG_INFO("[VK_REFL]   binding=%u type=%d stages=0x%X",
                  b.binding, b.descriptorType, b.stageFlags);
    T8_LOG_INFO("[VK_REFL] srvBindings: [%d,%d,%d,%d,%d,%d,%d,%d]",
                srvBindings[0], srvBindings[1], srvBindings[2], srvBindings[3],
                srvBindings[4], srvBindings[5], srvBindings[6], srvBindings[7]);
#endif
    return true;
  }

  void VulkanShader::Set(const DeviceContext& deviceContext) {
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = this;
    auto* driver = GetVkDriver();

    // Determine current render target format for pipeline creation
    uint8_t numColorAttachments = 1;
    VkFormat colorFormat = driver->m_swapChainFormat;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    if (driver->CurrentRT >= 0 && driver->CurrentRT < (int)driver->RTs.size()) {
      VulkanRT* rt = static_cast<VulkanRT*>(driver->RTs[driver->CurrentRT]);
      numColorAttachments = (uint8_t)rt->number_RT;
      colorFormat = rt->m_colorFormat;
      depthFormat = (rt->depth_format != BaseRT::NOTHING) ? rt->m_depthFormat : VK_FORMAT_UNDEFINED;
    }

    VkPipeline pipeline = driver->GetOrCreatePipeline(this, numColorAttachments, colorFormat, depthFormat);
    if (!pipeline) return;

    VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&deviceContext)->GetCommandBuffer();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  }

  void VulkanShader::DestroyAPIShader() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    if (m_pipelineLayout)       { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
    if (m_descriptorSetLayout)  { vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
    if (m_vertModule)           { vkDestroyShaderModule(device, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE; }
    if (m_fragModule)           { vkDestroyShaderModule(device, m_fragModule, nullptr); m_fragModule = VK_NULL_HANDLE; }
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — PSO Cache
  // ══════════════════════════════════════════════════════

} // namespace t800

#endif // OS_WINDOWS
