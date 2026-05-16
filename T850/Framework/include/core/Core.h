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

#ifdef OS_ANDROID
struct AInputEvent;
#endif

namespace t850 {
  class GUIManager;   // forward
  class DevGuiContext; // forward
  struct EngineContext;
  class RootFramework;
  class AppBase {
  public:
    AppBase() : bInited(false), bPaused(false), pFramework(0) {}
    virtual ~AppBase() = default;
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

    // Return true if the app is currently showing a modal UI (e.g. a line-edit popup).
    // Frameworks use this to suppress global keys like Escape-to-quit while modal.
    virtual bool IsModalActive() const { return false; }
#ifdef OS_ANDROID
    virtual bool HandleAndroidInputEvent(AInputEvent* /*event*/) { return false; }
#endif

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
    SceneBase() : pFramework(nullptr), pEngineContext(nullptr) {}
    virtual ~SceneBase(){}
    virtual void OnUpdate(float _DtSecs) = 0;
    virtual void OnDraw() = 0;
    virtual void OnInput(InputManager* IManager) = 0;
    virtual void OnLoadScene() = 0;
    virtual void OnDestoryScene() = 0;
    virtual void InitVars() = 0;
    virtual void CreateAssets() = 0;
    virtual void DestroyAssets() = 0;

    // GUI hooks – override in scene to participate in DevLayer GUI
    virtual void PopulateGUI(GUIManager& /*gui*/) {}
    virtual void SyncToGUI(GUIManager& /*gui*/)   {}   // scene props → sliders
    virtual void SyncFromGUI(GUIManager& /*gui*/) {}   // sliders → scene props
    virtual void DrawDevGui(DevGuiContext& /*gui*/) {}

    // Dump current scene state back to its JSON file
    virtual void SaveSceneState() {}

    // Request a frame dump (spacebar snapshot)
    virtual void RequestDump() {}

    void SetEngineContext(EngineContext* context) { pEngineContext = context; }
    EngineContext* GetEngineContext() const { return pEngineContext; }

    SceneProps		SceneProp;
    RootFramework	*pFramework;
    EngineContext* pEngineContext;
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
    virtual void ChangeAPI(GraphicsApi::E api) = 0;

    BaseDriver		*pVideoDriver;
    AppBase			*pBaseApp;
    bool m_inited;
    ApplicationDesc aplicationDescriptor;
  };
}

#endif
