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

#include <video/gl/GLDevice.h>
#include <video/gl/GLVertexBuffer.h>
#include <video/gl/GLIndexBuffer.h>
#include <video/gl/GLConstantBuffer.h>
#include <video/gl/GLTexture.h>
#include <video/gl/GLRT.h>
#include <video/gl/GLShader.h>

namespace t800 {
  void * GLDevice::GetAPIObject() const
  {
    return nullptr;
  }
  void ** GLDevice::GetAPIObjectReference() const
  {
    return nullptr;
  }
  void GLDevice::release()
  {
    delete this;
  }
  Buffer * GLDevice::CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void * initialData)
  {
    Buffer* retBuff;
    switch (bufferType)
    {
    case T8_BUFFER_TYPE::VERTEX:
      retBuff = new GLVertexBuffer;
      break;
    case T8_BUFFER_TYPE::INDEX:
      retBuff = new GLIndexBuffer;
      break;
    case T8_BUFFER_TYPE::CONSTANT:
      retBuff = new GLConstantBuffer;
      break;
    default:
      break;
    }
    retBuff->Create(*this, desc, initialData);
    return retBuff;
  }

  ShaderBase * GLDevice::CreateShader(std::string src_vs, std::string src_fs, ShaderKey key, const std::string& vs_name, const std::string& fs_name)
  {
    ShaderBase *sh = new GLShader();
    if (!sh->CreateShader(src_vs, src_fs, key, vs_name, fs_name)) {
      delete sh;
      return nullptr;
    }
    return sh;
  }

  Texture * GLDevice::CreateTexture(std::string path)
  {
    GLTexture* txture = new GLTexture;
    txture->LoadTexture(path.c_str());
    return txture;
  }

  Texture * GLDevice::CreateTextureFromMemory(const unsigned char * buff, int w, int h, int channels, std::string name)
  {
    GLTexture* txture = new GLTexture;
    txture->LoadFromMemory(buff, w, h, channels);
    return txture;
  }

  Texture * GLDevice::CreateCubeMap(const unsigned char * buff, int w, int h)
  {
    GLTexture* txture = new GLTexture;
    txture->CreateCubeMap(buff, w, h);
    return txture;
  }

  BaseRT * GLDevice::CreateRT(int nrt, int cf, int df, int w, int h, bool genMips)
  {
    BaseRT* rt = new GLRT;
    if (rt->LoadRT(nrt, cf, df, w, h, genMips)) {
      return rt;
    }
    delete rt;
    return nullptr;
  }
}
