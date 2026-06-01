/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanDriver.h: Umbrella header — includes all Vulkan classes
*********************************************************/

#ifndef T800_VULKANDRIVER_H
#define T800_VULKANDRIVER_H

#include <Config.h>

#if defined(OS_WINDOWS) || defined(OS_ANDROID)

// Include all per-class headers
#include <video/vulkan/VulkanVertexBuffer.h>
#include <video/vulkan/VulkanIndexBuffer.h>
#include <video/vulkan/VulkanConstantBuffer.h>
#include <video/vulkan/VulkanTexture.h>
#include <video/vulkan/VulkanRT.h>
#include <video/vulkan/VulkanShader.h>
#include <video/vulkan/VulkanDeviceContext.h>
#include <video/vulkan/VulkanDevice.h>
#include <video/vulkan/VulkanPipelineKey.h>
#include <video/vulkan/VulkanUtils.h>

#if defined(OS_WINDOWS)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(OS_ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <utility>

namespace t850 {

  // ══════════════════════════════════════════════════════
  //  Vulkan Driver — the main backend
  // ══════════════════════════════════════════════════════
  class VulkanDriver : public BaseDriver {
  public:
    static constexpr uint32_t kBackBufferCount = 3;  // triple-buffer for full CPU-GPU overlap

    VulkanDriver() { m_currentAPI = GraphicsApi::VULKAN; }

    // ── BaseDriver pure virtuals ──
    void InitDriver() override;
    void CreateSurfaces() override;
    void DestroySurfaces() override;
    void Update() override;
    void DestroyDriver() override;
    void SetWindow(void* window) override;
    void SetDimensions(int w, int h) override;
    void Clear() override;
    void ClearWithColor(float r, float g, float b, float a) override;
    void SwapBuffers() override;
    void SetBlendState(BlendStates state) override;
    void SetDepthStencilState(DepthStencilStates state) override;
    void SetCullFace(FaceCulling state) override;
    void PopRT() override;
    void SaveScreenshot(std::string path) override;
    void SaveRTToFile(int rtID, int attachment, std::string path) override;
    bool ReadRTColorFloat(int rtID, int attachment, float outRGBA[4]) override;
    bool ResizeSwapchain(int newW, int newH) override;
#ifdef T850_RENDER_TRACE
    void RefreshTracePendingRenderState() override;
#endif

    // ── Vulkan-specific overrides ──
    void BeginFrame() override;
    void EndFrame() override;
    void WaitForGPU() override;
    void FlushGPUResources() override;
    void BeginResourceUploadBatch() override;
    void EndResourceUploadBatch() override;
    bool IsResourceUploadBatchActive() const override { return m_uploadBatchDepth > 0; }
    void BuildPipelineObjects() override;
    void SetViewport(float x, float y, float w, float h) override;
    void SetScissorRect(int x, int y, int w, int h) override;

    // ── Accessors ──
    VkCommandBuffer    GetCmdBuffer() const { return m_commandBuffers[m_currentFrame]; }
    VkDevice           GetDevice() const { return m_device; }
    VmaAllocator       GetAllocator() const { return m_allocator; }
    VkPhysicalDevice   GetPhysicalDevice() const { return m_physicalDevice; }
    VkInstance          GetInstance() const { return m_instance; }
    VkQueue            GetGraphicsQueue() const { return m_graphicsQueue; }
    uint32_t           GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    VkRenderPass       GetBackbufferRenderPass() const { return m_backbufferRenderPass; }
    VkCommandPool      GetTransientCommandPool() const { return m_transientCommandPool; }

    // Transient command buffer helpers for one-shot GPU operations
    VkCommandBuffer GetTransientCommandBuffer();
    void SubmitTransientCommandBuffer(VkCommandBuffer cmd);
    VkCommandBuffer GetCurrentCommandBuffer() const { return m_commandBuffers[m_currentFrame]; }
    VkCommandBuffer GetResourceUploadCommandBuffer();
    void KeepResourceUploadBuffer(VkBuffer buffer, VmaAllocation alloc);

    // Defer cleanup of staging resources until frame completes
    void DeferCleanup(VkBuffer buffer, VmaAllocation alloc);

    DepthStencilStates GetCurrentDepthState() const { return m_currentDepth; }

    // PSO cache — lazy creation
    VkPipeline GetOrCreatePipeline(VulkanShader* shader, uint8_t numColorAttachments = 1,
                                   VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
                                   VkFormat depthFormat = VK_FORMAT_D32_SFLOAT);

    // Upload helper: stages data to GPU via a transient command buffer
    void UploadBufferData(VkBuffer dest, const void* data, VkDeviceSize dataSize);

    // Per-frame CB ring allocator: returns offset within the ring buffer with data copied
    VkDescriptorBufferInfo AllocateCBData(const void* data, uint32_t dataSize);

    // Rebind back buffer without depth (for GUI/overlay draws)
    void BindBackBufferNoDepth();

    // Descriptor set allocation from the per-frame pool
    VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout);

    // Write pending CB + textures into a descriptor set and bind it
    void BindPendingDescriptors(VkCommandBuffer cmd, VulkanShader* shader);

    // Currently active render pass (backbuffer or RT)
    VkRenderPass GetCurrentRenderPass() const { return m_activeRenderPass; }
    void SetActiveRenderPass(VkRenderPass rp) { m_activeRenderPass = rp; }

    // Ensure the backbuffer render pass is active (for ImGui overlay rendering)
    void EnsureBackbufferRenderPass();
    void SetPrePresentOverlayCallback(std::function<void()> callback) {
      if (!callback) {
        m_prePresentOverlayCallback = nullptr;
        return;
      }
      if (!m_prePresentOverlayCallback) {
        m_prePresentOverlayCallback = std::move(callback);
        return;
      }
      auto previous = std::move(m_prePresentOverlayCallback);
      m_prePresentOverlayCallback = [previous = std::move(previous), callback = std::move(callback)]() mutable {
        previous();
        callback();
      };
    }

    // Copy a rendered RT to the swapchain immediately before present. Used on
    // Android to avoid compositor-visible issues with the normal final quad.
    void SetLatePresentSource(int rtID, int attachment);
    bool SuspendWindowSurface();
    bool ResumeWindowSurface(void* nativeWindow, int newW, int newH);

    // End the currently active render pass (if any) — safe to call even when none is active
    bool EndRenderPassIfActive(VkCommandBuffer cmd) {
      if (m_renderPassActive) {
        vkCmdEndRenderPass(cmd);
        m_renderPassActive = false;
        m_activeRenderPass = VK_NULL_HANDLE;
        return true;
      }
      return false;
    }
    void SetRenderPassActive(bool active) { m_renderPassActive = active; }

    // Pending texture/CB bindings set by Texture::Set / CB::Set
    struct PendingTextureBinding {
      VkImageView imageView = VK_NULL_HANDLE;
      VkSampler   sampler   = VK_NULL_HANDLE;
      // Tracer-only: cached texture id + display name resolved at request time
      // so BindPendingDescriptors can emit a commit event correlating slot ->
      // texture id without a costly reverse lookup.
      int         tracerTexId = -1;
      char        tracerName[64] = {};
      char        tracerStage[4] = {};
      // Tracer-only: logical sampler signature id (built from TextBasicParams
      // by VulkanTexture::Set so cross-API trace diffs are meaningful).
      int         tracerSamplerId = -1;
    };
    PendingTextureBinding m_pendingTextures[VulkanShader::kMaxTextureSlots] = {};
    struct PendingConstantBufferBinding {
      VkDescriptorBufferInfo bufferInfo = {};
      int tracerId = -1;
    };
    PendingConstantBufferBinding m_pendingCBs[VulkanShader::kMaxCBufferSlots] = {};
    bool m_cbDirty = false;

    // Allocate vertex data from the per-frame ring buffer for dynamic VBs.
    struct VBRingAlloc { VkBuffer buffer; VkDeviceSize offset; bool valid; };
    VBRingAlloc AllocateVBRing(const void* data, uint32_t size);

  private:
    friend class VulkanShader;
    friend class VulkanDeviceContext;

    void CreateInstance();
    void CreateDevice();
    void CreateSwapChain();
    void CreateRenderPass();
    void CreateBackBufferViews();
    void CreateDepthBuffer();
    void CreateFramebuffers();
    void CreateCommandInfrastructure();
    void CreateSyncObjects();
    void CreateSwapchainImageSemaphores(uint32_t imageCount);
    void DestroySwapchainImageSemaphores();
    void CreateDescriptorPool();
    void CreateAllocator();
    void SubmitCurrentFrameAndWait(VkCommandBuffer cmd);
    void WaitForFence(uint32_t frameIndex);
    bool CreatePlatformSurface();
    void DestroyWindowSurfaceResources(bool destroySurface);
    bool CopyLatePresentSourceToSwapchain(VkCommandBuffer cmd);

    void* m_nativeWindow = nullptr;

    // Core Vulkan objects
    VkInstance          m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice    m_physicalDevice = VK_NULL_HANDLE;
    VkDevice            m_device = VK_NULL_HANDLE;
    VkQueue             m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue             m_presentQueue = VK_NULL_HANDLE;
    uint32_t            m_graphicsQueueFamily = 0;
    uint32_t            m_presentQueueFamily = 0;

    // Surface & swap chain
    VkSurfaceKHR        m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR      m_swapChain = VK_NULL_HANDLE;
    VkFormat            m_swapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D          m_swapChainExtent = {};
    bool                m_swapChainSupportsTransferDst = false;

    // Back buffer images (owned by swap chain)
    std::vector<VkImage>     m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    uint32_t                 m_imageIndex = 0;  // acquired swap chain image index

    // Depth buffer
    VkImage         m_depthImage = VK_NULL_HANDLE;
    VmaAllocation   m_depthAllocation = VK_NULL_HANDLE;
    VkImageView     m_depthImageView = VK_NULL_HANDLE;

    // Default backbuffer render pass & framebuffers
    VkRenderPass    m_backbufferRenderPass = VK_NULL_HANDLE;
    VkRenderPass    m_backbufferRenderPassLoad = VK_NULL_HANDLE;  // LOAD_OP_LOAD variant for restarts
    std::vector<VkFramebuffer> m_backbufferFramebuffers;

    // Command infrastructure — one buffer + allocator per frame in flight
    VkCommandPool       m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer     m_commandBuffers[kBackBufferCount] = {};
    VkCommandPool       m_transientCommandPool = VK_NULL_HANDLE;  // for upload helpers

    // Synchronization — per frame in flight
    VkSemaphore     m_imageAvailableSemaphores[kBackBufferCount] = {};
    VkSemaphore     m_renderFinishedSemaphores[kBackBufferCount] = {};
    std::vector<VkSemaphore> m_imageRenderFinishedSemaphores;
    VkFence         m_inFlightFences[kBackBufferCount] = {};
    uint32_t        m_currentFrame = 0;
    bool            m_renderPassActive = false;

    // Descriptors — one pool per frame in flight to avoid resetting in-use pools
    VkDescriptorPool        m_descriptorPools[kBackBufferCount] = {};
    VkDescriptorSetLayout   m_globalDescriptorSetLayout = VK_NULL_HANDLE;

    // VMA allocator
    VmaAllocator    m_allocator = VK_NULL_HANDLE;

    // Viewport / scissor
    VkViewport      m_viewport = {};
    VkRect2D        m_scissorRect = {};

    // Per-frame constant buffer ring allocator
    static const uint32_t kCBRingBufferSize = 64 * 1024 * 1024; // 64 MB per frame for large draw-count scenes
    VkBuffer        m_cbRingBuffers[kBackBufferCount] = {};
    VmaAllocation   m_cbRingAllocations[kBackBufferCount] = {};
    void*           m_cbRingMapped[kBackBufferCount] = {};
    uint32_t        m_cbRingOffset = 0;
    uint32_t        m_cbRingPeakUsage = 0; // high-water mark across all frames so far

    // Descriptor set cache — keyed by (layout + texture fingerprint)
    // Cleared each frame when the descriptor pool is reset.
    std::unordered_map<uint64_t, VkDescriptorSet> m_descriptorSetCache;

    // Deferred staging buffer cleanup (destroyed after frame completes)
    struct DeferredBuffer { VkBuffer buffer; VmaAllocation alloc; };
    std::vector<DeferredBuffer> m_deferredCleanup[kBackBufferCount];

    int m_uploadBatchDepth = 0;
    VkCommandBuffer m_uploadBatchCmd = VK_NULL_HANDLE;
    std::vector<DeferredBuffer> m_uploadBatchBuffers;
    uint32_t m_uploadBatchCommandCount = 0;

    // Cached pipeline state for deferred pipeline lookup
    BlendStates           m_currentBlend = BLEND_DEFAULT;
    DepthStencilStates   m_currentDepth = DEPTH_DEFAULT;
    FaceCulling           m_currentCull  = FRONT_FACES;
    bool                   m_frameStarted = false;
    bool                   m_screenshotConsumedSemaphore = false;
    bool                   m_swapchainNeedsRecreate = false;

    // Last-bound state for redundancy elimination
    VkPipeline      m_lastPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_lastPipelineLayout = VK_NULL_HANDLE;

    // Active render pass (set when beginning backbuffer or RT render passes)
    VkRenderPass    m_activeRenderPass = VK_NULL_HANDLE;
    int             m_latePresentRT = -1;
    int             m_latePresentAttachment = 0;

    // Dummy 1x1 texture for unbound descriptor slots
    VkImage         m_dummyImage = VK_NULL_HANDLE;
    VmaAllocation   m_dummyAllocation = VK_NULL_HANDLE;
    VkImageView     m_dummyImageView = VK_NULL_HANDLE;
    VkSampler       m_dummySampler = VK_NULL_HANDLE;
    VkImage         m_dummyCubeImage = VK_NULL_HANDLE;
    VmaAllocation   m_dummyCubeAllocation = VK_NULL_HANDLE;
    VkImageView     m_dummyCubeImageView = VK_NULL_HANDLE;
    void            CreateDummyTexture();

    // Pipeline cache: lazy-created per (shader × blend × depth × cull × attachment config)
    std::unordered_map<VulkanPipelineKey, VkPipeline, VulkanPipelineKeyHash> m_pipelineCache;
    VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;  // Vulkan driver-level cache
    std::function<void()> m_prePresentOverlayCallback;

    // Debug
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
  };

} // namespace t850

#endif // OS_WINDOWS
#endif // T800_VULKANDRIVER_H
