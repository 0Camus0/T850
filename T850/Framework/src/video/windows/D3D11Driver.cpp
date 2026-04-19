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

#include <video/windows/D3D11Driver.h>
#include <video/windows/D3D11RT.h>
#include <video/windows/D3D11Shader.h>
#include <video/windows/D3D11Texture.h>

#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <utils/Log.h>



namespace t800 {
  // D3D11 Main Objects
  ComPtr<IDXGISwapChain>			DXGISwapchain;	// Responsible of the swap buffers
  ComPtr<ID3D11RenderTargetView>  D3D11RenderTargetView;  // View into the back buffer
  ComPtr<ID3D11DepthStencilView>  D3D11DepthStencilTargetView; // View into the depth buffer
  ComPtr<ID3D11Texture2D>			D3D11DepthTex;	// Actual depth buffer texture

  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;
  void * D3DXDeviceContext::GetAPIObject() const
  {
    return (void*)APIContext;
  }
  void ** D3DXDeviceContext::GetAPIObjectReference() const
  {
    return (void**)&APIContext;
  }
  void D3DXDeviceContext::release()
  {
    APIContext->Release();
  }
  void D3DXDeviceContext::SetPrimitiveTopology(T8_TOPOLOGY::E topology)
  {
    D3D11_PRIMITIVE_TOPOLOGY apitopology;
    switch (topology)
    {
    case T8_TOPOLOGY::POINT_LIST:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
      break;
    case T8_TOPOLOGY::LINE_LIST:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
      break;
    case T8_TOPOLOGY::LINE_STRIP:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
      break;
    case T8_TOPOLOGY::TRIANLE_LIST:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      break;
    case T8_TOPOLOGY::TRIANGLE_STRIP:
      apitopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
      break;
    default:
      break;
    }
    APIContext->IASetPrimitiveTopology(apitopology);
  }

  void D3DXDeviceContext::DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex)
  {
    APIContext->DrawIndexed(vertexCount, startIndex, startVertex);
  }

  void * D3DXDevice::GetAPIObject() const
  {
    return (void*)APIDevice;
  }
  void ** D3DXDevice::GetAPIObjectReference() const
  {
    return (void**)&APIDevice;
  }
  void D3DXDevice::release()
  {
    APIDevice->Release();
  }

  Buffer * D3DXDevice::CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData)
  {
    Buffer* retBuff;
    switch (bufferType)
    {
    case T8_BUFFER_TYPE::VERTEX:
      retBuff = new D3DXVertexBuffer;
      break;
    case T8_BUFFER_TYPE::INDEX:
      retBuff = new D3DXIndexBuffer;
      break;
    case T8_BUFFER_TYPE::CONSTANT:
      retBuff = new D3DXConstantBuffer;
      break;
    default:
      break;
    }
    retBuff->Create(*this, desc, initialData);
    return retBuff;
  }

  ShaderBase * D3DXDevice::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key, const std::string& vs_name, const std::string& fs_name)
  {
    ShaderBase *sh = new D3DXShader();
    if (!sh->CreateShader(src_vs, src_fs, key, vs_name, fs_name)) {
      delete sh;
      return nullptr;
    }
    return sh;
  }

  Texture * D3DXDevice::CreateTexture(std::string path)
  {
    D3DXTexture* txture = new D3DXTexture;
    txture->LoadTexture(path.c_str());
    return txture;
  }

  Texture * D3DXDevice::CreateTextureFromMemory(const unsigned char * buff, int w, int h, int channels, std::string name)
  {
    D3DXTexture* txture = new D3DXTexture;
    txture->LoadFromMemory(buff,w,h,channels);
    return txture;
  }

  Texture * D3DXDevice::CreateCubeMap(const unsigned char * buff, int w, int h)
  {
    D3DXTexture* txture = new D3DXTexture;
    txture->CreateCubeMap(buff, w, h);
    return txture;
  }

  BaseRT * D3DXDevice::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips)
  {
    BaseRT* rt = new D3DXRT;
    if (rt->LoadRT(nrt, cf, df, w, h, genMips)) {
      return rt;
    }
    delete rt;
    return nullptr;
  }


  void * D3DXVertexBuffer::GetAPIObject() const
  {
    return (void*)APIBuffer;
  }

  void ** D3DXVertexBuffer::GetAPIObjectReference() const
  {
    return (void**)&APIBuffer;
  }

  void D3DXVertexBuffer::Set(const DeviceContext & deviceContext, const unsigned stride, const unsigned offset)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualVertexBuffer = (VertexBuffer*)this;
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->IASetVertexBuffers(0, 1, &APIBuffer, &stride, &offset);
  }
  void D3DXVertexBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
  {
    descriptor = desc;
    D3D11_USAGE usage;
    switch (desc.usage)
    {
    case T8_BUFFER_USAGE::DEFAULT:
      usage = D3D11_USAGE_DEFAULT;
      break;
    case T8_BUFFER_USAGE::DINAMIC:
      usage = D3D11_USAGE_DYNAMIC;
      break;
    case T8_BUFFER_USAGE::STATIC:
      usage = D3D11_USAGE_IMMUTABLE;
      break;
    default:
      usage = D3D11_USAGE_DEFAULT;
      break;
    }
    D3D11_BUFFER_DESC apiDesc{ 0 };
    apiDesc.ByteWidth = desc.byteWidth;
    apiDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    apiDesc.Usage = usage;
    //apiDesc.CPUAccessFlags = D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE;
    //apiDesc.StructureByteStride = ;
    //apiDesc.MiscFlags = ;

    if (initialData != nullptr)
    {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      D3D11_SUBRESOURCE_DATA subData = { initialData, 0, 0 };
      reinterpret_cast<ID3D11Device*>(device.GetAPIObject())->CreateBuffer(&apiDesc, &subData, &APIBuffer);
    }
    else
    {
      reinterpret_cast<ID3D11Device*>(device.GetAPIObject())->CreateBuffer(&apiDesc, 0, &APIBuffer);
    }

  }
  void D3DXVertexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer, 0, 0, &sysMemCpy[0], 0, 0);
  }
  void D3DXVertexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer, 0, 0, buffer, 0, 0);
  }
  void D3DXVertexBuffer::release()
  {
    APIBuffer->Release();
    sysMemCpy.clear();
    delete this;
  }


  void * D3DXIndexBuffer::GetAPIObject() const
  {
    return (void*)APIBuffer;
  }

  void ** D3DXIndexBuffer::GetAPIObjectReference() const
  {
    return (void**)&APIBuffer;
  }

  void D3DXIndexBuffer::Set(const DeviceContext & deviceContext, const unsigned offset, T8_IB_FORMAR::E format)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    DXGI_FORMAT apiformat;
    if (format == T8_IB_FORMAR::R16)
      apiformat = DXGI_FORMAT::DXGI_FORMAT_R16_UINT;
    else
      apiformat = DXGI_FORMAT::DXGI_FORMAT_R32_UINT;
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->IASetIndexBuffer(APIBuffer, apiformat, offset);
  }
  void D3DXIndexBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
  {
    descriptor = desc;
    D3D11_USAGE usage;
    switch (desc.usage)
    {
    case T8_BUFFER_USAGE::DEFAULT:
      usage = D3D11_USAGE_DEFAULT;
      break;
    case T8_BUFFER_USAGE::DINAMIC:
      usage = D3D11_USAGE_DYNAMIC;
      break;
    case T8_BUFFER_USAGE::STATIC:
      usage = D3D11_USAGE_IMMUTABLE;
      break;
    default:
      usage = D3D11_USAGE_DEFAULT;
      break;
    }
    D3D11_BUFFER_DESC apiDesc{ 0 };
    apiDesc.ByteWidth = desc.byteWidth;
    apiDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    apiDesc.Usage = usage;

    if (initialData != nullptr)
    {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      D3D11_SUBRESOURCE_DATA subData = { initialData, 0, 0 };
      reinterpret_cast<ID3D11Device*>(device.GetAPIObject())->CreateBuffer(&apiDesc, &subData, &APIBuffer);
    }
    else
    {
      reinterpret_cast<ID3D11Device*>(device.GetAPIObject())->CreateBuffer(&apiDesc, 0, &APIBuffer);
    }
  }
  void D3DXIndexBuffer::UpdateFromSystemCopy(const DeviceContext& deviceContext)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer, 0, 0, &sysMemCpy[0], 0, 0);
  }
  void D3DXIndexBuffer::UpdateFromBuffer(const DeviceContext& deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer, 0, 0, buffer, 0, 0);
  }
  void D3DXIndexBuffer::release()
  {
    APIBuffer->Release();
    sysMemCpy.clear();
    delete this;
  }


  void * D3DXConstantBuffer::GetAPIObject() const
  {
    return (void*)APIBuffer;
  }

  void ** D3DXConstantBuffer::GetAPIObjectReference() const
  {
    return (void**)&APIBuffer;
  }

  void D3DXConstantBuffer::Set(const DeviceContext & deviceContext)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualConstantBuffer = (ConstantBuffer*)this;
    ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject());
    context->VSSetConstantBuffers(0, 1, &APIBuffer);
    context->PSSetConstantBuffers(0, 1, &APIBuffer);
  }
  void D3DXConstantBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
  {
    descriptor = desc;
    ID3D11Device* apiDevice = reinterpret_cast<ID3D11Device*>(device.GetAPIObject());
    D3D11_USAGE usage;
    switch (desc.usage)
    {
    case T8_BUFFER_USAGE::DEFAULT:
      usage = D3D11_USAGE_DEFAULT;
      break;
    case T8_BUFFER_USAGE::DINAMIC:
      usage = D3D11_USAGE_DYNAMIC;
      break;
    case T8_BUFFER_USAGE::STATIC:
      usage = D3D11_USAGE_IMMUTABLE;
      break;
    default:
      usage = D3D11_USAGE_DEFAULT;
      break;
    }
    D3D11_BUFFER_DESC apiDesc{ 0 };
    apiDesc.ByteWidth = desc.byteWidth;
    apiDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    apiDesc.Usage = usage;

    if (initialData != nullptr)
    {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
      D3D11_SUBRESOURCE_DATA subData = { initialData, 0, 0 };
      reinterpret_cast<ID3D11Device*>(device.GetAPIObject())->CreateBuffer(&apiDesc, &subData, &APIBuffer);
    }
    else
    {
      reinterpret_cast<ID3D11Device*>(device.GetAPIObject())->CreateBuffer(&apiDesc, 0, &APIBuffer);
    }
  }
  void D3DXConstantBuffer::UpdateFromSystemCopy(const DeviceContext & deviceContext)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer, 0, 0, &sysMemCpy[0], 0, 0);
  }
  void D3DXConstantBuffer::UpdateFromBuffer(const DeviceContext & deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->UpdateSubresource(APIBuffer, 0, 0, (char*)buffer, 0, 0);
  }
  void D3DXConstantBuffer::release()
  {
    APIBuffer->Release();
    sysMemCpy.clear();
    delete this;
  }



  void D3DXDriver::InitDriver() {
    T8Device = new t800::D3DXDevice;
    T8DeviceContext = new t800::D3DXDeviceContext;

    //	Descriptor of the Back Buffer
    DXGI_MODE_DESC BackBufferDesc;
    ZeroMemory(&BackBufferDesc, sizeof(DXGI_MODE_DESC));
    BackBufferDesc.Width = width;
    BackBufferDesc.Height = height;
    BackBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA 32 bit buffer
    BackBufferDesc.RefreshRate.Numerator = 0;
    BackBufferDesc.RefreshRate.Denominator = 1;
    BackBufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    BackBufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

    //	Descriptor of the Swap Chain
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

    //	Function to create three objects:
    //	Device
    //	Device Context
    //	Swap Chain
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
#if DEBUG_DRIVER
      D3D11_CREATE_DEVICE_DEBUG,	// Debug errors
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
    descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;	// 24 bits for depth 8 bits for stencil
    descDepth.SampleDesc.Count = 1;
    descDepth.SampleDesc.Quality = 0;
    descDepth.Usage = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL; // -- > Use it as depth stencil
    descDepth.CPUAccessFlags = 0;
    descDepth.MiscFlags = 0;
    hr = device->CreateTexture2D(&descDepth, NULL, &D3D11DepthTex);	// Output to the depth texture

    // Descriptor to create the Depth View
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd;
    ZeroMemory(&dsvd, sizeof(dsvd));

    dsvd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    // Using the View we can operate with the depth buffer, note this view is created from the depth texture
    hr = device->CreateDepthStencilView(D3D11DepthTex.Get(), &dsvd, &D3D11DepthStencilTargetView);


    //	Now we create the main render target view from the back buffer texture
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
    device->CreateBlendState(&BlendStatedesc, &m_BlendStateOpaque);
    //Additive
    BlendStatedesc = CD3D11_BLEND_DESC(def);
    BlendStatedesc.RenderTarget[0].BlendEnable = TRUE;
    BlendStatedesc.RenderTarget[0].SrcBlend =
      BlendStatedesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    BlendStatedesc.RenderTarget[0].DestBlend =
      BlendStatedesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    device->CreateBlendState(&BlendStatedesc, &m_BlendStateAdditive);
    //AlphaBlend
    //BlendStatedesc = CD3D11_BLEND_DESC(def);
    //BlendStatedesc.RenderTarget[0].BlendEnable = TRUE;
    //BlendStatedesc.RenderTarget[0].SrcBlend =
    //  BlendStatedesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    //BlendStatedesc.RenderTarget[0].DestBlend =
    //  BlendStatedesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    //device->CreateBlendState(&BlendStatedesc, &m_BlendStateAlphaBlend);

    BlendStatedesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    BlendStatedesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    BlendStatedesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    BlendStatedesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    BlendStatedesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    BlendStatedesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    BlendStatedesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&BlendStatedesc, &m_BlendStateAlphaBlend);
    //NonPremultiplied
    BlendStatedesc = CD3D11_BLEND_DESC(def);
    BlendStatedesc.RenderTarget[0].BlendEnable = TRUE;
    BlendStatedesc.RenderTarget[0].SrcBlend =
      BlendStatedesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    BlendStatedesc.RenderTarget[0].DestBlend =
      BlendStatedesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    device->CreateBlendState(&BlendStatedesc, &m_BlendStateNonPremultiplied);

    /*DEPTH STATES*/

    //ReadWrite
    CD3D11_DEPTH_STENCIL_DESC BlendDesc(def);
    BlendDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device->CreateDepthStencilState(&BlendDesc, &m_depthStateReadWrite);
    // DepthNone
    BlendDesc = CD3D11_DEPTH_STENCIL_DESC(def);
    BlendDesc.DepthEnable = FALSE;
    BlendDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    BlendDesc.DepthFunc = D3D11_COMPARISON_EQUAL;
    device->CreateDepthStencilState(&BlendDesc, &m_depthStateNone);
    // DepthRead
    BlendDesc = CD3D11_DEPTH_STENCIL_DESC(def);
    BlendDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    BlendDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device->CreateDepthStencilState(&BlendDesc, &m_depthStateRead);

    /*RASTERIZER STATES*/


    //SetBlendState(BLEND_STATES::BLEND_DEFAULT);
    //SetDepthStencilState(DEPTH_STENCIL_STATES::DEPTH_DEFAULT);
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
    m_BlendStateAdditive->Release();
    m_BlendStateAlphaBlend->Release();
    m_BlendStateNonPremultiplied->Release();
    m_BlendStateOpaque->Release();
    m_depthStateNone->Release();
    m_depthStateRead->Release();
    m_depthStateReadWrite->Release();

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
    depthDesc.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage            = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags        = D3D11_BIND_DEPTH_STENCIL;
    hr = device->CreateTexture2D(&depthDesc, nullptr, &D3D11DepthTex);
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
    dsvd.Format        = DXGI_FORMAT_D24_UNORM_S8_UINT;
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

  void D3DXDriver::SetBlendState(BLEND_STATES state)
  {
    static const char* names[] = {"BLEND_DEFAULT","BLEND_OPAQUE","ADDITIVE","ALPHA_BLEND","NON_PREMULTIPLIED"};
    T8_LOG_TRACE("[D3D11] SetBlendState(%s)", (state >= 0 && state <= 4) ? names[state] : "?");
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    switch (state)
    {
    case t800::BaseDriver::BLEND_DEFAULT:
      deviceContext->OMSetBlendState(m_BlendStateOpaque, 0, 0xffffffff);
      break;
    case t800::BaseDriver::BLEND_STATES::BLEND_OPAQUE:
      break;
    case t800::BaseDriver::ADDITIVE:
      deviceContext->OMSetBlendState(m_BlendStateAdditive, 0, 0xffffffff);
      break;
    case t800::BaseDriver::ALPHA_BLEND:
      deviceContext->OMSetBlendState(m_BlendStateAlphaBlend, 0, 0xffffffff);
      break;
    case t800::BaseDriver::NON_PREMULTIPLIED:
      deviceContext->OMSetBlendState(m_BlendStateNonPremultiplied, 0, 0xffffffff);
      break;
    default:
      break;
    }
  }

  void D3DXDriver::SetDepthStencilState(DEPTH_STENCIL_STATES state)
  {
    static const char* names[] = {"DEPTH_DEFAULT","READ_WRITE","NONE","READ"};
    T8_LOG_TRACE("[D3D11] SetDepthStencilState(%s)", (state >= 0 && state <= 3) ? names[state] : "?");
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    switch (state)
    {
    case t800::BaseDriver::DEPTH_DEFAULT:
      deviceContext->OMSetDepthStencilState(m_depthStateReadWrite, 1);
      break;
    case t800::BaseDriver::READ_WRITE:
      deviceContext->OMSetDepthStencilState(m_depthStateReadWrite, 1);
      break;
    case t800::BaseDriver::NONE:
      deviceContext->OMSetDepthStencilState(m_depthStateNone, 1);
      break;
    case t800::BaseDriver::READ:
      deviceContext->OMSetDepthStencilState(m_depthStateRead, 1);
      break;
    default:
      break;
    }
  }

  void D3DXDriver::SetCullFace(FACE_CULLING state) {
	  // TO FILL
  }

  void D3DXDriver::Clear() {
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    float rgba[4];
    rgba[0] = 0.9f;
    rgba[1] = 0.9f;
    rgba[2] = 0.9f;
    rgba[3] = 1.0f;

    // Clearing the Main Render Target View
    deviceContext->ClearRenderTargetView(D3D11RenderTargetView.Get(), rgba);
    // Clearing the Depth Buffer
    deviceContext->ClearDepthStencilView(D3D11DepthStencilTargetView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
  }

  void D3DXDriver::SwapBuffers() {
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
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    deviceContext->OMSetRenderTargets(1, D3D11RenderTargetView.GetAddressOf(), D3D11DepthStencilTargetView.Get());


    deviceContext->RSSetViewports(1, &viewport);

    if (CurrentRT >= 0) {
      if (RTs[CurrentRT]->GenMips) {
        D3DXTexture* pTex = dynamic_cast<D3DXTexture*>(RTs[CurrentRT]->vColorTextures[0]);
        deviceContext->GenerateMips(pTex->pSRVTex.Get());
      }

    }

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