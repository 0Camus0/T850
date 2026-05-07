#include <pch.h>
#include <video/d3d11/D3D11Shader.h>
#include <utils/Log.h>
#include <utils/ShaderDiskCache.h>
#include <debug/RenderTrace.h>


namespace t850 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  namespace {
    std::string WideToUtf8(const wchar_t* text) {
      if (!text)
        return {};
      char buffer[256] = {};
      std::wcstombs(buffer, text, sizeof(buffer) - 1);
      return buffer;
    }

    std::string GetD3D11ShaderCacheDriverSignature(ID3D11Device* device) {
      std::ostringstream sig;
      sig << "d3d11;compiler=vs_5_0/ps_5_0";
      if (!device)
        return sig.str();

      ComPtr<IDXGIDevice> dxgiDevice;
      if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) {
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
          DXGI_ADAPTER_DESC desc = {};
          if (SUCCEEDED(adapter->GetDesc(&desc))) {
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
          }
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

  bool D3DXShader::CreateShaderAPI(std::string src_vs, std::string src_fs, const std::string& vs_name, const std::string& fs_name) {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    HRESULT hr = S_OK;
    cbvSlots.clear();
    VertexDecl.clear();
    const std::string driverSignature = GetD3D11ShaderCacheDriverSignature(device);
    const ShaderDiskCacheKey cacheKey = ShaderDiskCache::MakeKey("d3d11", driverSignature, key.bits, vs_name, fs_name, src_vs, src_fs);
    {
      VS_blob = nullptr;
      std::vector<uint8_t> cachedVS;
      if (ShaderDiskCache::LoadArtifact(cacheKey, "vs.dxbc", cachedVS) && CreateBlobFromBytes(cachedVS, VS_blob)) {
        T8_LOG_DEBUG("[ShaderCache][D3D11] VS hit %s", cacheKey.sha1.c_str());
      }
      else {
        ComPtr<ID3DBlob> errorBlob = nullptr;
        hr = D3DCompile(src_vs.c_str(), src_vs.size(), vs_name.empty() ? nullptr : vs_name.c_str(), 0, 0, "VS", "vs_5_0", 0, 0, &VS_blob, &errorBlob);
        if (hr != S_OK) {

          if (errorBlob) {
            T8_LOG_ERROR("VS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
          }

          if (!VS_blob) {
            return false;
          }
        }
        ShaderDiskCache::StoreArtifact(cacheKey, "vs.dxbc", VS_blob->GetBufferPointer(), VS_blob->GetBufferSize());
        ShaderDiskCache::WriteManifest(cacheKey, driverSignature);
      }

      hr = device->CreateVertexShader(VS_blob->GetBufferPointer(), VS_blob->GetBufferSize(), 0, &pVS);
      if (hr != S_OK) {
        T8_LOG_ERROR("CreateVertexShader failed (hr=0x%08X)", (unsigned)hr);
        exit(666);
      }
    }

    {
      FS_blob = nullptr;
      std::vector<uint8_t> cachedFS;
      if (ShaderDiskCache::LoadArtifact(cacheKey, "fs.dxbc", cachedFS) && CreateBlobFromBytes(cachedFS, FS_blob)) {
        T8_LOG_DEBUG("[ShaderCache][D3D11] FS hit %s", cacheKey.sha1.c_str());
      }
      else {
        ComPtr<ID3DBlob> errorBlob = nullptr;
        hr = D3DCompile(src_fs.c_str(), src_fs.size(), fs_name.empty() ? nullptr : fs_name.c_str(), 0, 0, "FS", "ps_5_0", 0, 0, &FS_blob, &errorBlob);
        if (hr != S_OK) {
          if (errorBlob) {
            T8_LOG_ERROR("PS compile error: %s", (char*)errorBlob->GetBufferPointer());
            return false;
          }

          if (!FS_blob) {
            return false;
          }
        }
        ShaderDiskCache::StoreArtifact(cacheKey, "fs.dxbc", FS_blob->GetBufferPointer(), FS_blob->GetBufferSize());
        ShaderDiskCache::WriteManifest(cacheKey, driverSignature);
      }

      hr = device->CreatePixelShader(FS_blob->GetBufferPointer(), FS_blob->GetBufferSize(), 0, &pFS);
      if (hr != S_OK) {
        T8_LOG_ERROR("CreatePixelShader failed (hr=0x%08X)", (unsigned)hr);
        return false;
      }
    }
    auto collectCBVSlots = [&](ID3DBlob* blob) {
      ID3D11ShaderReflection* resourceReflect = nullptr;
      if (D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&resourceReflect) != S_OK || !resourceReflect)
        return;
      D3D11_SHADER_DESC shaderDesc = {};
      resourceReflect->GetDesc(&shaderDesc);
      for (UINT i = 0; i < shaderDesc.BoundResources; i++) {
        D3D11_SHADER_INPUT_BIND_DESC bindDesc = {};
        resourceReflect->GetResourceBindingDesc(i, &bindDesc);
        if (bindDesc.Type == D3D_SIT_CBUFFER) {
          cbvSlots.insert((int)bindDesc.BindPoint);
        }
      }
      resourceReflect->Release();
    };
    collectCBVSlots(VS_blob.Get());
    collectCBVSlots(FS_blob.Get());

    ID3D11ShaderReflection* reflect;

    hr = D3DReflect(VS_blob->GetBufferPointer(), VS_blob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflect);
    D3D11_SHADER_DESC lShaderDesc;
    reflect->GetDesc(&lShaderDesc);
    int offset = 0;
    for (unsigned i = 0; i < lShaderDesc.InputParameters; i++)
    {
      D3D11_SIGNATURE_PARAMETER_DESC desc;
      reflect->GetInputParameterDesc(i, &desc);

      D3D11_INPUT_ELEMENT_DESC ie;
      ie.SemanticName = desc.SemanticName;
      ie.SemanticIndex = desc.SemanticIndex;
      ie.InputSlot = 0;
      ie.AlignedByteOffset = offset ;
      ie.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
      ie.InstanceDataStepRate = 0;
      if (desc.Mask == 1)
      {
        if (desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ie.Format = DXGI_FORMAT_R32_UINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ie.Format = DXGI_FORMAT_R32_SINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ie.Format = DXGI_FORMAT_R32_FLOAT;
        offset += 4;
      }
      else if (desc.Mask <= 3)
      {
        if (desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ie.Format = DXGI_FORMAT_R32G32_UINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ie.Format = DXGI_FORMAT_R32G32_SINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ie.Format = DXGI_FORMAT_R32G32_FLOAT;
        offset += 8;
      }
      else if (desc.Mask <= 7)
      {
        if (desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ie.Format = DXGI_FORMAT_R32G32B32_UINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ie.Format = DXGI_FORMAT_R32G32B32_SINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ie.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        offset += 12;
      }
      else if (desc.Mask <= 15)
      {
        if (desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ie.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ie.Format = DXGI_FORMAT_R32G32B32A32_SINT;
        else if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) ie.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        offset += 16;
      }

      VertexDecl.push_back(ie);
    }
    hr = device->CreateInputLayout(&VertexDecl[0], static_cast<UINT>(VertexDecl.size()), VS_blob->GetBufferPointer(), VS_blob->GetBufferSize(), &Layout);
    if (hr != S_OK) {
      T8_LOG_ERROR("CreateInputLayout failed (hr=0x%08X) — %d element(s), stride=%d  [VS='%s' FS='%s']:", (unsigned)hr, (int)VertexDecl.size(), offset, vs_name.c_str(), fs_name.c_str());
      for (int i = 0; i < (int)VertexDecl.size(); i++) {
        const auto& e = VertexDecl[i];
        T8_LOG_ERROR("  [%d] Semantic='%s' Index=%u Format=%u Offset=%u", i, e.SemanticName, e.SemanticIndex, (unsigned)e.Format, e.AlignedByteOffset);
      }
      reflect->Release();
      return false;
    }

    reflect->Release();

#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      // Stash the input layout for the tracer keyed by ShaderBase*; the
      // shader hasn't been registered yet (BaseDriver::CreateShader does
      // that after T8Device->CreateShader returns), so we can't use a
      // shader id here. Mirrors the D3D12 path.
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
      g_renderTracer->RegisterShaderInputsForPtr(this, (uint32_t)offset, std::move(attrs));
    }
#endif

    return true;
  }

  void D3DXShader::Set(const DeviceContext & deviceContext)
  {
    T8_LOG_TRACE("[D3D11] Shader::Set key=0x%016llX", static_cast<unsigned long long>(key.bits));
    const_cast<DeviceContext*>(&deviceContext)->actualShaderSet = (ShaderBase*)this;
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->VSSetShader(pVS.Get(), 0, 0);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->PSSetShader(pFS.Get(), 0, 0);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->IASetInputLayout(Layout.Get());
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int shId = g_renderTracer->LookupShaderId(this);
      g_renderTracer->EvBindShader(shId, key.bits);
      // No PSO concept in D3D11 — intentionally no EvBindPSO.
    }
#endif
  }
  void D3DXShader::DestroyAPIShader()
  {
    pVS.Reset();
    pFS.Reset();
    VS_blob.Reset();
    FS_blob.Reset();
    Layout.Reset();
  }
}
