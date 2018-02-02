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
// SDL
#include <SDL/SDL.h>
// Windows
#include <windows.h>
#include <mmsystem.h>
namespace t800 {
  void Win32Framework::InitGlobalVars() {


  }

  void Win32Framework::OnCreateApplication(ApplicationDesc desc) {
    aplicationDescriptor = desc;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
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
      case SDL_KEYDOWN: {
        if (evento.key.keysym.sym == SDLK_ESCAPE) {
          m_alive = false;
        }
        pBaseApp->IManager.KeyStates[0][evento.key.keysym.sym] = true;
      }break;
      case SDL_KEYUP: {
        pBaseApp->IManager.KeyStates[0][evento.key.keysym.sym] = false;
        pBaseApp->IManager.KeyStates[1][evento.key.keysym.sym] = false;
      }break;

	  case SDL_QUIT: {
		  m_alive = false;
	  }break;

      }
    }
    static int xDelta = 0;
    static int yDelta = 0;
    int x = 0, y = 0;

    //SDL_GetMouseState(&x, &y);
	POINT point;
	GetCursorPos(&point);
	x = point.x;
	y = point.y;
	

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
    std::string title = aplicationDescriptor.title;
    if (api == GRAPHICS_API::OPENGL)
      title += "   GL";
    else
      title += "   D3D11";

    SDL_WM_SetCaption(title.c_str(), 0);

    int flags = SDL_HWSURFACE;

	if (aplicationDescriptor.videoMode == t800::T8_VIDEO_MODE::FULLSCREEN) {
		flags |= SDL_FULLSCREEN;
	}

    if (api == GRAPHICS_API::OPENGL) {
      flags = flags | SDL_OPENGL;
      SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    }

    if (SDL_SetVideoMode(aplicationDescriptor.width, aplicationDescriptor.height, 32, flags) == 0) {
      printf("Video mode set failed: %s\n", SDL_GetError());
    }
    if (api == GRAPHICS_API::OPENGL)
      pVideoDriver = new GLDriver;
    else {
      pVideoDriver = new D3DXDriver;
      pVideoDriver->SetDimensions(aplicationDescriptor.width, aplicationDescriptor.height);
    }

    g_pBaseDriver = pVideoDriver;
    pVideoDriver->SetWindow(0);
    pVideoDriver->InitDriver();
    pBaseApp->CreateAssets();
  }
}
