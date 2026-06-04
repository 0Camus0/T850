#include <pch.h>

#include <physics/PhysicsAuthoring.h>

#include <scene/PrimitiveInstance.h>
#include <scene/RenderMesh.h>
#include <scene/RenderSkinnedMesh.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

XVECTOR3 TransformPhysicsVector(const XVECTOR3& vector, const XMATRIX44& matrix) {
  return XVECTOR3(
      vector.x * matrix.m11 + vector.y * matrix.m21 + vector.z * matrix.m31,
      vector.x * matrix.m12 + vector.y * matrix.m22 + vector.z * matrix.m32,
      vector.x * matrix.m13 + vector.y * matrix.m23 + vector.z * matrix.m33,
      0.0f);
}

XVECTOR3 NormalizeOr(const XVECTOR3& v, const XVECTOR3& fallback) {
  const float length = Length(v);
  if (length <= 0.000001f) {
    return fallback;
  }
  return XVECTOR3(v.x / length, v.y / length, v.z / length, 0.0f);
}

bool IsValidAxis(const XVECTOR3& axis) {
  return IsUsablePhysicsCoordinate(axis.x) &&
         IsUsablePhysicsCoordinate(axis.y) &&
         IsUsablePhysicsCoordinate(axis.z) &&
         Length(axis) > 0.000001f;
}

XVECTOR3 MatrixAxisX(const XMATRIX44& matrix) {
  return NormalizeOr(XVECTOR3(matrix.m11, matrix.m12, matrix.m13, 0.0f),
                     XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
}

XVECTOR3 MatrixAxisY(const XMATRIX44& matrix) {
  return NormalizeOr(XVECTOR3(matrix.m21, matrix.m22, matrix.m23, 0.0f),
                     XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
}

XVECTOR3 RejectFromAxis(const XVECTOR3& vector, const XVECTOR3& axis) {
  const float dot = Dot(vector, axis);
  return XVECTOR3(vector.x - axis.x * dot,
                  vector.y - axis.y * dot,
                  vector.z - axis.z * dot,
                  0.0f);
}

void NormalizeJointFrameAxes(XVECTOR3& twist,
                             XVECTOR3& plane,
                             const XVECTOR3& fallbackTwist,
                             const XVECTOR3& fallbackPlane) {
  twist = NormalizeOr(IsValidAxis(twist) ? twist : fallbackTwist, fallbackTwist);
  plane = RejectFromAxis(IsValidAxis(plane) ? plane : fallbackPlane, twist);
  if (!IsValidAxis(plane)) {
    plane = RejectFromAxis(std::fabs(twist.x) < 0.8f
                               ? XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f)
                               : XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f),
                           twist);
  }
  plane = NormalizeOr(plane, fallbackPlane);
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

float UniformScaleFromWorldTransform(const XMATRIX44& worldFromMesh) {
  const float sx = BasisLength(worldFromMesh, 0);
  const float sy = BasisLength(worldFromMesh, 1);
  const float sz = BasisLength(worldFromMesh, 2);
  const float scale = (std::max)(sx, (std::max)(sy, sz));
  return std::isfinite(scale) && scale > 0.000001f ? scale : 1.0f;
}

XVECTOR3 TransformSavedRagdollAxis(const std::array<float, 3>& savedAxis,
                                   const XMATRIX44& worldFromMesh,
                                   const XVECTOR3& fallback) {
  return NormalizeOr(
      TransformPhysicsVector(XVECTOR3(savedAxis[0], savedAxis[1], savedAxis[2], 0.0f), worldFromMesh),
      fallback);
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
  const float maxBasis =
      (std::max)(BasisLength(matrix, 0), (std::max)(BasisLength(matrix, 1), BasisLength(matrix, 2)));
  const float detEpsilon =
      (std::max)(1.0e-12f, maxBasis * maxBasis * maxBasis * 1.0e-6f);
  if (!std::isfinite(det) || std::fabs(det) <= detEpsilon) {
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
  const xF::xSkeleton* controllerBind = mesh.GetAnimController().GetBindSkeleton();
  if (controllerBind && !controllerBind->Bones.empty()) {
    return controllerBind;
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
    return JointLimits(80.0f, 0.0f);
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

int FindRagdollDescIndexForBone(const PhysicsRagdollDesc& desc, int boneIndex) {
  for (std::size_t i = 0; i < desc.bones.size(); ++i) {
    if (desc.bones[i].body.boneIndex == boneIndex) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void InitializeRagdollJointFrames(PhysicsRagdollDesc& desc) {
  for (PhysicsRagdollBoneDesc& bone : desc.bones) {
    const int parentIndex = FindRagdollDescIndexForBone(desc, bone.parentBoneIndex);
    const XMATRIX44& childWorld = bone.body.worldTransform;
    const XMATRIX44& parentWorld = parentIndex >= 0
        ? desc.bones[static_cast<std::size_t>(parentIndex)].body.worldTransform
        : childWorld;

    XVECTOR3 parentTwist = IsValidAxis(bone.parentJointTwistAxis)
        ? bone.parentJointTwistAxis
        : MatrixAxisY(parentWorld);
    XVECTOR3 parentPlane = IsValidAxis(bone.parentJointPlaneAxis)
        ? bone.parentJointPlaneAxis
        : MatrixAxisX(parentWorld);
    XVECTOR3 childTwist = IsValidAxis(bone.childJointTwistAxis)
        ? bone.childJointTwistAxis
        : MatrixAxisY(childWorld);
    XVECTOR3 childPlane = IsValidAxis(bone.childJointPlaneAxis)
        ? bone.childJointPlaneAxis
        : MatrixAxisX(childWorld);

    NormalizeJointFrameAxes(parentTwist, parentPlane, MatrixAxisY(parentWorld), MatrixAxisX(parentWorld));
    NormalizeJointFrameAxes(childTwist, childPlane, MatrixAxisY(childWorld), MatrixAxisX(childWorld));
    bone.parentJointTwistAxis = parentTwist;
    bone.parentJointPlaneAxis = parentPlane;
    bone.childJointTwistAxis = childTwist;
    bone.childJointPlaneAxis = childPlane;
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

struct RagdollEditBodyJson {
  int index = -1;
  int boneIndex = -1;
  std::string name;
  int parentBody = -1;
  int jointParentBody = kPhysicsRagdollJointInheritParent;
  bool bodyFrozen = false;
  bool jointFrozen = false;
  bool hasJointContactAnchor = false;
  bool jointContactAnchor = false;
  int jointType = 0;
  bool hasJointAnchor = false;
  std::array<float, 3> jointAnchor{};
  bool hasParentJointTwistAxis = false;
  bool hasParentJointPlaneAxis = false;
  bool hasChildJointTwistAxis = false;
  bool hasChildJointPlaneAxis = false;
  std::array<float, 3> parentJointTwistAxis{};
  std::array<float, 3> parentJointPlaneAxis{};
  std::array<float, 3> childJointTwistAxis{};
  std::array<float, 3> childJointPlaneAxis{};
  std::array<float, 16> bodyFromBone{};
  std::string shapeType = "capsule";
  float radius = 0.0f;
  float halfHeight = 0.0f;
  std::array<float, 3> halfExtents{};
  float swingLimitRadians = 0.0f;
  float twistLimitRadians = 0.0f;
  std::vector<int> controlledBones;
};

struct RagdollEditJson {
  int schema = 1;
  std::string model;
  std::vector<RagdollEditBodyJson> bodies;
};

std::string JsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (ch == '\\' || ch == '"') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

bool ParseJsonStringAt(const std::string& json, std::size_t keyPos, std::string& out) {
  const std::size_t colon = json.find(':', keyPos);
  if (colon == std::string::npos) return false;
  std::size_t quote = json.find('"', colon + 1);
  if (quote == std::string::npos) return false;
  ++quote;
  out.clear();
  bool escaped = false;
  for (std::size_t i = quote; i < json.size(); ++i) {
    const char ch = json[i];
    if (escaped) {
      out.push_back(ch);
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else if (ch == '"') {
      return true;
    } else {
      out.push_back(ch);
    }
  }
  return false;
}

bool ParseJsonIntAt(const std::string& json, std::size_t keyPos, int& out) {
  const std::size_t colon = json.find(':', keyPos);
  if (colon == std::string::npos) return false;
  char* end = nullptr;
  const long value = std::strtol(json.c_str() + colon + 1, &end, 10);
  if (end == json.c_str() + colon + 1) return false;
  out = static_cast<int>(value);
  return true;
}

bool ParseJsonFloatAt(const std::string& json, std::size_t keyPos, float& out) {
  const std::size_t colon = json.find(':', keyPos);
  if (colon == std::string::npos) return false;
  char* end = nullptr;
  const float value = std::strtof(json.c_str() + colon + 1, &end);
  if (end == json.c_str() + colon + 1) return false;
  out = value;
  return true;
}

bool ParseJsonBoolAt(const std::string& json, std::size_t keyPos, bool& out) {
  const std::size_t colon = json.find(':', keyPos);
  if (colon == std::string::npos) return false;
  const char* cursor = json.c_str() + colon + 1;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
  if (std::strncmp(cursor, "true", 4) == 0) {
    out = true;
    return true;
  }
  if (std::strncmp(cursor, "false", 5) == 0) {
    out = false;
    return true;
  }
  return false;
}

bool ParseFloatArray16At(const std::string& json, std::size_t keyPos, std::array<float, 16>& out) {
  const std::size_t start = json.find('[', keyPos);
  if (start == std::string::npos) return false;
  const char* cursor = json.c_str() + start + 1;
  char* end = nullptr;
  for (std::size_t i = 0; i < out.size(); ++i) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ',') ++cursor;
    out[i] = std::strtof(cursor, &end);
    if (end == cursor) return false;
    cursor = end;
  }
  return true;
}

bool ParseFloatArray3At(const std::string& json, std::size_t keyPos, std::array<float, 3>& out) {
  const std::size_t start = json.find('[', keyPos);
  if (start == std::string::npos) return false;
  const char* cursor = json.c_str() + start + 1;
  char* end = nullptr;
  for (std::size_t i = 0; i < out.size(); ++i) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ',') ++cursor;
    out[i] = std::strtof(cursor, &end);
    if (end == cursor) return false;
    cursor = end;
  }
  return true;
}

bool ParseIntArrayAt(const std::string& json, std::size_t keyPos, std::vector<int>& out) {
  out.clear();
  const std::size_t start = json.find('[', keyPos);
  if (start == std::string::npos) return false;
  const std::size_t endArray = json.find(']', start + 1);
  if (endArray == std::string::npos) return false;

  const char* cursor = json.c_str() + start + 1;
  const char* endCursor = json.c_str() + endArray;
  while (cursor < endCursor) {
    while (cursor < endCursor && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ',')) {
      ++cursor;
    }
    if (cursor >= endCursor) break;
    char* parsedEnd = nullptr;
    const long value = std::strtol(cursor, &parsedEnd, 10);
    if (parsedEnd == cursor) return false;
    out.push_back(static_cast<int>(value));
    cursor = parsedEnd;
  }
  return true;
}

bool ParseRagdollEditJson(const std::string& json, RagdollEditJson& out) {
  out = RagdollEditJson{};
  if (const std::size_t schemaKey = json.find("\"schema\""); schemaKey != std::string::npos) {
    ParseJsonIntAt(json, schemaKey, out.schema);
  }
  if (const std::size_t modelKey = json.find("\"model\""); modelKey != std::string::npos) {
    ParseJsonStringAt(json, modelKey, out.model);
  }

  std::size_t pos = json.find("\"capsules\"");
  if (pos == std::string::npos) {
    pos = json.find("\"bodies\"");
  }
  if (pos == std::string::npos) return true;
  while ((pos = json.find("\"index\"", pos)) != std::string::npos) {
    RagdollEditBodyJson body;
    if (!ParseJsonIntAt(json, pos, body.index)) break;

    const std::size_t boneIndexKey = json.find("\"bone_index\"", pos);
    const std::size_t nameKey = json.find("\"name\"", pos);
    const std::size_t parentKey = json.find("\"parent_capsule\"", pos);
    const std::size_t parentBodyKey = json.find("\"parent_body\"", pos);
    const std::size_t jointParentKey = json.find("\"joint_parent_capsule\"", pos);
    const std::size_t jointParentBodyKey = json.find("\"joint_parent_body\"", pos);
    const std::size_t bodyFrozenKey = json.find("\"capsule_frozen\"", pos);
    const std::size_t bodyFrozenKey2 = json.find("\"body_frozen\"", pos);
    const std::size_t jointFrozenKey = json.find("\"joint_frozen\"", pos);
    const std::size_t jointContactKey = json.find("\"joint_contact_anchor\"", pos);
    const std::size_t jointTypeKey = json.find("\"joint_type\"", pos);
    const std::size_t jointAnchorKey = json.find("\"joint_anchor\"", pos);
    const std::size_t parentJointTwistKey = json.find("\"parent_joint_twist_axis\"", pos);
    const std::size_t parentJointPlaneKey = json.find("\"parent_joint_plane_axis\"", pos);
    const std::size_t childJointTwistKey = json.find("\"child_joint_twist_axis\"", pos);
    const std::size_t childJointPlaneKey = json.find("\"child_joint_plane_axis\"", pos);
    const std::size_t matrixKey = json.find("\"body_from_bone\"", pos);
    const std::size_t shapeTypeKey = json.find("\"shape_type\"", pos);
    const std::size_t radiusKey = json.find("\"radius\"", pos);
    const std::size_t halfHeightKey = json.find("\"half_height\"", pos);
    const std::size_t halfExtentsKey = json.find("\"half_extents\"", pos);
    const std::size_t swingKey = json.find("\"swing_limit\"", pos);
    const std::size_t twistKey = json.find("\"twist_limit\"", pos);
    const std::size_t controlledKey = json.find("\"controlled_bones\"", pos);
    const std::size_t objectEnd = json.find('}', pos);
    if (objectEnd == std::string::npos) break;
    if (boneIndexKey == std::string::npos || nameKey == std::string::npos ||
        matrixKey == std::string::npos || radiusKey == std::string::npos ||
        halfHeightKey == std::string::npos || swingKey == std::string::npos ||
        twistKey == std::string::npos) {
      break;
    }

    ParseJsonIntAt(json, boneIndexKey, body.boneIndex);
    ParseJsonStringAt(json, nameKey, body.name);
    if (parentKey != std::string::npos && parentKey < objectEnd) {
      ParseJsonIntAt(json, parentKey, body.parentBody);
    } else if (parentBodyKey != std::string::npos && parentBodyKey < objectEnd) {
      ParseJsonIntAt(json, parentBodyKey, body.parentBody);
    }
    if (jointParentKey != std::string::npos && jointParentKey < objectEnd) {
      ParseJsonIntAt(json, jointParentKey, body.jointParentBody);
    } else if (jointParentBodyKey != std::string::npos && jointParentBodyKey < objectEnd) {
      ParseJsonIntAt(json, jointParentBodyKey, body.jointParentBody);
    }
    if (bodyFrozenKey != std::string::npos && bodyFrozenKey < objectEnd) {
      ParseJsonBoolAt(json, bodyFrozenKey, body.bodyFrozen);
    } else if (bodyFrozenKey2 != std::string::npos && bodyFrozenKey2 < objectEnd) {
      ParseJsonBoolAt(json, bodyFrozenKey2, body.bodyFrozen);
    }
    if (jointFrozenKey != std::string::npos && jointFrozenKey < objectEnd) {
      ParseJsonBoolAt(json, jointFrozenKey, body.jointFrozen);
    }
    if (jointContactKey != std::string::npos && jointContactKey < objectEnd) {
      body.hasJointContactAnchor = ParseJsonBoolAt(json, jointContactKey, body.jointContactAnchor);
    }
    if (jointTypeKey != std::string::npos && jointTypeKey < objectEnd) {
      ParseJsonIntAt(json, jointTypeKey, body.jointType);
    }
    if (jointAnchorKey != std::string::npos && jointAnchorKey < objectEnd) {
      body.hasJointAnchor = ParseFloatArray3At(json, jointAnchorKey, body.jointAnchor);
    }
    if (parentJointTwistKey != std::string::npos && parentJointTwistKey < objectEnd) {
      body.hasParentJointTwistAxis = ParseFloatArray3At(json, parentJointTwistKey, body.parentJointTwistAxis);
    }
    if (parentJointPlaneKey != std::string::npos && parentJointPlaneKey < objectEnd) {
      body.hasParentJointPlaneAxis = ParseFloatArray3At(json, parentJointPlaneKey, body.parentJointPlaneAxis);
    }
    if (childJointTwistKey != std::string::npos && childJointTwistKey < objectEnd) {
      body.hasChildJointTwistAxis = ParseFloatArray3At(json, childJointTwistKey, body.childJointTwistAxis);
    }
    if (childJointPlaneKey != std::string::npos && childJointPlaneKey < objectEnd) {
      body.hasChildJointPlaneAxis = ParseFloatArray3At(json, childJointPlaneKey, body.childJointPlaneAxis);
    }
    if (!ParseFloatArray16At(json, matrixKey, body.bodyFromBone)) break;
    if (shapeTypeKey != std::string::npos && shapeTypeKey < objectEnd) {
      ParseJsonStringAt(json, shapeTypeKey, body.shapeType);
    }
    if (!ParseJsonFloatAt(json, radiusKey, body.radius)) break;
    if (!ParseJsonFloatAt(json, halfHeightKey, body.halfHeight)) break;
    if (halfExtentsKey != std::string::npos && halfExtentsKey < objectEnd) {
      ParseFloatArray3At(json, halfExtentsKey, body.halfExtents);
    }
    if (!ParseJsonFloatAt(json, swingKey, body.swingLimitRadians)) break;
    if (!ParseJsonFloatAt(json, twistKey, body.twistLimitRadians)) break;
    if (controlledKey != std::string::npos && controlledKey < objectEnd) {
      ParseIntArrayAt(json, controlledKey, body.controlledBones);
    }
    out.bodies.push_back(std::move(body));
    pos = twistKey + 13;
  }
  return true;
}

std::array<float, 16> MatrixToArray16(const XMATRIX44& matrix) {
  std::array<float, 16> out{};
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      out[static_cast<std::size_t>(r * 4 + c)] = matrix.m[r][c];
  return out;
}

XMATRIX44 MatrixFromArray16(const std::array<float, 16>& values) {
  XMATRIX44 out;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      out.m[r][c] = values[static_cast<std::size_t>(r * 4 + c)];
  return out;
}

std::string FileSafeModelKey(std::string key) {
  if (key.empty()) key = "model";
  for (char& ch : key) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
    if (!ok) ch = '_';
  }
  return key;
}

constexpr float kRagdollMinShapeExtent = 0.001f;

const char* RagdollShapeTypeSaveName(PhysicsShapeType type) {
  return type == PhysicsShapeType::Box ? "box" : "capsule";
}

PhysicsShapeType RagdollShapeTypeFromSaveName(const std::string& name) {
  return LowerName(name) == "box" ? PhysicsShapeType::Box : PhysicsShapeType::Capsule;
}

XVECTOR3 ClampRagdollBoxHalfExtents(const XVECTOR3& halfExtents) {
  return XVECTOR3(
      (std::max)(kRagdollMinShapeExtent, halfExtents.x),
      (std::max)(kRagdollMinShapeExtent, halfExtents.y),
      (std::max)(kRagdollMinShapeExtent, halfExtents.z),
      0.0f);
}

float RagdollCapsuleVolume(float radius, float halfHeight) {
  radius = (std::max)(kRagdollMinShapeExtent, radius);
  halfHeight = (std::max)(0.0f, halfHeight);
  return 2.0f * xPI * radius * radius * halfHeight +
         (4.0f / 3.0f) * xPI * radius * radius * radius;
}

XVECTOR3 EquivalentBoxHalfExtentsFromCapsule(const PhysicsShapeDesc& shape) {
  const float radius = (std::max)(kRagdollMinShapeExtent, shape.radius);
  const float halfHeight = (std::max)(0.0f, shape.halfHeight);
  const float halfLength = (std::max)(radius + kRagdollMinShapeExtent, halfHeight + radius);
  const float volume = RagdollCapsuleVolume(radius, halfHeight);
  const float sideHalfExtent = std::sqrt((std::max)(kRagdollMinShapeExtent * kRagdollMinShapeExtent,
                                                   volume / (8.0f * halfLength)));
  return XVECTOR3(sideHalfExtent, halfLength, sideHalfExtent, 0.0f);
}

bool IsEditableRagdollShape(const PhysicsShapeDesc& shape) {
  return shape.type == PhysicsShapeType::Capsule || shape.type == PhysicsShapeType::Box;
}

float RagdollShapeSupportRadius(const PhysicsShapeDesc& shape,
                                const XMATRIX44& world,
                                const XVECTOR3& normalWorld) {
  const XVECTOR3 normal = NormalizeOr(normalWorld, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  if (shape.type == PhysicsShapeType::Box) {
    const XVECTOR3 extents = ClampRagdollBoxHalfExtents(shape.halfExtents);
    return std::fabs(Dot(MatrixAxisX(world), normal)) * extents.x +
           std::fabs(Dot(MatrixAxisY(world), normal)) * extents.y +
           std::fabs(Dot(NormalizeOr(XVECTOR3(world.m31, world.m32, world.m33, 0.0f),
                                     XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f)), normal)) * extents.z;
  }
  const float radius = (std::max)(kRagdollMinShapeExtent, shape.radius);
  const float halfHeight = (std::max)(0.0f, shape.halfHeight);
  return radius + std::fabs(Dot(MatrixAxisY(world), normal)) * halfHeight;
}

float Clamp01(float value) {
  return (std::max)(0.0f, (std::min)(1.0f, value));
}

void ClosestPointsOnSegments(const XVECTOR3& p1,
                             const XVECTOR3& q1,
                             const XVECTOR3& p2,
                             const XVECTOR3& q2,
                             XVECTOR3& outPoint1,
                             XVECTOR3& outPoint2) {
  constexpr float kEpsilon = 0.000001f;
  const XVECTOR3 d1 = q1 - p1;
  const XVECTOR3 d2 = q2 - p2;
  const XVECTOR3 r = p1 - p2;
  const float a = Dot(d1, d1);
  const float e = Dot(d2, d2);
  const float f = Dot(d2, r);

  float s = 0.0f;
  float t = 0.0f;
  if (a <= kEpsilon && e <= kEpsilon) {
    outPoint1 = p1;
    outPoint2 = p2;
    return;
  }

  if (a <= kEpsilon) {
    t = e > kEpsilon ? Clamp01(f / e) : 0.0f;
  } else {
    const float c = Dot(d1, r);
    if (e <= kEpsilon) {
      s = Clamp01(-c / a);
    } else {
      const float b = Dot(d1, d2);
      const float denom = a * e - b * b;
      if (std::fabs(denom) > kEpsilon) {
        s = Clamp01((b * f - c * e) / denom);
      }

      const float tNumerator = b * s + f;
      if (tNumerator < 0.0f) {
        t = 0.0f;
        s = Clamp01(-c / a);
      } else if (tNumerator > e) {
        t = 1.0f;
        s = Clamp01((b - c) / a);
      } else {
        t = tNumerator / e;
      }
    }
  }

  outPoint1 = p1 + d1 * s;
  outPoint2 = p2 + d2 * t;
  outPoint1.w = 1.0f;
  outPoint2.w = 1.0f;
}

bool ComputeCapsuleCapsuleContactAnchor(const PhysicsShapeDesc& childShape,
                                        const XMATRIX44& childWorld,
                                        const PhysicsShapeDesc& parentShape,
                                        const XMATRIX44& parentWorld,
                                        XVECTOR3& outAnchor) {
  auto getCapsuleSegment = [](const PhysicsShapeDesc& shape,
                              const XMATRIX44& bodyWorld,
                              XVECTOR3& outStart,
                              XVECTOR3& outEnd,
                              XVECTOR3& outCenter,
                              XVECTOR3& outAxis,
                              float& outRadius) {
    if (shape.type != PhysicsShapeType::Capsule) {
      return false;
    }
    outCenter = XVECTOR3(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
    outAxis = MatrixAxisY(bodyWorld);
    outRadius = (std::max)(kRagdollMinShapeExtent, shape.radius);
    const float halfHeight = (std::max)(0.0f, shape.halfHeight);
    outStart = outCenter - outAxis * halfHeight;
    outEnd = outCenter + outAxis * halfHeight;
    outStart.w = 1.0f;
    outEnd.w = 1.0f;
    return true;
  };

  XVECTOR3 childStart;
  XVECTOR3 childEnd;
  XVECTOR3 childCenter;
  XVECTOR3 childAxis;
  float childRadius = 0.0f;
  XVECTOR3 parentStart;
  XVECTOR3 parentEnd;
  XVECTOR3 parentCenter;
  XVECTOR3 parentAxis;
  float parentRadius = 0.0f;
  if (!getCapsuleSegment(childShape, childWorld, childStart, childEnd, childCenter, childAxis, childRadius) ||
      !getCapsuleSegment(parentShape, parentWorld, parentStart, parentEnd, parentCenter, parentAxis, parentRadius)) {
    return false;
  }

  XVECTOR3 childAxisPoint;
  XVECTOR3 parentAxisPoint;
  ClosestPointsOnSegments(childStart, childEnd, parentStart, parentEnd, childAxisPoint, parentAxisPoint);
  XVECTOR3 normal = NormalizeOr(parentAxisPoint - childAxisPoint, NormalizeOr(parentCenter - childCenter, parentAxis));
  const XVECTOR3 childSurface = childAxisPoint + normal * childRadius;
  const XVECTOR3 parentSurface = parentAxisPoint - normal * parentRadius;
  outAnchor = (childSurface + parentSurface) * 0.5f;
  outAnchor.w = 1.0f;
  return true;
}

bool ComputeRagdollShapeContactAnchor(const PhysicsShapeDesc& childShape,
                                      const XMATRIX44& childWorld,
                                      const PhysicsShapeDesc& parentShape,
                                      const XMATRIX44& parentWorld,
                                      XVECTOR3& outAnchor) {
  if (!IsEditableRagdollShape(childShape) || !IsEditableRagdollShape(parentShape)) {
    return false;
  }
  if (childShape.type == PhysicsShapeType::Capsule && parentShape.type == PhysicsShapeType::Capsule) {
    return ComputeCapsuleCapsuleContactAnchor(childShape, childWorld, parentShape, parentWorld, outAnchor);
  }
  const XVECTOR3 childCenter(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
  const XVECTOR3 parentCenter(parentWorld.m41, parentWorld.m42, parentWorld.m43, 1.0f);
  const XVECTOR3 centerDelta = Subtract(parentCenter, childCenter);
  const XVECTOR3 normal = NormalizeOr(centerDelta, MatrixAxisY(parentWorld));
  const float childSupport = RagdollShapeSupportRadius(childShape, childWorld, normal);
  const float parentSupport = RagdollShapeSupportRadius(parentShape, parentWorld, normal);
  const XVECTOR3 childSurface = AddScaled(childCenter, normal, childSupport);
  const XVECTOR3 parentSurface = AddScaled(parentCenter, normal, -parentSupport);
  outAnchor = XVECTOR3((childSurface.x + parentSurface.x) * 0.5f,
                       (childSurface.y + parentSurface.y) * 0.5f,
                       (childSurface.z + parentSurface.z) * 0.5f,
                       1.0f);
  return true;
}

bool GetGeneratedRagdollBoneWorldTransform(const PhysicsRagdollAnimationBinding& generatedBinding,
                                           int boneIndex,
                                           XMATRIX44& outWorld) {
  for (std::size_t i = 0; i < generatedBinding.referencePose.bones.size(); ++i) {
    if (generatedBinding.referencePose.bones[i].body.boneIndex != boneIndex ||
        i >= generatedBinding.bodyFromBone.size()) {
      continue;
    }
    XMATRIX44 boneFromBody;
    if (!InvertAffine(generatedBinding.bodyFromBone[i], boneFromBody)) {
      return false;
    }
    outWorld = boneFromBody * generatedBinding.referencePose.bones[i].body.worldTransform;
    return true;
  }
  return false;
}

bool GetRagdollAuthoringBoneWorldTransform(const RenderSkinnedMesh& mesh,
                                           const XMATRIX44& worldFromMesh,
                                           const PhysicsRagdollAnimationBinding& generatedBinding,
                                           int boneIndex,
                                           XMATRIX44& outWorld) {
  if (GetGeneratedRagdollBoneWorldTransform(generatedBinding, boneIndex, outWorld)) {
    return true;
  }
  const xF::xSkeleton* skeleton = FindReferenceSkeleton(mesh);
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return false;
  }
  outWorld = BoneWorldTransform(skeleton->Bones[static_cast<std::size_t>(boneIndex)], worldFromMesh);
  return true;
}

int GetEffectiveRagdollJointParent(const PhysicsRagdollAuthoringDesc& authoring, int childBody) {
  const std::size_t bodyCount = authoring.binding.referencePose.bones.size();
  if (childBody < 0 || childBody >= static_cast<int>(bodyCount)) {
    return -1;
  }
  if (childBody < static_cast<int>(authoring.jointParentBodyIndices.size())) {
    const int jointParent = authoring.jointParentBodyIndices[static_cast<std::size_t>(childBody)];
    if (jointParent == kPhysicsRagdollJointDisabled) {
      return -1;
    }
    if (jointParent >= 0 && jointParent < static_cast<int>(bodyCount) && jointParent != childBody) {
      return jointParent;
    }
  }
  if (childBody < static_cast<int>(authoring.parentBodyIndices.size())) {
    const int parent = authoring.parentBodyIndices[static_cast<std::size_t>(childBody)];
    if (parent >= 0 && parent < static_cast<int>(bodyCount) && parent != childBody) {
      return parent;
    }
  }
  return -1;
}

bool UpdateRagdollJointOffsetFromWorld(PhysicsRagdollAuthoringDesc& authoring,
                                       const RenderSkinnedMesh& mesh,
                                       const XMATRIX44& worldFromMesh,
                                       const PhysicsRagdollAnimationBinding& generatedBinding,
                                       int childBody) {
  auto& binding = authoring.binding;
  if (childBody < 0 || childBody >= static_cast<int>(binding.referencePose.bones.size())) {
    return false;
  }
  if (binding.jointFromBone.size() != binding.referencePose.bones.size()) {
    binding.jointFromBone.resize(binding.referencePose.bones.size(), XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
  }

  XMATRIX44 boneWorld;
  if (!GetRagdollAuthoringBoneWorldTransform(
          mesh,
          worldFromMesh,
          generatedBinding,
          binding.referencePose.bones[static_cast<std::size_t>(childBody)].body.boneIndex,
          boneWorld)) {
    binding.jointFromBone[static_cast<std::size_t>(childBody)] = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    return false;
  }
  XMATRIX44 inverseBoneWorld;
  if (!InvertAffine(boneWorld, inverseBoneWorld)) {
    return false;
  }
  binding.jointFromBone[static_cast<std::size_t>(childBody)] =
      TransformPhysicsPoint(
          binding.referencePose.bones[static_cast<std::size_t>(childBody)].jointWorldPosition,
          inverseBoneWorld);
  return true;
}

bool UpdateRagdollJointFrameOffsetsFromWorld(PhysicsRagdollAuthoringDesc& authoring, int childBody) {
  auto& binding = authoring.binding;
  auto& bones = binding.referencePose.bones;
  if (childBody < 0 || childBody >= static_cast<int>(bones.size())) {
    return false;
  }
  const int parentBody = GetEffectiveRagdollJointParent(authoring, childBody);
  const XMATRIX44& childWorld = bones[static_cast<std::size_t>(childBody)].body.worldTransform;
  const XMATRIX44& parentWorld =
      parentBody >= 0 && parentBody < static_cast<int>(bones.size())
          ? bones[static_cast<std::size_t>(parentBody)].body.worldTransform
          : childWorld;

  auto& bone = bones[static_cast<std::size_t>(childBody)];
  XVECTOR3 parentTwist = bone.parentJointTwistAxis;
  XVECTOR3 parentPlane = bone.parentJointPlaneAxis;
  XVECTOR3 childTwist = bone.childJointTwistAxis;
  XVECTOR3 childPlane = bone.childJointPlaneAxis;
  NormalizeJointFrameAxes(parentTwist, parentPlane, MatrixAxisY(parentWorld), MatrixAxisX(parentWorld));
  NormalizeJointFrameAxes(childTwist, childPlane, MatrixAxisY(childWorld), MatrixAxisX(childWorld));
  bone.parentJointTwistAxis = parentTwist;
  bone.parentJointPlaneAxis = parentPlane;
  bone.childJointTwistAxis = childTwist;
  bone.childJointPlaneAxis = childPlane;

  XMATRIX44 inverseParentWorld;
  XMATRIX44 inverseChildWorld;
  if (!InvertAffine(parentWorld, inverseParentWorld) || !InvertAffine(childWorld, inverseChildWorld)) {
    return false;
  }

  binding.parentJointTwistFromBody[static_cast<std::size_t>(childBody)] =
      TransformPhysicsVector(parentTwist, inverseParentWorld);
  binding.parentJointPlaneFromBody[static_cast<std::size_t>(childBody)] =
      TransformPhysicsVector(parentPlane, inverseParentWorld);
  binding.childJointTwistFromBody[static_cast<std::size_t>(childBody)] =
      TransformPhysicsVector(childTwist, inverseChildWorld);
  binding.childJointPlaneFromBody[static_cast<std::size_t>(childBody)] =
      TransformPhysicsVector(childPlane, inverseChildWorld);
  return true;
}

void SyncParentBodyIndicesFromBoneLinks(PhysicsRagdollAuthoringDesc& authoring) {
  const auto& bones = authoring.binding.referencePose.bones;
  authoring.parentBodyIndices.assign(bones.size(), -1);
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const int parent = FindRagdollDescIndexForBone(authoring.binding.referencePose, bones[i].parentBoneIndex);
    if (parent >= 0 && parent != static_cast<int>(i)) {
      authoring.parentBodyIndices[i] = parent;
    }
  }
}

bool ApplyRagdollParentBodyLinks(PhysicsRagdollAuthoringDesc& authoring) {
  auto& bones = authoring.binding.referencePose.bones;
  if (authoring.parentBodyIndices.size() != bones.size()) {
    return false;
  }
  for (std::size_t child = 0; child < bones.size(); ++child) {
    int parent = GetEffectiveRagdollJointParent(authoring, static_cast<int>(child));
    bool invalidParent = parent < 0 || parent >= static_cast<int>(bones.size()) || parent == static_cast<int>(child);
    int current = parent;
    for (std::size_t depth = 0; !invalidParent && depth < bones.size(); ++depth) {
      if (current == static_cast<int>(child)) {
        invalidParent = true;
        break;
      }
      if (current < 0 || current >= static_cast<int>(authoring.parentBodyIndices.size())) {
        break;
      }
      current = GetEffectiveRagdollJointParent(authoring, current);
    }
    bones[child].parentBoneIndex =
        invalidParent ? -1 : bones[static_cast<std::size_t>(parent)].body.boneIndex;
  }
  return true;
}

void EnsureRagdollAuthoringState(PhysicsRagdollAuthoringDesc& authoring) {
  auto& binding = authoring.binding;
  const std::size_t bodyCount = binding.referencePose.bones.size();
  if (authoring.parentBodyIndices.empty()) {
    SyncParentBodyIndicesFromBoneLinks(authoring);
  } else {
    authoring.parentBodyIndices.resize(bodyCount, -1);
  }
  authoring.jointParentBodyIndices.resize(bodyCount, kPhysicsRagdollJointInheritParent);
  authoring.frozenBodies.resize(bodyCount, 0u);
  authoring.frozenJoints.resize(bodyCount, 0u);
  authoring.contactJoints.resize(bodyCount, 0u);
  binding.jointFromBone.resize(bodyCount, XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
  binding.parentJointTwistFromBody.resize(bodyCount, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  binding.parentJointPlaneFromBody.resize(bodyCount, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  binding.childJointTwistFromBody.resize(bodyCount, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  binding.childJointPlaneFromBody.resize(bodyCount, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  binding.controlledBoneIndices.resize(bodyCount);
  binding.controlledBodyFromBone.resize(bodyCount);
  for (std::size_t i = 0; i < bodyCount; ++i) {
    if (binding.controlledBoneIndices[i].size() != binding.controlledBodyFromBone[i].size()) {
      binding.controlledBoneIndices[i].clear();
      binding.controlledBodyFromBone[i].clear();
    }
    if (binding.controlledBoneIndices[i].empty() &&
        i < binding.bodyFromBone.size() &&
        binding.referencePose.bones[i].body.boneIndex >= 0) {
      binding.controlledBoneIndices[i].push_back(binding.referencePose.bones[i].body.boneIndex);
      binding.controlledBodyFromBone[i].push_back(binding.bodyFromBone[i]);
    }
  }
  ApplyRagdollParentBodyLinks(authoring);
  for (int i = 0; i < static_cast<int>(bodyCount); ++i) {
    UpdateRagdollJointFrameOffsetsFromWorld(authoring, i);
  }
}

bool UpdateRagdollReferenceBodyFromLocal(PhysicsRagdollAuthoringDesc& authoring,
                                         const RenderSkinnedMesh& mesh,
                                         const XMATRIX44& worldFromMesh,
                                         const PhysicsRagdollAnimationBinding& generatedBinding,
                                         int bodyIndex) {
  auto& binding = authoring.binding;
  if (bodyIndex < 0 ||
      bodyIndex >= static_cast<int>(binding.referencePose.bones.size()) ||
      bodyIndex >= static_cast<int>(binding.bodyFromBone.size())) {
    return false;
  }
  auto& bone = binding.referencePose.bones[static_cast<std::size_t>(bodyIndex)];
  XMATRIX44 boneWorld;
  if (!GetRagdollAuthoringBoneWorldTransform(mesh, worldFromMesh, generatedBinding, bone.body.boneIndex, boneWorld)) {
    return false;
  }
  bone.body.worldTransform = binding.bodyFromBone[static_cast<std::size_t>(bodyIndex)] * boneWorld;
  if (bodyIndex < static_cast<int>(binding.jointFromBone.size())) {
    bone.jointWorldPosition =
        TransformPhysicsPoint(binding.jointFromBone[static_cast<std::size_t>(bodyIndex)], boneWorld);
  } else {
    bone.jointWorldPosition = XVECTOR3(boneWorld.m41, boneWorld.m42, boneWorld.m43, 1.0f);
    UpdateRagdollJointOffsetFromWorld(authoring, mesh, worldFromMesh, generatedBinding, bodyIndex);
  }

  if (bodyIndex < static_cast<int>(binding.controlledBoneIndices.size()) &&
      bodyIndex < static_cast<int>(binding.controlledBodyFromBone.size())) {
    const std::vector<int>& controlledBones = binding.controlledBoneIndices[static_cast<std::size_t>(bodyIndex)];
    std::vector<XMATRIX44>& controlledOffsets = binding.controlledBodyFromBone[static_cast<std::size_t>(bodyIndex)];
    controlledOffsets.clear();
    controlledOffsets.reserve(controlledBones.size());
    for (int controlledBone : controlledBones) {
      XMATRIX44 controlledBoneWorld;
      XMATRIX44 inverseControlledBoneWorld;
      if (GetRagdollAuthoringBoneWorldTransform(mesh, worldFromMesh, generatedBinding, controlledBone, controlledBoneWorld) &&
          InvertAffine(controlledBoneWorld, inverseControlledBoneWorld)) {
        controlledOffsets.push_back(bone.body.worldTransform * inverseControlledBoneWorld);
      } else {
        controlledOffsets.push_back(binding.bodyFromBone[static_cast<std::size_t>(bodyIndex)]);
      }
    }
  }
  UpdateRagdollJointFrameOffsetsFromWorld(authoring, bodyIndex);
  return true;
}

} // namespace

bool UpdateRagdollAuthoringBodyFromLocal(PhysicsRagdollAuthoringDesc& authoring,
                                         const RenderSkinnedMesh& mesh,
                                         const XMATRIX44& worldFromMesh,
                                         int bodyIndex) {
  return UpdateRagdollReferenceBodyFromLocal(authoring, mesh, worldFromMesh, authoring.binding, bodyIndex);
}

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
  InitializeRagdollJointFrames(outDesc);

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
  binding.parentJointTwistFromBody.resize(referencePose.bones.size());
  binding.parentJointPlaneFromBody.resize(referencePose.bones.size());
  binding.childJointTwistFromBody.resize(referencePose.bones.size());
  binding.childJointPlaneFromBody.resize(referencePose.bones.size());
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

    const int parentDescIndex = FindRagdollDescIndexForBone(referencePose, referencePose.bones[i].parentBoneIndex);
    const XMATRIX44& childBodyWorld = referencePose.bones[i].body.worldTransform;
    const XMATRIX44& parentBodyWorld = parentDescIndex >= 0
        ? referencePose.bones[static_cast<std::size_t>(parentDescIndex)].body.worldTransform
        : childBodyWorld;
    XMATRIX44 inverseChildBodyWorld;
    XMATRIX44 inverseParentBodyWorld;
    if (!InvertAffine(childBodyWorld, inverseChildBodyWorld) ||
        !InvertAffine(parentBodyWorld, inverseParentBodyWorld)) {
      return false;
    }

    XVECTOR3 parentTwist = referencePose.bones[i].parentJointTwistAxis;
    XVECTOR3 parentPlane = referencePose.bones[i].parentJointPlaneAxis;
    XVECTOR3 childTwist = referencePose.bones[i].childJointTwistAxis;
    XVECTOR3 childPlane = referencePose.bones[i].childJointPlaneAxis;
    NormalizeJointFrameAxes(parentTwist, parentPlane, MatrixAxisY(parentBodyWorld), MatrixAxisX(parentBodyWorld));
    NormalizeJointFrameAxes(childTwist, childPlane, MatrixAxisY(childBodyWorld), MatrixAxisX(childBodyWorld));
    binding.parentJointTwistFromBody[i] = TransformPhysicsVector(parentTwist, inverseParentBodyWorld);
    binding.parentJointPlaneFromBody[i] = TransformPhysicsVector(parentPlane, inverseParentBodyWorld);
    binding.childJointTwistFromBody[i] = TransformPhysicsVector(childTwist, inverseChildBodyWorld);
    binding.childJointPlaneFromBody[i] = TransformPhysicsVector(childPlane, inverseChildBodyWorld);
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

  const bool hasJointFrameOffsets =
      binding.parentJointTwistFromBody.size() == pose.bones.size() &&
      binding.parentJointPlaneFromBody.size() == pose.bones.size() &&
      binding.childJointTwistFromBody.size() == pose.bones.size() &&
      binding.childJointPlaneFromBody.size() == pose.bones.size();
  for (std::size_t i = 0; i < pose.bones.size(); ++i) {
    const int parentDescIndex = FindRagdollDescIndexForBone(pose, pose.bones[i].parentBoneIndex);
    const XMATRIX44& childBodyWorld = pose.bones[i].body.worldTransform;
    const XMATRIX44& parentBodyWorld = parentDescIndex >= 0
        ? pose.bones[static_cast<std::size_t>(parentDescIndex)].body.worldTransform
        : childBodyWorld;

    XVECTOR3 parentTwist = hasJointFrameOffsets
        ? TransformPhysicsVector(binding.parentJointTwistFromBody[i], parentBodyWorld)
        : pose.bones[i].parentJointTwistAxis;
    XVECTOR3 parentPlane = hasJointFrameOffsets
        ? TransformPhysicsVector(binding.parentJointPlaneFromBody[i], parentBodyWorld)
        : pose.bones[i].parentJointPlaneAxis;
    XVECTOR3 childTwist = hasJointFrameOffsets
        ? TransformPhysicsVector(binding.childJointTwistFromBody[i], childBodyWorld)
        : pose.bones[i].childJointTwistAxis;
    XVECTOR3 childPlane = hasJointFrameOffsets
        ? TransformPhysicsVector(binding.childJointPlaneFromBody[i], childBodyWorld)
        : pose.bones[i].childJointPlaneAxis;
    NormalizeJointFrameAxes(parentTwist, parentPlane, MatrixAxisY(parentBodyWorld), MatrixAxisX(parentBodyWorld));
    NormalizeJointFrameAxes(childTwist, childPlane, MatrixAxisY(childBodyWorld), MatrixAxisX(childBodyWorld));
    pose.bones[i].parentJointTwistAxis = parentTwist;
    pose.bones[i].parentJointPlaneAxis = parentPlane;
    pose.bones[i].childJointTwistAxis = childTwist;
    pose.bones[i].childJointPlaneAxis = childPlane;
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

bool BuildRagdollAuthoringFromSkeleton(const RenderSkinnedMesh& mesh,
                                       const XMATRIX44& worldFromMesh,
                                       uint32_t entityId,
                                       const PhysicsRagdollBuildSettings& settings,
                                       PhysicsRagdollAuthoringDesc& outAuthoring) {
  PhysicsRagdollDesc desc;
  if (!BuildRagdollDescFromSkeleton(mesh, worldFromMesh, entityId, settings, desc)) {
    return false;
  }

  PhysicsRagdollAnimationBinding binding;
  if (!BuildRagdollAnimationBinding(mesh, worldFromMesh, desc, binding)) {
    return false;
  }

  PhysicsRagdollAuthoringDesc authoring;
  authoring.binding = std::move(binding);
  authoring.parentBodyIndices.assign(authoring.binding.referencePose.bones.size(), -1);
  for (std::size_t i = 0; i < authoring.binding.referencePose.bones.size(); ++i) {
    const int parentBody =
        FindRagdollDescIndexForBone(
            authoring.binding.referencePose,
            authoring.binding.referencePose.bones[i].parentBoneIndex);
    if (parentBody >= 0 && parentBody != static_cast<int>(i)) {
      authoring.parentBodyIndices[i] = parentBody;
    }
  }
  EnsureRagdollAuthoringState(authoring);
  outAuthoring = std::move(authoring);
  return true;
}

std::string BuildRagdollEditModelKey(const std::string& modelPath) {
  std::string key = modelPath;
  const std::size_t slash = key.find_last_of("/\\");
  if (slash != std::string::npos) {
    key = key.substr(slash + 1);
  }
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return key;
}

std::string BuildRagdollEditResourcePath(const std::string& modelPathOrKey) {
  return "Models/RagdollEdits/" + FileSafeModelKey(BuildRagdollEditModelKey(modelPathOrKey)) + ".json";
}

std::filesystem::path ResolveRagdollEditWritePath(const std::string& resourcePath) {
  std::filesystem::path requested(resourcePath);
  if (requested.is_absolute()) {
    return requested;
  }

  const std::string normalized = ResourceLocator::NormalizePath(resourcePath);
#ifdef OS_ANDROID
  return ResourceLocator::Instance().ResolveCachePath(normalized);
#else
  ResourceLocator& locator = ResourceLocator::Instance();
  std::error_code ec;
  const std::filesystem::path existingPath = locator.ResolveFilePath(normalized);
  if (std::filesystem::is_regular_file(existingPath, ec)) {
    return existingPath;
  }

  auto canCreateNear = [](const std::filesystem::path& candidate) {
    const std::filesystem::path parent = candidate.parent_path();
    if (parent.empty()) {
      return false;
    }
    std::error_code existsEc;
    if (std::filesystem::exists(parent, existsEc)) {
      return true;
    }
    const std::filesystem::path parentParent = parent.parent_path();
    return !parentParent.empty() && std::filesystem::exists(parentParent, existsEc);
  };

  std::vector<std::filesystem::path> bases;
  if (!locator.GetBasePath().empty()) {
    bases.push_back(locator.GetBasePath());
  }
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    bases.push_back(cwd);
  }

  const std::filesystem::path relative(normalized);
  for (const std::filesystem::path& base : bases) {
    const std::array<std::filesystem::path, 3> candidates = {
        base / relative,
        base / "Assets" / relative,
        base / "T850" / "Assets" / relative};
    for (const std::filesystem::path& candidate : candidates) {
      if (canCreateNear(candidate)) {
        return candidate;
      }
    }
  }

  return relative;
#endif
}

bool LoadRagdollAuthoringAsset(const std::string& resourcePath,
                               const RenderSkinnedMesh& mesh,
                               const XMATRIX44& worldFromMesh,
                               const PhysicsRagdollAnimationBinding& generatedBinding,
                               PhysicsRagdollAuthoringDesc& outAuthoring,
                               int* outLoadedBodyCount) {
  if (outLoadedBodyCount) {
    *outLoadedBodyCount = 0;
  }

  std::string json;
  if (!ResourceLocator::Instance().ReadText(resourcePath, json)) {
    T8_LOG_INFO("[RagdollAuthoring] No saved ragdoll file found at '%s'; keeping generated ragdoll", resourcePath.c_str());
    return false;
  }

  RagdollEditJson data;
  if (!ParseRagdollEditJson(json, data)) {
    T8_LOG_ERROR("[RagdollAuthoring] Failed to parse '%s'", resourcePath.c_str());
    return false;
  }

  if (generatedBinding.referencePose.bones.size() != generatedBinding.bodyFromBone.size()) {
    T8_LOG_ERROR("[RagdollAuthoring] Cannot load '%s': generated binding is incomplete", resourcePath.c_str());
    return false;
  }

  PhysicsRagdollAuthoringDesc loaded;
  loaded.schema = data.schema;
  loaded.model = data.model;
  loaded.binding = generatedBinding;

  const auto& generatedBones = generatedBinding.referencePose.bones;
  std::vector<PhysicsRagdollBoneDesc> loadedBones;
  std::vector<XMATRIX44> loadedBodyFromBone;
  std::vector<XVECTOR3> loadedJointFromBone;
  std::vector<std::vector<int>> loadedControlledBones;
  std::vector<std::vector<XMATRIX44>> loadedControlledBodyFromBone;
  std::vector<int> loadedSavedIndices;
  std::vector<int> loadedParentRefs;
  std::vector<int> loadedJointParentRefs;
  std::vector<uint8_t> loadedFrozenBodies;
  std::vector<uint8_t> loadedFrozenJoints;
  std::vector<uint8_t> loadedContactJoints;
  loadedBones.reserve(data.bodies.size());
  loadedBodyFromBone.reserve(data.bodies.size());
  loadedJointFromBone.reserve(data.bodies.size());
  loadedControlledBones.reserve(data.bodies.size());
  loadedControlledBodyFromBone.reserve(data.bodies.size());
  loadedSavedIndices.reserve(data.bodies.size());
  loadedParentRefs.reserve(data.bodies.size());
  loadedJointParentRefs.reserve(data.bodies.size());
  loadedFrozenBodies.reserve(data.bodies.size());
  loadedFrozenJoints.reserve(data.bodies.size());
  loadedContactJoints.reserve(data.bodies.size());

  const float instanceUniformScale = UniformScaleFromWorldTransform(worldFromMesh);
  int applied = 0;
  for (const RagdollEditBodyJson& body : data.bodies) {
    int target = -1;
    if (body.index >= 0 && body.index < static_cast<int>(generatedBones.size())) {
      const auto& candidate = generatedBones[static_cast<std::size_t>(body.index)];
      if (body.boneIndex < 0 || candidate.body.boneIndex == body.boneIndex) {
        target = body.index;
      }
    }
    if (target < 0) {
      for (int i = 0; i < static_cast<int>(generatedBones.size()); ++i) {
        if (generatedBones[static_cast<std::size_t>(i)].body.boneIndex == body.boneIndex) {
          target = i;
          break;
        }
      }
    }
    if (target < 0) {
      T8_LOG_ERROR("[RagdollAuthoring] Skipping saved body '%s': bone %d is not in generated ragdoll for '%s'",
                   body.name.c_str(), body.boneIndex, resourcePath.c_str());
      continue;
    }
    if (body.boneIndex >= 0) {
      bool duplicate = false;
      for (const PhysicsRagdollBoneDesc& existing : loadedBones) {
        if (existing.body.boneIndex == body.boneIndex) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) {
        continue;
      }
    }

    PhysicsRagdollBoneDesc targetBone = generatedBones[static_cast<std::size_t>(target)];
    if (!body.name.empty()) {
      targetBone.body.debugName = body.name;
    }
    XMATRIX44 targetBodyFromBone =
        data.schema >= 3
            ? MatrixFromArray16(body.bodyFromBone)
            : generatedBinding.bodyFromBone[static_cast<std::size_t>(target)];
    XMATRIX44 targetPrimaryBoneWorld;
    const bool hasTargetBoneWorld =
        GetRagdollAuthoringBoneWorldTransform(
            mesh, worldFromMesh, generatedBinding, targetBone.body.boneIndex, targetPrimaryBoneWorld);
    if (hasTargetBoneWorld) {
      targetBone.body.worldTransform = targetBodyFromBone * targetPrimaryBoneWorld;
    }
    if (data.schema >= 6) {
      targetBone.jointType =
          body.jointType == static_cast<int>(PhysicsRagdollJointType::Fixed)
              ? PhysicsRagdollJointType::Fixed
              : PhysicsRagdollJointType::SwingTwist;
      if (body.hasJointAnchor) {
        targetBone.jointWorldPosition =
            TransformPhysicsPoint(
                XVECTOR3(body.jointAnchor[0], body.jointAnchor[1], body.jointAnchor[2], 1.0f),
                worldFromMesh);
      }
    }
    if (data.schema >= 11) {
      if (body.hasParentJointTwistAxis) {
        targetBone.parentJointTwistAxis =
            TransformSavedRagdollAxis(
                body.parentJointTwistAxis,
                worldFromMesh,
                targetBone.parentJointTwistAxis);
      }
      if (body.hasParentJointPlaneAxis) {
        targetBone.parentJointPlaneAxis =
            TransformSavedRagdollAxis(
                body.parentJointPlaneAxis,
                worldFromMesh,
                targetBone.parentJointPlaneAxis);
      }
      if (body.hasChildJointTwistAxis) {
        targetBone.childJointTwistAxis =
            TransformSavedRagdollAxis(
                body.childJointTwistAxis,
                worldFromMesh,
                targetBone.childJointTwistAxis);
      }
      if (body.hasChildJointPlaneAxis) {
        targetBone.childJointPlaneAxis =
            TransformSavedRagdollAxis(
                body.childJointPlaneAxis,
                worldFromMesh,
                targetBone.childJointPlaneAxis);
      }
    }

    XVECTOR3 targetJointFromBone(0.0f, 0.0f, 0.0f, 1.0f);
    if (hasTargetBoneWorld) {
      XMATRIX44 inverseTargetBoneWorld;
      if (InvertAffine(targetPrimaryBoneWorld, inverseTargetBoneWorld)) {
        targetJointFromBone = TransformPhysicsPoint(targetBone.jointWorldPosition, inverseTargetBoneWorld);
      }
    }

    targetBone.body.shape.type =
        data.schema >= 10 ? RagdollShapeTypeFromSaveName(body.shapeType) : PhysicsShapeType::Capsule;
    targetBone.body.shape.radius = (std::max)(kRagdollMinShapeExtent, body.radius * instanceUniformScale);
    targetBone.body.shape.halfHeight = (std::max)(kRagdollMinShapeExtent, body.halfHeight * instanceUniformScale);
    if (data.schema >= 10 && targetBone.body.shape.type == PhysicsShapeType::Box) {
      targetBone.body.shape.halfExtents =
          ClampRagdollBoxHalfExtents(
              XVECTOR3(
                  body.halfExtents[0] * instanceUniformScale,
                  body.halfExtents[1] * instanceUniformScale,
                  body.halfExtents[2] * instanceUniformScale,
                  0.0f));
    } else {
      targetBone.body.shape.halfExtents = EquivalentBoxHalfExtentsFromCapsule(targetBone.body.shape);
    }
    if (data.schema >= 2) {
      targetBone.swingLimitRadians = (std::max)(0.0f, body.swingLimitRadians);
      targetBone.twistLimitRadians = (std::max)(0.0f, body.twistLimitRadians);
    }

    std::vector<int> controlledBones;
    if (data.schema >= 4) {
      controlledBones = body.controlledBones;
    }
    if (data.schema < 4 && controlledBones.empty() && targetBone.body.boneIndex >= 0) {
      controlledBones.push_back(targetBone.body.boneIndex);
    }

    std::vector<XMATRIX44> controlledBodyFromBone;
    controlledBodyFromBone.reserve(controlledBones.size());
    for (int controlledBone : controlledBones) {
      XMATRIX44 controlledBoneWorld;
      XMATRIX44 inverseControlledBoneWorld;
      if (!GetRagdollAuthoringBoneWorldTransform(
              mesh, worldFromMesh, generatedBinding, controlledBone, controlledBoneWorld) ||
          !InvertAffine(controlledBoneWorld, inverseControlledBoneWorld)) {
        T8_LOG_ERROR("[RagdollAuthoring] Saved controlled bone %d for body %d has a singular or missing transform",
                     controlledBone, targetBone.body.boneIndex);
        continue;
      }
      controlledBodyFromBone.push_back(targetBone.body.worldTransform * inverseControlledBoneWorld);
    }
    if (controlledBodyFromBone.size() != controlledBones.size()) {
      controlledBones.clear();
      controlledBodyFromBone.clear();
      if (targetBone.body.boneIndex >= 0) {
        controlledBones.push_back(targetBone.body.boneIndex);
        controlledBodyFromBone.push_back(targetBodyFromBone);
      }
    }

    loadedBones.push_back(targetBone);
    loadedBodyFromBone.push_back(targetBodyFromBone);
    loadedJointFromBone.push_back(targetJointFromBone);
    loadedControlledBones.push_back(std::move(controlledBones));
    loadedControlledBodyFromBone.push_back(std::move(controlledBodyFromBone));
    loadedSavedIndices.push_back(body.index >= 0 ? body.index : static_cast<int>(loadedSavedIndices.size()));
    loadedParentRefs.push_back(data.schema >= 5 ? body.parentBody : -1);
    loadedJointParentRefs.push_back(data.schema >= 6 ? body.jointParentBody : kPhysicsRagdollJointInheritParent);
    loadedFrozenBodies.push_back(data.schema >= 7 && body.bodyFrozen ? 1u : 0u);
    loadedFrozenJoints.push_back(data.schema >= 7 && body.jointFrozen ? 1u : 0u);
    const bool legacyContactAnchor = data.schema < 9 && body.jointParentBody != kPhysicsRagdollJointDisabled;
    loadedContactJoints.push_back(
        ((data.schema >= 9 && body.hasJointContactAnchor && body.jointContactAnchor) || legacyContactAnchor) ? 1u : 0u);
    ++applied;
  }

  if (applied <= 0 && !data.bodies.empty()) {
    return false;
  }

  loaded.binding.referencePose.bones = std::move(loadedBones);
  loaded.binding.bodyFromBone = std::move(loadedBodyFromBone);
  loaded.binding.jointFromBone = std::move(loadedJointFromBone);
  loaded.binding.controlledBoneIndices = std::move(loadedControlledBones);
  loaded.binding.controlledBodyFromBone = std::move(loadedControlledBodyFromBone);
  loaded.frozenBodies = std::move(loadedFrozenBodies);
  loaded.frozenJoints = std::move(loadedFrozenJoints);
  loaded.contactJoints = std::move(loadedContactJoints);

  loaded.parentBodyIndices.assign(loaded.binding.referencePose.bones.size(), -1);
  loaded.jointParentBodyIndices.assign(
      loaded.binding.referencePose.bones.size(),
      kPhysicsRagdollJointInheritParent);
  if (data.schema >= 5) {
    auto findLoadedBySavedIndex = [&](int savedIndex) {
      for (int i = 0; i < static_cast<int>(loadedSavedIndices.size()); ++i) {
        if (loadedSavedIndices[static_cast<std::size_t>(i)] == savedIndex) {
          return i;
        }
      }
      if (savedIndex >= 0 && savedIndex < static_cast<int>(loaded.binding.referencePose.bones.size())) {
        return savedIndex;
      }
      return -1;
    };
    for (int child = 0; child < static_cast<int>(loadedParentRefs.size()); ++child) {
      loaded.parentBodyIndices[static_cast<std::size_t>(child)] =
          findLoadedBySavedIndex(loadedParentRefs[static_cast<std::size_t>(child)]);
    }
    if (data.schema >= 6) {
      for (int child = 0; child < static_cast<int>(loadedJointParentRefs.size()); ++child) {
        const int savedJointParent = loadedJointParentRefs[static_cast<std::size_t>(child)];
        loaded.jointParentBodyIndices[static_cast<std::size_t>(child)] =
            savedJointParent >= 0 ? findLoadedBySavedIndex(savedJointParent) : savedJointParent;
      }
    }
  } else {
    SyncParentBodyIndicesFromBoneLinks(loaded);
  }

  EnsureRagdollAuthoringState(loaded);
  for (int i = 0; i < static_cast<int>(loaded.binding.referencePose.bones.size()); ++i) {
    UpdateRagdollReferenceBodyFromLocal(loaded, mesh, worldFromMesh, generatedBinding, i);
  }

  int repairedContactAnchors = 0;
  for (int childBody = 0; childBody < static_cast<int>(loaded.binding.referencePose.bones.size()); ++childBody) {
    if (childBody >= static_cast<int>(loaded.contactJoints.size()) ||
        loaded.contactJoints[static_cast<std::size_t>(childBody)] == 0u) {
      continue;
    }
    const int parentBody = GetEffectiveRagdollJointParent(loaded, childBody);
    if (parentBody < 0 || parentBody >= static_cast<int>(loaded.binding.referencePose.bones.size()) || parentBody == childBody) {
      continue;
    }
    XVECTOR3 contactAnchor;
    auto& bones = loaded.binding.referencePose.bones;
    if (ComputeRagdollShapeContactAnchor(
            bones[static_cast<std::size_t>(childBody)].body.shape,
            bones[static_cast<std::size_t>(childBody)].body.worldTransform,
            bones[static_cast<std::size_t>(parentBody)].body.shape,
            bones[static_cast<std::size_t>(parentBody)].body.worldTransform,
            contactAnchor)) {
      bones[static_cast<std::size_t>(childBody)].jointWorldPosition = contactAnchor;
      UpdateRagdollJointOffsetFromWorld(loaded, mesh, worldFromMesh, generatedBinding, childBody);
      ++repairedContactAnchors;
    }
  }
  if (repairedContactAnchors > 0) {
    T8_LOG_INFO("[RagdollAuthoring] Recovered %d contact joint anchors from shape surfaces for '%s'",
                repairedContactAnchors, resourcePath.c_str());
  }

  EnsureRagdollAuthoringState(loaded);
  outAuthoring = std::move(loaded);
  if (outLoadedBodyCount) {
    *outLoadedBodyCount = applied;
  }
  T8_LOG_INFO("[RagdollAuthoring] Loaded %d edited bodies from '%s' instanceScale=%.6f",
              applied,
              resourcePath.c_str(),
              instanceUniformScale);
  return true;
}

bool SaveRagdollAuthoringAsset(const std::string& resourcePath,
                               const std::string& modelKey,
                               const PhysicsRagdollAuthoringDesc& authoring,
                               std::filesystem::path* outResolvedPath) {
  const auto& binding = authoring.binding;
  const auto& bones = binding.referencePose.bones;
  if (bones.size() != binding.bodyFromBone.size()) {
    T8_LOG_ERROR("[RagdollAuthoring] Cannot save '%s': ragdoll binding has %zu bodies but %zu local frames",
                 resourcePath.c_str(), bones.size(), binding.bodyFromBone.size());
    return false;
  }

  const std::filesystem::path path = ResolveRagdollEditWritePath(resourcePath);
  if (outResolvedPath) {
    *outResolvedPath = path;
  }

  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  if (ec) {
    T8_LOG_ERROR("[RagdollAuthoring] Failed to create '%s'", path.parent_path().string().c_str());
    return false;
  }

  std::ofstream file(path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[RagdollAuthoring] Failed to open '%s' for writing", path.string().c_str());
    return false;
  }

  const std::string savedModel = modelKey.empty() ? authoring.model : modelKey;
  file << "{\n";
  file << "  \"schema\": " << kPhysicsRagdollEditSchemaVersion << ",\n";
  file << "  \"model\": \"" << JsonEscape(savedModel) << "\",\n";
  file << "  \"constraint_profile\": \"inferred-name-v1\",\n";
  file << "  \"capsule_frame_profile\": \"bone-to-endpoint-v1\",\n";
  file << "  \"capsules\": [\n";
  file << std::fixed << std::setprecision(8);
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const auto& bone = bones[i];
    const auto& shape = bone.body.shape;
    const auto matrix = MatrixToArray16(binding.bodyFromBone[i]);
    const int parentBody = i < authoring.parentBodyIndices.size() ? authoring.parentBodyIndices[i] : -1;
    const int jointParentBody =
        i < authoring.jointParentBodyIndices.size()
            ? authoring.jointParentBodyIndices[i]
            : kPhysicsRagdollJointInheritParent;
    const bool bodyFrozen = i < authoring.frozenBodies.size() && authoring.frozenBodies[i] != 0u;
    const bool jointFrozen = i < authoring.frozenJoints.size() && authoring.frozenJoints[i] != 0u;
    const bool jointContactAnchor = i < authoring.contactJoints.size() && authoring.contactJoints[i] != 0u;
    file << "    {\n";
    file << "      \"index\": " << i << ",\n";
    file << "      \"bone_index\": " << bone.body.boneIndex << ",\n";
    file << "      \"name\": \"" << JsonEscape(bone.body.debugName) << "\",\n";
    file << "      \"parent_capsule\": " << parentBody << ",\n";
    file << "      \"joint_parent_capsule\": " << jointParentBody << ",\n";
    file << "      \"capsule_frozen\": " << (bodyFrozen ? "true" : "false") << ",\n";
    file << "      \"joint_frozen\": " << (jointFrozen ? "true" : "false") << ",\n";
    file << "      \"joint_contact_anchor\": " << (jointContactAnchor ? "true" : "false") << ",\n";
    file << "      \"joint_type\": " << (bone.jointType == PhysicsRagdollJointType::Fixed ? 1 : 0) << ",\n";
    file << "      \"joint_anchor\": [" << bone.jointWorldPosition.x << ", "
         << bone.jointWorldPosition.y << ", " << bone.jointWorldPosition.z << "],\n";
    file << "      \"parent_joint_twist_axis\": [" << bone.parentJointTwistAxis.x << ", "
         << bone.parentJointTwistAxis.y << ", " << bone.parentJointTwistAxis.z << "],\n";
    file << "      \"parent_joint_plane_axis\": [" << bone.parentJointPlaneAxis.x << ", "
         << bone.parentJointPlaneAxis.y << ", " << bone.parentJointPlaneAxis.z << "],\n";
    file << "      \"child_joint_twist_axis\": [" << bone.childJointTwistAxis.x << ", "
         << bone.childJointTwistAxis.y << ", " << bone.childJointTwistAxis.z << "],\n";
    file << "      \"child_joint_plane_axis\": [" << bone.childJointPlaneAxis.x << ", "
         << bone.childJointPlaneAxis.y << ", " << bone.childJointPlaneAxis.z << "],\n";
    file << "      \"body_from_bone\": [";
    for (std::size_t valueIndex = 0; valueIndex < matrix.size(); ++valueIndex) {
      if (valueIndex > 0) file << ", ";
      file << matrix[valueIndex];
    }
    file << "],\n";
    const XVECTOR3 savedHalfExtents = shape.type == PhysicsShapeType::Box
        ? ClampRagdollBoxHalfExtents(shape.halfExtents)
        : EquivalentBoxHalfExtentsFromCapsule(shape);
    file << "      \"shape_type\": \"" << RagdollShapeTypeSaveName(shape.type) << "\",\n";
    file << "      \"radius\": " << shape.radius << ",\n";
    file << "      \"half_height\": " << shape.halfHeight << ",\n";
    file << "      \"half_extents\": [" << savedHalfExtents.x << ", "
         << savedHalfExtents.y << ", " << savedHalfExtents.z << "],\n";
    file << "      \"swing_limit\": " << bone.swingLimitRadians << ",\n";
    file << "      \"twist_limit\": " << bone.twistLimitRadians << ",\n";
    file << "      \"controlled_bones\": [";
    if (i < binding.controlledBoneIndices.size()) {
      const std::vector<int>& controlledBones = binding.controlledBoneIndices[i];
      for (std::size_t controlledIndex = 0; controlledIndex < controlledBones.size(); ++controlledIndex) {
        if (controlledIndex > 0) file << ", ";
        file << controlledBones[controlledIndex];
      }
    }
    file << "]\n";
    file << "    }" << (i + 1 < bones.size() ? "," : "") << "\n";
  }
  file << "  ]\n";
  file << "}\n";

  T8_LOG_INFO("[RagdollAuthoring] Saved %zu bodies to '%s'", bones.size(), path.string().c_str());
  return true;
}

} // namespace t850
