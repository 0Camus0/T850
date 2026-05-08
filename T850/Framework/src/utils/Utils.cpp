#include <pch.h>
#include <utils/ResourceLocator.h>
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

#include <utils/Utils.h>
#include <utils/Log.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

#ifdef USING_GL_COMMON
void CheckGLError(){
    GLenum errCode;
    if ((errCode = glGetError()) !=
    GL_NO_ERROR)
    {
    printf("\nGL ERROR[%d]\n",errCode);
    exit(errCode);
    }
}

void CheckFBStatus(){
GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
switch(status)
{
  case GL_FRAMEBUFFER_COMPLETE:
  //  printf("Framebuffer complete.");
    break;

  case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
    printf("[ERROR] Framebuffer incomplete: Attachment is NOT complete.");
    break;

  case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
    printf("[ERROR] Framebuffer incomplete: No image is attached to FBO.");
   break;
#ifndef USING_OPENGL
  case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
    printf("[ERROR] Framebuffer incomplete: Attached images have different dimensions.");
    break;
#endif
  case GL_FRAMEBUFFER_UNSUPPORTED:
    printf("[ERROR] Unsupported by FBO implementation.");
    break;

   default:
    printf("[ERROR] Unknown error.");
    break;
}
}

void checkcompilederrors(GLuint shader, GLenum type) {
	GLint bShaderCompiled;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &bShaderCompiled);
	if (!bShaderCompiled) {
		int i32InfoLogLength, i32CharsWritten;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &i32InfoLogLength);

		char* pszInfoLog = new char[i32InfoLogLength];
		glGetShaderInfoLog(shader, i32InfoLogLength, &i32CharsWritten, pszInfoLog);
		char* pszMsg = new char[i32InfoLogLength + 256];
		if (type == GL_FRAGMENT_SHADER) {
			sprintf(pszMsg, "Failed to compile pixel shader: %s", pszInfoLog);
		}
		else if (type == GL_VERTEX_SHADER) {
			sprintf(pszMsg, "Failed to compile vertex shader: %s", pszInfoLog);
		}
		else {
			sprintf(pszMsg, "Failed to compile wtf shader: %s", pszInfoLog);
		}
		printf("%s", pszMsg);
		delete[] pszMsg;
		delete[] pszInfoLog;
	}
}

GLuint createShader(GLenum type, char* pSource) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, (const char**)&pSource, NULL);
	glCompileShader(shader);
	checkcompilederrors(shader, type);
	return shader;
}
#else
void checkcompilederrors(unsigned int shader, unsigned int type) {

}
unsigned int createShader(unsigned int type, char* pSource) {
	return 0;
}
#endif

char *file2string(const char *path) {
	if (!path || !*path) {
		T8_LOG_ERROR("Can't open shader/source file: empty path");
		return nullptr;
	}

	std::string text;
	if (!t850::ResourceLocator::Instance().ReadText(path, text)) {
		T8_LOG_ERROR("Can't open file '%s'", path);
		return nullptr;
	}
	char* str = static_cast<char*>(std::malloc(text.size() + 1));
	if (!str) {
		T8_LOG_ERROR("Out of memory loading file '%s' (%zu bytes)", path, text.size());
		return nullptr;
	}
	memcpy(str, text.data(), text.size());
	str[text.size()] = '\0';
	return str;
}

std::string RemovePath(std::string p) {
	std::string path = p;
	size_t firstSlash = path.find_last_of("\\") + 1;
	size_t Length = path.size() - firstSlash;
	path = path.substr(firstSlash, Length);
	return path;
}
