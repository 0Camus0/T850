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

#include <video/d3d11/D3D11Texture.h>
#include <utils/Log.h>

namespace t800 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  void	D3DXTexture::SetTextureParams() {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    D3D11_SAMPLER_DESC sdesc;

    sdesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sdesc.MaxAnisotropy = 16;

    if (params & TEXT_BASIC_PARAMS::NEAREST_FILTER) {
      sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
      sdesc.MaxAnisotropy = 1;
    }
    else if (params & TEXT_BASIC_PARAMS::LINEAR_FILTER) {
      sdesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
      sdesc.MaxAnisotropy = 1;
    }

    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    if (params & TEXT_BASIC_PARAMS::CLAMP_TO_EDGE) {
      sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    }

    if (params & TEXT_BASIC_PARAMS::TILED) {
      sdesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
      sdesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
      sdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    }

    if (params & TEXT_BASIC_PARAMS::CLAMP_TO_BORDER) {
      sdesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
      sdesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
      sdesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
      sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      sdesc.MaxAnisotropy = 1;
    }

    sdesc.BorderColor[0] = 0.0f;
    sdesc.BorderColor[1] = 0.0f;
    sdesc.BorderColor[2] = 0.0f;
    sdesc.BorderColor[3] = 0.0f;
    sdesc.MinLOD = 0.0f;
    sdesc.MaxLOD = (params & (TEXT_BASIC_PARAMS::NEAREST_FILTER | TEXT_BASIC_PARAMS::LINEAR_FILTER)) ? 0.0f : D3D11_FLOAT32_MAX;
    sdesc.MipLODBias = 0.0f;

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
    else if (cil_props & CIL_HALF_FLOAT)
      desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
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
    int bytesPerPixel = (cil_props & CIL_HALF_FLOAT) ? 8 : 4;
    int bufferSize = this->size / 6;
    if (cil_props & CIL_CUBE_MAP) {
      unsigned char *pHead = buffer;
      for (int i = 0; i < 6; i++) {
        initData[i].pSysMem = pHead;
        initData[i].SysMemPitch = sizeof(unsigned char) * this->x * bytesPerPixel;
        pHead += bufferSize;
      }
    }
    else {
      initData[0].pSysMem = buffer;
      initData[0].SysMemPitch = sizeof(unsigned char) * this->x * ((cil_props & CIL_HALF_FLOAT) ? 8 : m_channels);
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
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());

    DXGI_FORMAT format = DXGI_FORMAT_BC1_UNORM;
    int blockSize = 8;
    if (cil_props & CIL_DXT3) {
      format = DXGI_FORMAT_BC2_UNORM;
      blockSize = 16;
    } else if (cil_props & CIL_DXT5) {
      format = DXGI_FORMAT_BC3_UNORM;
      blockSize = 16;
    }

    int numFaces = (cil_props & CIL_CUBE_MAP) ? 6 : 1;
    int mipCount = (mipmaps > 0) ? mipmaps : 1;

    D3D11_TEXTURE2D_DESC desc = { 0 };
    desc.Width = this->x;
    desc.Height = this->y;
    desc.MipLevels = mipCount;
    desc.ArraySize = numFaces;
    desc.Format = format;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = 0;
    if (cil_props & CIL_CUBE_MAP)
      desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    D3D11_SUBRESOURCE_DATA* initData = new D3D11_SUBRESOURCE_DATA[numFaces * mipCount];
    unsigned char* pData = buffer;

    for (int face = 0; face < numFaces; face++) {
      int w = this->x;
      int h = this->y;
      for (int mip = 0; mip < mipCount; mip++) {
        int wBlocks = (w + 3) / 4;
        int hBlocks = (h + 3) / 4;
        if (wBlocks < 1) wBlocks = 1;
        if (hBlocks < 1) hBlocks = 1;
        int mipSize = wBlocks * hBlocks * blockSize;

        int subresource = face * mipCount + mip;
        initData[subresource].pSysMem = pData;
        initData[subresource].SysMemPitch = wBlocks * blockSize;
        initData[subresource].SysMemSlicePitch = 0;

        pData += mipSize;
        w >>= 1; if (w < 1) w = 1;
        h >>= 1; if (h < 1) h = 1;
      }
    }

    HRESULT hr = device->CreateTexture2D(&desc, initData, Tex.GetAddressOf());
    delete[] initData;

    if (hr != S_OK) {
      this->id = -1;
      return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    if (cil_props & CIL_CUBE_MAP) {
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
      srvDesc.TextureCube.MipLevels = mipCount;
      srvDesc.TextureCube.MostDetailedMip = 0;
    } else {
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = mipCount;
      srvDesc.Texture2D.MostDetailedMip = 0;
    }

    device->CreateShaderResourceView(Tex.Get(), &srvDesc, pSRVTex.GetAddressOf());

    SetTextureParams();
    static int texid = 0;
    this->id = texid;
    texid++;
  }

  void D3DXTexture::DestroyAPITexture() {
    Tex.Reset();
    pSRVTex.Reset();
    pSampler.Reset();
  }

  void D3DXTexture::Set(const DeviceContext & deviceContext, unsigned int slot, std::string shaderTextureName)
  {
    T8_LOG_TRACE("[D3D11] Texture::Set slot=%u name='%s' file='%s'", slot, shaderTextureName.c_str(), filepath.c_str());
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->PSSetShaderResources(slot, 1, pSRVTex.GetAddressOf());
  }

  void D3DXTexture::SetSampler(const DeviceContext & deviceContext, unsigned int slot)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->PSSetSamplers(slot, 1, pSampler.GetAddressOf());
  }

  void D3DXTexture::SetVS(const DeviceContext& deviceContext, unsigned int slot, std::string name)
  {
    auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject());
    ctx->VSSetShaderResources(slot, 1, pSRVTex.GetAddressOf());
  }

  void D3DXTexture::UpdateFloatData(const DeviceContext& deviceContext, int w, int h, const float* data)
  {
    auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject());
    ctx->UpdateSubresource(Tex.Get(), 0, nullptr, data, w * 16, 0);
  }
}