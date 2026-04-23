#include "pch.h"
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

namespace t800 {
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
}
