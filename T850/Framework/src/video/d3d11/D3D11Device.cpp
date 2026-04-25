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

#include <video/d3d11/D3D11Device.h>
#include <video/d3d11/D3D11VertexBuffer.h>
#include <video/d3d11/D3D11IndexBuffer.h>
#include <video/d3d11/D3D11ConstantBuffer.h>
#include <video/d3d11/D3D11Shader.h>
#include <video/d3d11/D3D11Texture.h>
#include <video/d3d11/D3D11RT.h>

namespace t850 {
  void * D3DXDevice::GetAPIObject() const
  {
    return (void*)APIDevice.Get();
  }
  void ** D3DXDevice::GetAPIObjectReference() const
  {
    // ComPtr stores the raw pointer as its sole member; GetAddressOf() yields T**
    // suitable for D3D11CreateDeviceAndSwapChain-style out-parameter creation.
    // Safe here because the ComPtr is empty at creation time (no AddRef leak).
    return reinterpret_cast<void**>(const_cast<Microsoft::WRL::ComPtr<ID3D11Device>&>(APIDevice).GetAddressOf());
  }
  void D3DXDevice::release()
  {
    APIDevice.Reset();
  }

  Buffer * D3DXDevice::CreateBuffer(BufferType::E bufferType, BufferDesc desc, void* initialData)
  {
    Buffer* retBuff;
    switch (bufferType)
    {
    case BufferType::VERTEX:
      retBuff = new D3DXVertexBuffer;
      break;
    case BufferType::INDEX:
      retBuff = new D3DXIndexBuffer;
      break;
    case BufferType::CONSTANT:
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

  Texture * D3DXDevice::CreateFloatTexture(int w, int h, const float* data)
  {
    D3DXTexture* tex = new D3DXTexture;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;
    initData.SysMemPitch = w * 16; // 4 floats * 4 bytes = 16 bytes per texel

    HRESULT hr = reinterpret_cast<ID3D11Device*>(GetAPIObject())->CreateTexture2D(
        &desc, data ? &initData : nullptr, tex->Tex.GetAddressOf());
    if (FAILED(hr)) { delete tex; return nullptr; }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = reinterpret_cast<ID3D11Device*>(GetAPIObject())->CreateShaderResourceView(
        tex->Tex.Get(), &srvDesc, tex->pSRVTex.GetAddressOf());
    if (FAILED(hr)) { delete tex; return nullptr; }

    tex->x = w;
    tex->y = h;
    return tex;
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
}
