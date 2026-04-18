/*********************************************************
* T850 Engine — D3D12 Backend
*
* Phase 2: Shader, Texture, Buffer, RT implementations.
* All D3D12-specific classes in one header (matches D3DXDriver.h pattern).
*********************************************************/

#ifndef T800_D3D12DRIVER_H
#define T800_D3D12DRIVER_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <dxgi1_4.h>
#include <D3Dcompiler.h>

#include <wrl.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <unordered_map>
#include <thread>
#include <atomic>
#include <fstream>
#include <mutex>

namespace t800 {

  // ══════════════════════════════════════════════════════
  //  D3D12 Descriptor Heap — linear allocator
  // ══════════════════════════════════════════════════════
  class D3D12Heap {
  public:
    enum Type {
      CBV_SRV_UAV_VISIBLE = 0,
      CBV_SRV_UAV_NOT_VISIBLE,
      SAMPLER,
      RTV,
      DSV,
      MAX
    };

    bool Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                uint32_t numDescriptors, bool shaderVisible);
    void Destroy();

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUStart() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUStart() const;
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateCPU();
    D3D12_GPU_DESCRIPTOR_HANDLE AllocateGPU();
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUAt(uint64_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUAt(uint64_t index) const;
    uint64_t GetCurrentIndex() const { return m_currentCount; }
    void Increment() { m_currentCount++; }
    ID3D12DescriptorHeap* GetHeap() const { return m_heap.Get(); }

  private:
    ComPtr<ID3D12DescriptorHeap> m_heap;
    D3D12_DESCRIPTOR_HEAP_TYPE   m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uint64_t m_maxDescriptors = 0;
    uint64_t m_currentCount   = 0;
    uint64_t m_incrementSize  = 0;
    bool     m_shaderVisible  = false;
  };

  // ══════════════════════════════════════════════════════
  //  D3D12 Shader
  // ══════════════════════════════════════════════════════
  class D3D12Shader : public ShaderBase {
  public:
    bool CreateShaderAPI(std::string src_vs, std::string src_fs,
                         const std::string& vs_name = "", const std::string& fs_name = "") override;
    void Set(const DeviceContext& deviceContext) override;
    void DestroyAPIShader() override;

    ComPtr<ID3DBlob>            VS_blob;
    ComPtr<ID3DBlob>            FS_blob;
    ComPtr<ID3D12RootSignature> pRootSignature;
    std::vector<D3D12_INPUT_ELEMENT_DESC> VertexDecl;
    int vertexStride = 0;

    // Root parameter slot indices (resolved during reflection)
    int cbvSlot = -1;      // root param index for constant buffer b0
    int samplerSlot = -1;  // root param index for sampler s0
    // SRV slots: root param index for each texture register t0..tN
    std::unordered_map<int, int> srvSlots; // register → root param index

  private:
    bool BuildRootSignature(ID3D12Device* device, ID3D12ShaderReflection* vsReflect,
                            ID3D12ShaderReflection* fsReflect);
    // Storage for semantic name strings (must outlive VertexDecl)
    std::vector<std::string> m_semanticNames;
  };

  // ══════════════════════════════════════════════════════
  //  D3D12 Texture
  // ══════════════════════════════════════════════════════
  class D3D12Texture : public Texture {
  public:
    D3D12Texture() {}

    void LoadAPITexture(DeviceContext* context, unsigned char* buffer) override;
    void LoadAPITextureCompressed(unsigned char* buffer) override;
    void DestroyAPITexture() override;
    void SetTextureParams() override;
    void GetFormatBpp(unsigned int& props, unsigned int& format, unsigned int& bpp) override;
    void Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) override;
    void SetSampler(const DeviceContext& deviceContext, unsigned int slot = 0) override;

    ComPtr<ID3D12Resource> pTexResource;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCPU = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGPU = {};
  };

  // ══════════════════════════════════════════════════════
  //  D3D12 Buffers
  // ══════════════════════════════════════════════════════
  class D3D12VertexBuffer : public VertexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class D3D12Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ComPtr<ID3D12Resource> m_buffer;
    D3D12_VERTEX_BUFFER_VIEW m_view = {};
    void* m_mappedData = nullptr;
  };

  class D3D12IndexBuffer : public IndexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format = T8_IB_FORMAR::R32) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class D3D12Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ComPtr<ID3D12Resource> m_buffer;
    D3D12_INDEX_BUFFER_VIEW m_view = {};
    void* m_mappedData = nullptr;
  };

  class D3D12ConstantBuffer : public ConstantBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void Set(const DeviceContext& deviceContext) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend class D3D12Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    ComPtr<ID3D12Resource> m_buffer;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle = {};
    void* m_mappedData = nullptr;
    uint32_t m_alignedSize = 0;
  };

  // ══════════════════════════════════════════════════════
  //  D3D12 Render Target
  // ══════════════════════════════════════════════════════
  class D3D12RT : public BaseRT {
  public:
    bool LoadAPIRT() override;
    void DestroyAPIRT() override;
    void Set(const DeviceContext& context) override;
    void ChangeCubeDepthTexture(int i) override;

    std::vector<ComPtr<ID3D12Resource>>       vColorResources;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>  vRTVHandles;
    ComPtr<ID3D12Resource>                    depthResource;
    D3D12_CPU_DESCRIPTOR_HANDLE               depthDSV = {};
    bool isCubeDepth = false;
    D3D12_CPU_DESCRIPTOR_HANDLE               cubeFaceDSVs[6] = {};
    // Track resource states for barriers
    std::vector<D3D12_RESOURCE_STATES>        vColorStates;
    D3D12_RESOURCE_STATES                     depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  };

  // ══════════════════════════════════════════════════════
  //  D3D12 Device Context
  // ══════════════════════════════════════════════════════
  class D3D12DeviceContext : public DeviceContext {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;
    void release() override;
    void SetPrimitiveTopology(T8_TOPOLOGY::E topology) override;
    void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) override;

    ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList.Get(); }

  private:
    friend class D3D12Driver;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
  };

  // ══════════════════════════════════════════════════════
  //  D3D12 Device
  // ══════════════════════════════════════════════════════
  class D3D12Device : public Device {
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

    ID3D12Device* GetNativeDevice() const { return m_device.Get(); }

  private:
    friend class D3D12Driver;
    ComPtr<ID3D12Device> m_device;
  };

  // ══════════════════════════════════════════════════════
  //  D3D12 Pipeline State cache key
  // ══════════════════════════════════════════════════════
  struct D3D12PipelineKey {
    uint32_t shaderKey;
    uint8_t  blend;
    uint8_t  depth;
    uint8_t  cull;
    uint8_t  numRTVs;
    DXGI_FORMAT rtvFormat;
    bool operator==(const D3D12PipelineKey& o) const {
      return shaderKey == o.shaderKey && blend == o.blend &&
             depth == o.depth && cull == o.cull && numRTVs == o.numRTVs &&
             rtvFormat == o.rtvFormat;
    }
  };

  struct D3D12PipelineKeyHash {
    size_t operator()(const D3D12PipelineKey& k) const {
      size_t h = std::hash<uint32_t>()(k.shaderKey);
      h ^= std::hash<uint8_t>()(k.blend)    << 1;
      h ^= std::hash<uint8_t>()(k.depth)    << 2;
      h ^= std::hash<uint8_t>()(k.cull)     << 3;
      h ^= std::hash<uint8_t>()(k.numRTVs)  << 4;
      h ^= std::hash<uint32_t>()((uint32_t)k.rtvFormat) << 5;
      return h;
    }
  };

  // ══════════════════════════════════════════════════════
  //  D3D12 Driver — the main backend
  // ══════════════════════════════════════════════════════
  class D3D12Driver : public BaseDriver {
  public:
    static const UINT kBackBufferCount = 2;

    D3D12Driver() { m_currentAPI = GRAPHICS_API::D3D12; }

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

    // ── D3D12-specific overrides ──
    void BeginFrame() override;
    void EndFrame() override;
    void WaitForGPU() override;
    void BuildPipelineObjects() override;

    // ── Helpers for resource creation ──
    D3D12Heap& GetHeap(D3D12Heap::Type type) { return m_heaps[type]; }
    ID3D12GraphicsCommandList* GetCmdList() const { return m_commandList.Get(); }
    ID3D12CommandQueue*        GetCmdQueue() const { return m_commandQueue.Get(); }

    // Upload helper: copies data to GPU using a temp command list
    void UploadBufferData(ID3D12Resource* dest, const void* data, size_t dataSize,
                          D3D12_RESOURCE_STATES afterState);

    // PSO cache — lazy creation
    ID3D12PipelineState* GetOrCreatePSO(D3D12Shader* shader, uint8_t numRTVs = 1,
                                         DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
                                         DXGI_FORMAT dsvFormat = DXGI_FORMAT_D32_FLOAT);

    // Default sampler GPU handle
    D3D12_GPU_DESCRIPTOR_HANDLE GetDefaultSamplerGPU() const { return m_defaultSamplerGPU; }

    // Per-frame CB ring allocator: returns GPU VA of a 256-aligned region with data copied
    D3D12_GPU_VIRTUAL_ADDRESS AllocateCBData(const void* data, UINT dataSize);

    // Per-frame dynamic CBV descriptor: copies data to ring buffer, creates CBV, returns GPU handle
    D3D12_GPU_DESCRIPTOR_HANDLE AllocateDynamicCBV(const void* data, UINT dataSize);

  private:
    void CreateDevice();
    void CreateCommandInfrastructure();
    void CreateSwapChain();
    void CreateBackBufferViews();
    void CreateDepthBuffer();
    void CreateHeaps();
    void CreateDefaultSampler();
    void WaitForFence();

    HWND m_hwnd = nullptr;

    // Core D3D12 objects
    ComPtr<IDXGIFactory4>       m_dxgiFactory;
    ComPtr<IDXGISwapChain3>     m_swapChain;

    // Command infrastructure
    ComPtr<ID3D12CommandQueue>         m_commandQueue;
    ComPtr<ID3D12CommandAllocator>     m_commandAllocators[kBackBufferCount];
    ComPtr<ID3D12GraphicsCommandList>  m_commandList;

    // Synchronization — single monotonically increasing fence
    ComPtr<ID3D12Fence> m_fence;
    UINT64              m_nextFenceValue = 1;              // next value to signal
    UINT64              m_frameFenceValues[kBackBufferCount] = {}; // value each BB must reach before reuse
    HANDLE              m_fenceEvent = nullptr;

    // Back buffers
    ComPtr<ID3D12Resource> m_backBuffers[kBackBufferCount];
    ComPtr<ID3D12Resource> m_depthBuffer;
    UINT m_currentBackBuffer = 0;

    // Descriptor heaps
    D3D12Heap m_heaps[D3D12Heap::MAX];

    // Back buffer RTV/DSV handles
    D3D12_CPU_DESCRIPTOR_HANDLE m_backBufferRTVs[kBackBufferCount] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_depthDSV = {};

    // Default sampler
    D3D12_CPU_DESCRIPTOR_HANDLE m_defaultSamplerCPU = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_defaultSamplerGPU = {};

    // Viewport / scissor
    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT     m_scissorRect = {};

    // Per-frame constant buffer ring allocator
    static const UINT kCBRingBufferSize = 4 * 1024 * 1024; // 4MB per frame
    ComPtr<ID3D12Resource> m_cbRingBuffers[kBackBufferCount];
    void*                  m_cbRingMapped[kBackBufferCount] = {};
    UINT                   m_cbRingOffset = 0;  // current offset within active ring buffer

    // Per-frame dynamic descriptor region (within CBV_SRV_UAV_VISIBLE heap)
    uint64_t m_dynamicDescriptorBase = 512;  // safe initial offset (permanent descs < 512)
    uint64_t m_dynamicDescriptorOffset = 0; // current offset within dynamic region

    // Cached pipeline state for deferred PSO lookup
    BLEND_STATES           m_currentBlend = BLEND_DEFAULT;
    DEPTH_STENCIL_STATES   m_currentDepth = DEPTH_DEFAULT;
    FACE_CULLING           m_currentCull  = FRONT_FACES;
    bool                   m_frameStarted = false;  // tracks whether BeginFrame was called this frame

    // PSO cache: lazy-created per (shader × blend × depth × cull × RT config)
    std::unordered_map<D3D12PipelineKey, ComPtr<ID3D12PipelineState>, D3D12PipelineKeyHash> m_psoCache;

    // ── Debug layer InfoQueue polling thread ──
    void StartDebugMessageThread();
    void StopDebugMessageThread();
    void PollDebugMessages();   // called by thread

    ComPtr<ID3D12InfoQueue>  m_infoQueue;
    std::thread              m_debugThread;
    std::atomic<bool>        m_debugThreadRunning{false};
    std::ofstream            m_debugLogFile;
    std::mutex               m_debugLogMutex;
  };

} // namespace t800

#endif // OS_WINDOWS
#endif // T800_D3D12DRIVER_H
