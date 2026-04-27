#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanRT.cpp: Render Target implementation
 *********************************************************/

#include <video/vulkan/VulkanRT.h>
#include <video/vulkan/VulkanDriver.h>
#include <video/vulkan/VulkanTexture.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS)

#include <utils/Log.h>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  VulkanRT
  // ══════════════════════════════════════════════════════

  bool VulkanRT::LoadAPIRT() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    // Detect UAV-only formats (for ray tracing output textures)
    bool isUAV = (color_format == BaseRT::RGBA16F_UAV || color_format == BaseRT::R8_UAV);

    // Helper to resolve BaseRT format enum to VkFormat
    auto resolveFormat = [](int fmt) -> VkFormat {
      switch (fmt) {
        case BaseRT::RGBA8:      return VK_FORMAT_R8G8B8A8_UNORM;
        case BaseRT::RGBA16F:    return VK_FORMAT_R16G16B16A16_SFLOAT;
        case BaseRT::F16:        return VK_FORMAT_R16_SFLOAT;
        case BaseRT::R8:         return VK_FORMAT_R8_UNORM;
        case BaseRT::F32:        return VK_FORMAT_R32_SFLOAT;
        case BaseRT::RGBA16F_UAV:return VK_FORMAT_R16G16B16A16_SFLOAT;
        case BaseRT::R8_UAV:     return VK_FORMAT_R8_UNORM;
        default:                 return VK_FORMAT_R8G8B8A8_UNORM;
      }
    };

    // Default color format (used when perColorFormats is empty)
    m_colorFormat = resolveFormat(color_format);

    // Per-attachment formats (mirrors D3D11/D3D12 behavior)
    std::vector<VkFormat> colorFormats(number_RT, m_colorFormat);
    if (!perColorFormats.empty()) {
      for (int i = 0; i < number_RT && i < (int)perColorFormats.size(); i++)
        colorFormats[i] = resolveFormat(perColorFormats[i]);
    }

    // ── 1. Color attachments ──
    vColorImages.resize(number_RT);
    vColorAllocations.resize(number_RT);
    vColorImageViews.resize(number_RT);
    vColorLayouts.resize(number_RT);
    vColorTextures.resize(number_RT);

    for (int i = 0; i < number_RT; i++) {
      VkFormat attachmentFormat = colorFormats[i];
      VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
      imgCI.imageType = VK_IMAGE_TYPE_2D;
      imgCI.format = attachmentFormat;
      imgCI.extent = { (uint32_t)w, (uint32_t)h, 1 };
      imgCI.mipLevels = 1;
      imgCI.arrayLayers = 1;
      imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
      imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
      imgCI.usage = isUAV
                  ? (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                  : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
      imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

      VmaAllocationCreateInfo allocCI = {};
      allocCI.usage = VMA_MEMORY_USAGE_AUTO;

      VkResult res = vmaCreateImage(allocator, &imgCI, &allocCI, &vColorImages[i], &vColorAllocations[i], nullptr);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] RT color image[%d] creation failed res=%d", i, res);
        return false;
      }

      VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
      ivCI.image = vColorImages[i];
      ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivCI.format = attachmentFormat;
      ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      ivCI.subresourceRange.baseMipLevel = 0;
      ivCI.subresourceRange.levelCount = 1;
      ivCI.subresourceRange.baseArrayLayer = 0;
      ivCI.subresourceRange.layerCount = 1;

      res = vkCreateImageView(device, &ivCI, nullptr, &vColorImageViews[i]);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] RT color image view[%d] creation failed res=%d", i, res);
        return false;
      }

      vColorLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;

      // Create wrapper Texture for this color attachment
      VulkanTexture* tex = new VulkanTexture;
      tex->m_image = vColorImages[i];
      tex->m_imageView = vColorImageViews[i];
      tex->m_format = attachmentFormat;
      tex->x = (unsigned int)w;
      tex->y = (unsigned int)h;
      tex->m_channels = 4;
      tex->params = CLAMP_TO_EDGE;
      tex->SetTextureParams();
      vColorTextures[i] = tex;
    }

    // ── 2. Depth attachment ──
    bool hasDepth = (depth_format != BaseRT::NOTHING);
    if (hasDepth) {
      switch (depth_format) {
        case BaseRT::F32:  m_depthFormat = VK_FORMAT_D32_SFLOAT; break;
        case BaseRT::FD16: m_depthFormat = VK_FORMAT_D16_UNORM;  break;
        default:           m_depthFormat = VK_FORMAT_D32_SFLOAT; break;
      }

      VkImageCreateInfo depthImgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
      depthImgCI.imageType = VK_IMAGE_TYPE_2D;
      depthImgCI.format = m_depthFormat;
      depthImgCI.extent = { (uint32_t)w, (uint32_t)h, 1 };
      depthImgCI.mipLevels = 1;
      depthImgCI.arrayLayers = 1;
      depthImgCI.samples = VK_SAMPLE_COUNT_1_BIT;
      depthImgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
      depthImgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      depthImgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      depthImgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

      VmaAllocationCreateInfo depthAllocCI = {};
      depthAllocCI.usage = VMA_MEMORY_USAGE_AUTO;

      VkResult res = vmaCreateImage(allocator, &depthImgCI, &depthAllocCI, &m_depthImage, &m_depthAllocation, nullptr);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] RT depth image creation failed res=%d", res);
        return false;
      }

      VkImageViewCreateInfo divCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
      divCI.image = m_depthImage;
      divCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
      divCI.format = m_depthFormat;
      divCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      divCI.subresourceRange.baseMipLevel = 0;
      divCI.subresourceRange.levelCount = 1;
      divCI.subresourceRange.baseArrayLayer = 0;
      divCI.subresourceRange.layerCount = 1;

      res = vkCreateImageView(device, &divCI, nullptr, &m_depthImageView);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] RT depth image view creation failed res=%d", res);
        return false;
      }

      // Create wrapper Texture for depth attachment
      VulkanTexture* depthTex = new VulkanTexture;
      depthTex->m_image = m_depthImage;
      depthTex->m_imageView = m_depthImageView;
      depthTex->m_format = m_depthFormat;
      depthTex->x = (unsigned int)w;
      depthTex->y = (unsigned int)h;
      depthTex->m_channels = 1;
      depthTex->params = CLAMP_TO_EDGE;
      depthTex->SetTextureParams();
      pDepthTexture = depthTex;
    }

    // ── 3. VkRenderPass ──
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colorRefs;

    for (int i = 0; i < number_RT; i++) {
      VkAttachmentDescription colorAtt = {};
      colorAtt.format = colorFormats[i];
      colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
      colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      colorAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      attachments.push_back(colorAtt);

      VkAttachmentReference ref = {};
      ref.attachment = (uint32_t)i;
      ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      colorRefs.push_back(ref);
    }

    VkAttachmentReference depthRef = {};
    if (hasDepth) {
      VkAttachmentDescription depthAtt = {};
      depthAtt.format = m_depthFormat;
      depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
      depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      attachments.push_back(depthAtt);

      depthRef.attachment = (uint32_t)number_RT;
      depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = (uint32_t)number_RT;
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (hasDepth)
      dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpCI = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpCI.attachmentCount = (uint32_t)attachments.size();
    rpCI.pAttachments = attachments.data();
    rpCI.subpassCount = 1;
    rpCI.pSubpasses = &subpass;
    rpCI.dependencyCount = 1;
    rpCI.pDependencies = &dependency;

    VkResult res = vkCreateRenderPass(device, &rpCI, nullptr, &m_renderPass);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] RT render pass creation failed res=%d", res);
      return false;
    }

    // ── 4. VkFramebuffer ──
    std::vector<VkImageView> fbAttachments;
    for (int i = 0; i < number_RT; i++)
      fbAttachments.push_back(vColorImageViews[i]);
    if (hasDepth)
      fbAttachments.push_back(m_depthImageView);

    VkFramebufferCreateInfo fbCI = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbCI.renderPass = m_renderPass;
    fbCI.attachmentCount = (uint32_t)fbAttachments.size();
    fbCI.pAttachments = fbAttachments.data();
    fbCI.width = (uint32_t)w;
    fbCI.height = (uint32_t)h;
    fbCI.layers = 1;

    res = vkCreateFramebuffer(device, &fbCI, nullptr, &m_framebuffer);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] RT framebuffer creation failed res=%d", res);
      return false;
    }

    // Initialize color images: clear to black then transition to SHADER_READ_ONLY_OPTIMAL
    // so they contain valid data when first sampled (avoids UNDEFINED→read-only warning).
    {
      VkCommandBuffer initCmd;
      VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      cmdAlloc.commandPool = driver->GetTransientCommandPool();
      cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmdAlloc.commandBufferCount = 1;
      vkAllocateCommandBuffers(device, &cmdAlloc, &initCmd);
      VkCommandBufferBeginInfo beginCI = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(initCmd, &beginCI);

      VkClearColorValue clearColor = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
      VkImageSubresourceRange colorRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
      for (int i = 0; i < number_RT; i++) {
        TransitionImageLayout(initCmd, vColorImages[i],
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_ASPECT_COLOR_BIT);
        vkCmdClearColorImage(initCmd, vColorImages[i],
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &colorRange);
        TransitionImageLayout(initCmd, vColorImages[i],
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_ASPECT_COLOR_BIT);
        vColorLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }
      if (hasDepth && m_depthImage) {
        VkClearDepthStencilValue clearDepth = { 1.0f, 0 };
        VkImageSubresourceRange depthRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        TransitionImageLayout(initCmd, m_depthImage,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_ASPECT_DEPTH_BIT);
        vkCmdClearDepthStencilImage(initCmd, m_depthImage,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearDepth, 1, &depthRange);
        TransitionImageLayout(initCmd, m_depthImage,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_ASPECT_DEPTH_BIT);
      }

      vkEndCommandBuffer(initCmd);
      VkSubmitInfo initSubmit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      initSubmit.commandBufferCount = 1;
      initSubmit.pCommandBuffers = &initCmd;
      vkQueueSubmit(driver->GetGraphicsQueue(), 1, &initSubmit, VK_NULL_HANDLE);
      vkQueueWaitIdle(driver->GetGraphicsQueue());
      vkFreeCommandBuffers(device, driver->GetTransientCommandPool(), 1, &initCmd);
    }

    T8_LOG_INFO("[Vulkan] LoadAPIRT OK (%dx%d, %d color, depth=%s)", w, h, number_RT, hasDepth ? "yes" : "no");
    return true;
  }

  void VulkanRT::DestroyAPIRT() {
    auto* driver = GetVkDriver();
    VkDevice device = driver->GetDevice();
    VmaAllocator allocator = driver->GetAllocator();

    if (m_framebuffer) { vkDestroyFramebuffer(device, m_framebuffer, nullptr); m_framebuffer = VK_NULL_HANDLE; }
    if (m_renderPass)  { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }

    // Destroy sampler from color texture wrappers (image/view destroyed below separately)
    for (auto* tex : vColorTextures) {
      if (tex) {
        VulkanTexture* vt = static_cast<VulkanTexture*>(tex);
        if (vt->m_sampler) { vkDestroySampler(device, vt->m_sampler, nullptr); vt->m_sampler = VK_NULL_HANDLE; }
        vt->m_image = VK_NULL_HANDLE;      // prevent double-free
        vt->m_imageView = VK_NULL_HANDLE;
        delete vt;
      }
    }
    vColorTextures.clear();

    for (auto& view : vColorImageViews) vkDestroyImageView(device, view, nullptr);
    for (size_t i = 0; i < vColorImages.size(); i++) vmaDestroyImage(allocator, vColorImages[i], vColorAllocations[i]);
    vColorImageViews.clear();
    vColorImages.clear();
    vColorAllocations.clear();
    vColorLayouts.clear();

    // Destroy depth texture wrapper sampler (image/view destroyed below)
    if (pDepthTexture) {
      VulkanTexture* dt = static_cast<VulkanTexture*>(pDepthTexture);
      if (dt->m_sampler) { vkDestroySampler(device, dt->m_sampler, nullptr); dt->m_sampler = VK_NULL_HANDLE; }
      dt->m_image = VK_NULL_HANDLE;
      dt->m_imageView = VK_NULL_HANDLE;
      delete dt;
      pDepthTexture = nullptr;
    }

    if (m_depthImageView) { vkDestroyImageView(device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
    if (m_depthImage)     { vmaDestroyImage(allocator, m_depthImage, m_depthAllocation); m_depthImage = VK_NULL_HANDLE; }

    for (int i = 0; i < 6; i++) {
      if (m_cubeFaceViews[i]) { vkDestroyImageView(device, m_cubeFaceViews[i], nullptr); m_cubeFaceViews[i] = VK_NULL_HANDLE; }
    }
  }

  void VulkanRT::Set(const DeviceContext& context) {
    auto* driver = GetVkDriver();
    VkCommandBuffer cmd = static_cast<const VulkanDeviceContext*>(&context)->GetCommandBuffer();

    // End any active render pass before starting the RT pass
    driver->EndRenderPassIfActive(cmd);

    // Transition color images to COLOR_ATTACHMENT if needed
    for (int i = 0; i < number_RT; i++) {
      if (vColorLayouts[i] == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        TransitionImageLayout(cmd, vColorImages[i],
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT);
      }
      vColorLayouts[i] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Begin the RT's own render pass with clear values
    bool hasDepth = (depth_format != BaseRT::NOTHING);
    std::vector<VkClearValue> clearValues(number_RT);
    for (int i = 0; i < number_RT; i++)
      clearValues[i].color = { {0.0f, 0.0f, 0.0f, 0.0f} };

    if (hasDepth) {
      VkClearValue depthClear = {};
      depthClear.depthStencil = { 1.0f, 0 };
      clearValues.push_back(depthClear);
    }

    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = m_renderPass;
    rpBegin.framebuffer = m_framebuffer;
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = { (uint32_t)w, (uint32_t)h };
    rpBegin.clearValueCount = (uint32_t)clearValues.size();
    rpBegin.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    driver->SetActiveRenderPass(m_renderPass);
    driver->SetRenderPassActive(true);

    // Set viewport and scissor to RT dimensions (negative height for Y-flip)
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = (float)h;
    viewport.width = (float)w;
    viewport.height = -(float)h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = { (uint32_t)w, (uint32_t)h };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    T8_LOG_TRACE("[Vulkan] RT::Set %dx%d (%d attachments)", w, h, number_RT);
  }

  void VulkanRT::ChangeCubeDepthTexture(int i) {
    T8_LOG_TRACE("[Vulkan] TODO: ChangeCubeDepthTexture(%d)", i);
  }

} // namespace t850

#endif // OS_WINDOWS
