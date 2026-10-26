#include <pch.h>

#include <BackyardScene.h>

#include <core/Config.h>
#include <core/EngineContext.h>
#include <imgui/DevGuiContext.h>
#include <scene/IBLResources.h>
#include <scene/SceneDescriptor.h>
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
  RebuildFluid();
}

bool BackyardScene::RebuildFluid() {
  if (!pFramework || !pFramework->pVideoDriver) return false;
  t850::BaseDriver* driver = pFramework->pVideoDriver;

  // Tear the solver down while the driver is idle. A texture still referenced by
  // an in-flight command list must never be destroyed.
  driver->FlushGPUResources();
  m_fluid.Shutdown(driver);
  m_fluidReady = false;
  if (m_waterSurfaceRT >= 0) {
    driver->DestroyRT(m_waterSurfaceRT);
    m_waterSurfaceRT = -1;
  }

  const t850::FluidSettings fluidSettings = MakeFluidSettings();
  m_appliedGridWidth = static_cast<int>(fluidSettings.width);
  m_appliedGridHeight = static_cast<int>(fluidSettings.height);
  m_appliedGridDepth = static_cast<int>(fluidSettings.depth);

  if (!m_fluid.TryInitialize(driver, fluidSettings)) {
    T8_LOG_INFO("[Backyard] Fluid solver unavailable; water renders without displacement");
    return false;
  }
  m_fluidReady = true;
  if (!m_fluid.Seed(driver, true))
    T8_LOG_ERROR("[Backyard] Fluid seed dispatch failed");

  // Bind a stable surface target onto the water instance at slot 23 (t23).
  // No render-graph RT involved: a graph target written by an out-of-graph
  // dispatch did not survive readback, so the RT is owned by the driver here.
  m_waterSurfaceRT = driver->CreateRT(1, t850::BaseRT::RGBA16F, t850::BaseRT::NOTHING,
                                     m_appliedGridWidth, m_appliedGridDepth, false, true);
  t850::Texture* surface = m_waterSurfaceRT >= 0
    ? driver->GetRTTexture(m_waterSurfaceRT, t850::BaseDriver::COLOR0_ATTACHMENT) : nullptr;
  if (!surface || !m_waterHandle.IsValid()) {
    T8_LOG_ERROR("[Backyard] Cannot bind the water surface texture");
    return false;
  }

  m_fluid.SetSurfaceOutput(surface);
  if (t850::PrimitiveInst* water = m_renderContainer.GetMesh(m_waterHandle)) {
    water->SetTexture(surface, 23);
    // Fabricated tiling ripple normal map (scripts/textures/build_water_normal.py).
    // Bound at slot 22 rather than the material normal-map slot 3, which crashes
    // the D3D12 path when the material itself has no normal map.
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
  T8_LOG_INFO("[Backyard] Fluid solver running on %dx%dx%d, %d pressure iterations",
              m_appliedGridWidth, m_appliedGridHeight, m_appliedGridDepth,
              static_cast<int>(SceneProp.Water.pressureIterations));
  return true;
}

t850::FluidSettings BackyardScene::MakeFluidSettings() const {
  const t850::WaterTunables& w = SceneProp.Water;
  t850::FluidSettings settings;
  settings.width = static_cast<uint32_t>((std::max)(w.gridWidth, 8));
  settings.height = static_cast<uint32_t>((std::max)(w.gridHeight, 4));
  settings.depth = static_cast<uint32_t>((std::max)(w.gridDepth, 8));
  settings.pressureIterations = static_cast<uint32_t>((std::max)(w.pressureIterations, 0));
  settings.cellSize = (std::max)(w.cellSize, 0.001f);
  settings.deltaTime = (std::max)(w.deltaTime, 1.0f / 1000.0f);
  settings.viscosity = w.viscosity;
  settings.gravity = w.gravity;
  settings.windX = w.windX;
  settings.windY = w.windY;
  settings.windZ = w.windZ;
  settings.damping = w.damping;
  settings.surfaceLevel = (std::min)((std::max)(w.surfaceLevel, 0.02f), 0.99f);
  settings.sorOmega = (std::min)((std::max)(w.sorOmega, 1.0f), 1.99f);
  return settings;
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
  // A grid change needs new 3D textures; everything else can be retuned in place.
  const t850::WaterTunables& w = SceneProp.Water;
  if (m_fluidReady &&
      (w.gridWidth != m_appliedGridWidth || w.gridHeight != m_appliedGridHeight ||
       w.gridDepth != m_appliedGridDepth)) {
    RebuildFluid();
  }
  if (m_fluidReady && pFramework && pFramework->pVideoDriver) {
    m_fluid.SetPressureIterations(static_cast<uint32_t>((std::max)(w.pressureIterations, 0)));
    m_fluid.SetSurfaceLevel((std::min)((std::max)(w.surfaceLevel, 0.02f), 0.99f));
    m_fluid.SetSOROmega((std::min)((std::max)(w.sorOmega, 1.0f), 1.99f));
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
  // otherwise rotate the camera without user intent. Only the vertical axis is
  // inverted.
  state.mouseLook = input->PressedMouseButton(0) || input->PressedMouseButton(2);
  state.mouseDeltaX = (std::clamp)(static_cast<float>(input->xDelta), -200.0f, 200.0f);
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

void BackyardScene::DrawDevGui(t850::DevGuiContext& gui) {
  if (!gui.BeginPanel("Backyard Water")) {
    return;
  }

  t850::WaterTunables& w = SceneProp.Water;

  // One line per knob: build a SliderDesc inline and hand the value by reference,
  // so an edit lands in SceneProps immediately and rides to the shader on the
  // next draw (or to the solver on the next dispatch).
  auto slider = [&gui](const char* name, const char* label, float& value,
                       float minValue, float maxValue, float step) {
    t850::SliderDesc desc;
    desc.name = name;
    desc.label = label;
    desc.min_val = minValue;
    desc.max_val = maxValue;
    desc.step = step;
    desc.default_val = value;
    return gui.Slider(desc, value);
  };
  auto sliderInt = [&gui](const char* name, const char* label, int& value,
                          float minValue, float maxValue, float step) {
    t850::SliderDesc desc;
    desc.name = name;
    desc.label = label;
    desc.min_val = minValue;
    desc.max_val = maxValue;
    desc.step = step;
    desc.default_val = static_cast<float>(value);
    return gui.SliderInt(desc, value);
  };

  if (gui.BeginSection("Solver")) {
    sliderInt("water_grid_w", "grid width", w.gridWidth, 16.0f, 192.0f, 1.0f);
    sliderInt("water_grid_h", "grid height", w.gridHeight, 4.0f, 64.0f, 1.0f);
    sliderInt("water_grid_d", "grid depth", w.gridDepth, 16.0f, 192.0f, 1.0f);
    sliderInt("water_iterations", "pressure iterations", w.pressureIterations, 1.0f, 200.0f, 1.0f);
    slider("water_sor_omega", "SOR omega (1 = Gauss-Seidel)", w.sorOmega, 1.0f, 1.99f, 0.001f);
    slider("water_cell", "cell size (m)", w.cellSize, 0.01f, 0.30f, 0.001f);
    slider("water_dt", "delta time (s)", w.deltaTime, 1.0f / 240.0f, 1.0f / 15.0f, 0.0001f);
    slider("water_viscosity", "viscosity", w.viscosity, 0.0f, 0.5f, 0.001f);
    slider("water_gravity", "gravity", w.gravity, 0.0f, 30.0f, 0.01f);
    slider("water_wind_x", "wind X", w.windX, -5.0f, 5.0f, 0.01f);
    slider("water_wind_y", "wind Y", w.windY, -5.0f, 5.0f, 0.01f);
    slider("water_wind_z", "wind Z", w.windZ, -5.0f, 5.0f, 0.01f);
    slider("water_damping", "damping", w.damping, 0.0f, 4.0f, 0.01f);
    slider("water_surface_level", "surface level", w.surfaceLevel, 0.05f, 0.99f, 0.001f);
    gui.Separator();
    if (gui.Button("Reseed solver") && m_fluidReady && pFramework && pFramework->pVideoDriver)
      m_fluid.Seed(pFramework->pVideoDriver, true);
    if (gui.Button("Rebuild grid"))
      RebuildFluid();
  }

  if (gui.BeginSection("Optics")) {
    slider("water_absorb_r", "absorption R", w.absorbR, 0.0f, 2.0f, 0.001f);
    slider("water_absorb_g", "absorption G", w.absorbG, 0.0f, 2.0f, 0.001f);
    slider("water_absorb_b", "absorption B", w.absorbB, 0.0f, 2.0f, 0.001f);
    slider("water_thickness_scale", "body depth scale", w.thicknessScale, 0.5f, 20.0f, 0.01f);
    slider("water_shallow_r", "shallow R", w.shallowR, 0.0f, 1.0f, 0.001f);
    slider("water_shallow_g", "shallow G", w.shallowG, 0.0f, 1.0f, 0.001f);
    slider("water_shallow_b", "shallow B", w.shallowB, 0.0f, 1.0f, 0.001f);
    slider("water_deep_r", "deep R", w.deepR, 0.0f, 1.0f, 0.001f);
    slider("water_deep_g", "deep G", w.deepG, 0.0f, 1.0f, 0.001f);
    slider("water_deep_b", "deep B", w.deepB, 0.0f, 1.0f, 0.001f);
    slider("water_refract", "refraction strength", w.refractionStrength, 0.0f, 0.15f, 0.0005f);
    slider("water_border_fade", "border fade width (uv)", w.refractionBorderFade, 0.005f, 0.5f, 0.001f);
    slider("water_caustic_gain", "caustic gain", w.causticGain, 0.0f, 4.0f, 0.01f);
    slider("water_caustic_power", "caustic power", w.causticPower, 0.5f, 12.0f, 0.01f);
    slider("water_caustic_slope", "caustic slope scale", w.causticSlopeScale, 0.1f, 10.0f, 0.01f);
    slider("water_caustic_base", "floor base light", w.causticFloorBase, 0.0f, 2.0f, 0.01f);
    slider("water_fresnel_boost", "fresnel boost", w.fresnelBoost, 0.0f, 6.0f, 0.01f);
    slider("water_fresnel_bias", "fresnel bias", w.fresnelBias, 0.0f, 1.0f, 0.001f);
    slider("water_scatter_wrap", "scatter wrap", w.scatterWrap, 0.0f, 0.5f, 0.001f);
    slider("water_scatter_normal", "scatter normal", w.scatterNormal, 0.0f, 0.5f, 0.001f);
    slider("water_scatter_depth", "scatter depth", w.scatterDepth, 0.0f, 3.0f, 0.01f);
    slider("water_line_y", "water line Y (m)", w.waterLineY, -1.0f, 0.5f, 0.001f);
    slider("water_to_floor", "water to floor (m)", w.waterToFloor, 0.1f, 4.0f, 0.01f);
  }

  if (gui.BeginSection("Surface detail")) {
    slider("water_wave_scale", "analytic wave scale", w.analyticWaveScale, 0.0f, 4.0f, 0.01f);
    slider("water_wave_speed", "analytic wave speed", w.analyticWaveSpeed, 0.0f, 4.0f, 0.01f);
    slider("water_vertex_wave", "vertex wave scale", w.vertexWaveScale, 0.0f, 4.0f, 0.01f);
    slider("water_normal_gain", "normal map gain", w.normalMapGain, 0.0f, 3.0f, 0.01f);
    slider("water_normal_tile_x", "normal tile X", w.normalTileX, 1.0f, 60.0f, 0.05f);
    slider("water_normal_tile_z", "normal tile Z", w.normalTileZ, 1.0f, 60.0f, 0.05f);
    slider("water_normal_drift_x", "normal drift X", w.normalDriftX, -0.2f, 0.2f, 0.0005f);
    slider("water_normal_drift_z", "normal drift Z", w.normalDriftZ, -0.2f, 0.2f, 0.0005f);
    slider("water_normal_tile2_x", "normal tile2 X", w.normalTile2X, 1.0f, 60.0f, 0.05f);
    slider("water_normal_tile2_z", "normal tile2 Z", w.normalTile2Z, 1.0f, 60.0f, 0.05f);
    slider("water_normal_drift2_x", "normal drift2 X", w.normalDrift2X, -0.2f, 0.2f, 0.0005f);
    slider("water_normal_drift2_z", "normal drift2 Z", w.normalDrift2Z, -0.2f, 0.2f, 0.0005f);
  }

  if (gui.BeginSection("Reflection")) {
    slider("water_ibl_scale", "IBL reflection scale", w.iblReflectionScale, 0.0f, 4.0f, 0.01f);
    slider("water_direct_scale", "direct light scale", w.directLightScale, 0.0f, 8.0f, 0.01f);
    slider("water_glint_power", "sun glint power", w.sunGlintPower, 1.0f, 4000.0f, 1.0f);
    slider("water_glint_scale", "sun glint scale", w.sunGlintScale, 0.0f, 40.0f, 0.1f);
    slider("water_ssr_weight", "SSR weight", w.ssrWeight, 0.0f, 1.0f, 0.01f);
    slider("water_ssr_distortion", "SSR distortion", w.ssrDistortion, 0.0f, 0.05f, 0.0001f);
    slider("water_ssr_blur_base", "SSR blur base (px)", w.ssrBlurBase, 0.0f, 12.0f, 0.1f);
    slider("water_ssr_blur_range", "SSR blur range (px)", w.ssrBlurRange, 0.0f, 40.0f, 0.1f);
    slider("water_ssr_max_dist", "SSR max distance", w.ssrMaxDistance, 1.0f, 120.0f, 0.5f);
    slider("water_ssr_step_start", "SSR step start (m)", w.ssrStepStart, 0.005f, 0.5f, 0.001f);
    slider("water_ssr_step_growth", "SSR step growth", w.ssrStepGrowth, 1.0f, 2.0f, 0.01f);
    slider("water_ssr_step_max", "SSR step max (m)", w.ssrStepMax, 0.05f, 5.0f, 0.01f);
    sliderInt("water_ssr_steps", "SSR steps", w.ssrSteps, 1.0f, 256.0f, 1.0f);
  }

  if (gui.BeginSection("Ripple octaves")) {
    static const char* const kOctaveNames[6] = { "octave 1 (coarse)", "octave 2", "octave 3",
                                                 "octave 4", "octave 5", "octave 6 (fine)" };
    static const char* const kAmpIds[6] = { "water_ripple_amp0", "water_ripple_amp1", "water_ripple_amp2",
                                            "water_ripple_amp3", "water_ripple_amp4", "water_ripple_amp5" };
    static const char* const kLenIds[6] = { "water_ripple_len0", "water_ripple_len1", "water_ripple_len2",
                                            "water_ripple_len3", "water_ripple_len4", "water_ripple_len5" };
    static const char* const kSpeedIds[6] = { "water_ripple_speed0", "water_ripple_speed1", "water_ripple_speed2",
                                              "water_ripple_speed3", "water_ripple_speed4", "water_ripple_speed5" };
    static const char* const kFadeIds[6] = { "water_ripple_fade0", "water_ripple_fade1", "water_ripple_fade2",
                                             "water_ripple_fade3", "water_ripple_fade4", "water_ripple_fade5" };
    for (int i = 0; i < 6; ++i) {
      if (gui.BeginSection(kOctaveNames[i], i < 3)) {
        slider(kAmpIds[i], "amplitude (m)", w.rippleAmp[i], 0.0f, 0.05f, 0.0001f);
        slider(kLenIds[i], "wavelength (m)", w.rippleLen[i], 0.02f, 4.0f, 0.001f);
        slider(kSpeedIds[i], "speed", w.rippleSpeed[i], 0.0f, 8.0f, 0.01f);
        slider(kFadeIds[i], "distance fade (m)", w.rippleFade[i], 1.0f, 200.0f, 0.5f);
      }
    }
  }

  if (gui.BeginSection("Foam")) {
    slider("water_foam_crest", "crest foam", w.foamCrest, 0.0f, 2.0f, 0.01f);
    slider("water_foam_border", "border foam", w.foamBorder, 0.0f, 4.0f, 0.01f);
    slider("water_foam_noise", "foam noise", w.foamNoise, 0.0f, 4.0f, 0.01f);
    slider("water_foam_strength", "foam strength", w.foamStrength, 0.0f, 1.0f, 0.01f);
    slider("water_foam_r", "foam R", w.foamR, 0.0f, 1.0f, 0.001f);
    slider("water_foam_g", "foam G", w.foamG, 0.0f, 1.0f, 0.001f);
    slider("water_foam_b", "foam B", w.foamB, 0.0f, 1.0f, 0.001f);
    slider("water_foam_f0x", "noise freq 0 X", w.foamFreq0X, 0.5f, 80.0f, 0.1f);
    slider("water_foam_f0y", "noise freq 0 Y", w.foamFreq0Y, 0.5f, 80.0f, 0.1f);
    slider("water_foam_f1x", "noise freq 1 X", w.foamFreq1X, 0.5f, 120.0f, 0.1f);
    slider("water_foam_f1y", "noise freq 1 Y", w.foamFreq1Y, 0.5f, 120.0f, 0.1f);
    slider("water_foam_f2x", "noise freq 2 X", w.foamFreq2X, 0.5f, 200.0f, 0.1f);
    slider("water_foam_f2y", "noise freq 2 Y", w.foamFreq2Y, 0.5f, 200.0f, 0.1f);
    slider("water_foam_fade", "noise distance fade", w.foamFade, 1.0f, 120.0f, 0.5f);
  }

  if (gui.BeginSection("Toggles")) {
    gui.Checkbox(t850::CheckboxDesc{ "water_normal_map_on", "normal map", true, true },
                 w.normalMapEnabled);
    gui.Checkbox(t850::CheckboxDesc{ "water_ssr_on", "screen-space reflections", true, true },
                 w.ssrEnabled);
  }

  gui.Separator();
  gui.Text("Values apply live. Grid size changes rebuild the solver.");
  if (gui.Button("Reset water settings")) {
    w = t850::WaterTunables{};
    RebuildFluid();
  }

  gui.EndPanel();
}
