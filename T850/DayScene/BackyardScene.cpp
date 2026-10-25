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
      // Slot 23 receives the solver's surface texture once the fluid is
      // initialized. The FLUID_SURFACE permutation is not yet registered in
      // the shader export pipeline, so the plane currently renders with the
      // standard mesh permutation (displacement dormant).
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

  // Start the volumetric solver and point it at the graph's surface target.
  t850::FluidSettings fluidSettings;
  fluidSettings.width = 64;
  fluidSettings.height = 32;
  fluidSettings.depth = 24;
  fluidSettings.pressureIterations = fluidIterations;
  fluidSettings.deltaTime = 1.0f / 60.0f;
  fluidSettings.cellSize = 0.09f;
  if (m_fluid.TryInitialize(driver, fluidSettings)) {
    m_fluidReady = true;
    if (!m_fluid.Seed(driver, true)) {
      T8_LOG_ERROR("[Backyard] Fluid seed dispatch failed");
    }
    // Bind the solver's stable surface texture straight onto the water
    // instance at slot 23 (t23). No render-graph RT involved: a graph target
    // written by an out-of-graph dispatch did not survive readback.
    t850::Texture* surface = m_fluid.SurfaceTexture();
    if (surface && m_waterHandle.IsValid()) {
      if (t850::PrimitiveInst* water = m_renderContainer.GetMesh(m_waterHandle)) {
        water->SetTexture(surface, 23);
        water->Update();
        T8_LOG_INFO("[Backyard] Water surface bound to solver texture (%dx%d)", surface->x, surface->y);
      }
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
  // otherwise rotate the camera without user intent.
  state.mouseLook = input->PressedMouseButton(0) || input->PressedMouseButton(2);
  state.mouseDeltaX = (std::clamp)(static_cast<float>(input->xDelta), -200.0f, 200.0f);
  state.mouseDeltaY = (std::clamp)(static_cast<float>(-input->yDelta), -200.0f, 200.0f);
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
      t850::Texture* surface = m_fluid.SurfaceTexture();
      float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      const int rt = m_fluid.SurfaceRenderTarget();
      const bool ok = surface && rt >= 0 && pFramework->pVideoDriver->ReadRTColorFloat(
        rt, t850::BaseDriver::COLOR0_ATTACHMENT, rgba);
      T8_LOG_INFO("[Backyard][surface] ok=%d height=%.4f foam=%.4f slopeX=%.4f slopeZ=%.4f",
                  ok ? 1 : 0, rgba[0], rgba[1], rgba[2], rgba[3]);
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
