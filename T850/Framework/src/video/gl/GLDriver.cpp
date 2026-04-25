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

#include <video/gl/GLDriver.h>
#include <video/gl/GLTexture.h>
#include <video/gl/GLRT.h>
#include <video/gl/GLShader.h>
#include <video/gl/GLDevice.h>
#include <video/gl/GLDeviceContext.h>
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <utils/Log.h>



#ifdef OS_WINDOWS
#if defined(USING_OPENGL_ES20)
#pragma comment(lib,"libEGL.lib")
#pragma comment(lib,"libGLESv2.lib")
#elif defined (USING_OPENGL_ES30) || defined (USING_OPENGL_ES31)
#pragma comment(lib,"libEGL.lib")
#pragma comment(lib,"libGLESv2.lib")
#elif defined(USING_OPENGL)
#ifdef _DEBUG
#pragma comment(lib,"libglew32d.lib")
#else
#pragma comment(lib,"libglew32.lib")
#endif
#pragma comment(lib,"OpenGL32.Lib")
#endif
#elif defined(OS_LINUX)
#include <GL/freeglut.h>
#endif
namespace t800 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

#if defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
  void EGLError(const char* c_ptr) {

    EGLint iErr = eglGetError();
    if (iErr != EGL_SUCCESS) {
      T8_LOG_ERROR("EGL CALL: %s Error Code: %d", c_ptr, iErr);
    }

  }
  bool OpenNativeDisplay(EGLNativeDisplayType* nativedisp_out)
  {
    *nativedisp_out = (EGLNativeDisplayType)NULL;
    return true;
  }
#endif
#if defined(USING_OPENGL) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
  GLenum GLDriver::DrawBuffers[16];
#endif
  void	GLDriver::InitDriver() {
    T8Device = new t800::GLDevice;
    T8DeviceContext = new t800::GLDeviceContext;
#ifdef T850_HEADLESS
    T8_LOG_INFO("USING HEADLESS CONTEXT");
    bool res;
    int32_t fd = open ("/dev/dri/renderD128", O_RDWR);
    assert (fd > 0);
 
   struct gbm_device *gbm = gbm_create_device (fd);
   assert (gbm != NULL);
 
   /* setup EGL from the GBM device */
   EGLDisplay egl_dpy = eglGetPlatformDisplay (EGL_PLATFORM_GBM_MESA, gbm, NULL);
   assert (egl_dpy != NULL);
 
   res = eglInitialize (egl_dpy, NULL, NULL);
   assert (res);
 
   const char *egl_extension_st = eglQueryString (egl_dpy, EGL_EXTENSIONS);
   assert (strstr (egl_extension_st, "EGL_KHR_create_context") != NULL);
   assert (strstr (egl_extension_st, "EGL_KHR_surfaceless_context") != NULL);
 
   static const EGLint config_attribs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
      EGL_NONE
   };
   EGLConfig cfg;
   EGLint count;
 
   res = eglChooseConfig (egl_dpy, config_attribs, &cfg, 1, &count);
   assert (res);
 
   res = eglBindAPI (EGL_OPENGL_ES_API);
   assert (res);
 
   static const EGLint attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 3,
      EGL_NONE
   };
   EGLContext core_ctx = eglCreateContext (egl_dpy,
                                           cfg,
                                           EGL_NO_CONTEXT,
                                           attribs);
   assert (core_ctx != EGL_NO_CONTEXT);
 
   res = eglMakeCurrent (egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, core_ctx);
   assert (res);

#else
#if (defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)) && defined(USING_SDL)
    EGLint numConfigs;
    EGLNativeDisplayType nativeDisplay;

    if (!OpenNativeDisplay(&nativeDisplay)) {
      T8_LOG_ERROR("Can't open native display");
    }

    eglDisplay = eglGetDisplay(nativeDisplay);

    EGLError("eglGetDisplay");

    EGLint iMajorVersion, iMinorVersion;

    if (!eglInitialize(eglDisplay, &iMajorVersion, &iMinorVersion)) {
      T8_LOG_ERROR("Failed to initialize EGL");
    }
    else {
      T8_LOG_INFO("EGL version %d.%d", iMajorVersion, iMinorVersion);
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLError("eglBindAPI");

    const EGLint attribs[] = {
      EGL_SURFACE_TYPE,	EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE,	EGL_OPENGL_ES3_BIT_KHR,
      EGL_BLUE_SIZE,		8,
      EGL_GREEN_SIZE,		8,
      EGL_RED_SIZE,		8,
      EGL_DEPTH_SIZE,		24,
      EGL_NONE
    };

    if (!eglChooseConfig(eglDisplay, attribs, &eglConfig, 1, &numConfigs)) {
      T8_LOG_ERROR("Failed to choose EGL config");
    }

    EGLError("eglChooseConfig");

    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, eglWindow, NULL);

    EGLError("eglCreateWindowSurface");

    EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    eglContext = eglCreateContext(eglDisplay, eglConfig, NULL, ai32ContextAttribs);

    EGLError("eglCreateContext");

    if (eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) == EGL_FALSE) {
      T8_LOG_ERROR("Failed to make EGL context current");
      return;
    }

    eglQuerySurface(eglDisplay, eglSurface, EGL_WIDTH, &width);
    eglQuerySurface(eglDisplay, eglSurface, EGL_HEIGHT, &height);
#elif defined(USING_OPENGL)
    GLenum err = glewInit();
    if (GLEW_OK != err) {
      T8_LOG_ERROR("GLEW init error: %s", glewGetErrorString(err));
    }
    else {
      T8_LOG_INFO("GLEW OK");
    }
    SDL_GetWindowSizeInPixels((SDL_Window*)m_sdlWindow, &width, &height);
#endif
#endif//HEADLESS
    std::string GL_Version = std::string((const char*)glGetString(GL_VERSION));
    std::string GL_Extensions = std::string((const char*)glGetString(GL_EXTENSIONS));

    std::istringstream iss(GL_Extensions);
    std::vector<std::string> tokens{ std::istream_iterator<std::string>{iss},
      std::istream_iterator<std::string>{} };

    ExtensionsTok = tokens;
    Extensions = GL_Extensions;

    T8_LOG_INFO("GL Version: %s", GL_Version.c_str());

    for (unsigned int i = 0; i < ExtensionsTok.size(); i++) {
      T8_LOG_VERBOSE("[%s]", ExtensionsTok[i].c_str());
    }

    const unsigned char *version = glGetString(GL_SHADING_LANGUAGE_VERSION);
    T8_LOG_INFO("GLSL Ver: %s", version);

    glEnable(GL_DEPTH_TEST);
    glClearDepthf(1.0f);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &CurrentFBO);

#if defined(USING_OPENGL) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    for (int i = 0; i < 16; i++) {
      GLDriver::DrawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
    }
#endif
  }

  void	GLDriver::CreateSurfaces() {

  }

  void	GLDriver::DestroySurfaces() {

  }

  void	GLDriver::Update() {

  }

  void	GLDriver::DestroyDriver() {
    DestroyShaders();
    DestroyRTs();
    DestroyTextures();
    T8Device->release();
    T8DeviceContext->release();
#if (defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)) && defined(OS_WINDOWS)
    eglDestroySurface(eglDisplay, eglSurface);
    eglDestroyContext(eglDisplay, eglContext);
    eglTerminate(eglDisplay);
#endif
  }

  void	GLDriver::SetWindow(void *window) {
#if (defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)) && defined(OS_WINDOWS)
    eglWindow = GetActiveWindow();
#endif
    m_sdlWindow = window;
  }

  void  GLDriver::SetWindowHandle(const WindowHandle& handle) {
#if (defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)) && defined(OS_WINDOWS)
    if (handle.kind == WindowHandle::WIN32_HWND && handle.nativeHandle) {
      eglWindow = reinterpret_cast<EGLNativeWindowType>(handle.nativeHandle);
    } else {
      eglWindow = GetActiveWindow();
    }
#endif
    if (handle.kind == WindowHandle::SDL_WINDOW) {
      m_sdlWindow = handle.sdlWindow;
    }
  }

  void	GLDriver::SetDimensions(int w, int h) {
    width = w;
    height = h;
  }

  bool GLDriver::ResizeSwapchain(int newW, int newH) {
    if (newW <= 0 || newH <= 0) return false;
    width  = newW;
    height = newH;
    glViewport(0, 0, newW, newH);
    T8_LOG_INFO("[GL] Viewport resized to %dx%d", newW, newH);
    return true;
  }

  void GLDriver::SetBlendState(BLEND_STATES state)
  {
    static const char* names[] = {"BLEND_DEFAULT","BLEND_OPAQUE","ADDITIVE","ALPHA_BLEND","NON_PREMULTIPLIED"};
    T8_LOG_TRACE("[GL] SetBlendState(%s)", (state >= 0 && state <= 4) ? names[state] : "?");
    switch (state)
    {
    case t800::BaseDriver::BLEND_DEFAULT:
      glDisable(GL_BLEND);
      break;
    case t800::BaseDriver::BLEND_OPAQUE:
      glDisable(GL_BLEND);
      break;
    case t800::BaseDriver::ADDITIVE:
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      break;
    case t800::BaseDriver::ALPHA_BLEND:
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case t800::BaseDriver::NON_PREMULTIPLIED:
      glEnable(GL_BLEND);
      break;
    default:
      break;
    }
  }

  void GLDriver::SetDepthStencilState(DEPTH_STENCIL_STATES state)
  {
    static const char* names[] = {"DEPTH_DEFAULT","READ_WRITE","NONE","READ"};
    T8_LOG_TRACE("[GL] SetDepthStencilState(%s)", (state >= 0 && state <= 3) ? names[state] : "?");
    switch (state)
    {
    case t800::BaseDriver::DEPTH_DEFAULT:
      glDepthMask(GL_TRUE);
      glEnable(GL_DEPTH_TEST);
      break;
    case t800::BaseDriver::READ_WRITE:
      glDepthMask(GL_TRUE);
      glEnable(GL_DEPTH_TEST);
      break;
    case t800::BaseDriver::NONE:
      glDepthMask(GL_FALSE);
      glDisable(GL_DEPTH_TEST);
      break;
    case t800::BaseDriver::READ:
     // glDepthMask(GL_FALSE);
      glDisable(GL_DEPTH_TEST);
      break;
    default:
      break;
    }
  }

  static void WritePPM(const std::string& path, int w, int h, const std::vector<unsigned char>& rgbBuf) {
    std::ofstream out(path + ".ppm", std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgbBuf.data()), rgbBuf.size());
  }

  static void ReadFBOToPPM(int w, int h, GLenum readFormat, GLenum readType, const std::string& path) {
    std::vector<unsigned char> rgbBuf(w * h * 3);

    int channels = (readFormat == GL_RGBA) ? 4 : 1;

    if (readType == GL_UNSIGNED_BYTE) {
      std::vector<unsigned char> pixels(w * h * channels);
      glReadPixels(0, 0, w, h, readFormat, GL_UNSIGNED_BYTE, pixels.data());
      for (int y = 0; y < h; y++) {
        int srcRow = (h - 1 - y);
        for (int x = 0; x < w; x++) {
          int dstIdx = (y * w + x) * 3;
          if (channels == 4) {
            int srcIdx = (srcRow * w + x) * 4;
            rgbBuf[dstIdx]     = pixels[srcIdx];
            rgbBuf[dstIdx + 1] = pixels[srcIdx + 1];
            rgbBuf[dstIdx + 2] = pixels[srcIdx + 2];
          } else {
            unsigned char v = pixels[srcRow * w + x];
            rgbBuf[dstIdx] = rgbBuf[dstIdx + 1] = rgbBuf[dstIdx + 2] = v;
          }
        }
      }
    } else {
      std::vector<float> fPixels(w * h * channels);
      glReadPixels(0, 0, w, h, readFormat, GL_FLOAT, fPixels.data());
      for (int y = 0; y < h; y++) {
        int srcRow = (h - 1 - y);
        for (int x = 0; x < w; x++) {
          int dstIdx = (y * w + x) * 3;
          if (channels == 4) {
            int srcIdx = (srcRow * w + x) * 4;
            for (int c = 0; c < 3; c++) {
              float v = fPixels[srcIdx + c];
              v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
              rgbBuf[dstIdx + c] = (unsigned char)(v * 255.f);
            }
          } else {
            float v = fPixels[srcRow * w + x];
            v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            unsigned char b = (unsigned char)(v * 255.f);
            rgbBuf[dstIdx] = rgbBuf[dstIdx + 1] = rgbBuf[dstIdx + 2] = b;
          }
        }
      }
    }

    WritePPM(path, w, h, rgbBuf);
  }

  void GLDriver::SaveScreenshot(std::string path)
  {
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    ReadFBOToPPM(viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, path);
  }

  void GLDriver::SaveRTToFile(int rtID, int attachment, std::string path) {
    if (rtID < 0 || rtID >= (int)RTs.size())
      return;

    BaseRT* rt = RTs[rtID];
    GLRT* glrt = static_cast<GLRT*>(rt);

    GLint prevFBO = 0;
    GLint prevViewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, glrt->vFrameBuffers[0]);
    glViewport(0, 0, rt->w, rt->h);

    if (attachment == DEPTH_ATTACHMENT) {
      std::vector<float> depthPixels(rt->w * rt->h, -1.0f);
      glReadPixels(0, 0, rt->w, rt->h, GL_DEPTH_COMPONENT, GL_FLOAT, depthPixels.data());

      bool hasDepthData = false;
      for (int i = 0; i < rt->w * rt->h; i++) {
        if (depthPixels[i] > 0.0f && depthPixels[i] <= 1.0f) {
          hasDepthData = true;
          break;
        }
      }

      if (!hasDepthData) {
        T8_LOG_INFO("[GLDriver] Depth texture export not supported on ANGLE/GLES3 (shadows work correctly at runtime)");
      }

      std::vector<unsigned char> rgbBuf(rt->w * rt->h * 3);
      for (int y = 0; y < rt->h; y++) {
        int srcRow = (rt->h - 1 - y);
        for (int x = 0; x < rt->w; x++) {
          float v = depthPixels[srcRow * rt->w + x];
          v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
          unsigned char b = (unsigned char)(v * 255.f);
          int dstIdx = (y * rt->w + x) * 3;
          rgbBuf[dstIdx] = rgbBuf[dstIdx + 1] = rgbBuf[dstIdx + 2] = b;
        }
      }
      WritePPM(path, rt->w, rt->h, rgbBuf);
    } else {
      int colorIndex = attachment;
      glReadBuffer(GL_COLOR_ATTACHMENT0 + colorIndex);

      GLenum readFormat = GL_RGBA;
      GLenum readType = GL_UNSIGNED_BYTE;
      switch (rt->color_format) {
        case BaseRT::R8:
          readFormat = GL_RED; readType = GL_UNSIGNED_BYTE; break;
        case BaseRT::F16:
        case BaseRT::F32:
          readFormat = GL_RGBA; readType = GL_FLOAT; break;
        case BaseRT::RGBA16F:
        case BaseRT::RGBA32F:
          readFormat = GL_RGBA; readType = GL_FLOAT; break;
        default:
          readFormat = GL_RGBA; readType = GL_UNSIGNED_BYTE; break;
      }

      ReadFBOToPPM(rt->w, rt->h, readFormat, readType, path);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
  }
  
  void GLDriver::SetCullFace(FACE_CULLING state) {
	  m_FaceCulling = state;
	  switch (m_FaceCulling) {
		  case t800::BaseDriver::FRONT_FACES:
			  glEnable(GL_CULL_FACE);
			  glCullFace(GL_FRONT);
			  break;
		  case t800::BaseDriver::BACK_FACES:
			  glEnable(GL_CULL_FACE);
			  glCullFace(GL_BACK);
			  break;
		  case t800::BaseDriver::FRONT_AND_BACK:
			  glDisable(GL_CULL_FACE);
			  glCullFace(GL_FRONT_AND_BACK);
			  break;
	  }
  }

  void	GLDriver::Clear() {
    glClearColor(1.0, 1.0, 1.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  }

  void	GLDriver::SwapBuffers() {
    T8_LOG_TRACE("[GLDriver] SwapBuffers");
#ifdef OS_WINDOWS
#if defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    eglSwapBuffers(eglDisplay, eglSurface);
#elif defined(USING_OPENGL)
    SDL_GL_SwapWindow((SDL_Window*)m_sdlWindow);
#endif
#elif defined(OS_LINUX)
#ifdef USING_FREEGLUT
    glutSwapBuffers();
#elif defined(USING_WAYLAND_NATIVE)
#endif
#endif

  }

  bool GLDriver::CheckExtension(std::string s) {
    return (Extensions.find(s) != std::string::npos);
  }


  void GLDriver::PopRT() {
    glBindFramebuffer(GL_FRAMEBUFFER, CurrentFBO);
    glViewport(0, 0, width, height);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (CurrentRT >= 0) {
      if (RTs[CurrentRT]->GenMips) {
        glBindTexture(GL_TEXTURE_2D, RTs[CurrentRT]->vColorTextures[0]->id);
        glGenerateMipmap(GL_TEXTURE_2D);
      }
    }

    CurrentRT = -1;
  }


}
