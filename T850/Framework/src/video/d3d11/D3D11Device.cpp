#include "pch.h"
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

namespace t800 {
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
}
