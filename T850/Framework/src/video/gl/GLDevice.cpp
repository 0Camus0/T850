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

#include <video/gl/GLDevice.h>
#include <video/gl/GLVertexBuffer.h>
#include <utils/Log.h>
#include <video/gl/GLIndexBuffer.h>
#include <video/gl/GLConstantBuffer.h>
#include <video/gl/GLTexture.h>
#include <video/gl/GLRT.h>
#include <video/gl/GLShader.h>

namespace t850 {
  namespace {
    void ClearPendingGLErrors()
    {
      while (glGetError() != GL_NO_ERROR) {}
    }

    const char* FloatCubeInternalFormatName(GLenum internalFormat)
    {
      switch (internalFormat) {
      case GL_RGBA32F: return "RGBA32F";
      case GL_RGBA16F: return "RGBA16F";
      default: return "UNKNOWN";
      }
    }

    GLenum UploadFloatCubeMapFaces(int size, int mipCount, const float* data, GLenum internalFormat, int& failedFace, int& failedMip)
    {
      failedFace = -1;
      failedMip = -1;
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

      const float* pData = data;
      for (int face = 0; face < 6; ++face) {
        int mipSize = size;
        for (int mip = 0; mip < mipCount; ++mip) {
          glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, internalFormat, mipSize, mipSize, 0, GL_RGBA, GL_FLOAT, pData);
          GLenum err = glGetError();
          if (err != GL_NO_ERROR) {
            failedFace = face;
            failedMip = mip;
            return err;
          }
          if (pData)
            pData += mipSize * mipSize * 4;
          mipSize >>= 1; if (mipSize < 1) mipSize = 1;
        }
      }

      return GL_NO_ERROR;
    }
  }

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
  Buffer * GLDevice::CreateBuffer(BufferType::E bufferType, BufferDesc desc, void * initialData)
  {
    Buffer* retBuff;
    switch (bufferType)
    {
    case BufferType::VERTEX:
      retBuff = new GLVertexBuffer;
      break;
    case BufferType::INDEX:
      retBuff = new GLIndexBuffer;
      break;
    case BufferType::CONSTANT:
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
    if (!txture->LoadTexture(path.c_str())) {
      delete txture;
      return nullptr;
    }
    return txture;
  }

  Texture * GLDevice::CreateTextureFromMemory(const unsigned char * buff, int w, int h, int channels, std::string name)
  {
    GLTexture* txture = new GLTexture;
    txture->LoadFromMemory(buff, w, h, channels, name.c_str());
    return txture;
  }

  Texture * GLDevice::CreateCubeMap(const unsigned char * buff, int w, int h)
  {
    GLTexture* txture = new GLTexture;
    txture->CreateCubeMap(buff, w, h);
    return txture;
  }

  Texture * GLDevice::CreateFloatTexture(int w, int h, const float* data)
  {
    GLTexture* tex = new GLTexture;
    glGenTextures(1, &tex->id);
    glBindTexture(GL_TEXTURE_2D, tex->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, data);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
      T8_LOG_ERROR("[GL] CreateFloatTexture FAILED: glTexImage2D error=0x%X (%dx%d)", err, w, h);
      delete tex;
      return nullptr;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    tex->x = w;
    tex->y = h;
    tex->mipmaps = 1;
    tex->m_channels = 4;
    tex->props = TextBasicFormat::CH_RGBA;
    tex->params = TextBasicParams::CLAMP_TO_EDGE | TextBasicParams::NEAREST_FILTER;
    T8_LOG_INFO("[GL] CreateFloatTexture: id=%u %dx%d RGBA32F", tex->id, w, h);
    return tex;
  }

  Texture * GLDevice::CreateFloatCubeMap(int size, int mipCount, const float* data)
  {
    if (size <= 0 || mipCount <= 0)
      return nullptr;

    const GLenum formats[] = { GL_RGBA32F, GL_RGBA16F };
    GLenum firstError = GL_NO_ERROR;
    int firstFailedFace = -1;
    int firstFailedMip = -1;

    for (GLenum internalFormat : formats) {
      GLTexture* tex = new GLTexture;
      tex->glTarget = GL_TEXTURE_CUBE_MAP;
      tex->x = size;
      tex->y = size;
      tex->mipmaps = mipCount;
      tex->m_channels = 4;
      tex->props = TextBasicFormat::CH_RGBA;
      tex->cil_props = CIL_CUBE_MAP;
      tex->params = TextBasicParams::CLAMP_TO_EDGE | TextBasicParams::MIPMAPS;

      ClearPendingGLErrors();
      glGenTextures(1, &tex->id);
      glBindTexture(GL_TEXTURE_CUBE_MAP, tex->id);

      int failedFace = -1;
      int failedMip = -1;
      GLenum err = UploadFloatCubeMapFaces(size, mipCount, data, internalFormat, failedFace, failedMip);
      if (err == GL_NO_ERROR) {
        tex->SetTextureParams();
        T8_LOG_INFO("[GL] CreateFloatCubeMap: id=%u %dx%d mips=%d %s", tex->id, size, size, mipCount, FloatCubeInternalFormatName(internalFormat));
        return tex;
      }

      if (firstError == GL_NO_ERROR) {
        firstError = err;
        firstFailedFace = failedFace;
        firstFailedMip = failedMip;
      }
      T8_LOG_INFO("[GL] CreateFloatCubeMap %s upload failed: glTexImage2D error=0x%X (%dx%d mips=%d face=%d mip=%d)",
                  FloatCubeInternalFormatName(internalFormat), err, size, size, mipCount, failedFace, failedMip);
      glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
      glDeleteTextures(1, &tex->id);
      delete tex;
    }

    T8_LOG_ERROR("[GL] CreateFloatCubeMap FAILED: glTexImage2D error=0x%X (%dx%d mips=%d face=%d mip=%d)",
                 firstError, size, size, mipCount, firstFailedFace, firstFailedMip);
    return nullptr;
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
