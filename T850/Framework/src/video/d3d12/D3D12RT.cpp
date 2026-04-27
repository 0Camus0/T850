#include <pch.h>
/*********************************************************
* T850 Engine — D3D12 Backend
* D3D12RT.cpp: Render target creation, binding, state transitions
*********************************************************/

#include <video/d3d12/D3D12Driver.h>

#ifdef OS_WINDOWS

#include <utils/Log.h>

namespace t850 {

  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static D3D12Driver* GetD3D12Driver() { return static_cast<D3D12Driver*>(g_pBaseDriver); }
  static ID3D12Device* GetNativeDevice() { return static_cast<D3D12Device*>(T8Device)->GetNativeDevice(); }

  // ══════════════════════════════════════════════════════
  //  D3D12RT — Create
  // ══════════════════════════════════════════════════════

  bool D3D12RT::LoadAPIRT() {
    ID3D12Device* device = GetNativeDevice();
    auto* driver = GetD3D12Driver();

    // Detect UAV-only formats (for ray tracing output textures)
    bool isUAV = (color_format == BaseRT::RGBA16F_UAV || color_format == BaseRT::R8_UAV);

    DXGI_FORMAT cfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    switch (color_format) {
      case BaseRT::NOTHING:    cfmt = DXGI_FORMAT_R8G8B8A8_UNORM; number_RT = 0; break;
      case BaseRT::R8:         cfmt = DXGI_FORMAT_R8_UNORM; break;
      case BaseRT::F16:        cfmt = DXGI_FORMAT_R16_FLOAT; break;
      case BaseRT::F32:        cfmt = DXGI_FORMAT_R32_FLOAT; break;
      case BaseRT::RGBA8:      cfmt = DXGI_FORMAT_R8G8B8A8_UNORM; break;
      case BaseRT::RGBA16F:    cfmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
      case BaseRT::RGBA32F:    cfmt = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
      case BaseRT::RGBA16F_UAV:cfmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
      case BaseRT::R8_UAV:     cfmt = DXGI_FORMAT_R8_UNORM; break;
      default: break;
    }

    DXGI_FORMAT depthFmt = DXGI_FORMAT_R32_TYPELESS;
    DXGI_FORMAT dsvFmt = DXGI_FORMAT_D32_FLOAT;
    DXGI_FORMAT srvDepthFmt = DXGI_FORMAT_R32_FLOAT;
    isCubeDepth = (depth_format == BaseRT::CUBE_F32);
    colorFormat = cfmt;  // cache for PSO lookup

    // Color attachments
    for (int i = 0; i < number_RT; i++) {
      // Per-attachment format if available
      DXGI_FORMAT thisFmt = cfmt;
      if (!perColorFormats.empty() && i < (int)perColorFormats.size()) {
        switch (perColorFormats[i]) {
          case BaseRT::R8:      thisFmt = DXGI_FORMAT_R8_UNORM; break;
          case BaseRT::F16:     thisFmt = DXGI_FORMAT_R16_FLOAT; break;
          case BaseRT::F32:     thisFmt = DXGI_FORMAT_R32_FLOAT; break;
          case BaseRT::RGBA8:   thisFmt = DXGI_FORMAT_R8G8B8A8_UNORM; break;
          case BaseRT::RGBA16F: thisFmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
          case BaseRT::RGBA32F: thisFmt = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
          default: break;
        }
      }

      D3D12_RESOURCE_DESC desc = {};
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Width = w; desc.Height = h; desc.DepthOrArraySize = 1;
      desc.MipLevels = 1; desc.Format = thisFmt;
      desc.SampleDesc.Count = 1;
      // UAV-only: add ALLOW_UNORDERED_ACCESS flag (skip RTV for RT output textures)
      if (isUAV) {
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      } else {
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      }

      D3D12_HEAP_PROPERTIES heapProps = {}; heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

      ComPtr<ID3D12Resource> colorRes;
      HRESULT hr;
      if (isUAV) {
        // UAV textures start in UNORDERED_ACCESS state; no clear value
        hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                              IID_PPV_ARGS(&colorRes));
      } else {
        D3D12_CLEAR_VALUE clearVal = {}; clearVal.Format = thisFmt;
        hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
                                              IID_PPV_ARGS(&colorRes));
      }
      if (FAILED(hr)) { T8_LOG_ERROR("[D3D12] RT color[%d] create failed hr=0x%08X", i, hr); return false; }
      vColorResources.push_back(colorRes);
      vColorStates.push_back(isUAV ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RENDER_TARGET);

      // RTV — only for non-UAV attachments
      if (!isUAV) {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = driver->GetHeap(D3D12Heap::RTV).AllocateCPU();
        device->CreateRenderTargetView(colorRes.Get(), nullptr, rtv);
        vRTVHandles.push_back(rtv);
      }

      // SRV for reading as texture
      D3D12Texture* colorTex = new D3D12Texture;
      colorTex->pTexResource = colorRes;
      colorTex->x = w; colorTex->y = h;
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = thisFmt;
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = 1;
      srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      colorTex->srvCPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateCPU();
      colorTex->srvGPU = driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE).AllocateGPU();
      device->CreateShaderResourceView(colorRes.Get(), &srvDesc, colorTex->srvCPU);
      vColorTextures.push_back(colorTex);

      T8_LOG_DEBUG("[D3D12] RT color[%d] created: %dx%d fmt=%d", i, w, h, thisFmt);
    }

    // Depth attachment
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

  // ══════════════════════════════════════════════════════
  //  D3D12RT — Destroy
  // ══════════════════════════════════════════════════════

  void D3D12RT::DestroyAPIRT() {
    if (pDepthTexture) { pDepthTexture->release(); pDepthTexture = nullptr; }
    for (auto* t : vColorTextures) t->release();
    vColorTextures.clear();
    vColorResources.clear();
    vRTVHandles.clear();
    depthResource.Reset();
  }

  // ══════════════════════════════════════════════════════
  //  D3D12RT — Set (bind as render target)
  // ══════════════════════════════════════════════════════

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

} // namespace t850

#endif // OS_WINDOWS
