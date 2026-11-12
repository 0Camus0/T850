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
  T8_LOG_INFO("[Backyard8] Fluid config '%s': %d particles max, sphere %.2f m at %.2f, "
              "%s renderer, %d smoothing iteration(s)",
              path, out.solver.maxParticles, out.solver.sphereRadius, out.solver.sphereHeight,
              out.particles.renderMode == 1 ? "screen-space" : "splat",
              out.screenSpace.smoothingIterations);
  return true;
}

}  // namespace t850
