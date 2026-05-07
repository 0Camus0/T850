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
#ifdef OS_ANDROID
#include <video/vulkan/VulkanDriver.h>
#endif
#ifndef OS_ANDROID
#include <imgui/DevGuiContext.h>
#endif
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cctype>

using namespace t850;
using std::string;

namespace t850 {
  extern DeviceContext* T8DeviceContext;
}

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

  bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
  }

  bool VecNearlyEqual(const std::array<float, 3>& lhs, const std::array<float, 3>& rhs) {
    return NearlyEqual(lhs[0], rhs[0]) && NearlyEqual(lhs[1], rhs[1]) && NearlyEqual(lhs[2], rhs[2]);
  }

  std::string SandboxProfileModelKey(const std::string& path) {
    std::string key = path;
    size_t slash = key.find_last_of("/\\");
    if (slash != std::string::npos)
      key = key.substr(slash + 1);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
      return (char)std::tolower(ch);
    });
    return key;
  }

  const t850::FloatOverrideDesc* FindFloatOverride(const std::vector<t850::FloatOverrideDesc>& values, const std::string& name) {
    for (const auto& value : values)
      if (value.name == name) return &value;
    return nullptr;
  }

  const t850::BoolOverrideDesc* FindBoolOverride(const std::vector<t850::BoolOverrideDesc>& values, const std::string& name) {
    for (const auto& value : values)
      if (value.name == name) return &value;
    return nullptr;
  }

  const t850::IntOverrideDesc* FindIntOverride(const std::vector<t850::IntOverrideDesc>& values, const std::string& name) {
    for (const auto& value : values)
      if (value.name == name) return &value;
    return nullptr;
  }

  const t850::SandboxLightOverrideDesc* FindLightOverride(const std::vector<t850::SandboxLightOverrideDesc>& values, int index) {
    for (const auto& value : values)
      if (value.index == index) return &value;
    return nullptr;
  }

  std::array<float, 3> ToArray(const XVECTOR3& value) {
    return {value.x, value.y, value.z};
  }

  XVECTOR3 FromArray(const std::array<float, 3>& value) {
    return XVECTOR3(value[0], value[1], value[2]);
  }

  const t850::SelectorDesc* FindSelectorDesc(const std::vector<t850::SelectorDesc>& selectors, const std::string& name) {
    for (const auto& selector : selectors)
      if (selector.name == name) return &selector;
    return nullptr;
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
  EnsureLightRuntimeState();
  if (!SceneProp.Lights.empty() && SceneProp.Lights[0].Type == LIGHT_DIRECTIONAL) {
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.Lights[0].Direction = LightCam.Look;
  }

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
  m_showWireframe = false;
  m_showSkeleton = false;
  m_drawLightDirection = false;

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
  SceneProp.FrustumCullingToggleAllowed = g_config.cullingLoadMode != t850::Config::CullingLoadMode::Disabled;
  SceneProp.FrustumCullingEnabled = g_config.cullingLoadMode == t850::Config::CullingLoadMode::FullOnLoad;

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

  PrimitiveMgr.SetEngineContext(pEngineContext);
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
    LoadSandboxProfile();
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
  m_lightArrowRenderer.Create();
  float arrowVerts[10 * 4] = {};
  unsigned short arrowIndices[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  m_lightArrowVB = t850::LineRenderer::CreatePositionVB(arrowVerts, 10, BufferUsage::DINAMIC);
  m_lightArrowIB = t850::LineRenderer::CreateIndexBuffer16(arrowIndices, 10);
  m_lightArrowIndexCount = 10;
}

void SandboxScene::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void SandboxScene::OnDestoryScene() {
  DestroyAssets();
}

void SandboxScene::DestroyAssets() {
  m_debugText.Destroy();
  if (m_lightArrowVB) m_lightArrowVB->release();
  if (m_lightArrowIB) m_lightArrowIB->release();
  m_lightArrowVB = nullptr;
  m_lightArrowIB = nullptr;
  m_lightArrowIndexCount = 0;
  m_lightArrowRenderer.Destroy();
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
    UpdateAttachedLights();
    SyncLightCameraFromDirectionalLight();
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

  bool imguiWantsMouse = false;
#ifndef OS_ANDROID
  imguiWantsMouse = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
#endif
  if (!imguiWantsMouse && IManager->PressedKey(T800K_LCTRL) && IManager->PressedMouseButton(0)) {
    if (AdjustSelectedDirectionalLightFromMouse(dx, dy)) return;
  }

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
    if (SceneProp.FrustumCullingToggleAllowed) {
      const bool requested = !SceneProp.FrustumCullingEnabled;
      if (!requested || !Meshes[0].pBase || static_cast<RenderMesh*>(Meshes[0].pBase)->EnsureCullingMetadata()) {
        SceneProp.FrustumCullingEnabled = requested;
      }
      T8_LOG_INFO("[CULLING] Frustum culling %s", SceneProp.FrustumCullingEnabled ? "enabled" : "disabled");
    } else {
      SceneProp.FrustumCullingEnabled = false;
      T8_LOG_INFO("[CULLING] Frustum culling locked off by startup policy");
    }
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

void SandboxScene::EnsureLightRuntimeState() {
  if (m_lightAttachToCamera.size() < SceneProp.Lights.size())
    m_lightAttachToCamera.resize(SceneProp.Lights.size(), false);
  else if (m_lightAttachToCamera.size() > SceneProp.Lights.size())
    m_lightAttachToCamera.resize(SceneProp.Lights.size());

  if (SceneProp.Lights.empty()) m_selectedLightIndex = 0;
  else if (m_selectedLightIndex < 0 || m_selectedLightIndex >= (int)SceneProp.Lights.size()) m_selectedLightIndex = 0;
  SceneProp.ActiveLights = (std::max)(0, (std::min)(SceneProp.ActiveLights, (int)SceneProp.Lights.size()));
}

void SandboxScene::UpdateAttachedLights() {
  EnsureLightRuntimeState();
  Camera* attachCamera = ActiveCam ? ActiveCam : &Cam;
  for (int i = 0; i < (int)SceneProp.Lights.size(); ++i) {
    if (SceneProp.Lights[i].Type == LIGHT_POINT && m_lightAttachToCamera[i]) {
      SceneProp.Lights[i].Position = attachCamera->Eye;
    }
  }
}

void SandboxScene::SyncLightCameraFromDirectionalLight() {
  for (const Light& light : SceneProp.Lights) {
    if (light.Type != LIGHT_DIRECTIONAL) continue;
    XVECTOR3 direction = light.Direction;
    if (direction.Length() <= 0.0001f) return;
    direction.Normalize();
    LightCam.SetLookAt(LightCam.Eye + direction);
    return;
  }
}

bool SandboxScene::AdjustSelectedDirectionalLightFromMouse(float dx, float dy) {
  EnsureLightRuntimeState();
  if (SceneProp.Lights.empty()) return false;
  Light& light = SceneProp.Lights[m_selectedLightIndex];
  if (light.Type != LIGHT_DIRECTIONAL) return false;
  if (std::fabs(dx) < 0.001f && std::fabs(dy) < 0.001f) return true;

  XVECTOR3 direction = light.Direction;
  if (direction.Length() <= 0.0001f) direction = XVECTOR3(0.0f, -1.0f, 0.0f);
  direction.Normalize();

  const float sensitivity = 0.005f;
  direction += Cam.Right * (dx * sensitivity);
  direction += Cam.Up * (-dy * sensitivity);
  if (direction.Length() <= 0.0001f) return true;
  direction.Normalize();
  light.Direction = direction;
  SyncLightCameraFromDirectionalLight();
  return true;
}

void SandboxScene::DrawSelectedDirectionalLightArrow() {
  EnsureLightRuntimeState();
  if (!m_drawLightDirection) return;
  if (SceneProp.Lights.empty() || !m_lightArrowRenderer.IsReady() || !m_lightArrowVB || !m_lightArrowIB) return;

  const Light& light = SceneProp.Lights[m_selectedLightIndex];
  if (light.Type != LIGHT_DIRECTIONAL) return;

  XVECTOR3 direction = light.Direction;
  if (direction.Length() <= 0.0001f) return;
  direction.Normalize();

  XVECTOR3 origin = m_orbitTarget + m_panOffset;
  float arrowLength = (std::max)(1.0f, m_modelRadius * 0.45f);
  float headLength = arrowLength * 0.22f;
  float headWidth = arrowLength * 0.08f;
  XVECTOR3 tip = origin + direction * arrowLength;

  XVECTOR3 side;
  XVecCross(side, Cam.Up, direction);
  if (side.Length() <= 0.0001f) XVecCross(side, XVECTOR3(0.0f, 1.0f, 0.0f), direction);
  if (side.Length() <= 0.0001f) XVecCross(side, XVECTOR3(1.0f, 0.0f, 0.0f), direction);
  side.Normalize();

  XVECTOR3 up;
  XVecCross(up, direction, side);
  up.Normalize();

  XVECTOR3 headBase = tip - direction * headLength;
  XVECTOR3 points[10] = {
    origin, tip,
    tip, headBase + side * headWidth,
    tip, headBase - side * headWidth,
    tip, headBase + up * headWidth,
    tip, headBase - up * headWidth,
  };

  float verts[10 * 4];
  for (int i = 0; i < 10; ++i) {
    verts[i * 4 + 0] = points[i].x;
    verts[i * 4 + 1] = points[i].y;
    verts[i * 4 + 2] = points[i].z;
    verts[i * 4 + 3] = 1.0f;
  }

  m_lightArrowVB->UpdateFromBuffer(*t850::T8DeviceContext, verts);
  XMATRIX44 identity;
  identity.Identity();
  m_lightArrowRenderer.SetDepthTestEnabled(false);
  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
  pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  m_lightArrowRenderer.DrawLines(identity, Cam.VP, XVECTOR3(1.0f, 0.82f, 0.25f, 1.0f),
                                 m_lightArrowVB, m_lightArrowIB, m_lightArrowIndexCount, 16,
                                 IndexBufferFormat::R16);
}

void SandboxScene::CaptureSandboxProfileState(t850::SandboxProfileDesc& state) {
  state = t850::SandboxProfileDesc{};
  state.model = m_profileModelKey.empty() ? SandboxProfileModelKey(g_config.modelPath) : m_profileModelKey;

  auto addFloat = [&](const char* name, float value) {
    state.sliders.push_back({name, value});
  };
  auto addBool = [&](const char* name, bool value) {
    state.checkboxes.push_back({name, value});
  };
  auto addInt = [&](const char* name, int value) {
    state.selectors.push_back({name, value});
  };

  addFloat("exposure", SceneProp.Exposure);
  addFloat("bloom_factor", SceneProp.BloomFactor);
  addFloat("bloom_threshold", SceneProp.BloomThreshold);
  addFloat("tm_white_level", SceneProp.ToneMapWhiteLevel);
  addFloat("tm_adapt_tau", SceneProp.LuminanceTau);
  addFloat("pcf_radius", SceneProp.PCFScale);
  addFloat("pcf_samples", SceneProp.PCFSamples);
  addFloat("ssao_kernel_size", (float)SceneProp.SSAOKernel.KernelSize);
  addFloat("ssao_radius", SceneProp.SSAOKernel.Radius);
  addFloat("dof_aperture", SceneProp.Aperture);
  addFloat("dof_focal_length", SceneProp.FocalLength);
  addFloat("dof_max_coc", SceneProp.MaxCoc);
  addFloat("dof_far_samples", SceneProp.DOF_Far_Samples_squared);
  addFloat("dof_near_samples", SceneProp.DOF_Near_Samples_squared);
  addFloat("light_volume_steps", SceneProp.LightVolumeSteps);
  addFloat("godrays_factor", SceneProp.GodRaysFactor);
  addFloat("fov", ActiveCam ? Rad2Deg(ActiveCam->Fov) : Rad2Deg(Cam.Fov));
  addFloat("shadow_bias", SceneProp.ShadowBias);
  addFloat("shadow_min", SceneProp.ShadowMin);
  addFloat("env_factor", SceneProp.EnvFactor);
  addFloat("ibl_factor", SceneProp.IBLFactor);
  addFloat("material_emissive_intensity", SceneProp.MaterialEmissiveIntensity);
  addFloat("material_transmission_multiplier", SceneProp.MaterialTransmissionMultiplier);
  addFloat("material_refraction_strength", SceneProp.MaterialRefractionStrength);

  for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
    GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
    if (!kernel) continue;
    std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
    addFloat((prefix + "radius").c_str(), kernel->radius);
    addFloat((prefix + "sigma").c_str(), kernel->sigma);
    addInt((prefix + "kernel_size").c_str(), kernel->kernelSize);
  }

  if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) {
    addFloat("anim_speed", skinned->GetAnimSpeed());
    addInt("anim_select", skinned->GetCurrentAnimSet());
    addInt("anim_mode", skinned->GetKeyframeMode() ? 1 : 0);
    if (skinned->GetKeyframeMode())
      state.current_keyframe = skinned->GetCurrentKeyframe();
  } else {
    addFloat("anim_speed", 1.0f);
    addInt("anim_select", 0);
    addInt("anim_mode", 0);
  }

  addBool("shadow_toggle", SceneProp.ToogleShadow != 0);
  addBool("ssao_toggle", SceneProp.ToogleSSAO != 0);
  addBool("show_wireframe", m_showWireframe);
  addBool("show_skeleton", Meshes[0].GetSkinnedMesh() != nullptr && m_showSkeleton);
  addBool("draw_direction", m_drawLightDirection);

  addInt("debug_render_target", m_debugRTSelection);
  addInt("cubemap", m_currentCubemapIndex);
  addInt("gauss_kernel_sample_count", 0);
  addInt("active_gauss_kernel", ChangeActiveGaussSelection);
  addInt("active_light", m_selectedLightIndex);

  EnsureLightRuntimeState();
  for (int lightIndex = 0; lightIndex < (int)SceneProp.Lights.size(); ++lightIndex) {
    const Light& light = SceneProp.Lights[lightIndex];
    t850::SandboxLightOverrideDesc lightState;
    lightState.index = lightIndex;
    lightState.position = ToArray(light.Position);
    lightState.direction = ToArray(light.Direction);
    lightState.color = ToArray(light.Color);
    lightState.diameter = light.radius * 2.0f;
    lightState.intensity = light.Intensity;
    lightState.attach_to_camera = light.Type == LIGHT_POINT && m_lightAttachToCamera[lightIndex];
    state.lights.push_back(lightState);
  }

  state.frustum_culling = SceneProp.FrustumCullingEnabled;
  state.show_culling_debug = m_showCullStats;

  t850::SandboxOrbitCameraDesc orbit;
  orbit.target = {m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z};
  orbit.pan_offset = {m_panOffset.x, m_panOffset.y, m_panOffset.z};
  orbit.eye = {Cam.Eye.x, Cam.Eye.y, Cam.Eye.z};
  orbit.yaw = m_orbitYaw;
  orbit.pitch = m_orbitPitch;
  orbit.distance = m_orbitDist;
  state.orbit_camera = orbit;
}

void SandboxScene::ApplySandboxProfileState(const t850::SandboxProfileDesc& state) {
  for (const auto& value : state.sliders) {
    if (value.name == "exposure") SceneProp.Exposure = value.value;
    else if (value.name == "bloom_factor") SceneProp.BloomFactor = value.value;
    else if (value.name == "bloom_threshold") SceneProp.BloomThreshold = value.value;
    else if (value.name == "tm_white_level") SceneProp.ToneMapWhiteLevel = value.value;
    else if (value.name == "tm_adapt_tau") SceneProp.LuminanceTau = value.value;
    else if (value.name == "pcf_radius") SceneProp.PCFScale = value.value;
    else if (value.name == "pcf_samples") SceneProp.PCFSamples = value.value;
    else if (value.name == "ssao_kernel_size") { SceneProp.SSAOKernel.KernelSize = (int)value.value; SceneProp.SSAOKernel.Update(); }
    else if (value.name == "ssao_radius") SceneProp.SSAOKernel.Radius = value.value;
    else if (value.name == "dof_aperture") SceneProp.Aperture = value.value;
    else if (value.name == "dof_focal_length") SceneProp.FocalLength = value.value;
    else if (value.name == "dof_max_coc") SceneProp.MaxCoc = value.value;
    else if (value.name == "dof_far_samples") SceneProp.DOF_Far_Samples_squared = value.value;
    else if (value.name == "dof_near_samples") SceneProp.DOF_Near_Samples_squared = value.value;
    else if (value.name == "light_volume_steps") SceneProp.LightVolumeSteps = value.value;
    else if (value.name == "godrays_factor") SceneProp.GodRaysFactor = value.value;
    else if (value.name == "fov" && ActiveCam) { ActiveCam->SetFov(Deg2Rad(value.value)); VP = ActiveCam->VP; }
    else if (value.name == "light_intensity" && !SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = value.value;
    else if (value.name == "shadow_bias") SceneProp.ShadowBias = value.value;
    else if (value.name == "shadow_min") SceneProp.ShadowMin = value.value;
    else if (value.name == "env_factor") SceneProp.EnvFactor = value.value;
    else if (value.name == "ibl_factor") SceneProp.IBLFactor = value.value;
    else if (value.name == "material_emissive_intensity") SceneProp.MaterialEmissiveIntensity = value.value;
    else if (value.name == "material_transmission_multiplier") SceneProp.MaterialTransmissionMultiplier = value.value;
    else if (value.name == "material_refraction_strength") SceneProp.MaterialRefractionStrength = value.value;
    else if (value.name == "anim_speed") { if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) skinned->SetAnimSpeed(value.value); }

    for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
      if (value.name == prefix + "radius") { kernel->radius = value.value; kernel->Update(); }
      else if (value.name == prefix + "sigma") { kernel->sigma = value.value; kernel->Update(); }
    }
  }

  for (const auto& value : state.checkboxes) {
    if (value.name == "shadow_toggle") SceneProp.ToogleShadow = value.value ? 1 : 0;
    else if (value.name == "ssao_toggle") SceneProp.ToogleSSAO = value.value ? 1 : 0;
    else if (value.name == "show_wireframe") m_showWireframe = value.value;
    else if (value.name == "show_skeleton") m_showSkeleton = value.value && (Meshes[0].GetSkinnedMesh() != nullptr);
    else if (value.name == "draw_direction") m_drawLightDirection = value.value;
  }

  for (const auto& value : state.selectors) {
    if (value.name == "debug_render_target") m_debugRTSelection = value.value;
    else if (value.name == "active_light") m_selectedLightIndex = value.value;
    else if (value.name == "cubemap") {
      const t850::SelectorDesc* cubemapDesc = FindSelectorDesc(m_guiSetup.descriptor.selectors, "cubemap");
      if (cubemapDesc && value.value >= 0 && value.value < (int)cubemapDesc->options.size()) {
        m_currentCubemapIndex = value.value;
        m_pendingCubemap = "sky/" + cubemapDesc->options[value.value];
      }
    }
    else if (value.name == "active_gauss_kernel") ChangeActiveGaussSelection = value.value;
    else if (value.name == "anim_select") {
      if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) {
        int guard = skinned->GetNumAnimSets() + 1;
        while (skinned->GetCurrentAnimSet() != value.value && guard-- > 0) skinned->NextAnimation();
      }
    }
    else if (value.name == "anim_mode") {
      if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) {
        bool keyframeMode = (value.value == 1);
        skinned->SetKeyframeMode(keyframeMode);
        if (keyframeMode) skinned->StepKeyframe(0);
      }
    }

    for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      std::string name = "gauss_" + std::to_string(kernelIndex) + "_kernel_size";
      if (value.name == name) { kernel->kernelSize = value.value; kernel->Update(); }
    }
  }

  EnsureLightRuntimeState();
  for (const auto& lightState : state.lights) {
    if (lightState.index < 0 || lightState.index >= (int)SceneProp.Lights.size()) continue;
    Light& light = SceneProp.Lights[lightState.index];
    if (lightState.position.has_value()) light.Position = FromArray(*lightState.position);
    if (lightState.direction.has_value()) {
      XVECTOR3 direction = FromArray(*lightState.direction);
      if (direction.Length() > 0.0001f) {
        direction.Normalize();
        light.Direction = direction;
      }
    }
    if (lightState.color.has_value()) light.Color = FromArray(*lightState.color);
    if (lightState.diameter.has_value()) light.radius = (std::max)(0.001f, *lightState.diameter * 0.5f);
    if (lightState.intensity.has_value()) light.Intensity = *lightState.intensity;
    if (lightState.attach_to_camera.has_value() && light.Type == LIGHT_POINT)
      m_lightAttachToCamera[lightState.index] = *lightState.attach_to_camera;
  }
  UpdateAttachedLights();
  SyncLightCameraFromDirectionalLight();

  if (state.frustum_culling.has_value()) {
    if (!SceneProp.FrustumCullingToggleAllowed) {
      SceneProp.FrustumCullingEnabled = false;
    } else if (g_config.cullingLoadMode == t850::Config::CullingLoadMode::FullOnLoad || !*state.frustum_culling) {
      SceneProp.FrustumCullingEnabled = *state.frustum_culling;
    }
  }
  if (state.show_culling_debug.has_value()) {
    m_showCullStats = *state.show_culling_debug;
    SceneProp.ShowCullingDebug = m_showCullStats;
  }
  if (state.orbit_camera.has_value()) {
    const auto& orbit = *state.orbit_camera;
    m_orbitTarget = XVECTOR3(orbit.target[0], orbit.target[1], orbit.target[2]);
    m_panOffset = XVECTOR3(orbit.pan_offset[0], orbit.pan_offset[1], orbit.pan_offset[2]);
    m_orbitYaw = orbit.yaw;
    m_orbitPitch = orbit.pitch;
    m_orbitDist = orbit.distance;
    Cam.Eye = XVECTOR3(orbit.eye[0], orbit.eye[1], orbit.eye[2]);
    ComputeOrbitCamera();
    VP = Cam.VP;
    UpdateAttachedLights();
  }
  if (state.current_keyframe.has_value()) {
    if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) {
      int targetKeyframe = *state.current_keyframe;
      int guard = skinned->GetTotalKeyframes() + 1;
      while (skinned->GetCurrentKeyframe() != targetKeyframe && guard-- > 0) {
        int direction = targetKeyframe > skinned->GetCurrentKeyframe() ? 1 : -1;
        skinned->StepKeyframe(direction);
      }
    }
  }
}

t850::SandboxProfileDesc SandboxScene::BuildSparseSandboxProfile(const t850::SandboxProfileDesc& current) const {
  t850::SandboxProfileDesc sparse;
  sparse.model = current.model;
  for (const auto& value : current.sliders) {
    const auto* baseline = FindFloatOverride(m_profileBaselineState.sliders, value.name);
    if (!baseline || !NearlyEqual(value.value, baseline->value)) sparse.sliders.push_back(value);
  }
  for (const auto& value : current.checkboxes) {
    const auto* baseline = FindBoolOverride(m_profileBaselineState.checkboxes, value.name);
    if (!baseline || value.value != baseline->value) sparse.checkboxes.push_back(value);
  }
  for (const auto& value : current.selectors) {
    const auto* baseline = FindIntOverride(m_profileBaselineState.selectors, value.name);
    if (!baseline || value.value != baseline->value) sparse.selectors.push_back(value);
  }
  for (const auto& value : current.lights) {
    t850::SandboxLightOverrideDesc lightSparse;
    lightSparse.index = value.index;
    const auto* baseline = FindLightOverride(m_profileBaselineState.lights, value.index);
    if (!baseline) {
      lightSparse = value;
    } else {
      if (value.position.has_value() && (!baseline->position.has_value() || !VecNearlyEqual(*value.position, *baseline->position)))
        lightSparse.position = value.position;
      if (value.direction.has_value() && (!baseline->direction.has_value() || !VecNearlyEqual(*value.direction, *baseline->direction)))
        lightSparse.direction = value.direction;
      if (value.color.has_value() && (!baseline->color.has_value() || !VecNearlyEqual(*value.color, *baseline->color)))
        lightSparse.color = value.color;
      if (value.diameter.has_value() && (!baseline->diameter.has_value() || !NearlyEqual(*value.diameter, *baseline->diameter)))
        lightSparse.diameter = value.diameter;
      if (value.intensity.has_value() && (!baseline->intensity.has_value() || !NearlyEqual(*value.intensity, *baseline->intensity)))
        lightSparse.intensity = value.intensity;
      if (value.attach_to_camera.has_value() && (!baseline->attach_to_camera.has_value() || *value.attach_to_camera != *baseline->attach_to_camera))
        lightSparse.attach_to_camera = value.attach_to_camera;
      if (value.attach_to_camera.has_value() && *value.attach_to_camera) {
        lightSparse.position.reset();
      }
    }
    if (lightSparse.position.has_value() || lightSparse.direction.has_value() || lightSparse.color.has_value() ||
        lightSparse.diameter.has_value() || lightSparse.intensity.has_value() || lightSparse.attach_to_camera.has_value()) {
      sparse.lights.push_back(lightSparse);
    }
  }
  if (current.frustum_culling != m_profileBaselineState.frustum_culling) sparse.frustum_culling = current.frustum_culling;
  if (current.show_culling_debug != m_profileBaselineState.show_culling_debug) sparse.show_culling_debug = current.show_culling_debug;
  if (current.current_keyframe != m_profileBaselineState.current_keyframe) sparse.current_keyframe = current.current_keyframe;
  if (current.orbit_camera.has_value() && m_profileBaselineState.orbit_camera.has_value()) {
    const auto& currentOrbit = *current.orbit_camera;
    const auto& baselineOrbit = *m_profileBaselineState.orbit_camera;
    if (!VecNearlyEqual(currentOrbit.target, baselineOrbit.target) ||
        !VecNearlyEqual(currentOrbit.pan_offset, baselineOrbit.pan_offset) ||
        !VecNearlyEqual(currentOrbit.eye, baselineOrbit.eye) ||
        !NearlyEqual(currentOrbit.yaw, baselineOrbit.yaw) ||
        !NearlyEqual(currentOrbit.pitch, baselineOrbit.pitch) ||
        !NearlyEqual(currentOrbit.distance, baselineOrbit.distance)) {
      sparse.orbit_camera = currentOrbit;
    }
  } else if (current.orbit_camera != m_profileBaselineState.orbit_camera) {
    sparse.orbit_camera = current.orbit_camera;
  }
  return sparse;
}

bool SandboxScene::SandboxProfileStatesEqual(const t850::SandboxProfileDesc& lhs, const t850::SandboxProfileDesc& rhs) const {
  return BuildSparseSandboxProfile(lhs).sliders == BuildSparseSandboxProfile(rhs).sliders &&
         BuildSparseSandboxProfile(lhs).checkboxes == BuildSparseSandboxProfile(rhs).checkboxes &&
         BuildSparseSandboxProfile(lhs).selectors == BuildSparseSandboxProfile(rhs).selectors &&
         BuildSparseSandboxProfile(lhs).lights == BuildSparseSandboxProfile(rhs).lights &&
         BuildSparseSandboxProfile(lhs).orbit_camera == BuildSparseSandboxProfile(rhs).orbit_camera &&
         BuildSparseSandboxProfile(lhs).frustum_culling == BuildSparseSandboxProfile(rhs).frustum_culling &&
         BuildSparseSandboxProfile(lhs).show_culling_debug == BuildSparseSandboxProfile(rhs).show_culling_debug &&
         BuildSparseSandboxProfile(lhs).current_keyframe == BuildSparseSandboxProfile(rhs).current_keyframe;
}

void SandboxScene::LoadSandboxProfile() {
  m_profileModelKey = SandboxProfileModelKey(g_config.modelPath);
  CaptureSandboxProfileState(m_profileBaselineState);
  m_profileSavedState = m_profileBaselineState;
  m_profileReady = true;
  m_profileDirty = false;

  for (const auto& profile : m_guiSetup.descriptor.profiles) {
    if (SandboxProfileModelKey(profile.model) != m_profileModelKey) continue;
    ApplySandboxProfileState(profile);
    CaptureSandboxProfileState(m_profileSavedState);
    T8_LOG_INFO("[SandboxScene] Applied profile for model '%s'", m_profileModelKey.c_str());
    return;
  }
  T8_LOG_INFO("[SandboxScene] No profile for model '%s'; using defaults", m_profileModelKey.c_str());
}

void SandboxScene::SaveSandboxProfile() {
  if (!m_profileReady) return;

  t850::SandboxProfileDesc current;
  CaptureSandboxProfileState(current);
  t850::SandboxProfileDesc sparse = BuildSparseSandboxProfile(current);

  auto& profiles = m_guiSetup.descriptor.profiles;
  auto existing = std::find_if(profiles.begin(), profiles.end(), [&](const t850::SandboxProfileDesc& profile) {
    return SandboxProfileModelKey(profile.model) == m_profileModelKey;
  });

  bool hasOverrides = !sparse.sliders.empty() || !sparse.checkboxes.empty() || !sparse.selectors.empty() ||
                      !sparse.lights.empty() ||
                      sparse.orbit_camera.has_value() || sparse.frustum_culling.has_value() ||
                      sparse.show_culling_debug.has_value() || sparse.current_keyframe.has_value();
  if (hasOverrides) {
    if (existing == profiles.end()) profiles.push_back(sparse);
    else *existing = sparse;
  } else if (existing != profiles.end()) {
    profiles.erase(existing);
  }

  if (t850::SaveSceneDescriptor("Scenes/SandboxScene.json", m_guiSetup.descriptor)) {
    m_profileSavedState = current;
    m_profileDirty = false;
    T8_LOG_INFO("[SandboxScene] Saved profile for model '%s'", m_profileModelKey.c_str());
  }
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

  pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(selected, attachment), 0);
  ShaderKey finalKey(0);
  finalKey.setPass(PassType::FSQUAD_1_TEX);
  finalKey.bits |= ShaderKey::HAS_TEXCOORD0;
  Quads[0].SetGlobalKey(finalKey);
  Quads[0].Draw();
#ifdef OS_ANDROID
  if (auto* vkDriver = static_cast<VulkanDriver*>(pFramework->pVideoDriver)) {
    vkDriver->SetLatePresentSource(selected, attachment);
  }
#endif

  // Draw wireframe and skeleton overlays.
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
    } else if (m_showWireframe) {
      RenderMesh* mesh = static_cast<RenderMesh*>(Meshes[0].pBase);
      int gbufHandle = GBufferPass;
      if (gbufHandle >= 0 && gbufHandle < (int)pFramework->pVideoDriver->RTs.size()) {
        auto* gbufRT = pFramework->pVideoDriver->RTs[gbufHandle];
        mesh->SetWireframeDepthTex(gbufRT->pDepthTexture);
      }
      mesh->SetWireframeViewport(g_pBaseDriver->width, g_pBaseDriver->height);
      pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
      mesh->DrawWireframe();
    }
  }

  DrawSelectedDirectionalLightArrow();

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

#ifndef OS_ANDROID
void SandboxScene::DrawDevGui(t850::DevGuiContext& gui) {
  if (m_guiSetup.descriptor.name.empty()) {
    m_guiSetup.Load("Scenes/SandboxScene.json");
  }

  struct Mapping { const char* name; int settingIndex; };

  static const Mapping sliderMappings[] = {
    {"exposure", CHANGE_EXPOSURE},
    {"bloom_factor", CHANGE_BLOOM_FACTOR},
    {"bloom_threshold", CHANGE_BLOOM_THRESHOLD},
    {"tm_white_level", CHANGE_TM_WHITE_LEVEL},
    {"tm_adapt_tau", CHANGE_TM_ADAPT_TAU},
    {"pcf_radius", CHANGE_PCF_RADIUS},
    {"pcf_samples", CHANGE_PCF_SAMPLES},
    {"ssao_kernel_size", CHANGE_SSAO_KERNEL_SIZE},
    {"ssao_radius", CHANGE_SSAO_RADIUS},
    {"dof_aperture", CHANGE_DOF_APERTURE},
    {"dof_focal_length", CHANGE_DOF_FOCAL_LENGHT},
    {"dof_max_coc", CHANGE_DOF_MAX_COC},
    {"dof_far_samples", CHANGE_DOF_FAR_SAMPLE},
    {"dof_near_samples", CHANGE_DOF_NEAR_SAMPLE},
    {"light_volume_steps", CHANGE_LIGHT_VOLUME_STEPS},
    {"godrays_factor", CHANGE_GODRAYS_FACTOR},
    {"gauss_kernel_radius", CHANGE_GAUSS_KERNEL_RADIUS},
    {"gauss_kernel_deviation", CHANGE_GAUSS_KERNEL_DEVIATION},
    {"fov", CHANGE_FOV},
    {"shadow_bias", CHANGE_SHADOW_BIAS},
    {"shadow_min", CHANGE_SHADOW_MIN},
    {"env_factor", CHANGE_ENV_FACTOR},
    {"ibl_factor", CHANGE_IBL_FACTOR},
    {"material_emissive_intensity", CHANGE_MATERIAL_EMISSIVE_INTENSITY},
    {"material_transmission_multiplier", CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER},
    {"material_refraction_strength", CHANGE_MATERIAL_REFRACTION_STRENGTH},
    {"anim_speed", CHANGE_ANIM_SPEED},
  };

  static const Mapping checkboxMappings[] = {
    {"shadow_toggle", CHANGE_PCF_TOOGLE},
    {"ssao_toggle", CHANGLE_SSAO_TOOGLE},
    {"show_wireframe", CHANGE_SHOW_WIREFRAME},
    {"show_skeleton", CHANGE_SHOW_SKELETON},
  };

  static const Mapping selectorMappings[] = {
    {"debug_render_target", CHANGE_DEBUG_RT},
    {"cubemap", CHANGE_CUBEMAP},
    {"gauss_kernel_sample_count", CHANGE_GAUSS_KERNEL_SAMPLE_COUNT},
    {"active_gauss_kernel", CHANGE_ACTIVE_GAUSS_KERNEL},
    {"anim_select", CHANGE_ANIM_SELECT},
    {"anim_mode", CHANGE_ANIM_MODE},
  };

  auto findSetting = [](const std::string& name, const Mapping* mappings, int count) {
    for (int i = 0; i < count; ++i) {
      if (name == mappings[i].name) return mappings[i].settingIndex;
    }
    return -1;
  };

  auto activeKernel = [&]() -> GaussFilter* {
    if (ChangeActiveGaussSelection < 0 || ChangeActiveGaussSelection >= (int)SceneProp.pGaussKernels.size()) return nullptr;
    return SceneProp.pGaussKernels[ChangeActiveGaussSelection];
  };

  auto skinnedMesh = [&]() -> RenderSkinnedMesh* {
    return Meshes[0].GetSkinnedMesh();
  };

  auto buildAnimationOptions = [&]() {
    std::vector<std::string> options;
    RenderSkinnedMesh* skinned = skinnedMesh();
    if (skinned && skinned->HasSkinData()) {
      int numSets = skinned->GetNumAnimSets();
      for (int i = 0; i < numSets; ++i) {
        if (skinned->xFile && !skinned->xFile->XMeshDataBase.empty()) {
          auto& anims = skinned->xFile->XMeshDataBase[0]->Animation.Animations;
          if (i < (int)anims.size() && !anims[i].Name.empty()) {
            options.push_back(anims[i].Name);
            continue;
          }
        }
        options.push_back("Anim " + std::to_string(i));
      }
    }
    if (options.empty()) options.push_back("None");
    return options;
  };

  auto getSliderValue = [&](int settingIndex, float& value) -> bool {
    GaussFilter* kernel = activeKernel();
    switch (settingIndex) {
    case CHANGE_EXPOSURE: value = SceneProp.Exposure; return true;
    case CHANGE_BLOOM_FACTOR: value = SceneProp.BloomFactor; return true;
    case CHANGE_BLOOM_THRESHOLD: value = SceneProp.BloomThreshold; return true;
    case CHANGE_TM_WHITE_LEVEL: value = SceneProp.ToneMapWhiteLevel; return true;
    case CHANGE_TM_ADAPT_TAU: value = SceneProp.LuminanceTau; return true;
    case CHANGE_PCF_RADIUS: value = SceneProp.PCFScale; return true;
    case CHANGE_PCF_SAMPLES: value = SceneProp.PCFSamples; return true;
    case CHANGE_SSAO_KERNEL_SIZE: value = (float)SceneProp.SSAOKernel.KernelSize; return true;
    case CHANGE_SSAO_RADIUS: value = SceneProp.SSAOKernel.Radius; return true;
    case CHANGE_DOF_APERTURE: value = SceneProp.Aperture; return true;
    case CHANGE_DOF_FOCAL_LENGHT: value = SceneProp.FocalLength; return true;
    case CHANGE_DOF_MAX_COC: value = SceneProp.MaxCoc; return true;
    case CHANGE_DOF_FAR_SAMPLE: value = SceneProp.DOF_Far_Samples_squared; return true;
    case CHANGE_DOF_NEAR_SAMPLE: value = SceneProp.DOF_Near_Samples_squared; return true;
    case CHANGE_LIGHT_VOLUME_STEPS: value = SceneProp.LightVolumeSteps; return true;
    case CHANGE_GODRAYS_FACTOR: value = SceneProp.GodRaysFactor; return true;
    case CHANGE_GAUSS_KERNEL_RADIUS: if (!kernel) return false; value = kernel->radius; return true;
    case CHANGE_GAUSS_KERNEL_DEVIATION: if (!kernel) return false; value = kernel->sigma; return true;
    case CHANGE_FOV: if (!ActiveCam) return false; value = Rad2Deg(ActiveCam->Fov); return true;
    case CHANGE_LIGHT_INTENSITY: if (SceneProp.Lights.empty()) return false; value = SceneProp.Lights[0].Intensity; return true;
    case CHANGE_SHADOW_BIAS: value = SceneProp.ShadowBias; return true;
    case CHANGE_SHADOW_MIN: value = SceneProp.ShadowMin; return true;
    case CHANGE_ENV_FACTOR: value = SceneProp.EnvFactor; return true;
    case CHANGE_IBL_FACTOR: value = SceneProp.IBLFactor; return true;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: value = SceneProp.MaterialEmissiveIntensity; return true;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: value = SceneProp.MaterialTransmissionMultiplier; return true;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: value = SceneProp.MaterialRefractionStrength; return true;
    case CHANGE_ANIM_SPEED: if (RenderSkinnedMesh* sk = skinnedMesh()) { value = sk->GetAnimSpeed(); return true; } return false;
    }
    return false;
  };

  auto setSliderValue = [&](int settingIndex, float value) {
    GaussFilter* kernel = activeKernel();
    switch (settingIndex) {
    case CHANGE_EXPOSURE: SceneProp.Exposure = value; break;
    case CHANGE_BLOOM_FACTOR: SceneProp.BloomFactor = value; break;
    case CHANGE_BLOOM_THRESHOLD: SceneProp.BloomThreshold = value; break;
    case CHANGE_TM_WHITE_LEVEL: SceneProp.ToneMapWhiteLevel = value; break;
    case CHANGE_TM_ADAPT_TAU: SceneProp.LuminanceTau = value; break;
    case CHANGE_PCF_RADIUS: SceneProp.PCFScale = value; break;
    case CHANGE_PCF_SAMPLES: SceneProp.PCFSamples = value; break;
    case CHANGE_SSAO_KERNEL_SIZE: SceneProp.SSAOKernel.KernelSize = (int)value; SceneProp.SSAOKernel.Update(); break;
    case CHANGE_SSAO_RADIUS: SceneProp.SSAOKernel.Radius = value; break;
    case CHANGE_DOF_APERTURE: SceneProp.Aperture = value; break;
    case CHANGE_DOF_FOCAL_LENGHT: SceneProp.FocalLength = value; break;
    case CHANGE_DOF_MAX_COC: SceneProp.MaxCoc = value; break;
    case CHANGE_DOF_FAR_SAMPLE: SceneProp.DOF_Far_Samples_squared = value; break;
    case CHANGE_DOF_NEAR_SAMPLE: SceneProp.DOF_Near_Samples_squared = value; break;
    case CHANGE_LIGHT_VOLUME_STEPS: SceneProp.LightVolumeSteps = value; break;
    case CHANGE_GODRAYS_FACTOR: SceneProp.GodRaysFactor = value; break;
    case CHANGE_GAUSS_KERNEL_RADIUS: if (kernel) { kernel->radius = value; kernel->Update(); } break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: if (kernel) { kernel->sigma = value; kernel->Update(); } break;
    case CHANGE_FOV:
      if (ActiveCam) {
        ActiveCam->SetFov(Deg2Rad(value));
        ActiveCam->VP = ActiveCam->View * ActiveCam->Projection;
        VP = ActiveCam->VP;
      }
      break;
    case CHANGE_LIGHT_INTENSITY: if (!SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = value; break;
    case CHANGE_SHADOW_BIAS: SceneProp.ShadowBias = value; break;
    case CHANGE_SHADOW_MIN: SceneProp.ShadowMin = value; break;
    case CHANGE_ENV_FACTOR: SceneProp.EnvFactor = value; break;
    case CHANGE_IBL_FACTOR: SceneProp.IBLFactor = value; break;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: SceneProp.MaterialEmissiveIntensity = value; break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: SceneProp.MaterialTransmissionMultiplier = value; break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: SceneProp.MaterialRefractionStrength = value; break;
    case CHANGE_ANIM_SPEED: if (RenderSkinnedMesh* sk = skinnedMesh()) sk->SetAnimSpeed(value); break;
    }
  };

  auto getCheckboxValue = [&](int settingIndex, bool& value) -> bool {
    switch (settingIndex) {
    case CHANGE_PCF_TOOGLE: value = (SceneProp.ToogleShadow != 0); return true;
    case CHANGLE_SSAO_TOOGLE: value = (SceneProp.ToogleSSAO != 0); return true;
    case CHANGE_SHOW_WIREFRAME: value = m_showWireframe; return true;
    case CHANGE_SHOW_SKELETON: value = (skinnedMesh() != nullptr) && m_showSkeleton; return true;
    }
    return false;
  };

  auto setCheckboxValue = [&](int settingIndex, bool value) {
    switch (settingIndex) {
    case CHANGE_PCF_TOOGLE: SceneProp.ToogleShadow = value ? 1 : 0; break;
    case CHANGLE_SSAO_TOOGLE: SceneProp.ToogleSSAO = value ? 1 : 0; break;
    case CHANGE_SHOW_WIREFRAME: m_showWireframe = value; break;
    case CHANGE_SHOW_SKELETON: m_showSkeleton = value && (skinnedMesh() != nullptr); break;
    }
  };

  auto getSelectorIndex = [&](const t850::SelectorDesc& desc, int settingIndex, int& selectedIndex) -> bool {
    switch (settingIndex) {
    case CHANGE_DEBUG_RT: selectedIndex = m_debugRTSelection; return true;
    case CHANGE_CUBEMAP: selectedIndex = m_currentCubemapIndex; return true;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      GaussFilter* kernel = activeKernel();
      if (!kernel) return false;
      for (int i = 0; i < (int)desc.options.size(); ++i) {
        if (std::atoi(desc.options[i].c_str()) == kernel->kernelSize) { selectedIndex = i; return true; }
      }
      selectedIndex = desc.default_index;
      return true;
    }
    case CHANGE_ACTIVE_GAUSS_KERNEL: selectedIndex = ChangeActiveGaussSelection; return true;
    case CHANGE_ANIM_SELECT: if (RenderSkinnedMesh* sk = skinnedMesh()) { selectedIndex = sk->GetCurrentAnimSet(); return true; } selectedIndex = 0; return true;
    case CHANGE_ANIM_MODE: if (RenderSkinnedMesh* sk = skinnedMesh()) { selectedIndex = sk->GetKeyframeMode() ? 1 : 0; return true; } selectedIndex = 0; return true;
    }
    return false;
  };

  auto setSelectorIndex = [&](const t850::SelectorDesc& desc, const std::vector<std::string>* options, int settingIndex, int selectedIndex) {
    const std::vector<std::string>& sourceOptions = options ? *options : desc.options;
    if (selectedIndex < 0 || selectedIndex >= (int)sourceOptions.size()) return;
    switch (settingIndex) {
    case CHANGE_DEBUG_RT: m_debugRTSelection = selectedIndex; break;
    case CHANGE_CUBEMAP:
      if (selectedIndex != m_currentCubemapIndex) {
        m_currentCubemapIndex = selectedIndex;
        m_pendingCubemap = "sky/" + sourceOptions[selectedIndex];
        T8_LOG_INFO("[SandboxScene] Cubemap change queued: '%s'", m_pendingCubemap.c_str());
      }
      break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      GaussFilter* kernel = activeKernel();
      if (kernel) { kernel->kernelSize = std::atoi(sourceOptions[selectedIndex].c_str()); kernel->Update(); }
    } break;
    case CHANGE_ACTIVE_GAUSS_KERNEL: ChangeActiveGaussSelection = selectedIndex; break;
    case CHANGE_ANIM_SELECT:
      if (RenderSkinnedMesh* sk = skinnedMesh()) {
        int guard = sk->GetNumAnimSets() + 1;
        while (sk->GetCurrentAnimSet() != selectedIndex && guard-- > 0) {
          sk->NextAnimation();
        }
      }
      break;
    case CHANGE_ANIM_MODE:
      if (RenderSkinnedMesh* sk = skinnedMesh()) {
        bool keyMode = (selectedIndex == 1);
        sk->SetKeyframeMode(keyMode);
        if (keyMode) sk->StepKeyframe(0);
      }
      break;
    }
  };

  if (gui.BeginSection("Controls")) {
    for (const auto& desc : m_guiSetup.descriptor.sliders) {
      int settingIndex = findSetting(desc.name, sliderMappings, (int)(sizeof(sliderMappings) / sizeof(sliderMappings[0])));
      if (settingIndex < 0) continue;
      float value = 0.0f;
      if (getSliderValue(settingIndex, value) && gui.Slider(desc, value)) {
        setSliderValue(settingIndex, value);
      }
    }
  }

  if (gui.BeginSection("Toggles")) {
    for (const auto& desc : m_guiSetup.descriptor.checkboxes) {
      int settingIndex = findSetting(desc.name, checkboxMappings, (int)(sizeof(checkboxMappings) / sizeof(checkboxMappings[0])));
      if (settingIndex < 0) continue;
      bool value = false;
      if (getCheckboxValue(settingIndex, value) && gui.Checkbox(desc, value)) {
        setCheckboxValue(settingIndex, value);
      }
    }
  }

  if (gui.BeginSection("Selectors")) {
    std::vector<std::string> animOptions;
    for (const auto& desc : m_guiSetup.descriptor.selectors) {
      int settingIndex = findSetting(desc.name, selectorMappings, (int)(sizeof(selectorMappings) / sizeof(selectorMappings[0])));
      if (settingIndex < 0) continue;
      const std::vector<std::string>* overrideOptions = nullptr;
      if (settingIndex == CHANGE_ANIM_SELECT) {
        animOptions = buildAnimationOptions();
        overrideOptions = &animOptions;
      }
      int selectedIndex = 0;
      if (getSelectorIndex(desc, settingIndex, selectedIndex) && gui.Combo(desc, selectedIndex, overrideOptions)) {
        setSelectorIndex(desc, overrideOptions, settingIndex, selectedIndex);
      }
    }
  }

  if (gui.BeginSection("Lights")) {
    EnsureLightRuntimeState();
    if (SceneProp.Lights.empty()) {
      gui.Text("No lights");
    } else {
      std::vector<std::string> lightOptions;
      lightOptions.reserve(SceneProp.Lights.size());
      for (int i = 0; i < (int)SceneProp.Lights.size(); ++i) {
        const char* typeName = SceneProp.Lights[i].Type == LIGHT_DIRECTIONAL ? "Directional" : "Point";
        lightOptions.push_back(std::string(typeName) + " " + std::to_string(i + 1));
      }

      t850::SelectorDesc lightSelector;
      lightSelector.name = "active_light";
      lightSelector.label = "Light";
      int selectedLight = m_selectedLightIndex;
      if (gui.Combo(lightSelector, selectedLight, &lightOptions)) {
        m_selectedLightIndex = selectedLight;
      }
      EnsureLightRuntimeState();

      Light& light = SceneProp.Lights[m_selectedLightIndex];
      ImGui::PushID(m_selectedLightIndex);

      float color[3] = {light.Color.x, light.Color.y, light.Color.z};
      if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_PickerHueBar)) {
        light.Color = XVECTOR3(color[0], color[1], color[2]);
      }

      const struct { const char* name; float rgb[3]; } palette[] = {
        {"White", {1.0f, 1.0f, 1.0f}},
        {"Warm", {1.0f, 0.84f, 0.58f}},
        {"Cool", {0.62f, 0.74f, 1.0f}},
        {"Amber", {1.0f, 0.52f, 0.18f}},
        {"Red", {1.0f, 0.18f, 0.15f}},
        {"Green", {0.3f, 1.0f, 0.42f}},
        {"Blue", {0.2f, 0.45f, 1.0f}}
      };
      const int paletteCount = (int)(sizeof(palette) / sizeof(palette[0]));
      for (int paletteIndex = 0; paletteIndex < paletteCount; ++paletteIndex) {
        if (paletteIndex > 0) ImGui::SameLine();
        ImGui::PushID(paletteIndex);
        ImVec4 swatch(palette[paletteIndex].rgb[0], palette[paletteIndex].rgb[1], palette[paletteIndex].rgb[2], 1.0f);
        if (ImGui::ColorButton(palette[paletteIndex].name, swatch, ImGuiColorEditFlags_NoTooltip, ImVec2(20.0f, 20.0f))) {
          light.Color = XVECTOR3(palette[paletteIndex].rgb[0], palette[paletteIndex].rgb[1], palette[paletteIndex].rgb[2]);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", palette[paletteIndex].name);
        ImGui::PopID();
      }

      t850::SliderDesc intensityDesc;
      intensityDesc.name = "light_intensity";
      intensityDesc.label = "Intensity";
      intensityDesc.min_val = 0.0f;
      intensityDesc.max_val = 50.0f;
      intensityDesc.step = 0.1f;
      intensityDesc.default_val = light.Intensity;
      float intensity = light.Intensity;
      if (gui.Slider(intensityDesc, intensity)) light.Intensity = intensity;

      if (light.Type == LIGHT_DIRECTIONAL) {
        t850::CheckboxDesc drawDirectionDesc;
        drawDirectionDesc.name = "draw_direction";
        drawDirectionDesc.label = "Draw direction";
        bool drawDirection = m_drawLightDirection;
        if (gui.Checkbox(drawDirectionDesc, drawDirection)) m_drawLightDirection = drawDirection;

        float direction[3] = {light.Direction.x, light.Direction.y, light.Direction.z};
        if (ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f, "%.3f")) {
          XVECTOR3 newDirection(direction[0], direction[1], direction[2]);
          if (newDirection.Length() > 0.0001f) {
            newDirection.Normalize();
            light.Direction = newDirection;
            SyncLightCameraFromDirectionalLight();
          }
        }
      } else {
        t850::CheckboxDesc attachDesc;
        attachDesc.name = "attach_to_camera";
        attachDesc.label = "Attach to camera";
        bool attachToCamera = m_lightAttachToCamera[m_selectedLightIndex];
        if (gui.Checkbox(attachDesc, attachToCamera)) {
          m_lightAttachToCamera[m_selectedLightIndex] = attachToCamera;
          UpdateAttachedLights();
        }

        float position[3] = {light.Position.x, light.Position.y, light.Position.z};
        if (attachToCamera) ImGui::BeginDisabled();
        if (ImGui::DragFloat3("Position", position, 0.05f, 0.0f, 0.0f, "%.3f")) {
          light.Position = XVECTOR3(position[0], position[1], position[2]);
        }
        if (attachToCamera) ImGui::EndDisabled();

        t850::SliderDesc diameterDesc;
        diameterDesc.name = "light_diameter";
        diameterDesc.label = "Diameter";
        diameterDesc.min_val = 0.01f;
        diameterDesc.max_val = 2000.0f;
        diameterDesc.step = 0.1f;
        diameterDesc.default_val = light.radius * 2.0f;
        float diameter = light.radius * 2.0f;
        if (gui.Slider(diameterDesc, diameter)) light.radius = (std::max)(0.001f, diameter * 0.5f);
      }

      ImGui::PopID();
    }
  }

  if (m_profileReady) {
    t850::SandboxProfileDesc currentProfileState;
    CaptureSandboxProfileState(currentProfileState);
    m_profileDirty = !SandboxProfileStatesEqual(currentProfileState, m_profileSavedState);
    if (gui.BeginSection("Profile")) {
      std::string profileText = "Model profile: " + (m_profileModelKey.empty() ? std::string("none") : m_profileModelKey);
      gui.Text(profileText.c_str());
      if (gui.Button("Save Profile", m_profileDirty)) {
        SaveSandboxProfile();
      }
    }
  }

  if (gui.BeginSection("Culling")) {
    t850::CheckboxDesc cullingDesc;
    cullingDesc.name = "frustum_culling";
    cullingDesc.label = "Frustum culling";
    cullingDesc.enabled = SceneProp.FrustumCullingToggleAllowed;
    bool cullingEnabled = SceneProp.FrustumCullingEnabled;
    if (gui.Checkbox(cullingDesc, cullingEnabled)) {
      if (!cullingEnabled || !Meshes[0].pBase || static_cast<RenderMesh*>(Meshes[0].pBase)->EnsureCullingMetadata()) {
        SceneProp.FrustumCullingEnabled = cullingEnabled;
      }
    }

    t850::CheckboxDesc statsDesc;
    statsDesc.name = "show_culling_debug";
    statsDesc.label = "Culling stats and frustum";
    statsDesc.enabled = SceneProp.FrustumCullingToggleAllowed;
    bool showCulling = m_showCullStats;
    if (gui.Checkbox(statsDesc, showCulling)) {
      m_showCullStats = showCulling;
      SceneProp.ShowCullingDebug = showCulling;
    }
  }
}
#else
void SandboxScene::DrawDevGui(t850::DevGuiContext&) {}
#endif

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
    case CHANGE_SHOW_SKELETON:  cb->checked = (Meshes[0].GetSkinnedMesh() != nullptr) && m_showSkeleton; break;
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
    case CHANGE_SHOW_SKELETON:  m_showSkeleton = cb->checked && (Meshes[0].GetSkinnedMesh() != nullptr); break;
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
