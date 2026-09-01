#pragma once

#include <functional>

#include <physics/CharacterController.h>

namespace t850::terrain {

struct VoxelCollisionQuery {
  std::function<bool(int worldX, int worldY, int worldZ)> isBlocked;
};

bool SweepVoxelBox(const CharacterBoxSweep& sweep,
                   const VoxelCollisionQuery& query,
                   CharacterCollisionHit& outHit);

} // namespace t850::terrain