/*********************************************************
* T850 Engine — D3D12 Backend
*
* D3D12Driver.h: Umbrella header — includes all D3D12 types
*********************************************************/

#ifndef T800_D3D12DRIVER_H
#define T800_D3D12DRIVER_H

#include <Config.h>
#include <video/BaseDriver.h>

#ifdef OS_WINDOWS

#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <D3Dcompiler.h>

#include <wrl.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include <unordered_map>
#include <thread>
#include <atomic>
#include <fstream>
#include <mutex>

// Per-class headers
#include <video/d3d12/D3D12Heap.h>
#include <video/d3d12/D3D12VertexBuffer.h>
#include <video/d3d12/D3D12IndexBuffer.h>
#include <video/d3d12/D3D12ConstantBuffer.h>
#include <video/d3d12/D3D12DeviceContext.h>
#include <video/d3d12/D3D12Device.h>
#include <video/d3d12/D3D12PipelineKey.h>
#include <video/d3d12/D3D12Shader.h>
#include <video/d3d12/D3D12Texture.h>
#include <video/d3d12/D3D12RT.h>

namespace t800 {

  // ══════════════════════════════════════════════════════
  //  D3D12 Driver — the main backend
  // ══════════════════════════════════════════════════════
  class D3D12Driver : public BaseDriver {
  public:
    static const UINT kBackBufferCount = 3;  // triple-buffer for full CPU-GPU overlap

    D3D12Driver() { m_currentAPI = GRAPHICS_API::D3D12; }

    // ── BaseDriver pure virtuals ──
    void InitDriver() override;
    void CreateSurfaces() override;
    void DestroySurfaces() override;
    void Update() override;
    void DestroyDriver() override;
    void SetWindow(void* window) override;
    void SetWindowHandle(const WindowHandle& handle) override;
    void SetDimensions(int w, int h) override;
    bool ResizeSwapchain(int newW, int newH) override;
    void Clear() override;
    void ClearWithColor(float r, float g, float b, float a) override;
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
    ID3D12GraphicsCommandList* GetCmdList() const { return m_commandLists[m_currentBackBuffer].Get(); }
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

    // Rebind back buffer without DSV (for GUI/overlay draws with depth disabled)
    void BindBackBufferNoDSV();

    DEPTH_STENCIL_STATES GetCurrentDepthState() const { return m_currentDepth; }

    // Per-frame CB ring allocator: returns GPU VA of a 256-aligned region with data copied
    D3D12_GPU_VIRTUAL_ADDRESS AllocateCBData(const void* data, UINT dataSize);

    // Per-frame ring allocator for vertex/index data (16-byte aligned)
    D3D12_GPU_VIRTUAL_ADDRESS AllocateRingData(const void* data, UINT dataSize);

    // Per-frame dynamic CBV descriptor: copies data to ring buffer, creates CBV, returns GPU handle
    D3D12_GPU_DESCRIPTOR_HANDLE AllocateDynamicCBV(const void* data, UINT dataSize);

  private:
    friend class D3D12Shader;
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

    // Command infrastructure — one list+allocator per back buffer for full overlap
    ComPtr<ID3D12CommandQueue>         m_commandQueue;
    ComPtr<ID3D12CommandAllocator>     m_commandAllocators[kBackBufferCount];
    ComPtr<ID3D12GraphicsCommandList>  m_commandLists[kBackBufferCount];

    // Synchronization — single monotonically increasing fence
    ComPtr<ID3D12Fence> m_fence;
    UINT64              m_nextFenceValue = 1;              // next value to signal
    UINT64              m_frameFenceValues[kBackBufferCount] = {}; // value each BB must reach before reuse
    HANDLE              m_fenceEvent = nullptr;

    // Tearing / waitable swap chain
    bool                m_tearingSupported = false;
    HANDLE              m_swapChainWaitableObject = nullptr;

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
    static const UINT kCBRingBufferSize = 4 * 1024 * 1024; // 4 MB per frame
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

    // Last-bound state for redundancy elimination
    ID3D12PipelineState*   m_lastPSO = nullptr;
    ID3D12RootSignature*   m_lastRootSig = nullptr;

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
