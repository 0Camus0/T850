#include <pch.h>

#include <Backyard8Scene.h>

#include <core/Config.h>
#include <core/EngineContext.h>
#include <imgui/DevGuiContext.h>
#include <scene/ParticleSystem.h>
#include <scene/PrimitiveManager.h>
#include <scene/SceneDescriptor.h>
#include <utils/Log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>

// Backyard8 = Backyard 7 with a particle fluid.
//
// The parent owns the whole scene: asset loading, cameras, the render container, the ball
// bodies, picking and wind. This file only replaces the fluid lifecycle and the Dev GUI:
//
//   OnLoadScene     parent loads the scene WITHOUT a fluid, then we create PbfFluid.
//   OnUpdate        parent's per-frame work runs, then PBF steps and publishes the surface.
//   OnDestoryScene  both solvers shut down.
//   DrawDevGui      the VOF panel is replaced by the particle-fluid panel. The old sliders
//                   tuned a heightfield Navier-Stokes solve that this scene does not run.
//
// Every tunable lives in Scenes/Backyard8Fluid.json (Backyard8FluidJson.h). Command-line
// flags override the file; the panel edits the live values and writes them back into the
// config so a re-seed uses exactly what is on screen.

int Backyard8Scene::s_startMode = -1;             // -1 = config file
int Backyard8Scene::s_solverIterations = -1;
int Backyard8Scene::s_substeps = -1;
double Backyard8Scene::s_particleRadius = -1.0;
int Backyard8Scene::s_diagFrames = 0;
double Backyard8Scene::s_artK = -1.0;
double Backyard8Scene::s_artN = -1.0;
double Backyard8Scene::s_viscosity = -1.0;
double Backyard8Scene::s_maxVelocity = -1.0;
double Backyard8Scene::s_gravity = -1.0;
double Backyard8Scene::s_windSpeed = -1.0;
double Backyard8Scene::s_windHeading = 0.0;
int Backyard8Scene::s_renderMode = -1;
const char* Backyard8Scene::s_configPath = "Scenes/Backyard8Fluid.json";

void Backyard8Scene::SetWind(double speed, double headingDeg) {
  s_windSpeed = speed;
  s_windHeading = headingDeg;
}

Backyard8Scene::Backyard8Scene()
    : BackyardScene("Scenes/Backyard8Scene.json", "Scenes/Backyard8Scene_RenderGraph.json",
                    "Scenes/BackyardWater.json", "Scenes/BackyardBallMaterials.json") {}

Backyard8Scene::~Backyard8Scene() = default;

bool Backyard8Scene::LoadFluidConfig() {
  if (m_configLoaded) return true;
  std::string error;
  if (!t850::LoadBackyard8FluidConfig(s_configPath, m_config, error) && !error.empty())
    T8_LOG_ERROR("[Backyard8] Fluid config rejected: %s (using built-in defaults)", error.c_str());
  m_configLoaded = true;
  if (s_startMode >= 0) m_config.scene.startMode = s_startMode;
  if (s_renderMode >= 0) m_config.particles.renderMode = s_renderMode;
  // Sanitise the ranges the renderer assumes.
  m_config.solver.maxParticles = (std::max)(m_config.solver.maxParticles, 1);
  m_config.solver.particleRadius = (std::max)(m_config.solver.particleRadius, 0.01f);
  m_config.solver.sphereRadius = (std::max)(m_config.solver.sphereRadius, 0.1f);
  m_config.solver.solverIterations = (std::max)(m_config.solver.solverIterations, 1);
  m_config.solver.substeps = (std::max)(m_config.solver.substeps, 1);
  m_config.solver.surfaceWidth = (std::max)(m_config.solver.surfaceWidth, 4);
  m_config.solver.surfaceDepth = (std::max)(m_config.solver.surfaceDepth, 4);
  m_config.scene.maxStepsPerFrame = (std::max)(m_config.scene.maxStepsPerFrame, 1);
  if (m_config.scene.fixedStep <= 1e-5f) m_config.scene.fixedStep = 1.0f / 60.0f;
  m_config.particles.renderMode = (std::min)((std::max)(m_config.particles.renderMode, 0), 1);
  m_config.particles.shape = (std::min)((std::max)(m_config.particles.shape, 0), 2);
  m_config.particles.blend = (std::min)((std::max)(m_config.particles.blend, 0), 1);
  m_config.particles.style = (std::min)((std::max)(m_config.particles.style, 0), 3);
  m_config.screenSpace.smoothingIterations =
      (std::min)((std::max)(m_config.screenSpace.smoothingIterations, 0), 8);
  m_config.screenSpace.smoothingStrength =
      (std::min)((std::max)(m_config.screenSpace.smoothingStrength, 0.0f), 1.0f);
  return true;
}

void Backyard8Scene::BuildSettings(t850::PbfSettings& out) const {
  const t850::Backyard8SolverConfig& c = m_config.solver;
  out = t850::PbfSettings{};
  out.particleRadius = c.particleRadius;
  out.smoothingRadius = c.particleRadius * (std::max)(c.smoothingRadiusScale, 1.0f);
  out.restDensityScale = c.restDensityScale;
  out.stiffnessEps = c.stiffnessEps;
  out.artPressureK = c.artPressureK;
  out.artPressureN = c.artPressureN;
  out.viscosityC = c.viscosityC;
  out.gravity = c.gravity;
  out.maxVelocity = c.maxVelocity;
  out.solverIterations = static_cast<uint32_t>(c.solverIterations);
  out.substeps = static_cast<uint32_t>(c.substeps);
  out.deltaTime = m_config.scene.fixedStep;
  out.sphereRadius = c.sphereRadius;
  out.sphereHeight = c.sphereHeight;
  out.dropSphere = m_config.scene.startMode == 1;
  out.prefillLevel = out.dropSphere ? c.prefillLevel : c.restingLevel;
  out.cullBelowY = c.cullBelowY;
  out.cullMargin = c.cullMargin;
  out.surfaceWidth = static_cast<uint32_t>(c.surfaceWidth);
  out.surfaceDepth = static_cast<uint32_t>(c.surfaceDepth);
  out.maxParticles = static_cast<uint32_t>(c.maxParticles);
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  out.windX = static_cast<float>(c.windSpeed * std::cos(c.windHeadingDeg * kDegToRad));
  out.windZ = static_cast<float>(c.windSpeed * std::sin(c.windHeadingDeg * kDegToRad));
  out.windStrength = (std::max)(c.windStrength, 0.0f);
  out.wallFriction = (std::max)(c.wallFriction, 0.0f);

  // Command-line overrides. A negative sentinel means "not supplied", so the file wins.
  if (s_solverIterations > 0) out.solverIterations = static_cast<uint32_t>(s_solverIterations);
  if (s_substeps > 0) out.substeps = static_cast<uint32_t>(s_substeps);
  if (s_particleRadius >= 0.0) {
    out.particleRadius = static_cast<float>(s_particleRadius);
    out.smoothingRadius = out.particleRadius * (std::max)(c.smoothingRadiusScale, 1.0f);
  }
  if (s_artK >= 0.0) out.artPressureK = static_cast<float>(s_artK);
  if (s_artN >= 0.0) out.artPressureN = static_cast<float>(s_artN);
  if (s_viscosity >= 0.0) out.viscosityC = static_cast<float>(s_viscosity);
  if (s_maxVelocity >= 0.0) out.maxVelocity = static_cast<float>(s_maxVelocity);
  if (s_gravity >= 0.0) out.gravity = static_cast<float>(s_gravity);
  if (s_windSpeed >= 0.0) {
    out.windX = static_cast<float>(s_windSpeed * std::cos(s_windHeading * kDegToRad));
    out.windZ = static_cast<float>(s_windSpeed * std::sin(s_windHeading * kDegToRad));
  }
}

void Backyard8Scene::CreateAssets() {
  // Runs on startup (App::InitVars -> InitVars + CreateAssets) and is also what
  // BackyardScene::OnLoadScene calls on a scene transition.
  //
  // Order matters: the solver must exist BEFORE the render graph loads, because the graph
  // resolves its "particle_system" pass by name and needs the particle count to size the
  // splat scratch buffers.
  LoadFluidConfig();
  InitPbf();
  RegisterParticleSystem();
  BackyardScene::CreateAssets();
  // Scene 8 draws its water with the particle renderer. The inherited water MESH is still
  // bound to the PBF heightfield, so it also displaces itself by the solver's elevation --
  // with a sphere in the air that produced a second, smooth dome above the pool on top of
  // the particles. Only one representation may draw: hide the mesh here.
  m_hideWater = true;
  // Hide the scripted ball mesh too. The parent only manages its visibility from the VOF
  // update path, which this scene never runs, so the GLB was left visible at its authored
  // transform and read as a solid sphere sitting over the pool.
  if (m_ballHandle.IsValid()) {
    if (t850::PrimitiveInst* ball = m_renderContainer.GetMesh(m_ballHandle)) {
      ball->SetVisible(false);
      ball->Update();
    }
  }
  // Publish AFTER the asset path: creating assets rebuilds the render container, and a
  // buffer published earlier could point at a freed allocation.
  m_pbf.PublishParticleBuffers(m_config.particles.buffer, m_config.particles.velocityBuffer);
  ApplyRenderModeToGraph();
}

void Backyard8Scene::ApplyRenderModeToGraph() {
  if (!m_assetsCreated) return;   // the graph is not loaded yet
  t850::RenderGraph& graph = m_renderContainer.Graph();
  const bool screenSpace = m_config.particles.renderMode == 1;
  const bool fallback = screenSpace && m_config.screenSpace.splatFallback;
  // Every alpha-blended particle style (water, wireframe, flat, spheres) now goes through the
  // key + sphere-shade path. Only additive blending keeps the legacy per-particle resolve.
  const bool spheres = !screenSpace && m_config.particles.blend == 1;
  // The stamp always runs: it produces the projected particle table the scatter reads.
  graph.SetPassEnabled("Fluid Particles (resolve)", (!screenSpace && !spheres) || fallback);
  graph.SetPassEnabled("Fluid Clear", screenSpace || spheres);
  graph.SetPassEnabled("Fluid Splat", screenSpace || spheres);
  graph.SetPassEnabled("Fluid Sphere Shade", spheres);
  graph.SetPassEnabled("Fluid Resolve", screenSpace);
  // The shade always reads FluidDepthA. With an odd iteration count the extra pass copies
  // FluidDepthB back to A, so the final target is deterministic.
  const int requested = (std::min)((std::max)(m_config.screenSpace.smoothingIterations, 0), 40);
  const int effective = requested + (requested & 1);
  for (int i = 1; i <= 8; ++i) {
    graph.SetPassEnabled("Fluid Smooth " + std::to_string(i), screenSpace && i <= effective);
  }
  graph.SetPassEnabled("Fluid Surface Shade", screenSpace);
}

void Backyard8Scene::OnLoadScene() {
  // Scene transition: parent InitVars + CreateAssets (our override starts PBF).
  BackyardScene::OnLoadScene();
  InitPbf();
}

void Backyard8Scene::BuildParticleDesc() {
  const t850::Backyard8ParticleConfig& c = m_config.particles;
  const t850::Backyard8ScreenSpaceConfig& ss = m_config.screenSpace;
  t850::ParticleSystemDesc d;
  d.name = c.name;
  d.source = t850::ParticleSourceKind::Buffer;
  d.shape = static_cast<t850::ParticleShape>((std::min)((std::max)(c.shape, 0), 2));
  d.blend = static_cast<t850::ParticleBlend>((std::min)((std::max)(c.blend, 0), 1));
  d.style = static_cast<t850::ParticleStyle>((std::min)((std::max)(c.style, 0), 3));
  d.render_mode = c.renderMode == 1 ? t850::ParticleRenderMode::ScreenSpace
                                    : t850::ParticleRenderMode::Splat;
  d.radius = (std::max)(c.radius, 0.001f);
  d.opacity = (std::min)((std::max)(c.opacity, 0.0f), 1.0f);
  d.intensity = (std::max)(c.intensity, 0.0f);
  d.softness = (std::min)((std::max)(c.softness, 0.0f), 1.0f);
  d.depth_bias = (std::max)(c.depthBias, 0.0f);
  d.size_scale_min = (std::max)(c.sizeScaleMin, 0.01f);
  d.size_scale_max = (std::max)(c.sizeScaleMax, d.size_scale_min);
  d.buffer = c.buffer;
  d.velocity_buffer = c.velocityBuffer;
  d.ramp.stops = c.colors;
  const int solverCount = m_pbf.IsReady() ? static_cast<int>(m_pbf.ParticleCount()) : 0;
  d.buffer_count = c.count > 0 ? (std::min)(c.count, solverCount) : solverCount;
  // Sized once for the scatter key's 17 index bits, so re-seeding to any count needs no graph rebuild.
  d.buffer_capacity = 1 << 17;

  d.screen_space.smoothing_iterations =
      (std::min)((std::max)(ss.smoothingIterations, 0), 8);
  d.screen_space.smoothing_strength =
      (std::min)((std::max)(ss.smoothingStrength, 0.0f), 1.0f);
  d.screen_space.smoothing_radius = (std::min)((std::max)(ss.smoothingRadius, 1.0f), 128.0f);
  d.screen_space.smoothing_world_radius = (std::max)(ss.smoothingWorldRadius, 0.0f);
  d.screen_space.depth_tolerance = (std::max)(ss.depthTolerance, 0.0f);
  d.screen_space.depth_radius_scale =
      (std::min)((std::max)(ss.depthRadiusScale, 0.05f), 1.5f);
  d.screen_space.packing_factor = (std::max)(ss.packingFactor, 1.0f);
  d.screen_space.thickness_scale = (std::max)(ss.thicknessScale, 0.0f);
  d.screen_space.absorption = ss.absorption;
  d.screen_space.absorption_scale = (std::max)(ss.absorptionScale, 0.0f);
  d.screen_space.fluid_color = ss.fluidColor;
  d.screen_space.reflection = ss.reflection;
  d.screen_space.refraction = (std::min)((std::max)(ss.refraction, 0.0f), 2.0f);
  d.screen_space.reflection_distance = (std::max)(ss.reflectionDistance, 0.0f);
  d.screen_space.specular = (std::max)(ss.specular, 0.0f);
  d.screen_space.fresnel_power = (std::max)(ss.fresnelPower, 0.0f);
  d.screen_space.normal_scale = (std::max)(ss.normalScale, 0.0f);
  d.screen_space.normal_step = (std::min)((std::max)(ss.normalStep, 1.0f), 8.0f);
  d.screen_space.foam = (std::min)((std::max)(ss.foam, 0.0f), 1.0f);
  d.screen_space.splat_fallback = ss.splatFallback;
  d.screen_space.debug_view = (std::min)((std::max)(ss.debugView, 0), 4);
  m_particleDesc = d;
}

void Backyard8Scene::RegisterParticleSystem() {
  BuildParticleDesc();
  if (!t850::ParticleSystemRegistry::Instance().Add(m_particleDesc)) {
    T8_LOG_ERROR("[Backyard8] Cannot register the water particle system");
    return;
  }
  ApplyRenderModeToGraph();
  T8_LOG_INFO("[Backyard8] Water particle system '%s': %d particles, radius %.3f m, %s, "
              "%s style, %s blend",
              m_particleDesc.name.c_str(), m_particleDesc.buffer_count, m_particleDesc.radius,
              m_particleDesc.render_mode == t850::ParticleRenderMode::ScreenSpace
                  ? "screen-space" : "splat",
              m_particleDesc.style == t850::ParticleStyle::Wireframe ? "wireframe"
                  : (m_particleDesc.style == t850::ParticleStyle::Flat ? "flat"
                  : (m_particleDesc.style == t850::ParticleStyle::Spheres ? "spheres" : "water")),
              m_particleDesc.blend == t850::ParticleBlend::Additive ? "additive" : "alpha");
}

bool Backyard8Scene::InitPbf() {
  if (m_pbf.IsReady()) return true;
  if (!m_configLoaded) LoadFluidConfig();
  if (!pFramework || !pFramework->pVideoDriver) return false;
  t850::BaseDriver* driver = pFramework->pVideoDriver;

  t850::PbfSettings settings;
  BuildSettings(settings);

  if (!m_pbf.TryInitialize(driver, settings, "Models/Backyard/backyard_layout.json")) {
    T8_LOG_ERROR("[Backyard8] PbfFluid::TryInitialize failed");
    return false;
  }

  // The water mesh samples its displacement from a driver-owned heightfield RT, exactly as
  // the VOF path does (slot 23, RGBA16F, width x depth over the basin footprint).
  const t850::PbfSettings& applied = m_pbf.Settings();
  if (m_waterSurfaceRT >= 0) {
    driver->DestroyRT(m_waterSurfaceRT);
    m_waterSurfaceRT = -1;
  }
  m_waterSurfaceRT = driver->CreateRT(1, t850::BaseRT::RGBA16F, t850::BaseRT::NOTHING,
                                      static_cast<int>(applied.surfaceWidth),
                                      static_cast<int>(applied.surfaceDepth), false, true);
  t850::Texture* surface = m_waterSurfaceRT >= 0
      ? driver->GetRTTexture(m_waterSurfaceRT, t850::BaseDriver::COLOR0_ATTACHMENT) : nullptr;
  if (!surface) {
    T8_LOG_ERROR("[Backyard8] Cannot create the water surface target");
    return false;
  }
  m_pbf.SetSurfaceOutput(surface);
  if (m_waterHandle.IsValid()) {
    if (t850::PrimitiveInst* water = m_renderContainer.GetMesh(m_waterHandle)) {
      water->SetTexture(surface, 23);
      water->Update();
    }
  }
  T8_LOG_INFO("[Backyard8] Particle fluid bound: %u particles, surface %ux%u, renderer %s",
              m_pbf.ParticleCount(), applied.surfaceWidth, applied.surfaceDepth,
              m_config.particles.renderMode == 1 ? "screen-space" : "splat");
  // The pool always settles before the first frame. For the water start the relaxed sphere
  // is appended afterwards, so the surface the drop lands on is already at rest.
  m_settlePending = true;
  return true;
}

bool Backyard8Scene::ReinitializePbf() {
  // Any immediate re-seed (Restart, start-mode change, load) cancels a pending debounce.
  m_reseedDelay = -1.0f;
  if (!pFramework || !pFramework->pVideoDriver) return false;
  t850::BaseDriver* driver = pFramework->pVideoDriver;
  if (!m_configLoaded) LoadFluidConfig();

  t850::PbfSettings settings;
  BuildSettings(settings);
  if (!m_pbf.TryInitialize(driver, settings, "Models/Backyard/backyard_layout.json")) {
    T8_LOG_ERROR("[Backyard8] Re-seed failed: PbfFluid::TryInitialize");
    return false;
  }
  const t850::PbfSettings& applied = m_pbf.Settings();
  if (m_waterSurfaceRT >= 0) {
    driver->DestroyRT(m_waterSurfaceRT);
    m_waterSurfaceRT = -1;
  }
  m_waterSurfaceRT = driver->CreateRT(1, t850::BaseRT::RGBA16F, t850::BaseRT::NOTHING,
                                      static_cast<int>(applied.surfaceWidth),
                                      static_cast<int>(applied.surfaceDepth), false, true);
  t850::Texture* surface = m_waterSurfaceRT >= 0
      ? driver->GetRTTexture(m_waterSurfaceRT, t850::BaseDriver::COLOR0_ATTACHMENT) : nullptr;
  if (!surface) {
    T8_LOG_ERROR("[Backyard8] Cannot recreate the water surface target");
    return false;
  }
  m_pbf.SetSurfaceOutput(surface);
  if (m_waterHandle.IsValid()) {
    if (t850::PrimitiveInst* water = m_renderContainer.GetMesh(m_waterHandle)) {
      water->SetTexture(surface, 23);
      water->Update();
    }
  }
  m_pbf.PublishParticleBuffers(m_config.particles.buffer, m_config.particles.velocityBuffer);
  RegisterParticleSystem();
  m_pbfAccumulator = 0.0f;
  m_settlePending = true;
  T8_LOG_INFO("[Backyard8] Re-seeded: %u particles, spacing %.3f m, sphere %.2f m at %.2f, "
              "surface %ux%u",
              m_pbf.ParticleCount(), applied.particleRadius, applied.sphereRadius,
              applied.sphereHeight, applied.surfaceWidth, applied.surfaceDepth);
  return true;
}

void Backyard8Scene::ApplyStartMode() {
  if (!m_pbf.IsReady()) return;
  if (!ReinitializePbf()) T8_LOG_ERROR("[Backyard8] Start mode re-seed failed");
}

void Backyard8Scene::OnUpdate(float deltaSeconds) {
  // Parent: camera, picking, GUI state, ball body integration (buoyancy is fed by the
  // VOF solver in the parent, so with PBF the balls keep their analytic model).
  BackyardScene::OnUpdate(deltaSeconds);
  if (!m_pbf.IsReady() || !pFramework || !pFramework->pVideoDriver) return;

  if (m_reseedDelay >= 0.0f) {
    m_reseedDelay -= deltaSeconds;
    if (m_reseedDelay < 0.0f) {
      m_reseedDelay = -1.0f;
      if (!ReinitializePbf()) T8_LOG_ERROR("[Backyard8] Re-seed failed");
    }
  }

  if (m_settlePending) {
    // A seeded lattice compacts by about one particle layer (the water drops ~16 cm). The pool
    // settles before the first frame; the water start then appends the relaxed sphere, so the
    // fall lands on a settled surface instead of showing lattice rows.
    m_settlePending = false;
    const auto start = std::chrono::steady_clock::now();
    if (!m_pbf.Settle(pFramework->pVideoDriver, 3.0f, 0.3f)) T8_LOG_ERROR("[Backyard8] Settle failed");
    if (m_pbf.HasPendingSphere()) {
      if (m_pbf.AppendPendingSphere(pFramework->pVideoDriver)) {
        m_pbf.PublishParticleBuffers(m_config.particles.buffer, m_config.particles.velocityBuffer);
        RegisterParticleSystem();
        T8_LOG_INFO("[Backyard8] Sphere appended after settling: %u particles", m_pbf.ParticleCount());
      } else {
        T8_LOG_ERROR("[Backyard8] Cannot append the relaxed sphere");
      }
    }
    T8_LOG_INFO("[Backyard8] Pool settled (3 s simulated) in %.0f ms",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    m_pbfAccumulator = 0.0f;
  }

  // Fixed timestep: clamp the frame delta and run at most N steps per frame, both from the
  // config. dt == 0 (a held frame) accumulates nothing, so the fluid does not advance in
  // paused frames. Substeps stay inside PbfFluid::Step.
  const float kStep = (std::max)(m_config.scene.fixedStep, 1.0e-4f);
  const int kMaxStepsPerFrame = (std::max)(m_config.scene.maxStepsPerFrame, 1);
  m_pbfAccumulator += (std::min)(deltaSeconds > 0.0f ? deltaSeconds : 0.0f,
                                 kStep * static_cast<float>(kMaxStepsPerFrame));
  int steps = 0;
  while (m_pbfAccumulator >= kStep && steps < kMaxStepsPerFrame) {
    m_pbf.SetDeltaTime(kStep);
    if (!m_pbf.Step(pFramework->pVideoDriver)) {
      T8_LOG_ERROR("[Backyard8] Particle step failed");
      m_pbfAccumulator = 0.0f;
      return;
    }
    m_pbfAccumulator -= kStep;
    ++steps;
  }

  // The particle registry clock drives procedural particle systems. The water is a buffer
  // system, but fire/smoke systems added later share the clock.
  t850::ParticleSystemRegistry::Instance().Advance(deltaSeconds > 0.0f ? deltaSeconds : 0.0f);

  if (s_diagFrames > 0 && ++m_diagCounter >= s_diagFrames) {
    m_diagCounter = 0;
    t850::PbfFluid::Diagnostics diag;
    if (m_pbf.ReadDiagnostics(pFramework->pVideoDriver, diag)) {
      T8_LOG_INFO("[Backyard8][pbf] t=%.2f particles=%u dead=%u meanY=%.3f maxY=%.3f aboveRim=%u meanSpeed=%.2f maxSpeed=%.2f at=(%.2f,%.2f,%.2f) rhoMean=%.3f rhoMax=%.3f nonFinite=%u",
                  m_pbf.SimulationTime(), m_pbf.ParticleCount(), diag.dead, diag.meanY, diag.maxY,
                  diag.aboveRim, diag.meanSpeed, diag.maxSpeed,
                  diag.maxSpeedX, diag.maxSpeedY, diag.maxSpeedZ,
                  diag.meanDensity, diag.maxDensity, diag.nonFinite);
    }
    std::vector<float> heights;
    if (m_pbf.ReadColumnHeights(pFramework->pVideoDriver, heights)) {
      std::vector<float> wet;
      wet.reserve(heights.size());
      for (float h : heights) if (h != 0.0f) wet.push_back(h);
      std::sort(wet.begin(), wet.end());
      const auto pick = [&](double q) -> float {
        if (wet.empty()) return 0.0f;
        const size_t index = static_cast<size_t>(q * static_cast<double>(wet.size() - 1));
        return wet[(std::min)(index, wet.size() - 1)];
      };
      T8_LOG_INFO("[Backyard8][pbf] columnTop p10=%.3f p50=%.3f p90=%.3f wet=%zu/%zu",
                  pick(0.10), pick(0.50), pick(0.90), wet.size(), heights.size());
    }
  }
  DumpParticleFieldsIfRequested();
}

void Backyard8Scene::DumpParticleFieldsIfRequested() {
  static const char* const kFrames = std::getenv("T850_PBF_DUMP");
  static const char* const kDir = std::getenv("T850_PBF_DUMP_DIR");
  static int frame = 0;
  ++frame;
  if (!kFrames || !*kFrames) return;
  // Cheap substring test before parsing: the env var is a frame list such as "60,300,1200".
  char needle[32];
  std::snprintf(needle, sizeof(needle), "%d", frame);
  const std::string list(kFrames);
  bool match = false;
  size_t at = 0;
  while (at <= list.size()) {
    const size_t end = list.find(',', at);
    const std::string token = list.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (token == needle) { match = true; break; }
    if (end == std::string::npos) break;
    at = end + 1;
  }
  if (!match) return;

  std::vector<float> pos, vel, lambda;
  if (!m_pbf.ReadParticleFields(pFramework->pVideoDriver, &pos, &vel, &lambda)) {
    T8_LOG_ERROR("[Backyard8][dump] Particle readback failed at frame %d", frame);
    return;
  }
  const std::string dir = (kDir && *kDir) ? kDir : ".";
  char path[512];
  std::snprintf(path, sizeof(path), "%s/pbf_f%d.csv", dir.c_str(), frame);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[Backyard8][dump] Cannot open '%s'", path);
    return;
  }
  file << "x,y,z,alive,vx,vy,vz,density,lambda,neighbours\n";
  const t850::PbfSettings& settings = m_pbf.Settings();
  const float wallTop = settings.wallTopY;
  const float planeY = settings.waterPlaneY;
  const float floorY = settings.basinMinY;
  uint32_t alive = 0, aboveRim = 0, abovePlane = 0;
  uint32_t yBins[24] = {};
  for (uint32_t i = 0; i < m_pbf.ParticleCount(); ++i) {
    const float x = pos[i * 4 + 0], y = pos[i * 4 + 1], z = pos[i * 4 + 2];
    const float a = pos[i * 4 + 3];
    if (a < 0.5f || y < -100.0f) continue;
    ++alive;
    if (y > wallTop) ++aboveRim;
    if (y > planeY) ++abovePlane;
    const int bin = static_cast<int>((y - floorY) * 4.0f);   // 25 cm bins from the floor
    if (bin >= 0 && bin < 24) ++yBins[bin];
    file << x << ',' << y << ',' << z << ',' << a << ','
         << vel[i * 4 + 0] << ',' << vel[i * 4 + 1] << ',' << vel[i * 4 + 2] << ','
         << lambda[i * 4 + 1] << ',' << lambda[i * 4 + 0] << ',' << lambda[i * 4 + 3] << '\n';
  }
  file.close();
  char bins[512];
  size_t used = 0;
  for (int b = 0; b < 24 && used < sizeof(bins); ++b) {
    used += static_cast<size_t>(std::snprintf(bins + used, sizeof(bins) - used, " %.2f:%u",
                                              floorY + (static_cast<float>(b) + 0.5f) * 0.25f, yBins[b]));
  }
  T8_LOG_INFO("[Backyard8][dump] frame=%d alive=%u abovePlane=%u aboveRim=%u -> %s",
              frame, alive, abovePlane, aboveRim, path);
  T8_LOG_INFO("[Backyard8][dump] y-histogram:%s", bins);
}

void Backyard8Scene::OnDestoryScene() {
  if (pFramework && pFramework->pVideoDriver) m_pbf.Shutdown(pFramework->pVideoDriver);
  BackyardScene::OnDestoryScene();
}

namespace {
// Shared widget helpers for the three scene-8 panels. Keeping them in one place avoids
// three copies of the SliderDesc/SelectorDesc boilerplate.
struct FluidPanelControls {
  t850::DevGuiContext& gui;

  bool slider(const char* name, const char* label, float& value,
              float minValue, float maxValue, float step) {
    t850::SliderDesc desc;
    desc.name = name;
    desc.label = label;
    desc.min_val = minValue;
    desc.max_val = maxValue;
    desc.step = step;
    desc.default_val = value;
    return gui.Slider(desc, value);
  }
  bool sliderInt(const char* name, const char* label, int& value,
                 float minValue, float maxValue, float step) {
    t850::SliderDesc desc;
    desc.name = name;
    desc.label = label;
    desc.min_val = minValue;
    desc.max_val = maxValue;
    desc.step = step;
    desc.default_val = static_cast<float>(value);
    return gui.SliderInt(desc, value);
  }
  bool combo(const char* name, const char* label,
             std::vector<std::string> options, int& value) {
    t850::SelectorDesc desc;
    desc.name = name;
    desc.label = label;
    desc.options = std::move(options);
    desc.default_index = value;
    return gui.Combo(desc, value);
  }
  bool check(const char* name, const char* label, bool& value) {
    return gui.Checkbox(t850::CheckboxDesc{name, label, false, true}, value);
  }
};
}  // namespace

void Backyard8Scene::DrawSolverPanel(t850::DevGuiContext& gui) {
  if (!gui.BeginPanel("Backyard 8 - Fluid Simulation")) { gui.EndPanel(); return; }
  FluidPanelControls controls{gui};
  bool seedChanged = false;
  bool lookChanged = false;

  t850::Backyard8SolverConfig& s = m_config.solver;
  t850::Backyard8SceneConfig& sc = m_config.scene;
  t850::Backyard8ParticleConfig& p = m_config.particles;

  // The renderer switch lives here, not in either renderer panel: it selects which of the
  // two following panels is live. Everything else in this panel is solver-wide.
  if (gui.BeginSection("Renderer", true)) {
    if (controls.combo("pbf8_render_mode", "renderer",
                       {"splat (particle splats)", "screen-space fluid"}, p.renderMode)) {
      if (s_renderMode < 0) s_renderMode = p.renderMode;
      lookChanged = true;
    }
    gui.Text(p.renderMode == 0 ? "ACTIVE: particle splats"
                               : "ACTIVE: screen-space fluid");
    gui.Text("Splat settings + wireframe/flat debug: 'Backyard 8 - Particles (Splat)'.");
    gui.Text("SSFR settings + normal/depth/thickness/coverage debug: 'Backyard 8 - Screen Space Fluid'.");
  }

  if (gui.BeginSection("Simulation", true)) {
    if (controls.combo("pbf8_start", "start mode", {"balls (resting pool)", "water (pool + falling sphere)",
                                                    "lake (resting pool)"}, sc.startMode)) {
      s_startMode = sc.startMode;
      ApplyStartMode();
    }
    if (controls.sliderInt("pbf8_iterations", "solver iterations", s.solverIterations, 1.0f, 10.0f, 1.0f))
      m_pbf.SetSolverIterations(static_cast<uint32_t>(s.solverIterations));
    if (controls.sliderInt("pbf8_substeps", "substeps per frame", s.substeps, 1.0f, 6.0f, 1.0f))
      m_pbf.SetSubsteps(static_cast<uint32_t>(s.substeps));
    if (controls.slider("pbf8_viscosity", "XSPH viscosity", s.viscosityC, 0.0f, 0.5f, 0.005f))
      m_pbf.SetViscosity(s.viscosityC);
    if (controls.slider("pbf8_stiffness", "stiffness epsilon", s.stiffnessEps, 0.0f, 0.5f, 0.005f))
      m_pbf.SetStiffness(s.stiffnessEps);
    if (controls.slider("pbf8_art_k", "artificial pressure k", s.artPressureK, 0.0f, 0.05f, 0.001f))
      m_pbf.SetArtificialPressure(s.artPressureK, s.artPressureN);
    if (controls.slider("pbf8_art_n", "artificial pressure n", s.artPressureN, 1.0f, 8.0f, 0.1f))
      m_pbf.SetArtificialPressure(s.artPressureK, s.artPressureN);
    if (controls.slider("pbf8_gravity", "gravity (m/s^2)", s.gravity, 0.0f, 30.0f, 0.05f))
      m_pbf.SetGravity(s.gravity);
    if (controls.slider("pbf8_maxvel", "max speed (m/s)", s.maxVelocity, 0.5f, 40.0f, 0.5f))
      m_pbf.SetMaxVelocity(s.maxVelocity);
    if (controls.slider("pbf8_wind_speed", "wind speed (m/s)", s.windSpeed, 0.0f, 20.0f, 0.1f) |
        controls.slider("pbf8_wind_dir", "wind direction (deg)", s.windHeadingDeg, 0.0f, 360.0f, 1.0f)) {
      constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
      m_pbf.SetWind(static_cast<float>(s.windSpeed * std::cos(s.windHeadingDeg * kDegToRad)),
                    static_cast<float>(s.windSpeed * std::sin(s.windHeadingDeg * kDegToRad)));
    }
    // 1 is physical air drag (pool essentially still); ~20 brings the old ripples back.
    if (controls.slider("pbf8_wind_strength", "wind strength (x air drag)", s.windStrength,
                        0.0f, 50.0f, 0.5f))
      m_pbf.SetWindStrength(s.windStrength);
    if (controls.slider("pbf8_wall_friction", "wall no-slip (x viscosity)", s.wallFriction,
                        0.0f, 20.0f, 0.5f))
      m_pbf.SetWallFriction(s.wallFriction);
    if (controls.slider("pbf8_fixed_step", "fixed step (s)", sc.fixedStep, 1.0f / 240.0f, 1.0f / 20.0f,
                        0.0001f))
      m_pbf.SetDeltaTime(sc.fixedStep);
    controls.sliderInt("pbf8_max_steps", "max steps per frame", sc.maxStepsPerFrame, 1.0f, 6.0f, 1.0f);
    gui.Separator();
    if (gui.Button("Restart (re-seed the water)")) {
      if (!ReinitializePbf()) T8_LOG_ERROR("[Backyard8] Re-seed failed");
    }
  }

  if (gui.BeginSection("Particle seeding & count", false)) {
    gui.Text("Every change here rebuilds the solver and re-seeds the water at once.");
    seedChanged |= controls.slider("pbf8_spacing", "particle spacing (m)", s.particleRadius,
                                   0.05f, 0.30f, 0.001f);
    seedChanged |= controls.slider("pbf8_h_scale", "smoothing radius / spacing", s.smoothingRadiusScale,
                                   1.5f, 4.0f, 0.05f);
    seedChanged |= controls.slider("pbf8_prefill", "pool level under the drop (m, 0 = plane)",
                                   s.prefillLevel, -1.6f, 0.3f, 0.01f);
    seedChanged |= controls.slider("pbf8_resting", "resting pool level (m, 0 = plane)",
                                   s.restingLevel, -1.6f, 0.3f, 0.01f);
    seedChanged |= controls.slider("pbf8_sphere_r", "water sphere radius (m)", s.sphereRadius,
                                   0.5f, 3.5f, 0.01f);
    seedChanged |= controls.slider("pbf8_sphere_h", "sphere height above water (m)", s.sphereHeight,
                                   0.5f, 6.0f, 0.01f);
    // The renderer's scatter key has 17 index bits.
    seedChanged |= controls.sliderInt("pbf8_max_particles", "max particles", s.maxParticles,
                                      1000.0f, 131072.0f, 1000.0f);
    seedChanged |= controls.sliderInt("pbf8_surface_w", "heightfield width", s.surfaceWidth,
                                      16.0f, 512.0f, 1.0f);
    seedChanged |= controls.sliderInt("pbf8_surface_d", "heightfield depth", s.surfaceDepth,
                                      8.0f, 256.0f, 1.0f);
    char line[192];
    std::snprintf(line, sizeof(line), "%u live particles   %.2f t   %u culled",
                  m_pbf.ParticleCount(), m_pbf.SimulationTime(), m_pbf.CulledParticles());
    gui.Text(line);
  }

  if (gui.BeginSection("Diagnostics", false)) {
    char line[256];
    std::snprintf(line, sizeof(line), "particles %u   t %.2f s   culled %u",
                  m_pbf.ParticleCount(), m_pbf.SimulationTime(), m_pbf.CulledParticles());
    gui.Text(line);
    if (gui.Button("Sample GPU diagnostics")) {
      t850::PbfFluid::Diagnostics diag;
      if (m_pbf.ReadDiagnostics(pFramework->pVideoDriver, diag)) {
        m_lastDiag = diag;
        m_diagValid = true;
      }
    }
    if (m_diagValid) {
      std::snprintf(line, sizeof(line),
                    "mean speed %.3f   max %.3f   mean rho %.3f   max rho %.3f",
                    m_lastDiag.meanSpeed, m_lastDiag.maxSpeed,
                    m_lastDiag.meanDensity, m_lastDiag.maxDensity);
      gui.Text(line);
      std::snprintf(line, sizeof(line),
                    "mean Y %.3f   max Y %.3f   above rim %u   dead %u   non-finite %u",
                    m_lastDiag.meanY, m_lastDiag.maxY, m_lastDiag.aboveRim,
                    m_lastDiag.dead, m_lastDiag.nonFinite);
      gui.Text(line);
    }
  }

  // Re-seed after the drag stops: rebuilding the solver every drag frame costs
  // 30-250 ms and makes the sliders unusable.
  if (seedChanged) m_reseedDelay = 0.2f;
  if (lookChanged) RegisterParticleSystem();
  gui.EndPanel();
}

void Backyard8Scene::DrawParticlePanel(t850::DevGuiContext& gui) {
  if (!gui.BeginPanel("Backyard 8 - Particles (Splat)")) { gui.EndPanel(); return; }
  FluidPanelControls controls{gui};
  bool lookChanged = false;
  t850::Backyard8ParticleConfig& p = m_config.particles;

  gui.Text(p.renderMode == 0 ? "ACTIVE: this renderer is drawing the pool"
                              : "INACTIVE: select 'splat' in Backyard 8 - Fluid Simulation");
  gui.Text("Water look uses the Optics of 'Backyard 8 - Screen Space Fluid' (absorption, refraction).");

  if (gui.BeginSection("Splat look", true)) {
    if (controls.combo("pbf8_shape", "falloff shape", {"soft sphere", "sprite", "streak"}, p.shape))
      lookChanged = true;
    if (controls.combo("pbf8_blend", "blend", {"additive", "alpha"}, p.blend))
      lookChanged = true;
    lookChanged |= controls.slider("pbf8_radius", "render radius (m)", p.radius, 0.01f, 0.4f, 0.001f);
    lookChanged |= controls.slider("pbf8_opacity", "splat opacity", p.opacity, 0.0f, 1.0f, 0.01f);
    lookChanged |= controls.slider("pbf8_intensity", "additive intensity", p.intensity, 0.0f, 8.0f, 0.05f);
    lookChanged |= controls.slider("pbf8_softness", "edge softness", p.softness, 0.0f, 1.0f, 0.01f);
    lookChanged |= controls.slider("pbf8_depth_bias", "soft depth bias (m)", p.depthBias, 0.0f, 0.5f, 0.005f);
    lookChanged |= controls.slider("pbf8_size_min", "size scale at age 0", p.sizeScaleMin, 0.1f, 4.0f, 0.05f);
    lookChanged |= controls.slider("pbf8_size_max", "size scale at age 1", p.sizeScaleMax, 0.1f, 4.0f, 0.05f);
  }

  if (gui.BeginSection("Colour ramp", false)) {
    gui.Text("Four stops over particle age; a buffer fluid adds a per-particle jitter.");
    static const char* const kNames[4] = {"stop 0", "stop 1", "stop 2", "stop 3"};
    for (int i = 0; i < 4; ++i) {
      char id[32];
      std::snprintf(id, sizeof(id), "pbf8_color%d_r", i);
      lookChanged |= controls.slider(id, (std::string(kNames[i]) + " R").c_str(), p.colors[i][0],
                                     0.0f, 1.0f, 0.001f);
      std::snprintf(id, sizeof(id), "pbf8_color%d_g", i);
      lookChanged |= controls.slider(id, (std::string(kNames[i]) + " G").c_str(), p.colors[i][1],
                                     0.0f, 1.0f, 0.001f);
      std::snprintf(id, sizeof(id), "pbf8_color%d_b", i);
      lookChanged |= controls.slider(id, (std::string(kNames[i]) + " B").c_str(), p.colors[i][2],
                                     0.0f, 1.0f, 0.001f);
    }
  }

  if (gui.BeginSection("Splat debug", false)) {
    gui.Text("The particle look and its debug views:");
    gui.Text("  water = particles blended into water (alpha blend)");
    gui.Text("  wireframe = particle lattice / per-particle rings");
    gui.Text("  flat = filled coverage discs");
    gui.Text("  spheres = one lit sphere per particle, depth sorted");
    if (controls.combo("pbf8_style", "style / splat debug",
                       {"water (blended)", "wireframe (particle lattice)", "flat (coverage discs)",
                        "spheres (lit particles)"}, p.style))
      lookChanged = true;
    if (controls.sliderInt("pbf8_draw_count", "draw only N particles (0 = all)", p.count,
                           0.0f, static_cast<float>((std::max)(static_cast<int>(m_pbf.ParticleCount()), 1)),
                           1.0f))
      lookChanged = true;
    gui.Separator();
    gui.Text("Screen-space normals/depth/thickness/coverage are a different debug path:");
    gui.Text("see 'Backyard 8 - Screen Space Fluid' -> Debug (screen space).");
  }

  if (lookChanged) RegisterParticleSystem();
  gui.EndPanel();
}

void Backyard8Scene::DrawScreenSpacePanel(t850::DevGuiContext& gui) {
  if (!gui.BeginPanel("Backyard 8 - Screen Space Fluid")) { gui.EndPanel(); return; }
  FluidPanelControls controls{gui};
  bool lookChanged = false;
  t850::Backyard8ParticleConfig& p = m_config.particles;
  t850::Backyard8ScreenSpaceConfig& ss = m_config.screenSpace;

  gui.Text(p.renderMode == 1 ? "ACTIVE: SSFR is reconstructing the pool surface"
                              : "INACTIVE: select 'screen-space fluid' in Backyard 8 - Fluid Simulation");
  gui.Text("These controls affect only the screen-space renderer. The splat path ignores them.");

  if (gui.BeginSection("Surface reconstruction", true)) {
    if (controls.sliderInt("pbf8_ss_iters", "smoothing passes (0-8)", ss.smoothingIterations,
                           0.0f, 8.0f, 1.0f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_strength", "smoothing strength", ss.smoothingStrength,
                        0.0f, 1.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_world_radius", "filter radius (m)", ss.smoothingWorldRadius,
                        0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_radius", "filter radius cap (px)", ss.smoothingRadius,
                        1.0f, 128.0f, 1.0f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_tol", "layer threshold (m)", ss.depthTolerance,
                        0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_depth_radius", "depth radius / spacing", ss.depthRadiusScale,
                        0.2f, 1.2f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_packing", "packing factor", ss.packingFactor, 1.0f, 8.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_thick", "thickness scale", ss.thicknessScale, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
  }

  if (gui.BeginSection("Optics", true)) {
    if (controls.slider("pbf8_ss_abs_r", "absorption R (1/m)", ss.absorption[0], 0.0f, 6.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_abs_g", "absorption G (1/m)", ss.absorption[1], 0.0f, 6.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_abs_b", "absorption B (1/m)", ss.absorption[2], 0.0f, 6.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_abs_scale", "absorption multiplier", ss.absorptionScale,
                        0.0f, 12.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_scatter_r", "scatter R", ss.fluidColor[0], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_scatter_g", "scatter G", ss.fluidColor[1], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_scatter_b", "scatter B", ss.fluidColor[2], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_reflect_r", "reflection R", ss.reflection[0], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_reflect_g", "reflection G", ss.reflection[1], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_reflect_b", "reflection B", ss.reflection[2], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_refract", "refraction (path per m)", ss.refraction, 0.0f, 2.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_reflect_dist", "reflection range (m)", ss.reflectionDistance,
                        0.0f, 80.0f, 0.5f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_spec", "sun specular", ss.specular, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_fresnel", "Fresnel exponent", ss.fresnelPower, 1.0f, 16.0f, 0.1f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_normal", "normal scale", ss.normalScale, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_normal_step", "normal stencil (px)", ss.normalStep, 1.0f, 8.0f, 1.0f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_foam", "spray whitewater", ss.foam, 0.0f, 1.0f, 0.01f))
      lookChanged = true;
  }

  if (gui.BeginSection("Debug (screen space)", false)) {
    gui.Text("These views are produced by the screen-space shade pass:");
    gui.Text("  normals / surface depth / thickness / coverage");
    if (controls.combo("pbf8_ss_debug", "debug view",
                       {"shaded", "normals", "surface depth", "thickness", "coverage"}, ss.debugView))
      lookChanged = true;
    if (controls.check("pbf8_ss_fallback", "keep splat underneath (debug fallback)", ss.splatFallback))
      lookChanged = true;
    gui.Separator();
    gui.Text("The splat wireframe/flat debug is a different path:");
    gui.Text("see 'Backyard 8 - Particles (Splat)' -> Splat debug.");
  }

  if (lookChanged) RegisterParticleSystem();
  gui.EndPanel();
}

void Backyard8Scene::DrawDevGui(t850::DevGuiContext& gui) {
  m_devGuiDrawnThisFrame = true;
  // Three panels, not one: the solver switch belongs with the solver, while the splat and
  // SSFR settings/debug helpers are deliberately separate because they render differently.
  DrawSolverPanel(gui);
  DrawParticlePanel(gui);
  DrawScreenSpacePanel(gui);
}
