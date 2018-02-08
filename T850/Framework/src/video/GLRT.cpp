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

#include <video/GLRT.h>
#include <video/GLTexture.h>
#include <video/GLDriver.h>
#include <utils/Utils.h>

#if defined(OS_LINUX)
#include <sys/time.h>
#endif
namespace t800 {
  bool GLRT::LoadAPIRT() {
    GLint cfmt, dfmt, cinternal;
    GLint bysize = 0;

    switch (this->color_format) {
    case  BaseRT::NOTHING: {
      number_RT = 0;
      cfmt = GL_RGB;
      cinternal = GL_RGBA;
      bysize = GL_UNSIGNED_BYTE;
    }break;
    case BaseRT::R8:
      cfmt = GL_RED;
      cinternal = GL_RED;
      bysize = GL_UNSIGNED_BYTE;
      break;
    case BaseRT::F16:
      cfmt = GL_R16F;
      cinternal = GL_RED;
      bysize = GL_HALF_FLOAT;
      break;
    case BaseRT::F32:
      cfmt = GL_R32F;
      cinternal = GL_RED;
      bysize = GL_FLOAT;
      break;
    case RGB8:
    case RGBA8: {
      cfmt = GL_RGBA;
      cinternal = GL_RGBA;
      bysize = GL_UNSIGNED_BYTE;
    }break;
    case RGBA16F: {
#if (GL_DRIVER_SELECTED == OGLES20)
      cfmt = GL_RGB16F_EXT;
#else
      cfmt = GL_RGBA16F;
#endif

      cinternal = GL_RGBA;
      bysize = GL_HALF_FLOAT;
    }break;
    case RGBA32F: {
#if (GL_DRIVER_SELECTED == OGLES20)
      cfmt = GL_RGB32F_EXT;
#else
      cfmt = GL_RGBA32F;
#endif
      cinternal = GL_RGBA;
      bysize = GL_FLOAT;
    }break;
    case BGR8: {
    }break;
    case BGRA8: {
    }break;
    case BGRA32: {
    }break;
    }

    dfmt = GL_DEPTH_COMPONENT;


    GLuint fbo;
#if defined(OS_LINUX)
    timeval start;
    gettimeofday(&start, 0);
#endif
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);


#if defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    int	Attachments[8];
    Attachments[0] = GL_COLOR_ATTACHMENT0;
    for (int i = 1; i < 8; i++) {
      Attachments[i] = GL_COLOR_ATTACHMENT1 + (i - 1);
    }
#endif
    GLuint ctex;
    GLint cffmt = cfmt;
    GLint cbysize = bysize;
    if (this->color_format == CUBE_F32) {
      glGenTextures(1, &ctex);
      glBindTexture(GL_TEXTURE_CUBE_MAP, ctex);
      for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, cffmt, w, h, 0, cinternal, cbysize, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, Attachments[i], GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, ctex, 0);
      }
        GLTexture *pTextureColor = new GLTexture;
        pTextureColor->x = w;
        pTextureColor->y = h;
        pTextureColor->id = ctex;
        vColorTextures.push_back(pTextureColor);
        vFrameBuffers.push_back(fbo);
        vGLColorTex.push_back(ctex);
    }
    else {
      for (int i = 0; i < number_RT; i++) {
        glGenTextures(1, &ctex);
        glBindTexture(GL_TEXTURE_2D, ctex);
        glTexImage2D(GL_TEXTURE_2D, 0, cffmt, w, h, 0, cinternal, cbysize, 0);
        if (i == 0 && this->color_format != BaseRT::R8) {
          glGenerateMipmap(GL_TEXTURE_2D);
          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        else {
          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        GLTexture *pTextureColor = new GLTexture;
#if defined(USING_OPENGL_ES20)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctex, 0);

        for (int i = 0; i < number_RT; i++) {
          pTextureColor->id = ctex;
          vColorTextures.push_back(pTextureColor);
          vFrameBuffers.push_back(fbo);
          vGLColorTex.push_back(ctex);
      }
        break;
#elif  defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
        glFramebufferTexture2D(GL_FRAMEBUFFER, Attachments[i], GL_TEXTURE_2D, ctex, 0);
#else
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, ctex, 0);
#endif
        pTextureColor->x = w;
        pTextureColor->y = h;
        pTextureColor->id = ctex;
        vColorTextures.push_back(pTextureColor);
        vFrameBuffers.push_back(fbo);
        vGLColorTex.push_back(ctex);
      }
    }

    //GLuint dtex;
    if (number_RT == 0)
      vFrameBuffers.push_back(fbo);
    if (this->depth_format == CUBE_F32) {
      GLuint dtex2;
      glGenTextures(1, &dtex2);
      glBindTexture(GL_TEXTURE_CUBE_MAP, dtex2);
      for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
      }
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X, dtex2, 0);
      GLTexture *pTextureDepth = new GLTexture;
      pTextureDepth->x = w;
      pTextureDepth->y = h;
      pTextureDepth->id = dtex2;
      pTextureDepth->glTarget = GL_TEXTURE_CUBE_MAP;
      this->pDepthTexture = (pTextureDepth);
      DepthTexture = dtex2;
    }
    else {
      GLuint dtex;
      glGenTextures(1, &dtex);
      glBindTexture(GL_TEXTURE_2D, dtex);
#ifdef USING_OPENGL_ES20
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
#else
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
#endif
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      if (number_RT == 0) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
      }

      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dtex, 0);
      GLTexture *pTextureDepth = new GLTexture;
      pTextureDepth->x = w;
      pTextureDepth->y = h;
      pTextureDepth->id = dtex;
      this->pDepthTexture = (pTextureDepth);
      DepthTexture = dtex;
    }

#if defined(OS_LINUX)
    timeval actual;
    gettimeofday(&actual, 0);
    double ttaken = double((actual.tv_sec - start.tv_sec) + (actual.tv_usec - start.tv_usec) / 1000000.0);

    static int sample = 0;
    static double avg = 0.0;

    avg += ttaken;
    sample++;

    if (sample > 50) {
      avg /= static_cast<double>(sample);
      printf("Average Time taken for FBO creation: %f \n", avg);
      sample = 0;
      avg = 0.0;
    }

#endif


    return true;
  }

  void GLRT::DestroyAPIRT() {
    GLuint FBO = vFrameBuffers[0];
    glDeleteFramebuffers(1, &FBO);
    for (int i = 0; i < vColorTextures.size(); i++) {
      vColorTextures[i]->release();
    }
    pDepthTexture->release();
  }
  void GLRT::Set(const DeviceContext&context)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, vFrameBuffers[0]);

#if defined(USING_OPENGL) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    if (number_RT != 0) {
      glDrawBuffers(number_RT, GLDriver::DrawBuffers);
    }
    else {
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    }
#endif
    glViewport(0, 0,w, h);
	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  }
  void GLRT::ChangeCubeDepthTexture(int i)
  {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, pDepthTexture->id, 0);
  }
}
