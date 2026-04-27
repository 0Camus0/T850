#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Ray Tracing
 * VulkanRTPipeline.cpp: Ray tracing pipeline + SBT
 *********************************************************/

#include <video/vulkan/VulkanDriver.h>

#if defined(OS_WINDOWS)

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <utils/Log.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>

namespace t850 {

  static VulkanDriver*   GetVkDrv()  { return static_cast<VulkanDriver*>(g_pBaseDriver); }
  static VulkanRTFunctions& GetRT()  { return GetVkDrv()->m_rtFuncs; }

  // ─────────────────────────────────────────────────────
  //  Helpers
  // ─────────────────────────────────────────────────────
  static std::string ReadFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
  }

  // Compile HLSL → SPIR-V for ray tracing using glslang
  static bool CompileRTShaderToSPIRV(const std::string& source, glslang_stage_t stage,
                                      std::vector<uint32_t>& spirv, const std::string& name) {
    static bool s_init = false;
    if (!s_init) { glslang_initialize_process(); s_init = true; }

    glslang_input_t input = {};
    input.language = GLSLANG_SOURCE_HLSL;
    input.stage = stage;
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_2;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_5;
    input.code = source.c_str();
    input.default_version = 460;
    input.default_profile = GLSLANG_NO_PROFILE;

    glslang_shader_t* shader = glslang_shader_create(&input);
    if (!glslang_shader_preprocess(shader, &input)) {
      T8_LOG_ERROR("[VkRT] Preprocess failed (%s): %s", name.c_str(), glslang_shader_get_info_log(shader));
      glslang_shader_delete(shader); return false;
    }
    if (!glslang_shader_parse(shader, &input)) {
      T8_LOG_ERROR("[VkRT] Parse failed (%s): %s", name.c_str(), glslang_shader_get_info_log(shader));
      glslang_shader_delete(shader); return false;
    }

    glslang_program_t* prog = glslang_program_create();
    glslang_program_add_shader(prog, shader);
    if (!glslang_program_link(prog, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
      T8_LOG_ERROR("[VkRT] Link failed (%s): %s", name.c_str(), glslang_program_get_info_log(prog));
      glslang_program_delete(prog); glslang_shader_delete(shader); return false;
    }

    glslang_program_SPIRV_generate(prog, stage);
    const uint32_t* data = glslang_program_SPIRV_get_ptr(prog);
    size_t size = glslang_program_SPIRV_get_size(prog);
    spirv.assign(data, data + size);

    glslang_program_delete(prog);
    glslang_shader_delete(shader);
    T8_LOG_INFO("[VkRT] Compiled '%s' (%zu SPIR-V words)", name.c_str(), size);
    return true;
  }

  static VkShaderModule MakeModule(VkDevice dev, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = spirv.size() * sizeof(uint32_t);
    ci.pCode = spirv.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(dev, &ci, nullptr, &mod);
    return mod;
  }

  // ─────────────────────────────────────────────────────
  bool VulkanRTPipeline::BuildDescriptorSetLayout() {
    VkDevice device = GetVkDrv()->GetDevice();

    VkDescriptorSetLayoutBinding bindings[5] = {};
    // binding 0: TLAS
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    // binding 1: output UAV (storage image)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    // bindings 2-4: G-buffer SRVs
    for (int i = 2; i <= 4; i++) {
      bindings[i].binding = i;
      bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    }

    VkDescriptorSetLayoutCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    ci.bindingCount = 5;
    ci.pBindings = bindings;
    vkCreateDescriptorSetLayout(device, &ci, nullptr, &descSetLayout);
    return descSetLayout != VK_NULL_HANDLE;
  }

  bool VulkanRTPipeline::BuildPipelineLayout() {
    VkDevice device = GetVkDrv()->GetDevice();
    VkPipelineLayoutCreateInfo ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    ci.setLayoutCount = 1;
    ci.pSetLayouts = &descSetLayout;
    vkCreatePipelineLayout(device, &ci, nullptr, &pipelineLayout);
    return pipelineLayout != VK_NULL_HANDLE;
  }

  bool VulkanRTPipeline::BuildSBT(uint32_t handleSize, uint32_t handleAlignment) {
    auto* drv = GetVkDrv();
    VkDevice device = drv->GetDevice();
    VmaAllocator allocator = drv->GetAllocator();
    auto& rt = GetRT();

    // 3 groups: raygen + miss + hit
    uint32_t groupCount = 3;
    uint32_t alignedHandleSize = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    uint32_t sbtSize = alignedHandleSize * groupCount;

    std::vector<uint8_t> handles(handleSize * groupCount);
    rt.vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, 0, groupCount, handles.size(), handles.data());

    VkBufferCreateInfo bufCI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufCI.size = sbtSize;
    bufCI.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocInfo = {};
    vmaCreateBuffer(allocator, &bufCI, &allocCI, &m_sbtBuffer, &m_sbtAlloc, &allocInfo);
    if (!m_sbtBuffer) return false;

    uint8_t* mapped = static_cast<uint8_t*>(allocInfo.pMappedData);
    for (uint32_t i = 0; i < groupCount; i++) {
      std::memcpy(mapped + i * alignedHandleSize, handles.data() + i * handleSize, handleSize);
    }

    VkBufferDeviceAddressInfo addrInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    addrInfo.buffer = m_sbtBuffer;
    VkDeviceAddress base = vkGetBufferDeviceAddress(device, &addrInfo);

    raygenRegion = { base,                              alignedHandleSize, alignedHandleSize };
    missRegion   = { base + alignedHandleSize,          alignedHandleSize, alignedHandleSize };
    hitGroupRegion = { base + alignedHandleSize * 2,    alignedHandleSize, alignedHandleSize };
    callableRegion = {};
    return true;
  }

  // ─────────────────────────────────────────────────────
  bool VulkanRTPipeline::Create(const char* raygenSrc, const char* missSrc, const char* closestHitSrc) {
    auto* drv = GetVkDrv();
    VkDevice device = drv->GetDevice();
    auto& rt = GetRT();
    if (!rt.IsValid()) { T8_LOG_ERROR("[VkRT] RT functions not loaded"); return false; }

    if (!BuildDescriptorSetLayout() || !BuildPipelineLayout()) return false;

    // Compile shaders
    std::string raygenCode   = ReadFile(raygenSrc);
    std::string missCode     = ReadFile(missSrc);
    std::string hitCode      = ReadFile(closestHitSrc);
    if (raygenCode.empty() || missCode.empty() || hitCode.empty()) {
      T8_LOG_ERROR("[VkRT] Failed to read RT shader files");
      return false;
    }

    std::vector<uint32_t> raygenSPV, missSPV, hitSPV;
    if (!CompileRTShaderToSPIRV(raygenCode,   GLSLANG_STAGE_RAYGEN_NV,      raygenSPV, raygenSrc))   return false;
    if (!CompileRTShaderToSPIRV(missCode,     GLSLANG_STAGE_MISS_NV,        missSPV,   missSrc))     return false;
    if (!CompileRTShaderToSPIRV(hitCode,      GLSLANG_STAGE_CLOSEST_HIT_NV, hitSPV,    closestHitSrc)) return false;

    VkShaderModule raygenMod = MakeModule(device, raygenSPV);
    VkShaderModule missMod   = MakeModule(device, missSPV);
    VkShaderModule hitMod    = MakeModule(device, hitSPV);

    // Shader stages
    VkPipelineShaderStageCreateInfo stages[3] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[0].module = raygenMod; stages[0].pName = "RayGenShader";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[1].module = missMod; stages[1].pName = "MissShader";

    stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    stages[2].module = hitMod; stages[2].pName = "ClosestHitShader";

    // Shader groups
    VkRayTracingShaderGroupCreateInfoKHR groups[3] = {};
    // RayGen group
    groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader = 0;
    groups[0].closestHitShader = groups[0].anyHitShader = groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;
    // Miss group
    groups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[1].generalShader = 1;
    groups[1].closestHitShader = groups[1].anyHitShader = groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;
    // Hit group
    groups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[2].generalShader = VK_SHADER_UNUSED_KHR;
    groups[2].closestHitShader = 2;
    groups[2].anyHitShader = groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

    VkRayTracingPipelineCreateInfoKHR pipelineCI = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    pipelineCI.stageCount = 3;
    pipelineCI.pStages = stages;
    pipelineCI.groupCount = 3;
    pipelineCI.pGroups = groups;
    pipelineCI.maxPipelineRayRecursionDepth = 2;
    pipelineCI.layout = pipelineLayout;

    VkResult res = rt.vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                      1, &pipelineCI, nullptr, &pipeline);
    vkDestroyShaderModule(device, raygenMod, nullptr);
    vkDestroyShaderModule(device, missMod, nullptr);
    vkDestroyShaderModule(device, hitMod, nullptr);

    if (res != VK_SUCCESS) { T8_LOG_ERROR("[VkRT] vkCreateRayTracingPipelinesKHR failed res=%d", res); return false; }

    // Query RT pipeline properties for SBT
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 devProps2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    devProps2.pNext = &rtProps;
    vkGetPhysicalDeviceProperties2(drv->GetPhysicalDevice(), &devProps2);

    if (!BuildSBT(rtProps.shaderGroupHandleSize, rtProps.shaderGroupHandleAlignment)) return false;

    valid = true;
    T8_LOG_INFO("[VkRT] RT pipeline created: raygen='%s'", raygenSrc);
    return true;
  }

  void VulkanRTPipeline::Destroy() {
    auto* drv = GetVkDrv();
    VkDevice device = drv ? drv->GetDevice() : VK_NULL_HANDLE;
    VmaAllocator allocator = drv ? drv->GetAllocator() : VK_NULL_HANDLE;
    if (m_sbtBuffer)     { vmaDestroyBuffer(allocator, m_sbtBuffer, m_sbtAlloc); m_sbtBuffer = VK_NULL_HANDLE; }
    if (pipeline)        { vkDestroyPipeline(device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }
    if (pipelineLayout)  { vkDestroyPipelineLayout(device, pipelineLayout, nullptr); pipelineLayout = VK_NULL_HANDLE; }
    if (descSetLayout)   { vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr); descSetLayout = VK_NULL_HANDLE; }
    valid = false;
  }

} // namespace t850

#endif // OS_WINDOWS
