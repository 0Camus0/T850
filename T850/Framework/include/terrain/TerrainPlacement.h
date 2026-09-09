#pragma once

#include <terrain/HeightmapTerrain.h>
#include <string_view>

namespace t850 {

struct TerrainPlacementResult {
  bool allowed = false;
  float baseHeight = 0.0f;
  std::string reason;
};

bool TerrainGridDimensions(const scene::SceneHeightmapDesc& terrain, uint32_t& columns, uint32_t& rows);
bool TerrainPlacementTransformSupported(const scene::Vec3f& rotation, const scene::Vec3f& scale);
TerrainPlacementResult CheckTerrainPlacement(const scene::SceneHeightmapDesc& terrain,
    const scene::SceneTerrainPlacementDesc& placement, std::string_view ignoreId = {});
bool ValidateTerrainPlacements(const scene::SceneHeightmapDesc& terrain, std::string* error = nullptr);
bool AddTerrainPlacement(scene::SceneHeightmapDesc& terrain, const scene::SceneTerrainPlacementDesc& placement,
    std::string* error = nullptr);
bool RemoveTerrainPlacement(scene::SceneHeightmapDesc& terrain, std::string_view id);
float TerrainSurfaceHeight(const scene::SceneHeightmapDesc& terrain, float localX, float localZ);
bool FitPlacementVisual(const scene::SceneHeightmapDesc& terrain, const scene::SceneTerrainPlacementDesc& placement,
  const AABB& modelBounds, XMATRIX44& transform, std::string* error = nullptr);
bool AppendTerrainBlockouts(const scene::SceneHeightmapDesc& terrain, MutableMeshSnapshot& mesh,
  std::string* error = nullptr);

}