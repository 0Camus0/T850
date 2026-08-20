#include <pch.h>

#include <scene/MutableMeshData.h>

#include <cmath>

namespace t850 {
namespace {

bool IsFinite(const XVECTOR3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
      std::isfinite(value.z) && std::isfinite(value.w);
}

bool Fail(std::string* error, std::string message) {
  if (error) *error = std::move(message);
  return false;
}

} // namespace

void RecalculateMutableMeshBounds(MutableMeshSnapshot& snapshot) {
  snapshot.localBounds = AABB{};
  for (const MutableMeshVertex& vertex : snapshot.vertices) {
    snapshot.localBounds.ExpandToInclude(vertex.position);
  }
}

bool ValidateMutableMeshSnapshot(const MutableMeshSnapshot& snapshot, std::string* error) {
  if (snapshot.vertices.empty() || snapshot.indices.empty()) {
    if (snapshot.vertices.empty() && snapshot.indices.empty() && snapshot.sections.empty()) return true;
    return Fail(error, "mutable mesh vertices and indices must both be empty or non-empty");
  }
  if ((snapshot.indices.size() % 3u) != 0u) {
    return Fail(error, "mutable mesh index count must be divisible by three");
  }
  if (snapshot.sections.empty()) {
    return Fail(error, "mutable mesh with geometry requires at least one section");
  }
  if (snapshot.materials.empty()) {
    return Fail(error, "mutable mesh with geometry requires at least one material");
  }
  if (!snapshot.localBounds.IsValid()) {
    return Fail(error, "mutable mesh bounds are invalid");
  }

  for (const MutableMeshVertex& vertex : snapshot.vertices) {
    if (!IsFinite(vertex.position) || !IsFinite(vertex.normal) ||
        !std::isfinite(vertex.u) || !std::isfinite(vertex.v)) {
      return Fail(error, "mutable mesh contains a non-finite vertex");
    }
  }
  for (uint32_t index : snapshot.indices) {
    if (index >= snapshot.vertices.size()) {
      return Fail(error, "mutable mesh index references a missing vertex");
    }
  }
  for (const MutableMeshSection& section : snapshot.sections) {
    const uint64_t sectionEnd = static_cast<uint64_t>(section.firstIndex) + section.indexCount;
    if (section.indexCount == 0 || (section.indexCount % 3u) != 0u ||
        sectionEnd > snapshot.indices.size()) {
      return Fail(error, "mutable mesh section has an invalid index range");
    }
    if (section.materialIndex >= snapshot.materials.size()) {
      return Fail(error, "mutable mesh section references a missing material");
    }
  }
  for (const MutableMeshMaterial& material : snapshot.materials) {
    if (!IsFinite(material.baseColor) || !IsFinite(material.emissiveColor) ||
        !std::isfinite(material.metallic) || !std::isfinite(material.roughness) ||
        !std::isfinite(material.alphaCutoff)) {
      return Fail(error, "mutable mesh contains a non-finite material value");
    }
  }
  return true;
}

} // namespace t850
