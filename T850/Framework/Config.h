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

#ifndef T800_CONFIG_H
#define T800_CONFIG_H

#ifdef __linux__
#ifndef OS_LINUX
#define OS_LINUX
#endif
#else
#define OS_WINDOWS
#endif

#ifndef FORCE_LOW_RES_TEXTURES
#define FORCE_LOW_RES_TEXTURES 
#endif

#ifndef FORCED_FACTOR
#define FORCED_FACTOR 2
#endif

#ifndef DEBUG_DRIVER
#define DEBUG_DRIVER 0
#endif

#ifndef VDEBUG_NO_LIGHT
#define VDEBUG_NO_LIGHT 0
#endif

#ifndef VDEBUG_SIMPLE_COLOR
#define VDEBUG_SIMPLE_COLOR 0
#endif

#if VDEBUG_SIMPLE_COLOR && VDEBUG_NO_LIGHT
#undef VDEBUG_NO_LIGHT
#define VDEBUG_NO_LIGHT 0
#endif

#define OGLES20 2
#define OGLES30 3
#define OGLES31 4
#define OGL 5

#define GL_DRIVER_SELECTED OGLES30

#if   GL_DRIVER_SELECTED == OGLES20
	#define USING_OPENGL_ES20
#elif GL_DRIVER_SELECTED == OGLES30
	#define USING_OPENGL_ES30
#elif GL_DRIVER_SELECTED == OGLES31
	#define USING_OPENGL_ES31
#elif GL_DRIVER_SELECTED == OGL
	#define USING_OPENGL
#else
	#define USING_OPENGL // Default GL
#endif

#if defined(OS_LINUX)
#undef USING_D3D11
#undef USING_OPENGL
#define USING_OPENGL_ES31
#endif

#if defined(USING_OPENGL_ES20) || defined(USING_OPENGL_ES30)  || defined(USING_OPENGL_ES31) || defined(USING_OPENGL)
	#define USING_GL_COMMON
#endif


#define SDL 1
#define FREEGLUT 2
#define WAYLAND_NATIVE 3


#define WINDOW_MANAGER WAYLAND_NATIVE

#if WINDOW_MANAGER == SDL
#define USING_SDL
#elif WINDOW_MANAGER == FREEGLUT
#define USING_FREEGLUT
#elif WINDOW_MANAGER == WAYLAND_NATIVE
#define USING_WAYLAND_NATIVE
#else
#define USING_SDL
#endif

#ifdef OS_WINDOWS
#undef USING_FREEGLUT
#define USING_SDL
#endif



#endif
