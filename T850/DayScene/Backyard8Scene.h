#pragma once

// Backyard8: the Backyard scene (index 7) running on a particle fluid instead of the
// Eulerian VOF solver.
//
// Everything shared - asset loading, cameras, the ball body model, picking, wind, the
// quick bar, the Dev GUI and the render graph - is inherited from BackyardScene. This
// scene overrides only the fluid lifecycle: TryInitialize, Step, Reset and the surface
// binding. See simulation/PbfFluid.h for the solver.
//
// Backyard (7) is untouched and keeps using FluidSimulation, so the two are directly
// comparable via --scene 7 / --scene 8.
//
// All tuning comes from Scenes/Backyard8Fluid.json (Backyard8FluidJson.h). Command-line
// flags override the file; the Dev GUI edits the live values.

#include <BackyardScene.h>
#include <Backyard8FluidJson.h>
#include <scene/ParticleSystem.h>
#include <simulation/PbfFluid.h>

#include <string>

class Backyard8Scene final : public BackyardScene {
public:
  Backyard8Scene();
  ~Backyard8Scene() override;

  // Startup loads the first scene through InitVars()/CreateAssets() directly (App::InitVars),
  // and later transitions go through OnLoadScene(). Both paths must start the particle
  // solver, so the hook is CreateAssets and InitPbf is idempotent.
  void CreateAssets() override;
  void OnLoadScene() override;
  void OnDestoryScene() override;
  void OnUpdate(float deltaSeconds) override;
  void DrawDevGui(t850::DevGuiContext& gui) override;
  // Scene 8's opening is the WATER sphere. The scripted ball is a solid mesh, so drawing it
  // put an opaque ball where the water should be.
  bool StartModeUsesBall() const override { return false; }
  // This scene runs the particle solver, not the Eulerian one.
  bool UsesVofFluid() const override { return false; }

  // --backyard-start water|lake|balls, shared with Backyard 7's flag.
  static void SetStartMode(int mode) { s_startMode = mode; }
  // --pbf-iterations N / --pbf-substeps N / --pbf-radius M
  static void SetSolverIterations(int n) { s_solverIterations = (std::max)(n, 1); }
  static void SetSubsteps(int n) { s_substeps = (std::max)(n, 1); }
  static void SetParticleRadius(double r) { s_particleRadius = r; }
  // Retained for command-line compatibility; the particle scene has no overflow height.
  static void SetOverflowHeight(double) {}
  static void SetMassAuditFrames(int n) { s_diagFrames = (std::max)(n, 0); }
  static void SetArtPressure(double k, double n) { s_artK = k; if (n >= 0.0) s_artN = n; }
  static void SetViscosity(double c) { s_viscosity = c; }
  static void SetMaxVelocity(double v) { s_maxVelocity = v; }
  static void SetGravity(double g) { s_gravity = g; }
  // --pbf-wind <speed> <headingDegrees>: surface wind, heading measured from +X toward +Z.
  static void SetWind(double speed, double headingDeg);
  // --fluid-render splat|screen-space. -1 keeps the config file's value.
  static void SetRenderMode(int mode) { s_renderMode = mode; }
  // --fluid-config <path>.
  static void SetConfigPath(const char* path) { if (path && *path) s_configPath = path; }

private:
  bool LoadFluidConfig();
  bool InitPbf();
  bool ReinitializePbf();
  void BuildSettings(t850::PbfSettings& out) const;
  void BuildParticleDesc();
  void RegisterParticleSystem();
  void ApplyStartMode();
  void DrawFluidPanel(t850::DevGuiContext& gui);

  static int s_startMode;
  static int s_solverIterations;
  static int s_substeps;
  static double s_particleRadius;
  static int s_diagFrames;
  static double s_artK, s_artN, s_viscosity, s_maxVelocity, s_gravity;
  static double s_windSpeed, s_windHeading;
  static int s_renderMode;
  static const char* s_configPath;

  t850::Backyard8FluidConfig m_config;
  bool m_configLoaded = false;
  t850::ParticleSystemDesc m_particleDesc;
  t850::PbfFluid m_pbf;
  t850::PbfFluid::Diagnostics m_lastDiag;
  bool m_diagValid = false;
  int m_diagCounter = 0;
  // Fixed-timestep accumulator. A variable dt from a hitch is larger than the position
  // projection can absorb; clamp and step from the config.
  float m_pbfAccumulator = 0.0f;
};
