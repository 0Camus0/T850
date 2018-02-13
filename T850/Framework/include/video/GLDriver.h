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
#include <SDL/SDL.h>
#else
#include <GL/glew.h>
#include <SDL/SDL.h>
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

#include <vector>

namespace t800 {
  /* DEVICES */
  class GLDeviceContext : public DeviceContext {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void release() override;
    void SetPrimitiveTopology(T8_TOPOLOGY::E topology) override;
    void DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex) override;
    int internalIBFormat;
    int internalTopology;
    int internalStride;
    unsigned int internalShaderProgram;
  private:
  };
  class GLDevice : public Device {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void release() override;
    Buffer* CreateBuffer(T8_BUFFER_TYPE::E bufferType, BufferDesc desc, void* initialData = nullptr) override;
    ShaderBase* CreateShader(std::string src_vs, std::string src_fs, unsigned long long sig = T8_NO_SIGNATURE) override;
    Texture* CreateTexture(std::string path) override;
    Texture* CreateTextureFromMemory(const unsigned char *buff, int w, int h, int channels, std::string name) override;
    Texture* CreateCubeMap(const unsigned char * buff, int w, int h) override;
    BaseRT* CreateRT(int nrt, int cf, int df, int w, int h, bool genMips = false) override; // TODO RT Desc
  private:
  };

  /* BUFFERS */
  class GLVertexBuffer : public VertexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    GLVertexBuffer() = default;
    GLVertexBuffer(GLVertexBuffer const& other) = default;
    GLVertexBuffer(GLVertexBuffer&& other) = default;

    void Set(const DeviceContext& deviceContext, const unsigned stride, const unsigned offset) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    unsigned int APIID;
  };
  class GLIndexBuffer : public IndexBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void Set(const DeviceContext& deviceContext, const unsigned offset, T8_IB_FORMAR::E format = T8_IB_FORMAR::R32) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    unsigned int APIID;
  };
  class GLConstantBuffer : public ConstantBuffer {
  public:
    void* GetAPIObject() const override;
    void** GetAPIObjectReference() const override;

    void Set(const DeviceContext& deviceContext) override;
    void UpdateFromSystemCopy(const DeviceContext& deviceContext) override;
    void UpdateFromBuffer(const DeviceContext& deviceContext, const void* buffer) override;
    void release() override;
  private:
    friend Device;
    void Create(const Device& device, BufferDesc desc, void* initialData = nullptr) override;
    unsigned int APIID;
  };



  class GLDriver : public BaseDriver {
  public:
    GLDriver() { m_currentAPI = GRAPHICS_API::OPENGL; }
    void	InitDriver();
    void	CreateSurfaces();
    void	DestroySurfaces();
    void	Update();
    void	DestroyDriver();
    void	SetWindow(void *window);
    void	SetDimensions(int, int);
    void SetBlendState(BLEND_STATES state) override;
    void SetDepthStencilState(DEPTH_STENCIL_STATES state) override;
    void SaveScreenshot(std::string path) override;
	void SetCullFace(FACE_CULLING state) override;

    void	PopRT();

    void	Clear();
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
    std::vector<std::string>	ExtensionsTok;
    std::string					Extensions;

  };
}
#endif
