#include <pch.h>
#include <scene/SceneRegions.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace t850::scene {

bool ValidateSceneRegions(std::span<const SceneRegionDesc> regions, std::string* error) {
  std::set<std::string> ids;
  for (const auto& region : regions) {
    bool valid = !region.id.empty() && ids.insert(region.id).second;
    for (float value : {region.position.x, region.position.y, region.position.z,
        region.rotation.x, region.rotation.y, region.rotation.z,
        region.half_extents.x, region.half_extents.y, region.half_extents.z}) valid &= std::isfinite(value);
    valid &= region.half_extents.x > 0 && region.half_extents.y > 0 && region.half_extents.z > 0;
    if (!valid) {
      if (error) *error = "Region requires a unique nonempty ID, finite transform, and positive extents: " + region.name;
      return false;
    }
  }
  return true;
}

bool RegionContainsPoint(const SceneRegionDesc& region, const XVECTOR3& point) {
  if (!region.enabled || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return false;
  XMATRIX44 rotateX, rotateY, rotateZ;
  XMatRotationX(rotateX, Deg2Rad(region.rotation.x));
  XMatRotationY(rotateY, Deg2Rad(region.rotation.y));
  XMatRotationZ(rotateZ, Deg2Rad(region.rotation.z));
  const XMATRIX44 rotation = rotateX * rotateY * rotateZ;
  const XVECTOR3 relative(point.x - region.position.x, point.y - region.position.y, point.z - region.position.z, 0.0f);
  const float localX = relative.x * rotation.m11 + relative.y * rotation.m12 + relative.z * rotation.m13;
  const float localY = relative.x * rotation.m21 + relative.y * rotation.m22 + relative.z * rotation.m23;
  const float localZ = relative.x * rotation.m31 + relative.y * rotation.m32 + relative.z * rotation.m33;
  return std::abs(localX) <= region.half_extents.x && std::abs(localY) <= region.half_extents.y &&
      std::abs(localZ) <= region.half_extents.z;
}

std::vector<std::string> QuerySceneRegions(std::span<const SceneRegionDesc> regions,
    const XVECTOR3& point, std::string_view tag) {
  std::vector<std::string> result;
  for (const auto& region : regions) {
    if (RegionContainsPoint(region, point) && (tag.empty() || std::find(region.tags.begin(), region.tags.end(), tag) != region.tags.end()))
      result.push_back(region.id);
  }
  return result;
}

}