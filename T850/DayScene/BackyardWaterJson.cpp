#include <pch.h>

#include <BackyardWaterJson.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <map>
#include <string>

// W-11: explicit glaze meta for the water tunables.
//
// Two reasons it is explicit rather than automatic:
//   * glaze's automatic reflection of this struct does not compile reliably, and
//   * the key list is the contract for the JSON file: a member NOT listed here cannot
//     be set from JSON and keeps its C++ default in WaterTunables.h.
//
// Deliberately absent, each for a reason:
//   gridWidth/gridHeight/gridDepth - derived from cellSize and the pool layout; the
//     scene writes them back, so a value here would be overwritten.
//   waterPlaneY  - owned by Models/Backyard/backyard_layout.json.
//   freeboard    - owned by the same layout; FluidSimulation overwrites it, so a value
//                  here would silently do nothing. Edit the layout instead.
//   floorTextureBound - runtime state the scene sets when the floor texture binds.
template <>
struct glz::meta<t850::WaterTunables> {
  using T = t850::WaterTunables;
  static constexpr auto value = glz::object(
            "pressureIterations", &T::pressureIterations, "cellSize", &T::cellSize,
      "openCeiling", &T::openCeiling, "deltaTime", &T::deltaTime,
      "maxDeltaTime", &T::maxDeltaTime, "timeScale", &T::timeScale,
      "fixedTimeStep", &T::fixedTimeStep, "viscosity", &T::viscosity,
      "gravity", &T::gravity, "windX", &T::windX,
      "windY", &T::windY, "windZ", &T::windZ,
      "damping", &T::damping, "surfaceLevel", &T::surfaceLevel,
      "sorOmega", &T::sorOmega, "maxSubstepDt", &T::maxSubstepDt,
      "alphaCompression", &T::alphaCompression, "volumeCheckInterval", &T::volumeCheckInterval,
      "absorbR", &T::absorbR, "absorbG", &T::absorbG,
      "absorbB", &T::absorbB, "thicknessScale", &T::thicknessScale,
      "shallowR", &T::shallowR, "shallowG", &T::shallowG,
      "shallowB", &T::shallowB, "deepR", &T::deepR,
      "deepG", &T::deepG, "deepB", &T::deepB,
      "refractionStrength", &T::refractionStrength, "refractionBorderFade", &T::refractionBorderFade,
      "causticGain", &T::causticGain, "causticPower", &T::causticPower,
      "causticSlopeScale", &T::causticSlopeScale, "causticFloorBase", &T::causticFloorBase,
      "fresnelBoost", &T::fresnelBoost, "fresnelBias", &T::fresnelBias,
      "scatterWrap", &T::scatterWrap, "scatterNormal", &T::scatterNormal,
      "scatterDepth", &T::scatterDepth, "waterLineY", &T::waterLineY,
      "waterToFloor", &T::waterToFloor, "analyticWaveScale", &T::analyticWaveScale,
      "analyticWaveSpeed", &T::analyticWaveSpeed, "normalMapGain", &T::normalMapGain,
      "vertexWaveScale", &T::vertexWaveScale, "normalTileX", &T::normalTileX,
      "normalTileZ", &T::normalTileZ, "normalDriftX", &T::normalDriftX,
      "normalDriftZ", &T::normalDriftZ, "normalTile2X", &T::normalTile2X,
      "normalTile2Z", &T::normalTile2Z, "normalDrift2X", &T::normalDrift2X,
      "normalDrift2Z", &T::normalDrift2Z, "iblReflectionScale", &T::iblReflectionScale,
      "directLightScale", &T::directLightScale, "sunGlintPower", &T::sunGlintPower,
      "sunGlintScale", &T::sunGlintScale, "ssrWeight", &T::ssrWeight,
      "ssrDistortion", &T::ssrDistortion, "ssrBlurBase", &T::ssrBlurBase,
      "ssrBlurRange", &T::ssrBlurRange, "ssrMaxDistance", &T::ssrMaxDistance,
      "ssrStepStart", &T::ssrStepStart, "ssrStepGrowth", &T::ssrStepGrowth,
      "ssrStepMax", &T::ssrStepMax, "ssrSteps", &T::ssrSteps,
      "foamCrest", &T::foamCrest, "foamBorder", &T::foamBorder,
      "foamNoise", &T::foamNoise, "foamStrength", &T::foamStrength,
      "foamR", &T::foamR, "foamG", &T::foamG,
      "foamB", &T::foamB, "foamFreq0X", &T::foamFreq0X,
      "foamFreq0Y", &T::foamFreq0Y, "foamFreq1X", &T::foamFreq1X,
      "foamFreq1Y", &T::foamFreq1Y, "foamFreq2X", &T::foamFreq2X,
      "foamFreq2Y", &T::foamFreq2Y, "foamFade", &T::foamFade,
      "normalMapEnabled", &T::normalMapEnabled, "ssrEnabled", &T::ssrEnabled,
      "displacementScale", &T::displacementScale, "normalStrength", &T::normalStrength,
      "fakeRipplesEnabled", &T::fakeRipplesEnabled, "forcingAmplitude", &T::forcingAmplitude,
      "sphereEnabled", &T::sphereEnabled, "sphereRadius", &T::sphereRadius,
      "sphereDensityRatio", &T::sphereDensityRatio, "sphereDragCoefficient", &T::sphereDragCoefficient,
      "sphereRestitution", &T::sphereRestitution, "sphereHeaveDamping", &T::sphereHeaveDamping,
      "sphereWaterDensity", &T::sphereWaterDensity, "sphereMassOverride", &T::sphereMassOverride,
      "sphereDropHeight", &T::sphereDropHeight, "sphereDropX", &T::sphereDropX,
      "sphereDropZ", &T::sphereDropZ, "sphereInitialVY", &T::sphereInitialVY,
      "sphereInitialVX", &T::sphereInitialVX, "sphereInitialVZ", &T::sphereInitialVZ,
      "sphereReleaseDelay", &T::sphereReleaseDelay,
      "spawnRadiusMin", &T::spawnRadiusMin,
      "spawnRadiusMax", &T::spawnRadiusMax, "spawnDensityMin", &T::spawnDensityMin,
      "spawnDensityMax", &T::spawnDensityMax, "spawnHeightMin", &T::spawnHeightMin,
      "spawnHeightMax", &T::spawnHeightMax, "spawnSeed", &T::spawnSeed);
};

// If a field is added to WaterTunables without a meta entry it would silently keep its
// C++ default and a JSON key for it would be rejected. The count makes that loud.
static_assert(glz::reflect<t850::WaterTunables>::size == 108,
              "WaterTunables gained or lost a field: update the glz::meta in BackyardWaterJson.cpp");

// The tunables file groups its keys under "water", so one file stays one concept. The
// struct lives at global scope with an explicit meta: glaze rejects a type with internal
// linkage, and naming the key here keeps the file shape obvious.
struct WaterFileShape {
  std::optional<glz::raw_json> water;
};

template <>
struct glz::meta<WaterFileShape> {
  using T = WaterFileShape;
  static constexpr auto value = glz::object("water", &T::water);
};

namespace t850 {

  bool LoadBackyardWaterTunables(const char* path, WaterTunables& out) {
    std::string text;
    if (!ResourceLocator::Instance().ReadText(path, text)) {
      T8_LOG_INFO("[Backyard] No water tunables file '%s'; using the C++ defaults in WaterTunables.h", path);
      return true;
    }

    // Captured as raw JSON first; the contents are validated below.
    WaterFileShape shape;
    if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(shape, text)) {
      T8_LOG_ERROR("[Backyard] Cannot parse '%s': %s", path, glz::format_error(ec, text).c_str());
      return false;
    }
    if (!shape.water) {
      T8_LOG_ERROR("[Backyard] '%s' has no \"water\" object", path);
      return false;
    }
    const std::string& body = shape.water->str;

    // Read permissively, then validate the keys by hand. glaze's own unknown-key check
    // needs a compile-time perfect hash, and the seed search fails for a struct this
    // large (json/read.hpp: static_assert bool(hash_info<T>.type)), so the strictness is
    // enforced here instead: a typo is still a hard error, never a silent default.
    std::map<std::string, glz::raw_json> entries;
    if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(entries, body)) {
      T8_LOG_ERROR("[Backyard] Cannot parse the \"water\" object in '%s': %s",
                   path, glz::format_error(ec, body).c_str());
      return false;
    }

    static constexpr auto kKeys = glz::reflect<WaterTunables>::keys;
    for (const auto& entry : entries) {
      bool known = false;
      for (const auto& known_key : kKeys) {
        if (entry.first == known_key) { known = true; break; }
      }
      if (!known) {
        T8_LOG_ERROR("[Backyard] Unknown water key '%s' in '%s'. Known keys are listed in "
                     "the glz::meta in BackyardWaterJson.cpp.", entry.first.c_str(), path);
        return false;
      }
    }

    if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(out, body)) {
      T8_LOG_ERROR("[Backyard] Cannot read the \"water\" object in '%s' into WaterTunables: %s",
                   path, glz::format_error(ec, body).c_str());
      return false;
    }

    T8_LOG_INFO("[Backyard] Water tunables loaded from '%s': %zu of %zu keys set, the rest keep "
                "their C++ defaults", path, entries.size(), kKeys.size());
    return true;
  }

} // namespace t850
