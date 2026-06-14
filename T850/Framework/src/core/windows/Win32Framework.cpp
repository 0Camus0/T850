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
#include <scene/MaterialAsset.h>
#include <scene/MeshAssetCache.h>
// SDL3
#include <SDL3/SDL.h>
// Windows
#include <windows.h>
#include <mmsystem.h>
#include <utils/ThreadPool.h>
#include <utils/Log.h>
#include <utils/ConfigRuntime.h>
#include <debug/RuntimeTelemetry.h>
#include <navigation/NavigationSystem.h>
namespace t850 {
  void Win32Framework::ReleaseMouseMode() {
    if (m_pWindow && m_relativeMouseMode) {
      SDL_SetWindowRelativeMouseMode(m_pWindow, false);
    }
    SDL_ShowCursor();
    ClipCursor(nullptr);
    m_cursorConfined = false;
    m_relativeMouseMode = false;
  }

  void Win32Framework::ResetInputAfterWindowStateChange() {
    if (!pBaseApp) {
      m_absMouseBaselineValid = false;
      return;
    }

    pBaseApp->IManager.xDelta = 0;
    pBaseApp->IManager.yDelta = 0;
    pBaseApp->IManager.scrollDelta = 0.0f;
    pBaseApp->IManager.textInput.clear();
    for (int i = 0; i < MAXMOUSEBUTTONS; ++i) {
      pBaseApp->IManager.MouseButtonStates[0][i] = false;
      pBaseApp->IManager.MouseButtonStates[1][i] = false;
    }
    if (m_relativeMouseMode) {
      float discardX = 0.0f;
      float discardY = 0.0f;
      SDL_GetRelativeMouseState(&discardX, &discardY);
    }
    m_absMouseBaselineValid = false;
    SDL_PumpEvents();
  }

  void Win32Framework::ResetMouseDeltaBaseline() {
    if (pBaseApp) {
      pBaseApp->IManager.xDelta = 0;
      pBaseApp->IManager.yDelta = 0;
      pBaseApp->IManager.scrollDelta = 0.0f;
    }
    m_absMouseBaselineValid = false;
    if (m_relativeMouseMode) {
      float discardX = 0.0f;
      float discardY = 0.0f;
      SDL_GetRelativeMouseState(&discardX, &discardY);
    }
  }

  const char* Win32WindowEventName(Uint32 eventType) {
    switch (eventType) {
    case SDL_EVENT_WINDOW_FOCUS_LOST: return "FOCUS_LOST";
    case SDL_EVENT_WINDOW_MINIMIZED: return "MINIMIZED";
    case SDL_EVENT_WINDOW_HIDDEN: return "HIDDEN";
    case SDL_EVENT_WINDOW_FOCUS_GAINED: return "FOCUS_GAINED";
    case SDL_EVENT_WINDOW_RESTORED: return "RESTORED";
    case SDL_EVENT_WINDOW_SHOWN: return "SHOWN";
    case SDL_EVENT_WINDOW_MAXIMIZED: return "MAXIMIZED";
    case SDL_EVENT_WINDOW_RESIZED: return "RESIZED";
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: return "PIXEL_SIZE_CHANGED";
    default: return "WINDOW_EVENT";
    }
  }

  void Win32Framework::TraceWindowEvent(const SDL_Event& event) {
      if (!m_pWindow) {
        return;
      }
      int wx = 0, wy = 0, ww = 0, wh = 0, pw = 0, ph = 0;
      SDL_GetWindowPosition(m_pWindow, &wx, &wy);
      SDL_GetWindowSize(m_pWindow, &ww, &wh);
      SDL_GetWindowSizeInPixels(m_pWindow, &pw, &ph);
      SDL_WindowFlags flags = SDL_GetWindowFlags(m_pWindow);
      float gx = 0.0f, gy = 0.0f, lx = 0.0f, ly = 0.0f;
      SDL_MouseButtonFlags globalButtons = SDL_GetGlobalMouseState(&gx, &gy);
      SDL_MouseButtonFlags localButtons = SDL_GetMouseState(&lx, &ly);
      T8_LOG_TRACE("[Win32WindowEvent] type=%s data=(%d,%d) winPos=(%d,%d) winSize=(%d,%d) pix=(%d,%d) flags=0x%llX globalMouse=(%.1f,%.1f) localMouse=(%.1f,%.1f) buttons=(0x%X,0x%X) confined=%d relative=%d",
                  Win32WindowEventName(event.type),
                  event.window.data1,
                  event.window.data2,
                  wx,
                  wy,
                  ww,
                  wh,
                  pw,
                  ph,
                  static_cast<unsigned long long>(flags),
                  gx,
                  gy,
                  lx,
                  ly,
                  static_cast<unsigned int>(globalButtons),
                  static_cast<unsigned int>(localButtons),
                  m_cursorConfined ? 1 : 0,
                  m_relativeMouseMode ? 1 : 0);
  }

  void Win32Framework::UpdateMouseMode() {
    if (!m_pWindow) {
      ReleaseMouseMode();
      return;
    }

    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(m_pWindow),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        NULL);
    if (!hwnd || GetForegroundWindow() != hwnd || IsIconic(hwnd) || !IsWindowVisible(hwnd)) {
      ReleaseMouseMode();
      return;
    }

    const bool wantsRelativeMouse = pBaseApp && pBaseApp->WantsRelativeMouseMode();
    if (wantsRelativeMouse) {
      ClipCursor(nullptr);
      m_cursorConfined = false;
      if (!m_relativeMouseMode) {
        SDL_SetWindowRelativeMouseMode(m_pWindow, true);
        float discardX = 0.0f;
        float discardY = 0.0f;
        SDL_GetRelativeMouseState(&discardX, &discardY);
      }
      SDL_HideCursor();
      m_relativeMouseMode = true;
      return;
    }

    if (m_relativeMouseMode) {
      SDL_SetWindowRelativeMouseMode(m_pWindow, false);
      float discardX = 0.0f;
      float discardY = 0.0f;
      SDL_GetRelativeMouseState(&discardX, &discardY);
      m_relativeMouseMode = false;
    }
    SDL_ShowCursor();

    if (!pBaseApp || !pBaseApp->IsModalActive()) {
      ClipCursor(nullptr);
      m_cursorConfined = false;
      return;
    }

    RECT clientRect = {};
    if (!GetClientRect(hwnd, &clientRect) ||
        clientRect.right <= clientRect.left ||
        clientRect.bottom <= clientRect.top) {
      ClipCursor(nullptr);
      m_cursorConfined = false;
      return;
    }

    POINT topLeft = { clientRect.left, clientRect.top };
    POINT bottomRight = { clientRect.right, clientRect.bottom };
    if (!ClientToScreen(hwnd, &topLeft) || !ClientToScreen(hwnd, &bottomRight)) {
      ClipCursor(nullptr);
      m_cursorConfined = false;
      return;
    }

    RECT clipRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
    if (ClipCursor(&clipRect)) {
      m_cursorConfined = true;
    } else if (m_cursorConfined) {
      ClipCursor(nullptr);
      m_cursorConfined = false;
    }
  }

  void Win32Framework::InitGlobalVars() {


  }

  void Win32Framework::OnCreateApplication(ApplicationDesc desc) {
    aplicationDescriptor = desc;
    InitGlobalThreadPool();
    RuntimeTelemetry::InitializeFromConfig(g_config);
    const navigation::NavigationBackendInfo navInfo = navigation::GetNavigationBackendInfo();
    T8_LOG_INFO("[Navigation] Recast=%d Detour=%d Crowd=%d TileCache=%d version=%s validation=%s",
                navInfo.recastAvailable ? 1 : 0,
                navInfo.detourAvailable ? 1 : 0,
                navInfo.detourCrowdAvailable ? 1 : 0,
                navInfo.detourTileCacheAvailable ? 1 : 0,
                navInfo.recastVersion.c_str(),
                navigation::ValidateNavigationBackend() ? "ok" : "failed");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      printf("Video initialization failed: %s\n", SDL_GetError());
    }
    pBaseApp->InitVars();
    ChangeAPI(desc.api);
    m_inited = true;
  }
  void Win32Framework::OnDestroyApplication() {
    ReleaseMouseMode();
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
    ReleaseMouseMode();
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
      ReleaseMouseMode();
		  m_alive = false;
	  }break;

      case SDL_EVENT_WINDOW_FOCUS_LOST:
      case SDL_EVENT_WINDOW_MINIMIZED:
      case SDL_EVENT_WINDOW_HIDDEN: {
        TraceWindowEvent(evento);
        ReleaseMouseMode();
      } break;

      case SDL_EVENT_WINDOW_FOCUS_GAINED:
      case SDL_EVENT_WINDOW_RESTORED:
      case SDL_EVENT_WINDOW_SHOWN: {
        TraceWindowEvent(evento);
        ResetInputAfterWindowStateChange();
        UpdateMouseMode();
      } break;

      case SDL_EVENT_WINDOW_MAXIMIZED:
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        TraceWindowEvent(evento);
        ResetMouseDeltaBaseline();
      } break;

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
    UpdateMouseMode();
    int x = 0, y = 0;
    float relativeX = 0.0f;
    float relativeY = 0.0f;
    const bool useRelativeMouse = m_relativeMouseMode && m_pWindow && SDL_GetWindowRelativeMouseMode(m_pWindow);
    if (useRelativeMouse) {
      SDL_GetRelativeMouseState(&relativeX, &relativeY);
    }

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

    if (useRelativeMouse) {
      pBaseApp->IManager.xDelta = static_cast<int>(relativeX);
      pBaseApp->IManager.yDelta = static_cast<int>(relativeY);
      T8_LOG_VERBOSE("[MouseInput] mode=relative abs=(%d,%d) client=(%d,%d) rel=(%.3f,%.3f) delta=(%d,%d)",
                     x,
                     y,
                     clientPt.x,
                     clientPt.y,
                     relativeX,
                     relativeY,
                     pBaseApp->IManager.xDelta,
                     pBaseApp->IManager.yDelta);
      m_lastAbsMouseX = x;
      m_lastAbsMouseY = y;
      m_absMouseBaselineValid = true;
      return;
    }

    if (!m_absMouseBaselineValid) {
      m_absMouseBaselineValid = true;
      m_lastAbsMouseX = x;
      m_lastAbsMouseY = y;
      pBaseApp->IManager.xDelta = 0;
      pBaseApp->IManager.yDelta = 0;
      return;
    }

    int xDelta = x - m_lastAbsMouseX;
    int yDelta = y - m_lastAbsMouseY;

    pBaseApp->IManager.xDelta = xDelta;
    pBaseApp->IManager.yDelta = yDelta;
    T8_LOG_VERBOSE("[MouseInput] mode=absolute abs=(%d,%d) client=(%d,%d) delta=(%d,%d) confined=%d relative=%d",
                   x,
                   y,
                   clientPt.x,
                   clientPt.y,
                   pBaseApp->IManager.xDelta,
                   pBaseApp->IManager.yDelta,
                   m_cursorConfined ? 1 : 0,
                   m_relativeMouseMode ? 1 : 0);

    m_lastAbsMouseX = x;
    m_lastAbsMouseY = y;
  }

  void Win32Framework::ResetApplication() {
  }

  bool Win32Framework::ResizeApplicationWindow(int width, int height) {
    if (width <= 0 || height <= 0 || !pVideoDriver) {
      return false;
    }
    aplicationDescriptor.width = width;
    aplicationDescriptor.height = height;
    if (m_pWindow && aplicationDescriptor.videoMode != t850::VideoMode::FULLSCREEN) {
      SDL_SetWindowSize(m_pWindow, width, height);
    }
    ResetInputAfterWindowStateChange();
    return pVideoDriver->ResizeSwapchain(width, height);
  }

  void Win32Framework::ChangeAPI(GraphicsApi::E api)
  {
#ifndef OS_WINDOWS
    if (api == GraphicsApi::D3D11) {
      api = GraphicsApi::OPENGL;
    }
#endif
    const GraphicsApi::E oldApi = pVideoDriver ? pVideoDriver->m_currentAPI : api;
    T8_LOG_INFO("[Framework] ChangeAPI begin %s -> %s",
                t850::config::ApiTag(oldApi),
                t850::config::ApiTag(api));
    if (m_inited) {
      ReleaseMouseMode();
      pVideoDriver->FlushGPUResources();  // release cmd buffer/descriptor refs before scene cleanup
      pBaseApp->DestroyAssets();
      MeshAssetCache::Get().Clear();
      MaterialAssetCache::Get().Clear();
      pBaseApp->resourceManager.Release();
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
      ReleaseMouseMode();
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
    t850::Log::SetSessionTag(t850::config::ApiTag(pVideoDriver->m_currentAPI));
    pVideoDriver->SetWindow(m_pWindow);
    pVideoDriver->InitDriver();
    RefreshEngineContextFromGlobals();
    pBaseApp->CreateAssets();
    // For D3D12: record where permanent descriptors end so per-frame dynamic CBVs start after them
    pVideoDriver->BuildPipelineObjects();
    T8_LOG_INFO("[Framework] ChangeAPI complete %s (%dx%d)",
                t850::config::ApiTag(pVideoDriver->m_currentAPI),
                aplicationDescriptor.width,
                aplicationDescriptor.height);
  }
}
