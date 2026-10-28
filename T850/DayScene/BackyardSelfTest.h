#pragma once

#include <video/BaseDriver.h>

namespace t850 {

  struct BackyardSelfTestOptions {
    // Steps per gate run. The still-pool gate needs ~2 s, the mass gate ~10 s.
    uint32_t frames = 900;
    // Deterministic step. Fixed rather than the real frame delta so two runs are
    // comparable; the interactive path uses real dt and cannot be compared to itself.
    //
    // 1/300 is chosen deliberately: the still-pool residual scales roughly linearly
    // with dt (measured rms 21.9 mm at 1/60, 19.3 mm at 1/120, 10.4 mm at 1/200,
    // 3.6 mm at 1/400, mean speed 67 / 47 / 30 / 17 mm/s), so a coarse fixed step
    // measures the solver's O(dt) hydrostatic residual rather than a regression. The
    // gate must run at a dt representative of production, where the same pool reads
    // 0.1 mm.
    float fixedDt = 1.0f / 300.0f;
    // Pressure sweeps. Must match production (60): at 20 sweeps the pressure solve
    // leaves residual divergence and the pool drifts instead of staying still, which
    // made the still-pool gate measure convergence depth rather than surface shape.
    uint32_t pressureIterations = 60;
    // Skip the drop gates and run only the still-pool and mass gates.
    bool skipBall = false;
  };

  // Runs the backyard water gates against a standalone solver on the real pool grid,
  // with no renderer. Returns 0 when every gate passes, 1 otherwise.
  //
  // The gates exist because the previous "verified" pass on this branch could not
  // fail: it measured volume and wall penetration but never the surface shape, so a
  // kernel that read its grid row from the wrong dispatch axis survived untouched.
  int RunBackyardSelfTest(BaseDriver* driver, const BackyardSelfTestOptions& options);

} // namespace t850
