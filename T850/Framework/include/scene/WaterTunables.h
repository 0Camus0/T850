#pragma once

#include <utils/xMaths.h>

namespace t850 {

  // Every water constant that used to be a literal in the shader or in
  // FluidSettings, in one place so the dev GUI can drive it live.
  //
  // Two groups: solver values that the host feeds to FluidSimulation, and look
  // values that ride to the fragment shader in the material constant buffer.
  struct WaterTunables {
    // ── Solver (host-side compute dispatches) ──
    int   gridWidth = 100;
    int   gridHeight = 18;
    int   gridDepth = 50;
    int   pressureIterations = 30;
    float cellSize = 0.09f;
    float deltaTime = 1.0f / 60.0f;
    float viscosity = 0.02f;
    float gravity = 9.81f;
    float windX = 0.6f;
    float windY = 0.0f;
    float windZ = -0.35f;
    float damping = 0.6f;
    float surfaceLevel = 0.96f;

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
    float refractionBorderFade = 30.0f;
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
    float waterToFloor = 1.56f;

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
  };

  // Mirrors the tail of MeshMaterialCBuffer and the HLSL MeshMaterialCB exactly.
  // Fifteen float4 groups; field names are identical on both sides so the layout
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
    XVECTOR3 WaterMisc0;
  };

  inline WaterGpuParams BuildWaterGpu(const WaterTunables& w) {
    WaterGpuParams g;
    g.WaterOptics0 = XVECTOR3(w.absorbR, w.absorbG, w.absorbB, w.thicknessScale);
    g.WaterOptics1 = XVECTOR3(w.shallowR, w.shallowG, w.shallowB, w.refractionStrength);
    g.WaterOptics2 = XVECTOR3(w.deepR, w.deepG, w.deepB, w.refractionBorderFade);
    g.WaterOptics3 = XVECTOR3(w.causticGain, w.causticPower, w.causticSlopeScale, w.causticFloorBase);
    g.WaterOptics4 = XVECTOR3(w.fresnelBoost, w.fresnelBias, w.scatterWrap, w.scatterNormal);
    g.WaterOptics5 = XVECTOR3(w.scatterDepth, w.waterToFloor, w.waterLineY, 0.0f);
    g.WaterWaves0 = XVECTOR3(w.analyticWaveScale, w.analyticWaveSpeed, w.normalMapGain, w.vertexWaveScale);
    g.WaterWaves1 = XVECTOR3(w.normalTileX, w.normalTileZ, w.normalDriftX, w.normalDriftZ);
    g.WaterWaves2 = XVECTOR3(w.normalTile2X, w.normalTile2Z, w.normalDrift2X, w.normalDrift2Z);
    g.WaterReflect0 = XVECTOR3(w.iblReflectionScale, w.directLightScale, w.sunGlintPower, w.sunGlintScale);
    g.WaterReflect1 = XVECTOR3(w.ssrWeight, w.ssrDistortion, w.ssrBlurBase, w.ssrBlurRange);
    g.WaterReflect2 = XVECTOR3(w.ssrMaxDistance, w.ssrStepStart, w.ssrStepGrowth, w.ssrStepMax);
    g.WaterFoam0 = XVECTOR3(w.foamCrest, w.foamBorder, w.foamNoise, w.foamStrength);
    g.WaterFoam1 = XVECTOR3(w.foamR, w.foamG, w.foamB, 0.0f);
    g.WaterMisc0 = XVECTOR3(static_cast<float>(w.ssrSteps), 0.0f, 0.0f, 0.0f);
    return g;
  }

} // namespace t850
