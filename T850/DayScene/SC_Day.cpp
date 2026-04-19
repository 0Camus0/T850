#include "SC_Day.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <utils/Log.h>
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
    T8_LOG_ERROR("[SC_Day] Failed to load Scenes/SC_Day.json");
    return;
  }
  m_sceneSetup.Apply(SceneProp);

  ActiveCam = m_sceneSetup.GetCamera(0);
  ChangeActiveGaussSelection = SHADOW_KERNEL;
  m_debugRTSelection = 0;
  m_showSpline = false;
  m_showLights = false;
  m_activeCameraIndex = 0;
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
    T8_LOG_ERROR("[SC_Day] Failed to load render graph");
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
  BrightPassPass   = m_renderGraph.GetRTHandle("BrightPass");
  GodRaysCalcPass  = m_renderGraph.GetRTHandle("GodRaysCalc");
  GodRaysCalcExtraPass = m_renderGraph.GetRTHandle("GodRaysCalcExtra");
  LuminanceMapPass = m_renderGraph.GetRTHandle("LuminanceMap");
  AdaptedLumCurrentPass = m_renderGraph.GetRTHandle("AdaptedLumCurrent");
  AdaptedLumPrevPass = m_renderGraph.GetRTHandle("AdaptedLumPrev");
  CoCPass          = m_renderGraph.GetRTHandle("CoC");
  CombineCoCPass   = m_renderGraph.GetRTHandle("CombineCoC");
  CoCHelperPass    = m_renderGraph.GetRTHandle("CoCHelper");
  CoCHelperPass2   = m_renderGraph.GetRTHandle("CoCHelper2");

  //
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);
  m_flare.Init(PrimitiveMgr);

  SceneProp.SSAOKernel.InitTexture();

  EnvMapTexIndex = g_pBaseDriver->CreateTexture(m_sceneSetup.environmentMap);

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

  m_wireframeSphere.Create(8, 16);
  m_wireframeArrow.Create(24, 6);

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

  // Apply persisted toggle states that need post-asset setup
  bool dofOn = (SceneProp.ToogleDOF != 0);
  m_renderGraph.SetPassEnabled("CoC", dofOn);
  m_renderGraph.SetPassEnabled("Combine CoC", dofOn);
  m_renderGraph.SetPassEnabled("DOF", dofOn);
  m_renderGraph.SetPassEnabled("DOF 2", dofOn);
  Meshes[0].SetParallaxEnabled(SceneProp.ToogleParallax != 0);
  Meshes[0].SetParallaxShadowEnabled(SceneProp.ToogleParallaxShadow != 0);

  // Initialize light camera direction so LightDir is valid from the first frame
  {
    Camera& LightCam = m_sceneSetup.lightCameras[0];
    LightCam.Update(0.0f);
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.Lights[0].Direction = LightCam.Look;
  }

  // Sync cubemap index to match loaded environment_map
  auto& selDescs = m_sceneSetup.descriptor.selectors;
  for (auto& sd : selDescs) {
    if (sd.name == "cubemap") {
      std::string envFile = m_sceneSetup.environmentMap;
      size_t slashPos = envFile.rfind('/');
      if (slashPos != std::string::npos) envFile = envFile.substr(slashPos + 1);
      for (int i = 0; i < (int)sd.options.size(); i++) {
        if (sd.options[i] == envFile) { m_currentCubemapIndex = i; break; }
      }
      break;
    }
  }
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
  // Only advance scene timer when spline camera is driving the tour
  if (ActiveCam->m_externalControl)
    totalTime += _DtSecs;
  frameCounter++;
  DtSecs = _DtSecs;
  SceneProp.FrameDeltaSec = DtSecs;
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
    SceneProp.pLightCameras[0]->Yaw -= 0.008f *DtSecs;
    SceneProp.pLightCameras[0]->Update(DtSecs);
    // Capture light position AFTER auto-rotation so shadow matches lighting
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.Lights[0].Direction = LightCam.Look;
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
    T8_LOG_VERBOSE("Position[%f,%f,%f] Rot[%f,%f,%f] Sc[%f]", Position.x, Position.y, Position.z, Orientation.x, Orientation.y, Orientation.z, Scaling.x);
  }

  if (IManager->PressedOnceKey(T800K_k)) {
    T8_LOG_INFO("Position[%f, %f, %f]", ActiveCam->Eye.x, ActiveCam->Eye.y, ActiveCam->Eye.z);
    T8_LOG_INFO("Orientation[%f, %f, %f]", ActiveCam->Pitch, ActiveCam->Roll, ActiveCam->Yaw);
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

  // Toggle spline-guided / free camera
  if (IManager->PressedOnceKey(T800K_t)) {
    if (ActiveCam->m_externalControl) {
      // Detach from spline: keep current position and orientation
      ActiveCam->DettachAgent();
      ActiveCam->m_externalControl = false;
      T8_LOG_INFO("[CAMERA] Switched to FREE camera");
    }
    else {
      // Re-attach to spline agent
      t800::SplineAgent& agent = m_sceneSetup.agents[0];
      ActiveCam->AttachAgent(agent);
      ActiveCam->m_lookAtCenter = false;
      T8_LOG_INFO("[CAMERA] Switched to SPLINE camera");
    }
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
    pFramework->ChangeAPI(GRAPHICS_API::D3D12);
  }
  if (IManager->PressedOnceKey(T800K_4)) {
    pFramework->ChangeAPI(GRAPHICS_API::VULKAN);
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
      {GBufferPass,     BaseDriver::COLOR2_ATTACHMENT, "GBuffer_Color2"},
      {GBufferPass,     BaseDriver::COLOR3_ATTACHMENT, "GBuffer_Color3"},
      {GBufferPass,     BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Depth"},
      {DepthPass,       BaseDriver::DEPTH_ATTACHMENT,  "ShadowMap_Depth"},
      {ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {DeferredPass,    BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {Extra16FPass,    BaseDriver::COLOR0_ATTACHMENT, "Extra16F"},
      {ExtraHelperPass, BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
      {BloomAccumPass,  BaseDriver::COLOR0_ATTACHMENT, "Bloom"},
      {GodRaysCalcPass, BaseDriver::COLOR0_ATTACHMENT, "GodRays"},
      {LuminanceMapPass, BaseDriver::COLOR0_ATTACHMENT, "LuminanceMap"},
      {AdaptedLumCurrentPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumCurrent"},
      {AdaptedLumPrevPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumPrev"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs);
    if (m_dumper.ShouldExit()) exit(0);
  }

  // Debug RT override: draw selected render target fullscreen
  if (m_debugRTSelection > 0) {
    int selected = -1;
    int attachment = BaseDriver::COLOR0_ATTACHMENT;
    switch (m_debugRTSelection) {
    case 1:  selected = GBufferPass;     attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Albedo
    case 2:  selected = GBufferPass;     attachment = BaseDriver::COLOR1_ATTACHMENT; break; // Normals
    case 3:  selected = GBufferPass;     attachment = BaseDriver::COLOR2_ATTACHMENT; break; // Specular
    case 4:  selected = GBufferPass;     attachment = BaseDriver::COLOR3_ATTACHMENT; break; // Emissive
    case 5:  selected = GBufferPass;     attachment = BaseDriver::COLOR4_ATTACHMENT; break; // GBuf Depth
    case 6:  selected = DepthPass;       attachment = BaseDriver::DEPTH_ATTACHMENT;  break; // Shadow Map
    case 7:  selected = ShadowAccumPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Shadow Accum
    case 8:  selected = DeferredPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Deferred
    case 9:  selected = Extra16FPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Extra16F
    case 10: selected = ExtraHelperPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break; // HDR
    case 11: selected = BloomAccumPass;  attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Bloom
    case 12: selected = GodRaysCalcPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break; // God Rays
    case 13: selected = LuminanceMapPass;attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Luminance
    case 14: selected = CoCPass;         attachment = BaseDriver::COLOR0_ATTACHMENT; break; // CoC
    case 15: selected = BrightPassPass;  attachment = BaseDriver::COLOR0_ATTACHMENT; break; // Bright
    }
    if (selected >= 0) {
      Quads[7].SetTexture(pFramework->pVideoDriver->GetRTTexture(selected, attachment), 0);
      ShaderKey dbgKey(0); dbgKey.setPass(PassType::FSQUAD_1_TEX); dbgKey.bits |= ShaderKey::HAS_TEXCOORD0;
      Quads[7].SetGlobalKey(dbgKey);
      Quads[7].Draw();
    }
  }

  if (SceneProp.pCameras[0]->Eye.y > 80) {
    m_flare.Draw();
  }

  // Draw spline wireframe overlay
  if (m_showSpline) {
    splineInst.Draw();
  }

  // Draw light gizmos overlay
  if (m_showLights) {
    unsigned int numLights = SceneProp.ActiveLights;
    if (numLights > SceneProp.Lights.size())
      numLights = static_cast<unsigned int>(SceneProp.Lights.size());
    for (unsigned int i = 0; i < numLights; i++) {
      Light& light = SceneProp.Lights[i];
      if (light.Type == LIGHT_DIRECTIONAL) {
        // Draw arrow gizmo at an editor position above scene center
        XVECTOR3 gizmoPos(0.0f, 80.0f, 0.0f);
        m_wireframeArrow.Draw(VP, gizmoPos, light.Direction, 10.0f);
      } else {
        m_wireframeSphere.Draw(VP, light.Position, light.radius);
      }
    }
  }

#endif
}

void  SC_Day::ChangeSettingsOnPlus() {
  Camera& LightCam = m_sceneSetup.lightCameras[0];
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    float prevVal = SceneProp.Exposure;
    SceneProp.Exposure += 0.1f;
    T8_LOG_VERBOSE("[CHANGE_EXPOSURE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Exposure);
  }break;
  case CHANGE_BLOOM_FACTOR: {
    float prevVal = SceneProp.BloomFactor;
    SceneProp.BloomFactor += 0.1f;
    T8_LOG_VERBOSE("[CHANGE_BLOOM_FACTOR] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.BloomFactor);
  }break;
  case CHANGE_BLOOM_THRESHOLD: {
    float prevVal = SceneProp.BloomThreshold;
    SceneProp.BloomThreshold += 0.5f;
    T8_LOG_VERBOSE("[CHANGE_BLOOM_THRESHOLD] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.BloomThreshold);
  }break;
  case CHANGE_TM_WHITE_LEVEL: {
    float prevVal = SceneProp.ToneMapWhiteLevel;
    SceneProp.ToneMapWhiteLevel += 0.25f;
    T8_LOG_VERBOSE("[CHANGE_TM_WHITE_LEVEL] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ToneMapWhiteLevel);
  }break;
  case CHANGE_TM_ADAPT_TAU: {
    float prevVal = SceneProp.LuminanceTau;
    SceneProp.LuminanceTau += 0.1f;
    T8_LOG_VERBOSE("[CHANGE_TM_ADAPT_TAU] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LuminanceTau);
  }break;
  case CHANGE_NUM_LIGHTS: {
    int prevVal = SceneProp.ActiveLights;
    SceneProp.ActiveLights *= 2;
    if (SceneProp.ActiveLights >= 127) {
      SceneProp.ActiveLights = 127;
    }
    T8_LOG_VERBOSE("[CHANGE_NUM_LIGHTS] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ActiveLights);
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    int prevVal = ChangeActiveGaussSelection;
    ChangeActiveGaussSelection++;
    if (ChangeActiveGaussSelection >= (int)SceneProp.pGaussKernels.size()) {
      ChangeActiveGaussSelection = static_cast<int>(SceneProp.pGaussKernels.size()) - 1;
    }
    T8_LOG_VERBOSE("[CHANGE_ACTIVE_GAUSS_KERNEL] Previous Value[%d] Actual Value[%d]", prevVal, ChangeActiveGaussSelection);
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    int prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize += 2;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize);
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius += 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius);
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma += 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_DEVIATION] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma);
  }break;
  case CHANGE_PCF_RADIUS: {
    float prevVal = SceneProp.PCFScale;
    SceneProp.PCFScale += 0.1f;
    T8_LOG_VERBOSE("[CHANGE_PCF_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.PCFScale);
  }break;
  case CHANGE_PCF_SAMPLES: {
    float prevVal = SceneProp.PCFSamples;
    SceneProp.PCFSamples++;
    T8_LOG_VERBOSE("[CHANGE_PCF_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.PCFSamples);
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    float prevVal = (float)SceneProp.SSAOKernel.KernelSize;
    SceneProp.SSAOKernel.KernelSize += 2;
    SceneProp.SSAOKernel.Update();
    T8_LOG_VERBOSE("[CHANGE_SSAO_KERNEL_SIZE] Previous Value[%f] Actual Value[%d]", prevVal, SceneProp.SSAOKernel.KernelSize);
  }break;
  case CHANGE_SSAO_RADIUS: {
    float prevVal = SceneProp.SSAOKernel.Radius;
    SceneProp.SSAOKernel.Radius += 0.5f;
    T8_LOG_VERBOSE("[CHANGE_SSAO_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.SSAOKernel.Radius);
  }break;
  case CHANGE_DOF_APERTURE: {
    float prevVal = SceneProp.Aperture;
    SceneProp.Aperture += 10.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_APERTURE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Aperture);
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    float prevVal = SceneProp.FocalLength;
    SceneProp.FocalLength += 10.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_FOCAL_LENGHT] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.FocalLength);
  }break;
  case CHANGE_DOF_MAX_COC: {
    float prevVal = SceneProp.MaxCoc;
    SceneProp.MaxCoc += 0.5f;
    T8_LOG_VERBOSE("[CHANGE_DOF_MAX_COC] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.MaxCoc);
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Far_Samples_squared;
    SceneProp.DOF_Far_Samples_squared += 1.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_FAR_SAMPLE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.DOF_Far_Samples_squared);
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Near_Samples_squared;
    SceneProp.DOF_Near_Samples_squared += 1.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_NEAR_SAMPLE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.DOF_Near_Samples_squared);
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    bool prevVal = SceneProp.AutoFocus;
    SceneProp.AutoFocus = true;
    T8_LOG_VERBOSE("[CHANGE_DOF_AUTO_FOCUS] Previous Value[%d] Actual Value[%d]", (int)prevVal, (int)SceneProp.AutoFocus);
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    float prevVal = SceneProp.ParallaxLowSamples;
    SceneProp.ParallaxLowSamples += 5.0f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_LOW_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxLowSamples);
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    float prevVal = SceneProp.ParallaxHighSamples;
    SceneProp.ParallaxHighSamples += 10.0f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_HIGH_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxHighSamples);
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    float prevVal = SceneProp.ParallaxHeight;
    SceneProp.ParallaxHeight += 0.01f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_HEIGHT] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxHeight);
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    float prevVal = SceneProp.LightVolumeSteps;
    SceneProp.LightVolumeSteps += 16.0f;
    T8_LOG_VERBOSE("[CHANGE_LIGHT_VOLUME_STEPS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightVolumeSteps);
  }break;
  case CHANGE_PCF_TOOGLE: {
	  int prevVal = SceneProp.ToogleShadow;
	  SceneProp.ToogleShadow = 1;
	  T8_LOG_VERBOSE("[CHANGE_PCF_TOOGLE] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ToogleShadow);
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  int prevVal = SceneProp.ToogleSSAO;
	  SceneProp.ToogleSSAO = 1;
	  T8_LOG_VERBOSE("[CHANGLE_SSAO_TOOGLE] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ToogleSSAO);
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    float prevVal = LightCam.NPlane;
    LightCam.NPlane += 1.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    T8_LOG_VERBOSE("[CHANGE_LIGHT_NEAR_PLANE] Previous Value[%f] Actual Value[%f]", prevVal, LightCam.NPlane);
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    float prevVal = LightCam.FPlane;
    LightCam.FPlane += 10.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    T8_LOG_VERBOSE("[CHANGE_LIGHT_FAR_PLANE] Previous Value[%f] Actual Value[%f]", prevVal, LightCam.FPlane);
  }break;
  case CHANGE_FOV: {
    float prevVal = ActiveCam->Fov;
    ActiveCam->SetFov(ActiveCam->Fov + Deg2Rad(5.0f));
    if (ActiveCam->Fov > Deg2Rad(150.0f)) ActiveCam->SetFov(Deg2Rad(150.0f));
    T8_LOG_VERBOSE("[CHANGE_FOV] Previous Value[%f] Actual Value[%f]", Rad2Deg(prevVal), Rad2Deg(ActiveCam->Fov));
  }break;
  case CHANGE_SHOW_SPLINE: {
    m_showSpline = true;
    T8_LOG_VERBOSE("[CHANGE_SHOW_SPLINE] Value[%d]", (int)m_showSpline);
  }break;
  case CHANGE_SHOW_LIGHTS: {
    m_showLights = true;
    T8_LOG_VERBOSE("[CHANGE_SHOW_LIGHTS] Value[%d]", (int)m_showLights);
  }break;
  case CHANGE_LIGHT_INTENSITY: {
    if (!SceneProp.Lights.empty()) {
      float prevVal = SceneProp.Lights[0].Intensity;
      SceneProp.Lights[0].Intensity += 0.5f;
      if (SceneProp.Lights[0].Intensity > 20.0f) SceneProp.Lights[0].Intensity = 20.0f;
      T8_LOG_VERBOSE("[CHANGE_LIGHT_INTENSITY] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Lights[0].Intensity);
    }
  }break;
  }
}

void  SC_Day::ChangeSettingsOnMinus() {
  Camera& LightCam = m_sceneSetup.lightCameras[0];
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    float prevVal = SceneProp.Exposure;
    SceneProp.Exposure -= 0.1f;
    T8_LOG_VERBOSE("[CHANGE_EXPOSURE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Exposure);
  }break;
  case CHANGE_BLOOM_FACTOR: {
    float prevVal = SceneProp.BloomFactor;
    SceneProp.BloomFactor -= 0.1f;
    T8_LOG_VERBOSE("[CHANGE_BLOOM_FACTOR] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.BloomFactor);
  }break;
  case CHANGE_BLOOM_THRESHOLD: {
    float prevVal = SceneProp.BloomThreshold;
    SceneProp.BloomThreshold -= 0.5f;
    if (SceneProp.BloomThreshold < 0.0f) SceneProp.BloomThreshold = 0.0f;
    T8_LOG_VERBOSE("[CHANGE_BLOOM_THRESHOLD] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.BloomThreshold);
  }break;
  case CHANGE_TM_WHITE_LEVEL: {
    float prevVal = SceneProp.ToneMapWhiteLevel;
    SceneProp.ToneMapWhiteLevel -= 0.25f;
    if (SceneProp.ToneMapWhiteLevel < 0.5f) SceneProp.ToneMapWhiteLevel = 0.5f;
    T8_LOG_VERBOSE("[CHANGE_TM_WHITE_LEVEL] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ToneMapWhiteLevel);
  }break;
  case CHANGE_TM_ADAPT_TAU: {
    float prevVal = SceneProp.LuminanceTau;
    SceneProp.LuminanceTau -= 0.1f;
    if (SceneProp.LuminanceTau < 0.05f) SceneProp.LuminanceTau = 0.05f;
    T8_LOG_VERBOSE("[CHANGE_TM_ADAPT_TAU] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LuminanceTau);
  }break;
  case CHANGE_NUM_LIGHTS: {
    int prevVal = SceneProp.ActiveLights;
    SceneProp.ActiveLights /= 2;
    if (SceneProp.ActiveLights <= 0) {
      SceneProp.ActiveLights = 1;
    }
    T8_LOG_VERBOSE("[CHANGE_NUM_LIGHTS] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ActiveLights);
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    int prevVal = ChangeActiveGaussSelection;
    ChangeActiveGaussSelection--;
    if (ChangeActiveGaussSelection < 0) {
      ChangeActiveGaussSelection = 0;
    }
    T8_LOG_VERBOSE("[CHANGE_ACTIVE_GAUSS_KERNEL] Previous Value[%d] Actual Value[%d]", prevVal, ChangeActiveGaussSelection);
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    int prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize -= 2;
    if (SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize <= 2) {
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize = 3;
    }
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize);
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius -= 0.5f;
    if (SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius <= 0.5f) {
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius = 0.5f;
    }
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius);
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    float prevVal = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma -= 0.5;
    SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    T8_LOG_VERBOSE("[CHANGE_GAUSS_KERNEL_DEVIATION] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma);
  }break;
  case CHANGE_PCF_RADIUS: {
    float prevVal = SceneProp.PCFScale;
    SceneProp.PCFScale -= 0.1f;
    T8_LOG_VERBOSE("[CHANGE_PCF_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.PCFScale);
  }break;
  case CHANGE_PCF_SAMPLES: {
    float prevVal = SceneProp.PCFSamples;
    SceneProp.PCFSamples--;
    T8_LOG_VERBOSE("[CHANGE_PCF_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.PCFSamples);
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    float prevVal = (float)SceneProp.SSAOKernel.KernelSize;
    SceneProp.SSAOKernel.KernelSize -= 2;
    SceneProp.SSAOKernel.Update();
    T8_LOG_VERBOSE("[CHANGE_SSAO_KERNEL_SIZE] Previous Value[%f] Actual Value[%d]", prevVal, SceneProp.SSAOKernel.KernelSize);
  }break;
  case CHANGE_SSAO_RADIUS: {
    float prevVal = SceneProp.SSAOKernel.Radius;
    SceneProp.SSAOKernel.Radius -= 0.5f;
    T8_LOG_VERBOSE("[CHANGE_SSAO_RADIUS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.SSAOKernel.Radius);
  }break;
  case CHANGE_DOF_APERTURE: {
    float prevVal = SceneProp.Aperture;
    SceneProp.Aperture -= 10.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_APERTURE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Aperture);
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    float prevVal = SceneProp.FocalLength;
    SceneProp.FocalLength -= 10.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_FOCAL_LENGHT] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.FocalLength);
  }break;
  case CHANGE_DOF_MAX_COC: {
    float prevVal = SceneProp.MaxCoc;
    SceneProp.MaxCoc -= 0.5f;
    T8_LOG_VERBOSE("[CHANGE_DOF_MAX_COC] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.MaxCoc);
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Far_Samples_squared;
    SceneProp.DOF_Far_Samples_squared -= 1.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_FAR_SAMPLE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.DOF_Far_Samples_squared);
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    float prevVal = SceneProp.DOF_Near_Samples_squared;
    SceneProp.DOF_Near_Samples_squared -= 1.0f;
    T8_LOG_VERBOSE("[CHANGE_DOF_NEAR_SAMPLE] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.DOF_Near_Samples_squared);
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    bool prevVal = SceneProp.AutoFocus;
    SceneProp.AutoFocus = false;
    T8_LOG_VERBOSE("[CHANGE_DOF_AUTO_FOCUS] Previous Value[%d] Actual Value[%d]", (int)prevVal, (int)SceneProp.AutoFocus);
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    float prevVal = SceneProp.ParallaxLowSamples;
    SceneProp.ParallaxLowSamples -= 5.0f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_LOW_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxLowSamples);
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    float prevVal = SceneProp.ParallaxHighSamples;
    SceneProp.ParallaxHighSamples -= 10.0f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_HIGH_SAMPLES] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxHighSamples);
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    float prevVal = SceneProp.ParallaxHeight;
    SceneProp.ParallaxHeight -= 0.01f;
    T8_LOG_VERBOSE("[CHANGE_PARALLAX_HEIGHT] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.ParallaxHeight);
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    float prevVal = SceneProp.LightVolumeSteps;
    SceneProp.LightVolumeSteps -= 16.0f;
    T8_LOG_VERBOSE("[CHANGE_LIGHT_VOLUME_STEPS] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.LightVolumeSteps);
  }break;
  case CHANGE_PCF_TOOGLE: {
	  int prevVal = SceneProp.ToogleShadow;
	  SceneProp.ToogleShadow = 0;
	  T8_LOG_VERBOSE("[CHANGE_PCF_TOOGLE] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ToogleShadow);
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  int prevVal = SceneProp.ToogleSSAO;
	  SceneProp.ToogleSSAO = 0;
	  T8_LOG_VERBOSE("[CHANGLE_SSAO_TOOGLE] Previous Value[%d] Actual Value[%d]", prevVal, SceneProp.ToogleSSAO);
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    float prevVal = LightCam.NPlane;
    LightCam.NPlane -= 1.0f;
    if (LightCam.NPlane < 0.1f) LightCam.NPlane = 0.1f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    T8_LOG_VERBOSE("[CHANGE_LIGHT_NEAR_PLANE] Previous Value[%f] Actual Value[%f]", prevVal, LightCam.NPlane);
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    float prevVal = LightCam.FPlane;
    LightCam.FPlane -= 10.0f;
    if (LightCam.FPlane < 1.0f) LightCam.FPlane = 1.0f;
    LightCam.CreatePojection();
    LightCam.Update(0);
    T8_LOG_VERBOSE("[CHANGE_LIGHT_FAR_PLANE] Previous Value[%f] Actual Value[%f]", prevVal, LightCam.FPlane);
  }break;
  case CHANGE_FOV: {
    float prevVal = ActiveCam->Fov;
    ActiveCam->SetFov(ActiveCam->Fov - Deg2Rad(5.0f));
    if (ActiveCam->Fov < Deg2Rad(60.0f)) ActiveCam->SetFov(Deg2Rad(60.0f));
    T8_LOG_VERBOSE("[CHANGE_FOV] Previous Value[%f] Actual Value[%f]", Rad2Deg(prevVal), Rad2Deg(ActiveCam->Fov));
  }break;
  case CHANGE_SHOW_SPLINE: {
    m_showSpline = false;
    T8_LOG_VERBOSE("[CHANGE_SHOW_SPLINE] Value[%d]", (int)m_showSpline);
  }break;
  case CHANGE_SHOW_LIGHTS: {
    m_showLights = false;
    T8_LOG_VERBOSE("[CHANGE_SHOW_LIGHTS] Value[%d]", (int)m_showLights);
  }break;
  case CHANGE_LIGHT_INTENSITY: {
    if (!SceneProp.Lights.empty()) {
      float prevVal = SceneProp.Lights[0].Intensity;
      SceneProp.Lights[0].Intensity -= 0.5f;
      if (SceneProp.Lights[0].Intensity < 0.1f) SceneProp.Lights[0].Intensity = 0.1f;
      T8_LOG_VERBOSE("[CHANGE_LIGHT_INTENSITY] Previous Value[%f] Actual Value[%f]", prevVal, SceneProp.Lights[0].Intensity);
    }
  }break;
  }
}

void SC_Day::printCurrSelection() {
  Camera& LightCam = m_sceneSetup.lightCameras[0];
  switch (SceneSettingSelection) {
  case CHANGE_EXPOSURE: {
    T8_LOG_VERBOSE("Option[CHANGE_EXPOSURE] Value[%f]", SceneProp.Exposure);
  }break;
  case CHANGE_BLOOM_FACTOR: {
    T8_LOG_VERBOSE("Option[CHANGE_BLOOM_FACTOR] Value[%f]", SceneProp.BloomFactor);
  }break;
  case CHANGE_BLOOM_THRESHOLD: {
    T8_LOG_VERBOSE("Option[CHANGE_BLOOM_THRESHOLD] Value[%f]", SceneProp.BloomThreshold);
  }break;
  case CHANGE_TM_WHITE_LEVEL: {
    T8_LOG_VERBOSE("Option[CHANGE_TM_WHITE_LEVEL] Value[%f]", SceneProp.ToneMapWhiteLevel);
  }break;
  case CHANGE_TM_ADAPT_TAU: {
    T8_LOG_VERBOSE("Option[CHANGE_TM_ADAPT_TAU] Value[%f]", SceneProp.LuminanceTau);
  }break;
  case CHANGE_NUM_LIGHTS: {
    T8_LOG_VERBOSE("Option[CHANGE_NUM_LIGHTS] Value[%d]", SceneProp.ActiveLights);
  }break;
  case CHANGE_ACTIVE_GAUSS_KERNEL: {
    T8_LOG_VERBOSE("Option[CHANGE_ACTIVE_GAUSS_KERNEL] Value[%d]", ChangeActiveGaussSelection);
  }break;
  case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
    T8_LOG_VERBOSE("Option[CHANGE_GAUSS_KERNEL_SAMPLE_COUNT] Value[%d]", SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize);
  }break;
  case CHANGE_GAUSS_KERNEL_RADIUS: {
    T8_LOG_VERBOSE("Option[CHANGE_GAUSS_KERNEL_RADIUS] Value[%f]", SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius);
  }break;
  case CHANGE_GAUSS_KERNEL_DEVIATION: {
    T8_LOG_VERBOSE("Option[CHANGE_GAUSS_KERNEL_DEVIATION] Value[%f]", SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma);
  }break;
  case CHANGE_PCF_RADIUS: {
    T8_LOG_VERBOSE("Option[CHANGE_PCF_RADIUS] Value[%f]", SceneProp.PCFScale);
  }break;
  case CHANGE_PCF_SAMPLES: {
    T8_LOG_VERBOSE("Option[CHANGE_PCF_SAMPLES] Value[%f]", SceneProp.PCFSamples);
  }break;
  case CHANGE_SSAO_KERNEL_SIZE: {
    T8_LOG_VERBOSE("Option[CHANGE_SSAO_KERNEL_SIZE] Value[%d]", SceneProp.SSAOKernel.KernelSize);
  }break;
  case CHANGE_SSAO_RADIUS: {
    T8_LOG_VERBOSE("Option[CHANGE_SSAO_RADIUS] Value[%f]", SceneProp.SSAOKernel.Radius);
  }break;
  case CHANGE_DOF_APERTURE: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_APERTURE] Value[%f]", SceneProp.Aperture);
  }break;
  case CHANGE_DOF_FOCAL_LENGHT: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_FOCAL_LENGHT] Value[%f]", SceneProp.FocalLength);
  }break;
  case CHANGE_DOF_MAX_COC: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_MAX_COC] Value[%f]", SceneProp.MaxCoc);
  }break;
  case CHANGE_DOF_FAR_SAMPLE: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_FAR_SAMPLE] Value[%f]", SceneProp.DOF_Far_Samples_squared);
  }break;
  case CHANGE_DOF_NEAR_SAMPLE: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_NEAR_SAMPLE] Value[%f]", SceneProp.DOF_Near_Samples_squared);
  }break;
  case CHANGE_DOF_AUTO_FOCUS: {
    T8_LOG_VERBOSE("Option[CHANGE_DOF_AUTO_FOCUS] Value[%d]", (int)SceneProp.AutoFocus);
  }break;
  case CHANGE_PARALLAX_LOW_SAMPLES: {
    T8_LOG_VERBOSE("Option[CHANGE_PARALLAX_LOW_SAMPLES] Value[%f]", SceneProp.ParallaxLowSamples);
  }break;
  case CHANGE_PARALLAX_HIGH_SAMPLES: {
    T8_LOG_VERBOSE("Option[CHANGE_PARALLAX_HIGH_SAMPLES] Value[%f]", SceneProp.ParallaxHighSamples);
  }break;
  case CHANGE_PARALLAX_HEIGHT: {
    T8_LOG_VERBOSE("Option[CHANGE_PARALLAX_HEIGHT] Value[%f]", SceneProp.ParallaxHeight);
  }break;
  case CHANGE_LIGHT_VOLUME_STEPS: {
    T8_LOG_VERBOSE("Option[CHANGE_LIGHT_VOLUME_STEPS] Value[%f]", SceneProp.LightVolumeSteps);
  }break;
  case CHANGE_PCF_TOOGLE: {
	  T8_LOG_VERBOSE("Option[CHANGE_PCF_TOOGLE] Value[%d]", SceneProp.ToogleShadow);
  }break;
  case CHANGLE_SSAO_TOOGLE: {
	  T8_LOG_VERBOSE("Option[CHANGLE_SSAO_TOOGLE] Value[%d]", SceneProp.ToogleSSAO);
  }break;
  case CHANGE_LIGHT_NEAR_PLANE: {
    T8_LOG_VERBOSE("Option[CHANGE_LIGHT_NEAR_PLANE] Value[%f]", LightCam.NPlane);
  }break;
  case CHANGE_LIGHT_FAR_PLANE: {
    T8_LOG_VERBOSE("Option[CHANGE_LIGHT_FAR_PLANE] Value[%f]", LightCam.FPlane);
  }break;
  case CHANGE_FOV: {
    T8_LOG_VERBOSE("Option[CHANGE_FOV] Value[%f]", Rad2Deg(ActiveCam->Fov));
  }break;
  case CHANGE_SHOW_SPLINE: {
    T8_LOG_VERBOSE("Option[CHANGE_SHOW_SPLINE] Value[%d]", (int)m_showSpline);
  }break;
  case CHANGE_SHOW_LIGHTS: {
    T8_LOG_VERBOSE("Option[CHANGE_SHOW_LIGHTS] Value[%d]", (int)m_showLights);
  }break;
  case CHANGE_LIGHT_INTENSITY: {
    if (!SceneProp.Lights.empty())
      T8_LOG_VERBOSE("Option[CHANGE_LIGHT_INTENSITY] Value[%f]", SceneProp.Lights[0].Intensity);
  }break;
  }
}

int SC_Day::FindLightOption(int activeLights) {
  // Match against the selector options: "1","2","4","8","16","32","64","127"
  auto& selPairs = m_sceneSetup.descriptor.selectors;
  for (auto& sd : selPairs) {
    if (sd.name == "num_lights") {
      for (size_t i = 0; i < sd.options.size(); i++) {
        if (std::atoi(sd.options[i].c_str()) == activeLights) return (int)i;
      }
    }
  }
  return 0;
}

void SC_Day::PopulateGUI(t800::GUIManager& gui) {
  struct SliderMapping {
    const char* name;
    int settingIndex;
  };
  static const SliderMapping mappings[] = {
    {"exposure",              CHANGE_EXPOSURE},
    {"bloom_factor",          CHANGE_BLOOM_FACTOR},
    {"bloom_threshold",       CHANGE_BLOOM_THRESHOLD},
    {"tm_white_level",        CHANGE_TM_WHITE_LEVEL},
    {"tm_adapt_tau",          CHANGE_TM_ADAPT_TAU},
    {"pcf_radius",            CHANGE_PCF_RADIUS},
    {"pcf_samples",           CHANGE_PCF_SAMPLES},
    {"ssao_kernel_size",      CHANGE_SSAO_KERNEL_SIZE},
    {"ssao_radius",           CHANGE_SSAO_RADIUS},
    {"dof_aperture",          CHANGE_DOF_APERTURE},
    {"dof_focal_length",      CHANGE_DOF_FOCAL_LENGHT},
    {"dof_max_coc",           CHANGE_DOF_MAX_COC},
    {"dof_far_samples",       CHANGE_DOF_FAR_SAMPLE},
    {"dof_near_samples",      CHANGE_DOF_NEAR_SAMPLE},
    {"parallax_low_samples",  CHANGE_PARALLAX_LOW_SAMPLES},
    {"parallax_high_samples", CHANGE_PARALLAX_HIGH_SAMPLES},
    {"parallax_height",       CHANGE_PARALLAX_HEIGHT},
    {"parallax_shadow_min_layers", CHANGE_PARALLAX_SHADOW_MIN_LAYERS},
    {"parallax_shadow_max_layers", CHANGE_PARALLAX_SHADOW_MAX_LAYERS},
    {"parallax_shadow_softness",   CHANGE_PARALLAX_SHADOW_SOFTNESS},
    {"parallax_shadow_strength",   CHANGE_PARALLAX_SHADOW_STRENGTH},
    {"light_volume_steps",    CHANGE_LIGHT_VOLUME_STEPS},
    {"godrays_factor",        CHANGE_GODRAYS_FACTOR},
    {"gauss_kernel_radius",   CHANGE_GAUSS_KERNEL_RADIUS},
    {"gauss_kernel_deviation", CHANGE_GAUSS_KERNEL_DEVIATION},
    {"fov",                    CHANGE_FOV},
    {"light_intensity",        CHANGE_LIGHT_INTENSITY},
    {"shadow_bias",             CHANGE_SHADOW_BIAS},
    {"shadow_min",              CHANGE_SHADOW_MIN},
    {"env_factor",              CHANGE_ENV_FACTOR},
  };

  auto& sliderDescs = m_sceneSetup.descriptor.sliders;
  for (auto& sd : sliderDescs) {
    int settingIdx = -1;
    for (auto& m : mappings) {
      if (sd.name == m.name) {
        settingIdx = m.settingIndex;
        break;
      }
    }
    gui.AddSlider(sd, settingIdx);
  }

  // Checkbox mappings
  struct CheckboxMapping {
    const char* name;
    int settingIndex;
  };
  static const CheckboxMapping cbMappings[] = {
    {"shadow_toggle",   CHANGE_PCF_TOOGLE},
    {"ssao_toggle",     CHANGLE_SSAO_TOOGLE},
    {"dof_auto_focus",  CHANGE_DOF_AUTO_FOCUS},
    {"show_spline",    CHANGE_SHOW_SPLINE},
    {"show_lights",    CHANGE_SHOW_LIGHTS},
    {"dof_toggle",     CHANGE_DOF_TOGGLE},
    {"parallax_toggle", CHANGE_PARALLAX_TOGGLE},
    {"parallax_shadow_toggle", CHANGE_PARALLAX_SHADOW_TOGGLE},
    {"godrays_toggle", CHANGE_GODRAYS_TOGGLE},
  };

  auto& cbDescs = m_sceneSetup.descriptor.checkboxes;
  for (auto& cd : cbDescs) {
    int settingIdx = -1;
    for (auto& m : cbMappings) {
      if (cd.name == m.name) {
        settingIdx = m.settingIndex;
        break;
      }
    }
    gui.AddCheckbox(cd, settingIdx);
  }

  // Selector mappings
  struct SelectorMapping {
    const char* name;
    int settingIndex;
  };
  static const SelectorMapping selMappings[] = {
    {"num_lights",          CHANGE_NUM_LIGHTS},
    {"active_gauss_kernel",      CHANGE_ACTIVE_GAUSS_KERNEL},
    {"gauss_kernel_sample_count", CHANGE_GAUSS_KERNEL_SAMPLE_COUNT},
    {"debug_render_target",       CHANGE_DEBUG_RT},
    {"active_camera",               CHANGE_ACTIVE_CAMERA},
    {"cubemap",                     CHANGE_CUBEMAP},
  };

  auto& selDescs = m_sceneSetup.descriptor.selectors;
  for (auto& sd : selDescs) {
    int settingIdx = -1;
    for (auto& m : selMappings) {
      if (sd.name == m.name) {
        settingIdx = m.settingIndex;
        break;
      }
    }
    gui.AddSelector(sd, settingIdx);
  }
}

void SC_Day::SyncToGUI(t800::GUIManager& gui) {
  for (auto& sp : gui.GetSliderPairs()) {
    auto* slider = sp.slider;
    switch (slider->settingIndex) {
    case CHANGE_EXPOSURE:           slider->SetValue(SceneProp.Exposure); break;
    case CHANGE_BLOOM_FACTOR:       slider->SetValue(SceneProp.BloomFactor); break;
    case CHANGE_BLOOM_THRESHOLD:     slider->SetValue(SceneProp.BloomThreshold); break;
    case CHANGE_TM_WHITE_LEVEL:     slider->SetValue(SceneProp.ToneMapWhiteLevel); break;
    case CHANGE_TM_ADAPT_TAU:       slider->SetValue(SceneProp.LuminanceTau); break;
    case CHANGE_PCF_RADIUS:         slider->SetValue(SceneProp.PCFScale); break;
    case CHANGE_PCF_SAMPLES:        slider->SetValue(SceneProp.PCFSamples); break;
    case CHANGE_SSAO_KERNEL_SIZE:   slider->SetValue((float)SceneProp.SSAOKernel.KernelSize); break;
    case CHANGE_SSAO_RADIUS:        slider->SetValue(SceneProp.SSAOKernel.Radius); break;
    case CHANGE_DOF_APERTURE:       slider->SetValue(SceneProp.Aperture); break;
    case CHANGE_DOF_FOCAL_LENGHT:   slider->SetValue(SceneProp.FocalLength); break;
    case CHANGE_DOF_MAX_COC:        slider->SetValue(SceneProp.MaxCoc); break;
    case CHANGE_DOF_FAR_SAMPLE:     slider->SetValue(SceneProp.DOF_Far_Samples_squared); break;
    case CHANGE_DOF_NEAR_SAMPLE:    slider->SetValue(SceneProp.DOF_Near_Samples_squared); break;
    case CHANGE_PARALLAX_LOW_SAMPLES:  slider->SetValue(SceneProp.ParallaxLowSamples); break;
    case CHANGE_PARALLAX_HIGH_SAMPLES: slider->SetValue(SceneProp.ParallaxHighSamples); break;
    case CHANGE_PARALLAX_HEIGHT:    slider->SetValue(SceneProp.ParallaxHeight); break;
    case CHANGE_PARALLAX_SHADOW_MIN_LAYERS: slider->SetValue(SceneProp.ParallaxShadowMinLayers); break;
    case CHANGE_PARALLAX_SHADOW_MAX_LAYERS: slider->SetValue(SceneProp.ParallaxShadowMaxLayers); break;
    case CHANGE_PARALLAX_SHADOW_SOFTNESS:   slider->SetValue(SceneProp.ParallaxShadowSoftness); break;
    case CHANGE_PARALLAX_SHADOW_STRENGTH:   slider->SetValue(SceneProp.ParallaxShadowStrength); break;
    case CHANGE_LIGHT_VOLUME_STEPS: slider->SetValue(SceneProp.LightVolumeSteps); break;
    case CHANGE_GODRAYS_FACTOR:    slider->SetValue(SceneProp.GodRaysFactor); break;
    case CHANGE_GAUSS_KERNEL_RADIUS:   slider->SetValue(SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius); break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: slider->SetValue(SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma); break;
    case CHANGE_FOV:                slider->SetValue(Rad2Deg(ActiveCam->Fov)); break;
    case CHANGE_LIGHT_INTENSITY:    if (!SceneProp.Lights.empty()) slider->SetValue(SceneProp.Lights[0].Intensity); break;
    case CHANGE_SHADOW_BIAS:        slider->SetValue(SceneProp.ShadowBias); break;
    case CHANGE_SHADOW_MIN:         slider->SetValue(SceneProp.ShadowMin); break;
    case CHANGE_ENV_FACTOR:         slider->SetValue(SceneProp.EnvFactor); break;
    }
  }
  for (auto& cp : gui.GetCheckboxPairs()) {
    auto* cb = cp.checkbox;
    switch (cb->settingIndex) {
    case CHANGE_PCF_TOOGLE:     cb->checked = (SceneProp.ToogleShadow != 0); break;
    case CHANGLE_SSAO_TOOGLE:   cb->checked = (SceneProp.ToogleSSAO != 0); break;
    case CHANGE_DOF_AUTO_FOCUS: cb->checked = SceneProp.AutoFocus; break;
    case CHANGE_SHOW_SPLINE:    cb->checked = m_showSpline; break;
    case CHANGE_SHOW_LIGHTS:    cb->checked = m_showLights; break;
    case CHANGE_DOF_TOGGLE:     cb->checked = (SceneProp.ToogleDOF != 0); break;
    case CHANGE_PARALLAX_TOGGLE: cb->checked = (SceneProp.ToogleParallax != 0); break;
    case CHANGE_PARALLAX_SHADOW_TOGGLE: cb->checked = (SceneProp.ToogleParallaxShadow != 0); break;
    case CHANGE_GODRAYS_TOGGLE: cb->checked = (SceneProp.ToogleGodRays != 0); break;
    }
  }
  for (auto& sp : gui.GetSelectorPairs()) {
    auto* sel = sp.selector;
    switch (sel->settingIndex) {
    case CHANGE_NUM_LIGHTS:         sel->selectedIndex = FindLightOption(SceneProp.ActiveLights); break;
    case CHANGE_ACTIVE_GAUSS_KERNEL: sel->selectedIndex = ChangeActiveGaussSelection; break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      int ks = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
      // Find matching option index for the kernel size
      for (int i = 0; i < (int)sel->options.size(); i++) {
        if (std::atoi(sel->options[i].c_str()) == ks) { sel->selectedIndex = i; break; }
      }
    } break;
    case CHANGE_DEBUG_RT: sel->selectedIndex = m_debugRTSelection; break;
    case CHANGE_ACTIVE_CAMERA: sel->selectedIndex = m_activeCameraIndex; break;
    case CHANGE_CUBEMAP: sel->selectedIndex = m_currentCubemapIndex; break;
    }
  }
}

void SC_Day::SyncFromGUI(t800::GUIManager& gui) {
  for (auto& sp : gui.GetSliderPairs()) {
    auto* slider = sp.slider;
    if (!slider->knobDragging && !slider->knobHover) continue;
    switch (slider->settingIndex) {
    case CHANGE_EXPOSURE:           SceneProp.Exposure = slider->value; break;
    case CHANGE_BLOOM_FACTOR:       SceneProp.BloomFactor = slider->value; break;
    case CHANGE_BLOOM_THRESHOLD:     SceneProp.BloomThreshold = slider->value; break;
    case CHANGE_TM_WHITE_LEVEL:     SceneProp.ToneMapWhiteLevel = slider->value; break;
    case CHANGE_TM_ADAPT_TAU:       SceneProp.LuminanceTau = slider->value; break;
    case CHANGE_PCF_RADIUS:         SceneProp.PCFScale = slider->value; break;
    case CHANGE_PCF_SAMPLES:        SceneProp.PCFSamples = slider->value; break;
    case CHANGE_SSAO_KERNEL_SIZE:   SceneProp.SSAOKernel.KernelSize = (int)slider->value; SceneProp.SSAOKernel.Update(); break;
    case CHANGE_SSAO_RADIUS:        SceneProp.SSAOKernel.Radius = slider->value; break;
    case CHANGE_DOF_APERTURE:       SceneProp.Aperture = slider->value; break;
    case CHANGE_DOF_FOCAL_LENGHT:   SceneProp.FocalLength = slider->value; break;
    case CHANGE_DOF_MAX_COC:        SceneProp.MaxCoc = slider->value; break;
    case CHANGE_DOF_FAR_SAMPLE:     SceneProp.DOF_Far_Samples_squared = slider->value; break;
    case CHANGE_DOF_NEAR_SAMPLE:    SceneProp.DOF_Near_Samples_squared = slider->value; break;
    case CHANGE_PARALLAX_LOW_SAMPLES:  SceneProp.ParallaxLowSamples = slider->value; break;
    case CHANGE_PARALLAX_HIGH_SAMPLES: SceneProp.ParallaxHighSamples = slider->value; break;
    case CHANGE_PARALLAX_HEIGHT:    SceneProp.ParallaxHeight = slider->value; break;
    case CHANGE_PARALLAX_SHADOW_MIN_LAYERS: SceneProp.ParallaxShadowMinLayers = slider->value; break;
    case CHANGE_PARALLAX_SHADOW_MAX_LAYERS: SceneProp.ParallaxShadowMaxLayers = slider->value; break;
    case CHANGE_PARALLAX_SHADOW_SOFTNESS:   SceneProp.ParallaxShadowSoftness = slider->value; break;
    case CHANGE_PARALLAX_SHADOW_STRENGTH:   SceneProp.ParallaxShadowStrength = slider->value; break;
    case CHANGE_LIGHT_VOLUME_STEPS: SceneProp.LightVolumeSteps = slider->value; break;
    case CHANGE_GODRAYS_FACTOR:    SceneProp.GodRaysFactor = slider->value; break;
    case CHANGE_GAUSS_KERNEL_RADIUS:
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius = slider->value;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
      break;
    case CHANGE_GAUSS_KERNEL_DEVIATION:
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma = slider->value;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
      break;
    case CHANGE_FOV:
      ActiveCam->SetFov(Deg2Rad(slider->value));
      // Recompute VP so FOV changes are visible even when paused
      // (Camera::Update is skipped while paused, leaving VP stale).
      ActiveCam->VP = ActiveCam->View * ActiveCam->Projection;
      VP = ActiveCam->VP;
      break;
    case CHANGE_LIGHT_INTENSITY:
      if (!SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = slider->value;
      break;
    case CHANGE_SHADOW_BIAS:
      SceneProp.ShadowBias = slider->value;
      break;
    case CHANGE_SHADOW_MIN:
      SceneProp.ShadowMin = slider->value;
      break;
    case CHANGE_ENV_FACTOR:
      SceneProp.EnvFactor = slider->value;
      break;
    }
  }

  // Apply parallax settings immediately so changes are visible even when paused.
  Meshes[0].SetParallaxSettings(SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);
  Meshes[0].SetParallaxShadowSettings(SceneProp.ParallaxShadowMinLayers, SceneProp.ParallaxShadowMaxLayers,
                                       SceneProp.ParallaxShadowSoftness, SceneProp.ParallaxShadowStrength);

  for (auto& cp : gui.GetCheckboxPairs()) {
    auto* cb = cp.checkbox;
    if (!cb->justToggled) continue;
    switch (cb->settingIndex) {
    case CHANGE_PCF_TOOGLE:     SceneProp.ToogleShadow = cb->checked ? 1 : 0; break;
    case CHANGLE_SSAO_TOOGLE:   SceneProp.ToogleSSAO = cb->checked ? 1 : 0; break;
    case CHANGE_DOF_AUTO_FOCUS: SceneProp.AutoFocus = cb->checked; break;
    case CHANGE_SHOW_SPLINE:    m_showSpline = cb->checked; break;
    case CHANGE_SHOW_LIGHTS:    m_showLights = cb->checked; break;
    case CHANGE_DOF_TOGGLE:
      SceneProp.ToogleDOF = cb->checked ? 1 : 0;
      m_renderGraph.SetPassEnabled("CoC", cb->checked);
      m_renderGraph.SetPassEnabled("Combine CoC", cb->checked);
      m_renderGraph.SetPassEnabled("DOF", cb->checked);
      m_renderGraph.SetPassEnabled("DOF 2", cb->checked);
      break;
    case CHANGE_PARALLAX_TOGGLE:
      SceneProp.ToogleParallax = cb->checked ? 1 : 0;
      Meshes[0].SetParallaxEnabled(cb->checked);
      break;
    case CHANGE_PARALLAX_SHADOW_TOGGLE:
      SceneProp.ToogleParallaxShadow = cb->checked ? 1 : 0;
      // Toggle via strength uniform: 0 = disabled, saved value = enabled
      if (cb->checked) {
        SceneProp.ParallaxShadowStrength = 1.0f; // restore default
      } else {
        SceneProp.ParallaxShadowStrength = 0.0f; // disable
      }
      break;
    case CHANGE_GODRAYS_TOGGLE:
      SceneProp.ToogleGodRays = cb->checked ? 1 : 0;
      break;
    }
  }
  for (auto& sp : gui.GetSelectorPairs()) {
    auto* sel = sp.selector;
    if (!sel->justChanged) continue;
    switch (sel->settingIndex) {
    case CHANGE_NUM_LIGHTS: {
      int val = std::atoi(sel->CurrentOption().c_str());
      SceneProp.ActiveLights = val;
    } break;
    case CHANGE_ACTIVE_GAUSS_KERNEL:
      ChangeActiveGaussSelection = sel->selectedIndex;
      break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      int newSize = std::atoi(sel->CurrentOption().c_str());
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize = newSize;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    } break;
    case CHANGE_DEBUG_RT:
      m_debugRTSelection = sel->selectedIndex;
      break;
    case CHANGE_ACTIVE_CAMERA: {
      m_activeCameraIndex = sel->selectedIndex;
      Camera& LightCam = m_sceneSetup.lightCameras[0];
      Camera& Cam = m_sceneSetup.cameras[0];
      if (m_activeCameraIndex == 0) {
        // Spline
        ActiveCam = &Cam;
        t800::SplineAgent& agent = m_sceneSetup.agents[0];
        ActiveCam->AttachAgent(agent);
        ActiveCam->m_lookAtCenter = false;
      } else if (m_activeCameraIndex == 1) {
        // Free
        ActiveCam = &Cam;
        ActiveCam->DettachAgent();
        ActiveCam->m_externalControl = false;
      } else {
        // Light
        ActiveCam = &LightCam;
      }
      SceneProp.pCameras[0] = ActiveCam;
    } break;
    case CHANGE_CUBEMAP: {
      if (sel->selectedIndex != m_currentCubemapIndex) {
        m_currentCubemapIndex = sel->selectedIndex;
        // Unload current cubemap
        g_pBaseDriver->DestroyTexture(EnvMapTexIndex);
        // Load new cubemap
        std::string newPath = "sky/" + sel->CurrentOption();
        EnvMapTexIndex = g_pBaseDriver->CreateTexture(newPath);
        Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));
      }
    } break;
    }
  }
}

void SC_Day::SaveSceneState() {
  // Sync cubemap path back to descriptor before saving
  auto& selDescs = m_sceneSetup.descriptor.selectors;
  for (auto& sd : selDescs) {
    if (sd.name == "cubemap" && m_currentCubemapIndex < (int)sd.options.size()) {
      m_sceneSetup.descriptor.environment_map = "sky/" + sd.options[m_currentCubemapIndex];
      m_sceneSetup.environmentMap = m_sceneSetup.descriptor.environment_map;
      sd.default_index = m_currentCubemapIndex;
    }
  }

  // Sync GUI element defaults to match current runtime state
  for (auto& sd : m_sceneSetup.descriptor.sliders) {
    if (sd.name == "shadow_bias") sd.default_val = SceneProp.ShadowBias;
    else if (sd.name == "shadow_min")  sd.default_val = SceneProp.ShadowMin;
    else if (sd.name == "env_factor")  sd.default_val = SceneProp.EnvFactor;
    else if (sd.name == "godrays_factor") sd.default_val = SceneProp.GodRaysFactor;
  }
  for (auto& cd : m_sceneSetup.descriptor.checkboxes) {
    if (cd.name == "dof_toggle")       cd.default_val = (SceneProp.ToogleDOF != 0);
    else if (cd.name == "parallax_toggle")  cd.default_val = (SceneProp.ToogleParallax != 0);
    else if (cd.name == "godrays_toggle")   cd.default_val = (SceneProp.ToogleGodRays != 0);
    else if (cd.name == "shadow_toggle")    cd.default_val = (SceneProp.ToogleShadow != 0);
    else if (cd.name == "ssao_toggle")      cd.default_val = (SceneProp.ToogleSSAO != 0);
    else if (cd.name == "dof_auto_focus")   cd.default_val = SceneProp.AutoFocus;
  }

  m_sceneSetup.SaveState(this, "Scenes/SC_Day.json");
}
