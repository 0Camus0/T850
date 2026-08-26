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
#include <debug/RenderTrace.h>

namespace t850 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  void	D3DXTexture::SetTextureParams() {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    D3D11_SAMPLER_DESC sdesc = {};

    sdesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sdesc.MaxAnisotropy = 16;

    if ((cil_props & CIL_CUBE_MAP) && !(params & (TextBasicParams::NEAREST_FILTER | TextBasicParams::LINEAR_FILTER | TextBasicParams::CLAMP_TO_BORDER))) {
      sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      sdesc.MaxAnisotropy = 1;
    }

    if (params & TextBasicParams::NEAREST_FILTER) {
      sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
      sdesc.MaxAnisotropy = 1;
    }
    else if (params & TextBasicParams::LINEAR_FILTER) {
      sdesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
      sdesc.MaxAnisotropy = 1;
    }

    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    if (params & TextBasicParams::CLAMP_TO_EDGE) {
      sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    }

    if (params & TextBasicParams::TILED) {
      sdesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
      sdesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
      sdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    }

    if (params & TextBasicParams::CLAMP_TO_BORDER) {
      sdesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
      sdesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
      sdesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
      sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      sdesc.MaxAnisotropy = 1;
    }

    const float border = (params & TextBasicParams::CLAMP_TO_BORDER) ? 1.0f : 0.0f;
    sdesc.BorderColor[0] = border;
    sdesc.BorderColor[1] = border;
    sdesc.BorderColor[2] = border;
    sdesc.BorderColor[3] = border;
    sdesc.MinLOD = 0.0f;
    sdesc.MaxLOD = (params & (TextBasicParams::NEAREST_FILTER | TextBasicParams::LINEAR_FILTER)) ? 0.0f : D3D11_FLOAT32_MAX;
    sdesc.MipLODBias = 0.0f;
    sdesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    HRESULT hr = device->CreateSamplerState(&sdesc, pSampler.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
      T8_LOG_ERROR("[D3D11] CreateSamplerState failed hr=0x%08X texture='%s'", hr, filepath.c_str());
    }

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

    if (this->props&TextBasicFormat::CH_ALPHA)
      desc.Format = DXGI_FORMAT_R8_UNORM;
    else if (cil_props & CIL_HALF_FLOAT)
      desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    else
      desc.Format = (this->srgb) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

    const bool isCube = (cil_props & CIL_CUBE_MAP) != 0;
    const int mipCount = (mipmaps > 0) ? mipmaps : 1;
    const bool hasSourceMips = mipCount > 1 && buffer != nullptr && this->size > 0;
    const int bytesPerPixel = (cil_props & CIL_HALF_FLOAT) ? 8 : ((this->props & TextBasicFormat::CH_ALPHA) ? 1 : ((this->props & TextBasicFormat::CH_RGB) ? 3 : 4));

    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (hasSourceMips ? 0 : D3D11_BIND_RENDER_TARGET);

    desc.MiscFlags = 0;
    if (isCube) {
      desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    }
    desc.MipLevels = hasSourceMips ? mipCount : 0;
    if (!hasSourceMips)
      desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;

    HRESULT hr;
    std::vector<D3D11_SUBRESOURCE_DATA> sourceData;
    if (hasSourceMips) {
      sourceData.resize(desc.ArraySize * mipCount);
      unsigned char* pData = buffer;
      for (UINT face = 0; face < desc.ArraySize; ++face) {
        int mipWidth = this->x;
        int mipHeight = this->y;
        for (int mip = 0; mip < mipCount; ++mip) {
          UINT subresource = D3D11CalcSubresource(mip, face, mipCount);
          sourceData[subresource].pSysMem = pData;
          sourceData[subresource].SysMemPitch = mipWidth * bytesPerPixel;
          sourceData[subresource].SysMemSlicePitch = 0;
          pData += mipWidth * mipHeight * bytesPerPixel;
          mipWidth >>= 1; if (mipWidth < 1) mipWidth = 1;
          mipHeight >>= 1; if (mipHeight < 1) mipHeight = 1;
        }
      }
    }

    hr = device->CreateTexture2D(&desc, hasSourceMips ? sourceData.data() : nullptr, Tex.GetAddressOf());

    if (hr != S_OK) {
      this->id = -1;
      return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    if (isCube) {
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
      srvDesc.TextureCube.MipLevels = hasSourceMips ? mipCount : -1;
      srvDesc.TextureCube.MostDetailedMip = 0;
    }
    else {
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = hasSourceMips ? mipCount : -1;
      srvDesc.Texture2D.MostDetailedMip = 0;
    }

    device->CreateShaderResourceView(Tex.Get(), &srvDesc, pSRVTex.GetAddressOf());

    D3D11_TEXTURE2D_DESC pDesc;
    Tex->GetDesc(&pDesc);
    int MipMapCount = pDesc.MipLevels;
    this->mipmaps = MipMapCount;
    if (!hasSourceMips) {
      D3D11_SUBRESOURCE_DATA initData[6];
      int baseBytesPerPixel = (cil_props & CIL_HALF_FLOAT) ? 8 : 4;
      int bufferSize = isCube ? this->size / 6 : 0;
      if (isCube) {
        unsigned char *pHead = buffer;
        for (int i = 0; i < 6; i++) {
          initData[i].pSysMem = pHead;
          initData[i].SysMemPitch = sizeof(unsigned char) * this->x * baseBytesPerPixel;
          pHead += bufferSize;
        }
      }
      else {
        initData[0].pSysMem = buffer;
        initData[0].SysMemPitch = sizeof(unsigned char) * this->x * ((cil_props & CIL_HALF_FLOAT) ? 8 : m_channels);
      }
      if (isCube) {
        for (int i = 0; i < 6; i++) {
          deviceContext->UpdateSubresource(Tex.Get(), D3D11CalcSubresource(0, i, MipMapCount), 0, initData[i].pSysMem, initData[i].SysMemPitch, 0);
        }
      }
      else {
        deviceContext->UpdateSubresource(Tex.Get(), 0, 0, buffer, initData[0].SysMemPitch, 0);
      }
      deviceContext->GenerateMips(pSRVTex.Get());
    }

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
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int texId = g_renderTracer->LookupTextureId(this);
      // D3D11 binds are synchronous and the per-texture sampler is created
      // from the same TextBasicParams bits as the other backends — register
      // a logical sampler signature so cross-API trace diffs surface real
      // mismatches rather than backend-specific descriptor handles.
      int sampId = g_renderTracer->RegisterSampler(
        RenderTracer::MakeSamplerSigD3D11(params, (cil_props & CIL_CUBE_MAP) != 0));
      g_renderTracer->EvBindTextureRequest(slot, texId, shaderTextureName, "ps");
      g_renderTracer->EvBindTextureCommit(slot, texId, /*viewId=*/-1, sampId, shaderTextureName, "ps");
    }
#endif
  }

  void D3DXTexture::SetSampler(const DeviceContext & deviceContext, unsigned int slot)
  {
    reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject())->PSSetSamplers(slot, 1, pSampler.GetAddressOf());
  }

  void D3DXTexture::SetVS(const DeviceContext& deviceContext, unsigned int slot, std::string name)
  {
    auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject());
    ctx->VSSetShaderResources(slot, 1, pSRVTex.GetAddressOf());
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int texId = g_renderTracer->LookupTextureId(this);
      int sampId = g_renderTracer->RegisterSampler(
        RenderTracer::MakeSamplerSigD3D11(params, (cil_props & CIL_CUBE_MAP) != 0));
      g_renderTracer->EvBindTextureRequest(slot, texId, name, "vs");
      g_renderTracer->EvBindTextureCommit(slot, texId, /*viewId=*/-1, sampId, name, "vs");
    }
#endif
  }

  void D3DXTexture::UpdateFloatData(const DeviceContext& deviceContext, int w, int h, const float* data)
  {
    auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(deviceContext.GetAPIObject());
    ctx->UpdateSubresource(Tex.Get(), 0, nullptr, data, w * 16, 0);
  }
}
