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

#ifndef T800_GLDRIVER_H
#define T800_GLDRIVER_H

#include <Config.h>
#include <video/BaseDriver.h>
#ifdef T850_HEADLESS
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <assert.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#else
#ifdef OS_WINDOWS
#if defined(USING_OPENGL_ES20)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(USING_OPENGL_ES30)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#elif defined(USING_OPENGL_ES31)
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#elif defined(USING_OPENGL)
#include <GL/glew.h>
#include <SDL3/SDL.h>
#else
#include <GL/glew.h>
#include <SDL3/SDL.h>
#endif
#elif defined(OS_LINUX)
#if defined(USING_OPENGL_ES20)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(USING_OPENGL_ES30)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#elif defined(USING_OPENGL_ES31)
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#elif defined(USING_OPENGL)
#include <GL/glew.h>
#else
#include <GL/glew.h>
#endif
#endif
#endif

#include <unordered_map>
#include <vector>

// Include all GL component headers
#include <video/gl/GLDeviceContext.h>
#include <video/gl/GLDevice.h>
#include <video/gl/GLVertexBuffer.h>
#include <video/gl/GLIndexBuffer.h>
#include <video/gl/GLConstantBuffer.h>

namespace t850 {

  class GLDriver : public BaseDriver {
  public:
    GLDriver() { m_currentAPI = GraphicsApi::OPENGL; }
    void	InitDriver();
    void	CreateSurfaces();
    void	DestroySurfaces();
    void	Update();
    void	DestroyDriver();
    void	SetWindow(void *window);
    void  SetWindowHandle(const WindowHandle& handle) override;
    void	SetDimensions(int, int);
    bool  ResizeSwapchain(int newW, int newH) override;
    void SetBlendState(BlendStates state) override;
    void SetDepthStencilState(DepthStencilStates state) override;
    void SaveScreenshot(std::string path) override;
    void SaveRTToFile(int rtID, int attachment, std::string path) override;
	void SetCullFace(FaceCulling state) override;
#ifdef T850_RENDER_TRACE
    void RefreshTracePendingRenderState() override;
#endif

    void	PopRT();

    void	Clear();
    void	ClearWithColor(float r, float g, float b, float a) override;
    void	SwapBuffers();
    bool	CheckExtension(std::string s);
#if defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    EGLDisplay			eglDisplay;
    EGLConfig			eglConfig;
    EGLSurface			eglSurface;
    EGLContext			eglContext;

    EGLNativeWindowType	eglWindow;
#endif
    GLint				CurrentFBO;
#if defined(USING_OPENGL) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    static GLenum		DrawBuffers[16];
#endif
    void*               m_sdlWindow = nullptr;
    std::vector<std::string>	ExtensionsTok;
    std::string					Extensions;

    private:
#if defined(T850_HEADLESS) || defined(USING_OPENGL) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
        void FenceOffscreenTarget(int rt);
        void WaitForOffscreenTargetFence(int rt);
        void DestroyOffscreenFences();
        std::unordered_map<int, GLsync> m_offscreenFences;
#endif

  };
}
#endif
