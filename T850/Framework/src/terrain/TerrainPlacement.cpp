#include <pch.h>
#include <terrain/TerrainPlacement.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace t850 {

bool TerrainPlacementTransformSupported(const scene::Vec3f& rotation, const scene::Vec3f& scale) {
  return std::abs(rotation.x) < 0.0001f && std::abs(rotation.y) < 0.0001f && std::abs(rotation.z) < 0.0001f &&
      std::abs(scale.x - 1.0f) < 0.0001f && std::abs(scale.y - 1.0f) < 0.0001f && std::abs(scale.z - 1.0f) < 0.0001f;
}

bool TerrainGridDimensions(const scene::SceneHeightmapDesc& terrain, uint32_t& columns, uint32_t& rows) {
  columns = rows = 0;
  const auto& grid = terrain.placement_grid;
  if (!grid.enabled || !std::isfinite(grid.cell_size) || grid.cell_size <= 0.0f ||
      !std::isfinite(terrain.size_x) || !std::isfinite(terrain.size_z) || terrain.size_x <= 0 || terrain.size_z <= 0 ||
      !std::isfinite(grid.max_height_difference) || grid.max_height_difference < 0 ||
      !std::isfinite(grid.max_slope_degrees) || grid.max_slope_degrees < 0 || grid.max_slope_degrees >= 90) return false;
  const double horizontal = std::floor(static_cast<double>(terrain.size_x) / grid.cell_size);
  const double vertical = std::floor(static_cast<double>(terrain.size_z) / grid.cell_size);
  if (horizontal < 1 || vertical < 1 || horizontal > 1024 || vertical > 1024) return false;
  columns = static_cast<uint32_t>(horizontal);
  rows = static_cast<uint32_t>(vertical);
  return true;
}

float TerrainSurfaceHeight(const scene::SceneHeightmapDesc& terrain, float localX, float localZ) {
  if (terrain.samples_x < 2 || terrain.samples_z < 2 || terrain.size_x <= 0 || terrain.size_z <= 0 ||
      terrain.elevations.size() != static_cast<size_t>(terrain.samples_x) * terrain.samples_z ||
      !std::isfinite(localX) || !std::isfinite(localZ)) return std::numeric_limits<float>::quiet_NaN();
  const float horizontal = std::clamp(localX / terrain.size_x, 0.0f, 1.0f) * (terrain.samples_x - 1);
  const float vertical = std::clamp(localZ / terrain.size_z, 0.0f, 1.0f) * (terrain.samples_z - 1);
  const uint32_t column = (std::min)(static_cast<uint32_t>(horizontal), terrain.samples_x - 2);
  const uint32_t row = (std::min)(static_cast<uint32_t>(vertical), terrain.samples_z - 2);
  const float fractionX = horizontal - column;
  const float fractionZ = vertical - row;
  const float topLeft = terrain.elevations[row * terrain.samples_x + column];
  const float topRight = terrain.elevations[row * terrain.samples_x + column + 1];
  const float bottomLeft = terrain.elevations[(row + 1) * terrain.samples_x + column];
  const float bottomRight = terrain.elevations[(row + 1) * terrain.samples_x + column + 1];
  if (fractionX + fractionZ <= 1.0f)
    return topLeft + (topRight - topLeft) * fractionX + (bottomLeft - topLeft) * fractionZ;
  return bottomRight + (bottomLeft - bottomRight) * (1.0f - fractionX) + (topRight - bottomRight) * (1.0f - fractionZ);
}

TerrainPlacementResult CheckTerrainPlacement(const scene::SceneHeightmapDesc& terrain,
    const scene::SceneTerrainPlacementDesc& placement, std::string_view ignoreId) {
  auto rejected = [](const char* reason) { return TerrainPlacementResult{false, 0.0f, reason}; };
  uint32_t columns = 0, rows = 0;
  if (!TerrainGridDimensions(terrain, columns, rows)) return rejected("Enable a valid placement grid (1..1024 cells per axis)");
  if (placement.kind != "building" && placement.kind != "unit") return rejected("Unknown blockout kind");
  if (placement.visual && (!std::isfinite(placement.visual->yaw_degrees) ||
      !std::isfinite(placement.visual->animation_speed) || placement.visual->animation_speed < 0 ||
      placement.visual->animation_speed > 10)) return rejected("Invalid placement visual rotation or animation speed");
  if (!placement.width || !placement.depth || placement.cell_x < 0 || placement.cell_z < 0 ||
      static_cast<uint64_t>(placement.cell_x) + placement.width > columns ||
      static_cast<uint64_t>(placement.cell_z) + placement.depth > rows) return rejected("Footprint is outside the grid");
  if (!std::isfinite(placement.height) || placement.height <= 0 || placement.height > 10000)
    return rejected("Blockout height must be positive and finite");
  for (float value : {placement.color.x, placement.color.y, placement.color.z})
    if (!std::isfinite(value) || value < 0 || value > 1) return rejected("Blockout color must be in [0, 1]");
  for (const auto& existing : terrain.placements) {
    if (!ignoreId.empty() && existing.id == ignoreId) continue;
    if (static_cast<int64_t>(placement.cell_x) < static_cast<int64_t>(existing.cell_x) + existing.width &&
        static_cast<int64_t>(existing.cell_x) < static_cast<int64_t>(placement.cell_x) + placement.width &&
        static_cast<int64_t>(placement.cell_z) < static_cast<int64_t>(existing.cell_z) + existing.depth &&
        static_cast<int64_t>(existing.cell_z) < static_cast<int64_t>(placement.cell_z) + placement.depth)
      return rejected("Footprint overlaps an occupied cell");
  }
  if (terrain.samples_x < 2 || terrain.samples_z < 2 ||
      terrain.elevations.size() != static_cast<size_t>(terrain.samples_x) * terrain.samples_z)
    return rejected("Prepare full-resolution terrain elevations before checking placement");
  const float cell = terrain.placement_grid.cell_size;
  const float left = placement.cell_x * cell, top = placement.cell_z * cell;
  const float right = (placement.cell_x + placement.width) * cell;
  const float bottom = (placement.cell_z + placement.depth) * cell;
  const float spacingX = terrain.size_x / (terrain.samples_x - 1);
  const float spacingZ = terrain.size_z / (terrain.samples_z - 1);
  const uint32_t firstX = static_cast<uint32_t>(left / spacingX);
  const uint32_t firstZ = static_cast<uint32_t>(top / spacingZ);
  const uint32_t endX = (std::min)(static_cast<uint32_t>(std::ceil(right / spacingX)), terrain.samples_x - 1);
  const uint32_t endZ = (std::min)(static_cast<uint32_t>(std::ceil(bottom / spacingZ)), terrain.samples_z - 1);
  float minimum = std::numeric_limits<float>::max();
  float maximum = -minimum;
  float maximumSlope = 0;
  for (uint32_t row = firstZ; row < endZ; ++row) {
    for (uint32_t column = firstX; column < endX; ++column) {
      const float topLeft = terrain.elevations[row * terrain.samples_x + column];
      const float topRight = terrain.elevations[row * terrain.samples_x + column + 1];
      const float bottomLeft = terrain.elevations[(row + 1) * terrain.samples_x + column];
      const float bottomRight = terrain.elevations[(row + 1) * terrain.samples_x + column + 1];
      for (float value : {topLeft, topRight, bottomLeft, bottomRight}) {
        if (!std::isfinite(value)) return rejected("Terrain contains a non-finite elevation");
        minimum = (std::min)(minimum, value);
        maximum = (std::max)(maximum, value);
      }
      maximumSlope = (std::max)(maximumSlope, std::hypot((topRight - topLeft) / spacingX, (bottomLeft - topLeft) / spacingZ));
      maximumSlope = (std::max)(maximumSlope, std::hypot((bottomRight - bottomLeft) / spacingX, (bottomRight - topRight) / spacingZ));
    }
  }
  if (minimum > maximum) return rejected("Footprint has no terrain coverage");
  if (placement.kind == "building" && terrain.placement_grid.flat_buildings_only &&
      (maximum - minimum > terrain.placement_grid.max_height_difference ||
       std::atan(maximumSlope) * 57.295779513f > terrain.placement_grid.max_slope_degrees))
    return rejected("Buildings require flat ground across the entire footprint");
  return {true, maximum, {}};
}

bool ValidateTerrainPlacements(const scene::SceneHeightmapDesc& terrain, std::string* error) {
  if (terrain.placements.size() > 1024) {
    if (error) *error = "Terrain supports at most 1024 static blockouts";
    return false;
  }
  if (terrain.placements.empty()) return true;
  std::set<std::string> ids;
  for (const auto& placement : terrain.placements) {
    if (placement.id.empty() || !ids.insert(placement.id).second) {
      if (error) *error = "Terrain placements require unique nonempty IDs";
      return false;
    }
    const auto result = CheckTerrainPlacement(terrain, placement, placement.id);
    if (!result.allowed) {
      if (error) *error = placement.name + ": " + result.reason;
      return false;
    }
  }
  return true;
}

bool AddTerrainPlacement(scene::SceneHeightmapDesc& terrain, const scene::SceneTerrainPlacementDesc& placement, std::string* error) {
  if (terrain.placements.size() >= 1024) {
    if (error) *error = "Terrain supports at most 1024 static blockouts";
    return false;
  }
  if (placement.id.empty() || std::any_of(terrain.placements.begin(), terrain.placements.end(),
      [&](const auto& existing) { return existing.id == placement.id; })) {
    if (error) *error = "Placement requires a new nonempty ID";
    return false;
  }
  const auto result = CheckTerrainPlacement(terrain, placement);
  if (!result.allowed) {
    if (error) *error = result.reason;
    return false;
  }
  terrain.placements.push_back(placement);
  return true;
}

bool RemoveTerrainPlacement(scene::SceneHeightmapDesc& terrain, std::string_view id) {
  return std::erase_if(terrain.placements, [&](const auto& placement) { return placement.id == id; }) != 0;
}

bool FitPlacementVisual(const scene::SceneHeightmapDesc& terrain, const scene::SceneTerrainPlacementDesc& placement,
    const AABB& modelBounds, XMATRIX44& transform, std::string* error) {
  const auto site = CheckTerrainPlacement(terrain, placement, placement.id);
  const float yaw = placement.visual ? placement.visual->yaw_degrees : 0.0f;
  if (!site.allowed || !modelBounds.IsValid() || !std::isfinite(yaw)) {
    if (error) *error = site.allowed ? "Invalid visual bounds or rotation" : site.reason;
    return false;
  }
  XMATRIX44 rotation;
  XMatRotationY(rotation, Deg2Rad(yaw));
  const AABB rotated = modelBounds.Transformed(rotation);
  const XVECTOR3 size = rotated.vMax - rotated.vMin;
  if (!std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z) ||
      size.x <= 0.000001f || size.y <= 0.000001f || size.z <= 0.000001f) {
    if (error) *error = "Model must have finite nonzero bounds on all axes";
    return false;
  }
  const float cell = terrain.placement_grid.cell_size;
  const float scale = (std::min)({placement.width * cell / size.x, placement.height / size.y, placement.depth * cell / size.z});
  const auto center = rotated.Center();
  XMATRIX44 scaling, translation;
  XMatScaling(scaling, scale, scale, scale);
  XMatTranslation(translation,
      (placement.cell_x + placement.width * 0.5f) * cell - center.x * scale,
      site.baseHeight - rotated.vMin.y * scale,
      (placement.cell_z + placement.depth * 0.5f) * cell - center.z * scale);
  transform = scaling * rotation * translation;
  return true;
}

bool AppendTerrainBlockouts(const scene::SceneHeightmapDesc& terrain, MutableMeshSnapshot& mesh, std::string* error) {
  if (!ValidateTerrainPlacements(terrain, error)) return false;
  constexpr unsigned faces[6][4] = {{4, 7, 6, 5}, {0, 1, 2, 3}, {0, 4, 5, 1}, {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 5, 6, 2}};
  const XVECTOR3 normals[] = {XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f), XVECTOR3(0.0f, -1.0f, 0.0f, 0.0f),
      XVECTOR3(0.0f, 0.0f, -1.0f, 0.0f), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f),
      XVECTOR3(-1.0f, 0.0f, 0.0f, 0.0f), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f)};
  for (const auto& placement : terrain.placements) {
    const auto result = CheckTerrainPlacement(terrain, placement, placement.id);
    const float cell = terrain.placement_grid.cell_size;
    const float left = placement.cell_x * cell, top = placement.cell_z * cell;
    const float right = left + placement.width * cell, bottom = top + placement.depth * cell;
    const float base = result.baseHeight, upper = base + placement.height;
    const XVECTOR3 corners[] = {XVECTOR3(left, base, top, 1.0f), XVECTOR3(right, base, top, 1.0f),
        XVECTOR3(right, base, bottom, 1.0f), XVECTOR3(left, base, bottom, 1.0f),
        XVECTOR3(left, upper, top, 1.0f), XVECTOR3(right, upper, top, 1.0f),
        XVECTOR3(right, upper, bottom, 1.0f), XVECTOR3(left, upper, bottom, 1.0f)};
    const uint32_t firstIndex = static_cast<uint32_t>(mesh.indices.size());
    const uint32_t materialIndex = static_cast<uint32_t>(mesh.materials.size());
    MutableMeshMaterial material;
    material.baseColor = XVECTOR3(placement.color.x, placement.color.y, placement.color.z, 1.0f);
    mesh.materials.push_back(material);
    for (unsigned face = 0; face < 6; ++face) {
      const uint32_t firstVertex = static_cast<uint32_t>(mesh.vertices.size());
      for (unsigned corner = 0; corner < 4; ++corner) {
        MutableMeshVertex vertex;
        vertex.position = corners[faces[face][corner]];
        vertex.normal = normals[face];
        vertex.u = corner == 1 || corner == 2 ? 1.0f : 0.0f;
        vertex.v = corner >= 2 ? 1.0f : 0.0f;
        mesh.vertices.push_back(vertex);
      }
      mesh.indices.insert(mesh.indices.end(), {firstVertex, firstVertex + 1, firstVertex + 2,
          firstVertex, firstVertex + 2, firstVertex + 3});
    }
    mesh.sections.push_back({firstIndex, 36, materialIndex});
  }
  RecalculateMutableMeshBounds(mesh);
  return true;
}

}