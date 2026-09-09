#include <pch.h>
#include <terrain/HeightmapTerrain.h>
#include <terrain/TerrainPlacement.h>
#include <utils/ResourceLocator.h>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096
#include "../../../Librerias/terrain-stb/stb_image.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace t850 {
namespace {
bool Fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}

bool ValidateDescriptor(const scene::SceneHeightmapDesc& desc, std::string* error) {
  uint32_t gridColumns = 0, gridRows = 0;
  if (desc.placement_grid.enabled && !TerrainGridDimensions(desc, gridColumns, gridRows))
    return Fail(error, "Placement grid requires positive cell size, valid flatness tolerances, and 1..1024 cells per axis");
  if (desc.samples_x < 2 || desc.samples_z < 2 || desc.samples_x > 1025 || desc.samples_z > 1025)
    return Fail(error, "Heightmap grid dimensions must be between 2 and 1025");
  if (!std::isfinite(desc.size_x) || desc.size_x <= 0.0f || desc.size_x > 1000000.0f ||
      !std::isfinite(desc.size_z) || desc.size_z <= 0.0f || desc.size_z > 1000000.0f ||
      !std::isfinite(desc.height_scale) || std::abs(desc.height_scale) > 1000000.0f ||
      !std::isfinite(desc.height_offset) || std::abs(desc.height_offset) > 1000000.0f ||
      !std::isfinite(desc.uv_scale) || desc.uv_scale <= 0.0f || desc.uv_scale > 1000000.0f)
    return Fail(error, "Heightmap size, elevation and UV values must be finite and within engine limits");
  for (float color : {desc.base_color.x, desc.base_color.y, desc.base_color.z}) {
    if (!std::isfinite(color) || color < 0.0f || color > 1.0f)
      return Fail(error, "Heightmap base color must be in [0, 1]");
  }
  if (desc.lod_levels < 1 || desc.lod_levels > 5 || !std::isfinite(desc.lod_distance) || desc.lod_distance <= 0.0f)
    return Fail(error, "Terrain LOD requires 1..5 levels and a positive finite distance");
  const size_t vertices = static_cast<size_t>(desc.samples_x) * desc.samples_z;
  const size_t cells = static_cast<size_t>(desc.samples_x - 1) * (desc.samples_z - 1);
  if (!desc.elevations.empty() && desc.elevations.size() != vertices)
    return Fail(error, "Terrain elevations must match the authored grid dimensions");
  for (float elevation : desc.elevations) {
    if (!std::isfinite(elevation) || std::abs(elevation) > 1000000.0f)
      return Fail(error, "Terrain elevations must be finite and within engine limits");
  }
  if (desc.materials.size() > 16) return Fail(error, "Terrain supports at most 16 materials");
  for (const auto& material : desc.materials) {
    for (float value : {material.color.x, material.color.y, material.color.z, material.roughness, material.metallic}) {
      if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
        return Fail(error, "Terrain material values must be in [0, 1]");
    }
  }
  if (!desc.cell_materials.empty() && desc.cell_materials.size() != cells)
    return Fail(error, "Terrain material map must match its cell dimensions");
  for (uint32_t material : desc.cell_materials) {
    if (material >= (std::max)(size_t{1}, desc.materials.size()))
      return Fail(error, "Terrain cell references an unknown material");
  }
  return true;
}
}

bool BuildHeightmapTerrain(const scene::SceneHeightmapDesc& desc,
    std::span<const float> heights, uint32_t width, uint32_t height,
    MutableMeshSnapshot& output, std::string* error) {
  if (error) error->clear();
  if (!ValidateDescriptor(desc, error)) return false;
  if (width < 2 || height < 2 || width > 4096 || height > 4096 ||
      heights.size() != static_cast<size_t>(width) * height)
    return Fail(error, "Heightmap image dimensions must be between 2 and 4096 and match its samples");
  for (float value : heights) {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
      return Fail(error, "Heightmap samples must be finite normalized values in [0, 1]");
  }
  MutableMeshSnapshot result;
  result.version = 1;
  result.vertices.resize(static_cast<size_t>(desc.samples_x) * desc.samples_z);
  result.indices.reserve(static_cast<size_t>(desc.samples_x - 1) * (desc.samples_z - 1) * 6);
  for (uint32_t row = 0; row < desc.samples_z; ++row) {
    for (uint32_t column = 0; column < desc.samples_x; ++column) {
      const float horizontal = static_cast<float>(column) / (desc.samples_x - 1);
      const float vertical = static_cast<float>(row) / (desc.samples_z - 1);
      const float imageX = horizontal * (width - 1);
      const float imageY = vertical * (height - 1);
      const uint32_t left = static_cast<uint32_t>(imageX);
      const uint32_t top = static_cast<uint32_t>(imageY);
      const uint32_t right = (std::min)(left + 1, width - 1);
      const uint32_t bottom = (std::min)(top + 1, height - 1);
      const float upper = std::lerp(heights[top * width + left], heights[top * width + right], imageX - left);
      const float lower = std::lerp(heights[bottom * width + left], heights[bottom * width + right], imageX - left);
      auto& vertex = result.vertices[row * desc.samples_x + column];
      vertex.position = XVECTOR3(horizontal * desc.size_x,
          desc.elevations.empty() ? desc.height_offset + std::lerp(upper, lower, imageY - top) * desc.height_scale
            : desc.elevations[row * desc.samples_x + column],
          vertical * desc.size_z, 1.0f);
      vertex.normal = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      vertex.u = horizontal * desc.uv_scale;
      vertex.v = vertical * desc.uv_scale;
      if (column + 1 < desc.samples_x && row + 1 < desc.samples_z) {
        const uint32_t corner = row * desc.samples_x + column;
        result.indices.insert(result.indices.end(), {corner, corner + desc.samples_x, corner + 1,
            corner + 1, corner + desc.samples_x, corner + desc.samples_x + 1});
      }
    }
  }
  for (size_t triangle = 0; triangle < result.indices.size(); triangle += 3) {
    auto& first = result.vertices[result.indices[triangle]];
    auto& second = result.vertices[result.indices[triangle + 1]];
    auto& third = result.vertices[result.indices[triangle + 2]];
    const XVECTOR3 edgeOne = second.position - first.position;
    const XVECTOR3 edgeTwo = third.position - first.position;
    const XVECTOR3 normal(edgeOne.y * edgeTwo.z - edgeOne.z * edgeTwo.y,
        edgeOne.z * edgeTwo.x - edgeOne.x * edgeTwo.z,
        edgeOne.x * edgeTwo.y - edgeOne.y * edgeTwo.x, 0.0f);
    first.normal = first.normal + normal;
    second.normal = second.normal + normal;
    third.normal = third.normal + normal;
  }
  for (auto& vertex : result.vertices) {
    const double length = std::sqrt(static_cast<double>(vertex.normal.x) * vertex.normal.x +
        static_cast<double>(vertex.normal.y) * vertex.normal.y +
        static_cast<double>(vertex.normal.z) * vertex.normal.z);
    if (length <= 0.0 || !std::isfinite(length)) return Fail(error, "Heightmap generated degenerate geometry");
    vertex.normal = XVECTOR3(static_cast<float>(vertex.normal.x / length),
        static_cast<float>(vertex.normal.y / length), static_cast<float>(vertex.normal.z / length), 0.0f);
  }
  MutableMeshMaterial material;
  material.baseColor = XVECTOR3(desc.base_color.x, desc.base_color.y, desc.base_color.z, 1.0f);
  result.materials.push_back(material);
  if (!desc.materials.empty()) {
    result.materials.clear();
    for (const auto& layer : desc.materials) {
      MutableMeshMaterial painted;
      painted.baseColor = XVECTOR3(layer.color.x, layer.color.y, layer.color.z, 1.0f);
      painted.roughness = layer.roughness;
      painted.metallic = layer.metallic;
      painted.baseColorTexture = layer.texture;
      painted.usesBaseColorTexture = !layer.texture.empty();
      result.materials.push_back(painted);
    }
  }
  if (desc.cell_materials.empty()) {
    result.sections.push_back({0, static_cast<uint32_t>(result.indices.size()), 0});
  } else {
    std::vector<uint32_t> indices;
    indices.reserve(result.indices.size());
    for (uint32_t layer = 0; layer < result.materials.size(); ++layer) {
      const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
      for (size_t cell = 0; cell < desc.cell_materials.size(); ++cell) {
        if (desc.cell_materials[cell] == layer)
          indices.insert(indices.end(), result.indices.begin() + cell * 6, result.indices.begin() + cell * 6 + 6);
      }
      const uint32_t count = static_cast<uint32_t>(indices.size()) - firstIndex;
      if (count) result.sections.push_back({firstIndex, count, layer});
    }
    result.indices = std::move(indices);
  }
  if (!desc.placements.empty()) {
    auto placementTerrain = desc;
    if (placementTerrain.elevations.empty()) {
      placementTerrain.elevations.reserve(result.vertices.size());
      for (const auto& vertex : result.vertices) placementTerrain.elevations.push_back(vertex.position.y);
    }
    if (!AppendTerrainBlockouts(placementTerrain, result, error)) return false;
  }
  RecalculateMutableMeshBounds(result);
  if (!ValidateMutableMeshSnapshot(result, error)) return false;
  output = std::move(result);
  return true;
}

bool LoadHeightmapTerrain(const scene::SceneHeightmapDesc& desc,
    MutableMeshSnapshot& output, std::string* error) {
  if (error) error->clear();
  if (!ValidateDescriptor(desc, error)) return false;
  std::vector<unsigned char> bytes;
  if (!desc.elevations.empty() || desc.image.empty()) {
    const std::array<float, 4> flat{};
    return BuildHeightmapTerrain(desc, flat, 2, 2, output, error);
  }
  if (desc.image.empty() || !ResourceLocator::Instance().ReadBinary(desc.image, bytes))
    return Fail(error, "Heightmap image is missing or unreadable");
  if (bytes.empty() || bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
    return Fail(error, "Heightmap image is empty or too large");
  int width = 0;
  int height = 0;
  int channels = 0;
  const int byteCount = static_cast<int>(bytes.size());
  if (!stbi_info_from_memory(bytes.data(), byteCount, &width, &height, &channels) ||
      width < 2 || height < 2 || width > 4096 || height > 4096 ||
      stbi_is_hdr_from_memory(bytes.data(), byteCount))
    return Fail(error, "Heightmap requires an LDR image between 2 and 4096 pixels per axis");
    std::unique_ptr<stbi_us, decltype(&stbi_image_free)> pixels(
      stbi_load_16_from_memory(bytes.data(), byteCount, &width, &height, &channels, 1), stbi_image_free);
  if (!pixels) return Fail(error, "Heightmap image decoding failed");
  std::vector<float> heights(static_cast<size_t>(width) * height);
  for (size_t index = 0; index < heights.size(); ++index) heights[index] = pixels.get()[index] / 65535.0f;
  return BuildHeightmapTerrain(desc, heights, width, height, output, error);
}

bool InitializeTerrainEditing(scene::SceneHeightmapDesc& desc, std::string* error) {
  if (!ValidateDescriptor(desc, error)) return false;
  if (!desc.elevations.empty()) return true;
  MutableMeshSnapshot mesh;
  if (!LoadHeightmapTerrain(desc, mesh, error)) return false;
  std::vector<float> elevations;
  const size_t gridVertexCount = static_cast<size_t>(desc.samples_x) * desc.samples_z;
  elevations.reserve(gridVertexCount);
  for (size_t index = 0; index < gridVertexCount; ++index) elevations.push_back(mesh.vertices[index].position.y);
  desc.elevations = std::move(elevations);
  return true;
}

bool ApplyTerrainBrush(scene::SceneHeightmapDesc& desc, const TerrainBrush& brush, bool* changed, std::string* error) {
  if (changed) *changed = false;
  if (error) error->clear();
  if (!std::isfinite(brush.x) || !std::isfinite(brush.z) || !std::isfinite(brush.radius) || brush.radius <= 0.0f ||
      !std::isfinite(brush.amount) || brush.amount < 0.0f || !std::isfinite(brush.targetHeight) ||
      !std::isfinite(brush.hardness) || brush.hardness < 0.0f || brush.hardness > 1.0f)
    return Fail(error, "Terrain brush parameters are invalid");
  if (!ValidateDescriptor(desc, error)) return false;
  if (brush.mode == TerrainBrushMode::Material && brush.material >= desc.materials.size())
    return Fail(error, "Select a terrain material before painting");
  if (brush.x + brush.radius < 0.0f || brush.z + brush.radius < 0.0f ||
      brush.x - brush.radius > desc.size_x || brush.z - brush.radius > desc.size_z || brush.amount == 0.0f) return true;
  auto result = desc;
  if (!InitializeTerrainEditing(result, error)) return false;
  const auto previous = result.elevations;
  if (result.cell_materials.empty()) result.cell_materials.resize(static_cast<size_t>(desc.samples_x - 1) * (desc.samples_z - 1));
  bool modified = false;
  const bool painting = brush.mode == TerrainBrushMode::Material;
  const uint32_t columns = desc.samples_x - (painting ? 1 : 0);
  const uint32_t rows = desc.samples_z - (painting ? 1 : 0);
  for (uint32_t row = 0; row < rows; ++row) {
    for (uint32_t column = 0; column < columns; ++column) {
      const float horizontal = (column + (painting ? 0.5f : 0.0f)) * desc.size_x / (desc.samples_x - 1);
      const float vertical = (row + (painting ? 0.5f : 0.0f)) * desc.size_z / (desc.samples_z - 1);
      const float distance = std::hypot(horizontal - brush.x, vertical - brush.z) / brush.radius;
      if (distance > 1.0f) continue;
      const float weight = distance <= brush.hardness ? 1.0f : (1.0f - distance) / (1.0f - brush.hardness);
      if (painting) {
        auto& material = result.cell_materials[row * columns + column];
        modified |= material != brush.material;
        material = brush.material;
        continue;
      }
      const size_t index = row * desc.samples_x + column;
      float value = previous[index];
      switch (brush.mode) {
        case TerrainBrushMode::Raise: value += brush.amount * weight; break;
        case TerrainBrushMode::Lower: value -= brush.amount * weight; break;
        case TerrainBrushMode::Flatten: value = std::lerp(value, brush.targetHeight, (std::min)(1.0f, brush.amount * weight)); break;
        case TerrainBrushMode::Smooth: {
          float total = 0.0f;
          unsigned count = 0;
          for (uint32_t neighborRow = row ? row - 1 : 0; neighborRow <= (std::min)(row + 1, desc.samples_z - 1); ++neighborRow)
            for (uint32_t neighborColumn = column ? column - 1 : 0; neighborColumn <= (std::min)(column + 1, desc.samples_x - 1); ++neighborColumn) {
              total += previous[neighborRow * desc.samples_x + neighborColumn];
              ++count;
            }
          value = std::lerp(value, total / count, (std::min)(1.0f, brush.amount * weight));
          break;
        }
        default: break;
      }
      if (!std::isfinite(value) || std::abs(value) > 1000000.0f) return Fail(error, "Brush exceeds terrain elevation limits");
      modified |= value != previous[index];
      result.elevations[index] = value;
    }
  }
  if (modified) desc = std::move(result);
  if (changed) *changed = modified;
  return true;
}
}