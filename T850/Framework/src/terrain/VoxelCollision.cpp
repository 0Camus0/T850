#include <pch.h>

#include <terrain/VoxelCollision.h>

#include <algorithm>
#include <cmath>

namespace t850::terrain {
namespace {

bool IntersectAxis(float origin, float delta, float boxMin, float boxMax,
                   const XVECTOR3& negativeNormal, const XVECTOR3& positiveNormal,
                   float& enter, float& exit, XVECTOR3& normal) {
  constexpr float epsilon = 0.0000001f;
  if (std::fabs(delta) <= epsilon)
    return origin >= boxMin && origin <= boxMax;
  const float nearDistance = delta > 0.0f ? boxMin - origin : boxMax - origin;
  const float farDistance = delta > 0.0f ? boxMax - origin : boxMin - origin;
  const float nearTime = nearDistance / delta;
  const float farTime = farDistance / delta;
  if (nearTime > enter) {
    enter = nearTime;
    normal = delta > 0.0f ? negativeNormal : positiveNormal;
  }
  exit = (std::min)(exit, farTime);
  return enter <= exit;
}

XVECTOR3 InitialContactNormal(const XVECTOR3& start,
                              const XVECTOR3& expandedMin,
                              const XVECTOR3& expandedMax) {
  float nearestDistance = start.x - expandedMin.x;
  XVECTOR3 normal(-1.0f, 0.0f, 0.0f, 0.0f);
  auto useIfNearer = [&](float distance, const XVECTOR3& candidate) {
    if (distance < nearestDistance) {
      nearestDistance = distance;
      normal = candidate;
    }
  };
  useIfNearer(expandedMax.x - start.x, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  useIfNearer(start.y - expandedMin.y, XVECTOR3(0.0f, -1.0f, 0.0f, 0.0f));
  useIfNearer(expandedMax.y - start.y, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  useIfNearer(start.z - expandedMin.z, XVECTOR3(0.0f, 0.0f, -1.0f, 0.0f));
  useIfNearer(expandedMax.z - start.z, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
  return normal;
}

} // namespace

bool SweepVoxelBox(const CharacterBoxSweep& sweep,
                   const VoxelCollisionQuery& query,
                   CharacterCollisionHit& outHit) {
  outHit = CharacterCollisionHit{};
  if (!query.isBlocked) return false;
  const XVECTOR3& start = sweep.startCenter;
  const XVECTOR3& displacement = sweep.displacement;
  const XVECTOR3& half = sweep.halfExtents;
  if (displacement.x * displacement.x + displacement.y * displacement.y +
      displacement.z * displacement.z < 0.00000001f) return false;

  const XVECTOR3 end = start + displacement;
  const int minX = static_cast<int>(std::floor((std::min)(start.x, end.x) - half.x));
  const int maxX = static_cast<int>(std::floor((std::max)(start.x, end.x) + half.x));
  const int minY = static_cast<int>(std::floor((std::min)(start.y, end.y) - half.y));
  const int maxY = static_cast<int>(std::floor((std::max)(start.y, end.y) + half.y));
  const int minZ = static_cast<int>(std::floor((std::min)(start.z, end.z) - half.z));
  const int maxZ = static_cast<int>(std::floor((std::max)(start.z, end.z) + half.z));

  bool hit = false;
  float bestFraction = 1.0f;
  XVECTOR3 bestNormal(0.0f, 1.0f, 0.0f, 0.0f);
  for (int y = minY; y <= maxY; ++y) {
    for (int z = minZ; z <= maxZ; ++z) {
      for (int x = minX; x <= maxX; ++x) {
        if (!query.isBlocked(x, y, z)) continue;
        const XVECTOR3 expandedMin(static_cast<float>(x) - half.x,
                                   static_cast<float>(y) - half.y,
                                   static_cast<float>(z) - half.z, 1.0f);
        const XVECTOR3 expandedMax(static_cast<float>(x + 1) + half.x,
                                   static_cast<float>(y + 1) + half.y,
                                   static_cast<float>(z + 1) + half.z, 1.0f);
        float enter = 0.0f;
        float exit = 1.0f;
        XVECTOR3 normal(0.0f, 0.0f, 0.0f, 0.0f);
        if (!IntersectAxis(start.x, displacement.x, expandedMin.x, expandedMax.x,
                           XVECTOR3(-1.0f, 0.0f, 0.0f, 0.0f), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f),
                           enter, exit, normal) ||
            !IntersectAxis(start.y, displacement.y, expandedMin.y, expandedMax.y,
                           XVECTOR3(0.0f, -1.0f, 0.0f, 0.0f), XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f),
                           enter, exit, normal) ||
            !IntersectAxis(start.z, displacement.z, expandedMin.z, expandedMax.z,
                           XVECTOR3(0.0f, 0.0f, -1.0f, 0.0f), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f),
                           enter, exit, normal)) continue;
        if (exit < 0.0f || enter > bestFraction || enter > 1.0f) continue;
        if (normal.x * normal.x + normal.y * normal.y + normal.z * normal.z <= 0.00000001f)
          normal = InitialContactNormal(start, expandedMin, expandedMax);
        if (!hit || enter < bestFraction) {
          hit = true;
          bestFraction = (std::max)(0.0f, enter);
          bestNormal = normal;
        }
      }
    }
  }

  if (!hit) return false;
  outHit.hit = true;
  outHit.fraction = bestFraction;
  outHit.position = start + displacement * bestFraction;
  outHit.normal = bestNormal;
  return true;
}

} // namespace t850::terrain