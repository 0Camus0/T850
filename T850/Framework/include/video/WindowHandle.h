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

#ifndef T800_WINDOW_HANDLE_H
#define T800_WINDOW_HANDLE_H

#include <Config.h>

// Lightweight, backend-neutral description of the OS window the renderer
// should target. Lets editor hosts (Win32 child HWND, future Qt/WPF hosts)
// hand a native handle directly to BaseDriver instead of an SDL_Window*.
//
// Phase 0 of the editor plan (see EDITOR.md): keep the existing SDL path
// working untouched while making the typed HWND path a first-class option.
//
// This header intentionally does NOT include <Windows.h> — we use void* to
// keep the Framework public headers portable to OS_LINUX. Backends cast to
// HWND in their .cpp files.

namespace t850 {

  struct WindowHandle {
    enum Kind {
      NONE = 0,
      SDL_WINDOW,    // sdlWindow points to an SDL_Window
      WIN32_HWND     // nativeHandle is an HWND
    };

    Kind  kind;
    void* sdlWindow;     // valid when kind == SDL_WINDOW
    void* nativeHandle;  // valid when kind == WIN32_HWND (HWND on Windows)

    WindowHandle()
      : kind(NONE), sdlWindow(nullptr), nativeHandle(nullptr) {}

    static WindowHandle FromSDL(void* sdlWnd) {
      WindowHandle h;
      h.kind = SDL_WINDOW;
      h.sdlWindow = sdlWnd;
      return h;
    }

    static WindowHandle FromHWND(void* hwnd) {
      WindowHandle h;
      h.kind = WIN32_HWND;
      h.nativeHandle = hwnd;
      return h;
    }
  };

}

#endif
