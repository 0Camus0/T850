#include <pch.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12Shader.cpp: Shader compilation, reflection, root signature
*********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>
#include <utils/ShaderDiskCache.h>
#include <debug/RenderTrace.h>
#include <algorithm>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }

  namespace {
    std::string WideToUtf8(const wchar_t* text) {
      if (!text)
        return {};
      char buffer[256] = {};
      std::wcstombs(buffer, text, sizeof(buffer) - 1);
      return buffer;
    }

    bool SameLuid(const LUID& a, const LUID& b) {
      return a.HighPart == b.HighPart && a.LowPart == b.LowPart;
    }

    std::string GetD3D12ShaderCacheDriverSignature(ID3D12Device* device) {
      std::ostringstream sig;
      sig << "d3d12;compiler=vs_5_0/ps_5_0";
      if (!device)
        return sig.str();

      const LUID deviceLuid = device->GetAdapterLuid();
      ComPtr<IDXGIFactory4> factory;
      if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
          DXGI_ADAPTER_DESC1 desc = {};
          if (SUCCEEDED(adapter->GetDesc1(&desc)) && SameLuid(desc.AdapterLuid, deviceLuid)) {
            LARGE_INTEGER driverVersion = {};
            const HRESULT versionHr = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
            sig << ";adapter=" << WideToUtf8(desc.Description)
                << ";vendor=" << desc.VendorId
                << ";device=" << desc.DeviceId
                << ";subsys=" << desc.SubSysId
                << ";revision=" << desc.Revision;
            if (SUCCEEDED(versionHr) && (driverVersion.HighPart != 0 || driverVersion.LowPart != 0))
              sig << ";driver=" << driverVersion.HighPart << "." << driverVersion.LowPart;
            else
              sig << ";driver=unknown";
            break;
          }
          adapter.Reset();
        }
      }
      return sig.str();
    }

    bool CreateBlobFromBytes(const std::vector<uint8_t>& bytes, ComPtr<ID3DBlob>& blob) {
      if (bytes.empty())
        return false;
      ComPtr<ID3DBlob> created;
      if (FAILED(D3DCreateBlob(bytes.size(), &created)))
        return false;
      std::memcpy(created->GetBufferPointer(), bytes.data(), bytes.size());
      blob = created;
      return true;
    }
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Shader — Root Signature
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

    // Build root parameters — CBVs use inline root descriptors (no descriptor table needed),
    // SRVs and Samplers use descriptor tables.
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges(resources.size());
    std::vector<D3D12_ROOT_PARAMETER>   params(resources.size());

    for (int i = 0; i < (int)resources.size(); i++) {
      auto& r = resources[i];

      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
        // Inline root CBV — binds a GPU VA directly, no descriptor table overhead
        params[i] = {};
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[i].Descriptor.ShaderRegister = r.reg;
        params[i].Descriptor.RegisterSpace  = r.space;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      } else {
        // SRV and Sampler — use descriptor tables
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
      }

      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
        cbvSlots[(int)r.reg] = i;
        if (r.reg == 0) cbvSlot = i;
      }
      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV) srvSlots[r.reg] = i;
      if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER) {
        samplerSlots[r.reg] = i;
        if (r.reg == 0) samplerSlot = i;
      }

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

    T8_LOG_DEBUG("[D3D12] Root signature created: cbvSlots=%d samplerSlot=%d srvSlots=%d",
                 (int)cbvSlots.size(), samplerSlot, (int)srvSlots.size());
    return true;
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Shader — Compile & Reflect
  // ══════════════════════════════════════════════════════

  bool D3D12Shader::CreateShaderAPI(std::string src_vs, std::string src_fs,
                                     const std::string& vs_name, const std::string& fs_name) {
    ID3D12Device* device = GetNativeDevice();
    cbvSlot = -1;
    samplerSlot = -1;
    cbvSlots.clear();
    srvSlots.clear();
    samplerSlots.clear();
    const std::string driverSignature = GetD3D12ShaderCacheDriverSignature(device);
    const ShaderDiskCacheKey cacheKey = ShaderDiskCache::MakeKey("d3d12", driverSignature, key.bits, vs_name, fs_name, src_vs, src_fs);

    // Compile VS
    {
      std::vector<uint8_t> cachedVS;
      if (ShaderDiskCache::LoadArtifact(cacheKey, "vs.dxbc", cachedVS) && CreateBlobFromBytes(cachedVS, VS_blob)) {
        T8_LOG_DEBUG("[ShaderCache][D3D12] VS hit %s", cacheKey.sha1.c_str());
      }
      else {
        ComPtr<ID3DBlob> errBlob;
        HRESULT hr = D3DCompile(src_vs.c_str(), src_vs.size(),
                                 vs_name.empty() ? nullptr : vs_name.c_str(),
                                 nullptr, nullptr, "VS", "vs_5_0", 0, 0, &VS_blob, &errBlob);
        if (FAILED(hr)) {
          T8_LOG_ERROR("[D3D12] VS compile error: %s", errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
          return false;
        }
        ShaderDiskCache::StoreArtifact(cacheKey, "vs.dxbc", VS_blob->GetBufferPointer(), VS_blob->GetBufferSize());
        ShaderDiskCache::WriteManifest(cacheKey, driverSignature);
      }
      T8_LOG_VERBOSE("[D3D12] VS compiled: %u bytes [%s]", (unsigned)VS_blob->GetBufferSize(), vs_name.c_str());
    }

    // Compile FS
    {
      std::vector<uint8_t> cachedFS;
      if (ShaderDiskCache::LoadArtifact(cacheKey, "fs.dxbc", cachedFS) && CreateBlobFromBytes(cachedFS, FS_blob)) {
        T8_LOG_DEBUG("[ShaderCache][D3D12] FS hit %s", cacheKey.sha1.c_str());
      }
      else {
        ComPtr<ID3DBlob> errBlob;
        HRESULT hr = D3DCompile(src_fs.c_str(), src_fs.size(),
                                 fs_name.empty() ? nullptr : fs_name.c_str(),
                                 nullptr, nullptr, "FS", "ps_5_0", 0, 0, &FS_blob, &errBlob);
        if (FAILED(hr)) {
          T8_LOG_ERROR("[D3D12] FS compile error: %s", errBlob ? (char*)errBlob->GetBufferPointer() : "unknown");
          return false;
        }
        ShaderDiskCache::StoreArtifact(cacheKey, "fs.dxbc", FS_blob->GetBufferPointer(), FS_blob->GetBufferSize());
        ShaderDiskCache::WriteManifest(cacheKey, driverSignature);
      }
      T8_LOG_VERBOSE("[D3D12] FS compiled: %u bytes [%s]", (unsigned)FS_blob->GetBufferSize(), fs_name.c_str());
    }

    // Reflect VS for input layout
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

#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      // Stash the input layout for the tracer keyed by ShaderBase*; the
      // shader hasn't been registered yet (BaseDriver::CreateShader does
      // that after T8Device->CreateShader returns), so we can't use a
      // shader id here.
      std::vector<TraceShaderAttr> attrs;
      attrs.reserve(VertexDecl.size());
      for (size_t i = 0; i < VertexDecl.size(); ++i) {
        const auto& ie = VertexDecl[i];
        TraceShaderAttr a;
        a.semantic   = std::string(ie.SemanticName ? ie.SemanticName : "")
                     + (ie.SemanticIndex > 0 ? std::to_string(ie.SemanticIndex) : std::string());
        a.location   = (int)ie.SemanticIndex;
        a.input_slot = (int)ie.InputSlot;
        a.offset     = ie.AlignedByteOffset;
        switch (ie.Format) {
          case DXGI_FORMAT_R32_FLOAT:           a.format = "R32_FLOAT";          a.size_bytes = 4;  break;
          case DXGI_FORMAT_R32G32_FLOAT:        a.format = "R32G32_FLOAT";       a.size_bytes = 8;  break;
          case DXGI_FORMAT_R32G32B32_FLOAT:     a.format = "R32G32B32_FLOAT";    a.size_bytes = 12; break;
          case DXGI_FORMAT_R32G32B32A32_FLOAT:  a.format = "R32G32B32A32_FLOAT"; a.size_bytes = 16; break;
          case DXGI_FORMAT_R32_UINT:            a.format = "R32_UINT";           a.size_bytes = 4;  break;
          case DXGI_FORMAT_R32G32_UINT:         a.format = "R32G32_UINT";        a.size_bytes = 8;  break;
          case DXGI_FORMAT_R32G32B32_UINT:      a.format = "R32G32B32_UINT";     a.size_bytes = 12; break;
          case DXGI_FORMAT_R32G32B32A32_UINT:   a.format = "R32G32B32A32_UINT";  a.size_bytes = 16; break;
          case DXGI_FORMAT_R32_SINT:            a.format = "R32_SINT";           a.size_bytes = 4;  break;
          case DXGI_FORMAT_R32G32_SINT:         a.format = "R32G32_SINT";        a.size_bytes = 8;  break;
          case DXGI_FORMAT_R32G32B32_SINT:      a.format = "R32G32B32_SINT";     a.size_bytes = 12; break;
          case DXGI_FORMAT_R32G32B32A32_SINT:   a.format = "R32G32B32A32_SINT";  a.size_bytes = 16; break;
          default:                              a.format = "DXGI_FORMAT_" + std::to_string((int)ie.Format); break;
        }
        attrs.push_back(std::move(a));
      }
      g_renderTracer->RegisterShaderInputsForPtr(this, vertexStride, std::move(attrs));
    }
#endif

    // Reflect FS
    ComPtr<ID3D12ShaderReflection> fsReflect;
    D3DReflect(FS_blob->GetBufferPointer(), FS_blob->GetBufferSize(), IID_PPV_ARGS(&fsReflect));

    // Build root signature from reflection
    if (!BuildRootSignature(device, vsReflect.Get(), fsReflect.Get())) return false;

#ifdef T8_DUMP_SHADER_REFLECTION
    // Dump D3D12 reflection as reference for validating SPIR-V reflection
    T8_LOG_INFO("[D3D12_REFL] === key=0x%016llX VS='%s' FS='%s' ===", static_cast<unsigned long long>(key.bits), vs_name.c_str(), fs_name.c_str());
    T8_LOG_INFO("[D3D12_REFL] VS Inputs (%u):", vsDesc.InputParameters);
    for (UINT i = 0; i < vsDesc.InputParameters; i++) {
      D3D12_SIGNATURE_PARAMETER_DESC pd; vsReflect->GetInputParameterDesc(i, &pd);
      int components = 0;
      if (pd.Mask == 1) components = 1;
      else if (pd.Mask <= 3) components = 2;
      else if (pd.Mask <= 7) components = 3;
      else components = 4;
      T8_LOG_INFO("[D3D12_REFL]   [%u] %s%u  components=%d  offset=%d",
                  i, pd.SemanticName, pd.SemanticIndex, components,
                  VertexDecl[i].AlignedByteOffset);
    }
    T8_LOG_INFO("[D3D12_REFL] VS stride=%d", vertexStride);

    T8_LOG_INFO("[D3D12_REFL] VS Resources (%u):", vsDesc.BoundResources);
    for (UINT i = 0; i < vsDesc.BoundResources; i++) {
      D3D12_SHADER_INPUT_BIND_DESC bd; vsReflect->GetResourceBindingDesc(i, &bd);
      T8_LOG_INFO("[D3D12_REFL]   [%u] '%s' type=%d bindPoint=%u bindCount=%u space=%u",
                  i, bd.Name, bd.Type, bd.BindPoint, bd.BindCount, bd.Space);
    }

    D3D12_SHADER_DESC fsDesc2; fsReflect->GetDesc(&fsDesc2);
    T8_LOG_INFO("[D3D12_REFL] FS Resources (%u):", fsDesc2.BoundResources);
    for (UINT i = 0; i < fsDesc2.BoundResources; i++) {
      D3D12_SHADER_INPUT_BIND_DESC bd; fsReflect->GetResourceBindingDesc(i, &bd);
      T8_LOG_INFO("[D3D12_REFL]   [%u] '%s' type=%d bindPoint=%u bindCount=%u space=%u",
                  i, bd.Name, bd.Type, bd.BindPoint, bd.BindCount, bd.Space);
    }
#endif

    T8_LOG_INFO("[D3D12] Shader created: key=0x%016llX stride=%d rootParams: cbv=%d sampler=%d srvs=%d",
          static_cast<unsigned long long>(key.bits), vertexStride, cbvSlot, samplerSlot, (int)srvSlots.size());
    return true;
  }

  // ══════════════════════════════════════════════════════
  //  D3D12Shader — Set (bind shader + PSO)
  // ══════════════════════════════════════════════════════

  void D3D12Shader::Set(const DeviceContext& deviceContext) {
    T8_LOG_TRACE("[D3D12] Shader::Set key=0x%016llX", static_cast<unsigned long long>(key.bits));
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = (ShaderBase*)this;

    auto* cmdList = static_cast<const D3D12DeviceContext*>(&deviceContext)->GetCommandList();
    auto* driver = GetD3D12Driver();

    ID3D12DescriptorHeap* heaps[] = {
      driver->m_heaps[D3D12Heap::CBV_SRV_UAV_VISIBLE].GetHeap(),
      driver->m_heaps[D3D12Heap::SAMPLER].GetHeap()
    };
    cmdList->SetDescriptorHeaps(2, heaps);

    // Determine current RT configuration for PSO
    uint8_t numRTVs = 1;
    DXGI_FORMAT rtvFormats[8] = {};
    for (auto& fmt : rtvFormats) fmt = DXGI_FORMAT_UNKNOWN;
    rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT dsvFmt = DXGI_FORMAT_D32_FLOAT;

    int curRT = driver->CurrentRT;
    if (curRT >= 0 && curRT < (int)driver->RTs.size()) {
      D3D12RT* rt = static_cast<D3D12RT*>(driver->RTs[curRT]);
      numRTVs = (uint8_t)(rt->number_RT > 0 ? rt->number_RT : 0);
      for (int i = 0; i < rt->number_RT && i < 8; ++i)
        rtvFormats[i] = (i < (int)rt->vColorFormats.size()) ? rt->vColorFormats[i] : rt->colorFormat;
    }

    // Get or create PSO for current state
    ID3D12PipelineState* pso = driver->GetOrCreatePSO(this, numRTVs, rtvFormats, dsvFmt);

    // Skip redundant root signature and PSO binds
    ID3D12RootSignature* rootSig = pRootSignature.Get();
    if (rootSig != driver->m_lastRootSig) {
      cmdList->SetGraphicsRootSignature(rootSig);
      driver->m_lastRootSig = rootSig;
    }
    if (pso && pso != driver->m_lastPSO) {
      cmdList->SetPipelineState(pso);
      driver->m_lastPSO = pso;
    }

    // Bind default sampler as a fallback for draws that never bind a
    // texture sampler. Textured draws rebind the texture's own sampler
    // atomically with its SRV inside Texture::Set/SetVS (which run after
    // this call), so per-texture samplers (e.g. the voxel atlas NEAREST
    // sampler) always win over the aniso default.
    for (const auto& samplerBinding : samplerSlots) {
      cmdList->SetGraphicsRootDescriptorTable(samplerBinding.second, driver->GetDefaultSamplerGPU());
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int shId = g_renderTracer->LookupShaderId(this);
      g_renderTracer->EvBindShader(shId, key.bits);
      g_renderTracer->EvBindPSO((int)(uintptr_t)pso);
    }
#endif
  }

  void D3D12Shader::DestroyAPIShader() {
    VS_blob.Reset(); FS_blob.Reset();
    pRootSignature.Reset();
    VertexDecl.clear(); m_semanticNames.clear();
    srvSlots.clear(); samplerSlots.clear();
  }

} // namespace t850

#endif // OS_WINDOWS
