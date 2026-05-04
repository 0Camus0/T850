#include <SandboxScene.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderMesh.h>
#include <scene/RenderSkinnedMesh.h>
#include <scene/SceneDescriptor.h>
#include <scene/IBLResources.h>
#include <core/Config.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <vector>

using namespace t850;
using std::string;

namespace {
  t850::Mat4Json MatrixToSnapshotJson(const XMATRIX44& mat) {
    t850::Mat4Json j;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        j[r][c] = mat.m[r][c];
    return j;
  }

  XMATRIX44 MatrixFromSnapshotJson(const t850::Mat4Json& j) {
    XMATRIX44 mat;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        mat.m[r][c] = j[r][c];
    return mat;
  }

  t850::SnapshotSkinnedJson CaptureSkinnedSnapshot(RenderSkinnedMesh* skinned,
                                                   bool wireframeVisible,
                                                   bool skeletonVisible) {
    t850::SnapshotSkinnedJson snap;
    if (!skinned || !skinned->HasSkinData()) return snap;

    snap.has_skin = true;
    snap.playing = skinned->IsPlaying();
    snap.looping = skinned->IsLooping();
    snap.use_slerp = skinned->GetUseSlerp();
    snap.use_quat_skinning = skinned->GetUseQuatSkinning();
    snap.keyframe_mode = skinned->GetKeyframeMode();
    snap.wireframe_visible = wireframeVisible;
    snap.skeleton_visible = skeletonVisible;
    snap.animation_speed = skinned->GetAnimSpeed();
    snap.local_time = skinned->GetAnimLocalTime();
    snap.tick_time = skinned->GetAnimTickTime();
    snap.ticks_per_second = skinned->GetAnimTicksPerSecond();
    snap.current_anim_set = skinned->GetCurrentAnimSet();
    snap.num_anim_sets = skinned->GetNumAnimSets();
    snap.current_keyframe = skinned->GetCurrentKeyframe();
    snap.total_keyframes = skinned->GetTotalKeyframes();
    snap.num_bones = skinned->GetNumBones();
    snap.bone_texture_width = skinned->GetBoneTextureWidth();
    snap.bone_texture_rgba32f = skinned->GetBoneTextureData();

    std::vector<XMATRIX44> bones;
    skinned->ExportBoneMatrices(bones);
    snap.bone_matrices.reserve(bones.size());
    for (const XMATRIX44& bone : bones) {
      snap.bone_matrices.push_back(MatrixToSnapshotJson(bone));
    }
    return snap;
  }

  void ApplySkinnedSnapshot(RenderSkinnedMesh* skinned,
                            const t850::SnapshotSkinnedJson& snap,
                            bool& wireframeVisible,
                            bool& skeletonVisible) {
    if (!skinned || !skinned->HasSkinData() || !snap.has_skin) return;

    skinned->SetAnimSpeed(snap.animation_speed);
    skinned->SetLooping(snap.looping);
    skinned->SetUseSlerp(snap.use_slerp);
    skinned->SetUseQuatSkinning(snap.use_quat_skinning);
    skinned->SetKeyframeMode(snap.keyframe_mode);
    if (snap.playing) skinned->PlayAnimation();
    else skinned->PauseAnimation();

    int targetSet = snap.current_anim_set;
    int numSets = skinned->GetNumAnimSets();
    if (numSets > 0 && targetSet >= 0 && targetSet < numSets) {
      int guard = 0;
      while (skinned->GetCurrentAnimSet() != targetSet && guard++ < numSets) {
        skinned->NextAnimation();
      }
    }

    std::vector<XMATRIX44> bones;
    bones.reserve(snap.bone_matrices.size());
    for (const auto& bone : snap.bone_matrices) {
      bones.push_back(MatrixFromSnapshotJson(bone));
    }
    if (!bones.empty()) {
      skinned->ApplySnapshotBoneMatrices(bones);
    } else {
      skinned->ClearSnapshotBoneMatrices();
    }

    wireframeVisible = snap.wireframe_visible;
    skeletonVisible = snap.skeleton_visible;
  }
}

void SandboxScene::InitVars() {



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
  SceneProp.IBLMipCount = 4.0f;
  SceneProp.IBLBRDFLUTEnabled = 0.0f;

  if (m_guiSetup.Load("Scenes/SandboxScene.json")) {
    m_guiSetup.ApplyQualityAndSettings(SceneProp);
  } else {
    T8_LOG_ERROR("[SandboxScene] Failed to load Scenes/SandboxScene.json");
  }
  SceneProp.FrustumCullingEnabled = !g_config.flags.cullDisabled;

  t850::FrameDumperConfig dumpCfg;
  dumpCfg.dumpEnabled        = g_config.flags.dumpEnabled;
  dumpCfg.dumpByFrame        = g_config.flags.dumpByFrame;
  dumpCfg.dumpFrame          = g_config.dumpFrame;
  dumpCfg.dumpSeconds        = g_config.dumpSeconds;
  dumpCfg.debugFrames        = g_config.flags.debugFrames;
  dumpCfg.keepRunning        = g_config.flags.keepRunning;
  dumpCfg.replaySnapshotPath = g_config.replaySnapshotPath;
  dumpCfg.sceneIndex         = g_config.startScene;
  m_dumper.Init(dumpCfg);
}

void SandboxScene::CreateAssets() {
  if (!m_renderGraph.Load("Scenes/SandboxScene_RenderGraph.json")) {
    T8_LOG_ERROR("[SandboxScene] Failed to load render graph");
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
  EnvMaps.SetFallback(EnvMapTexIndex);
  if (m_guiSetup.descriptor.name.empty()) {
    m_guiSetup.Load("Scenes/SandboxScene.json");
  }
  LoadEnvironmentIBLResources(
    g_pBaseDriver,
    {m_guiSetup.environmentDiffuseIBL, m_guiSetup.environmentSpecularIBL, m_guiSetup.environmentBrdfLUT,
     m_guiSetup.environmentSheenIBL, m_guiSetup.environmentCharlieLUT, m_guiSetup.environmentSheenELUT},
    EnvMaps,
    DiffuseIBLTexIndex,
    SpecularIBLTexIndex,
    BrdfLUTTexIndex,
    SheenIBLTexIndex,
    CharlieLUTTexIndex,
    SheenELUTTexIndex);
  UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);

  // Load the glTF model
  int index = PrimitiveMgr.CreateMesh(g_config.modelPath.c_str());
  if (index < 0) {
    T8_LOG_ERROR("[SandboxScene] Failed to load '%s'", g_config.modelPath.c_str());
  } else {
    T8_LOG_INFO("[SandboxScene] Loaded model '%s', primitive index=%d", g_config.modelPath.c_str(), index);
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

void SandboxScene::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void SandboxScene::OnDestoryScene() {
  DestroyAssets();
}

void SandboxScene::DestroyAssets() {
  PrimitiveMgr.DestroyPrimitives();
  pFramework->pVideoDriver->DestroyRTs();
}

void SandboxScene::OnUpdate(float _DtSecs) {
  DtSecs = _DtSecs;
  SceneProp.FrameDeltaSec = DtSecs;

  // Apply deferred cubemap change BEFORE any rendering begins.
  // D3D12 texture upload submits a temp command list + fence wait, which
  // conflicts with the main command list if done mid-frame.
  if (!m_pendingCubemap.empty()) {
    T8_LOG_INFO("[SandboxScene] Loading cubemap '%s' (old slot=%d)",
                m_pendingCubemap.c_str(), EnvMapTexIndex);
    // Flush GPU before destroying — D3D12 may still reference the old
    // texture from the previous frame's command list.
    g_pBaseDriver->WaitForGPU();
    int newEnvMapTexIndex = g_pBaseDriver->CreateTexture(m_pendingCubemap);
    if (newEnvMapTexIndex >= 0) {
      if (EnvMapTexIndex >= 0 && EnvMapTexIndex != newEnvMapTexIndex)
        g_pBaseDriver->DestroyTexture(EnvMapTexIndex);
      EnvMapTexIndex = newEnvMapTexIndex;
      if (m_guiSetup.environmentDiffuseIBL.empty() && DiffuseIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(DiffuseIBLTexIndex);
        DiffuseIBLTexIndex = -1;
      }
      if (m_guiSetup.environmentSpecularIBL.empty() && SpecularIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(SpecularIBLTexIndex);
        SpecularIBLTexIndex = -1;
      }
      if (m_guiSetup.environmentSheenIBL.empty() && SheenIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(SheenIBLTexIndex);
        SheenIBLTexIndex = -1;
      }
      EnvMaps.SetFallback(EnvMapTexIndex);
      LoadEnvironmentIBLResources(
        g_pBaseDriver,
        {m_guiSetup.environmentDiffuseIBL, m_guiSetup.environmentSpecularIBL, m_guiSetup.environmentBrdfLUT,
         m_guiSetup.environmentSheenIBL, m_guiSetup.environmentCharlieLUT, m_guiSetup.environmentSheenELUT},
        EnvMaps,
        DiffuseIBLTexIndex,
        SpecularIBLTexIndex,
        BrdfLUTTexIndex,
        SheenIBLTexIndex,
        CharlieLUTTexIndex,
        SheenELUTTexIndex);
      EnvMaps.BrdfLUT = BrdfLUTTexIndex;
      EnvMaps.CharlieIBL = SheenIBLTexIndex;
      EnvMaps.CharlieLUT = CharlieLUTTexIndex;
      EnvMaps.SheenELUT = SheenELUTTexIndex;
      UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);
      Texture* newTex = g_pBaseDriver->GetTexture(EnvMapTexIndex);
      T8_LOG_INFO("[SandboxScene] Cubemap loaded: slot=%d tex=%p (%dx%d)",
                  EnvMapTexIndex, newTex, newTex ? newTex->x : 0, newTex ? newTex->y : 0);
      Quads[0].SetEnvironmentMap(newTex);
      if (Meshes[0].pBase) {
        Meshes[0].SetEnvironmentMap(newTex);
      }
    } else {
      T8_LOG_ERROR("[SandboxScene] Failed to load cubemap '%s'; keeping previous cubemap", m_pendingCubemap.c_str());
    }
    m_pendingCubemap.clear();
  }

  // Replay snapshot: load and apply (one-time)
  if (m_dumper.HasPendingReplay()) {
    if (m_dumper.LoadReplaySnapshot()) {
      m_dumper.ApplySnapshot(Cam, LightCam, SceneProp);
      if (const t850::SnapshotSkinnedJson* skinnedSnap = m_dumper.GetReplaySkinnedState()) {
        if (Meshes[0].pBase) {
          RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[0].pBase);
          ApplySkinnedSnapshot(skinned, *skinnedSnap, m_showWireframe, m_showSkeleton);
        }
      }
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

  // --dumpMatrices: log all camera matrices per frame, then exit
  if (g_config.flags.dumpMatrices) {
    static int s_matDumpFrame = 0;
    static std::ofstream s_matFile;
    if (s_matDumpFrame == 0) {
      s_matFile.open("matrix_dump.csv", std::ios::out | std::ios::trunc);
      s_matFile << "frame,";
      s_matFile << "cam_eye_x,cam_eye_y,cam_eye_z,";
      s_matFile << "cam_pitch,cam_roll,cam_yaw,";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "camView_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "camProj_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "camVP_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "lightView_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "lightProj_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "lightVP_" << r << c << (r == 3 && c == 3 ? "" : ",");
      s_matFile << "\n";
    }
    s_matFile << s_matDumpFrame << ",";
    s_matFile << Cam.Eye.x << "," << Cam.Eye.y << "," << Cam.Eye.z << ",";
    s_matFile << Cam.Pitch << "," << Cam.Roll << "," << Cam.Yaw << ",";
    auto writeM = [&](const XMATRIX44& M) {
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << M.m[r][c] << ",";
    };
    writeM(Cam.View);
    writeM(Cam.Projection);
    writeM(Cam.VP);
    writeM(LightCam.View);
    writeM(LightCam.Projection);
    auto& LVP = LightCam.VP;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        s_matFile << LVP.m[r][c] << (r == 3 && c == 3 ? "" : ",");
    s_matFile << "\n";
    s_matFile.flush();
    s_matDumpFrame++;
    if (s_matDumpFrame >= g_config.dumpMatricesFrames) {
      s_matFile.close();
      T8_LOG_INFO("[dumpMatrices] Wrote %d frames to matrix_dump.csv", s_matDumpFrame);
      exit(0);
    }
  }
}

void SandboxScene::OnInput(InputManager* IManager) {
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
    pFramework->ChangeAPI(GraphicsApi::D3D11);
  if (IManager->PressedOnceKey(T800K_2))
    pFramework->ChangeAPI(GraphicsApi::OPENGL);

  // Debug toggles
  if (IManager->PressedOnceKey(T800K_F2)) {
    m_showCullStats = !m_showCullStats;
    SceneProp.ShowCullingDebug = m_showCullStats;
  }
  if (IManager->PressedOnceKey(T800K_KP6) || IManager->PressedOnceKey(T800K_6)) {
    SceneProp.FrustumCullingEnabled = !SceneProp.FrustumCullingEnabled;
    T8_LOG_INFO("[CULLING] Frustum culling %s", SceneProp.FrustumCullingEnabled ? "enabled" : "disabled");
  }
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

void SandboxScene::FitModelToView() {
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
  m_orbitYaw = g_config.orbitYawOverride ? g_config.orbitYaw : 0.0f;
  m_orbitPitch = 0.0f;

  // Adjust near/far planes to the model scale
  Cam.NPlane = m_modelRadius * 0.01f;
  Cam.FPlane = m_modelRadius * 100.0f;
  Cam.CreatePojection();

  T8_LOG_INFO("[SandboxScene] Model center=(%.2f,%.2f,%.2f) radius=%.2f dist=%.2f",
    m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z, m_modelRadius, m_orbitDist);
}

void SandboxScene::ComputeOrbitCamera() {
  // Spherical coordinates around the target
  XVECTOR3 target = m_orbitTarget + m_panOffset;
  float cy = std::cos(m_orbitYaw),   sy = std::sin(m_orbitYaw);
  float cp = std::cos(m_orbitPitch), sp = std::sin(m_orbitPitch);

  XVECTOR3 offset(sy * cp, sp, cy * cp);
  Cam.Eye = target + offset * m_orbitDist;
  // Clear velocity — orbit camera manages Eye directly; any residual
  // velocity from Input would be re-applied inside SetLookAt→Update,
  // corrupting the position we just computed.
  Cam.Velocity = XVECTOR3(0, 0, 0);
  Cam.SetLookAt(target);
}

void SandboxScene::OnDraw() {
  SceneProp.ShowCullingDebug = m_showCullStats;
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
    EnvMaps
  );

  // RT Dump via FrameDumper
  if (m_dumper.ShouldDump(DtSecs)) {
    t850::SnapshotSkinnedJson skinnedSnapshot;
    const t850::SnapshotSkinnedJson* skinnedSnapshotPtr = nullptr;
    if (Meshes[0].pBase) {
      RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[0].pBase);
      skinnedSnapshot = CaptureSkinnedSnapshot(skinned, m_showWireframe, m_showSkeleton);
      if (skinnedSnapshot.has_skin)
        skinnedSnapshotPtr = &skinnedSnapshot;
    }

    std::vector<t850::RTDumpEntry> rts = {
      {GBufferPass,           BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
      {GBufferPass,           BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {GBufferPass,           BaseDriver::COLOR2_ATTACHMENT, "GBuffer_PBR"},
      {GBufferPass,           BaseDriver::COLOR3_ATTACHMENT, "GBuffer_GeoNormal"},
      {GBufferPass,           BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Emissive"},
      {GBufferPass,           BaseDriver::COLOR5_ATTACHMENT, "GBuffer_Sheen"},
      {GBufferPass,           BaseDriver::COLOR6_ATTACHMENT, "GBuffer_SpecularOcclusion"},
      {GBufferPass,           BaseDriver::DEPTH_ATTACHMENT,  "GBuffer_Depth"},
      {DepthPass,             BaseDriver::DEPTH_ATTACHMENT,  "ShadowMap_Depth"},
      {ShadowAccumPass,       BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {DeferredPass,          BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {Extra16FPass,          BaseDriver::COLOR0_ATTACHMENT, "Extra16F"},
      {ExtraHelperPass,       BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
      {BloomAccumPass,        BaseDriver::COLOR0_ATTACHMENT, "Bloom"},
      {LuminanceMapPass,      BaseDriver::COLOR0_ATTACHMENT, "LuminanceMap"},
      {AdaptedLumCurrentPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumCurrent"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs,
                       nullptr, nullptr, skinnedSnapshotPtr);
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
    case 5:  selected = GBufferPass;     attachment = BaseDriver::DEPTH_ATTACHMENT;  break;
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
        // Bind GBuffer depth for shader-based depth comparison
        int gbufHandle = GBufferPass;
        if (gbufHandle >= 0 && gbufHandle < (int)pFramework->pVideoDriver->RTs.size()) {
          auto* gbufRT = pFramework->pVideoDriver->RTs[gbufHandle];
          skinned->SetWireframeDepthTex(gbufRT->pDepthTexture);
        }
        skinned->SetWireframeViewport(g_pBaseDriver->width, g_pBaseDriver->height);
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
    XVECTOR3 yellow(1.0f, 1.0f, 0.2f);
    XVECTOR3 gray(0.7f, 0.7f, 0.7f);
    const float statScale = 0.56f;
    const float lineHeight = 34.0f * statScale * ((float)h / 720.0f);
    const float bottomMargin = 26.0f * ((float)h / 720.0f);
    float y = (float)h - bottomMargin - lineHeight * 4.0f;
    auto drawCenteredStat = [&](const XVECTOR3& color, const char* text) {
      float textW = m_debugText.MeasurePixel(text, w, h) * statScale;
      float x = ((float)w - textW) * 0.5f;
      m_debugText.DrawPixelScaled(x, y, statScale, statScale, w, h, color, text);
      y += lineHeight;
    };

    snprintf(buf, sizeof(buf), "Meshes: %d/%d  culled %d",
            rm->m_visibleMeshes, rm->m_totalMeshes, rm->m_culledMeshes);
    drawCenteredStat(yellow, buf);

    snprintf(buf, sizeof(buf), "Subsets: %d/%d  culled %d  drawn %d",
            rm->m_visibleSubsets, rm->m_totalSubsets, rm->m_culledSubsets, rm->m_drawnSubsets);
    drawCenteredStat(yellow, buf);

    snprintf(buf, sizeof(buf), "Clusters: %d/%d  culled %d  drawn %d",
            rm->m_visibleClusters, rm->m_totalClusters, rm->m_culledClusters, rm->m_drawnClusters);
    drawCenteredStat(yellow, buf);

        snprintf(buf, sizeof(buf), "GBuffer indices: %llu/%llu  6/KP6: culling %s  F2: stats  F3: AABBs  K: cam pos",
          rm->m_drawnIndices, rm->m_totalIndices,
          SceneProp.FrustumCullingEnabled ? "ON" : "OFF");
    drawCenteredStat(gray, buf);

    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
  }
}

void SandboxScene::PopulateGUI(t850::GUIManager& gui) {
  // Load SandboxScene.json for GUI descriptors
  if (m_guiSetup.descriptor.name.empty()) {
    m_guiSetup.Load("Scenes/SandboxScene.json");
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
    {"material_emissive_intensity", CHANGE_MATERIAL_EMISSIVE_INTENSITY},
    {"material_transmission_multiplier", CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER},
    {"material_refraction_strength", CHANGE_MATERIAL_REFRACTION_STRENGTH},
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

void SandboxScene::SyncToGUI(t850::GUIManager& gui) {
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
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: slider->SetValue(SceneProp.MaterialEmissiveIntensity); break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: slider->SetValue(SceneProp.MaterialTransmissionMultiplier); break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: slider->SetValue(SceneProp.MaterialRefractionStrength); break;
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

void SandboxScene::SyncFromGUI(t850::GUIManager& gui) {
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
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: SceneProp.MaterialEmissiveIntensity = slider->value; break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: SceneProp.MaterialTransmissionMultiplier = slider->value; break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: SceneProp.MaterialRefractionStrength = slider->value; break;
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
        T8_LOG_INFO("[SandboxScene] Cubemap change queued: '%s'", m_pendingCubemap.c_str());
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
