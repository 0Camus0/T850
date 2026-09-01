#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <utils/xMaths.h>

namespace t850::terrain {

struct VoxelNavigationSettings {
  float agentRadius = 0.25f;
  float agentHeight = 1.4f;
  int minFeetY = 1;
  int maxFeetY = 63;
  int maxStepUp = 1;
  int maxStepDown = 1;
  int projectionHorizontalRadius = 2;
  int projectionVerticalRadius = 4;
  std::size_t maxVisitedNodes = 65536;
};

struct VoxelNavigationQuery {
  std::function<bool(int worldX, int worldZ)> isColumnLoaded;
  std::function<bool(int worldX, int worldY, int worldZ)> isSolid;
};

struct VoxelNavigationResult {
  bool success = false;
  std::vector<XVECTOR3> points;
  std::size_t visitedNodes = 0;
  std::string error;
};

VoxelNavigationResult FindVoxelPath(const XVECTOR3& startFeet,
                                    const XVECTOR3& endFeet,
                                    const VoxelNavigationSettings& settings,
                                    const VoxelNavigationQuery& query);

} // namespace t850::terrain