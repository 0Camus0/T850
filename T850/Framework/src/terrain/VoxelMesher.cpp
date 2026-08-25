#include <pch.h>

#include <terrain/VoxelMesher.h>

#include <algorithm>
#include <array>
#include <unordered_map>

namespace t850::terrain {
namespace {

struct MaskCell {
  BlockId block = kAirBlock;
  bool backFace = false;

  friend bool operator==(const MaskCell&, const MaskCell&) = default;
};

struct Bucket {
  BlockId block = kAirBlock;
  std::vector<MutableMeshVertex> vertices;
  std::vector<uint32_t> indices;
};

} // namespace

bool BuildGreedyVoxelMesh(const VoxelChunk& chunk,
                          const BlockRegistry& registry,
                          const NeighborBlockSampler& neighborSampler,
                          MutableMeshSnapshot& output,
                          std::string* error) {
  output = MutableMeshSnapshot{};
  output.version = chunk.Version();
  const ChunkDimensions dimensions = chunk.Dimensions();
  const int size[3] = {dimensions.x, dimensions.y, dimensions.z};
  const int maxMaskCells = (std::max)({
      size[0] * size[1], size[1] * size[2], size[2] * size[0]});
  std::vector<MaskCell> mask(static_cast<std::size_t>(maxMaskCells));
  std::vector<Bucket> buckets;
  std::unordered_map<BlockId, std::size_t> bucketByBlock;

  auto sample = [&](int x, int y, int z) {
    if (chunk.InBounds(x, y, z)) return chunk.Get(x, y, z);
    return neighborSampler ? neighborSampler(x, y, z) : kAirBlock;
  };
  auto bucketFor = [&](BlockId block) -> Bucket& {
    const auto found = bucketByBlock.find(block);
    if (found != bucketByBlock.end()) return buckets[found->second];
    const std::size_t index = buckets.size();
    bucketByBlock.emplace(block, index);
    buckets.push_back(Bucket{.block = block});
    return buckets.back();
  };

  int x[3] = {};
  int q[3] = {};
  for (int axis = 0; axis < 3; ++axis) {
    const int u = (axis + 1) % 3;
    const int v = (axis + 2) % 3;
    q[0] = q[1] = q[2] = 0;
    q[axis] = 1;
    x[0] = x[1] = x[2] = 0;

    for (x[axis] = -1; x[axis] < size[axis];) {
      std::size_t maskIndex = 0;
      for (x[v] = 0; x[v] < size[v]; ++x[v]) {
        for (x[u] = 0; x[u] < size[u]; ++x[u]) {
          const BlockId before = x[axis] >= 0 ? sample(x[0], x[1], x[2]) : kAirBlock;
          const BlockId after = sample(x[0] + q[0], x[1] + q[1], x[2] + q[2]);
          const BlockDefinition& beforeDef = registry.Get(before);
          const BlockDefinition& afterDef = registry.Get(after);
          MaskCell cell;
          if (beforeDef.renderable && !afterDef.occludes) {
            cell = MaskCell{before, false};
          } else if (afterDef.renderable && !beforeDef.occludes) {
            cell = MaskCell{after, true};
          }
          mask[maskIndex++] = cell;
        }
      }
      ++x[axis];

      maskIndex = 0;
      for (int j = 0; j < size[v]; ++j) {
        for (int i = 0; i < size[u];) {
          const MaskCell cell = mask[maskIndex];
          if (cell.block == kAirBlock) {
            ++i;
            ++maskIndex;
            continue;
          }

          const BlockDefinition& definition = registry.Get(cell.block);
          const bool mergeFaces = !definition.usesBaseColorTexture;
          int width = 1;
          while (mergeFaces && i + width < size[u] && mask[maskIndex + width] == cell) ++width;
          int height = 1;
          bool stop = false;
          while (mergeFaces && j + height < size[v] && !stop) {
            for (int offset = 0; offset < width; ++offset) {
              if (!(mask[maskIndex + offset + height * size[u]] == cell)) {
                stop = true;
                break;
              }
            }
            if (!stop) ++height;
          }

          x[u] = i;
          x[v] = j;
          int du[3] = {};
          int dv[3] = {};
          du[u] = width;
          dv[v] = height;
          XVECTOR3 normal(0.0f, 0.0f, 0.0f, 0.0f);
          if (axis == 0) normal.x = cell.backFace ? -1.0f : 1.0f;
          else if (axis == 1) normal.y = cell.backFace ? -1.0f : 1.0f;
          else normal.z = cell.backFace ? -1.0f : 1.0f;
          const XVECTOR3 p0(static_cast<float>(x[0]), static_cast<float>(x[1]), static_cast<float>(x[2]), 1.0f);
          const XVECTOR3 p1(static_cast<float>(x[0] + du[0]), static_cast<float>(x[1] + du[1]), static_cast<float>(x[2] + du[2]), 1.0f);
          const XVECTOR3 p2(static_cast<float>(x[0] + du[0] + dv[0]), static_cast<float>(x[1] + du[1] + dv[1]), static_cast<float>(x[2] + du[2] + dv[2]), 1.0f);
          const XVECTOR3 p3(static_cast<float>(x[0] + dv[0]), static_cast<float>(x[1] + dv[1]), static_cast<float>(x[2] + dv[2]), 1.0f);

          Bucket& bucket = bucketFor(cell.block);
          const uint32_t base = static_cast<uint32_t>(bucket.vertices.size());
          const float u0 = definition.usesBaseColorTexture ? definition.atlasU0 : 0.0f;
          const float v0 = definition.usesBaseColorTexture ? definition.atlasV0 : 0.0f;
          const float u1 = definition.usesBaseColorTexture ? definition.atlasU1 : static_cast<float>(width);
          const float v1 = definition.usesBaseColorTexture ? definition.atlasV1 : static_cast<float>(height);
          bucket.vertices.push_back(MutableMeshVertex{p0, normal, u0, v0});
          bucket.vertices.push_back(MutableMeshVertex{p1, normal, u1, v0});
          bucket.vertices.push_back(MutableMeshVertex{p2, normal, u1, v1});
          bucket.vertices.push_back(MutableMeshVertex{p3, normal, u0, v1});
          if (cell.backFace) {
            bucket.indices.insert(bucket.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
          } else {
            bucket.indices.insert(bucket.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
          }

          for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
              mask[maskIndex + column + row * size[u]] = MaskCell{};
            }
          }
          i += width;
          maskIndex += width;
        }
      }
    }
  }

  for (Bucket& bucket : buckets) {
    const BlockDefinition& definition = registry.Get(bucket.block);
    const uint32_t vertexBase = static_cast<uint32_t>(output.vertices.size());
    const uint32_t firstIndex = static_cast<uint32_t>(output.indices.size());
    output.vertices.insert(output.vertices.end(), bucket.vertices.begin(), bucket.vertices.end());
    for (uint32_t index : bucket.indices) output.indices.push_back(vertexBase + index);
    MutableMeshMaterial material;
    material.baseColor = definition.color;
    material.metallic = definition.metallic;
    material.roughness = definition.roughness;
    material.alphaMode = definition.alphaMode;
    material.alphaCutoff = definition.alphaCutoff;
    material.doubleSided = definition.doubleSided;
    material.usesBaseColorTexture = definition.usesBaseColorTexture;
    material.unlit = definition.unlit;
    output.materials.push_back(material);
    output.sections.push_back(MutableMeshSection{
        .firstIndex = firstIndex,
        .indexCount = static_cast<uint32_t>(bucket.indices.size()),
        .materialIndex = static_cast<uint32_t>(output.materials.size() - 1)});
  }
  RecalculateMutableMeshBounds(output);
  return ValidateMutableMeshSnapshot(output, error);
}

} // namespace t850::terrain
