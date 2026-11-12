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
  m_config.screenSpace.thicknessDownsample =
      (std::min)((std::max)(m_config.screenSpace.thicknessDownsample, 0), 2);
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
  m_pbf.PublishParticleBuffers(m_config.particles.buffer, m_config.particles.velocityBuffer,
                               m_config.particles.anisotropyBuffer);
  ApplyRenderModeToGraph();
}

bool Backyard8Scene::UsesWaterShade() const {
  return m_config.particles.renderMode == 1 ||
         (m_config.particles.renderMode == 0 && m_config.particles.blend == 1 &&
          m_config.particles.style == 0);
}

bool Backyard8Scene::UsesSurfels() const {
  return m_config.particles.renderMode == 0 && m_config.particles.blend == 1 &&
         m_config.particles.style == 0;
}

void Backyard8Scene::ApplyRenderModeToGraph() {
  if (!m_assetsCreated) return;   // the graph is not loaded yet
  t850::RenderGraph& graph = m_renderContainer.Graph();
  const bool screenSpace = m_config.particles.renderMode == 1;
  const bool fallback = screenSpace && m_config.screenSpace.splatFallback;
  // Every alpha-blended particle style (water, wireframe, flat, spheres) now goes through the
  // key + sphere-shade path. Only additive blending keeps the legacy per-particle resolve.
  const bool spheres = !screenSpace && m_config.particles.blend == 1 &&
                       m_config.particles.style != 0;
  const bool waterShade = UsesWaterShade();
  const bool surfels = UsesSurfels();
  // The stamp always runs: it produces the projected particle table the scatter reads.
  graph.SetPassEnabled("Fluid Particles (resolve)", (!waterShade && !spheres) || fallback);
  // The screen-space renderer scatters at the fluid targets' (half) resolution; the particle
  // renderer's sphere shade reads the same buffers at full resolution, so it has its own copy.
  graph.SetPassEnabled("Fluid Clear", waterShade);
  graph.SetPassEnabled("Fluid Splat", waterShade);
  graph.SetPassEnabled("Fluid Surfel Splat", surfels);
  graph.SetPassEnabled("Fluid Clear (splat mode)", spheres);
  graph.SetPassEnabled("Fluid Splat (splat mode)", spheres);
  graph.SetPassEnabled("Whitewater Splat", waterShade);
  graph.SetPassEnabled("Fluid Sphere Shade", spheres);
  graph.SetPassEnabled("Fluid Resolve", screenSpace);
  graph.SetPassEnabled("Fluid Resolve (surfels)", surfels);
  // The shade always reads FluidDepthA. With an odd iteration count the extra pass copies
  // FluidDepthB back to A, so the final target is deterministic.
  const int requested = (std::min)((std::max)(m_config.screenSpace.smoothingIterations, 0), 40);
  const int effective = requested + (requested & 1);
  for (int i = 1; i <= 8; ++i) {
    graph.SetPassEnabled("Fluid Smooth " + std::to_string(i), screenSpace && i <= effective);
  }
  graph.SetPassEnabled("Fluid Surface Shade", waterShade);
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
  d.render_mode = UsesWaterShade() ? t850::ParticleRenderMode::ScreenSpace
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
  d.anisotropy_buffer = c.anisotropyBuffer;
  // Whitewater: per-particle foam/air and the secondary pool are read by passes; all four
  // solver buffers are declared so the graph accepts them before the solver publishes.
  d.whitewater_buffer = c.whitewaterBuffer;
  d.diffuse_buffer = c.diffuseBuffer;
  d.extra_buffers = {c.whitewaterBuffer, c.diffuseBuffer, c.causticBuffer, c.wetnessBuffer};
  {
    const t850::PbfSettings& s = m_pbf.Settings();
    const t850::PbfWhitewaterLayout layout = m_pbf.WhitewaterLayout();
    d.geometry.pool_min = {{s.basinMinX, s.basinMinY, s.basinMinZ}};
    d.geometry.pool_max = {{s.basinMaxX, s.wallTopY, s.basinMaxZ}};
    d.geometry.water_plane_y = s.waterPlaneY;
    d.geometry.deck_y = s.waterPlaneY + 0.16f;   // CS_PbfCull's deck height
    d.geometry.wet_origin_x = layout.wetOriginX;
    d.geometry.wet_origin_z = layout.wetOriginZ;
    d.geometry.wet_texel = layout.wetTexel;
    d.geometry.wet_width = static_cast<int>(layout.wetWidth);
    d.geometry.wet_height = static_cast<int>(layout.wetHeight);
    d.geometry.surface_width = static_cast<int>(layout.surfaceWidth);
    d.geometry.surface_depth = static_cast<int>(layout.surfaceDepth);
    d.geometry.diffuse_capacity = static_cast<int>(layout.diffuseCapacity);
  }
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
  d.screen_space.sky_horizon = ss.skyHorizon;
  d.screen_space.refraction = (std::min)((std::max)(ss.refraction, 0.0f), 2.0f);
  d.screen_space.refraction_strength = (std::min)((std::max)(ss.refractionStrength, 0.0f), 1.0f);
  d.screen_space.reflection_distance = (std::max)(ss.reflectionDistance, 0.0f);
  d.screen_space.specular = (std::max)(ss.specular, 0.0f);
  d.screen_space.glint_exponent = (std::max)(ss.glintExponent, 1.0f);
  d.screen_space.fresnel_power = (std::max)(ss.fresnelPower, 0.0f);
  d.screen_space.normal_scale = (std::max)(ss.normalScale, 0.0f);
  d.screen_space.normal_step = (std::min)((std::max)(ss.normalStep, 1.0f), 8.0f);
  d.screen_space.foam = (std::min)((std::max)(ss.foam, 0.0f), 1.0f);
  d.screen_space.ripple_slope = (std::max)(ss.rippleSlope, 0.0f);
  d.screen_space.ripple_wavelength = (std::max)(ss.rippleWavelength, 0.01f);
  d.screen_space.ripple_speed = (std::max)(ss.rippleSpeed, 0.0f);
  d.screen_space.ripple_distance = (std::max)(ss.rippleDistance, 0.1f);
  d.screen_space.caustics = (std::max)(ss.caustics, 0.0f);
  d.screen_space.caustic_scale = (std::max)(ss.causticScale, 0.01f);
  d.screen_space.caustic_speed = (std::max)(ss.causticSpeed, 0.0f);
  d.screen_space.caustic_depth = (std::max)(ss.causticDepth, 0.01f);
  d.screen_space.splat_fallback = ss.splatFallback;
  d.screen_space.anisotropy = ss.anisotropy;
  // Scene lighting (the 2026-11 realism pass): clamps mirror ParticleSystem.cpp's validation.
  d.screen_space.env_intensity = (std::max)(ss.envIntensity, 0.0f);
  d.screen_space.sun_attenuation = (std::min)((std::max)(ss.sunAttenuation, 0.0f), 1.0f);
  d.screen_space.caustic_focus = (std::max)(ss.causticFocus, 0.0f);
  d.screen_space.contact_line = (std::max)(ss.contactLine, 0.0f);
  d.screen_space.backlight = (std::max)(ss.backlight, 0.0f);
  d.screen_space.scatter_sun = (std::max)(ss.scatterSun, 0.0f);
  d.screen_space.scatter_sky = (std::max)(ss.scatterSky, 0.0f);
  d.screen_space.scatter_anisotropy = (std::min)((std::max)(ss.scatterAnisotropy, 0.0f), 0.95f);
  d.screen_space.ripple_calm = (std::max)(ss.rippleCalm, 0.0f);
  d.screen_space.ripple_agitation = (std::max)(ss.rippleAgitation, 0.0f);
  // Whitewater (Ihmsen et al. 2012): foam, aeration, secondary particles and the wet deck.
  d.screen_space.foam_strength = (std::max)(ss.foamStrength, 0.0f);
  d.screen_space.foam_texture_scale = (std::max)(ss.foamTextureScale, 0.01f);
  d.screen_space.aeration = (std::max)(ss.aeration, 0.0f);
  d.screen_space.bubble_strength = (std::max)(ss.bubbleStrength, 0.0f);
  d.screen_space.spray_strength = (std::max)(ss.sprayStrength, 0.0f);
  d.screen_space.wetness = (std::max)(ss.wetness, 0.0f);
  d.screen_space.debug_view = (std::min)((std::max)(ss.debugView, 0), 9);
  // Surfel splatting: the particle renderer's water style reconstructs the surface by blending
  // the front layer of anisotropic particles, not by smoothing the depth field.
  d.screen_space.surfels = UsesSurfels();
  d.screen_space.surfel_band = (std::min)((std::max)(c.surfelBand, 0.1f), 8.0f);
  d.screen_space.surfel_sharpness = (std::min)((std::max)(c.surfelSharpness, 0.25f), 8.0f);
  d.screen_space.surfel_smoothing = (std::min)((std::max)(c.surfelSmoothing, 0.0f), 4.0f);
  d.screen_space.thickness_downsample =
      (std::min)((std::max)(ss.thicknessDownsample, 0), 2);
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
                  ? (UsesSurfels() ? "screen-space fluid (surfel splatting)"
                                   : "screen-space fluid (smoothed depth)")
                  : "splat",
              m_particleDesc.style == t850::ParticleStyle::Wireframe ? "wireframe"
                  : (m_particleDesc.style == t850::ParticleStyle::Flat ? "flat"
                  : (m_particleDesc.style == t850::ParticleStyle::Spheres ? "spheres" : "water")),
              m_particleDesc.blend == t850::ParticleBlend::Additive ? "additive" : "alpha");
}

// Builds the solver's whitewater settings from the config. The solver clamps maxDiffuse to
// 64..1<<20, wetResolution to 16..2048 and wetTexel to >= 0.01 (CreateWhitewaterBuffers);
// clamp identically here so the values the solver applies are the ones the Dev GUI shows.
t850::PbfWhitewaterSettings Backyard8Scene::WhitewaterSettings() const {
  const t850::Backyard8WhitewaterConfig& w = m_config.whitewater;
  t850::PbfWhitewaterSettings s;
  s.enabled = w.enabled;
  s.trappedAirMin = w.trappedAirMin;
  s.trappedAirMax = w.trappedAirMax;
  s.crestMin = w.crestMin;
  s.crestMax = w.crestMax;
  s.energyMin = w.energyMin;
  s.energyMax = w.energyMax;
  s.surfaceNeighbours = w.surfaceNeighbours;
  s.foamGain = w.foamGain;
  s.foamLifetime = w.foamLifetime;
  s.aerationGain = w.aerationGain;
  s.aerationLifetime = w.aerationLifetime;
  s.maxDiffuse = static_cast<uint32_t>((std::min)((std::max)(w.maxDiffuse, 64u), 1u << 20));
  s.sprayRate = w.sprayRate;
  s.crestRate = w.crestRate;
  s.diffuseLifetime = w.diffuseLifetime;
  s.bubbleBuoyancy = w.bubbleBuoyancy;
  s.bubbleDrag = w.bubbleDrag;
  s.sprayDrag = w.sprayDrag;
  s.dryingTime = w.dryingTime;
  s.wetTexel = (std::max)(w.wetTexel, 0.01f);
  s.wetResolution = static_cast<uint32_t>((std::min)((std::max)(w.wetResolution, 16u), 2048u));
  s.causticGain = w.causticGain;
  return s;
}

void Backyard8Scene::ConfigureWhitewater() {
  const t850::Backyard8ParticleConfig& p = m_config.particles;
  m_pbf.SetWhitewaterBufferNames(p.whitewaterBuffer, p.diffuseBuffer, p.causticBuffer, p.wetnessBuffer);
  m_pbf.SetWhitewater(WhitewaterSettings());
}

bool Backyard8Scene::InitPbf() {
  if (m_pbf.IsReady()) return true;
  if (!m_configLoaded) LoadFluidConfig();
  if (!pFramework || !pFramework->pVideoDriver) return false;
  t850::BaseDriver* driver = pFramework->pVideoDriver;

  t850::PbfSettings settings;
  BuildSettings(settings);
  ConfigureWhitewater();

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
              UsesWaterShade() ? (UsesSurfels() ? "screen-space fluid (surfel splatting)"
                                                 : "screen-space fluid (smoothed depth)")
                                : "splat");
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
  ConfigureWhitewater();
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
  m_pbf.PublishParticleBuffers(m_config.particles.buffer, m_config.particles.velocityBuffer,
                               m_config.particles.anisotropyBuffer);
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
        m_pbf.PublishParticleBuffers(m_config.particles.buffer, m_config.particles.velocityBuffer,
                                     m_config.particles.anisotropyBuffer);
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

  // Anisotropic particle shapes for the screen-space depth splat, from the committed positions
  // of this frame. The water shade (both surface reconstructions) reads them.
  if (UsesWaterShade() && m_config.screenSpace.anisotropy) {
    const t850::Backyard8ScreenSpaceConfig& ss = m_config.screenSpace;
    t850::PbfAnisotropySettings anisotropy;
    anisotropy.smoothing = ss.anisotropySmoothing;
    anisotropy.maxRatio = ss.anisotropyMaxRatio;
    anisotropy.radiusScale = ss.anisotropyRadius;
    anisotropy.sparseScale = ss.anisotropySprayRadius;
    anisotropy.fullNeighbours = ss.anisotropyNeighbours;
    anisotropy.sparseNeighbours = ss.anisotropySprayNeighbours;
    anisotropy.maxStretch = ss.anisotropyMaxStretch;
    m_pbf.SetAnisotropy(anisotropy);
    if (!m_pbf.ComputeAnisotropy(pFramework->pVideoDriver) && !m_anisotropyErrorLogged) {
      T8_LOG_ERROR("[Backyard8] Anisotropy pass failed; the surface falls back to spheres");
      m_anisotropyErrorLogged = true;
    }
  }
  // Foam, entrained air, spray/foam/bubble particles, caustic field and deck wetness, over the
  // time simulated this frame (after the anisotropy: they reuse its neighbour grid).
  if (UsesWaterShade() && m_pbf.Whitewater().enabled) {
    if (!m_pbf.ComputeWhitewater(pFramework->pVideoDriver, static_cast<float>(steps) * kStep) &&
        !m_whitewaterErrorLogged) {
      T8_LOG_ERROR("[Backyard8] Whitewater pass failed; the water renders without foam, spray or wet deck");
      m_whitewaterErrorLogged = true;
    }
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
                       {"particles (surfel splatting)", "screen-space fluid (smoothed depth)"},
                       p.renderMode)) {
      if (s_renderMode < 0) s_renderMode = p.renderMode;
      lookChanged = true;
    }
    gui.Text(p.renderMode == 0 ? "ACTIVE: particle renderer (surfel splatting)"
                               : "ACTIVE: screen-space fluid (smoothed depth)");
    gui.Text(p.renderMode == 0
                 ? "Renderer settings: 'Backyard 8 - Particles (Splat)' (INACTIVE until selected here)."
                 : "Renderer settings: 'Backyard 8 - Screen Space Fluid' (INACTIVE until selected here).");
    gui.Text("Both renderers draw with one water shade: 'Backyard 8 - Water Shading' holds its settings.");
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
  t850::Backyard8ScreenSpaceConfig& ss = m_config.screenSpace;

  const bool rendererActive = p.renderMode == 0;
  // The particle renderer and the water style only coexist in one combination: alpha blend +
  // the water style (UsesSurfels / UsesWaterShade). Anything else is a debug style.
  const bool waterStyle = UsesSurfels();
  gui.Text(rendererActive ? "ACTIVE: this renderer is drawing the pool"
                          : "INACTIVE: select 'particles (surfel splatting)' in Backyard 8 - Fluid Simulation");
  if (rendererActive) {
    gui.Text(waterStyle ? "Drawing with: the water style (surfel splatting), shaded by the water shade."
                        : "Drawing with: the debug style below, not the water.");
    gui.Text(waterStyle ? "Its shading lives in 'Backyard 8 - Water Shading'."
                        : "That style shades itself: 'Backyard 8 - Water Shading' is not in use.");
  } else {
    gui.Text("Nothing below is drawn: the screen-space renderer is drawing the pool.");
  }

  // render radius and the size scales also drive the water: the stamp turns them into the
  // projected particle radius (Projected.z), which sizes CS_FluidSplat's thickness/whitewater
  // discs and the chord (worldThick), and CS_ParticleShade's normal stencil and gather radius
  // use Radius (ShadeParams.y) too. They live with the other shared splat controls.
  if (gui.BeginSection("Splat look", true)) {
    if (controls.combo("pbf8_shape", "falloff shape", {"soft sphere", "sprite", "streak"}, p.shape))
      lookChanged = true;
    if (controls.combo("pbf8_blend", "blend", {"additive", "alpha"}, p.blend))
      lookChanged = true;
    lookChanged |= controls.slider("pbf8_opacity", "splat opacity", p.opacity, 0.0f, 1.0f, 0.01f);
    lookChanged |= controls.slider("pbf8_intensity", "additive intensity", p.intensity, 0.0f, 8.0f, 0.05f);
    // Softness, shape, depth bias, intensity and opacity are read only by the per-pixel
    // resolve of CS_ParticleSplat (the "Fluid Particles (resolve)" pass), which
    // ApplyRenderModeToGraph disables for the water style. The stamp writes only the
    // projected radius and the tile masks.
    lookChanged |= controls.slider("pbf8_softness", "edge softness", p.softness, 0.0f, 1.0f, 0.01f);
    lookChanged |= controls.slider("pbf8_depth_bias", "soft depth bias (m)", p.depthBias, 0.0f, 0.5f, 0.005f);
    gui.Text("The water style ignores falloff shape, opacity, intensity, edge softness and depth");
    gui.Text("bias: the per-pixel splat resolve that reads them runs only for the additive look");
    gui.Text("and the screen-space debug fallback. Blend selects the water (alpha + water style)");
    gui.Text("or the additive look. Splat sizes live in 'Water Shading' -> Splat.");
  }

  if (gui.BeginSection("Surfels", true)) {
    gui.Text("Surfel splatting: the front layer of anisotropic particles blended into depth");
    gui.Text("and normal in one pass. Only the water style (alpha blend) uses it.");
    lookChanged |= controls.slider("pbf8_srf_band", "surfel band (radii)", p.surfelBand, 0.1f, 8.0f, 0.05f);
    lookChanged |= controls.slider("pbf8_srf_sharp", "surfel sharpness", p.surfelSharpness,
                                   0.25f, 8.0f, 0.05f);
    lookChanged |= controls.slider("pbf8_srf_smooth", "normal smoothing (radii)", p.surfelSmoothing,
                                   0.0f, 4.0f, 0.05f);
  }

  if (gui.BeginSection("Colour ramp", false)) {
    gui.Text("Four stops over particle age; a buffer fluid adds a per-particle jitter.");
    gui.Text("The water style ignores all four stops: its particles are not tinted by the ramp.");
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
    // Alpha blend is required as well: additive blend keeps the legacy per-particle resolve.
    gui.Text(waterStyle ? "  water style selected: alpha blend, so the water is drawn."
                        : "  the water needs the water style AND alpha blend; otherwise this style draws.");
    if (controls.combo("pbf8_style", "style / splat debug",
                       {"water (surfel splatting)", "wireframe (particle lattice)",
                        "flat (coverage discs)", "spheres (lit particles)"}, p.style))
      lookChanged = true;
    if (controls.sliderInt("pbf8_draw_count", "draw only N particles (0 = all)", p.count,
                           0.0f, static_cast<float>((std::max)(static_cast<int>(m_pbf.ParticleCount()), 1)),
                           1.0f))
      lookChanged = true;
    // Only CS_FluidSphereShade reads this (the spheres style's own refraction); the water
    // shade uses 'refraction strength' in 'Water Shading' -> Optics.
    if (controls.slider("pbf8_ss_refract", "spheres style: refraction (path per m)", ss.refraction,
                        0.0f, 2.0f, 0.01f))
      lookChanged = true;
    gui.Separator();
    gui.Text("Screen-space normals/depth/thickness/coverage are a different debug path:");
    gui.Text("see 'Backyard 8 - Water Shading' -> Debug (water shade).");
  }

  if (lookChanged) RegisterParticleSystem();
  gui.EndPanel();
}

void Backyard8Scene::DrawScreenSpacePanel(t850::DevGuiContext& gui) {
  if (!gui.BeginPanel("Backyard 8 - Screen Space Fluid")) { gui.EndPanel(); return; }
  FluidPanelControls controls{gui};
  bool lookChanged = false;
  t850::Backyard8ScreenSpaceConfig& ss = m_config.screenSpace;

  // The two renderer panels are exclusive: the renderer combo is the only switch between them.
  const bool rendererActive = m_config.particles.renderMode == 1;
  gui.Text(rendererActive ? "ACTIVE: the screen-space renderer is reconstructing the pool"
                          : "INACTIVE: select 'screen-space fluid (smoothed depth)' in Backyard 8 - Fluid Simulation");
  if (rendererActive) {
    gui.Text("Surface reconstruction: smoothed depth (a-trous smoothing passes)");
    gui.Text("Its shading lives in 'Backyard 8 - Water Shading'.");
  } else {
    gui.Text("Nothing below is drawn: the particle renderer is drawing the pool.");
  }
  gui.Text("Only this renderer uses these: the smoothing controls below, and the splat fallback.");

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
    gui.Text("These five tune the smoothing passes, which only this renderer runs.");
  }

  if (gui.BeginSection("Debug", false)) {
    if (controls.check("pbf8_ss_fallback", "keep splat underneath (debug fallback)", ss.splatFallback))
      lookChanged = true;
    gui.Text("Only effective in screen-space mode: the particle renderer never runs the splat resolve.");
  }

  if (lookChanged) RegisterParticleSystem();
  gui.EndPanel();
}

void Backyard8Scene::DrawWaterShadingPanel(t850::DevGuiContext& gui) {
  if (!gui.BeginPanel("Backyard 8 - Water Shading")) { gui.EndPanel(); return; }
  FluidPanelControls controls{gui};
  bool lookChanged = false;
  t850::Backyard8ScreenSpaceConfig& ss = m_config.screenSpace;

  // Both renderers end in the same water shade (CS_ParticleShade): the screen-space renderer as
  // Mode 1 over the smoothed depth, the particle renderer's water style as Mode 2 over the
  // surfel surface. Everything below is read by that shade in both modes.
  if (UsesWaterShade()) {
    gui.Text(UsesSurfels() ? "IN USE by the particle renderer (surfel splatting)"
                           : "IN USE by the screen-space renderer");
    gui.Text("The one water shade of both renderers: splat, optics, lighting, whitewater, debug.");
  } else {
    gui.Text("NOT IN USE: the particle renderer's current style does not use the water shade");
    const char* style = m_config.particles.blend == 0 ? "additive blend"
        : (m_config.particles.style == 1 ? "wireframe"
        : (m_config.particles.style == 2 ? "flat"
        : (m_config.particles.style == 3 ? "spheres" : "the water style without alpha blend")));
    char line[192];
    std::snprintf(line, sizeof(line), "  (drawing: %s)", style);
    gui.Text(line);
    // A debug style shades in its own kernel, but the spheres style still takes its optics
    // from these values (BuildParticleSphereConstants). The wireframe and flat styles read
    // none of them: CS_FluidSphereShade tints them from the colour ramp only.
    if (m_config.particles.blend == 1 && m_config.particles.style == 3) {
      gui.Text("The spheres style still reads these: sun specular, Fresnel exponent, thickness");
      gui.Text("scale, packing factor, absorption R/G/B, absorption multiplier, scatter R/G/B,");
      gui.Text("zenith sky R/G/B (the horizon colours are not read by it).");
    } else {
      gui.Text("This style reads nothing from this panel.");
    }
    gui.Text("The one water shade of both renderers: splat, optics, lighting, whitewater, debug.");
  }

  if (gui.BeginSection("Splat (depth and thickness)", true)) {
    // The stamp's projected radius (Projected.z = Radius * sizeScale * ...) sizes the splat
    // discs, the chord and the shade's normal stencil, so these three are water controls.
    lookChanged |= controls.slider("pbf8_radius", "render radius (m)", m_config.particles.radius,
                                   0.01f, 0.4f, 0.001f);
    // A buffer-source particle (the PBF fluid) is stamped at age 1, so only the age-1 scale
    // reaches it; the age-0 scale is inert for this fluid.
    lookChanged |= controls.slider("pbf8_size_min", "size scale at age 0 (unused: buffer age is 1)",
                                   m_config.particles.sizeScaleMin, 0.1f, 4.0f, 0.05f);
    lookChanged |= controls.slider("pbf8_size_max", "size scale at age 1", m_config.particles.sizeScaleMax,
                                   0.1f, 4.0f, 0.05f);
    gui.Text("Render radius and the age-1 size scale set every splat disc and the chord length.");
    if (controls.slider("pbf8_ss_depth_radius", "depth radius / spacing", ss.depthRadiusScale,
                        0.2f, 1.2f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_packing", "packing factor", ss.packingFactor, 1.0f, 8.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_thick", "thickness scale", ss.thicknessScale, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    // WO-P1: the thickness/whitewater sums run at half resolution on large windows.
    if (controls.combo("pbf8_ss_thick_ds", "thickness downsample",
                       {"auto (half on 1440p+)", "full (depth grid)", "half (half grid)"},
                       ss.thicknessDownsample))
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
    if (controls.slider("pbf8_ss_reflect_r", "zenith sky R", ss.reflection[0], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_reflect_g", "zenith sky G", ss.reflection[1], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_reflect_b", "zenith sky B", ss.reflection[2], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_horizon_r", "horizon sky R", ss.skyHorizon[0], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_horizon_g", "horizon sky G", ss.skyHorizon[1], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_horizon_b", "horizon sky B", ss.skyHorizon[2], 0.0f, 1.0f, 0.005f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_refract_strength", "refraction strength", ss.refractionStrength,
                        0.0f, 1.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_reflect_dist", "reflection range (m)", ss.reflectionDistance,
                        0.0f, 80.0f, 0.5f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_spec", "sun specular", ss.specular, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_glint", "sun glint exponent", ss.glintExponent, 16.0f, 4000.0f, 1.0f))
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

  if (gui.BeginSection("Surface detail", true)) {
    if (controls.slider("pbf8_ss_ripple_slope", "ripple slope", ss.rippleSlope, 0.0f, 0.2f, 0.001f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_ripple_wave", "ripple wavelength (m)", ss.rippleWavelength,
                        0.05f, 4.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_ripple_speed", "ripple speed", ss.rippleSpeed, 0.0f, 3.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_ripple_dist", "ripple fade distance (m)", ss.rippleDistance,
                        1.0f, 100.0f, 0.5f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_caustics", "caustics", ss.caustics, 0.0f, 3.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_caustic_scale", "caustic scale (m)", ss.causticScale, 0.1f, 4.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_caustic_speed", "caustic speed", ss.causticSpeed, 0.0f, 4.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ss_caustic_depth", "caustic depth fade (m)", ss.causticDepth,
                        0.1f, 6.0f, 0.05f))
      lookChanged = true;
  }

  if (gui.BeginSection("Scene lighting", true)) {
    gui.Text("Lighting the water shade takes from the scene:");
    if (controls.slider("pbf8_wwo_env", "environment intensity", ss.envIntensity, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_wwo_sun_atten", "sun attenuation (under water)", ss.sunAttenuation,
                        0.0f, 1.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_wwo_caustic_focus", "caustic focus", ss.causticFocus, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_wwo_contact", "contact line", ss.contactLine, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_wwo_backlight", "backlight", ss.backlight, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_wwo_scatter_sun", "in-scatter sun", ss.scatterSun, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_wwo_scatter_sky", "in-scatter sky", ss.scatterSky, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_wwo_scatter_aniso", "in-scatter anisotropy (g)", ss.scatterAnisotropy,
                        0.0f, 0.95f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_wwo_ripple_calm", "ripple calm", ss.rippleCalm, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_wwo_ripple_agg", "ripple agitation", ss.rippleAgitation, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
  }

  if (gui.BeginSection("Whitewater", true)) {
    t850::Backyard8WhitewaterConfig& ww = m_config.whitewater;
    gui.Text("Whitewater look (the solver fields below refresh the pool):");
    if (controls.slider("pbf8_ww_foam", "foam strength", ss.foamStrength, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_ww_foam_cell", "foam cell size (m)", ss.foamTextureScale,
                        0.05f, 2.0f, 0.01f))
      lookChanged = true;
    if (controls.slider("pbf8_ww_aeration", "aeration", ss.aeration, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_ww_bubbles", "bubbles", ss.bubbleStrength, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_ww_spray", "spray", ss.sprayStrength, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    if (controls.slider("pbf8_ww_wetness", "wetness", ss.wetness, 0.0f, 4.0f, 0.05f))
      lookChanged = true;
    gui.Separator();
    gui.Text("Solver emission rates, lifetimes and drying time:");
    bool solverChanged = false;
    if (controls.slider("pbf8_ww_spray_rate", "spray rate", ww.sprayRate, 0.0f, 60.0f, 0.5f))
      solverChanged = true;
    if (controls.slider("pbf8_ww_crest_rate", "crest rate", ww.crestRate, 0.0f, 60.0f, 0.5f))
      solverChanged = true;
    if (controls.slider("pbf8_ww_diffuse_life", "diffuse lifetime (s)", ww.diffuseLifetime,
                        0.1f, 10.0f, 0.1f))
      solverChanged = true;
    if (controls.slider("pbf8_ww_foam_gain", "foam gain", ww.foamGain, 0.0f, 10.0f, 0.1f))
      solverChanged = true;
    if (controls.slider("pbf8_ww_foam_life", "foam lifetime (s)", ww.foamLifetime, 0.1f, 10.0f, 0.1f))
      solverChanged = true;
    if (controls.slider("pbf8_ww_aer_gain", "aeration gain", ww.aerationGain, 0.0f, 10.0f, 0.1f))
      solverChanged = true;
    if (controls.slider("pbf8_ww_aer_life", "aeration lifetime (s)", ww.aerationLifetime,
                        0.1f, 10.0f, 0.1f))
      solverChanged = true;
    if (controls.slider("pbf8_ww_drying", "drying time (s)", ww.dryingTime, 1.0f, 300.0f, 1.0f))
      solverChanged = true;
    // The solver clamps maxDiffuse to 64..1<<20 and wetResolution to 16..2048; the sliders
    // stay inside those ranges so the GUI can never push a value the solver would clamp.
    int maxDiffuse = static_cast<int>(ww.maxDiffuse);
    if (controls.sliderInt("pbf8_ww_max_diffuse", "max secondary particles", maxDiffuse,
                           64.0f, 1048576.0f, 64.0f)) {
      ww.maxDiffuse = static_cast<uint32_t>(maxDiffuse);
      solverChanged = true;
    }
    int wetResolution = static_cast<int>(ww.wetResolution);
    if (controls.sliderInt("pbf8_ww_wet_res", "wetness map resolution", wetResolution,
                           16.0f, 2048.0f, 1.0f)) {
      ww.wetResolution = static_cast<uint32_t>(wetResolution);
      solverChanged = true;
    }
    if (controls.slider("pbf8_ww_wet_texel", "wetness texel (m)", ww.wetTexel, 0.01f, 0.5f,
                        0.005f))
      solverChanged = true;
    // The solver fields above recreate the solver's buffers when maxDiffuse, wetResolution
    // or wetTexel change; refresh the descriptor's pool capacity and wetness map size from
    // the new layout so the render graph keeps matching the solver.
    if (solverChanged) {
      m_pbf.SetWhitewater(WhitewaterSettings());
      RegisterParticleSystem();
    }
  }

  if (gui.BeginSection("Debug (water shade)", false)) {
    gui.Text("The water shade's debug views (both renderers):");
    gui.Text("  shaded / normals / surface depth / water path / coverage / reflection /");
    gui.Text("  refraction / whitewater / light under water / wetness");
    if (controls.combo("pbf8_ss_debug", "debug view",
                       {"shaded", "normals", "surface depth", "water path", "coverage",
                        "reflection", "refraction", "whitewater", "light under water",
                        "wetness"}, ss.debugView))
      lookChanged = true;
    gui.Separator();
    gui.Text("The particle renderer's wireframe/flat/sphere debug is a different path:");
    gui.Text("see 'Backyard 8 - Particles (Splat)' -> Splat debug.");
  }

  if (lookChanged) RegisterParticleSystem();
  gui.EndPanel();
}

void Backyard8Scene::DrawDevGui(t850::DevGuiContext& gui) {
  m_devGuiDrawnThisFrame = true;
  // Four panels, not one: the solver switch belongs with the solver; the two renderer panels
  // are exclusive (each says ACTIVE only when the combo selects it) because they render
  // differently; the water shade both renderers draw with has its own panel.
  DrawSolverPanel(gui);
  DrawParticlePanel(gui);
  DrawScreenSpacePanel(gui);
  DrawWaterShadingPanel(gui);
}
