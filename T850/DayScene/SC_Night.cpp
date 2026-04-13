#include "SC_Night.h"
using namespace t800;
using std::cout;
using std::endl;
using std::string;
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

  SceneProp.AddDirectionalLight(XVECTOR3(0.0f, -1.0f, 0.0f), XVECTOR3(0.1215, 0.1607, 0.2090), 1.0f, true);
  SceneProp.AddLight(omniLightPos, XVECTOR3(1.0, 0.57, 0.16), 30, 1.0f, LIGHT_POINT, true);
  SceneProp.ActiveLights = 120;
  for (int i = 0; i < SceneProp.ActiveLights-2; ++i) {
    /*SceneProp.AddLight(XVECTOR3(-200 + i*4, 4, -200 + i * 4), XVECTOR3(1.0, 0.57, 0.16), 5, true);*/
    float r = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
    float g = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
    float b = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
    SceneProp.AddLight(XVECTOR3(0.0f, 15.0f, 0.0f), XVECTOR3(r, g, b), 10, 1.0f, LIGHT_POINT, true);
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

  m_agent.SetOffset(0);
  m_agent.m_pSpline = &m_spline;
  m_agent.m_moving = true;
  m_agent.m_velocity = 15.0f;

  m_lightAgent.SetOffset(50);
  m_lightAgent.m_pSpline = &m_spline;
  m_lightAgent.m_moving = true;
  m_lightAgent.m_velocity = 15.0f;
  
  // Initialize frame dumper from command-line globals
  extern bool g_dumpEnabled, g_dumpByFrame, g_debugFrames, g_keepRunning;
  extern int g_dumpFrame, g_startScene;
  extern float g_dumpSeconds;
  extern std::string g_replaySnapshotPath;
  FrameDumperConfig dumpCfg;
  dumpCfg.dumpEnabled     = g_dumpEnabled;
  dumpCfg.dumpByFrame     = g_dumpByFrame;
  dumpCfg.dumpFrame       = g_dumpFrame;
  dumpCfg.dumpSeconds     = g_dumpSeconds;
  dumpCfg.debugFrames     = g_debugFrames;
  dumpCfg.keepRunning     = g_keepRunning;
  dumpCfg.replaySnapshotPath = g_replaySnapshotPath;
  dumpCfg.sceneIndex      = g_startScene;
  m_dumper.Init(dumpCfg);
}
void SC_Night::CreateAssets() {
  //Create RT's via RenderGraph
  if (!m_renderGraph.Load("Scenes/SC_Night_RenderGraph.json")) {
    printf("[SC_Night] ERROR: Failed to load render graph\n");
    return;
  }
  m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp);
  m_renderGraph.PrintGraph();

  // Alias RT handles for debug display
  GBufferPass      = m_renderGraph.GetRTHandle("GBuffer");
  DeferredPass     = m_renderGraph.GetRTHandle("Deferred");
  Extra16FPass     = m_renderGraph.GetRTHandle("Extra16F");
  ShadowAccumPass  = m_renderGraph.GetRTHandle("ShadowAccum");
  ExtraHelperPass  = m_renderGraph.GetRTHandle("ExtraHelper");
  BloomAccumPass   = m_renderGraph.GetRTHandle("BloomAccum");
  GodRaysCalcPass  = m_renderGraph.GetRTHandle("GodRaysCalc");
  CoCPass          = m_renderGraph.GetRTHandle("CoC");
  CombineCoCPass   = m_renderGraph.GetRTHandle("CombineCoC");
  CoCHelperPass    = m_renderGraph.GetRTHandle("CoCHelper");
  CoCHelperPass2   = m_renderGraph.GetRTHandle("CoCHelper2");
  OmniShadowDepthPass = m_renderGraph.GetRTHandle("OmniShadowDepth");
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);
  SceneProp.SSAOKernel.InitTexture();

  EnvMapTexIndex = g_pBaseDriver->CreateTexture(string("sky/Pisa.dds"));

  int index = PrimitiveMgr.CreateMesh("Models/SkyBox.X");
  Meshes[1].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
  Meshes[1].TranslateAbsolute(0.0f, -10.0f, 0.0f);
  Meshes[1].Update();

  index = PrimitiveMgr.CreateMesh("Models/SponzaEsc.X");
  Meshes[0].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);

  index = PrimitiveMgr.CreateMesh("Models/NuCroc.X");
  Meshes[2].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
  Meshes[2].TranslateAbsolute(omniLightPos.x, omniLightPos.y, omniLightPos.z);
  Meshes[2].ScaleAbsolute(0.01f);
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
  // Only advance scene timer when spline camera is driving the tour
  if (ActiveCam->m_externalControl)
    totalTime += _DtSecs;
  DtSecs = _DtSecs;
  Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);

  // Replay snapshot: load and apply (one-time)
  if (m_dumper.HasPendingReplay()) {
    if (m_dumper.LoadReplaySnapshot()) {
      m_dumper.ApplySnapshot(Cam, LightCam, SceneProp, OmniLightCam, &omniLightPos);
      VP = ActiveCam->VP;
    }
  }
  m_dumper.UpdateReplayState();

  // Normal camera/light updates (skipped when replay is active or dump pending)
  if (!m_dumper.SkipCameraUpdates()) {
    m_agent.Update(DtSecs);
    m_lightAgent.Update(DtSecs);
    omniLightPos = m_lightAgent.m_actualPoint;
    ActiveCam->Update(DtSecs);
    VP = ActiveCam->VP;
  }

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

  if (IManager->PressedOnceKey(T800K_SPACE)) {
    m_dumper.RequestDump();
  }

  // Skip mouse-driven camera movement when replay snapshot is active
  if (!m_dumper.IsReplayActive()) {
    float yaw = 0.005f*static_cast<float>(IManager->xDelta);
    ActiveCam->MoveYaw(yaw);
    float pitch = 0.005f*static_cast<float>(IManager->yDelta);
    ActiveCam->MovePitch(pitch);
  }
}

void SC_Night::OnDraw() {
  // Execute the render graph (all passes including final backbuffer)
  m_renderGraph.Execute(
    pFramework->pVideoDriver,
    SceneProp,
    Meshes, 3,
    Quads,
    &Cam,
    &LightCam,
    OmniLightCam,
    EnvMapTexIndex
  );

  // RT Dump via FrameDumper
  if (m_dumper.ShouldDump(DtSecs)) {
    std::vector<RTDumpEntry> rts = {
      {GBufferPass,         BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Color0"},
      {GBufferPass,         BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {GBufferPass,         BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Depth"},
      {DeferredPass,        BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {ShadowAccumPass,     BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {Extra16FPass,        BaseDriver::COLOR0_ATTACHMENT, "Extra16F"},
      {ExtraHelperPass,     BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
      {BloomAccumPass,      BaseDriver::COLOR0_ATTACHMENT, "Bloom"},
      {GodRaysCalcPass,     BaseDriver::COLOR0_ATTACHMENT, "GodRays"},
      {OmniShadowDepthPass, BaseDriver::DEPTH_ATTACHMENT,  "OmniShadowDepth"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs,
                       OmniLightCam, &omniLightPos);
    if (m_dumper.ShouldExit()) exit(0);
  }
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
      ChangeActiveGaussSelection = static_cast<int>(SceneProp.pGaussKernels.size()) - 1;
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

