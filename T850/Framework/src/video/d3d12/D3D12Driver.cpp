#include <pch.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12Driver.cpp: Driver lifecycle, Heap, Device, DeviceContext,
*                  Buffers (VB, IB, CB), PSO cache, debug layer.
*********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <debug/Profiler.h>
#include <debug/RenderTrace.h>
#include <core/Config.h>
#include <iostream>
#include <string>
#include <cassert>
#include <chrono>
#include <filesystem>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // ══════════════════════════════════════════════════════
  //  Shared helpers — used by all D3D12 source files
  // ══════════════════════════════════════════════════════
  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }


  // ══════════════════════════════════════════════════════
  //  D3D12 Debug InfoQueue Thread
  // ══════════════════════════════════════════════════════

  static const char* D3D12SeverityToStr(D3D12_MESSAGE_SEVERITY sev) {
    switch (sev) {
      case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
      case D3D12_MESSAGE_SEVERITY_ERROR:      return "ERROR";
      case D3D12_MESSAGE_SEVERITY_WARNING:    return "WARNING";
      case D3D12_MESSAGE_SEVERITY_INFO:       return "INFO";
      case D3D12_MESSAGE_SEVERITY_MESSAGE:    return "MESSAGE";
      default: return "UNKNOWN";
    }
  }

  static const char* D3D12CategoryToStr(D3D12_MESSAGE_CATEGORY cat) {
    switch (cat) {
      case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED:  return "APP";
      case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS:        return "MISC";
      case D3D12_MESSAGE_CATEGORY_INITIALIZATION:       return "INIT";
      case D3D12_MESSAGE_CATEGORY_CLEANUP:              return "CLEANUP";
      case D3D12_MESSAGE_CATEGORY_COMPILATION:          return "COMPILE";
      case D3D12_MESSAGE_CATEGORY_STATE_CREATION:       return "STATE";
      case D3D12_MESSAGE_CATEGORY_STATE_SETTING:        return "SETTING";
      case D3D12_MESSAGE_CATEGORY_STATE_GETTING:        return "GETTING";
      case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION:return "RESOURCE";
      case D3D12_MESSAGE_CATEGORY_EXECUTION:            return "EXEC";
      case D3D12_MESSAGE_CATEGORY_SHADER:               return "SHADER";
      default: return "?";
    }
  }

  void D3D12Driver::PollDebugMessages() {
    if (!m_infoQueue) return;

    UINT64 messageCount = m_infoQueue->GetNumStoredMessages();
    if (messageCount == 0) return;

    for (UINT64 i = 0; i < messageCount; ++i) {
      SIZE_T size = 0;
      m_infoQueue->GetMessage(i, nullptr, &size);
      if (size == 0) continue;

      std::vector<char> buffer(size);
      auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
      HRESULT hr = m_infoQueue->GetMessage(i, msg, &size);
      if (FAILED(hr)) continue;

      auto now = std::chrono::system_clock::now();
      auto time_t_now = std::chrono::system_clock::to_time_t(now);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
      struct tm tm_buf;
      localtime_s(&tm_buf, &time_t_now);
      char timeBuf[32];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03d",
               tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms.count());

      {
        std::lock_guard<std::mutex> lock(m_debugLogMutex);
        if (m_debugLogFile.is_open()) {
          m_debugLogFile << "[" << timeBuf << "] "
                         << "[" << D3D12SeverityToStr(msg->Severity) << "] "
                         << "[" << D3D12CategoryToStr(msg->Category) << "] "
                         << "[ID:" << msg->ID << "] "
                         << msg->pDescription << "\n";
          m_debugLogFile.flush();
        }
      }

      if (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
          msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
        T8_LOG_ERROR("[D3D12-DBG] [%s] %s", D3D12CategoryToStr(msg->Category), msg->pDescription);
      }
    }

    m_infoQueue->ClearStoredMessages();
  }

  void D3D12Driver::StartDebugMessageThread() {
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();

    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&m_infoQueue));
    if (FAILED(hr) || !m_infoQueue) {
      T8_LOG_ERROR("[D3D12] Failed to get ID3D12InfoQueue");
      return;
    }

    m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
    m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
    m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);
    m_infoQueue->SetMessageCountLimit(4096);

    std::string debugLogPath;
    if (!g_config.logFile.empty()) {
      std::filesystem::path p(g_config.logFile);
      std::string stem = p.stem().string();
      std::string ext  = p.extension().string();
      debugLogPath = (p.parent_path() / (stem + "_d3d12debug" + ext)).string();
    } else {
      auto now = std::chrono::system_clock::now();
      auto tt = std::chrono::system_clock::to_time_t(now);
      struct tm tm_buf;
      localtime_s(&tm_buf, &tt);
      char buf[64];
      snprintf(buf, sizeof(buf), "logs/T850_%04d%02d%02d_%02d%02d%02d_d3d12debug.log",
               tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
               tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
      debugLogPath = buf;
      std::filesystem::create_directories(std::filesystem::path(debugLogPath).parent_path());
    }

    m_debugLogFile.open(debugLogPath, std::ios::out | std::ios::trunc);
    if (!m_debugLogFile.is_open()) {
      T8_LOG_ERROR("[D3D12] Failed to open debug log: %s", debugLogPath.c_str());
      return;
    }

    m_debugLogFile << "=== D3D12 Debug Layer Messages ===\n";
    m_debugLogFile << "Log file: " << debugLogPath << "\n";
    m_debugLogFile << "==================================\n\n";
    m_debugLogFile.flush();

    T8_LOG_INFO("[D3D12] Debug message log: %s", debugLogPath.c_str());

    m_debugThreadRunning = true;
    m_debugThread = std::thread([this]() {
      while (m_debugThreadRunning) {
        PollDebugMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
      }
      PollDebugMessages();
    });

    T8_LOG_INFO("[D3D12] Debug message thread started");
  }

  void D3D12Driver::StopDebugMessageThread() {
    if (!m_debugThreadRunning) return;

    m_debugThreadRunning = false;
    if (m_debugThread.joinable()) {
      m_debugThread.join();
    }

    {
      std::lock_guard<std::mutex> lock(m_debugLogMutex);
      if (m_debugLogFile.is_open()) {
        m_debugLogFile << "\n=== Debug thread stopped ===\n";
        m_debugLogFile.close();
      }
    }

    m_infoQueue.Reset();
    T8_LOG_INFO("[D3D12] Debug message thread stopped");
  }


  // ══════════════════════════════════════════════════════
  //  D3D12Driver — PSO Cache
  // ══════════════════════════════════════════════════════

  ID3D12PipelineState* D3D12Driver::GetOrCreatePSO(D3D12Shader* shader, uint8_t numRTVs,
                                                     const DXGI_FORMAT* rtvFormats, DXGI_FORMAT dsvFormat) {
    if (numRTVs > 0 && rtvFormats && rtvFormats[0] == DXGI_FORMAT_UNKNOWN)
      numRTVs = 0;

    // When depth is disabled, keep DSV format matching the bound depth buffer
    // (the depth test/write is disabled in the PSO's DepthStencilState already)

    D3D12PipelineKey key = {};
    key.shaderPtr = reinterpret_cast<uintptr_t>(shader);
    key.blend = (uint8_t)m_currentBlend;
    key.depth = (uint8_t)m_currentDepth;
    key.cull = (uint8_t)m_currentCull;
    auto* d3dContext = static_cast<D3D12DeviceContext*>(T8DeviceContext);
    key.topology = (uint8_t)(d3dContext ? d3dContext->GetTopologyType() : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    key.numRTVs = numRTVs;
    key.rtvFormats.fill(DXGI_FORMAT_UNKNOWN);
    for (int i = 0; i < numRTVs && i < (int)key.rtvFormats.size(); ++i)
      key.rtvFormats[i] = rtvFormats ? rtvFormats[i] : DXGI_FORMAT_R8G8B8A8_UNORM;
    key.dsvFormat = dsvFormat;

    auto it = m_psoCache.find(key);
    if (it != m_psoCache.end()) return it->second.Get();

    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout.pInputElementDescs = shader->VertexDecl.data();
    pso.InputLayout.NumElements = (UINT)shader->VertexDecl.size();
    pso.pRootSignature = shader->pRootSignature.Get();
    pso.VS.pShaderBytecode = shader->VS_blob->GetBufferPointer();
    pso.VS.BytecodeLength = shader->VS_blob->GetBufferSize();
    pso.PS.pShaderBytecode = shader->FS_blob->GetBufferPointer();
    pso.PS.BytecodeLength = shader->FS_blob->GetBufferSize();
    pso.SampleMask = UINT_MAX;
    pso.SampleDesc.Count = 1;
    pso.PrimitiveTopologyType = (D3D12_PRIMITIVE_TOPOLOGY_TYPE)key.topology;
    pso.NumRenderTargets = numRTVs;
    for (int i = 0; i < numRTVs; i++) pso.RTVFormats[i] = key.rtvFormats[i];
    pso.DSVFormat = dsvFormat;

    // Rasterizer
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    switch (m_currentCull) {
      case FRONT_FACES:     pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;  break;
      case BACK_FACES:      pso.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
      case FRONT_AND_BACK:  pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;  break;
      default:              pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;  break;
    }
    pso.RasterizerState.DepthClipEnable = TRUE;

    // Depth/stencil
    switch (m_currentDepth) {
      case DEPTH_DEFAULT: case READ_WRITE:
        pso.DepthStencilState.DepthEnable = TRUE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        break;
      case READ:
        pso.DepthStencilState.DepthEnable = TRUE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        break;
      case NONE:
        pso.DepthStencilState.DepthEnable = FALSE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        break;
    }

    // Blend
    for (int i = 0; i < numRTVs; i++) {
      auto& rt = pso.BlendState.RenderTarget[i];
      rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
      switch (m_currentBlend) {
        case BLEND_DEFAULT: case BLEND_OPAQUE:
          rt.BlendEnable = FALSE; break;
        case ADDITIVE:
          rt.BlendEnable = TRUE;
          rt.SrcBlend = D3D12_BLEND_SRC_ALPHA; rt.DestBlend = D3D12_BLEND_ONE;
          rt.BlendOp = D3D12_BLEND_OP_ADD;
          rt.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA; rt.DestBlendAlpha = D3D12_BLEND_ONE;
          rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
          break;
        case ALPHA_BLEND:
          rt.BlendEnable = TRUE;
          rt.SrcBlend = D3D12_BLEND_SRC_ALPHA; rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
          rt.BlendOp = D3D12_BLEND_OP_ADD;
          rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
          rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
          break;
        case NON_PREMULTIPLIED:
          rt.BlendEnable = TRUE;
          rt.SrcBlend = D3D12_BLEND_SRC_ALPHA; rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
          rt.BlendOp = D3D12_BLEND_OP_ADD;
          rt.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA; rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
          rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
          break;
      }
    }

    ComPtr<ID3D12PipelineState> psoObj;
    HRESULT hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoObj));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] CreatePSO failed hr=0x%08X shader=%p blend=%d depth=%d cull=%d nRTV=%d fmt0=%d",
           hr, shader, key.blend, key.depth, key.cull, key.numRTVs, key.rtvFormats[0]);
      return nullptr;
    }

    T8_LOG_DEBUG("[D3D12] PSO created: shader=%p blend=%d depth=%d cull=%d nRTV=%d",
                 shader, key.blend, key.depth, key.cull, key.numRTVs);
    m_psoCache[key] = psoObj;
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      TracePSORec rec;
      rec.backend                 = "d3d12";
      rec.shader_id               = g_renderTracer->LookupShaderId(shader);
      rec.shader_key_bits         = shader ? shader->key.bits : 0;
      rec.blend                   = key.blend;
      rec.depth                   = key.depth;
      rec.cull                    = key.cull;
      rec.topology                = key.topology;
      rec.num_color_attachments   = key.numRTVs;
      for (int i = 0; i < key.numRTVs && i < (int)key.rtvFormats.size(); ++i)
        rec.color_formats.push_back((uint32_t)key.rtvFormats[i]);
      rec.depth_format            = (uint32_t)key.dsvFormat;
      rec.vertex_stride           = 0;
      rec.render_pass             = 0;
      g_renderTracer->EvCreatePSO(rec);
    }
#endif
    return psoObj.Get();
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Driver — Core lifecycle
  // ══════════════════════════════════════════════════════

  void D3D12Driver::SetWindow(void* window) { m_hwnd = GetActiveWindow(); }

  void D3D12Driver::SetWindowHandle(const WindowHandle& handle) {
    // Editor host path: honor an explicit HWND (e.g. an editor child window
    // hosting the viewport). Falls back to GetActiveWindow() so the legacy
    // SDL-driven flow keeps behaving exactly as before.
    if (handle.kind == WindowHandle::WIN32_HWND && handle.nativeHandle) {
      m_hwnd = reinterpret_cast<HWND>(handle.nativeHandle);
    } else {
      m_hwnd = GetActiveWindow();
    }
  }
  void D3D12Driver::SetDimensions(int w, int h) { width = w; height = h; }

  bool D3D12Driver::ResizeSwapchain(int newW, int newH) {
    if (newW <= 0 || newH <= 0) return false;
    if (!m_swapChain) return false;

    // Flush all in-flight GPU work
    WaitForGPU();

    // Release back buffer references (but keep the RTV descriptor handles — we reuse them)
    for (UINT i = 0; i < kBackBufferCount; i++)
      m_backBuffers[i].Reset();
    m_depthBuffer.Reset();

    HRESULT hr = m_swapChain->ResizeBuffers(kBackBufferCount, (UINT)newW, (UINT)newH,
                                             DXGI_FORMAT_R8G8B8A8_UNORM,
                                             DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] ResizeBuffers failed (0x%08X)", (unsigned)hr);
      return false;
    }

    // Recreate back buffer RTVs at the same descriptor slots
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    for (UINT i = 0; i < kBackBufferCount; i++) {
      m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
      device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, m_backBufferRTVs[i]);
    }

    // Recreate depth buffer at the same DSV descriptor slot
    width = newW; height = newH;
    D3D12_RESOURCE_DESC dd = {};
    dd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    dd.Width = (UINT)newW; dd.Height = (UINT)newH;
    dd.DepthOrArraySize = 1; dd.MipLevels = 1;
    dd.Format = DXGI_FORMAT_D32_FLOAT; dd.SampleDesc.Count = 1;
    dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv = {}; cv.Format = DXGI_FORMAT_D32_FLOAT; cv.DepthStencil.Depth = 0.0f;
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd,
                                          D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
                                          IID_PPV_ARGS(&m_depthBuffer));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] Depth buffer recreate failed (0x%08X)", (unsigned)hr);
      return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
    dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(m_depthBuffer.Get(), &dv, m_depthDSV);

    // Update current back buffer index, viewport, and scissor
    m_currentBackBuffer = m_swapChain->GetCurrentBackBufferIndex();
    m_viewport = { 0.f, 0.f, (float)newW, (float)newH, 0.f, 1.f };
    m_scissorRect = { 0, 0, (LONG)newW, (LONG)newH };

    T8_LOG_INFO("[D3D12] Swapchain resized to %dx%d", newW, newH);
    return true;
  }

  void D3D12Driver::CreateDevice() {
    // Enable debug layer only when requested (--d3d12debug flag)
    if (g_config.flags.d3d12Debug) {
      ComPtr<ID3D12Debug> debugController;
      if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        T8_LOG_INFO("[D3D12] Debug layer ENABLED (--d3d12debug)");
      } else {
        T8_LOG_ERROR("[D3D12] D3D12GetDebugInterface failed");
      }
    }

    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_dxgiFactory));
    if (FAILED(hr)) {
      hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory));
      if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CreateDXGIFactory failed hr=0x%08X", hr); return; }
    }
    T8_LOG_INFO("[D3D12] DXGI factory created");

    // Check tearing support
    ComPtr<IDXGIFactory5> factory5;
    m_tearingSupported = false;
    if (SUCCEEDED(m_dxgiFactory.As(&factory5))) {
      BOOL allow = FALSE;
      if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) {
        m_tearingSupported = (allow == TRUE);
      }
    }
    T8_LOG_INFO("[D3D12] Tearing support: %s", m_tearingSupported ? "YES" : "NO");

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_dxgiFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
      DXGI_ADAPTER_DESC1 desc; adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                              IID_PPV_ARGS(&static_cast<D3D12Device*>(T8Device)->m_device));
      if (SUCCEEDED(hr)) {
        char name[128]; wcstombs(name, desc.Description, 128);
        T8_LOG_INFO("[D3D12] Adapter: %s", name);
        T8_LOG_INFO("[D3D12] Device created (feature level 11_0)");
        return;
      }
    }
    T8_LOG_ERROR("[D3D12] Failed to create D3D12 device");
  }

  void D3D12Driver::CreateCommandInfrastructure() {
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    D3D12_COMMAND_QUEUE_DESC qDesc = {}; qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT hr = device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CreateCommandQueue failed hr=0x%08X", hr); return; }
    for (UINT i = 0; i < kBackBufferCount; i++) {
      device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]));
      device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[i].Get(), nullptr, IID_PPV_ARGS(&m_commandLists[i]));
      m_commandLists[i]->Close();
    }
    static_cast<D3D12DeviceContext*>(T8DeviceContext)->m_commandList = m_commandLists[0];
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CreateFence failed hr=0x%08X", hr); return; }
    m_nextFenceValue = 1;
    for (UINT i = 0; i < kBackBufferCount; i++) m_frameFenceValues[i] = 0;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    T8_LOG_INFO("[D3D12] Command infrastructure created (%d lists + %d allocators)", kBackBufferCount, kBackBufferCount);
    T8_LOG_INFO("[D3D12] Sync objects created (fence + %u frames in flight)", kBackBufferCount);
  }

  void D3D12Driver::CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC1 sc = {};
    sc.Width = width; sc.Height = height; sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc.SampleDesc.Count = 1; sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = kBackBufferCount; sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (m_tearingSupported)
      sc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = m_dxgiFactory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_hwnd, &sc, nullptr, nullptr, &sc1);
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CreateSwapChain failed hr=0x%08X", hr); return; }
    sc1.As(&m_swapChain);

    // Disable ALT+ENTER fullscreen toggle (interferes with Independent Flip)
    m_dxgiFactory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Set max frame latency for waitable swap chain
    ComPtr<IDXGISwapChain2> sc2;
    if (SUCCEEDED(m_swapChain.As(&sc2))) {
      sc2->SetMaximumFrameLatency(kBackBufferCount);
      m_swapChainWaitableObject = sc2->GetFrameLatencyWaitableObject();
      T8_LOG_INFO("[D3D12] Waitable swap chain enabled (latency=%u)", kBackBufferCount);
    }

    m_currentBackBuffer = m_swapChain->GetCurrentBackBufferIndex();
    T8_LOG_INFO("[D3D12] Swap chain created (%dx%d, %u buffers, tearing=%s)",
                width, height, kBackBufferCount, m_tearingSupported ? "on" : "off");
  }

  void D3D12Driver::CreateHeaps() {
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, true);
    m_heaps[D3D12Heap::CBV_SRV_UAV_NOT_VISIBLE].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512, false);
    m_heaps[D3D12Heap::SAMPLER].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256, true);
    m_heaps[D3D12Heap::RTV].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 128, false);
    m_heaps[D3D12Heap::DSV].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64, false);
    T8_LOG_INFO("[D3D12] Descriptor heaps created (%d)", D3D12Heap::MAX);
  }

  void D3D12Driver::CreateBackBufferViews() {
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    for (UINT i = 0; i < kBackBufferCount; i++) {
      m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
      m_backBufferRTVs[i] = m_heaps[D3D12Heap::RTV].AllocateCPU();
      device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, m_backBufferRTVs[i]);
    }
    T8_LOG_INFO("[D3D12] Back buffer RTVs created");
  }

  void D3D12Driver::CreateDepthBuffer() {
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    D3D12_RESOURCE_DESC dd = {}; dd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    dd.Width = width; dd.Height = height; dd.DepthOrArraySize = 1; dd.MipLevels = 1;
    dd.Format = DXGI_FORMAT_D32_FLOAT; dd.SampleDesc.Count = 1;
    dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv = {}; cv.Format = DXGI_FORMAT_D32_FLOAT; cv.DepthStencil.Depth = 0.0f;
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&m_depthBuffer));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] Depth buffer creation failed hr=0x%08X", hr); return; }
    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_depthDSV = m_heaps[D3D12Heap::DSV].AllocateCPU();
    device->CreateDepthStencilView(m_depthBuffer.Get(), &dv, m_depthDSV);
    T8_LOG_INFO("[D3D12] Depth buffer created (%dx%d)", width, height);
  }

  void D3D12Driver::CreateDefaultSampler() {
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    D3D12_SAMPLER_DESC sd = {};
    sd.Filter = D3D12_FILTER_ANISOTROPIC;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sd.MaxAnisotropy = 16; sd.MaxLOD = D3D12_FLOAT32_MAX;
    sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    m_defaultSamplerCPU = m_heaps[D3D12Heap::SAMPLER].AllocateCPU();
    m_defaultSamplerGPU = m_heaps[D3D12Heap::SAMPLER].AllocateGPU();
    device->CreateSampler(&sd, m_defaultSamplerCPU);
    T8_LOG_INFO("[D3D12] Default sampler created (aniso x16)");
  }

  void D3D12Driver::InitDriver() {
    T8Device = new D3D12Device;
    T8DeviceContext = new D3D12DeviceContext;
    T8_LOG_INFO("[D3D12] >> CreateDevice...");
    CreateDevice();
    T8_LOG_INFO("[D3D12] >> CreateCommandInfrastructure...");
    CreateCommandInfrastructure();
    T8_LOG_INFO("[D3D12] >> CreateSwapChain...");
    CreateSwapChain();
    T8_LOG_INFO("[D3D12] >> CreateHeaps...");
    CreateHeaps();
    T8_LOG_INFO("[D3D12] >> CreateBackBufferViews...");
    CreateBackBufferViews();
    T8_LOG_INFO("[D3D12] >> CreateDepthBuffer...");
    CreateDepthBuffer();
    T8_LOG_INFO("[D3D12] >> CreateDefaultSampler...");
    CreateDefaultSampler();

    // Create per-frame CB ring buffers
    {
      ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
      D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
      D3D12_RESOURCE_DESC rd = {};
      rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      rd.Width = kCBRingBufferSize;
      rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
      rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      for (UINT i = 0; i < kBackBufferCount; i++) {
        HRESULT hrRing = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                         IID_PPV_ARGS(&m_cbRingBuffers[i]));
        if (FAILED(hrRing)) { T8_LOG_ERROR("[D3D12] CB ring buffer[%u] creation failed hr=0x%08X", i, hrRing); return; }
        m_cbRingBuffers[i]->Map(0, nullptr, &m_cbRingMapped[i]);
      }
      T8_LOG_INFO("[D3D12] CB ring buffers created (%u KB x %u)", kCBRingBufferSize / 1024, kBackBufferCount);
    }

    m_viewport = { 0.f, 0.f, (float)width, (float)height, 0.f, 1.f };
    m_scissorRect = { 0, 0, (LONG)width, (LONG)height };

    // Start debug message polling thread only when debug layer is enabled
    if (g_config.flags.d3d12Debug) {
      StartDebugMessageThread();
    }

    T8_LOG_INFO("[D3D12] Driver initialized (%dx%d)", width, height);
  }

  void D3D12Driver::CreateSurfaces() {}
  void D3D12Driver::DestroySurfaces() {}
  void D3D12Driver::Update() {}

  void D3D12Driver::DestroyDriver() {
    StopDebugMessageThread();
    WaitForGPU();
    DestroyShaders(); DestroyRTs(); DestroyTextures();
    m_psoCache.clear();
    for (UINT i = 0; i < kBackBufferCount; i++) {
      if (m_cbRingMapped[i]) { m_cbRingBuffers[i]->Unmap(0, nullptr); m_cbRingMapped[i] = nullptr; }
      m_cbRingBuffers[i].Reset();
    }
    for (int i = 0; i < D3D12Heap::MAX; i++) m_heaps[i].Destroy();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    m_depthBuffer.Reset();
    for (UINT i = 0; i < kBackBufferCount; i++) { m_backBuffers[i].Reset(); m_commandAllocators[i].Reset(); }
    for(auto& cl:m_commandLists)cl.Reset(); m_fence.Reset(); m_commandQueue.Reset(); m_swapChain.Reset();
    T8Device->release(); T8DeviceContext->release();
    delete T8Device; delete T8DeviceContext; T8Device = nullptr; T8DeviceContext = nullptr;
    m_dxgiFactory.Reset();
    T8_LOG_INFO("[D3D12] Driver destroyed");
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Driver — Synchronization
  // ══════════════════════════════════════════════════════

  void D3D12Driver::WaitForFence() {
    const UINT64 fenceToSignal = m_nextFenceValue++;
    m_commandQueue->Signal(m_fence.Get(), fenceToSignal);
    m_frameFenceValues[m_currentBackBuffer] = fenceToSignal;
    if (m_fence->GetCompletedValue() < fenceToSignal) {
      m_fence->SetEventOnCompletion(fenceToSignal, m_fenceEvent);
      WaitForSingleObject(m_fenceEvent, INFINITE);
    }
  }

  void D3D12Driver::WaitForGPU() {
    const UINT64 fenceToSignal = m_nextFenceValue++;
    m_commandQueue->Signal(m_fence.Get(), fenceToSignal);
    if (m_fence->GetCompletedValue() < fenceToSignal) {
      m_fence->SetEventOnCompletion(fenceToSignal, m_fenceEvent);
      WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    for (UINT i = 0; i < kBackBufferCount; i++)
      m_frameFenceValues[i] = fenceToSignal;
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Driver — Frame lifecycle
  // ══════════════════════════════════════════════════════

  void D3D12Driver::BeginFrame() {
    {
      T8_PROFILE_CPU_SCOPE(t850::g_profiler, "D3D12_FenceWait");
      // Wait for the specific backbuffer's fence to ensure its allocator is safe to reset
      const UINT64 lastFenceForThisBuffer = m_frameFenceValues[m_currentBackBuffer];
      if (m_fence->GetCompletedValue() < lastFenceForThisBuffer) {
        m_fence->SetEventOnCompletion(lastFenceForThisBuffer, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
      }
    }

    {
      T8_PROFILE_CPU_SCOPE(t850::g_profiler, "D3D12_CmdListReset");
      auto& cmdList = m_commandLists[m_currentBackBuffer];
      m_commandAllocators[m_currentBackBuffer]->Reset();
      cmdList->Reset(m_commandAllocators[m_currentBackBuffer].Get(), nullptr);
      static_cast<D3D12DeviceContext*>(T8DeviceContext)->m_commandList = cmdList;
      ID3D12DescriptorHeap* heaps[] = {
        m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetHeap(),
        m_heaps[D3D12Heap::SAMPLER].GetHeap()
      };
      cmdList->SetDescriptorHeaps(2, heaps);
    }

    m_cbRingOffset = 0;
    m_dynamicDescriptorOffset = 0;
    m_lastPSO = nullptr;
    m_lastRootSig = nullptr;
    static_cast<D3D12DeviceContext*>(T8DeviceContext)->m_topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  }

  void D3D12Driver::EndFrame() {}

  void D3D12Driver::BuildPipelineObjects() {
    m_dynamicDescriptorBase = m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetCurrentIndex();
    m_dynamicDescriptorOffset = 0;
    T8_LOG_INFO("[D3D12] BuildPipelineObjects: dynamic descriptor base = %llu",
                (unsigned long long)m_dynamicDescriptorBase);
  }

  void D3D12Driver::Clear() {
    if (!m_frameStarted) {
      BeginFrame();
      m_frameStarted = true;

      if ((CurrentRT < 0 || IsCurrentOffscreenTarget()) && BindOffscreenTarget(true))
        return;

      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = m_backBuffers[m_currentBackBuffer].Get();
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      m_commandLists[m_currentBackBuffer]->ResourceBarrier(1, &b);
    }

    if ((CurrentRT < 0 || IsCurrentOffscreenTarget()) && BindOffscreenTarget(true))
      return;

    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size()) {
      const float cc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
      D3D12RT* rt = static_cast<D3D12RT*>(RTs[CurrentRT]);
      for (auto& rtv : rt->vRTVHandles)
        m_commandLists[m_currentBackBuffer]->ClearRenderTargetView(rtv, cc, 0, nullptr);
      if (rt->depthResource)
        m_commandLists[m_currentBackBuffer]->ClearDepthStencilView(rt->depthDSV, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
      T8_TRACE(EvClearRT(CurrentRT,
                         (rt->number_RT > 0 ? 1u : 0u) | (rt->depthResource ? 2u : 0u),
                         cc[0], cc[1], cc[2], cc[3], 0.0f, 0));
    } else {
      m_commandLists[m_currentBackBuffer]->OMSetRenderTargets(1, &m_backBufferRTVs[m_currentBackBuffer], FALSE, &m_depthDSV);
      m_commandLists[m_currentBackBuffer]->RSSetViewports(1, &m_viewport);
      m_commandLists[m_currentBackBuffer]->RSSetScissorRects(1, &m_scissorRect);
      const float cc[4] = { 0.227f, 0.227f, 0.227f, 1.0f };
      m_commandLists[m_currentBackBuffer]->ClearRenderTargetView(m_backBufferRTVs[m_currentBackBuffer], cc, 0, nullptr);
      m_commandLists[m_currentBackBuffer]->ClearDepthStencilView(m_depthDSV, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
      T8_TRACE(EvClearRT(-1, 1u | 2u, cc[0], cc[1], cc[2], cc[3], 0.0f, 0));
    }
  }

  void D3D12Driver::ClearWithColor(float r, float g, float b, float a) {
    const float cc[4] = { r, g, b, a };
    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size()) {
      D3D12RT* rt = static_cast<D3D12RT*>(RTs[CurrentRT]);
      for (auto& rtv : rt->vRTVHandles)
        m_commandLists[m_currentBackBuffer]->ClearRenderTargetView(rtv, cc, 0, nullptr);
      if (rt->depthResource)
        m_commandLists[m_currentBackBuffer]->ClearDepthStencilView(rt->depthDSV, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
      T8_TRACE(EvClearRT(CurrentRT,
                         (rt->number_RT > 0 ? 1u : 0u) | (rt->depthResource ? 2u : 0u),
                         cc[0], cc[1], cc[2], cc[3], 0.0f, 0));
    } else {
      m_commandLists[m_currentBackBuffer]->ClearRenderTargetView(m_backBufferRTVs[m_currentBackBuffer], cc, 0, nullptr);
      m_commandLists[m_currentBackBuffer]->ClearDepthStencilView(m_depthDSV, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
      T8_TRACE(EvClearRT(-1, 1u | 2u, cc[0], cc[1], cc[2], cc[3], 0.0f, 0));
    }
  }

  void D3D12Driver::SwapBuffers() {

    if (IsOffscreenEnabled()) {
      {
        T8_PROFILE_CPU_SCOPE(t850::g_profiler, "D3D12_OffscreenCmdClose+Execute");
        m_commandLists[m_currentBackBuffer]->Close();
        ID3D12CommandList* lists[] = { m_commandLists[m_currentBackBuffer].Get() };
        m_commandQueue->ExecuteCommandLists(1, lists);
      }

      const UINT64 fenceVal = m_nextFenceValue++;
      m_commandQueue->Signal(m_fence.Get(), fenceVal);
      m_frameFenceValues[m_currentBackBuffer] = fenceVal;

      m_frameStarted = false;
      CompleteOffscreenFrame();
      m_currentBackBuffer = (m_currentBackBuffer + 1) % kBackBufferCount;

      if (m_infoQueue) PollDebugMessages();
      return;
    }

    {
      T8_PROFILE_CPU_SCOPE(t850::g_profiler, "D3D12_CmdClose+Execute");
      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = m_backBuffers[m_currentBackBuffer].Get();
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      m_commandLists[m_currentBackBuffer]->ResourceBarrier(1, &b);
      m_commandLists[m_currentBackBuffer]->Close();
      ID3D12CommandList* lists[] = { m_commandLists[m_currentBackBuffer].Get() };
      m_commandQueue->ExecuteCommandLists(1, lists);
    }

    {
      T8_PROFILE_CPU_SCOPE(t850::g_profiler, "D3D12_Present_Call");
      UINT presentFlags = m_tearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0;
      m_swapChain->Present(0, presentFlags);
    }

    // Signal the fence for this frame — DON'T wait here.
    // BeginFrame will wait only when it needs to reuse this buffer's allocator,
    // which gives the GPU a full frame of latency to finish.
    const UINT64 fenceVal = m_nextFenceValue++;
    m_commandQueue->Signal(m_fence.Get(), fenceVal);
    m_frameFenceValues[m_currentBackBuffer] = fenceVal;

    m_currentBackBuffer = m_swapChain->GetCurrentBackBufferIndex();
    m_frameStarted = false;

    if (m_infoQueue) PollDebugMessages();
  }

  void D3D12Driver::SetBlendState(BlendStates state) {
    T8_LOG_TRACE("[D3D12] SetBlendState(%d)", state);
    m_currentBlend = state;
    T8_TRACE(EvSetBlend((int)state));
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

  void D3D12Driver::SetDepthStencilState(DepthStencilStates state) {
    T8_LOG_TRACE("[D3D12] SetDepthStencilState(%d)", state);
    m_currentDepth = state;
    T8_TRACE(EvSetDepth((int)state));
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

  void D3D12Driver::SetCullFace(FaceCulling state) {
    T8_LOG_TRACE("[D3D12] SetCullFace(%d)", state);
    m_currentCull = state; m_FaceCulling = state;
    T8_TRACE(EvSetCull((int)state));
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

#ifdef T850_RENDER_TRACE
  void D3D12Driver::RefreshTracePendingRenderState() {
    if (!T8_TRACE_ACTIVE()) return;
    int numAtt = 1;
    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size() && RTs[CurrentRT])
      numAtt = RTs[CurrentRT]->number_RT > 0 ? RTs[CurrentRT]->number_RT : 1;
    g_renderTracer->RecomputePendingRenderStateD3D12(numAtt);
  }
#endif

  void D3D12Driver::PopRT() {
    T8_TRACE(EvPopRT());
    T8_LOG_TRACE("[D3D12] PopRT (CurrentRT=%d)", CurrentRT);
    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size()) {
      D3D12RT* rt = static_cast<D3D12RT*>(RTs[CurrentRT]);
      for (int i = 0; i < rt->number_RT; i++) {
        if (rt->vColorStates[i] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
          D3D12_RESOURCE_BARRIER b = {};
          b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          b.Transition.pResource = rt->vColorResources[i].Get();
          b.Transition.StateBefore = rt->vColorStates[i];
          b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
          m_commandLists[m_currentBackBuffer]->ResourceBarrier(1, &b);
          rt->vColorStates[i] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
      }
      if (rt->depthResource && rt->depthState == D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = rt->depthResource.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandLists[m_currentBackBuffer]->ResourceBarrier(1, &b);
        rt->depthState = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      }
    }

    if (BindOffscreenTarget(false))
      return;

    m_commandLists[m_currentBackBuffer]->OMSetRenderTargets(1, &m_backBufferRTVs[m_currentBackBuffer], FALSE, &m_depthDSV);
    m_commandLists[m_currentBackBuffer]->RSSetViewports(1, &m_viewport);
    m_commandLists[m_currentBackBuffer]->RSSetScissorRects(1, &m_scissorRect);
    CurrentRT = -1;
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Driver — Readback / Screenshot
  // ══════════════════════════════════════════════════════

  static void SaveD3D12ResourceToPPM(ID3D12Resource* srcResource, D3D12_RESOURCE_STATES currentState,
                                      const std::string& path, D3D12Driver* driver) {
    ID3D12Device* device = GetNativeDevice();
    D3D12_RESOURCE_DESC desc = srcResource->GetDesc();
    UINT w = (UINT)desc.Width;
    UINT h = desc.Height;
    DXGI_FORMAT fmt = desc.Format;

    if (fmt == DXGI_FORMAT_R32_TYPELESS) fmt = DXGI_FORMAT_R32_FLOAT;
    else if (fmt == DXGI_FORMAT_R16_TYPELESS) fmt = DXGI_FORMAT_R16_FLOAT;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0; UINT64 rowSize = 0, totalSize = 0;
    D3D12_RESOURCE_DESC readDesc = desc;
    readDesc.Format = fmt;
    device->GetCopyableFootprints(&readDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &totalSize);

    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = totalSize;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1; bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> readbackBuf;
    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuf));

    ComPtr<ID3D12CommandAllocator> tmpAlloc;
    ComPtr<ID3D12GraphicsCommandList> tmpList;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList));

    if (currentState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = srcResource;
      b.Transition.StateBefore = currentState;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      tmpList->ResourceBarrier(1, &b);
    }

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {}, srcLoc = {};
    dstLoc.pResource = readbackBuf.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;
    srcLoc.pResource = srcResource;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;
    tmpList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    if (currentState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = srcResource;
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
      b.Transition.StateAfter = currentState;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      tmpList->ResourceBarrier(1, &b);
    }

    tmpList->Close();
    ID3D12CommandList* lists[] = { tmpList.Get() };
    driver->GetCmdQueue()->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> tmpFence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence));
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    driver->GetCmdQueue()->Signal(tmpFence.Get(), 1);
    if (tmpFence->GetCompletedValue() < 1) {
      tmpFence->SetEventOnCompletion(1, evt);
      WaitForSingleObject(evt, INFINITE);
    }
    CloseHandle(evt);

    void* mapped = nullptr;
    readbackBuf->Map(0, nullptr, &mapped);

    auto half2float = [](unsigned short h) -> float {
      unsigned int sign = (h >> 15) & 1;
      unsigned int exp  = (h >> 10) & 0x1F;
      unsigned int mant = h & 0x3FF;
      if (exp == 0) return 0.0f;
      if (exp == 31) return sign ? -1e30f : 1e30f;
      float f = powf(2.0f, (float)((int)exp - 15)) * (1.0f + mant / 1024.0f);
      return sign ? -f : f;
    };

    std::vector<unsigned char> rgbBuf(w * h * 3);
    unsigned char* data = (unsigned char*)mapped;

    for (UINT y = 0; y < h; y++) {
      unsigned char* row = data + y * footprint.Footprint.RowPitch;
      for (UINT x = 0; x < w; x++) {
        unsigned int r = 0, g = 0, b_ch = 0;
        if (fmt == DXGI_FORMAT_R8G8B8A8_UNORM) {
          r = row[x * 4]; g = row[x * 4 + 1]; b_ch = row[x * 4 + 2];
        } else if (fmt == DXGI_FORMAT_B8G8R8A8_UNORM) {
          b_ch = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2];
        } else if (fmt == DXGI_FORMAT_R8_UNORM) {
          r = g = b_ch = row[x];
        } else if (fmt == DXGI_FORMAT_R16G16B16A16_FLOAT) {
          const unsigned short* hf = (const unsigned short*)(row) + x * 4;
          float rf = half2float(hf[0]), gf = half2float(hf[1]), bf = half2float(hf[2]);
          rf = rf < 0.f ? 0.f : (rf > 1.f ? 1.f : rf);
          gf = gf < 0.f ? 0.f : (gf > 1.f ? 1.f : gf);
          bf = bf < 0.f ? 0.f : (bf > 1.f ? 1.f : bf);
          r = (unsigned int)(rf * 255.f); g = (unsigned int)(gf * 255.f); b_ch = (unsigned int)(bf * 255.f);
        } else if (fmt == DXGI_FORMAT_R32_FLOAT) {
          float v = *((const float*)(row) + x);
          v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
          r = g = b_ch = (unsigned int)(v * 255.f);
        } else if (fmt == DXGI_FORMAT_R16_FLOAT) {
          float v = half2float(*((const unsigned short*)(row) + x));
          v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
          r = g = b_ch = (unsigned int)(v * 255.f);
        } else {
          r = row[x * 4]; g = row[x * 4 + 1]; b_ch = row[x * 4 + 2];
        }
        unsigned int idx = (y * w + x) * 3;
        rgbBuf[idx] = (unsigned char)r; rgbBuf[idx+1] = (unsigned char)g; rgbBuf[idx+2] = (unsigned char)b_ch;
      }
    }

    D3D12_RANGE emptyRange = { 0, 0 };
    readbackBuf->Unmap(0, &emptyRange);

    std::ofstream out(path + ".ppm", std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    out.write((const char*)rgbBuf.data(), rgbBuf.size());
    T8_LOG_INFO("[D3D12] Saved %s.ppm (%ux%u fmt=%d)", path.c_str(), w, h, fmt);
  }

  void D3D12Driver::SaveScreenshot(std::string path) {
    // The current frame's command list is still open. Close and execute it first,
    // then do the readback, then reopen for any subsequent rendering.
    m_commandLists[m_currentBackBuffer]->Close();
    ID3D12CommandList* lists[] = { m_commandLists[m_currentBackBuffer].Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGPU();

    // Now the backbuffer has the rendered content
    SaveD3D12ResourceToPPM(m_backBuffers[m_currentBackBuffer].Get(),
                            D3D12_RESOURCE_STATE_RENDER_TARGET, path, this);

    // Reopen the command list for any subsequent work in this frame
    m_commandAllocators[m_currentBackBuffer]->Reset();
    m_commandLists[m_currentBackBuffer]->Reset(m_commandAllocators[m_currentBackBuffer].Get(), nullptr);
    ID3D12DescriptorHeap* heaps[] = {
      m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetHeap(),
      m_heaps[D3D12Heap::SAMPLER].GetHeap()
    };
    m_commandLists[m_currentBackBuffer]->SetDescriptorHeaps(2, heaps);

    // Rebind back buffer render targets, viewport, and scissor
    // (OM state is lost after command list reset)
    m_commandLists[m_currentBackBuffer]->OMSetRenderTargets(1, &m_backBufferRTVs[m_currentBackBuffer], FALSE, &m_depthDSV);
    m_commandLists[m_currentBackBuffer]->RSSetViewports(1, &m_viewport);
    m_commandLists[m_currentBackBuffer]->RSSetScissorRects(1, &m_scissorRect);
  }

  void D3D12Driver::SaveRTToFile(int rtID, int attachment, std::string path) {
    if (rtID < 0 || rtID >= (int)RTs.size()) return;
    WaitForGPU();

    D3D12RT* rt = static_cast<D3D12RT*>(RTs[rtID]);
    if (attachment == DEPTH_ATTACHMENT) {
      if (rt->depthResource) {
        SaveD3D12ResourceToPPM(rt->depthResource.Get(), rt->depthState, path, this);
      }
    } else if (attachment >= 0 && attachment < rt->number_RT) {
      SaveD3D12ResourceToPPM(rt->vColorResources[attachment].Get(),
                              rt->vColorStates[attachment], path, this);
    }
  }

  void D3D12Driver::UploadBufferData(ID3D12Resource*, const void*, size_t, D3D12_RESOURCE_STATES) {}

  void D3D12Driver::BindBackBufferNoDSV() {
    T8_LOG_TRACE("[D3D12] BindBackBufferNoDSV: bb=%u viewport=%.0fx%.0f", m_currentBackBuffer, m_viewport.Width, m_viewport.Height);
    // Pass the DSV even for depth-disabled draws — D3D12 is okay with an unused DSV bound
    m_commandLists[m_currentBackBuffer]->OMSetRenderTargets(1, &m_backBufferRTVs[m_currentBackBuffer], FALSE, &m_depthDSV);
    m_commandLists[m_currentBackBuffer]->RSSetViewports(1, &m_viewport);
    m_commandLists[m_currentBackBuffer]->RSSetScissorRects(1, &m_scissorRect);
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Driver — Dynamic CB ring allocator
  // ══════════════════════════════════════════════════════

  D3D12_GPU_VIRTUAL_ADDRESS D3D12Driver::AllocateCBData(const void* data, UINT dataSize) {
    UINT alignedSize = (dataSize + 255) & ~255;
    if (m_cbRingOffset + alignedSize > kCBRingBufferSize) {
      // Wrapping mid-frame would overwrite CB data still being read by earlier
      // draws in the same command list. Crash loud rather than corrupt rendering.
      T8_LOG_ERROR("[D3D12] CB ring buffer overflow! offset=%u + size=%u > %u (peak so far=%u)",
                   m_cbRingOffset, alignedSize, kCBRingBufferSize, m_cbRingPeakUsage);
      assert(false && "D3D12 CB ring buffer overflow — increase kCBRingBufferSize");
      // Fail-safe: return the start of the ring (guaranteed in-bounds, but the
      // current draw will be wrong; better than corrupting prior draws).
      return m_cbRingBuffers[m_currentBackBuffer]->GetGPUVirtualAddress();
    }

    UINT bufIdx = m_currentBackBuffer;
    unsigned char* dst = (unsigned char*)m_cbRingMapped[bufIdx] + m_cbRingOffset;
    memcpy(dst, data, dataSize);

    D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = m_cbRingBuffers[bufIdx]->GetGPUVirtualAddress() + m_cbRingOffset;
    m_cbRingOffset += alignedSize;
    if (m_cbRingOffset > m_cbRingPeakUsage) m_cbRingPeakUsage = m_cbRingOffset;
    return gpuAddr;
  }

  D3D12_GPU_VIRTUAL_ADDRESS D3D12Driver::AllocateRingData(const void* data, UINT dataSize) {
    // Use 256-byte alignment to stay compatible with CBV allocations from the same ring buffer
    UINT alignedSize = (dataSize + 255) & ~255;
    if (m_cbRingOffset + alignedSize > kCBRingBufferSize) {
      T8_LOG_ERROR("[D3D12] Ring buffer overflow! offset=%u + size=%u > %u (peak so far=%u)",
                   m_cbRingOffset, alignedSize, kCBRingBufferSize, m_cbRingPeakUsage);
      assert(false && "D3D12 ring buffer overflow — increase kCBRingBufferSize");
      return m_cbRingBuffers[m_currentBackBuffer]->GetGPUVirtualAddress();
    }

    UINT bufIdx = m_currentBackBuffer;
    unsigned char* dst = (unsigned char*)m_cbRingMapped[bufIdx] + m_cbRingOffset;
    memcpy(dst, data, dataSize);

    D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = m_cbRingBuffers[bufIdx]->GetGPUVirtualAddress() + m_cbRingOffset;
    m_cbRingOffset += alignedSize;
    if (m_cbRingOffset > m_cbRingPeakUsage) m_cbRingPeakUsage = m_cbRingOffset;
    return gpuAddr;
  }

  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Driver::AllocateDynamicCBV(const void* data, UINT dataSize) {
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = AllocateCBData(data, dataSize);

    ID3D12Device* dev = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    auto& heap = m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE];

    uint64_t descIndex = m_dynamicDescriptorBase + m_dynamicDescriptorOffset;
    if (descIndex >= 65536) {
      T8_LOG_ERROR("[D3D12] Dynamic descriptor overflow! base=%llu offset=%llu",
                   (unsigned long long)m_dynamicDescriptorBase, (unsigned long long)m_dynamicDescriptorOffset);
      descIndex = m_dynamicDescriptorBase;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpuH = heap.GetCPUAt(descIndex);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuH = heap.GetGPUAt(descIndex);
    m_dynamicDescriptorOffset++;

    UINT alignedSize = (dataSize + 255) & ~255;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = gpuAddr;
    cbvDesc.SizeInBytes = alignedSize;
    dev->CreateConstantBufferView(&cbvDesc, cpuH);

    return gpuH;
  }

} // namespace t850

#endif // OS_WINDOWS
