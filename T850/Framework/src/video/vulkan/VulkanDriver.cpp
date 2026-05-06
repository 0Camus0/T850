#include <pch.h>
/*********************************************************
 * T850 Engine — Vulkan Backend
 * VulkanDriver.cpp: Driver lifecycle, command infrastructure,
 *                   pipeline management, rendering operations.
 *********************************************************/

#include <video/vulkan/VulkanDriver.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID)

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#if defined(OS_WINDOWS)
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#endif

#if defined(OS_WINDOWS)
#include <SDL3/SDL_vulkan.h>
#elif defined(OS_ANDROID)
#include <android/native_window.h>
#endif

#include <utils/Log.h>
#include <utils/ShaderDiskCache.h>
#include <utils/SPIRVReflection.h>
#include <debug/Profiler.h>
#include <debug/RenderTrace.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <utility>

namespace t850 {

  namespace {
    std::string GetVulkanDriverCacheSignature(VkPhysicalDevice physicalDevice) {
      std::ostringstream sig;
      sig << "vulkan;shaderCompiler=glslang-hlsl-spv1.0;pipelineCache=1";
      if (!physicalDevice)
        return sig.str();
      VkPhysicalDeviceProperties props = {};
      vkGetPhysicalDeviceProperties(physicalDevice, &props);
      sig << ";deviceName=" << props.deviceName
          << ";vendor=" << props.vendorID
          << ";device=" << props.deviceID
          << ";driver=" << props.driverVersion
          << ";api=" << props.apiVersion;
      return sig.str();
    }
  }

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Pipeline Management & Rendering
  // ══════════════════════════════════════════════════════

  VkPipeline VulkanDriver::GetOrCreatePipeline(VulkanShader* shader, uint8_t numColorAttachments,
                                                VkFormat colorFormat, VkFormat depthFormat) {
    VulkanPipelineKey key = {};
    key.shaderPtr = reinterpret_cast<uintptr_t>(shader);
    key.blend = (uint8_t)m_currentBlend;
    key.depth = (uint8_t)m_currentDepth;
    key.cull = (uint8_t)m_currentCull;
    key.numColorAttachments = numColorAttachments;
    auto* vkContext = static_cast<VulkanDeviceContext*>(T8DeviceContext);
    key.topology = (uint8_t)vkContext->GetTopology();
    if (!shader->m_vertexAttributes.empty()) {
      key.vertexStride = vkContext->GetVertexStride() ? vkContext->GetVertexStride() : shader->m_vertexBinding.stride;
    }
    key.colorFormat = colorFormat;
    key.depthFormat = depthFormat;
    VkRenderPass renderPass = m_backbufferRenderPass;
    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size()) {
      VulkanRT* rt = static_cast<VulkanRT*>(RTs[CurrentRT]);
      renderPass = rt->m_renderPass;
    }
    key.renderPass = VulkanRenderPassKey(renderPass);

    auto it = m_pipelineCache.find(key);
    if (it != m_pipelineCache.end()) {
      T8_LOG_TRACE("[Vulkan] Pipeline cache hit: shader=%p topo=%d blend=%d depth=%d",
                   shader, key.topology, key.blend, key.depth);
      return it->second;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader->m_vertModule;
    stages[0].pName = "VS";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader->m_fragModule;
    stages[1].pName = "FS";

    // Vertex input (use shader's reflection data or empty for now)
    VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkVertexInputBindingDescription vertexBinding = shader->m_vertexBinding;
    vertexBinding.stride = key.vertexStride;
    if (!shader->m_vertexAttributes.empty()) {
      vertexInput.vertexBindingDescriptionCount = 1;
      vertexInput.pVertexBindingDescriptions = &vertexBinding;
      vertexInput.vertexAttributeDescriptionCount = (uint32_t)shader->m_vertexAttributes.size();
      vertexInput.pVertexAttributeDescriptions = shader->m_vertexAttributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = (VkPrimitiveTopology)key.topology;

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
    // Negative viewport height flips winding, so use clockwise front face
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
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
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        break;
      case READ:
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
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
          att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
    // Use canonical backbuffer pass for pipeline creation when on backbuffer;
    // the LOAD variant is render-pass-compatible, so pipelines work with both.
    pipelineCI.renderPass = renderPass;
    pipelineCI.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult res = vkCreateGraphicsPipelines(m_device, m_vkPipelineCache, 1, &pipelineCI, nullptr, &pipeline);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] CreateGraphicsPipelines failed res=%d shader=%p blend=%d depth=%d cull=%d",
                   res, shader, key.blend, key.depth, key.cull);
      return VK_NULL_HANDLE;
    }

    T8_LOG_DEBUG("[Vulkan] Pipeline created: shader=%p blend=%d depth=%d cull=%d topo=%d stride=%u colors=%d renderPass=0x%llx",
           shader, key.blend, key.depth, key.cull, key.topology, key.vertexStride, key.numColorAttachments, key.renderPass);
    m_pipelineCache[key] = pipeline;
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      TracePSORec rec;
      rec.backend                 = "vulkan";
      rec.shader_id               = g_renderTracer->LookupShaderId(shader);
      rec.shader_key_bits         = shader ? shader->key.bits : 0;
      rec.blend                   = key.blend;
      rec.depth                   = key.depth;
      rec.cull                    = key.cull;
      rec.topology                = key.topology;
      rec.num_color_attachments   = key.numColorAttachments;
      rec.color_formats.push_back((uint32_t)key.colorFormat);
      rec.depth_format            = (uint32_t)key.depthFormat;
      rec.vertex_stride           = key.vertexStride;
      rec.render_pass             = key.renderPass;
      g_renderTracer->EvCreatePSO(rec);
    }
#endif
    return pipeline;
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Core lifecycle
  // ══════════════════════════════════════════════════════

  void VulkanDriver::SetWindow(void* window) {
    m_nativeWindow = window;
#if defined(OS_WINDOWS)
    if (!m_nativeWindow) m_nativeWindow = GetActiveWindow();
#endif
    // Surface creation is deferred to InitDriver where the VkInstance is available.
    // The native window pointer is stored for use there.
  }

  void VulkanDriver::SetDimensions(int w, int h) { width = w; height = h; }

  void VulkanDriver::CreateInstance() {
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "T850 Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "T850";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> extensions;
#if defined(OS_WINDOWS)
    // Get SDL-required extensions
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    for (uint32_t i = 0; i < sdlExtCount; i++)
      extensions.push_back(sdlExts[i]);
#elif defined(OS_ANDROID)
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif

#ifdef T8_VULKAN_VALIDATION
    // Check if validation layer is available
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    bool validationAvailable = false;
    for (auto& lp : availableLayers) {
      if (strcmp(lp.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
        validationAvailable = true;
        break;
      }
    }
    const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    if (validationAvailable) {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      T8_LOG_INFO("[Vulkan] Validation layer available — enabling");
    } else {
      T8_LOG_INFO("[Vulkan] Validation layer NOT available — install Vulkan SDK for validation");
    }
#endif

    VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();

#ifdef T8_VULKAN_VALIDATION
    if (validationAvailable) {
      ci.enabledLayerCount = 1;
      ci.ppEnabledLayerNames = validationLayers;

      // Enable synchronization validation to catch barrier/layout hazards
      static VkValidationFeatureEnableEXT enabledFeatures[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
      };
      static VkValidationFeaturesEXT validationFeatures = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
      validationFeatures.enabledValidationFeatureCount = 2;
      validationFeatures.pEnabledValidationFeatures = enabledFeatures;
      ci.pNext = &validationFeatures;
      T8_LOG_INFO("[Vulkan] Synchronization validation + best practices ENABLED");
    }
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

    // Query features before creating device (best practice)
    VkPhysicalDeviceFeatures supportedFeatures = {};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supportedFeatures);

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCI = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueCI.queueFamilyIndex = m_graphicsQueueFamily;
    queueCI.queueCount = 1;
    queueCI.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      VK_KHR_MAINTENANCE1_EXTENSION_NAME,  // negative viewport height for Y-flip
    };

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy ? VK_TRUE : VK_FALSE;

    VkDeviceCreateInfo deviceCI = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceCI.queueCreateInfoCount = 1;
    deviceCI.pQueueCreateInfos = &queueCI;
    deviceCI.enabledExtensionCount = 2;
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
      if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) { chosenMode = mode; break; }
    }
    // Fallback to MAILBOX if IMMEDIATE unavailable
    if (chosenMode == VK_PRESENT_MODE_FIFO_KHR) {
      for (auto mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) { chosenMode = mode; break; }
      }
    }
    T8_LOG_INFO("[Vulkan] Present mode: %d (0=IMMEDIATE,1=MAILBOX,2=FIFO)", (int)chosenMode);

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
    scCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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

    // Create a LOAD variant for restarting the backbuffer pass after RT pops
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[0] = colorAttachment;
    attachments[1] = depthAttachment;
    res = vkCreateRenderPass(m_device, &rpCI, nullptr, &m_backbufferRenderPassLoad);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkCreateRenderPass (load) failed res=%d", res);
    }

    T8_LOG_INFO("[Vulkan] Backbuffer render passes created (clear + load)");
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
    imgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

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
    uint32_t imageCount = (uint32_t)m_swapChainImageViews.size();
    m_backbufferFramebuffers.resize(imageCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < imageCount; i++) {
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
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 },
    };

    VkDescriptorPoolCreateInfo dpCI = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpCI.maxSets = 4096;
    dpCI.poolSizeCount = 2;
    dpCI.pPoolSizes = poolSizes;
    dpCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      VkResult res = vkCreateDescriptorPool(m_device, &dpCI, nullptr, &m_descriptorPools[i]);
      if (res != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] vkCreateDescriptorPool[%u] failed res=%d", i, res);
      }
    }
    T8_LOG_INFO("[Vulkan] Descriptor pools created (%u)", kBackBufferCount);
  }

  void VulkanDriver::InitDriver() {
    T8Device = new VulkanDevice;
    T8DeviceContext = new VulkanDeviceContext;

    CreateInstance();

#ifdef T8_VULKAN_VALIDATION
    // Setup debug messenger for validation layer output
    {
      auto createFunc = (PFN_vkCreateDebugUtilsMessengerEXT)
          vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
      if (createFunc) {
        VkDebugUtilsMessengerCreateInfoEXT dbgCI = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        dbgCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        dbgCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCI.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                    VkDebugUtilsMessageTypeFlagsEXT,
                                    const VkDebugUtilsMessengerCallbackDataEXT* data,
                                    void*) -> VkBool32 {
          if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            T8_LOG_ERROR("[VK_VALIDATION] %s", data->pMessage);
          else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            T8_LOG_INFO("[VK_VALIDATION:WARN] %s", data->pMessage);
          else
            T8_LOG_DEBUG("[VK_VALIDATION] %s", data->pMessage);
          return VK_FALSE;
        };
        createFunc(m_instance, &dbgCI, nullptr, &m_debugMessenger);
        T8_LOG_INFO("[Vulkan] Validation layers enabled with debug messenger");
      }
    }
#endif

    // Create platform surface.
    if (m_nativeWindow && m_instance) {
#if defined(OS_WINDOWS)
      SDL_Window* sdlWin = (SDL_Window*)m_nativeWindow;
      if (!SDL_Vulkan_CreateSurface(sdlWin, m_instance, nullptr, &m_surface)) {
        T8_LOG_ERROR("[Vulkan] SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
      } else {
        T8_LOG_INFO("[Vulkan] Surface created via SDL");
      }
#elif defined(OS_ANDROID)
      VkAndroidSurfaceCreateInfoKHR surfaceCI = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
      surfaceCI.window = static_cast<ANativeWindow*>(m_nativeWindow);
      VkResult surfaceResult = vkCreateAndroidSurfaceKHR(m_instance, &surfaceCI, nullptr, &m_surface);
      if (surfaceResult != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] vkCreateAndroidSurfaceKHR failed res=%d", surfaceResult);
      } else {
        T8_LOG_INFO("[Vulkan] Surface created from ANativeWindow");
      }
#endif
    }

    CreateDevice();
    T8_LOG_INFO("[Vulkan] >> CreateAllocator...");
    CreateAllocator();
    T8_LOG_INFO("[Vulkan] >> CreateCommandInfrastructure...");
    CreateCommandInfrastructure();
    T8_LOG_INFO("[Vulkan] >> CreateSwapChain...");
    CreateSwapChain();
    T8_LOG_INFO("[Vulkan] >> CreateBackBufferViews...");
    CreateBackBufferViews();
    T8_LOG_INFO("[Vulkan] >> CreateRenderPass...");
    CreateRenderPass();
    T8_LOG_INFO("[Vulkan] >> CreateDepthBuffer...");
    CreateDepthBuffer();
    T8_LOG_INFO("[Vulkan] >> CreateFramebuffers...");
    CreateFramebuffers();
    T8_LOG_INFO("[Vulkan] >> CreateSyncObjects...");
    CreateSyncObjects();
    T8_LOG_INFO("[Vulkan] >> CreateDescriptorPool...");
    CreateDescriptorPool();

    // Pipeline cache
    const std::string driverSignature = GetVulkanDriverCacheSignature(m_physicalDevice);
    ShaderDiskCache::EnsureApiMetadata("vulkan", driverSignature);
    std::vector<uint8_t> pipelineCacheBytes;
    ShaderDiskCache::LoadApiArtifact("vulkan", "pipeline_cache.bin", pipelineCacheBytes);
    VkPipelineCacheCreateInfo pcCI = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    pcCI.initialDataSize = pipelineCacheBytes.size();
    pcCI.pInitialData = pipelineCacheBytes.empty() ? nullptr : pipelineCacheBytes.data();
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

    // Negative height flips Y to match D3D/GL NDC (requires VK_KHR_maintenance1)
    m_viewport = { 0.f, (float)height, (float)width, -(float)height, 0.f, 1.f };
    m_scissorRect = { {0, 0}, {(uint32_t)width, (uint32_t)height} };

    CreateDummyTexture();

    T8_LOG_INFO("[Vulkan] Driver initialized (%dx%d)", width, height);
  }

  void VulkanDriver::CreateSurfaces() {}
  void VulkanDriver::DestroySurfaces() {}
  void VulkanDriver::Update() {}

  bool VulkanDriver::ResizeSwapchain(int newW, int newH) {
    if (newW <= 0 || newH <= 0) return false;
    T8_LOG_INFO("[Vulkan] ResizeSwapchain %dx%d -> %dx%d", width, height, newW, newH);

    // Flush GPU: wait for idle AND reset command buffers/descriptor pools
    // so no stale references to resources we're about to destroy
    FlushGPUResources();

    // Destroy old framebuffers, depth, image views
    for (auto& fb : m_backbufferFramebuffers)
      if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_backbufferFramebuffers.clear();

    if (m_depthImageView) { vkDestroyImageView(m_device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
    if (m_depthImage) { vmaDestroyImage(m_allocator, m_depthImage, m_depthAllocation); m_depthImage = VK_NULL_HANDLE; }

    for (auto& iv : m_swapChainImageViews)
      vkDestroyImageView(m_device, iv, nullptr);
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();

    // Destroy old swap chain
    VkSwapchainKHR oldSwapChain = m_swapChain;
    m_swapChain = VK_NULL_HANDLE;
    if (oldSwapChain) vkDestroySwapchainKHR(m_device, oldSwapChain, nullptr);

    // Update dimensions
    width = newW;
    height = newH;

    // Recreate swap chain, views, depth, framebuffers
    CreateSwapChain();
    CreateBackBufferViews();
    CreateDepthBuffer();
    CreateFramebuffers();

    // Update viewport
    m_viewport = { 0.f, (float)height, (float)width, -(float)height, 0.f, 1.f };
    m_scissorRect = { {0, 0}, {(uint32_t)width, (uint32_t)height} };

    // Invalidate pipeline cache entries that reference the old backbuffer render pass
    // (render passes themselves are NOT recreated — same formats, same attachments)

    // Reset frame state
    m_currentFrame = 0;
    m_frameStarted = false;
    m_renderPassActive = false;

    T8_LOG_INFO("[Vulkan] Swapchain recreated (%dx%d)", width, height);
    return true;
  }

  void VulkanDriver::FlushGPUResources() {
    if (!m_device) return;
    WaitForGPU();
    // Reset all command buffers to release references to resources
    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_commandBuffers[i])
        vkResetCommandBuffer(m_commandBuffers[i], 0);
    }
    // Reset descriptor pools to release descriptor set references
    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_descriptorPools[i])
        vkResetDescriptorPool(m_device, m_descriptorPools[i], 0);
    }
    T8_LOG_INFO("[Vulkan] GPU flushed, command buffers and descriptor pools reset");
  }

  void VulkanDriver::DestroyDriver() {
    FlushGPUResources();

    DestroyShaders();
    DestroyRTs();
    DestroyTextures();

    // Destroy pipeline cache entries
    for (auto& pair : m_pipelineCache)
      vkDestroyPipeline(m_device, pair.second, nullptr);
    m_pipelineCache.clear();

    if (m_vkPipelineCache) {
      size_t cacheSize = 0;
      if (vkGetPipelineCacheData(m_device, m_vkPipelineCache, &cacheSize, nullptr) == VK_SUCCESS && cacheSize > 0) {
        std::vector<uint8_t> cacheData(cacheSize);
        if (vkGetPipelineCacheData(m_device, m_vkPipelineCache, &cacheSize, cacheData.data()) == VK_SUCCESS) {
          ShaderDiskCache::StoreApiArtifact("vulkan", "pipeline_cache.bin", cacheData.data(), cacheSize);
        }
      }
      vkDestroyPipelineCache(m_device, m_vkPipelineCache, nullptr);
      m_vkPipelineCache = VK_NULL_HANDLE;
    }

    // Destroy dummy texture
    if (m_dummySampler)   { vkDestroySampler(m_device, m_dummySampler, nullptr); m_dummySampler = VK_NULL_HANDLE; }
    if (m_dummyImageView) { vkDestroyImageView(m_device, m_dummyImageView, nullptr); m_dummyImageView = VK_NULL_HANDLE; }
    if (m_dummyImage)     { vmaDestroyImage(m_allocator, m_dummyImage, m_dummyAllocation); m_dummyImage = VK_NULL_HANDLE; }
    if (m_dummyCubeImageView) { vkDestroyImageView(m_device, m_dummyCubeImageView, nullptr); m_dummyCubeImageView = VK_NULL_HANDLE; }
    if (m_dummyCubeImage) { vmaDestroyImage(m_allocator, m_dummyCubeImage, m_dummyCubeAllocation); m_dummyCubeImage = VK_NULL_HANDLE; }

    // Destroy CB ring buffers
    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_cbRingBuffers[i]) {
        vmaDestroyBuffer(m_allocator, m_cbRingBuffers[i], m_cbRingAllocations[i]);
        m_cbRingBuffers[i] = VK_NULL_HANDLE;
        m_cbRingMapped[i] = nullptr;
      }
    }

    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_descriptorPools[i]) { vkDestroyDescriptorPool(m_device, m_descriptorPools[i], nullptr); m_descriptorPools[i] = VK_NULL_HANDLE; }
    }

    for (uint32_t i = 0; i < kBackBufferCount; i++) {
      if (m_imageAvailableSemaphores[i]) vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
      if (m_renderFinishedSemaphores[i]) vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
      if (m_inFlightFences[i])           vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }

    for (auto& fb : m_backbufferFramebuffers)
      if (fb) { vkDestroyFramebuffer(m_device, fb, nullptr); fb = VK_NULL_HANDLE; }
    m_backbufferFramebuffers.clear();

    if (m_depthImageView) { vkDestroyImageView(m_device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
    if (m_depthImage)     { vmaDestroyImage(m_allocator, m_depthImage, m_depthAllocation); m_depthImage = VK_NULL_HANDLE; }

    if (m_backbufferRenderPass) { vkDestroyRenderPass(m_device, m_backbufferRenderPass, nullptr); m_backbufferRenderPass = VK_NULL_HANDLE; }
    if (m_backbufferRenderPassLoad) { vkDestroyRenderPass(m_device, m_backbufferRenderPassLoad, nullptr); m_backbufferRenderPassLoad = VK_NULL_HANDLE; }

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

#ifdef T8_VULKAN_VALIDATION
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
    {
      T8_PROFILE_CPU_SCOPE(t850::g_profiler, "VK_FenceWait");
      WaitForFence(m_currentFrame);
      vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    }

    if (!IsOffscreenEnabled()) {
      VkResult res = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
                                            m_imageAvailableSemaphores[m_currentFrame],
                                            VK_NULL_HANDLE, &m_imageIndex);
      if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_ERROR_SURFACE_LOST_KHR) {
        T8_LOG_INFO("[Vulkan] Swap chain out of date (res=%d), recreating...", res);
        ResizeSwapchain(width, height);
        // After recreation, fences are re-signaled by WaitForGPU inside ResizeSwapchain.
        // Reset the fence for the current frame before re-acquiring.
        WaitForFence(m_currentFrame);
        vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
        // Re-acquire after recreation
        res = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
                                    m_imageAvailableSemaphores[m_currentFrame],
                                    VK_NULL_HANDLE, &m_imageIndex);
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
          T8_LOG_ERROR("[Vulkan] vkAcquireNextImageKHR failed after recreation res=%d", res);
          m_frameStarted = false;
          return;
        }
      } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        T8_LOG_ERROR("[Vulkan] vkAcquireNextImageKHR failed res=%d", res);
        m_frameStarted = false;
        return;
      }
    }

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    static_cast<VulkanDeviceContext*>(T8DeviceContext)->m_commandBuffer = cmd;

    // Flush profiler query pool reset (must happen before any render pass)
#ifdef T8_ENABLE_PROFILER
    if (t850::g_profiler) t850::g_profiler->FlushVulkanQueryReset(cmd);
#endif

    // Reset per-frame descriptor pool and pending state
    vkResetDescriptorPool(m_device, m_descriptorPools[m_currentFrame], 0);
    m_descriptorSetCache.clear();
    m_cbRingOffset = 0;
    m_cbDirty = false;
    memset(m_pendingTextures, 0, sizeof(m_pendingTextures));

    // Clean up deferred staging buffers from this frame slot (now safe — GPU done with it)
    for (auto& db : m_deferredCleanup[m_currentFrame]) {
      vmaDestroyBuffer(m_allocator, db.buffer, db.alloc);
    }
    m_deferredCleanup[m_currentFrame].clear();

    // Reserve a dummy CB region so draws without explicit CB still have valid descriptors.
    // Every logical CB slot starts at this dummy slice; ConstantBuffer::Set(slot)
    // overwrites only the slot it owns.
    for (auto& cb : m_pendingCBs) {
      cb = {};
      cb.bufferInfo.buffer = m_cbRingBuffers[m_currentFrame];
      cb.bufferInfo.offset = 0;
      cb.bufferInfo.range  = 256;
      cb.tracerId = -1;
    }
    m_cbRingOffset = 256;

    m_lastPipeline = VK_NULL_HANDLE;
    m_lastPipelineLayout = VK_NULL_HANDLE;
    m_screenshotConsumedSemaphore = false;
    m_frameStarted = true;

    // Reset topology to triangle list at the start of each frame
    static_cast<VulkanDeviceContext*>(T8DeviceContext)->m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    static_cast<VulkanDeviceContext*>(T8DeviceContext)->m_vertexStride = 0;
  }

  void VulkanDriver::EndFrame() {}

  VkCommandBuffer VulkanDriver::GetTransientCommandBuffer() {
    VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool = m_transientCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &allocInfo, &cmd);
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
  }

  void VulkanDriver::SubmitTransientCommandBuffer(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &cmd);
  }

  void VulkanDriver::BeginResourceUploadBatch() {
    ++m_uploadBatchDepth;
  }

  void VulkanDriver::EndResourceUploadBatch() {
    if (m_uploadBatchDepth <= 0)
      return;
    --m_uploadBatchDepth;
    if (m_uploadBatchDepth > 0)
      return;

    if (m_uploadBatchCmd) {
      vkEndCommandBuffer(m_uploadBatchCmd);

      VkFence fence = VK_NULL_HANDLE;
      VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
      vkCreateFence(m_device, &fenceInfo, nullptr, &fence);

      VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &m_uploadBatchCmd;
      vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence);
      vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
      vkDestroyFence(m_device, fence, nullptr);
      vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &m_uploadBatchCmd);
      T8_LOG_INFO("[Vulkan] Resource upload batch flushed: %u copy operation(s)", m_uploadBatchCommandCount);
      m_uploadBatchCmd = VK_NULL_HANDLE;
    }

    for (auto& buffer : m_uploadBatchBuffers)
      vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.alloc);
    m_uploadBatchBuffers.clear();
    m_uploadBatchCommandCount = 0;
  }

  VkCommandBuffer VulkanDriver::GetResourceUploadCommandBuffer() {
    if (m_uploadBatchCmd)
      return m_uploadBatchCmd;

    VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool = m_transientCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_uploadBatchCmd) != VK_SUCCESS) {
      m_uploadBatchCmd = VK_NULL_HANDLE;
      return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(m_uploadBatchCmd, &beginInfo) != VK_SUCCESS) {
      vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &m_uploadBatchCmd);
      m_uploadBatchCmd = VK_NULL_HANDLE;
      return VK_NULL_HANDLE;
    }
    return m_uploadBatchCmd;
  }

  void VulkanDriver::KeepResourceUploadBuffer(VkBuffer buffer, VmaAllocation alloc) {
    if (!buffer || !alloc)
      return;
    m_uploadBatchBuffers.push_back({ buffer, alloc });
    ++m_uploadBatchCommandCount;
  }

  void VulkanDriver::DeferCleanup(VkBuffer buffer, VmaAllocation alloc) {
    m_deferredCleanup[m_currentFrame].push_back({ buffer, alloc });
  }

  void VulkanDriver::BuildPipelineObjects() {
    T8_LOG_INFO("[Vulkan] BuildPipelineObjects");
  }

  void VulkanDriver::Clear() {
    if (!m_frameStarted) {
      BeginFrame();
      m_frameStarted = true;
    }

    if ((CurrentRT < 0 || IsCurrentOffscreenTarget()) && BindOffscreenTarget(true))
      return;

    if (CurrentRT < 0) {
      VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

      // End any active render pass before starting a new one
      EndRenderPassIfActive(cmd);

      VkClearValue clearValues[2] = {};
      clearValues[0].color = { {0.9f, 0.9f, 0.9f, 1.0f} };
      clearValues[1].depthStencil = { 0.0f, 0 };

      VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
      rpBegin.renderPass = m_backbufferRenderPass;
      rpBegin.framebuffer = m_backbufferFramebuffers[m_imageIndex];
      rpBegin.renderArea.offset = { 0, 0 };
      rpBegin.renderArea.extent = m_swapChainExtent;
      rpBegin.clearValueCount = 2;
      rpBegin.pClearValues = clearValues;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      m_activeRenderPass = m_backbufferRenderPass;
      m_renderPassActive = true;

      vkCmdSetViewport(cmd, 0, 1, &m_viewport);
      vkCmdSetScissor(cmd, 0, 1, &m_scissorRect);
      T8_TRACE(EvClearRT(-1, 1u | 2u, 0.9f, 0.9f, 0.9f, 1.0f, 0.0f, 0));
    }
  }

  void VulkanDriver::EnsureBackbufferRenderPass() {
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    if (!m_renderPassActive) {
      // Begin the backbuffer render pass with LOAD_OP_LOAD (preserve existing content)
      VkRenderPassBeginInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
      rpInfo.renderPass  = m_backbufferRenderPassLoad;
      rpInfo.framebuffer = m_backbufferFramebuffers[m_imageIndex];
      rpInfo.renderArea  = m_scissorRect;
      vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
      m_renderPassActive = true;
      m_activeRenderPass = m_backbufferRenderPassLoad;
      vkCmdSetViewport(cmd, 0, 1, &m_viewport);
      vkCmdSetScissor(cmd, 0, 1, &m_scissorRect);
    }
  }

  void VulkanDriver::SwapBuffers() {
    T8_LOG_TRACE("[Vulkan] SwapBuffers");
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

    if (IsOffscreenEnabled()) {
      if (m_renderPassActive) {
        vkCmdEndRenderPass(cmd);
        m_renderPassActive = false;
      }

      VkResult endRes = vkEndCommandBuffer(cmd);
      if (endRes != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] vkEndCommandBuffer failed res=%d", endRes);
      }

      VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &cmd;

      VkResult submitRes = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
      if (submitRes != VK_SUCCESS) {
        T8_LOG_ERROR("[Vulkan] offscreen vkQueueSubmit failed res=%d", submitRes);
        if (submitRes == VK_ERROR_DEVICE_LOST) {
          T8_LOG_ERROR("[Vulkan] Device lost! Waiting for idle...");
          vkDeviceWaitIdle(m_device);
        }
      }

      m_frameStarted = false;
      CompleteOffscreenFrame();
      m_currentFrame = (m_currentFrame + 1) % kBackBufferCount;
      return;
    }

    // End the render pass if one is active
    if (m_renderPassActive) {
      vkCmdEndRenderPass(cmd);
      m_renderPassActive = false;
    }

    // Transition backbuffer from COLOR_ATTACHMENT → PRESENT_SRC for presentation
    TransitionImageLayout(cmd, m_swapChainImages[m_imageIndex],
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_ASPECT_COLOR_BIT);

    // End command buffer
    VkResult endRes = vkEndCommandBuffer(cmd);
    if (endRes != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkEndCommandBuffer failed res=%d", endRes);
    }

    // Submit
    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = m_screenshotConsumedSemaphore ? 0 : 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitRes = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);
    if (submitRes != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkQueueSubmit failed res=%d", submitRes);
      if (submitRes == VK_ERROR_DEVICE_LOST) {
        T8_LOG_ERROR("[Vulkan] Device lost! Waiting for idle...");
        vkDeviceWaitIdle(m_device);
      }
    }

    // Present
    VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_imageIndex;

    VkResult presentRes = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_ERROR_SURFACE_LOST_KHR ||
        presentRes == VK_SUBOPTIMAL_KHR) {
      T8_LOG_INFO("[Vulkan] Present: swap chain needs recreation (res=%d)", presentRes);
      // Swapchain will be recreated on next BeginFrame's acquire
    } else if (presentRes != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] vkQueuePresentKHR failed res=%d", presentRes);
    }

    m_currentFrame = (m_currentFrame + 1) % kBackBufferCount;
    m_frameStarted = false;
  }

  void VulkanDriver::SetBlendState(BlendStates state) {
    T8_LOG_TRACE("[Vulkan] SetBlendState(%d)", state);
    m_currentBlend = state;
    T8_TRACE(EvSetBlend((int)state));
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

  void VulkanDriver::SetDepthStencilState(DepthStencilStates state) {
    T8_LOG_TRACE("[Vulkan] SetDepthStencilState(%d)", state);
    m_currentDepth = state;
    T8_TRACE(EvSetDepth((int)state));
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

  void VulkanDriver::SetCullFace(FaceCulling state) {
    T8_LOG_TRACE("[Vulkan] SetCullFace(%d)", state);
    m_currentCull = state;
    m_FaceCulling = state;
    T8_TRACE(EvSetCull((int)state));
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

#ifdef T850_RENDER_TRACE
  void VulkanDriver::RefreshTracePendingRenderState() {
    if (!T8_TRACE_ACTIVE()) return;
    int numAtt = 1;
    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size() && RTs[CurrentRT])
      numAtt = RTs[CurrentRT]->number_RT > 0 ? RTs[CurrentRT]->number_RT : 1;
    g_renderTracer->RecomputePendingRenderStateVulkan(numAtt);
  }
#endif

  void VulkanDriver::PopRT() {
    T8_LOG_TRACE("[Vulkan] PopRT (CurrentRT=%d)", CurrentRT);
    T8_TRACE(EvPopRT());

    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size() && RTs[CurrentRT]) {
      VulkanRT* rt = static_cast<VulkanRT*>(RTs[CurrentRT]);
      VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];

      // 1. End the current RT's render pass (guarded)
      if (m_renderPassActive) {
        vkCmdEndRenderPass(cmd);
        m_renderPassActive = false;
      }

      // 2. Transition color images from COLOR_ATTACHMENT → SHADER_READ_ONLY
      for (int i = 0; i < rt->number_RT; i++) {
        TransitionImageLayout(cmd, rt->vColorImages[i],
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_ASPECT_COLOR_BIT);
        rt->vColorLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }

      // 3. Transition depth from DEPTH_STENCIL_ATTACHMENT → SHADER_READ_ONLY
      bool hasDepth = (rt->depth_format != BaseRT::NOTHING);
      if (hasDepth && rt->m_depthImage) {
        TransitionImageLayout(cmd, rt->m_depthImage,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_IMAGE_ASPECT_DEPTH_BIT);
        rt->m_depthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }

      if (IsOffscreenEnabled()) {
        CurrentRT = -1;
#ifdef T850_RENDER_TRACE
        RefreshTracePendingRenderState();
#endif
        return;
      }

      // 4. Restore the backbuffer render pass (LOAD variant to preserve content)
      VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
      rpBegin.renderPass = m_backbufferRenderPassLoad;
      rpBegin.framebuffer = m_backbufferFramebuffers[m_imageIndex];
      rpBegin.renderArea.offset = { 0, 0 };
      rpBegin.renderArea.extent = m_swapChainExtent;
      rpBegin.clearValueCount = 0;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      m_activeRenderPass = m_backbufferRenderPassLoad;
      m_renderPassActive = true;

      vkCmdSetViewport(cmd, 0, 1, &m_viewport);
      vkCmdSetScissor(cmd, 0, 1, &m_scissorRect);
    }

    CurrentRT = -1;
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

  void VulkanDriver::SaveScreenshot(std::string path) {
    // End the current render pass if active
    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    if (m_renderPassActive) {
      vkCmdEndRenderPass(cmd);
      m_renderPassActive = false;
    }

    // Close and submit current command buffer, wait for GPU
    // Must wait on image-available semaphore (first submit using this swapchain image)
    vkEndCommandBuffer(cmd);
    VkSemaphore waitSem[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStage[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = m_screenshotConsumedSemaphore ? 0 : 1;
    submitInfo.pWaitSemaphores = waitSem;
    submitInfo.pWaitDstStageMask = waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    m_screenshotConsumedSemaphore = true;

    // Get swap chain image
    VkImage srcImage = m_swapChainImages[m_imageIndex];
    uint32_t w = (uint32_t)width;
    uint32_t h = (uint32_t)height;

    // Create staging buffer for readback
    VkDeviceSize bufSize = w * h * 4;
    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = bufSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer stagingBuf;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingAllocInfo;
    vmaCreateBuffer(m_allocator, &bufInfo, &allocCI, &stagingBuf, &stagingAlloc, &stagingAllocInfo);

    // Record copy command
    VkCommandBuffer copyCmd;
    VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = m_transientCommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_device, &cmdAlloc, &copyCmd);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(copyCmd, &beginInfo);

    // Transition swapchain image to TRANSFER_SRC (image is in COLOR_ATTACHMENT after render pass)
    TransitionImageLayout(copyCmd, srcImage,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { w, h, 1 };
    vkCmdCopyImageToBuffer(copyCmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuf, 1, &region);

    // Transition back to COLOR_ATTACHMENT (we restart the render pass next)
    TransitionImageLayout(copyCmd, srcImage,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_COLOR_BIT);

    vkEndCommandBuffer(copyCmd);
    VkSubmitInfo copySubmit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    copySubmit.commandBufferCount = 1;
    copySubmit.pCommandBuffers = &copyCmd;
    vkQueueSubmit(m_graphicsQueue, 1, &copySubmit, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    // Read pixels and write PPM
    const uint8_t* pixels = (const uint8_t*)stagingAllocInfo.pMappedData;
    std::vector<uint8_t> rgb(w * h * 3);
    for (uint32_t i = 0; i < w * h; i++) {
      rgb[i * 3 + 0] = pixels[i * 4 + 0]; // R (or B if BGRA)
      rgb[i * 3 + 1] = pixels[i * 4 + 1]; // G
      rgb[i * 3 + 2] = pixels[i * 4 + 2]; // B (or R if BGRA)
    }

    // Check if format is BGRA and swap
    if (m_swapChainFormat == VK_FORMAT_B8G8R8A8_UNORM || m_swapChainFormat == VK_FORMAT_B8G8R8A8_SRGB) {
      for (uint32_t i = 0; i < w * h; i++) {
        std::swap(rgb[i * 3 + 0], rgb[i * 3 + 2]);
      }
    }

    std::ofstream out(path + ".ppm", std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    out.write((const char*)rgb.data(), rgb.size());
    T8_LOG_INFO("[Vulkan] Saved %s.ppm (%ux%u)", path.c_str(), w, h);

    vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &copyCmd);
    vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);

    // Reopen command buffer for continued rendering
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo);

    // Restart backbuffer render pass
    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = m_backbufferRenderPass;
    rpBegin.framebuffer = m_backbufferFramebuffers[m_imageIndex];
    rpBegin.renderArea.extent = { (uint32_t)width, (uint32_t)height };
    VkClearValue clears[2] = {};
    clears[0].color = {{0.9f, 0.9f, 0.9f, 1.0f}};
    clears[1].depthStencil = {0.0f, 0};
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues = clears;
    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    m_renderPassActive = true;
    m_activeRenderPass = m_backbufferRenderPass;
  }

  void VulkanDriver::SaveRTToFile(int rtID, int attachment, std::string path) {
    if (rtID < 0 || rtID >= (int)RTs.size() || !RTs[rtID]) return;
    VulkanRT* rt = static_cast<VulkanRT*>(RTs[rtID]);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    const bool frameOpen = m_frameStarted;

    if (frameOpen) {
      // End any active render pass before readback.
      if (m_renderPassActive) {
        vkCmdEndRenderPass(cmd);
        m_renderPassActive = false;
      }

      // Flush command buffer.
      vkEndCommandBuffer(cmd);
      VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &cmd;
      vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
      vkQueueWaitIdle(m_graphicsQueue);
    } else {
      vkQueueWaitIdle(m_graphicsQueue);
    }

    VkImage srcImage = VK_NULL_HANDLE;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t w = (uint32_t)rt->w;
    uint32_t h = (uint32_t)rt->h;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    if (attachment == DEPTH_ATTACHMENT) {
      if (!rt->m_depthImage) goto reopen;
      srcImage = rt->m_depthImage;
      fmt = rt->m_depthFormat;
      srcLayout = rt->m_depthLayout;
      aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else if (attachment >= 0 && attachment < rt->number_RT) {
      srcImage = rt->vColorImages[attachment];
      fmt = (attachment < (int)rt->m_colorFormats.size()) ? rt->m_colorFormats[attachment] : rt->m_colorFormat;
      srcLayout = (attachment < (int)rt->vColorLayouts.size()) ? rt->vColorLayouts[attachment] : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else {
      goto reopen;
    }

    {
      // Determine bytes per pixel from format
      uint32_t bpp = 4;
      bool isFloat16 = (fmt == VK_FORMAT_R16_SFLOAT || fmt == VK_FORMAT_R16G16B16A16_SFLOAT);
      bool isFloat32 = (fmt == VK_FORMAT_R32_SFLOAT || fmt == VK_FORMAT_D32_SFLOAT);
      bool isR8 = (fmt == VK_FORMAT_R8_UNORM);
      if (isFloat16) bpp = (fmt == VK_FORMAT_R16_SFLOAT) ? 2 : 8;
      else if (isFloat32) bpp = 4;
      else if (isR8) bpp = 1;

      VkDeviceSize bufSize = (VkDeviceSize)w * h * bpp;
      VkBufferCreateInfo bufCI = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      bufCI.size = bufSize;
      bufCI.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      VmaAllocationCreateInfo allocCI = {};
      allocCI.usage = VMA_MEMORY_USAGE_AUTO;
      allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
      VkBuffer stagingBuf;
      VmaAllocation stagingAlloc;
      VmaAllocationInfo stagingInfo;
      vmaCreateBuffer(m_allocator, &bufCI, &allocCI, &stagingBuf, &stagingAlloc, &stagingInfo);

      // Record copy
      VkCommandBuffer copyCmd;
      VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      cmdAlloc.commandPool = m_transientCommandPool;
      cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cmdAlloc.commandBufferCount = 1;
      vkAllocateCommandBuffers(m_device, &cmdAlloc, &copyCmd);
      VkCommandBufferBeginInfo beginCI = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(copyCmd, &beginCI);

      TransitionImageLayout(copyCmd, srcImage, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, aspect);

      VkBufferImageCopy region = {};
      region.imageSubresource.aspectMask = aspect;
      region.imageSubresource.layerCount = 1;
      region.imageExtent = { w, h, 1 };
      vkCmdCopyImageToBuffer(copyCmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuf, 1, &region);

      TransitionImageLayout(copyCmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcLayout, aspect);

      vkEndCommandBuffer(copyCmd);
      VkSubmitInfo copySubmit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      copySubmit.commandBufferCount = 1;
      copySubmit.pCommandBuffers = &copyCmd;
      vkQueueSubmit(m_graphicsQueue, 1, &copySubmit, VK_NULL_HANDLE);
      vkQueueWaitIdle(m_graphicsQueue);

      // Convert to RGB PPM
      const uint8_t* pixels = (const uint8_t*)stagingInfo.pMappedData;
      std::vector<uint8_t> rgb(w * h * 3);
      // IEEE 754 binary16 → float (matches D3D11/D3D12 PPM dump path so cross-API
      // diffs reflect actual shader output, not dump-format mismatch).
      auto h2f = [](uint16_t h) -> float {
        uint32_t sign = (h >> 15) & 1; uint32_t exp = (h >> 10) & 0x1F; uint32_t mant = h & 0x3FF;
        if (exp == 0) return sign ? -0.0f : 0.0f;
        if (exp == 31) return sign ? -1e30f : 1e30f;
        float f = ((float)mant / 1024.0f + 1.0f) * ldexpf(1.0f, (int)exp - 15);
        return sign ? -f : f;
      };
      for (uint32_t i = 0; i < w * h; i++) {
        if (bpp == 4 && !isFloat32) {
          rgb[i*3+0] = pixels[i*4+0];
          rgb[i*3+1] = pixels[i*4+1];
          rgb[i*3+2] = pixels[i*4+2];
        } else if (isFloat16 && bpp == 8) {
          // RGBA16F → uint8 via IEEE 754 half-float decode
          const uint16_t* fp = (const uint16_t*)(pixels + i * 8);
          for (int c = 0; c < 3; c++) {
            float v = h2f(fp[c]);
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            rgb[i*3+c] = (uint8_t)(v * 255.0f);
          }
        } else if (isR8) {
          rgb[i*3+0] = rgb[i*3+1] = rgb[i*3+2] = pixels[i];
        } else if (isFloat32) {
          const float* fp = (const float*)(pixels + i * 4);
          float v = *fp; v = v < 0 ? 0 : (v > 1 ? 1 : v);
          uint8_t b = (uint8_t)(v * 255.0f);
          rgb[i*3+0] = rgb[i*3+1] = rgb[i*3+2] = b;
        } else if (isFloat16 && bpp == 2) {
          // R16_SFLOAT → uint8 via IEEE 754 half-float decode (NOT uint16 normalized).
          // The previous (float)(*fp)/65535.0f path treated half-float bytes as a
          // normalized integer, which silently misinterpreted negative half-floats
          // (e.g. log(luminance) < 0) as ~0.5+ on the PPM, producing a fake "Vulkan
          // LuminanceMap is 10x brighter than D3D12" signal during cross-API diffs.
          const uint16_t* fp = (const uint16_t*)(pixels + i * 2);
          float v = h2f(*fp);
          v = v < 0 ? 0 : (v > 1 ? 1 : v);
          uint8_t b = (uint8_t)(v * 255.0f);
          rgb[i*3+0] = rgb[i*3+1] = rgb[i*3+2] = b;
        }
      }

      // BGRA swap for backbuffer format
      if (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB) {
        for (uint32_t i = 0; i < w * h; i++)
          std::swap(rgb[i*3+0], rgb[i*3+2]);
      }

      std::ofstream out(path + ".ppm", std::ios::binary);
      out << "P6\n" << w << " " << h << "\n255\n";
      out.write((const char*)rgb.data(), rgb.size());
      T8_LOG_INFO("[Vulkan] Saved %s.ppm (%ux%u fmt=%d)", path.c_str(), w, h, (int)fmt);

      vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &copyCmd);
      vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);
    }

reopen:
    if (!frameOpen)
      return;

    // Reopen command buffer
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo);

    if (IsOffscreenEnabled()) {
      BindOffscreenTarget(false);
      return;
    }

    // Restart backbuffer render pass (LOAD to preserve)
    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = m_backbufferRenderPassLoad;
    rpBegin.framebuffer = m_backbufferFramebuffers[m_imageIndex];
    rpBegin.renderArea.extent = m_swapChainExtent;
    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    m_renderPassActive = true;
    m_activeRenderPass = m_backbufferRenderPassLoad;

    vkCmdSetViewport(m_commandBuffers[m_currentFrame], 0, 1, &m_viewport);
    vkCmdSetScissor(m_commandBuffers[m_currentFrame], 0, 1, &m_scissorRect);
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
    memcpy(stagingAllocInfo.pMappedData, data, static_cast<size_t>(dataSize));

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
      // Wrapping mid-frame would overwrite UBO data still being read by earlier
      // draws in the same command buffer (descriptor sets point at fixed offsets
      // via dynamic offset). The result looks like flicker / z-fighting because
      // shaders read partially-stale matrices. Crash loud so we can grow the
      // ring rather than corrupt rendering silently.
      T8_LOG_ERROR("[Vulkan] CB ring buffer overflow! offset=%u + size=%u > %u (peak so far=%u)",
                   m_cbRingOffset, alignedSize, kCBRingBufferSize, m_cbRingPeakUsage);
      assert(false && "Vulkan CB ring buffer overflow — increase kCBRingBufferSize");
      // Fail-stop in release builds: return a dummy descriptor pointing into the
      // reserved 0..256 dummy region. The draw will be visually wrong but we
      // won't trash earlier draws in the same frame.
      VkDescriptorBufferInfo info = {};
      info.buffer = m_cbRingBuffers[m_currentFrame];
      info.offset = 0;
      info.range  = (alignedSize <= 256) ? alignedSize : 256;
      return info;
    }

    uint32_t bufIdx = m_currentFrame;
    unsigned char* dst = (unsigned char*)m_cbRingMapped[bufIdx] + m_cbRingOffset;
    memcpy(dst, data, dataSize);

    VkDescriptorBufferInfo info = {};
    info.buffer = m_cbRingBuffers[bufIdx];
    info.offset = m_cbRingOffset;
    info.range = alignedSize;

    m_cbRingOffset += alignedSize;
    if (m_cbRingOffset > m_cbRingPeakUsage) m_cbRingPeakUsage = m_cbRingOffset;
    return info;
  }

  VulkanDriver::VBRingAlloc VulkanDriver::AllocateVBRing(const void* data, uint32_t size) {
    // Must align to 256 so subsequent UBO allocations from the same ring stay aligned
    uint32_t aligned = (size + 255) & ~255u;
    if (m_cbRingOffset + aligned > kCBRingBufferSize) {
      T8_LOG_ERROR("[Vulkan] CB ring buffer overflow in VB path! offset=%u + size=%u > %u (peak so far=%u)",
                   m_cbRingOffset, aligned, kCBRingBufferSize, m_cbRingPeakUsage);
      assert(false && "Vulkan CB ring buffer overflow (VB path) — increase kCBRingBufferSize");
      return { VK_NULL_HANDLE, 0, false };
    }
    uint32_t bufIdx = m_currentFrame;
    unsigned char* dst = (unsigned char*)m_cbRingMapped[bufIdx] + m_cbRingOffset;
    memcpy(dst, data, size);
    VBRingAlloc alloc;
    alloc.buffer = m_cbRingBuffers[bufIdx];
    alloc.offset = m_cbRingOffset;
    alloc.valid = true;
    m_cbRingOffset += aligned;
    if (m_cbRingOffset > m_cbRingPeakUsage) m_cbRingPeakUsage = m_cbRingOffset;
    return alloc;
  }

  void VulkanDriver::BindBackBufferNoDepth() {
    T8_LOG_TRACE("[Vulkan] BindBackBufferNoDepth");
    // In Vulkan, depth test is controlled by PSO state (NONE), so this is a no-op.
    // The depth attachment remains bound but the pipeline disables depth testing.
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Descriptor set allocation & binding
  // ══════════════════════════════════════════════════════

  VkDescriptorSet VulkanDriver::AllocateDescriptorSet(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = m_descriptorPools[m_currentFrame];
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet ds = VK_NULL_HANDLE;
    VkResult res = vkAllocateDescriptorSets(m_device, &allocInfo, &ds);
    if (res != VK_SUCCESS) {
      T8_LOG_ERROR("[Vulkan] AllocateDescriptorSets failed res=%d", res);
      return VK_NULL_HANDLE;
    }
    return ds;
  }

  void VulkanDriver::BindPendingDescriptors(VkCommandBuffer cmd, VulkanShader* shader) {
    std::vector<std::pair<int, int>> cbSlots; // descriptor binding -> logical slot
    cbSlots.reserve(VulkanShader::kMaxCBufferSlots);
    for (int slot = 0; slot < VulkanShader::kMaxCBufferSlots; slot++) {
      int binding = shader->cbvBindings[slot];
      if (binding >= 0) cbSlots.emplace_back(binding, slot);
    }
    std::sort(cbSlots.begin(), cbSlots.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<uint32_t> dynamicOffsets;
    dynamicOffsets.reserve(cbSlots.size());
    for (const auto& cb : cbSlots) {
      const int slot = cb.second;
      dynamicOffsets.push_back((uint32_t)m_pendingCBs[slot].bufferInfo.offset);
    }

    // Compute a fingerprint from layout + texture bindings (image view AND sampler)
    // to reuse descriptor sets when the same textures+samplers are bound across
    // multiple draws (e.g. same material, different passes).
    //
    // IMPORTANT: sampler must be part of the fingerprint. The descriptor type is
    // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, so two draws that bind the same
    // imageView with different samplers MUST get different descriptor sets.
    // Without sampler in the key, the second draw would silently reuse the first
    // draw's descriptor (with the wrong sampler) — visible as e.g. shadow-edge
    // flicker when the same depth texture is sampled with different filter modes
    // across passes.
    uint64_t fingerprint = (uint64_t)(uintptr_t)shader->m_descriptorSetLayout;
    for (int i = 0; i < VulkanShader::kMaxTextureSlots; i++) {
      if (m_pendingTextures[i].imageView) {
        fingerprint ^= ((uint64_t)(uintptr_t)m_pendingTextures[i].imageView) * (0x9e3779b97f4a7c15ULL + i);
        fingerprint ^= ((uint64_t)(uintptr_t)m_pendingTextures[i].sampler)   * (0xc6a4a7935bd1e995ULL + i);
      }
    }
    for (const auto& cb : cbSlots) {
      const int binding = cb.first;
      const int slot = cb.second;
      const VkDescriptorBufferInfo& info = m_pendingCBs[slot].bufferInfo;
      fingerprint ^= ((uint64_t)(uintptr_t)info.buffer) * (0x94d049bb133111ebULL + binding);
      fingerprint ^= ((uint64_t)info.range)             * (0xbf58476d1ce4e5b9ULL + binding);
    }

    auto it = m_descriptorSetCache.find(fingerprint);
    VkDescriptorSet ds;

    if (it != m_descriptorSetCache.end()) {
      ds = it->second;
    } else {
      ds = AllocateDescriptorSet(shader->m_descriptorSetLayout);
      if (!ds) return;

      std::vector<VkWriteDescriptorSet> writes;
      std::vector<VkDescriptorImageInfo> imageInfos;
      std::vector<VkDescriptorBufferInfo> bufferInfos;
      writes.reserve(cbSlots.size() + VulkanShader::kMaxTextureSlots);
      imageInfos.reserve(VulkanShader::kMaxTextureSlots);
      bufferInfos.reserve(cbSlots.size());

      // UBO bindings — offset=0 in descriptor, actual offsets passed as dynamic offsets
      for (const auto& cb : cbSlots) {
        const int binding = cb.first;
        const int slot = cb.second;
        VkDescriptorBufferInfo cbBufInfo = m_pendingCBs[slot].bufferInfo;
        cbBufInfo.offset = 0;
        bufferInfos.push_back(cbBufInfo);

        VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = ds;
        w.dstBinding = (uint32_t)binding;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        w.pBufferInfo = &bufferInfos.back();
        writes.push_back(w);
      }

      // Texture bindings (from reflected srv bindings)
      for (int slot = 0; slot < VulkanShader::kMaxTextureSlots; slot++) {
        if (shader->srvBindings[slot] < 0) continue;

        VkDescriptorImageInfo imgInfo = {};
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // Use cubemap dummy for slots that expect cubemap views
        VkImageView defaultView = shader->srvIsCubemap[slot] ? m_dummyCubeImageView : m_dummyImageView;
        imgInfo.imageView = m_pendingTextures[slot].imageView ? m_pendingTextures[slot].imageView : defaultView;
        imgInfo.sampler   = m_pendingTextures[slot].sampler   ? m_pendingTextures[slot].sampler   : m_dummySampler;

        imageInfos.push_back(imgInfo);

        VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = ds;
        w.dstBinding = (uint32_t)shader->srvBindings[slot];
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &imageInfos.back();
        writes.push_back(w);
      }

      if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
      }

      m_descriptorSetCache[fingerprint] = ds;
    }

    // Always rebind with current dynamic UBO offset
    uint32_t dynOffsetCount = (uint32_t)dynamicOffsets.size();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            shader->m_pipelineLayout, 0, 1, &ds,
                            dynOffsetCount, dynamicOffsets.empty() ? nullptr : dynamicOffsets.data());
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      // Emit one commit event per slot the shader actually consumes — this
      // reflects what the GPU will see (including dummy-image fallbacks for
      // unbound slots, which is exactly the kind of mismatch we need to
      // catch when comparing two API traces).
      for (int slot = 0; slot < VulkanShader::kMaxTextureSlots; slot++) {
        if (shader->srvBindings[slot] < 0) continue;
        bool hasUserBind = (m_pendingTextures[slot].imageView != VK_NULL_HANDLE);
        int  texId       = hasUserBind ? m_pendingTextures[slot].tracerTexId : -1;
        const char* nm   = hasUserBind && m_pendingTextures[slot].tracerName[0]
                            ? m_pendingTextures[slot].tracerName : "<dummy>";
        const char* stage = hasUserBind && m_pendingTextures[slot].tracerStage[0]
                            ? m_pendingTextures[slot].tracerStage : "ps";
        // viewId reuses the imageView raw pointer cast to int — not a stable
        // resource id but unique enough to flag mismatches between two API
        // traces (different VkImageView pointers => different views).
        int viewId    = (int)(uintptr_t)m_pendingTextures[slot].imageView;
        // samplerId now points to a logical signature so cross-API trace
        // diffs surface real mismatches; falls back to the VkSampler raw
        // pointer for unbound/dummy slots.
        int samplerId = hasUserBind && m_pendingTextures[slot].tracerSamplerId >= 0
                          ? m_pendingTextures[slot].tracerSamplerId
                          : (int)(uintptr_t)m_pendingTextures[slot].sampler;
        g_renderTracer->EvBindTextureCommit(slot, texId, viewId, samplerId, nm ? nm : "", stage);
      }
      // Commit each logical cbuffer slot the shader actually consumes.
      for (const auto& cb : cbSlots) {
        const int slot = cb.second;
        if (m_pendingCBs[slot].tracerId >= 0) {
          g_renderTracer->EvBindCBufferCommit(slot, m_pendingCBs[slot].tracerId);
        }
      }
    }
#endif
  }

  // ══════════════════════════════════════════════════════
  //  VulkanDriver — Dummy texture for unbound slots
  // ══════════════════════════════════════════════════════

  void VulkanDriver::CreateDummyTexture() {
    // 1x1 white pixel
    uint8_t pixel[4] = { 255, 255, 255, 255 };

    VkImageCreateInfo imgCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgCI.imageType = VK_IMAGE_TYPE_2D;
    imgCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgCI.extent = { 1, 1, 1 };
    imgCI.mipLevels = 1;
    imgCI.arrayLayers = 1;
    imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;

    vmaCreateImage(m_allocator, &imgCI, &allocCI, &m_dummyImage, &m_dummyAllocation, nullptr);

    // Upload pixel via staging buffer
    VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = 4;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocCI = {};
    stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingAllocInfo;
    vmaCreateBuffer(m_allocator, &stagingInfo, &stagingAllocCI, &stagingBuffer, &stagingAlloc, &stagingAllocInfo);
    memcpy(stagingAllocInfo.pMappedData, pixel, 4);

    VkCommandBufferAllocateInfo cmdAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = m_transientCommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    TransitionImageLayout(cmd, m_dummyImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { 1, 1, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_dummyImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(cmd, m_dummyImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_ASPECT_COLOR_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &cmd);
    vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAlloc);

    // Image view
    VkImageViewCreateInfo ivCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivCI.image = m_dummyImage;
    ivCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivCI.subresourceRange.levelCount = 1;
    ivCI.subresourceRange.layerCount = 1;
    vkCreateImageView(m_device, &ivCI, nullptr, &m_dummyImageView);

    // Sampler
    VkSamplerCreateInfo sampCI = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampCI.magFilter = VK_FILTER_NEAREST;
    sampCI.minFilter = VK_FILTER_NEAREST;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(m_device, &sampCI, nullptr, &m_dummySampler);

    // Dummy cubemap (1x1x6 faces) for unbound cubemap slots (e.g. texEnv)
    VkImageCreateInfo cubeCI = imgCI;
    cubeCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cubeCI.arrayLayers = 6;
    VkImage dummyCubeImage = VK_NULL_HANDLE;
    VmaAllocation dummyCubeAlloc = VK_NULL_HANDLE;
    vmaCreateImage(m_allocator, &cubeCI, &allocCI, &dummyCubeImage, &dummyCubeAlloc, nullptr);

    // Transition cube to SHADER_READ_ONLY (clear first to avoid UNDEFINED→read-only warning)
    {
      VkCommandBuffer cubeCmd;
      vkAllocateCommandBuffers(m_device, &cmdAlloc, &cubeCmd);
      vkBeginCommandBuffer(cubeCmd, &beginInfo);

      VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
      barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.image = dummyCubeImage;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = 6;
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cubeCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                           0, nullptr, 0, nullptr, 1, &barrier);

      VkClearColorValue clearColor = {{ 0.0f, 0.0f, 0.0f, 1.0f }};
      VkImageSubresourceRange clearRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
      vkCmdClearColorImage(cubeCmd, dummyCubeImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);

      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cubeCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                           0, nullptr, 0, nullptr, 1, &barrier);

      vkEndCommandBuffer(cubeCmd);
      VkSubmitInfo cubeSubmit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      cubeSubmit.commandBufferCount = 1;
      cubeSubmit.pCommandBuffers = &cubeCmd;
      vkQueueSubmit(m_graphicsQueue, 1, &cubeSubmit, VK_NULL_HANDLE);
      vkQueueWaitIdle(m_graphicsQueue);
      vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &cubeCmd);
    }

    VkImageViewCreateInfo cubeIvCI = ivCI;
    cubeIvCI.image = dummyCubeImage;
    cubeIvCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cubeIvCI.subresourceRange.layerCount = 6;
    vkCreateImageView(m_device, &cubeIvCI, nullptr, &m_dummyCubeImageView);

    m_dummyCubeImage = dummyCubeImage;
    m_dummyCubeAllocation = dummyCubeAlloc;

    T8_LOG_INFO("[Vulkan] Dummy textures created (2D + Cube)");
  }

} // namespace t850

#endif // OS_WINDOWS

