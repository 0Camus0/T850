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

#include <video/GLDriver.h>
#include <video/GLTexture.h>
#include <video/GLRT.h>
#include <video/GLShader.h>
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <fstream>



#ifdef OS_WINDOWS
#if defined(USING_OPENGL_ES20)
#pragma comment(lib,"libEGL.lib")
#pragma comment(lib,"libGLESv2.lib")
#elif defined (USING_OPENGL_ES30) || defined (USING_OPENGL_ES31)
#pragma comment(lib,"libEGL.lib")
#pragma comment(lib,"libGLESv2.lib")
#elif defined(USING_OPENGL)
#pragma comment(lib,"glew.lib")
#pragma comment(lib,"OpenGL32.Lib")
#endif
#elif defined(OS_LINUX)
#include <GL/freeglut.h>
#endif
namespace t800 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;
  void * GLDeviceContext::GetAPIObject() const
  {
    return nullptr;
  }
  void ** GLDeviceContext::GetAPIObjectReference() const
  {
    return nullptr;
  }
  void GLDeviceContext::release()
  {
    delete this;
  }
  void GLDeviceContext::SetPrimitiveTopology(T8_TOPOLOGY::E topology)
  {
    switch (topology)
    {
    case T8_TOPOLOGY::POINT_LIST:
      internalTopology = GL_POINTS;
      break;
    case T8_TOPOLOGY::LINE_LIST:
      internalTopology = GL_LINES;
      break;
    case T8_TOPOLOGY::LINE_STRIP:
      internalTopology = GL_LINE_STRIP;
      break;
    case T8_TOPOLOGY::TRIANLE_LIST:
      internalTopology = GL_TRIANGLES;
      break;
    case T8_TOPOLOGY::TRIANGLE_STRIP:
      internalTopology = GL_TRIANGLE_STRIP;
      break;
    default:
      internalTopology = GL_TRIANGLES;
      break;
    }
  }
  void GLDeviceContext::DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex)
  {
    glDrawElements(internalTopology, vertexCount, internalIBFormat, 0);
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

  ShaderBase * GLDevice::CreateShader(std::string src_vs, std::string src_fs, unsigned long long sig)
  {
    ShaderBase *sh = new GLShader();
    sh->CreateShader(src_vs, src_fs,sig);
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


  void * GLVertexBuffer::GetAPIObject() const
  {
    return nullptr;
  }

  void ** GLVertexBuffer::GetAPIObjectReference() const
  {
    return nullptr;
  }

  void GLVertexBuffer::Set(const DeviceContext & deviceContext, const unsigned stride, const unsigned offset)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualVertexBuffer = (VertexBuffer*)this;
    reinterpret_cast<GLDeviceContext*>(const_cast<DeviceContext*>(&deviceContext))->internalStride = stride;
    glBindBuffer(GL_ARRAY_BUFFER, APIID);
  }
  void GLVertexBuffer::UpdateFromSystemCopy(const DeviceContext & deviceContext)
  {
    glBindBuffer(GL_ARRAY_BUFFER, APIID);
    glBufferData(GL_ARRAY_BUFFER, descriptor.byteWidth, &sysMemCpy[0], GL_STATIC_DRAW);
  }
  void GLVertexBuffer::UpdateFromBuffer(const DeviceContext & deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    UpdateFromSystemCopy(deviceContext);
  }
  void GLVertexBuffer::release()
  {
    sysMemCpy.clear();
    glDeleteBuffers(1,&APIID);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    delete this;
  }
  void GLVertexBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
  {
    descriptor = desc;
    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
    }
    glGenBuffers(1, &APIID);
    glBindBuffer(GL_ARRAY_BUFFER, APIID);
    glBufferData(GL_ARRAY_BUFFER, desc.byteWidth, initialData, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }


  void * GLIndexBuffer::GetAPIObject() const
  {
    return nullptr;
  }

  void ** GLIndexBuffer::GetAPIObjectReference() const
  {
    return nullptr;
  }

  void GLIndexBuffer::Set(const DeviceContext & deviceContext, const unsigned offset, T8_IB_FORMAR::E format)
  {
    switch (format)
    {
    case T8_IB_FORMAR::R16:
      reinterpret_cast<GLDeviceContext*>(const_cast<DeviceContext*>(&deviceContext))->internalIBFormat = GL_UNSIGNED_SHORT;
      break;
    case T8_IB_FORMAR::R32:
      reinterpret_cast<GLDeviceContext*>(const_cast<DeviceContext*>(&deviceContext))->internalIBFormat = GL_UNSIGNED_INT;
      break;
    default:
      break;
    }
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, APIID);
  }
  void GLIndexBuffer::UpdateFromSystemCopy(const DeviceContext & deviceContext)
  {
    glBindBuffer(GL_ARRAY_BUFFER, APIID);
    glBufferData(GL_ARRAY_BUFFER, descriptor.byteWidth, &sysMemCpy[0], GL_STATIC_DRAW);
  }
  void GLIndexBuffer::UpdateFromBuffer(const DeviceContext & deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    UpdateFromSystemCopy(deviceContext);
  }
  void GLIndexBuffer::release()
  {
    sysMemCpy.clear();
    glDeleteBuffers(1, &APIID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    delete this;
  }
  void GLIndexBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
  {
    descriptor = desc;
    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
    }
    glGenBuffers(1, &APIID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, APIID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, desc.byteWidth, initialData, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  }


  void * GLConstantBuffer::GetAPIObject() const
  {
    return nullptr;
  }

  void ** GLConstantBuffer::GetAPIObjectReference() const
  {
    return nullptr;
  }

  void GLConstantBuffer::Set(const DeviceContext & deviceContext)
  {
    const_cast<DeviceContext*>(&deviceContext)->actualConstantBuffer = (ConstantBuffer*)this;
    GLShader* sh = reinterpret_cast<GLShader*>(deviceContext.actualShaderSet);

    for (auto &it : sh->internalUniformsLocs) {
      switch (it.type)
      {
      case hyperspace::shader::datatype_::INT_:
        glUniform1i(it.loc, *reinterpret_cast<GLint*>(&sysMemCpy[it.bufferBytePosition]));
        break;
      case hyperspace::shader::datatype_::BOOLEAN_:
        glUniform1i(it.loc, *reinterpret_cast<GLint*>(&sysMemCpy[it.bufferBytePosition]));
        break;
      case hyperspace::shader::datatype_::FLOAT_:
        glUniform1f(it.loc, *reinterpret_cast<GLfloat*>(&sysMemCpy[it.bufferBytePosition]));
        break;
      case hyperspace::shader::datatype_::MAT2_:
        glUniformMatrix2fv(it.loc, it.num, GL_FALSE, reinterpret_cast<GLfloat*>(&sysMemCpy[it.bufferBytePosition]));
        break;
      case hyperspace::shader::datatype_::MAT3_:
        glUniformMatrix3fv(it.loc, it.num, GL_FALSE, reinterpret_cast<GLfloat*>(&sysMemCpy[it.bufferBytePosition]));
        break;
      case hyperspace::shader::datatype_::MAT4_:
        glUniformMatrix4fv(it.loc, it.num, GL_FALSE, reinterpret_cast<GLfloat*>(&sysMemCpy[it.bufferBytePosition]));
        break;
      case hyperspace::shader::datatype_::VECTOR2_:
        glUniform2fv(it.loc, it.num, reinterpret_cast<GLfloat*>(&sysMemCpy[it.bufferBytePosition]));
        break;
      case hyperspace::shader::datatype_::VECTOR3_:
        glUniform3fv(it.loc, it.num, reinterpret_cast<GLfloat*>(&sysMemCpy[it.bufferBytePosition]));
        break;
      case hyperspace::shader::datatype_::VECTOR4_:
        glUniform4fv(it.loc, it.num, reinterpret_cast<GLfloat*>(&sysMemCpy[it.bufferBytePosition]));
        break;
      default:
        break;
      }
    }
  }
  void GLConstantBuffer::UpdateFromSystemCopy(const DeviceContext & deviceContext)
  {
  }
  void GLConstantBuffer::UpdateFromBuffer(const DeviceContext & deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
  }
  void GLConstantBuffer::release()
  {
    sysMemCpy.clear();
  }
  void GLConstantBuffer::Create(const Device & device, BufferDesc desc, void * initialData)
  {
    descriptor = desc;
    if (initialData) {
      sysMemCpy.assign((char*)initialData, (char*)initialData + desc.byteWidth);
    }
  }


#if defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
  void EGLError(const char* c_ptr) {

    EGLint iErr = eglGetError();
    if (iErr != EGL_SUCCESS) {
      std::cout << "EGL CALL: " << c_ptr << " Error Code: " << iErr << std::endl;
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
    std::cout << "USING HEADLESS CONTEXT" << std::endl;
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
      std::cout << "can't open native display" << std::endl;
    }

    eglDisplay = eglGetDisplay(nativeDisplay);

    EGLError("eglGetDisplay");

    EGLint iMajorVersion, iMinorVersion;

    if (!eglInitialize(eglDisplay, &iMajorVersion, &iMinorVersion)) {
      std::cout << "Failed to initialize egl" << std::endl;
    }
    else {
      std::cout << "EGL version " << iMajorVersion << "." << iMinorVersion << std::endl;
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLError("eglBindAPI");

    const EGLint attribs[] = {
      EGL_SURFACE_TYPE,	EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE,	EGL_OPENGL_ES2_BIT,
      EGL_BLUE_SIZE,		8,
      EGL_GREEN_SIZE,		8,
      EGL_RED_SIZE,		8,
      EGL_DEPTH_SIZE,		24,
      EGL_NONE
    };

    if (!eglChooseConfig(eglDisplay, attribs, &eglConfig, 1, &numConfigs)) {
      std::cout << "Failed to choose config" << std::endl;
    }

    EGLError("eglChooseConfig");

    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, eglWindow, NULL);

    EGLError("eglCreateWindowSurface");

    EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    eglContext = eglCreateContext(eglDisplay, eglConfig, NULL, ai32ContextAttribs);

    EGLError("eglCreateContext");

    if (eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) == EGL_FALSE) {
      std::cout << "Failed to make current" << std::endl;
      return;
    }

    eglQuerySurface(eglDisplay, eglSurface, EGL_WIDTH, &width);
    eglQuerySurface(eglDisplay, eglSurface, EGL_HEIGHT, &height);
#elif defined(USING_OPENGL)
    GLenum err = glewInit();
    if (GLEW_OK != err) {
      printf("Error: %s\n", glewGetErrorString(err));
    }
    else {
      printf("GLEW OK\n");
    }
    SDL_Surface *sur = SDL_GetVideoSurface();
    width = sur->w;
    height = sur->h;
#endif
#endif//HEADLESS
    std::string GL_Version = std::string((const char*)glGetString(GL_VERSION));
    std::string GL_Extensions = std::string((const char*)glGetString(GL_EXTENSIONS));

    std::istringstream iss(GL_Extensions);
    std::vector<std::string> tokens{ std::istream_iterator<std::string>{iss},
      std::istream_iterator<std::string>{} };

    ExtensionsTok = tokens;
    Extensions = GL_Extensions;

    std::cout << "GL Version: " << GL_Version << "\n\nExtensions\n\n";

    for (unsigned int i = 0; i < ExtensionsTok.size(); i++) {
      printf("[%s]\n", ExtensionsTok[i].c_str());
    }

    const unsigned char *version = glGetString(GL_SHADING_LANGUAGE_VERSION);
    printf("GLSL Ver: %s \n", version);

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
  }

  void	GLDriver::SetDimensions(int w, int h) {
    width = w;
    height = h;
  }

  void GLDriver::SetBlendState(BLEND_STATES state)
  {
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

  void GLDriver::SaveScreenshot(std::string path)
  {
    unsigned char *pixels;
    std::ofstream out(path + std::string(".ppm"), std::ios::binary);
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    pixels = new unsigned char[viewport[2] * viewport[3] * 4];

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, viewport[2], viewport[3], GL_RGBA,
      GL_UNSIGNED_BYTE, pixels);
    glReadBuffer(GL_BACK);

    std::string h = std::string("P3\n");
    out.write(h.c_str(),h.size());
    h = std::string("# Test Image\n");
    out.write(h.c_str(), h.size());
    h = std::string(std::to_string(viewport[2]) + std::string(" ")+ std::to_string( viewport[3]) + std::string("\n"));
    out.write(h.c_str(), h.size());
    h = std::string("255\n");
    out.write(h.c_str(), h.size());

    for (int i = viewport[3] * 4 - 4; i >=0; i-=4)
    {
      for (int j = 0; j < viewport[2] * 4; j+=4)
      {
        int indx = j + i*viewport[2];
        unsigned int val = (unsigned int)pixels[indx];
        out << val;
        out << " ";
        val = (unsigned int)pixels[indx +1];
        out << val;
        out << " ";
        val = (unsigned int)pixels[indx +2];
        out << val;
        out << " ";
      }
      out.put('\n');
    }

    delete[] pixels;
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
#ifdef OS_WINDOWS
#if defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30)
    eglSwapBuffers(eglDisplay, eglSurface);
#elif defined(USING_OPENGL)
    SDL_GL_SwapBuffers();
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

  }



}
