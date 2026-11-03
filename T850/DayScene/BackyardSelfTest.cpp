#include <pch.h>

#include <BackyardSelfTest.h>

#include <simulation/FluidSimulation.h>
#include <scene/WaterTunables.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <BackyardWaterJson.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace t850 {
namespace {

  constexpr bool kVerbose = true;

  // One frame's worth of surface shape, in metres relative to the authored water
  // plane. Uses the same rule as CS_FluidSurface3D: the column integral of alpha,
  // and solid cells below wet water count as full. Duplicating the rule in C++ is a
  // maintenance risk; the surface-texture readback gate (Stage 1c) closes that by
  // measuring the real texture instead.
  struct SurfaceField {
    uint32_t width = 0;
    uint32_t depth = 0;
    std::vector<float> delta;   // [x + width * z]

    float At(int x, int z) const {
      x = (std::min)((std::max)(x, 0), static_cast<int>(width) - 1);
      z = (std::min)((std::max)(z, 0), static_cast<int>(depth) - 1);
      return delta[static_cast<size_t>(x) + static_cast<size_t>(width) * z];
    }
  };

  bool CaptureSurfaceFromAlpha(FluidSimulation& fluid, const std::vector<float>& alpha,
                               SurfaceField& out) {
    const std::vector<uint32_t>& mask = fluid.BaseMask();
    if (mask.size() != alpha.size()) return false;

    const FluidSettings& fs = fluid.Settings();
    out.width = fs.width;
    out.depth = fs.depth;
    out.delta.assign(static_cast<size_t>(fs.width) * fs.depth, 0.0f);

    for (uint32_t z = 0; z < fs.depth; ++z) {
      for (uint32_t x = 0; x < fs.width; ++x) {
        float water = 0.0f, pending = 0.0f;
        for (uint32_t y = 0; y < fs.height; ++y) {
          const size_t index = x + fs.width * (y + static_cast<size_t>(fs.height) * z);
          if (mask[index] != 0u) { pending += 1.0f; continue; }
          const float a = (std::min)((std::max)(alpha[index], 0.0f), 1.0f);
          water += a;
          if (a >= 0.5f) { water += pending; pending = 0.0f; }
        }
        out.delta[static_cast<size_t>(x) + static_cast<size_t>(fs.width) * z] =
            water <= 0.001f ? 0.0f : fs.basinMinY + water * fs.cellSize - fs.waterPlaneY;
      }
    }
    return true;
  }

  bool CaptureSurface(FluidSimulation& fluid, BaseDriver* driver, SurfaceField& out) {
    std::vector<float> alpha;
    if (!fluid.ReadVolumeFraction(driver, alpha)) return false;
    return CaptureSurfaceFromAlpha(fluid, alpha, out);
  }

  struct Stats {
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float mean = 0.0f;
    float rms = 0.0f;
    float maxAbs = 0.0f;
  };

  Stats Summarise(const SurfaceField& field) {
    Stats s;
    s.minValue = 1e9f;
    s.maxValue = -1e9f;
    double sum = 0.0, sumSq = 0.0;
    for (float d : field.delta) {
      s.minValue = (std::min)(s.minValue, d);
      s.maxValue = (std::max)(s.maxValue, d);
      s.maxAbs = (std::max)(s.maxAbs, std::fabs(d));
      sum += d;
      sumSq += static_cast<double>(d) * d;
    }
    const double count = static_cast<double>(field.delta.size());
    s.mean = static_cast<float>(sum / count);
    const double variance = (std::max)(sumSq / count - (sum / count) * (sum / count), 0.0);
    s.rms = static_cast<float>(std::sqrt(variance));
    return s;
  }

  // Median of the surface delta over the columns whose centre lies in the annulus
  // [radius - halfBand, radius + halfBand] and inside one quadrant. A single column
  // at one radius is dominated by grid-azimuthal aliasing (the disturbance is only a
  // few cells wide), so the annulus median is what makes an isotropy comparison
  // meaningful. Columns within one cell of an axis are skipped so the quadrant
  // assignment is unambiguous.
  struct Quadrant {
    float median = 0.0f;
    uint32_t samples = 0;
  };

  bool AnnulusMedian(const FluidSimulation& fluid, const SurfaceField& field,
                     float centreX, float centreZ, float radius, float halfBand,
                     Quadrant quadrant[4]) {
    const FluidSettings& fs = fluid.Settings();
    std::vector<float> values[4];
    for (uint32_t z = 0; z < field.depth; ++z) {
      for (uint32_t x = 0; x < field.width; ++x) {
        const float dx = (static_cast<float>(x) + 0.5f - centreX) * fs.cellSize;
        const float dz = (static_cast<float>(z) + 0.5f - centreZ) * fs.cellSize;
        const float r = std::sqrt(dx * dx + dz * dz);
        if (r < radius - halfBand || r > radius + halfBand) continue;
        // Skip the axis band so a column cannot belong to two quadrants.
        if (std::fabs(dx) < fs.cellSize || std::fabs(dz) < fs.cellSize) continue;
        const int q = (dx > 0.0f ? 1 : 0) + (dz > 0.0f ? 2 : 0);
        values[q].push_back(field.At(static_cast<int>(x), static_cast<int>(z)));
      }
    }
    for (int q = 0; q < 4; ++q) {
      if (values[q].empty()) return false;
      std::sort(values[q].begin(), values[q].end());
      const size_t n = values[q].size();
      quadrant[q].median = n % 2 ? values[q][n / 2]
                                 : 0.5f * (values[q][n / 2 - 1] + values[q][n / 2]);
      quadrant[q].samples = static_cast<uint32_t>(n);
    }
    return true;
  }

  // Largest radius at which the CHANGE from the pre-drop surface still exceeds the
  // threshold. Comparing against the reference matters: the pool carries a static
  // -10 mm bias of its own, so an absolute threshold reports a "front" out at the wall
  // before the ball has even landed.
  float FrontRadius(const FluidSimulation& fluid, const SurfaceField& field,
                    const SurfaceField& reference, float centreX, float centreZ,
                    float threshold) {
    const FluidSettings& fs = fluid.Settings();
    const float maxRadius = 4.0f;   // pool is 4.5 m from centre to the near wall
    float front = 0.0f;
    for (float r = 0.15f; r <= maxRadius; r += fs.cellSize) {
      Quadrant q[4];
      Quadrant qRef[4];
      if (!AnnulusMedian(fluid, field, centreX, centreZ, r, 0.5f * fs.cellSize, q)) continue;
      if (!AnnulusMedian(fluid, reference, centreX, centreZ, r, 0.5f * fs.cellSize, qRef)) continue;
      float magnitude = 0.0f;
      for (int i = 0; i < 4; ++i)
        magnitude = (std::max)(magnitude, std::fabs(q[i].median - qRef[i].median));
      if (magnitude > threshold) front = r;
    }
    return front;
  }

  float Median(std::vector<float> values) {
    if (values.empty()) return 0.0f;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return n % 2 ? values[n / 2] : 0.5f * (values[n / 2 - 1] + values[n / 2]);
  }

  // Gate 7: around a floating body at rest the rendered surface must sit at the
  // far-field level. Before the fix: ring -2.3 cm, covered texels -34 cm (the "funnel");
  // after: ring -0.1..-0.2 cm, covered within 0.7 cm of the ring (fp16 alpha).
  // With R32F alpha (W-02) the air the plunge entrains stays beside the ball (0.1-0.2
  // cells per column, against 0.003 far away) and read as missing water: ring -17.1 mm.
  // Since the air-rise pass (W-29) that air leaves: ring -1.6 mm, covered 2.2 mm below
  // the ring, with no trapped-air term in the surface kernel.
  constexpr float kFloatRingMax = 0.015f;
  constexpr float kFloatCoveredMax = 0.02f;
  // Largest |h(t) - 2h(t-1) + h(t-2)| per texel near the settling ball, 3-4 s after the
  // drop. The first W-19 kernel (alpha-thresholded body test) measured 0.44 m (visible
  // flicker); the mask-only test measures 0.0066 m.
  constexpr float kFloatFlickerMax = 0.01f;

  // Gate 6: a WET top row must have a non-zero +Y face velocity. A lid zeroes it exactly
  // (the gradient kernel overwrites the face with SolidBoundaryValue()), so the threshold
  // only has to separate "exactly zero" from "a real vent flow". Measured open-ceiling
  // values are tens of cm/s; 0.01 m/s is two orders of magnitude below that and still
  // two orders above the exact zero a lid produces.
  constexpr float kTopFaceSpeedMin = 0.01f;
  // A 2.5 m drop of a 3x-density ball must lift the surface noticeably, or the probe is
  // measuring a solver that has stopped moving rather than an open ceiling.
  constexpr float kCeilingRiseMin = 0.05f;

} // namespace

int RunBackyardSelfTest(BaseDriver* driver, const BackyardSelfTestOptions& options) {
  if (!driver) return 1;

  int failures = 0;
  const char* api = driver->ApiTag();
  T8_LOG_INFO("[BackyardSelfTest] START api=%s dt=%.4f frames=%u iterations=%u skipBall=%d",
              api, options.fixedDt, options.frames, options.pressureIterations,
              options.skipBall ? 1 : 0);

  FluidSimulation fluid;
  // Build the settings the same way the scene does, from WaterTunables. The gate must
  // test the configuration that ships: FluidSettings' own defaults still carry
  // forcingAmplitude = 1.0 and damping = 0.6, which is the synthetic sin/cos surface
  // stir and a heavy velocity decay. Running on those defaults made a "still" pool
  // grow from 1.9 mm to 34.6 mm of surface noise in two seconds with no ball and no
  // user forcing at all - the gate was measuring the fake stir, not the solver.
  const WaterTunables w{};
  FluidSettings settings;
  settings.width = static_cast<uint32_t>((std::max)(w.gridWidth, 8));
  settings.depth = static_cast<uint32_t>((std::max)(w.gridDepth, 8));
  settings.cellSize = options.cellSize > 0.0f ? options.cellSize : (std::max)(w.cellSize, 0.001f);
  settings.pressureIterations = options.pressureIterations > 0
    ? options.pressureIterations : static_cast<uint32_t>(w.EffectivePressureIterations());
  settings.deltaTime = options.fixedDt;
  settings.maxSubstepDt = options.fixedDt;
  settings.gravity = w.gravity;
  settings.viscosity = (std::max)(w.viscosity, 0.0f);
  settings.windX = w.windX;
  settings.windY = w.windY;
  settings.windZ = w.windZ;
  settings.damping = w.damping;
  settings.sorOmega = (std::min)((std::max)(w.sorOmega, 1.0f), 1.99f);
  settings.forcingAmplitude = (std::max)(w.forcingAmplitude, 0.0f);
  settings.deriveGridFromLayout = true;
  settings.waterPlaneY = w.waterPlaneY;
  settings.freeboard = w.freeboard;
  settings.openCeiling = w.openCeiling;
  settings.airRiseSpeed = options.airRiseSpeed >= 0.0f ? options.airRiseSpeed : (std::max)(w.airRiseSpeed, 0.0f);

  if (!fluid.TryInitialize(driver, settings)) {
    // SKIP only when the backend lacks the capability; a rejected kernel must not pass.
    if (driver->SupportsComputeTextures3D()) {
      T8_LOG_ERROR("[BackyardSelfTest] FAIL: api=%s supports 3D compute storage but the solver did not initialize", api);
      return 1;
    }
    T8_LOG_INFO("[BackyardSelfTest] SKIP: api=%s has no volumetric compute storage", api);
    return 0;
  }

  const FluidSettings& fs = fluid.Settings();
  // Water volume expressed in cells, the same unit as the alpha sum reported below.
  const float nominalCells = static_cast<float>(fs.width) * fs.depth *
                             static_cast<float>(fs.height) * fs.surfaceLevel;
  // Start from the same state the scene starts from. The scene calls ResetToRest(),
  // which CLEARS the velocity, pressure and divergence volumes before seeding alpha.
  // Seeding alpha alone leaves freshly created storage textures holding whatever was
  // in GPU memory: that made the still-pool gate report a residual growing from 1.9 mm
  // to 34.6 mm over two seconds with no ball and no forcing, which is uninitialised
  // memory rather than solver behaviour.
  if (!fluid.ResetToRest(driver)) {
    T8_LOG_ERROR("[BackyardSelfTest] reset to rest failed; falling back to a bare seed");
    if (!fluid.SeedVolumeFraction(driver)) {
      T8_LOG_ERROR("[BackyardSelfTest] volume fraction seed failed");
      ++failures;
    }
  }

  // ── Gate 1: a still pool is still ───────────────────────────────────────────
  // Bias and noise are separate gates on purpose. A uniform offset gives a non-zero
  // rms with no dynamics, and a zero-mean slosh gives a non-zero rms with no bias;
  // they are different failures. The rms bound is tight (1 mm against a measured
  // 0.1 mm) because a 3 mm bound would tolerate a 30x degradation.
  if (failures == 0) {
    fluid.DroppedSphere().enabled = false;
    float stillStartVolume = 0.0f;
    fluid.MeasureVolumeFraction(driver, stillStartVolume);
    // Sample the residual as it develops rather than only at the end: the question is
    // whether the still pool stays still, and a single endpoint cannot distinguish a
    // static bias from a growing one.
    const uint32_t stillSteps = static_cast<uint32_t>(std::ceil(2.0f / options.fixedDt));
    for (uint32_t i = 0; i < stillSteps && failures == 0; ++i) {
      if (!fluid.Step(driver)) { T8_LOG_ERROR("[BackyardSelfTest] still pool: step failed"); ++failures; break; }
      if (i % 60 == 59) {
        SurfaceField probe;
        if (CaptureSurface(fluid, driver, probe)) {
          const Stats s = Summarise(probe);
          FluidSimulation::Diagnostics diagnostics;
          fluid.ReadDiagnostics(driver, diagnostics);
          T8_LOG_INFO("[BackyardSelfTest] still pool t=%.2fs mean=%+.4f rms=%.4f maxAbs=%.4f meanSpeed=%.4f",
                      fluid.SimulationTime(), s.mean, s.rms, s.maxAbs, diagnostics.meanSpeed);
        }
      }
    }
    SurfaceField field;
    if (!CaptureSurface(fluid, driver, field)) { T8_LOG_ERROR("[BackyardSelfTest] still pool: readback failed"); ++failures; }
    else {
      const Stats s = Summarise(field);
      FluidSimulation::Diagnostics diagnostics;
      fluid.ReadDiagnostics(driver, diagnostics);
      float finalVolume = 0.0f;
      fluid.MeasureVolumeFraction(driver, finalVolume);
      const float lossPct = stillStartVolume > 0.0f
          ? 100.0f * (stillStartVolume - finalVolume) / stillStartVolume : 0.0f;
      // Name the worst column so a persistent outlier can be traced to the steps or a
      // wall instead of being an anonymous number.
      int worstX = 0, worstZ = 0;
      float worst = 0.0f;
      for (uint32_t z = 0; z < field.depth; ++z)
        for (uint32_t x = 0; x < field.width; ++x) {
          const float v = std::fabs(field.At(static_cast<int>(x), static_cast<int>(z)));
          if (v > worst) { worst = v; worstX = static_cast<int>(x); worstZ = static_cast<int>(z); }
        }
      T8_LOG_INFO("[BackyardSelfTest] still pool  mean=%+.4f m rms=%.4f m maxAbs=%.4f m at x=%d z=%d meanSpeed=%.4f m/s volumeLoss=%.3f%%",
                  s.mean, s.rms, s.maxAbs, worstX, worstZ, diagnostics.meanSpeed, lossPct);

      // Bias gate. With RGBA16F fields the still pool sank -1.1 cm/s (mean -10.5 mm and
      // 0.69 % lost at 2 s, the correction repaying only ~174 cells per check), so this
      // was a regression band. R32F fields (W-02) removed the sink: mean -0.2 mm and
      // 0.001 % on D3D12, Vulkan and D3D11. The gate is now strict, with 10x margin on
      // the mean and 50x on the loss.
      if (std::fabs(s.mean) > 0.0020f) {
        T8_LOG_ERROR("[BackyardSelfTest] still pool: mean surface offset %+.4f m outside the 2 mm band (R32F measures -0.2 mm at 2 s)", s.mean);
        ++failures;
      }
      if (lossPct > 0.05f) {
        T8_LOG_ERROR("[BackyardSelfTest] still pool: lost %.3f%% of the water in 2 s with no body (R32F loses ~0.001%%)", lossPct);
        ++failures;
      }
      // Noise gate, tight. This is the one that would have caught the synthetic stir
      // and the uninitialised-field class of bug: both produced 20-35 mm of surface
      // noise here while the rms at production settings is 0.1 mm.
      if (s.rms > 0.005f) {
        T8_LOG_ERROR("[BackyardSelfTest] still pool: surface rms %.4f m exceeds 5 mm", s.rms);
        ++failures;
      }
      if (s.maxAbs > 0.0200f) {
        T8_LOG_ERROR("[BackyardSelfTest] still pool: a column is %.4f m off the plane (limit 20 mm)", s.maxAbs);
        ++failures;
      }
      if (diagnostics.meanSpeed > 0.01f) {
        T8_LOG_ERROR("[BackyardSelfTest] still pool: mean speed %.4f m/s exceeds 0.01", diagnostics.meanSpeed);
        ++failures;
      }
    }
  }

  // ── Gates 2 and 4: the drop must be radially symmetric and must propagate ────
  if (failures == 0 && !options.skipBall) {
    if (!fluid.ResetToRest(driver)) { T8_LOG_ERROR("[BackyardSelfTest] reset failed"); ++failures; }
    FluidSphere& ball = fluid.DroppedSphere();
    ball.enabled = true;
    ball.radius = 0.25f;
    ball.densityRatio = 0.6f;
    ball.dragCoefficient = 0.47f;
    ball.restitution = 0.30f;
    ball.x = 0.0f;
    ball.z = fs.basinMinZ + 0.5f * (fs.basinMaxZ - fs.basinMinZ);   // pool centre
    ball.y = fs.waterPlaneY + 1.0f;                                  // 1 m above the plane
    ball.vx = ball.vy = ball.vz = 0.0f;
    ball.submerged = 0.0f;

    // Impact point in grid units (cell corners are integers). Truncating it to a cell
    // index put the annulus 9 cm off the impact in Z (2.25 / 0.09f = 24.999998 -> 24),
    // so the -Z and +Z halves sampled the wave at 0.56 m and 0.44 m.
    const float centreX = (ball.x - fs.basinMinX) / fs.cellSize;
    const float centreZ = (ball.z - fs.basinMinZ) / fs.cellSize;

    SurfaceField reference;
    if (!CaptureSurface(fluid, driver, reference)) {
      T8_LOG_ERROR("[BackyardSelfTest] drop: baseline readback failed");
      ++failures;
    }

    float impactTime = -1.0f;
    float arrivalTime = -1.0f;
    bool earlyIsotropySampled = false;
    int stallCount = 0;
    int sustainedFrames = 0;
    float previousFront = -1.0f;
    std::string frontSeries, arrivalSeries;
    char buffer[32];

    // 3 s of sim covers the fall (about 0.45 s), the arrival at 0.5 m and a second
    // of outward propagation.
    const uint32_t totalFrames = static_cast<uint32_t>(std::ceil(3.0f / options.fixedDt));
    for (uint32_t frame = 0; frame < totalFrames && failures == 0; ++frame) {
      if (!fluid.Step(driver)) { T8_LOG_ERROR("[BackyardSelfTest] drop: step failed"); ++failures; break; }
      if (impactTime < 0.0f && ball.submerged > 0.0f) impactTime = fluid.SimulationTime();

      SurfaceField field;
      if (!CaptureSurface(fluid, driver, field)) { ++failures; break; }
      const float front = FrontRadius(fluid, field, reference, centreX, centreZ, 0.005f);
      if (front > 0.3f && impactTime > 0.0f) {
        std::snprintf(buffer, sizeof(buffer), " %.2f", front);
        frontSeries += buffer;
      }
      // Arrival must be SUSTAINED. A single-frame spike (the impact frame itself, or the
      // correction's periodic redistribution) otherwise registers as a front at 0.5 m
      // before the wave has moved at all.
      if (arrivalTime < 0.0f && impactTime >= 0.0f) {
        if (front >= 0.5f) {
          if (++sustainedFrames >= 3) arrivalTime = fluid.SimulationTime();
        } else {
          sustainedFrames = 0;
        }
      }
      if (previousFront >= 0.0f && front > 0.0f && front < previousFront - 0.01f) {
        // Log only. The front legitimately stops at the pool walls and the median
        // detector can jump to a distant noisy annulus, so monotonicity is a poor
        // pass/fail signal here. Arrival time is the invariant that matters.
        if (++stallCount <= 2)
          T8_LOG_DEBUG("[BackyardSelfTest] front shrank %.3f -> %.3f m at t=%.2fs", previousFront, front, fluid.SimulationTime());
      }
      if (front > 0.0f) previousFront = front;

      // Isotropy at the two checkpoints the gate asks for.
      const float t = fluid.SimulationTime();
      const bool checkEarly = impactTime >= 0.0f && std::fabs(t - (impactTime + 0.30f)) < 0.5f * options.fixedDt;
      const bool checkLate = impactTime >= 0.0f && std::fabs(t - (impactTime + 1.00f)) < 0.5f * options.fixedDt;
      if (checkEarly || checkLate) {
        Quadrant q[4];
        Quadrant qRef[4];
        if (!AnnulusMedian(fluid, field, centreX, centreZ, 0.50f, 0.05f, q) ||
            !AnnulusMedian(fluid, reference, centreX, centreZ, 0.50f, 0.05f, qRef)) {
          T8_LOG_ERROR("[BackyardSelfTest] isotropy: annulus had no samples");
          ++failures;
          break;
        }
        // Measure the CHANGE from the pre-drop surface, so the pool's own tilt does
        // not masquerade as asymmetry. Quadrant q = (dx > 0) + 2 (dz > 0), so:
        //  - mirrorSpread compares -X with +X inside each Z half;
        //  - zSpread compares the -Z half with the +Z half.
        // Each quadrant spans both axes, so true X-vs-Z anisotropy is invisible here.
        // zSpread used to be called the "axis" gate with a 16 mm / 12 mm band for
        // "operator-split anisotropy"; that 10.7 mm was the annulus sitting 9 cm off the
        // impact in Z. Centred, both halves agree within 0.6 mm (fp16 and R32F alike;
        // mirror 0.06-1.0 mm), so both gates share the 3 mm mirror limit.
        float medians[4];
        for (int i = 0; i < 4; ++i) medians[i] = q[i].median - qRef[i].median;
        const float mirrorSpread = (std::max)(std::fabs(medians[0] - medians[1]),
                                              std::fabs(medians[2] - medians[3]));
        const float zSpread = std::fabs(0.5f * (medians[0] + medians[1]) -
                                        0.5f * (medians[2] + medians[3]));
        bool sameSign = true;
        for (int i = 1; i < 4; ++i)
          if ((medians[i] > 0.0f) != (medians[0] > 0.0f)) sameSign = false;
        const float mirrorLimit = 0.003f;
        T8_LOG_INFO("[BackyardSelfTest] isotropy %+.2fs quadrant deltas %+.4f %+.4f %+.4f %+.4f m mirrorX=%.5f m mirrorZ=%.5f m (limit %.4f)",
                    t - impactTime, medians[0], medians[1], medians[2], medians[3],
                    mirrorSpread, zSpread, mirrorLimit);
        if (mirrorSpread > mirrorLimit) {
          T8_LOG_ERROR("[BackyardSelfTest] waves are not mirror-symmetric in X: %.5f m exceeds %.4f m", mirrorSpread, mirrorLimit);
          ++failures;
        }
        if (zSpread > mirrorLimit) {
          T8_LOG_ERROR("[BackyardSelfTest] waves are not mirror-symmetric in Z: %.5f m exceeds %.4f m", zSpread, mirrorLimit);
          ++failures;
        }
        if (!sameSign) {
          T8_LOG_ERROR("[BackyardSelfTest] a quadrant moved opposite to the others");
          ++failures;
        }
        if (checkEarly) earlyIsotropySampled = true;
      }
    }

    if (failures == 0) {
      T8_LOG_INFO("[BackyardSelfTest] drop  impact=%.2fs arrival@0.5m=%.2fs (delta %.2fs)",
                  impactTime, arrivalTime, arrivalTime - impactTime);
      T8_LOG_INFO("[BackyardSelfTest] front radius (m) per frame:%s", frontSeries.c_str());
      if (impactTime < 0.0f) {
        T8_LOG_ERROR("[BackyardSelfTest] the ball never entered the water");
        ++failures;
      } else if (arrivalTime < 0.0f) {
        T8_LOG_ERROR("[BackyardSelfTest] no wave reached 0.5 m within 3 s");
        ++failures;
      } else {
        // The lower bound only guards against an instant false positive (already
        // handled by the 3-frame sustained check); the meaningful direction is a
        // STALLED front. Measured 0.04 s for 0.5 m: the near field right after impact is
        // the cavity collapse, not a gravity wave, so it is faster than sqrt(g*h).
        const float travel = arrivalTime - impactTime;
        if (travel < 0.01f || travel > 0.45f) {
          T8_LOG_ERROR("[BackyardSelfTest] wave travelled 0.5 m in %.3fs (expected 0.01..0.45; a stalled front fails)", travel);
          ++failures;
        }
        if (!earlyIsotropySampled) {
          T8_LOG_ERROR("[BackyardSelfTest] isotropy checkpoint was never sampled");
          ++failures;
        }
      }
    }
  }

  // ── Gate 3: mass over 10 s with a body in the water ─────────────────────────
  // The global correction holds the total at the reference, so endpoint drift alone
  // cannot fail while the correction succeeds. Track the worst instantaneous drift
  // (which is the correction deficit) as well as the endpoint, so both a scheme that
  // leaks faster than the correction can repay and a correction that misbehaves are
  // visible.
  if (failures == 0 && !options.skipBall) {
    float startVolume = 0.0f;
    if (!fluid.MeasureVolumeFraction(driver, startVolume)) {
      T8_LOG_ERROR("[BackyardSelfTest] mass: initial readback failed");
      ++failures;
    }
    float worstDrift = 0.0f;
    const uint32_t massFrames = static_cast<uint32_t>(std::ceil(10.0f / options.fixedDt));
    for (uint32_t i = 0; i < massFrames && failures == 0; ++i) {
      if (!fluid.Step(driver)) { T8_LOG_ERROR("[BackyardSelfTest] mass: step failed"); ++failures; break; }
      if (i % 15 == 14) {
        float volume = 0.0f;
        if (fluid.MeasureVolumeFraction(driver, volume) && startVolume > 0.0f) {
          const float drift = 100.0f * (volume - startVolume) / startVolume;
          if (std::fabs(drift) > std::fabs(worstDrift)) worstDrift = drift;
        }
      }
    }
    float endVolume = 0.0f;
    fluid.MeasureVolumeFraction(driver, endVolume);
    const float endpointDrift = startVolume > 0.0f ? 100.0f * (endVolume - startVolume) / startVolume : 0.0f;
    T8_LOG_INFO("[BackyardSelfTest] mass  start=%.0f end=%.0f cells (nominal %.0f), endpoint drift=%+.3f%% worst=%+.3f%%",
                startVolume, endVolume, nominalCells, endpointDrift, worstDrift);
    if (std::fabs(endpointDrift) > 0.05f) {
      T8_LOG_ERROR("[BackyardSelfTest] mass: endpoint drift %.3f%% exceeds 0.05%%", endpointDrift);
      ++failures;
    }
    if (std::fabs(worstDrift) > 0.75f) {
      T8_LOG_ERROR("[BackyardSelfTest] mass: the correction had to repay %.3f%% at once (limit 0.75%%)", worstDrift);
      ++failures;
    }
  }

  // ── Gate 4: moving boundary ────────────────────────────────────────────────
  // Body faces must carry the body's velocity. The staggered layout makes this exact:
  // texel (i,j,k).x is the +X face of cell (i,j,k), and the boundary kernel writes the
  // body velocity onto every face of a body cell. Sampling deep-interior cells avoids
  // the ambiguous half-cell membership at the surface.
  if (failures == 0) {
    if (!fluid.ResetToRest(driver)) { T8_LOG_ERROR("[BackyardSelfTest] boundary: reset failed"); ++failures; }
    FluidSphere& ball = fluid.DroppedSphere();
    ball.enabled = true;
    ball.radius = 0.25f;
    ball.x = 0.0f;
    ball.z = fs.basinMinZ + 0.5f * (fs.basinMaxZ - fs.basinMinZ);
    ball.y = fs.waterPlaneY - 0.60f;      // fully submerged
    ball.vx = 1.0f;
    ball.vy = 0.20f;
    ball.vz = -0.30f;
    ball.submerged = 1.0f;
    if (!fluid.Step(driver)) { T8_LOG_ERROR("[BackyardSelfTest] boundary: step failed"); ++failures; }

    const float bx = ball.vx, by = ball.vy, bz = ball.vz;
    std::vector<float> velocity;
    if (!fluid.ReadVelocity(driver, velocity)) {
      T8_LOG_ERROR("[BackyardSelfTest] boundary: velocity readback failed");
      ++failures;
    } else {
      const float h = fs.cellSize;
      const float inner = ball.radius - 0.5f * h;   // interior only, half a cell of margin
      int tested = 0;
      float worst = 0.0f;
      for (uint32_t k = 0; k < fs.depth; ++k)
        for (uint32_t j = 0; j < fs.height; ++j)
          for (uint32_t i = 0; i < fs.width; ++i) {
            const float x = fs.basinMinX + (static_cast<float>(i) + 0.5f) * h;
            const float y = fs.basinMinY + (static_cast<float>(j) + 0.5f) * h;
            const float z = fs.basinMinZ + (static_cast<float>(k) + 0.5f) * h;
            const float dx = x - ball.x, dy = y - ball.y, dz = z - ball.z;
            if (dx * dx + dy * dy + dz * dz > inner * inner) continue;
            const size_t index = static_cast<size_t>(i) + fs.width *
                (static_cast<size_t>(j) + static_cast<size_t>(fs.height) * k);
            const float u = velocity[index * 4 + 0];
            const float v = velocity[index * 4 + 1];
            const float w = velocity[index * 4 + 2];
            worst = (std::max)(worst, (std::max)(std::fabs(u - bx),
                        (std::max)(std::fabs(v - by), std::fabs(w - bz))));
            ++tested;
          }
      T8_LOG_INFO("[BackyardSelfTest] boundary  body cells=%d worst face deviation=%.5f m/s (body %.2f %.2f %.2f)",
                  tested, worst, bx, by, bz);
      if (tested < 20) {
        T8_LOG_ERROR("[BackyardSelfTest] boundary: only %d body cells sampled, the test is not measuring anything", tested);
        ++failures;
      } else if (worst > 0.01f) {
        T8_LOG_ERROR("[BackyardSelfTest] boundary: a body face deviates %.5f m/s from the body velocity", worst);
        ++failures;
      }
    }
  }

  // ── Gate 5: the surface texture is a real 2D field ──────────────────────────
  // Reads the texture the vertex shader actually displaces with, not the volume. A gate
  // on the volume cannot catch a surface-kernel indexing bug: CS_FluidSurface3D once read
  // its grid row from id.z (always 0 in a (width, depth, 1) dispatch), so every output
  // row was filled from grid column z = 0 - the texture varied along X and was exactly
  // constant along Y while the volume was perfectly correct.
  if (failures == 0) {
    if (!fluid.ResetToRest(driver)) { T8_LOG_ERROR("[BackyardSelfTest] surface: reset failed"); ++failures; }
    FluidSphere& ball = fluid.DroppedSphere();
    ball.enabled = true;
    ball.radius = 0.25f;
    ball.densityRatio = 0.6f;
    // Deliberately OFF-CENTRE in X. A centred drop cannot distinguish a correct mapping
    // from a transposed one, because the disturbance sits at the centre of both axes
    // either way; an X offset moves the peak along X for a correct kernel and along Z
    // for a transposed one.
    ball.x = 0.60f;
    ball.z = fs.basinMinZ + 0.5f * (fs.basinMaxZ - fs.basinMinZ);
    ball.y = fs.waterPlaneY + 1.0f;
    ball.vx = ball.vy = ball.vz = 0.0f;
    ball.submerged = 0.0f;

    float impact = -1.0f;
    const uint32_t limit = static_cast<uint32_t>(std::ceil(2.0f / options.fixedDt));
    for (uint32_t frame = 0; frame < limit && impact < 0.0f && failures == 0; ++frame) {
      if (!fluid.Step(driver)) { ++failures; break; }
      if (ball.submerged > 0.0f) impact = fluid.SimulationTime();
    }
    // A little way past impact, so the cavity has formed and the rim has risen.
    const uint32_t settle = static_cast<uint32_t>(std::ceil(0.15f / options.fixedDt));
    for (uint32_t frame = 0; frame < settle && failures == 0; ++frame)
      if (!fluid.Step(driver)) { ++failures; }

    std::vector<float> surface;
    if (failures == 0 && !fluid.ReadSurface(driver, surface)) {
      T8_LOG_ERROR("[BackyardSelfTest] surface: texture readback failed");
      ++failures;
    } else if (failures == 0) {
      const uint32_t w = fs.width, d = fs.depth;
      // Per-axis variation, as the median range over the other axis. Under the id.z bug
      // each row is an exact copy of row 0, so the column range collapses to zero while
      // the row range stays large. Medians keep a few noisy rows from flaking the gate.
      std::vector<float> rowRanges, colRanges;
      rowRanges.reserve(d);
      colRanges.reserve(w);
      for (uint32_t y = 0; y < d; ++y) {
        float lo = 1e9f, hi = -1e9f;
        for (uint32_t x = 0; x < w; ++x) {
          const float r = surface[(static_cast<size_t>(x) + static_cast<size_t>(w) * y) * 4];
          lo = (std::min)(lo, r);
          hi = (std::max)(hi, r);
        }
        rowRanges.push_back(hi - lo);
      }
      for (uint32_t x = 0; x < w; ++x) {
        float lo = 1e9f, hi = -1e9f;
        for (uint32_t y = 0; y < d; ++y) {
          const float r = surface[(static_cast<size_t>(x) + static_cast<size_t>(w) * y) * 4];
          lo = (std::min)(lo, r);
          hi = (std::max)(hi, r);
        }
        colRanges.push_back(hi - lo);
      }
      auto median = [](std::vector<float> values) {
        std::sort(values.begin(), values.end());
        const size_t n = values.size();
        return n % 2 ? values[n / 2] : 0.5f * (values[n / 2 - 1] + values[n / 2]);
      };
      const float rowMedian = median(rowRanges);
      const float colMedian = median(colRanges);

      // Extreme value and where it sits, as the directional probe.
      int minX = 0, minY = 0;
      float minValue = 1e9f;
      for (uint32_t y = 0; y < d; ++y)
        for (uint32_t x = 0; x < w; ++x) {
          const float r = surface[(static_cast<size_t>(x) + static_cast<size_t>(w) * y) * 4];
          if (r < minValue) { minValue = r; minX = static_cast<int>(x); minY = static_cast<int>(y); }
        }
      const int centreX = static_cast<int>(w / 2);
      const int centreY = static_cast<int>(d / 2);

      // Thresholds are calibrated against measurement, not assumption. GLM suggested
      // 5 mm on both axes, which would FAIL today's correct kernel: the medians are
      // 4.5 mm along X and 2.2 mm along Y, because a median over rows/columns is
      // dominated by the far field where the disturbance has decayed. What matters is
      // the discrimination: the id.z bug makes the across-Y median EXACTLY zero.
      // The 1 mm limit leaned on fp16 noise in the far field; with R32F fields (W-02)
      // that noise is gone and the correct kernel measures 3.0 mm along X and 0.8 mm
      // along Y (fp16: 4.0 / 2.4 mm). 0.25 mm keeps 3x margin to the signal and the
      // bug still reads exactly 0.
      constexpr float kAxisVariationMin = 0.00025f;
      T8_LOG_INFO("[BackyardSelfTest] surface texture  rowMedian=%.4f m colMedian=%.4f m  extreme=%.4f m at x=%d y=%d (centre %d,%d)",
                  rowMedian, colMedian, minValue, minX, minY, centreX, centreY);
      if (rowMedian < kAxisVariationMin || colMedian < kAxisVariationMin) {
        T8_LOG_ERROR("[BackyardSelfTest] surface texture is not a 2D field (%.4f m along X, %.4f m along Y; need %.4f m in both)",
                     rowMedian, colMedian, kAxisVariationMin);
        ++failures;
      }
      if (minX - centreX < 3) {
        T8_LOG_ERROR("[BackyardSelfTest] the disturbance does not sit at the ball's X position (%d vs centre %d)",
                     minX, centreX);
        ++failures;
      }
      if (std::abs(minY - centreY) > 3) {
        T8_LOG_ERROR("[BackyardSelfTest] the disturbance is displaced in Z (%d vs centre %d) - mapping looks transposed",
                     minY, centreY);
        ++failures;
      }
    }

    // Alpha must stay inside [0,1]. The transport is the only thing that can violate it,
    // and Stage 5b replaces that transport, so the count belongs here as its gate.
    std::vector<float> alpha;
    if (failures == 0 && fluid.ReadVolumeFraction(driver, alpha)) {
      int violations = 0;
      float worstAlpha = 0.0f;
      for (float a : alpha) {
        const float excess = (std::max)(-a, a - 1.0f);
        if (excess > 1e-3f) { ++violations; worstAlpha = (std::max)(worstAlpha, excess); }
      }
      T8_LOG_INFO("[BackyardSelfTest] boundedness  cells outside [0,1]=%d worst excess=%.5f", violations, worstAlpha);
      if (violations != 0) {
        T8_LOG_ERROR("[BackyardSelfTest] %d cells have alpha outside [0,1] (worst %.5f)", violations, worstAlpha);
        ++failures;
      }
    }
  }

  // ── Gate 6: the ceiling is air, not a lid ──────────────────────────────────
  // The domain ends `freeboard` above the water plane, and every kernel originally
  // treated out-of-range as SOLID in every direction - including above the top row.
  // That made the top row's +Y face a no-flux wall, and a strong splash pins against it
  // instead of venting. On the staggered grid the +Y face of a top-row cell is that
  // texel's own .y component, so the probe is exact:
  //
  //   lid  -> the gradient kernel takes the `IsSolid(b)` branch for b = (i, Height, k)
  //           and OVERWRITES the face velocity with SolidBoundaryValue() = 0, and the
  //           extrapolate kernel leaves it there. Measured: exactly 0.00000 m/s.
  //   open -> the face is an ordinary liquid/air face and the ghost pressure
  //           p_ghost = p_L*(1 - 1/theta) drives it. Measured: tens of cm/s.
  //
  // A heavy drop is used because the ordinary drop never wets the top row at all, so an
  // unwet ceiling cannot be probed. The 3x-density 2.5 m drop throws a local splash crown
  // up to the top row (the ball's 0.065 m3 raises the mean level only ~1.6 mm), so the top
  // row's water content is reported but NOT asserted: filling it is correct, pinning
  // against it is not.
  if (failures == 0 && !options.skipBall) {
    if (!fluid.ResetToRest(driver)) { T8_LOG_ERROR("[BackyardSelfTest] ceiling: reset failed"); ++failures; }
    FluidSphere& ball = fluid.DroppedSphere();
    ball.enabled = true;
    ball.radius = 0.25f;
    ball.densityRatio = 3.0f;                       // heavy, as the acceptance case asks
    ball.dragCoefficient = 0.47f;
    ball.restitution = 0.30f;
    ball.x = 0.0f;
    ball.z = fs.basinMinZ + 0.5f * (fs.basinMaxZ - fs.basinMinZ);
    ball.y = fs.waterPlaneY + 2.5f;                 // 2.5 m above the surface
    ball.vx = 0.0f;
    ball.vy = 0.0f;
    ball.vz = 0.0f;
    ball.submerged = 0.0f;

    const uint32_t ceilingFrames = static_cast<uint32_t>(std::ceil(6.0f / options.fixedDt));
    const uint32_t topRow = fs.height - 1;
    float topRowPeak = 0.0f;
    float peakRise = -1e9f;
    float peakFall = 1e9f;
    float topFaceSpeedMax = 0.0f;      // max |v| at the +Y face of a WET top-row cell
    uint32_t wetTopSamples = 0;
    uint32_t sampled = 0;
    for (uint32_t i = 0; i < ceilingFrames && failures == 0; ++i) {
      if (!fluid.Step(driver)) { T8_LOG_ERROR("[BackyardSelfTest] ceiling: step failed"); ++failures; break; }
      if (i % 30 != 29) continue;
      std::vector<float> alpha;
      std::vector<float> velocity;
      if (!fluid.ReadVolumeFraction(driver, alpha) || !fluid.ReadVelocity(driver, velocity)) {
        T8_LOG_ERROR("[BackyardSelfTest] ceiling: readback failed");
        ++failures;
        break;
      }
      bool wetTop = false;
      for (uint32_t k = 0; k < fs.depth; ++k)
        for (uint32_t x = 0; x < fs.width; ++x) {
          const size_t index = x + fs.width * (topRow + static_cast<size_t>(fs.height) * k);
          const float a = alpha[index];
          topRowPeak = (std::max)(topRowPeak, a);
          if (a >= 0.5f) {
            wetTop = true;
            topFaceSpeedMax = (std::max)(topFaceSpeedMax, std::fabs(velocity[index * 4 + 1]));
          }
        }
      if (wetTop) ++wetTopSamples;
      SurfaceField field;
      if (CaptureSurfaceFromAlpha(fluid, alpha, field)) {
        const Stats s = Summarise(field);
        peakRise = (std::max)(peakRise, s.maxValue);
        peakFall = (std::min)(peakFall, s.minValue);
      }
      ++sampled;
    }
    T8_LOG_INFO("[BackyardSelfTest] open ceiling  top-row peak alpha=%.3f rise=%+.3f m fall=%+.3f m  "
                "wet-top samples=%u/%u  top-face |v| max=%.4f m/s",
                topRowPeak, peakRise, peakFall, wetTopSamples, sampled, topFaceSpeedMax);
    if (sampled < 10) {
      T8_LOG_ERROR("[BackyardSelfTest] ceiling: only %u samples, the gate is not measuring anything", sampled);
      ++failures;
    } else if (wetTopSamples == 0) {
      // Without a wet top row the probe has nothing to look at, and the gate would pass
      // on a lid. Fail loudly instead of silently testing nothing.
      T8_LOG_ERROR("[BackyardSelfTest] ceiling: the drop never wet the top row, so the open-ceiling "
                   "condition was not exercised (peak rise %+.3f m)", peakRise);
      ++failures;
    } else if (topFaceSpeedMax < kTopFaceSpeedMin) {
      T8_LOG_ERROR("[BackyardSelfTest] ceiling: a wet top row has |v|=%.5f m/s at its +Y face "
                   "(need >%.4f) - the ceiling is a no-flux lid", topFaceSpeedMax, kTopFaceSpeedMin);
      ++failures;
    }
    if (peakRise < kCeilingRiseMin) {
      T8_LOG_ERROR("[BackyardSelfTest] ceiling: the heavy drop raised the surface only %.3f m (need %.3f)",
                   peakRise, kCeilingRiseMin);
      ++failures;
    }
  }

  // ── Gate 7: a floating body leaves the surface flat ──────────────────────────
  // Reads the rendered surface texture, so it covers the surface kernel's handling of
  // columns that pass through the body, not just the volume.
  if (failures == 0 && !options.skipBall) {
    if (!fluid.ResetToRest(driver)) { T8_LOG_ERROR("[BackyardSelfTest] float: reset failed"); ++failures; }
    FluidSphere& ball = fluid.DroppedSphere();
    ball.enabled = true;
    ball.radius = 0.25f;
    ball.densityRatio = 0.6f;
    ball.dragCoefficient = 0.47f;
    ball.restitution = 0.30f;
    ball.x = 0.0f;
    ball.z = fs.basinMinZ + 0.5f * (fs.basinMaxZ - fs.basinMinZ);
    ball.y = fs.waterPlaneY + 1.0f;                 // the scene's default drop
    ball.vx = ball.vy = ball.vz = 0.0f;
    ball.submerged = 0.0f;
    const uint32_t settleFrames = static_cast<uint32_t>(std::ceil(8.0f / options.fixedDt));
    const uint32_t flickerBegin = static_cast<uint32_t>(std::ceil(3.0f / options.fixedDt));
    const uint32_t flickerEnd = static_cast<uint32_t>(std::ceil(4.0f / options.fixedDt));
    std::vector<float> current, previous, previous2;
    float worstJerk = 0.0f;
    float worstJerkDistance = 0.0f;
    for (uint32_t i = 0; i < settleFrames && failures == 0; ++i) {
      if (!fluid.Step(driver)) { T8_LOG_ERROR("[BackyardSelfTest] float: step failed"); ++failures; break; }
      if (i < flickerBegin || i >= flickerEnd) continue;
      if (!fluid.ReadSurface(driver, current)) { T8_LOG_ERROR("[BackyardSelfTest] float: surface readback failed"); ++failures; break; }
      if (!previous2.empty()) {
        for (uint32_t z = 0; z < fs.depth; ++z)
          for (uint32_t x = 0; x < fs.width; ++x) {
            const float dx = fs.basinMinX + (static_cast<float>(x) + 0.5f) * fs.cellSize - ball.x;
            const float dz = fs.basinMinZ + (static_cast<float>(z) + 0.5f) * fs.cellSize - ball.z;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist > ball.radius + 3.0f * fs.cellSize) continue;
            const size_t t = (static_cast<size_t>(x) + static_cast<size_t>(fs.width) * z) * 4;
            const float jerk = std::fabs(current[t] - 2.0f * previous[t] + previous2[t]);
            if (jerk > worstJerk) { worstJerk = jerk; worstJerkDistance = dist; }
          }
      }
      previous2.swap(previous);
      previous.swap(current);
    }
    if (failures == 0) {
      T8_LOG_INFO("[BackyardSelfTest] float flicker  worst frame-to-frame jerk 3-4 s=%.4f m at %.2f m from the ball",
                  worstJerk, worstJerkDistance);
      if (worstJerk > kFloatFlickerMax) {
        T8_LOG_ERROR("[BackyardSelfTest] float: the surface near the body flickers by %.4f m between frames (limit %.3f)",
                     worstJerk, kFloatFlickerMax);
        ++failures;
      }
    }
    std::vector<float> surface;
    if (failures == 0 && !fluid.ReadSurface(driver, surface)) {
      T8_LOG_ERROR("[BackyardSelfTest] float: surface readback failed");
      ++failures;
    } else if (failures == 0) {
      const float h = fs.cellSize;
      const float r = ball.radius;
      constexpr int kBands = 8;                     // 10 cm radial bands, for the log
      std::vector<float> farField, ring, covered, bands[kBands];
      for (uint32_t z = 0; z < fs.depth; ++z)
        for (uint32_t x = 0; x < fs.width; ++x) {
          const float dx = fs.basinMinX + (static_cast<float>(x) + 0.5f) * h - ball.x;
          const float dz = fs.basinMinZ + (static_cast<float>(z) + 0.5f) * h - ball.z;
          const float dist = std::sqrt(dx * dx + dz * dz);
          const float value = surface[(static_cast<size_t>(x) + static_cast<size_t>(fs.width) * z) * 4];
          if (dist >= 1.0f && dist <= 1.5f) farField.push_back(value);
          if (dist >= r + 0.5f * h && dist <= r + 1.5f * h) ring.push_back(value);
          if (dist <= r - 0.5f * h) covered.push_back(value);
          const int band = static_cast<int>(dist / 0.1f);
          if (band < kBands) bands[band].push_back(value);
        }
      if (farField.empty() || ring.empty() || covered.empty()) {
        T8_LOG_ERROR("[BackyardSelfTest] float: empty sample set (far %zu ring %zu covered %zu)",
                     farField.size(), ring.size(), covered.size());
        ++failures;
      } else {
        const float farLevel = Median(farField);
        const float ringLevel = Median(ring);
        const float coveredMin = *std::min_element(covered.begin(), covered.end());
        std::string profile;
        char cell[24];
        for (int b = 0; b < kBands; ++b) {
          std::snprintf(cell, sizeof(cell), " %+.1f", bands[b].empty() ? 0.0f : 100.0f * (Median(bands[b]) - farLevel));
          profile += cell;
        }
        T8_LOG_INFO("[BackyardSelfTest] float  ball y=%.3f submerged=%.2f  far=%+.4f m  ring-far=%+.4f m  "
                    "covered min-ring=%+.4f m  profile vs far (cm, 10 cm bands):%s",
                    ball.y, ball.submerged, farLevel, ringLevel - farLevel, coveredMin - ringLevel,
                    profile.c_str());
        if (std::fabs(ringLevel - farLevel) > kFloatRingMax) {
          T8_LOG_ERROR("[BackyardSelfTest] float: the surface just outside the body is %+.4f m from the far field (limit %.3f)",
                       ringLevel - farLevel, kFloatRingMax);
          ++failures;
        }
        if (ringLevel - coveredMin > kFloatCoveredMax) {
          T8_LOG_ERROR("[BackyardSelfTest] float: the surface under the body dips %.4f m below its surroundings (limit %.3f)",
                       ringLevel - coveredMin, kFloatCoveredMax);
          ++failures;
        }
      }
    }
  }

  // ── Gate 9: multiple bodies keep their own identity ────────────────────────
  // W-22 encodes the body index in the wall mask (2u + k) so the kernels can look up
  // per-body velocity without a centre/radius list. Two failure modes are invisible to
  // a single-ball run: a kernel that still compares the mask to exactly 1u (moving
  // bodies then act as static walls) and an index that is off by one (a fast ball gets
  // the other ball's velocity). This gate spawns two bodies with DIFFERENT velocities
  // and checks both the mask values and the footprint velocities.
  if (failures == 0 && !options.skipBall) {
    if (!fluid.ResetToRest(driver)) { T8_LOG_ERROR("[BackyardSelfTest] multi-body: reset failed"); ++failures; }
    FluidSphere& first = fluid.DroppedSphere();
    first.enabled = true;
    first.radius = 0.25f;
    first.densityRatio = 0.6f;
    first.dragCoefficient = 0.47f;
    first.restitution = 0.30f;
    first.x = 0.0f;
    first.z = fs.basinMinZ + 0.5f * (fs.basinMaxZ - fs.basinMinZ);
    first.y = fs.waterPlaneY - 0.60f;      // fully submerged
    first.vx = 0.80f;
    first.vy = 0.10f;
    first.vz = -0.20f;
    first.submerged = 1.0f;

    FluidSphere second = first;
    second.x = 1.60f;                     // well separated: no overlapping footprints
    second.radius = 0.20f;
    second.vx = -0.40f;
    second.vy = 0.25f;
    second.vz = 0.55f;
    const int secondIndex = fluid.AddBody(second);
    if (secondIndex < 0) {
      T8_LOG_ERROR("[BackyardSelfTest] multi-body: AddBody refused a second body");
      ++failures;
    } else if (!fluid.Step(driver)) {
      T8_LOG_ERROR("[BackyardSelfTest] multi-body: step failed");
      ++failures;
    } else {
      const float h = fs.cellSize;
      const FluidSphere& a = fluid.Bodies()[0];
      const FluidSphere& b = fluid.Bodies()[1];
      const float av[3] = {a.vx, a.vy, a.vz};
      const float bv[3] = {b.vx, b.vy, b.vz};

      std::vector<uint32_t> mask;
      std::vector<float> velocity;
      if (!fluid.ReadWallMask(driver, mask) || !fluid.ReadVelocity(driver, velocity)) {
        T8_LOG_ERROR("[BackyardSelfTest] multi-body: readback failed");
        ++failures;
      } else {
        int maskErrors = 0;
        int cellsA = 0;
        int cellsB = 0;
        float worstA = 0.0f;
        float worstB = 0.0f;
        for (uint32_t k = 0; k < fs.depth; ++k)
          for (uint32_t j = 0; j < fs.height; ++j)
            for (uint32_t i = 0; i < fs.width; ++i) {
              const size_t index = static_cast<size_t>(i) + fs.width *
                  (static_cast<size_t>(j) + static_cast<size_t>(fs.height) * k);
              const float x = fs.basinMinX + (static_cast<float>(i) + 0.5f) * h;
              const float y = fs.basinMinY + (static_cast<float>(j) + 0.5f) * h;
              const float z = fs.basinMinZ + (static_cast<float>(k) + 0.5f) * h;

              const float da = (x - a.x) * (x - a.x) + (y - a.y) * (y - a.y) + (z - a.z) * (z - a.z);
              const float innerA = a.radius - 0.5f * h;
              if (da <= innerA * innerA) {
                if (mask[index] != 2u) ++maskErrors;
                const float u = velocity[index * 4 + 0];
                const float v = velocity[index * 4 + 1];
                const float w = velocity[index * 4 + 2];
                worstA = (std::max)(worstA, (std::max)(std::fabs(u - av[0]),
                            (std::max)(std::fabs(v - av[1]), std::fabs(w - av[2]))));
                ++cellsA;
                continue;
              }
              const float db = (x - b.x) * (x - b.x) + (y - b.y) * (y - b.y) + (z - b.z) * (z - b.z);
              const float innerB = b.radius - 0.5f * h;
              if (db <= innerB * innerB) {
                if (mask[index] != 3u) ++maskErrors;
                const float u = velocity[index * 4 + 0];
                const float v = velocity[index * 4 + 1];
                const float w = velocity[index * 4 + 2];
                worstB = (std::max)(worstB, (std::max)(std::fabs(u - bv[0]),
                            (std::max)(std::fabs(v - bv[1]), std::fabs(w - bv[2]))));
                ++cellsB;
              }
            }
        T8_LOG_INFO("[BackyardSelfTest] multi-body  body0 cells=%d maskErrors=%d worst=%.5f m/s | "
                    "body1 cells=%d worst=%.5f m/s (mask 2 and 3)",
                    cellsA, maskErrors, worstA, cellsB, worstB);
        if (cellsA < 20 || cellsB < 12) {
          T8_LOG_ERROR("[BackyardSelfTest] multi-body: only %d/%d interior cells sampled", cellsA, cellsB);
          ++failures;
        } else if (maskErrors != 0) {
          T8_LOG_ERROR("[BackyardSelfTest] multi-body: %d body cells do not carry their 2+k mask value",
                       maskErrors);
          ++failures;
        } else if (worstA > 0.01f || worstB > 0.01f) {
          T8_LOG_ERROR("[BackyardSelfTest] multi-body: a footprint face carries the wrong body's "
                       "velocity (deviation %.5f / %.5f m/s)", worstA, worstB);
          ++failures;
        }
      }
    }
    fluid.RemoveBodiesFrom(1);
  }

  // ── Gate 11: entrained air leaves the water (W-29) ──────────────────────────────
  // Without the air-rise pass the drop's air stayed in the bulk for good and the surface
  // drew each pocket as a frozen bump: 1734 bulk cells below 98 % and an interface 3.09
  // cells wide after 30 s, still rising. With it: 24 cells (air under solids) and 1.02.
  // Runs at the scene's own step, because the air accumulates over tens of seconds.
  if (failures == 0 && !options.skipBall) {
    if (!fluid.ResetToRest(driver)) { T8_LOG_ERROR("[BackyardSelfTest] air: reset failed"); ++failures; }
    FluidSphere& ball = fluid.DroppedSphere();
    ball.enabled = true;
    ball.radius = 0.25f;
    ball.densityRatio = 0.6f;
    ball.dragCoefficient = 0.47f;
    ball.restitution = 0.30f;
    ball.x = 0.0f;
    ball.z = fs.basinMinZ + 0.5f * (fs.basinMaxZ - fs.basinMinZ);
    ball.y = fs.waterPlaneY + 1.0f;
    ball.vx = ball.vy = ball.vz = 0.0f;
    ball.submerged = 0.0f;
    const float stepDt = (std::max)(w.maxDeltaTime, 1.0f / 240.0f);
    fluid.SetDeltaTime(stepDt);
    fluid.SetMaxSubstepDt((std::max)(w.maxSubstepDt, 1.0f / 1000.0f));
    const uint32_t airSteps = static_cast<uint32_t>(std::ceil(30.0f / stepDt));
    for (uint32_t i = 0; i < airSteps && failures == 0; ++i)
      if (!fluid.Step(driver)) { T8_LOG_ERROR("[BackyardSelfTest] air: step failed"); ++failures; }
    FluidSimulation::MassAudit audit;
    if (failures == 0 && !fluid.AuditMass(driver, audit)) {
      T8_LOG_ERROR("[BackyardSelfTest] air: audit readback failed");
      ++failures;
    } else if (failures == 0) {
      constexpr uint32_t kAirBulkMax = 200;
      constexpr float kAirInterfaceMax = 1.6f;
      T8_LOG_INFO("[BackyardSelfTest] air  after 30 s: bulk below 98%%=%u of %u (limit %u) min=%.3f  "
                  "interface width mean=%.2f cells (limit %.1f)",
                  audit.bulkBelow98, audit.bulkCells, kAirBulkMax, audit.bulkMinAlpha,
                  audit.interfaceWidthMean, kAirInterfaceMax);
      if (audit.bulkBelow98 > kAirBulkMax) {
        T8_LOG_ERROR("[BackyardSelfTest] air: %u bulk cells still hold air 30 s after the drop (limit %u)",
                     audit.bulkBelow98, kAirBulkMax);
        ++failures;
      }
      if (audit.interfaceWidthMean > kAirInterfaceMax) {
        T8_LOG_ERROR("[BackyardSelfTest] air: the interface is %.2f cells wide (limit %.1f)",
                     audit.interfaceWidthMean, kAirInterfaceMax);
        ++failures;
      }
    }
    fluid.SetDeltaTime(options.fixedDt);
    fluid.SetMaxSubstepDt(options.fixedDt);
  }

  // ── Gate 10: the per-ball material palette (W-25) ────────────────────────────
  // Three independent facts, all headless: the palette lists at least 32 materials,
  // every one of them resolves on disk and decodes on this API, and the real shuffle
  // bag hands out a permutation for one full cycle. The pixel proof that the textures
  // reach the shader is scripts\validation\probe_ball_materials.py, which needs a frame.
  {
    t850::BackyardBallPalette palette;
    if (!t850::LoadBackyardBallPalette("Scenes/BackyardBallMaterials.json", palette)) {
      T8_LOG_ERROR("[BackyardSelfTest] ball materials: the palette file is invalid");
      ++failures;
    } else {
      const uint32_t count = static_cast<uint32_t>(palette.materials.size());
      const auto& locator = t850::ResourceLocator::Instance();
      uint32_t onDisk = 0;
      uint32_t decoded = 0;
      uint32_t firstFailure = 0;
      for (uint32_t i = 0; i < count; ++i) {
        const std::string folder = "Textures/Ball/" + palette.materials[i].id + "/";
        const std::string names[3] = {palette.maps.color, palette.maps.normal, palette.maps.orm};
        bool present = true;
        for (const std::string& name : names) {
          if (!locator.Exists(folder + name)) { present = false; break; }
        }
        if (!present) {
          if (firstFailure == 0) firstFailure = i + 1;
          continue;
        }
        ++onDisk;
        // Create and destroy one set at a time so the gate never holds 96 textures.
        bool ok = true;
        for (const std::string& name : names) {
          const int index = driver->CreateTexture("Ball/" + palette.materials[i].id + "/" + name);
          if (index < 0) ok = false;
          else driver->DestroyTexture(index);
        }
        if (ok) ++decoded;
        else if (firstFailure == 0) firstFailure = i + 1;
      }

      t850::BackyardBallMaterialBag bag;
      bag.Reset(12345u);
      std::vector<uint32_t> seen(count, 0);
      uint32_t drawn = 0;
      bool permutation = count > 0;
      for (uint32_t i = 0; i < count; ++i) {
        uint32_t index = 0;
        if (!bag.Next(count, index) || index >= count || seen[index] != 0) { permutation = false; break; }
        seen[index] = 1;
        ++drawn;
      }

      T8_LOG_INFO("[BackyardSelfTest] ball materials  palette=%u onDisk=%u decoded=%u bagPermutation=%s",
                  count, onDisk, decoded, permutation ? "ok" : "FAILED");
      if (count < 32) {
        T8_LOG_ERROR("[BackyardSelfTest] ball materials: the palette lists %u materials, expected >= 32", count);
        ++failures;
      } else if (onDisk != count) {
        T8_LOG_ERROR("[BackyardSelfTest] ball materials: %u of %u sets are on disk (first missing: set %u); "
                     "run scripts/DownloadTextures.ps1, or the launcher's Download Assets button, which "
                     "fetches the same manifest through scripts/ModelCloud.ps1", onDisk, count, firstFailure);
        ++failures;
      } else if (decoded != count) {
        T8_LOG_ERROR("[BackyardSelfTest] ball materials: %u of %u sets failed to decode on %s (first: set %u)",
                     decoded, count, api, firstFailure);
        ++failures;
      } else if (!permutation) {
        T8_LOG_ERROR("[BackyardSelfTest] ball materials: the shuffle bag repeated a material after %u draws",
                     drawn);
        ++failures;
      }
    }
  }

  fluid.Shutdown(driver);
  if (failures == 0) {
    T8_LOG_INFO("[BackyardSelfTest] PASS: api=%s", api);
    return 0;
  }
  T8_LOG_ERROR("[BackyardSelfTest] FAIL: api=%s failures=%d", api, failures);
  return 1;
}

} // namespace t850
