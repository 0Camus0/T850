#include "SC_Day.h"
#include <iostream>
#include <cstdio>
using namespace t800;
using std::cout;
using std::endl;
using std::string;

#define NUM_LIGHTS 1
#define RADI 170.0f

#define HIGHQ 1
#define MEDIUMQ 2
#define LOWQ 3

#define QUALITY_SELECTED HIGHQ

#if   QUALITY_SELECTED == HIGHQ
#define MAX_QUALITY
#elif QUALITY_SELECTED == MEDIUMQ
#define MEDIUM_QUALITY
#elif QUALITY_SELECTED == LOWQ
#define LOW_QUALITY 
#endif

void SC_Day::InitVars() {
  Position = XVECTOR3(0.0f, 0.0f, 0.0f);
  Orientation = XVECTOR3(0.0f, 0.0f, 0.0f);
  Scaling = XVECTOR3(1.0f, 1.0f, 1.0f);
  SelectedMesh = 0;

  CamSelection = NORMAL_CAM1;
  SceneSettingSelection = CHANGE_EXPOSURE;

  if (!m_sceneSetup.Load("Scenes/SC_Day.json")) {
    printf("[SC_Day] ERROR: Failed to load Scenes/SC_Day.json\n");
    return;
  }
  m_sceneSetup.Apply(SceneProp);

  ActiveCam = m_sceneSetup.GetCamera(0);
  ChangeActiveGaussSelection = SHADOW_KERNEL;
  RTIndex = -1;

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
void SC_Day::CreateAssets() {
  //Create RT's via RenderGraph
  if (!m_renderGraph.Load("Scenes/SC_Day_RenderGraph.json")) {
    printf("[SC_Day] ERROR: Failed to load render graph\n");
    return;
  }
  m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp);
  m_renderGraph.PrintGraph();

  // Alias RT handles for FrameDumper and debug display
  GBufferPass      = m_renderGraph.GetRTHandle("GBuffer");
  DeferredPass     = m_renderGraph.GetRTHandle("Deferred");
  Extra16FPass     = m_renderGraph.GetRTHandle("Extra16F");
  DepthPass        = m_renderGraph.GetRTHandle("DepthPass");
  ShadowAccumPass  = m_renderGraph.GetRTHandle("ShadowAccum");
  ExtraHelperPass  = m_renderGraph.GetRTHandle("ExtraHelper");
  BloomAccumPass   = m_renderGraph.GetRTHandle("BloomAccum");
  GodRaysCalcPass  = m_renderGraph.GetRTHandle("GodRaysCalc");
  GodRaysCalcExtraPass = m_renderGraph.GetRTHandle("GodRaysCalcExtra");
  CoCPass          = m_renderGraph.GetRTHandle("CoC");
  CombineCoCPass   = m_renderGraph.GetRTHandle("CombineCoC");
  CoCHelperPass    = m_renderGraph.GetRTHandle("CoCHelper");
  CoCHelperPass2   = m_renderGraph.GetRTHandle("CoCHelper2");

  //
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);
  m_flare.Init(PrimitiveMgr);

  SceneProp.SSAOKernel.InitTexture();

  EnvMapTexIndex = g_pBaseDriver->CreateTexture(string("CubeMap_Mountains.dds"));

  int index = PrimitiveMgr.CreateMesh("Models/SkyBox.X");
  Meshes[1].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
  Meshes[1].TranslateAbsolute(0.0, -10.0f, 0.0f);
  Meshes[1].Update();

  index = PrimitiveMgr.CreateMesh("Models/SponzaEsc.X");
  Meshes[0].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);

  index = PrimitiveMgr.CreateSpline(m_sceneSetup.splines[0]);
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

  t800::Spline& m_spline = m_sceneSetup.splines[0];
  t800::SplineAgent& m_agent = m_sceneSetup.agents[0];
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

void SC_Day::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void SC_Day::OnDestoryScene() {
  DestroyAssets();
}

void SC_Day::DestroyAssets() {
  PrimitiveMgr.DestroyPrimitives();
  pFramework->pVideoDriver->DestroyRTs();
  //pFramework->pVideoDriver->DestroyShaders();
  //pFramework->pVideoDriver->DestroyTextures();
  //pFramework->pVideoDriver->DestroyTechniques();
}

void SC_Day::OnUpdate(float _DtSecs) {
  Camera& Cam = m_sceneSetup.cameras[0];
  Camera& LightCam = m_sceneSetup.lightCameras[0];
  t800::SplineAgent& m_agent = m_sceneSetup.agents[0];

  static float totalTime = 0.0f;
  static int frameCounter = 0;
  totalTime += _DtSecs;
  frameCounter++;
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

  // Normal camera/light updates (skipped when feed is active or dump pending)
  if (!m_dumper.SkipCameraUpdates()) {
    m_agent.Update(DtSecs);
    ActiveCam->Update(DtSecs);
    VP = ActiveCam->VP;
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.pLightCameras[0]->Yaw -= 0.008f *DtSecs;
    SceneProp.pLightCameras[0]->Update(DtSecs);
  }


  if (totalTime > 150.0f) {
    totalTime = 0.0;
#ifdef T850_HEADLESS
    exit(0);
#else
    pFramework->pBaseApp->LoadScene(1);
#endif
  }
}

void SC_Day::OnInput(InputManager* IManager) {
  Camera& Cam = m_sceneSetup.cameras[0];
  Camera& LightCam = m_sceneSetup.lightCameras[0];

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

  if (IManager->PressedOnceKey(T800K_k)) {
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

  if (IManager->PressedKey(T800K_q)) {
	  ActiveCam->MoveUp(DtSecs);
  }

  if (IManager->PressedKey(T800K_e)) {
	  ActiveCam->MoveDown(DtSecs);
  }

  if (IManager->PressedKey(T800K_KP3)) {
	  ActiveCam->MoveRoll(DtSecs);
  }

  if (IManager->PressedKey(T800K_KP1)) {
	  ActiveCam->MoveRoll(-DtSecs);
  }

  if (IManager->PressedOnceKey(T800K_SPACE)) {
    m_dumper.RequestDump();
  }

  if (IManager->PressedOnceKey(T800K_1)) {
    pFramework->ChangeAPI(GRAPHICS_API::D3D11);
  }

  if (IManager->PressedOnceKey(T800K_2)) {
    pFramework->ChangeAPI(GRAPHICS_API::OPENGL);
  }
  if (IManager->PressedOnceKey(T800K_3)) {
    pFramework->pVideoDriver->ModifyRT(DepthPass,0, BaseRT::NOTHING, BaseRT::F32, 128, 128, false);
  }
  // Skip mouse-driven camera movement when replay snapshot is active
  if (!m_dumper.IsReplayActive()) {
    float yaw = 0.005f*static_cast<float>(IManager->xDelta);
    ActiveCam->MoveYaw(yaw);
    float pitch = 0.005f*static_cast<float>(IManager->yDelta);
    ActiveCam->MovePitch(pitch);
  }
}

void SC_Day::OnDraw() {
  Camera& Cam = m_sceneSetup.cameras[0];
  Camera& LightCam = m_sceneSetup.lightCameras[0];

  // Execute the render graph (all passes up to and including HDR Composition)
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

#ifdef T850_HEADLESS
  //Save file
  const float timeToScreenshot = 5.0;
  static float screenshotTime = 0.0;
  static int screenshotNum = 0;
  screenshotTime += DtSecs;
  if (screenshotTime >= timeToScreenshot) {
    screenshotTime = 0;
    pFramework->pVideoDriver->SaveScreenshot("Test_" + std::to_string(screenshotNum));
    screenshotNum++;
  }
#else
  // RT Dump via FrameDumper
  if (m_dumper.ShouldDump(DtSecs)) {
    std::vector<RTDumpEntry> rts = {
      {GBufferPass,     BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Color0"},
      {GBufferPass,     BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {GBufferPass,     BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Depth"},
      {DepthPass,       BaseDriver::DEPTH_ATTACHMENT,  "ShadowMap_Depth"},
      {ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {DeferredPass,    BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {Extra16FPass,    BaseDriver::COLOR0_ATTACHMENT, "Extra16F"},
      {ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
      {BloomAccumPass,  BaseDriver::COLOR0_ATTACHMENT, "Bloom"},
      {GodRaysCalcPass, BaseDriver::COLOR0_ATTACHMENT, "GodRays"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs);
    if (m_dumper.ShouldExit()) exit(0);
  }

  if (SceneProp.pCameras[0]->Eye.y > 80) {
    m_flare.Draw();
  }
#endif
}

void  SC_Day::ChangeSettingsOnPlus() {
  Camera& LightCam = m_sceneSetup.lightCameras[0];
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
  case CHANGE_PCF_TOOGLE: {
	  int prevVal = SceneProp.ToogleShadow;
	  SceneProp.ToogleShadow = 1;
	  cout << "[CHANGE_PCF_TOOGLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ToogleShadow << "]" << endl;
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  int prevVal = SceneProp.ToogleSSAO;
	  SceneProp.ToogleSSAO = 1;
	  cout << "[CHANGLE_SSAO_TOOGLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ToogleSSAO << "]" << endl;
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    float prevVal = LightCam.NPlane;
    LightCam.NPlane += 1.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    cout << "[CHANGE_LIGHT_NEAR_PLANE] Previous Value[" << prevVal << "] Actual Value[" << LightCam.NPlane << "]" << endl;
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    float prevVal = LightCam.FPlane;
    LightCam.FPlane += 10.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    cout << "[CHANGE_LIGHT_FAR_PLANE] Previous Value[" << prevVal << "] Actual Value[" << LightCam.FPlane << "]" << endl;
  }break;
  }
}

void  SC_Day::ChangeSettingsOnMinus() {
  Camera& LightCam = m_sceneSetup.lightCameras[0];
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
  case CHANGE_PCF_TOOGLE: {
	  int prevVal = SceneProp.ToogleShadow;
	  SceneProp.ToogleShadow = 0;
	  cout << "[CHANGE_PCF_TOOGLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ToogleShadow << "]" << endl;
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  int prevVal = SceneProp.ToogleSSAO;
	  SceneProp.ToogleSSAO = 0;
	  cout << "[CHANGLE_SSAO_TOOGLE] Previous Value[" << prevVal << "] Actual Value[" << SceneProp.ToogleSSAO << "]" << endl;
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    float prevVal = LightCam.NPlane;
    LightCam.NPlane -= 1.0f;
    if (LightCam.NPlane < 0.1f) LightCam.NPlane = 0.1f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    cout << "[CHANGE_LIGHT_NEAR_PLANE] Previous Value[" << prevVal << "] Actual Value[" << LightCam.NPlane << "]" << endl;
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    float prevVal = LightCam.FPlane;
    LightCam.FPlane -= 10.0f;
    if (LightCam.FPlane < 1.0f) LightCam.FPlane = 1.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    cout << "[CHANGE_LIGHT_FAR_PLANE] Previous Value[" << prevVal << "] Actual Value[" << LightCam.FPlane << "]" << endl;
  }break;
  }
}

void SC_Day::printCurrSelection() {
  Camera& LightCam = m_sceneSetup.lightCameras[0];
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
  case CHANGE_PCF_TOOGLE: {
	  cout << "Option[CHANGE_PCF_TOOGLE] Value[" << SceneProp.ToogleShadow << "]" << endl;
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  cout << "Option[CHANGLE_SSAO_TOOGLE] Value[" << SceneProp.ToogleSSAO << "]" << endl;
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    cout << "Option[CHANGE_LIGHT_NEAR_PLANE] Value[" << LightCam.NPlane << "]" << endl;
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    cout << "Option[CHANGE_LIGHT_FAR_PLANE] Value[" << LightCam.FPlane << "]" << endl;
  }break;
  }
}
