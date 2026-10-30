#pragma once

#include <scene/WaterTunables.h>

#include <string>
#include <vector>

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

  // W-25: the per-ball PBR palette (Scenes/BackyardBallMaterials.json). Each material
  // is a folder Textures/Ball/<id>/ holding the three files named in "maps".
  struct BackyardBallMaterial {
    std::string id;
    std::string label;
  };
  struct BackyardBallMaps {
    std::string color;
    std::string normal;
    std::string orm;
  };
  struct BackyardBallPalette {
    int version = 0;
    BackyardBallMaps maps;
    std::vector<BackyardBallMaterial> materials;
  };

  // A missing file yields an empty palette (the balls keep the GLB's own look). Returns
  // false for a malformed file or an id/file name that is not a plain name.
  bool LoadBackyardBallPalette(const char* path, BackyardBallPalette& out);

} // namespace t850
