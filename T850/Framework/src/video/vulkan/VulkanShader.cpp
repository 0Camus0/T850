#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanShader.cpp: Shader implementation
 *********************************************************/

#include <video/vulkan/VulkanShader.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS)

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <utils/Log.h>
#include <utils/SPIRVReflection.h>
#include <debug/RenderTrace.h>

namespace t850 {

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

  static bool CompileHLSLToSPIRV(const std::string& source, EShLanguage stage,
                                   std::vector<uint32_t>& spirv, const std::string& debugName) {
    if (!s_glslangInitialized) {
      glslang::InitializeProcess();
      s_glslangInitialized = true;
    }

    const char* src = source.c_str();
    const char* entryPoint = (stage == EShLangVertex) ? "VS" : "FS";
    EShMessages messages = (EShMessages)(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);

    glslang::TShader shader(stage);
    shader.setStrings(&src, 1);
    shader.setEntryPoint(entryPoint);
    shader.setSourceEntryPoint(entryPoint);
    shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);
    shader.setTextureSamplerTransformMode(EShTexSampTransUpgradeTextureRemoveSampler);

    if (!shader.parse(GetDefaultResources(), 100, false, messages)) {
      T8_LOG_ERROR("[Vulkan] Shader parse failed (%s): %s", debugName.c_str(), shader.getInfoLog());
      return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
      T8_LOG_ERROR("[Vulkan] Program link failed (%s): %s", debugName.c_str(), program.getInfoLog());
      return false;
    }

    std::vector<unsigned int> generated;
    glslang::GlslangToSpv(*program.getIntermediate(stage), generated);
    spirv.assign(generated.begin(), generated.end());
    return true;
  }

  // ══════════════════════════════════════════════════════
  //  VulkanShader
  // ══════════════════════════════════════════════════════

  bool VulkanShader::CreateShaderAPI(std::string src_vs, std::string src_fs,
                                      const std::string& vs_name, const std::string& fs_name) {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    cbvBinding = -1;
    std::fill(cbvBindings, cbvBindings + VulkanShader::kMaxCBufferSlots, -1);
    std::fill(srvBindings, srvBindings + VulkanShader::kMaxTextureSlots, -1);
    std::fill(srvIsCubemap, srvIsCubemap + VulkanShader::kMaxTextureSlots, false);

    // Compile vertex shader (HLSL → SPIR-V)
    std::vector<uint32_t> vsSPIRV;
    if (!CompileHLSLToSPIRV(src_vs, EShLangVertex, vsSPIRV, vs_name.empty() ? "VS" : vs_name)) {
      return false;
    }
    // Patch SPIR-V: shift UBO bindings to avoid collision with textures at binding 0+
    SPIRVReflection::ShiftUBOBindings(vsSPIRV.data(), vsSPIRV.size(), VulkanShader::kMaxTextureSlots);

    m_vertModule = CreateShaderModule(device, vsSPIRV.data(), vsSPIRV.size() * sizeof(uint32_t));
    if (!m_vertModule) return false;

    // Compile fragment shader (HLSL → SPIR-V)
    std::vector<uint32_t> fsSPIRV;
    if (!CompileHLSLToSPIRV(src_fs, EShLangFragment, fsSPIRV, fs_name.empty() ? "FS" : fs_name)) {
      return false;
    }
    SPIRVReflection::ShiftUBOBindings(fsSPIRV.data(), fsSPIRV.size(), VulkanShader::kMaxTextureSlots);
    m_fragModule = CreateShaderModule(device, fsSPIRV.data(), fsSPIRV.size() * sizeof(uint32_t));
    if (!m_fragModule) return false;

    // ── SPIR-V Reflection ──
    SPIRVReflection vsRefl, fsRefl;
    vsRefl.Parse(vsSPIRV.data(), vsSPIRV.size());
    fsRefl.Parse(fsSPIRV.data(), fsSPIRV.size());

    // Build descriptor set layout from reflected bindings
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindingMap;

    auto recordCBVBinding = [&](uint32_t binding) {
      int logicalSlot = (int)binding;
      if (logicalSlot >= VulkanShader::kMaxTextureSlots) logicalSlot -= VulkanShader::kMaxTextureSlots;
      if (logicalSlot >= 0 && logicalSlot < VulkanShader::kMaxCBufferSlots) {
        cbvBindings[logicalSlot] = (int)binding;
      }
      if (logicalSlot == 0 || cbvBinding < 0) cbvBinding = (int)binding;
    };

    // VS uniform buffers
    for (auto& ub : vsRefl.uniformBuffers) {
      auto& b = bindingMap[ub.binding];
      b.binding = ub.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
      recordCBVBinding(ub.binding);
    }
    // FS uniform buffers
    for (auto& ub : fsRefl.uniformBuffers) {
      auto& b = bindingMap[ub.binding];
      b.binding = ub.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
      recordCBVBinding(ub.binding);
    }
    // FS sampled images (textures) — derive engine slot from binding (undo +1 texture shift)
    for (int idx = 0; idx < (int)fsRefl.sampledImages.size(); idx++) {
      auto& si = fsRefl.sampledImages[idx];
      auto& b = bindingMap[si.binding];
      b.binding = si.binding;
      b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      b.descriptorCount = 1;
      b.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
      int engineSlot = (int)si.binding;
      if (engineSlot >= 0 && engineSlot < VulkanShader::kMaxTextureSlots) {
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
      if (engineSlot >= 0 && engineSlot < VulkanShader::kMaxTextureSlots) {
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

#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      // Register the input layout the shader actually expects, indexed by
      // SPIR-V location and named after the source HLSL semantic where
      // available. The shader hasn't been registered yet (BaseDriver does
      // that after T8Device->CreateShader returns).
      std::vector<TraceShaderAttr> attrs;
      attrs.reserve(m_vertexAttributes.size());
      const size_t reflCount = vsRefl.stageInputs.size();
      for (size_t i = 0; i < m_vertexAttributes.size(); ++i) {
        const auto& va = m_vertexAttributes[i];
        TraceShaderAttr a;
        a.location   = (int)va.location;
        a.input_slot = (int)va.binding;
        a.offset     = va.offset;
        if (i < reflCount) a.semantic = vsRefl.stageInputs[i].name;
        switch (va.format) {
          case VK_FORMAT_R32_SFLOAT:           a.format = "VK_FORMAT_R32_SFLOAT";           a.size_bytes = 4;  break;
          case VK_FORMAT_R32G32_SFLOAT:        a.format = "VK_FORMAT_R32G32_SFLOAT";        a.size_bytes = 8;  break;
          case VK_FORMAT_R32G32B32_SFLOAT:     a.format = "VK_FORMAT_R32G32B32_SFLOAT";     a.size_bytes = 12; break;
          case VK_FORMAT_R32G32B32A32_SFLOAT:  a.format = "VK_FORMAT_R32G32B32A32_SFLOAT";  a.size_bytes = 16; break;
          default:                             a.format = "VK_FORMAT_" + std::to_string((int)va.format); break;
        }
        attrs.push_back(std::move(a));
      }
      g_renderTracer->RegisterShaderInputsForPtr(this, vertexStride, std::move(attrs));
    }
#endif

    T8_LOG_INFO("[Vulkan] Shader '%s'/'%s': %zu bindings (cbv=%d), %zu inputs (stride=%d)",
                vs_name.c_str(), fs_name.c_str(),
                bindings.size(), cbvBinding,
                m_vertexAttributes.size(), vertexStride);

#ifdef T8_DUMP_SHADER_REFLECTION
    T8_LOG_INFO("[VK_REFL] === key=0x%016llX VS='%s' FS='%s' ===", static_cast<unsigned long long>(key.bits), vs_name.c_str(), fs_name.c_str());
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
    T8_LOG_INFO("[VK_REFL] srvBindings: [%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]",
                srvBindings[0], srvBindings[1], srvBindings[2], srvBindings[3],
          srvBindings[4], srvBindings[5], srvBindings[6], srvBindings[7],
          srvBindings[8], srvBindings[9], srvBindings[10], srvBindings[11],
          srvBindings[12], srvBindings[13], srvBindings[14], srvBindings[15]);
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
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int shId = g_renderTracer->LookupShaderId(this);
      g_renderTracer->EvBindShader(shId, key.bits);
      // Surface the pipeline pointer so two API traces can be diffed for
      // pipeline equality at the same draw position.
      g_renderTracer->EvBindPSO((int)(uintptr_t)pipeline);
    }
#endif
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

} // namespace t850

#endif // OS_WINDOWS
