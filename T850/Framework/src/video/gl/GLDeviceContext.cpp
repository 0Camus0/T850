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

#include <video/gl/GLDeviceContext.h>

#ifdef OS_WINDOWS
#if defined(USING_OPENGL_ES20)
#elif defined (USING_OPENGL_ES30) || defined (USING_OPENGL_ES31)
#elif defined(USING_OPENGL)
#endif
#elif defined(OS_LINUX)
#endif

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
  void GLDeviceContext::SetPrimitiveTopology(Topology::E topology)
  {
    switch (topology)
    {
    case Topology::POINT_LIST:
      internalTopology = GL_POINTS;
      break;
    case Topology::LINE_LIST:
      internalTopology = GL_LINES;
      break;
    case Topology::LINE_STRIP:
      internalTopology = GL_LINE_STRIP;
      break;
    case Topology::TRIANLE_LIST:
      internalTopology = GL_TRIANGLES;
      break;
    case Topology::TRIANGLE_STRIP:
      internalTopology = GL_TRIANGLE_STRIP;
      break;
    default:
      internalTopology = GL_TRIANGLES;
      break;
    }
  }
  void GLDeviceContext::DrawIndexed(unsigned vertexCount, unsigned startIndex, unsigned startVertex)
  {
    // Convert startIndex (in elements) to a byte offset into the bound
    // IB, picking the size from the format set by IndexBuffer::Set().
    const unsigned indexStride = (internalIBFormat == GL_UNSIGNED_INT) ? 4u : 2u;
    const GLsizeiptr byteOffset = static_cast<GLsizeiptr>(startIndex) * indexStride;
    if (startVertex == 0u) {
      glDrawElements(internalTopology, vertexCount, internalIBFormat,
                     reinterpret_cast<const void*>(byteOffset));
    } else {
      // glDrawElementsBaseVertex is core in GL 3.2 / GLES 3.2. T850's
      // GL build path requires at least GL 3.3 for the ES_30 shader
      // dialect, so this is safe on desktop GL. ES2/ES3 callers must
      // pre-add the base vertex into their indices when sharing pools.
      glDrawElementsBaseVertex(internalTopology, vertexCount, internalIBFormat,
                               reinterpret_cast<void*>(byteOffset),
                               static_cast<GLint>(startVertex));
    }
  }
}
