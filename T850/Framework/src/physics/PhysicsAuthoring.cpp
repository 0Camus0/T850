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

constexpr float kMaxPhysicsAuthoringCoordinate = 1.0e12f;

bool IsUsablePhysicsCoordinate(float value) {
  return std::isfinite(value) && std::fabs(value) <= kMaxPhysicsAuthoringCoordinate;
}

bool IsUsablePhysicsPoint(const XVECTOR3& point) {
  return IsUsablePhysicsCoordinate(point.x) &&
         IsUsablePhysicsCoordinate(point.y) &&
         IsUsablePhysicsCoordinate(point.z);
}

bool IsValidRenderBounds(const RenderMesh::AABB& bounds) {
  return IsUsablePhysicsPoint(bounds.min) &&
         IsUsablePhysicsPoint(bounds.max) &&
         bounds.min.x <= bounds.max.x &&
         bounds.min.y <= bounds.max.y &&
         bounds.min.z <= bounds.max.z;
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
  XVECTOR3 right = Normalize(Cross(up, reference));
  XVECTOR3 forward = Normalize(Cross(right, up));

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

float BasisLength(const XMATRIX44& matrix, int row) {
  return Length(XVECTOR3(matrix.m[row][0], matrix.m[row][1], matrix.m[row][2], 0.0f));
}

void PreserveBasisLengths(const XMATRIX44& source, XMATRIX44& target) {
  for (int row = 0; row < 3; ++row) {
    const float sourceLength = BasisLength(source, row);
    const float targetLength = BasisLength(target, row);
    if (sourceLength <= 0.000001f || targetLength <= 0.000001f) {
      continue;
    }
    const float scale = sourceLength / targetLength;
    target.m[row][0] *= scale;
    target.m[row][1] *= scale;
    target.m[row][2] *= scale;
  }
}

void NormalizeBasisRows(XMATRIX44& matrix) {
  for (int row = 0; row < 3; ++row) {
    const float length = BasisLength(matrix, row);
    if (length <= 0.000001f) {
      continue;
    }
    matrix.m[row][0] /= length;
    matrix.m[row][1] /= length;
    matrix.m[row][2] /= length;
  }
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
  const xF::xSkeleton* animatedSkeleton = mesh.GetAnimController().GetAnimSkeleton();
  if (animatedSkeleton && !animatedSkeleton->Bones.empty()) {
    return animatedSkeleton;
  }
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

bool IsDeformationHelperBoneName(const std::string& lowerName) {
  return NameContains(lowerName, "roll") || NameContains(lowerName, "twist") || NameContains(lowerName, "_pin");
}

bool IsAttachmentBoneName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {
      "armor", "weapon", "launcher", "blade", "serration", "guard",
      "thumb", "index", "middle", "ring", "pinky", "knuckle",
      "jaw", "tongue", "teeth", "lip", "brow", "nose", "nostril", "snarl",
      "cheek", "eye", "ear", "crease", "puff", "eyelid", "helmet"});
}

bool IsExcludedHumanoidBoneName(const std::string& lowerName) {
  return IsDeformationHelperBoneName(lowerName) || IsAttachmentBoneName(lowerName);
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

bool IsSpineLikeName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {"hips", "pelvis", "spine", "chest", "neck", "head"});
}

bool IsUpperLegName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {"leg_upper", "upperleg", "upper_leg", "thigh"});
}

bool IsLowerLegName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {"leg_lower", "lowerleg", "lower_leg", "calf", "shin"});
}

bool IsFootName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {"leg_foot", "foot"});
}

bool IsUpperArmName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {"arm_upper", "upperarm", "upper_arm"});
}

bool IsLowerArmName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {"arm_lower", "lowerarm", "forearm", "lower_arm"});
}

bool IsHandName(const std::string& lowerName) {
  return NameContains(lowerName, "hand") && !NameContains(lowerName, "weapon");
}

bool IsFingerName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {"thumb", "index", "middle", "ring", "pinky", "finger"});
}

bool IsToeName(const std::string& lowerName) {
  return NameContainsAny(lowerName, {"ball", "toe"});
}

struct RagdollJointLimits {
  float swingRadians = Deg2Rad(35.0f);
  float twistRadians = Deg2Rad(15.0f);
};

RagdollJointLimits JointLimits(float swingDegrees, float twistDegrees) {
  RagdollJointLimits limits;
  limits.swingRadians = Deg2Rad(swingDegrees);
  limits.twistRadians = Deg2Rad(twistDegrees);
  return limits;
}

RagdollJointLimits InferRagdollJointLimits(const xF::xSkeleton& skeleton,
                                           uint32_t boneIndex,
                                           int parentBoneIndex,
                                           int endpointBoneIndex) {
  if (boneIndex >= skeleton.Bones.size()) {
    return {};
  }

  const std::string boneName = LowerName(skeleton.Bones[boneIndex].Name);
  const std::string parentName =
      parentBoneIndex >= 0 && static_cast<std::size_t>(parentBoneIndex) < skeleton.Bones.size()
          ? LowerName(skeleton.Bones[static_cast<std::size_t>(parentBoneIndex)].Name)
          : std::string();
  const std::string endpointName =
      endpointBoneIndex >= 0 && static_cast<std::size_t>(endpointBoneIndex) < skeleton.Bones.size()
          ? LowerName(skeleton.Bones[static_cast<std::size_t>(endpointBoneIndex)].Name)
          : std::string();

  const std::string& jointName = boneName;
  const std::string& leafName = endpointName.empty() ? boneName : endpointName;

  if (NameContains(jointName, "end") || NameContains(leafName, "end")) {
    return JointLimits(8.0f, 4.0f);
  }
  if (IsDeformationHelperBoneName(jointName) || IsDeformationHelperBoneName(leafName)) {
    return JointLimits(8.0f, 5.0f);
  }
  if (IsFingerName(jointName) || IsFingerName(leafName)) {
    return JointLimits(24.0f, 8.0f);
  }
  if (IsToeName(jointName) || IsToeName(leafName)) {
    return JointLimits(18.0f, 6.0f);
  }
  if (IsAttachmentBoneName(jointName) || IsAttachmentBoneName(leafName)) {
    return JointLimits(6.0f, 3.0f);
  }

  if (NameContainsAny(jointName, {"hips", "pelvis"})) {
    return JointLimits(24.0f, 12.0f);
  }
  if (NameContainsAny(jointName, {"spine", "chest"})) {
    if (NameContainsAny(parentName, {"hips", "pelvis"})) {
      return JointLimits(22.0f, 14.0f);
    }
    return JointLimits(16.0f, 10.0f);
  }
  if (NameContains(jointName, "neck")) {
    return JointLimits(22.0f, 14.0f);
  }
  if (NameContains(jointName, "head")) {
    return JointLimits(28.0f, 18.0f);
  }
  if (NameContains(jointName, "clavicle")) {
    return JointLimits(35.0f, 18.0f);
  }
  if (IsUpperArmName(jointName)) {
    return JointLimits(65.0f, 32.0f);
  }
  if (IsLowerArmName(jointName)) {
    return JointLimits(75.0f, 8.0f);
  }
  if (IsHandName(jointName)) {
    return JointLimits(35.0f, 18.0f);
  }
  if (IsUpperLegName(jointName)) {
    return JointLimits(55.0f, 24.0f);
  }
  if (IsLowerLegName(jointName)) {
    return JointLimits(80.0f, 8.0f);
  }
  if (IsFootName(jointName)) {
    return JointLimits(35.0f, 14.0f);
  }

  return JointLimits(30.0f, 12.0f);
}

int AnatomicalChildPriority(const std::string& parentLowerName, const std::string& childLowerName) {
  int score = 0;
  if (IsAttachmentBoneName(childLowerName)) {
    score -= 1000;
  }
  if (IsDeformationHelperBoneName(childLowerName)) {
    score -= 300;
  }

  if (NameContainsAny(parentLowerName, {"hips", "pelvis"})) {
    if (NameContainsAny(childLowerName, {"spine", "chest", "neck", "head"})) score += 600;
    if (IsUpperLegName(childLowerName)) score += 200;
  } else if (NameContainsAny(parentLowerName, {"spine", "chest"})) {
    if (NameContainsAny(childLowerName, {"spine", "chest", "neck", "head"})) score += 600;
    if (NameContains(childLowerName, "clavicle")) score += 150;
  } else if (NameContains(parentLowerName, "neck")) {
    if (NameContains(childLowerName, "head")) score += 600;
    if (NameContains(childLowerName, "neck")) score += 300;
  } else if (NameContains(parentLowerName, "clavicle")) {
    if (IsUpperArmName(childLowerName)) score += 600;
  } else if (IsUpperArmName(parentLowerName)) {
    if (IsLowerArmName(childLowerName)) score += 600;
  } else if (IsLowerArmName(parentLowerName)) {
    if (IsHandName(childLowerName)) score += 600;
  } else if (IsUpperLegName(parentLowerName)) {
    if (IsLowerLegName(childLowerName)) score += 600;
    if (IsFootName(childLowerName)) score += 250;
  } else if (IsLowerLegName(parentLowerName)) {
    if (IsFootName(childLowerName)) score += 600;
  } else if (IsFootName(parentLowerName)) {
    if (NameContainsAny(childLowerName, {"ball", "toe"})) score += 600;
  }

  if (IsHumanoidRagdollBoneName(childLowerName)) {
    score += 100;
  }
  return score;
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

void AppendUniqueChild(std::vector<uint32_t>& out, uint32_t childIndex) {
  if (std::find(out.begin(), out.end(), childIndex) == out.end()) {
    out.push_back(childIndex);
  }
}

std::vector<uint32_t> GetCombinedChildren(const xF::xSkeleton& skeleton,
                                          uint32_t boneIndex,
                                          const std::vector<std::vector<uint32_t>>& children) {
  std::vector<uint32_t> result;
  if (boneIndex >= skeleton.Bones.size()) {
    return result;
  }
  for (uint32_t childIndex : skeleton.Bones[boneIndex].Sons) {
    if (childIndex < skeleton.Bones.size()) {
      AppendUniqueChild(result, childIndex);
    }
  }
  if (boneIndex < children.size()) {
    for (uint32_t childIndex : children[boneIndex]) {
      if (childIndex < skeleton.Bones.size()) {
        AppendUniqueChild(result, childIndex);
      }
    }
  }
  return result;
}

struct ChildEndpointCandidate {
  uint32_t boneIndex = 0;
  int score = 0;
  float length = 0.0f;
};

void GatherHumanoidEndpointCandidates(const xF::xSkeleton& skeleton,
                                      uint32_t ownerBoneIndex,
                                      uint32_t searchBoneIndex,
                                      const std::vector<std::vector<uint32_t>>& children,
                                      const std::vector<bool>& selected,
                                      const XMATRIX44& worldFromMesh,
                                      const PhysicsRagdollBuildSettings& settings,
                                      uint32_t depth,
                                      std::vector<ChildEndpointCandidate>& outCandidates) {
  if (ownerBoneIndex >= skeleton.Bones.size() || searchBoneIndex >= skeleton.Bones.size() || depth > 4) {
    return;
  }

  const std::string ownerName = LowerName(skeleton.Bones[ownerBoneIndex].Name);
  const XVECTOR3 ownerWorld = BonePosition(skeleton.Bones[ownerBoneIndex], worldFromMesh);
  const std::vector<uint32_t> combinedChildren = GetCombinedChildren(skeleton, searchBoneIndex, children);
  for (uint32_t childIndex : combinedChildren) {
    const std::string childName = LowerName(skeleton.Bones[childIndex].Name);
    if (IsAttachmentBoneName(childName)) {
      continue;
    }

    const XVECTOR3 childWorld = BonePosition(skeleton.Bones[childIndex], worldFromMesh);
    const float length = Length(Subtract(childWorld, ownerWorld));
    const bool childSelected = childIndex < selected.size() && selected[childIndex];
    const bool endpointHelper = IsEndpointHelperForBone(ownerName, childName);
    if ((childSelected || endpointHelper) && length >= settings.minBoneLength) {
      outCandidates.push_back({
          childIndex,
          AnatomicalChildPriority(ownerName, childName) - static_cast<int>(depth) * 10,
          length});
    }

    const bool canSearchThroughChild =
        length < settings.minBoneLength ||
        IsDeformationHelperBoneName(childName) ||
        (!childSelected && !endpointHelper);
    if (canSearchThroughChild) {
      GatherHumanoidEndpointCandidates(
          skeleton,
          ownerBoneIndex,
          childIndex,
          children,
          selected,
          worldFromMesh,
          settings,
          depth + 1,
          outCandidates);
    }
  }
}

void GatherEndpointCandidates(const xF::xSkeleton& skeleton,
                              uint32_t ownerBoneIndex,
                              uint32_t searchBoneIndex,
                              const std::vector<std::vector<uint32_t>>& children,
                              const std::vector<bool>& selected,
                              const XMATRIX44& worldFromMesh,
                              const PhysicsRagdollBuildSettings& settings,
                              uint32_t depth,
                              std::vector<unsigned char>& visited,
                              std::vector<ChildEndpointCandidate>& outCandidates) {
  if (ownerBoneIndex >= skeleton.Bones.size() ||
      searchBoneIndex >= skeleton.Bones.size() ||
      depth > skeleton.Bones.size()) {
    return;
  }

  const XVECTOR3 ownerWorld = BonePosition(skeleton.Bones[ownerBoneIndex], worldFromMesh);
  const float endpointThreshold = settings.forceCapsuleForEveryBone ? 0.00001f : settings.minBoneLength;
  const std::vector<uint32_t> combinedChildren = GetCombinedChildren(skeleton, searchBoneIndex, children);
  for (uint32_t childIndex : combinedChildren) {
    if (childIndex >= skeleton.Bones.size() ||
        childIndex >= visited.size() ||
        visited[childIndex]) {
      continue;
    }
    visited[childIndex] = 1;

    const XVECTOR3 childWorld = BonePosition(skeleton.Bones[childIndex], worldFromMesh);
    const float length = Length(Subtract(childWorld, ownerWorld));
    if (childIndex < selected.size() && selected[childIndex] && length > endpointThreshold) {
      outCandidates.push_back({
          childIndex,
          -static_cast<int>(depth),
          length});
    }

    GatherEndpointCandidates(
        skeleton,
        ownerBoneIndex,
        childIndex,
        children,
        selected,
        worldFromMesh,
        settings,
        depth + 1,
        visited,
        outCandidates);
  }
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

  if (settings.preferHumanoidBones) {
    std::vector<ChildEndpointCandidate> candidates;
    GatherHumanoidEndpointCandidates(
        skeleton,
        boneIndex,
        boneIndex,
        children,
        selected,
        worldFromMesh,
        settings,
        0,
        candidates);
    if (!candidates.empty()) {
      std::sort(candidates.begin(), candidates.end(), [](const ChildEndpointCandidate& a, const ChildEndpointCandidate& b) {
        if (a.score != b.score) {
          return a.score > b.score;
        }
        return a.length > b.length;
      });
      return static_cast<int>(candidates.front().boneIndex);
    }
    return -1;
  }

  std::vector<ChildEndpointCandidate> candidates;
  std::vector<unsigned char> visited(skeleton.Bones.size(), 0);
  if (boneIndex < visited.size()) {
    visited[boneIndex] = 1;
  }
  GatherEndpointCandidates(
      skeleton,
      boneIndex,
      boneIndex,
      children,
      selected,
      worldFromMesh,
      settings,
      0,
      visited,
      candidates);
  if (!candidates.empty()) {
    std::sort(candidates.begin(), candidates.end(), [](const ChildEndpointCandidate& a, const ChildEndpointCandidate& b) {
      if (a.score != b.score) {
        return a.score > b.score;
      }
      return a.length > b.length;
    });
    return static_cast<int>(candidates.front().boneIndex);
  }
  return -1;
}

XVECTOR3 FindFallbackBoneDirection(const xF::xSkeleton& skeleton,
                                   uint32_t boneIndex,
                                   const std::vector<std::vector<uint32_t>>& children,
                                   const XMATRIX44& worldFromMesh) {
  if (boneIndex >= skeleton.Bones.size()) {
    return XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
  }

  const XVECTOR3 boneWorld = BonePosition(skeleton.Bones[boneIndex], worldFromMesh);
  const std::vector<uint32_t> combinedChildren = GetCombinedChildren(skeleton, boneIndex, children);
  float bestChildLength = 0.0f;
  XVECTOR3 bestChildDirection(0.0f, 0.0f, 0.0f, 0.0f);
  for (uint32_t childIndex : combinedChildren) {
    if (childIndex >= skeleton.Bones.size()) {
      continue;
    }
    const XVECTOR3 childVector = Subtract(BonePosition(skeleton.Bones[childIndex], worldFromMesh), boneWorld);
    const float childLength = Length(childVector);
    if (childLength > bestChildLength) {
      bestChildLength = childLength;
      bestChildDirection = childVector;
    }
  }
  if (bestChildLength > 0.0001f) {
    return Normalize(bestChildDirection);
  }

  const xF::xBone& bone = skeleton.Bones[boneIndex];
  if (bone.Dad < skeleton.Bones.size() && bone.Dad != boneIndex) {
    const XVECTOR3 parentVector = Subtract(boneWorld, BonePosition(skeleton.Bones[bone.Dad], worldFromMesh));
    if (Length(parentVector) > 0.0001f) {
      return Normalize(parentVector);
    }
  }

  const XMATRIX44 boneTransform = BoneWorldTransform(bone, worldFromMesh);
  const XVECTOR3 localY(boneTransform.m21, boneTransform.m22, boneTransform.m23, 0.0f);
  if (Length(localY) > 0.0001f) {
    return Normalize(localY);
  }
  return XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
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

void AppendHelperSamplesForBone(const xF::xSkeleton& skeleton,
                                uint32_t currentBoneIndex,
                                const std::vector<std::vector<uint32_t>>& children,
                                const std::vector<bool>& selected,
                                const std::vector<std::vector<WeightedPoint>>& directSamples,
                                std::vector<WeightedPoint>& outSamples,
                                uint32_t depth) {
  if (currentBoneIndex >= skeleton.Bones.size() || depth > 6) {
    return;
  }

  const std::vector<uint32_t> combinedChildren = GetCombinedChildren(skeleton, currentBoneIndex, children);
  for (uint32_t childIndex : combinedChildren) {
    if (childIndex >= skeleton.Bones.size()) {
      continue;
    }
    const std::string childName = LowerName(skeleton.Bones[childIndex].Name);
    if (childIndex < selected.size() && selected[childIndex]) {
      continue;
    }
    if (IsAttachmentBoneName(childName) || !IsDeformationHelperBoneName(childName)) {
      continue;
    }
    if (childIndex < directSamples.size()) {
      outSamples.insert(outSamples.end(), directSamples[childIndex].begin(), directSamples[childIndex].end());
    }
    AppendHelperSamplesForBone(
        skeleton,
        childIndex,
        children,
        selected,
        directSamples,
        outSamples,
        depth + 1);
  }
}

std::vector<std::vector<WeightedPoint>> BuildMergedFitSamples(const xF::xSkeleton& skeleton,
                                                              const std::vector<std::vector<uint32_t>>& children,
                                                              const std::vector<bool>& selected,
                                                              const std::vector<std::vector<WeightedPoint>>& directSamples) {
  std::vector<std::vector<WeightedPoint>> merged = directSamples;
  for (uint32_t boneIndex = 0; boneIndex < skeleton.Bones.size() && boneIndex < selected.size(); ++boneIndex) {
    if (!selected[boneIndex]) {
      continue;
    }
    AppendHelperSamplesForBone(skeleton, boneIndex, children, selected, directSamples, merged[boneIndex], 0);
  }
  return merged;
}

bool IsRedundantCoLocatedHumanoidBone(const xF::xSkeleton& skeleton,
                                      uint32_t boneIndex,
                                      const std::vector<bool>& selected,
                                      const XMATRIX44& worldFromMesh,
                                      const PhysicsRagdollBuildSettings& settings) {
  if (!settings.preferHumanoidBones || boneIndex >= skeleton.Bones.size()) {
    return false;
  }

  const xF::xBone& bone = skeleton.Bones[boneIndex];
  if (bone.Dad >= skeleton.Bones.size() || bone.Dad == boneIndex ||
      bone.Dad >= selected.size() || !selected[bone.Dad]) {
    return false;
  }

  const std::string boneName = LowerName(bone.Name);
  const std::string parentName = LowerName(skeleton.Bones[bone.Dad].Name);
  if (!IsSpineLikeName(boneName) || !IsSpineLikeName(parentName)) {
    return false;
  }

  const float parentDistance = Length(Subtract(
      BonePosition(bone, worldFromMesh),
      BonePosition(skeleton.Bones[bone.Dad], worldFromMesh)));
  return parentDistance < settings.minBoneLength * 0.25f;
}

int FindNearestGeneratedParent(const xF::xSkeleton& skeleton, int boneIndex, const std::vector<bool>& generated) {
  if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= skeleton.Bones.size()) {
    return -1;
  }

  uint32_t current = static_cast<uint32_t>(boneIndex);
  for (std::size_t depth = 0; depth < skeleton.Bones.size(); ++depth) {
    const xF::xBone& bone = skeleton.Bones[current];
    if (bone.Dad >= skeleton.Bones.size() || bone.Dad == current) {
      return -1;
    }
    current = bone.Dad;
    if (current < generated.size() && generated[current]) {
      return static_cast<int>(current);
    }
  }
  return -1;
}

void RebuildGeneratedParentLinks(const xF::xSkeleton& skeleton, PhysicsRagdollDesc& desc) {
  std::vector<bool> generated(skeleton.Bones.size(), false);
  for (const PhysicsRagdollBoneDesc& bone : desc.bones) {
    if (bone.body.boneIndex >= 0 && static_cast<std::size_t>(bone.body.boneIndex) < generated.size()) {
      generated[static_cast<std::size_t>(bone.body.boneIndex)] = true;
    }
  }

  for (PhysicsRagdollBoneDesc& bone : desc.bones) {
    bone.parentBoneIndex = FindNearestGeneratedParent(skeleton, bone.body.boneIndex, generated);
  }
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
  const xF::xSkeleton* skeleton = FindReferenceSkeleton(mesh);
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
  const std::vector<std::vector<WeightedPoint>> fitSamples =
      hasSkinnedSamples && settings.preferHumanoidBones
          ? BuildMergedFitSamples(*skeleton, children, selectedBones, skinnedSamples)
          : skinnedSamples;
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
  uint32_t redundantCount = 0;
  uint32_t syntheticCount = 0;
  uint32_t directEdgeCount = 0;
  float maxDirectEdgeLength = 0.0f;
  float maxRootDistance = 0.0f;
  const XVECTOR3 rootWorldPosition = BonePosition(skeleton->Bones[0], worldFromMesh);
  for (uint32_t boneIndex = 0; boneIndex < skeleton->Bones.size(); ++boneIndex) {
    const XVECTOR3 boneWorld = BonePosition(skeleton->Bones[boneIndex], worldFromMesh);
    maxRootDistance = (std::max)(maxRootDistance, Length(Subtract(boneWorld, rootWorldPosition)));
    const xF::xBone& bone = skeleton->Bones[boneIndex];
    if (bone.Dad < skeleton->Bones.size() && bone.Dad != boneIndex) {
      const float edgeLength = Length(Subtract(boneWorld, BonePosition(skeleton->Bones[bone.Dad], worldFromMesh)));
      maxDirectEdgeLength = (std::max)(maxDirectEdgeLength, edgeLength);
      if (edgeLength > 0.00001f) {
        ++directEdgeCount;
      }
    }
  }

  for (uint32_t boneIndex = 0; boneIndex < skeleton->Bones.size(); ++boneIndex) {
    if (boneIndex >= selectedBones.size() || !selectedBones[boneIndex]) {
      continue;
    }
    if (!settings.forceCapsuleForEveryBone &&
        IsRedundantCoLocatedHumanoidBone(*skeleton, boneIndex, selectedBones, worldFromMesh, settings)) {
      ++redundantCount;
      continue;
    }

    const xF::xBone& bone = skeleton->Bones[boneIndex];
    const XVECTOR3 boneWorld = BonePosition(bone, worldFromMesh);

    const int parentBoneIndex = FindNearestSelectedParent(*skeleton, boneIndex, selectedBones);
    XVECTOR3 capsuleStartWorld = boneWorld;
    XVECTOR3 endWorld = boneWorld;
    XVECTOR3 jointWorldPosition = boneWorld;
    int primaryChildIndex = -1;
    primaryChildIndex = FindPrimaryChild(*skeleton, boneIndex, children, selectedBones, worldFromMesh, settings);

    if (primaryChildIndex >= 0 &&
        static_cast<std::size_t>(primaryChildIndex) < skeleton->Bones.size()) {
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
      if (!settings.forceCapsuleForEveryBone) {
        continue;
      }
      const XVECTOR3 fallbackDirection = FindFallbackBoneDirection(*skeleton, boneIndex, children, worldFromMesh);
      const float syntheticLength = (std::max)(settings.minBoneLength, settings.syntheticBoneLength);
      endWorld = AddScaled(capsuleStartWorld, fallbackDirection, syntheticLength);
      ++syntheticCount;
    }

    XVECTOR3 boneVector(
        endWorld.x - capsuleStartWorld.x,
        endWorld.y - capsuleStartWorld.y,
        endWorld.z - capsuleStartWorld.z,
        0.0f);
    float length = Length(boneVector);
    if (length < settings.minBoneLength) {
      ++tooShortCount;
      if (!settings.forceCapsuleForEveryBone) {
        continue;
      }
      if (length <= 0.00001f) {
        const XVECTOR3 fallbackDirection = FindFallbackBoneDirection(*skeleton, boneIndex, children, worldFromMesh);
        length = (std::max)(settings.minBoneLength, settings.syntheticBoneLength);
        boneVector = fallbackDirection * length;
        endWorld = AddScaled(capsuleStartWorld, fallbackDirection, length);
        ++syntheticCount;
      }
    }

    XVECTOR3 direction = Normalize(boneVector);
    float startT = 0.0f;
    float endT = length;
    float radius = Clamp(length * settings.radiusScale, settings.minRadius, settings.maxRadius);
    const bool childIsSelected =
        primaryChildIndex >= 0 &&
        static_cast<std::size_t>(primaryChildIndex) < selectedBones.size() &&
        selectedBones[static_cast<std::size_t>(primaryChildIndex)];
    if (hasSkinnedSamples && boneIndex < fitSamples.size()) {
      float fittedStartT = startT;
      float fittedEndT = endT;
      float fittedRadius = radius;
      if (FitCapsuleToSamples(
          fitSamples[boneIndex],
          capsuleStartWorld,
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
    if (settings.preferHumanoidBones) {
      startT = 0.0f;
      endT = length;
    }

    const float capsuleLength = (std::max)(settings.minBoneLength * 0.25f, endT - startT);
    const XVECTOR3 center = AddScaled(capsuleStartWorld, direction, (startT + endT) * 0.5f);

    PhysicsRagdollBoneDesc boneDesc;
    boneDesc.parentBoneIndex = parentBoneIndex;
    boneDesc.jointWorldPosition = jointWorldPosition;
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
    const RagdollJointLimits jointLimits =
        InferRagdollJointLimits(*skeleton, boneIndex, parentBoneIndex, primaryChildIndex);
    boneDesc.swingLimitRadians = jointLimits.swingRadians;
    boneDesc.twistLimitRadians = jointLimits.twistRadians;
    outDesc.bones.push_back(boneDesc);
  }
  RebuildGeneratedParentLinks(*skeleton, outDesc);

  uint32_t connectedEdgeCount = 0;
  uint32_t disconnectedEdgeCount = 0;
  uint32_t leafBodyCount = 0;
  if (settings.forceCapsuleForEveryBone) {
    std::vector<int> generatedParent(skeleton->Bones.size(), -2);
    for (const PhysicsRagdollBoneDesc& boneDesc : outDesc.bones) {
      if (boneDesc.body.boneIndex >= 0 &&
          static_cast<std::size_t>(boneDesc.body.boneIndex) < generatedParent.size()) {
        generatedParent[static_cast<std::size_t>(boneDesc.body.boneIndex)] = boneDesc.parentBoneIndex;
      }
    }

    for (uint32_t boneIndex = 0; boneIndex < skeleton->Bones.size(); ++boneIndex) {
      if (boneIndex >= selectedBones.size() || !selectedBones[boneIndex]) {
        continue;
      }
      const bool hasBody = generatedParent[boneIndex] != -2;
      const std::vector<uint32_t> boneChildren = GetCombinedChildren(*skeleton, boneIndex, children);
      bool hasSelectedChild = false;
      for (uint32_t childIndex : boneChildren) {
        if (childIndex < selectedBones.size() && selectedBones[childIndex]) {
          hasSelectedChild = true;
          break;
        }
      }
      if (!hasSelectedChild && hasBody) {
        ++leafBodyCount;
      }

      const xF::xBone& bone = skeleton->Bones[boneIndex];
      if (bone.Dad >= skeleton->Bones.size() ||
          bone.Dad == boneIndex ||
          bone.Dad >= selectedBones.size() ||
          !selectedBones[bone.Dad]) {
        continue;
      }
      if (hasBody && generatedParent[boneIndex] == static_cast<int>(bone.Dad)) {
        ++connectedEdgeCount;
      } else {
        ++disconnectedEdgeCount;
      }
    }
  }

  if (outDesc.bones.empty()) {
    T8_LOG_ERROR("[PhysicsAuthoring] Ragdoll build produced no bodies: skeletonBones=%zu selected=%u sampled=%u noEndpoint=%u tooShort=%u redundant=%u synthetic=%u disconnected=%u minBoneLength=%.3f model='%s'",
                 skeleton->Bones.size(),
                 selectedCount,
                 sampledBoneCount,
                 noEndpointCount,
                 tooShortCount,
                 redundantCount,
                 syntheticCount,
                 disconnectedEdgeCount,
                 settings.minBoneLength,
                 mesh.m_sourcePath.c_str());
  } else {
    T8_LOG_INFO("[PhysicsAuthoring] Ragdoll build: bodies=%zu skeletonBones=%zu selected=%u sampled=%u redundant=%u synthetic=%u connectedEdges=%u disconnectedEdges=%u leafBodies=%u directEdges=%u maxDirect=%.6f maxRoot=%.6f model='%s'",
                outDesc.bones.size(),
                skeleton->Bones.size(),
                selectedCount,
                sampledBoneCount,
                redundantCount,
                syntheticCount,
                connectedEdgeCount,
                disconnectedEdgeCount,
                leafBodyCount,
                directEdgeCount,
                maxDirectEdgeLength,
                maxRootDistance,
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
  binding.jointFromBone.resize(referencePose.bones.size());
  binding.controlledBoneIndices.resize(referencePose.bones.size());
  binding.controlledBodyFromBone.resize(referencePose.bones.size());

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
    binding.jointFromBone[i] = TransformPhysicsPoint(referencePose.bones[i].jointWorldPosition, inverseBoneWorld);
    binding.controlledBoneIndices[i].push_back(boneIndex);
    binding.controlledBodyFromBone[i].push_back(binding.bodyFromBone[i]);
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
    NormalizeBasisRows(pose.bones[i].body.worldTransform);
    pose.bones[i].jointWorldPosition =
        i < binding.jointFromBone.size()
            ? TransformPhysicsPoint(binding.jointFromBone[i], boneWorld)
            : BonePosition(bone, worldFromMesh);
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
  std::vector<unsigned char> emitted(skeleton->Bones.size(), 0);

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

    const bool hasControlledList =
        i < binding.controlledBoneIndices.size() &&
        i < binding.controlledBodyFromBone.size() &&
        binding.controlledBoneIndices[i].size() == binding.controlledBodyFromBone[i].size();
    const std::size_t controlledCount = hasControlledList ? binding.controlledBoneIndices[i].size() : 1u;
    for (std::size_t controlledIndex = 0; controlledIndex < controlledCount; ++controlledIndex) {
      const int controlledBoneIndex =
          hasControlledList ? binding.controlledBoneIndices[i][controlledIndex] : boneIndex;
      if (controlledBoneIndex < 0 ||
          static_cast<std::size_t>(controlledBoneIndex) >= skeleton->Bones.size() ||
          emitted[static_cast<std::size_t>(controlledBoneIndex)]) {
        continue;
      }

      const XMATRIX44& controlledBodyFromBone =
          hasControlledList ? binding.controlledBodyFromBone[i][controlledIndex] : binding.bodyFromBone[i];
      XMATRIX44 boneFromBody;
      if (!InvertAffine(controlledBodyFromBone, boneFromBody)) {
        continue;
      }

      const XMATRIX44 currentBoneWorld = BoneWorldTransform(skeleton->Bones[controlledBoneIndex], worldFromMesh);
      const XMATRIX44 currentBodyWorld = controlledBodyFromBone * currentBoneWorld;
      XMATRIX44 scaledBodyWorld = state->worldTransform;
      PreserveBasisLengths(currentBodyWorld, scaledBodyWorld);

      const XMATRIX44 boneWorld = boneFromBody * scaledBodyWorld;
      const XMATRIX44 boneMesh = boneWorld * meshFromWorld;
      XMATRIX44 combined = FlipMatrixZ(boneMesh);
      PreserveBasisLengths(skeleton->Bones[controlledBoneIndex].Combined, combined);
      emitted[static_cast<std::size_t>(controlledBoneIndex)] = 1;
      outBoneIndices.push_back(controlledBoneIndex);
      outCombinedMatrices.push_back(combined);
    }
  }

  return !outBoneIndices.empty();
}

} // namespace t850
