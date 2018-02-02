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

#ifndef LINUXFRAMEWORK_H_INCLUDED
#define LINUXFRAMEWORK_H_INCLUDED


#include <Config.h>

#include <core/Core.h>
#include <video/BaseDriver.h>

#ifdef USING_FREEGLUT
    #include <GL/freeglut.h>
#elif defined(USING_WAYLAND_NATIVE)
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>

    #include <wayland-client.h>
    #include <wayland-server.h>
    #include <wayland-client-protocol.h>
    #include <wayland-egl.h>

    #include <EGL/egl.h>
    #include <GLES2/gl2.h>
#endif

#include <memory>
namespace t800 {
class LinuxFramework : public RootFramework {
public:
	LinuxFramework(AppBase *pBaseApp);
	void InitGlobalVars();
	void OnCreateApplication(ApplicationDesc desc);
	void OnDestroyApplication();
	void OnInterruptApplication();
	void OnResumeApplication();
	void UpdateApplication();
	void ProcessInput();
	void ResetApplication();
  void ChangeAPI(GRAPHICS_API::E api);
	~LinuxFramework() {	}

#ifdef USING_FREEGLUT
	static void IdleFunction();
    static void MouseClickFunction(int button, int state, int x, int y);
    static void MouseMoveFunction(int x, int y);
    static void ResizeWindow(int w, int h);
    static void KeyboardEvent(unsigned char key, int x, int y);
    static void KeyboardReleaseEvent(unsigned char key, int x, int y);
#elif defined(USING_WAYLAND_NATIVE)
    struct wl_display       *wlnd_display;
    struct wl_surface       *wlnd_surface;
    struct wl_egl_window    *wland_egl_window;
    struct wl_shell_surface *wlnd_shell_surface;
    struct wl_region        *wland_region;

    EGLDisplay  eglDisplay;
    EGLConfig   eglConfig;
    EGLSurface  eglSurface;
    EGLContext  eglContext;
#endif
	bool	m_alive;

	static LinuxFramework* thiz;
 };
}


#endif // LINUXFRAMEWORK_H_INCLUDED
