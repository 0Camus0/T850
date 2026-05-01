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

#include <video/gl/GLConstantBuffer.h>
#include <video/gl/GLShader.h>
#include <utils/Log.h>
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
      // Bounds check: skip if uniform byte position exceeds CB data size
      int endPos = it.bufferBytePosition + it.size;
      if (it.bufferBytePosition < 0 || endPos > (int)sysMemCpy.size()) {
        T8_LOG_ERROR("[GL] CB::Set uniform '%s' bytePos=%d size=%d exceeds CB size %d — skipped",
                     it.name.c_str(), it.bufferBytePosition, it.size, (int)sysMemCpy.size());
        continue;
      }
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
#ifdef T850_RENDER_TRACE
    if (T8_TRACE_ACTIVE() && !sysMemCpy.empty()) {
      int bufId = g_renderTracer->EnsureBufferId(this, "cbuffer");
      // GL has no real CB object; uniforms were just plumbed via glUniform*.
      // Record the same update + bind request + commit triple as D3D11 so
      // the trace shape is identical across backends. Slot 0 stands in for
      // "the engine's single CB" since GL uses loose uniforms, not a UBO.
      g_renderTracer->EvUpdateCBuffer(bufId, sysMemCpy.data(),
                                      (uint32_t)sysMemCpy.size(),
                                      /*allocOffset=*/0);
      g_renderTracer->EvBindCBufferRequest(bufId);
      g_renderTracer->EvBindCBufferCommit(/*slot=*/0, bufId);
    }
#endif
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
}
