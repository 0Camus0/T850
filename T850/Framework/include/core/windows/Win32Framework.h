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


#include <memory>
namespace t800 {
  class Win32Framework : public RootFramework {
  public:
    Win32Framework(AppBase *pBaseApp) : RootFramework(pBaseApp) {
      pBaseApp->SetParentFramework(this);
      m_inited = false;
      m_alive = true;
    }
    void InitGlobalVars();
    void OnCreateApplication(ApplicationDesc desc);
    void OnDestroyApplication();
    void OnInterruptApplication();
    void OnResumeApplication();
    void UpdateApplication();
    void ProcessInput();
    void ResetApplication();
    void ChangeAPI(GRAPHICS_API::E api);
    ~Win32Framework() {	}

  private:
  };
}

#endif
