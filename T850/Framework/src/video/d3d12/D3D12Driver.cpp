/*********************************************************
* T850 Engine — D3D12 Backend Implementation
*
* Phase 2: Full resource implementations.
* Shader (HLSL compile + reflect + root signature + PSO)
* Texture (upload + SRV)
* Buffers (VB, IB, CB with upload heap)
* Render Targets (color + depth with SRV)
*********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <iostream>
#include <string>
#include <cassert>
#include <chrono>
#include <filesystem>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Defined in App.cpp — runtime flag for D3D12 debug layer
extern bool g_d3d12Debug;
extern std::string g_logFile;

namespace t800 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  // Helper: get D3D12Driver from global
  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }

  // ══════════════════════════════════════════════════════
  //  D3D12Heap (unchanged from Phase 1)
  // ══════════════════════════════════════════════════════

  bool D3D12Heap::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                          uint32_t numDescriptors, bool shaderVisible) {
    m_type = type;
    m_maxDescriptors = numDescriptors;
    m_currentCount = 0;
    m_shaderVisible = shaderVisible;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                               : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] CreateDescriptorHeap failed type=%d hr=0x%08X", type, hr);
      return false;
    }
    m_incrementSize = device->GetDescriptorHandleIncrementSize(type);
    T8_LOG_INFO("[D3D12] Heap type=%d created: %u descriptors, increment=%llu, visible=%d",
                type, numDescriptors, (unsigned long long)m_incrementSize, shaderVisible);
    return true;
  }

  void D3D12Heap::Destroy() { m_heap.Reset(); m_currentCount = 0; }

  D3D12_CPU_DESCRIPTOR_HANDLE D3D12Heap::GetCPUStart() const { return m_heap->GetCPUDescriptorHandleForHeapStart(); }
  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Heap::GetGPUStart() const { return m_heap->GetGPUDescriptorHandleForHeapStart(); }

  D3D12_CPU_DESCRIPTOR_HANDLE D3D12Heap::AllocateCPU() {
    auto h = GetCPUAt(m_currentCount); m_currentCount++; return h;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Heap::AllocateGPU() {
    return GetGPUAt(m_currentCount - 1); // pair with AllocateCPU
  }

  D3D12_CPU_DESCRIPTOR_HANDLE D3D12Heap::GetCPUAt(uint64_t i) const {
    auto h = GetCPUStart(); h.ptr += i * m_incrementSize; return h;
  }
  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Heap::GetGPUAt(uint64_t i) const {
    auto h = GetGPUStart(); h.ptr += i * m_incrementSize; return h;
  }

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

      // Format timestamp
      auto now = std::chrono::system_clock::now();
      auto time_t_now = std::chrono::system_clock::to_time_t(now);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
      struct tm tm_buf;
      localtime_s(&tm_buf, &time_t_now);
      char timeBuf[32];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03d",
               tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms.count());

      // Write to debug log file
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

      // Also emit to the main log for ERROR/CORRUPTION severity
      if (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
          msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
        T8_LOG_ERROR("[D3D12-DBG] [%s] %s", D3D12CategoryToStr(msg->Category), msg->pDescription);
      }
    }

    m_infoQueue->ClearStoredMessages();
  }

  void D3D12Driver::StartDebugMessageThread() {
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();

    // Query InfoQueue from device
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&m_infoQueue));
    if (FAILED(hr) || !m_infoQueue) {
      T8_LOG_ERROR("[D3D12] Failed to get ID3D12InfoQueue — debug messages unavailable");
      return;
    }

    // Configure: don't break on errors (we'll poll instead), store all messages
    m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
    m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
    m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

    // Allow large message storage
    m_infoQueue->SetMessageCountLimit(4096);

    // Determine debug log file path from the main log path
    std::string debugLogPath;
    if (!g_logFile.empty()) {
      std::filesystem::path p(g_logFile);
      std::string stem = p.stem().string();
      std::string ext  = p.extension().string();
      debugLogPath = (p.parent_path() / (stem + "_d3d12debug" + ext)).string();
    } else {
      // Fallback: create in current directory with timestamp
      auto now = std::chrono::system_clock::now();
      auto tt = std::chrono::system_clock::to_time_t(now);
      struct tm tm_buf;
      localtime_s(&tm_buf, &tt);
      char buf[64];
      snprintf(buf, sizeof(buf), "logs/T850_%04d%02d%02d_%02d%02d%02d_d3d12debug.log",
               tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
               tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
      debugLogPath = buf;
      // Ensure directory exists
      std::filesystem::create_directories(std::filesystem::path(debugLogPath).parent_path());
    }

    m_debugLogFile.open(debugLogPath, std::ios::out | std::ios::trunc);
    if (!m_debugLogFile.is_open()) {
      T8_LOG_ERROR("[D3D12] Failed to open debug log: %s", debugLogPath.c_str());
      return;
    }

    // Write header
    m_debugLogFile << "=== D3D12 Debug Layer Messages ===\n";
    m_debugLogFile << "Log file: " << debugLogPath << "\n";
    m_debugLogFile << "GPU-based validation: ENABLED\n";
    m_debugLogFile << "==================================\n\n";
    m_debugLogFile.flush();

    T8_LOG_INFO("[D3D12] Debug message log: %s", debugLogPath.c_str());

    // Start polling thread
    m_debugThreadRunning = true;
    m_debugThread = std::thread([this]() {
      while (m_debugThreadRunning) {
        PollDebugMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60Hz poll
      }
      // Final poll to catch any remaining messages
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

    // Final flush
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
  //  D3D12DeviceContext
  // ══════════════════════════════════════════════════════

  void* D3D12DeviceContext::GetAPIObject() const { return (void*)m_commandList.Get(); }
  void** D3D12DeviceContext::GetAPIObjectReference() const { return nullptr; }
  void D3D12DeviceContext::release() { m_commandList.Reset(); }

  void D3D12DeviceContext::SetPrimitiveTopology(T8_TOPOLOGY::E topology) {
    D3D12_PRIMITIVE_TOPOLOGY t;
    switch (topology) {
      case T8_TOPOLOGY::POINT_LIST:     t = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;    break;
      case T8_TOPOLOGY::LINE_LIST:      t = D3D_PRIMITIVE_TOPOLOGY_LINELIST;     break;
      case T8_TOPOLOGY::LINE_STRIP:     t = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;    break;
      case T8_TOPOLOGY::TRIANLE_LIST:   t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
      case T8_TOPOLOGY::TRIANGLE_STRIP: t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;break;
      default: t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
    }
    m_commandList->IASetPrimitiveTopology(t);
  }

  void D3D12DeviceContext::DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) {
    T8_LOG_TRACE("[D3D12] DrawIndexed(%u, %u, %u)", vertexCount, startIndex, startVertex);
    m_commandList->DrawIndexedInstanced(vertexCount, 1, startIndex, startVertex, 0);
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Device
  // ══════════════════════════════════════════════════════

  void* D3D12Device::GetAPIObject() const { return (void*)m_device.Get(); }
  void** D3D12Device::GetAPIObjectReference() const { return nullptr; }
  void D3D12Device::release() { m_device.Reset(); }

  Buffer* D3D12Device::CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData) {
    T8_LOG_DEBUG("[D3D12] CreateBuffer type=%d size=%d", bufferType, desc.byteWidth);
    Buffer* buf = nullptr;
    switch (bufferType) {
      case T8_BUFFER_TYPE::VERTEX:   buf = new D3D12VertexBuffer;   break;
      case T8_BUFFER_TYPE::INDEX:    buf = new D3D12IndexBuffer;    break;
      case T8_BUFFER_TYPE::CONSTANT: buf = new D3D12ConstantBuffer; break;
    }
    if (buf) buf->Create(*this, desc, initialData);
    return buf;
  }

  ShaderBase* D3D12Device::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key,
                                         const std::string& vs_name, const std::string& fs_name) {
    D3D12Shader* sh = new D3D12Shader();
    if (!sh->CreateShader(src_vs, src_fs, key, vs_name, fs_name)) {
      delete sh;
      return nullptr;
    }
    return sh;
  }

  Texture* D3D12Device::CreateTexture(std::string path) {
    D3D12Texture* tex = new D3D12Texture;
    tex->LoadTexture(path.c_str());
    return tex;
  }

  Texture* D3D12Device::CreateTextureFromMemory(const unsigned char* buff, int w, int h, int channels, std::string name) {
    D3D12Texture* tex = new D3D12Texture;
    tex->LoadFromMemory(buff, w, h, channels);
    return tex;
  }

  Texture* D3D12Device::CreateCubeMap(const unsigned char* buff, int w, int h) {
    D3D12Texture* tex = new D3D12Texture;
    tex->CreateCubeMap(buff, w, h);
    return tex;
  }

  BaseRT* D3D12Device::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips) {
    D3D12RT* rt = new D3D12RT;
    if (rt->LoadRT(nrt, cf, df, w, h, genMips)) return rt;
    delete rt;
    return nullptr;
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Shader
  // ══════════════════════════════════════════════════════

  bool D3D12Shader::BuildRootSignature(ID3D12Device* device,
                                        ID3D12ShaderReflection* vsReflect,
                                        ID3D12ShaderReflection* fsReflect) {
    // Collect all bound resources from both VS and FS
    struct BoundResource { D3D12_DESCRIPTOR_RANGE_TYPE rangeType; UINT reg; UINT space; std::string name; };
    std::vector<BoundResource> resources;
    auto collectResources = [&](ID3D12ShaderReflection* reflect) {
      D3D12_SHADER_DESC sd; reflect->GetDesc(&sd);
      for (UINT i = 0; i < sd.BoundResources; i++) {
        D3D12_SHADER_INPUT_BIND_DESC bd; reflect->GetResourceBindingDesc(i, &bd);
        D3D12_DESCRIPTOR_RANGE_TYPE rt;
        switch (bd.Type) {
          case D3D_SIT_CBUFFER:    rt = D3D12_DESCRIPTOR_RANGE_TYPE_CBV; break;
          case D3D_SIT_TEXTURE:    rt = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; break;
          case D3D_SIT_SAMPLER:    rt = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; break;
          case D3D_SIT_STRUCTURED: rt = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; break;
          default: continue;
        }
        // Dedup by register+type
        bool found = false;
        for (auto& r : resources)
          if (r.rangeType == rt && r.reg == bd.BindPoint) { found = true; break; }
        if (!found)
          resources.push_back({ rt, bd.BindPoint, bd.Space, bd.Name });
      }
    };
    collectResources(vsReflect);
    collectResources(fsReflect);

    // Sort: CBV first, then SRV, then SAMPLER
    std::sort(resources.begin(), resources.end(), [](const BoundResource& a, const BoundResource& b) {
      return a.rangeType < b.rangeType || (a.rangeType == b.rangeType && a.reg < b.reg);
    });

    T8_LOG_DEBUG("[D3D12] Root signature: %d resources", (int)resources.size());

    // Build root parameters — one descriptor table per resource
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges(resources.size());
    std::vector<D3D12_ROOT_PARAMETER>   params(resources.size());

    for (int i = 0; i < (int)resources.size(); i++) {
      auto& r = resources[i];

      ranges[i] = {};
      ranges[i].RangeType = r.rangeType;
      ranges[i].NumDescriptors = 1;
      ranges[i].BaseShaderRegister = r.reg;
      ranges[i].RegisterSpace = r.space;
      ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

      params[i] = {};
      params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      params[i].DescriptorTable.NumDescriptorRanges = 1;
      params[i].DescriptorTable.pDescriptorRanges = &ranges[i];
      params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV && r.reg == 0) cbvSlot = i;
      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV) srvSlots[r.reg] = i;
      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER && r.reg == 0) samplerSlot = i;

      T8_LOG_VERBOSE("[D3D12]   RootParam[%d] type=%d reg=%u name='%s'", i, r.rangeType, r.reg, r.name.c_str());
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = (UINT)params.size();
    rsDesc.pParameters = params.data();
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] SerializeRootSignature failed: %s",
                   errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
      return false;
    }

    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                      IID_PPV_ARGS(&pRootSignature));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] CreateRootSignature failed hr=0x%08X", hr);
      return false;
    }

    T8_LOG_DEBUG("[D3D12] Root signature created: cbvSlot=%d samplerSlot=%d srvSlots=%d",
                 cbvSlot, samplerSlot, (int)srvSlots.size());
    return true;
  }

  bool D3D12Shader::CreateShaderAPI(std::string src_vs, std::string src_fs,
                                     const std::string& vs_name, const std::string& fs_name) {
    ID3D12Device* device = GetNativeDevice();

    // ── Compile VS ──
    {
      ComPtr<ID3DBlob> errBlob;
      HRESULT hr = D3DCompile(src_vs.c_str(), src_vs.size(),
                               vs_name.empty() ? nullptr : vs_name.c_str(),
                               nullptr, nullptr, "VS", "vs_5_0", 0, 0, &VS_blob, &errBlob);
      if (FAILED(hr)) {
        T8_LOG_ERROR("[D3D12] VS compile error: %s", errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
        return false;
      }
      T8_LOG_VERBOSE("[D3D12] VS compiled: %u bytes [%s]", (unsigned)VS_blob->GetBufferSize(), vs_name.c_str());
    }

    // ── Compile FS ──
    {
      ComPtr<ID3DBlob> errBlob;
      HRESULT hr = D3DCompile(src_fs.c_str(), src_fs.size(),
                               fs_name.empty() ? nullptr : fs_name.c_str(),
                               nullptr, nullptr, "FS", "ps_5_0", 0, 0, &FS_blob, &errBlob);
      if (FAILED(hr)) {
        T8_LOG_ERROR("[D3D12] FS compile error: %s", errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
        return false;
      }
      T8_LOG_VERBOSE("[D3D12] FS compiled: %u bytes [%s]", (unsigned)FS_blob->GetBufferSize(), fs_name.c_str());
    }

    // ── Reflect VS for input layout ──
    ComPtr<ID3D12ShaderReflection> vsReflect;
    D3DReflect(VS_blob->GetBufferPointer(), VS_blob->GetBufferSize(), IID_PPV_ARGS(&vsReflect));
    D3D12_SHADER_DESC vsDesc; vsReflect->GetDesc(&vsDesc);

    int offset = 0;
    m_semanticNames.clear();
    VertexDecl.clear();
    // First pass: collect all semantic names (to avoid vector reallocation invalidating pointers)
    for (UINT i = 0; i < vsDesc.InputParameters; i++) {
      D3D12_SIGNATURE_PARAMETER_DESC pd; vsReflect->GetInputParameterDesc(i, &pd);
      m_semanticNames.push_back(pd.SemanticName);
    }
    // Second pass: build input element descs with stable pointers
    for (UINT i = 0; i < vsDesc.InputParameters; i++) {
      D3D12_SIGNATURE_PARAMETER_DESC pd; vsReflect->GetInputParameterDesc(i, &pd);

      D3D12_INPUT_ELEMENT_DESC ie = {};
      ie.SemanticName = m_semanticNames[i].c_str();
      ie.SemanticIndex = pd.SemanticIndex;
      ie.InputSlot = 0;
      ie.AlignedByteOffset = offset;
      ie.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

      if (pd.Mask == 1) {
        ie.Format = (pd.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ? DXGI_FORMAT_R32_FLOAT
                  : (pd.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  ? DXGI_FORMAT_R32_UINT
                  : DXGI_FORMAT_R32_SINT;
        offset += 4;
      } else if (pd.Mask <= 3) {
        ie.Format = (pd.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ? DXGI_FORMAT_R32G32_FLOAT
                  : (pd.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  ? DXGI_FORMAT_R32G32_UINT
                  : DXGI_FORMAT_R32G32_SINT;
        offset += 8;
      } else if (pd.Mask <= 7) {
        ie.Format = (pd.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ? DXGI_FORMAT_R32G32B32_FLOAT
                  : (pd.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  ? DXGI_FORMAT_R32G32B32_UINT
                  : DXGI_FORMAT_R32G32B32_SINT;
        offset += 12;
      } else {
        ie.Format = (pd.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ? DXGI_FORMAT_R32G32B32A32_FLOAT
                  : (pd.ComponentType == D3D_REGISTER_COMPONENT_UINT32)  ? DXGI_FORMAT_R32G32B32A32_UINT
                  : DXGI_FORMAT_R32G32B32A32_SINT;
        offset += 16;
      }
      VertexDecl.push_back(ie);
    }
    vertexStride = offset;
    T8_LOG_VERBOSE("[D3D12] Input layout: %d elements, stride=%d", (int)VertexDecl.size(), vertexStride);

    // ── Reflect FS ──
    ComPtr<ID3D12ShaderReflection> fsReflect;
    D3DReflect(FS_blob->GetBufferPointer(), FS_blob->GetBufferSize(), IID_PPV_ARGS(&fsReflect));

    // ── Build root signature from reflection ──
    if (!BuildRootSignature(device, vsReflect.Get(), fsReflect.Get())) return false;

    T8_LOG_INFO("[D3D12] Shader created: key=0x%08X stride=%d rootParams: cbv=%d sampler=%d srvs=%d",
                key.bits, vertexStride, cbvSlot, samplerSlot, (int)srvSlots.size());
    return true;
  }

  void D3D12Shader::Set(const DeviceContext& deviceContext) {
    T8_LOG_TRACE("[D3D12] Shader::Set key=0x%08X", key.bits);
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = (ShaderBase*)this;

    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* driver = GetD3D12Driver();

    // Set root signature
    cmdList->SetGraphicsRootSignature(pRootSignature.Get());

    // Determine current RT configuration for PSO
    uint8_t numRTVs = 1;
    DXGI_FORMAT rtvFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT dsvFmt = DXGI_FORMAT_D32_FLOAT;

    int curRT = driver->CurrentRT;
    if (curRT >= 0 && curRT < (int)driver->RTs.size()) {
      D3D12RT* rt = static_cast<D3D12RT*>(driver->RTs[curRT]);
      // Use the RT's actual color count; depth-only uses 0
      numRTVs = (uint8_t)(rt->number_RT > 0 ? rt->number_RT : 0);
      if (!rt->vColorResources.empty()) {
        D3D12_RESOURCE_DESC desc = rt->vColorResources[0]->GetDesc();
        rtvFmt = desc.Format;
      } else if (rt->number_RT == 0) {
        rtvFmt = DXGI_FORMAT_UNKNOWN; // depth-only — no color
      }
    }
    // Back buffer: 1 RTV R8G8B8A8
    // Depth-only RT: 0 RTVs with UNKNOWN format

    // Get or create PSO for current state
    ID3D12PipelineState* pso = driver->GetOrCreatePSO(this, numRTVs, rtvFmt, dsvFmt);
    if (pso) {
      cmdList->SetPipelineState(pso);
    }

    // Bind default sampler if shader uses one
    if (samplerSlot >= 0) {
      cmdList->SetGraphicsRootDescriptorTable(samplerSlot, driver->GetDefaultSamplerGPU());
    }
  }

  void D3D12Shader::DestroyAPIShader() {
    VS_blob.Reset(); FS_blob.Reset();
    pRootSignature.Reset();
    VertexDecl.clear(); m_semanticNames.clear();
    srvSlots.clear();
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Texture
  // ══════════════════════════════════════════════════════

  void D3D12Texture::LoadAPITexture(DeviceContext* context, unsigned char* buffer) {
    ID3D12Device* device = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    int bytesPerPixel = 4;
    if (this->props & TEXT_BASIC_FORMAT::CH_ALPHA) { fmt = DXGI_FORMAT_R8_UNORM; bytesPerPixel = 1; }
    if (cil_props & CIL_HALF_FLOAT) { fmt = DXGI_FORMAT_R16G16B16A16_FLOAT; bytesPerPixel = 8; }

    bool isCube = (cil_props & CIL_CUBE_MAP) != 0;
    UINT arraySize = isCube ? 6 : 1;

    // Create texture resource
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = this->x;
    texDesc.Height = this->y;
    texDesc.DepthOrArraySize = arraySize;
    texDesc.MipLevels = 1;
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&pTexResource));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] Texture CreateCommittedResource failed hr=0x%08X (%ux%u)", hr, this->x, this->y);
      this->id = (unsigned)-1; return;
    }

    // Upload via staging buffer
    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[6];
    UINT numRows[6]; UINT64 rowSizes[6];
    device->GetCopyableFootprints(&texDesc, 0, arraySize, 0, footprints, numRows, rowSizes, &uploadSize);

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1; uploadDesc.DepthOrArraySize = 1; uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap = {}; uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> uploadBuf;
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));

    // Map and copy
    void* mapped = nullptr;
    uploadBuf->Map(0, nullptr, &mapped);
    UINT srcPitch = this->x * bytesPerPixel;
    for (UINT face = 0; face < arraySize; face++) {
      auto& fp = footprints[face];
      unsigned char* src = buffer + face * (this->x * this->y * bytesPerPixel);
      unsigned char* dst = (unsigned char*)mapped + fp.Offset;
      for (UINT row = 0; row < numRows[face]; row++) {
        memcpy(dst + row * fp.Footprint.RowPitch, src + row * srcPitch,
               (size_t)(srcPitch < fp.Footprint.RowPitch ? srcPitch : fp.Footprint.RowPitch));
      }
    }
    uploadBuf->Unmap(0, nullptr);

    // Copy via temp command list
    ComPtr<ID3D12CommandAllocator> tmpAlloc;
    ComPtr<ID3D12GraphicsCommandList> tmpList;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList));

    for (UINT face = 0; face < arraySize; face++) {
      D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
      dst.pResource = pTexResource.Get();
      dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      dst.SubresourceIndex = face;
      src.pResource = uploadBuf.Get();
      src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      src.PlacedFootprint = footprints[face];
      tmpList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    // Barrier: COPY_DEST → PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pTexResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    tmpList->ResourceBarrier(1, &barrier);

    tmpList->Close();
    ID3D12CommandList* lists[] = { tmpList.Get() };
    driver->GetCmdQueue()->ExecuteCommandLists(1, lists);

    // Fence wait for upload
    ComPtr<ID3D12Fence> tmpFence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence));
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    driver->GetCmdQueue()->Signal(tmpFence.Get(), 1);
    if (tmpFence->GetCompletedValue() < 1) {
      tmpFence->SetEventOnCompletion(1, evt);
      WaitForSingleObject(evt, INFINITE);
    }
    CloseHandle(evt);

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (isCube) {
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      srvDesc.TextureCube.MipLevels = 1;
    } else {
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = 1;
    }

    srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
    device->CreateShaderResourceView(pTexResource.Get(), &srvDesc, srvCPU);

    static int texId = 0;
    this->id = texId++;

    T8_LOG_DEBUG("[D3D12] Texture created: '%s' -> slot %d (%ux%u, fmt=%d%s)",
                 filepath.c_str(), this->id, this->x, this->y, fmt, isCube ? ", cube" : "");
  }

  void D3D12Texture::LoadAPITextureCompressed(unsigned char* buffer) {
    ID3D12Device* device = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    DXGI_FORMAT fmt = DXGI_FORMAT_BC1_UNORM;
    int blockSize = 8;
    if (cil_props & CIL_DXT3) { fmt = DXGI_FORMAT_BC2_UNORM; blockSize = 16; }
    else if (cil_props & CIL_DXT5) { fmt = DXGI_FORMAT_BC3_UNORM; blockSize = 16; }

    bool isCube = (cil_props & CIL_CUBE_MAP) != 0;
    UINT numFaces = isCube ? 6 : 1;
    UINT mipCount = (mipmaps > 0) ? mipmaps : 1;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = this->x; texDesc.Height = this->y;
    texDesc.DepthOrArraySize = numFaces;
    texDesc.MipLevels = mipCount;
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;

    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&pTexResource));
    if (FAILED(hr)) { this->id = (unsigned)-1; T8_LOG_ERROR("[D3D12] Compressed tex create failed"); return; }

    UINT totalSubs = numFaces * mipCount;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(totalSubs);
    std::vector<UINT> numRows(totalSubs);
    std::vector<UINT64> rowSizes(totalSubs);
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, totalSubs, 0, footprints.data(), numRows.data(), rowSizes.data(), &uploadSize);

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1; uploadDesc.DepthOrArraySize = 1; uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1; uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap = {}; uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> uploadBuf;
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));

    void* mapped = nullptr;
    uploadBuf->Map(0, nullptr, &mapped);
    unsigned char* pData = buffer;
    for (UINT face = 0; face < numFaces; face++) {
      int w = this->x, h = this->y;
      for (UINT mip = 0; mip < mipCount; mip++) {
        UINT sub = face * mipCount + mip;
        int wBlocks = (w + 3) / 4; if (wBlocks < 1) wBlocks = 1;
        int hBlocks = (h + 3) / 4; if (hBlocks < 1) hBlocks = 1;
        UINT srcPitch = wBlocks * blockSize;
        auto& fp = footprints[sub];
        unsigned char* dst = (unsigned char*)mapped + fp.Offset;
        for (int row = 0; row < hBlocks; row++) {
          memcpy(dst + row * fp.Footprint.RowPitch, pData + row * srcPitch, srcPitch);
        }
        pData += wBlocks * hBlocks * blockSize;
        w >>= 1; if (w < 1) w = 1;
        h >>= 1; if (h < 1) h = 1;
      }
    }
    uploadBuf->Unmap(0, nullptr);

    // Copy + transition
    ComPtr<ID3D12CommandAllocator> tmpAlloc;
    ComPtr<ID3D12GraphicsCommandList> tmpList;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList));

    for (UINT sub = 0; sub < totalSubs; sub++) {
      D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
      dst.pResource = pTexResource.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = sub;
      src.pResource = uploadBuf.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprints[sub];
      tmpList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pTexResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    tmpList->ResourceBarrier(1, &barrier);
    tmpList->Close();

    ID3D12CommandList* lists[] = { tmpList.Get() };
    driver->GetCmdQueue()->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> tmpFence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence));
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    driver->GetCmdQueue()->Signal(tmpFence.Get(), 1);
    if (tmpFence->GetCompletedValue() < 1) { tmpFence->SetEventOnCompletion(1, evt); WaitForSingleObject(evt, INFINITE); }
    CloseHandle(evt);

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (isCube) { srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; srvDesc.TextureCube.MipLevels = mipCount; }
    else { srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srvDesc.Texture2D.MipLevels = mipCount; }

    srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
    device->CreateShaderResourceView(pTexResource.Get(), &srvDesc, srvCPU);

    static int texId = 0;
    this->id = texId++;
    T8_LOG_DEBUG("[D3D12] Compressed texture created: '%s' -> slot %d (%ux%u, fmt=%d, mips=%u)",
                 filepath.c_str(), this->id, this->x, this->y, fmt, mipCount);
  }

  void D3D12Texture::DestroyAPITexture() { pTexResource.Reset(); }
  void D3D12Texture::SetTextureParams() { /* Sampler is shared, set at driver level */ }
  void D3D12Texture::GetFormatBpp(unsigned int&, unsigned int&, unsigned int&) {}

  void D3D12Texture::Set(const DeviceContext& deviceContext, unsigned int slot, std::string shaderTextureName) {
    T8_LOG_TRACE("[D3D12] Texture::Set slot=%u name='%s' file='%s'", slot, shaderTextureName.c_str(), filepath.c_str());
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* shader = static_cast<D3D12Shader*>(deviceContext.actualShaderSet);
    if (!shader) return;

    auto it = shader->srvSlots.find(slot);
    if (it != shader->srvSlots.end()) {
      cmdList->SetGraphicsRootDescriptorTable(it->second, srvGPU);
    }
  }

  void D3D12Texture::SetSampler(const DeviceContext&, unsigned int) {
    // Sampler is set globally via D3D12Shader::Set
  }

  // ══════════════════════════════════════════════════════
  //  D3D12 Buffers
  // ══════════════════════════════════════════════════════

  // ── Vertex Buffer ──
  void* D3D12VertexBuffer::GetAPIObject() const { return (void*)m_buffer.Get(); }
  void** D3D12VertexBuffer::GetAPIObjectReference() const { return nullptr; }

  void D3D12VertexBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    ID3D12Device* dev = GetNativeDevice();

    // Always create as upload heap for simplicity (dynamic)
    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = desc.byteWidth;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = dev->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&m_buffer));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] VB create failed hr=0x%08X", hr); return; }

    m_buffer->Map(0, nullptr, &m_mappedData);
    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      memcpy(m_mappedData, initialData, desc.byteWidth);
    }

    m_view.BufferLocation = m_buffer->GetGPUVirtualAddress();
    m_view.SizeInBytes = desc.byteWidth;
    m_view.StrideInBytes = 0; // Set during Set() call
    T8_LOG_DEBUG("[D3D12] VB created: %d bytes", desc.byteWidth);
  }

  void D3D12VertexBuffer::Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) {
    T8_LOG_TRACE("[D3D12] VB::Set stride=%u", stride);
    const_cast<DeviceContext*>(&deviceContext)->actualVertexBuffer = (VertexBuffer*)this;
    m_view.StrideInBytes = stride;
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    cmdList->IASetVertexBuffers(0, 1, &m_view);
  }

  void D3D12VertexBuffer::UpdateFromSystemCopy(const DeviceContext&) {
    if (m_mappedData && !sysMemCpy.empty())
      memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }
  void D3D12VertexBuffer::UpdateFromBuffer(const DeviceContext&, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }
  void D3D12VertexBuffer::release() {
    if (m_mappedData) { m_buffer->Unmap(0, nullptr); m_mappedData = nullptr; }
    m_buffer.Reset(); sysMemCpy.clear(); delete this;
  }

  // ── Index Buffer ──
  void* D3D12IndexBuffer::GetAPIObject() const { return (void*)m_buffer.Get(); }
  void** D3D12IndexBuffer::GetAPIObjectReference() const { return nullptr; }

  void D3D12IndexBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    ID3D12Device* dev = GetNativeDevice();

    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = desc.byteWidth;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = dev->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&m_buffer));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] IB create failed hr=0x%08X", hr); return; }

    m_buffer->Map(0, nullptr, &m_mappedData);
    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      memcpy(m_mappedData, initialData, desc.byteWidth);
    }

    m_view.BufferLocation = m_buffer->GetGPUVirtualAddress();
    m_view.SizeInBytes = desc.byteWidth;
    m_view.Format = DXGI_FORMAT_R32_UINT;
    T8_LOG_DEBUG("[D3D12] IB created: %d bytes", desc.byteWidth);
  }

  void D3D12IndexBuffer::Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format) {
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    m_view.Format = (format == T8_IB_FORMAR::R16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    cmdList->IASetIndexBuffer(&m_view);
  }

  void D3D12IndexBuffer::UpdateFromSystemCopy(const DeviceContext&) {
    if (m_mappedData && !sysMemCpy.empty()) memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }
  void D3D12IndexBuffer::UpdateFromBuffer(const DeviceContext&, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }
  void D3D12IndexBuffer::release() {
    if (m_mappedData) { m_buffer->Unmap(0, nullptr); m_mappedData = nullptr; }
    m_buffer.Reset(); sysMemCpy.clear(); delete this;
  }

  // ── Constant Buffer ──
  void* D3D12ConstantBuffer::GetAPIObject() const { return (void*)m_buffer.Get(); }
  void** D3D12ConstantBuffer::GetAPIObjectReference() const { return nullptr; }

  void D3D12ConstantBuffer::Create(const Device& device, BufferDesc desc, void* initialData) {
    descriptor = desc;
    ID3D12Device* dev = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    m_alignedSize = (desc.byteWidth + 255) & ~255; // 256-byte aligned

    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = m_alignedSize;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = dev->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&m_buffer));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CB create failed hr=0x%08X size=%d", hr, m_alignedSize); return; }

    m_buffer->Map(0, nullptr, &m_mappedData);

    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      memcpy(m_mappedData, initialData, desc.byteWidth);
    }

    // Create CBV in shader-visible heap
    m_cpuHandle = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    m_gpuHandle = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_buffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = m_alignedSize;
    dev->CreateConstantBufferView(&cbvDesc, m_cpuHandle);

    T8_LOG_DEBUG("[D3D12] CB created: %d bytes (aligned=%d)", desc.byteWidth, m_alignedSize);
  }

  void D3D12ConstantBuffer::Set(const DeviceContext& deviceContext) {
    const_cast<DeviceContext*>(&deviceContext)->actualConstantBuffer = (ConstantBuffer*)this;
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* shader = static_cast<D3D12Shader*>(deviceContext.actualShaderSet);
    if (shader && shader->cbvSlot >= 0 && !sysMemCpy.empty()) {
      // Allocate a per-draw CBV: copies data to ring buffer + creates temp descriptor
      auto* driver = GetD3D12Driver();
      D3D12_GPU_DESCRIPTOR_HANDLE gpuH = driver->AllocateDynamicCBV(sysMemCpy.data(), (UINT)sysMemCpy.size());
      T8_LOG_TRACE("[D3D12] CB::Set cbvSlot=%d gpuH=0x%llX dataSize=%d first4floats=[%f,%f,%f,%f]",
                   shader->cbvSlot, gpuH.ptr, (int)sysMemCpy.size(),
                   sysMemCpy.size() >= 16 ? *(float*)&sysMemCpy[0] : 0.f,
                   sysMemCpy.size() >= 16 ? *(float*)&sysMemCpy[4] : 0.f,
                   sysMemCpy.size() >= 16 ? *(float*)&sysMemCpy[8] : 0.f,
                   sysMemCpy.size() >= 16 ? *(float*)&sysMemCpy[12] : 0.f);
      cmdList->SetGraphicsRootDescriptorTable(shader->cbvSlot, gpuH);
    }
  }

  void D3D12ConstantBuffer::UpdateFromSystemCopy(const DeviceContext&) {
    if (m_mappedData && !sysMemCpy.empty()) memcpy(m_mappedData, sysMemCpy.data(), sysMemCpy.size());
  }
  void D3D12ConstantBuffer::UpdateFromBuffer(const DeviceContext&, const void* buffer) {
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    if (m_mappedData) memcpy(m_mappedData, buffer, descriptor.byteWidth);
  }
  void D3D12ConstantBuffer::release() {
    if (m_mappedData) { m_buffer->Unmap(0, nullptr); m_mappedData = nullptr; }
    m_buffer.Reset(); sysMemCpy.clear(); delete this;
  }

  // ══════════════════════════════════════════════════════
  //  D3D12RT — Render Target
  // ══════════════════════════════════════════════════════

  bool D3D12RT::LoadAPIRT() {
    ID3D12Device* device = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    DXGI_FORMAT cfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    switch (color_format) {
      case BaseRT::NOTHING: cfmt = DXGI_FORMAT_R8G8B8A8_UNORM; number_RT = 0; break;
      case BaseRT::R8:      cfmt = DXGI_FORMAT_R8_UNORM; break;
      case BaseRT::F16:     cfmt = DXGI_FORMAT_R16_FLOAT; break;
      case BaseRT::F32:     cfmt = DXGI_FORMAT_R32_FLOAT; break;
      case BaseRT::RGBA8:   cfmt = DXGI_FORMAT_R8G8B8A8_UNORM; break;
      case BaseRT::RGBA16F: cfmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
      case BaseRT::RGBA32F: cfmt = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
      default: break;
    }

    DXGI_FORMAT depthFmt = DXGI_FORMAT_R32_TYPELESS;
    DXGI_FORMAT dsvFmt = DXGI_FORMAT_D32_FLOAT;
    DXGI_FORMAT srvDepthFmt = DXGI_FORMAT_R32_FLOAT;
    isCubeDepth = (depth_format == BaseRT::CUBE_F32);

    // ── Color attachments ──
    for (int i = 0; i < number_RT; i++) {
      D3D12_RESOURCE_DESC desc = {};
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Width = w; desc.Height = h; desc.DepthOrArraySize = 1;
      desc.MipLevels = 1; desc.Format = cfmt;
      desc.SampleDesc.Count = 1;
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

      D3D12_CLEAR_VALUE clearVal = {}; clearVal.Format = cfmt;
      D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

      ComPtr<ID3D12Resource> colorRes;
      HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
                                                    IID_PPV_ARGS(&colorRes));
      if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] RT color[%d] create failed hr=0x%08X", i, hr); return false; }
      vColorResources.push_back(colorRes);
      vColorStates.push_back(D3D12_RESOURCE_STATE_RENDER_TARGET);

      // RTV
      D3D12_CPU_DESCRIPTOR_HANDLE rtv = driver->GetHeap(D3D12Heap::RTV).AllocateCPU();
      device->CreateRenderTargetView(colorRes.Get(), nullptr, rtv);
      vRTVHandles.push_back(rtv);

      // SRV for reading as texture
      D3D12Texture* colorTex = new D3D12Texture;
      colorTex->pTexResource = colorRes;
      colorTex->x = w; colorTex->y = h;
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = cfmt;
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = 1;
      srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      colorTex->srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
      colorTex->srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
      device->CreateShaderResourceView(colorRes.Get(), &srvDesc, colorTex->srvCPU);
      vColorTextures.push_back(colorTex);

      T8_LOG_DEBUG("[D3D12] RT color[%d] created: %dx%d fmt=%d", i, w, h, cfmt);
    }

    // ── Depth attachment ──
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = w; depthDesc.Height = h;
    depthDesc.DepthOrArraySize = isCubeDepth ? 6 : 1;
    depthDesc.MipLevels = 1; depthDesc.Format = depthFmt;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {}; depthClear.Format = dsvFmt;
    depthClear.DepthStencil.Depth = 1.0f;
    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                                  D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                                                  IID_PPV_ARGS(&depthResource));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] RT depth create failed hr=0x%08X", hr); return false; }
    depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    if (isCubeDepth) {
      for (int face = 0; face < 6; face++) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = dsvFmt;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.FirstArraySlice = face;
        dsvDesc.Texture2DArray.ArraySize = 1;
        cubeFaceDSVs[face] = driver->GetHeap(D3D12Heap::DSV).AllocateCPU();
        device->CreateDepthStencilView(depthResource.Get(), &dsvDesc, cubeFaceDSVs[face]);
      }
      depthDSV = cubeFaceDSVs[0];
    } else {
      D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
      dsvDesc.Format = dsvFmt;
      dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
      depthDSV = driver->GetHeap(D3D12Heap::DSV).AllocateCPU();
      device->CreateDepthStencilView(depthResource.Get(), &dsvDesc, depthDSV);
    }

    // Depth SRV
    D3D12Texture* depthTex = new D3D12Texture;
    depthTex->pTexResource = depthResource;
    depthTex->x = w; depthTex->y = h;
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = srvDepthFmt;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (isCubeDepth) {
      depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      depthSrvDesc.TextureCube.MipLevels = 1;
    } else {
      depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      depthSrvDesc.Texture2D.MipLevels = 1;
    }
    depthTex->srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
    depthTex->srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
    device->CreateShaderResourceView(depthResource.Get(), &depthSrvDesc, depthTex->srvCPU);
    pDepthTexture = depthTex;

    T8_LOG_INFO("[D3D12] RT created: %dx%d, %d colors (fmt=%d), depth (cube=%d)", w, h, number_RT, cfmt, isCubeDepth);
    return true;
  }

  void D3D12RT::DestroyAPIRT() {
    if (pDepthTexture) { pDepthTexture->release(); pDepthTexture = nullptr; }
    for (auto* t : vColorTextures) t->release();
    vColorTextures.clear();
    vColorResources.clear();
    vRTVHandles.clear();
    depthResource.Reset();
  }

  void D3D12RT::Set(const DeviceContext& context) {
    auto* cmdList = static_cast<const D3D12DeviceContext*>(&context)->GetCommandList();
    auto* driver = GetD3D12Driver();

    T8_LOG_TRACE("[D3D12] RT::Set %dx%d colors=%d depth=%s", w, h, number_RT, isCubeDepth ? "cube" : "2D");

    // Transition colors to RENDER_TARGET if needed
    for (int i = 0; i < number_RT; i++) {
      if (vColorStates[i] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = vColorResources[i].Get();
        b.Transition.StateBefore = vColorStates[i];
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &b);
        vColorStates[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
    }

    // Transition depth to DEPTH_WRITE if needed
    if (depthResource && depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = depthResource.Get();
      b.Transition.StateBefore = depthState;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      cmdList->ResourceBarrier(1, &b);
      depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    // Set render targets
    if (number_RT > 0)
      cmdList->OMSetRenderTargets(number_RT, vRTVHandles.data(), FALSE, &depthDSV);
    else
      cmdList->OMSetRenderTargets(0, nullptr, FALSE, &depthDSV);

    // Viewport + scissor
    D3D12_VIEWPORT vp = { 0.f, 0.f, (float)w, (float)h, 0.f, 1.f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &sc);

    // Clear
    float black[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < number_RT; i++)
      cmdList->ClearRenderTargetView(vRTVHandles[i], black, 0, nullptr);
    cmdList->ClearDepthStencilView(depthDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  }

  void D3D12RT::ChangeCubeDepthTexture(int i) {
    if (!isCubeDepth || i < 0 || i >= 6) return;
    depthDSV = cubeFaceDSVs[i];
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Driver — PSO Cache
  // ══════════════════════════════════════════════════════

  ID3D12PipelineState* D3D12Driver::GetOrCreatePSO(D3D12Shader* shader, uint8_t numRTVs,
                                                     DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat) {
    // Normalize: depth-only passes use 0 RTVs with UNKNOWN format
    if (rtvFormat == DXGI_FORMAT_UNKNOWN) {
      numRTVs = 0;
      rtvFormat = DXGI_FORMAT_UNKNOWN;
    }

    D3D12PipelineKey key = {};
    key.shaderKey = shader->key.bits;
    key.blend = (uint8_t)m_currentBlend;
    key.depth = (uint8_t)m_currentDepth;
    key.cull = (uint8_t)m_currentCull;
    key.numRTVs = numRTVs;
    key.rtvFormat = rtvFormat;

    auto it = m_psoCache.find(key);
    if (it != m_psoCache.end()) return it->second.Get();

    // Create new PSO
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
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = numRTVs;
    for (int i = 0; i < numRTVs; i++) pso.RTVFormats[i] = rtvFormat;
    pso.DSVFormat = dsvFormat;

    // Rasterizer — match D3D11 default (CullMode=BACK, FrontCounterClockwise=FALSE)
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
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        break;
      case READ:
        pso.DepthStencilState.DepthEnable = TRUE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
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
          rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_ZERO;
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
      T8_LOG_ERROR("[D3D12] CreatePSO failed hr=0x%08X key=0x%08X blend=%d depth=%d cull=%d nRTV=%d fmt=%d inputElems=%d",
                   hr, key.shaderKey, key.blend, key.depth, key.cull, key.numRTVs, key.rtvFormat,
                   (int)shader->VertexDecl.size());
      // Log PSO details for debugging
      T8_LOG_ERROR("[D3D12]   VS size=%u FS size=%u rootSig=%p stride=%d",
                   (unsigned)shader->VS_blob->GetBufferSize(), (unsigned)shader->FS_blob->GetBufferSize(),
                   shader->pRootSignature.Get(), shader->vertexStride);
      for (int i = 0; i < (int)shader->VertexDecl.size(); i++) {
        auto& e = shader->VertexDecl[i];
        T8_LOG_ERROR("[D3D12]   InputElem[%d] Semantic='%s' Idx=%u Fmt=%u Offset=%u",
                     i, e.SemanticName, e.SemanticIndex, (unsigned)e.Format, e.AlignedByteOffset);
      }
      return nullptr;
    }

    T8_LOG_DEBUG("[D3D12] PSO created: shader=0x%08X blend=%d depth=%d cull=%d nRTV=%d",
                 key.shaderKey, key.blend, key.depth, key.cull, key.numRTVs);
    m_psoCache[key] = psoObj;
    return psoObj.Get();
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Driver — Core lifecycle (mostly unchanged from Phase 1)
  // ══════════════════════════════════════════════════════

  void D3D12Driver::SetWindow(void* window) { m_hwnd = GetActiveWindow(); }
  void D3D12Driver::SetDimensions(int w, int h) { width = w; height = h; }

  void D3D12Driver::CreateDevice() {
    if (g_d3d12Debug) {
      ComPtr<ID3D12Debug> debugController;
      if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        T8_LOG_INFO("[D3D12] Debug layer ENABLED (--d3d12debug)");
        // GPU-based validation disabled — too slow with many shaders (minutes per PSO)
        // Enable manually for targeted debugging if needed
        // ComPtr<ID3D12Debug1> debug1;
        // if (SUCCEEDED(debugController.As(&debug1))) {
        //   debug1->SetEnableGPUBasedValidation(TRUE);
        // }
      } else {
        T8_LOG_ERROR("[D3D12] D3D12GetDebugInterface failed");
      }
    }

    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory));
    if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] CreateDXGIFactory1 failed hr=0x%08X", hr); return; }

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
    device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&m_commandQueue));
    for (UINT i = 0; i < kBackBufferCount; i++)
      device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList));
    m_commandList->Close();
    static_cast<D3D12DeviceContext*>(T8DeviceContext)->m_commandList = m_commandList;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_nextFenceValue = 1;
    m_frameFenceValues[0] = m_frameFenceValues[1] = 0;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    T8_LOG_INFO("[D3D12] Command infrastructure created");
  }

  void D3D12Driver::CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC1 sc = {};
    sc.Width = width; sc.Height = height; sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc.SampleDesc.Count = 1; sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = kBackBufferCount; sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> sc1;
    m_dxgiFactory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_hwnd, &sc, nullptr, nullptr, &sc1);
    sc1.As(&m_swapChain);
    m_currentBackBuffer = m_swapChain->GetCurrentBackBufferIndex();
    T8_LOG_INFO("[D3D12] Swap chain created (%dx%d, %u buffers)", width, height, kBackBufferCount);
  }

  void D3D12Driver::CreateHeaps() {
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, true);
    m_heaps[D3D12Heap::CBV_SRV_UAV_NOT_VISIBLE].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512, false);
    m_heaps[D3D12Heap::SAMPLER].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 64, true);
    m_heaps[D3D12Heap::RTV].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 128, false);
    m_heaps[D3D12Heap::DSV].Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64, false);
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
    D3D12_CLEAR_VALUE cv = {}; cv.Format = DXGI_FORMAT_D32_FLOAT; cv.DepthStencil.Depth = 1.0f;
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&m_depthBuffer));
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
    CreateDevice();
    CreateCommandInfrastructure();
    CreateSwapChain();
    CreateHeaps();
    CreateBackBufferViews();
    CreateDepthBuffer();
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
        device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                         IID_PPV_ARGS(&m_cbRingBuffers[i]));
        m_cbRingBuffers[i]->Map(0, nullptr, &m_cbRingMapped[i]);
      }
      T8_LOG_INFO("[D3D12] CB ring buffers created (%u KB x %u)", kCBRingBufferSize / 1024, kBackBufferCount);
    }

    m_viewport = { 0.f, 0.f, (float)width, (float)height, 0.f, 1.f };
    m_scissorRect = { 0, 0, (LONG)width, (LONG)height };

    // Start debug message polling thread (if --d3d12debug)
    if (g_d3d12Debug) {
      StartDebugMessageThread();
    }

    T8_LOG_INFO("[D3D12] Driver initialized (%dx%d)", width, height);
  }

  void D3D12Driver::CreateSurfaces() {}
  void D3D12Driver::DestroySurfaces() {}
  void D3D12Driver::Update() {}

  void D3D12Driver::DestroyDriver() {
    // Stop debug thread first — it references the device
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
    m_commandList.Reset(); m_fence.Reset(); m_commandQueue.Reset(); m_swapChain.Reset();
    T8Device->release(); T8DeviceContext->release();
    delete T8Device; delete T8DeviceContext; T8Device = nullptr; T8DeviceContext = nullptr;
    m_dxgiFactory.Reset();
    T8_LOG_INFO("[D3D12] Driver destroyed");
  }

  void D3D12Driver::WaitForFence() {
    // Signal with the next monotonic value
    const UINT64 fenceToSignal = m_nextFenceValue++;
    m_commandQueue->Signal(m_fence.Get(), fenceToSignal);
    // Record which fence value this back buffer needs to complete before reuse
    m_frameFenceValues[m_currentBackBuffer] = fenceToSignal;
    // Wait for completion
    if (m_fence->GetCompletedValue() < fenceToSignal) {
      m_fence->SetEventOnCompletion(fenceToSignal, m_fenceEvent);
      WaitForSingleObject(m_fenceEvent, INFINITE);
    }
  }

  void D3D12Driver::WaitForGPU() {
    // Signal and wait for ALL in-flight work
    const UINT64 fenceToSignal = m_nextFenceValue++;
    m_commandQueue->Signal(m_fence.Get(), fenceToSignal);
    if (m_fence->GetCompletedValue() < fenceToSignal) {
      m_fence->SetEventOnCompletion(fenceToSignal, m_fenceEvent);
      WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    // All frames are now complete
    for (UINT i = 0; i < kBackBufferCount; i++)
      m_frameFenceValues[i] = fenceToSignal;
  }

  void D3D12Driver::BeginFrame() {
    // Wait for the previous frame that used this back buffer's allocator to complete
    const UINT64 lastFenceForThisBuffer = m_frameFenceValues[m_currentBackBuffer];
    if (m_fence->GetCompletedValue() < lastFenceForThisBuffer) {
      m_fence->SetEventOnCompletion(lastFenceForThisBuffer, m_fenceEvent);
      WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    HRESULT hr = m_commandAllocators[m_currentBackBuffer]->Reset();
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] BeginFrame: Allocator[%u] Reset failed hr=0x%08X", m_currentBackBuffer, hr);
    }
    hr = m_commandList->Reset(m_commandAllocators[m_currentBackBuffer].Get(), nullptr);
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D12] BeginFrame: CommandList Reset failed hr=0x%08X", hr);
    }
    static_cast<D3D12DeviceContext*>(T8DeviceContext)->m_commandList = m_commandList;
    ID3D12DescriptorHeap* heaps[] = {
      m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetHeap(),
      m_heaps[D3D12Heap::SAMPLER].GetHeap()
    };
    m_commandList->SetDescriptorHeaps(2, heaps);

    // Reset CB ring allocator for this frame
    m_cbRingOffset = 0;
    // Reset dynamic descriptor allocator for this frame
    // If BuildPipelineObjects hasn't been called yet, snapshot the current heap index
    if (m_dynamicDescriptorBase == 0) {
      m_dynamicDescriptorBase = m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetCurrentIndex();
      T8_LOG_INFO("[D3D12] Dynamic descriptor base auto-set to %llu in BeginFrame",
                  (unsigned long long)m_dynamicDescriptorBase);
    }
    m_dynamicDescriptorOffset = 0;
  }

  void D3D12Driver::EndFrame() {}

  void D3D12Driver::BuildPipelineObjects() {
    // Record where permanent descriptors (textures, RTs, initial CBs) end
    m_dynamicDescriptorBase = m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetCurrentIndex();
    m_dynamicDescriptorOffset = 0;
    T8_LOG_ERROR("[D3D12] *** BuildPipelineObjects called! Dynamic descriptor base = %llu ***",
                (unsigned long long)m_dynamicDescriptorBase);
  }

  void D3D12Driver::Clear() {
    // Only call BeginFrame once per frame (on the first Clear call)
    if (!m_frameStarted) {
      BeginFrame();
      m_frameStarted = true;

      // Transition back buffer: PRESENT → RENDER_TARGET
      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = m_backBuffers[m_currentBackBuffer].Get();
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      m_commandList->ResourceBarrier(1, &b);
    }

    // If we're on a custom RT, clear that RT (D3D12RT::Set already clears)
    // If on back buffer, clear back buffer
    if (CurrentRT < 0) {
      m_commandList->OMSetRenderTargets(1, &m_backBufferRTVs[m_currentBackBuffer], FALSE, &m_depthDSV);
      m_commandList->RSSetViewports(1, &m_viewport);
      m_commandList->RSSetScissorRects(1, &m_scissorRect);
      const float cc[4] = { 0.9f, 0.9f, 0.9f, 1.0f };
      m_commandList->ClearRenderTargetView(m_backBufferRTVs[m_currentBackBuffer], cc, 0, nullptr);
      m_commandList->ClearDepthStencilView(m_depthDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }
    // If on a custom RT, the RT's Set() already cleared it.
  }

  void D3D12Driver::SwapBuffers() {
    T8_LOG_TRACE("[D3D12] SwapBuffers");
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = m_backBuffers[m_currentBackBuffer].Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &b);
    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    m_swapChain->Present(0, 0);
    WaitForFence();
    m_currentBackBuffer = m_swapChain->GetCurrentBackBufferIndex();
    m_frameStarted = false; // Ready for next frame's Clear→BeginFrame

    // Flush debug messages after each frame (synchronous — we just waited on fence)
    if (m_infoQueue) PollDebugMessages();
  }

  void D3D12Driver::SetBlendState(BLEND_STATES state) {
    T8_LOG_TRACE("[D3D12] SetBlendState(%d)", state);
    m_currentBlend = state;
  }

  void D3D12Driver::SetDepthStencilState(DEPTH_STENCIL_STATES state) {
    T8_LOG_TRACE("[D3D12] SetDepthStencilState(%d)", state);
    m_currentDepth = state;
  }

  void D3D12Driver::SetCullFace(FACE_CULLING state) {
    m_currentCull = state; m_FaceCulling = state;
  }

  void D3D12Driver::PopRT() {
    T8_LOG_TRACE("[D3D12] PopRT (CurrentRT=%d)", CurrentRT);
    // Transition RT colors back to SRV for reading
    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size()) {
      D3D12RT* rt = static_cast<D3D12RT*>(RTs[CurrentRT]);
      // Color attachments: RT → SRV
      for (int i = 0; i < rt->number_RT; i++) {
        if (rt->vColorStates[i] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
          D3D12_RESOURCE_BARRIER b = {};
          b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          b.Transition.pResource = rt->vColorResources[i].Get();
          b.Transition.StateBefore = rt->vColorStates[i];
          b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
          m_commandList->ResourceBarrier(1, &b);
          rt->vColorStates[i] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
      }
      // Depth attachment: DEPTH_WRITE → DEPTH_READ|PIXEL_SHADER_RESOURCE
      if (rt->depthResource && rt->depthState == D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = rt->depthResource.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &b);
        rt->depthState = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      }
    }
    // Restore back buffer as render target
    m_commandList->OMSetRenderTargets(1, &m_backBufferRTVs[m_currentBackBuffer], FALSE, &m_depthDSV);
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    // Reset CurrentRT so PSO creation knows we're on the back buffer
    CurrentRT = -1;
  }

  // ── D3D12 Readback helper ──
  static void SaveD3D12ResourceToPPM(ID3D12Resource* srcResource, D3D12_RESOURCE_STATES currentState,
                                      const std::string& path, D3D12Driver* driver) {
    ID3D12Device* device = GetNativeDevice();
    D3D12_RESOURCE_DESC desc = srcResource->GetDesc();
    UINT w = (UINT)desc.Width;
    UINT h = desc.Height;
    DXGI_FORMAT fmt = desc.Format;

    // Resolve typeless formats
    if (fmt == DXGI_FORMAT_R32_TYPELESS) fmt = DXGI_FORMAT_R32_FLOAT;
    else if (fmt == DXGI_FORMAT_R16_TYPELESS) fmt = DXGI_FORMAT_R16_FLOAT;

    // Get layout for readback
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0; UINT64 rowSize = 0, totalSize = 0;
    D3D12_RESOURCE_DESC readDesc = desc;
    readDesc.Format = fmt; // use concrete format
    device->GetCopyableFootprints(&readDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &totalSize);

    // Create readback buffer
    D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = totalSize;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1; bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> readbackBuf;
    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuf));

    // Temp command list for copy
    ComPtr<ID3D12CommandAllocator> tmpAlloc;
    ComPtr<ID3D12GraphicsCommandList> tmpList;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList));

    // Transition to COPY_SOURCE if needed
    if (currentState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = srcResource;
      b.Transition.StateBefore = currentState;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      tmpList->ResourceBarrier(1, &b);
    }

    // Copy texture to readback buffer
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {}, srcLoc = {};
    dstLoc.pResource = readbackBuf.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;
    srcLoc.pResource = srcResource;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;
    tmpList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // Transition back
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

    // Fence wait
    ComPtr<ID3D12Fence> tmpFence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence));
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    driver->GetCmdQueue()->Signal(tmpFence.Get(), 1);
    if (tmpFence->GetCompletedValue() < 1) {
      tmpFence->SetEventOnCompletion(1, evt);
      WaitForSingleObject(evt, INFINITE);
    }
    CloseHandle(evt);

    // Map and convert to RGB
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
    WaitForGPU();
    SaveD3D12ResourceToPPM(m_backBuffers[m_currentBackBuffer].Get(),
                            D3D12_RESOURCE_STATE_PRESENT, path, this);
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

  void D3D12Driver::UploadBufferData(ID3D12Resource*, const void*, size_t, D3D12_RESOURCE_STATES) {
    // Implemented inline in texture/buffer creation using temp command lists
  }

  D3D12_GPU_VIRTUAL_ADDRESS D3D12Driver::AllocateCBData(const void* data, UINT dataSize) {
    UINT alignedSize = (dataSize + 255) & ~255;
    if (m_cbRingOffset + alignedSize > kCBRingBufferSize) {
      T8_LOG_ERROR("[D3D12] CB ring buffer overflow! offset=%u + size=%u > %u", m_cbRingOffset, alignedSize, kCBRingBufferSize);
      m_cbRingOffset = 0; // wrap around (may cause artifacts but won't crash)
    }

    UINT bufIdx = m_currentBackBuffer;
    unsigned char* dst = (unsigned char*)m_cbRingMapped[bufIdx] + m_cbRingOffset;
    memcpy(dst, data, dataSize);

    D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = m_cbRingBuffers[bufIdx]->GetGPUVirtualAddress() + m_cbRingOffset;
    m_cbRingOffset += alignedSize;
    return gpuAddr;
  }

  D3D12_GPU_DESCRIPTOR_HANDLE D3D12Driver::AllocateDynamicCBV(const void* data, UINT dataSize) {
    // 1. Copy data to ring buffer
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = AllocateCBData(data, dataSize);

    // 2. Create CBV descriptor in the dynamic region of the shader-visible heap
    ID3D12Device* dev = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    auto& heap = m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE];

    uint64_t descIndex = m_dynamicDescriptorBase + m_dynamicDescriptorOffset;
    if (descIndex >= 65536) {
      T8_LOG_ERROR("[D3D12] Dynamic descriptor overflow! base=%llu offset=%llu",
                   (unsigned long long)m_dynamicDescriptorBase, (unsigned long long)m_dynamicDescriptorOffset);
      descIndex = m_dynamicDescriptorBase; // fallback
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

} // namespace t800

#endif // OS_WINDOWS
