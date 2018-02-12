#include "SC_Tech.h"
using namespace t800;
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
}
void SC_Tech::CreateAssets() {
  //Create RT's
  GBufferPass = pFramework->pVideoDriver->CreateRT(4, BaseRT::RGBA8, BaseRT::F32, 0, 0, true);
  DeferredPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA16F, BaseRT::NOTHING, 0, 0, true);
  Extra16FPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA16F, BaseRT::NOTHING, 0, 0, true);
  DepthPass = pFramework->pVideoDriver->CreateRT(0, BaseRT::NOTHING, BaseRT::F32, (int)SceneProp.ShadowMapResolution, (int)SceneProp.ShadowMapResolution, false);
  ShadowAccumPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::R8, BaseRT::NOTHING, 0, 0, true);
  ExtraHelperPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 0, 0, true);
  BloomAccumPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 512, 512, true);
  GodRaysCalcPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::RGBA8, BaseRT::NOTHING, 512, 512, true);
  CoCPass = pFramework->pVideoDriver->CreateRT(2, BaseRT::F16, BaseRT::NOTHING, 512, 512, true);
  CombineCoCPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::F16, BaseRT::NOTHING, 512, 512, true);
  CoCHelperPass = pFramework->pVideoDriver->CreateRT(1, BaseRT::F16, BaseRT::NOTHING, 512, 512, false);
  CoCHelperPass2 = pFramework->pVideoDriver->CreateRT(1, BaseRT::F16, BaseRT::NOTHING, 512, 512, false);

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
  m_agent.Update(DtSecs);
  ActiveCam->Update(DtSecs);
  VP = ActiveCam->VP;
  SceneProp.Lights[0].Position = LightCam.Eye;

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
  float yaw = 0.005f*static_cast<float>(IManager->xDelta);
  ActiveCam->MoveYaw(yaw);
  float pitch = 0.005f*static_cast<float>(IManager->yDelta);
  ActiveCam->MovePitch(pitch);
}

void SC_Tech::OnDraw() {
  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ_WRITE);

  // Shadow Map Depth Pass
  pFramework->pVideoDriver->PushRT(DepthPass);
  SceneProp.pCameras[0] = &LightCam;
  pFramework->pVideoDriver->SetCullFace(BaseDriver::FACE_CULLING::BACK_FACES);
  for (int i = 0; i < 2; i++) {
    Meshes[i].SetGlobalSignature(Signature::SHADOW_MAP_PASS);
    Meshes[i].Draw();
    Meshes[i].SetGlobalSignature(Signature::FORWARD_PASS);
  }
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->SetCullFace(BaseDriver::FACE_CULLING::FRONT_FACES);

  // G Buffer Pass
  pFramework->pVideoDriver->PushRT(GBufferPass);
  SceneProp.pCameras[0] = &Cam;
  for (int i = 0; i < 2; i++) {
    Meshes[i].SetGlobalSignature(Signature::GBUFF_PASS);
    Meshes[i].Draw();
    Meshes[i].SetGlobalSignature(Signature::FORWARD_PASS);
  }
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ);

  // Shadow Map Buffer Accumulation + Occlusion 
  pFramework->pVideoDriver->PushRT(ShadowAccumPass);
  pFramework->pVideoDriver->Clear();
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::DEPTH_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DepthPass, BaseDriver::DEPTH_ATTACHMENT), 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR1_ATTACHMENT), 2);
  Quads[0].SetTexture(SceneProp.SSAOKernel.NoiseTex, 3);
  Quads[0].SetGlobalSignature(Signature::SHADOW_COMP_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();


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
  Quads[0].SetGlobalSignature(Signature::DEFERRED_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();


  // God Rays and Volumetric Pass
  pFramework->pVideoDriver->PushRT(GodRaysCalcPass);
  Quads[0].SetGlobalSignature(Signature::LIGHT_RAY_MARCHING);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::DEPTH_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DepthPass, BaseDriver::DEPTH_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();
  pFramework->pVideoDriver->PushRT(Extra16FPass);
  Quads[0].SetGlobalSignature(Signature::LIGHT_ADD);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(GodRaysCalcPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  // Bright Pass
  pFramework->pVideoDriver->PushRT(BloomAccumPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(Extra16FPass, BaseDriver::COLOR0_ATTACHMENT), 0);
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
  pFramework->pVideoDriver->PushRT(DeferredPass);
  Quads[0].SetGlobalSignature(Signature::DOF_PASS);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(Extra16FPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CombineCoCPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  pFramework->pVideoDriver->PushRT(Extra16FPass);
  Quads[0].SetGlobalSignature(Signature::DOF_PASS_2);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(CombineCoCPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();

  // HDR Composition Pass
  pFramework->pVideoDriver->PushRT(ExtraHelperPass);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(Extra16FPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(BloomAccumPass, BaseDriver::COLOR0_ATTACHMENT), 1);
  Quads[0].SetGlobalSignature(Signature::HDR_COMP_PASS);
  Quads[0].Draw();
  pFramework->pVideoDriver->PopRT();



  // Final Draw
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
  /*
  Quads[1].SetTexture(pFramework->pVideoDriver->GetRTTexture(DeferredPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[1].SetGlobalSignature(Signature::FSQUAD_3_TEX);
  Quads[1].Draw();

  Quads[2].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR1_ATTACHMENT), 0);
  Quads[2].SetGlobalSignature(Signature::FSQUAD_3_TEX);
  Quads[2].Draw();

  Quads[3].SetTexture(pFramework->pVideoDriver->GetRTTexture(GBufferPass, BaseDriver::COLOR2_ATTACHMENT), 0);
  Quads[3].SetGlobalSignature(Signature::FSQUAD_3_TEX);
  Quads[3].Draw();

  Quads[4].SetTexture(pFramework->pVideoDriver->GetRTTexture(ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT), 0);
  Quads[4].SetGlobalSignature(Signature::FSQUAD_3_TEX);
  Quads[4].Draw();
  */
}







