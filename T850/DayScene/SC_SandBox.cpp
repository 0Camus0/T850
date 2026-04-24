#include "SC_SandBox.h"
#include <video/BaseDriver.h>
#include <utils/Log.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderMesh.h>
#include <scene/RenderSkinnedMesh.h>
#include <scene/SceneDescriptor.h>
#include <iostream>
#include <string>
#include <cmath>

using namespace t800;
using std::string;

void SC_SandBox::InitVars() {
  // Free camera
  Cam.InitPerspective(XVECTOR3(0.0f, 1.0f, 10.0f), Deg2Rad(46.8f), 1280.0f / 720.0f, 0.1f, 5000.0f);
  Cam.Speed = 10.0f;
  Cam.Eye = XVECTOR3(0.0f, 5.0f, -15.0f);
  Cam.Pitch = 0.2f;
  Cam.Roll = 0.0f;
  Cam.Yaw = 0.0f;
  Cam.m_externalControl = false;
  Cam.Update(0.0f);

  // Initialize orbit camera defaults
  m_orbitTarget = XVECTOR3(0, 0, 0);
  m_panOffset   = XVECTOR3(0, 0, 0);
  m_orbitYaw    = 0.0f;
  m_orbitPitch  = 0.0f;
  m_orbitDist   = 5.0f;
  m_modelRadius = 1.0f;

  LightCam.InitPerspective(XVECTOR3(0.0f, 100.0f, 10.0f), Deg2Rad(45.0f), 1.0f, 10.0f, 500.0f);
  LightCam.Speed = 10.0f;
  LightCam.Eye = XVECTOR3(50.0f, 150.0f, -50.0f);
  LightCam.Pitch = 1.0f;
  LightCam.Roll = 0.0f;
  LightCam.Yaw = -1.57f;
  LightCam.Update(0.0f);

  ActiveCam = &Cam;

  SceneProp.AddCamera(ActiveCam);
  SceneProp.AddLightCamera(&LightCam);

  SceneProp.AddDirectionalLight(XVECTOR3(-0.2f, -1.0f, 0.1f), XVECTOR3(1, 1, 1), 5.0f, true);
  SceneProp.AddLight(XVECTOR3(10.0f, 10.0f, -10.0f), XVECTOR3(1.0, 0.9, 0.8), 100.0f, 1.0f, LIGHT_POINT, true);
  SceneProp.ActiveLights = 2;
  SceneProp.AmbientColor = XVECTOR3(0.3f, 0.3f, 0.3f);

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

  SceneProp.AddGaussKernel(&ShadowFilter);
  SceneProp.AddGaussKernel(&BloomFilter);
  SceneProp.AddGaussKernel(&NearDOFFilter);
  SceneProp.ActiveGaussKernel = 0;

  SceneProp.ShadowMapResolution = 1024.0f;
  SceneProp.PCFScale = 1.7f;
  SceneProp.PCFSamples = 1.0f;
  SceneProp.SSAOKernel.Radius = 1.5f;
  SceneProp.SSAOKernel.KernelSize = 8;
  SceneProp.SSAOKernel.Update();

  SceneProp.ToogleShadow = true;
  SceneProp.ToogleSSAO = true;

  SceneProp.Exposure = 1.0f;
  SceneProp.BloomFactor = 0.35f;
  SceneProp.BloomThreshold = 1.5f;
  SceneProp.ToneMapWhiteLevel = 5.5f;
  SceneProp.LuminanceTau = 1.1f;

  // Initialize frame dumper from command-line globals
  extern bool g_dumpEnabled, g_dumpByFrame, g_debugFrames, g_keepRunning;
  extern int g_dumpFrame, g_startScene;
  extern float g_dumpSeconds;
  extern std::string g_replaySnapshotPath;
  t800::FrameDumperConfig dumpCfg;
  dumpCfg.dumpEnabled        = g_dumpEnabled;
  dumpCfg.dumpByFrame        = g_dumpByFrame;
  dumpCfg.dumpFrame          = g_dumpFrame;
  dumpCfg.dumpSeconds        = g_dumpSeconds;
  dumpCfg.debugFrames        = g_debugFrames;
  dumpCfg.keepRunning        = g_keepRunning;
  dumpCfg.replaySnapshotPath = g_replaySnapshotPath;
  dumpCfg.sceneIndex         = g_startScene;
  m_dumper.Init(dumpCfg);
}

void SC_SandBox::CreateAssets() {
  if (!m_renderGraph.Load("Scenes/SC_SandBox_RenderGraph.json")) {
    T8_LOG_ERROR("[SC_SandBox] Failed to load render graph");
    return;
  }
  m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp);

  GBufferPass           = m_renderGraph.GetRTHandle("GBuffer");
  DeferredPass          = m_renderGraph.GetRTHandle("Deferred");
  Extra16FPass          = m_renderGraph.GetRTHandle("Extra16F");
  DepthPass             = m_renderGraph.GetRTHandle("DepthPass");
  ShadowAccumPass       = m_renderGraph.GetRTHandle("ShadowAccum");
  ExtraHelperPass       = m_renderGraph.GetRTHandle("ExtraHelper");
  BloomAccumPass        = m_renderGraph.GetRTHandle("BloomAccum");
  LuminanceMapPass      = m_renderGraph.GetRTHandle("LuminanceMap");
  AdaptedLumCurrentPass = m_renderGraph.GetRTHandle("AdaptedLumCurrent");
  AdaptedLumPrevPass    = m_renderGraph.GetRTHandle("AdaptedLumPrev");

  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);

  SceneProp.SSAOKernel.InitTexture();

  EnvMapTexIndex = g_pBaseDriver->CreateTexture(string("sky/Ennis.dds"));

  extern std::string g_modelPath;

  // Load the glTF model
  int index = PrimitiveMgr.CreateMesh(g_modelPath.c_str());
  if (index < 0) {
    T8_LOG_ERROR("[SC_SandBox] Failed to load '%s'", g_modelPath.c_str());
  } else {
    T8_LOG_INFO("[SC_SandBox] Loaded model '%s', primitive index=%d", g_modelPath.c_str(), index);
    Meshes[0].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
    FitModelToView();
  }

  // No SkyBox mesh needed — cleared GBuffer pixels (MatId=0) sample
  // the environment cubemap directly in the deferred pass using the
  // interpolated view ray (PosCorner). This avoids cull-face issues
  // and works across all APIs.

  // Fullscreen quad setup
  m.Identity();
  Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[0], 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[1], 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[2], 2);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[3], 3);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->pDepthTexture, 4);
  Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));

  for (int i = 1; i <= 7; i++)
    Quads[i].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);

  PrimitiveMgr.SetSceneProps(&SceneProp);

  Quads[0].TranslateAbsolute(0.0f, 0.0f, 0.0f);
  Quads[0].Update();

  // Debug visualization
  m_debugText.LoadFromFile(24, "Fonts/Martius-LV9L4.ttf", 512.0f);
  m_debugSphere.Create(6, 12);
}

void SC_SandBox::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void SC_SandBox::OnDestoryScene() {
  DestroyAssets();
}

void SC_SandBox::DestroyAssets() {
  PrimitiveMgr.DestroyPrimitives();
  pFramework->pVideoDriver->DestroyRTs();
}

void SC_SandBox::OnUpdate(float _DtSecs) {
  DtSecs = _DtSecs;
  SceneProp.FrameDeltaSec = DtSecs;

  // Apply deferred cubemap change BEFORE any rendering begins.
  // D3D12 texture upload submits a temp command list + fence wait, which
  // conflicts with the main command list if done mid-frame.
  if (!m_pendingCubemap.empty()) {
    T8_LOG_INFO("[SC_SandBox] Loading cubemap '%s' (old slot=%d)",
                m_pendingCubemap.c_str(), EnvMapTexIndex);
    // Flush GPU before destroying — D3D12 may still reference the old
    // texture from the previous frame's command list.
    g_pBaseDriver->WaitForGPU();
    if (EnvMapTexIndex >= 0) {
      g_pBaseDriver->DestroyTexture(EnvMapTexIndex);
      EnvMapTexIndex = -1;
    }
    EnvMapTexIndex = g_pBaseDriver->CreateTexture(m_pendingCubemap);
    Texture* newTex = g_pBaseDriver->GetTexture(EnvMapTexIndex);
    T8_LOG_INFO("[SC_SandBox] Cubemap loaded: slot=%d tex=%p (%dx%d)",
                EnvMapTexIndex, newTex, newTex ? newTex->x : 0, newTex ? newTex->y : 0);
    Quads[0].SetEnvironmentMap(newTex);
    if (Meshes[0].pBase) {
      Meshes[0].SetEnvironmentMap(newTex);
    }
    m_pendingCubemap.clear();
  }

  // Replay snapshot: load and apply (one-time)
  if (m_dumper.HasPendingReplay()) {
    if (m_dumper.LoadReplaySnapshot()) {
      m_dumper.ApplySnapshot(Cam, LightCam, SceneProp);
      VP = Cam.VP;
    }
  }
  m_dumper.UpdateReplayState();

  if (!m_dumper.SkipCameraUpdates()) {
    ComputeOrbitCamera();
    VP = Cam.VP;
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.Lights[0].Direction = LightCam.Look;
  }
}

void SC_SandBox::OnInput(InputManager* IManager) {
  // Spacebar: request frame dump (--debugFrames mode)
  if (IManager->PressedOnceKey(T800K_SPACE)) {
    m_dumper.RequestDump();
  }

  // Skip mouse-driven camera when replay snapshot is active
  if (m_dumper.IsReplayActive()) return;

  float dx = static_cast<float>(IManager->xDelta);
  float dy = static_cast<float>(IManager->yDelta);

  // Left click + drag: orbit rotate
  if (IManager->PressedMouseButton(0)) {
    m_orbitYaw   += dx * 0.005f;
    m_orbitPitch += dy * 0.005f;
    // Clamp pitch to avoid gimbal lock
    const float maxP = Deg2Rad(89.0f);
    if (m_orbitPitch >  maxP) m_orbitPitch =  maxP;
    if (m_orbitPitch < -maxP) m_orbitPitch = -maxP;
  }

  // Right click + drag: zoom (vertical drag)
  if (IManager->PressedMouseButton(2)) {
    m_orbitDist -= dy * 0.02f * m_modelRadius;
    if (m_orbitDist < m_modelRadius * 0.05f)
      m_orbitDist = m_modelRadius * 0.05f;
  }

  // Middle click + drag: pan
  if (IManager->PressedMouseButton(1)) {
    float panSpeed = m_orbitDist * 0.002f;
    // Pan along camera right and up axes
    m_panOffset += Cam.Right * (-dx * panSpeed);
    m_panOffset += Cam.Up    * ( dy * panSpeed);
  }

  // Mouse wheel: zoom
  if (IManager->scrollDelta != 0.0f) {
    m_orbitDist -= IManager->scrollDelta * 0.15f * m_modelRadius;
    if (m_orbitDist < m_modelRadius * 0.05f)
      m_orbitDist = m_modelRadius * 0.05f;
  }

  // Print camera position
  if (IManager->PressedOnceKey(T800K_k)) {
    T8_LOG_INFO("Orbit: target[%f,%f,%f] dist=%f yaw=%f pitch=%f",
      m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z,
      m_orbitDist, m_orbitYaw, m_orbitPitch);
  }

  // API switching
  if (IManager->PressedOnceKey(T800K_1))
    pFramework->ChangeAPI(GRAPHICS_API::D3D11);
  if (IManager->PressedOnceKey(T800K_2))
    pFramework->ChangeAPI(GRAPHICS_API::OPENGL);

  // Debug toggles
  if (IManager->PressedOnceKey(T800K_F2))
    m_showCullStats = !m_showCullStats;
  if (IManager->PressedOnceKey(T800K_F3))
    m_showAABBs = !m_showAABBs;

  // Arrow keys: step keyframes when in keyframe mode
  RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
  if (sk && sk->GetKeyframeMode()) {
    if (IManager->PressedOnceKey(T800K_RIGHT))
      sk->StepKeyframe(1);
    if (IManager->PressedOnceKey(T800K_LEFT))
      sk->StepKeyframe(-1);
  }
}

void SC_SandBox::FitModelToView() {
  if (!Meshes[0].pBase) return;
  RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);

  // Compute the union of all geometry AABBs
  RenderMesh::AABB total;
  total.Reset();
  for (auto& mi : rm->Info) {
    total.Expand(mi.bounds.min.x, mi.bounds.min.y, mi.bounds.min.z);
    total.Expand(mi.bounds.max.x, mi.bounds.max.y, mi.bounds.max.z);
  }

  m_orbitTarget = XVECTOR3(
    (total.min.x + total.max.x) * 0.5f,
    (total.min.y + total.max.y) * 0.5f,
    (total.min.z + total.max.z) * 0.5f);
  m_panOffset = XVECTOR3(0, 0, 0);

  float ex = (total.max.x - total.min.x) * 0.5f;
  float ey = (total.max.y - total.min.y) * 0.5f;
  float ez = (total.max.z - total.min.z) * 0.5f;
  m_modelRadius = std::sqrt(ex*ex + ey*ey + ez*ez);
  if (m_modelRadius < 1e-4f) m_modelRadius = 1.0f;

  // Place camera at a distance that fits the bounding sphere in the FOV
  float halfFov = Cam.Fov * 0.5f;
  m_orbitDist = m_modelRadius / std::tan(halfFov);
  m_orbitYaw = 0.0f;
  m_orbitPitch = 0.0f;

  // Adjust near/far planes to the model scale
  Cam.NPlane = m_modelRadius * 0.01f;
  Cam.FPlane = m_modelRadius * 100.0f;
  Cam.CreatePojection();

  T8_LOG_INFO("[SC_SandBox] Model center=(%.2f,%.2f,%.2f) radius=%.2f dist=%.2f",
    m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z, m_modelRadius, m_orbitDist);
}

void SC_SandBox::ComputeOrbitCamera() {
  // Spherical coordinates around the target
  XVECTOR3 target = m_orbitTarget + m_panOffset;
  float cy = std::cos(m_orbitYaw),   sy = std::sin(m_orbitYaw);
  float cp = std::cos(m_orbitPitch), sp = std::sin(m_orbitPitch);

  XVECTOR3 offset(sy * cp, sp, cy * cp);
  Cam.Eye = target + offset * m_orbitDist;
  Cam.SetLookAt(target);
}

void SC_SandBox::OnDraw() {
  // FPS logging (every 120 frames)
  static int sFrameCount = 0;
  static float sAccumTime = 0.0f;
  sAccumTime += DtSecs;
  sFrameCount++;
  if (sFrameCount % 120 == 0) {
    float avgFps = (sAccumTime > 0.0f) ? (float)sFrameCount / sAccumTime : 0.0f;
    T8_LOG_INFO("[FPS] %.1f fps (avg over %d frames, dt=%.3f ms)",
                avgFps, sFrameCount, DtSecs * 1000.0f);
  }

  // Update animation and upload bone texture BEFORE render graph
  // (Vulkan copy commands cannot run inside a render pass)
  if (Meshes[0].pBase) {
    RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[0].pBase);
    if (skinned && skinned->HasSkinData())
      skinned->UpdateAnimationAndBones();
  }

  // Execute the render graph (all passes through HDR Composition)
  m_renderGraph.Execute(
    pFramework->pVideoDriver,
    SceneProp,
    Meshes, 1,
    Quads,
    &Cam,
    &LightCam,
    nullptr,
    EnvMapTexIndex
  );

  // RT Dump via FrameDumper
  if (m_dumper.ShouldDump(DtSecs)) {
    std::vector<t800::RTDumpEntry> rts = {
      {GBufferPass,           BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
      {GBufferPass,           BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {GBufferPass,           BaseDriver::COLOR2_ATTACHMENT, "GBuffer_PBR"},
      {GBufferPass,           BaseDriver::COLOR3_ATTACHMENT, "GBuffer_GeoNormal"},
      {GBufferPass,           BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Depth"},
      {DepthPass,             BaseDriver::DEPTH_ATTACHMENT,  "ShadowMap_Depth"},
      {ShadowAccumPass,       BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {DeferredPass,          BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {Extra16FPass,          BaseDriver::COLOR0_ATTACHMENT, "Extra16F"},
      {ExtraHelperPass,       BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
      {BloomAccumPass,        BaseDriver::COLOR0_ATTACHMENT, "Bloom"},
      {LuminanceMapPass,      BaseDriver::COLOR0_ATTACHMENT, "LuminanceMap"},
      {AdaptedLumCurrentPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumCurrent"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs);
    if (m_dumper.ShouldExit()) exit(0);
  }

  // Blit final HDR result to backbuffer
  int selected = ExtraHelperPass;
  int attachment = BaseDriver::COLOR0_ATTACHMENT;

  if (m_debugRTSelection > 0) {
    switch (m_debugRTSelection) {
    case 1:  selected = GBufferPass;     attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 2:  selected = GBufferPass;     attachment = BaseDriver::COLOR1_ATTACHMENT; break;
    case 3:  selected = GBufferPass;     attachment = BaseDriver::COLOR2_ATTACHMENT; break;
    case 4:  selected = GBufferPass;     attachment = BaseDriver::COLOR3_ATTACHMENT; break;
    case 5:  selected = GBufferPass;     attachment = BaseDriver::COLOR4_ATTACHMENT; break;
    case 6:  selected = DepthPass;       attachment = BaseDriver::DEPTH_ATTACHMENT;  break;
    case 7:  selected = ShadowAccumPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 8:  selected = DeferredPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 9:  selected = Extra16FPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 10: selected = ExtraHelperPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 11: selected = BloomAccumPass;  attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 12: selected = LuminanceMapPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 13: selected = AdaptedLumCurrentPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    }
  }

  Quads[7].SetTexture(pFramework->pVideoDriver->GetRTTexture(selected, attachment), 0);
  ShaderKey finalKey(0);
  finalKey.setPass(PassType::FSQUAD_1_TEX);
  finalKey.bits |= ShaderKey::HAS_TEXCOORD0;
  Quads[7].SetGlobalKey(finalKey);
  Quads[7].Draw();

  // Draw wireframe and skeleton overlays for skinned meshes (GPU-skinned)
  if (Meshes[0].pBase) {
    RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[0].pBase);
    if (skinned && skinned->HasSkinData()) {
      if (m_showWireframe) {
        pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
        skinned->DrawWireframe();
      }
      if (m_showSkeleton) {
        pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
        pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
        skinned->DrawSkeleton();
      }
    }
  }

  // Debug: draw wireframe AABBs for visible meshes
  if (m_showAABBs && Meshes[0].pBase) {
    RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);
    XVECTOR3 frustumPlanes[6];
    RenderMesh::ExtractFrustumPlanes(Cam.VP, frustumPlanes);

    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    for (std::size_t i = 0; i < rm->Info.size(); i++) {
      RenderMesh::AABB& box = rm->Info[i].bounds;
      if (!RenderMesh::AABBInsideFrustum(box, rm->transform, frustumPlanes))
        continue;
      XVECTOR3 center((box.min.x+box.max.x)*0.5f, (box.min.y+box.max.y)*0.5f, (box.min.z+box.max.z)*0.5f);
      float ex = (box.max.x-box.min.x)*0.5f;
      float ey = (box.max.y-box.min.y)*0.5f;
      float ez = (box.max.z-box.min.z)*0.5f;
      float radius = std::sqrt(ex*ex + ey*ey + ez*ez);
      m_debugSphere.Draw(VP, center, radius);
    }
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  }

  // Debug: on-screen cull stats
  if (m_showCullStats && Meshes[0].pBase) {
    RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);
    int w = g_pBaseDriver->width;
    int h = g_pBaseDriver->height;

    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);

    char buf[256];
    snprintf(buf, sizeof(buf), "Meshes: %d/%zu  Culled: %d  Subsets drawn: %d/%d",
             (int)rm->Info.size() - rm->m_culledMeshes, rm->Info.size(),
             rm->m_culledMeshes, rm->m_drawnSubsets, rm->m_totalSubsets);
    XVECTOR3 yellow(1.0f, 1.0f, 0.2f);
    m_debugText.DrawPixel(10.0f, 40.0f, w, h, yellow, buf);

    snprintf(buf, sizeof(buf), "F2: stats  F3: AABBs  K: cam pos");
    XVECTOR3 gray(0.7f, 0.7f, 0.7f);
    m_debugText.DrawPixel(10.0f, 65.0f, w, h, gray, buf);

    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
  }
}

void SC_SandBox::PopulateGUI(t800::GUIManager& gui) {
  // Load SC_SandBox.json for GUI descriptors
  if (m_guiSetup.descriptor.name.empty()) {
    m_guiSetup.Load("Scenes/SC_SandBox.json");
  }

  struct SliderMapping { const char* name; int settingIndex; };
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
    {"light_volume_steps",    CHANGE_LIGHT_VOLUME_STEPS},
    {"godrays_factor",        CHANGE_GODRAYS_FACTOR},
    {"gauss_kernel_radius",   CHANGE_GAUSS_KERNEL_RADIUS},
    {"gauss_kernel_deviation", CHANGE_GAUSS_KERNEL_DEVIATION},
    {"fov",                   CHANGE_FOV},
    {"light_intensity",       CHANGE_LIGHT_INTENSITY},
    {"shadow_bias",           CHANGE_SHADOW_BIAS},
    {"shadow_min",            CHANGE_SHADOW_MIN},
    {"env_factor",            CHANGE_ENV_FACTOR},
    {"ibl_factor",             CHANGE_IBL_FACTOR},
    {"anim_speed",             CHANGE_ANIM_SPEED},
  };

  for (auto& sd : m_guiSetup.descriptor.sliders) {
    int settingIdx = -1;
    for (auto& m : mappings) {
      if (sd.name == m.name) { settingIdx = m.settingIndex; break; }
    }
    gui.AddSlider(sd, settingIdx);
  }

  struct CheckboxMapping { const char* name; int settingIndex; };
  static const CheckboxMapping cbMappings[] = {
    {"shadow_toggle",          CHANGE_PCF_TOOGLE},
    {"ssao_toggle",            CHANGLE_SSAO_TOOGLE},
    {"show_wireframe",         CHANGE_SHOW_WIREFRAME},
    {"show_skeleton",          CHANGE_SHOW_SKELETON},
  };

  for (auto& cd : m_guiSetup.descriptor.checkboxes) {
    int settingIdx = -1;
    for (auto& m : cbMappings) {
      if (cd.name == m.name) { settingIdx = m.settingIndex; break; }
    }
    gui.AddCheckbox(cd, settingIdx);
  }

  struct SelectorMapping { const char* name; int settingIndex; };
  static const SelectorMapping selMappings[] = {
    {"debug_render_target", CHANGE_DEBUG_RT},
    {"cubemap",             CHANGE_CUBEMAP},
    {"gauss_kernel_sample_count", CHANGE_GAUSS_KERNEL_SAMPLE_COUNT},
    {"active_gauss_kernel",        CHANGE_ACTIVE_GAUSS_KERNEL},
    {"anim_select",                CHANGE_ANIM_SELECT},
    {"anim_mode",                  CHANGE_ANIM_MODE},
  };

  for (auto& sd : m_guiSetup.descriptor.selectors) {
    int settingIdx = -1;
    for (auto& m : selMappings) {
      if (sd.name == m.name) { settingIdx = m.settingIndex; break; }
    }
    gui.AddSelector(sd, settingIdx);
  }

  // Populate animation selector with actual animation names from the loaded model
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (skinned && skinned->HasSkinData()) {
    for (auto& sp : gui.GetSelectorPairs()) {
      if (sp.selector->settingIndex == CHANGE_ANIM_SELECT) {
        sp.selector->options.clear();
        int numSets = skinned->GetNumAnimSets();
        for (int i = 0; i < numSets; i++) {
          auto& ctrl = skinned->GetAnimController();
          // Use animation set name if available
          xF::xAnimationInfo* info = nullptr;
          const xF::xSkeleton* skel = ctrl.GetAnimSkeleton();
          // Get name from the mesh container's animation info
          if (skinned->xFile && !skinned->xFile->XMeshDataBase.empty()) {
            auto& anims = skinned->xFile->XMeshDataBase[0]->Animation.Animations;
            if (i < (int)anims.size() && !anims[i].Name.empty()) {
              sp.selector->options.push_back(anims[i].Name);
            } else {
              sp.selector->options.push_back("Anim " + std::to_string(i));
            }
          } else {
            sp.selector->options.push_back("Anim " + std::to_string(i));
          }
        }
        if (sp.selector->options.empty())
          sp.selector->options.push_back("None");
        break;
      }
    }
  }
}

void SC_SandBox::SyncToGUI(t800::GUIManager& gui) {
  for (auto& sp : gui.GetSliderPairs()) {
    auto* slider = sp.slider;
    switch (slider->settingIndex) {
    case CHANGE_EXPOSURE:        slider->SetValue(SceneProp.Exposure); break;
    case CHANGE_BLOOM_FACTOR:    slider->SetValue(SceneProp.BloomFactor); break;
    case CHANGE_BLOOM_THRESHOLD: slider->SetValue(SceneProp.BloomThreshold); break;
    case CHANGE_TM_WHITE_LEVEL:  slider->SetValue(SceneProp.ToneMapWhiteLevel); break;
    case CHANGE_TM_ADAPT_TAU:    slider->SetValue(SceneProp.LuminanceTau); break;
    case CHANGE_PCF_RADIUS:      slider->SetValue(SceneProp.PCFScale); break;
    case CHANGE_PCF_SAMPLES:     slider->SetValue(SceneProp.PCFSamples); break;
    case CHANGE_SSAO_KERNEL_SIZE: slider->SetValue((float)SceneProp.SSAOKernel.KernelSize); break;
    case CHANGE_SSAO_RADIUS:     slider->SetValue(SceneProp.SSAOKernel.Radius); break;
    case CHANGE_DOF_APERTURE:    slider->SetValue(SceneProp.Aperture); break;
    case CHANGE_DOF_FOCAL_LENGHT: slider->SetValue(SceneProp.FocalLength); break;
    case CHANGE_DOF_MAX_COC:     slider->SetValue(SceneProp.MaxCoc); break;
    case CHANGE_DOF_FAR_SAMPLE:  slider->SetValue(SceneProp.DOF_Far_Samples_squared); break;
    case CHANGE_DOF_NEAR_SAMPLE: slider->SetValue(SceneProp.DOF_Near_Samples_squared); break;
    case CHANGE_LIGHT_VOLUME_STEPS: slider->SetValue(SceneProp.LightVolumeSteps); break;
    case CHANGE_GODRAYS_FACTOR:  slider->SetValue(SceneProp.GodRaysFactor); break;
    case CHANGE_GAUSS_KERNEL_RADIUS: slider->SetValue(SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius); break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: slider->SetValue(SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma); break;
    case CHANGE_FOV:             slider->SetValue(Rad2Deg(ActiveCam->Fov)); break;
    case CHANGE_LIGHT_INTENSITY: if (!SceneProp.Lights.empty()) slider->SetValue(SceneProp.Lights[0].Intensity); break;
    case CHANGE_SHADOW_BIAS:     slider->SetValue(SceneProp.ShadowBias); break;
    case CHANGE_SHADOW_MIN:      slider->SetValue(SceneProp.ShadowMin); break;
    case CHANGE_ENV_FACTOR:      slider->SetValue(SceneProp.EnvFactor); break;
    case CHANGE_IBL_FACTOR:      slider->SetValue(SceneProp.IBLFactor); break;
    case CHANGE_ANIM_SPEED: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk) slider->SetValue(sk->GetAnimSpeed());
    } break;
    }
  }

  for (auto& cp : gui.GetCheckboxPairs()) {
    auto* cb = cp.checkbox;
    switch (cb->settingIndex) {
    case CHANGE_PCF_TOOGLE:   cb->checked = (SceneProp.ToogleShadow != 0); break;
    case CHANGLE_SSAO_TOOGLE: cb->checked = (SceneProp.ToogleSSAO != 0); break;
    case CHANGE_SHOW_WIREFRAME: cb->checked = m_showWireframe; break;
    case CHANGE_SHOW_SKELETON:  cb->checked = m_showSkeleton; break;
    }
  }

  for (auto& sp : gui.GetSelectorPairs()) {
    auto* sel = sp.selector;
    switch (sel->settingIndex) {
    case CHANGE_DEBUG_RT: sel->selectedIndex = m_debugRTSelection; break;
    case CHANGE_CUBEMAP:  sel->selectedIndex = m_currentCubemapIndex; break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      int ks = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
      std::string ksStr = std::to_string(ks);
      for (int i = 0; i < (int)sel->options.size(); i++) {
        if (sel->options[i] == ksStr) { sel->selectedIndex = i; break; }
      }
    } break;
    case CHANGE_ACTIVE_GAUSS_KERNEL:
      sel->selectedIndex = ChangeActiveGaussSelection;
      break;
    case CHANGE_ANIM_SELECT: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk) sel->selectedIndex = sk->GetCurrentAnimSet();
    } break;
    }
  }
}

void SC_SandBox::SyncFromGUI(t800::GUIManager& gui) {
  for (auto& sp : gui.GetSliderPairs()) {
    auto* slider = sp.slider;
    if (!slider->knobDragging && !slider->knobHover) continue;
    switch (slider->settingIndex) {
    case CHANGE_EXPOSURE:        SceneProp.Exposure = slider->value; break;
    case CHANGE_BLOOM_FACTOR:    SceneProp.BloomFactor = slider->value; break;
    case CHANGE_BLOOM_THRESHOLD: SceneProp.BloomThreshold = slider->value; break;
    case CHANGE_TM_WHITE_LEVEL:  SceneProp.ToneMapWhiteLevel = slider->value; break;
    case CHANGE_TM_ADAPT_TAU:    SceneProp.LuminanceTau = slider->value; break;
    case CHANGE_PCF_RADIUS:      SceneProp.PCFScale = slider->value; break;
    case CHANGE_PCF_SAMPLES:     SceneProp.PCFSamples = slider->value; break;
    case CHANGE_SSAO_KERNEL_SIZE: SceneProp.SSAOKernel.KernelSize = (int)slider->value; SceneProp.SSAOKernel.Update(); break;
    case CHANGE_SSAO_RADIUS:     SceneProp.SSAOKernel.Radius = slider->value; break;
    case CHANGE_DOF_APERTURE:    SceneProp.Aperture = slider->value; break;
    case CHANGE_DOF_FOCAL_LENGHT: SceneProp.FocalLength = slider->value; break;
    case CHANGE_DOF_MAX_COC:     SceneProp.MaxCoc = slider->value; break;
    case CHANGE_DOF_FAR_SAMPLE:  SceneProp.DOF_Far_Samples_squared = slider->value; break;
    case CHANGE_DOF_NEAR_SAMPLE: SceneProp.DOF_Near_Samples_squared = slider->value; break;
    case CHANGE_LIGHT_VOLUME_STEPS: SceneProp.LightVolumeSteps = slider->value; break;
    case CHANGE_GODRAYS_FACTOR:  SceneProp.GodRaysFactor = slider->value; break;
    case CHANGE_GAUSS_KERNEL_RADIUS: SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius = slider->value;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update(); break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma = slider->value;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update(); break;
    case CHANGE_FOV:             ActiveCam->Fov = Deg2Rad(slider->value); break;
    case CHANGE_LIGHT_INTENSITY: if (!SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = slider->value; break;
    case CHANGE_SHADOW_BIAS:     SceneProp.ShadowBias = slider->value; break;
    case CHANGE_SHADOW_MIN:      SceneProp.ShadowMin = slider->value; break;
    case CHANGE_ENV_FACTOR:      SceneProp.EnvFactor = slider->value; break;
    case CHANGE_IBL_FACTOR:      SceneProp.IBLFactor = slider->value; break;
    case CHANGE_ANIM_SPEED: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk) sk->SetAnimSpeed(slider->value);
    } break;
    }
  }

  for (auto& cp : gui.GetCheckboxPairs()) {
    auto* cb = cp.checkbox;
    if (!cb->justToggled) continue;
    switch (cb->settingIndex) {
    case CHANGE_PCF_TOOGLE:   SceneProp.ToogleShadow = cb->checked ? 1 : 0; break;
    case CHANGLE_SSAO_TOOGLE: SceneProp.ToogleSSAO = cb->checked ? 1 : 0; break;
    case CHANGE_SHOW_WIREFRAME: m_showWireframe = cb->checked; break;
    case CHANGE_SHOW_SKELETON:  m_showSkeleton = cb->checked; break;
    }
  }

  for (auto& sp : gui.GetSelectorPairs()) {
    auto* sel = sp.selector;
    if (!sel->justChanged) continue;
    switch (sel->settingIndex) {
    case CHANGE_DEBUG_RT:
      m_debugRTSelection = sel->selectedIndex;
      break;
    case CHANGE_CUBEMAP: {
      if (sel->selectedIndex != m_currentCubemapIndex) {
        m_currentCubemapIndex = sel->selectedIndex;
        m_pendingCubemap = "sky/" + sel->CurrentOption();
        T8_LOG_INFO("[SC_SandBox] Cubemap change queued: '%s'", m_pendingCubemap.c_str());
      }
    } break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      int newSize = std::atoi(sel->CurrentOption().c_str());
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize = newSize;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    } break;
    case CHANGE_ACTIVE_GAUSS_KERNEL:
      ChangeActiveGaussSelection = sel->selectedIndex;
      break;
    case CHANGE_ANIM_SELECT: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk && sel->selectedIndex != sk->GetCurrentAnimSet()) {
        // Switch to selected animation set
        while (sk->GetCurrentAnimSet() != sel->selectedIndex) {
          sk->NextAnimation();
        }
      }
    } break;
    case CHANGE_ANIM_MODE: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk) {
        bool keyMode = (sel->selectedIndex == 1);
        sk->SetKeyframeMode(keyMode);
        if (keyMode) {
          sk->StepKeyframe(0); // snap to current keyframe
        }
      }
    } break;
    }
  }
}
