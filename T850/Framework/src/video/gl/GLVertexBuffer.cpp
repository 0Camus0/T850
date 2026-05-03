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

#include <video/gl/GLVertexBuffer.h>
#include <video/gl/GLDeviceContext.h>
#include <debug/RenderTrace.h>

#ifdef T850_HEADLESS
#include <GLES3/gl31.h>
#else
#ifdef OS_WINDOWS
#if defined(USING_OPENGL_ES20)
#include <GLES2/gl2.h>
#elif defined(USING_OPENGL_ES30)
#include <GLES3/gl3.h>
#elif defined(USING_OPENGL_ES31)
#include <GLES3/gl31.h>
#elif defined(USING_OPENGL)
#include <GL/glew.h>
#else
#include <GL/glew.h>
#endif
#elif defined(OS_LINUX)
#if defined(USING_OPENGL_ES20)
#include <GLES2/gl2.h>
#elif defined(USING_OPENGL_ES30)
#include <GLES3/gl3.h>
#elif defined(USING_OPENGL_ES31)
#include <GLES3/gl31.h>
#elif defined(USING_OPENGL)
#include <GL/glew.h>
#else
#include <GL/glew.h>
#endif
#endif
#endif

namespace t850 {
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
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->EvBindVertexBufferRequest(bufId, stride, offset);
    }
#endif
  }
  void GLVertexBuffer::UpdateFromSystemCopy(const DeviceContext & deviceContext)
  {
    glBindBuffer(GL_ARRAY_BUFFER, APIID);
    if (descriptor.usage == BufferUsage::DINAMIC) {
      glBufferSubData(GL_ARRAY_BUFFER, 0, descriptor.byteWidth, &sysMemCpy[0]);
    } else {
      glBufferData(GL_ARRAY_BUFFER, descriptor.byteWidth, &sysMemCpy[0], GL_STATIC_DRAW);
    }
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && !sysMemCpy.empty()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->RecordBufferUpdate(bufId, sysMemCpy.data(), (uint32_t)sysMemCpy.size(), "vb", "");
    }
#endif
  }
  void GLVertexBuffer::UpdateFromBuffer(const DeviceContext & deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    UpdateFromSystemCopy(deviceContext);
    // RecordBufferUpdate is intentionally only emitted by UpdateFromSystemCopy
    // — it is the leaf path that all updates flow through. Adding another
    // record here would double-version the same upload.
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
    GLenum usage = GL_STATIC_DRAW;
    if (desc.usage == BufferUsage::DINAMIC) usage = GL_DYNAMIC_DRAW;
    glGenBuffers(1, &APIID);
    glBindBuffer(GL_ARRAY_BUFFER, APIID);
    glBufferData(GL_ARRAY_BUFFER, desc.byteWidth, initialData, usage);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && initialData) {
      int bufId = g_renderTracer->EnsureBufferId(this, "vb");
      g_renderTracer->RecordBufferUpdate(bufId, initialData, desc.byteWidth, "vb", "");
    }
#endif
  }
}
