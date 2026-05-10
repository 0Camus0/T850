#include <pch.h>

#include <physics/PhysicsAuthoring.h>

#include <scene/PrimitiveInstance.h>
#include <scene/RenderMesh.h>
#include <scene/RenderSkinnedMesh.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>

namespace t850 {
namespace {

bool IsValidRenderBounds(const RenderMesh::AABB& bounds) {
  return bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
}

float Length(const XVECTOR3& v) {
  return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

XVECTOR3 Normalize(const XVECTOR3& v) {
  const float length = Length(v);
  if (length <= 0.000001f) {
    return XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
  }
  return XVECTOR3(v.x / length, v.y / length, v.z / length, 0.0f);
}

XVECTOR3 Cross(const XVECTOR3& a, const XVECTOR3& b) {
  return XVECTOR3(
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
      0.0f);
}

float Dot(const XVECTOR3& a, const XVECTOR3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float Clamp(float value, float minValue, float maxValue) {
  return (std::max)(minValue, (std::min)(value, maxValue));
}

XVECTOR3 TransformPhysicsPoint(const XVECTOR3& point, const XMATRIX44& matrix) {
  return XVECTOR3(
      point.x * matrix.m11 + point.y * matrix.m21 + point.z * matrix.m31 + matrix.m41,
      point.x * matrix.m12 + point.y * matrix.m22 + point.z * matrix.m32 + matrix.m42,
      point.x * matrix.m13 + point.y * matrix.m23 + point.z * matrix.m33 + matrix.m43,
      1.0f);
}

XMATRIX44 FlipMatrixZ(const XMATRIX44& matrix) {
  XMATRIX44 out = matrix;
  for (int i = 0; i < 4; ++i) {
    out.m[2][i] = -out.m[2][i];
    out.m[i][2] = -out.m[i][2];
  }
  out.m[2][2] = matrix.m[2][2];
  return out;
}

AABB ToPhysicsAABB(const RenderMesh::AABB& bounds) {
  return AABB(bounds.min, bounds.max);
}

XMATRIX44 MakeBodyTransform(const XVECTOR3& position, const XVECTOR3& localY) {
  const XVECTOR3 up = Normalize(localY);
  XVECTOR3 reference = std::fabs(Dot(up, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f))) > 0.92f
      ? XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f)
      : XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  XVECTOR3 right = Normalize(Cross(reference, up));
  XVECTOR3 forward = Normalize(Cross(up, right));

  XMATRIX44 out;
  out.m11 = right.x;   out.m12 = right.y;   out.m13 = right.z;   out.m14 = 0.0f;
  out.m21 = up.x;      out.m22 = up.y;      out.m23 = up.z;      out.m24 = 0.0f;
  out.m31 = forward.x; out.m32 = forward.y; out.m33 = forward.z; out.m34 = 0.0f;
  out.m41 = position.x; out.m42 = position.y; out.m43 = position.z; out.m44 = 1.0f;
  return out;
}

XMATRIX44 MakeCenteredBoxWorldTransform(const AABB& bounds, const XMATRIX44& worldFromMesh, XVECTOR3& outHalfExtents) {
  const XVECTOR3 center = bounds.Center();
  XVECTOR3 extents = bounds.Extents();

  const float sx = (std::max)(Length(XVECTOR3(worldFromMesh.m11, worldFromMesh.m12, worldFromMesh.m13, 0.0f)), 0.001f);
  const float sy = (std::max)(Length(XVECTOR3(worldFromMesh.m21, worldFromMesh.m22, worldFromMesh.m23, 0.0f)), 0.001f);
  const float sz = (std::max)(Length(XVECTOR3(worldFromMesh.m31, worldFromMesh.m32, worldFromMesh.m33, 0.0f)), 0.001f);

  outHalfExtents = XVECTOR3(
      (std::max)(0.001f, extents.x * sx),
      (std::max)(0.001f, extents.y * sy),
      (std::max)(0.001f, extents.z * sz),
      0.0f);

  XMATRIX44 transform = worldFromMesh;
  if (sx > 0.000001f) { transform.m11 /= sx; transform.m12 /= sx; transform.m13 /= sx; }
  if (sy > 0.000001f) { transform.m21 /= sy; transform.m22 /= sy; transform.m23 /= sy; }
  if (sz > 0.000001f) { transform.m31 /= sz; transform.m32 /= sz; transform.m33 /= sz; }

  const XVECTOR3 worldCenter = TransformPhysicsPoint(center, worldFromMesh);
  transform.m41 = worldCenter.x;
  transform.m42 = worldCenter.y;
  transform.m43 = worldCenter.z;
  return transform;
}

XVECTOR3 BonePosition(const xF::xBone& bone, const XMATRIX44& worldFromMesh) {
  const XMATRIX44 boneWorld = FlipMatrixZ(bone.Combined) * worldFromMesh;
  return XVECTOR3(boneWorld.m41, boneWorld.m42, boneWorld.m43, 1.0f);
}

XMATRIX44 BoneWorldTransform(const xF::xBone& bone, const XMATRIX44& worldFromMesh) {
  return FlipMatrixZ(bone.Combined) * worldFromMesh;
}

bool InvertAffine(const XMATRIX44& matrix, XMATRIX44& out) {
  const float a00 = matrix.m11, a01 = matrix.m12, a02 = matrix.m13;
  const float a10 = matrix.m21, a11 = matrix.m22, a12 = matrix.m23;
  const float a20 = matrix.m31, a21 = matrix.m32, a22 = matrix.m33;

  const float det =
      a00 * (a11 * a22 - a12 * a21) -
      a01 * (a10 * a22 - a12 * a20) +
      a02 * (a10 * a21 - a11 * a20);
  if (std::fabs(det) <= 0.000001f) {
    return false;
  }

  const float invDet = 1.0f / det;
  out.m11 =  (a11 * a22 - a12 * a21) * invDet;
  out.m12 =  (a02 * a21 - a01 * a22) * invDet;
  out.m13 =  (a01 * a12 - a02 * a11) * invDet;
  out.m14 = 0.0f;
  out.m21 =  (a12 * a20 - a10 * a22) * invDet;
  out.m22 =  (a00 * a22 - a02 * a20) * invDet;
  out.m23 =  (a02 * a10 - a00 * a12) * invDet;
  out.m24 = 0.0f;
  out.m31 =  (a10 * a21 - a11 * a20) * invDet;
  out.m32 =  (a01 * a20 - a00 * a21) * invDet;
  out.m33 =  (a00 * a11 - a01 * a10) * invDet;
  out.m34 = 0.0f;

  const float tx = matrix.m41;
  const float ty = matrix.m42;
  const float tz = matrix.m43;
  out.m41 = -(tx * out.m11 + ty * out.m21 + tz * out.m31);
  out.m42 = -(tx * out.m12 + ty * out.m22 + tz * out.m32);
  out.m43 = -(tx * out.m13 + ty * out.m23 + tz * out.m33);
  out.m44 = 1.0f;
  return true;
}

const xF::xSkeleton* FindReferenceSkeleton(const RenderSkinnedMesh& mesh) {
  if (mesh.xFile && !mesh.xFile->XMeshDataBase.empty() && mesh.xFile->XMeshDataBase[0]) {
    const xF::xMeshContainer* meshContainer = mesh.xFile->XMeshDataBase[0];
    if (!meshContainer->Skeleton.Bones.empty()) {
      return &meshContainer->Skeleton;
    }
    if (!meshContainer->SkeletonAnimated.Bones.empty()) {
      return &meshContainer->SkeletonAnimated;
    }
  }
  return mesh.GetAnimController().GetAnimSkeleton();
}

const xF::xSkeleton* FindAnimatedSkeleton(const RenderSkinnedMesh& mesh) {
  const xF::xSkeleton* skeleton = mesh.GetAnimController().GetAnimSkeleton();
  if (skeleton && !skeleton->Bones.empty()) {
    return skeleton;
  }
  return FindReferenceSkeleton(mesh);
}

XVECTOR3 Subtract(const XVECTOR3& a, const XVECTOR3& b) {
  return XVECTOR3(a.x - b.x, a.y - b.y, a.z - b.z, 0.0f);
}

XVECTOR3 AddScaled(const XVECTOR3& a, const XVECTOR3& b, float scale) {
  return XVECTOR3(a.x + b.x * scale, a.y + b.y * scale, a.z + b.z * scale, 1.0f);
}

float ReadComponent(const XVECTOR3& value, int component) {
  switch (component) {
    case 0: return value.x;
    case 1: return value.y;
    case 2: return value.z;
    default: return value.w;
  }
}

std::string LowerName(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (unsigned char c : name) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

bool NameContains(const std::string& name, const char* token) {
  return name.find(token) != std::string::npos;
}

bool NameContainsAny(const std::string& name, const std::initializer_list<const char*>& tokens) {
  for (const char* token : tokens) {
    if (NameContains(name, token)) {
      return true;
    }
  }
  return false;
}

bool IsExcludedHumanoidBoneName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {
      "roll", "twist", "armor", "weapon", "launcher", "blade", "serration", "guard",
      "thumb", "index", "middle", "ring", "pinky", "knuckle",
      "jaw", "tongue", "teeth", "lip", "brow", "nose", "nostril", "snarl",
      "cheek", "eye", "ear", "crease", "puff", "eyelid", "helmet"});
}

bool IsHumanoidRagdollBoneName(const std::string& lowerName) {
  if (lowerName.empty() || IsExcludedHumanoidBoneName(lowerName)) {
    return false;
  }

  if (NameContainsAny(lowerName, {"hips", "pelvis", "spine", "chest", "neck", "head", "clavicle"})) {
    return true;
  }
  if (NameContainsAny(lowerName, {"arm_upper", "upperarm", "upper_arm", "arm_lower", "lowerarm", "forearm", "lower_arm", "arm_hand"})) {
    return true;
  }
  if (NameContains(lowerName, "hand") && !NameContains(lowerName, "weapon")) {
    return true;
  }
  if (NameContainsAny(lowerName, {"leg_upper", "upperleg", "upper_leg", "thigh", "leg_lower", "lowerleg", "lower_leg", "calf", "shin", "leg_foot"})) {
    return true;
  }
  return NameContains(lowerName, "foot");
}

bool IsEndpointHelperForBone(const std::string& parentLowerName, const std::string& childLowerName) {
  if (NameContains(childLowerName, "end")) {
    return true;
  }
  if (NameContains(parentLowerName, "foot") && NameContainsAny(childLowerName, {"ball", "toe"})) {
    return true;
  }
  return false;
}

struct WeightedPoint {
  XVECTOR3 position;
  float weight = 1.0f;
};

struct WeightedScalar {
  float value = 0.0f;
  float weight = 1.0f;
};

float WeightedPercentile(std::vector<WeightedScalar> values, float percentile, float fallback) {
  if (values.empty()) {
    return fallback;
  }

  values.erase(
      std::remove_if(values.begin(), values.end(), [](const WeightedScalar& v) { return v.weight <= 0.0f; }),
      values.end());
  if (values.empty()) {
    return fallback;
  }

  std::sort(values.begin(), values.end(), [](const WeightedScalar& a, const WeightedScalar& b) {
    return a.value < b.value;
  });

  float totalWeight = 0.0f;
  for (const WeightedScalar& value : values) {
    totalWeight += value.weight;
  }
  if (totalWeight <= 0.0f) {
    return fallback;
  }

  const float target = Clamp(percentile, 0.0f, 1.0f) * totalWeight;
  float accumulated = 0.0f;
  for (const WeightedScalar& value : values) {
    accumulated += value.weight;
    if (accumulated >= target) {
      return value.value;
    }
  }
  return values.back().value;
}

std::vector<bool> SelectRagdollBones(const xF::xSkeleton& skeleton, const PhysicsRagdollBuildSettings& settings) {
  std::vector<bool> selected(skeleton.Bones.size(), true);
  if (!settings.preferHumanoidBones) {
    return selected;
  }

  uint32_t selectedCount = 0;
  for (std::size_t i = 0; i < skeleton.Bones.size(); ++i) {
    selected[i] = IsHumanoidRagdollBoneName(LowerName(skeleton.Bones[i].Name));
    if (selected[i]) {
      ++selectedCount;
    }
  }

  if (selectedCount < 6) {
    std::fill(selected.begin(), selected.end(), true);
  }
  return selected;
}

std::vector<std::vector<uint32_t>> BuildSkeletonChildrenFromParents(const xF::xSkeleton& skeleton) {
  std::vector<std::vector<uint32_t>> children(skeleton.Bones.size());
  for (uint32_t boneIndex = 0; boneIndex < skeleton.Bones.size(); ++boneIndex) {
    const xF::xBone& bone = skeleton.Bones[boneIndex];
    if (bone.Dad < skeleton.Bones.size() && bone.Dad != boneIndex) {
      children[bone.Dad].push_back(boneIndex);
    }
  }
  return children;
}

int FindNearestSelectedParent(const xF::xSkeleton& skeleton, uint32_t boneIndex, const std::vector<bool>& selected) {
  if (boneIndex >= skeleton.Bones.size()) {
    return -1;
  }

  uint32_t current = boneIndex;
  for (std::size_t depth = 0; depth < skeleton.Bones.size(); ++depth) {
    const xF::xBone& bone = skeleton.Bones[current];
    if (bone.Dad >= skeleton.Bones.size() || bone.Dad == current) {
      return -1;
    }
    current = bone.Dad;
    if (current < selected.size() && selected[current]) {
      return static_cast<int>(current);
    }
  }
  return -1;
}

int FindPrimaryChild(const xF::xSkeleton& skeleton,
                     uint32_t boneIndex,
                     const std::vector<std::vector<uint32_t>>& children,
                     const std::vector<bool>& selected,
                     const XMATRIX44& worldFromMesh,
                     const PhysicsRagdollBuildSettings& settings) {
  if (boneIndex >= skeleton.Bones.size()) {
    return -1;
  }

  const xF::xBone& bone = skeleton.Bones[boneIndex];
  for (uint32_t childIndex : bone.Sons) {
    if (childIndex < selected.size() && selected[childIndex]) {
      return static_cast<int>(childIndex);
    }
  }
  if (boneIndex < children.size()) {
    for (uint32_t childIndex : children[boneIndex]) {
      if (childIndex < selected.size() && selected[childIndex]) {
        return static_cast<int>(childIndex);
      }
    }
  }

  if (settings.preferHumanoidBones) {
    const std::string parentName = LowerName(bone.Name);
    for (uint32_t childIndex : bone.Sons) {
      if (childIndex >= skeleton.Bones.size()) {
        continue;
      }
      const std::string childName = LowerName(skeleton.Bones[childIndex].Name);
      if (IsEndpointHelperForBone(parentName, childName)) {
        return static_cast<int>(childIndex);
      }
    }
    if (boneIndex < children.size()) {
      for (uint32_t childIndex : children[boneIndex]) {
        if (childIndex >= skeleton.Bones.size()) {
          continue;
        }
        const std::string childName = LowerName(skeleton.Bones[childIndex].Name);
        if (IsEndpointHelperForBone(parentName, childName)) {
          return static_cast<int>(childIndex);
        }
      }
    }
    return -1;
  }

  float bestLength = 0.0f;
  int bestChild = -1;
  const XVECTOR3 boneWorld = BonePosition(bone, worldFromMesh);
  for (uint32_t childIndex : bone.Sons) {
    if (childIndex >= skeleton.Bones.size()) {
      continue;
    }
    const XVECTOR3 childWorld = BonePosition(skeleton.Bones[childIndex], worldFromMesh);
    const float length = Length(Subtract(childWorld, boneWorld));
    if (length > bestLength) {
      bestLength = length;
      bestChild = static_cast<int>(childIndex);
    }
  }
  if (boneIndex < children.size()) {
    for (uint32_t childIndex : children[boneIndex]) {
      if (childIndex >= skeleton.Bones.size()) {
        continue;
      }
      const XVECTOR3 childWorld = BonePosition(skeleton.Bones[childIndex], worldFromMesh);
      const float length = Length(Subtract(childWorld, boneWorld));
      if (length > bestLength) {
        bestLength = length;
        bestChild = static_cast<int>(childIndex);
      }
    }
  }
  return bestChild;
}

bool CollectSkinnedVertexSamples(const RenderSkinnedMesh& mesh,
                                 const XMATRIX44& worldFromMesh,
                                 std::size_t boneCount,
                                 const PhysicsRagdollBuildSettings& settings,
                                 std::vector<std::vector<WeightedPoint>>& outSamples) {
  outSamples.assign(boneCount, {});
  if (!settings.fitToSkinnedGeometry || !mesh.xFile || mesh.xFile->XMeshDataBase.empty() || !mesh.xFile->XMeshDataBase[0]) {
    return false;
  }

  const xF::xMeshContainer* meshContainer = mesh.xFile->XMeshDataBase[0];
  const std::size_t geometryCount = (std::min)(meshContainer->Geometry.size(), mesh.xFile->MeshInfo.size());
  for (std::size_t geometryIndex = 0; geometryIndex < geometryCount; ++geometryIndex) {
    const xF::xMeshGeometry& sourceGeometry = meshContainer->Geometry[geometryIndex];
    const xF::xFinalGeometry& finalGeometry = mesh.xFile->MeshInfo[geometryIndex];
    const bool hasSkin =
        (sourceGeometry.VertexAttributes & xF::xMeshGeometry::HAS_SKINWEIGHTS0) != 0 &&
        (sourceGeometry.VertexAttributes & xF::xMeshGeometry::HAS_SKININDEXES0) != 0 &&
        !sourceGeometry.SkinWeights.empty() &&
        !sourceGeometry.SkinIndices.empty();
    if (!hasSkin) {
      continue;
    }
    if ((!finalGeometry.pData || finalGeometry.VertexSize < sizeof(float) * 3u) && sourceGeometry.Positions.empty()) {
      continue;
    }

    std::size_t vertexCount = (std::min)(sourceGeometry.SkinWeights.size(), sourceGeometry.SkinIndices.size());
    if (!sourceGeometry.Positions.empty()) {
      vertexCount = (std::min)(vertexCount, sourceGeometry.Positions.size());
    }
    if (finalGeometry.pData && finalGeometry.VertexSize >= sizeof(float) * 3u) {
      vertexCount = (std::min)(vertexCount, static_cast<std::size_t>(finalGeometry.NumVertex));
    }
    if (vertexCount == 0) {
      continue;
    }

    const uint32_t strideFloats = finalGeometry.VertexSize / sizeof(float);
    for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
      XVECTOR3 localPosition;
      if (finalGeometry.pData && strideFloats >= 3u) {
        const float* vertex = finalGeometry.pData + vertexIndex * strideFloats;
        localPosition = XVECTOR3(vertex[0], vertex[1], vertex[2], 1.0f);
      } else {
        localPosition = sourceGeometry.Positions[vertexIndex];
        localPosition.w = 1.0f;
      }
      const XVECTOR3 worldPosition = TransformPhysicsPoint(localPosition, worldFromMesh);
      const XVECTOR3& weights = sourceGeometry.SkinWeights[vertexIndex];
      const XVECTOR3& indices = sourceGeometry.SkinIndices[vertexIndex];
      for (int component = 0; component < 4; ++component) {
        const float weight = ReadComponent(weights, component);
        if (weight < settings.minSkinWeight) {
          continue;
        }

        const int boneIndex = static_cast<int>(std::floor(ReadComponent(indices, component) + 0.5f));
        if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= outSamples.size()) {
          continue;
        }
        outSamples[static_cast<std::size_t>(boneIndex)].push_back({worldPosition, weight});
      }
    }
  }

  for (const std::vector<WeightedPoint>& samples : outSamples) {
    if (!samples.empty()) {
      return true;
    }
  }
  return false;
}

bool FitCapsuleToSamples(const std::vector<WeightedPoint>& samples,
                         const XVECTOR3& boneWorld,
                         const XVECTOR3& direction,
                         float length,
                         bool trimStart,
                         bool trimEnd,
                         const PhysicsRagdollBuildSettings& settings,
                         float& outStartT,
                         float& outEndT,
                         float& outRadius) {
  if (samples.size() < settings.minFitSamples || length < 0.0001f) {
    return false;
  }

  const float padding = (std::max)(0.0f, settings.projectionPadding) * length;
  const float minProjection = -padding;
  const float maxProjection = length + padding;
  std::vector<WeightedScalar> projections;
  projections.reserve(samples.size());

  for (const WeightedPoint& sample : samples) {
    const XVECTOR3 offset = Subtract(sample.position, boneWorld);
    const float t = Dot(offset, direction);
    if (t < minProjection || t > maxProjection) {
      continue;
    }
    projections.push_back({t, sample.weight});
  }

  if (projections.size() < settings.minFitSamples) {
    return false;
  }

  float startT = WeightedPercentile(projections, settings.projectionStartPercentile, 0.0f);
  float endT = WeightedPercentile(projections, settings.projectionEndPercentile, length);
  startT = Clamp(startT, minProjection, maxProjection);
  endT = Clamp(endT, minProjection, maxProjection);
  if (startT > endT) {
    std::swap(startT, endT);
  }

  const float trim = Clamp(settings.jointTrimFraction, 0.0f, 0.45f) * length;
  if (trimStart) {
    startT = (std::max)(startT, trim);
  }
  if (trimEnd) {
    endT = (std::min)(endT, length - trim);
  }
  if (endT - startT < settings.minBoneLength * 0.25f) {
    return false;
  }

  std::vector<WeightedScalar> radii;
  radii.reserve(samples.size());
  for (const WeightedPoint& sample : samples) {
    const XVECTOR3 offset = Subtract(sample.position, boneWorld);
    const float t = Dot(offset, direction);
    if (t < startT || t > endT) {
      continue;
    }

    const XVECTOR3 closest = AddScaled(boneWorld, direction, t);
    const float radius = Length(Subtract(sample.position, closest));
    radii.push_back({radius, sample.weight});
  }

  if (radii.size() < settings.minFitSamples) {
    radii.clear();
    for (const WeightedPoint& sample : samples) {
      const XVECTOR3 offset = Subtract(sample.position, boneWorld);
      const float t = Dot(offset, direction);
      if (t < minProjection || t > maxProjection) {
        continue;
      }
      const XVECTOR3 closest = AddScaled(boneWorld, direction, t);
      radii.push_back({Length(Subtract(sample.position, closest)), sample.weight});
    }
  }

  if (radii.size() < settings.minFitSamples) {
    return false;
  }

  outStartT = startT;
  outEndT = endT;
  outRadius = Clamp(
      WeightedPercentile(radii, settings.radiusPercentile, length * settings.radiusScale),
      settings.minRadius,
      settings.maxRadius);
  return true;
}

} // namespace

bool BuildMeshBoxBodyDesc(const RenderMesh& mesh,
                          const XMATRIX44& worldFromMesh,
                          uint32_t entityId,
                          PhysicsBodyMotion motion,
                          PhysicsBodyDesc& outDesc) {
  AABB combined;
  for (const RenderMesh::MeshInfo& info : mesh.Info) {
    if (IsValidRenderBounds(info.bounds)) {
      combined.ExpandToInclude(ToPhysicsAABB(info.bounds));
    }
  }

  if (!combined.IsValid()) {
    return false;
  }

  XVECTOR3 halfExtents;
  outDesc = PhysicsBodyDesc{};
  outDesc.entityId = entityId;
  outDesc.debugName = "mesh-box";
  outDesc.shape = PhysicsShapeDesc::Box(halfExtents);
  outDesc.worldTransform = MakeCenteredBoxWorldTransform(combined, worldFromMesh, halfExtents);
  outDesc.shape = PhysicsShapeDesc::Box(halfExtents);
  outDesc.motion = motion;
  return true;
}

bool AttachMeshBoxBody(JoltPhysicsSystem& physics,
                       PrimitiveInst& instance,
                       const RenderMesh& mesh,
                       PhysicsBodyMotion motion) {
  PhysicsBodyDesc desc;
  if (!BuildMeshBoxBodyDesc(mesh, instance.Final, instance.GetEntityId(), motion, desc)) {
    return false;
  }

  PhysicsBodyHandle handle = physics.CreateBody(desc);
  if (!handle.IsValid()) {
    return false;
  }

  instance.AttachPhysicsBody(handle);
  return true;
}

bool BuildStaticTriangleMeshBodyDesc(const RenderMesh& mesh,
                                     const XMATRIX44& worldFromMesh,
                                     uint32_t entityId,
                                     const PhysicsTriangleMeshCookSettings& settings,
                                     PhysicsTriangleMeshBodyDesc& outDesc,
                                     PhysicsCookStats* outStats) {
  const auto start = std::chrono::steady_clock::now();
  if (outStats) {
    *outStats = PhysicsCookStats{};
  }

  if (!mesh.xFile || mesh.xFile->XMeshDataBase.empty() || !mesh.xFile->XMeshDataBase[0]) {
    return false;
  }

  const xF::xMeshContainer* meshContainer = mesh.xFile->XMeshDataBase[0];
  if (meshContainer->Geometry.empty() || mesh.xFile->MeshInfo.empty()) {
    return false;
  }

  std::vector<XVECTOR3> worldVertices;
  std::vector<uint32_t> indices;
  AABB worldBounds;
  uint32_t vertexOffset = 0;

  const std::size_t geometryCount = (std::min)(mesh.xFile->MeshInfo.size(), meshContainer->Geometry.size());
  for (std::size_t geometryIndex = 0; geometryIndex < geometryCount; ++geometryIndex) {
    const xF::xFinalGeometry& finalGeometry = mesh.xFile->MeshInfo[geometryIndex];
    const xF::xMeshGeometry& sourceGeometry = meshContainer->Geometry[geometryIndex];
    if (!finalGeometry.pData || finalGeometry.VertexSize < sizeof(float) * 3u || finalGeometry.NumVertex == 0) {
      continue;
    }

    const uint32_t strideFloats = finalGeometry.VertexSize / sizeof(float);
    worldVertices.reserve(worldVertices.size() + finalGeometry.NumVertex);
    for (uint32_t vertexIndex = 0; vertexIndex < finalGeometry.NumVertex; ++vertexIndex) {
      const float* vertex = finalGeometry.pData + static_cast<std::size_t>(vertexIndex) * strideFloats;
      const XVECTOR3 worldPosition = TransformPhysicsPoint(XVECTOR3(vertex[0], vertex[1], vertex[2], 1.0f), worldFromMesh);
      worldVertices.push_back(worldPosition);
      worldBounds.ExpandToInclude(worldPosition);
    }

    if (sourceGeometry.Indices32Bit && !sourceGeometry.Triangles32.empty()) {
      indices.reserve(indices.size() + sourceGeometry.Triangles32.size());
      for (uint32_t index : sourceGeometry.Triangles32) {
        if (index < finalGeometry.NumVertex) {
          indices.push_back(vertexOffset + index);
        }
      }
    } else {
      indices.reserve(indices.size() + sourceGeometry.Triangles.size());
      for (uint16_t index : sourceGeometry.Triangles) {
        if (index < finalGeometry.NumVertex) {
          indices.push_back(vertexOffset + static_cast<uint32_t>(index));
        }
      }
    }

    vertexOffset += finalGeometry.NumVertex;
  }

  const uint32_t triangleCount = static_cast<uint32_t>(indices.size() / 3u);
  if (worldVertices.empty() || triangleCount == 0 || !worldBounds.IsValid()) {
    return false;
  }
  indices.resize(static_cast<std::size_t>(triangleCount) * 3u);

  const XVECTOR3 center = worldBounds.Center();
  AABB localBounds;
  for (XVECTOR3& vertex : worldVertices) {
    vertex.x -= center.x;
    vertex.y -= center.y;
    vertex.z -= center.z;
    vertex.w = 1.0f;
    localBounds.ExpandToInclude(vertex);
  }

  XMATRIX44 bodyTransform;
  bodyTransform.Identity();
  bodyTransform.m41 = center.x;
  bodyTransform.m42 = center.y;
  bodyTransform.m43 = center.z;

  outDesc = PhysicsTriangleMeshBodyDesc{};
  outDesc.entityId = entityId;
  outDesc.debugName = mesh.m_sourcePath.empty() ? "static-triangle-mesh" : mesh.m_sourcePath;
  outDesc.mesh.sourcePath = mesh.m_sourcePath;
  outDesc.mesh.vertices = std::move(worldVertices);
  outDesc.mesh.indices = std::move(indices);
  outDesc.mesh.localBounds = localBounds;
  outDesc.mesh.settings = settings;
  outDesc.worldTransform = bodyTransform;

  if (outStats) {
    const auto end = std::chrono::steady_clock::now();
    outStats->extractionMs = std::chrono::duration<double, std::milli>(end - start).count();
    outStats->vertexCount = static_cast<uint32_t>(outDesc.mesh.vertices.size());
    outStats->triangleCount = triangleCount;
  }
  return true;
}

bool AttachStaticTriangleMeshBody(JoltPhysicsSystem& physics,
                                  PrimitiveInst& instance,
                                  const RenderMesh& mesh,
                                  const PhysicsTriangleMeshCookSettings& settings,
                                  PhysicsCookStats* outStats) {
  PhysicsTriangleMeshBodyDesc desc;
  PhysicsCookStats localStats;
  PhysicsCookStats* stats = outStats ? outStats : &localStats;
  if (!BuildStaticTriangleMeshBodyDesc(mesh, instance.Final, instance.GetEntityId(), settings, desc, stats)) {
    return false;
  }

  const double extractionMs = stats->extractionMs;
  PhysicsBodyHandle handle = physics.CreateTriangleMeshBody(desc, stats);
  stats->extractionMs = extractionMs;
  stats->totalMs += extractionMs;
  if (!handle.IsValid()) {
    return false;
  }

  instance.AttachPhysicsBody(handle);
  return true;
}

bool BuildRagdollDescFromSkeleton(const RenderSkinnedMesh& mesh,
                                  const XMATRIX44& worldFromMesh,
                                  uint32_t entityId,
                                  const PhysicsRagdollBuildSettings& settings,
                                  PhysicsRagdollDesc& outDesc) {
  const xF::xSkeleton* skeleton = nullptr;
  if (mesh.xFile && !mesh.xFile->XMeshDataBase.empty() && mesh.xFile->XMeshDataBase[0]) {
    const xF::xMeshContainer* meshContainer = mesh.xFile->XMeshDataBase[0];
    if (!meshContainer->Skeleton.Bones.empty()) {
      skeleton = &meshContainer->Skeleton;
    } else if (!meshContainer->SkeletonAnimated.Bones.empty()) {
      skeleton = &meshContainer->SkeletonAnimated;
    }
  }
  if (!skeleton || skeleton->Bones.empty()) {
    skeleton = mesh.GetAnimController().GetAnimSkeleton();
  }
  if (!skeleton || skeleton->Bones.empty()) {
    return false;
  }

  outDesc = PhysicsRagdollDesc{};
  outDesc.entityId = entityId;
  outDesc.animationMode = PhysicsAnimationMode::AnimationDriven;
  outDesc.animationToPhysicsBlend = 0.0f;

  const std::vector<bool> selectedBones = SelectRagdollBones(*skeleton, settings);
  const std::vector<std::vector<uint32_t>> children = BuildSkeletonChildrenFromParents(*skeleton);
  uint32_t selectedCount = 0;
  for (bool selected : selectedBones) {
    if (selected) {
      ++selectedCount;
    }
  }
  std::vector<std::vector<WeightedPoint>> skinnedSamples;
  const bool hasSkinnedSamples = CollectSkinnedVertexSamples(mesh, worldFromMesh, skeleton->Bones.size(), settings, skinnedSamples);
  uint32_t sampledBoneCount = 0;
  if (hasSkinnedSamples) {
    for (const std::vector<WeightedPoint>& samples : skinnedSamples) {
      if (!samples.empty()) {
        ++sampledBoneCount;
      }
    }
  }
  uint32_t noEndpointCount = 0;
  uint32_t tooShortCount = 0;

  for (uint32_t boneIndex = 0; boneIndex < skeleton->Bones.size(); ++boneIndex) {
    if (boneIndex >= selectedBones.size() || !selectedBones[boneIndex]) {
      continue;
    }

    const xF::xBone& bone = skeleton->Bones[boneIndex];
    const XVECTOR3 boneWorld = BonePosition(bone, worldFromMesh);

    XVECTOR3 endWorld = boneWorld;
    const int parentBoneIndex = FindNearestSelectedParent(*skeleton, boneIndex, selectedBones);
    const int primaryChildIndex = FindPrimaryChild(*skeleton, boneIndex, children, selectedBones, worldFromMesh, settings);
    if (primaryChildIndex >= 0 && static_cast<std::size_t>(primaryChildIndex) < skeleton->Bones.size()) {
      endWorld = BonePosition(skeleton->Bones[primaryChildIndex], worldFromMesh);
    } else if (settings.includeLeafBones && parentBoneIndex >= 0) {
      const XVECTOR3 parentWorld = BonePosition(skeleton->Bones[parentBoneIndex], worldFromMesh);
      endWorld = XVECTOR3(
          boneWorld.x + (boneWorld.x - parentWorld.x),
          boneWorld.y + (boneWorld.y - parentWorld.y),
          boneWorld.z + (boneWorld.z - parentWorld.z),
          1.0f);
    } else {
      ++noEndpointCount;
      continue;
    }

    XVECTOR3 boneVector(
        endWorld.x - boneWorld.x,
        endWorld.y - boneWorld.y,
        endWorld.z - boneWorld.z,
        0.0f);
    float length = Length(boneVector);
    if (length < settings.minBoneLength) {
      ++tooShortCount;
      continue;
    }

    XVECTOR3 direction = Normalize(boneVector);
    float startT = 0.0f;
    float endT = length;
    float radius = Clamp(length * settings.radiusScale, settings.minRadius, settings.maxRadius);
    const bool childIsSelected =
        primaryChildIndex >= 0 &&
        static_cast<std::size_t>(primaryChildIndex) < selectedBones.size() &&
        selectedBones[static_cast<std::size_t>(primaryChildIndex)];
    if (hasSkinnedSamples && boneIndex < skinnedSamples.size()) {
      float fittedStartT = startT;
      float fittedEndT = endT;
      float fittedRadius = radius;
      if (FitCapsuleToSamples(
          skinnedSamples[boneIndex],
          boneWorld,
          direction,
          length,
          parentBoneIndex >= 0,
          childIsSelected,
          settings,
          fittedStartT,
          fittedEndT,
          fittedRadius)) {
        startT = fittedStartT;
        endT = fittedEndT;
        radius = fittedRadius;
      }
    }

    const float capsuleLength = (std::max)(settings.minBoneLength * 0.25f, endT - startT);
    const XVECTOR3 center = AddScaled(boneWorld, direction, (startT + endT) * 0.5f);

    PhysicsRagdollBoneDesc boneDesc;
    boneDesc.parentBoneIndex = parentBoneIndex;
    boneDesc.jointWorldPosition = boneWorld;
    boneDesc.body.entityId = entityId;
    boneDesc.body.boneIndex = static_cast<int>(boneIndex);
    boneDesc.body.debugName = bone.Name;
    boneDesc.body.motion = PhysicsBodyMotion::Kinematic;
    boneDesc.body.mass = (std::max)(0.1f, capsuleLength + radius * 2.0f);
    boneDesc.body.worldTransform = MakeBodyTransform(center, direction);
    if (settings.useCapsules) {
      boneDesc.body.shape = PhysicsShapeDesc::Capsule(radius, (std::max)(0.001f, capsuleLength * 0.5f - radius));
    } else {
      boneDesc.body.shape = PhysicsShapeDesc::Box(XVECTOR3(radius, capsuleLength * 0.5f, radius, 0.0f));
    }
    outDesc.bones.push_back(boneDesc);
  }

  if (outDesc.bones.empty()) {
    T8_LOG_ERROR("[PhysicsAuthoring] Ragdoll build produced no bodies: skeletonBones=%zu selected=%u sampled=%u noEndpoint=%u tooShort=%u minBoneLength=%.3f model='%s'",
                 skeleton->Bones.size(),
                 selectedCount,
                 sampledBoneCount,
                 noEndpointCount,
                 tooShortCount,
                 settings.minBoneLength,
                 mesh.m_sourcePath.c_str());
  } else {
    T8_LOG_INFO("[PhysicsAuthoring] Ragdoll build: bodies=%zu skeletonBones=%zu selected=%u sampled=%u model='%s'",
                outDesc.bones.size(),
                skeleton->Bones.size(),
                selectedCount,
                sampledBoneCount,
                mesh.m_sourcePath.c_str());
  }
  return !outDesc.bones.empty();
}

bool AttachSkeletonRagdoll(JoltPhysicsSystem& physics,
                           PrimitiveInst& instance,
                           const RenderSkinnedMesh& mesh,
                           const PhysicsRagdollBuildSettings& settings,
                           PhysicsBodyMotion initialMotion,
                           PhysicsRagdollDesc* outDesc) {
  PhysicsRagdollDesc desc;
  if (!BuildRagdollDescFromSkeleton(mesh, instance.Final, instance.GetEntityId(), settings, desc)) {
    return false;
  }

  PhysicsRagdollHandle handle = physics.CreateRagdoll(desc, initialMotion);
  if (!handle.IsValid()) {
    return false;
  }

  instance.AttachPhysicsRagdoll(handle);
  if (outDesc) {
    *outDesc = desc;
  }
  return true;
}

bool BuildRagdollAnimationBinding(const RenderSkinnedMesh& mesh,
                                  const XMATRIX44& worldFromMesh,
                                  const PhysicsRagdollDesc& referencePose,
                                  PhysicsRagdollAnimationBinding& outBinding) {
  const xF::xSkeleton* skeleton = FindReferenceSkeleton(mesh);
  if (!skeleton || skeleton->Bones.empty() || referencePose.bones.empty()) {
    return false;
  }

  PhysicsRagdollAnimationBinding binding;
  binding.referencePose = referencePose;
  binding.bodyFromBone.resize(referencePose.bones.size());

  for (std::size_t i = 0; i < referencePose.bones.size(); ++i) {
    const int boneIndex = referencePose.bones[i].body.boneIndex;
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= skeleton->Bones.size()) {
      return false;
    }

    const XMATRIX44 boneWorld = BoneWorldTransform(skeleton->Bones[boneIndex], worldFromMesh);
    XMATRIX44 inverseBoneWorld;
    if (!InvertAffine(boneWorld, inverseBoneWorld)) {
      return false;
    }
    binding.bodyFromBone[i] = referencePose.bones[i].body.worldTransform * inverseBoneWorld;
  }

  outBinding = std::move(binding);
  return true;
}

bool BuildRagdollPoseFromAnimation(const RenderSkinnedMesh& mesh,
                                   const XMATRIX44& worldFromMesh,
                                   const PhysicsRagdollAnimationBinding& binding,
                                   PhysicsRagdollDesc& outPose) {
  const xF::xSkeleton* skeleton = FindAnimatedSkeleton(mesh);
  if (!skeleton || skeleton->Bones.empty() || binding.referencePose.bones.empty() ||
      binding.referencePose.bones.size() != binding.bodyFromBone.size()) {
    return false;
  }

  PhysicsRagdollDesc pose = binding.referencePose;
  pose.animationMode = PhysicsAnimationMode::AnimationDriven;
  pose.animationToPhysicsBlend = 0.0f;

  for (std::size_t i = 0; i < pose.bones.size(); ++i) {
    const int boneIndex = pose.bones[i].body.boneIndex;
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= skeleton->Bones.size()) {
      return false;
    }

    const xF::xBone& bone = skeleton->Bones[boneIndex];
    const XMATRIX44 boneWorld = BoneWorldTransform(bone, worldFromMesh);
    pose.bones[i].body.worldTransform = binding.bodyFromBone[i] * boneWorld;
    pose.bones[i].jointWorldPosition = BonePosition(bone, worldFromMesh);
  }

  outPose = std::move(pose);
  return true;
}

bool BuildSkeletonPoseFromRagdollState(const RenderSkinnedMesh& mesh,
                                       const XMATRIX44& worldFromMesh,
                                       const PhysicsRagdollAnimationBinding& binding,
                                       const std::vector<PhysicsBodyState>& states,
                                       std::vector<int>& outBoneIndices,
                                       std::vector<XMATRIX44>& outCombinedMatrices) {
  const xF::xSkeleton* skeleton = FindAnimatedSkeleton(mesh);
  if (!skeleton || skeleton->Bones.empty() || states.empty() ||
      binding.referencePose.bones.empty() ||
      binding.referencePose.bones.size() != binding.bodyFromBone.size()) {
    return false;
  }

  XMATRIX44 meshFromWorld;
  if (!InvertAffine(worldFromMesh, meshFromWorld)) {
    return false;
  }

  outBoneIndices.clear();
  outCombinedMatrices.clear();
  outBoneIndices.reserve(binding.referencePose.bones.size());
  outCombinedMatrices.reserve(binding.referencePose.bones.size());

  for (std::size_t i = 0; i < binding.referencePose.bones.size(); ++i) {
    const int boneIndex = binding.referencePose.bones[i].body.boneIndex;
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= skeleton->Bones.size()) {
      continue;
    }

    const PhysicsBodyState* state = nullptr;
    for (const PhysicsBodyState& candidate : states) {
      if (candidate.boneIndex == boneIndex) {
        state = &candidate;
        break;
      }
    }
    if (!state) {
      continue;
    }

    XMATRIX44 boneFromBody;
    if (!InvertAffine(binding.bodyFromBone[i], boneFromBody)) {
      continue;
    }

    const XMATRIX44 boneWorld = boneFromBody * state->worldTransform;
    const XMATRIX44 boneMesh = boneWorld * meshFromWorld;
    outBoneIndices.push_back(boneIndex);
    outCombinedMatrices.push_back(FlipMatrixZ(boneMesh));
  }

  return !outBoneIndices.empty();
}

} // namespace t850
