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
#ifndef OS_ANDROID
#include <core/DevLayer.h>
#endif
#include <imgui/ImGuiSystem.h>

#include <physics/JoltPhysicsSystem.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/SceneProp.h>
#include <scene/TextRenderer.h>

#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>

#include <string>
#include <memory>
#include <unordered_set>
#include <vector>

class App : public t850::AppBase {
public:
	App() : AppBase() {}
  void InitVars() override;
  void LoadAssets() override;
  void CreateAssets() override;
  void DestroyAssets() override;

  void OnUpdate() override;
  void OnDraw() override;
  void OnInput() override;

  void OnPause() override;
  void OnResume() override;
  void OnReset() override;

  void LoadScene(int id) override;
  void DrawRuntimeGui();
  bool RunOffscreenBenchmarkFastPath(float initialDtSecs);
  bool HandleRuntimeGuiToggle(const char* phase);
#ifndef OS_ANDROID
  void SubmitRuntimeGamepadGuiInput();
  void HandleRuntimeGuiPanelFocusSwitch();
#endif
#ifdef OS_ANDROID
  bool HandleAndroidInputEvent(AInputEvent* event) override;
  void OnAndroidNativeWindowChanged(ANativeWindow* window) override;
  void RegisterAndroidGuiTap(float x, float y);
  void UpdateAndroidGuiHoldToggle();
  void LoadAndroidGuiSettings();
  void SaveAndroidGuiSettings() const;
  void DrawAndroidPhysicsGui(t850::DevGuiContext& gui);
#endif

  // Modal UI state queried by the framework to block Esc-to-quit.
  bool IsModalActive() const override;
  bool WantsRelativeMouseMode() const override;


  Timer			DtTimer;
  float			DtSecs;

  Timer			FadeTimer;
  bool			FirstFrame;
  std::string m_fpsString;
  XVECTOR3 m_fpsCol = XVECTOR3(0.2f, 0.8f, 0.2f);
  t850::TextRenderer m_textRender;
  std::vector<std::unique_ptr<t850::SceneBase>> m_scenes;
  t850::SceneBase* m_actualScene = nullptr;
  t850::JoltPhysicsSystem m_physics;
  t850::ImGuiSystem m_imgui;
  bool m_imguiReady = false;
  bool m_imguiVisible = false;
#ifndef OS_ANDROID
  t850::DevLayer m_devLayer;
  bool m_debugPanelVisible = false;
  bool m_runtimeGuiGamepadFocusPending = false;
  bool m_runtimeGuiInputBlockThisFrame = false;
  int m_runtimeGuiFocusedPanelIndex = 1;
  std::unordered_set<std::string> m_debugOpenTargets;
#else
  float m_androidGuiScale = 1.6f;
  int m_androidGuiPanelMode = 0;
  int m_androidGuiTapSide = -1;
  int m_androidGuiTapCount = 0;
  float m_androidGuiTapWindowSecs = 0.0f;
  float m_androidGuiTapStartX = 0.0f;
  float m_androidGuiTapStartY = 0.0f;
  float m_androidGuiHoldSecs = 0.0f;
  float m_androidGuiHoldStartX = 0.0f;
  float m_androidGuiHoldStartY = 0.0f;
  bool m_androidGuiHoldActive = false;
  bool m_androidGuiHoldSuppressed = false;
  bool m_androidGuiUndockRequested = false;
#endif
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
