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

#include <video/windows/D3DXTexture.h>

namespace t800 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  void	D3DXTexture::SetTextureParams() {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    D3D11_SAMPLER_DESC sdesc;

    sdesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sdesc.MaxAnisotropy = 16;

    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    if (params & TEXT_BASIC_PARAMS::CLAMP_TO_EDGE) {
      sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    }

    if (params & TEXT_BASIC_PARAMS::TILED) {
      sdesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
      sdesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
      sdesc.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;
    }

    sdesc.BorderColor[0] = 0.0f;
    sdesc.BorderColor[1] = 0.0f;
    sdesc.BorderColor[2] = 0.0f;
    sdesc.BorderColor[3] = 0.0f;
    sdesc.MinLOD = 0.0f;
    sdesc.MaxLOD = D3D11_FLOAT32_MAX;
    sdesc.MipLODBias = 0.0f;
    sdesc.MaxAnisotropy = 16;

    device->CreateSamplerState(&sdesc, pSampler.GetAddressOf());

  }

  void	D3DXTexture::GetFormatBpp(unsigned int &props, unsigned int &Format, unsigned int &bpp) {

  }

  void	D3DXTexture::LoadAPITexture(DeviceContext* context, unsigned char* buffer) {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    D3D11_TEXTURE2D_DESC desc = { 0 };
    desc.Width = this->x;
    desc.Height = this->y;

    if (cil_props & CIL_CUBE_MAP)
      desc.ArraySize = 6;
    else
      desc.ArraySize = 1;

    if (this->props&TEXT_BASIC_FORMAT::CH_ALPHA)
      desc.Format = DXGI_FORMAT_R8_UNORM;
    else
      desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    desc.MiscFlags = 0;
    if (cil_props & CIL_CUBE_MAP) {
      desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    }
    desc.MipLevels = 0;
    desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;

    HRESULT hr;
    hr = device->CreateTexture2D(&desc, nullptr, Tex.GetAddressOf());

    if (hr != S_OK) {
      this->id = -1;
      return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    if (cil_props & CIL_CUBE_MAP) {
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
      srvDesc.Texture2D.MipLevels = -1;
      srvDesc.TextureCube.MipLevels = -1;
    }
    else {
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = -1;
    }

    device->CreateShaderResourceView(Tex.Get(), &srvDesc, pSRVTex.GetAddressOf());

    D3D11_SUBRESOURCE_DATA initData[6];
    int bufferSize = this->size / 6;
    if (cil_props & CIL_CUBE_MAP) {
      unsigned char *pHead = buffer;
      for (int i = 0; i < 6; i++) {
        initData[i].pSysMem = pHead;
        initData[i].SysMemPitch = sizeof(unsigned char) * this->x * 4;
        pHead += bufferSize;
      }
    }
    else {
      initData[0].pSysMem = buffer;
      initData[0].SysMemPitch = sizeof(unsigned char) * this->x * m_channels;
    }
    D3D11_TEXTURE2D_DESC pDesc;
    Tex->GetDesc(&pDesc);
    int MipMapCount = pDesc.MipLevels;
    if (cil_props & CIL_CUBE_MAP) {
      for (int i = 0; i < 6; i++) {
        deviceContext->UpdateSubresource(Tex.Get(), D3D11CalcSubresource(0, i, MipMapCount), 0, initData[i].pSysMem, initData[i].SysMemPitch, 0);
      }
    }
    else {
      deviceContext->UpdateSubresource(Tex.Get(), 0, 0, buffer, initData[0].SysMemPitch, 0);
    }

    deviceContext->GenerateMips(pSRVTex.Get());

    SetTextureParams();
    static int texid = 0;
    this->id = texid;
    texid++;
  }

  void	D3DXTexture::LoadAPITextureCompressed(unsigned char* buffer) {

  }

  void D3DXTexture::DestroyAPITexture() {
    Tex.Reset();
    pSRVTex.Reset();
    pSampler.Reset();
  }

  void D3DXTexture::Set(const DeviceContext & deviceContext, unsigned int slot, std::string shaderTextureName)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->PSSetShaderResources(slot, 1, pSRVTex.GetAddressOf());
  }

  void D3DXTexture::SetSampler(const DeviceContext & deviceContext)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->PSSetSamplers(0, 1, pSampler.GetAddressOf());
  }
}