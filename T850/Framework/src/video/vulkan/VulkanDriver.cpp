/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanDriver.cpp: Driver lifecycle, Device, DeviceContext,
 *                   Buffers (VB, IB, CB), PSO cache, Texture,
 *                   RenderTarget, Shader.
 *********************************************************/

#include <video/vulkan/VulkanDriver.h>

#if defined(OS_WINDOWS)

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <SDL3/SDL_vulkan.h>

#include <utils/Log.h>
#include <debug/T8_Profiler.h>
#include <iostream>
#include <string>
#include <cassert>
#include <cstring>
#include <algorithm>

namespace t800 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  Shared helpers
  // ══════════════════════════════════════════════════════
  static VulkanDriver* GetVkDriver() { return static_cast<VulkanDriver*>(g_pBaseDriver); }

  // ══════════════════════════════════════════════════════
  //  VulkanDeviceContext
  // ══════════════════════════════════════════════════════

  void* VulkanDeviceContext::GetAPIObject() const { return (void*)m_commandBuffer; }
  void** VulkanDeviceContext::GetAPIObjectReference() const { return nullptr; }
  void VulkanDeviceContext::release() { m_commandBuffer = VK_NULL_HANDLE; }

  void VulkanDeviceContext::SetPrimitiveTopology(T8_TOPOLOGY::E topology) {
    // Vulkan sets topology at pipeline creation time; store for later pipeline lookup.
    (void)topology;
  }

  void VulkanDeviceContext::DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) {
    T8_LOG_TRACE("[Vulkan] DrawIndexed(%u, %u, %u)", vertexCount, startIndex, startVertex);
    if (m_commandBuffer)
      vkCmdDrawIndexed(m_commandBuffer, vertexCount, 1, startIndex, (int32_t)startVertex, 0);
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDevice
  // ══════════════════════════════════════════════════════

  void* VulkanDevice::GetAPIObject() const { return (void*)m_device; }
  void** VulkanDevice::GetAPIObjectReference() const { return nullptr; }
  void VulkanDevice::release() { m_device = VK_NULL_HANDLE; }

  Buffer* VulkanDevice::CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData) {
    T8_LOG_DEBUG("[Vulkan] CreateBuffer type=%d size=%d", bufferType, desc.byteWidth);
    Buffer* buf = nullptr;
    switch (bufferType) {
      case T8_BUFFER_TYPE::VERTEX:   buf = new VulkanVertexBuffer;   break;
      case T8_BUFFER_TYPE::INDEX:    buf = new VulkanIndexBuffer;    break;
      case T8_BUFFER_TYPE::CONSTANT: buf = new VulkanConstantBuffer; break;
    }
    if (buf) buf->Create(*this, desc, initialData);
    return buf;
  }

  ShaderBase* VulkanDevice::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key,
                                          const std::string& vs_name, const std::string& fs_name) {
    VulkanShader* sh = new VulkanShader();
    if (!sh->CreateShader(src_vs, src_fs, key, vs_name, fs_name)) {
      delete sh;
      return nullptr;
    }
    return sh;
  }

  Texture* VulkanDevice::CreateTexture(std::string path) {
    VulkanTexture* tex = new VulkanTexture;
    tex->LoadTexture(path.c_str());
    return tex;
  }

  Texture* VulkanDevice::CreateTextureFromMemory(const unsigned char* buff, int w, int h, int channels, std::string name) {
    VulkanTexture* tex = new VulkanTexture;
    tex->LoadFromMemory(buff, w, h, channels);
    return tex;
  }

  Texture* VulkanDevice::CreateCubeMap(const unsigned char* buff, int w, int h) {
    VulkanTexture* tex = new VulkanTexture;
    tex->CreateCubeMap(buff, w, h);
    return tex;
  }

  BaseRT* VulkanDevice::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips) {
    VulkanRT* rt = new VulkanRT;
    if (rt->LoadRT(nrt, cf, df, w, h, genMips)) return rt;
    delete rt;
    return nullptr;
  }

  // ══════════════════════════════════════════════════════
  //  Vulkan Buffers — Vertex Buffer
  // ══════════════════════════════════════════════════════

  void* VulkanVertexBuffer::GetAPIObject() const { return (void*)m_buffer; }
  void** VulkanVertexBuffer::GetAPIObjectReference() const { return nullptr; }

  void VulkanVertexBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    auto* driver = GetVkDriver();
    VmaAllocator allocator = driver->GetAllocator();

    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = desc.byteWidth;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo = {};
    VkResult res = vmaCreateBuffer(allocator, &bufInfo, &allocCI, &m_buffer, &m_allocation, &allocInfo);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] VB create failed res=%d", res);
      return;
    }
    m_mappedData = allocInfo.pMappedData;

    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      if (m_mappedData) memcpy(m_mappedData, initialData, desc.byteWidth);
    }
    T8_LOG_DEBUG("[Vulkan] VB created: %d bytes", desc.byteWidth);
  }

  void VulkanVertexBuffer::Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) {
    const_cast<DeviceContext*>(&deviceContext)->actualVertexBuffer = (VertexBuffer*)this;
    VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&deviceContext)->GetCommandBuffer();
    VkDeviceSize off = offset;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_buffer, &off);
  }

  void VulkanVertexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext) {
    if (m_mappedData && !sysMemCpy.empty())
      memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }

  void VulkanVertexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }

  void VulkanVertexBuffer::release() {
    auto* driver = GetVkDriver();
    if (m_buffer && driver->GetAllocator()) {
      vmaDestroyBuffer(driver->GetAllocator(), m_buffer, m_allocation);
    }
    m_buffer = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_mappedData = nullptr;
    sysMemCpy.clear();
    delete this;
  }

  // ══════════════════════════════════════════════════════
  //  Vulkan Buffers — Index Buffer
  // ══════════════════════════════════════════════════════

  void* VulkanIndexBuffer::GetAPIObject() const { return (void*)m_buffer; }
  void** VulkanIndexBuffer::GetAPIObjectReference() const { return nullptr; }

  void VulkanIndexBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    auto* driver = GetVkDriver();
    VmaAllocator allocator = driver->GetAllocator();

    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = desc.byteWidth;
    bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo = {};
    VkResult res = vmaCreateBuffer(allocator, &bufInfo, &allocCI, &m_buffer, &m_allocation, &allocInfo);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] IB create failed res=%d", res);
      return;
    }
    m_mappedData = allocInfo.pMappedData;

    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      if (m_mappedData) memcpy(m_mappedData, initialData, desc.byteWidth);
    }
    T8_LOG_DEBUG("[Vulkan] IB created: %d bytes", desc.byteWidth);
  }

  void VulkanIndexBuffer::Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format) {
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&deviceContext)->GetCommandBuffer();
    VkIndexType idxType = (format == T8_IB_FORMAR::R16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd, m_buffer, (VkDeviceSize)offset, idxType);
  }

  void VulkanIndexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext) {
    if (m_mappedData && !sysMemCpy.empty())
      memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }

  void VulkanIndexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }

  void VulkanIndexBuffer::release() {
    auto* driver = GetVkDriver();
    if (m_buffer && driver->GetAllocator()) {
      vmaDestroyBuffer(driver->GetAllocator(), m_buffer, m_allocation);
    }
    m_buffer = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_mappedData = nullptr;
    sysMemCpy.clear();
    delete this;
  }

  // ══════════════════════════════════════════════════════
  //  Vulkan Buffers — Constant Buffer
  // ══════════════════════════════════════════════════════

  void* VulkanConstantBuffer::GetAPIObject() const { return (void*)m_buffer; }
  void** VulkanConstantBuffer::GetAPIObjectReference() const { return nullptr; }

  void VulkanConstantBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    auto* driver = GetVkDriver();
    VmaAllocator allocator = driver->GetAllocator();

    // Align to minUniformBufferOffsetAlignment (typically 256)
    m_alignedSize = (desc.byteWidth + 255) & ~255u;

    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = m_alignedSize;
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo = {};
    VkResult res = vmaCreateBuffer(allocator, &bufInfo, &allocCI, &m_buffer, &m_allocation, &allocInfo);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CB create failed res=%d size=%u", res, m_alignedSize);
      return;
    }
    m_mappedData = allocInfo.pMappedData;

    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      if (m_mappedData) memcpy(m_mappedData, initialData, desc.byteWidth);
    }
    T8_LOG_DEBUG("[Vulkan] CB created: %d bytes (aligned=%u)", desc.byteWidth, m_alignedSize);
  }

  void VulkanConstantBuffer::Set(const DeviceContext& deviceContext) {
    const_cast<DeviceContext*>(&deviceContext)->actualConstantBuffer = (ConstantBuffer*)this;
    // CB data is pushed via the per-frame ring buffer in the descriptor update path.
    // Actual binding happens during VulkanShader::Set via push descriptors or descriptor sets.
    T8_LOG_TRACE("[Vulkan] CB::Set dataSize=%d", (int)sysMemCpy.size());
  }

  void VulkanConstantBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext) {
    if (m_mappedData && !sysMemCpy.empty())
      memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }

  void VulkanConstantBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }

  void VulkanConstantBuffer::release() {
    auto* driver = GetVkDriver();
    if (m_buffer && driver->GetAllocator()) {
      vmaDestroyBuffer(driver->GetAllocator(), m_buffer, m_allocation);
    }
    m_buffer = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_mappedData = nullptr;
    sysMemCpy.clear();
    delete this;
  }

  // ══════════════════════════════════════════════════════
  //  VulkanTexture
  // ══════════════════════════════════════════════════════

  void VulkanTexture::LoadAPITexture(DeviceContext* context, unsigned char* buffer) {
    T8_LOG_INFO("[Vulkan] TODO: LoadAPITexture (%ux%u)", x, y);
  }

  void VulkanTexture::LoadAPITextureCompressed(unsigned char* buffer) {
    T8_LOG_INFO("[Vulkan] TODO: LoadAPITextureCompressed");
  }

  void VulkanTexture::DestroyAPITexture() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    if (m_sampler)   { vkDestroySampler(device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
    if (m_imageView) { vkDestroyImageView(device, m_imageView, nullptr); m_imageView = VK_NULL_HANDLE; }
    if (m_image)     { vmaDestroyImage(allocator, m_image, m_allocation); m_image = VK_NULL_HANDLE; }
  }

  void VulkanTexture::SetTextureParams() {
    T8_LOG_TRACE("[Vulkan] TODO: SetTextureParams");
  }

  void VulkanTexture::GetFormatBpp(unsigned int& props, unsigned int& format, unsigned int& bpp) {
    props = CH_RGBA;
    format = VK_FORMAT_R8G8B8A8_UNORM;
    bpp = 4;
  }

  void VulkanTexture::Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) {
    T8_LOG_TRACE("[Vulkan] Texture::Set slot=%u name=%s", slot, shaderTextureName.c_str());
  }

  void VulkanTexture::SetSampler(const DeviceContext& deviceContext, unsigned int slot) {
    // In Vulkan, samplers are part of the descriptor set, bound during shader Set().
  }

  // ══════════════════════════════════════════════════════
  //  VulkanRT
  // ══════════════════════════════════════════════════════

  bool VulkanRT::LoadAPIRT() {
    T8_LOG_INFO("[Vulkan] TODO: LoadAPIRT (%dx%d, %d color attachments)", w, h, number_RT);
    return true;
  }

  void VulkanRT::DestroyAPIRT() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    if (m_framebuffer) { vkDestroyFramebuffer(device, m_framebuffer, nullptr); m_framebuffer = VK_NULL_HANDLE; }
    if (m_renderPass)  { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }

    for (auto& view : vColorImageViews) vkDestroyImageView(device, view, nullptr);
    for (size_t i = 0; i < vColorImages.size(); i++) vmaDestroyImage(allocator, vColorImages[i], vColorAllocations[i]);
    vColorImageViews.clear();
    vColorImages.clear();
    vColorAllocations.clear();
    vColorLayouts.clear();

    if (m_depthImageView) { vkDestroyImageView(device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
    if (m_depthImage)     { vmaDestroyImage(allocator, m_depthImage, m_depthAllocation); m_depthImage = VK_NULL_HANDLE; }

    for (int i = 0; i < 6; i++) {
      if (m_cubeFaceViews[i]) { vkDestroyImageView(device, m_cubeFaceViews[i], nullptr); m_cubeFaceViews[i] = VK_NULL_HANDLE; }
    }
  }

  void VulkanRT::Set(const DeviceContext& context) {
    T8_LOG_TRACE("[Vulkan] TODO: RT::Set");
  }

  void VulkanRT::ChangeCubeDepthTexture(int i) {
    T8_LOG_TRACE("[Vulkan] TODO: ChangeCubeDepthTexture(%d)", i);
  }

  // ══════════════════════════════════════════════════════
  //  VulkanShader
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

  static bool CompileGLSLToSPIRV(const std::string& source, glslang_stage_t stage,
                                   std::vector<uint32_t>& spirv, const std::string& debugName) {
    if (!s_glslangInitialized) {
      glslang_initialize_process();
      s_glslangInitialized = true;
    }

    const char* src = source.c_str();
    glslang_input_t input = {};
    input.language = GLSLANG_SOURCE_GLSL;
    input.stage = stage;
    input.client = GLSLANG_CLIENT_VULKAN;
    input.client_version = GLSLANG_TARGET_VULKAN_1_0;
    input.target_language = GLSLANG_TARGET_SPV;
    input.target_language_version = GLSLANG_TARGET_SPV_1_0;
    input.code = src;
    input.default_version = 450;
    input.default_profile = GLSLANG_NO_PROFILE;
    input.force_default_version_and_profile = false;
    input.forward_compatible = false;
    input.messages = GLSLANG_MSG_DEFAULT_BIT;
    input.resource = glslang_default_resource();

    glslang_shader_t* shader = glslang_shader_create(&input);
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

  bool VulkanShader::CreateShaderAPI(std::string src_vs, std::string src_fs,
                                      const std::string& vs_name, const std::string& fs_name) {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();

    // Compile vertex shader
    std::vector<uint32_t> vsSPIRV;
    if (!CompileGLSLToSPIRV(src_vs, GLSLANG_STAGE_VERTEX, vsSPIRV, vs_name.empty() ? "VS" : vs_name)) {
      return false;
    }
    m_vertModule = CreateShaderModule(device, vsSPIRV.data(), vsSPIRV.size() * sizeof(uint32_t));
    if (!m_vertModule) return false;

    // Compile fragment shader
    std::vector<uint32_t> fsSPIRV;
    if (!CompileGLSLToSPIRV(src_fs, GLSLANG_STAGE_FRAGMENT, fsSPIRV, fs_name.empty() ? "FS" : fs_name)) {
      return false;
    }
    m_fragModule = CreateShaderModule(device, fsSPIRV.data(), fsSPIRV.size() * sizeof(uint32_t));
    if (!m_fragModule) return false;

    // Create a minimal descriptor set layout (one UBO binding, up to 8 texture bindings)
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    // Binding 0: UBO (vertex + fragment)
    VkDescriptorSetLayoutBinding uboBinding = {};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(uboBinding);
    cbvBinding = 0;

    // Bindings 1-8: combined image samplers
    for (int i = 0; i < 8; i++) {
      VkDescriptorSetLayoutBinding texBinding = {};
      texBinding.binding = 1 + i;
      texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      texBinding.descriptorCount = 1;
      texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings.push_back(texBinding);
      srvBindings[i] = 1 + i;
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

    T8_LOG_INFO("[Vulkan] Shader created: VS='%s' FS='%s'", vs_name.c_str(), fs_name.c_str());
    return true;
  }

  void VulkanShader::Set(const DeviceContext& deviceContext) {
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = this;
    auto* driver = GetVkDriver();
    VkPipeline pipeline = driver->GetOrCreatePipeline(this);
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

  VkPipeline VulkanDriver::GetOrCreatePipeline(VulkanShader* shader, uint8_t numColorAttachments,
                                                VkFormat colorFormat, VkFormat depthFormat) {
    VulkanPipelineKey key = {};
    key.shaderPtr = reinterpret_cast<uintptr_t>(shader);
    key.blend = (uint8_t)m_currentBlend;
    key.depth = (uint8_t)m_currentDepth;
    key.cull = (uint8_t)m_currentCull;
    key.numColorAttachments = numColorAttachments;
    key.colorFormat = colorFormat;
    key.depthFormat = depthFormat;

    auto it = m_pipelineCache.find(key);
    if (it != m_pipelineCache.end()) return it->second;

    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader->m_vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader->m_fragModule;
    stages[1].pName = "main";

    // Vertex input (use shader's reflection data or empty for now)
    VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    if (!shader->m_vertexAttributes.empty()) {
      vertexInput.vertexBindingDescriptionCount = 1;
      vertexInput.pVertexBindingDescriptions = &shader->m_vertexBinding;
      vertexInput.vertexAttributeDescriptionCount = (uint32_t)shader->m_vertexAttributes.size();
      vertexInput.pVertexAttributeDescriptions = shader->m_vertexAttributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Dynamic viewport/scissor
    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    switch (m_currentCull) {
      case FRONT_FACES:    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  break;
      case BACK_FACES:     rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; break;
      case FRONT_AND_BACK: rasterizer.cullMode = VK_CULL_MODE_NONE;      break;
      default:             rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  break;
    }
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    switch (m_currentDepth) {
      case DEPTH_DEFAULT: case READ_WRITE:
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
      case READ:
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
      case NONE:
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        break;
    }

    // Color blend
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(numColorAttachments);
    for (auto& att : blendAttachments) {
      att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
      switch (m_currentBlend) {
        case BLEND_DEFAULT: case BLEND_OPAQUE:
          att.blendEnable = VK_FALSE;
          break;
        case ADDITIVE:
          att.blendEnable = VK_TRUE;
          att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
          att.colorBlendOp = VK_BLEND_OP_ADD;
          att.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
          att.alphaBlendOp = VK_BLEND_OP_ADD;
          break;
        case ALPHA_BLEND:
          att.blendEnable = VK_TRUE;
          att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
          att.colorBlendOp = VK_BLEND_OP_ADD;
          att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
          att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
          att.alphaBlendOp = VK_BLEND_OP_ADD;
          break;
        case NON_PREMULTIPLIED:
          att.blendEnable = VK_TRUE;
          att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
          att.colorBlendOp = VK_BLEND_OP_ADD;
          att.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
          att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
          att.alphaBlendOp = VK_BLEND_OP_ADD;
          break;
      }
    }

    VkPipelineColorBlendStateCreateInfo colorBlend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlend.attachmentCount = numColorAttachments;
    colorBlend.pAttachments = blendAttachments.data();

    // Dynamic states
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = stages;
    pipelineCI.pVertexInputState = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState = &multisampling;
    pipelineCI.pDepthStencilState = &depthStencil;
    pipelineCI.pColorBlendState = &colorBlend;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.layout = shader->m_pipelineLayout;
    pipelineCI.renderPass = m_backbufferRenderPass;
    pipelineCI.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult res = vkCreateGraphicsPipelines(m_device, m_vkPipelineCache, 1, &pipelineCI, nullptr, &pipeline);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateGraphicsPipelines failed res=%d shader=%p blend=%d depth=%d cull=%d",
                   res, shader, key.blend, key.depth, key.cull);
      return VK_NULL_HANDLE;
    }

    T8_LOG_DEBUG("[Vulkan] Pipeline created: shader=%p blend=%d depth=%d cull=%d",
                 shader, key.blend, key.depth, key.cull);
    m_pipelineCache[key] = pipeline;
    return pipeline;
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Core lifecycle
  // ══════════════════════════════════════════════════════

  void VulkanDriver::SetWindow(void* window) {
    m_hwnd = (HWND)window;
    if (!m_hwnd) m_hwnd = GetActiveWindow();
    // Surface creation is deferred to InitDriver where the VkInstance is available.
    // The window pointer is stored for use there.
  }

  void VulkanDriver::SetDimensions(int w, int h) { width = w; height = h; }

  void VulkanDriver::CreateInstance() {
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "T850 Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "T850";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // Get SDL-required extensions
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

    std::vector<const char*> extensions;
    for (uint32_t i = 0; i < sdlExtCount; i++)
      extensions.push_back(sdlExts[i]);

#ifdef _DEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();

#ifdef _DEBUG
    const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = validationLayers;
#endif

    VkResult res = vkCreateInstance(&ci, nullptr, &m_instance);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateInstance failed res=%d", res);
      return;
    }
    T8_LOG_INFO("[Vulkan] Instance created (%u extensions)", (uint32_t)extensions.size());
  }

  void VulkanDriver::CreateDevice() {
    // Pick physical device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
      T8_LOG_ERROR("[Vulkan] No GPU with Vulkan support found");
      return;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Pick first discrete GPU, or fallback to first device
    m_physicalDevice = devices[0];
    for (auto& dev : devices) {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(dev, &props);
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        m_physicalDevice = dev;
        T8_LOG_INFO("[Vulkan] GPU: %s", props.deviceName);
        break;
      }
    }

    // Find graphics queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    m_graphicsQueueFamily = 0;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
      if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        m_graphicsQueueFamily = i;
        break;
      }
    }
    m_presentQueueFamily = m_graphicsQueueFamily;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCI = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueCI.queueFamilyIndex = m_graphicsQueueFamily;
    queueCI.queueCount = 1;
    queueCI.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo deviceCI = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceCI.queueCreateInfoCount = 1;
    deviceCI.pQueueCreateInfos = &queueCI;
    deviceCI.enabledExtensionCount = 1;
    deviceCI.ppEnabledExtensionNames = deviceExtensions;
    deviceCI.pEnabledFeatures = &deviceFeatures;

    VkResult res = vkCreateDevice(m_physicalDevice, &deviceCI, nullptr, &m_device);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateDevice failed res=%d", res);
      return;
    }
    static_cast<VulkanDevice*>(T8Device)->m_device = m_device;

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    m_presentQueue = m_graphicsQueue;
    T8_LOG_INFO("[Vulkan] Logical device created, graphics queue family=%u", m_graphicsQueueFamily);
  }

  void VulkanDriver::CreateAllocator() {
    VmaAllocatorCreateInfo allocCI = {};
    allocCI.physicalDevice = m_physicalDevice;
    allocCI.device = m_device;
    allocCI.instance = m_instance;
    allocCI.vulkanApiVersion = VK_API_VERSION_1_0;

    VkResult res = vmaCreateAllocator(&allocCI, &m_allocator);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vmaCreateAllocator failed res=%d", res);
      return;
    }
    T8_LOG_INFO("[Vulkan] VMA allocator created");
  }

  void VulkanDriver::CreateSwapChain() {
    // Query surface capabilities
    VkSurfaceCapabilitiesKHR surfCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfCaps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

    // Prefer B8G8R8A8_UNORM, SRGB_NONLINEAR
    m_swapChainFormat = formats[0].format;
    VkColorSpaceKHR colorSpace = formats[0].colorSpace;
    for (auto& fmt : formats) {
      if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        m_swapChainFormat = fmt.format;
        colorSpace = fmt.colorSpace;
        break;
      }
    }

    // Pick present mode: prefer MAILBOX, fallback to FIFO
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    VkPresentModeKHR chosenMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto mode : presentModes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) { chosenMode = mode; break; }
    }

    m_swapChainExtent = { (uint32_t)width, (uint32_t)height };
    if (surfCaps.currentExtent.width != UINT32_MAX)
      m_swapChainExtent = surfCaps.currentExtent;

    uint32_t imageCount = surfCaps.minImageCount + 1;
    if (surfCaps.maxImageCount > 0 && imageCount > surfCaps.maxImageCount)
      imageCount = surfCaps.maxImageCount;
    if (imageCount < kBackBufferCount) imageCount = kBackBufferCount;

    VkSwapchainCreateInfoKHR scCI = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    scCI.surface = m_surface;
    scCI.minImageCount = imageCount;
    scCI.imageFormat = m_swapChainFormat;
    scCI.imageColorSpace = colorSpace;
    scCI.imageExtent = m_swapChainExtent;
    scCI.imageArrayLayers = 1;
    scCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scCI.preTransform = surfCaps.currentTransform;
    scCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scCI.presentMode = chosenMode;
    scCI.clipped = VK_TRUE;

    VkResult res = vkCreateSwapchainKHR(m_device, &scCI, nullptr, &m_swapChain);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateSwapchainKHR failed res=%d", res);
      return;
    }
    T8_LOG_INFO("[Vulkan] Swap chain created (%ux%u, %u images, mode=%d)",
                m_swapChainExtent.width, m_swapChainExtent.height, imageCount, chosenMode);
  }

  void VulkanDriver::CreateBackBufferViews() {
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
      VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
      ivCI.image = m_swapChainImages[i];
      ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivCI.format = m_swapChainFormat;
      ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      ivCI.subresourceRange.levelCount = 1;
      ivCI.subresourceRange.layerCount = 1;
      vkCreateImageView(m_device, &ivCI, nullptr, &m_swapChainImageViews[i]);
    }
    T8_LOG_INFO("[Vulkan] Back buffer image views created (%u)", imageCount);
  }

  void VulkanDriver::CreateRenderPass() {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapChainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo rpCI = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpCI.attachmentCount = 2;
    rpCI.pAttachments = attachments;
    rpCI.subpassCount = 1;
    rpCI.pSubpasses = &subpass;
    rpCI.dependencyCount = 1;
    rpCI.pDependencies = &dependency;

    VkResult res = vkCreateRenderPass(m_device, &rpCI, nullptr, &m_backbufferRenderPass);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateRenderPass failed res=%d", res);
      return;
    }
    T8_LOG_INFO("[Vulkan] Backbuffer render pass created");
  }

  void VulkanDriver::CreateDepthBuffer() {
    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = VK_FORMAT_D32_SFLOAT;
    imgCI.extent = { (uint32_t)width, (uint32_t)height, 1 };
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult res = vmaCreateImage(m_allocator, &imgCI, &allocCI, &m_depthImage, &m_depthAllocation, nullptr);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Depth image creation failed res=%d", res);
      return;
    }

    VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivCI.image = m_depthImage;
    ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = VK_FORMAT_D32_SFLOAT;
    ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    ivCI.subresourceRange.levelCount = 1;
    ivCI.subresourceRange.layerCount = 1;

    res = vkCreateImageView(m_device, &ivCI, nullptr, &m_depthImageView);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] Depth image view creation failed res=%d", res);
      return;
    }
    T8_LOG_INFO("[Vulkan] Depth buffer created (%dx%d)", width, height);
  }

  void VulkanDriver::CreateFramebuffers() {
    for (uint32_t i = 0; i < (uint32_t)m_swapChainImageViews.size() && i < kBackBufferCount; i++) {
      VkImageView attachments[] = { m_swapChainImageViews[i], m_depthImageView };

      VkFramebufferCreateInfo fbCI = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
      fbCI.renderPass = m_backbufferRenderPass;
      fbCI.attachmentCount = 2;
      fbCI.pAttachments = attachments;
      fbCI.width = m_swapChainExtent.width;
      fbCI.height = m_swapChainExtent.height;
      fbCI.layers = 1;

      VkResult res = vkCreateFramebuffer(m_device, &fbCI, nullptr, &m_backbufferFramebuffers[i]);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] vkCreateFramebuffer[%u] failed res=%d", i, res);
      }
    }
    T8_LOG_INFO("[Vulkan] Framebuffers created (%u)", (uint32_t)m_swapChainImageViews.size());
  }

  void VulkanDriver::CreateCommandInfrastructure() {
    // Main command pool
    VkCommandPoolCreateInfo poolCI = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolCI.queueFamilyIndex = m_graphicsQueueFamily;
    poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(m_device, &poolCI, nullptr, &m_commandPool);

    // Transient command pool for upload helpers
    VkCommandPoolCreateInfo transientPoolCI = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    transientPoolCI.queueFamilyIndex = m_graphicsQueueFamily;
    transientPoolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    vkCreateCommandPool(m_device, &transientPoolCI, nullptr, &m_transientCommandPool);

    // Allocate per-frame command buffers
    VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kBackBufferCount;
    vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers);

    static_cast<VulkanDeviceContext*>(T8DeviceContext)->m_commandBuffer = m_commandBuffers[0];
    T8_LOG_INFO("[Vulkan] Command infrastructure created (%u command buffers)", kBackBufferCount);
  }

  void VulkanDriver::CreateSyncObjects() {
    VkSemaphoreCreateInfo semCI = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceCI = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      vkCreateSemaphore(m_device, &semCI, nullptr, &m_imageAvailableSemaphores[i]);
      vkCreateSemaphore(m_device, &semCI, nullptr, &m_renderFinishedSemaphores[i]);
      vkCreateFence(m_device, &fenceCI, nullptr, &m_inFlightFences[i]);
    }
    T8_LOG_INFO("[Vulkan] Sync objects created (%u frames in flight)", kBackBufferCount);
  }

  void VulkanDriver::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 },
    };

    VkDescriptorPoolCreateInfo dpCI = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpCI.maxSets = 4096;
    dpCI.poolSizeCount = 2;
    dpCI.pPoolSizes = poolSizes;
    dpCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    VkResult res = vkCreateDescriptorPool(m_device, &dpCI, nullptr, &m_descriptorPool);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateDescriptorPool failed res=%d", res);
    }
    T8_LOG_INFO("[Vulkan] Descriptor pool created");
  }

  void VulkanDriver::InitDriver() {
    T8Device = new VulkanDevice;
    T8DeviceContext = new VulkanDeviceContext;

    CreateInstance();

    // Create surface via SDL — m_hwnd holds the SDL_Window* passed through SetWindow()
    if (m_hwnd && m_instance) {
      SDL_Window* sdlWin = (SDL_Window*)m_hwnd;
      if (!SDL_Vulkan_CreateSurface(sdlWin, m_instance, nullptr, &m_surface)) {
        T8_LOG_ERROR("[Vulkan] SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
      } else {
        T8_LOG_INFO("[Vulkan] Surface created via SDL");
      }
    }

    CreateDevice();
    CreateAllocator();
    CreateCommandInfrastructure();
    CreateSwapChain();
    CreateBackBufferViews();
    CreateRenderPass();
    CreateDepthBuffer();
    CreateFramebuffers();
    CreateSyncObjects();
    CreateDescriptorPool();

    // Pipeline cache
    VkPipelineCacheCreateInfo pcCI = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    vkCreatePipelineCache(m_device, &pcCI, nullptr, &m_vkPipelineCache);

    // Per-frame CB ring buffers
    {
      VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      bufInfo.size = kCBRingBufferSize;
      bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
      bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

      VmaAllocationCreateInfo allocCI = {};
      allocCI.usage = VMA_MEMORY_USAGE_AUTO;
      allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

      for (uint32_t i = 0; i < kBackBufferCount; i++) {
        VmaAllocationInfo allocInfo = {};
        vmaCreateBuffer(m_allocator, &bufInfo, &allocCI,
                        &m_cbRingBuffers[i], &m_cbRingAllocations[i], &allocInfo);
        m_cbRingMapped[i] = allocInfo.pMappedData;
      }
      T8_LOG_INFO("[Vulkan] CB ring buffers created (%u KB x %u)", kCBRingBufferSize / 1024, kBackBufferCount);
    }

    m_viewport = { 0.f, 0.f, (float)width, (float)height, 0.f, 1.f };
    m_scissorRect = { {0, 0}, {(uint32_t)width, (uint32_t)height} };

    T8_LOG_INFO("[Vulkan] Driver initialized (%dx%d)", width, height);
  }

  void VulkanDriver::CreateSurfaces() {}
  void VulkanDriver::DestroySurfaces() {}
  void VulkanDriver::Update() {}

  void VulkanDriver::DestroyDriver() {
    WaitForGPU();
    DestroyShaders();
    DestroyRTs();
    DestroyTextures();

    // Destroy pipeline cache entries
    for (auto& pair : m_pipelineCache)
      vkDestroyPipeline(m_device, pair.second, nullptr);
    m_pipelineCache.clear();

    if (m_vkPipelineCache) { vkDestroyPipelineCache(m_device, m_vkPipelineCache, nullptr); m_vkPipelineCache = VK_NULL_HANDLE; }

    // Destroy CB ring buffers
    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_cbRingBuffers[i]) {
        vmaDestroyBuffer(m_allocator, m_cbRingBuffers[i], m_cbRingAllocations[i]);
        m_cbRingBuffers[i] = VK_NULL_HANDLE;
        m_cbRingMapped[i] = nullptr;
      }
    }

    if (m_descriptorPool) { vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }

    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_imageAvailableSemaphores[i]) vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
      if (m_renderFinishedSemaphores[i]) vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
      if (m_inFlightFences[i])           vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    for (auto& fb : m_backbufferFramebuffers)
      if (fb) { vkDestroyFramebuffer(m_device, fb, nullptr); fb = VK_NULL_HANDLE; }

    if (m_depthImageView) { vkDestroyImageView(m_device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
    if (m_depthImage)     { vmaDestroyImage(m_allocator, m_depthImage, m_depthAllocation); m_depthImage = VK_NULL_HANDLE; }

    if (m_backbufferRenderPass) { vkDestroyRenderPass(m_device, m_backbufferRenderPass, nullptr); m_backbufferRenderPass = VK_NULL_HANDLE; }

    for (auto& iv : m_swapChainImageViews)
      vkDestroyImageView(m_device, iv, nullptr);
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();

    if (m_swapChain) { vkDestroySwapchainKHR(m_device, m_swapChain, nullptr); m_swapChain = VK_NULL_HANDLE; }

    if (m_transientCommandPool) { vkDestroyCommandPool(m_device, m_transientCommandPool, nullptr); m_transientCommandPool = VK_NULL_HANDLE; }
    if (m_commandPool) { vkDestroyCommandPool(m_device, m_commandPool, nullptr); m_commandPool = VK_NULL_HANDLE; }

    if (m_allocator) { vmaDestroyAllocator(m_allocator); m_allocator = VK_NULL_HANDLE; }

    if (m_surface) { vkDestroySurfaceKHR(m_instance, m_surface, nullptr); m_surface = VK_NULL_HANDLE; }

    if (m_device) { vkDestroyDevice(m_device, nullptr); m_device = VK_NULL_HANDLE; }

#ifdef _DEBUG
    if (m_debugMessenger) {
      auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
      if (func) func(m_instance, m_debugMessenger, nullptr);
      m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (m_instance) { vkDestroyInstance(m_instance, nullptr); m_instance = VK_NULL_HANDLE; }

    T8Device->release();
    T8DeviceContext->release();
    delete T8Device;   T8Device = nullptr;
    delete T8DeviceContext; T8DeviceContext = nullptr;

    T8_LOG_INFO("[Vulkan] Driver destroyed");
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Synchronization
  // ══════════════════════════════════════════════════════

  void VulkanDriver::WaitForFence(uint32_t frameIndex) {
    vkWaitForFences(m_device, 1, &m_inFlightFences[frameIndex], VK_TRUE, UINT64_MAX);
  }

  void VulkanDriver::WaitForGPU() {
    if (m_device) vkDeviceWaitIdle(m_device);
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Frame lifecycle
  // ══════════════════════════════════════════════════════

  void VulkanDriver::BeginFrame() {
    WaitForFence(m_currentFrame);
    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

    VkResult res = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
                                          m_imageAvailableSemaphores[m_currentFrame],
                                          VK_NULL_HANDLE, &m_imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
      T8_LOG_INFO("[Vulkan] Swap chain out of date, needs recreation");
      return;
    }

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    static_cast<VulkanDeviceContext*>(T8DeviceContext)->m_commandBuffer = cmd;

    m_cbRingOffset = 0;
    m_lastPipeline = VK_NULL_HANDLE;
    m_lastPipelineLayout = VK_NULL_HANDLE;
  }

  void VulkanDriver::EndFrame() {}

  void VulkanDriver::BuildPipelineObjects() {
    T8_LOG_INFO("[Vulkan] BuildPipelineObjects");
  }

  void VulkanDriver::Clear() {
    if (!m_frameStarted) {
      BeginFrame();
      m_frameStarted = true;
    }

    if (CurrentRT < 0) {
      VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

      VkClearValue clearValues[2] = {};
      clearValues[0].color = { {0.9f, 0.9f, 0.9f, 1.0f} };
      clearValues[1].depthStencil = { 1.0f, 0 };

      VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
      rpBegin.renderPass = m_backbufferRenderPass;
      rpBegin.framebuffer = m_backbufferFramebuffers[m_imageIndex];
      rpBegin.renderArea.offset = { 0, 0 };
      rpBegin.renderArea.extent = m_swapChainExtent;
      rpBegin.clearValueCount = 2;
      rpBegin.pClearValues = clearValues;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

      vkCmdSetViewport(cmd, 0, 1, &m_viewport);
      vkCmdSetScissor(cmd, 0, 1, &m_scissorRect);
    }
  }

  void VulkanDriver::SwapBuffers() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    // End the render pass
    vkCmdEndRenderPass(cmd);

    // End command buffer
    vkEndCommandBuffer(cmd);

    // Submit
    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);

    // Present
    VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_imageIndex;

    vkQueuePresentKHR(m_presentQueue, &presentInfo);

    m_currentFrame = (m_currentFrame + 1) % kBackBufferCount;
    m_frameStarted = false;
  }

  void VulkanDriver::SetBlendState(BLEND_STATES state) {
    T8_LOG_TRACE("[Vulkan] SetBlendState(%d)", state);
    m_currentBlend = state;
  }

  void VulkanDriver::SetDepthStencilState(DEPTH_STENCIL_STATES state) {
    T8_LOG_TRACE("[Vulkan] SetDepthStencilState(%d)", state);
    m_currentDepth = state;
  }

  void VulkanDriver::SetCullFace(FACE_CULLING state) {
    T8_LOG_TRACE("[Vulkan] SetCullFace(%d)", state);
    m_currentCull = state;
    m_FaceCulling = state;
  }

  void VulkanDriver::PopRT() {
    T8_LOG_TRACE("[Vulkan] PopRT (CurrentRT=%d)", CurrentRT);
    // TODO: end current RT render pass, transition resources, rebind backbuffer
    CurrentRT = -1;
  }

  void VulkanDriver::SaveScreenshot(std::string path) {
    T8_LOG_INFO("[Vulkan] TODO: SaveScreenshot('%s') — not implemented", path.c_str());
  }

  void VulkanDriver::SaveRTToFile(int rtID, int attachment, std::string path) {
    T8_LOG_INFO("[Vulkan] TODO: SaveRTToFile(rt=%d, att=%d, '%s') — not implemented", rtID, attachment, path.c_str());
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Upload & ring buffer helpers
  // ══════════════════════════════════════════════════════

  void VulkanDriver::UploadBufferData(VkBuffer dest, const void* data, VkDeviceSize dataSize) {
    // Create staging buffer
    VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = dataSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCI = {};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingAllocInfo;
    vmaCreateBuffer(m_allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
    memcpy(stagingAllocInfo.pMappedData, data, dataSize);

    // Record and submit copy command
    VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = m_transientCommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion = {};
    copyRegion.size = dataSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, dest, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &cmd);
    vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAlloc);
  }

  VkDescriptorBufferInfo VulkanDriver::AllocateCBData(const void* data, uint32_t dataSize) {
    uint32_t alignedSize = (dataSize + 255) & ~255u;
    if (m_cbRingOffset + alignedSize > kCBRingBufferSize) {
      T8_LOG_ERROR("[Vulkan] CB ring buffer overflow! offset=%u + size=%u > %u", m_cbRingOffset, alignedSize, kCBRingBufferSize);
      m_cbRingOffset = 0;
    }

    uint32_t bufIdx = m_currentFrame;
    unsigned char* dst = (unsigned char*)m_cbRingMapped[bufIdx] + m_cbRingOffset;
    memcpy(dst, data, dataSize);

    VkDescriptorBufferInfo info = {};
    info.buffer = m_cbRingBuffers[bufIdx];
    info.offset = m_cbRingOffset;
    info.range = alignedSize;

    m_cbRingOffset += alignedSize;
    return info;
  }

  void VulkanDriver::BindBackBufferNoDepth() {
    T8_LOG_TRACE("[Vulkan] BindBackBufferNoDepth");
    // In Vulkan, depth test is controlled by PSO state (NONE), so this is a no-op.
    // The depth attachment remains bound but the pipeline disables depth testing.
  }

} // namespace t800

#endif // OS_WINDOWS
