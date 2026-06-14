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

#include <video/d3d11/D3D11RT.h>
#include <debug/RenderTrace.h>
#include <iostream>

namespace t850 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  bool D3DXRT::LoadAPIRT() {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    DXGI_FORMAT cfmt;
    DXGI_FORMAT depthFormat, depthShaderViewFormat, depthResourceViewFormat;

    switch (this->color_format) {
    case BaseRT::NOTHING: {
      cfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
      number_RT = 0;
    }break;
    case BaseRT::R8:
      cfmt = DXGI_FORMAT_R8_UNORM;
      break;
    case BaseRT::F16:
      cfmt = DXGI_FORMAT_R16_FLOAT;
      break;
    case BaseRT::F32:
      cfmt = DXGI_FORMAT_R32_FLOAT;
      break;
    case BaseRT::RGB8:
    case BaseRT::RGBA8: {
      cfmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    }break;
    case BaseRT::RGBA16F: {
      cfmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
    }break;
    case BaseRT::RGBA32F: {
      cfmt = DXGI_FORMAT_R32G32B32A32_FLOAT;
    }break;
    }

    switch (this->depth_format) {
    case BaseRT::NOTHING: {
      depthFormat = DXGI_FORMAT_R32_TYPELESS;
      depthShaderViewFormat = DXGI_FORMAT_D32_FLOAT;
      depthResourceViewFormat = DXGI_FORMAT_R32_FLOAT;
    }break;
    case BaseRT::FD16: {
      depthFormat = DXGI_FORMAT_R16_TYPELESS;
      depthShaderViewFormat = DXGI_FORMAT_D16_UNORM;
      depthResourceViewFormat = DXGI_FORMAT_R16_FLOAT;
    }break;
    case BaseRT::F32: {
      depthFormat = DXGI_FORMAT_R32_TYPELESS;
      depthShaderViewFormat = DXGI_FORMAT_D32_FLOAT;
      depthResourceViewFormat = DXGI_FORMAT_R32_FLOAT;
    }break;
    case BaseRT::CUBE_F32: {
      depthFormat = DXGI_FORMAT_R32_TYPELESS;
      depthShaderViewFormat = DXGI_FORMAT_D32_FLOAT;
      depthResourceViewFormat = DXGI_FORMAT_R32_FLOAT;
      isCubeDepth = true;
    }break;
    }


    HRESULT hr;
    for (int i = 0; i < number_RT; i++) {
      D3D11_TEXTURE2D_DESC desc = { 0 };
      desc.Width = w;
      desc.Height = h;
      desc.ArraySize = 1;
      // Use per-attachment format if available, otherwise single cfmt
      DXGI_FORMAT thisFmt = cfmt;
      if (!perColorFormats.empty() && i < (int)perColorFormats.size()) {
        switch (perColorFormats[i]) {
          case BaseRT::R8:      thisFmt = DXGI_FORMAT_R8_UNORM; break;
          case BaseRT::F16:     thisFmt = DXGI_FORMAT_R16_FLOAT; break;
          case BaseRT::F32:     thisFmt = DXGI_FORMAT_R32_FLOAT; break;
          case BaseRT::RGB8:
          case BaseRT::RGBA8:   thisFmt = DXGI_FORMAT_R8G8B8A8_UNORM; break;
          case BaseRT::RGBA16F: thisFmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
          case BaseRT::RGBA32F: thisFmt = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
          default: break;
        }
      }
      desc.Format = thisFmt;
      desc.SampleDesc.Count = 1;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.MipLevels = GenMips ? 0 : 1;
      desc.MiscFlags = GenMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
      ComPtr<ID3D11Texture2D> Tex;
      hr = device->CreateTexture2D(&desc, nullptr, Tex.GetAddressOf());
      if (hr != S_OK) {
        std::cout << "Error loading RT texture index " << i << std::endl;
        exit(444);
      }
      vD3D11ColorTex.push_back(Tex);
      D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
      ZeroMemory(&rtDesc, sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
      rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtDesc.Format = desc.Format;
      ComPtr<ID3D11RenderTargetView> RTV;
      hr = device->CreateRenderTargetView(Tex.Get(), &rtDesc, &RTV);
      if (hr != S_OK) {
        std::cout << "Error creating RTV index " << i << std::endl;
        exit(444);
      }
      vD3D11RenderTargetView.push_back(RTV);

      D3DXTexture *pTextureColor = new D3DXTexture;
      pTextureColor->Tex = Tex;

      D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
      shaderResourceViewDesc.Format = thisFmt;
      shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
      shaderResourceViewDesc.Texture2D.MipLevels = GenMips ? -1 : 1;

      hr = device->CreateShaderResourceView(Tex.Get(), &shaderResourceViewDesc, &pTextureColor->pSRVTex);
      if (hr != S_OK) {
        delete pTextureColor;
        std::cout << "Error creating Shader Resource View index " << i << std::endl;
        exit(444);
      }
      pTextureColor->x = w;
      pTextureColor->y = h;
      if (!GenMips || i > 0) {
        pTextureColor->params = TextBasicParams::LINEAR_FILTER;
      }
      pTextureColor->SetTextureParams();
      vColorTextures.push_back(pTextureColor);
    }

    D3D11_TEXTURE2D_DESC descDepth;
    ZeroMemory(&descDepth, sizeof(descDepth));
    descDepth.Width = w;
    descDepth.Height = h;
    descDepth.MipLevels = 1;
    descDepth.Format = depthFormat;
    descDepth.SampleDesc.Count = 1;
    descDepth.SampleDesc.Quality = 0;
    descDepth.Usage = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
    descDepth.CPUAccessFlags = 0;

    if (isCubeDepth) {
      descDepth.ArraySize = 6;
      descDepth.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    } else {
      descDepth.ArraySize = 1;
      descDepth.MiscFlags = 0;
    }

    hr = device->CreateTexture2D(&descDepth, NULL, &D3D11DepthTex);
    if (hr != S_OK) {
      std::cout << "Error loading RT depth texture " << std::endl;
      exit(444);
    }

    if (isCubeDepth) {
      // Create per-face DSVs for cubemap rendering
      for (int face = 0; face < 6; face++) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvd;
        ZeroMemory(&dsvd, sizeof(dsvd));
        dsvd.Format = depthShaderViewFormat;
        dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvd.Texture2DArray.MipSlice = 0;
        dsvd.Texture2DArray.FirstArraySlice = face;
        dsvd.Texture2DArray.ArraySize = 1;
        hr = device->CreateDepthStencilView(D3D11DepthTex.Get(), &dsvd, &D3D11CubeFaceDSVs[face]);
        if (hr != S_OK) {
          std::cout << "Error creating Cube Depth Stencil View face " << face << std::endl;
          exit(444);
        }
      }
      D3D11DepthStencilTargetView = D3D11CubeFaceDSVs[0];
    } else {
      D3D11_DEPTH_STENCIL_VIEW_DESC dsvd;
      ZeroMemory(&dsvd, sizeof(dsvd));
      dsvd.Format = depthShaderViewFormat;
      dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
      hr = device->CreateDepthStencilView(D3D11DepthTex.Get(), &dsvd, &D3D11DepthStencilTargetView);
      if (hr != S_OK) {
        std::cout << "Error creating Depth Stencil View " << std::endl;
        exit(444);
      }
    }

    D3DXTexture *pTextureDepth = new D3DXTexture;
    pTextureDepth->Tex = D3D11DepthTex;
    D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
    ZeroMemory(&shaderResourceViewDesc, sizeof(shaderResourceViewDesc));
    shaderResourceViewDesc.Format = depthResourceViewFormat;
    if (isCubeDepth) {
      shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
      shaderResourceViewDesc.TextureCube.MostDetailedMip = 0;
      shaderResourceViewDesc.TextureCube.MipLevels = 1;
    } else {
      shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
      shaderResourceViewDesc.Texture2D.MipLevels = 1;
    }
    hr = device->CreateShaderResourceView(D3D11DepthTex.Get(), &shaderResourceViewDesc, &pTextureDepth->pSRVTex);
    if (hr != S_OK) {
      delete pTextureDepth;
      std::cout << "Error creating Shader Resource View Depth " << std::endl;
      exit(444);
    }
    pTextureDepth->x = w;
    pTextureDepth->y = h;
    pTextureDepth->params |= TextBasicParams::CLAMP_TO_BORDER;
    pTextureDepth->SetTextureParams();
    pDepthTexture = ( pTextureDepth);


    return true;
  }

  void D3DXRT::DestroyAPIRT() {
    if (T8DeviceContext && T8DeviceContext->GetAPIObject()) {
      auto* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
      ID3D11RenderTargetView* nullRTV[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      ID3D11ShaderResourceView* nullSRV[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
      deviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRTV, nullptr);
      deviceContext->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
      deviceContext->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
      deviceContext->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
      deviceContext->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
    }
    if (pDepthTexture) {
      pDepthTexture->release();
      pDepthTexture = nullptr;
    }

    for (size_t i = 0; i < vColorTextures.size(); i++) {
      if (vColorTextures[i])
        vColorTextures[i]->release();
    }
    vColorTextures.clear();
    vD3D11RenderTargetView.clear();
    vD3D11ColorTex.clear();
    D3D11DepthTex.Reset();
    D3D11DepthStencilTargetView.Reset();
    for (auto& dsv : D3D11CubeFaceDSVs)
      dsv.Reset();
    isCubeDepth = false;
  }
  void D3DXRT::Set(const DeviceContext& context)
  {
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    std::vector<ID3D11RenderTargetView**> RTVA;
    for (int i = 0; i < number_RT; i++) {
      RTVA.push_back(vD3D11RenderTargetView[i].GetAddressOf());
    }

    if (number_RT == 0)
      RTVA.push_back(0);

    // For cube depth, clear all 6 faces then bind face 0
    if (isCubeDepth) {
      for (int face = 0; face < 6; face++) {
        deviceContext->OMSetRenderTargets(number_RT, &RTVA[0][0], D3D11CubeFaceDSVs[face].Get());
        deviceContext->ClearDepthStencilView(D3D11CubeFaceDSVs[face].Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
      }
      D3D11DepthStencilTargetView = D3D11CubeFaceDSVs[0];
    }

    deviceContext->OMSetRenderTargets(number_RT, &RTVA[0][0], D3D11DepthStencilTargetView.Get());

    D3D11_VIEWPORT viewport_RT;
    viewport_RT.TopLeftX = 0;
    viewport_RT.TopLeftY = 0;
    viewport_RT.Width = static_cast<float>(w);
    viewport_RT.Height = static_cast<float>(h);
    viewport_RT.MinDepth = 0;
    viewport_RT.MaxDepth = 1;

    deviceContext->RSSetViewports(1, &viewport_RT);

    float rgba[4];
    rgba[0] = 0.0f;
    rgba[1] = 0.0f;
    rgba[2] = 0.0f;
    rgba[3] = 0.0f;

    for (int i = 0; i < number_RT; i++) {
      deviceContext->ClearRenderTargetView(vD3D11RenderTargetView[i].Get(), rgba);
    }

    if (!isCubeDepth) {
      deviceContext->ClearDepthStencilView(D3D11DepthStencilTargetView.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int rtId = g_renderTracer->LookupRTId(this);
      uint32_t flags = (number_RT > 0 ? 1u : 0u) | 2u;
      g_renderTracer->EvClearRT(rtId, flags, rgba[0], rgba[1], rgba[2], rgba[3], 0.0f, 0);
    }
#endif
  }

  void D3DXRT::SetLoad(const DeviceContext& context)
  {
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    std::vector<ID3D11RenderTargetView**> RTVA;
    for (int i = 0; i < number_RT; i++) {
      RTVA.push_back(vD3D11RenderTargetView[i].GetAddressOf());
    }

    if (number_RT == 0)
      RTVA.push_back(0);

    if (isCubeDepth) {
      D3D11DepthStencilTargetView = D3D11CubeFaceDSVs[0];
    }

    deviceContext->OMSetRenderTargets(number_RT, &RTVA[0][0], D3D11DepthStencilTargetView.Get());

    D3D11_VIEWPORT viewport_RT;
    viewport_RT.TopLeftX = 0;
    viewport_RT.TopLeftY = 0;
    viewport_RT.Width = static_cast<float>(w);
    viewport_RT.Height = static_cast<float>(h);
    viewport_RT.MinDepth = 0;
    viewport_RT.MaxDepth = 1;

    deviceContext->RSSetViewports(1, &viewport_RT);
  }

  void D3DXRT::ChangeCubeDepthTexture(int i)
  {
    if (!isCubeDepth || i < 0 || i >= 6) return;

    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());

    D3D11DepthStencilTargetView = D3D11CubeFaceDSVs[i];

    // Re-bind OM targets with the new face DSV
    if (number_RT > 0) {
      std::vector<ID3D11RenderTargetView**> RTVA;
      for (int j = 0; j < number_RT; j++) {
        RTVA.push_back(vD3D11RenderTargetView[j].GetAddressOf());
      }
      deviceContext->OMSetRenderTargets(number_RT, &RTVA[0][0], D3D11DepthStencilTargetView.Get());
    } else {
      ID3D11RenderTargetView* nullRTV = nullptr;
      deviceContext->OMSetRenderTargets(0, &nullRTV, D3D11DepthStencilTargetView.Get());
    }
  }
}