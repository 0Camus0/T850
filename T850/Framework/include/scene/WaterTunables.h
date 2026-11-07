#pragma once

#include <utils/xMaths.h>
#include <utils/DeviceInfo.h>

#include <algorithm>
#include <cmath>

namespace t850 {

  // Every water constant that used to be a literal in the shader or in
  // FluidSettings, in one place so the dev GUI can drive it live.
  //
  // Two groups: solver values that the host feeds to FluidSimulation, and look
  // values that ride to the fragment shader in the material constant buffer.
  struct WaterTunables {    // ── Solver (host-side compute dispatches) ──
    int   gridWidth = 100;
    int   gridHeight = 18;
    int   gridDepth = 50;
    // Red-black Gauss-Seidel sweeps per substep. Conservation is tied to this number: the
    // transport clips alpha at 1 in cells that receive inflow while full, so residual
    // divergence destroys mass (measured on the still pool: +29.6 cells lost at 60 sweeps,
    // +4.4 at 120, +0.1 at 240). 120 costs ~0.9 ms of solver recording at 720p on D3D12
    // (397 -> 298 fps with nothing else running) and is the shipped setting; W-15 P2
    // (in-place red-black on R32F) is what makes 240 affordable later.
    int   pressureIterations = 120;
    // Mobile builds and phone or tablet browsers cap the sweep count: they pay the same per-sweep
    // cost as a desktop card for a much smaller frame budget. Measured on D3D12 at 60 sweeps (see
    // the staged plan, "Mobile and web defaults") the gates still pass, with the still-pool loss
    // moving 0.002 % -> 0.013 %, which is why 60 is the cap rather than 30. The surface is not
    // the same, though: at 60 the waves keep about a third of their height and every ball sits in
    // a dent, so a desktop browser keeps the desktop count.
    static constexpr int kMobilePressureIterations = 60;
    float cellSize = 0.09f;
    // Absolute world Y of the still-water surface (the plane the water mesh is
    // authored at), and the metres of air the solver keeps above it so a free
    // surface has room to rise. Both are overridden from the pool layout JSON,
    // which is the authority; these are only the fallback.
    float waterPlaneY = -0.04f;
    float freeboard = 0.42f;
    // The rows above the pool are air, not a lid. The solver's pressure solve then
    // vents at the ceiling instead of reflecting off it. Set false to close the box,
    // which is what the synthetic solver tests require.
    bool openCeiling = true;
    // Fixed step used only when fixedTimeStep is on. When it is off (the
    // default) the solver advances by the real frame delta so the water runs in
    // real time instead of at (frame rate / 60) times real speed.
    float deltaTime = 1.0f / 60.0f;
    // Longest real delta the solver will accept. W-31 turned this into a HITCH clamp only: the
    // substep count now comes from the measured flow (see adaptiveSubsteps below), so a slow frame
    // no longer has to be squeezed into 1/30 s. The old comment claimed the von Neumann bound
    // h^2/(6 nu) = 0.0675 s required it, but that bound is for the viscosity this build actually
    // ships (1e-6 m^2/s) at ~1350 s; 0.25 s only stops a stall (window drag, breakpoint, Android
    // resume) from handing the solver seconds of dt.
    float maxDeltaTime = 0.25f;
    // Multiplies elapsed time. 1.0 is real time; smaller makes the water lazy.
    float timeScale = 1.0f;
    // Deterministic fixed-step mode (for reproducible frame dumps and tests).
    bool fixedTimeStep = false;
    // Kinematic viscosity in m^2/s (water is 1e-6). Advection already adds far more
    // numerical diffusion at 9 cm cells, so this only matters when raised.
    float viscosity = 1.0e-6f;
    float gravity = 9.81f;
    // Wind speed in m/s; surface stress rho_air * 1.3e-3 * |U| U on the top wet layer (W-27).
    // windY exerts no tangential stress and is ignored by the solver.
    float windX = 0.6f;
    float windY = 0.0f;
    float windZ = -0.35f;
    // Velocity decay per second. 0.6 removed almost all of a wave's energy within
    // a second or two, so nothing could propagate far. Real water is lightly
    // damped; viscosity is what actually dissipates a pool's waves.
    float damping = 0.05f;
    float surfaceLevel = 0.96f;
    // ── Solver values promoted to tunables in W-11 (previously literals) ──
    // Ceiling on one substep. With adaptiveSubsteps on (the default since W-31) this is only the
    // upper bound; 1/120 was the fixed cadence that made devices run the water at 0.53x real time.
    float maxSubstepDt = 1.0f / 30.0f;
    // Size each substep from the measured (|u|+|v|+|w|) instead of a fixed worst case. Off
    // reproduces the pre-W-31 behaviour exactly.
    bool adaptiveSubsteps = true;
    // Stability target for the unsplit upwind alpha transport: cflNumber * h / uBound is the step.
    float cflNumber = 0.5f;
    // Floor on the velocity bound, m/s, so a settling pool does not ask for an enormous substep.
    float minCflSpeed = 0.5f;
    // Most substeps one frame may simulate. Past this the water runs slow rather than the frame
    // running long, which bounds the frame cost so there is no spiral of death.
    int maxSubstepsPerFrame = 4;
    // Interface sharpening for the flux-form alpha advection (0 = pure upwind,
    // which smears entrained air through the whole pool). Also a FluidSettings
    // literal before W-11.
    float alphaCompression = 0.5f;
    // Speed in m/s at which entrained air rises back to the surface (W-29). Without it a
    // splash leaves air pockets that the surface draws as frozen bumps.
    float airRiseSpeed = 0.25f;
    // Substeps between volume-correction passes. This was a hardcoded 15 inside
    // FluidSimulation with no setter at all before W-11.
    int volumeCheckInterval = 15;
    // SOR factor for the red-black Gauss-Seidel pressure sweep.
    float sorOmega = 1.8f;

    // ── Optics ──
    float absorbR = 0.38f;
    float absorbG = 0.17f;
    float absorbB = 0.10f;
    float thicknessScale = 5.0f;
    float shallowR = 0.09f;
    float shallowG = 0.34f;
    float shallowB = 0.33f;
    float deepR = 0.012f;
    float deepG = 0.10f;
    float deepB = 0.17f;
    float refractionStrength = 0.025f;
    // Width of the border fade zone in UV units (fraction of the pool span).
    float refractionBorderFade = 1.0f / 30.0f;
    float causticGain = 1.0f;
    float causticPower = 2.0f;
    float causticSlopeScale = 2.0f;
    float causticFloorBase = 0.45f;
    float fresnelBoost = 2.2f;
    float fresnelBias = 0.06f;
    float scatterWrap = 0.05f;
    float scatterNormal = 0.03f;
    float scatterDepth = 0.45f;
    float waterLineY = -0.04f;
    // Fallback depth when no floor-height texture is bound. It is one constant, so
    // it is only correct at the deep end (1.56 m); the scene binds a per-column
    // floor texture and the shader prefers it. Kept so a scene can opt out.
    float waterToFloor = 1.56f;
    // Set by BackyardScene once the per-column floor texture is bound at t24.
    bool floorTextureBound = false;

    // ── Surface detail ──
    float analyticWaveScale = 1.0f;
    float analyticWaveSpeed = 1.0f;
    float normalMapGain = 0.65f;
    float vertexWaveScale = 1.0f;
    float normalTileX = 18.0f;
    float normalTileZ = 9.0f;
    float normalDriftX = 0.021f;
    float normalDriftZ = 0.013f;
    float normalTile2X = 24.0f;
    float normalTile2Z = 12.0f;
    float normalDrift2X = -0.017f;
    float normalDrift2Z = 0.029f;

    // ── Reflection ──
    float iblReflectionScale = 1.0f;
    float directLightScale = 1.6f;
    float sunGlintPower = 400.0f;
    float sunGlintScale = 5.0f;
    float ssrWeight = 0.85f;
    float ssrDistortion = 0.006f;
    float ssrBlurBase = 1.5f;
    float ssrBlurRange = 10.0f;
    float ssrMaxDistance = 26.0f;
    float ssrStepStart = 0.05f;
    float ssrStepGrowth = 1.30f;
    float ssrStepMax = 1.0f;
    int   ssrSteps = 48;

    // ── Foam ──
    float foamCrest = 0.6f;
    float foamBorder = 2.0f;
    float foamNoise = 1.1f;
    float foamStrength = 0.7f;
    float foamR = 0.74f;
    float foamG = 0.82f;
    float foamB = 0.84f;

    // ── Ripple octaves (amplitude m, wavelength m, speed, distance fade m) ──
    float rippleAmp[6] = { 0.0160f, 0.0090f, 0.0060f, 0.0036f, 0.0022f, 0.0014f };
    float rippleLen[6] = { 0.62f, 0.41f, 0.27f, 0.19f, 0.14f, 0.11f };
    float rippleSpeed[6] = { 0.55f, 0.75f, 1.05f, 1.35f, 1.70f, 2.10f };
    float rippleFade[6] = { 60.0f, 48.0f, 36.0f, 27.0f, 20.0f, 15.0f };

    // ── Foam noise frequencies (rad/m) and its distance fade ──
    float foamFreq0X = 8.0f;
    float foamFreq0Y = 9.5f;
    float foamFreq1X = 21.0f;
    float foamFreq1Y = 19.0f;
    float foamFreq2X = 47.0f;
    float foamFreq2Y = 43.0f;
    float foamFade = 22.0f;

    // ── Feature toggles ──
    bool normalMapEnabled = true;
    bool ssrEnabled = true;

    // ── Presentation gains ──
    // A calm 1.6 m pool yields only a few millimetres of surface variation, which
    // at 6-13 m is under one pixel (one pixel is about 1.3 mm there). The solver
    // stays physical; this scales the presentation so what it computed is legible.
    // Set to 1.0 for a physically exact surface.
    // Metres of vertex displacement per metre of simulated surface elevation.
    // 1.0 is physically exact: R is the true delta from the authored water plane,
    // so a 0.25 m cavity depresses the mesh by 0.25 m. It used to be 12 to make a
    // sub-millimetre analytic ripple visible; once the surface came from the real
    // solver that amplification turned every feature into a crater.
    float displacementScale = 1.0f;
    // Artistic gain on the shading normal. The geometric slope conversion is
    // handled in the shader from cellSize; this only exaggerates the tilt.
    float normalStrength = 1.0f;
    // Draw the procedural ripple octaves and the tiling normal map on top of the
    // simulated surface. Off means the water is driven purely by the solver.
    bool fakeRipplesEnabled = false;
    // Sustained forcing amplitude so the pool is not perfectly flat at rest.
    // A one-shot seed decays within seconds; without continuous forcing the
    // solver correctly produces a motionless pool.
    // Sustained surface forcing gain. This drove a synthetic sin/cos stir on the
    // surface layer so the pool would not decay to flat. Now that the solver has a
    // real driving event (the dropped body) and real gravity waves, leaving it on
    // drowns the physics in a synthetic pattern, which is exactly what made the
    // water look like rolling sine waves. Off by default.
    float forcingAmplitude = 0.0f;

    // ── Dropped sphere ──
    // A ballistic body released above the surface, so the water reacts to a real
    // event instead of only ever showing its own equilibrium.
    bool  sphereEnabled = true;
    float sphereRadius = 0.25f;
    // Density of the dropped body relative to water. Below 1 floats, above 1 sinks.
    // This, not mass, decides the floating depth: equilibrium submerged fraction
    // equals this value.
    float sphereDensityRatio = 0.6f;
    // Drag coefficient of a smooth sphere, and the bounce at the floor and walls.
    float sphereDragCoefficient = 0.47f;
    // W-30: air drag on the cap above the waterline, so the wind pushes a floating body. 0
    // disables it and restores the pre-W-30 behaviour exactly; the default gives the classic
    // leeway of ~2.8 % of the wind speed for a body of density 0.6.
    float sphereAirDragCoefficient = 0.47f;
    float sphereRestitution = 0.30f;
    // Heave damping ratio standing in for wave radiation (the solver is one-way coupled).
    float sphereHeaveDamping = 0.15f;
    // Water density used to turn density ratio into a mass in kg. Mass is DERIVED
    // from radius and density; editing the mass slider back-solves the density so
    // the two can never disagree.
    float sphereWaterDensity = 1000.0f;
    // When on, the mass slider is shown and drives density instead of the other way.
    bool  sphereMassOverride = false;
    float sphereDropHeight = 1.0f;
    float sphereDropX = 0.0f;
    float sphereDropZ = -6.75f;
    float sphereInitialVY = 0.0f;
    float sphereInitialVX = 0.0f;
    float sphereInitialVZ = 0.0f;
    // Seconds after scene start before the sphere is released.
    float sphereReleaseDelay = 0.0f;

    // ── Spawned balls (W-22) ──
    // A GUI button drops an extra ball at a random place that always lands in the pool.
    // The scripted ball above is body 0 and keeps its own parameters; these describe the
    // extras only. There is no limit on how many can be spawned.
    float spawnRadiusMin = 0.10f;            // clamped up to 1.2 x cellSize by the scene
    float spawnRadiusMax = 0.35f;
    float spawnDensityMin = 0.2f;            // below 1 floats, above 1 sinks
    float spawnDensityMax = 1.6f;
    float spawnHeightMin = 0.5f;             // metres above the water plane
    float spawnHeightMax = 2.0f;
    // 0 = random each run; non-zero makes a spawn sequence reproducible.
    int   spawnSeed = 0;

    // Mass in kg, derived from radius and density: a body's mass is a property of
    // its size and material, so radius + density is the consistent parameterisation
    // and mass is what falls out of it.
    float SphereMassKg() const {
      const float r = (std::max)(sphereRadius, 0.001f);
      return sphereDensityRatio * sphereWaterDensity * (4.0f / 3.0f) * 3.14159265358979f * r * r * r;
    }
    // Inverse of SphereMassKg, used when the mass slider is edited.
    float SphereDensityFromMass(float massKg) const {
      const float r = (std::max)(sphereRadius, 0.001f);
      const float volume = (4.0f / 3.0f) * 3.14159265358979f * r * r * r;
      const float density = massKg / (sphereWaterDensity * (std::max)(volume, 1e-6f));
      return (std::min)((std::max)(density, 0.02f), 20.0f);
    }

    // Sweeps the scene and the gates should actually run. Android caps the configured value; a
    // browser caps it unless the page classified the device as a PC (an unknown device is
    // capped); the Steam Deck is capped at run time, because it is a Linux build and a
    // compile-time `OS_LINUX` branch would also cap a desktop Linux session. One helper so the
    // headless gates validate the configuration that ships on every platform.
    int EffectivePressureIterations() const {
      const int configured = (std::max)(pressureIterations, 1);
      return CapsPressureIterations() ? (std::min)(configured, kMobilePressureIterations) : configured;
    }

    // Why that number is lower than the configured one, for the log line. Empty when it is not.
    const char* PressureIterationsCapReason() const {
      if (!CapsPressureIterations() || (std::max)(pressureIterations, 1) <= kMobilePressureIterations) return "";
#if defined(OS_ANDROID)
      return "mobile cap";
#elif defined(__EMSCRIPTEN__)
      return GetDeviceInfo().webDevice.empty() ? "web cap, device unknown" : "phone/tablet browser cap";
#else
      return "Steam Deck cap";
#endif
    }

    static bool CapsPressureIterations() {
#if defined(OS_ANDROID)
      return true;
#elif defined(__EMSCRIPTEN__)
      return GetDeviceInfo().webDevice != "pc";
#else
      return GetDeviceInfo().steamDeck;
#endif
    }
  };

  // Mirrors the tail of MeshMaterialCBuffer and the HLSL MeshMaterialCB exactly.
  // Twenty-four float4 groups; field names are identical on both sides so the layout
  // is checkable by eye.
  struct WaterGpuParams {
    XVECTOR3 WaterOptics0;
    XVECTOR3 WaterOptics1;
    XVECTOR3 WaterOptics2;
    XVECTOR3 WaterOptics3;
    XVECTOR3 WaterOptics4;
    XVECTOR3 WaterOptics5;
    XVECTOR3 WaterWaves0;
    XVECTOR3 WaterWaves1;
    XVECTOR3 WaterWaves2;
    XVECTOR3 WaterReflect0;
    XVECTOR3 WaterReflect1;
    XVECTOR3 WaterReflect2;
    XVECTOR3 WaterFoam0;
    XVECTOR3 WaterFoam1;
    XVECTOR3 WaterFoam2;
    XVECTOR3 WaterFoam3;
    XVECTOR3 WaterRipple0;
    XVECTOR3 WaterRipple1;
    XVECTOR3 WaterRipple2;
    XVECTOR3 WaterRipple3;
    XVECTOR3 WaterRipple4;
    XVECTOR3 WaterRipple5;
    XVECTOR3 WaterMisc0;
    XVECTOR3 WaterMisc1;
  };

  inline WaterGpuParams BuildWaterGpu(const WaterTunables& w) {
    WaterGpuParams g;
    g.WaterOptics0 = XVECTOR3(w.absorbR, w.absorbG, w.absorbB, w.thicknessScale);
    g.WaterOptics1 = XVECTOR3(w.shallowR, w.shallowG, w.shallowB, w.refractionStrength);
    g.WaterOptics2 = XVECTOR3(w.deepR, w.deepG, w.deepB, w.refractionBorderFade);
    g.WaterOptics3 = XVECTOR3(w.causticGain, w.causticPower, w.causticSlopeScale, w.causticFloorBase);
    g.WaterOptics4 = XVECTOR3(w.fresnelBoost, w.fresnelBias, w.scatterWrap, w.scatterNormal);
    g.WaterOptics5 = XVECTOR3(w.scatterDepth, w.waterToFloor, w.waterLineY,
                              w.floorTextureBound ? 1.0f : 0.0f);
    g.WaterWaves0 = XVECTOR3(w.analyticWaveScale, w.analyticWaveSpeed, w.normalMapGain, w.vertexWaveScale);
    g.WaterWaves1 = XVECTOR3(w.normalTileX, w.normalTileZ, w.normalDriftX, w.normalDriftZ);
    g.WaterWaves2 = XVECTOR3(w.normalTile2X, w.normalTile2Z, w.normalDrift2X, w.normalDrift2Z);
    g.WaterReflect0 = XVECTOR3(w.iblReflectionScale, w.directLightScale, w.sunGlintPower, w.sunGlintScale);
    g.WaterReflect1 = XVECTOR3(w.ssrWeight, w.ssrDistortion, w.ssrBlurBase, w.ssrBlurRange);
    g.WaterReflect2 = XVECTOR3(w.ssrMaxDistance, w.ssrStepStart, w.ssrStepGrowth, w.ssrStepMax);
    g.WaterFoam0 = XVECTOR3(w.foamCrest, w.foamBorder, w.foamNoise, w.foamStrength);
    g.WaterFoam1 = XVECTOR3(w.foamR, w.foamG, w.foamB, 0.0f);
    g.WaterFoam2 = XVECTOR3(w.foamFreq0X, w.foamFreq0Y, w.foamFreq1X, w.foamFreq1Y);
    g.WaterFoam3 = XVECTOR3(w.foamFreq2X, w.foamFreq2Y, w.foamFade, 0.0f);
    g.WaterRipple0 = XVECTOR3(w.rippleAmp[0], w.rippleLen[0], w.rippleSpeed[0], w.rippleFade[0]);
    g.WaterRipple1 = XVECTOR3(w.rippleAmp[1], w.rippleLen[1], w.rippleSpeed[1], w.rippleFade[1]);
    g.WaterRipple2 = XVECTOR3(w.rippleAmp[2], w.rippleLen[2], w.rippleSpeed[2], w.rippleFade[2]);
    g.WaterRipple3 = XVECTOR3(w.rippleAmp[3], w.rippleLen[3], w.rippleSpeed[3], w.rippleFade[3]);
    g.WaterRipple4 = XVECTOR3(w.rippleAmp[4], w.rippleLen[4], w.rippleSpeed[4], w.rippleFade[4]);
    g.WaterRipple5 = XVECTOR3(w.rippleAmp[5], w.rippleLen[5], w.rippleSpeed[5], w.rippleFade[5]);
    g.WaterMisc0 = XVECTOR3(static_cast<float>(w.ssrSteps),
                            w.normalMapEnabled ? 1.0f : 0.0f,
                            w.ssrEnabled ? 1.0f : 0.0f,
                            w.fakeRipplesEnabled ? 1.0f : 0.0f);
    g.WaterMisc1 = XVECTOR3(w.displacementScale, w.normalStrength, w.cellSize, w.forcingAmplitude);
    return g;
  }

} // namespace t850
