#include <pch.h>
/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include <video/d3d11/D3D11Driver.h>
#include <video/d3d11/D3D11RT.h>
#include <video/d3d11/D3D11Shader.h>
#include <video/d3d11/D3D11Texture.h>

#include <debug/Profiler.h>
#include <debug/RenderTrace.h>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <utils/Log.h>



namespace t850 {
  // D3D11 Main Objects
  ComPtr<IDXGISwapChain>DXGISwapchain;// Responsible of the swap buffers
  ComPtr<ID3D11RenderTargetView>  D3D11RenderTargetView;  // View into the back buffer
  ComPtr<ID3D11DepthStencilView>  D3D11DepthStencilTargetView; // View into the depth buffer
  ComPtr<ID3D11Texture2D>D3D11DepthTex;// Actual depth buffer texture

  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;


  void D3DXDriver::InitDriver() {
    T8Device = new t850::D3DXDevice;
    T8DeviceContext = new t850::D3DXDeviceContext;

    //Descriptor of the Back Buffer
    DXGI_MODE_DESC BackBufferDesc;
    ZeroMemory(&BackBufferDesc, sizeof(DXGI_MODE_DESC));
    BackBufferDesc.Width = width;
    BackBufferDesc.Height = height;
    BackBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA 32 bit buffer
    BackBufferDesc.RefreshRate.Numerator = 0;
    BackBufferDesc.RefreshRate.Denominator = 1;
    BackBufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    BackBufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

    //Descriptor of the Swap Chain
    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    ZeroMemory(&SwapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));
    SwapChainDesc.BufferDesc = BackBufferDesc;
    SwapChainDesc.BufferCount = 1;
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.SampleDesc.Count = 1;
    SwapChainDesc.SampleDesc.Quality = 0;
    SwapChainDesc.OutputWindow = hwnd;
    SwapChainDesc.Windowed = true;
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    //Function to create three objects:
    //Device
    //Device Context
    //Swap Chain
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
#if DEBUG_DRIVER
      D3D11_CREATE_DEVICE_DEBUG,// Debug errors
#else
      0,
#endif
      NULL, NULL, D3D11_SDK_VERSION, &SwapChainDesc, &DXGISwapchain,
      reinterpret_cast<ID3D11Device**>(T8Device->GetAPIObjectReference()), NULL,
      reinterpret_cast<ID3D11DeviceContext**>(T8DeviceContext->GetAPIObjectReference()));

    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());

    // Get the back buffer
    ComPtr<ID3D11Texture2D> BackBuffer;
    hr = DXGISwapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), &BackBuffer);

    // Descriptor to create the Depth Buffer
    D3D11_TEXTURE2D_DESC descDepth;
    descDepth.Width = width;
    descDepth.Height = height;
    descDepth.MipLevels = 1;
    descDepth.ArraySize = 1;
    descDepth.Format = DXGI_FORMAT_D32_FLOAT;
    descDepth.SampleDesc.Count = 1;
    descDepth.SampleDesc.Quality = 0;
    descDepth.Usage = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL; // -- > Use it as depth stencil
    descDepth.CPUAccessFlags = 0;
    descDepth.MiscFlags = 0;
    hr = device->CreateTexture2D(&descDepth, NULL, &D3D11DepthTex);// Output to the depth texture

    // Descriptor to create the Depth View
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd;
    ZeroMemory(&dsvd, sizeof(dsvd));

    dsvd.Format = DXGI_FORMAT_D32_FLOAT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    // Using the View we can operate with the depth buffer, note this view is created from the depth texture
    hr = device->CreateDepthStencilView(D3D11DepthTex.Get(), &dsvd, &D3D11DepthStencilTargetView);


    //Now we create the main render target view from the back buffer texture
    hr = device->CreateRenderTargetView(BackBuffer.Get(), NULL, &D3D11RenderTargetView);

    // Using the Context now we set the render targets, that would be the Main Render Target View (Back Buffer)
    // and the Depth Buffer View (Depth)
    deviceContext->OMSetRenderTargets(1, D3D11RenderTargetView.GetAddressOf(), D3D11DepthStencilTargetView.Get());


    // Set the Viewport of the size of the screen
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0;
    viewport.MaxDepth = 1;

    deviceContext->RSSetViewports(1, &viewport);

    /*BLEND STATES*/

    CD3D11_DEFAULT def;
    //Opaque
    CD3D11_BLEND_DESC BlendStatedesc(def);
    device->CreateBlendState(&BlendStatedesc, m_BlendStateOpaque.ReleaseAndGetAddressOf());
    //Additive
    BlendStatedesc = CD3D11_BLEND_DESC(def);
    BlendStatedesc.RenderTarget[0].BlendEnable = TRUE;
    BlendStatedesc.RenderTarget[0].SrcBlend =
      BlendStatedesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    BlendStatedesc.RenderTarget[0].DestBlend =
      BlendStatedesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    device->CreateBlendState(&BlendStatedesc, m_BlendStateAdditive.ReleaseAndGetAddressOf());
    //AlphaBlend
    //BlendStatedesc = CD3D11_BLEND_DESC(def);
    //BlendStatedesc.RenderTarget[0].BlendEnable = TRUE;
    //BlendStatedesc.RenderTarget[0].SrcBlend =
    //  BlendStatedesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    //BlendStatedesc.RenderTarget[0].DestBlend =
    //  BlendStatedesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    //device->CreateBlendState(&BlendStatedesc, &m_BlendStateAlphaBlend);

    BlendStatedesc = CD3D11_BLEND_DESC(def);
    BlendStatedesc.RenderTarget[0].BlendEnable = TRUE;
    BlendStatedesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    BlendStatedesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    BlendStatedesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    BlendStatedesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    BlendStatedesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    BlendStatedesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    BlendStatedesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&BlendStatedesc, m_BlendStateAlphaBlend.ReleaseAndGetAddressOf());
    //NonPremultiplied
    BlendStatedesc = CD3D11_BLEND_DESC(def);
    BlendStatedesc.RenderTarget[0].BlendEnable = TRUE;
    BlendStatedesc.RenderTarget[0].SrcBlend =
      BlendStatedesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    BlendStatedesc.RenderTarget[0].DestBlend =
      BlendStatedesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    device->CreateBlendState(&BlendStatedesc, m_BlendStateNonPremultiplied.ReleaseAndGetAddressOf());

    /*DEPTH STATES*/

    //ReadWrite
    CD3D11_DEPTH_STENCIL_DESC BlendDesc(def);
    BlendDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    device->CreateDepthStencilState(&BlendDesc, m_depthStateReadWrite.ReleaseAndGetAddressOf());
    // DepthNone
    BlendDesc = CD3D11_DEPTH_STENCIL_DESC(def);
    BlendDesc.DepthEnable = FALSE;
    BlendDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    BlendDesc.DepthFunc = D3D11_COMPARISON_EQUAL;
    device->CreateDepthStencilState(&BlendDesc, m_depthStateNone.ReleaseAndGetAddressOf());
    // DepthRead
    BlendDesc = CD3D11_DEPTH_STENCIL_DESC(def);
    BlendDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    BlendDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    device->CreateDepthStencilState(&BlendDesc, m_depthStateRead.ReleaseAndGetAddressOf());

    /*RASTERIZER STATES*/


    //SetBlendState(BlendStates::BLEND_DEFAULT);
    //SetDepthStencilState(DepthStencilStates::DEPTH_DEFAULT);
  }

  void D3DXDriver::CreateSurfaces() {

  }

  void D3DXDriver::DestroySurfaces() {

  }

  void D3DXDriver::Update() {

  }

  void D3DXDriver::DestroyDriver() {
    DestroyShaders();
    DestroyRTs();
    DestroyTextures();
    // ComPtr members release automatically; explicit Reset() makes ordering explicit
    m_BlendStateAdditive.Reset();
    m_BlendStateAlphaBlend.Reset();
    m_BlendStateNonPremultiplied.Reset();
    m_BlendStateOpaque.Reset();
    m_depthStateNone.Reset();
    m_depthStateRead.Reset();
    m_depthStateReadWrite.Reset();
    m_RasterStateWireframe.Reset();
    m_RasterStateCullNone.Reset();
    m_RasterStateCullClockWise.Reset();
    m_RasterStateCullCounterClockwise.Reset();

    T8Device->release();
    T8DeviceContext->release();
  }

  void D3DXDriver::SetWindow(void *window) {
    hwnd = GetActiveWindow(); // Get the HWND of the window
  }

  void D3DXDriver::SetWindowHandle(const WindowHandle& handle) {
    // Editor host path: use the explicit HWND it provides (e.g. a child
    // window that owns the viewport). Falls back to GetActiveWindow() so
    // the legacy SDL-driven flow keeps behaving exactly as before.
    if (handle.kind == WindowHandle::WIN32_HWND && handle.nativeHandle) {
      hwnd = reinterpret_cast<HWND>(handle.nativeHandle);
    } else {
      hwnd = GetActiveWindow();
    }
  }

  void D3DXDriver::SetDimensions(int w, int h) {
    width = w;
    height = h;
  }

  bool D3DXDriver::ResizeSwapchain(int newW, int newH) {
    if (newW <= 0 || newH <= 0) return false;
    if (!DXGISwapchain) return false;

    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    if (!device || !deviceContext) return false;

    // Unbind render targets before resizing
    deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    D3D11RenderTargetView.Reset();
    D3D11DepthStencilTargetView.Reset();
    D3D11DepthTex.Reset();

    HRESULT hr = DXGISwapchain->ResizeBuffers(0, (UINT)newW, (UINT)newH,
                                               DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D11] ResizeBuffers failed (0x%08X)", (unsigned)hr);
      return false;
    }

    // Recreate RTV from the new back buffer
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = DXGISwapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
    if (FAILED(hr)) return false;
    hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, &D3D11RenderTargetView);
    if (FAILED(hr)) return false;

    // Recreate depth buffer at the new size
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width            = (UINT)newW;
    depthDesc.Height           = (UINT)newH;
    depthDesc.MipLevels        = 1;
    depthDesc.ArraySize        = 1;
    depthDesc.Format           = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage            = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags        = D3D11_BIND_DEPTH_STENCIL;
    hr = device->CreateTexture2D(&depthDesc, nullptr, &D3D11DepthTex);
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
    dsvd.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device->CreateDepthStencilView(D3D11DepthTex.Get(), &dsvd, &D3D11DepthStencilTargetView);
    if (FAILED(hr)) return false;

    // Rebind and update viewport
    deviceContext->OMSetRenderTargets(1, D3D11RenderTargetView.GetAddressOf(),
                                     D3D11DepthStencilTargetView.Get());

    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width    = (float)newW;
    viewport.Height   = (float)newH;
    viewport.MinDepth = 0;
    viewport.MaxDepth = 1;
    deviceContext->RSSetViewports(1, &viewport);

    width  = newW;
    height = newH;
    T8_LOG_INFO("[D3D11] Swapchain resized to %dx%d", newW, newH);
    return true;
  }

  void D3DXDriver::SetBlendState(BlendStates state)
  {
    static const char* names[] = {"BLEND_DEFAULT","BLEND_OPAQUE","ADDITIVE","ALPHA_BLEND","NON_PREMULTIPLIED"};
    T8_LOG_TRACE("[D3D11] SetBlendState(%s)", (state >= 0 && state <= 4) ? names[state] : "?");
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    switch (state)
    {
    case t850::BaseDriver::BLEND_DEFAULT:
      deviceContext->OMSetBlendState(m_BlendStateOpaque.Get(), 0, 0xffffffff);
      break;
    case t850::BaseDriver::BlendStates::BLEND_OPAQUE:
      deviceContext->OMSetBlendState(m_BlendStateOpaque.Get(), 0, 0xffffffff);
      break;
    case t850::BaseDriver::ADDITIVE:
      deviceContext->OMSetBlendState(m_BlendStateAdditive.Get(), 0, 0xffffffff);
      break;
    case t850::BaseDriver::ALPHA_BLEND:
      deviceContext->OMSetBlendState(m_BlendStateAlphaBlend.Get(), 0, 0xffffffff);
      break;
    case t850::BaseDriver::NON_PREMULTIPLIED:
      deviceContext->OMSetBlendState(m_BlendStateNonPremultiplied.Get(), 0, 0xffffffff);
      break;
    default:
      break;
    }
    T8_TRACE(EvSetBlend((int)state));
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

  void D3DXDriver::SetDepthStencilState(DepthStencilStates state)
  {
    static const char* names[] = {"DEPTH_DEFAULT","READ_WRITE","NONE","READ"};
    T8_LOG_TRACE("[D3D11] SetDepthStencilState(%s)", (state >= 0 && state <= 3) ? names[state] : "?");
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    switch (state)
    {
    case t850::BaseDriver::DEPTH_DEFAULT:
      deviceContext->OMSetDepthStencilState(m_depthStateReadWrite.Get(), 1);
      break;
    case t850::BaseDriver::READ_WRITE:
      deviceContext->OMSetDepthStencilState(m_depthStateReadWrite.Get(), 1);
      break;
    case t850::BaseDriver::NONE:
      deviceContext->OMSetDepthStencilState(m_depthStateNone.Get(), 1);
      break;
    case t850::BaseDriver::READ:
      deviceContext->OMSetDepthStencilState(m_depthStateRead.Get(), 1);
      break;
    default:
      break;
    }
    T8_TRACE(EvSetDepth((int)state));
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

  void D3DXDriver::SetCullFace(FaceCulling state) {
    m_FaceCulling = state;

    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = FALSE;
    rd.AntialiasedLineEnable = FALSE;

    switch (state) {
      case FRONT_FACES:       rd.CullMode = D3D11_CULL_BACK;  break;
      case BACK_FACES:        rd.CullMode = D3D11_CULL_FRONT; break;
      case FRONT_AND_BACK:    rd.CullMode = D3D11_CULL_NONE;  break;
      default:                rd.CullMode = D3D11_CULL_BACK;  break;
    }

    ID3D11RasterizerState* rs = nullptr;
    if (SUCCEEDED(device->CreateRasterizerState(&rd, &rs))) {
      deviceContext->RSSetState(rs);
      rs->Release();
    }
    T8_TRACE(EvSetCull((int)state));
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

#ifdef T850_RENDER_TRACE
  void D3DXDriver::RefreshTracePendingRenderState() {
    if (!T8_TRACE_ACTIVE()) return;
    int numAtt = 1;
    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size() && RTs[CurrentRT])
      numAtt = RTs[CurrentRT]->number_RT > 0 ? RTs[CurrentRT]->number_RT : 1;
    g_renderTracer->RecomputePendingRenderStateD3D11(numAtt);
  }
#endif

  void D3DXDriver::Clear() {
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    float rgba[4] = { 0.227f, 0.227f, 0.227f, 1.0f };
    deviceContext->ClearRenderTargetView(D3D11RenderTargetView.Get(), rgba);
    deviceContext->ClearDepthStencilView(D3D11DepthStencilTargetView.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    T8_TRACE(EvClearRT(-1, 1u | 2u, rgba[0], rgba[1], rgba[2], rgba[3], 0.0f, 0));
  }

  void D3DXDriver::ClearWithColor(float r, float g, float b, float a) {
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    float rgba[4] = { r, g, b, a };
    if (CurrentRT >= 0 && CurrentRT < (int)RTs.size()) {
      D3DXRT* rt = static_cast<D3DXRT*>(RTs[CurrentRT]);
      for (auto& rtv : rt->vD3D11RenderTargetView)
        deviceContext->ClearRenderTargetView(rtv.Get(), rgba);
      if (rt->D3D11DepthStencilTargetView)
        deviceContext->ClearDepthStencilView(rt->D3D11DepthStencilTargetView.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
      T8_TRACE(EvClearRT(CurrentRT,
                         (rt->number_RT > 0 ? 1u : 0u) | (rt->D3D11DepthStencilTargetView ? 2u : 0u),
                         rgba[0], rgba[1], rgba[2], rgba[3], 0.0f, 0));
    } else {
      deviceContext->ClearRenderTargetView(D3D11RenderTargetView.Get(), rgba);
      deviceContext->ClearDepthStencilView(D3D11DepthStencilTargetView.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
      T8_TRACE(EvClearRT(-1, 1u | 2u, rgba[0], rgba[1], rgba[2], rgba[3], 0.0f, 0));
    }
  }

  void D3DXDriver::SwapBuffers() {
    T8_PROFILE_SCOPE(t850::g_profiler, "D3D11_Present");
    T8_LOG_TRACE("[D3DXDriver] SwapBuffers/Present");

    // Frame timing
    static LARGE_INTEGER freq = {};
    static LARGE_INTEGER lastSwap = {};
    static int frameNum = 0;
    if (freq.QuadPart == 0) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&lastSwap); }
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    double ms = (now.QuadPart - lastSwap.QuadPart) * 1000.0 / freq.QuadPart;
    lastSwap = now;
    frameNum++;
    if (frameNum % 60 == 0) {
      T8_LOG_INFO("[D3D11] Frame %d: %.1fms (%.1f FPS)", frameNum, ms, 1000.0/ms);
    }

    // Swap between back and front buffer
    DXGISwapchain->Present(0, 0);
  }

  void D3DXDriver::PopRT() {
    T8_TRACE(EvPopRT());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    deviceContext->OMSetRenderTargets(1, D3D11RenderTargetView.GetAddressOf(), D3D11DepthStencilTargetView.Get());


    deviceContext->RSSetViewports(1, &viewport);

    if (CurrentRT >= 0) {
      if (RTs[CurrentRT]->GenMips) {
        D3DXTexture* pTex = dynamic_cast<D3DXTexture*>(RTs[CurrentRT]->vColorTextures[0]);
        deviceContext->GenerateMips(pTex->pSRVTex.Get());
      }

    }

    CurrentRT = -1;
#ifdef T850_RENDER_TRACE
    RefreshTracePendingRenderState();
#endif
  }

  static void SaveD3D11TextureToPPM(ID3D11Texture2D* srcTex, std::string path) {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());

    D3D11_TEXTURE2D_DESC desc;
    srcTex->GetDesc(&desc);

    // Resolve typeless formats to concrete formats for the staging texture
    DXGI_FORMAT readFormat = desc.Format;
    if (readFormat == DXGI_FORMAT_R32_TYPELESS)        readFormat = DXGI_FORMAT_R32_FLOAT;
    else if (readFormat == DXGI_FORMAT_R16_TYPELESS)    readFormat = DXGI_FORMAT_R16_FLOAT;
    else if (readFormat == DXGI_FORMAT_R24G8_TYPELESS)  readFormat = DXGI_FORMAT_R32_FLOAT;

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Format = readFormat;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;

    ComPtr<ID3D11Texture2D> stagingTex;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTex);
    if (FAILED(hr)) { T8_LOG_ERROR("CreateTexture2D staging failed hr=0x%08X fmt=%d", hr, readFormat); return; }

    deviceContext->CopySubresourceRegion(stagingTex.Get(), 0, 0, 0, 0, srcTex, 0, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = deviceContext->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) { T8_LOG_ERROR("Map failed hr=0x%08X", hr); return; }

    unsigned int w = desc.Width;
    unsigned int h = desc.Height;

    // Convert to RGB byte buffer first, then write as binary PPM P6
    std::vector<unsigned char> rgbBuf(w * h * 3);
    unsigned char* data = reinterpret_cast<unsigned char*>(mapped.pData);

    auto half2float = [](unsigned short h) -> float {
      unsigned int sign = (h >> 15) & 1;
      unsigned int exp = (h >> 10) & 0x1F;
      unsigned int mant = h & 0x3FF;
      if (exp == 0) return 0.0f;
      if (exp == 31) return sign ? -1e30f : 1e30f;
      float f = powf(2.0f, (float)((int)exp - 15)) * (1.0f + mant / 1024.0f);
      return sign ? -f : f;
    };

    for (unsigned int y = 0; y < h; y++) {
      unsigned char* row = data + y * mapped.RowPitch;
      for (unsigned int x = 0; x < w; x++) {
        unsigned int r = 0, g = 0, b = 0;
        if (readFormat == DXGI_FORMAT_R8G8B8A8_UNORM) {
          r = row[x * 4]; g = row[x * 4 + 1]; b = row[x * 4 + 2];
        } else if (readFormat == DXGI_FORMAT_B8G8R8A8_UNORM || readFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
          b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2];
        } else if (readFormat == DXGI_FORMAT_R8_UNORM) {
          r = g = b = row[x];
        } else if (readFormat == DXGI_FORMAT_R16G16B16A16_FLOAT) {
          const unsigned short* hf = reinterpret_cast<const unsigned short*>(row) + x * 4;
          float rf = half2float(hf[0]), gf = half2float(hf[1]), bf = half2float(hf[2]);
          rf = rf < 0.f ? 0.f : (rf > 1.f ? 1.f : rf);
          gf = gf < 0.f ? 0.f : (gf > 1.f ? 1.f : gf);
          bf = bf < 0.f ? 0.f : (bf > 1.f ? 1.f : bf);
          r = (unsigned int)(rf * 255.f); g = (unsigned int)(gf * 255.f); b = (unsigned int)(bf * 255.f);
        } else if (readFormat == DXGI_FORMAT_R32_FLOAT) {
          float v = *(reinterpret_cast<const float*>(row) + x);
          v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
          r = g = b = (unsigned int)(v * 255.f);
        } else if (readFormat == DXGI_FORMAT_R16_FLOAT) {
          float v = half2float(*(reinterpret_cast<const unsigned short*>(row) + x));
          v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
          r = g = b = (unsigned int)(v * 255.f);
        } else {
          r = row[x * 4]; g = row[x * 4 + 1]; b = row[x * 4 + 2];
        }
        unsigned int idx = (y * w + x) * 3;
        rgbBuf[idx] = (unsigned char)r; rgbBuf[idx+1] = (unsigned char)g; rgbBuf[idx+2] = (unsigned char)b;
      }
    }

    deviceContext->Unmap(stagingTex.Get(), 0);

    std::ofstream out(path + ".ppm", std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgbBuf.data()), rgbBuf.size());
  }

  void D3DXDriver::SaveScreenshot(std::string path) {
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = DXGISwapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
    if (FAILED(hr)) return;
    SaveD3D11TextureToPPM(backBuffer.Get(), path);
  }

  void D3DXDriver::SaveRTToFile(int rtID, int attachment, std::string path) {
    if (rtID < 0 || rtID >= (int)RTs.size())
      return;
    Texture* tex = GetRTTexture(rtID, attachment);
    if (!tex) return;
    D3DXTexture* d3dTex = dynamic_cast<D3DXTexture*>(tex);
    if (!d3dTex || !d3dTex->Tex) return;
    SaveD3D11TextureToPPM(d3dTex->Tex.Get(), path);
  }
}
