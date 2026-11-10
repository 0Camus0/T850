#pragma once

// Backyard 8 fluid configuration.
//
// Everything the particle-fluid scene tunes lives in Scenes/Backyard8Fluid.json: the PBF
// solver, the seed, the particle look, and the screen-space fluid parameters. The scene
// loads this file before it creates the solver and registers the particle system, so there
// is no scene-specific constant hidden in C++.
//
// glaze is confined to Backyard8FluidJson.cpp for the same MSVC parser-limit reason as
// BackyardWaterJson.h: including it from a header that reaches the legacy DayScene
// translation unit fails with C3878.

#include <array>
#include <string>

namespace t850 {

  struct Backyard8SolverConfig {
    float particleRadius = 0.13f;
    float smoothingRadiusScale = 2.0f;
    // Rest target as a fraction of the cubic-lattice density. 0.907 is the relaxed packing
    // of the seeded fluid: a pool seeded at the water plane then settles on it, instead of
    // one particle layer low (measured -0.19 m at 1.0; see the Phase 1 notes).
    float restDensityScale = 0.907f;
    float stiffnessEps = 0.05f;
    float artPressureK = 0.0f;
    float artPressureN = 4.0f;
    float viscosityC = 0.15f;
    float gravity = 9.81f;
    float windSpeed = 8.0f;
    float windHeadingDeg = 0.0f;
    // Multiplier on physical air drag (1 = real air, near-still pool; ~20 for ripples).
    float windStrength = 1.0f;
    // No-slip boundary viscosity multiplier (1 = the fluid's XSPH coefficient). 8 damps
    // the wall layer enough to stop the persistent wall-driven circulation; measured
    // surface |v| 0.024 m/s and lake level -0.043 m.
    float wallFriction = 8.0f;
    float maxVelocity = 12.0f;
    int solverIterations = 3;
    int substeps = 2;
    float deltaTime = 1.0f / 60.0f;
    float sphereRadius = 2.0f;
    float sphereHeight = 3.08f;
    // Seeded pool surface, m relative to the water plane. The resting starts seed at the
    // plane (the 0.907 rest density settles the lattice there, so no compaction offset).
    // The water start seeds 0.27 m low: the pool settles first, then the 2 m sphere is
    // appended, and the part that stays in the pool brings it back to the plane (about
    // 10k of 15k sphere particles are thrown over the rim).
    float prefillLevel = -0.27f;
    float restingLevel = 0.0f;
    float cullBelowY = 0.0f;
    float cullMargin = 0.35f;
    int surfaceWidth = 100;
    int surfaceDepth = 50;
    int maxParticles = 50000;
  };

  struct Backyard8SceneConfig {
    int startMode = 2;               // 0 balls / 2 lake: resting pool at the plane; 1 water: pool + dropped sphere
    float fixedStep = 1.0f / 60.0f;  // simulation step
    int maxStepsPerFrame = 3;
  };

  struct Backyard8ParticleConfig {
    std::string name = "BackyardWater";
    float radius = 0.13f;
    float opacity = 0.80f;
    float intensity = 1.0f;
    float softness = 0.45f;
    float depthBias = 0.13f;
    int shape = 0;        // 0 soft sphere, 1 sprite, 2 streak
    int blend = 1;        // 0 additive, 1 alpha
    int style = 0;        // 0 water (blended), 1 wireframe, 2 flat, 3 lit spheres
    int renderMode = 1;   // 0 splat, 1 screen space
    float sizeScaleMin = 1.0f;
    float sizeScaleMax = 1.0f;
    std::string buffer = "BackyardWaterPositions";
    std::string velocityBuffer = "BackyardWaterVelocities";
    int count = 0;        // 0 = all solver particles; lower values draw a prefix
    std::array<std::array<float, 3>, 4> colors = {{
      {{0.30f, 0.62f, 0.85f}}, {{0.42f, 0.72f, 0.90f}},
      {{0.62f, 0.84f, 0.95f}}, {{0.88f, 0.95f, 1.00f}}
    }};
  };

  struct Backyard8ScreenSpaceConfig {
    int smoothingIterations = 6;      // 0..8 a-trous passes, tap spacing doubling each pass
    float smoothingStrength = 1.0f;   // blend toward the filtered depth
    float smoothingRadius = 64.0f;    // projected filter radius cap, pixels
    float smoothingWorldRadius = 0.3f;  // filter radius, metres
    float depthTolerance = 0.30f;     // metres; narrow-range layer threshold
    float depthRadiusScale = 1.0f;    // depth sphere radius / particle spacing
    float packingFactor = 4.19f;      // cubic-lattice sphere packing factor
    float thicknessScale = 1.0f;
    std::array<float, 3> absorption = {{0.45f, 0.06f, 0.02f}};
    float absorptionScale = 2.0f;
    std::array<float, 3> fluidColor = {{0.10f, 0.30f, 0.42f}};
    std::array<float, 3> reflection = {{0.45f, 0.62f, 0.85f}};
    float refraction = 0.5f;          // refracted path per metre of thickness
    float reflectionDistance = 40.0f; // screen-space reflection range, metres (0 = sky only)
    float specular = 1.0f;
    float fresnelPower = 5.0f;
    float normalScale = 1.0f;
    float normalStep = 1.0f;
    float foam = 0.6f;                // spray whitewater, 0..1
    bool splatFallback = false;       // keep the splat resolve under the SSFR layer
    int debugView = 0;                // 0 shaded, 1 normals, 2 depth, 3 thickness, 4 coverage
  };

  struct Backyard8FluidConfig {
    int version = 1;
    Backyard8SolverConfig solver;
    Backyard8SceneConfig scene;
    Backyard8ParticleConfig particles;
    Backyard8ScreenSpaceConfig screenSpace;
  };

  // Returns false only when the file exists and is malformed. A missing file leaves the
  // C++ defaults, which are the same values the shipped JSON contains.
  bool LoadBackyard8FluidConfig(const char* path, Backyard8FluidConfig& out, std::string& error);

}  // namespace t850
