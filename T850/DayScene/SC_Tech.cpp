#include "SC_Tech.h"
using namespace t800;
using std::cout;
using std::endl;
using std::string;
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

void SC_Tech::InitVars() {
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

  SceneProp.AddLight(LightCam.Eye, XVECTOR3(1, 1, 1), 30000, true);
  SceneProp.AddLight(XVECTOR3(-55, 10, 0), XVECTOR3(1.0, 0.57, 0.16), 60, true);
  SceneProp.AddLight(XVECTOR3(55, 10, 0), XVECTOR3(1.0, 0.57, 0.16), 60, true);
  SceneProp.AddLight(XVECTOR3(60, 10, 30), XVECTOR3(1.0, 0.57, 0.16), 60, true);
  SceneProp.AddLight(XVECTOR3(60, 10, -30), XVECTOR3(1.0, 0.57, 0.16), 60, true);
  SceneProp.ActiveLights = 5;
  SceneProp.AmbientColor = XVECTOR3(0.8f, 0.8f, 0.8f);

  ShadowFilter.kernelSize = 4;
  ShadowFilter.radius = 1.f;
  ShadowFilter.sigma = 1.0f;
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
  SceneProp.SSAOKernel.Radius = 1.5f;
  SceneProp.SSAOKernel.KernelSize = 128;
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
  SceneProp.SSAOKernel.Radius = 1.5f;
  SceneProp.SSAOKernel.KernelSize = 32;
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
  SceneProp.SSAOKernel.Radius = 1.5f;
  SceneProp.SSAOKernel.KernelSize = 8;
#endif

  SceneProp.SSAOKernel.Update();


  SceneProp.ToogleShadow = true;
  SceneProp.ToogleSSAO = true;
  SceneProp.AutoFocus = true;



  SceneProp.AddGaussKernel(&ShadowFilter);
  SceneProp.AddGaussKernel(&BloomFilter);
  SceneProp.AddGaussKernel(&NearDOFFilter);
  SceneProp.ActiveGaussKernel = SHADOW_KERNEL;
  ChangeActiveGaussSelection = SHADOW_KERNEL;

  RTIndex = -1;

  m_spline.m_points.push_back(SplinePoint(80, 90, -80));
  m_spline.m_points.back().m_velocity = 3.0f;
  m_spline.m_points.push_back(SplinePoint(80, 90, -20));
  m_spline.m_points.back().m_velocity = 6.f;
  m_spline.m_points.push_back(SplinePoint(20, 110, 0));
  m_spline.m_points.back().m_velocity = 20.f;
  m_spline.m_points.push_back(SplinePoint(30, 20, 0));
  m_spline.m_points.back().m_velocity = 7;
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
  m_spline.m_points.push_back(SplinePoint(-20, 15, 0));
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(0, 10, 0)); //
  m_spline.m_points.back().m_velocity = 7;
  m_spline.m_points.push_back(SplinePoint(-40, 25, 3));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-55, 30, 5));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-55, 30, -25));
  m_spline.m_points.back().m_velocity = 3;
  m_spline.m_points.push_back(SplinePoint(-65, 25, 0));
  m_spline.m_points.back().m_velocity = 3;
  m_spline.m_points.push_back(SplinePoint(-60, 25, 25));
  m_spline.m_points.back().m_velocity = 3;
  m_spline.m_points.push_back(SplinePoint(-35, 30, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(15, 30, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(25, 30, 20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(50, 30, 20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(50, 30, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-50, 30, -20));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-50, 30, 0));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(-30, 30, 0));
  m_spline.m_points.back().m_velocity = 10;
  m_spline.m_points.push_back(SplinePoint(0, 5, 0));
  m_spline.m_points.back().m_velocity = 10;

  m_spline.m_looped = false;
  m_spline.Init();

  m_agent.SetOffset(0);
  m_agent.m_pSpline = &m_spline;
  m_agent.m_moving = true;
  m_agent.m_velocity = 15.0f;

  totalTime = 0.0f;

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
void SC_Tech::CreateAssets() {
  //Create RT's via RenderGraph
  if (!m_renderGraph.Load("Scenes/SC_Tech_RenderGraph.json")) {
    printf("[SC_Tech] ERROR: Failed to load render graph\n");
    return;
  }
  m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp);
  m_renderGraph.PrintGraph();

  // Alias RT handles for RT cycling display
  GBufferPass      = m_renderGraph.GetRTHandle("GBuffer");
  DeferredPass     = m_renderGraph.GetRTHandle("Deferred");
  Extra16FPass     = m_renderGraph.GetRTHandle("Extra16F");
  DepthPass        = m_renderGraph.GetRTHandle("DepthPass");
  ShadowAccumPass  = m_renderGraph.GetRTHandle("ShadowAccum");
  ExtraHelperPass  = m_renderGraph.GetRTHandle("ExtraHelper");
  BloomAccumPass   = m_renderGraph.GetRTHandle("BloomAccum");
  GodRaysCalcPass  = m_renderGraph.GetRTHandle("GodRaysCalc");
  CoCPass          = m_renderGraph.GetRTHandle("CoC");
  CombineCoCPass   = m_renderGraph.GetRTHandle("CombineCoC");
  CoCHelperPass    = m_renderGraph.GetRTHandle("CoCHelper");
  CoCHelperPass2   = m_renderGraph.GetRTHandle("CoCHelper2");

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
  Quads[1].TranslateAbsolute(-0.75f, +0.75f, 0.0f);
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

void SC_Tech::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void SC_Tech::OnDestoryScene() {
  DestroyAssets();
}

void SC_Tech::DestroyAssets() {
  PrimitiveMgr.DestroyPrimitives();
  pFramework->pVideoDriver->DestroyRTs();
  //pFramework->pVideoDriver->DestroyShaders();
  //pFramework->pVideoDriver->DestroyTextures();
  //pFramework->pVideoDriver->DestroyTechniques();
}

void SC_Tech::OnUpdate(float _DtSecs) {
  
  totalTime += _DtSecs;
  DtSecs = _DtSecs;
  Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);

  // Replay snapshot: load and apply (one-time)
  if (m_dumper.HasPendingReplay()) {
    if (m_dumper.LoadReplaySnapshot()) {
      m_dumper.ApplySnapshot(Cam, LightCam, SceneProp);
      VP = ActiveCam->VP;
    }
  }
  m_dumper.UpdateReplayState();

  // Normal camera/light updates (skipped when replay is active or dump pending)
  if (!m_dumper.SkipCameraUpdates()) {
    m_agent.Update(DtSecs);
    ActiveCam->Update(DtSecs);
    VP = ActiveCam->VP;
    SceneProp.Lights[0].Position = LightCam.Eye;
  }

  if (totalTime > 45.0f) {
    totalTime = 0.0;
    pFramework->pBaseApp->LoadScene(0);
  }
}

void SC_Tech::OnInput(InputManager* IManager) {
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

void SC_Tech::OnDraw() {
  // Execute the render graph (all passes through HDR Composition)
  m_renderGraph.Execute(
    pFramework->pVideoDriver,
    SceneProp,
    Meshes, 2,
    Quads,
    &Cam,
    &LightCam,
    nullptr,
    EnvMapTexIndex
  );

  // RT cycling display (Tech showcase)
  int selected = 0;
  int attachment = 0;
  const float stepTime = 5.0f;
  if (totalTime < stepTime) {
    selected = GBufferPass;
    attachment = BaseDriver::COLOR0_ATTACHMENT;
  }
  else if (totalTime < stepTime * 2.0f) {
    selected = GBufferPass;
    attachment = BaseDriver::COLOR1_ATTACHMENT;
  }
  else if (totalTime < stepTime * 3.0f) {
    selected = GBufferPass;
    attachment = BaseDriver::COLOR2_ATTACHMENT;
  }
  else if (totalTime < stepTime * 4) {
    selected = Extra16FPass;
    attachment = BaseDriver::COLOR0_ATTACHMENT;
  }
  else if (totalTime < stepTime * 5) {
    selected = DeferredPass;
    attachment = BaseDriver::COLOR0_ATTACHMENT;
  }
  else if (totalTime < stepTime * 6) {
    selected = CombineCoCPass;
    attachment = BaseDriver::COLOR0_ATTACHMENT;
  }
  else if (totalTime < stepTime * 7) {
    selected = BloomAccumPass;
    attachment = BaseDriver::COLOR0_ATTACHMENT;
  }
  else if (totalTime < stepTime * 8) {
    selected = ShadowAccumPass;
    attachment = BaseDriver::COLOR0_ATTACHMENT;
  }
  else {
    selected = GodRaysCalcPass;
    attachment = BaseDriver::COLOR0_ATTACHMENT;
  }
  Quads[7].SetTexture(pFramework->pVideoDriver->GetRTTexture(selected, attachment), 0);
  Quads[7].SetGlobalSignature(Signature::FSQUAD_1_TEX);
  Quads[7].Draw();

  // RT Dump via FrameDumper
  if (m_dumper.ShouldDump(DtSecs)) {
    std::vector<RTDumpEntry> rts = {
      {GBufferPass,     BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Color0"},
      {GBufferPass,     BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {GBufferPass,     BaseDriver::COLOR2_ATTACHMENT, "GBuffer_Color2"},
      {DepthPass,       BaseDriver::DEPTH_ATTACHMENT,  "ShadowMap_Depth"},
      {DeferredPass,    BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {Extra16FPass,    BaseDriver::COLOR0_ATTACHMENT, "Extra16F"},
      {ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
      {BloomAccumPass,  BaseDriver::COLOR0_ATTACHMENT, "Bloom"},
      {GodRaysCalcPass, BaseDriver::COLOR0_ATTACHMENT, "GodRays"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs);
    if (m_dumper.ShouldExit()) exit(0);
  }
}







