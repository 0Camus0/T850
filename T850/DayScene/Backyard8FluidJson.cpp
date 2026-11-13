#include <pch.h>

#include <Backyard8FluidJson.h>

#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace t850 {

bool LoadBackyard8FluidConfig(const char* path, Backyard8FluidConfig& out, std::string& error) {
  error.clear();
  if (!path || !*path) return true;
  std::string json;
  if (!ResourceLocator::Instance().ReadText(path, json)) {
    T8_LOG_INFO("[Backyard8] No fluid config at '%s'; using the built-in defaults", path);
    return true;
  }
  Backyard8FluidConfig parsed;
  const auto ec = glz::read<glz::opts{.error_on_unknown_keys = true}>(parsed, json);
  if (ec) {
    error = glz::format_error(ec, json);
    T8_LOG_ERROR("[Backyard8] Cannot parse '%s': %s", path, error.c_str());
    return false;
  }
  if (parsed.version != 1) {
    error = "unsupported version";
    T8_LOG_ERROR("[Backyard8] '%s' has unsupported version %d", path, parsed.version);
    return false;
  }
  out = parsed;
#if defined(OS_ANDROID)
  // Mobile profile: the tablet is fill-rate and compute limited at its native resolution.
  // The JSON values are conservative; every field is optional.
  if (out.android.solverIterations > 0) out.solver.solverIterations = out.android.solverIterations;
  if (out.android.substeps > 0) out.solver.substeps = out.android.substeps;
  if (out.android.smoothingIterations >= 0)
    out.screenSpace.smoothingIterations = out.android.smoothingIterations;
  if (out.android.thicknessDownsample >= 0)
    out.screenSpace.thicknessDownsample = out.android.thicknessDownsample;
  if (out.android.maxStepsPerFrame > 0) out.scene.maxStepsPerFrame = out.android.maxStepsPerFrame;
  if (out.android.particleRadius > 0.0f) out.solver.particleRadius = out.android.particleRadius;
  if (out.android.fixedStep > 0.0f) out.scene.fixedStep = out.android.fixedStep;
  if (out.android.anisotropy >= 0) out.screenSpace.anisotropy = out.android.anisotropy != 0;
  if (out.android.whitewater >= 0) out.whitewater.enabled = out.android.whitewater != 0;
  T8_LOG_INFO("[Backyard8] Android profile: %d substeps x %d iterations, %.3f m spacing, "
              "%d smoothing pass(es), thickness downsample %d, fixed step %.4f s, max steps/frame %d",
              out.solver.substeps, out.solver.solverIterations, out.solver.particleRadius,
              out.screenSpace.smoothingIterations, out.screenSpace.thicknessDownsample,
              out.scene.fixedStep, out.scene.maxStepsPerFrame);
#endif
  T8_LOG_INFO("[Backyard8] Fluid config '%s': %d particles max, sphere %.2f m at %.2f, "
              "%s renderer, %d smoothing iteration(s)",
              path, out.solver.maxParticles, out.solver.sphereRadius, out.solver.sphereHeight,
              out.particles.renderMode == 1 ? "screen-space" : "splat",
              out.screenSpace.smoothingIterations);
  return true;
}

}  // namespace t850
