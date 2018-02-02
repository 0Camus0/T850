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

#include <Config.h>
#ifdef OS_LINUX
    #include <core/LinuxFramework.h>
#elif defined(OS_WINDOWS)
	#include <core/windows/Win32Framework.h>
#endif

#include "Application.h"

#include <iostream>
#include <string>
#include <vector>

#include <T8_descriptors.h>

std::vector<std::string> g_args;

t800::AppBase		  *pApp = 0;
t800::RootFramework *pFrameWork = 0;

int main(int arg,char ** args){
  t800::ApplicationDesc desc;
  desc.api = t800::GRAPHICS_API::OPENGL;
  desc.height = 720;
  desc.width = 1280;
  desc.videoMode = t800::T8_VIDEO_MODE::WINDOWED;
  desc.title = "T800 Project";

    for(int i=0;i<arg;i++){
        g_args.push_back( std::string( args[i] ) );
    }

	pApp = new App;
#ifdef OS_LINUX
    pFrameWork = new t800::LinuxFramework((t800::AppBase*)pApp);
    pFrameWork->InitGlobalVars();
	pFrameWork->OnCreateApplication(desc);
#elif defined(OS_WINDOWS)
	pFrameWork = new t800::Win32Framework((t800::AppBase*)pApp);
	pFrameWork->InitGlobalVars();
	pFrameWork->OnCreateApplication(desc);
	pFrameWork->UpdateApplication();
	pFrameWork->OnDestroyApplication();
#endif

	delete pFrameWork;
	delete pApp;

    return 0;
}

