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

#include <video/GLTexture.h>

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
#include <SDL/SDL.h>
#endif

#include "video/GLShader.h"


namespace t800 {
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

    //if(params & TEXT_BASIC_PARAMS::MIPMAPS)
    glFiltering = GL_LINEAR_MIPMAP_LINEAR; //GL_TEXTURE_MAX_ANISOTROPY_EXT

    if (params & TEXT_BASIC_PARAMS::CLAMP_TO_EDGE)
      glWrap = GL_CLAMP_TO_EDGE;

    if (params & TEXT_BASIC_PARAMS::TILED)
      glWrap = GL_REPEAT;

    glTexParameteri(glTarget, GL_TEXTURE_MIN_FILTER, glFiltering);
    glTexParameteri(glTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(glTarget, GL_TEXTURE_WRAP_S, glWrap);
    glTexParameteri(glTarget, GL_TEXTURE_WRAP_T, glWrap);

    int Max = 1;
    glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &Max);
    glTexParameteri(glTarget, GL_TEXTURE_MAX_ANISOTROPY_EXT, Max);

    glBindTexture(glTarget, 0);
  }

  void GLTexture::GetFormatBpp(unsigned int &props, unsigned int &glFormat, unsigned int &bpp) {

  }

  void GLTexture::LoadAPITexture(DeviceContext* context, unsigned char* buffer) {
    unsigned int glFormat = 0;
    unsigned int glChannel = GL_UNSIGNED_BYTE;

    if (cil_props & CIL_CUBE_MAP)
      glTarget = GL_TEXTURE_CUBE_MAP;
    else
      glTarget = GL_TEXTURE_2D;

    if (this->props&TEXT_BASIC_FORMAT::CH_ALPHA)
      glFormat = GL_ALPHA;
    else if (this->props&TEXT_BASIC_FORMAT::CH_RGB)
      glFormat = GL_RGB;
    else if (this->props&TEXT_BASIC_FORMAT::CH_RGBA)
      glFormat = GL_RGBA;

    glGenTextures(1, &id);
    glBindTexture(glTarget, id);

    if (this->x % 4 != 0)
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    else
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (cil_props & CIL_CUBE_MAP) {
      int bufferSize = this->size / 6;
      unsigned char *pHead = buffer;
      for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, glFormat, this->x, this->y, 0, glFormat, glChannel, (void*)(pHead));
        if (buffer)
          pHead += bufferSize;
      }
    }
    else {
      glTexImage2D(glTarget, 0, glFormat, this->x, this->y, 0, glFormat, glChannel, (void*)(buffer));
    }

    glGenerateMipmap(glTarget);

    SetTextureParams();
  }

  void GLTexture::LoadAPITextureCompressed(unsigned char* buffer) {

  }

  void GLTexture::DestroyAPITexture() {
    glDeleteTextures(1, &id);
  }

  void GLTexture::Set(const DeviceContext & deviceContext, unsigned int slot, std::string name)
  {
    m_shaderTextureName = name;
    int slot_active = GL_TEXTURE0 + slot;
    int deb = reinterpret_cast<GLShader*>(deviceContext.actualShaderSet)->ShaderProg;
    APITextureLoc = glGetUniformLocation(reinterpret_cast<GLShader*>(deviceContext.actualShaderSet)->ShaderProg, m_shaderTextureName.c_str());
    if (APITextureLoc != -1) {
      glActiveTexture(slot_active);
      glBindTexture(glTarget, id);
      glUniform1i(APITextureLoc, slot);
    }
  }

  void GLTexture::SetSampler(const DeviceContext & deviceContext)
  {
  }

}
