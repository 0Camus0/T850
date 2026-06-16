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

#ifndef T800_WIN32FRAMEWORK_H
#define T800_WIN32FRAMEWORK_H

#include <Config.h>

#include <core/Core.h>
#include <video/BaseDriver.h>

struct SDL_Window;
union SDL_Event;
struct SDL_GLContextState;
typedef struct SDL_GLContextState *SDL_GLContext;

#include <memory>
#include <string>
namespace t850 {
  class Win32Framework : public RootFramework {
  public:
    Win32Framework(AppBase *pBaseApp) : RootFramework(pBaseApp), m_alive(true), m_pWindow(nullptr), m_glContext(nullptr) {
      pBaseApp->SetParentFramework(this);
      m_inited = false;
    }
    void InitGlobalVars();
    void OnCreateApplication(ApplicationDesc desc);
    void OnDestroyApplication();
    void OnInterruptApplication();
    void OnResumeApplication();
    void UpdateApplication();
    void ProcessInput();
    void ResetApplication();
    void ChangeAPI(GraphicsApi::E api);
    bool ResizeApplicationWindow(int width, int height);
    ~Win32Framework() {	}

    bool	m_alive;
    SDL_Window* m_pWindow;
    SDL_GLContext m_glContext;
  private:
    void UpdateMouseMode();
    void ReleaseMouseMode();
    void ResetInputAfterWindowStateChange();
    void ResetMouseDeltaBaseline();
    void TraceWindowEvent(const SDL_Event& event);
    void InitializeGamepads();
    void ShutdownGamepads();
    void OpenGamepad(int instanceId);
    void CloseGamepad(int instanceId);
    void RefreshGamepadState();
    bool m_cursorConfined = false;
    bool m_relativeMouseMode = false;
    int m_lastAbsMouseX = 0;
    int m_lastAbsMouseY = 0;
    bool m_absMouseBaselineValid = false;
    void* m_gamepad = nullptr;
    int m_gamepadInstanceId = 0;
    bool m_handheldDetected = false;
    std::string m_handheldReason;
  };
}

#endif
