#include "SC_Night.h"
using namespace t800;
#define SEPARATED_BLUR 1
#define DEGENERATED_FBO_TEST 0

#define NUM_LIGHTS 1
#define RADI 170.0f

#define HIGHQ 1
#define MEDIUMQ 2
#define LOWQ 3

#define QUALITY_SELECTED LOWQ

#if   QUALITY_SELECTED == HIGHQ
#define MAX_QUALITY
#elif QUALITY_SELECTED == MEDIUMQ
#define MEDIUM_QUALITY
#elif QUALITY_SELECTED == LOWQ
#define LOW_QUALITY 
#endif

void SC_Night::InitVars() {
  Position = XVECTOR3(0.0f, 0.0f, 0.0f);
  Orientation = XVECTOR3(0.0f, 0.0f, 0.0f);
  Scaling = XVECTOR3(1.0f, 1.0f, 1.0f);
  SelectedMesh = 0;

  CamSelection = NORMAL_CAM1;
  SceneSettingSelection = CHANGE_EXPOSURE;

  Cam.InitPerspective(XVECTOR3(0.0f, 1.0f, 10.0f), Deg2Rad(46.8f), 1280.0f / 720.0f, 2.0f, 12000.0f);
  Cam.Speed = 10.0f;
  Cam.Eye = XVECTOR3(0.0f, 9.75f, -31.0f);
  Cam.Pitch = 0.14f;
  Cam.Roll = 0.0f;
  Cam.Yaw = 0.020f;
  Cam.Update(0.0f);

  LightCam.InitPerspective(XVECTOR3(0.0f, 100.0f, 10.0f), Deg2Rad(45.0f), 1.0f, 110.0f, 280.0f);
  LightCam.Speed = 10.0f;
  LightCam.Eye = XVECTOR3(64.0f, 205.0f, 0.0f);
  LightCam.Pitch = 1.310797f;
  LightCam.Roll = 0.0f;
  LightCam.Yaw = -1.569204f;
  LightCam.Update(0.0f);

  ActiveCam = &Cam;

  SceneProp.AddCamera(ActiveCam);
  SceneProp.AddLightCamera(&LightCam);
  omniLightPos = XVECTOR3(-10.0f, 10.0f, 0.0f);
  for (size_t i = 0; i < 6; i++)
  {
    OmniLightCam[i].InitPerspective(XVECTOR3(0.0f, 10.0f, 10.0f), Deg2Rad(90.0f), 1.0f, 0.1f, 300.0f);
    OmniLightCam[i].Speed = 0.0f;
    OmniLightCam[i].Eye = omniLightPos;
    OmniLightCam[i].Update(0.0f);
    SceneProp.AddLightCamera(&OmniLightCam[i]);
  }
  //Right
  OmniLightCam[0].SetLookAt(OmniLightCam[0].Eye + XVECTOR3(10,0,0));
  OmniLightCam[0].Update(0.0f);
  //Left
  OmniLightCam[1].SetLookAt(OmniLightCam[1].Eye + XVECTOR3(-10, 0, 0));
  OmniLightCam[1].Update(0.0f);
  //Top
  OmniLightCam[2].SetLookAt(OmniLightCam[2].Eye + XVECTOR3(0, -10, 0));
  OmniLightCam[2].Update(0.0f);
  //Bot
  OmniLightCam[3].SetLookAt(OmniLightCam[3].Eye + XVECTOR3(0, 10, 0));
  OmniLightCam[3].Update(0.0f);
  //Near
  OmniLightCam[4].SetLookAt(OmniLightCam[4].Eye + XVECTOR3(0, 0, 10));
  OmniLightCam[4].Update(0.0f);
  //Far
  OmniLightCam[5].SetLookAt(OmniLightCam[5].Eye + XVECTOR3(0, 0, -10));
  OmniLightCam[5].Update(0.0f);

  SceneProp.AddLight(XVECTOR3(1000, 2000, 0), XVECTOR3(0.1215, 0.1607, 0.2090), 30000, true);
  SceneProp.AddLight(omniLightPos, XVECTOR3(1.0, 0.57, 0.16), 30, true);
  SceneProp.ActiveLights = 120;
  for (int i = 0; i < SceneProp.ActiveLights-2; ++i) {
    /*SceneProp.AddLight(XVECTOR3(-200 + i*4, 4, -200 + i * 4), XVECTOR3(1.0, 0.57, 0.16), 5, true);*/
    float r = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
    float g = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
    float b = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
    SceneProp.AddLight(XVECTOR3(0.0f, 15.0f, 0.0f), XVECTOR3(r, g, b), 10, true);
  }

  SceneProp.AmbientColor = XVECTOR3(0.8f, 0.8f, 0.8f);

  SceneProp.SSAOKernel.Radius = 2.5f;
  SceneProp.SSAOKernel.KernelSize = 16;
  SceneProp.SSAOKernel.Update();

  ShadowFilter.kernelSize = 7;
  ShadowFilter.radius = 2.5f;
  ShadowFilter.sigma = 2.0f;
  ShadowFilter.Update();

  BloomFilter.kernelSize = 11;
  BloomFilter.radius = 2.5f;
  BloomFilter.sigma = 4.5f;
  BloomFilter.Update();


  NearDOFFilter.kernelSize = 23;
  NearDOFFilter.radius = 3.0f;
  NearDOFFilter.sigma = 6.f;
  NearDOFFilter.Update();

  SceneProp.Aperture = 120;
  SceneProp.FocalLength = 50;
  SceneProp.MaxCoc = 2.5;
#ifdef  MAX_QUALITY
  SceneProp.DOF_Near_Samples_squared = 2.0f;
  SceneProp.DOF_Far_Samples_squared = 4.0f;
  SceneProp.ShadowMapResolution = 4096.0f;
  SceneProp.PCFScale = 2.1f;
  SceneProp.PCFSamples = 5.0f;
  SceneProp.ParallaxLowSamples = 50.0f;
  SceneProp.ParallaxHighSamples = 300;
  SceneProp.ParallaxHeight = 0.03f;
  SceneProp.LightVolumeSteps = 128.0f;
#elif defined(MEDIUM_QUALITY)
  SceneProp.DOF_Near_Samples_squared = 1.0f;
  SceneProp.DOF_Far_Samples_squared = 3.0f;
  SceneProp.ShadowMapResolution = 2048.0f;
  SceneProp.PCFScale = 2.1f;
  SceneProp.PCFSamples = 3.0f;
  SceneProp.ParallaxLowSamples = 10.0f;
  SceneProp.ParallaxHighSamples = 18.0f;
  SceneProp.ParallaxHeight = 0.02f;
  SceneProp.LightVolumeSteps = 96.0f;
#elif defined(LOW_QUALITY)
  SceneProp.DOF_Near_Samples_squared = 1.0f;
  SceneProp.DOF_Far_Samples_squared = 2.0f;
  SceneProp.ShadowMapResolution = 1024.0f;
  SceneProp.PCFScale = 1.7f;
  SceneProp.PCFSamples = 1.0f;
  SceneProp.ParallaxLowSamples = 2.0f;
  SceneProp.ParallaxHighSamples = 8.0f;
  SceneProp.ParallaxHeight = 0.01f;
  SceneProp.LightVolumeSteps = 32.0f;
#endif
  SceneProp.AutoFocus = true;



  SceneProp.AddGaussKernel(&ShadowFilter);
  SceneProp.AddGaussKernel(&BloomFilter);
  SceneProp.AddGaussKernel(&NearDOFFilter);
  SceneProp.ActiveGaussKernel = SHADOW_KERNEL;
  ChangeActiveGaussSelection = SHADOW_KERNEL;

  RTIndex = -1;
  m_spline.m_points.push_back(SplinePoint(40, 6, 0));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(50, 4, 20));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(40, 4, 20));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(0, 4, 20));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(-50, 3, 20));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(-50, 6, 0));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(-20, 7, 0));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(0, 10, 0)); //
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(40, 10, 3));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(55, 10, 5));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(50, 10, 0));
  m_spline.m_points.back().m_velocity = 3;
  m_spline.m_points.push_back(SplinePoint(55, 10, 25));
  m_spline.m_points.back().m_velocity = 3;
  m_spline.m_points.push_back(SplinePoint(35, 10, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-15, 10, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-25, 10, 20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-50, 10, 20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-50, 10, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(50, 10, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(50, 10, 0));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(30, 10, 0));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(0, 5, 0));
  m_spline.m_points.back().m_velocity = 10;

  m_spline.m_looped = false;
  m_spline.Init();

  m_agent.m_pSpline = &m_spline;
  m_agent.m_moving = true;
  m_agent.m_velocity = 15.0f;

  m_lightAgent.m_pSpline = &m_spline;
  m_lightAgent.m_moving = true;
  m_lightAgent.m_velocity = 15.0f;
  
}
void SC_Night::CreateAssets() {
  //Create Cube map
  pFramework->pVideoDriver->CreateCubeMap(nullptr, 256, 256);

  //Create RT's
  GBufferPass = pFramework->pVideoDriver->CreateRT(4, BaseRT::RGBA8, BaseRT::F32, 0, 0, true);
  DeferredPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA16F, BaseRT::NOTHING, 0, 0, true);
  Extra16FPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA16F, BaseRT::NOTHING, 0, 0, true);
  DepthPass = pFramework->pVideoDriver->CreateRT(0, BaseRT::NOTHING, BaseRT::F32, (int)SceneProp.ShadowMapResolution, (int)SceneProp.ShadowMapResolution, false);
  ShadowAccumPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 0, 0, true);
  ExtraHelperPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 0, 0, true);
  BloomAccumPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 512, 512, true);
  GodRaysCalcPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 512, 512, true);
  CoCPass = pFramework->pVideoDriver->CreateRT(2, BaseRT::F16, BaseRT::NOTHING, 512, 512, true);
  CombineCoCPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::F16, BaseRT::NOTHING, 512, 512, true);
  CoCHelperPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::F16, BaseRT::NOTHING, 512, 512, false);
  CoCHelperPass2 = pFramework->pVideoDriver->CreateRT(1, BaseRT::F16, BaseRT::NOTHING, 512, 512, false);
  OmniShadowDepthPass = pFramework->pVideoDriver->CreateRT(0, BaseRT::NOTHING, BaseRT::CUBE_F32, 1024, 1024, false);
  //OmniShadowCubePass = pFramework->pVideoDriver->CreateRT(6, BaseRT::F32, BaseRT::NOTHING, 512, 512, true);
  //
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);
  SceneProp.SSAOKernel.InitTexture();

  EnvMapTexIndex = g_pBaseDriver->CreateTexture(string("CubeMap_Mountains.dds"));

  int index = PrimitiveMgr.CreateMesh("Models/SkyBox.X");
  Meshes[1].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
  Meshes[1].TranslateAbsolute(0.0, -10.0f, 0.0f);
  Meshes[1].Update();

  index = PrimitiveMgr.CreateMesh("Models/SponzaEsc.X");
  Meshes[0].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);

  index = PrimitiveMgr.CreateMesh("Models/NuCroc.X");
  Meshes[2].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
  Meshes[2].TranslateAbsolute(omniLightPos.x, omniLightPos.y, omniLightPos.z);
  Meshes[2].ScaleAbsolute(0.01);
  Meshes[2].Update();

  index = PrimitiveMgr.CreateSpline(m_spline);
  splineWire = (SplineWireframe*)PrimitiveMgr.GetPrimitive(index);
  splineInst.CreateInstance(splineWire, &VP);
  m.Identity();
  Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[0], 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[1], 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[2], 2);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[3], 3);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->pDepthTexture, 4);
  Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));

  Quads[1].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[2].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[3].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[4].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[5].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[6].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[7].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);

  PrimitiveMgr.SetSceneProps(&SceneProp);

  m_agent.m_actualPoint = m_spline.GetPoint(m_spline.GetNormalizedOffset(0));
  ActiveCam->AttachAgent(m_agent);
  ActiveCam->m_lookAtCenter = false;

  Quads[0].TranslateAbsolute(0.0f, 0.0f, 0.0f);
  Quads[0].Update();

  Quads[1].ScaleAbsolute(0.25);
  Quads[1].TranslateAbsolute(-0.75f, 0.75f, 0.0f);
  Quads[1].Update();

  Quads[2].ScaleAbsolute(0.25f);
  Quads[2].TranslateAbsolute(0.75f, +0.75f, 0.0f);
  Quads[2].Update();

  Quads[3].ScaleAbsolute(0.25f);
  Quads[3].TranslateAbsolute(-0.75f, -0.75f, 0.0f);
  Quads[3].Update();

  Quads[4].ScaleAbsolute(0.25f);
  Quads[4].TranslateAbsolute(0.75f, -0.75f, 0.0f);
  Quads[4].Update();

  Quads[5].ScaleAbsolute(0.25f);
  Quads[5].TranslateAbsolute(0.75f, 0.0f, 0.0f);
  Quads[5].Update();

  Quads[6].ScaleAbsolute(0.25f);
  Quads[6].TranslateAbsolute(-0.75f, 0.0f, 0.0f);
  Quads[6].Update();

  Quads[7].ScaleAbsolute(1.0f);
  Quads[7].TranslateAbsolute(0.0f, 0.0f, 0.1f);
  Quads[7].Update();
}

void SC_Night::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void SC_Night::OnDestoryScene() {
  DestroyAssets();
}

void SC_Night::DestroyAssets() {
  PrimitiveMgr.DestroyPrimitives();
}

void SC_Night::OnUpdate(float _DtSecs) {
  static float totalTime = 0.0f;
  totalTime += _DtSecs;
  DtSecs = _DtSecs;
  Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);
  m_agent.Update(DtSecs);
  m_lightAgent.Update(DtSecs + 0.02);
  omniLightPos = m_lightAgent.m_actualPoint;
  ActiveCam->Update(DtSecs);
  VP = ActiveCam->VP;

  float speed = 0.5f;
  static float freq = 0.0f;
  freq += DtSecs*speed;
  static float freq2 = 3.1415f / 2.0f;
  freq2 += DtSecs*speed;

  float dist = RADI;
  float Offset = 2.0f*3.1415f / SceneProp.ActiveLights;
  float Offset2 = 4.0f*3.1415f / SceneProp.ActiveLights;
  for (int i = 2; i<SceneProp.ActiveLights; i++) {
    SceneProp.Lights[i].Position = Position;
    float RadA = dist*0.35f + dist*0.4f * sin(freq + float(i*Offset))*cos(freq + float(i*Offset));
    float RadB = dist*0.35f + dist*0.4f * sin(freq2 + float(i*Offset2))*cos(freq2 + float(i*Offset2));
    SceneProp.Lights[i].Position.x += RadA*sin(freq + float(i*Offset));
    SceneProp.Lights[i].Position.z += RadB*cos(freq + float(i*Offset2));
    SceneProp.Lights[i].Position.y += 10;
  }
  for (size_t i = 0; i < 6; i++)
  {
    OmniLightCam[i].Eye = omniLightPos;
    OmniLightCam[i].Update(0.0f);
  }
  Meshes[2].TranslateAbsolute(omniLightPos.x, omniLightPos.y,omniLightPos.z);
  Meshes[2].Update();
  SceneProp.Lights[1].Position = omniLightPos;
  if (totalTime > 60.0f) {
    totalTime = 0.0;
    pFramework->pBaseApp->LoadScene(2);
  }

}

void SC_Night::OnInput(InputManager* IManager) {
  bool changed = false;
  const float speedFactor = 10.0f;
  if (IManager->PressedKey(T800K_UP)) {
    Position.y += 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_DOWN)) {
    Position.y -= 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_LEFT)) {
    Position.x -= 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_RIGHT)) {
    Position.x += 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_i)) {
    omniLightPos.y += 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_k)) {
    omniLightPos.y -= 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_j)) {
    omniLightPos.x -= 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_l)) {
    omniLightPos.x += 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_z)) {
    Position.z -= 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_x)) {
    Position.z += 1.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedOnceKey(T800K_KP_PLUS)) {
    ChangeSettingsOnPlus();
  }

  if (IManager->PressedOnceKey(T800K_KP_MINUS)) {

    ChangeSettingsOnMinus();
  }

  if (IManager->PressedOnceKey(T800K_b)) {
    SceneSettingSelection--;
    if (SceneSettingSelection < 0) {
      SceneSettingSelection = CHANGE_MAX_NUM_OPTIONS - 1;
    }

    printCurrSelection();
  }

  if (IManager->PressedOnceKey(T800K_n)) {
    SceneSettingSelection++;
    if (SceneSettingSelection == CHANGE_MAX_NUM_OPTIONS) {
      SceneSettingSelection = CHANGE_EXPOSURE;
    }

    printCurrSelection();
  }

  if (IManager->PressedKey(T800K_KP5)) {
    Orientation.x -= 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP6)) {
    Orientation.x += 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP2)) {
    Orientation.y -= 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP3)) {
    Orientation.y += 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP0)) {
    Orientation.z -= 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP_PERIOD)) {
    Orientation.z += 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  if (IManager->PressedKey(T800K_KP_PERIOD)) {
    Orientation.z += 60.0f*speedFactor*DtSecs;
    changed = true;
  }

  bool displayInfo = false;
  if (changed && displayInfo) {
    printf("Position[%f,%f,%f] Rot[%f,%f,%f] Sc[%f]\n", Position.x, Position.y, Position.z, Orientation.x, Orientation.y, Orientation.z, Scaling.x);
  }

  if (IManager->PressedOnceKey(T800K_p)) {
    printf("Position[%f, %f, %f]\n\n", ActiveCam->Eye.x, ActiveCam->Eye.y, ActiveCam->Eye.z);
    printf("Orientation[%f, %f, %f]\n\n", ActiveCam->Pitch, ActiveCam->Roll, ActiveCam->Yaw);
  }


  if (IManager->PressedOnceKey(T800K_c)) {
    if (ActiveCam == (&Cam)) {
      ActiveCam = &LightCam;
    }
    else {
      ActiveCam = &Cam;
    }
    SceneProp.pCameras[0] = ActiveCam;
  }

  if (IManager->PressedKey(T800K_w)) {
    ActiveCam->MoveForward(DtSecs);
  }

  if (IManager->PressedKey(T800K_s)) {
    ActiveCam->MoveBackward(DtSecs);
  }

  if (IManager->PressedKey(T800K_a)) {
    ActiveCam->StrafeLeft(DtSecs);
  }

  if (IManager->PressedKey(T800K_d)) {
    ActiveCam->StrafeRight(DtSecs);
  }

  if (IManager->PressedOnceKey(T800K_1)) {
    pFramework->ChangeAPI(GRAPHICS_API::D3D11);
  }

  if (IManager->PressedOnceKey(T800K_2)) {
    pFramework->ChangeAPI(GRAPHICS_API::OPENGL);
  }
  float yaw = 0.005f*static_cast<float>(IManager->xDelta);
  ActiveCam->MoveYaw(yaw);
  float pitch = 0.005f*static_cast<float>(IManager->yDelta);
  ActiveCam->MovePitch(pitch);
}

void SC_Night::OnDraw() {
  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ_WRITE);
  pFramework->pVideoDriver->PushRT(OmniShadowDepthPass);
  SceneProp.ActiveLightCamera = 1;
  for (size_t i = 0; i < 6; i++)
  {
    pFramework->pVideoDriver->RTs[OmniShadowDepthPass]->ChangeCubeDepthTexture(i);
    pFramework->pVideoDriver->Clear();
  }
  // Omnidirectional Shadow Map Depth Pass
  pFramework->pVideoDriver->SetCullFace(BaseDriver::FACE_CULLING::BACK_FACES);
  Meshes[0].SetGlobalSignature(Signature::RADIAL_DEPTH_PASS);
  for (size_t i = 0; i < 6; i++)
  {
    SceneProp.pCameras[0] = &OmniLightCam[i];
    pFramework->pVideoDriver->RTs[OmniShadowDepthPass]->ChangeCubeDepthTexture(i);
    Meshes[0].Draw();
  }
  pFramework->pVideoDriver->PopRT();
  Meshes[0].SetGlobalSignature(Signature::FORWARD_PASS);

  pFramework->pVideoDriver->SetCullFace(BaseDriver::FACE_CULLING::FRONT_FACES);
  SceneProp.ActiveLightCamera = 0;

  // G Buffer Pass
  pFramework->pVideoDriver->PushRT(GBufferPass);
  SceneProp.pCameras[0] = &Cam;
  for (int i = 0; i < 3; i++) {
    Meshes[i].SetGlobalSignature(Signature::GBUFF_PASS);
    Meshes[i].Draw();
    Meshes[i].SetGlobalSignature(Signature::FORWARD_PASS);
  }
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ);

  // Shadow Map Buffer Accumulation + Occlusion 
  SceneProp.ActiveLightCamera = 1;
  pFramework->pVideoDriver->PushRT(ShadowAccumPass);
  pFramework->pVideoDriver->Clear();
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::DEPTH_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(OmniShadowDepthPass, BaseDriver::DEPTH_ATTACHMENT), 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR1_ATTACHMENT), 2);
  Quads[0].SetTexture(SceneProp.SSAOKernel.NoiseTex, 3);
  Quads[0].SetGlobalSignature(Signature::SHADOW_COMP_PASS | Signature::USE_OMNIDIRECTIONAL_SHADOWS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();
  SceneProp.ActiveLightCamera = 0;

  // Shadow Map Blur Pass
  pFramework->pVideoDriver->PushRT(ExtraHelperPass);
  SceneProp.ActiveGaussKernel = SHADOW_KERNEL;
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::VERTICAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->PushRT(ShadowAccumPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::HORIZONTAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  // Deferred Pass
  pFramework->pVideoDriver->PushRT(DeferredPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR1_ATTACHMENT), 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR2_ATTACHMENT), 2);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR3_ATTACHMENT), 3);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::DEPTH_ATTACHMENT), 4);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT), 5);
  Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));
  //Quads[0].SetEnvironmentMap(pFramework->pVideoDriver->GetRTTexture(OmniShadowDepthPass, BaseDriver::DEPTH_ATTACHMENT));
  Quads[0].SetGlobalSignature(Signature::DEFERRED_PASS);
  Quads[0].Draw();
 // pFramework->pVideoDriver->PopRT();

  // Volumetric ball pass
  pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
  //pFramework->pVideoDriver->PushRT(DeferredPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::DEPTH_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::RAY_MARCH);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  //pFramework->pVideoDriver->PushRT(Extra16FPass);
  //Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GodRaysCalcPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  //Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  //Quads[0].SetGlobalSignature(Signature::FSQUAD_2_TEX);
  //Quads[0].Draw();
  //pFramework->pVideoDriver->PopRT();
  pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_STATES::BLEND_DEFAULT);
  // Bright Pass
  pFramework->pVideoDriver->PushRT(BloomAccumPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::BRIGHT_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  SceneProp.ActiveGaussKernel = BLOOM_KERNEL;
  pFramework->pVideoDriver->PushRT(ExtraHelperPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(BloomAccumPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::HORIZONTAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->PushRT(BloomAccumPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetGlobalSignature(Signature::VERTICAL_BLUR_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();


  SceneProp.ActiveGaussKernel = DOF_KERNEL;
  //DOF PASS
  pFramework->pVideoDriver->PushRT(CoCPass);
  Quads[0].SetGlobalSignature(Signature::COC_PASS);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::DEPTH_ATTACHMENT), 0);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  //COMBINE COC
  pFramework->pVideoDriver->PushRT(CombineCoCPass);
  Quads[0].SetGlobalSignature(Signature::COMBINE_COC_PASS);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CoCPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CoCPass, BaseDriver::COLOR1_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();
  ////DOF_BLUR
  pFramework->pVideoDriver->PushRT(Extra16FPass);
  Quads[0].SetGlobalSignature(Signature::DOF_PASS);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CombineCoCPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->PushRT(DeferredPass);
  Quads[0].SetGlobalSignature(Signature::DOF_PASS_2);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(Extra16FPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CombineCoCPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  // HDR Composition Pass
  pFramework->pVideoDriver->PushRT(ExtraHelperPass);
#if	SEPARATED_BLUR
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(BloomAccumPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].SetGlobalSignature(Signature::HDR_COMP_PASS);
  Quads[0].Draw();
#else
  PrimitiveMgr.GetPrimitive(QuadIndex)->SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  PrimitiveMgr.GetPrimitive(QuadIndex)->SetTexture(pFramework->pVideoDriver->GetRTTexture(ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].SetSignature(Signature::HDR_COMP_PASS);
  Quads[0].Draw();
#endif
  pFramework->pVideoDriver->PopRT();

  // Final Draw
  Quads[7].SetTexture(pFramework->pVideoDriver->GetRTTexture(ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[7].SetGlobalSignature(Signature::VIGNETTE_PASS);
  Quads[7].Draw();

  //Quads[1].SetTexture(pFramework->pVideoDriver->GetRTTexture(ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  //Quads[1].SetGlobalSignature(Signature::FSQUAD_1_TEX);
  //Quads[1].Draw();

  Quads[2].SetTexture(pFramework->pVideoDriver->GetRTTexture(GodRaysCalcPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[2].SetGlobalSignature(Signature::FSQUAD_1_TEX);
  Quads[2].Draw();

  //Quads[3].SetTexture(pFramework->pVideoDriver->GetRTTexture(CombineCoCPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  //Quads[3].SetGlobalSignature(Signature::FSQUAD_1_TEX);
  //Quads[3].Draw();

  //Quads[4].SetTexture(pFramework->pVideoDriver->GetRTTexture(ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  //Quads[4].SetGlobalSignature(Signature::FSQUAD_1_TEX);
  //Quads[4].Draw();
  //
  //Quads[4].SetTexture(pFramework->pVideoDriver->GetRTTexture(ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  //Quads[4].SetGlobalSignature(Signature::FSQUAD_1_TEX);
  //Quads[4].Draw();

}

void  SC_Night::ChangeSettingsOnPlus() {
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    float prevVal = SceneProp.Exposure;
    SceneProp.Exposure += 0.1f;
    cout << "[CHANGE_EXPOSURE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.Exposure << "]" << endl;
  }break;
  case CHANGE_BLOOM_FACTOR: {
    float prevVal = SceneProp.BloomFactor;
    SceneProp.BloomFactor += 0.1f;
    cout << "[CHANGE_BLOOM_FACTOR] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.BloomFactor << "]" << endl;
  }break;
  case CHANGE_NUM_LIGHTS: {
    int prevVal = SceneProp.ActiveLights;
    SceneProp.ActiveLights *= 2;
    if (SceneProp.ActiveLights >= 127) {
      SceneProp.ActiveLights = 127;
    }
    cout << "[CHANGE_NUM_LIGHTS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ActiveLights << "]" << endl;
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    int prevVal = ChangeActiveGaussSelection;
    ChangeActiveGaussSelection++;
    if (ChangeActiveGaussSelection >= (int)SceneProp.pGaussKernels.size()) {
      ChangeActiveGaussSelection = SceneProp.pGaussKernels.size() - 1;
    }
    cout << "[CHANGE_ACTIVE_GAUSS_KERNEL] Previous Value[" << prevVal << "] Actual Value[" << ChangeActiveGaussSelection << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    int prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize += 2;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius += 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma += 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_DEVIATION] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma << "]" << endl;
  }break;
  case CHANGE_PCF_RADIUS: {
    float prevVal = SceneProp.PCFScale;
    SceneProp.PCFScale += 0.1f;
    cout << "[CHANGE_PCF_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.PCFScale << "]" << endl;
  }break;
  case CHANGE_PCF_SAMPLES: {
    float prevVal = SceneProp.PCFSamples;
    SceneProp.PCFSamples++;
    cout << "[CHANGE_PCF_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.PCFSamples << "]" << endl;
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    float prevVal = (float)SceneProp.SSAOKernel.KernelSize;
    SceneProp.SSAOKernel.KernelSize += 2;
    SceneProp.SSAOKernel.Update();
    cout << "[CHANGE_SSAO_KERNEL_SIZE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.SSAOKernel.KernelSize << "]" << endl;
  }break;
  case CHANGE_SSAO_RADIUS: {
    float prevVal = SceneProp.SSAOKernel.Radius;
    SceneProp.SSAOKernel.Radius += 0.5f;
    cout << "[CHANGE_SSAO_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.SSAOKernel.Radius << "]" << endl;
  }break;
  case CHANGE_DOF_APERTURE: {
    float prevVal = SceneProp.Aperture;
    SceneProp.Aperture += 10.0f;
    cout << "[CHANGE_DOF_APERTURE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.Aperture << "]" << endl;
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    float prevVal = SceneProp.FocalLength;
    SceneProp.FocalLength += 10.0f;
    cout << "[CHANGE_DOF_FOCAL_LENGHT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.FocalLength << "]" << endl;
  }break;
  case CHANGE_DOF_MAX_COC: {
    float prevVal = SceneProp.MaxCoc;
    SceneProp.MaxCoc += 0.5f;
    cout << "[CHANGE_DOF_MAX_COC] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.MaxCoc << "]" << endl;
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Far_Samples_squared;
    SceneProp.DOF_Far_Samples_squared += 1.0f;
    cout << "[CHANGE_DOF_FAR_SAMPLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.DOF_Far_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Near_Samples_squared;
    SceneProp.DOF_Near_Samples_squared += 1.0f;
    cout << "[CHANGE_DOF_NEAR_SAMPLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.DOF_Near_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    bool prevVal = SceneProp.AutoFocus;
    SceneProp.AutoFocus = true;
    cout << "[CHANGE_DOF_AUTO_FOCUS] Previous Value[" << prevVal << "] Actual Value[" << (int)SceneProp.AutoFocus << "]" << endl;
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    float prevVal = SceneProp.ParallaxLowSamples;
    SceneProp.ParallaxLowSamples += 5.0f;
    cout << "[CHANGE_PARALLAX_LOW_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxLowSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    float prevVal = SceneProp.ParallaxHighSamples;
    SceneProp.ParallaxHighSamples += 10.0f;
    cout << "[CHANGE_PARALLAX_HIGH_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxHighSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    float prevVal = SceneProp.ParallaxHeight;
    SceneProp.ParallaxHeight += 0.01f;
    cout << "[CHANGE_PARALLAX_HEIGHT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxHeight << "]" << endl;
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    float prevVal = SceneProp.LightVolumeSteps;
    SceneProp.LightVolumeSteps += 16.0f;
    cout << "[CHANGE_LIGHT_VOLUME_STEPS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.LightVolumeSteps << "]" << endl;
  }break;
  }
}

void  SC_Night::ChangeSettingsOnMinus() {
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    float prevVal = SceneProp.Exposure;
    SceneProp.Exposure -= 0.1f;
    cout << "[CHANGE_EXPOSURE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.Exposure << "]" << endl;
  }break;
  case CHANGE_BLOOM_FACTOR: {
    float prevVal = SceneProp.BloomFactor;
    SceneProp.BloomFactor -= 0.1f;
    cout << "[CHANGE_BLOOM_FACTOR] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.BloomFactor << "]" << endl;
  }break;
  case CHANGE_NUM_LIGHTS: {
    int prevVal = SceneProp.ActiveLights;
    SceneProp.ActiveLights /= 2;
    if (SceneProp.ActiveLights <= 0) {
      SceneProp.ActiveLights = 1;
    }
    cout << "[CHANGE_NUM_LIGHTS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ActiveLights << "]" << endl;
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    int prevVal = ChangeActiveGaussSelection;
    ChangeActiveGaussSelection--;
    if (ChangeActiveGaussSelection < 0) {
      ChangeActiveGaussSelection = 0;
    }
    cout << "[CHANGE_ACTIVE_GAUSS_KERNEL] Previous Value[" << prevVal << "] Actual Value[" << ChangeActiveGaussSelection << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    int prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize -= 2;
    if (SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize <= 2) {
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize = 3;
    }
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius -= 0.5f;
    if (SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius <= 0.5f) {
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius = 0.5f;
    }
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma -= 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    cout << "[CHANGE_GAUSS_KERNEL_DEVIATION] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma << "]" << endl;
  }break;
  case CHANGE_PCF_RADIUS: {
    float prevVal = SceneProp.PCFScale;
    SceneProp.PCFScale -= 0.1f;
    cout << "[CHANGE_PCF_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.PCFScale << "]" << endl;
  }break;
  case CHANGE_PCF_SAMPLES: {
    float prevVal = SceneProp.PCFSamples;
    SceneProp.PCFSamples--;
    cout << "[CHANGE_PCF_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.PCFSamples << "]" << endl;
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    float prevVal = (float)SceneProp.SSAOKernel.KernelSize;
    SceneProp.SSAOKernel.KernelSize -= 2;
    SceneProp.SSAOKernel.Update();
    cout << "[CHANGE_SSAO_KERNEL_SIZE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.SSAOKernel.KernelSize << "]" << endl;
  }break;
  case CHANGE_SSAO_RADIUS: {
    float prevVal = SceneProp.SSAOKernel.Radius;
    SceneProp.SSAOKernel.Radius -= 0.5f;
    cout << "[CHANGE_SSAO_RADIUS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.SSAOKernel.Radius << "]" << endl;
  }break;
  case CHANGE_DOF_APERTURE: {
    float prevVal = SceneProp.Aperture;
    SceneProp.Aperture -= 10.0f;
    cout << "[CHANGE_DOF_APERTURE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.Aperture << "]" << endl;
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    float prevVal = SceneProp.FocalLength;
    SceneProp.FocalLength -= 10.0f;
    cout << "[CHANGE_DOF_FOCAL_LENGHT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.FocalLength << "]" << endl;
  }break;
  case CHANGE_DOF_MAX_COC: {
    float prevVal = SceneProp.MaxCoc;
    SceneProp.MaxCoc -= 0.5f;
    cout << "[CHANGE_DOF_MAX_COC] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.MaxCoc << "]" << endl;
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Far_Samples_squared;
    SceneProp.DOF_Far_Samples_squared -= 1.0f;
    cout << "[CHANGE_DOF_FAR_SAMPLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.DOF_Far_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Near_Samples_squared;
    SceneProp.DOF_Near_Samples_squared -= 1.0f;
    cout << "[CHANGE_DOF_NEAR_SAMPLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.DOF_Near_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    bool prevVal = SceneProp.AutoFocus;
    SceneProp.AutoFocus = false;
    cout << "[CHANGE_DOF_AUTO_FOCUS] Previous Value[" << prevVal << "] Actual Value[" << (int)SceneProp.AutoFocus << "]" << endl;
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    float prevVal = SceneProp.ParallaxLowSamples;
    SceneProp.ParallaxLowSamples -= 5.0f;
    cout << "[CHANGE_PARALLAX_LOW_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxLowSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    float prevVal = SceneProp.ParallaxHighSamples;
    SceneProp.ParallaxHighSamples -= 10.0f;
    cout << "[CHANGE_PARALLAX_HIGH_SAMPLES] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxHighSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    float prevVal = SceneProp.ParallaxHeight;
    SceneProp.ParallaxHeight -= 0.01f;
    cout << "[CHANGE_PARALLAX_HEIGHT] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ParallaxHeight << "]" << endl;
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    float prevVal = SceneProp.LightVolumeSteps;
    SceneProp.LightVolumeSteps -= 16.0f;
    cout << "[CHANGE_LIGHT_VOLUME_STEPS] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.LightVolumeSteps << "]" << endl;
  }break;
  }
}

void SC_Night::printCurrSelection() {
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    cout << "Option[CHANGE_EXPOSURE] Value[" << SceneProp.Exposure << "]" << endl;
  }break;
  case CHANGE_BLOOM_FACTOR: {
    cout << "Option[CHANGE_BLOOM_FACTOR] Value[" << SceneProp.BloomFactor << "]" << endl;
  }break;
  case CHANGE_NUM_LIGHTS: {
    cout << "Option[CHANGE_NUM_LIGHTS] Value[" << SceneProp.ActiveLights << "]" << endl;
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    cout << "Option[CHANGE_ACTIVE_GAUSS_KERNEL] Value[" << ChangeActiveGaussSelection << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    cout << "Option[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    cout << "Option[CHANGE_GAUSS_KERNEL_RADIUS] Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius << "]" << endl;
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    cout << "Option[CHANGE_GAUSS_KERNEL_DEVIATION] Value[" << SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma << "]" << endl;
  }break;
  case CHANGE_PCF_RADIUS: {
    cout << "Option[CHANGE_PCF_RADIUS] Value[" << SceneProp.PCFScale << "]" << endl;
  }break;
  case CHANGE_PCF_SAMPLES: {
    cout << "Option[CHANGE_PCF_SAMPLES] Value[" << SceneProp.PCFSamples << "]" << endl;
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    cout << "Option[CHANGE_SSAO_KERNEL_SIZE] Value[" << SceneProp.SSAOKernel.KernelSize << "]" << endl;
  }break;
  case CHANGE_SSAO_RADIUS: {
    cout << "Option[CHANGE_SSAO_RADIUS] Value[" << SceneProp.SSAOKernel.Radius << "]" << endl;
  }break;
  case CHANGE_DOF_APERTURE: {
    cout << "Option[CHANGE_DOF_APERTURE] Value[" << SceneProp.Aperture << "]" << endl;
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    cout << "Option[CHANGE_DOF_FOCAL_LENGHT] Value[" << SceneProp.FocalLength << "]" << endl;
  }break;
  case CHANGE_DOF_MAX_COC: {
    cout << "Option[CHANGE_DOF_MAX_COC] Value[" << SceneProp.MaxCoc << "]" << endl;
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    cout << "Option[CHANGE_DOF_FAR_SAMPLE] Value[" << SceneProp.DOF_Far_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    cout << "Option[CHANGE_DOF_NEAR_SAMPLE] Value[" << SceneProp.DOF_Near_Samples_squared << "]" << endl;
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    cout << "Option[CHANGE_DOF_AUTO_FOCUS] Value[" << (int)SceneProp.AutoFocus << "]" << endl;
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    cout << "Option[CHANGE_PARALLAX_LOW_SAMPLES] Value[" << SceneProp.ParallaxLowSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    cout << "Option[CHANGE_PARALLAX_HIGH_SAMPLES] Value[" << SceneProp.ParallaxHighSamples << "]" << endl;
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    cout << "Option[CHANGE_PARALLAX_HEIGHT] Value[" << SceneProp.ParallaxHeight << "]" << endl;
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    cout << "Option[CHANGE_LIGHT_VOLUME_STEPS] Value[" << SceneProp.LightVolumeSteps << "]" << endl;
  }break;
  }
}

