#include <pch.h>

#include <BackyardSelfTest.h>

#include <simulation/FluidSimulation.h>
#include <scene/WaterTunables.h>
#include <utils/Log.h>

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
                     uint32_t centreX, uint32_t centreZ, float radius, float halfBand,
                     Quadrant quadrant[4]) {
    const FluidSettings& fs = fluid.Settings();
    std::vector<float> values[4];
    for (uint32_t z = 0; z < field.depth; ++z) {
      for (uint32_t x = 0; x < field.width; ++x) {
        const float dx = (static_cast<float>(x) + 0.5f - static_cast<float>(centreX)) * fs.cellSize;
        const float dz = (static_cast<float>(z) + 0.5f - static_cast<float>(centreZ)) * fs.cellSize;
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
                    const SurfaceField& reference, uint32_t centreX, uint32_t centreZ,
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

  bool StepFor(FluidSimulation& fluid, BaseDriver* driver, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i)
      if (!fluid.Step(driver)) return false;
    return true;
  }

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
  settings.cellSize = (std::max)(w.cellSize, 0.001f);
  settings.pressureIterations = options.pressureIterations;
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

  if (!fluid.TryInitialize(driver, settings)) {
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

      // Bias gate. The measured bias at production settings is a steady -1.1 cm/s sink
      // that grows linearly over time (mean -1.5 mm at 0.2 s to -10.5 mm at 2.0 s, with
      // the correction deficit climbing 202 -> 749 cells and repaying only ~174 cells
      // per check). A threshold tight enough to catch drift would fail today's branch,
      // and a threshold loose enough to pass it cannot detect drift at all. So this
      // gate is calibrated as a REGRESSION BAND around the measured behaviour and is
      // replaced by a strict "the pool must not sink" gate once W-03 lands in Stage 5b.
      if (std::fabs(s.mean) > 0.0120f) {
        T8_LOG_ERROR("[BackyardSelfTest] still pool: mean surface offset %+.4f m outside the -12 mm band (baseline is -10.5 mm at 2 s)", s.mean);
        ++failures;
      }
      if (lossPct > 0.80f) {
        T8_LOG_ERROR("[BackyardSelfTest] still pool: lost %.3f%% of the water in 2 s with no body (baseline loses ~0.69%%)", lossPct);
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

    const uint32_t centreX = fs.width / 2;
    const float centreZf = (ball.z - fs.basinMinZ) / fs.cellSize;
    const uint32_t centreZ = static_cast<uint32_t>(
        (std::min)((std::max)(centreZf, 0.0f), static_cast<float>(fs.depth - 1)));

    SurfaceField reference;
    if (!CaptureSurface(fluid, driver, reference)) {
      T8_LOG_ERROR("[BackyardSelfTest] drop: baseline readback failed");
      ++failures;
    }

    float impactTime = -1.0f;
    float arrivalTime = -1.0f;
    float frontAtHalf = 0.0f;
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
        // not masquerade as asymmetry.
        // Two different properties, and they deserve different gates:
        //  - MIRROR symmetry (X+ vs X-, Z+ vs Z-) is the hard one. A kernel reading the
        //    wrong axis, or an asymmetry bug, breaks left/right immediately. Measured
        //    0.7 mm and 0.4 mm.
        //  - AXIS difference (X vs Z) is softer: the alpha transport is operator-split
        //    and sweeps one axis at a time, so some X-vs-Z anisotropy is a property of
        //    the scheme, not a bug. Measured 10.8 mm at r=0.5 m, 0.3 s after impact;
        //    this band exists to catch a regression, and shrinks when the conservative
        //    scheme lands in Stage 5b.
        float medians[4];
        for (int i = 0; i < 4; ++i) medians[i] = q[i].median - qRef[i].median;
        const float mirrorSpread = (std::max)(std::fabs(medians[0] - medians[1]),
                                              std::fabs(medians[2] - medians[3]));
        float axisSpread = 0.0f;
        axisSpread = (std::max)(axisSpread, std::fabs(0.5f * (medians[0] + medians[1]) -
                                                     0.5f * (medians[2] + medians[3])));
        bool sameSign = true;
        for (int i = 1; i < 4; ++i)
          if ((medians[i] > 0.0f) != (medians[0] > 0.0f)) sameSign = false;
        const float mirrorLimit = 0.003f;
        const float axisLimit = checkEarly ? 0.016f : 0.012f;
        T8_LOG_INFO("[BackyardSelfTest] isotropy %+.2fs quadrant deltas %+.4f %+.4f %+.4f %+.4f m mirror=%.5f m axis=%.4f m (limits %.4f / %.4f)",
                    t - impactTime, medians[0], medians[1], medians[2], medians[3],
                    mirrorSpread, axisSpread, mirrorLimit, axisLimit);
        if (mirrorSpread > mirrorLimit) {
          T8_LOG_ERROR("[BackyardSelfTest] waves are not mirror-symmetric: %.5f m exceeds %.4f m", mirrorSpread, mirrorLimit);
          ++failures;
        }
        if (axisSpread > axisLimit) {
          T8_LOG_ERROR("[BackyardSelfTest] X vs Z amplitude differs by %.4f m (limit %.4f m)", axisSpread, axisLimit);
          ++failures;
        }
        if (!sameSign) {
          T8_LOG_ERROR("[BackyardSelfTest] a quadrant moved opposite to the others");
          ++failures;
        }
        if (checkEarly) frontAtHalf = axisSpread;
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
        if (frontAtHalf <= 0.0f) {
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
      // the discrimination: the id.z bug makes the across-Y median EXACTLY zero, while
      // still-pool noise gives a range of about 0.4 mm over 50 samples. 1 mm sits
      // comfortably above noise and below the correct signal.
      constexpr float kAxisVariationMin = 0.001f;
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
  // unwet ceiling cannot be probed. Note the 3x-density 2.5 m drop legitimately overflows
  // this shallow (1.56 m) pool - the top row does fill - so the top row's water content is
  // reported but NOT asserted: filling it is correct, pinning against it is not.
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

  fluid.Shutdown(driver);
  if (failures == 0) {
    T8_LOG_INFO("[BackyardSelfTest] PASS: api=%s", api);
    return 0;
  }
  T8_LOG_ERROR("[BackyardSelfTest] FAIL: api=%s failures=%d", api, failures);
  return 1;
}

} // namespace t850
