#include <pch.h>

#include <BackyardScene.h>

#include <core/Config.h>
#include <core/EngineContext.h>
#include <scene/IBLResources.h>
#include <scene/PrimitiveManager.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <array>
#include <stdexcept>
#include <string>

namespace {
  constexpr const char* kSceneDescriptorPath = "Scenes/BackyardScene.json";
  constexpr const char* kRenderGraphPath = "Scenes/BackyardScene_RenderGraph.json";

  struct BackyardAsset {
    const char* path;
    const char* label;
  };

  // Emitted by scripts/blender/build_backyard.py. Each glTF node already
  // carries its authored world transform, so instances stay at the origin.
  constexpr std::array<BackyardAsset, 6> kBackyardAssets = {{
    {"Models/Backyard/Backyard_Ground.glb", "ground"},
    {"Models/Backyard/Backyard_House.glb", "house"},
    {"Models/Backyard/Backyard_Pool.glb", "pool"},
    {"Models/Backyard/Backyard_Wall.glb", "wall"},
    {"Models/Backyard/Backyard_Props.glb", "props"},
    {"Models/Backyard/Backyard_Water.glb", "water"},
  }};
}

BackyardScene::BackyardScene() = default;

void BackyardScene::InitVars() {
  m_deltaSeconds = 0.0f;
  m_assetsCreated = false;
  m_primitiveIndices.clear();
  m_instances.clear();
  m_envMaps = t850::EnvironmentMapSet{};
  m_envMapTexture = -1;
  m_diffuseIBLTexture = -1;
  m_specularIBLTexture = -1;
  m_brdfLUTTexture = -1;
  m_sheenIBLTexture = -1;
  m_charlieLUTTexture = -1;
  m_sheenELUTTexture = -1;

  SceneProp = SceneProps{};
  if (!m_sceneSetup.Load(kSceneDescriptorPath)) {
    throw std::runtime_error(std::string("Backyard scene requires a valid descriptor: ") + kSceneDescriptorPath);
  }
  if (m_sceneSetup.cameras.empty() || m_sceneSetup.lightCameras.empty()) {
    throw std::runtime_error("Backyard scene requires at least one camera and one light camera");
  }

  m_sceneSetup.Apply(SceneProp);
  m_sceneSetup.ApplyInputSettings(SceneProp);
  m_camera = *m_sceneSetup.GetCamera();
  m_lightCamera = *m_sceneSetup.GetLightCamera();
  SceneProp.pCameras = {&m_camera};
  SceneProp.pLightCameras = {&m_lightCamera};
  // Profile must be selected before attaching: AttachCamera activates the
  // current profile, and the default Orbit profile overwrites the authored
  // camera transform.
  m_cameraController.SetActiveProfile(t850::CameraProfileType::FreeFly);
  m_cameraController.AttachCamera(&m_camera);
  m_cameraController.Update(0.0f, t850::CameraUpdateContext{});
  m_lightCamera.Update(0.0f);

  t850::FrameDumperConfig dumpConfig;
  dumpConfig.dumpEnabled = t850::g_config.flags.dumpEnabled;
  dumpConfig.dumpByFrame = t850::g_config.flags.dumpByFrame;
  dumpConfig.dumpFrame = t850::g_config.dumpFrame;
  dumpConfig.dumpSeconds = t850::g_config.dumpSeconds;
  dumpConfig.debugFrames = t850::g_config.flags.debugFrames;
  dumpConfig.keepRunning = t850::g_config.flags.keepRunning;
  dumpConfig.replaySnapshotPath = t850::g_config.replaySnapshotPath;
  dumpConfig.sceneIndex = kSceneIndex;
  m_dumper.Init(dumpConfig);

  T8_LOG_INFO("[Backyard] Descriptor '%s' ready: %zu camera(s), %zu light camera(s), %d light(s)",
              kSceneDescriptorPath,
              m_sceneSetup.cameras.size(),
              m_sceneSetup.lightCameras.size(),
              SceneProp.ActiveLights);
}

void BackyardScene::CreateAssets() {
  if (m_assetsCreated || !pFramework || !pFramework->pVideoDriver) return;
  t850::BaseDriver* driver = pFramework->pVideoDriver;

  SceneProp.SSAOKernel.InitTexture();

  XMATRIX44 identity;
  XMatIdentity(identity);
  PrimitiveMgr.SetEngineContext(pEngineContext);
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&identity);

  t850::RenderContainerDesc descriptor;
  descriptor.name = m_sceneSetup.name.empty() ? "Backyard" : m_sceneSetup.name;
  descriptor.renderGraphPath = kRenderGraphPath;
  descriptor.width = driver->width;
  descriptor.height = driver->height;
  descriptor.sceneProps = &SceneProp;
  if (!m_renderContainer.Initialize(driver, pEngineContext, descriptor)) {
    T8_LOG_ERROR("[Backyard] Render container initialization failed for '%s'", kRenderGraphPath);
    return;
  }
  m_renderContainer.SetMainCamera(&m_camera);
  m_renderContainer.SetLightCamera(&m_lightCamera);
  if (!SceneProp.ToogleDOF) {
    m_renderContainer.Graph().SetPassEnabled("CoC", false);
    m_renderContainer.Graph().SetPassEnabled("Combine CoC", false);
    m_renderContainer.Graph().SetPassEnabled("DOF", false);
    m_renderContainer.Graph().SetPassEnabled("DOF 2", false);
  }
  if (!SceneProp.ToogleShadow) m_renderContainer.Graph().DisablePass("Shadow Depth");
  if (!SceneProp.ToogleSSAO) m_renderContainer.Graph().DisablePass("Shadow Accumulation");

  m_envMapTexture = driver->CreateTexture(m_sceneSetup.environmentMap);
  m_envMaps.SetFallback(m_envMapTexture);
  t850::LoadEnvironmentIBLResources(
      driver,
      {m_sceneSetup.environmentDiffuseIBL, m_sceneSetup.environmentSpecularIBL,
       m_sceneSetup.environmentBrdfLUT, m_sceneSetup.environmentSheenIBL,
       m_sceneSetup.environmentCharlieLUT, m_sceneSetup.environmentSheenELUT},
      m_envMaps, m_diffuseIBLTexture, m_specularIBLTexture, m_brdfLUTTexture,
      m_sheenIBLTexture, m_charlieLUTTexture, m_sheenELUTTexture);
  t850::UpdateSceneIBLSettings(SceneProp, driver, m_envMaps);
  m_renderContainer.SetEnvironmentMaps(m_envMaps);

  for (const BackyardAsset& asset : kBackyardAssets) {
    const int primitive = PrimitiveMgr.CreateMesh(asset.path);
    if (primitive < 0) {
      T8_LOG_ERROR("[Backyard] Failed to load '%s' (%s)", asset.path, asset.label);
      continue;
    }
    t850::PrimitiveInst instance;
    instance.CreateInstance(PrimitiveMgr.GetPrimitive(primitive), &identity);
    if (std::string(asset.label) == "water") {
      // The solver's surface texture is bound at slot 23; this persistent flag
      // survives the render graph's per-pass SetGlobalKey and selects the
      // displacement/foam shader permutation.
      instance.PersistentFeatureBits = t850::ShaderKey::FLUID_SURFACE;
      // Keep the transparent water out of the depth-writing GBuffer and shadow
      // passes. If it wrote GBuffer depth, SceneDepthTex would hold the water
      // itself and screen-space reflections would have nothing behind it.
      instance.DrawPassMask = ~((1ull << t850::PassType::GBUFFER) |
                                (1ull << t850::PassType::SHADOW_MAP));
    }
    instance.Update();
    const t850::RenderInstanceHandle handle = m_renderContainer.AddMeshInstance(instance);
    if (!handle.IsValid()) {
      T8_LOG_ERROR("[Backyard] Could not add '%s' (%s) to the render container", asset.path, asset.label);
      continue;
    }
    m_primitiveIndices.push_back(static_cast<unsigned int>(primitive));
    m_instances.push_back(handle);
    if (std::string(asset.label) == "water") m_waterHandle = handle;
    T8_LOG_INFO("[Backyard] Loaded '%s' (%s) primitive=%d instance=%u",
                asset.path, asset.label, primitive, handle.index);
  }

  PrimitiveMgr.SetSceneProps(&SceneProp);
  m_assetsCreated = m_instances.size() == kBackyardAssets.size();
  if (!m_assetsCreated) {
    T8_LOG_ERROR("[Backyard] Only %zu/%zu assets loaded", m_instances.size(), kBackyardAssets.size());
  } else {
    T8_LOG_INFO("[Backyard] Ready: %zu mesh instances", m_instances.size());
  }

  // Grid matches the pool basin: 9.0 m (X) x 1.6 m (Y) x 4.5 m (Z) at a
  // 9 cm cell. The surface UV space therefore maps 1:1 onto the pool mesh.
  t850::FluidSettings fluidSettings;
  fluidSettings.width = 100;
  fluidSettings.height = 18;
  fluidSettings.depth = 50;
  fluidSettings.pressureIterations = fluidIterations;
  fluidSettings.deltaTime = 1.0f / 60.0f;
  fluidSettings.cellSize = 0.09f;
  fluidSettings.surfaceLevel = 0.96f;
  if (m_fluid.TryInitialize(driver, fluidSettings)) {
    m_fluidReady = true;
    if (!m_fluid.Seed(driver, true)) {
      T8_LOG_ERROR("[Backyard] Fluid seed dispatch failed");
    }
    // Bind a stable surface target onto the water instance at slot 23 (t23).
    // No render-graph RT involved: a graph target written by an out-of-graph
    // dispatch did not survive readback, so the RT is owned by the driver here.
    m_waterSurfaceRT = driver->CreateRT(1, t850::BaseRT::RGBA16F, t850::BaseRT::NOTHING,
                                       static_cast<int>(fluidSettings.width),
                                       static_cast<int>(fluidSettings.depth), false, true);
    t850::Texture* surface = m_waterSurfaceRT >= 0
      ? driver->GetRTTexture(m_waterSurfaceRT, t850::BaseDriver::COLOR0_ATTACHMENT) : nullptr;
    if (surface && m_waterHandle.IsValid()) {
      m_fluid.SetSurfaceOutput(surface);
      if (t850::PrimitiveInst* water = m_renderContainer.GetMesh(m_waterHandle)) {
        water->SetTexture(surface, 23);
        // Fabricated tiling ripple normal map (scripts/textures/build_water_normal.py).
        // Bound at slot 22 rather than the material normal-map slot 3, which
        // crashes the D3D12 path when the material itself has no normal map.
        const int normalTextureIndex = driver->CreateTexture("Backyard/WaterNormal.png");
        t850::Texture* normalTexture = normalTextureIndex >= 0
          ? driver->GetTexture(normalTextureIndex) : nullptr;
        if (normalTexture) {
          water->SetTexture(normalTexture, 22);
          T8_LOG_INFO("[Backyard] Water normal map bound (%dx%d)", normalTexture->x, normalTexture->y);
        } else {
          T8_LOG_ERROR("[Backyard] Cannot load Textures/Backyard/WaterNormal.png");
        }
        water->Update();
        T8_LOG_INFO("[Backyard] Water surface bound to solver texture (%dx%d)", surface->x, surface->y);
      }
      EnsureWaterShaderPermutations(driver);
    } else {
      T8_LOG_ERROR("[Backyard] Cannot bind the water surface texture");
    }
  } else {
    T8_LOG_INFO("[Backyard] Fluid solver unavailable; water renders without displacement");
  }
}

void BackyardScene::DestroyAssets() {
  if (!m_assetsCreated && !pFramework) return;
  if (m_fluidReady && pFramework && pFramework->pVideoDriver) {
    m_fluid.Shutdown(pFramework->pVideoDriver);
  }
  m_fluidReady = false;
  if (m_waterSurfaceRT >= 0 && pFramework && pFramework->pVideoDriver)
    pFramework->pVideoDriver->DestroyRT(m_waterSurfaceRT);
  m_waterSurfaceRT = -1;
  m_instances.clear();
  m_primitiveIndices.clear();
  PrimitiveMgr.DestroyPrimitives();
  m_envMaps = t850::EnvironmentMapSet{};
  if (pFramework && pFramework->pVideoDriver) {
    m_renderContainer.Destroy(pFramework->pVideoDriver);
  }
  SceneProp.SSAOKernel.Destroy();
  m_assetsCreated = false;
}

void BackyardScene::EnsureWaterShaderPermutations(t850::BaseDriver* driver) {
  if (!driver) return;
  // RenderMesh only pre-compiles permutations derived from the material key, so
  // a draw-time instance flag (FLUID_SURFACE) has no compiled program and the
  // draw is skipped. Create the exact keys the water mesh needs here.
  std::string vertexSource, fragmentSource;
  if (!t850::ResourceLocator::Instance().ReadText("Shaders/VS_Mesh.hlsl", vertexSource) ||
      !t850::ResourceLocator::Instance().ReadText("Shaders/FS_Mesh.hlsl", fragmentSource)) {
    T8_LOG_ERROR("[Backyard] Cannot read mesh shader sources for the water permutation");
    return;
  }
  // Water material: normals, tangents, binormals, uv0 + glTF tangent space.
  const uint64_t baseBits =
    t850::ShaderKey::HAS_NORMALS | t850::ShaderKey::HAS_TANGENTS |
    t850::ShaderKey::HAS_BINORMALS | t850::ShaderKey::HAS_TEXCOORD0 |
    t850::ShaderKey::GLTF_TANGENT_SPACE;
  for (uint8_t pass : {t850::PassType::FORWARD, t850::PassType::GBUFFER,
                       t850::PassType::SHADOW_MAP, t850::PassType::RADIAL_DEPTH}) {
    t850::ShaderKey key(baseBits | t850::ShaderKey::FLUID_SURFACE);
    key.setPass(pass);
    T8_LOG_INFO("[Backyard] Precompiling water permutation pass=%u", static_cast<unsigned>(pass));
    // Names must match RenderMesh exactly: the shader family hash includes the
    // source text and these basenames, not the resource path.
    driver->CreateShader(vertexSource, fragmentSource, key, "VS_Mesh.hlsl", "FS_Mesh.hlsl");
  }
}

void BackyardScene::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void BackyardScene::OnDestoryScene() {
  DestroyAssets();
}

void BackyardScene::OnUpdate(float deltaSeconds) {
  m_deltaSeconds = deltaSeconds;
  if (!m_dumper.SkipCameraUpdates()) {
    m_cameraController.Update(deltaSeconds, t850::CameraUpdateContext{});
  }
  m_lightCamera.Update(0.0f);
  if (m_fluidReady && pFramework && pFramework->pVideoDriver) {
    if (!m_fluid.Step(pFramework->pVideoDriver)) {
      T8_LOG_ERROR("[Backyard] Fluid step failed; disabling the solver for this scene");
      m_fluidReady = false;
    } else {
      // Shading time follows the solver clock so ripples are frame-deterministic.
      SceneProp.TimeSeconds = m_fluid.SimulationTime();
    }
  }
  m_dumper.UpdateReplayState();
}

void BackyardScene::OnInput(InputManager* input) {
  if (!input) return;
  t850::CameraInputState state;
  state.moveForward = input->PressedKey(T800K_w);
  state.moveBackward = input->PressedKey(T800K_s);
  state.moveLeft = input->PressedKey(T800K_a);
  state.moveRight = input->PressedKey(T800K_d);
  state.jump = input->PressedKey(T800K_SPACE);
  state.crouch = input->PressedKey(T800K_LCTRL);
  state.sprint = input->PressedKey(T800K_LSHIFT);
  // Look only while a mouse button is held. Stray window-focus deltas would
  // otherwise rotate the camera without user intent. Both axes are inverted so
  // dragging moves the view opposite to the pointer.
  state.mouseLook = input->PressedMouseButton(0) || input->PressedMouseButton(2);
  state.mouseDeltaX = (std::clamp)(-static_cast<float>(input->xDelta), -200.0f, 200.0f);
  state.mouseDeltaY = (std::clamp)(static_cast<float>(input->yDelta), -200.0f, 200.0f);
  state.scrollDelta = input->scrollDelta;
  t850::ApplyGamepadToCameraInput(state, *input, m_deltaSeconds, true);
  m_cameraController.HandleInput(state);
}

void BackyardScene::OnDraw() {
  if (!m_assetsCreated) return;
  m_renderContainer.Execute(pFramework->pVideoDriver, m_deltaSeconds);

  if (m_fluidReady) {
    static int probeFrame = 0;
    if (++probeFrame == 120) {
      t850::FluidSimulation::Diagnostics diagnostics;
      if (m_fluid.ReadDiagnostics(pFramework->pVideoDriver, diagnostics))
        T8_LOG_INFO("[Backyard][fluid] samples=%u meanSpeed=%.4f maxSpeed=%.4f nonFinite=%u",
                    diagnostics.samples, diagnostics.meanSpeed, diagnostics.maxSpeed, diagnostics.nonFinite);
    }
  }

  if (m_dumper.ShouldDump(m_deltaSeconds)) {
    std::vector<t850::RTDumpEntry> targets = {
        {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
        {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
        {m_renderContainer.Graph().GetRTHandle("Deferred"), t850::BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
        {m_renderContainer.Graph().GetRTHandle("ExtraHelper"), t850::BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"}};
    m_dumper.DumpFrame(
        pFramework->pVideoDriver, m_camera, m_lightCamera, SceneProp, targets, m_deltaSeconds);
    if (m_dumper.ShouldExit()) std::exit(0);
  }
}
