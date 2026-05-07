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

#include <video/gl/GLRT.h>
#include <video/gl/GLTexture.h>
#include <video/gl/GLDriver.h>
#include <utils/Utils.h>
#include <debug/RenderTrace.h>

#if defined(OS_LINUX)
#include <sys/time.h>
#endif
namespace t850 {
  namespace {
    void ResolveGLRTColorFormat(int format, GLint& internalFormat, GLint& dataFormat, GLint& dataType) {
      switch (format) {
      case BaseRT::R8:
        internalFormat = GL_R8;
        dataFormat = GL_RED;
        dataType = GL_UNSIGNED_BYTE;
        break;
      case BaseRT::F16:
        internalFormat = GL_R16F;
        dataFormat = GL_RED;
        dataType = GL_HALF_FLOAT;
        break;
      case BaseRT::F32:
        internalFormat = GL_R32F;
        dataFormat = GL_RED;
        dataType = GL_FLOAT;
        break;
      case BaseRT::RGBA16F:
#if (GL_DRIVER_SELECTED == OGLES20)
        internalFormat = GL_RGB16F_EXT;
#else
        internalFormat = GL_RGBA16F;
#endif
        dataFormat = GL_RGBA;
        dataType = GL_HALF_FLOAT;
        break;
      case BaseRT::RGBA32F:
#if (GL_DRIVER_SELECTED == OGLES20)
        internalFormat = GL_RGB32F_EXT;
#else
        internalFormat = GL_RGBA32F;
#endif
        dataFormat = GL_RGBA;
        dataType = GL_FLOAT;
        break;
      case BaseRT::RGB8:
      case BaseRT::RGBA8:
      case BaseRT::NOTHING:
      default:
        internalFormat = GL_RGBA;
        dataFormat = GL_RGBA;
        dataType = GL_UNSIGNED_BYTE;
        break;
      }
    }
  }

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
      cfmt = GL_R8;
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
      GLenum CubeAttachments[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
                                    GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5 };
      glGenTextures(1, &ctex);
      glBindTexture(GL_TEXTURE_CUBE_MAP, ctex);
      for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, cffmt, w, h, 0, cinternal, cbysize, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, CubeAttachments[i], GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, ctex, 0);
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
        GLint attachmentFormat = cffmt;
        GLint attachmentDataFormat = cinternal;
        GLint attachmentType = cbysize;
        if (!perColorFormats.empty() && i < (int)perColorFormats.size())
          ResolveGLRTColorFormat(perColorFormats[i], attachmentFormat, attachmentDataFormat, attachmentType);

        glGenTextures(1, &ctex);
        glBindTexture(GL_TEXTURE_2D, ctex);
        glTexImage2D(GL_TEXTURE_2D, 0, attachmentFormat, w, h, 0, attachmentDataFormat, attachmentType, 0);
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
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812D); // GL_CLAMP_TO_BORDER
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812D); // GL_CLAMP_TO_BORDER
      // Reverse-Z convention: depth=1.0 is at the light (near), depth=0.0 is far.
      // Border samples must read as "in shadow" (no occluder seen from light) so PCF taps
      // outside the shadow map don't bleed light. With GL_GEQUAL, comparison is
      // (LightPos.z + bias) >= textureValue. Using border=1.0 makes that test fail for
      // any in-frustum sample (LightPos.z < 1.0), preventing shadow-edge light bleed.
      float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
      glTexParameterfv(GL_TEXTURE_2D, 0x1004, borderColor); // GL_TEXTURE_BORDER_COLOR

      if (number_RT == 0) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL);
      } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
      }

      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dtex, 0);
      GLTexture *pTextureDepth = new GLTexture;
      pTextureDepth->x = w;
      pTextureDepth->y = h;
      pTextureDepth->id = dtex;
      this->pDepthTexture = (pTextureDepth);
      DepthTexture = dtex;
    }

    // Zero-initialize all attachments so cross-API behavior is deterministic.
    // glTexImage2D(..., NULL) leaves content undefined per OpenGL spec; some
    // drivers fill with zero, others don't, which causes RTs that get sampled
    // before they're first written (e.g. AdaptedLumPrev on frame 0 of the
    // tone-mapping ping-pong) to feed garbage into the pipeline and diverge
    // from D3D11/D3D12 (which zero-init by spec/driver) and Vulkan (which
    // explicitly clears at creation in VulkanRT::LoadAPIRT).
    if (number_RT > 0 || this->depth_format != BaseRT::NOTHING) {
      glBindFramebuffer(GL_FRAMEBUFFER, vFrameBuffers[0]);
#if defined(USING_OPENGL) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
      if (number_RT > 0) {
        glDrawBuffers(number_RT, GLDriver::DrawBuffers);
      }
#endif
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
      // Reverse-Z: cleared depth = 0.0 matches the engine's depth convention.
      glClearDepthf(0.0f);
      GLbitfield clearMask = 0;
      if (number_RT > 0) clearMask |= GL_COLOR_BUFFER_BIT;
      if (this->depth_format != BaseRT::NOTHING) clearMask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
      GLboolean previousDepthMask = GL_TRUE;
      if (clearMask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
        glDepthMask(GL_TRUE);
      }
      if (clearMask) glClear(clearMask);
      if (clearMask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        glDepthMask(previousDepthMask);
      }
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    if (!vFrameBuffers.empty()) {
      GLuint FBO = vFrameBuffers[0];
      glDeleteFramebuffers(1, &FBO);
    }
    for (size_t i = 0; i < vColorTextures.size(); i++) {
      if (vColorTextures[i])
        vColorTextures[i]->release();
    }
    vColorTextures.clear();
    vFrameBuffers.clear();
    vGLColorTex.clear();
    if (pDepthTexture) {
      pDepthTexture->release();
      pDepthTexture = nullptr;
    }
    DepthTexture = 0;
  }
  void GLRT::Set(const DeviceContext&context)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, vFrameBuffers[0]);

#if defined(USING_OPENGL) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    if (number_RT != 0) {
      glDrawBuffers(number_RT, GLDriver::DrawBuffers);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    else {
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    }
#endif
    glViewport(0, 0,w, h);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    GLboolean previousDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glDepthMask(previousDepthMask);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int rtId = g_renderTracer->LookupRTId(this);
      uint32_t flags = (number_RT > 0 ? 1u : 0u) | 2u | 4u;
      g_renderTracer->EvClearRT(rtId, flags, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0);
    }
#endif
  }

  void GLRT::SetLoad(const DeviceContext&context)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, vFrameBuffers[0]);

#if defined(USING_OPENGL) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    if (number_RT != 0) {
      glDrawBuffers(number_RT, GLDriver::DrawBuffers);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    else {
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    }
#endif
    glViewport(0, 0,w, h);
  }

  void GLRT::ChangeCubeDepthTexture(int i)
  {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, pDepthTexture->id, 0);
  }
}
