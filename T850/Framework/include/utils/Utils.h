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

#ifndef T800_UTILS_GL_H
#define T800_UTILS_GL_H

#include <Config.h>

#include <stdio.h>
#include <iostream>
#include <string>

#if defined(USING_OPENGL_ES20)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(USING_OPENGL_ES30)
#include <GLES3/gl3.h>
#elif defined(USING_OPENGL_ES31)
#include <GLES3/gl31.h>
#elif defined(USING_OPENGL)
#ifdef OS_WINDOWS
#include <GL/glew.h>
#elif defined(OS_LINUX)
#include <GL/gl.h>
#endif
#endif

#ifdef USING_GL_COMMON
void checkcompilederrors(GLuint shader, GLenum type);
void CheckGLError();
void CheckFBStatus();
GLuint createShader(GLenum type, char* pSource);
#else
void checkcompilederrors(unsigned int shader, unsigned int type);
unsigned int createShader(unsigned int type, char* pSource);
#endif

char *file2string(const char *path);
std::string RemovePath(std::string p);

#endif
