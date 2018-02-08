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

#ifndef T800_GLESXRT_H
#define T800_GLESXRT_H

#include <Config.h>

#include <video/BaseDriver.h>
#include <vector>

#if defined(USING_OPENGL_ES20)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(USING_OPENGL_ES30)
#include <GLES3/gl3.h>
#elif defined(USING_OPENGL_ES31)
#include <GLES3/gl31.h>
#elif defined(USING_OPENGL)
#include <GL/glew.h>
#else
#include <GL/glew.h>
#ifdef USING_SDL
#include <SDL/SDL.h>
#endif
#endif

namespace t800 {
  class GLRT : public BaseRT {
  public:

    virtual ~GLRT() {}
    bool	LoadAPIRT() override;
    void	DestroyAPIRT() override;
    void Set(const DeviceContext& context) override;
    void ChangeCubeDepthTexture(int i);
    std::vector<GLuint>		vFrameBuffers;
    std::vector<GLuint>		vGLColorTex;
    GLuint					DepthTexture;
  };
}


#endif
