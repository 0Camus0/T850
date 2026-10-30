#include <pch.h>

#include <BackyardScene.h>

#include <core/Config.h>
#include <core/EngineContext.h>
#include <BackyardWaterJson.h>
#include <imgui/DevGuiContext.h>
#include <scene/IBLResources.h>
#include <scene/RenderMesh.h>
#include <scene/SceneDescriptor.h>
#include <scene/PrimitiveManager.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
  constexpr const char* kSceneDescriptorPath = "Scenes/BackyardScene.json";
  constexpr const char* kRenderGraphPath = "Scenes/BackyardScene_RenderGraph.json";
  // W-11: the water tunables file. Separate from the scene descriptor because glaze
  // cannot be included in SceneDescriptor.h without breaking DayScene.cpp (see
  // BackyardWaterJson.h).
  constexpr const char* kWaterTunablesPath = "Scenes/BackyardWater.json";
  constexpr const char* kBallPalettePath = "Scenes/BackyardBallMaterials.json";
  // Mouse drag limits: the steering speed bounds the solver's boundary velocity and its
  // substep count; the ceiling keeps a held ball within sight of the pool.
  constexpr float kMaxDragSpeed = 6.0f;          // m/s
  constexpr float kMaxDragHeightAboveWater = 2.5f; // m

  struct BackyardAsset {
    const char* path;
    const char* label;
  };

  // Emitted by scripts/blender/build_backyard.py. Each glTF node already
  // carries its authored world transform, so instances stay at the origin.
  constexpr std::array<BackyardAsset, 7> kBackyardAssets = {{
    {"Models/Backyard/Backyard_Ground.glb", "ground"},
    {"Models/Backyard/Backyard_House.glb", "house"},
    {"Models/Backyard/Backyard_Pool.glb", "pool"},
    {"Models/Backyard/Backyard_Wall.glb", "wall"},
    {"Models/Backyard/Backyard_Props.glb", "props"},
    {"Models/Backyard/Backyard_Water.glb", "water"},
    // Unit-radius sphere at the origin, emitted by scripts/models/build_ball.py.
    // The scene positions and scales it every frame from the simulated body, so
    // the asset carries no placement of its own.
    {"Models/Backyard/Backyard_Ball.glb", "ball"},
  }};
}

int BackyardScene::s_spawnOnStart = 0;
uint32_t BackyardScene::s_spawnSeed = 0;
bool BackyardScene::s_pickTest = false;

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
  // W-11: the water tunables come from Scenes/BackyardWater.json (see BackyardWaterJson.h
  // for why they are not inline in the scene descriptor). A bad key is fatal so a typo
  // never silently falls back to a default.
  if (!t850::LoadBackyardWaterTunables(kWaterTunablesPath, SceneProp.Water)) {
    throw std::runtime_error(std::string("Backyard scene has an invalid water tunables file: ") +
                             kWaterTunablesPath);
  }
  // W-22: a non-zero spawnSeed reproduces a spawn sequence; 0 draws a fresh seed so
  // each run differs. --backyard-spawn-seed overrides the JSON value for tests.
  if (s_spawnSeed != 0) SceneProp.Water.spawnSeed = static_cast<int32_t>(s_spawnSeed);
  m_spawnRng = SceneProp.Water.spawnSeed != 0
      ? static_cast<uint32_t>(SceneProp.Water.spawnSeed)
      : static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  m_ballMaterialBag.Reset(m_spawnRng ^ 0x9E3779B9u);
  m_extraBallHandles.clear();
  m_dragBody = -1;
  m_cursorFreed = false;
  m_pickTestInput = InputManager{};
  m_pickTestFrame = 0;
  m_pickTestTarget = -1;
  m_pickTestVictim = -1;
  m_pickTestFailures = 0;
  m_pickTestDone = false;
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
      // FLUID_SURFACE is NOT set here. It selects the displacement/foam shader
      // permutation, and that permutation is only compiled once the compute solver
      // is up. Setting it at mesh-load time made every backend that has no working
      // solver (GL, WebGPU) request a permutation that was never compiled, so the
      // draw was skipped and the water plane rendered invisible. The bit is now
      // applied in RebuildFluid() strictly after TryInitialize() succeeds, and
      // cleared whenever the solver is torn down.
      //
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
    if (std::string(asset.label) == "ball") { m_ballHandle = handle; m_ballPrimitive = primitive; }
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

  LoadBallPalette();
  if (m_ballHandle.IsValid()) {
    if (t850::PrimitiveInst* ball = m_renderContainer.GetMesh(m_ballHandle)) {
      if (const char* material = AssignBallMaterial(*ball))
        T8_LOG_INFO("[Backyard] Scripted ball material: %s", material);
    }
  }

  // Grid matches the pool basin: 9.0 m (X) x 1.6 m (Y) x 4.5 m (Z) at a
  // 9 cm cell. The surface UV space therefore maps 1:1 onto the pool mesh.
  RebuildFluid();

  // Test hook (--backyard-spawn): drop N balls as soon as the solver exists, so the
  // multi-body path can be exercised deterministically without the GUI.
  for (int i = 0; i < s_spawnOnStart && m_fluidReady; ++i) {
    if (!SpawnBall()) break;
  }
}

bool BackyardScene::RebuildFluid() {
  if (!pFramework || !pFramework->pVideoDriver) return false;
  t850::BaseDriver* driver = pFramework->pVideoDriver;
  EndBallDrag();

  // Tear the solver down while the driver is idle. A texture still referenced by
  // an in-flight command list must never be destroyed.
  driver->FlushGPUResources();
  m_fluid.Shutdown(driver);
  m_fluidReady = false;
  SetWaterFluidFeature(false);
  if (m_waterSurfaceRT >= 0) {
    driver->DestroyRT(m_waterSurfaceRT);
    m_waterSurfaceRT = -1;
  }

  const t850::FluidSettings fluidSettings = MakeFluidSettings();

  if (!m_fluid.TryInitialize(driver, fluidSettings)) {
    T8_LOG_INFO("[Backyard] Fluid solver unavailable; water renders as a flat plane "
                "with the ordinary material permutation (no FLUID_SURFACE)");
    SetWaterFluidFeature(false);
    return false;
  }
  // The solver derives its grid height and fill level from the pool layout JSON,
  // so read the applied values back rather than trusting the request.
  {
    const t850::FluidSettings& applied = m_fluid.Settings();
    m_appliedGridWidth = static_cast<int>(applied.width);
    m_appliedGridHeight = static_cast<int>(applied.height);
    m_appliedGridDepth = static_cast<int>(applied.depth);
    // Write the derived dims back into the tunables. OnUpdate() rebuilds the
    // solver whenever the requested grid differs from the applied one, so leaving
    // the tunable at its literal default made that fire EVERY frame and silently
    // re-seed the simulation before it could ever accumulate. This was why the
    // water never appeared to move.
    SceneProp.Water.gridWidth = m_appliedGridWidth;
    SceneProp.Water.gridHeight = m_appliedGridHeight;
    SceneProp.Water.gridDepth = m_appliedGridDepth;
    // The solver snaps the cell size so the pool length is a whole number of cells.
    m_appliedCellSize = applied.cellSize;
    SceneProp.Water.cellSize = applied.cellSize;
    m_gridEditCell = applied.cellSize;
    // Same for the fill level. LoadPoolLayout() derives it so the still surface
    // lands exactly on the authored water plane; leaving the tunable at its literal
    // default meant OnUpdate() re-imposed the wrong value, and a reset would re-seed
    // the pool at 96% instead of the derived 78.8%.
    SceneProp.Water.surfaceLevel = applied.surfaceLevel;
  }
  m_fluidReady = true;
  // Start from a motionless pool; Seed() writes a self-test velocity pattern.
  if (!m_fluid.ResetToRest(driver))
    T8_LOG_ERROR("[Backyard] Fluid reset to rest failed");

  // Initialise the dropped sphere from JSON-driven values: released after a delay
  // from a configured height above the water plane.
  {
    t850::FluidSphere& sphere = m_fluid.DroppedSphere();
    sphere.radius = SceneProp.Water.sphereRadius;
    sphere.densityRatio = SceneProp.Water.sphereDensityRatio;
    sphere.dragCoefficient = SceneProp.Water.sphereDragCoefficient;
    sphere.restitution = SceneProp.Water.sphereRestitution;
    sphere.heaveDamping = SceneProp.Water.sphereHeaveDamping;
    sphere.x = SceneProp.Water.sphereDropX;
    sphere.z = SceneProp.Water.sphereDropZ;
    sphere.y = SceneProp.Water.sphereDropHeight;
    sphere.vx = SceneProp.Water.sphereInitialVX;
    sphere.vy = SceneProp.Water.sphereInitialVY;
    sphere.vz = SceneProp.Water.sphereInitialVZ;
    sphere.enabled = false;  // released after the delay
    m_sphereReleaseDelay = SceneProp.Water.sphereReleaseDelay;
  }

  // Bind a stable surface target onto the water instance at slot 23 (t23).
  // No render-graph RT involved: a graph target written by an out-of-graph
  // dispatch did not survive readback, so the RT is owned by the driver here.
  m_waterSurfaceRT = driver->CreateRT(1, t850::BaseRT::RGBA16F, t850::BaseRT::NOTHING,
                                     m_appliedGridWidth, m_appliedGridDepth, false, true);
  t850::Texture* surface = m_waterSurfaceRT >= 0
    ? driver->GetRTTexture(m_waterSurfaceRT, t850::BaseDriver::COLOR0_ATTACHMENT) : nullptr;
  if (!surface || !m_waterHandle.IsValid()) {
    T8_LOG_ERROR("[Backyard] Cannot bind the water surface texture");
    SetWaterFluidFeature(false);
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
    // Per-column static floor height, so absorption uses the real depth over the
    // steps (0.26-0.86 m) instead of one constant 1.56 m.
    if (BindFloorHeightTexture(driver, water))
      SceneProp.Water.floorTextureBound = true;
    else
      SceneProp.Water.floorTextureBound = false;
    WaterUVMatchesSolver(water);
    // Only now — with a live solver and a bound surface texture — may the
    // displacement permutation be selected.
    water->PersistentFeatureBits |= t850::ShaderKey::FLUID_SURFACE;
    water->Update();
    T8_LOG_INFO("[Backyard] Water surface bound to solver texture (%dx%d), FLUID_SURFACE enabled",
                surface->x, surface->y);
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
  settings.gravity = w.gravity;
  settings.viscosity = (std::max)(w.viscosity, 0.0f);
  settings.windX = w.windX;
  settings.windY = w.windY;
  settings.windZ = w.windZ;
  settings.damping = w.damping;
  settings.surfaceLevel = (std::min)((std::max)(w.surfaceLevel, 0.02f), 0.99f);
  settings.sorOmega = (std::min)((std::max)(w.sorOmega, 1.0f), 1.99f);
  settings.forcingAmplitude = (std::max)(w.forcingAmplitude, 0.0f);
  // Let the solver derive its row count and fill level from the pool layout JSON,
  // so the basin gets real freeboard above the waterline and nothing about the
  // pool placement is a literal.
  settings.deriveGridFromLayout = true;
  settings.waterPlaneY = w.waterPlaneY;
  settings.freeboard = w.freeboard;
  // The pool's ceiling is 0.42 m of real air, so the top of the domain must vent
  // rather than act as a wall. The solver keeps this off by default so the
  // synthetic closed-box tests stay closed.
  settings.openCeiling = w.openCeiling;
  // W-11: solver values that were literals before, now data-driven.
  settings.maxSubstepDt = (std::max)(w.maxSubstepDt, 1.0f / 1000.0f);
  settings.alphaCompression = (std::min)((std::max)(w.alphaCompression, 0.0f), 1.0f);
  return settings;
}

void BackyardScene::DestroyAssets() {
  if (!m_assetsCreated && !pFramework) return;
  EndBallDrag();
  if (m_fluidReady && pFramework && pFramework->pVideoDriver) {
    m_fluid.Shutdown(pFramework->pVideoDriver);
  }
  m_fluidReady = false;
  m_fluidDebug.Destroy();
  m_fluidDebugMeshBound = false;
  if (m_waterSurfaceRT >= 0 && pFramework && pFramework->pVideoDriver)
    pFramework->pVideoDriver->DestroyRT(m_waterSurfaceRT);
  m_waterSurfaceRT = -1;
  if (m_waterFloorTexture >= 0 && pFramework && pFramework->pVideoDriver)
    pFramework->pVideoDriver->DestroyTexture(m_waterFloorTexture);
  m_waterFloorTexture = -1;
  m_waterFloorWidth = 0;
  m_waterFloorDepth = 0;
  DestroyBallMaterials();
  SceneProp.Water.floorTextureBound = false;
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

bool BackyardScene::BindFloorHeightTexture(t850::BaseDriver* driver, t850::PrimitiveInst* water) {
  if (!driver || !water) return false;
  const t850::FluidSettings& fs = m_fluid.Settings();
  const uint32_t width = fs.width;
  const uint32_t depth = fs.depth;
  const uint32_t rows = fs.height;
  // The authored mask is static, so an existing texture with the same dimensions is
  // still correct: rebind it rather than churning a new one on every slider release.
  if (m_waterFloorTexture >= 0 && m_waterFloorWidth == width && m_waterFloorDepth == depth) {
    if (t850::Texture* existing = driver->GetTexture(m_waterFloorTexture)) {
      water->SetTexture(existing, 24);
      water->Update();
      return true;
    }
    m_waterFloorTexture = -1;
  }
  const std::vector<uint32_t>& mask = m_fluid.BaseMask();
  if (mask.size() != static_cast<size_t>(width) * rows * depth) {
    T8_LOG_ERROR("[Backyard] Floor height texture: mask holds %zu cells, expected %ux%ux%u",
                 mask.size(), width, rows, depth);
    return false;
  }
  // One texel per solver column, holding the world Y of the floor. The floor is the
  // top of the solid run that starts at the basin bottom: the submerged steps are
  // solid from the floor up, and an open water column has no solid there at all.
  // Built from the AUTHORED mask, so the floating body (which is stamped into a
  // separate working mask) can never move the floor.
  std::vector<float> texels(static_cast<size_t>(width) * depth * 4, 0.0f);
  float lowest = 0.0f;
  float highest = 0.0f;
  bool first = true;
  for (uint32_t z = 0; z < depth; ++z) {
    for (uint32_t x = 0; x < width; ++x) {
      uint32_t solidRun = 0;
      for (uint32_t y = 0; y < rows; ++y) {
        const size_t index = x + static_cast<size_t>(width) * (y + static_cast<size_t>(rows) * z);
        if (mask[index] == 0u) break;
        ++solidRun;
      }
      const float floorY = fs.basinMinY + static_cast<float>(solidRun) * fs.cellSize;
      const size_t out = (static_cast<size_t>(x) + static_cast<size_t>(width) * z) * 4;
      texels[out] = floorY;
      if (first) { lowest = highest = floorY; first = false; }
      else { lowest = (std::min)(lowest, floorY); highest = (std::max)(highest, floorY); }
    }
  }
  const int textureIndex = driver->CreateFloatTexture(static_cast<int>(width),
                                                      static_cast<int>(depth), texels.data());
  t850::Texture* texture = textureIndex >= 0 ? driver->GetTexture(textureIndex) : nullptr;
  if (!texture) {
    T8_LOG_ERROR("[Backyard] Cannot create the floor height texture (%ux%u)", width, depth);
    return false;
  }
  water->SetTexture(texture, 24);
  water->Update();
  if (m_waterFloorTexture >= 0 && m_waterFloorTexture != textureIndex)
    driver->DestroyTexture(m_waterFloorTexture);
  m_waterFloorTexture = textureIndex;
  m_waterFloorWidth = width;
  m_waterFloorDepth = depth;
  T8_LOG_INFO("[Backyard] Floor height texture bound (%ux%u), floor Y %.3f .. %.3f "
              "(water plane %.3f, so depth %.3f .. %.3f m)",
              width, depth, lowest, highest, fs.waterPlaneY,
              fs.waterPlaneY - highest, fs.waterPlaneY - lowest);
  return true;
}

float BackyardScene::NextSpawnRandom() {
  // Deterministic LCG (Numerical Recipes constants). spawnSeed != 0 makes a whole
  // spawn sequence reproducible, which the acceptance test relies on.
  m_spawnRng = m_spawnRng * 1664525u + 1013904223u;
  return static_cast<float>((m_spawnRng >> 8) & 0x00FFFFFFu) / 16777216.0f;
}

void BackyardScene::LoadBallPalette() {
  DestroyBallMaterials();
  if (!t850::LoadBackyardBallPalette(kBallPalettePath, m_ballPalette)) {
    m_ballPalette = t850::BackyardBallPalette{};
    return;
  }
  // The texture files are fetched by scripts/DownloadTextures.ps1, not committed, so a
  // set missing on disk is dropped once here instead of failing at every spawn.
  const auto& locator = t850::ResourceLocator::Instance();
  const t850::BackyardBallMaps& maps = m_ballPalette.maps;
  const size_t listed = m_ballPalette.materials.size();
  std::erase_if(m_ballPalette.materials, [&](const t850::BackyardBallMaterial& material) {
    const std::string folder = "Textures/Ball/" + material.id + "/";
    return !locator.Exists(folder + maps.color) || !locator.Exists(folder + maps.normal) ||
           !locator.Exists(folder + maps.orm);
  });
  m_ballMaterialMaps.assign(m_ballPalette.materials.size(), BallMaterialMaps{});
  if (m_ballPalette.materials.size() < listed) {
    T8_LOG_INFO("[Backyard] Ball palette: only %zu of %zu materials are on disk; run "
                "scripts/DownloadTextures.ps1 to fetch the rest", m_ballPalette.materials.size(), listed);
  } else {
    T8_LOG_INFO("[Backyard] Ball palette: %zu materials from '%s'", listed, kBallPalettePath);
  }
}

void BackyardScene::DestroyBallMaterials() {
  t850::BaseDriver* driver = pFramework ? pFramework->pVideoDriver : nullptr;
  if (driver) {
    for (const BallMaterialMaps& maps : m_ballMaterialMaps) {
      for (int index : {maps.colorIndex, maps.normalIndex, maps.ormIndex})
        if (index >= 0) driver->DestroyTexture(index);
    }
  }
  m_ballMaterialMaps.clear();
  m_ballMaterialBag.Reset(m_spawnRng ^ 0x9E3779B9u);
}

const char* BackyardScene::AssignBallMaterial(t850::PrimitiveInst& inst) {
  t850::BaseDriver* driver = pFramework ? pFramework->pVideoDriver : nullptr;
  const uint32_t count = static_cast<uint32_t>(m_ballMaterialMaps.size());
  if (!driver || count == 0) return nullptr;
  // A set that fails to load is skipped; at most one full pass over the palette.
  for (uint32_t attempt = 0; attempt < count; ++attempt) {
    uint32_t index = 0;
    if (!m_ballMaterialBag.Next(count, index)) return nullptr;

    BallMaterialMaps& maps = m_ballMaterialMaps[index];
    if (!maps.attempted) {
      maps.attempted = true;
      const std::string folder = "Ball/" + m_ballPalette.materials[index].id + "/";
      auto load = [&](const std::string& file, int& slot) -> t850::Texture* {
        slot = driver->CreateTexture(folder + file);
        return slot >= 0 ? driver->GetTexture(slot) : nullptr;
      };
      maps.color = load(m_ballPalette.maps.color, maps.colorIndex);
      maps.normal = load(m_ballPalette.maps.normal, maps.normalIndex);
      maps.orm = load(m_ballPalette.maps.orm, maps.ormIndex);
    }
    if (maps.color && maps.normal && maps.orm) {
      inst.SetTexture(maps.color, 0);
      inst.SetTexture(maps.normal, 3);
      inst.SetTexture(maps.orm, 6);
      inst.InstanceMaterialMaps = true;
      return m_ballPalette.materials[index].label.c_str();
    }
  }
  return nullptr;
}

t850::Ray BackyardScene::CursorRay(float mouseX, float mouseY) const {
  const t850::BaseDriver* driver = pFramework ? pFramework->pVideoDriver : nullptr;
  const float width = static_cast<float>((std::max)(driver ? driver->width : 1, 1));
  const float height = static_cast<float>((std::max)(driver ? driver->height : 1, 1));
  XMATRIX44 viewProjection = m_camera.VP;
  XMATRIX44 inverseViewProjection;
  viewProjection.Inverse(&inverseViewProjection);
  // Unproject a mid-depth point and aim from the eye: correct for standard and reverse-Z
  // depth alike, where ScreenPointToRay assumes NDC z = 0 is the near plane.
  const float ndcX = 2.0f * ((mouseX + 0.5f) / width) - 1.0f;
  const float ndcY = 1.0f - 2.0f * ((mouseY + 0.5f) / height);
  const XVECTOR3 point = t850::TransformPoint(XVECTOR3(ndcX, ndcY, 0.5f), inverseViewProjection);
  XVECTOR3 direction(point.x - m_camera.Eye.x, point.y - m_camera.Eye.y, point.z - m_camera.Eye.z);
  direction.Normalize();
  t850::Ray ray;
  ray.origin = XVECTOR3(m_camera.Eye.x, m_camera.Eye.y, m_camera.Eye.z);
  ray.direction = direction;
  return ray;
}

bool BackyardScene::ProjectToScreen(const XVECTOR3& world, float& x, float& y) const {
  const t850::BaseDriver* driver = pFramework ? pFramework->pVideoDriver : nullptr;
  const float width = static_cast<float>((std::max)(driver ? driver->width : 1, 1));
  const float height = static_cast<float>((std::max)(driver ? driver->height : 1, 1));
  const XMATRIX44& vp = m_camera.VP;
  const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
  if (w <= 1e-6f) return false;   // behind the eye
  const XVECTOR3 ndc = t850::TransformPoint(world, vp);
  // Exact inverse of the pixel-to-NDC mapping in CursorRay.
  x = (ndc.x + 1.0f) * 0.5f * width - 0.5f;
  y = (1.0f - ndc.y) * 0.5f * height - 0.5f;
  return x >= 0.0f && y >= 0.0f && x < width && y < height;
}

bool BackyardScene::HandleBallDrag(InputManager* input, bool imguiWantsMouse, bool ctrlDown) {
  const float mouseX = static_cast<float>(input->mouseX);
  const float mouseY = static_cast<float>(input->mouseY);
  if (m_dragBody >= 0) {
    if (!input->PressedMouseButton(0) || !m_fluidReady ||
        static_cast<uint32_t>(m_dragBody) >= m_fluid.BodyCount()) {
      EndBallDrag();
      return false;
    }
    if (input->scrollDelta != 0.0f)
      m_dragDistance = (std::max)(m_dragDistance * (1.0f + 0.1f * input->scrollDelta), 0.5f);
    const t850::Ray ray = CursorRay(mouseX, mouseY);
    m_dragTarget = XVECTOR3(ray.origin.x + ray.direction.x * m_dragDistance + m_dragOffset.x,
                            ray.origin.y + ray.direction.y * m_dragDistance + m_dragOffset.y,
                            ray.origin.z + ray.direction.z * m_dragDistance + m_dragOffset.z);
    return true;
  }

  // Consume the click even over the GUI, so a press that starts on a panel never turns
  // into a grab when the cursor later crosses a ball. Without Ctrl the click is the
  // camera's, never a pick.
  const bool clicked = input->PressedOnceMouseButton(0);
  if (!clicked || !ctrlDown || imguiWantsMouse || !m_fluidReady) return false;

  const t850::Ray ray = CursorRay(mouseX, mouseY);
  int best = -1;
  float bestDistance = FLT_MAX;
  for (uint32_t index = 0; index < m_fluid.BodyCount(); ++index) {
    const t850::FluidSphere& body = m_fluid.Bodies()[index];
    if (!body.enabled || (index == 0 && !SceneProp.Water.sphereEnabled)) continue;
    t850::BoundingSphere sphere;
    sphere.center = XVECTOR3(body.x, body.y, body.z);
    sphere.radius = body.radius;
    float distance = 0.0f;
    if (t850::RayIntersectsSphere(ray, sphere, distance) && distance < bestDistance) {
      bestDistance = distance;
      best = static_cast<int>(index);
    }
  }
  t850::FluidSphere* body = best >= 0 ? m_fluid.Body(static_cast<uint32_t>(best)) : nullptr;
  if (!body) return false;

  const float along = (body->x - ray.origin.x) * ray.direction.x +
                      (body->y - ray.origin.y) * ray.direction.y +
                      (body->z - ray.origin.z) * ray.direction.z;
  m_dragDistance = (std::max)(along, 0.5f);
  m_dragOffset = XVECTOR3(body->x - (ray.origin.x + ray.direction.x * along),
                          body->y - (ray.origin.y + ray.direction.y * along),
                          body->z - (ray.origin.z + ray.direction.z * along));
  m_dragTarget = XVECTOR3(body->x, body->y, body->z);
  body->held = true;
  body->vx = body->vy = body->vz = 0.0f;
  m_dragBody = best;
  T8_LOG_INFO("[Backyard] Holding ball %d (radius %.2f m) at (%.2f, %.2f, %.2f)",
              best, body->radius, body->x, body->y, body->z);
  return true;
}

void BackyardScene::ApplyBallDrag(float dt) {
  t850::FluidSphere* body = m_fluid.Body(static_cast<uint32_t>(m_dragBody));
  if (!body || !body->held || dt <= 0.0f) return;
  const t850::FluidSettings& fs = m_fluid.Settings();
  const float radius = body->radius;
  // The basin walls stop the ball anyway; clamping the target too keeps the steering
  // velocity from pushing into a wall forever.
  const float targetX = (std::clamp)(m_dragTarget.x, fs.basinMinX + radius, (std::max)(fs.basinMaxX - radius, fs.basinMinX + radius));
  const float targetY = (std::clamp)(m_dragTarget.y, fs.basinMinY + radius, fs.waterPlaneY + kMaxDragHeightAboveWater);
  const float targetZ = (std::clamp)(m_dragTarget.z, fs.basinMinZ + radius, (std::max)(fs.basinMaxZ - radius, fs.basinMinZ + radius));
  float vx = (targetX - body->x) / dt;
  float vy = (targetY - body->y) / dt;
  float vz = (targetZ - body->z) / dt;
  const float speed = std::sqrt(vx * vx + vy * vy + vz * vz);
  if (speed > kMaxDragSpeed) {
    const float scale = kMaxDragSpeed / speed;
    vx *= scale;
    vy *= scale;
    vz *= scale;
  }
  body->vx = vx;
  body->vy = vy;
  body->vz = vz;
}

void BackyardScene::EndBallDrag() {
  if (m_dragBody < 0) return;
  if (t850::FluidSphere* body = m_fluid.Body(static_cast<uint32_t>(m_dragBody))) {
    body->held = false;
    T8_LOG_INFO("[Backyard] Released ball %d at (%.2f, %.2f, %.2f), velocity (%.2f, %.2f, %.2f) m/s",
                m_dragBody, body->x, body->y, body->z, body->vx, body->vy, body->vz);
  }
  m_dragBody = -1;
}

void BackyardScene::UpdateExtraBallTransforms() {
  for (size_t index = 0; index < m_extraBallHandles.size(); ++index) {
    const uint32_t bodyIndex = static_cast<uint32_t>(index + 1);   // body 0 is the scripted ball
    if (bodyIndex >= m_fluid.BodyCount()) break;
    t850::PrimitiveInst* inst = m_renderContainer.GetMesh(m_extraBallHandles[index]);
    if (!inst) continue;
    const t850::FluidSphere& body = m_fluid.Bodies()[bodyIndex];
    inst->SetVisible(body.enabled);
    inst->TranslateAbsolute(body.x, body.y, body.z);
    inst->ScaleAbsolute(body.radius, body.radius, body.radius);
    inst->Update();
  }
}

void BackyardScene::ClearSpawnedBalls() {
  EndBallDrag();
  for (t850::RenderInstanceHandle handle : m_extraBallHandles) {
    if (handle.IsValid()) m_renderContainer.RemoveMesh(handle);
  }
  m_extraBallHandles.clear();
  // Body 0 is the scripted ball and must survive.
  m_fluid.RemoveBodiesFrom(1);
  T8_LOG_INFO("[Backyard] Spawned balls cleared; %u body remains", m_fluid.BodyCount());
}

bool BackyardScene::SpawnBall() {
  if (!m_fluidReady) {
    T8_LOG_ERROR("[Backyard] Cannot spawn a ball: the fluid solver is not running on this API");
    return false;
  }
  const t850::WaterTunables& w = SceneProp.Water;
  const t850::FluidSettings& fs = m_fluid.Settings();
  if (m_ballPrimitive < 0) {
    T8_LOG_ERROR("[Backyard] Cannot spawn a ball: the ball primitive is unavailable");
    return false;
  }

  // The basin comes from the solver settings, never from literals, so a pool layout
  // change moves the spawn area with it.
  const float cell = (std::max)(fs.cellSize, 0.001f);
  const float margin = cell;
  // Clamp the radius band so a ball is always resolved by the grid (>= 1.2 cells).
  float radiusMin = (std::max)(w.spawnRadiusMin, 1.2f * cell);
  float radiusMax = (std::max)(w.spawnRadiusMax, radiusMin);
  const float densityMin = (std::max)(w.spawnDensityMin, 0.02f);
  const float densityMax = (std::max)(w.spawnDensityMax, densityMin);
  const float heightMin = (std::max)(w.spawnHeightMin, 0.05f);
  const float heightMax = (std::max)(w.spawnHeightMax, heightMin);

  t850::FluidSphere body = m_fluid.DroppedSphere();   // inherit drag, restitution, damping
  body.enabled = true;
  body.held = false;
  body.vx = body.vy = body.vz = 0.0f;
  body.submerged = 0.0f;

  bool placed = false;
  for (int attempt = 0; attempt < 32 && !placed; ++attempt) {
    const float t = NextSpawnRandom();
    body.radius = radiusMin + (radiusMax - radiusMin) * t;
    body.densityRatio = densityMin + (densityMax - densityMin) * NextSpawnRandom();

    const float loX = fs.basinMinX + body.radius + margin;
    const float hiX = fs.basinMaxX - body.radius - margin;
    const float loZ = fs.basinMinZ + body.radius + margin;
    const float hiZ = fs.basinMaxZ - body.radius - margin;
    if (hiX <= loX || hiZ <= loZ) continue;   // basin too small for this radius
    body.x = loX + (hiX - loX) * NextSpawnRandom();
    body.z = loZ + (hiZ - loZ) * NextSpawnRandom();
    // Straight drop from above the surface, so it always lands inside the walls.
    body.y = fs.waterPlaneY + body.radius + heightMin +
             (heightMax - heightMin) * NextSpawnRandom();

    // Reject if it would start inside another body (the stamp would then overlap and
    // the later index would silently win).
    placed = true;
    for (uint32_t other = 0; other < m_fluid.BodyCount() && placed; ++other) {
      const t850::FluidSphere& existing = m_fluid.Bodies()[other];
      if (!existing.enabled) continue;
      const float dx = body.x - existing.x;
      const float dy = body.y - existing.y;
      const float dz = body.z - existing.z;
      const float minGap = body.radius + existing.radius + margin;
      if (dx * dx + dy * dy + dz * dz < minGap * minGap) placed = false;
    }
  }
  if (!placed) {
    // The air above the pool is crowded: keep the last drawn x, z and drop the ball from
    // above every body it would touch, so a spawn never fails for lack of room.
    float top = body.y;
    for (const t850::FluidSphere& existing : m_fluid.Bodies()) {
      if (!existing.enabled) continue;
      const float dx = body.x - existing.x;
      const float dz = body.z - existing.z;
      const float reach = body.radius + existing.radius + margin;
      if (dx * dx + dz * dz < reach * reach)
        top = (std::max)(top, existing.y + existing.radius + body.radius + margin);
    }
    body.y = top;
    placed = true;
  }

  const int index = m_fluid.AddBody(body);

  // One render instance per body, sharing the ball primitive.
  XMATRIX44 identity;
  XMatIdentity(identity);
  t850::PrimitiveInst instance;
  instance.CreateInstance(PrimitiveMgr.GetPrimitive(static_cast<unsigned int>(m_ballPrimitive)), &identity);
  instance.TranslateAbsolute(body.x, body.y, body.z);
  instance.ScaleAbsolute(body.radius, body.radius, body.radius);
  instance.Update();
  const char* material = AssignBallMaterial(instance);
  const t850::RenderInstanceHandle handle = m_renderContainer.AddMeshInstance(instance);
  if (!handle.IsValid()) {
    T8_LOG_ERROR("[Backyard] Cannot add the render instance for a spawned ball");
    m_fluid.RemoveBodiesFrom(m_fluid.BodyCount() - 1u);
    return false;
  }
  m_extraBallHandles.push_back(handle);

  T8_LOG_INFO("[Backyard] Spawned ball %d: position (%.2f, %.2f, %.2f) radius %.2f m, "
              "density %.2f x water, mass %.2f kg, material %s; %u bodies",
              index, body.x, body.y, body.z, body.radius, body.densityRatio,
              body.densityRatio * w.sphereWaterDensity * (4.0f / 3.0f) * 3.14159265f *
              body.radius * body.radius * body.radius,
              material ? material : "GLB default",
              m_fluid.BodyCount());
  return true;
}

bool BackyardScene::WaterUVMatchesSolver(t850::PrimitiveInst* water) const {
  auto* mesh = water ? dynamic_cast<t850::RenderMesh*>(water->pBase) : nullptr;
  if (!mesh || !mesh->xFile || mesh->xFile->XMeshDataBase.empty() || !mesh->xFile->XMeshDataBase[0])
    return true;
  float zAtMinV = 0.0f, zAtMaxV = 0.0f;
  float minV = 0.0f, maxV = 0.0f;
  bool any = false;
  for (const xF::xMeshGeometry& geom : mesh->xFile->XMeshDataBase[0]->Geometry) {
    const std::vector<XVECTOR2>& uv = geom.TexCoordinates[0];
    if (uv.size() != geom.Positions.size()) continue;
    for (size_t v = 0; v < uv.size(); ++v) {
      if (!any || uv[v].y < minV) { minV = uv[v].y; zAtMinV = geom.Positions[v].z; }
      if (!any || uv[v].y > maxV) { maxV = uv[v].y; zAtMaxV = geom.Positions[v].z; }
      any = true;
    }
  }
  if (!any || zAtMinV <= zAtMaxV) return true;
  T8_LOG_ERROR("[Backyard] Water mesh UV is mirrored in Z (v=%.2f at z=%.2f, v=%.2f at z=%.2f): "
               "waves and the ball's footprint draw on the wrong side of the pool. Regenerate "
               "Backyard_Water.glb with scripts/blender/build_backyard.py",
               minV, zAtMinV, maxV, zAtMaxV);
  return false;
}

void BackyardScene::SetWaterFluidFeature(bool enabled) {  if (!m_waterHandle.IsValid()) return;
  t850::PrimitiveInst* water = m_renderContainer.GetMesh(m_waterHandle);
  if (!water) return;
  const uint64_t bit = t850::ShaderKey::FLUID_SURFACE;
  const uint64_t before = water->PersistentFeatureBits;
  water->PersistentFeatureBits = enabled ? (before | bit) : (before & ~bit);
  if (water->PersistentFeatureBits != before) water->Update();
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

void BackyardScene::ResetSimulation() {
  if (!m_fluidReady || !pFramework || !pFramework->pVideoDriver) return;
  t850::BaseDriver* driver = pFramework->pVideoDriver;
  const t850::WaterTunables& w = SceneProp.Water;

  // W-22: a reset also clears the spawned balls, so a new drop height is retested from
  // the same single-body state the gates assume.
  ClearSpawnedBalls();

  if (!m_fluid.ResetToRest(driver)) {
    T8_LOG_ERROR("[Backyard] Reset failed: the solver could not be cleared");
    return;
  }

  // Re-arm the body at the configured height. Parameters are NOT reset: the whole
  // point is to retest a changed drop height from an identical initial condition.
  t850::FluidSphere& sphere = m_fluid.DroppedSphere();
  sphere.radius = w.sphereRadius;
  sphere.densityRatio = w.sphereDensityRatio;
  sphere.dragCoefficient = w.sphereDragCoefficient;
  sphere.restitution = w.sphereRestitution;
  sphere.heaveDamping = w.sphereHeaveDamping;
  sphere.x = w.sphereDropX;
  sphere.z = w.sphereDropZ;
  sphere.y = w.sphereDropHeight;
  sphere.vx = w.sphereInitialVX;
  sphere.vy = w.sphereInitialVY;
  sphere.vz = w.sphereInitialVZ;
  sphere.submerged = 0.0f;
  sphere.enabled = false;   // re-armed: the release delay applies again

  m_elapsedSeconds = 0.0f;
  m_sphereReleaseDelay = w.sphereReleaseDelay;

  T8_LOG_INFO("[Backyard] Simulation reset: pool at rest, ball armed at y=%.2f r=%.2f density=%.2f (mass %.2f kg)",
              sphere.y, sphere.radius, sphere.densityRatio, w.SphereMassKg());
}

void BackyardScene::OnUpdate(float deltaSeconds) {
  m_deltaSeconds = deltaSeconds;
  if (!m_dumper.SkipCameraUpdates()) {
    m_cameraController.Update(deltaSeconds, t850::CameraUpdateContext{});
  }
  m_lightCamera.Update(0.0f);
  if (s_pickTest && !m_pickTestDone) RunPickTestStep();
  // A grid change needs new 3D textures; everything else can be retuned in place.
  const t850::WaterTunables& w = SceneProp.Water;
  if (m_fluidReady &&
      (std::fabs(w.cellSize - m_appliedCellSize) > 1e-6f ||
       w.gridWidth != m_appliedGridWidth || w.gridHeight != m_appliedGridHeight ||
       w.gridDepth != m_appliedGridDepth)) {
    RebuildFluid();
  }
  if (m_fluidReady && pFramework && pFramework->pVideoDriver) {
    m_fluid.SetPressureIterations(static_cast<uint32_t>((std::max)(w.pressureIterations, 0)));
    m_fluid.SetSurfaceLevel((std::min)((std::max)(w.surfaceLevel, 0.02f), 0.99f));
    m_fluid.SetSOROmega((std::min)((std::max)(w.sorOmega, 1.0f), 1.99f));
    // W-11: the three promoted solver values, pushed live like their neighbours.
    m_fluid.SetMaxSubstepDt((std::max)(w.maxSubstepDt, 1.0f / 1000.0f));
    m_fluid.SetAlphaCompression((std::min)((std::max)(w.alphaCompression, 0.0f), 1.0f));
    m_fluid.SetVolumeCheckInterval(static_cast<uint32_t>((std::max)(w.volumeCheckInterval, 1)));
    m_fluid.SetForcingAmplitude((std::max)(w.forcingAmplitude, 0.0f));
    m_fluid.SetViscosity((std::max)(w.viscosity, 0.0f));
    m_fluid.SetGravity(w.gravity);
    m_fluid.SetDamping((std::max)(w.damping, 0.0f));
    m_fluid.SetWind(w.windX, w.windY, w.windZ);

    // Release the sphere once the configured delay has elapsed.
    m_elapsedSeconds += deltaSeconds;
    {
      t850::FluidSphere& sphere = m_fluid.DroppedSphere();
      if (!sphere.enabled && w.sphereEnabled && m_elapsedSeconds >= m_sphereReleaseDelay) {
        sphere.enabled = true;
        T8_LOG_INFO("[Backyard] Sphere released at y=%.2f radius=%.2f density=%.2f mass=%.2f kg (impact speed %.2f m/s)",
                    sphere.y, sphere.radius, sphere.densityRatio, w.SphereMassKg(),
                    std::sqrt(2.0f * 9.81f * std::max(0.0f, sphere.y)));
      }
      // Keep the visible ball welded to the simulated body. Without this the
      // physics responds but there is nothing on screen to see doing it.
      if (m_ballHandle.IsValid()) {
        if (t850::PrimitiveInst* ball = m_renderContainer.GetMesh(m_ballHandle)) {
          if (w.sphereEnabled) {
            ball->SetVisible(true);
            ball->TranslateAbsolute(sphere.x, sphere.y, sphere.z);
            ball->ScaleAbsolute(sphere.radius, sphere.radius, sphere.radius);
            ball->Update();
          } else {
            ball->SetVisible(false);
          }
        }
      }
      // W-22: the spawned balls follow their own bodies the same way.
      UpdateExtraBallTransforms();
    }

    // Advance by REAL elapsed time. Stepping a fixed 1/60 s once per rendered
    // frame tied the water's speed to the frame rate: at 721 fps the simulation
    // ran 12x faster than real time, which is why it looked like fast-forward.
    // The delta is clamped because the viscous term is integrated explicitly and
    // its von Neumann bound here is h^2/(6*nu) = 0.0675 s.
    float fluidDelta = w.fixedTimeStep ? w.deltaTime : deltaSeconds * w.timeScale;
    fluidDelta = (std::min)((std::max)(fluidDelta, 0.0f), (std::max)(w.maxDeltaTime, 1.0f / 1000.0f));
    if (m_dragBody >= 0) ApplyBallDrag(fluidDelta);
    m_fluid.SetDeltaTime(fluidDelta);
    if (!m_fluid.Step(pFramework->pVideoDriver)) {
      T8_LOG_ERROR("[Backyard] Fluid step failed; disabling the solver for this scene");
      m_fluidReady = false;
    } else {
      // Shading time follows the solver clock so the ripple and foam scroll match
      // the simulated water speed instead of running at frame rate.
      SceneProp.TimeSeconds = m_fluid.SimulationTime();
      // Report the ratio now and then: 1.00 means the water is running in real
      // time. Also tracks total alpha so mass conservation is measurable in the
      // live scene, not just in the test harness.
      static float reportedWall = 0.0f;
      static float reportedSim = 0.0f;
      static int reportFrames = 0;
      static float initialVolume = -1.0f;
      reportedWall += deltaSeconds;
      reportedSim += fluidDelta;
      if (++reportFrames == 600) {
        float volume = 0.0f;
        // The measurement is a full readback stall, so only pay it when the line is logged.
        if (t850::Log::GetMaxLevel() >= t850::Log::LVL_INFO &&
            m_fluid.MeasureVolumeFraction(pFramework->pVideoDriver, volume)) {
          if (initialVolume < 0.0f) initialVolume = volume;
          T8_LOG_INFO("[Backyard][timing] sim/wall ratio = %.2f (%d frames); alpha volume = %.0f cells (%+.3f%% vs start)",
                      reportedWall > 0.0f ? reportedSim / reportedWall : 0.0f,
                      reportFrames, volume,
                      initialVolume > 0.0f ? 100.0f * (volume - initialVolume) / initialVolume : 0.0f);
        } else {
          T8_LOG_INFO("[Backyard][timing] sim/wall ratio = %.2f (%d frames)",
                      reportedWall > 0.0f ? reportedSim / reportedWall : 0.0f, reportFrames);
        }
        reportFrames = 0;
        reportedWall = 0.0f;
        reportedSim = 0.0f;
      }
    }
  }
  m_dumper.UpdateReplayState();
}

void BackyardScene::OnInput(InputManager* input) {
  if (!input) return;
  bool imguiWantsMouse = false;
#ifndef OS_ANDROID
  imguiWantsMouse = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
#endif
  ProcessSceneInput(input, imguiWantsMouse);
}

void BackyardScene::ProcessSceneInput(InputManager* input, bool imguiWantsMouse) {
  // Ctrl frees the cursor (AllowsMouseCapture) and turns a left click into a pick.
  const bool ctrlDown = input->PressedKey(T800K_LCTRL) || input->PressedKey(T800K_RCTRL);
  const bool draggingBall = HandleBallDrag(input, imguiWantsMouse, ctrlDown);
  m_cursorFreed = ctrlDown || draggingBall;
  t850::CameraInputState state;
  state.moveForward = input->PressedKey(T800K_w);
  state.moveBackward = input->PressedKey(T800K_s);
  state.moveLeft = input->PressedKey(T800K_a);
  state.moveRight = input->PressedKey(T800K_d);
  state.jump = input->PressedKey(T800K_SPACE);
  state.sprint = input->PressedKey(T800K_LSHIFT);
  // Look only while a mouse button is held. Stray window-focus deltas would
  // otherwise rotate the camera without user intent. Only the vertical axis is
  // inverted. Ctrl, a held ball and the GUI own the mouse, so none turns the camera.
  state.mouseLook = !ctrlDown && !draggingBall && !imguiWantsMouse &&
      (input->PressedMouseButton(0) || input->PressedMouseButton(2));
  state.mouseDeltaX = (std::clamp)(static_cast<float>(input->xDelta), -200.0f, 200.0f);
  state.mouseDeltaY = (std::clamp)(static_cast<float>(input->yDelta), -200.0f, 200.0f);
  state.scrollDelta = (draggingBall || imguiWantsMouse) ? 0.0f : input->scrollDelta;
  t850::ApplyGamepadToCameraInput(state, *input, m_deltaSeconds, true);
  m_cameraController.HandleInput(state);
}

void BackyardScene::RunPickTestStep() {
  constexpr int kSettleFrames = 600;   // let the spawned balls land before picking
  constexpr int kDragFrames = 300;
  constexpr int kHoldFrames = 300;
  constexpr int kRegrabFrames = 150;
  constexpr int kGrab = kSettleFrames + 2;
  constexpr int kDragStart = kGrab + 1;
  constexpr int kHoldEnd = kDragStart + kDragFrames + kHoldFrames;
  constexpr int kRegrab = kHoldEnd + 1;
  constexpr int kEnd = kRegrab + 1 + kRegrabFrames;
  const int frame = m_pickTestFrame++;
  InputManager& in = m_pickTestInput;

  auto check = [&](bool ok, const char* what) {
    if (ok) {
      T8_LOG_INFO("[BackyardPickTest] ok: %s", what);
    } else {
      ++m_pickTestFailures;
      T8_LOG_ERROR("[BackyardPickTest] FAIL: %s", what);
    }
    return ok;
  };
  auto finish = [&]() {
    m_pickTestDone = true;
    T8_LOG_INFO("[BackyardPickTest] %s (%d failure(s))", m_pickTestFailures ? "FAIL" : "PASS",
                m_pickTestFailures);
    m_dumper.RequestDump();
  };
  auto setCursor = [&](float x, float y) {
    in.mouseX = static_cast<int>(std::lround(x));
    in.mouseY = static_cast<int>(std::lround(y));
  };
  auto setButton = [&](bool down) {
    in.MouseButtonStates[0][0] = down;
    if (!down) in.MouseButtonStates[1][0] = false;
  };
  auto position = [&](int index) {
    const t850::FluidSphere& body = m_fluid.Bodies()[static_cast<uint32_t>(index)];
    return XVECTOR3(body.x, body.y, body.z);
  };
  // Perpendicular distance from a body's centre to the ray under the synthetic cursor.
  auto offRay = [&](int index) {
    const t850::Ray ray = CursorRay(static_cast<float>(in.mouseX), static_cast<float>(in.mouseY));
    const XVECTOR3 p = position(index);
    const float dx = p.x - ray.origin.x, dy = p.y - ray.origin.y, dz = p.z - ray.origin.z;
    const float along = dx * ray.direction.x + dy * ray.direction.y + dz * ray.direction.z;
    const float px = dx - along * ray.direction.x, py = dy - along * ray.direction.y,
                pz = dz - along * ray.direction.z;
    return std::sqrt(px * px + py * py + pz * pz);
  };

  if (!m_fluidReady) {
    T8_LOG_INFO("[BackyardPickTest] SKIP: no fluid solver (and so no balls) on this API");
    m_pickTestDone = true;
    m_dumper.RequestDump();
    return;
  }

  if (frame == kSettleFrames) {
    // Target: the enabled ball nearest the eye with its centre on screen, so no other
    // ball can be in front of it. Victim: the ball nearest the target.
    const XVECTOR3 eye(m_camera.Eye.x, m_camera.Eye.y, m_camera.Eye.z);
    float best = FLT_MAX;
    for (uint32_t index = 0; index < m_fluid.BodyCount(); ++index) {
      const t850::FluidSphere& body = m_fluid.Bodies()[index];
      float sx = 0.0f, sy = 0.0f;
      if (!body.enabled || (index == 0 && !SceneProp.Water.sphereEnabled) ||
          !ProjectToScreen(XVECTOR3(body.x, body.y, body.z), sx, sy)) continue;
      const float d = (body.x - eye.x) * (body.x - eye.x) + (body.y - eye.y) * (body.y - eye.y) +
                      (body.z - eye.z) * (body.z - eye.z);
      if (d < best) { best = d; m_pickTestTarget = static_cast<int>(index); }
    }
    best = FLT_MAX;
    for (uint32_t index = 0; m_pickTestTarget >= 0 && index < m_fluid.BodyCount(); ++index) {
      const t850::FluidSphere& body = m_fluid.Bodies()[index];
      if (static_cast<int>(index) == m_pickTestTarget || !body.enabled ||
          (index == 0 && !SceneProp.Water.sphereEnabled)) continue;
      const XVECTOR3 t = position(m_pickTestTarget);
      const float d = (body.x - t.x) * (body.x - t.x) + (body.y - t.y) * (body.y - t.y) +
                      (body.z - t.z) * (body.z - t.z);
      if (d < best) { best = d; m_pickTestVictim = static_cast<int>(index); }
    }
    if (!check(m_pickTestTarget >= 0 && m_pickTestVictim >= 0,
               "two balls are in view (run with --backyard-spawn 2 or more)")) {
      finish();
      return;
    }
    ProjectToScreen(position(m_pickTestTarget), m_pickTestFrom[0], m_pickTestFrom[1]);
    T8_LOG_INFO("[BackyardPickTest] target ball %d at pixel (%.0f, %.0f), victim ball %d",
                m_pickTestTarget, m_pickTestFrom[0], m_pickTestFrom[1], m_pickTestVictim);
    setCursor(m_pickTestFrom[0], m_pickTestFrom[1]);
    setButton(true);
    ProcessSceneInput(&in, false);
    check(m_dragBody < 0, "a left click without Ctrl does not pick a ball");
    return;
  }
  if (frame == kSettleFrames + 1) {
    setButton(false);
    ProcessSceneInput(&in, false);
    return;
  }
  if (frame == kGrab || frame == kRegrab) {
    float x = 0.0f, y = 0.0f;
    ProjectToScreen(position(m_pickTestTarget), x, y);
    setCursor(x, y);
    in.KeyStates[0][T800K_LCTRL] = true;
    setButton(true);
    ProcessSceneInput(&in, false);
    if (!check(m_dragBody == m_pickTestTarget, "Ctrl + left click picks the ball under the cursor")) {
      finish();
      return;
    }
    check(!AllowsMouseCapture(), "the cursor is free (no mouse capture) while Ctrl is held");
    m_pickTestFrom[0] = x;
    m_pickTestFrom[1] = y;
    if (frame == kGrab) {
      m_pickTestVictimStart = position(m_pickTestVictim);
      ProjectToScreen(m_pickTestVictimStart, m_pickTestTo[0], m_pickTestTo[1]);
    } else {
      m_pickTestTo[0] = x;
      m_pickTestTo[1] = y - 80.0f;   // lift it in view for the frame dump
    }
    return;
  }
  if ((frame >= kDragStart && frame < kHoldEnd) || (frame > kRegrab && frame < kEnd)) {
    const bool regrab = frame > kRegrab;
    const int moveFrames = regrab ? kRegrabFrames : kDragFrames;
    const float t = (std::min)(static_cast<float>(frame - (regrab ? kRegrab + 1 : kDragStart)) /
                               static_cast<float>(moveFrames), 1.0f);
    in.KeyStates[0][T800K_LCTRL] = false;
    in.scrollDelta = 0.0f;
    if (frame == kDragStart) {
      // The real wheel path moves the ball to the victim's depth along the cursor ray.
      const XVECTOR3 v = m_pickTestVictimStart;
      const float eyeToVictim = std::sqrt((v.x - m_camera.Eye.x) * (v.x - m_camera.Eye.x) +
                                          (v.y - m_camera.Eye.y) * (v.y - m_camera.Eye.y) +
                                          (v.z - m_camera.Eye.z) * (v.z - m_camera.Eye.z));
      in.scrollDelta = (eyeToVictim / (std::max)(m_dragDistance, 0.5f) - 1.0f) / 0.1f;
    }
    setCursor(m_pickTestFrom[0] + (m_pickTestTo[0] - m_pickTestFrom[0]) * t,
              m_pickTestFrom[1] + (m_pickTestTo[1] - m_pickTestFrom[1]) * t);
    ProcessSceneInput(&in, false);
    if (frame == kDragStart)
      check(!AllowsMouseCapture(), "the cursor stays free while a ball is held after Ctrl is released");
    if (m_dragBody != m_pickTestTarget) {
      check(false, "the ball stays held while the button is down");
      finish();
      return;
    }
    if (regrab && frame == kEnd - 1) {
      const t850::FluidSphere& held = m_fluid.Bodies()[static_cast<uint32_t>(m_pickTestTarget)];
      check(offRay(m_pickTestTarget) < held.radius, "the held ball sits under the cursor");
      T8_LOG_INFO("[BackyardPickTest] dump frame shows ball %d held at pixel (%d, %d), radius %.2f m",
                  m_pickTestTarget, in.mouseX, in.mouseY, held.radius);
      finish();
    }
    return;
  }
  if (frame == kHoldEnd) {
    const t850::FluidSphere& held = m_fluid.Bodies()[static_cast<uint32_t>(m_pickTestTarget)];
    const XVECTOR3 heldPos = position(m_pickTestTarget);
    const XVECTOR3 victimPos = position(m_pickTestVictim);
    const float victimRadius = m_fluid.Bodies()[static_cast<uint32_t>(m_pickTestVictim)].radius;
    const float pushed = std::sqrt((victimPos.x - m_pickTestVictimStart.x) * (victimPos.x - m_pickTestVictimStart.x) +
                                   (victimPos.y - m_pickTestVictimStart.y) * (victimPos.y - m_pickTestVictimStart.y) +
                                   (victimPos.z - m_pickTestVictimStart.z) * (victimPos.z - m_pickTestVictimStart.z));
    const float separation = std::sqrt((victimPos.x - heldPos.x) * (victimPos.x - heldPos.x) +
                                       (victimPos.y - heldPos.y) * (victimPos.y - heldPos.y) +
                                       (victimPos.z - heldPos.z) * (victimPos.z - heldPos.z));
    const float contact = held.radius + victimRadius;
    T8_LOG_INFO("[BackyardPickTest] after the drag: held ball %.3f m off the cursor ray, victim pushed "
                "%.3f m, separation %.3f m (contact %.3f m)",
                offRay(m_pickTestTarget), pushed, separation, contact);
    check(offRay(m_pickTestTarget) < held.radius, "the dragged ball follows the cursor");
    check(pushed > 0.5f * contact, "the dragged ball pushes the ball it is moved into");
    check(separation > 0.9f * contact, "the two balls do not interpenetrate");
    setButton(false);
    ProcessSceneInput(&in, false);
    check(m_dragBody < 0 && !held.held, "releasing the button drops the ball");
    check(AllowsMouseCapture() == SceneProp.MouseCaptureAllowed,
          "the scene's mouse capture policy returns after the release");
  }
}

void BackyardScene::DrawFluidDebug() {
  if (!m_fluidReady || !m_fluidDebugView.Any()) return;
  t850::BaseDriver* driver = pFramework->pVideoDriver;
  if (!m_fluidDebug.IsReady() && !m_fluidDebug.Create()) return;
  t850::PrimitiveInst* water = m_renderContainer.GetMesh(m_waterHandle);
  if (m_fluidDebugView.meshWireframe && !m_fluidDebugMeshBound && water) {
    if (auto* mesh = dynamic_cast<t850::RenderMesh*>(water->pBase))
      m_fluidDebugMeshBound = m_fluidDebug.SetWaterMesh(*mesh);
  }
  const t850::WaterTunables& w = SceneProp.Water;
  m_fluidDebugView.displacementScale = w.displacementScale;
  m_fluidDebugView.rippleOctaves = w.fakeRipplesEnabled;
  m_fluidDebugView.rippleScale = w.vertexWaveScale;
  m_fluidDebugView.rippleTime = SceneProp.TimeSeconds;
  // GBuffer depth holds the opaque scene only (the water is kept out of it), so voxels
  // under the surface stay visible while the house, coping and ball still occlude.
  t850::Texture* depth = nullptr;
  const int gbuffer = m_renderContainer.Graph().GetRTHandle("GBuffer");
  if (gbuffer >= 0 && gbuffer < static_cast<int>(driver->RTs.size()) && driver->RTs[gbuffer])
    depth = driver->RTs[gbuffer]->pDepthTexture;
  XMATRIX44 meshWorld;
  meshWorld.Identity();
  if (water) meshWorld = water->Final;
  m_fluidDebug.Draw(driver, m_fluid, m_fluidDebugView, meshWorld, m_camera.VP, m_camera.FPlane,
                    depth, driver->width, driver->height);
}

void BackyardScene::OnDraw() {
  if (!m_assetsCreated) return;
  if (t850::PrimitiveInst* water = m_renderContainer.GetMesh(m_waterHandle))
    water->SetVisible(!m_hideWater);
  m_renderContainer.Execute(pFramework->pVideoDriver, m_deltaSeconds);
#ifdef OS_ANDROID
  if (m_fluidDebugView.Any())
    pFramework->pVideoDriver->SetPrePresentOverlayCallback([this]() { DrawFluidDebug(); });
#else
  DrawFluidDebug();
#endif

  // The probe's readbacks stall the GPU, so skip them when nothing would be logged.
  if (m_fluidReady && t850::Log::GetMaxLevel() >= t850::Log::LVL_INFO) {
    static int probeFrame = 0;
    if (++probeFrame % 60 == 0) {
      t850::FluidSimulation::Diagnostics diagnostics;
      if (m_fluid.ReadDiagnostics(pFramework->pVideoDriver, diagnostics))
        T8_LOG_INFO("[Backyard][fluid] samples=%u meanSpeed=%.4f maxSpeed=%.4f nonFinite=%u",
                    diagnostics.samples, diagnostics.meanSpeed, diagnostics.maxSpeed, diagnostics.nonFinite);
      const t850::FluidSphere& s = m_fluid.DroppedSphere();
      T8_LOG_INFO("[Backyard][sphere] y=%.3f vy=%.3f submerged=%.2f enabled=%d t=%.2f",
                  s.y, s.vy, s.submerged, s.enabled ? 1 : 0, m_fluid.SimulationTime());
      // Rendered surface elevation relative to the authored water plane, read from the
      // texture the vertex shader displaces with. The profile is the only way to tell a
      // dimple that tracks the body apart from waves that propagate: a rendered frame
      // cannot show the difference.
      std::vector<float> surface;
      if (m_fluid.ReadSurface(pFramework->pVideoDriver, surface)) {
        const t850::FluidSettings& fs = m_fluid.Settings();
        const uint32_t w = fs.width, dep = fs.depth;
        auto delta = [&](uint32_t x, uint32_t z) {
          return surface[(x + static_cast<size_t>(w) * z) * 4];
        };
        float minH = 1e9f, maxH = -1e9f, sum = 0.0f, sumSq = 0.0f;
        for (uint32_t z = 0; z < dep; ++z) {
          for (uint32_t x = 0; x < w; ++x) {
            const float d = delta(x, z);
            minH = (std::min)(minH, d);
            maxH = (std::max)(maxH, d);
            sum += d;
            sumSq += d * d;
          }
        }
        const float count = static_cast<float>(w * dep);
        const float mean = sum / count;
        T8_LOG_INFO("[Backyard][surface] delta vs water plane (m): min=%.4f max=%.4f mean=%.4f rms=%.4f",
                    minH, maxH, mean, std::sqrt((std::max)(sumSq / count - mean * mean, 0.0f)));
        const float rowF = (s.z - fs.basinMinZ) / fs.cellSize;
        const uint32_t row = static_cast<uint32_t>((std::min)((std::max)(rowF, 0.0f), static_cast<float>(dep - 1)));
        char line[32];
        std::string profile;
        for (uint32_t x = 0; x < w; x += 4) {
          std::snprintf(line, sizeof(line), " %+.0f", delta(x, row) * 100.0f);
          profile += line;
        }
        T8_LOG_INFO("[Backyard][columns] surface delta (cm) along X, z=%u:%s", row, profile.c_str());
      }
    }
  }

  if (m_dumper.ShouldDump(m_deltaSeconds)) {
    std::vector<t850::RTDumpEntry> targets = {
        {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
        {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
        {m_renderContainer.Graph().GetRTHandle("Deferred"), t850::BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
        {m_renderContainer.Graph().GetRTHandle("ExtraHelper"), t850::BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
        // W-10: the forward pass draws the water into Deferred's own depth buffer, so
        // this holds exactly the visible water surface (opaque depth is GBuffer:DEPTH).
        // CoC takes the nearer of the two. With DOF on, the DOF pass re-pushes Deferred
        // and clears it before this end-of-frame dump, so dump it with DOF off.
        {m_renderContainer.Graph().GetRTHandle("Deferred"), t850::BaseDriver::DEPTH_ATTACHMENT, "WaterDepth"}};
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
    // Cells are cubes and the pool size is fixed, so one cell size decides width, depth and
    // height together. The three controls are linked views of it; the grid rebuilds on release.
    const t850::FluidSimulation::LayoutExtent& pool = m_fluid.PoolExtent();
    if (m_fluidReady && pool.sizeX > 0.0f && pool.sizeZ > 0.0f) {
      constexpr float kMinCell = 0.03f;
      constexpr float kMaxCell = 0.30f;
      if (m_gridEditCell <= 0.0f) m_gridEditCell = w.cellSize;
      using Grid = t850::FluidSimulation::DerivedGrid;
      Grid preview = t850::FluidSimulation::DeriveGrid(pool, m_gridEditCell);
      int gridW = static_cast<int>(preview.width);
      int gridD = static_cast<int>(preview.depth);
      bool commit = false;
      if (sliderInt("water_grid_w", "grid width (X cells)", gridW,
                    std::ceil(pool.sizeX / kMaxCell), std::floor(pool.sizeX / kMinCell), 1.0f))
        m_gridEditCell = pool.sizeX / static_cast<float>(gridW);
      commit |= ImGui::IsItemDeactivatedAfterEdit();
      if (sliderInt("water_grid_d", "grid depth (Z cells)", gridD,
                    std::ceil(pool.sizeZ / kMaxCell), std::floor(pool.sizeZ / kMinCell), 1.0f))
        m_gridEditCell = pool.sizeZ / static_cast<float>(gridD);
      commit |= ImGui::IsItemDeactivatedAfterEdit();
      slider("water_cell", "cell size (m)", m_gridEditCell, kMinCell, kMaxCell, 0.001f);
      commit |= ImGui::IsItemDeactivatedAfterEdit();
      m_gridEditCell = (std::min)((std::max)(m_gridEditCell, kMinCell), kMaxCell);
      preview = t850::FluidSimulation::DeriveGrid(pool, m_gridEditCell);

      char line[192];
      const t850::FluidSettings& applied = m_fluid.Settings();
      std::snprintf(line, sizeof(line), "grid height (Y cells) %u: pool floor to %.2f m above the water",
                    preview.height, pool.domainHeight - (applied.waterPlaneY - applied.basinMinY));
      gui.Text(line);
      const double cells = static_cast<double>(preview.width) * preview.height * preview.depth;
      const double running = static_cast<double>(m_appliedGridWidth) * m_appliedGridHeight * m_appliedGridDepth;
      // Work per step scales with the cell count; the CFL limit adds substeps as cells shrink.
      const double cost = running > 0.0 && preview.cellSize > 0.0f
          ? cells / running * (m_appliedCellSize / preview.cellSize) : 1.0;
      std::snprintf(line, sizeof(line), "%u x %u x %u = %.0f cells @ %.4f m  (~%.1fx solver cost)%s",
                    preview.width, preview.height, preview.depth, cells, preview.cellSize, cost,
                    std::fabs(preview.cellSize - m_appliedCellSize) > 1e-6f ? "  - release to rebuild" : "");
      gui.Text(line);
      if (commit) w.cellSize = preview.cellSize;
    } else {
      gui.Text("Grid controls need the running solver (D3D11, D3D12 or Vulkan).");
    }
    sliderInt("water_iterations", "pressure iterations", w.pressureIterations, 1.0f, 200.0f, 1.0f);
    slider("water_sor_omega", "SOR omega (1 = Gauss-Seidel)", w.sorOmega, 1.0f, 1.99f, 0.001f);
    slider("water_alpha_compression", "alpha compression (0 = pure upwind)", w.alphaCompression, 0.0f, 1.0f, 0.01f);
    slider("water_max_substep_dt", "max substep dt (s)", w.maxSubstepDt, 1.0f / 400.0f, 1.0f / 30.0f, 0.0005f);
    sliderInt("water_volume_check_interval", "volume check interval (substeps)", w.volumeCheckInterval, 1, 120, 1);
    slider("water_dt", "fixed step (s, only when fixed)", w.deltaTime, 1.0f / 240.0f, 1.0f / 15.0f, 0.0001f);
    slider("water_max_dt", "max delta (s)", w.maxDeltaTime, 1.0f / 240.0f, 1.0f / 10.0f, 0.0001f);
    slider("water_time_scale", "time scale (1 = real time)", w.timeScale, 0.05f, 3.0f, 0.01f);
    gui.Checkbox(t850::CheckboxDesc{ "water_fixed_dt", "fixed time step", false, true },
                 w.fixedTimeStep);
    slider("water_viscosity", "viscosity (m^2/s)", w.viscosity, 0.0f, 0.01f, 0.000001f);
    slider("water_gravity", "gravity", w.gravity, 0.0f, 30.0f, 0.01f);
    slider("water_wind_x", "wind X", w.windX, -5.0f, 5.0f, 0.01f);
    slider("water_wind_y", "wind Y", w.windY, -5.0f, 5.0f, 0.01f);
    slider("water_wind_z", "wind Z", w.windZ, -5.0f, 5.0f, 0.01f);
    slider("water_damping", "damping", w.damping, 0.0f, 4.0f, 0.01f);
    slider("water_surface_level", "surface level", w.surfaceLevel, 0.05f, 0.99f, 0.001f);
    gui.Separator();
    if (gui.Button("Reseed solver"))
      ResetSimulation();
    if (gui.Button("Rebuild grid"))
      RebuildFluid();
  }

  if (gui.BeginSection("Debug view")) {
    t850::FluidDebugView& v = m_fluidDebugView;
    auto check = [&gui](const char* name, const char* label, bool& value) {
      return gui.Checkbox(t850::CheckboxDesc{ name, label, false, true }, value);
    };
    check("water_dbg_mesh", "water mesh wireframe (white)", v.meshWireframe);
    check("water_dbg_grid", "solver surface, 1 vertex per column (yellow)", v.surfaceGrid);
    check("water_dbg_liquid", "liquid voxels at the surface, alpha >= 0.5 (blue)", v.liquidVoxels);
    check("water_dbg_partial", "partially filled cells, height = alpha (teal)", v.partialCells);
    check("water_dbg_solid", "solid voxels: steps grey, body orange", v.solidVoxels);
    check("water_dbg_velocity", "velocity on a vertical slice (magenta)", v.velocitySlice);
    check("water_dbg_velocity_volume",
          "velocity in the whole water volume (cyan < 5 cm/s, yellow < 25 cm/s, red faster)", v.velocityVolume);
    if (v.velocitySlice || v.velocityVolume)
      slider("water_dbg_velocity_scale", "velocity line length (m per m/s)", v.velocityScale, 0.01f, 2.0f, 0.01f);
    if (v.velocitySlice) {
      const float maxRow = m_fluidReady ? static_cast<float>(m_fluid.Settings().depth) - 1.0f : 191.0f;
      sliderInt("water_dbg_slice", "slice Z row (-1 follows the ball)", v.velocitySliceZ, -1.0f, maxRow, 1.0f);
    }
    if (v.velocityVolume) {
      sliderInt("water_dbg_volume_step", "one arrow every N cells (0 = auto)", v.volumeSampling, 0.0f, 8.0f, 1.0f);
      slider("water_dbg_min_speed", "hide cells slower than (m/s)", v.minSpeed, 0.0f, 0.2f, 0.001f);
    }
    check("water_dbg_bounds", "solver grid bounds (green)", v.domainBounds);
    check("water_dbg_xray", "x-ray (ignore scene depth)", v.xray);
    check("water_dbg_hide", "hide shaded water", m_hideWater);
    if (m_fluidReady) {
      const t850::FluidSettings& fs = m_fluid.Settings();
      const t850::FluidDebugStats& st = m_fluidDebug.Stats();
      char line[192];
      std::snprintf(line, sizeof(line), "grid %u x %u x %u = %u voxels, cell %.3f m",
                    fs.width, fs.height, fs.depth, fs.width * fs.height * fs.depth, fs.cellSize);
      gui.Text(line);
      if (v.Any()) {
        std::snprintf(line, sizeof(line), "mesh lines %u | liquid %u/%u | partial %u/%u | solid %u/%u | body %u/%u (drawn/total)",
                      st.meshLines, st.liquidDrawn, st.liquidVoxels, st.partialDrawn, st.partialCells,
                      st.solidDrawn, st.solidVoxels, st.bodyDrawn, st.bodyVoxels);
        gui.Text(line);
        if (st.bulkCells > 0) {
          // Water away from the surface should stay at alpha 1; lower means it is being diluted.
          std::snprintf(line, sizeof(line), "bulk water alpha: mean %.3f, min %.3f, %u of %u cells below 0.98",
                        st.bulkMeanAlpha, st.bulkMinAlpha, st.bulkBelow98, st.bulkCells);
          gui.Text(line);
        }
        if (v.velocitySlice) {
          std::snprintf(line, sizeof(line), "velocity lines %u on slice z=%d", st.velocityLines, st.velocitySliceZ);
          gui.Text(line);
        }
        if (v.velocityVolume) {
          std::snprintf(line, sizeof(line), "volume: %u arrows (every %u cells) of %u moving / %u liquid cells",
                        st.volumeArrows, st.volumeStep, st.volumeMoving, st.volumeLiquidCells);
          gui.Text(line);
          std::snprintf(line, sizeof(line), "fastest liquid cell %.3f m/s at (%d, %d, %d), alpha %.2f",
                        st.volumeMaxSpeed, st.volumeMaxCell[0], st.volumeMaxCell[1], st.volumeMaxCell[2],
                        st.volumeMaxAlpha);
          gui.Text(line);
        }
        gui.Text("Layers are read back from the GPU every frame; expect a lower frame rate.");
      }
    }
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

  if (gui.BeginSection("Dropped sphere")) {
    gui.Checkbox(t850::CheckboxDesc{ "water_sphere_on", "enabled", true, true },
                 w.sphereEnabled);
    slider("water_sphere_radius", "radius (m)", w.sphereRadius, 0.02f, 0.60f, 0.005f);
    // Density is the physical parameter: equilibrium submerged fraction equals it,
    // so it alone decides whether the body floats and how deep it sits.
    slider("water_sphere_density", "density (x water)", w.sphereDensityRatio, 0.05f, 3.0f, 0.01f);

    // Mass is DERIVED from radius and density, and shown always. If the override is
    // enabled the mass slider drives the density instead, so editing either one
    // updates the other instead of the two fighting each frame.
    char massText[128];
    std::snprintf(massText, sizeof(massText), "mass %.2f kg  (density %.0f kg/m3)",
                  w.SphereMassKg(), w.sphereDensityRatio * w.sphereWaterDensity);
    gui.Text(massText);
    gui.Checkbox(t850::CheckboxDesc{ "water_sphere_mass_override", "edit by mass", false, true },
                 w.sphereMassOverride);
    if (w.sphereMassOverride) {
      float massEdit = w.SphereMassKg();
      if (slider("water_sphere_mass", "mass (kg)", massEdit, 0.01f, 500.0f, 0.01f))
        w.sphereDensityRatio = w.SphereDensityFromMass(massEdit);
    }
    slider("water_sphere_cd", "drag coefficient", w.sphereDragCoefficient, 0.05f, 1.5f, 0.01f);
    slider("water_sphere_restitution", "bounce (0 = dead stop)", w.sphereRestitution, 0.0f, 0.9f, 0.01f);
    slider("water_sphere_heave_damping", "heave damping ratio", w.sphereHeaveDamping, 0.0f, 1.0f, 0.01f);

    slider("water_sphere_height", "drop height (m)", w.sphereDropHeight, 0.05f, 3.0f, 0.01f);
    slider("water_sphere_x", "drop X", w.sphereDropX, -4.0f, 4.0f, 0.05f);
    slider("water_sphere_z", "drop Z", w.sphereDropZ, -8.5f, -5.0f, 0.05f);
    slider("water_sphere_vy", "initial VY", w.sphereInitialVY, -5.0f, 5.0f, 0.05f);
    slider("water_sphere_vx", "initial VX", w.sphereInitialVX, -5.0f, 5.0f, 0.05f);
    slider("water_sphere_vz", "initial VZ", w.sphereInitialVZ, -5.0f, 5.0f, 0.05f);
    slider("water_sphere_delay", "release delay (s)", w.sphereReleaseDelay, 0.0f, 20.0f, 0.1f);
    if (gui.Button("Drop now")) {
      t850::FluidSphere& sphere = m_fluid.DroppedSphere();
      sphere.radius = w.sphereRadius;
      sphere.densityRatio = w.sphereDensityRatio;
      sphere.dragCoefficient = w.sphereDragCoefficient;
      sphere.restitution = w.sphereRestitution;
      sphere.heaveDamping = w.sphereHeaveDamping;
      sphere.x = w.sphereDropX;
      sphere.z = w.sphereDropZ;
      sphere.y = w.sphereDropHeight;
      sphere.vx = w.sphereInitialVX;
      sphere.vy = w.sphereInitialVY;
      sphere.vz = w.sphereInitialVZ;
      sphere.submerged = 0.0f;
      sphere.enabled = true;
    }
    // Reset the pool AND re-arm the body, so a new drop height can be tested from a
    // comparable initial state. Clearing the solver's momentum is the part that
    // matters: without it the next drop lands in water the previous one set moving.
    if (gui.Button("Reset simulation")) {
      ResetSimulation();
    }

    // W-22: spawn extra bodies, any number. Disabled (with the reason shown) when the
    // solver is not running - GL and WebGPU have no volumetric compute.
    gui.Text("spawned balls");
    char spawnText[64];
    std::snprintf(spawnText, sizeof(spawnText), "balls: %u", m_fluid.BodyCount() - 1u);
    gui.Text(spawnText);
    if (!m_fluidReady)
      gui.Text("spawning needs the fluid solver (unavailable on this API)");
    if (gui.Button("Spawn ball") && m_fluidReady) {
      SpawnBall();
    }
    if (gui.Button("Spawn 10 balls") && m_fluidReady) {
      for (int spawn = 0; spawn < 10; ++spawn) SpawnBall();
    }
    if (gui.Button("Clear spawned balls")) {
      ClearSpawnedBalls();
    }
    gui.Text("Ctrl frees the mouse: Ctrl + left-drag a ball to move it; the wheel moves it");
    gui.Text("nearer/farther; release the button to throw it");
    slider("water_spawn_radius_min", "spawn radius min (m)", w.spawnRadiusMin, 0.05f, 0.6f, 0.005f);
    slider("water_spawn_radius_max", "spawn radius max (m)", w.spawnRadiusMax, 0.05f, 0.6f, 0.005f);
    slider("water_spawn_density_min", "spawn density min (x water)", w.spawnDensityMin, 0.05f, 3.0f, 0.01f);
    slider("water_spawn_density_max", "spawn density max (x water)", w.spawnDensityMax, 0.05f, 3.0f, 0.01f);
    slider("water_spawn_height_min", "spawn height min (m)", w.spawnHeightMin, 0.1f, 3.0f, 0.01f);
    slider("water_spawn_height_max", "spawn height max (m)", w.spawnHeightMax, 0.1f, 3.0f, 0.01f);
    sliderInt("water_spawn_seed", "spawn seed (0 = random)", w.spawnSeed, 0, 100000, 1);
  }

  if (gui.BeginSection("Presentation")) {
    slider("water_disp_scale", "displacement scale", w.displacementScale, 1.0f, 60.0f, 0.1f);
    slider("water_normal_strength", "normal strength", w.normalStrength, 0.0f, 4.0f, 0.01f);
    slider("water_forcing", "forcing amplitude", w.forcingAmplitude, 0.0f, 20.0f, 0.1f);
    gui.Checkbox(t850::CheckboxDesc{ "water_fake_ripples", "procedural ripples", true, true },
                 w.fakeRipplesEnabled);
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
