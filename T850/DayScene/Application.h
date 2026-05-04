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

#include <core/Core.h>
#include <core/DevLayer.h>
#include <imgui/ImGuiSystem.h>

#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/SceneProp.h>
#include <scene/TextRenderer.h>

#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class App : public t850::AppBase {
public:
	App() : AppBase() {}
	void InitVars();
  void LoadAssets();
	void CreateAssets();
	void DestroyAssets();

	void OnUpdate();
	void OnDraw();
	void OnInput();

	void OnPause();
	void OnResume();
	void OnReset();

  void LoadScene(int id);
  void DrawRuntimeGui();

  // Modal state (DevLayer's GUI popup) — queried by the framework to block Esc-to-quit.
  bool IsModalActive() const override;


  Timer			DtTimer;
  float			DtSecs;

  Timer			FadeTimer;
  bool			FirstFrame;
  std::string m_fpsString;
  XVECTOR3 m_fpsCol = XVECTOR3(0.2f, 0.8f, 0.2f);
  t850::TextRenderer m_textRender;
  std::vector<t850::SceneBase*> m_scenes;
  t850::SceneBase* m_actualScene;
  t850::DevLayer m_devLayer;
  t850::ImGuiSystem m_imgui;
  bool m_imguiReady = false;
  bool m_imguiVisible = false;
  bool m_debugPanelVisible = false;
  std::unordered_set<std::string> m_debugOpenTargets;
  std::unordered_map<void*, uintptr_t> m_debugTextureDescriptors;
  std::unordered_map<void*, uintptr_t> m_debugOpaqueTextureDescriptors;
  t850::PrimitiveInst	Quads[10];
  t850::PrimitiveManager PrimitiveMgr;
  XMATRIX44 VP;
  SceneProps		SceneProp;
  Camera			Cam;

  bool fading;
  bool fadeOut;
  float totalFadeTime;

  float _fadeTime = 0.5f;
  inline void FadeFX(float time, bool out) {
    FadeTimer.Init();
    fading = true;
    fadeOut = out;
    totalFadeTime = 0;
    _fadeTime = time;
    while (totalFadeTime <= _fadeTime) {
      FadeTimer.Update();
      float fadeSecsElapsed = FadeTimer.GetDTSecs();
      totalFadeTime += fadeSecsElapsed;
      OnUpdate();
    }
    fading = false;
    Quads[0].SetBrightness(1.0);
  }
};
