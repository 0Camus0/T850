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

#include <video/gl/GLTexture.h>
#include <utils/Log.h>
#include <debug/RenderTrace.h>

#if defined(USING_OPENGL_ES20)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(USING_OPENGL_ES30)
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#elif defined(USING_OPENGL_ES31)
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#elif defined(USING_OPENGL)
#include <GL/glew.h>
#else
#include <GL/glew.h>
#include <SDL3/SDL.h>
#endif

#include <video/gl/GLShader.h>


namespace t850 {
  GLTexture::GLTexture() : glTarget(GL_TEXTURE_2D)
  {
  }

  void	GLTexture::SetTextureParams() {
    if (cil_props & CIL_CUBE_MAP)
      glTarget = GL_TEXTURE_CUBE_MAP;
    else
      glTarget = GL_TEXTURE_2D;

    glBindTexture(glTarget, id);

    unsigned int glFiltering = 0;
    unsigned int glWrap = 0;

    glFiltering = GL_LINEAR_MIPMAP_LINEAR;
    glWrap = GL_CLAMP_TO_EDGE;

    //if(params & TextBasicParams::MIPMAPS)
    glFiltering = GL_LINEAR_MIPMAP_LINEAR; //GL_TEXTURE_MAX_ANISOTROPY_EXT

    if (params & TextBasicParams::CLAMP_TO_EDGE)
      glWrap = GL_CLAMP_TO_EDGE;

    if (params & TextBasicParams::TILED)
      glWrap = GL_REPEAT;

    if (params & TextBasicParams::CLAMP_TO_BORDER) {
      glWrap = 0x812D; // GL_CLAMP_TO_BORDER
      float borderColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
      glTexParameterfv(glTarget, 0x1004, borderColor); // GL_TEXTURE_BORDER_COLOR
    }

    if (params & TextBasicParams::NEAREST_FILTER) {
      glFiltering = GL_NEAREST;
      glTexParameteri(glTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(glTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(glTarget, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(glTarget, GL_TEXTURE_MAX_LEVEL, 0);
    } else {
      glTexParameteri(glTarget, GL_TEXTURE_MIN_FILTER, glFiltering);
      glTexParameteri(glTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    if (mipmaps > 1)
      glTexParameteri(glTarget, GL_TEXTURE_MAX_LEVEL, mipmaps - 1);
    glTexParameteri(glTarget, GL_TEXTURE_WRAP_S, glWrap);
    glTexParameteri(glTarget, GL_TEXTURE_WRAP_T, glWrap);

    if (!(params & TextBasicParams::NEAREST_FILTER)) {
      int Max = 1;
      glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &Max);
      glTexParameteri(glTarget, GL_TEXTURE_MAX_ANISOTROPY_EXT, Max);
    }

    glBindTexture(glTarget, 0);
  }

  void GLTexture::GetFormatBpp(unsigned int &props, unsigned int &glFormat, unsigned int &bpp) {

  }

  void GLTexture::LoadAPITexture(DeviceContext* context, unsigned char* buffer) {
    unsigned int glFormat = 0;
    unsigned int glInternalFormat = 0;
    unsigned int glChannel = GL_UNSIGNED_BYTE;

    if (cil_props & CIL_CUBE_MAP)
      glTarget = GL_TEXTURE_CUBE_MAP;
    else
      glTarget = GL_TEXTURE_2D;

    if (cil_props & CIL_HALF_FLOAT) {
      glFormat = GL_RGBA;
      glInternalFormat = GL_RGBA16F;
      glChannel = GL_HALF_FLOAT;
    } else if (this->props&TextBasicFormat::CH_ALPHA) {
      glFormat = GL_ALPHA;
      glInternalFormat = GL_ALPHA;
    } else if (this->props&TextBasicFormat::CH_RGB) {
      glFormat = GL_RGB;
      glInternalFormat = GL_RGB;
    } else if (this->props&TextBasicFormat::CH_RGBA) {
      glFormat = GL_RGBA;
      glInternalFormat = GL_RGBA;
    }

    glGenTextures(1, &id);
    glBindTexture(glTarget, id);

    if (this->x % 4 != 0)
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    else
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    int numFaces = (cil_props & CIL_CUBE_MAP) ? 6 : 1;
    int mipCount = (mipmaps > 0) ? mipmaps : 1;
    int bytesPerPixel = (cil_props & CIL_HALF_FLOAT) ? 8 : ((this->props & TextBasicFormat::CH_ALPHA) ? 1 : ((this->props & TextBasicFormat::CH_RGB) ? 3 : 4));
    unsigned char* pHead = buffer;

    for (int face = 0; face < numFaces; ++face) {
      int mipWidth = this->x;
      int mipHeight = this->y;
      for (int mip = 0; mip < mipCount; ++mip) {
        unsigned int target = (cil_props & CIL_CUBE_MAP) ? (GL_TEXTURE_CUBE_MAP_POSITIVE_X + face) : GL_TEXTURE_2D;
        glTexImage2D(target, mip, glInternalFormat, mipWidth, mipHeight, 0, glFormat, glChannel, (void*)(pHead));
        if (pHead)
          pHead += mipWidth * mipHeight * bytesPerPixel;
        mipWidth >>= 1; if (mipWidth < 1) mipWidth = 1;
        mipHeight >>= 1; if (mipHeight < 1) mipHeight = 1;
      }
    }

    if (mipCount <= 1)
      glGenerateMipmap(glTarget);
    else {
      this->mipmaps = mipCount;
      params |= TextBasicParams::MIPMAPS;
    }

    SetTextureParams();
  }

  void GLTexture::LoadAPITextureCompressed(unsigned char* buffer) {
    unsigned int glFormat = CIL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    int blockSize = 8;
    if (cil_props & CIL_DXT3) {
      glFormat = CIL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
      blockSize = 16;
    } else if (cil_props & CIL_DXT5) {
      glFormat = CIL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
      blockSize = 16;
    }

    int numFaces = (cil_props & CIL_CUBE_MAP) ? 6 : 1;
    int mipCount = (mipmaps > 0) ? mipmaps : 1;

    if (cil_props & CIL_CUBE_MAP)
      glTarget = GL_TEXTURE_CUBE_MAP;
    else
      glTarget = GL_TEXTURE_2D;

    glGenTextures(1, &id);
    glBindTexture(glTarget, id);

    unsigned char* pData = buffer;

    for (int face = 0; face < numFaces; face++) {
      unsigned int target = (cil_props & CIL_CUBE_MAP)
        ? (GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
        : GL_TEXTURE_2D;
      int w = this->x;
      int h = this->y;
      for (int mip = 0; mip < mipCount; mip++) {
        int wBlocks = (w + 3) / 4;
        int hBlocks = (h + 3) / 4;
        if (wBlocks < 1) wBlocks = 1;
        if (hBlocks < 1) hBlocks = 1;
        int mipSize = wBlocks * hBlocks * blockSize;

        glCompressedTexImage2D(target, mip, glFormat, w, h, 0, mipSize, pData);

        pData += mipSize;
        w >>= 1; if (w < 1) w = 1;
        h >>= 1; if (h < 1) h = 1;
      }
    }

    SetTextureParams();
  }

  void GLTexture::DestroyAPITexture() {
    glDeleteTextures(1, &id);
  }

  void GLTexture::Set(const DeviceContext & deviceContext, unsigned int slot, std::string name)
  {
    T8_LOG_TRACE("[GL] Texture::Set slot=%u name='%s' file='%s'", slot, name.c_str(), filepath.c_str());
    m_shaderTextureName = name;
    int slot_active = GL_TEXTURE0 + slot;
    int deb = reinterpret_cast<GLShader*>(deviceContext.actualShaderSet)->ShaderProg;
    APITextureLoc = glGetUniformLocation(reinterpret_cast<GLShader*>(deviceContext.actualShaderSet)->ShaderProg, m_shaderTextureName.c_str());
    if (APITextureLoc != -1) {
      glActiveTexture(slot_active);
      glBindTexture(glTarget, id);
      glUniform1i(APITextureLoc, slot);
    } else {
      static bool sLogged = false;
      if (!sLogged && name == "u_BoneTex") {
        T8_LOG_ERROR("[GL] Bone texture uniform '%s' NOT FOUND in prog=%d", name.c_str(), deb);
        sLogged = true;
      }
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int texId = g_renderTracer->LookupTextureId(this);
      // Build the sampler signature from the same params bits the actual
      // GL sampler state uses (see SetTextureParams above). All 4 backends
      // build the signature the same way, so equivalent samplers hash to
      // the same id and cross-API trace diffs surface mismatches cleanly.
      int sampId = g_renderTracer->RegisterSampler(
        RenderTracer::MakeSamplerSigGL(params));
      g_renderTracer->EvBindTextureRequest(slot, texId, name, "ps");
      g_renderTracer->EvBindTextureCommit(slot, texId, /*viewId=*/-1, sampId, name, "ps");
    }
#endif
  }

  void GLTexture::SetVS(const DeviceContext & deviceContext, unsigned int slot, std::string name)
  {
    T8_LOG_TRACE("[GL] Texture::SetVS slot=%u name='%s' file='%s'", slot, name.c_str(), filepath.c_str());
    m_shaderTextureName = name;
    int slot_active = GL_TEXTURE0 + slot;
    int prog = reinterpret_cast<GLShader*>(deviceContext.actualShaderSet)->ShaderProg;
    APITextureLoc = glGetUniformLocation(prog, m_shaderTextureName.c_str());
    if (APITextureLoc != -1) {
      glActiveTexture(slot_active);
      glBindTexture(glTarget, id);
      glUniform1i(APITextureLoc, slot);
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int texId = g_renderTracer->LookupTextureId(this);
      int sampId = g_renderTracer->RegisterSampler(
        RenderTracer::MakeSamplerSigGL(params));
      g_renderTracer->EvBindTextureRequest(slot, texId, name, "vs");
      g_renderTracer->EvBindTextureCommit(slot, texId, /*viewId=*/-1, sampId, name, "vs");
    }
#endif
  }

  void GLTexture::SetSampler(const DeviceContext & deviceContext, unsigned int slot)
  {
  }

  void GLTexture::UpdateFloatData(const DeviceContext& deviceContext, int w, int h, const float* data)
  {
    glBindTexture(GL_TEXTURE_2D, id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, data);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

}
