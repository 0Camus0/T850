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
 // m_scenes.push_back(new SC_Night());
 // m_scenes.push_back(new SC_Tech());
  for (auto &it : m_scenes) {
    it->pFramework = pFramework;
    //it->InitVars();
  }
  m_actualScene = m_scenes[0];
  m_actualScene->InitVars();


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
  FadeFX(0.5,false);
}

void App::LoadAssets()
{
}

void App::CreateAssets() {
  m_actualScene->CreateAssets();
  m_textRender.LoadFromFile(36,"Fonts/tahomabd.ttf",512.0f);
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);
  PrimitiveMgr.SetSceneProps(&SceneProp);
  Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &VP);
  FadeFX(0.5, false);
}

void App::DestroyAssets() {
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
   m_actualScene->OnUpdate(DtSecs);
   
   OnInput();
   OnDraw();
}

void App::OnDraw() {
  pFramework->pVideoDriver->Clear();
  FirstFrame = false;
  m_actualScene->OnDraw();
  m_textRender.Draw(-0.9f, -0.8f, m_fpsCol, m_fpsString);
  if (fading) {
    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::READ);
    //Fade
    Quads[0].SetGlobalSignature(Signature::FADE_PASS);
    if (fadeOut)
      Quads[0].SetBrightness(totalFadeTime / _fadeTime);
    else
      Quads[0].SetBrightness(1.0f-totalFadeTime / _fadeTime);
    Quads[0].Draw();
    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
  }

  pFramework->pVideoDriver->SwapBuffers();
}



void App::OnInput() {
	if (FirstFrame)
		return;
  m_actualScene->OnInput(&IManager);
}

void App::OnPause() {

}

void App::OnResume() {

}

void App::OnReset() {

}
