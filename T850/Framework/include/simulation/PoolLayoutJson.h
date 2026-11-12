#pragma once

// Minimal mirror of Models/Backyard/backyard_layout.json.
//
// Shared by the Eulerian solver (FluidSimulation, scene 7) and the particle solver
// (PbfFluid, scene 8) so the two cannot drift apart on how the pool geometry is read.
//
// These types deliberately live at namespace scope rather than in an anonymous namespace:
// glaze reflects members to build a parser, and MSVC rejects that for a type with internal
// linkage (error C7631, "variable with internal linkage declared but not defined" through
// glz::detail::external). GCC accepts it, so the problem only shows up on the Windows build.
//
// Parsing is permissive (error_on_unknown_keys = false) so the layout file can grow keys the
// solver does not care about without breaking every scene that reads it.

#include <string>
#include <vector>

namespace t850 {

  // One pool step. The authored JSON names its axes X / world-Y / world-Z, which is why the
  // X/Z fields carry the min/max names rather than x/y/z.
  struct LayoutStepJson {
    std::string name;
    double minX = 0.0, minY = 0.0, minZ = 0.0;
    double maxX = 0.0, maxY = 0.0, maxZ = 0.0;
  };

  struct LayoutPoolJson {
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    double depth = 0.0;
    // Metres of air the solver domain keeps above the still-water plane so a splash has
    // room to rise into. With no freeboard the top row of the volume is already full and
    // the free surface behaves like a solid lid.
    double freeboard = 0.35;
    double copingHeight = 0.08;
    double wallThickness = 0.25;
    std::vector<LayoutStepJson> steps;
  };

  struct LayoutJson {
    double waterLine = 0.0;
    // Absolute world Y of the still-water surface, i.e. the plane the water mesh is
    // authored at. Authoritative: the solver derives its fill level from it.
    double waterPlaneY = 0.0;
    LayoutPoolJson pool;
  };

}  // namespace t850
