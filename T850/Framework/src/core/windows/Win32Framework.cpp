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
#include <iomanip>
#include <filesystem>


std::string GetDate() {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    using namespace std::chrono;

    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(&tm, "%d_%m_%Y_%H_%M_%S");
    oss << '_' << std::setfill('0') << std::setw(3) << ms.count();
    auto str = oss.str();
    return str;
}

std::string GetDir() {
    char NPath[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, NPath);   
    string dir = NPath;
    return dir;
}

void RunProcess(std::string process,std::string arguments) {
    PROCESS_INFORMATION ProcessInfo;

    STARTUPINFO StartupInfo; 
    
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof StartupInfo; 

    if (CreateProcess(process.c_str(), (LPSTR)arguments.c_str(),
        NULL, NULL, FALSE, 0, NULL,
        NULL, &StartupInfo, &ProcessInfo))
    {
        WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);

        printf("\n\nProcess %s Launched Successfully\n\n", process.c_str());
    }
    else
    {
        printf("\n\nCannot Launch Process %s\n\n", process.c_str());
    }
}

void EnableGuC(t800::ApplicationDesc desc) {
    int r = remove("c:\\UkLog.dat");
    printf("Deleting UkLog.dat with status [%d]\n\n", r);
    std::string args = "glc.exe -e ";
    args += std::to_string(desc.gucenableparam);
    args += " -v ";
    args += std::to_string(desc.gucverbosity);
    RunProcess("glc.exe", args);
}

void DisableGuC() {
    RunProcess("glc.exe", "glc.exe -d");
    std::string fname = GetDir();
    fname += "\\UkLog_";
    fname += GetDate();
    fname += ".dat";
    
    int tries = 0;
    while (!(CopyFile("c:\\UkLog.dat", fname.c_str(), FALSE) != 0)) {
        Sleep(500);
        cout << "\nTrying to copy again [" << fname.c_str() << "]\n" << endl;    
        tries++;
        if (tries > 5) {
            cout << "\n\nCannot copy the log for some reason, max 5 tries, I am done.\n\n" << endl;
            return;
        }
    }
    cout << "\n\nGuC Log copied successfully\n\n" << endl;
}

namespace t800 {
  void Win32Framework::InitGlobalVars() {

     

  }

  void Win32Framework::OnCreateApplication(ApplicationDesc desc) {
    aplicationDescriptor = desc;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      printf("Video initialization failed: %s\n", SDL_GetError());
    }    
    if (aplicationDescriptor.enableguclogging) {
        EnableGuC(aplicationDescriptor);
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
    if (aplicationDescriptor.enableguclogging) {
        DisableGuC();
    }
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
