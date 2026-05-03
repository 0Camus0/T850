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

#include <video/gl/GLIndexBuffer.h>
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
  void * GLIndexBuffer::GetAPIObject() const
  {
    return nullptr;
  }

  void ** GLIndexBuffer::GetAPIObjectReference() const
  {
    return nullptr;
  }

  void GLIndexBuffer::Set(const DeviceContext & deviceContext, const unsigned offset, IndexBufferFormat::E format)
  {
    switch (format)
    {
    case IndexBufferFormat::R16:
      reinterpret_cast<GLDeviceContext*>(const_cast<DeviceContext*>(&deviceContext))->internalIBFormat = GL_UNSIGNED_SHORT;
      break;
    case IndexBufferFormat::R32:
      reinterpret_cast<GLDeviceContext*>(const_cast<DeviceContext*>(&deviceContext))->internalIBFormat = GL_UNSIGNED_INT;
      break;
    default:
      break;
    }
    const_cast<DeviceContext*>(&deviceContext)->actualIndexBuffer = (IndexBuffer*)this;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, APIID);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "ib");
      g_renderTracer->EvBindIndexBufferRequest(bufId, (int)format, offset);
    }
#endif
  }
  void GLIndexBuffer::UpdateFromSystemCopy(const DeviceContext & deviceContext)
  {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, APIID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, descriptor.byteWidth, &sysMemCpy[0], GL_STATIC_DRAW);
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && !sysMemCpy.empty()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "ib");
      g_renderTracer->RecordBufferUpdate(bufId, sysMemCpy.data(), (uint32_t)sysMemCpy.size(), "ib", "");
    }
#endif
  }
  void GLIndexBuffer::UpdateFromBuffer(const DeviceContext & deviceContext, const void * buffer)
  {
    sysMemCpy.clear();
    sysMemCpy.assign((char*)buffer, (char*)buffer + descriptor.byteWidth);
    UpdateFromSystemCopy(deviceContext);
    // RecordBufferUpdate is intentionally only emitted by UpdateFromSystemCopy
    // — it is the leaf path that all updates flow through. Adding another
    // record here would double-version the same upload.
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
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && initialData) {
      int bufId = g_renderTracer->EnsureBufferId(this, "ib");
      g_renderTracer->RecordBufferUpdate(bufId, initialData, desc.byteWidth, "ib", "");
    }
#endif
  }
}
