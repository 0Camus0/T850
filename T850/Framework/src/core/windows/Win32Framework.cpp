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

#include <core/windows/Win32Framework.h>
#include <core/EngineContext.h>
#include <core/Config.h>

#include <video/gl/GLDriver.h>
#if defined(OS_WINDOWS)
#include <video/d3d11/D3D11Driver.h>
#include <video/d3d12/D3D12Driver.h>
#include <video/vulkan/VulkanDriver.h>
#endif
// SDL3
#include <SDL3/SDL.h>
// Windows
#include <windows.h>
#include <mmsystem.h>
#include <utils/ThreadPool.h>
#include <utils/Log.h>
#include <debug/RuntimeTelemetry.h>
namespace t850 {
  void Win32Framework::InitGlobalVars() {


  }

  void Win32Framework::OnCreateApplication(ApplicationDesc desc) {
    aplicationDescriptor = desc;
    InitGlobalThreadPool();
    RuntimeTelemetry::InitializeFromConfig(g_config);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      printf("Video initialization failed: %s\n", SDL_GetError());
    }
    pBaseApp->InitVars();
    ChangeAPI(desc.api);
    m_inited = true;
  }
  void Win32Framework::OnDestroyApplication() {
    pVideoDriver->FlushGPUResources();  // release cmd buffer/descriptor refs before scene cleanup
    pBaseApp->DestroyAssets();
    RuntimeTelemetry::Shutdown();
    pVideoDriver->DestroyDriver();
    delete pVideoDriver;
    pVideoDriver = nullptr;
    g_pBaseDriver = nullptr;
    ShutdownGlobalThreadPool();
    ClearEngineContext();
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
    pBaseApp->IManager.scrollDelta = 0.0f;
    SDL_Event       evento;
    while (SDL_PollEvent(&evento)) {
      switch (evento.type) {
      case SDL_EVENT_KEY_DOWN: {
        int t800key = SDL3KeyToSTDKEY((unsigned int)evento.key.key);
        if (t800key == T800K_ESCAPE) {
          // Modal UI (e.g. line-edit popup) consumes Escape; don't quit the app.
          if (!pBaseApp || !pBaseApp->IsModalActive()) {
            m_alive = false;
          }
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

      case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int btn = evento.button.button - 1; // SDL buttons are 1-based
        if (btn >= 0 && btn < MAXMOUSEBUTTONS)
          pBaseApp->IManager.MouseButtonStates[0][btn] = true;
      }break;
      case SDL_EVENT_MOUSE_BUTTON_UP: {
        int btn = evento.button.button - 1;
        if (btn >= 0 && btn < MAXMOUSEBUTTONS) {
          pBaseApp->IManager.MouseButtonStates[0][btn] = false;
          pBaseApp->IManager.MouseButtonStates[1][btn] = false;
        }
      }break;

      case SDL_EVENT_TEXT_INPUT: {
        if (evento.text.text) {
          pBaseApp->IManager.textInput.append(evento.text.text);
        }
      }break;

      case SDL_EVENT_MOUSE_WHEEL: {
        float wy = evento.wheel.y;
        if (evento.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) wy = -wy;
        pBaseApp->IManager.scrollDelta += wy;
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

    // Window-relative mouse position for GUI
    POINT clientPt = point;
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(m_pWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (hwnd) {
      ScreenToClient(hwnd, &clientPt);
    }
    pBaseApp->IManager.mouseX = clientPt.x;
    pBaseApp->IManager.mouseY = clientPt.y;

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
  void Win32Framework::ChangeAPI(GraphicsApi::E api)
  {
#ifndef OS_WINDOWS
    if (api == GraphicsApi::D3D11) {
      api = GraphicsApi::OPENGL;
    }
#endif
    if (m_inited) {
      pVideoDriver->FlushGPUResources();  // release cmd buffer/descriptor refs before scene cleanup
      pBaseApp->DestroyAssets();
      pVideoDriver->DestroyDriver();
      delete pVideoDriver;
      pVideoDriver = nullptr;
      g_pBaseDriver = nullptr;
      ClearEngineContext();
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
    if (api == GraphicsApi::OPENGL)
      title += "   GL";
    else if (api == GraphicsApi::D3D12)
      title += "   D3D12";
    else if (api == GraphicsApi::VULKAN)
      title += "   Vulkan";
    else
      title += "   D3D11";

    Uint64 flags = 0;

	if (aplicationDescriptor.videoMode == t850::VideoMode::FULLSCREEN) {
		flags |= SDL_WINDOW_FULLSCREEN;
	}

    if (api == GraphicsApi::OPENGL) {
#if defined(USING_OPENGL)
      SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "0");
      flags |= SDL_WINDOW_OPENGL;
      SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
      T8_LOG_INFO("SDL_WINDOW_OPENGL flag set, depth=24 (WGL forced)");
#else
      T8_LOG_ERROR("USING_OPENGL not defined — GL context will NOT be created");
#endif
    }
    else if (api == GraphicsApi::VULKAN) {
      flags |= SDL_WINDOW_VULKAN;
    }

    m_pWindow = SDL_CreateWindow(title.c_str(), aplicationDescriptor.width, aplicationDescriptor.height, flags);
    if (!m_pWindow) {
      printf("Window creation failed: %s\n", SDL_GetError());
    }

    // Enable text input so SDL_EVENT_TEXT_INPUT events are generated for ImGui text fields.
    SDL_StartTextInput(m_pWindow);

    // Set window icon from embedded exe resource
    {
      HICON hIcon = LoadIcon(GetModuleHandle(NULL), TEXT("IDI_ICON1"));
      if (hIcon) {
        ICONINFO ii;
        if (GetIconInfo(hIcon, &ii)) {
          BITMAP bm;
          GetObject(ii.hbmColor, sizeof(bm), &bm);
          int w = bm.bmWidth;
          int h = bm.bmHeight;
          BITMAPINFOHEADER bi = {};
          bi.biSize = sizeof(bi);
          bi.biWidth = w;
          bi.biHeight = -h; // top-down
          bi.biPlanes = 1;
          bi.biBitCount = 32;
          bi.biCompression = BI_RGB;
          HDC hdc = GetDC(NULL);
          unsigned char* pixels = new unsigned char[w * h * 4];
          GetDIBits(hdc, ii.hbmColor, 0, h, pixels, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
          ReleaseDC(NULL, hdc);
          // GDI returns BGRA, SDL wants RGBA — swap R and B
          for (int i = 0; i < w * h * 4; i += 4) {
            unsigned char tmp = pixels[i];
            pixels[i] = pixels[i + 2];
            pixels[i + 2] = tmp;
          }
          SDL_Surface* icon = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
          if (icon) {
            SDL_SetWindowIcon(m_pWindow, icon);
            SDL_DestroySurface(icon);
          }
          delete[] pixels;
          DeleteObject(ii.hbmColor);
          DeleteObject(ii.hbmMask);
        }
        DestroyIcon(hIcon);
      }
    }

    if (api == GraphicsApi::OPENGL) {
#if defined(USING_OPENGL)
      m_glContext = SDL_GL_CreateContext(m_pWindow);
      if (!m_glContext) {
        T8_LOG_ERROR("GL context creation failed: %s", SDL_GetError());
      } else {
        T8_LOG_INFO("SDL GL context created OK");
      }
#else
      T8_LOG_ERROR("USING_OPENGL not defined — skipping SDL_GL_CreateContext");
#endif
      pVideoDriver = new GLDriver;
    }
    else if (api == GraphicsApi::D3D12) {
      pVideoDriver = new D3D12Driver;
      pVideoDriver->SetDimensions(aplicationDescriptor.width, aplicationDescriptor.height);
    }
    else if (api == GraphicsApi::VULKAN) {
      pVideoDriver = new VulkanDriver;
      pVideoDriver->SetDimensions(aplicationDescriptor.width, aplicationDescriptor.height);
    }
    else {
      pVideoDriver = new D3DXDriver;
      pVideoDriver->SetDimensions(aplicationDescriptor.width, aplicationDescriptor.height);
    }

    g_pBaseDriver = pVideoDriver;
    pVideoDriver->SetWindow(m_pWindow);
    pVideoDriver->InitDriver();
    RefreshEngineContextFromGlobals();
    pBaseApp->CreateAssets();
    // For D3D12: record where permanent descriptors end so per-frame dynamic CBVs start after them
    pVideoDriver->BuildPipelineObjects();
  }
}
