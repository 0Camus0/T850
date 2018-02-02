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

#ifndef T800_GLSHADER_H
#define T800_GLSHADER_H

#include <Config.h>

#include <video/BaseDriver.h>

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
#include <SDL/SDL.h>
#endif

#include <utils/Utils.h>
#include <vector>
#include <video/GLSLParser.h>

namespace t800 {
  class GLShader : public ShaderBase {
  public:
	  GLShader() {}

	  virtual ~GLShader() {}

	  bool CreateShaderAPI(std::string src_vs, std::string src_fs, unsigned long long sig) override;
    void  Set(const DeviceContext& deviceContext) override;
    void DestroyAPIShader() override;
	  unsigned int ShaderProg;


    std::vector<InputElement> locs;
    std::vector<InputElement> internalUniformsLocs;
    int vertexWidth;

    GLSL_Parser m_parser;
    GLuint vshader_id;
    GLuint fshader_id;
  };
}


#endif
