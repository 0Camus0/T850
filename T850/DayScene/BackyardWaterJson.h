#pragma once

#include <scene/WaterTunables.h>

namespace t850 {

  // W-11: loads the water tunables for the Backyard from a JSON file.
  //
  // This lives in its own translation unit on purpose. glaze is included here and
  // nowhere in WaterTunables.h or SceneDescriptor.h: when glaze reaches the legacy
  // DayScene.cpp translation unit through one of those headers, MSVC fails with
  // `glaze/util/parse.hpp C3878: syntax error: unexpected token ')'` (an MSVC parser
  // limit inside glaze's integer parsing, reproducible regardless of how many fields
  // WaterTunables has). Keeping glaze in a leaf TU avoids it entirely.
  //
  // Returns false only when the file exists and is malformed, so a missing file means
  // "use the C++ defaults". An unknown key inside the file is an error.
  bool LoadBackyardWaterTunables(const char* path, WaterTunables& out);

} // namespace t850
