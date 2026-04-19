/*********************************************************
* T850 Engine — Vulkan Backend
*
* VulkanDriver.h: Driver, Device, DeviceContext,
*                 Buffers (VB, IB, CB), Texture, RT,
*                 Shader, Pipeline cache.
*********************************************************/

#ifndef T800_VULKANDRIVER_H
#define T800_VULKANDRIVER_H

#include <Config.h>
#include <video/BaseDriver.h>

#if defined(OS_WINDOWS)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>

namespace t800 {

  // ══════════════════════════════════════════════════════
  //  Vulkan Vertex Buffer
  // ══════════════════════════════════════════════════════
  class VulkanVertexBuffer : public VertexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class VulkanDevice;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    VkBuffer        m_buffer = VK_NULL_HANDLE;
    VmaAllocation   m_allocation = VK_NULL_HANDLE;
    void*           m_mappedData = nullptr;
  };

  // ══════════════════════════════════════════════════════
  //  Vulkan Index Buffer
  // ══════════════════════════════════════════════════════
  class VulkanIndexBuffer : public IndexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format = T8_IB_FORMAR::R32) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class VulkanDevice;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    VkBuffer        m_buffer = VK_NULL_HANDLE;
    VmaAllocation   m_allocation = VK_NULL_HANDLE;
    void*           m_mappedData = nullptr;
  };

  // ══════════════════════════════════════════════════════
  //  Vulkan Constant Buffer
  // ══════════════════════════════════════════════════════
  class VulkanConstantBuffer : public ConstantBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class VulkanDevice;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    VkBuffer        m_buffer = VK_NULL_HANDLE;
    VmaAllocation   m_allocation = VK_NULL_HANDLE;
    void*           m_mappedData = nullptr;   // persistently mapped
    uint32_t        m_alignedSize = 0;
  };

  // ══════════════════════════════════════════════════════
  //  Vulkan Texture
  // ══════════════════════════════════════════════════════
  class VulkanTexture : public Texture {
  public:
    VulkanTexture() {}

    void LoadAPITexture(DeviceContext* context, unsigned char* buffer) override;
    void LoadAPITextureCompressed(unsigned char* buffer) override;
    void DestroyAPITexture() override;
    void SetTextureParams() override;
    void GetFormatBpp(unsigned int& props, unsigned int& format, unsigned int& bpp) override;
    void Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void SetSampler(const DeviceContext& deviceContext, unsigned int slot = 0) override;

    VkImage         m_image = VK_NULL_HANDLE;
    VmaAllocation   m_allocation = VK_NULL_HANDLE;
    VkImageView     m_imageView = VK_NULL_HANDLE;
    VkSampler       m_sampler = VK_NULL_HANDLE;
    VkFormat        m_format = VK_FORMAT_R8G8B8A8_UNORM;
  };

  // ══════════════════════════════════════════════════════
  //  Vulkan Render Target
  // ══════════════════════════════════════════════════════
  class VulkanRT : public BaseRT {
  public:
    bool LoadAPIRT() override;
    void DestroyAPIRT() override;
    void Set(const DeviceContext& context) override;
    void ChangeCubeDepthTexture(int i) override;

    VkRenderPass    m_renderPass = VK_NULL_HANDLE;
    VkFramebuffer   m_framebuffer = VK_NULL_HANDLE;

    // Color attachments
    std::vector<VkImage>        vColorImages;
    std::vector<VmaAllocation>  vColorAllocations;
    std::vector<VkImageView>    vColorImageViews;
    std::vector<VkImageLayout>  vColorLayouts;
    VkFormat                    m_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    // Depth attachment
    VkImage         m_depthImage = VK_NULL_HANDLE;
    VmaAllocation   m_depthAllocation = VK_NULL_HANDLE;
    VkImageView     m_depthImageView = VK_NULL_HANDLE;
    VkFormat        m_depthFormat = VK_FORMAT_D32_SFLOAT;

    bool            m_isCubeDepth = false;
    VkImageView     m_cubeFaceViews[6] = {};
  };

  // ══════════════════════════════════════════════════════
  //  Vulkan Shader
  // ══════════════════════════════════════════════════════
  class VulkanShader : public ShaderBase {
  public:
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
    int srvBindings[8] = {-1,-1,-1,-1,-1,-1,-1,-1}; // slot → binding index
    int maxBinding = 0;  // highest binding number in the layout
  };

  // ══════════════════════════════════════════════════════
  //  Vulkan Device Context
  // ══════════════════════════════════════════════════════
  class VulkanDeviceContext : public DeviceContext {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void release() override;
    void SetPrimitiveTopology(T8_TOPOLOGY::E topology) override;
    void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) override;

    VkCommandBuffer GetCommandBuffer() const { return m_commandBuffer; }

  private:
    friend class VulkanDriver;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
  };

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
    BaseRT*     CreateRT(int nrt, int cf, int df, int w, int h, bool genMips = false) override;

    VkDevice GetNativeDevice() const { return m_device; }

  private:
    friend class VulkanDriver;
    VkDevice m_device = VK_NULL_HANDLE;
  };

  // ══════════════════════════════════════════════════════
  //  Vulkan Pipeline cache key
  // ══════════════════════════════════════════════════════
  struct VulkanPipelineKey {
    uintptr_t shaderPtr;
    uint8_t   blend;
    uint8_t   depth;
    uint8_t   cull;
    uint8_t   numColorAttachments;
    VkFormat  colorFormat;
    VkFormat  depthFormat;
    bool operator==(const VulkanPipelineKey& o) const {
      return shaderPtr == o.shaderPtr && blend == o.blend &&
             depth == o.depth && cull == o.cull &&
             numColorAttachments == o.numColorAttachments &&
             colorFormat == o.colorFormat && depthFormat == o.depthFormat;
    }
  };

  struct VulkanPipelineKeyHash {
    size_t operator()(const VulkanPipelineKey& k) const {
      size_t h = std::hash<uintptr_t>()(k.shaderPtr);
      h ^= std::hash<uint8_t>()(k.blend)    << 1;
      h ^= std::hash<uint8_t>()(k.depth)    << 2;
      h ^= std::hash<uint8_t>()(k.cull)     << 3;
      h ^= std::hash<uint8_t>()(k.numColorAttachments) << 4;
      h ^= std::hash<uint32_t>()(static_cast<uint32_t>(k.colorFormat)) << 5;
      h ^= std::hash<uint32_t>()(static_cast<uint32_t>(k.depthFormat)) << 6;
      return h;
    }
  };

  // ══════════════════════════════════════════════════════
  //  Vulkan Driver — the main backend
  // ══════════════════════════════════════════════════════
  class VulkanDriver : public BaseDriver {
  public:
    static const UINT kBackBufferCount = 3;  // triple-buffer for full CPU-GPU overlap

    VulkanDriver() { m_currentAPI = GRAPHICS_API::VULKAN; }

    // ── BaseDriver pure virtuals ──
    void InitDriver() override;
    void CreateSurfaces() override;
    void DestroySurfaces() override;
    void Update() override;
    void DestroyDriver() override;
    void SetWindow(void* window) override;
    void SetDimensions(int w, int h) override;
    void Clear() override;
    void SwapBuffers() override;
    void SetBlendState(BLEND_STATES state) override;
    void SetDepthStencilState(DEPTH_STENCIL_STATES state) override;
    void SetCullFace(FACE_CULLING state) override;
    void PopRT() override;
    void SaveScreenshot(std::string path) override;
    void SaveRTToFile(int rtID, int attachment, std::string path) override;

    // ── Vulkan-specific overrides ──
    void BeginFrame() override;
    void EndFrame() override;
    void WaitForGPU() override;
    void BuildPipelineObjects() override;

    // ── Accessors ──
    VkCommandBuffer    GetCmdBuffer() const { return m_commandBuffers[m_currentFrame]; }
    VkDevice           GetDevice() const { return m_device; }
    VmaAllocator       GetAllocator() const { return m_allocator; }
    VkPhysicalDevice   GetPhysicalDevice() const { return m_physicalDevice; }
    VkQueue            GetGraphicsQueue() const { return m_graphicsQueue; }
    uint32_t           GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    VkRenderPass       GetBackbufferRenderPass() const { return m_backbufferRenderPass; }
    VkCommandPool      GetTransientCommandPool() const { return m_transientCommandPool; }

    DEPTH_STENCIL_STATES GetCurrentDepthState() const { return m_currentDepth; }

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

    // End the currently active render pass (if any) — safe to call even when none is active
    void EndRenderPassIfActive(VkCommandBuffer cmd) {
      if (m_renderPassActive) {
        vkCmdEndRenderPass(cmd);
        m_renderPassActive = false;
      }
    }
    void SetRenderPassActive(bool active) { m_renderPassActive = active; }

    // Pending texture/CB bindings set by Texture::Set / CB::Set
    struct PendingTextureBinding {
      VkImageView imageView = VK_NULL_HANDLE;
      VkSampler   sampler   = VK_NULL_HANDLE;
    };
    PendingTextureBinding m_pendingTextures[8] = {};
    VkDescriptorBufferInfo m_pendingCB = {};
    bool m_cbDirty = false;

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
    void CreateDescriptorPool();
    void CreateAllocator();
    void WaitForFence(uint32_t frameIndex);

    HWND m_hwnd = nullptr;

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
    VkFramebuffer   m_backbufferFramebuffers[kBackBufferCount] = {};

    // Command infrastructure — one buffer + allocator per frame in flight
    VkCommandPool       m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer     m_commandBuffers[kBackBufferCount] = {};
    VkCommandPool       m_transientCommandPool = VK_NULL_HANDLE;  // for upload helpers

    // Synchronization — per frame in flight
    VkSemaphore     m_imageAvailableSemaphores[kBackBufferCount] = {};
    VkSemaphore     m_renderFinishedSemaphores[kBackBufferCount] = {};
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
    static const uint32_t kCBRingBufferSize = 4 * 1024 * 1024; // 4 MB per frame
    VkBuffer        m_cbRingBuffers[kBackBufferCount] = {};
    VmaAllocation   m_cbRingAllocations[kBackBufferCount] = {};
    void*           m_cbRingMapped[kBackBufferCount] = {};
    uint32_t        m_cbRingOffset = 0;

    // Cached pipeline state for deferred pipeline lookup
    BLEND_STATES           m_currentBlend = BLEND_DEFAULT;
    DEPTH_STENCIL_STATES   m_currentDepth = DEPTH_DEFAULT;
    FACE_CULLING           m_currentCull  = FRONT_FACES;
    bool                   m_frameStarted = false;

    // Last-bound state for redundancy elimination
    VkPipeline      m_lastPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_lastPipelineLayout = VK_NULL_HANDLE;

    // Active render pass (set when beginning backbuffer or RT render passes)
    VkRenderPass    m_activeRenderPass = VK_NULL_HANDLE;

    // Dummy 1x1 texture for unbound descriptor slots
    VkImage         m_dummyImage = VK_NULL_HANDLE;
    VmaAllocation   m_dummyAllocation = VK_NULL_HANDLE;
    VkImageView     m_dummyImageView = VK_NULL_HANDLE;
    VkSampler       m_dummySampler = VK_NULL_HANDLE;
    void            CreateDummyTexture();

    // Pipeline cache: lazy-created per (shader × blend × depth × cull × attachment config)
    std::unordered_map<VulkanPipelineKey, VkPipeline, VulkanPipelineKeyHash> m_pipelineCache;
    VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;  // Vulkan driver-level cache

    // Debug
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
  };

} // namespace t800

#endif // OS_WINDOWS
#endif // T800_VULKANDRIVER_H
