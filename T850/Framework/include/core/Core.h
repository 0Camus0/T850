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

#ifndef T800_CORE_H
#define T800_CORE_H

#include <Config.h>

#include <video/BaseDriver.h>
#include <utils/InputManager.h>
#include <utils/ResourceManager.h>
#include <scene/SceneProp.h>
namespace t800 {
  class RootFramework;
  class AppBase {
  public:
    AppBase() : bInited(false), bPaused(false), pFramework(0) {}
    virtual void InitVars() = 0;
    virtual void CreateAssets() = 0;
    virtual void LoadAssets() = 0;
    virtual void DestroyAssets() = 0;
    virtual void OnUpdate() = 0;
    virtual void OnDraw() = 0;
    virtual void OnInput() = 0;
    virtual void OnPause() = 0;
    virtual void OnResume() = 0;
    virtual void OnReset() = 0;

    virtual void LoadScene(int id) = 0;
    void	SetParentFramework(RootFramework* pParentFramework) {
      pFramework = pParentFramework;
    }
    bool			bInited;
    bool			bPaused;
    RootFramework	*pFramework;
    InputManager	IManager;
    ResourceManager resourceManager;
  };
  class SceneBase {
  public:
    SceneBase()  {}
    virtual ~SceneBase(){}
    virtual void OnUpdate(float _DtSecs) = 0;
    virtual void OnDraw() = 0;
    virtual void OnInput(InputManager* IManager) = 0;
    virtual void OnLoadScene() = 0;
    virtual void OnDestoryScene() = 0;
    virtual void InitVars() = 0;
    virtual void CreateAssets() = 0;
    virtual void DestroyAssets() = 0;
    SceneProps		SceneProp;
    RootFramework	*pFramework;
  };
  class RootFramework {
  public:
    RootFramework(AppBase *pApp) : pBaseApp(pApp) {}
    virtual void InitGlobalVars() = 0;
    virtual void OnCreateApplication(ApplicationDesc desc) = 0;
    virtual void OnDestroyApplication() = 0;
    virtual void OnInterruptApplication() = 0;
    virtual void OnResumeApplication() = 0;
    virtual void UpdateApplication() = 0;
    virtual void ProcessInput() = 0;
    virtual void ResetApplication() = 0;
    virtual void ChangeAPI(GRAPHICS_API::E api) = 0;

    BaseDriver		*pVideoDriver;
    AppBase			*pBaseApp;
    bool m_inited;
    ApplicationDesc aplicationDescriptor;
  };
}

#endif
