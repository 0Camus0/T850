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

struct SDL_Window;
union SDL_Event;

#include <memory>
#include <string>
namespace t850 {
class LinuxFramework : public RootFramework {
public:
	LinuxFramework(AppBase *pBaseApp);
  ~LinuxFramework() override;
	void InitGlobalVars();
	void OnCreateApplication(ApplicationDesc desc);
	void OnDestroyApplication();
	void OnInterruptApplication();
	void OnResumeApplication();
	void UpdateApplication();
	void ProcessInput();
	void ResetApplication();
  void ChangeAPI(GraphicsApi::E api);

	bool	m_alive;
  SDL_Window* m_pWindow;

	static LinuxFramework* thiz;
private:
  void ResetInputAfterWindowStateChange();
  void InitializeGamepads();
  void ShutdownGamepads();
  void OpenGamepad(int instanceId);
  void CloseGamepad(int instanceId);
  void RefreshGamepadState();

  void* m_gamepad = nullptr;
  int m_gamepadInstanceId = 0;
  bool m_handheldDetected = false;
  std::string m_handheldReason;
 };
}


#endif // LINUXFRAMEWORK_H_INCLUDED
