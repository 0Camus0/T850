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

#include "Application.h"
#include <video/BaseDriver.h>
#include <utils/InputManager.h>
#include <utils/Log.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(OS_LINUX)
#include <sys/time.h>
#endif

#include <iostream>
#include <string>
#include <vector>

using namespace t800;
extern std::vector<std::string> g_args;
extern int g_startScene;
extern bool g_guiOnStart;
extern bool g_guiScreenshot;
extern std::string g_guiScreenshotPath;
extern bool g_guiEdit;
extern bool g_guiSnap;
extern bool g_guiControlEdit;
extern std::string g_guiControlTarget;



#include "SC_Day.h"
#include "SC_Night.h"
#include "SC_Tech.h"
void App::InitVars() {
  //t800::T8Technique tech("Techniques/test_technique.xml");
	DtTimer.Init();
	DtTimer.Update();
	srand((unsigned int)DtTimer.GetDTSecs());
  FirstFrame = true;

  m_scenes.push_back(new SC_Day());
  m_scenes.push_back(new SC_Night());
  m_scenes.push_back(new SC_Tech());
  for (auto &it : m_scenes) {
    it->pFramework = pFramework;
    //it->InitVars();
  }
  int sceneIdx = (g_startScene >= 0 && g_startScene < (int)m_scenes.size()) ? g_startScene : 0;
  m_actualScene = m_scenes[sceneIdx];
  m_actualScene->InitVars();

  m_devLayer.Init(pFramework);
  m_devLayer.SetActiveScene(m_actualScene);

  Cam.InitPerspective(XVECTOR3(0.0f, 1.0f, 10.0f), Deg2Rad(46.8f), 1280.0f / 720.0f, 2.0f, 12000.0f);
  Cam.Speed = 10.0f;
  Cam.Eye = XVECTOR3(0.0f, 9.75f, -31.0f);
  Cam.Pitch = 0.14f;
  Cam.Roll = 0.0f;
  Cam.Yaw = 0.020f;
  Cam.Update(0.0f);
  SceneProp.AddCamera(&Cam);
  fading = false;
}

void App::LoadScene(int id) {
  if (m_actualScene != nullptr) {
    FadeFX(0.5, true);
    m_actualScene->OnDestoryScene();
  }

  m_actualScene = m_scenes[id];
  m_actualScene->OnLoadScene();
  m_devLayer.SetActiveScene(m_actualScene);
  m_devLayer.RebuildGUIForScene();
  FadeFX(0.5,false);
}

void App::LoadAssets()
{
}

void App::CreateAssets() {
  m_actualScene->CreateAssets();
  m_textRender.LoadFromFile(36,"Fonts/Martius-LV9L4.ttf",512.0f);
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);
  PrimitiveMgr.SetSceneProps(&SceneProp);
  Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &VP);

  // Build GUI before FadeFX so it's visible during fade frames
  m_devLayer.RebuildGUIForScene();
  if (g_guiOnStart) {
    m_devLayer.GetGUI().SetVisible(true);
  }
  if (g_guiEdit) {
    m_devLayer.SetEditMode(true);
  }
  if (g_guiControlEdit) {
    m_devLayer.SetControlEditMode(true);
    if (!m_devLayer.SetControlEditTargetByName(g_guiControlTarget)) {
      T8_LOG_ERROR("[App] Unknown gui control target '%s' (expected slider_knob|selector_control|checkbox_mark)", g_guiControlTarget.c_str());
    }
  }
  if (g_guiSnap) {
    m_devLayer.SetSnapToGrid(true);
  }

  // Skip fade when doing an automated screenshot
  if (!g_guiScreenshot) {
    FadeFX(0.5, false);
  }
}

void App::DestroyAssets() {
   m_devLayer.Destroy();
   m_textRender.Destroy(); 
   m_actualScene->DestroyAssets();
}

void App::OnUpdate() {
   DtTimer.Update();
   DtSecs = DtTimer.GetDTSecs();
  if (FirstFrame) {
    DtSecs = 1.0f / 60.0f;
  }
   static float timeAccum = 0;
   timeAccum += DtSecs;
  
   if (timeAccum > 1.0) {
     m_fpsString = "FPS " + std::to_string((int)(1.0 / DtSecs));
     m_fpsCol = XVECTOR3(0.2, 0.8, 0.2);
     timeAccum = 0;
   }
   // Feed FPS text to GUI so it can be displayed as a movable element
   m_devLayer.GetGUI().SetFPSText(m_fpsString, m_fpsCol);
   m_devLayer.Update(DtSecs);
   
   OnInput();
   OnDraw();
}

void App::OnDraw() {
  static int frameCount = 0;
  T8_LOG_TRACE("[Frame %d] === OnDraw BEGIN ===", frameCount);
  pFramework->pVideoDriver->Clear();
  FirstFrame = false;
  m_devLayer.Draw();
  // Draw FPS label using layout position when GUI overlay is not visible
  if (!m_devLayer.GetGUI().IsVisible()) {
    T8_LOG_TRACE("[Frame %d] DrawFPSOnly (GUI hidden)", frameCount);
    m_devLayer.GetGUI().DrawFPSOnly();
  }
  if (fading) {
    T8_LOG_TRACE("[Frame %d] Fade quad draw", frameCount);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::READ);
    //Fade
    ShaderKey fadeKey(0); fadeKey.setPass(PassType::FADE); fadeKey.bits |= ShaderKey::HAS_TEXCOORD0;
    Quads[0].SetGlobalKey(fadeKey);
    if (fadeOut)
      Quads[0].SetBrightness(totalFadeTime / _fadeTime);
    else
      Quads[0].SetBrightness(1.0f-totalFadeTime / _fadeTime);
    Quads[0].Draw();
    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
  }

  // --guiScreenshot: after a few frames (let scene stabilise), save backbuffer and exit
  if (g_guiScreenshot && frameCount >= 3) {
    pFramework->pVideoDriver->SaveScreenshot(g_guiScreenshotPath);
    printf("[guiScreenshot] Saved to %s\n", g_guiScreenshotPath.c_str());
    pFramework->pVideoDriver->SwapBuffers();
    exit(0);
  }
  frameCount++;

  // Skip presenting the first frame (black with only text)
  if (frameCount > 1) {
    T8_LOG_TRACE("[Frame %d] === SwapBuffers ===" , frameCount);
    pFramework->pVideoDriver->SwapBuffers();
  } else {
    T8_LOG_TRACE("[Frame %d] === SKIPPED SwapBuffers (first frame) ===" , frameCount);
  }
}



void App::OnInput() {
	if (FirstFrame)
		return;
  m_devLayer.ProcessInput(&IManager);
}

void App::OnPause() {

}

void App::OnResume() {

}

void App::OnReset() {

}

bool App::IsModalActive() const {
  return m_devLayer.GetGUI().IsPopupActive();
}
