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

#include <core/windows/Win32Framework.h>

#include <video/GLDriver.h>
#if defined(OS_WINDOWS)
#include <video/windows/D3DXDriver.h>
#endif
// SDL3
#include <SDL3/SDL.h>
// Windows
#include <windows.h>
#include <mmsystem.h>
namespace t800 {
  void Win32Framework::InitGlobalVars() {


  }

  void Win32Framework::OnCreateApplication(ApplicationDesc desc) {
    aplicationDescriptor = desc;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      printf("Video initialization failed: %s\n", SDL_GetError());
    }
    pBaseApp->InitVars();
    ChangeAPI(desc.api);
    m_inited = true;
  }
  void Win32Framework::OnDestroyApplication() {
    pBaseApp->DestroyAssets();
    pVideoDriver->DestroyDriver();
    delete pVideoDriver;
    if (m_glContext) {
      SDL_GL_DestroyContext(m_glContext);
      m_glContext = nullptr;
    }
    if (m_pWindow) {
      SDL_DestroyWindow(m_pWindow);
      m_pWindow = nullptr;
    }
    SDL_Quit();
    m_inited = false;
  }
  void Win32Framework::OnInterruptApplication() {
  }
  void Win32Framework::OnResumeApplication() {
  }
  void Win32Framework::UpdateApplication() {
    while (m_alive) {
      ProcessInput();
      pBaseApp->OnUpdate();
    }
  }
  void Win32Framework::ProcessInput() {
    SDL_Event       evento;
    while (SDL_PollEvent(&evento)) {
      switch (evento.type) {
      case SDL_EVENT_KEY_DOWN: {
        int t800key = SDL3KeyToSTDKEY((unsigned int)evento.key.key);
        if (t800key == T800K_ESCAPE) {
          m_alive = false;
        }
        if (t800key >= 0 && t800key < MAXKEYS)
          pBaseApp->IManager.KeyStates[0][t800key] = true;
      }break;
      case SDL_EVENT_KEY_UP: {
        int t800key = SDL3KeyToSTDKEY((unsigned int)evento.key.key);
        if (t800key >= 0 && t800key < MAXKEYS) {
          pBaseApp->IManager.KeyStates[0][t800key] = false;
          pBaseApp->IManager.KeyStates[1][t800key] = false;
        }
      }break;

	  case SDL_EVENT_QUIT: {
		  m_alive = false;
	  }break;

      }
    }
    static int xDelta = 0;
    static int yDelta = 0;
    static bool firstCall = true;
    int x = 0, y = 0;

	POINT point;
	GetCursorPos(&point);
	x = point.x;
	y = point.y;

    if (firstCall) {
      firstCall = false;
      xDelta = x;
      yDelta = y;
    }

    xDelta = x - xDelta;
    yDelta = y - yDelta;

    pBaseApp->IManager.xDelta = xDelta;
    pBaseApp->IManager.yDelta = yDelta;

    xDelta = x;
    yDelta = y;
  }

  void Win32Framework::ResetApplication() {
  }
  void Win32Framework::ChangeAPI(GRAPHICS_API::E api)
  {
#ifndef OS_WINDOWS
    if (api == GRAPHICS_API::D3D11) {
      api = GRAPHICS_API::OPENGL;
    }
#endif
    if (m_inited) {
      pBaseApp->DestroyAssets();
      pVideoDriver->DestroyDriver();
      delete pVideoDriver;
    }

    // Destroy previous SDL window/context if changing API
    if (m_glContext) {
      SDL_GL_DestroyContext(m_glContext);
      m_glContext = nullptr;
    }
    if (m_pWindow) {
      SDL_DestroyWindow(m_pWindow);
      m_pWindow = nullptr;
    }

    std::string title = aplicationDescriptor.title;
    if (api == GRAPHICS_API::OPENGL)
      title += "   GL";
    else
      title += "   D3D11";

    Uint64 flags = 0;

	if (aplicationDescriptor.videoMode == t800::T8_VIDEO_MODE::FULLSCREEN) {
		flags |= SDL_WINDOW_FULLSCREEN;
	}

    if (api == GRAPHICS_API::OPENGL) {
#if defined(USING_OPENGL)
      flags |= SDL_WINDOW_OPENGL;
      SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#endif
    }

    m_pWindow = SDL_CreateWindow(title.c_str(), aplicationDescriptor.width, aplicationDescriptor.height, flags);
    if (!m_pWindow) {
      printf("Window creation failed: %s\n", SDL_GetError());
    }

    if (api == GRAPHICS_API::OPENGL) {
#if defined(USING_OPENGL)
      m_glContext = SDL_GL_CreateContext(m_pWindow);
      if (!m_glContext) {
        printf("GL context creation failed: %s\n", SDL_GetError());
      }
#endif
      pVideoDriver = new GLDriver;
    }
    else {
      pVideoDriver = new D3DXDriver;
      pVideoDriver->SetDimensions(aplicationDescriptor.width, aplicationDescriptor.height);
    }

    g_pBaseDriver = pVideoDriver;
    pVideoDriver->SetWindow(m_pWindow);
    pVideoDriver->InitDriver();
    pBaseApp->CreateAssets();
  }
}
