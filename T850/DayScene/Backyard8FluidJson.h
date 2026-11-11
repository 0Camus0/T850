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
#include <cstdint>
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
    int solverIterations = 6;   // 2 substeps x 6 = 12 projections/frame; 1.27% mean density error
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
    // With 2x6 the lake settles at -0.044 m with this seed (the old 2x3 needed 0.0).
    float restingLevel = -0.02f;
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
    // Surfel splatting (the particle renderer's water style): the front layer of anisotropic
    // particles blended into depth and normal in one pass.
    float surfelBand = 1.0f;        // surfel band, in particle radii (see ParticleSystem.h)
    float surfelSharpness = 2.0f;   // exponent on the ellipsoid's centre weight
    float sizeScaleMin = 1.0f;
    float sizeScaleMax = 1.0f;
    std::string buffer = "BackyardWaterPositions";
    std::string velocityBuffer = "BackyardWaterVelocities";
    // Anisotropic particle shapes published by the solver for the screen-space depth splat.
    std::string anisotropyBuffer = "BackyardWaterAnisotropy";
    // Whitewater buffers the solver publishes for the screen-space renderer (the render graph
    // binds them by these names): per-particle foam/air, the secondary spray/foam/bubble pool,
    // the caustic focus field and the deck wetness map.
    std::string whitewaterBuffer = "BackyardWaterWhitewater";
    std::string diffuseBuffer = "BackyardWaterDiffuse";
    std::string causticBuffer = "BackyardWaterCaustics";
    std::string wetnessBuffer = "BackyardWaterWetness";
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
    std::array<float, 3> reflection = {{0.45f, 0.62f, 0.85f}};  // zenith sky for reflections
    std::array<float, 3> skyHorizon = {{0.85f, 0.80f, 0.74f}};  // horizon sky for reflections
    float refraction = 0.5f;          // splat water style: refracted path per metre of thickness
    float refractionStrength = 1.0f;  // SSFR: 0..1, 1 = Snell refraction through the water column
    float reflectionDistance = 40.0f; // screen-space reflection range, metres (0 = sky only)
    float specular = 1.0f;
    float glintExponent = 900.0f;     // sun glint sharpness
    float fresnelPower = 5.0f;
    float normalScale = 1.0f;
    float normalStep = 1.0f;
    float foam = 0.6f;                // spray whitewater, 0..1
    // Wind ripples on near-horizontal water (the particles resolve ~0.1 m; real pool water
    // carries centimetre-scale capillary/wind waves that make reflections sparkle).
    float rippleSlope = 0.05f;        // slope amplitude, 0 = mirror-flat
    float rippleWavelength = 0.9f;    // longest wavelength, metres
    float rippleSpeed = 0.6f;         // multiplier on deep-water wave speed
    float rippleDistance = 30.0f;     // metres over which ripples fade with distance
    // Caustics on the submerged floor/walls seen through the water.
    float caustics = 0.6f;            // strength, 0 = off
    float causticScale = 0.9f;        // pattern scale, metres
    float causticSpeed = 1.0f;
    float causticDepth = 1.6f;        // metres of water over which caustics fade
    bool splatFallback = false;       // keep the splat resolve under the SSFR layer
    // Anisotropic particles (Yu and Turk 2013): each particle is an ellipsoid shaped by its
    // neighbours, so free surfaces are flat and sheets are films instead of rows of domes.
    bool anisotropy = true;
    float anisotropySmoothing = 0.5f;       // 0..1, centre pulled toward the neighbour mean
    float anisotropyMaxRatio = 4.0f;        // longest / shortest axis
    float anisotropyRadius = 1.0f;          // interior particle radius, in spacings
    float anisotropySprayRadius = 0.5f;     // isolated particle radius, in spacings
    float anisotropyNeighbours = 14.0f;     // neighbours for the full anisotropic shape
    float anisotropySprayNeighbours = 5.0f; // at or below: an isolated droplet
    float anisotropyMaxStretch = 1.6f;      // longest axis cap, in interior radii
    // ---- Scene lighting (2026-11 realism pass) ----
    // Multiplier on the environment cube for reflected rays the screen trace cannot resolve.
    float envIntensity = 1.0f;
    // Sunlight absorbed on its way down to submerged surfaces (0 = lit as if in air).
    float sunAttenuation = 1.0f;
    // Caustic focusing by the simulated surface (0 = procedural pattern only).
    float causticFocus = 1.0f;
    // Brightening of the meniscus where the water meets a wall.
    float contactLine = 0.5f;
    // Thin crests and sheets glowing when seen toward the sun.
    float backlight = 1.0f;
    // In-scattering: sun and sky light scattered toward the eye inside the water.
    float scatterSun = 0.6f;
    float scatterSky = 0.8f;
    float scatterAnisotropy = 0.45f;   // Henyey-Greenstein g, 0 = isotropic
    // Ripple slope multipliers: still water, and the extra slope at full local agitation.
    float rippleCalm = 0.6f;
    float rippleAgitation = 1.5f;
    // ---- Whitewater (Ihmsen et al. 2012) ----
    float foamStrength = 1.0f;         // surface foam
    float foamTextureScale = 0.35f;    // foam lace cell size, metres
    float aeration = 1.0f;             // milky entrained air under the surface
    float bubbleStrength = 1.0f;       // bubble sparkles
    float sprayStrength = 1.0f;       // secondary spray droplets
    float wetness = 1.0f;              // darkening and sheen of wet deck/coping
    // 0 shaded, 1 normals, 2 depth, 3 water path, 4 coverage, 5 reflection, 6 refraction,
    // 7 whitewater (r foam, g aeration, b spray), 8 sun light under water, 9 wetness.
    int debugView = 0;
  };

  // Whitewater solver settings (Ihmsen et al. 2012): per-particle foam and entrained air,
  // a pool of secondary spray/foam/bubble particles, the caustic focus field and the
  // drying wetness map of the deck. Mirrors t850::PbfWhitewaterSettings field for field.
  struct Backyard8WhitewaterConfig {
    bool enabled = true;
    // Potentials are clamped to [min, max] and mapped to 0..1.
    float trappedAirMin = 3.0f;
    float trappedAirMax = 15.0f;
    float crestMin = 0.3f;
    float crestMax = 2.0f;
    float energyMin = 0.5f;
    float energyMax = 10.0f;
    float surfaceNeighbours = 24.0f;   // at or above: a submerged particle
    // Whitewater carried by the fluid.
    float foamGain = 3.0f;             // per second at full potential
    float foamLifetime = 1.5f;         // e-folding seconds
    float aerationGain = 2.5f;
    float aerationLifetime = 1.2f;
    // Secondary particles.
    uint32_t maxDiffuse = 65536;
    float sprayRate = 12.0f;           // per second per fluid particle at full trapped air
    float crestRate = 8.0f;            // per second per fluid particle at full crest potential
    float diffuseLifetime = 2.0f;      // seconds (randomised 0.5..1.5x)
    float bubbleBuoyancy = 3.0f;       // times gravity
    float bubbleDrag = 0.5f;           // fraction of the velocity difference removed per update
    float sprayDrag = 0.4f;            // per second
    // Wet deck.
    float dryingTime = 45.0f;          // e-folding seconds
    float wetTexel = 0.06f;           // metres
    uint32_t wetResolution = 320;     // texels per side, centred on the basin
    // Caustic focusing gain on the simulated surface curvature, (n - 1) / n for water.
    float causticGain = 0.25f;
  };

  struct Backyard8FluidConfig {
    int version = 1;
    Backyard8SolverConfig solver;
    Backyard8SceneConfig scene;
    Backyard8ParticleConfig particles;
    Backyard8ScreenSpaceConfig screenSpace;
    Backyard8WhitewaterConfig whitewater;
  };

  // Returns false only when the file exists and is malformed. A missing file leaves the
  // C++ defaults, which are the same values the shipped JSON contains.
  bool LoadBackyard8FluidConfig(const char* path, Backyard8FluidConfig& out, std::string& error);

}  // namespace t850
