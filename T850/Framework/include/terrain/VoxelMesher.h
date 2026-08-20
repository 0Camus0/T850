#pragma once

#include <terrain/VoxelChunk.h>

#include <functional>
#include <string>

namespace t850::terrain {

using NeighborBlockSampler = std::function<BlockId(int localX, int localY, int localZ)>;

bool BuildGreedyVoxelMesh(const VoxelChunk& chunk,
                          const BlockRegistry& registry,
                          const NeighborBlockSampler& neighborSampler,
                          MutableMeshSnapshot& output,
                          std::string* error = nullptr);

} // namespace t850::terrain
