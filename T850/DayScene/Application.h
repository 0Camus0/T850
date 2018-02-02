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

#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/SceneProp.h>
#include <scene/T8_TextRenderer.h>

#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>

#include <vector>

class App : public t800::AppBase {
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


  Timer			DtTimer;
  float			DtSecs;

  Timer			FadeTimer;
  bool			FirstFrame;
  std::string m_fpsString;
  XVECTOR3 m_fpsCol;
  t800::TextRenderer m_textRender;
  std::vector<t800::SceneBase*> m_scenes;
  t800::SceneBase* m_actualScene;
  t800::PrimitiveInst	Quads[10];
  t800::PrimitiveManager PrimitiveMgr;
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
