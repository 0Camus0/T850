#include <SandboxScene.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>
#include <utils/RuntimeProfile.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderMesh.h>
#include <scene/RenderSkinnedMesh.h>
#include <scene/SceneDescriptor.h>
#include <scene/IBLResources.h>
#include <core/Config.h>
#include <core/EngineContext.h>
#include <physics/PhysicsAuthoring.h>
#include <utils/Picking.h>
#include <utils/ResourceLocator.h>
#ifdef OS_ANDROID
#include <video/vulkan/VulkanDriver.h>
#endif
#include <imgui/DevGuiContext.h>
#include <array>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <string>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>
#include <functional>
#include <initializer_list>
#include <limits>

using namespace t850;
using std::string;

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;
}

namespace {
  bool InvertAffineNoExit(const XMATRIX44& matrix, XMATRIX44& out) {
    for (int i = 0; i < 16; ++i) {
      if (!std::isfinite(matrix.mat[i])) {
        return false;
      }
    }

    const float a00 = matrix.m11, a01 = matrix.m12, a02 = matrix.m13;
    const float a10 = matrix.m21, a11 = matrix.m22, a12 = matrix.m23;
    const float a20 = matrix.m31, a21 = matrix.m32, a22 = matrix.m33;

    const float det =
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);
    if (!std::isfinite(det) || std::fabs(det) <= 0.000001f) {
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

  std::filesystem::path ResolveRagdollEditWritePath(const std::string& resourcePath) {
    std::filesystem::path requested(resourcePath);
    if (requested.is_absolute()) {
      return requested;
    }

    const std::string normalized = t850::ResourceLocator::NormalizePath(resourcePath);
#ifdef OS_ANDROID
    return t850::ResourceLocator::Instance().ResolveCachePath(normalized);
#else
    t850::ResourceLocator& locator = t850::ResourceLocator::Instance();
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

  t850::Mat4Json MatrixToSnapshotJson(const XMATRIX44& mat) {
    t850::Mat4Json j;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        j[r][c] = mat.m[r][c];
    return j;
  }

  XMATRIX44 MatrixFromSnapshotJson(const t850::Mat4Json& j) {
    XMATRIX44 mat;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        mat.m[r][c] = j[r][c];
    return mat;
  }

  t850::SnapshotSkinnedJson CaptureSkinnedSnapshot(RenderSkinnedMesh* skinned,
                                                   bool wireframeVisible,
                                                   bool skeletonVisible) {
    t850::SnapshotSkinnedJson snap;
    if (!skinned || !skinned->HasSkinData()) return snap;

    snap.has_skin = true;
    snap.playing = skinned->IsPlaying();
    snap.looping = skinned->IsLooping();
    snap.use_slerp = skinned->GetUseSlerp();
    snap.use_quat_skinning = skinned->GetUseQuatSkinning();
    snap.keyframe_mode = skinned->GetKeyframeMode();
    snap.wireframe_visible = wireframeVisible;
    snap.skeleton_visible = skeletonVisible;
    snap.animation_speed = skinned->GetAnimSpeed();
    snap.local_time = skinned->GetAnimLocalTime();
    snap.tick_time = skinned->GetAnimTickTime();
    snap.ticks_per_second = skinned->GetAnimTicksPerSecond();
    snap.current_anim_set = skinned->GetCurrentAnimSet();
    snap.num_anim_sets = skinned->GetNumAnimSets();
    snap.current_keyframe = skinned->GetCurrentKeyframe();
    snap.total_keyframes = skinned->GetTotalKeyframes();
    snap.num_bones = skinned->GetNumBones();
    snap.bone_texture_width = skinned->GetBoneTextureWidth();
    snap.bone_texture_rgba32f = skinned->GetBoneTextureData();

    std::vector<XMATRIX44> bones;
    skinned->ExportBoneMatrices(bones);
    snap.bone_matrices.reserve(bones.size());
    for (const XMATRIX44& bone : bones) {
      snap.bone_matrices.push_back(MatrixToSnapshotJson(bone));
    }
    return snap;
  }

  void ApplySkinnedSnapshot(RenderSkinnedMesh* skinned,
                            const t850::SnapshotSkinnedJson& snap,
                            bool& wireframeVisible,
                            bool& skeletonVisible) {
    if (!skinned || !skinned->HasSkinData() || !snap.has_skin) return;

    skinned->SetAnimSpeed(snap.animation_speed);
    skinned->SetLooping(snap.looping);
    skinned->SetUseSlerp(snap.use_slerp);
    skinned->SetUseQuatSkinning(snap.use_quat_skinning);
    skinned->SetKeyframeMode(snap.keyframe_mode);
    if (snap.playing) skinned->PlayAnimation();
    else skinned->PauseAnimation();

    int targetSet = snap.current_anim_set;
    int numSets = skinned->GetNumAnimSets();
    if (numSets > 0 && targetSet >= 0 && targetSet < numSets) {
      int guard = 0;
      while (skinned->GetCurrentAnimSet() != targetSet && guard++ < numSets) {
        skinned->NextAnimation();
      }
    }

    std::vector<XMATRIX44> bones;
    bones.reserve(snap.bone_matrices.size());
    for (const auto& bone : snap.bone_matrices) {
      bones.push_back(MatrixFromSnapshotJson(bone));
    }
    if (!bones.empty()) {
      skinned->ApplySnapshotBoneMatrices(bones);
    } else {
      skinned->ClearSnapshotBoneMatrices();
    }

    wireframeVisible = snap.wireframe_visible;
    skeletonVisible = snap.skeleton_visible;
  }

  bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
  }

  bool VecNearlyEqual(const std::array<float, 3>& lhs, const std::array<float, 3>& rhs) {
    return NearlyEqual(lhs[0], rhs[0]) && NearlyEqual(lhs[1], rhs[1]) && NearlyEqual(lhs[2], rhs[2]);
  }

  std::string SandboxProfileModelKey(const std::string& path) {
    std::string key = path;
    size_t slash = key.find_last_of("/\\");
    if (slash != std::string::npos)
      key = key.substr(slash + 1);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
      return (char)std::tolower(ch);
    });
    return key;
  }

  const t850::FloatOverrideDesc* FindFloatOverride(const std::vector<t850::FloatOverrideDesc>& values, const std::string& name) {
    for (const auto& value : values)
      if (value.name == name) return &value;
    return nullptr;
  }

  const t850::BoolOverrideDesc* FindBoolOverride(const std::vector<t850::BoolOverrideDesc>& values, const std::string& name) {
    for (const auto& value : values)
      if (value.name == name) return &value;
    return nullptr;
  }

  const t850::IntOverrideDesc* FindIntOverride(const std::vector<t850::IntOverrideDesc>& values, const std::string& name) {
    for (const auto& value : values)
      if (value.name == name) return &value;
    return nullptr;
  }

  const t850::SandboxLightOverrideDesc* FindLightOverride(const std::vector<t850::SandboxLightOverrideDesc>& values, int index) {
    for (const auto& value : values)
      if (value.index == index) return &value;
    return nullptr;
  }

  std::array<float, 3> ToArray(const XVECTOR3& value) {
    return {value.x, value.y, value.z};
  }

  XVECTOR3 FromArray(const std::array<float, 3>& value) {
    return XVECTOR3(value[0], value[1], value[2]);
  }

  XVECTOR3 TransformPoint(const XVECTOR3& point, const XMATRIX44& matrix) {
    return XVECTOR3(
        point.x * matrix.m11 + point.y * matrix.m21 + point.z * matrix.m31 + matrix.m41,
        point.x * matrix.m12 + point.y * matrix.m22 + point.z * matrix.m32 + matrix.m42,
        point.x * matrix.m13 + point.y * matrix.m23 + point.z * matrix.m33 + matrix.m43,
        1.0f);
  }

  XVECTOR3 TransformVectorNoTranslation(const XVECTOR3& vector, const XMATRIX44& matrix) {
    return XVECTOR3(
        vector.x * matrix.m11 + vector.y * matrix.m21 + vector.z * matrix.m31,
        vector.x * matrix.m12 + vector.y * matrix.m22 + vector.z * matrix.m32,
        vector.x * matrix.m13 + vector.y * matrix.m23 + vector.z * matrix.m33,
        0.0f);
  }

  float Dot3(const XVECTOR3& lhs, const XVECTOR3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
  }

  float Length3(const XVECTOR3& vector) {
    return std::sqrt((std::max)(0.0f, Dot3(vector, vector)));
  }

  constexpr float kMaxPhysicsAuthoringCoordinate = 1.0e12f;

  bool IsUsablePhysicsCoordinate(float value) {
    return std::isfinite(value) && std::fabs(value) <= kMaxPhysicsAuthoringCoordinate;
  }

  bool IsUsablePhysicsPoint(const XVECTOR3& point) {
    return IsUsablePhysicsCoordinate(point.x) &&
           IsUsablePhysicsCoordinate(point.y) &&
           IsUsablePhysicsCoordinate(point.z);
  }

  bool IsUsableRenderBounds(const RenderMesh::AABB& bounds) {
    return IsUsablePhysicsPoint(bounds.min) &&
           IsUsablePhysicsPoint(bounds.max) &&
           bounds.min.x <= bounds.max.x &&
           bounds.min.y <= bounds.max.y &&
           bounds.min.z <= bounds.max.z;
  }

  XVECTOR3 Cross3(const XVECTOR3& lhs, const XVECTOR3& rhs) {
    return XVECTOR3(
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
        0.0f);
  }

  XVECTOR3 Normalize3(const XVECTOR3& vector, const XVECTOR3& fallback = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)) {
    const float length = Length3(vector);
    if (length <= 0.000001f) {
      return fallback;
    }
    return XVECTOR3(vector.x / length, vector.y / length, vector.z / length, 0.0f);
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

  bool NameContainsAny(const std::string& name, std::initializer_list<const char*> tokens) {
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

  bool IsHumanoidDisplayBoneName(const std::string& lowerName) {
    if (lowerName.empty() || IsDeformationHelperBoneName(lowerName) || IsAttachmentBoneName(lowerName)) {
      return false;
    }
    if (NameContainsAny(lowerName, {"hips", "pelvis", "spine", "chest", "neck", "head", "clavicle"})) return true;
    if (NameContainsAny(lowerName, {"arm_upper", "upperarm", "upper_arm", "arm_lower", "lowerarm", "forearm", "lower_arm", "arm_hand"})) return true;
    if (NameContains(lowerName, "hand") && !NameContains(lowerName, "weapon")) return true;
    if (NameContainsAny(lowerName, {"leg_upper", "upperleg", "upper_leg", "thigh", "leg_lower", "lowerleg", "lower_leg", "calf", "shin", "leg_foot"})) return true;
    return NameContains(lowerName, "foot");
  }

  bool IsEndpointHelperForBone(const std::string& parentLowerName, const std::string& childLowerName) {
    if (NameContains(childLowerName, "end")) return true;
    if (NameContains(parentLowerName, "foot") && NameContainsAny(childLowerName, {"ball", "toe"})) return true;
    return false;
  }

  bool IsSpineLikeDisplayName(const std::string& lowerName) {
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

  int DisplayChildPriority(const std::string& parentLowerName, const std::string& childLowerName) {
    int score = 0;
    if (IsAttachmentBoneName(childLowerName)) score -= 1000;
    if (IsDeformationHelperBoneName(childLowerName)) score -= 300;
    if (NameContainsAny(parentLowerName, {"hips", "pelvis"})) {
      if (NameContainsAny(childLowerName, {"spine", "chest", "neck", "head"})) score += 600;
      if (IsUpperLegName(childLowerName)) score += 200;
    } else if (NameContainsAny(parentLowerName, {"spine", "chest"})) {
      if (NameContainsAny(childLowerName, {"spine", "chest", "neck", "head"})) score += 600;
      if (NameContains(childLowerName, "clavicle")) score += 150;
    } else if (NameContains(parentLowerName, "neck")) {
      if (NameContains(childLowerName, "head")) score += 600;
    } else if (NameContains(parentLowerName, "clavicle")) {
      if (IsUpperArmName(childLowerName)) score += 600;
    } else if (IsUpperArmName(parentLowerName)) {
      if (IsLowerArmName(childLowerName)) score += 600;
    } else if (IsLowerArmName(parentLowerName)) {
      if (IsHandName(childLowerName)) score += 600;
    } else if (IsUpperLegName(parentLowerName)) {
      if (IsLowerLegName(childLowerName)) score += 600;
    } else if (IsLowerLegName(parentLowerName)) {
      if (IsFootName(childLowerName)) score += 600;
    } else if (IsFootName(parentLowerName)) {
      if (NameContainsAny(childLowerName, {"ball", "toe"})) score += 600;
    }
    if (IsHumanoidDisplayBoneName(childLowerName)) score += 100;
    return score;
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

  XMATRIX44 MakeCapsuleBodyTransform(const XVECTOR3& position, const XVECTOR3& localY) {
    const XVECTOR3 up = Normalize3(localY);
    const XVECTOR3 reference = std::fabs(Dot3(up, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f))) > 0.92f
        ? XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f)
        : XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
    const XVECTOR3 right = Normalize3(Cross3(up, reference), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    const XVECTOR3 forward = Normalize3(Cross3(right, up), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));

    XMATRIX44 out;
    out.m11 = right.x;   out.m12 = right.y;   out.m13 = right.z;   out.m14 = 0.0f;
    out.m21 = up.x;      out.m22 = up.y;      out.m23 = up.z;      out.m24 = 0.0f;
    out.m31 = forward.x; out.m32 = forward.y; out.m33 = forward.z; out.m34 = 0.0f;
    out.m41 = position.x; out.m42 = position.y; out.m43 = position.z; out.m44 = 1.0f;
    return out;
  }

  void BuildOctahedralBonePoints(const XVECTOR3& root,
                                 const XVECTOR3& tip,
                                 float widthScale,
                                 float minWidth,
                                 std::array<XVECTOR3, 6>& outPoints) {
    XVECTOR3 axis = tip - root;
    float length = Length3(axis);
    if (length <= 0.0001f) {
      axis = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      length = 0.02f;
    } else {
      axis = axis / length;
    }

    const XVECTOR3 ref = std::fabs(axis.y) < 0.85f
        ? XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)
        : XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    const XVECTOR3 sideA = Normalize3(Cross3(ref, axis), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    const XVECTOR3 sideB = Normalize3(Cross3(axis, sideA), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    const XVECTOR3 center = root + axis * (length * 0.38f);
    const float width = (std::max)(minWidth, length * widthScale);

    outPoints[0] = root;
    outPoints[1] = tip;
    outPoints[2] = center + sideA * width;
    outPoints[3] = center - sideA * width;
    outPoints[4] = center + sideB * width;
    outPoints[5] = center - sideB * width;
  }

  float MatrixMaxAbsDiff(const XMATRIX44& a, const XMATRIX44& b) {
    float maxDiff = 0.0f;
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
        maxDiff = (std::max)(maxDiff, std::fabs(a.m[r][c] - b.m[r][c]));
    return maxDiff;
  }

  float MatrixTranslationDistance(const XMATRIX44& a, const XMATRIX44& b) {
    const float dx = a.m41 - b.m41;
    const float dy = a.m42 - b.m42;
    const float dz = a.m43 - b.m43;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  void WriteMatrixCsv(std::ofstream& file, const XMATRIX44& matrix) {
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        file << ',' << matrix.m[r][c];
      }
    }
  }

  const char* BoneNameOrEmpty(const xF::xSkeleton* skeleton, int boneIndex) {
    if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
      return "";
    }
    return skeleton->Bones[boneIndex].Name.c_str();
  }

  constexpr int kRagdollJointDisabled = -2;
  constexpr int kRagdollJointInheritParent = -1;
  constexpr int kRagdollSelectCapsules = 0;
  constexpr int kRagdollSelectJoints = 1;
  constexpr int kRagdollSelectBones = 2;
  constexpr int kRagdollToolSelect = 0;
  constexpr int kRagdollToolEditCapsule = 1;
  constexpr int kRagdollToolMove = 2;
  constexpr int kRagdollToolRotate = 3;

  const char* RagdollToolName(int toolMode) {
    switch (toolMode) {
      case kRagdollToolEditCapsule: return "Edit capsule";
      case kRagdollToolMove: return "Move";
      case kRagdollToolRotate: return "Rotate";
      default: return "Select";
    }
  }

  t850::PhysicsRagdollJointType RagdollJointTypeFromInt(int value) {
    return value == static_cast<int>(t850::PhysicsRagdollJointType::Fixed)
        ? t850::PhysicsRagdollJointType::Fixed
        : t850::PhysicsRagdollJointType::SwingTwist;
  }

  int RagdollJointTypeToInt(t850::PhysicsRagdollJointType type) {
    return type == t850::PhysicsRagdollJointType::Fixed ? 1 : 0;
  }

  const char* RagdollJointTypeName(t850::PhysicsRagdollJointType type) {
    return type == t850::PhysicsRagdollJointType::Fixed ? "Fixed" : "Swing/Twist";
  }

  struct SkeletonEditBoneJson {
    int index = -1;
    std::string name;
    std::array<float, 16> combined{};
  };

  struct SkeletonEditJson {
    std::string model;
    std::vector<SkeletonEditBoneJson> bones;
  };

  struct RagdollEditCapsuleJson {
    int index = -1;
    int boneIndex = -1;
    std::string name;
    int parentCapsule = -1;
    int jointParentCapsule = kRagdollJointInheritParent;
    bool capsuleFrozen = false;
    bool jointFrozen = false;
    bool hasJointContactAnchor = false;
    bool jointContactAnchor = false;
    int jointType = 0;
    bool hasJointAnchor = false;
    std::array<float, 3> jointAnchor{};
    std::array<float, 16> bodyFromBone{};
    float radius = 0.0f;
    float halfHeight = 0.0f;
    float swingLimitRadians = 0.0f;
    float twistLimitRadians = 0.0f;
    std::vector<int> controlledBones;
  };

  struct RagdollEditJson {
    int schema = 1;
    std::string model;
    std::vector<RagdollEditCapsuleJson> capsules;
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

  bool ParseSkeletonEditJson(const std::string& json, SkeletonEditJson& out) {
    out = SkeletonEditJson{};
    if (const std::size_t modelKey = json.find("\"model\""); modelKey != std::string::npos) {
      ParseJsonStringAt(json, modelKey, out.model);
    }

    std::size_t pos = json.find("\"bones\"");
    if (pos == std::string::npos) return true;
    while ((pos = json.find("\"index\"", pos)) != std::string::npos) {
      SkeletonEditBoneJson bone;
      const std::size_t colon = json.find(':', pos);
      if (colon == std::string::npos) break;
      bone.index = std::atoi(json.c_str() + colon + 1);

      const std::size_t nameKey = json.find("\"name\"", colon);
      const std::size_t combinedKey = json.find("\"combined\"", colon);
      if (nameKey == std::string::npos || combinedKey == std::string::npos) break;
      ParseJsonStringAt(json, nameKey, bone.name);
      if (!ParseFloatArray16At(json, combinedKey, bone.combined)) break;
      out.bones.push_back(std::move(bone));
      pos = combinedKey + 10;
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
    if (pos == std::string::npos) return true;
    while ((pos = json.find("\"index\"", pos)) != std::string::npos) {
      RagdollEditCapsuleJson capsule;
      if (!ParseJsonIntAt(json, pos, capsule.index)) break;

      const std::size_t boneIndexKey = json.find("\"bone_index\"", pos);
      const std::size_t nameKey = json.find("\"name\"", pos);
      const std::size_t parentKey = json.find("\"parent_capsule\"", pos);
      const std::size_t jointParentKey = json.find("\"joint_parent_capsule\"", pos);
      const std::size_t capsuleFrozenKey = json.find("\"capsule_frozen\"", pos);
      const std::size_t jointFrozenKey = json.find("\"joint_frozen\"", pos);
      const std::size_t jointContactKey = json.find("\"joint_contact_anchor\"", pos);
      const std::size_t jointTypeKey = json.find("\"joint_type\"", pos);
      const std::size_t jointAnchorKey = json.find("\"joint_anchor\"", pos);
      const std::size_t matrixKey = json.find("\"body_from_bone\"", pos);
      const std::size_t radiusKey = json.find("\"radius\"", pos);
      const std::size_t halfHeightKey = json.find("\"half_height\"", pos);
      const std::size_t swingKey = json.find("\"swing_limit\"", pos);
      const std::size_t twistKey = json.find("\"twist_limit\"", pos);
      const std::size_t controlledKey = json.find("\"controlled_bones\"", pos);
      const std::size_t objectEnd = json.find('}', pos);
      if (objectEnd == std::string::npos) {
        break;
      }
      if (boneIndexKey == std::string::npos || nameKey == std::string::npos ||
          matrixKey == std::string::npos || radiusKey == std::string::npos ||
          halfHeightKey == std::string::npos || swingKey == std::string::npos ||
          twistKey == std::string::npos) {
        break;
      }

      ParseJsonIntAt(json, boneIndexKey, capsule.boneIndex);
      ParseJsonStringAt(json, nameKey, capsule.name);
      if (parentKey != std::string::npos && parentKey < objectEnd) {
        ParseJsonIntAt(json, parentKey, capsule.parentCapsule);
      }
      if (jointParentKey != std::string::npos && jointParentKey < objectEnd) {
        ParseJsonIntAt(json, jointParentKey, capsule.jointParentCapsule);
      }
      if (capsuleFrozenKey != std::string::npos && capsuleFrozenKey < objectEnd) {
        ParseJsonBoolAt(json, capsuleFrozenKey, capsule.capsuleFrozen);
      }
      if (jointFrozenKey != std::string::npos && jointFrozenKey < objectEnd) {
        ParseJsonBoolAt(json, jointFrozenKey, capsule.jointFrozen);
      }
      if (jointContactKey != std::string::npos && jointContactKey < objectEnd) {
        capsule.hasJointContactAnchor = ParseJsonBoolAt(json, jointContactKey, capsule.jointContactAnchor);
      }
      if (jointTypeKey != std::string::npos && jointTypeKey < objectEnd) {
        ParseJsonIntAt(json, jointTypeKey, capsule.jointType);
      }
      if (jointAnchorKey != std::string::npos && jointAnchorKey < objectEnd) {
        capsule.hasJointAnchor = ParseFloatArray3At(json, jointAnchorKey, capsule.jointAnchor);
      }
      if (!ParseFloatArray16At(json, matrixKey, capsule.bodyFromBone)) break;
      if (!ParseJsonFloatAt(json, radiusKey, capsule.radius)) break;
      if (!ParseJsonFloatAt(json, halfHeightKey, capsule.halfHeight)) break;
      if (!ParseJsonFloatAt(json, swingKey, capsule.swingLimitRadians)) break;
      if (!ParseJsonFloatAt(json, twistKey, capsule.twistLimitRadians)) break;
      if (controlledKey != std::string::npos && controlledKey < objectEnd) {
        ParseIntArrayAt(json, controlledKey, capsule.controlledBones);
      }
      out.capsules.push_back(std::move(capsule));
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

  std::array<float, 3> MatrixTranslation(const XMATRIX44& matrix) {
    return {matrix.m41, matrix.m42, matrix.m43};
  }

  std::array<float, 3> MatrixEulerDegreesXYZ(const XMATRIX44& matrix) {
    const float y = std::asin((std::max)(-1.0f, (std::min)(1.0f, -matrix.m13)));
    const float cy = std::cos(y);
    float x = 0.0f;
    float z = 0.0f;
    if (std::fabs(cy) > 0.00001f) {
      x = std::atan2(matrix.m23, matrix.m33);
      z = std::atan2(matrix.m12, matrix.m11);
    } else {
      x = std::atan2(-matrix.m32, matrix.m22);
    }
    return {Rad2Deg(x), Rad2Deg(y), Rad2Deg(z)};
  }

  XMATRIX44 MatrixFromTranslationEulerDegreesXYZ(const std::array<float, 3>& translation,
                                                 const std::array<float, 3>& eulerDegrees) {
    XMATRIX44 rx, ry, rz;
    XMatIdentity(rx);
    XMatIdentity(ry);
    XMatIdentity(rz);
    XMatRotationX(rx, Deg2Rad(eulerDegrees[0]));
    XMatRotationY(ry, Deg2Rad(eulerDegrees[1]));
    XMatRotationZ(rz, Deg2Rad(eulerDegrees[2]));
    XMATRIX44 matrix = rx * ry * rz;
    matrix.m41 = translation[0];
    matrix.m42 = translation[1];
    matrix.m43 = translation[2];
    matrix.m44 = 1.0f;
    return matrix;
  }

  const char* RagdollCapsuleHandleName(int handleIndex) {
    switch (handleIndex) {
    case 0: return "center";
    case 1: return "top length";
    case 2: return "bottom length";
    case 3: return "+X radius";
    case 4: return "-X radius";
    case 5: return "+Z radius";
    case 6: return "-Z radius";
    default: return "none";
    }
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

  ImVec2 ProjectWorldToScreen(const XVECTOR3& point, const XMATRIX44& vp, int width, int height, bool& visible) {
    const float cx = point.x * vp.m11 + point.y * vp.m21 + point.z * vp.m31 + vp.m41;
    const float cy = point.x * vp.m12 + point.y * vp.m22 + point.z * vp.m32 + vp.m42;
    const float cw = point.x * vp.m14 + point.y * vp.m24 + point.z * vp.m34 + vp.m44;
    visible = std::fabs(cw) > 0.000001f;
    if (!visible) return ImVec2(-1.0f, -1.0f);
    const float ndcX = cx / cw;
    const float ndcY = cy / cw;
    visible = ndcX >= -1.15f && ndcX <= 1.15f && ndcY >= -1.15f && ndcY <= 1.15f;
    return ImVec2((ndcX * 0.5f + 0.5f) * width,
                  (1.0f - (ndcY * 0.5f + 0.5f)) * height);
  }

  float DistancePointToSegmentSq(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    const float abX = b.x - a.x;
    const float abY = b.y - a.y;
    const float abLenSq = abX * abX + abY * abY;
    float t = 0.0f;
    if (abLenSq > 0.000001f) {
      t = ((p.x - a.x) * abX + (p.y - a.y) * abY) / abLenSq;
      t = (std::max)(0.0f, (std::min)(1.0f, t));
    }
    const float closestX = a.x + abX * t;
    const float closestY = a.y + abY * t;
    const float dx = p.x - closestX;
    const float dy = p.y - closestY;
    return dx * dx + dy * dy;
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
    const float a = Dot3(d1, d1);
    const float e = Dot3(d2, d2);
    const float f = Dot3(d2, r);

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
      const float c = Dot3(d1, r);
      if (e <= kEpsilon) {
        s = Clamp01(-c / a);
      } else {
        const float b = Dot3(d1, d2);
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

  bool RayPlaneIntersection(const t850::Ray& ray,
                            const XVECTOR3& planePoint,
                            const XVECTOR3& planeNormal,
                            XVECTOR3& outPoint) {
    const float denom = Dot3(ray.direction, planeNormal);
    if (std::fabs(denom) < 0.000001f) {
      return false;
    }
    const float t = Dot3(planePoint - ray.origin, planeNormal) / denom;
    if (t < 0.0f) {
      return false;
    }
    outPoint = ray.origin + ray.direction * t;
    outPoint.w = 1.0f;
    return true;
  }

  bool ClosestRayAxisParameter(const t850::Ray& ray,
                               const XVECTOR3& axisOrigin,
                               const XVECTOR3& axisDirection,
                               float& outParameter) {
    const XVECTOR3 axis = Normalize3(axisDirection, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    const XVECTOR3 rayDirection = Normalize3(ray.direction, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    const XVECTOR3 w0 = axisOrigin - ray.origin;
    const float a = Dot3(axis, axis);
    const float b = Dot3(axis, rayDirection);
    const float c = Dot3(rayDirection, rayDirection);
    const float d = Dot3(axis, w0);
    const float e = Dot3(rayDirection, w0);
    const float denom = a * c - b * b;
    if (std::fabs(denom) < 0.000001f) {
      return false;
    }
    outParameter = (b * e - c * d) / denom;
    return true;
  }

  ImU32 RagdollGizmoAxisColor(int axis, bool active) {
    if (active) return IM_COL32(255, 255, 255, 255);
    switch (axis) {
    case 0: return IM_COL32(245, 70, 70, 235);
    case 1: return IM_COL32(70, 230, 90, 235);
    case 2: return IM_COL32(80, 130, 255, 235);
    default: return IM_COL32(255, 255, 255, 235);
    }
  }

  bool DumpRagdollF5MatrixComparison(const RenderSkinnedMesh& skinned,
                                     const t850::PhysicsRagdollDesc& expectedPose,
                                     const std::vector<t850::PhysicsBodyState>& physicsStates,
                                     const std::vector<int>& physicsBoneIndices,
                                     const std::vector<XMATRIX44>& physicsCombinedMatrices,
                                     const std::vector<XMATRIX44>& animationShaderMatrices,
                                     const std::vector<XMATRIX44>& physicsShaderMatrices) {
    std::error_code ec;
    std::filesystem::create_directories("Logs", ec);
    if (ec) {
      T8_LOG_ERROR("[SandboxScene] Failed to create Logs directory for ragdoll matrix dump");
      return false;
    }

    const xF::xSkeleton* skeleton = skinned.GetAnimController().GetAnimSkeleton();
    const std::filesystem::path shaderPath = std::filesystem::path("Logs") / "ragdoll_f5_shader_compare.csv";
    const std::filesystem::path combinedPath = std::filesystem::path("Logs") / "ragdoll_f5_combined_overrides.csv";
    const std::filesystem::path bodyPath = std::filesystem::path("Logs") / "ragdoll_f5_physics_bodies.csv";

    std::ofstream shaderFile(shaderPath, std::ios::out | std::ios::trunc);
    if (!shaderFile.is_open()) {
      T8_LOG_ERROR("[SandboxScene] Failed to open ragdoll shader matrix dump '%s'", shaderPath.string().c_str());
      return false;
    }
    shaderFile << std::fixed << std::setprecision(8);
    shaderFile << "bone_index,bone_name,has_physics_override,max_abs_diff,translation_diff";
    for (int i = 0; i < 16; ++i) shaderFile << ",anim_m" << i;
    for (int i = 0; i < 16; ++i) shaderFile << ",phys_m" << i;
    shaderFile << '\n';

    float maxShaderDiff = 0.0f;
    float maxShaderTranslationDiff = 0.0f;
    int maxShaderBone = -1;
    const std::size_t shaderCount = (std::min)(animationShaderMatrices.size(), physicsShaderMatrices.size());
    for (std::size_t boneIndex = 0; boneIndex < shaderCount; ++boneIndex) {
      const bool hasOverride = std::find(physicsBoneIndices.begin(), physicsBoneIndices.end(), static_cast<int>(boneIndex)) != physicsBoneIndices.end();
      const float maxDiff = MatrixMaxAbsDiff(animationShaderMatrices[boneIndex], physicsShaderMatrices[boneIndex]);
      const float translationDiff = MatrixTranslationDistance(animationShaderMatrices[boneIndex], physicsShaderMatrices[boneIndex]);
      if (maxDiff > maxShaderDiff) {
        maxShaderDiff = maxDiff;
        maxShaderTranslationDiff = translationDiff;
        maxShaderBone = static_cast<int>(boneIndex);
      }
      shaderFile << boneIndex << ",\"" << BoneNameOrEmpty(skeleton, static_cast<int>(boneIndex)) << "\","
                 << (hasOverride ? 1 : 0) << ',' << maxDiff << ',' << translationDiff;
      WriteMatrixCsv(shaderFile, animationShaderMatrices[boneIndex]);
      WriteMatrixCsv(shaderFile, physicsShaderMatrices[boneIndex]);
      shaderFile << '\n';
    }

    std::ofstream combinedFile(combinedPath, std::ios::out | std::ios::trunc);
    if (!combinedFile.is_open()) {
      T8_LOG_ERROR("[SandboxScene] Failed to open ragdoll combined matrix dump '%s'", combinedPath.string().c_str());
      return false;
    }
    combinedFile << std::fixed << std::setprecision(8);
    combinedFile << "bone_index,bone_name";
    for (int i = 0; i < 16; ++i) combinedFile << ",combined_m" << i;
    combinedFile << '\n';
    for (std::size_t i = 0; i < physicsBoneIndices.size() && i < physicsCombinedMatrices.size(); ++i) {
      const int boneIndex = physicsBoneIndices[i];
      combinedFile << boneIndex << ",\"" << BoneNameOrEmpty(skeleton, boneIndex) << "\"";
      WriteMatrixCsv(combinedFile, physicsCombinedMatrices[i]);
      combinedFile << '\n';
    }

    std::ofstream bodyFile(bodyPath, std::ios::out | std::ios::trunc);
    if (!bodyFile.is_open()) {
      T8_LOG_ERROR("[SandboxScene] Failed to open ragdoll body matrix dump '%s'", bodyPath.string().c_str());
      return false;
    }
    bodyFile << std::fixed << std::setprecision(8);
    bodyFile << "bone_index,bone_name,body_max_abs_diff,body_translation_diff";
    for (int i = 0; i < 16; ++i) bodyFile << ",expected_body_world_m" << i;
    for (int i = 0; i < 16; ++i) bodyFile << ",actual_body_world_m" << i;
    bodyFile << ",linear_vx,linear_vy,linear_vz,angular_vx,angular_vy,angular_vz\n";

    float maxBodyDiff = 0.0f;
    float maxBodyTranslationDiff = 0.0f;
    int maxBodyBone = -1;
    for (const t850::PhysicsBodyState& state : physicsStates) {
      const XMATRIX44* expectedBodyWorld = nullptr;
      for (const t850::PhysicsRagdollBoneDesc& bone : expectedPose.bones) {
        if (bone.body.boneIndex == state.boneIndex) {
          expectedBodyWorld = &bone.body.worldTransform;
          break;
        }
      }
      const float bodyDiff = expectedBodyWorld ? MatrixMaxAbsDiff(*expectedBodyWorld, state.worldTransform) : 0.0f;
      const float bodyTranslationDiff = expectedBodyWorld ? MatrixTranslationDistance(*expectedBodyWorld, state.worldTransform) : 0.0f;
      if (bodyDiff > maxBodyDiff) {
        maxBodyDiff = bodyDiff;
        maxBodyTranslationDiff = bodyTranslationDiff;
        maxBodyBone = state.boneIndex;
      }

      bodyFile << state.boneIndex << ",\"" << BoneNameOrEmpty(skeleton, state.boneIndex) << "\","
               << bodyDiff << ',' << bodyTranslationDiff;
      if (expectedBodyWorld) {
        WriteMatrixCsv(bodyFile, *expectedBodyWorld);
      } else {
        XMATRIX44 identity;
        identity.Identity();
        WriteMatrixCsv(bodyFile, identity);
      }
      WriteMatrixCsv(bodyFile, state.worldTransform);
      bodyFile << ',' << state.linearVelocity.x << ',' << state.linearVelocity.y << ',' << state.linearVelocity.z
               << ',' << state.angularVelocity.x << ',' << state.angularVelocity.y << ',' << state.angularVelocity.z
               << '\n';
    }

    T8_LOG_INFO("[SandboxScene] F5 ragdoll matrix dump: shader='%s' combined='%s' bodies='%s' maxShaderDiff=%.6f bone=%d('%s') transDiff=%.6f maxBodyDiff=%.6f bone=%d('%s') bodyTransDiff=%.6f",
                shaderPath.string().c_str(),
                combinedPath.string().c_str(),
                bodyPath.string().c_str(),
                maxShaderDiff,
                maxShaderBone,
                BoneNameOrEmpty(skeleton, maxShaderBone),
                maxShaderTranslationDiff,
                maxBodyDiff,
                maxBodyBone,
                BoneNameOrEmpty(skeleton, maxBodyBone),
                maxBodyTranslationDiff);
    return true;
  }

  bool BuildWorldBounds(RenderMesh* mesh, const XMATRIX44& worldFromMesh, RenderMesh::AABB& outBounds) {
    if (!mesh) {
      return false;
    }

    outBounds.Reset();
    bool expanded = false;
    for (const RenderMesh::MeshInfo& meshInfo : mesh->Info) {
      const RenderMesh::AABB& bounds = meshInfo.bounds;
      if (!IsUsableRenderBounds(bounds)) {
        continue;
      }

      const float xs[2] = { bounds.min.x, bounds.max.x };
      const float ys[2] = { bounds.min.y, bounds.max.y };
      const float zs[2] = { bounds.min.z, bounds.max.z };
      for (float x : xs) {
        for (float y : ys) {
          for (float z : zs) {
            const XVECTOR3 point = TransformPoint(XVECTOR3(x, y, z, 1.0f), worldFromMesh);
            if (!IsUsablePhysicsPoint(point)) {
              continue;
            }
            outBounds.Expand(point.x, point.y, point.z);
            expanded = true;
          }
        }
      }
    }
    return expanded && IsUsableRenderBounds(outBounds);
  }

  void ExpandBounds(RenderMesh::AABB& bounds, const RenderMesh::AABB& other) {
    if (!IsUsableRenderBounds(other)) {
      return;
    }
    bounds.Expand(other.min.x, other.min.y, other.min.z);
    bounds.Expand(other.max.x, other.max.y, other.max.z);
  }

  bool BuildRagdollCapsuleBounds(const t850::PhysicsRagdollDesc& pose, RenderMesh::AABB& outBounds) {
    outBounds.Reset();
    bool expanded = false;
    for (const t850::PhysicsRagdollBoneDesc& bone : pose.bones) {
      if (bone.body.shape.type != t850::PhysicsShapeType::Capsule ||
          !IsUsablePhysicsPoint(XVECTOR3(
              bone.body.worldTransform.m41,
              bone.body.worldTransform.m42,
              bone.body.worldTransform.m43,
              1.0f))) {
        continue;
      }

      XVECTOR3 axisY(
          bone.body.worldTransform.m21,
          bone.body.worldTransform.m22,
          bone.body.worldTransform.m23,
          0.0f);
      axisY = Normalize3(axisY, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
      const float radius = (std::max)(0.0f, bone.body.shape.radius);
      const float halfHeight = (std::max)(0.0f, bone.body.shape.halfHeight);
      const float extentX = radius + std::fabs(axisY.x) * halfHeight;
      const float extentY = radius + std::fabs(axisY.y) * halfHeight;
      const float extentZ = radius + std::fabs(axisY.z) * halfHeight;
      const XVECTOR3 center(
          bone.body.worldTransform.m41,
          bone.body.worldTransform.m42,
          bone.body.worldTransform.m43,
          1.0f);
      outBounds.Expand(center.x - extentX, center.y - extentY, center.z - extentZ);
      outBounds.Expand(center.x + extentX, center.y + extentY, center.z + extentZ);
      expanded = true;
    }
    return expanded && IsUsableRenderBounds(outBounds);
  }

  const t850::SelectorDesc* FindSelectorDesc(const std::vector<t850::SelectorDesc>& selectors, const std::string& name) {
    for (const auto& selector : selectors)
      if (selector.name == name) return &selector;
    return nullptr;
  }
}

void SandboxScene::InitVars() {



  // Free camera
  Cam.InitPerspective(XVECTOR3(0.0f, 1.0f, 10.0f), Deg2Rad(46.8f), 1280.0f / 720.0f, 0.1f, 5000.0f);
  Cam.Speed = 10.0f;
  Cam.Eye = XVECTOR3(0.0f, 5.0f, -15.0f);
  Cam.Pitch = 0.2f;
  Cam.Roll = 0.0f;
  Cam.Yaw = 0.0f;
  Cam.m_externalControl = false;
  Cam.Update(0.0f);

  // Initialize orbit camera defaults
  m_orbitTarget = XVECTOR3(0, 0, 0);
  m_panOffset   = XVECTOR3(0, 0, 0);
  m_orbitYaw    = 0.0f;
  m_orbitPitch  = 0.0f;
  m_orbitDist   = 5.0f;
  m_modelRadius = 1.0f;

  LightCam.InitPerspective(XVECTOR3(0.0f, 100.0f, 10.0f), Deg2Rad(45.0f), 1.0f, 10.0f, 500.0f);
  LightCam.Speed = 10.0f;
  LightCam.Eye = XVECTOR3(50.0f, 150.0f, -50.0f);
  LightCam.Pitch = 1.0f;
  LightCam.Roll = 0.0f;
  LightCam.Yaw = -1.57f;
  LightCam.Update(0.0f);

  ActiveCam = &Cam;

  SceneProp.AddCamera(ActiveCam);
  SceneProp.AddLightCamera(&LightCam);

  SceneProp.AddDirectionalLight(XVECTOR3(-0.2f, -1.0f, 0.1f), XVECTOR3(1, 1, 1), 5.0f, true);
  SceneProp.AddLight(XVECTOR3(10.0f, 10.0f, -10.0f), XVECTOR3(1.0, 0.9, 0.8), 100.0f, 1.0f, LIGHT_POINT, true);
  SceneProp.ActiveLights = 2;
  SceneProp.AmbientColor = XVECTOR3(0.3f, 0.3f, 0.3f);
  EnsureLightRuntimeState();
  if (!SceneProp.Lights.empty() && SceneProp.Lights[0].Type == LIGHT_DIRECTIONAL) {
    SceneProp.Lights[0].Position = LightCam.Eye;
    SceneProp.Lights[0].Direction = LightCam.Look;
  }

  ShadowFilter.kernelSize = 4;
  ShadowFilter.radius = 1.f;
  ShadowFilter.sigma = 1.0f;
  ShadowFilter.Update();

  BloomFilter.kernelSize = 11;
  BloomFilter.radius = 2.5f;
  BloomFilter.sigma = 4.5f;
  BloomFilter.Update();

  NearDOFFilter.kernelSize = 23;
  NearDOFFilter.radius = 3.0f;
  NearDOFFilter.sigma = 6.f;
  NearDOFFilter.Update();

  SceneProp.AddGaussKernel(&ShadowFilter);
  SceneProp.AddGaussKernel(&BloomFilter);
  SceneProp.AddGaussKernel(&NearDOFFilter);
  SceneProp.ActiveGaussKernel = 0;

  SceneProp.ShadowMapResolution = 1024.0f;
  SceneProp.PCFScale = 1.7f;
  SceneProp.PCFSamples = 1.0f;
  SceneProp.SSAOKernel.Radius = 1.5f;
  SceneProp.SSAOKernel.KernelSize = 8;
  SceneProp.SSAOKernel.Update();

  SceneProp.ToogleShadow = true;
  SceneProp.ToogleSSAO = true;
  m_showWireframe = false;
  m_showSkeleton = false;
  m_showPhysics = false;
  m_drawLightDirection = false;
  m_driveRagdollFromAnimation = false;
  m_ragdollPhysicsDriven = false;
  m_ragdollDriveLogEmitted = false;
  m_ragdollPhysicsLogEmitted = false;
  m_ragdollEditDirty = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditSelectedCapsule = -1;
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditSelectedHandle = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditSavePath.clear();
  m_skeletonEditMode = false;
  m_skeletonEditWasPlaying = false;
  m_skeletonEditDragging = false;
  m_skeletonEditDirty = false;
  m_skeletonEditSelectedBone = -1;
  m_skeletonEditBindCombined.clear();
  m_skeletonEditCombined.clear();
  m_skeletonEditSavePath.clear();
  m_ragdollAnimationBinding = t850::PhysicsRagdollAnimationBinding{};
  m_ragdollAnimationPose = t850::PhysicsRagdollDesc{};
  m_ragdollParentCapsules.clear();
  m_ragdollJointParentCapsules.clear();
  m_ragdollPhysicsStates.clear();
  m_ragdollPhysicsBoneIndices.clear();
  m_ragdollPhysicsCombinedMatrices.clear();
  m_floorBody.Reset();
  m_ragdollGeneratedBinding = t850::PhysicsRagdollAnimationBinding{};

  SceneProp.Exposure = 1.0f;
  SceneProp.BloomFactor = 0.35f;
  SceneProp.BloomThreshold = 1.5f;
  SceneProp.ToneMapWhiteLevel = 5.5f;
  SceneProp.LuminanceTau = 1.1f;
  SceneProp.IBLMipCount = 4.0f;
  SceneProp.IBLBRDFLUTEnabled = 0.0f;

  if (m_guiSetup.Load("Scenes/SandboxScene.json")) {
    m_guiSetup.ApplyQualityAndSettings(SceneProp);
  } else {
    T8_LOG_ERROR("[SandboxScene] Failed to load Scenes/SandboxScene.json");
  }
  SceneProp.FrustumCullingToggleAllowed = g_config.cullingLoadMode != t850::Config::CullingLoadMode::Disabled;
  SceneProp.FrustumCullingEnabled = g_config.cullingLoadMode == t850::Config::CullingLoadMode::FullOnLoad;

  t850::FrameDumperConfig dumpCfg;
  dumpCfg.dumpEnabled        = g_config.flags.dumpEnabled;
  dumpCfg.dumpByFrame        = g_config.flags.dumpByFrame;
  dumpCfg.dumpFrame          = g_config.dumpFrame;
  dumpCfg.dumpSeconds        = g_config.dumpSeconds;
  dumpCfg.debugFrames        = g_config.flags.debugFrames;
  dumpCfg.keepRunning        = g_config.flags.keepRunning;
  dumpCfg.replaySnapshotPath = g_config.replaySnapshotPath;
  dumpCfg.sceneIndex         = g_config.startScene;
  m_dumper.Init(dumpCfg);
}

void SandboxScene::CreateAssets() {
  if (!m_renderGraph.Load("Scenes/SandboxScene_RenderGraph.json")) {
    T8_LOG_ERROR("[SandboxScene] Failed to load render graph");
    return;
  }
  m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp);

  GBufferPass           = m_renderGraph.GetRTHandle("GBuffer");
  DeferredPass          = m_renderGraph.GetRTHandle("Deferred");
  Extra16FPass          = m_renderGraph.GetRTHandle("Extra16F");
  DepthPass             = m_renderGraph.GetRTHandle("DepthPass");
  ShadowAccumPass       = m_renderGraph.GetRTHandle("ShadowAccum");
  ExtraHelperPass       = m_renderGraph.GetRTHandle("ExtraHelper");
  BloomAccumPass        = m_renderGraph.GetRTHandle("BloomAccum");
  LuminanceMapPass      = m_renderGraph.GetRTHandle("LuminanceMap");
  AdaptedLumCurrentPass = m_renderGraph.GetRTHandle("AdaptedLumCurrent");
  AdaptedLumPrevPass    = m_renderGraph.GetRTHandle("AdaptedLumPrev");

  PrimitiveMgr.SetEngineContext(pEngineContext);
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);

  SceneProp.SSAOKernel.InitTexture();

  EnvMapTexIndex = g_pBaseDriver->CreateTexture(string("sky/Ennis.dds"));
  EnvMaps.SetFallback(EnvMapTexIndex);
  if (m_guiSetup.descriptor.name.empty()) {
    m_guiSetup.Load("Scenes/SandboxScene.json");
  }
  LoadEnvironmentIBLResources(
    g_pBaseDriver,
    {m_guiSetup.environmentDiffuseIBL, m_guiSetup.environmentSpecularIBL, m_guiSetup.environmentBrdfLUT,
     m_guiSetup.environmentSheenIBL, m_guiSetup.environmentCharlieLUT, m_guiSetup.environmentSheenELUT},
    EnvMaps,
    DiffuseIBLTexIndex,
    SpecularIBLTexIndex,
    BrdfLUTTexIndex,
    SheenIBLTexIndex,
    CharlieLUTTexIndex,
    SheenELUTTexIndex);
  UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);

  // Load the glTF model
  int index = PrimitiveMgr.CreateMesh(g_config.modelPath.c_str());
  if (index < 0) {
    T8_LOG_ERROR("[SandboxScene] Failed to load '%s'", g_config.modelPath.c_str());
  } else {
    T8_LOG_INFO("[SandboxScene] Loaded model '%s', primitive index=%d", g_config.modelPath.c_str(), index);
    Meshes[0].CreateInstance(PrimitiveMgr.GetPrimitive(index), &VP);
    FitModelToView();
    LoadSandboxProfile();
  }

  // No SkyBox mesh needed — cleared GBuffer pixels (MatId=0) sample
  // the environment cubemap directly in the deferred pass using the
  // interpolated view ray (PosCorner). This avoids cull-face issues
  // and works across all APIs.

  // Fullscreen quad setup
  m.Identity();
  Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[0], 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[1], 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[2], 2);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[3], 3);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->pDepthTexture, 4);
  Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));

  for (int i = 1; i <= 7; i++)
    Quads[i].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);

  PrimitiveMgr.SetSceneProps(&SceneProp);

  Quads[0].TranslateAbsolute(0.0f, 0.0f, 0.0f);
  Quads[0].Update();

  // Debug visualization
  m_debugText.LoadFromFile(24, "Fonts/Martius-LV9L4.ttf", 512.0f);
  m_debugSphere.Create(6, 12);
  m_lightArrowRenderer.Create();
  m_ragdollJointRenderer.Create();
  m_physicsDebugRenderer.Create();
  float arrowVerts[10 * 4] = {};
  unsigned short arrowIndices[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  m_lightArrowVB = t850::LineRenderer::CreatePositionVB(arrowVerts, 10, BufferUsage::DINAMIC);
  m_lightArrowIB = t850::LineRenderer::CreateIndexBuffer16(arrowIndices, 10);
  m_lightArrowIndexCount = 10;
  m_ragdollJointVertexCapacity = 0;
  m_ragdollJointIndexCapacity = 0;
  m_ragdollJointIndexCount = 0;

  if (Meshes[0].pBase && !Meshes[0].HasPhysicsBody() && !Meshes[0].HasPhysicsRagdoll()) {
    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (engineContext && engineContext->physics && engineContext->physics->IsInitialized()) {
      bool attachedPhysics = false;
      RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
      if (skinned) {
        skinned->UpdateAnimationPose();
        t850::PhysicsRagdollBuildSettings settings;
        settings.fitToSkinnedGeometry = false;
        settings.preferHumanoidBones = false;
        settings.forceCapsuleForEveryBone = true;
        settings.minBoneLength = (std::max)(0.0002f, m_modelRadius * 0.0002f);
        settings.syntheticBoneLength = (std::max)(0.001f, m_modelRadius * 0.001f);
        settings.minRadius = (std::max)(0.0006f, m_modelRadius * 0.0008f);
        settings.maxRadius = (std::max)(0.02f, m_modelRadius * 0.035f);
        settings.radiusScale = 0.12f;
        settings.minSkinWeight = 0.08f;
        settings.radiusPercentile = 0.86f;
        settings.jointTrimFraction = 0.0f;
        t850::PhysicsRagdollDesc ragdollDesc;
        attachedPhysics = t850::AttachSkeletonRagdoll(
            *engineContext->physics,
            Meshes[0],
            *skinned,
            settings,
            t850::PhysicsBodyMotion::Kinematic,
            &ragdollDesc);
        if (attachedPhysics) {
          m_driveRagdollFromAnimation = t850::BuildRagdollAnimationBinding(
              *skinned,
              Meshes[0].Final,
              ragdollDesc,
              m_ragdollAnimationBinding);
          m_ragdollPhysicsDriven = false;
          m_ragdollPhysicsLogEmitted = false;
          m_ragdollDriveLogEmitted = false;
          if (m_driveRagdollFromAnimation) {
            m_ragdollGeneratedBinding = m_ragdollAnimationBinding;
            m_ragdollEditSavePath = BuildRagdollEditSavePath();
            m_ragdollEditSelectedCapsule = m_ragdollAnimationBinding.referencePose.bones.empty() ? -1 : 0;
            m_ragdollEditSelectedHandle = 0;
            LoadRagdollEditPose();
          }
          T8_LOG_INFO("[SandboxScene] Attached full-skeleton ragdoll physics for '%s'", g_config.modelPath.c_str());
          if (!m_driveRagdollFromAnimation) {
            T8_LOG_ERROR("[SandboxScene] Failed to bind full-skeleton ragdoll to animation pose for '%s'", g_config.modelPath.c_str());
          }
          CreatePhysicsFloor(*engineContext->physics);
        } else {
          T8_LOG_ERROR("[SandboxScene] Failed to attach full-skeleton ragdoll physics for '%s'", g_config.modelPath.c_str());
        }
      }

      if (!attachedPhysics && !skinned) {
        CreatePhysicsFloor(*engineContext->physics);
        RenderMesh* mesh = static_cast<RenderMesh*>(Meshes[0].pBase);
        attachedPhysics = t850::AttachMeshBoxBody(*engineContext->physics, Meshes[0], *mesh, t850::PhysicsBodyMotion::Static);
        if (attachedPhysics) {
          T8_LOG_INFO("[SandboxScene] Attached static mesh-box physics for '%s'", g_config.modelPath.c_str());
        }
      }
    }
  }
}

void SandboxScene::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void SandboxScene::OnDestoryScene() {
  DestroyAssets();
}

void SandboxScene::DestroyAssets() {
  if (Meshes[0].HasPhysicsRagdoll() || Meshes[0].HasPhysicsBody() || m_floorBody.IsValid()) {
    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (engineContext && engineContext->physics) {
      if (Meshes[0].HasPhysicsRagdoll()) {
        engineContext->physics->DestroyRagdoll(Meshes[0].GetPhysicsRagdoll());
      }
      if (Meshes[0].HasPhysicsBody()) {
        engineContext->physics->DestroyBody(Meshes[0].GetPhysicsBody());
      }
      if (m_floorBody.IsValid()) {
        engineContext->physics->DestroyBody(m_floorBody);
      }
    }
    Meshes[0].AttachPhysicsRagdoll(t850::PhysicsRagdollHandle{});
    m_floorBody.Reset();
  }
  m_driveRagdollFromAnimation = false;
  m_ragdollPhysicsDriven = false;
  m_ragdollDriveLogEmitted = false;
  m_ragdollPhysicsLogEmitted = false;
  m_ragdollEditDirty = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditSelectedCapsule = -1;
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditSelectedHandle = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditSavePath.clear();
  m_skeletonEditMode = false;
  m_skeletonEditWasPlaying = false;
  m_skeletonEditDragging = false;
  m_skeletonEditDirty = false;
  m_skeletonEditSelectedBone = -1;
  m_skeletonEditBindCombined.clear();
  m_skeletonEditCombined.clear();
  m_skeletonEditSavePath.clear();
  m_ragdollAnimationBinding = t850::PhysicsRagdollAnimationBinding{};
  m_ragdollAnimationPose = t850::PhysicsRagdollDesc{};
  m_ragdollParentCapsules.clear();
  m_ragdollJointParentCapsules.clear();
  m_ragdollPhysicsStates.clear();
  m_ragdollPhysicsBoneIndices.clear();
  m_ragdollPhysicsCombinedMatrices.clear();
  m_ragdollGeneratedBinding = t850::PhysicsRagdollAnimationBinding{};
  m_debugText.Destroy();
  if (m_lightArrowVB) m_lightArrowVB->release();
  if (m_lightArrowIB) m_lightArrowIB->release();
  m_lightArrowVB = nullptr;
  m_lightArrowIB = nullptr;
  m_lightArrowIndexCount = 0;
  ReleaseRagdollJointDebugBuffers();
  m_lightArrowRenderer.Destroy();
  m_ragdollJointRenderer.Destroy();
  m_physicsDebugRenderer.Destroy();
  PrimitiveMgr.DestroyPrimitives();
  pFramework->pVideoDriver->DestroyRTs();
}

void SandboxScene::OnUpdate(float _DtSecs) {
  DtSecs = _DtSecs;
  SceneProp.FrameDeltaSec = DtSecs;

  if (m_ragdollClearRequested) {
    ClearRagdollCapsules();
  }
  if (m_ragdollEditRebuildRequested && !m_ragdollClearRequested) {
    m_ragdollEditRebuildRequested = false;
    ApplyRagdollEditPose(true);
  }

  // Apply deferred cubemap change BEFORE any rendering begins.
  // D3D12 texture upload submits a temp command list + fence wait, which
  // conflicts with the main command list if done mid-frame.
  if (!m_pendingCubemap.empty()) {
    T8_LOG_INFO("[SandboxScene] Loading cubemap '%s' (old slot=%d)",
                m_pendingCubemap.c_str(), EnvMapTexIndex);
    // Flush GPU before destroying — D3D12 may still reference the old
    // texture from the previous frame's command list.
    g_pBaseDriver->WaitForGPU();
    int newEnvMapTexIndex = g_pBaseDriver->CreateTexture(m_pendingCubemap);
    if (newEnvMapTexIndex >= 0) {
      if (EnvMapTexIndex >= 0 && EnvMapTexIndex != newEnvMapTexIndex)
        g_pBaseDriver->DestroyTexture(EnvMapTexIndex);
      EnvMapTexIndex = newEnvMapTexIndex;
      if (m_guiSetup.environmentDiffuseIBL.empty() && DiffuseIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(DiffuseIBLTexIndex);
        DiffuseIBLTexIndex = -1;
      }
      if (m_guiSetup.environmentSpecularIBL.empty() && SpecularIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(SpecularIBLTexIndex);
        SpecularIBLTexIndex = -1;
      }
      if (m_guiSetup.environmentSheenIBL.empty() && SheenIBLTexIndex >= 0) {
        g_pBaseDriver->DestroyTexture(SheenIBLTexIndex);
        SheenIBLTexIndex = -1;
      }
      EnvMaps.SetFallback(EnvMapTexIndex);
      LoadEnvironmentIBLResources(
        g_pBaseDriver,
        {m_guiSetup.environmentDiffuseIBL, m_guiSetup.environmentSpecularIBL, m_guiSetup.environmentBrdfLUT,
         m_guiSetup.environmentSheenIBL, m_guiSetup.environmentCharlieLUT, m_guiSetup.environmentSheenELUT},
        EnvMaps,
        DiffuseIBLTexIndex,
        SpecularIBLTexIndex,
        BrdfLUTTexIndex,
        SheenIBLTexIndex,
        CharlieLUTTexIndex,
        SheenELUTTexIndex);
      EnvMaps.BrdfLUT = BrdfLUTTexIndex;
      EnvMaps.CharlieIBL = SheenIBLTexIndex;
      EnvMaps.CharlieLUT = CharlieLUTTexIndex;
      EnvMaps.SheenELUT = SheenELUTTexIndex;
      UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);
      Texture* newTex = g_pBaseDriver->GetTexture(EnvMapTexIndex);
      T8_LOG_INFO("[SandboxScene] Cubemap loaded: slot=%d tex=%p (%dx%d)",
                  EnvMapTexIndex, newTex, newTex ? newTex->x : 0, newTex ? newTex->y : 0);
      Quads[0].SetEnvironmentMap(newTex);
      if (Meshes[0].pBase) {
        Meshes[0].SetEnvironmentMap(newTex);
      }
    } else {
      T8_LOG_ERROR("[SandboxScene] Failed to load cubemap '%s'; keeping previous cubemap", m_pendingCubemap.c_str());
    }
    m_pendingCubemap.clear();
  }

  // Replay snapshot: load and apply (one-time)
  if (m_dumper.HasPendingReplay()) {
    if (m_dumper.LoadReplaySnapshot()) {
      m_dumper.ApplySnapshot(Cam, LightCam, SceneProp);
      if (const t850::SnapshotSkinnedJson* skinnedSnap = m_dumper.GetReplaySkinnedState()) {
        if (Meshes[0].pBase) {
          RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[0].pBase);
          ApplySkinnedSnapshot(skinned, *skinnedSnap, m_showWireframe, m_showSkeleton);
        }
      }
      VP = Cam.VP;
    }
  }
  m_dumper.UpdateReplayState();

  if (!m_dumper.SkipCameraUpdates()) {
    ComputeOrbitCamera();
    VP = Cam.VP;
    UpdateAttachedLights();
    SyncLightCameraFromDirectionalLight();
  }

  // --dumpMatrices: log all camera matrices per frame, then exit
  if (g_config.flags.dumpMatrices) {
    static int s_matDumpFrame = 0;
    static std::ofstream s_matFile;
    if (s_matDumpFrame == 0) {
      s_matFile.open("matrix_dump.csv", std::ios::out | std::ios::trunc);
      s_matFile << "frame,";
      s_matFile << "cam_eye_x,cam_eye_y,cam_eye_z,";
      s_matFile << "cam_pitch,cam_roll,cam_yaw,";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "camView_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "camProj_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "camVP_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "lightView_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "lightProj_" << r << c << ",";
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << "lightVP_" << r << c << (r == 3 && c == 3 ? "" : ",");
      s_matFile << "\n";
    }
    s_matFile << s_matDumpFrame << ",";
    s_matFile << Cam.Eye.x << "," << Cam.Eye.y << "," << Cam.Eye.z << ",";
    s_matFile << Cam.Pitch << "," << Cam.Roll << "," << Cam.Yaw << ",";
    auto writeM = [&](const XMATRIX44& M) {
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          s_matFile << M.m[r][c] << ",";
    };
    writeM(Cam.View);
    writeM(Cam.Projection);
    writeM(Cam.VP);
    writeM(LightCam.View);
    writeM(LightCam.Projection);
    auto& LVP = LightCam.VP;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        s_matFile << LVP.m[r][c] << (r == 3 && c == 3 ? "" : ",");
    s_matFile << "\n";
    s_matFile.flush();
    s_matDumpFrame++;
    if (s_matDumpFrame >= g_config.dumpMatricesFrames) {
      s_matFile.close();
      T8_LOG_INFO("[dumpMatrices] Wrote %d frames to matrix_dump.csv", s_matDumpFrame);
      exit(0);
    }
  }

  if (Meshes[0].pBase) {
    RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
    if (skinned && skinned->HasSkinData() && !m_ragdollPhysicsDriven) {
      skinned->UpdateAnimationPose();
      DriveRagdollFromAnimation(DtSecs);
    }
  }
}

void SandboxScene::DriveRagdollFromAnimation(float deltaSeconds) {
  if (!m_driveRagdollFromAnimation || !Meshes[0].HasPhysicsRagdoll()) {
    return;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (!engineContext || !engineContext->physics || !skinned || skinned->HasSnapshotBoneMatrices()) {
    return;
  }

  if (!t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, m_ragdollAnimationPose)) {
    if (!m_ragdollDriveLogEmitted) {
      T8_LOG_ERROR("[SandboxScene] Failed to build animation-driven ragdoll pose for '%s'", g_config.modelPath.c_str());
      m_ragdollDriveLogEmitted = true;
    }
    return;
  }

  if (!engineContext->physics->DriveRagdollFromPose(Meshes[0].GetPhysicsRagdoll(), m_ragdollAnimationPose, deltaSeconds)) {
    if (!m_ragdollDriveLogEmitted) {
      T8_LOG_ERROR("[SandboxScene] Failed to drive ragdoll from animation pose for '%s'", g_config.modelPath.c_str());
      m_ragdollDriveLogEmitted = true;
    }
    return;
  }

  if (!m_ragdollDriveLogEmitted) {
    T8_LOG_INFO("[SandboxScene] Driving humanoid ragdoll from animation pose: bodies=%zu", m_ragdollAnimationPose.bones.size());
    LogRagdollFloorDiagnostics("animation driven");
    m_ragdollDriveLogEmitted = true;
  }
}

void SandboxScene::UpdateSkeletonFromRagdollPhysics() {
  if (!m_ragdollPhysicsDriven || !Meshes[0].HasPhysicsRagdoll()) {
    return;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (!engineContext || !engineContext->physics || !skinned || !skinned->HasSkinData()) {
    return;
  }

  if (!engineContext->physics->GetRagdollState(Meshes[0].GetPhysicsRagdoll(), m_ragdollPhysicsStates) ||
      !t850::BuildSkeletonPoseFromRagdollState(
          *skinned,
          Meshes[0].Final,
          m_ragdollAnimationBinding,
          m_ragdollPhysicsStates,
          m_ragdollPhysicsBoneIndices,
          m_ragdollPhysicsCombinedMatrices) ||
      !skinned->GetAnimController().ApplyCombinedPoseOverrides(
          m_ragdollPhysicsBoneIndices,
          m_ragdollPhysicsCombinedMatrices)) {
    if (!m_ragdollPhysicsLogEmitted) {
      T8_LOG_ERROR("[SandboxScene] Failed to drive skinned skeleton from physics for '%s'", g_config.modelPath.c_str());
      m_ragdollPhysicsLogEmitted = true;
    }
    return;
  }

  if (!m_ragdollPhysicsLogEmitted) {
    T8_LOG_INFO("[SandboxScene] Driving skinned skeleton from dynamic ragdoll physics: bodies=%zu", m_ragdollPhysicsStates.size());
    m_ragdollPhysicsLogEmitted = true;
  }
  if (!m_ragdollFloorRuntimeDiagEmitted) {
    LogRagdollFloorDiagnostics("first dynamic frame");
    m_ragdollFloorRuntimeDiagEmitted = true;
  }
}

void SandboxScene::SwitchRagdollToPhysics() {
  if (m_ragdollPhysicsDriven) {
    T8_LOG_INFO("[SandboxScene] Ragdoll is already physics-driven");
    return;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (!engineContext || !engineContext->physics || !Meshes[0].HasPhysicsRagdoll() || !skinned || !skinned->HasSkinData()) {
    T8_LOG_ERROR("[SandboxScene] Cannot switch to ragdoll physics: no skinned ragdoll is attached");
    return;
  }

  std::vector<XMATRIX44> animationShaderMatrices;
  skinned->ExportBoneMatrices(animationShaderMatrices);

  if (m_driveRagdollFromAnimation &&
      t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, m_ragdollAnimationPose)) {
    engineContext->physics->DriveRagdollFromPose(Meshes[0].GetPhysicsRagdoll(), m_ragdollAnimationPose, 0.0f);
  }

  std::vector<t850::PhysicsBodyState> handoffPhysicsStates;
  std::vector<int> handoffBoneIndices;
  std::vector<XMATRIX44> handoffCombinedMatrices;
  std::vector<XMATRIX44> handoffShaderMatrices;
  if (engineContext->physics->GetRagdollState(Meshes[0].GetPhysicsRagdoll(), handoffPhysicsStates) &&
      t850::BuildSkeletonPoseFromRagdollState(
          *skinned,
          Meshes[0].Final,
          m_ragdollAnimationBinding,
          handoffPhysicsStates,
          handoffBoneIndices,
          handoffCombinedMatrices) &&
      skinned->GetAnimController().ApplyCombinedPoseOverrides(handoffBoneIndices, handoffCombinedMatrices)) {
    skinned->ExportBoneMatrices(handoffShaderMatrices);
    DumpRagdollF5MatrixComparison(
        *skinned,
        m_ragdollAnimationPose,
        handoffPhysicsStates,
        handoffBoneIndices,
        handoffCombinedMatrices,
        animationShaderMatrices,
        handoffShaderMatrices);
    m_ragdollPhysicsStates = std::move(handoffPhysicsStates);
    m_ragdollPhysicsBoneIndices = std::move(handoffBoneIndices);
    m_ragdollPhysicsCombinedMatrices = std::move(handoffCombinedMatrices);
  } else {
    T8_LOG_ERROR("[SandboxScene] Failed to dump F5 ragdoll matrix comparison for '%s'", g_config.modelPath.c_str());
  }

  if (m_floorBody.IsValid()) {
    engineContext->physics->DestroyBody(m_floorBody);
    m_floorBody.Reset();
  }
  CreatePhysicsFloor(*engineContext->physics);
  LogRagdollFloorDiagnostics("F5 pre-dynamic");
  if (!engineContext->physics->SetRagdollMotion(Meshes[0].GetPhysicsRagdoll(), t850::PhysicsBodyMotion::Dynamic)) {
    T8_LOG_ERROR("[SandboxScene] Failed to switch ragdoll bodies to dynamic physics");
    return;
  }
  engineContext->physics->SetRagdollVelocity(
      Meshes[0].GetPhysicsRagdoll(),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));

  m_driveRagdollFromAnimation = false;
  m_ragdollPhysicsDriven = true;
  m_ragdollPhysicsLogEmitted = false;
  m_ragdollFloorRuntimeDiagEmitted = false;
  m_showPhysics = true;
  skinned->PauseAnimation();
  skinned->ClearSnapshotBoneMatrices();
  LogRagdollFloorDiagnostics("F5 post-dynamic");
  T8_LOG_INFO("[SandboxScene] F5: animation-to-physics ragdoll transition started");
}

bool SandboxScene::ResetRagdollPhysicsAndAnimation() {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!skinned || !skinned->HasSkinData()) {
    T8_LOG_ERROR("[SandboxScene] Cannot reset ragdoll: no skinned model is loaded");
    return false;
  }

  if (m_skeletonEditMode) {
    ExitSkeletonEditMode();
  }

  skinned->ClearSnapshotBoneMatrices();
  skinned->ResetAnimation();
  skinned->PlayAnimation();
  skinned->GetAnimController().Update(0.0f);

  m_ragdollPhysicsDriven = false;
  m_driveRagdollFromAnimation = true;
  m_ragdollDriveLogEmitted = false;
  m_ragdollPhysicsLogEmitted = false;
  m_ragdollPhysicsStates.clear();
  m_ragdollPhysicsBoneIndices.clear();
  m_ragdollPhysicsCombinedMatrices.clear();

  if (!engineContext || !engineContext->physics || m_ragdollAnimationBinding.referencePose.bones.empty()) {
    T8_LOG_INFO("[SandboxScene] F7: animation state reset");
    return true;
  }

  t850::PhysicsRagdollDesc pose;
  if (!t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, pose)) {
    return ApplyRagdollEditPose(true);
  }

  if (!Meshes[0].HasPhysicsRagdoll()) {
    const bool recreated = RecreateRagdollFromPose(pose);
    if (recreated) {
      T8_LOG_INFO("[SandboxScene] F7: animation and ragdoll reset");
    }
    return recreated;
  }

  engineContext->physics->SetRagdollMotion(Meshes[0].GetPhysicsRagdoll(), t850::PhysicsBodyMotion::Kinematic);
  engineContext->physics->SetRagdollVelocity(
      Meshes[0].GetPhysicsRagdoll(),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
  const bool driven = engineContext->physics->DriveRagdollFromPose(Meshes[0].GetPhysicsRagdoll(), pose, 0.0f);
  if (driven) {
    m_ragdollAnimationPose = std::move(pose);
    T8_LOG_INFO("[SandboxScene] F7: animation and ragdoll reset");
  }
  return driven;
}

void SandboxScene::LogRagdollFloorDiagnostics(const char* stage) {
  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics || !engineContext->physics->IsInitialized()) {
    return;
  }

  std::vector<t850::PhysicsDebugBody> bodies;
  if (!engineContext->physics->GetDebugBodies(bodies)) {
    return;
  }

  auto boxVerticalExtent = [](const XMATRIX44& transform, const XVECTOR3& halfExtents) {
    return std::fabs(transform.m12) * halfExtents.x +
           std::fabs(transform.m22) * halfExtents.y +
           std::fabs(transform.m32) * halfExtents.z;
  };
  auto capsuleVerticalExtent = [](const XMATRIX44& transform, const t850::PhysicsShapeDesc& shape) {
    return shape.radius + std::fabs(transform.m22) * shape.halfHeight;
  };

  bool hasFloor = false;
  float floorTop = 0.0f;
  float floorCenterY = 0.0f;
  float floorHalfHeight = 0.0f;
  for (const t850::PhysicsDebugBody& body : bodies) {
    if (body.debugName == "Sandbox ragdoll floor" && body.shape.type == t850::PhysicsShapeType::Box) {
      floorCenterY = body.state.worldTransform.m42;
      floorHalfHeight = boxVerticalExtent(body.state.worldTransform, body.shape.halfExtents);
      floorTop = floorCenterY + floorHalfHeight;
      hasFloor = true;
      break;
    }
  }
  if (!hasFloor) {
    T8_LOG_INFO("[RagdollFloor] %s: no static ragdoll floor body is present", stage ? stage : "unknown");
    return;
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  const uint32_t entityId = Meshes[0].GetEntityId();
  int capsuleCount = 0;
  int lowestBone = -1;
  float lowestMinY = (std::numeric_limits<float>::max)();
  float lowestCenterY = 0.0f;
  float lowestRadius = 0.0f;
  float lowestHalfHeight = 0.0f;
  float highestMaxY = -(std::numeric_limits<float>::max)();

  for (const t850::PhysicsDebugBody& body : bodies) {
    if (body.state.entityId != entityId ||
        body.state.boneIndex < 0 ||
        body.shape.type != t850::PhysicsShapeType::Capsule) {
      continue;
    }

    const float extentY = capsuleVerticalExtent(body.state.worldTransform, body.shape);
    const float minY = body.state.worldTransform.m42 - extentY;
    const float maxY = body.state.worldTransform.m42 + extentY;
    ++capsuleCount;
    if (minY < lowestMinY) {
      lowestMinY = minY;
      lowestCenterY = body.state.worldTransform.m42;
      lowestRadius = body.shape.radius;
      lowestHalfHeight = body.shape.halfHeight;
      lowestBone = body.state.boneIndex;
    }
    highestMaxY = (std::max)(highestMaxY, maxY);

  }

  const char* lowestBoneName =
      skeleton && lowestBone >= 0 && lowestBone < static_cast<int>(skeleton->Bones.size())
          ? skeleton->Bones[static_cast<std::size_t>(lowestBone)].Name.c_str()
          : "<unknown>";
  T8_LOG_INFO("[RagdollFloor] %s summary: floorTop=%.3f floorCenterY=%.3f floorHalfHeight=%.3f capsules=%d minCapsuleY=%.3f maxCapsuleY=%.3f lowestBone=%d '%s' lowestCenterY=%.3f clearance=%.3f lowestRadius=%.3f lowestHalfHeight=%.3f",
              stage ? stage : "unknown",
              floorTop,
              floorCenterY,
              floorHalfHeight,
              capsuleCount,
              lowestMinY,
              highestMaxY,
              lowestBone,
              lowestBoneName,
              lowestCenterY,
              lowestMinY - floorTop,
              lowestRadius,
              lowestHalfHeight);
}

void SandboxScene::CreatePhysicsFloor(t850::JoltPhysicsSystem& physics) {
  if (m_floorBody.IsValid() || !Meshes[0].pBase) {
    return;
  }

  RenderMesh* mesh = static_cast<RenderMesh*>(Meshes[0].pBase);
  RenderMesh::AABB worldBounds;
  if (!BuildWorldBounds(mesh, Meshes[0].Final, worldBounds)) {
    T8_LOG_ERROR("[SandboxScene] Failed to build physics floor: model bounds are unavailable");
    return;
  }

  RenderMesh::AABB floorBounds;
  floorBounds.Reset();
  ExpandBounds(floorBounds, worldBounds);
  RenderMesh::AABB ragdollBounds;
  const bool hasRagdollBounds =
      BuildRagdollCapsuleBounds(m_ragdollAnimationPose, ragdollBounds) ||
      BuildRagdollCapsuleBounds(m_ragdollAnimationBinding.referencePose, ragdollBounds);
  if (hasRagdollBounds) {
    ExpandBounds(floorBounds, ragdollBounds);
  }

  const float extentX = (floorBounds.max.x - floorBounds.min.x) * 0.5f;
  const float extentZ = (floorBounds.max.z - floorBounds.min.z) * 0.5f;
  constexpr float kRagdollFloorAreaScale = 3.0f;
  const float baseHalfSize = (std::max)((std::max)(extentX, extentZ) * 2.0f, (std::max)(1.0f, m_modelRadius * 2.0f));
  const float halfSize = baseHalfSize * std::sqrt(kRagdollFloorAreaScale);
  const float halfHeight = (std::max)(0.05f, m_modelRadius * 0.04f);
  const float gap = hasRagdollBounds
      ? (std::max)(0.05f, (std::min)(25.0f, m_modelRadius * 0.04f))
      : (std::max)(0.08f, m_modelRadius * 0.08f);
  const float floorSourceMinY = hasRagdollBounds ? ragdollBounds.min.y : worldBounds.min.y;

  XMATRIX44 floorTransform;
  floorTransform.Identity();
  floorTransform.m41 = (floorBounds.min.x + floorBounds.max.x) * 0.5f;
  floorTransform.m42 = floorSourceMinY - gap - halfHeight;
  floorTransform.m43 = (floorBounds.min.z + floorBounds.max.z) * 0.5f;

  t850::PhysicsBodyDesc floorDesc;
  floorDesc.entityId = Meshes[0].GetEntityId();
  floorDesc.debugName = "Sandbox ragdoll floor";
  floorDesc.shape = t850::PhysicsShapeDesc::Box(XVECTOR3(halfSize, halfHeight, halfSize, 0.0f));
  floorDesc.worldTransform = floorTransform;
  floorDesc.motion = t850::PhysicsBodyMotion::Static;
  floorDesc.friction = 0.85f;
  floorDesc.restitution = 0.05f;

  m_floorBody = physics.CreateBody(floorDesc);
  if (m_floorBody.IsValid()) {
    T8_LOG_INFO("[SandboxScene] Added static ragdoll floor top y=%.3f source=%s sourceMinY=%.3f meshMinY=%.3f halfSize=%.3f halfHeight=%.3f gap=%.3f areaScale=%.1f",
                floorTransform.m42 + halfHeight,
                hasRagdollBounds ? "ragdoll" : "mesh",
                floorSourceMinY,
                worldBounds.min.y,
                halfSize,
                halfHeight,
                gap,
                kRagdollFloorAreaScale);
  } else {
    T8_LOG_ERROR("[SandboxScene] Failed to create static ragdoll floor");
  }
}

std::string SandboxScene::BuildSkeletonEditSavePath() const {
  const std::string key = FileSafeModelKey(m_profileModelKey.empty() ? SandboxProfileModelKey(g_config.modelPath) : m_profileModelKey);
  return t850::ResourceLocator::Instance().ResolveCachePath("SkeletonEdits/" + key + ".json").string();
}

bool SandboxScene::EnterSkeletonEditMode() {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (!skinned || !skinned->HasSkinData()) {
    T8_LOG_ERROR("[SkeletonEdit] Cannot enter edit mode: active model has no skinned skeleton");
    return false;
  }

  if (m_ragdollPhysicsDriven) {
    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (engineContext && engineContext->physics && Meshes[0].HasPhysicsRagdoll()) {
      engineContext->physics->SetRagdollMotion(Meshes[0].GetPhysicsRagdoll(), t850::PhysicsBodyMotion::Kinematic);
      engineContext->physics->SetRagdollVelocity(
          Meshes[0].GetPhysicsRagdoll(),
          XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
          XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
    }
    m_ragdollPhysicsDriven = false;
  }

  m_skeletonEditWasPlaying = skinned->IsPlaying();
  skinned->PauseAnimation();
  skinned->ClearSnapshotBoneMatrices();
  T8_LOG_INFO("[SkeletonEdit] Moving '%s' to bind pose", g_config.modelPath.c_str());
  if (!skinned->GetAnimController().ApplyBindPose() ||
      !skinned->GetAnimController().ExportCombinedPose(m_skeletonEditBindCombined)) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to move '%s' to bind pose", g_config.modelPath.c_str());
    return false;
  }
  T8_LOG_INFO("[SkeletonEdit] Captured bind pose: bones=%zu", m_skeletonEditBindCombined.size());

  m_skeletonEditCombined = m_skeletonEditBindCombined;
  m_skeletonEditSavePath = BuildSkeletonEditSavePath();
  m_skeletonEditSelectedBone = m_skeletonEditCombined.empty() ? -1 : 0;
  m_skeletonEditDragging = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_skeletonEditDirty = false;
  m_skeletonEditMode = true;
  m_showSkeleton = true;
  m_showPhysics = m_showPhysics || Meshes[0].HasPhysicsRagdoll();
  T8_LOG_INFO("[SkeletonEdit] Applying edit pose");
  LoadSkeletonEditPose();
  ApplySkeletonEditPose();
  if (!m_ragdollEditDirty) {
    LoadRagdollEditPose();
  } else if (!m_ragdollAnimationBinding.referencePose.bones.empty()) {
    for (int capsuleIndex = 0; capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()); ++capsuleIndex) {
      UpdateRagdollReferenceBodyFromLocal(capsuleIndex);
    }
    ApplyRagdollEditPose(true);
  }
  T8_LOG_INFO("[SkeletonEdit] Entered bind-pose edit mode for '%s'", g_config.modelPath.c_str());
  return true;
}

void SandboxScene::ExitSkeletonEditMode() {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  m_skeletonEditMode = false;
  m_skeletonEditDragging = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  if (skinned && m_skeletonEditWasPlaying) {
    skinned->PlayAnimation();
  }
  T8_LOG_INFO("[SkeletonEdit] Exited edit mode");
}

bool SandboxScene::ApplySkeletonEditPose() {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (!skinned || !skinned->HasSkinData() || m_skeletonEditCombined.empty()) {
    return false;
  }

  std::vector<int> boneIndices;
  boneIndices.reserve(m_skeletonEditCombined.size());
  for (std::size_t i = 0; i < m_skeletonEditCombined.size(); ++i) {
    boneIndices.push_back(static_cast<int>(i));
  }
  if (!skinned->GetAnimController().ApplyCombinedPoseOverrides(boneIndices, m_skeletonEditCombined)) {
    return false;
  }
  m_ragdollDriveLogEmitted = false;
  return true;
}

bool SandboxScene::ResetSkeletonEditPose() {
  if (m_skeletonEditBindCombined.empty()) {
    return false;
  }
  m_skeletonEditCombined = m_skeletonEditBindCombined;
  m_skeletonEditDirty = true;
  return ApplySkeletonEditPose();
}

bool SandboxScene::LoadSkeletonEditPose() {
  if (m_skeletonEditSavePath.empty()) {
    m_skeletonEditSavePath = BuildSkeletonEditSavePath();
  }

  std::ifstream file(m_skeletonEditSavePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return false;
  }

  const std::streamsize size = file.tellg();
  if (size < 0) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to read '%s'", m_skeletonEditSavePath.c_str());
    return false;
  }
  file.seekg(0, std::ios::beg);
  std::string json(static_cast<std::size_t>(size), '\0');
  if (size > 0 && !file.read(json.data(), size)) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to read '%s'", m_skeletonEditSavePath.c_str());
    return false;
  }

  SkeletonEditJson data;
  if (!ParseSkeletonEditJson(json, data)) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to parse '%s'", m_skeletonEditSavePath.c_str());
    return false;
  }

  const xF::xSkeleton* skeleton = Meshes[0].GetSkinnedMesh()
      ? Meshes[0].GetSkinnedMesh()->GetAnimController().GetAnimSkeleton()
      : nullptr;
  if (!skeleton || m_skeletonEditCombined.empty()) {
    return false;
  }

  int applied = 0;
  for (const SkeletonEditBoneJson& bone : data.bones) {
    int target = -1;
    if (bone.index >= 0 && bone.index < static_cast<int>(m_skeletonEditCombined.size()) &&
        bone.index < static_cast<int>(skeleton->Bones.size()) &&
        (bone.name.empty() || skeleton->Bones[bone.index].Name == bone.name)) {
      target = bone.index;
    } else if (!bone.name.empty()) {
      for (int i = 0; i < static_cast<int>(skeleton->Bones.size()) && i < static_cast<int>(m_skeletonEditCombined.size()); ++i) {
        if (skeleton->Bones[i].Name == bone.name) {
          target = i;
          break;
        }
      }
    }
    if (target >= 0) {
      m_skeletonEditCombined[static_cast<std::size_t>(target)] = MatrixFromArray16(bone.combined);
      ++applied;
    }
  }

  m_skeletonEditDirty = false;
  if (applied > 0) {
    ApplySkeletonEditPose();
    T8_LOG_INFO("[SkeletonEdit] Loaded %d edited bones from '%s'", applied, m_skeletonEditSavePath.c_str());
  }
  return applied > 0;
}

bool SandboxScene::SaveSkeletonEditPose() {
  if (m_skeletonEditSavePath.empty()) {
    m_skeletonEditSavePath = BuildSkeletonEditSavePath();
  }
  if (m_skeletonEditCombined.empty() || m_skeletonEditBindCombined.size() != m_skeletonEditCombined.size()) {
    return false;
  }

  const xF::xSkeleton* skeleton = Meshes[0].GetSkinnedMesh()
      ? Meshes[0].GetSkinnedMesh()->GetAnimController().GetAnimSkeleton()
      : nullptr;

  SkeletonEditJson data;
  data.model = m_profileModelKey.empty() ? SandboxProfileModelKey(g_config.modelPath) : m_profileModelKey;
  for (std::size_t i = 0; i < m_skeletonEditCombined.size(); ++i) {
    if (MatrixMaxAbsDiff(m_skeletonEditCombined[i], m_skeletonEditBindCombined[i]) <= 0.00001f) {
      continue;
    }
    SkeletonEditBoneJson bone;
    bone.index = static_cast<int>(i);
    if (skeleton && i < skeleton->Bones.size()) {
      bone.name = skeleton->Bones[i].Name;
    }
    bone.combined = MatrixToArray16(m_skeletonEditCombined[i]);
    data.bones.push_back(std::move(bone));
  }

  const std::filesystem::path path(m_skeletonEditSavePath);
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to create '%s'", path.parent_path().string().c_str());
    return false;
  }

  std::ofstream file(path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[SkeletonEdit] Failed to open '%s' for writing", m_skeletonEditSavePath.c_str());
    return false;
  }
  file << "{\n";
  file << "  \"model\": \"" << JsonEscape(data.model) << "\",\n";
  file << "  \"bones\": [\n";
  file << std::fixed << std::setprecision(8);
  for (std::size_t boneIndex = 0; boneIndex < data.bones.size(); ++boneIndex) {
    const SkeletonEditBoneJson& bone = data.bones[boneIndex];
    file << "    {\n";
    file << "      \"index\": " << bone.index << ",\n";
    file << "      \"name\": \"" << JsonEscape(bone.name) << "\",\n";
    file << "      \"combined\": [";
    for (std::size_t valueIndex = 0; valueIndex < bone.combined.size(); ++valueIndex) {
      if (valueIndex > 0) file << ", ";
      file << bone.combined[valueIndex];
    }
    file << "]\n";
    file << "    }" << (boneIndex + 1 < data.bones.size() ? "," : "") << "\n";
  }
  file << "  ]\n";
  file << "}\n";
  m_skeletonEditDirty = false;
  T8_LOG_INFO("[SkeletonEdit] Saved %zu edited bones to '%s'", data.bones.size(), m_skeletonEditSavePath.c_str());
  return true;
}

std::string SandboxScene::BuildRagdollEditSavePath() const {
  const std::string key = FileSafeModelKey(m_profileModelKey.empty() ? SandboxProfileModelKey(g_config.modelPath) : m_profileModelKey);
  return "Models/RagdollEdits/" + key + ".json";
}

int SandboxScene::FindRagdollCapsuleForBone(int boneIndex) const {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
    if (bones[static_cast<std::size_t>(i)].body.boneIndex == boneIndex) {
      return i;
    }
  }
  return -1;
}

int SandboxScene::FindRagdollCapsuleControllingBone(int boneIndex) const {
  const auto& controlled = m_ragdollAnimationBinding.controlledBoneIndices;
  for (int capsuleIndex = 0; capsuleIndex < static_cast<int>(controlled.size()); ++capsuleIndex) {
    const auto& controlledBones = controlled[static_cast<std::size_t>(capsuleIndex)];
    if (std::find(controlledBones.begin(), controlledBones.end(), boneIndex) != controlledBones.end()) {
      return capsuleIndex;
    }
  }
  return -1;
}

void SandboxScene::EnsureRagdollControlledBones() {
  auto& binding = m_ragdollAnimationBinding;
  const std::size_t capsuleCount = binding.referencePose.bones.size();
  const std::size_t previousControlledCount = binding.controlledBoneIndices.size();
  const std::size_t previousFrameCount = binding.controlledBodyFromBone.size();
  if (binding.controlledBoneIndices.size() != capsuleCount) {
    binding.controlledBoneIndices.resize(capsuleCount);
  }
  if (binding.controlledBodyFromBone.size() != capsuleCount) {
    binding.controlledBodyFromBone.resize(capsuleCount);
  }

  for (std::size_t i = 0; i < capsuleCount; ++i) {
    auto& controlledBones = binding.controlledBoneIndices[i];
    auto& controlledFrames = binding.controlledBodyFromBone[i];
    const bool invalidMapping = controlledBones.size() != controlledFrames.size();
    if (invalidMapping) {
      controlledBones.clear();
      controlledFrames.clear();
    }
    const bool addedMissingEntry = i >= previousControlledCount || i >= previousFrameCount;
    if ((addedMissingEntry || invalidMapping) &&
        controlledBones.empty() &&
        i < binding.bodyFromBone.size() &&
        binding.referencePose.bones[i].body.boneIndex >= 0) {
      controlledBones.push_back(binding.referencePose.bones[i].body.boneIndex);
      controlledFrames.push_back(binding.bodyFromBone[i]);
    }
  }
}

void SandboxScene::SelectRagdollEditCapsule(int capsuleIndex, bool syncBoneSelection) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
    m_ragdollEditSelectedCapsule = -1;
    m_ragdollEditSelectedJoint = -1;
    m_ragdollEditSelectedHandle = -1;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_skeletonEditDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
    m_ragdollEditSelectedUnassignedBone = -1;
    m_ragdollEditSelectedAffectedBone = -1;
    if (m_ragdollBoneSelectionActive) {
      m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
      m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
    }
    m_ragdollBoneSelectionActive = false;
    m_ragdollBoneMarqueeDragging = false;
    m_ragdollBoneSelectionPending.clear();
    return;
  }

  m_ragdollEditSelectedCapsule = capsuleIndex;
  m_ragdollEditSelectedHandle = 0;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_skeletonEditDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditSelectedUnassignedBone = -1;
  m_ragdollEditSelectedAffectedBone = -1;
  if (m_ragdollBoneSelectionActive) {
    m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
    m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
  }
  m_ragdollBoneSelectionActive = false;
  m_ragdollBoneMarqueeDragging = false;
  m_ragdollBoneSelectionPending.clear();
  if (GetRagdollEffectiveJointParentCapsule(capsuleIndex) >= 0) {
    m_ragdollEditSelectedJoint = capsuleIndex;
  } else {
    m_ragdollEditSelectedJoint = -1;
  }
  const int boneIndex = bones[static_cast<std::size_t>(capsuleIndex)].body.boneIndex;
  if (syncBoneSelection &&
      boneIndex >= 0 &&
      boneIndex < static_cast<int>(m_skeletonEditCombined.size())) {
    m_skeletonEditSelectedBone = boneIndex;
  }
}

void SandboxScene::SyncRagdollParentCapsulesFromBoneLinks() {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  m_ragdollParentCapsules.assign(bones.size(), -1);
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const int parentCapsule = FindRagdollCapsuleForBone(bones[i].parentBoneIndex);
    if (parentCapsule >= 0 && parentCapsule != static_cast<int>(i)) {
      m_ragdollParentCapsules[i] = parentCapsule;
    }
  }
}

void SandboxScene::EnsureRagdollParentCapsules() {
  const std::size_t capsuleCount = m_ragdollAnimationBinding.referencePose.bones.size();
  if (m_ragdollParentCapsules.size() == capsuleCount) {
    return;
  }

  if (m_ragdollParentCapsules.empty()) {
    SyncRagdollParentCapsulesFromBoneLinks();
    return;
  }

  std::vector<int> previous = std::move(m_ragdollParentCapsules);
  m_ragdollParentCapsules.assign(capsuleCount, -1);
  const std::size_t copyCount = (std::min)(previous.size(), capsuleCount);
  for (std::size_t i = 0; i < copyCount; ++i) {
    const int parentCapsule = previous[i];
    if (parentCapsule >= 0 && parentCapsule < static_cast<int>(capsuleCount) &&
        parentCapsule != static_cast<int>(i)) {
      m_ragdollParentCapsules[i] = parentCapsule;
    }
  }
}

void SandboxScene::EnsureRagdollJointState() {
  EnsureRagdollParentCapsules();
  auto& binding = m_ragdollAnimationBinding;
  const std::size_t capsuleCount = binding.referencePose.bones.size();

  if (m_ragdollJointParentCapsules.size() != capsuleCount) {
    const std::size_t previousSize = m_ragdollJointParentCapsules.size();
    m_ragdollJointParentCapsules.resize(capsuleCount, kRagdollJointInheritParent);
    for (std::size_t i = 0; i < (std::min)(previousSize, capsuleCount); ++i) {
      int& jointParent = m_ragdollJointParentCapsules[i];
      if (jointParent >= static_cast<int>(capsuleCount) || jointParent == static_cast<int>(i)) {
        jointParent = kRagdollJointInheritParent;
      }
    }
  }

  m_ragdollContactJoints.resize(capsuleCount, 0u);

  if (binding.jointFromBone.size() != capsuleCount) {
    const std::size_t previousSize = binding.jointFromBone.size();
    binding.jointFromBone.resize(capsuleCount, XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
    for (std::size_t i = previousSize; i < capsuleCount; ++i) {
      UpdateRagdollJointOffsetFromWorld(static_cast<int>(i));
    }
  }
}

void SandboxScene::EnsureRagdollFreezeState() {
  const std::size_t capsuleCount = m_ragdollAnimationBinding.referencePose.bones.size();
  m_ragdollFrozenCapsules.resize(capsuleCount, 0u);
  m_ragdollFrozenJoints.resize(capsuleCount, 0u);
}

bool SandboxScene::IsRagdollCapsuleFrozen(int capsuleIndex) const {
  return capsuleIndex >= 0 &&
      capsuleIndex < static_cast<int>(m_ragdollFrozenCapsules.size()) &&
      m_ragdollFrozenCapsules[static_cast<std::size_t>(capsuleIndex)] != 0u;
}

bool SandboxScene::IsRagdollJointFrozen(int childCapsule) const {
  return childCapsule >= 0 &&
      childCapsule < static_cast<int>(m_ragdollFrozenJoints.size()) &&
      m_ragdollFrozenJoints[static_cast<std::size_t>(childCapsule)] != 0u;
}

void SandboxScene::SetRagdollCapsuleFrozen(int capsuleIndex, bool frozen) {
  EnsureRagdollFreezeState();
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(m_ragdollFrozenCapsules.size())) {
    return;
  }
  m_ragdollFrozenCapsules[static_cast<std::size_t>(capsuleIndex)] = frozen ? 1u : 0u;
  if (frozen && m_ragdollEditSelectedCapsule == capsuleIndex) {
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditGizmoAxis = -1;
  }
  m_ragdollEditDirty = true;
}

void SandboxScene::SetRagdollJointFrozen(int childCapsule, bool frozen) {
  EnsureRagdollFreezeState();
  if (childCapsule < 0 || childCapsule >= static_cast<int>(m_ragdollFrozenJoints.size())) {
    return;
  }
  m_ragdollFrozenJoints[static_cast<std::size_t>(childCapsule)] = frozen ? 1u : 0u;
  if (frozen && m_ragdollEditSelectedJoint == childCapsule) {
    m_ragdollEditJointDragging = false;
    m_ragdollEditJointAxis = -1;
  }
  m_ragdollEditDirty = true;
}

int SandboxScene::GetRagdollEffectiveJointParentCapsule(int childCapsule) const {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size())) {
    return -1;
  }

  if (childCapsule < static_cast<int>(m_ragdollJointParentCapsules.size())) {
    const int jointParent = m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)];
    if (jointParent == kRagdollJointDisabled) {
      return -1;
    }
    if (jointParent >= 0 && jointParent < static_cast<int>(bones.size()) && jointParent != childCapsule) {
      return jointParent;
    }
  }

  if (childCapsule < static_cast<int>(m_ragdollParentCapsules.size())) {
    const int parentCapsule = m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)];
    if (parentCapsule >= 0 && parentCapsule < static_cast<int>(bones.size()) && parentCapsule != childCapsule) {
      return parentCapsule;
    }
  }
  return -1;
}

bool SandboxScene::UpdateRagdollJointOffsetFromWorld(int childCapsule) {
  auto& binding = m_ragdollAnimationBinding;
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(binding.referencePose.bones.size())) {
    return false;
  }
  if (binding.jointFromBone.size() != binding.referencePose.bones.size()) {
    binding.jointFromBone.resize(binding.referencePose.bones.size(), XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
  }

  XMATRIX44 boneWorld;
  if (!GetRagdollAuthoringBoneWorldTransform(binding.referencePose.bones[static_cast<std::size_t>(childCapsule)].body.boneIndex, boneWorld)) {
    binding.jointFromBone[static_cast<std::size_t>(childCapsule)] = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    return false;
  }
  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot update joint offset for capsule %d: bone frame is singular", childCapsule);
    return false;
  }
  binding.jointFromBone[static_cast<std::size_t>(childCapsule)] =
      t850::TransformPoint(binding.referencePose.bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition,
                           inverseBoneWorld);
  return true;
}

bool SandboxScene::ApplyRagdollParentCapsuleLinks() {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  if (m_ragdollParentCapsules.size() != bones.size()) {
    return false;
  }

  for (std::size_t childIndex = 0; childIndex < bones.size(); ++childIndex) {
    int parentCapsule = GetRagdollEffectiveJointParentCapsule(static_cast<int>(childIndex));
    bool invalidParent = parentCapsule < 0 ||
        parentCapsule >= static_cast<int>(bones.size()) ||
        parentCapsule == static_cast<int>(childIndex);
    int current = parentCapsule;
    for (std::size_t depth = 0; !invalidParent && depth < bones.size(); ++depth) {
      if (current == static_cast<int>(childIndex)) {
        invalidParent = true;
        break;
      }
      if (current < 0 || current >= static_cast<int>(m_ragdollParentCapsules.size())) {
        break;
      }
      current = GetRagdollEffectiveJointParentCapsule(current);
    }

    if (invalidParent) {
      bones[childIndex].parentBoneIndex = -1;
      continue;
    }
    bones[childIndex].parentBoneIndex = bones[static_cast<std::size_t>(parentCapsule)].body.boneIndex;
  }
  return true;
}

bool SandboxScene::SetRagdollCapsuleParent(int childCapsule, int parentCapsule) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size()) ||
      parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) ||
      childCapsule == parentCapsule ||
      m_ragdollParentCapsules.size() != bones.size()) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before changing its parent", childCapsule);
    return false;
  }

  int current = parentCapsule;
  for (std::size_t depth = 0; depth < m_ragdollParentCapsules.size(); ++depth) {
    if (current == childCapsule) {
      T8_LOG_ERROR("[RagdollEdit] Refusing cyclic parent link: capsule %d -> %d", childCapsule, parentCapsule);
      return false;
    }
    if (current < 0 || current >= static_cast<int>(m_ragdollParentCapsules.size())) {
      break;
    }
    current = m_ragdollParentCapsules[static_cast<std::size_t>(current)];
  }

  if (m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] == parentCapsule) {
    return true;
  }

  m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  if (childCapsule < static_cast<int>(m_ragdollJointParentCapsules.size()) &&
      m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] == kRagdollJointDisabled) {
    m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] = kRagdollJointInheritParent;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  m_ragdollEditDirty = true;
  m_ragdollEditSelectedJoint = childCapsule;
  m_showPhysics = true;
  T8_LOG_INFO("[RagdollEdit] Capsule %d parent set to capsule %d", childCapsule, parentCapsule);
  return ApplyRagdollEditPose(true);
}

bool SandboxScene::ClearRagdollCapsuleParent(int childCapsule) {
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(m_ragdollParentCapsules.size())) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before clearing its parent", childCapsule);
    return false;
  }
  if (m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] < 0) {
    return true;
  }

  m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] = -1;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  if (m_ragdollEditSelectedJoint == childCapsule) {
    m_ragdollEditSelectedJoint = -1;
  }
  m_ragdollEditDirty = true;
  T8_LOG_INFO("[RagdollEdit] Capsule %d parent cleared", childCapsule);
  return ApplyRagdollEditPose(true);
}

bool SandboxScene::SetRagdollCapsuleJoint(int childCapsule, int parentCapsule) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size()) ||
      parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) ||
      childCapsule == parentCapsule ||
      m_ragdollJointParentCapsules.size() != bones.size()) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(childCapsule) || IsRagdollJointFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule/joint %d is frozen; unfreeze it before changing its joint", childCapsule);
    return false;
  }

  int current = parentCapsule;
  for (std::size_t depth = 0; depth < bones.size(); ++depth) {
    if (current == childCapsule) {
      T8_LOG_ERROR("[RagdollEdit] Refusing cyclic joint link: capsule %d -> %d", childCapsule, parentCapsule);
      return false;
    }
    if (current < 0 || current >= static_cast<int>(bones.size())) {
      break;
    }
    current = GetRagdollEffectiveJointParentCapsule(current);
  }

  XMATRIX44 childWorld;
  XMATRIX44 parentWorld;
  if (GetCurrentRagdollEditCapsuleWorld(childCapsule, childWorld) &&
      GetCurrentRagdollEditCapsuleWorld(parentCapsule, parentWorld)) {
    bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition =
        XVECTOR3((childWorld.m41 + parentWorld.m41) * 0.5f,
                 (childWorld.m42 + parentWorld.m42) * 0.5f,
                 (childWorld.m43 + parentWorld.m43) * 0.5f,
                 1.0f);
    UpdateRagdollJointOffsetFromWorld(childCapsule);
  }

  m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  m_ragdollEditDirty = true;
  m_ragdollEditSelectedJoint = childCapsule;
  m_showPhysics = true;
  T8_LOG_INFO("[RagdollEdit] Capsule %d parent/joint set to capsule %d", childCapsule, parentCapsule);
  return ApplyRagdollEditPose(true);
}

bool SandboxScene::ComputeRagdollCapsuleContactAnchor(int childCapsule, int parentCapsule, XVECTOR3& outAnchor) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size()) ||
      parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) ||
      childCapsule == parentCapsule) {
    return false;
  }

  auto getCapsuleSegment = [&](int capsuleIndex,
                               const XMATRIX44& bodyWorld,
                               XVECTOR3& outStart,
                               XVECTOR3& outEnd,
                               XVECTOR3& outCenter,
                               XVECTOR3& outAxis,
                               float& outRadius) {
    const auto& shape = bones[static_cast<std::size_t>(capsuleIndex)].body.shape;
    if (shape.type != t850::PhysicsShapeType::Capsule) {
      return false;
    }
    outCenter = XVECTOR3(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
    outAxis = Normalize3(XVECTOR3(bodyWorld.m21, bodyWorld.m22, bodyWorld.m23, 0.0f),
                         XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    outRadius = (std::max)(0.001f, shape.radius);
    const float halfHeight = (std::max)(0.0f, shape.halfHeight);
    outStart = outCenter - outAxis * halfHeight;
    outEnd = outCenter + outAxis * halfHeight;
    outStart.w = 1.0f;
    outEnd.w = 1.0f;
    return true;
  };

  const XMATRIX44& childWorld = bones[static_cast<std::size_t>(childCapsule)].body.worldTransform;
  const XMATRIX44& parentWorld = bones[static_cast<std::size_t>(parentCapsule)].body.worldTransform;

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
  if (!getCapsuleSegment(childCapsule, childWorld, childStart, childEnd, childCenter, childAxis, childRadius) ||
      !getCapsuleSegment(parentCapsule, parentWorld, parentStart, parentEnd, parentCenter, parentAxis, parentRadius)) {
    return false;
  }

  XVECTOR3 childAxisPoint;
  XVECTOR3 parentAxisPoint;
  ClosestPointsOnSegments(childStart, childEnd, parentStart, parentEnd, childAxisPoint, parentAxisPoint);
  const XVECTOR3 normal = Normalize3(parentAxisPoint - childAxisPoint,
                                     Normalize3(parentCenter - childCenter, parentAxis));
  const XVECTOR3 childSurface = childAxisPoint + normal * childRadius;
  const XVECTOR3 parentSurface = parentAxisPoint - normal * parentRadius;
  outAnchor = (childSurface + parentSurface) * 0.5f;
  outAnchor.w = 1.0f;
  return true;
}

bool SandboxScene::SetRagdollCapsuleJointAtContact(int childCapsule, int parentCapsule) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 || childCapsule >= static_cast<int>(bones.size()) ||
      parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) ||
      childCapsule == parentCapsule ||
      m_ragdollParentCapsules.size() != bones.size() ||
      m_ragdollJointParentCapsules.size() != bones.size()) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(childCapsule) || IsRagdollJointFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule/joint %d is frozen; unfreeze it before changing its joint", childCapsule);
    return false;
  }

  int current = parentCapsule;
  for (std::size_t depth = 0; depth < bones.size(); ++depth) {
    if (current == childCapsule) {
      T8_LOG_ERROR("[RagdollEdit] Refusing cyclic contact joint link: capsule %d -> %d", childCapsule, parentCapsule);
      return false;
    }
    if (current < 0 || current >= static_cast<int>(bones.size())) {
      break;
    }
    current = GetRagdollEffectiveJointParentCapsule(current);
  }

  auto getCapsuleSegment = [&](int capsuleIndex,
                               const XMATRIX44& bodyWorld,
                               XVECTOR3& outStart,
                               XVECTOR3& outEnd,
                               XVECTOR3& outCenter,
                               XVECTOR3& outAxis,
                               float& outRadius) {
    if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
      return false;
    }
    const auto& shape = bones[static_cast<std::size_t>(capsuleIndex)].body.shape;
    if (shape.type != t850::PhysicsShapeType::Capsule) {
      return false;
    }
    outCenter = XVECTOR3(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
    outAxis = Normalize3(XVECTOR3(bodyWorld.m21, bodyWorld.m22, bodyWorld.m23, 0.0f),
                         XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    outRadius = (std::max)(0.001f, shape.radius);
    const float halfHeight = (std::max)(0.0f, shape.halfHeight);
    outStart = outCenter - outAxis * halfHeight;
    outEnd = outCenter + outAxis * halfHeight;
    outStart.w = 1.0f;
    outEnd.w = 1.0f;
    return true;
  };

  XMATRIX44 childWorld;
  XMATRIX44 parentWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(childCapsule, childWorld) ||
      !GetCurrentRagdollEditCapsuleWorld(parentCapsule, parentWorld)) {
    return false;
  }

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
  if (!getCapsuleSegment(childCapsule, childWorld, childStart, childEnd, childCenter, childAxis, childRadius) ||
      !getCapsuleSegment(parentCapsule, parentWorld, parentStart, parentEnd, parentCenter, parentAxis, parentRadius)) {
    return false;
  }

  XVECTOR3 movedChildStart = childStart;
  XVECTOR3 movedChildEnd = childEnd;
  XVECTOR3 totalDelta(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 normal = Normalize3(parentCenter - childCenter, parentAxis);
  constexpr float kContactTolerance = 0.0001f;
  for (int iteration = 0; iteration < 4; ++iteration) {
    XVECTOR3 childAxisPoint;
    XVECTOR3 parentAxisPoint;
    ClosestPointsOnSegments(movedChildStart, movedChildEnd, parentStart, parentEnd, childAxisPoint, parentAxisPoint);
    normal = Normalize3(parentAxisPoint - childAxisPoint, normal);
    const float centerlineDistance = Length3(parentAxisPoint - childAxisPoint);
    const float surfaceGap = centerlineDistance - (childRadius + parentRadius);
    if (std::fabs(surfaceGap) <= kContactTolerance) {
      break;
    }

    const XVECTOR3 delta = normal * surfaceGap;
    movedChildStart += delta;
    movedChildEnd += delta;
    totalDelta += delta;
  }

  childWorld.m41 += totalDelta.x;
  childWorld.m42 += totalDelta.y;
  childWorld.m43 += totalDelta.z;
  if ((std::fabs(totalDelta.x) > 0.000001f ||
       std::fabs(totalDelta.y) > 0.000001f ||
       std::fabs(totalDelta.z) > 0.000001f) &&
      !SetRagdollEditCapsuleWorldTransform(childCapsule, childWorld, false)) {
    return false;
  }

  XVECTOR3 childAxisPoint;
  XVECTOR3 parentAxisPoint;
  ClosestPointsOnSegments(movedChildStart, movedChildEnd, parentStart, parentEnd, childAxisPoint, parentAxisPoint);
  normal = Normalize3(parentAxisPoint - childAxisPoint, normal);
  const XVECTOR3 childSurface = childAxisPoint + normal * childRadius;
  const XVECTOR3 parentSurface = parentAxisPoint - normal * parentRadius;
  XVECTOR3 jointAnchor = (childSurface + parentSurface) * 0.5f;
  jointAnchor.w = 1.0f;

  bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition = jointAnchor;
  UpdateRagdollJointOffsetFromWorld(childCapsule);
  m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] = parentCapsule;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 1u;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  m_ragdollEditDirty = true;
  m_ragdollEditSelectedJoint = childCapsule;
  m_showPhysics = true;
  T8_LOG_INFO("[RagdollEdit] Capsule %d contact-snapped to parent/joint capsule %d at %.3f, %.3f, %.3f",
              childCapsule, parentCapsule, jointAnchor.x, jointAnchor.y, jointAnchor.z);
  return ApplyRagdollEditPose(true);
}

bool SandboxScene::ClearRagdollCapsuleJoint(int childCapsule) {
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(m_ragdollJointParentCapsules.size())) {
    return false;
  }
  if (IsRagdollJointFrozen(childCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Joint %d is frozen; unfreeze it before deleting", childCapsule);
    return false;
  }

  const int jointParent = GetRagdollEffectiveJointParentCapsule(childCapsule);
  if (jointParent < 0) {
    return true;
  }

  const int logicalParent =
      childCapsule < static_cast<int>(m_ragdollParentCapsules.size())
          ? m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)]
          : -1;
  m_ragdollJointParentCapsules[static_cast<std::size_t>(childCapsule)] =
      logicalParent >= 0 ? kRagdollJointDisabled : kRagdollJointInheritParent;
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  if (!ApplyRagdollParentCapsuleLinks()) {
    return false;
  }
  if (m_ragdollEditSelectedJoint == childCapsule) {
    m_ragdollEditSelectedJoint = -1;
  }
  m_ragdollEditDirty = true;
  T8_LOG_INFO("[RagdollEdit] Capsule %d joint cleared", childCapsule);
  return ApplyRagdollEditPose(true);
}

bool SandboxScene::ClearRagdollCapsuleJointBetween(int capsuleA, int capsuleB) {
  EnsureRagdollJointState();
  if (capsuleA < 0 || capsuleB < 0 || capsuleA == capsuleB) {
    return false;
  }
  if (GetRagdollEffectiveJointParentCapsule(capsuleA) == capsuleB) {
    return ClearRagdollCapsuleJoint(capsuleA);
  }
  if (GetRagdollEffectiveJointParentCapsule(capsuleB) == capsuleA) {
    return ClearRagdollCapsuleJoint(capsuleB);
  }
  return false;
}

bool SandboxScene::AddControlledBoneToSelectedCapsule(int boneIndex) {
  EnsureRagdollControlledBones();
  EnsureRagdollFreezeState();
  if (m_ragdollEditSelectedCapsule < 0 ||
      m_ragdollEditSelectedCapsule >= static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()) ||
      boneIndex < 0) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before adding bones", m_ragdollEditSelectedCapsule);
    return false;
  }

  const std::size_t capsuleIndex = static_cast<std::size_t>(m_ragdollEditSelectedCapsule);
  auto& controlledBones = m_ragdollAnimationBinding.controlledBoneIndices[capsuleIndex];
  auto& controlledFrames = m_ragdollAnimationBinding.controlledBodyFromBone[capsuleIndex];
  if (std::find(controlledBones.begin(), controlledBones.end(), boneIndex) != controlledBones.end()) {
    return false;
  }

  XMATRIX44 bodyWorld;
  XMATRIX44 boneWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(m_ragdollEditSelectedCapsule, bodyWorld) ||
      !GetSkeletonEditBoneWorldTransform(boneIndex, boneWorld)) {
    return false;
  }

  for (std::size_t otherCapsule = 0; otherCapsule < m_ragdollAnimationBinding.controlledBoneIndices.size(); ++otherCapsule) {
    if (otherCapsule == capsuleIndex) {
      continue;
    }
    auto& otherBones = m_ragdollAnimationBinding.controlledBoneIndices[otherCapsule];
    auto& otherFrames = m_ragdollAnimationBinding.controlledBodyFromBone[otherCapsule];
    for (std::size_t i = 0; i < otherBones.size(); ++i) {
      if (otherBones[i] == boneIndex) {
        otherBones.erase(otherBones.begin() + static_cast<std::ptrdiff_t>(i));
        if (i < otherFrames.size()) {
          otherFrames.erase(otherFrames.begin() + static_cast<std::ptrdiff_t>(i));
        }
        break;
      }
    }
  }

  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot add bone %d to capsule %d: bone transform is singular",
                 boneIndex, m_ragdollEditSelectedCapsule);
    return false;
  }
  controlledBones.push_back(boneIndex);
  controlledFrames.push_back(bodyWorld * inverseBoneWorld);
  m_ragdollEditSelectedUnassignedBone = -1;
  m_ragdollEditSelectedAffectedBone = boneIndex;
  m_ragdollEditDirty = true;
  T8_LOG_INFO("[RagdollEdit] Capsule %d now controls bone %d", m_ragdollEditSelectedCapsule, boneIndex);
  return true;
}

bool SandboxScene::RemoveControlledBoneFromSelectedCapsule(int boneIndex) {
  EnsureRagdollControlledBones();
  EnsureRagdollFreezeState();
  if (m_ragdollEditSelectedCapsule < 0 ||
      m_ragdollEditSelectedCapsule >= static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size()) ||
      boneIndex < 0) {
    return false;
  }
  if (IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before removing bones", m_ragdollEditSelectedCapsule);
    return false;
  }

  const std::size_t capsuleIndex = static_cast<std::size_t>(m_ragdollEditSelectedCapsule);
  auto& controlledBones = m_ragdollAnimationBinding.controlledBoneIndices[capsuleIndex];
  auto& controlledFrames = m_ragdollAnimationBinding.controlledBodyFromBone[capsuleIndex];
  for (std::size_t i = 0; i < controlledBones.size(); ++i) {
    if (controlledBones[i] == boneIndex) {
      controlledBones.erase(controlledBones.begin() + static_cast<std::ptrdiff_t>(i));
      if (i < controlledFrames.size()) {
        controlledFrames.erase(controlledFrames.begin() + static_cast<std::ptrdiff_t>(i));
      }
      m_ragdollEditSelectedUnassignedBone = boneIndex;
      m_ragdollEditSelectedAffectedBone = -1;
      m_ragdollEditDirty = true;
      T8_LOG_INFO("[RagdollEdit] Capsule %d no longer controls bone %d", m_ragdollEditSelectedCapsule, boneIndex);
      return true;
    }
  }
  return false;
}

int SandboxScene::FindGeneratedRagdollCapsuleForBone(int boneIndex) const {
  const auto& bones = m_ragdollGeneratedBinding.referencePose.bones;
  for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
    if (bones[static_cast<std::size_t>(i)].body.boneIndex == boneIndex) {
      return i;
    }
  }
  return -1;
}

bool SandboxScene::GetSkeletonEditBoneWorldTransform(int boneIndex, XMATRIX44& outWorld) const {
  if (boneIndex >= 0 && boneIndex < static_cast<int>(m_skeletonEditCombined.size())) {
    outWorld = FlipMatrixZ(m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)]) * Meshes[0].Final;
    return true;
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return false;
  }
  outWorld = FlipMatrixZ(skeleton->Bones[static_cast<std::size_t>(boneIndex)].Combined) * Meshes[0].Final;
  return true;
}

bool SandboxScene::GetRagdollAuthoringBoneWorldTransform(int boneIndex, XMATRIX44& outWorld) const {
  if (boneIndex >= 0 && boneIndex < static_cast<int>(m_skeletonEditCombined.size())) {
    outWorld = FlipMatrixZ(m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)]) * Meshes[0].Final;
    return true;
  }

  const int generatedIndex = FindGeneratedRagdollCapsuleForBone(boneIndex);
  if (generatedIndex >= 0 &&
      generatedIndex < static_cast<int>(m_ragdollGeneratedBinding.referencePose.bones.size()) &&
      generatedIndex < static_cast<int>(m_ragdollGeneratedBinding.bodyFromBone.size())) {
    XMATRIX44 boneFromGeneratedBody;
    if (InvertAffineNoExit(m_ragdollGeneratedBinding.bodyFromBone[static_cast<std::size_t>(generatedIndex)],
                           boneFromGeneratedBody)) {
      outWorld = boneFromGeneratedBody *
          m_ragdollGeneratedBinding.referencePose.bones[static_cast<std::size_t>(generatedIndex)].body.worldTransform;
      return true;
    }
  }

  return GetSkeletonEditBoneWorldTransform(boneIndex, outWorld);
}

int SandboxScene::FindSkeletonEditDisplayEndpoint(int boneIndex) const {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return -1;
  }

  const std::string ownerName = LowerName(skeleton->Bones[static_cast<std::size_t>(boneIndex)].Name);
  if (!IsHumanoidDisplayBoneName(ownerName)) {
    return -1;
  }

  const xF::xBone& bone = skeleton->Bones[static_cast<std::size_t>(boneIndex)];
  if (bone.Dad < skeleton->Bones.size() && bone.Dad != static_cast<unsigned short>(boneIndex)) {
    const std::string parentName = LowerName(skeleton->Bones[bone.Dad].Name);
    XMATRIX44 boneWorld;
    XMATRIX44 parentWorld;
    if (IsSpineLikeDisplayName(ownerName) &&
        IsSpineLikeDisplayName(parentName) &&
        GetSkeletonEditBoneWorldTransform(boneIndex, boneWorld) &&
        GetSkeletonEditBoneWorldTransform(bone.Dad, parentWorld) &&
        Length3(XVECTOR3(boneWorld.m41 - parentWorld.m41, boneWorld.m42 - parentWorld.m42, boneWorld.m43 - parentWorld.m43, 0.0f)) < 0.001f) {
      return -1;
    }
  }

  auto combinedChildren = [&](int index) {
    std::vector<int> result;
    if (index < 0 || index >= static_cast<int>(skeleton->Bones.size())) {
      return result;
    }
    for (unsigned int child : skeleton->Bones[static_cast<std::size_t>(index)].Sons) {
      if (child < skeleton->Bones.size() &&
          std::find(result.begin(), result.end(), static_cast<int>(child)) == result.end()) {
        result.push_back(static_cast<int>(child));
      }
    }
    for (int child = 0; child < static_cast<int>(skeleton->Bones.size()); ++child) {
      if (skeleton->Bones[static_cast<std::size_t>(child)].Dad == static_cast<unsigned short>(index) &&
          child != index &&
          std::find(result.begin(), result.end(), child) == result.end()) {
        result.push_back(child);
      }
    }
    return result;
  };

  XMATRIX44 ownerWorld;
  if (!GetSkeletonEditBoneWorldTransform(boneIndex, ownerWorld)) {
    return -1;
  }
  const XVECTOR3 ownerPosition(ownerWorld.m41, ownerWorld.m42, ownerWorld.m43, 1.0f);

  struct Candidate {
    int boneIndex = -1;
    int score = 0;
    float length = 0.0f;
  };
  std::vector<Candidate> candidates;
  std::function<void(int, int)> gather = [&](int searchBoneIndex, int depth) {
    if (searchBoneIndex < 0 || searchBoneIndex >= static_cast<int>(skeleton->Bones.size()) || depth > 4) {
      return;
    }
    for (int childIndex : combinedChildren(searchBoneIndex)) {
      const std::string childName = LowerName(skeleton->Bones[static_cast<std::size_t>(childIndex)].Name);
      if (IsAttachmentBoneName(childName)) {
        continue;
      }

      XMATRIX44 childWorld;
      if (!GetSkeletonEditBoneWorldTransform(childIndex, childWorld)) {
        continue;
      }
      const XVECTOR3 childPosition(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
      const float length = Length3(childPosition - ownerPosition);
      const bool displayChild = IsHumanoidDisplayBoneName(childName);
      const bool endpointHelper = IsEndpointHelperForBone(ownerName, childName);
      if ((displayChild || endpointHelper) && length >= 0.001f) {
        candidates.push_back({childIndex, DisplayChildPriority(ownerName, childName) - depth * 10, length});
      }
      if (length < 0.001f || IsDeformationHelperBoneName(childName) || (!displayChild && !endpointHelper)) {
        gather(childIndex, depth + 1);
      }
    }
  };
  gather(boneIndex, 0);
  if (candidates.empty()) {
    return -1;
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return a.length > b.length;
  });
  return candidates.front().boneIndex;
}

bool SandboxScene::BuildSkeletonEditBoneOctahedron(int boneIndex, float widthScale, std::array<XVECTOR3, 6>& outPoints) const {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return false;
  }

  const xF::xBone& bone = skeleton->Bones[static_cast<std::size_t>(boneIndex)];
  if (bone.Dad == static_cast<unsigned short>(boneIndex) || bone.Dad >= skeleton->Bones.size()) {
    return false;
  }

  XMATRIX44 rootWorld;
  XMATRIX44 tipWorld;
  if (!GetSkeletonEditBoneWorldTransform(bone.Dad, rootWorld) ||
      !GetSkeletonEditBoneWorldTransform(boneIndex, tipWorld)) {
    return false;
  }

  const XVECTOR3 root(rootWorld.m41, rootWorld.m42, rootWorld.m43, 1.0f);
  const XVECTOR3 tip(tipWorld.m41, tipWorld.m42, tipWorld.m43, 1.0f);
  BuildOctahedralBonePoints(root, tip, widthScale, (std::max)(0.001f, m_modelRadius * 0.004f), outPoints);
  return true;
}

bool SandboxScene::RebuildRagdollParentLinks() {
  if (!m_ragdollParentCapsules.empty()) {
    return ApplyRagdollParentCapsuleLinks();
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton) {
    return false;
  }

  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  for (auto& ragdollBone : bones) {
    ragdollBone.parentBoneIndex = -1;
    int current = ragdollBone.body.boneIndex;
    for (std::size_t depth = 0; depth < skeleton->Bones.size(); ++depth) {
      if (current < 0 || current >= static_cast<int>(skeleton->Bones.size())) {
        break;
      }
      const xF::xBone& skeletonBone = skeleton->Bones[static_cast<std::size_t>(current)];
      if (skeletonBone.Dad == static_cast<unsigned short>(current) ||
          skeletonBone.Dad >= skeleton->Bones.size()) {
        break;
      }
      current = skeletonBone.Dad;
      if (FindRagdollCapsuleForBone(current) >= 0) {
        ragdollBone.parentBoneIndex = current;
        break;
      }
    }
  }
  SyncRagdollParentCapsulesFromBoneLinks();
  return true;
}

bool SandboxScene::BuildDefaultRagdollCapsuleForBone(int boneIndex,
                                                     t850::PhysicsRagdollBoneDesc& outBone,
                                                     XMATRIX44& outBodyFromBone) const {
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
  if (!skeleton || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton->Bones.size())) {
    return false;
  }

  XMATRIX44 boneWorld;
  if (!GetSkeletonEditBoneWorldTransform(boneIndex, boneWorld)) {
    return false;
  }
  const XVECTOR3 root(boneWorld.m41, boneWorld.m42, boneWorld.m43, 1.0f);

  int endpointBone = FindSkeletonEditDisplayEndpoint(boneIndex);
  float bestLength = 0.0f;
  if (endpointBone < 0) {
    for (int i = 0; i < static_cast<int>(skeleton->Bones.size()); ++i) {
      const xF::xBone& candidate = skeleton->Bones[static_cast<std::size_t>(i)];
      if (candidate.Dad != static_cast<unsigned short>(boneIndex)) {
        continue;
      }
      XMATRIX44 childWorld;
      if (!GetSkeletonEditBoneWorldTransform(i, childWorld)) {
        continue;
      }
      const XVECTOR3 childPosition(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
      const float length = Length3(childPosition - root);
      if (length > bestLength) {
        bestLength = length;
        endpointBone = i;
      }
    }
  }

  XVECTOR3 end = root;
  if (endpointBone >= 0) {
    XMATRIX44 childWorld;
    if (!GetSkeletonEditBoneWorldTransform(endpointBone, childWorld)) {
      return false;
    }
    end = XVECTOR3(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
  } else {
    const xF::xBone& bone = skeleton->Bones[static_cast<std::size_t>(boneIndex)];
    if (bone.Dad == static_cast<unsigned short>(boneIndex) || bone.Dad >= skeleton->Bones.size()) {
      return false;
    }
    XMATRIX44 parentWorld;
    if (!GetSkeletonEditBoneWorldTransform(bone.Dad, parentWorld)) {
      return false;
    }
    const XVECTOR3 parent(parentWorld.m41, parentWorld.m42, parentWorld.m43, 1.0f);
    end = root + (root - parent);
  }

  XVECTOR3 axis = end - root;
  float length = Length3(axis);
  if (length <= 0.0001f) {
    return false;
  }
  axis = axis / length;

  const float radius = (std::max)(0.004f, (std::min)((std::max)(0.004f, m_modelRadius * 0.035f), length * 0.18f));
  const float capsuleLength = (std::max)(radius * 2.0f + 0.002f, length);
  const XVECTOR3 center = root + axis * (capsuleLength * 0.5f);
  const XMATRIX44 bodyWorld = MakeCapsuleBodyTransform(center, axis);

  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Failed to create capsule for bone %d: bone transform is singular", boneIndex);
    return false;
  }

  t850::PhysicsRagdollBoneDesc desc;
  desc.parentBoneIndex = -1;
  desc.jointWorldPosition = root;
  desc.body.entityId = Meshes[0].GetEntityId();
  desc.body.boneIndex = boneIndex;
  desc.body.debugName = skeleton->Bones[static_cast<std::size_t>(boneIndex)].Name;
  desc.body.motion = t850::PhysicsBodyMotion::Kinematic;
  desc.body.mass = (std::max)(0.1f, capsuleLength + radius * 2.0f);
  desc.body.worldTransform = bodyWorld;
  desc.body.shape = t850::PhysicsShapeDesc::Capsule(radius, (std::max)(0.001f, capsuleLength * 0.5f - radius));

  outBone = desc;
  outBodyFromBone = bodyWorld * inverseBoneWorld;
  return true;
}

bool SandboxScene::CreateRagdollCapsuleForBone(int boneIndex) {
  EnsureRagdollControlledBones();
  if (boneIndex < 0 ||
      FindRagdollCapsuleForBone(boneIndex) >= 0 ||
      FindRagdollCapsuleControllingBone(boneIndex) >= 0) {
    return false;
  }

  t850::PhysicsRagdollBoneDesc bone;
  XMATRIX44 bodyFromBone;
  const int generatedIndex = FindGeneratedRagdollCapsuleForBone(boneIndex);
  if (generatedIndex >= 0 &&
      generatedIndex < static_cast<int>(m_ragdollGeneratedBinding.referencePose.bones.size()) &&
      generatedIndex < static_cast<int>(m_ragdollGeneratedBinding.bodyFromBone.size())) {
    bone = m_ragdollGeneratedBinding.referencePose.bones[static_cast<std::size_t>(generatedIndex)];
    bodyFromBone = m_ragdollGeneratedBinding.bodyFromBone[static_cast<std::size_t>(generatedIndex)];
    bone.body.entityId = Meshes[0].GetEntityId();
    bone.body.motion = t850::PhysicsBodyMotion::Kinematic;
  } else if (!BuildDefaultRagdollCapsuleForBone(boneIndex, bone, bodyFromBone)) {
    T8_LOG_ERROR("[RagdollEdit] Failed to create capsule for bone %d", boneIndex);
    return false;
  }

  auto& desc = m_ragdollAnimationBinding.referencePose;
  if (desc.entityId == 0) {
    desc.entityId = Meshes[0].GetEntityId();
    desc.animationMode = t850::PhysicsAnimationMode::AnimationDriven;
    desc.animationToPhysicsBlend = 0.0f;
  }
  desc.bones.push_back(bone);
  m_ragdollAnimationBinding.bodyFromBone.push_back(bodyFromBone);
  m_ragdollAnimationBinding.jointFromBone.push_back(XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
  m_ragdollAnimationBinding.controlledBoneIndices.emplace_back();
  m_ragdollAnimationBinding.controlledBodyFromBone.emplace_back();
  EnsureRagdollParentCapsules();
  if (m_ragdollParentCapsules.size() < desc.bones.size()) {
    m_ragdollParentCapsules.resize(desc.bones.size(), -1);
  } else if (m_ragdollParentCapsules.size() > desc.bones.size()) {
    m_ragdollParentCapsules.resize(desc.bones.size());
  }
  m_ragdollParentCapsules.back() = -1;
  m_ragdollJointParentCapsules.resize(desc.bones.size(), kRagdollJointInheritParent);
  m_ragdollJointParentCapsules.back() = kRagdollJointInheritParent;
  m_ragdollFrozenCapsules.resize(desc.bones.size(), 0u);
  m_ragdollFrozenCapsules.back() = 0u;
  m_ragdollFrozenJoints.resize(desc.bones.size(), 0u);
  m_ragdollFrozenJoints.back() = 0u;
  m_ragdollContactJoints.resize(desc.bones.size(), 0u);
  m_ragdollContactJoints.back() = 0u;
  const int newIndex = static_cast<int>(desc.bones.size()) - 1;
  UpdateRagdollJointOffsetFromWorld(newIndex);
  if (!UpdateRagdollReferenceBodyFromLocal(newIndex)) {
    desc.bones.pop_back();
    m_ragdollAnimationBinding.bodyFromBone.pop_back();
    m_ragdollAnimationBinding.jointFromBone.pop_back();
    m_ragdollAnimationBinding.controlledBoneIndices.pop_back();
    m_ragdollAnimationBinding.controlledBodyFromBone.pop_back();
    if (!m_ragdollParentCapsules.empty()) {
      m_ragdollParentCapsules.pop_back();
    }
    if (!m_ragdollJointParentCapsules.empty()) {
      m_ragdollJointParentCapsules.pop_back();
    }
    if (!m_ragdollFrozenCapsules.empty()) {
      m_ragdollFrozenCapsules.pop_back();
    }
    if (!m_ragdollFrozenJoints.empty()) {
      m_ragdollFrozenJoints.pop_back();
    }
    if (!m_ragdollContactJoints.empty()) {
      m_ragdollContactJoints.pop_back();
    }
    T8_LOG_ERROR("[RagdollEdit] Failed to create capsule for bone %d '%s': could not update reference transform",
                 bone.body.boneIndex, bone.body.debugName.c_str());
    return false;
  }
  RebuildRagdollParentLinks();

  SelectRagdollEditCapsule(newIndex, true);
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditDirty = true;
  m_ragdollEditRebuildRequested = true;
  m_showPhysics = true;
  T8_LOG_INFO("[RagdollEdit] Created capsule for bone %d '%s'", bone.body.boneIndex, bone.body.debugName.c_str());
  return true;
}

bool SandboxScene::DeleteSelectedRagdollCapsule() {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  auto& locals = m_ragdollAnimationBinding.bodyFromBone;
  const int index = m_ragdollEditSelectedCapsule;
  if (index < 0 || index >= static_cast<int>(bones.size()) || index >= static_cast<int>(locals.size())) {
    return false;
  }
  EnsureRagdollFreezeState();
  if (IsRagdollCapsuleFrozen(index)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before deleting", index);
    return false;
  }

  const int boneIndex = bones[static_cast<std::size_t>(index)].body.boneIndex;
  const std::string debugName = bones[static_cast<std::size_t>(index)].body.debugName;
  bones.erase(bones.begin() + index);
  locals.erase(locals.begin() + index);
  if (index < static_cast<int>(m_ragdollAnimationBinding.jointFromBone.size())) {
    m_ragdollAnimationBinding.jointFromBone.erase(
        m_ragdollAnimationBinding.jointFromBone.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size())) {
    m_ragdollAnimationBinding.controlledBoneIndices.erase(
        m_ragdollAnimationBinding.controlledBoneIndices.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.controlledBodyFromBone.size())) {
    m_ragdollAnimationBinding.controlledBodyFromBone.erase(
        m_ragdollAnimationBinding.controlledBodyFromBone.begin() + index);
  }
  if (index < static_cast<int>(m_ragdollParentCapsules.size())) {
    m_ragdollParentCapsules.erase(m_ragdollParentCapsules.begin() + index);
    for (int& parentCapsule : m_ragdollParentCapsules) {
      if (parentCapsule == index) {
        parentCapsule = -1;
      } else if (parentCapsule > index) {
        --parentCapsule;
      }
    }
  } else {
    m_ragdollParentCapsules.clear();
  }
  if (index < static_cast<int>(m_ragdollJointParentCapsules.size())) {
    m_ragdollJointParentCapsules.erase(m_ragdollJointParentCapsules.begin() + index);
    for (int& jointParentCapsule : m_ragdollJointParentCapsules) {
      if (jointParentCapsule == index) {
        jointParentCapsule = kRagdollJointDisabled;
      } else if (jointParentCapsule > index) {
        --jointParentCapsule;
      }
    }
  } else {
    m_ragdollJointParentCapsules.clear();
  }
  if (index < static_cast<int>(m_ragdollFrozenCapsules.size())) {
    m_ragdollFrozenCapsules.erase(m_ragdollFrozenCapsules.begin() + index);
  } else {
    m_ragdollFrozenCapsules.clear();
  }
  if (index < static_cast<int>(m_ragdollFrozenJoints.size())) {
    m_ragdollFrozenJoints.erase(m_ragdollFrozenJoints.begin() + index);
  } else {
    m_ragdollFrozenJoints.clear();
  }
  if (index < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints.erase(m_ragdollContactJoints.begin() + index);
  } else {
    m_ragdollContactJoints.clear();
  }
  if (m_ragdollEditSelectedJoint == index) {
    m_ragdollEditSelectedJoint = -1;
  } else if (m_ragdollEditSelectedJoint > index) {
    --m_ragdollEditSelectedJoint;
  }
  if (m_ragdollEditRenamingCapsule == index) {
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
  } else if (m_ragdollEditRenamingCapsule > index) {
    --m_ragdollEditRenamingCapsule;
  }
  m_ragdollEditSelectedHandle = 0;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditSelectedCapsule = bones.empty()
      ? -1
      : (std::min)(index, static_cast<int>(bones.size()) - 1);
  RebuildRagdollParentLinks();
  m_ragdollEditDirty = true;

  if (bones.empty()) {
    m_ragdollClearRequested = true;
    m_ragdollEditSelectedCapsule = -1;
    m_ragdollEditSelectedJoint = -1;
    m_ragdollEditSelectedHandle = -1;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
    m_driveRagdollFromAnimation = false;
    m_ragdollPhysicsDriven = false;
    T8_LOG_INFO("[RagdollEdit] Deleted last capsule assignment for bone %d '%s'", boneIndex, debugName.c_str());
    return true;
  }

  T8_LOG_INFO("[RagdollEdit] Deleted capsule assignment for bone %d '%s'", boneIndex, debugName.c_str());
  return ApplyRagdollEditPose(true);
}

bool SandboxScene::ClearRagdollCapsules() {
  const t850::PhysicsRagdollHandle ragdollHandle = Meshes[0].GetPhysicsRagdoll();
  Meshes[0].AttachPhysicsRagdoll(t850::PhysicsRagdollHandle{});

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (engineContext && engineContext->physics && ragdollHandle.IsValid()) {
    engineContext->physics->DestroyRagdoll(ragdollHandle);
  }

  m_ragdollAnimationBinding.referencePose.bones.clear();
  m_ragdollAnimationBinding.bodyFromBone.clear();
  m_ragdollAnimationBinding.jointFromBone.clear();
  m_ragdollAnimationBinding.controlledBoneIndices.clear();
  m_ragdollAnimationBinding.controlledBodyFromBone.clear();
  m_ragdollParentCapsules.clear();
  m_ragdollJointParentCapsules.clear();
  m_ragdollFrozenCapsules.clear();
  m_ragdollFrozenJoints.clear();
  m_ragdollContactJoints.clear();
  m_ragdollAnimationPose = t850::PhysicsRagdollDesc{};
  m_ragdollPhysicsStates.clear();
  m_ragdollPhysicsBoneIndices.clear();
  m_ragdollPhysicsCombinedMatrices.clear();
  m_ragdollEditSelectedCapsule = -1;
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditSelectedHandle = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_skeletonEditDragging = false;
  m_ragdollClearRequested = false;
  m_ragdollEditRebuildRequested = false;
  m_driveRagdollFromAnimation = false;
  m_ragdollPhysicsDriven = false;
  m_showPhysics = false;
  m_ragdollEditDirty = true;
  T8_LOG_INFO("[RagdollEdit] Cleared all capsule assignments for '%s'", g_config.modelPath.c_str());
  return true;
}

bool SandboxScene::UpdateRagdollReferenceBodyFromLocal(int capsuleIndex) {
  if (capsuleIndex < 0 ||
      capsuleIndex >= static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()) ||
      capsuleIndex >= static_cast<int>(m_ragdollAnimationBinding.bodyFromBone.size())) {
    return false;
  }

  auto& bone = m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)];
  XMATRIX44 boneWorld;
  if (!GetRagdollAuthoringBoneWorldTransform(bone.body.boneIndex, boneWorld)) {
    const int generatedIndex = FindGeneratedRagdollCapsuleForBone(bone.body.boneIndex);
    if (generatedIndex < 0 ||
        generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.referencePose.bones.size()) ||
        generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.bodyFromBone.size())) {
      return false;
    }
    XMATRIX44 generatedBodyFromBone = m_ragdollGeneratedBinding.bodyFromBone[static_cast<std::size_t>(generatedIndex)];
    XMATRIX44 boneFromGeneratedBody;
    if (!InvertAffineNoExit(generatedBodyFromBone, boneFromGeneratedBody)) {
      T8_LOG_ERROR("[RagdollEdit] Cannot update capsule %d: generated capsule frame is singular", capsuleIndex);
      return false;
    }
    boneWorld =
        boneFromGeneratedBody *
        m_ragdollGeneratedBinding.referencePose.bones[static_cast<std::size_t>(generatedIndex)].body.worldTransform;
  }

  m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)].body.worldTransform =
      m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)] * boneWorld;
  if (capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.jointFromBone.size())) {
    m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)].jointWorldPosition =
        t850::TransformPoint(m_ragdollAnimationBinding.jointFromBone[static_cast<std::size_t>(capsuleIndex)], boneWorld);
  } else {
    m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)].jointWorldPosition =
        XVECTOR3(boneWorld.m41, boneWorld.m42, boneWorld.m43, 1.0f);
    UpdateRagdollJointOffsetFromWorld(capsuleIndex);
  }
  if (capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size()) &&
      capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.controlledBodyFromBone.size())) {
    const XMATRIX44& bodyWorld =
        m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(capsuleIndex)].body.worldTransform;
    const std::vector<int>& controlledBones =
        m_ragdollAnimationBinding.controlledBoneIndices[static_cast<std::size_t>(capsuleIndex)];
    std::vector<XMATRIX44>& controlledOffsets =
        m_ragdollAnimationBinding.controlledBodyFromBone[static_cast<std::size_t>(capsuleIndex)];
    controlledOffsets.clear();
    controlledOffsets.reserve(controlledBones.size());
    for (int controlledBone : controlledBones) {
      XMATRIX44 controlledBoneWorld;
      if (!GetRagdollAuthoringBoneWorldTransform(controlledBone, controlledBoneWorld)) {
        controlledOffsets.push_back(m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)]);
        continue;
      }
      XMATRIX44 inverseControlledBoneWorld;
      if (InvertAffineNoExit(controlledBoneWorld, inverseControlledBoneWorld)) {
        controlledOffsets.push_back(bodyWorld * inverseControlledBoneWorld);
      } else {
        T8_LOG_ERROR("[RagdollEdit] Controlled bone %d for capsule %d has a singular transform; using primary capsule offset",
                     controlledBone, capsuleIndex);
        controlledOffsets.push_back(m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)]);
      }
    }
  }
  return true;
}

bool SandboxScene::SetRagdollEditCapsuleWorldTransform(int capsuleIndex, const XMATRIX44& bodyWorld, bool rebuildRagdoll) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  auto& locals = m_ragdollAnimationBinding.bodyFromBone;
  if (capsuleIndex < 0 ||
      capsuleIndex >= static_cast<int>(bones.size()) ||
      capsuleIndex >= static_cast<int>(locals.size())) {
    return false;
  }
  EnsureRagdollFreezeState();
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }

  XMATRIX44 boneWorld;
  if (!GetSkeletonEditBoneWorldTransform(bones[static_cast<std::size_t>(capsuleIndex)].body.boneIndex, boneWorld)) {
    return false;
  }
  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot set capsule %d transform: bone %d transform is singular",
                 capsuleIndex, bones[static_cast<std::size_t>(capsuleIndex)].body.boneIndex);
    return false;
  }
  locals[static_cast<std::size_t>(capsuleIndex)] = bodyWorld * inverseBoneWorld;
  if (!UpdateRagdollReferenceBodyFromLocal(capsuleIndex)) {
    return false;
  }
  m_ragdollEditDirty = true;
  return ApplyRagdollEditPose(rebuildRagdoll);
}

bool SandboxScene::MoveRagdollEditCapsuleByWorldDelta(int capsuleIndex, const XVECTOR3& worldDelta, bool rebuildRagdoll) {
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }
  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }
  bodyWorld.m41 += worldDelta.x;
  bodyWorld.m42 += worldDelta.y;
  bodyWorld.m43 += worldDelta.z;
  return SetRagdollEditCapsuleWorldTransform(capsuleIndex, bodyWorld, rebuildRagdoll);
}

bool SandboxScene::RotateRagdollEditCapsuleWorld(int capsuleIndex,
                                                 const XVECTOR3& axisWorld,
                                                 float angleRadians,
                                                 bool rebuildRagdoll) {
  if (std::fabs(angleRadians) < 0.000001f) {
    return true;
  }
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }

  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }
  const XVECTOR3 center(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
  XMATRIX44 toOrigin;
  XMATRIX44 rotation;
  XMATRIX44 fromOrigin;
  XMatTranslation(toOrigin, -center.x, -center.y, -center.z);
  XMatRotationAxis(rotation, Normalize3(axisWorld, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)), angleRadians);
  XMatTranslation(fromOrigin, center.x, center.y, center.z);
  const XMATRIX44 rotatedWorld = bodyWorld * toOrigin * rotation * fromOrigin;
  return SetRagdollEditCapsuleWorldTransform(capsuleIndex, rotatedWorld, rebuildRagdoll);
}

bool SandboxScene::FlipRagdollEditCapsuleLocalAxis(int capsuleIndex, int axisIndex) {
  if (axisIndex < 0 || axisIndex > 2 || IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }
  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollEditGizmoFrame(capsuleIndex, center, axes, size)) {
    return false;
  }
  (void)center;
  (void)size;
  return RotateRagdollEditCapsuleWorld(capsuleIndex, axes[static_cast<std::size_t>(axisIndex)], xPI, true);
}

bool SandboxScene::RecreateRagdollFromPose(const t850::PhysicsRagdollDesc& pose) {
  if (pose.bones.empty()) {
    return false;
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics || !engineContext->physics->IsInitialized()) {
    return false;
  }

  const t850::PhysicsRagdollHandle oldHandle = Meshes[0].GetPhysicsRagdoll();
  const t850::PhysicsRagdollHandle newHandle =
      engineContext->physics->CreateRagdoll(pose, t850::PhysicsBodyMotion::Kinematic);
  if (!newHandle.IsValid()) {
    T8_LOG_ERROR("[RagdollEdit] Failed to recreate ragdoll for '%s'", g_config.modelPath.c_str());
    return false;
  }

  if (oldHandle.IsValid()) {
    engineContext->physics->DestroyRagdoll(oldHandle);
  }
  Meshes[0].AttachPhysicsRagdoll(newHandle);
  m_ragdollAnimationPose = pose;
  m_driveRagdollFromAnimation = true;
  m_ragdollPhysicsDriven = false;
  m_ragdollDriveLogEmitted = false;
  m_ragdollPhysicsLogEmitted = false;
  m_showPhysics = true;
  return true;
}

bool SandboxScene::ApplyRagdollEditPose(bool rebuildRagdoll) {
  if (m_ragdollAnimationBinding.referencePose.bones.empty() ||
      m_ragdollAnimationBinding.referencePose.bones.size() != m_ragdollAnimationBinding.bodyFromBone.size()) {
    return false;
  }
  EnsureRagdollControlledBones();

  t850::PhysicsRagdollDesc pose = m_ragdollAnimationBinding.referencePose;
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (skinned && skinned->HasSkinData()) {
    t850::PhysicsRagdollDesc animationPose;
    if (t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, animationPose)) {
      pose = std::move(animationPose);
    }
  }

  if (rebuildRagdoll || !Meshes[0].HasPhysicsRagdoll()) {
    return RecreateRagdollFromPose(pose);
  }

  t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->physics) {
    return false;
  }

  const bool updated = engineContext->physics->DriveRagdollFromPose(Meshes[0].GetPhysicsRagdoll(), pose, 0.0f);
  if (updated) {
    m_ragdollAnimationPose = pose;
    m_ragdollDriveLogEmitted = false;
  }
  return updated;
}

bool SandboxScene::LoadRagdollEditPose() {
  if (m_ragdollEditSavePath.empty()) {
    m_ragdollEditSavePath = BuildRagdollEditSavePath();
  }

  std::string json;
  if (!t850::ResourceLocator::Instance().ReadText(m_ragdollEditSavePath, json)) {
    T8_LOG_INFO("[RagdollEdit] No saved capsule file found at '%s'; keeping generated ragdoll", m_ragdollEditSavePath.c_str());
    return false;
  }

  RagdollEditJson data;
  if (!ParseRagdollEditJson(json, data)) {
    T8_LOG_ERROR("[RagdollEdit] Failed to parse '%s'", m_ragdollEditSavePath.c_str());
    return false;
  }

  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  auto& bodyFromBone = m_ragdollAnimationBinding.bodyFromBone;
  if (bones.size() != bodyFromBone.size()) {
    return false;
  }
  std::vector<t850::PhysicsRagdollBoneDesc> loadedBones;
  std::vector<XMATRIX44> loadedBodyFromBone;
  std::vector<XVECTOR3> loadedJointFromBone;
  std::vector<std::vector<int>> loadedControlledBones;
  std::vector<std::vector<XMATRIX44>> loadedControlledBodyFromBone;
  std::vector<int> loadedSavedIndices;
  std::vector<int> loadedParentRefs;
  std::vector<int> loadedJointParentRefs;
  std::vector<uint8_t> loadedFrozenCapsules;
  std::vector<uint8_t> loadedFrozenJoints;
  std::vector<uint8_t> loadedContactJoints;
  loadedBones.reserve(data.capsules.size());
  loadedBodyFromBone.reserve(data.capsules.size());
  loadedJointFromBone.reserve(data.capsules.size());
  loadedControlledBones.reserve(data.capsules.size());
  loadedControlledBodyFromBone.reserve(data.capsules.size());
  loadedSavedIndices.reserve(data.capsules.size());
  loadedParentRefs.reserve(data.capsules.size());
  loadedJointParentRefs.reserve(data.capsules.size());
  loadedFrozenCapsules.reserve(data.capsules.size());
  loadedFrozenJoints.reserve(data.capsules.size());
  loadedContactJoints.reserve(data.capsules.size());

  int applied = 0;
  for (const RagdollEditCapsuleJson& capsule : data.capsules) {
    int target = -1;
    if (capsule.index >= 0 && capsule.index < static_cast<int>(bones.size())) {
      const auto& candidate = bones[static_cast<std::size_t>(capsule.index)];
      if (capsule.boneIndex < 0 || candidate.body.boneIndex == capsule.boneIndex) {
        target = capsule.index;
      }
    }
    if (target < 0) {
      for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        const auto& candidate = bones[static_cast<std::size_t>(i)];
        if (candidate.body.boneIndex == capsule.boneIndex) {
          target = i;
          break;
        }
      }
    }
    if (target < 0) {
      t850::PhysicsRagdollBoneDesc createdBone;
      XMATRIX44 createdBodyFromBone;
      if (!BuildDefaultRagdollCapsuleForBone(capsule.boneIndex, createdBone, createdBodyFromBone)) {
        continue;
      }
      bones.push_back(createdBone);
      bodyFromBone.push_back(createdBodyFromBone);
      target = static_cast<int>(bones.size()) - 1;
    }
    if (capsule.boneIndex >= 0) {
      bool duplicate = false;
      for (const t850::PhysicsRagdollBoneDesc& loaded : loadedBones) {
        if (loaded.body.boneIndex == capsule.boneIndex) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) {
        continue;
      }
    }

    t850::PhysicsRagdollBoneDesc targetBone = bones[static_cast<std::size_t>(target)];
    if (!capsule.name.empty()) {
      targetBone.body.debugName = capsule.name;
    }
    XMATRIX44 targetBodyFromBone =
        data.schema >= 3
            ? MatrixFromArray16(capsule.bodyFromBone)
            : bodyFromBone[static_cast<std::size_t>(target)];
    XMATRIX44 targetPrimaryBoneWorld;
    const bool hasTargetBoneWorld = GetRagdollAuthoringBoneWorldTransform(targetBone.body.boneIndex, targetPrimaryBoneWorld);
    if (hasTargetBoneWorld) {
      targetBone.body.worldTransform = targetBodyFromBone * targetPrimaryBoneWorld;
    }
    if (data.schema >= 6) {
      targetBone.jointType = RagdollJointTypeFromInt(capsule.jointType);
      if (capsule.hasJointAnchor) {
        targetBone.jointWorldPosition = XVECTOR3(capsule.jointAnchor[0], capsule.jointAnchor[1], capsule.jointAnchor[2], 1.0f);
      }
    }
    XVECTOR3 targetJointFromBone(0.0f, 0.0f, 0.0f, 1.0f);
    if (hasTargetBoneWorld) {
      XMATRIX44 inverseTargetBoneWorld;
      if (InvertAffineNoExit(targetPrimaryBoneWorld, inverseTargetBoneWorld)) {
        targetJointFromBone = t850::TransformPoint(targetBone.jointWorldPosition, inverseTargetBoneWorld);
      }
    }
    targetBone.body.shape.type = t850::PhysicsShapeType::Capsule;
    targetBone.body.shape.radius = (std::max)(0.001f, capsule.radius);
    targetBone.body.shape.halfHeight = (std::max)(0.001f, capsule.halfHeight);
    if (data.schema >= 2) {
      targetBone.swingLimitRadians = (std::max)(0.0f, capsule.swingLimitRadians);
      targetBone.twistLimitRadians = (std::max)(0.0f, capsule.twistLimitRadians);
    }
    std::vector<int> controlledBones;
    if (data.schema >= 4) {
      controlledBones = capsule.controlledBones;
    }
    if (data.schema < 4 && controlledBones.empty() && targetBone.body.boneIndex >= 0) {
      controlledBones.push_back(targetBone.body.boneIndex);
    }

    std::vector<XMATRIX44> controlledBodyFromBone;
    controlledBodyFromBone.reserve(controlledBones.size());
    for (int controlledBone : controlledBones) {
      XMATRIX44 controlledBoneWorld;
      if (!GetRagdollAuthoringBoneWorldTransform(controlledBone, controlledBoneWorld)) {
        continue;
      }
      XMATRIX44 inverseControlledBoneWorld;
      if (!InvertAffineNoExit(controlledBoneWorld, inverseControlledBoneWorld)) {
        T8_LOG_ERROR("[RagdollEdit] Saved controlled bone %d for capsule %d has a singular transform",
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
    loadedSavedIndices.push_back(capsule.index >= 0 ? capsule.index : static_cast<int>(loadedSavedIndices.size()));
    loadedParentRefs.push_back(data.schema >= 5 ? capsule.parentCapsule : -1);
    loadedJointParentRefs.push_back(data.schema >= 6 ? capsule.jointParentCapsule : kRagdollJointInheritParent);
    loadedFrozenCapsules.push_back(data.schema >= 7 && capsule.capsuleFrozen ? 1u : 0u);
    loadedFrozenJoints.push_back(data.schema >= 7 && capsule.jointFrozen ? 1u : 0u);
    const bool legacyContactAnchor = data.schema < 9 && capsule.jointParentCapsule != kRagdollJointDisabled;
    loadedContactJoints.push_back(
        ((data.schema >= 9 && capsule.hasJointContactAnchor && capsule.jointContactAnchor) || legacyContactAnchor) ? 1u : 0u);
    ++applied;
  }

  if (applied > 0 || data.capsules.empty()) {
    std::vector<int> loadedParentCapsules(loadedBones.size(), -1);
    std::vector<int> loadedJointParentCapsules(loadedBones.size(), kRagdollJointInheritParent);
    if (data.schema >= 5) {
      auto findLoadedBySavedIndex = [&](int savedIndex) {
        for (int i = 0; i < static_cast<int>(loadedSavedIndices.size()); ++i) {
          if (loadedSavedIndices[static_cast<std::size_t>(i)] == savedIndex) {
            return i;
          }
        }
        if (savedIndex >= 0 && savedIndex < static_cast<int>(loadedBones.size())) {
          return savedIndex;
        }
        return -1;
      };
      for (int child = 0; child < static_cast<int>(loadedParentRefs.size()); ++child) {
        loadedParentCapsules[static_cast<std::size_t>(child)] =
            findLoadedBySavedIndex(loadedParentRefs[static_cast<std::size_t>(child)]);
      }
      if (data.schema >= 6) {
        for (int child = 0; child < static_cast<int>(loadedJointParentRefs.size()); ++child) {
          const int savedJointParent = loadedJointParentRefs[static_cast<std::size_t>(child)];
          loadedJointParentCapsules[static_cast<std::size_t>(child)] =
              savedJointParent >= 0 ? findLoadedBySavedIndex(savedJointParent) : savedJointParent;
        }
      }
    }

    bones = std::move(loadedBones);
    bodyFromBone = std::move(loadedBodyFromBone);
    m_ragdollAnimationBinding.jointFromBone = std::move(loadedJointFromBone);
    m_ragdollAnimationBinding.controlledBoneIndices = std::move(loadedControlledBones);
    m_ragdollAnimationBinding.controlledBodyFromBone = std::move(loadedControlledBodyFromBone);
    m_ragdollFrozenCapsules = std::move(loadedFrozenCapsules);
    m_ragdollFrozenJoints = std::move(loadedFrozenJoints);
    m_ragdollContactJoints = std::move(loadedContactJoints);
    EnsureRagdollFreezeState();
    EnsureRagdollControlledBones();
    m_ragdollContactJoints.resize(bones.size(), 0u);
    if (data.schema >= 5) {
      m_ragdollParentCapsules = std::move(loadedParentCapsules);
      m_ragdollJointParentCapsules = std::move(loadedJointParentCapsules);
      ApplyRagdollParentCapsuleLinks();
    } else {
      m_ragdollParentCapsules.clear();
      m_ragdollJointParentCapsules.clear();
      RebuildRagdollParentLinks();
    }
    for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
      UpdateRagdollReferenceBodyFromLocal(i);
    }
    int repairedContactAnchors = 0;
    for (int childCapsule = 0; childCapsule < static_cast<int>(bones.size()); ++childCapsule) {
      if (childCapsule >= static_cast<int>(m_ragdollContactJoints.size()) ||
          m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] == 0u) {
        continue;
      }
      const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
      if (parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) || parentCapsule == childCapsule) {
        continue;
      }
      XVECTOR3 contactAnchor;
      if (ComputeRagdollCapsuleContactAnchor(childCapsule, parentCapsule, contactAnchor)) {
        bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition = contactAnchor;
        UpdateRagdollJointOffsetFromWorld(childCapsule);
        ++repairedContactAnchors;
      }
    }
    if (repairedContactAnchors > 0) {
      T8_LOG_INFO("[RagdollEdit] Recovered %d contact joint anchors from capsule surfaces for '%s'",
                  repairedContactAnchors, m_ragdollEditSavePath.c_str());
    }
    SelectRagdollEditCapsule(
        (m_ragdollEditSelectedCapsule < 0 || m_ragdollEditSelectedCapsule >= static_cast<int>(bones.size()))
            ? (bones.empty() ? -1 : 0)
            : m_ragdollEditSelectedCapsule,
        false);
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
    m_ragdollEditDirty = false;
    if (!bones.empty()) {
      ApplyRagdollEditPose(true);
    }
    if (data.schema < 2) {
      T8_LOG_INFO("[RagdollEdit] Preserved inferred constraints for legacy cache '%s'", m_ragdollEditSavePath.c_str());
    }
    if (data.schema < 3) {
      T8_LOG_INFO("[RagdollEdit] Preserved generated capsule frames for legacy cache '%s'", m_ragdollEditSavePath.c_str());
    }
    if (data.schema < 4) {
      T8_LOG_INFO("[RagdollEdit] Initialized controlled bones for legacy cache '%s'", m_ragdollEditSavePath.c_str());
    }
    if (data.schema < 5) {
      T8_LOG_INFO("[RagdollEdit] Initialized capsule parent links for legacy cache '%s'", m_ragdollEditSavePath.c_str());
    }
    if (data.schema < 6) {
      T8_LOG_INFO("[RagdollEdit] Initialized explicit joint data for legacy cache '%s'", m_ragdollEditSavePath.c_str());
    }
    if (data.schema < 7) {
      T8_LOG_INFO("[RagdollEdit] Initialized freeze flags for legacy cache '%s'", m_ragdollEditSavePath.c_str());
    }
    T8_LOG_INFO("[RagdollEdit] Loaded %d edited capsules from '%s'", applied, m_ragdollEditSavePath.c_str());
  }
  return applied > 0 || data.capsules.empty();
}

bool SandboxScene::SaveRagdollEditPose() {
  if (m_ragdollEditSavePath.empty()) {
    m_ragdollEditSavePath = BuildRagdollEditSavePath();
  }

  EnsureRagdollParentCapsules();
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  ApplyRagdollParentCapsuleLinks();

  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (bones.size() != m_ragdollAnimationBinding.bodyFromBone.size()) {
    return false;
  }

  const std::filesystem::path path = ResolveRagdollEditWritePath(m_ragdollEditSavePath);
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  if (ec) {
    T8_LOG_ERROR("[RagdollEdit] Failed to create '%s'", path.parent_path().string().c_str());
    return false;
  }

  std::ofstream file(path, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[RagdollEdit] Failed to open '%s' for writing", path.string().c_str());
    return false;
  }

  const std::string model = m_profileModelKey.empty() ? SandboxProfileModelKey(g_config.modelPath) : m_profileModelKey;
  file << "{\n";
  file << "  \"schema\": 9,\n";
  file << "  \"model\": \"" << JsonEscape(model) << "\",\n";
  file << "  \"constraint_profile\": \"inferred-name-v1\",\n";
  file << "  \"capsule_frame_profile\": \"bone-to-endpoint-v1\",\n";
  file << "  \"capsules\": [\n";
  file << std::fixed << std::setprecision(8);
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const auto& bone = bones[i];
    const auto& shape = bone.body.shape;
    const auto matrix = MatrixToArray16(m_ragdollAnimationBinding.bodyFromBone[i]);
    file << "    {\n";
    file << "      \"index\": " << i << ",\n";
    file << "      \"bone_index\": " << bone.body.boneIndex << ",\n";
    file << "      \"name\": \"" << JsonEscape(bone.body.debugName) << "\",\n";
    const int parentCapsule =
        i < m_ragdollParentCapsules.size() ? m_ragdollParentCapsules[i] : -1;
    const int jointParentCapsule =
        i < m_ragdollJointParentCapsules.size() ? m_ragdollJointParentCapsules[i] : kRagdollJointInheritParent;
    const bool capsuleFrozen =
        i < m_ragdollFrozenCapsules.size() && m_ragdollFrozenCapsules[i] != 0u;
    const bool jointFrozen =
        i < m_ragdollFrozenJoints.size() && m_ragdollFrozenJoints[i] != 0u;
    const bool jointContactAnchor =
        i < m_ragdollContactJoints.size() && m_ragdollContactJoints[i] != 0u;
    file << "      \"parent_capsule\": " << parentCapsule << ",\n";
    file << "      \"joint_parent_capsule\": " << jointParentCapsule << ",\n";
    file << "      \"capsule_frozen\": " << (capsuleFrozen ? "true" : "false") << ",\n";
    file << "      \"joint_frozen\": " << (jointFrozen ? "true" : "false") << ",\n";
    file << "      \"joint_contact_anchor\": " << (jointContactAnchor ? "true" : "false") << ",\n";
    file << "      \"joint_type\": " << RagdollJointTypeToInt(bone.jointType) << ",\n";
    file << "      \"joint_anchor\": [" << bone.jointWorldPosition.x << ", "
         << bone.jointWorldPosition.y << ", " << bone.jointWorldPosition.z << "],\n";
    file << "      \"body_from_bone\": [";
    for (std::size_t valueIndex = 0; valueIndex < matrix.size(); ++valueIndex) {
      if (valueIndex > 0) file << ", ";
      file << matrix[valueIndex];
    }
    file << "],\n";
    file << "      \"radius\": " << shape.radius << ",\n";
    file << "      \"half_height\": " << shape.halfHeight << ",\n";
    file << "      \"swing_limit\": " << bone.swingLimitRadians << ",\n";
    file << "      \"twist_limit\": " << bone.twistLimitRadians << ",\n";
    file << "      \"controlled_bones\": [";
    const std::vector<int>* controlledBones =
        i < m_ragdollAnimationBinding.controlledBoneIndices.size()
            ? &m_ragdollAnimationBinding.controlledBoneIndices[i]
            : nullptr;
    if (controlledBones) {
      for (std::size_t controlledIndex = 0; controlledIndex < controlledBones->size(); ++controlledIndex) {
        if (controlledIndex > 0) file << ", ";
        file << (*controlledBones)[controlledIndex];
      }
    }
    file << "]\n";
    file << "    }" << (i + 1 < bones.size() ? "," : "") << "\n";
  }
  file << "  ]\n";
  file << "}\n";
  m_ragdollEditDirty = false;
  T8_LOG_INFO("[RagdollEdit] Saved %zu capsules to '%s'", bones.size(), path.string().c_str());
  return true;
}

bool SandboxScene::ResetRagdollEditPose() {
  if (m_ragdollGeneratedBinding.referencePose.bones.empty() ||
      m_ragdollGeneratedBinding.referencePose.bones.size() != m_ragdollGeneratedBinding.bodyFromBone.size()) {
    return false;
  }
  m_ragdollAnimationBinding = m_ragdollGeneratedBinding;
  m_ragdollParentCapsules.clear();
  m_ragdollJointParentCapsules.clear();
  m_ragdollFrozenCapsules.assign(m_ragdollAnimationBinding.referencePose.bones.size(), 0u);
  m_ragdollFrozenJoints.assign(m_ragdollAnimationBinding.referencePose.bones.size(), 0u);
  m_ragdollContactJoints.assign(m_ragdollAnimationBinding.referencePose.bones.size(), 0u);
  RebuildRagdollParentLinks();
  m_ragdollEditSelectedJoint = -1;
  m_ragdollEditRenamingCapsule = -1;
  m_ragdollEditRenameFocusPending = false;
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditDirty = true;
  return ApplyRagdollEditPose(true);
}

bool SandboxScene::ResetSelectedRagdollCapsule() {
  EnsureRagdollControlledBones();
  const int index = m_ragdollEditSelectedCapsule;
  if (index < 0 ||
      index >= static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()) ||
      index >= static_cast<int>(m_ragdollAnimationBinding.bodyFromBone.size())) {
    return false;
  }
  EnsureRagdollFreezeState();
  if (IsRagdollCapsuleFrozen(index)) {
    T8_LOG_INFO("[RagdollEdit] Capsule %d is frozen; unfreeze it before resetting", index);
    return false;
  }

  const int boneIndex = m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(index)].body.boneIndex;
  const int generatedIndex = FindGeneratedRagdollCapsuleForBone(boneIndex);
  if (generatedIndex < 0 ||
      generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.referencePose.bones.size()) ||
      generatedIndex >= static_cast<int>(m_ragdollGeneratedBinding.bodyFromBone.size())) {
    return false;
  }

  m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(index)] =
      m_ragdollGeneratedBinding.referencePose.bones[static_cast<std::size_t>(generatedIndex)];
  m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(index)] =
      m_ragdollGeneratedBinding.bodyFromBone[static_cast<std::size_t>(generatedIndex)];
  if (m_ragdollAnimationBinding.jointFromBone.size() < m_ragdollAnimationBinding.referencePose.bones.size()) {
    m_ragdollAnimationBinding.jointFromBone.resize(m_ragdollAnimationBinding.referencePose.bones.size(),
                                                   XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
  }
  if (generatedIndex < static_cast<int>(m_ragdollGeneratedBinding.jointFromBone.size())) {
    m_ragdollAnimationBinding.jointFromBone[static_cast<std::size_t>(index)] =
        m_ragdollGeneratedBinding.jointFromBone[static_cast<std::size_t>(generatedIndex)];
  } else {
    UpdateRagdollJointOffsetFromWorld(index);
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size())) {
    m_ragdollAnimationBinding.controlledBoneIndices[static_cast<std::size_t>(index)] =
        std::vector<int>{m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(index)].body.boneIndex};
  }
  if (index < static_cast<int>(m_ragdollAnimationBinding.controlledBodyFromBone.size())) {
    m_ragdollAnimationBinding.controlledBodyFromBone[static_cast<std::size_t>(index)] =
        std::vector<XMATRIX44>{m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(index)]};
  }
  ApplyRagdollParentCapsuleLinks();
  m_ragdollEditHandleDragging = false;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditJointDragging = false;
  m_ragdollEditGizmoAxis = -1;
  m_ragdollEditJointAxis = -1;
  m_ragdollEditDirty = true;
  return ApplyRagdollEditPose(true);
}

bool SandboxScene::GetSkeletonEditBoneWorldPosition(int boneIndex, XVECTOR3& outWorld) const {
  if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeletonEditCombined.size())) {
    return false;
  }
  const XMATRIX44& combined = m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)];
  const XVECTOR3 meshPosition(combined.m41, combined.m42, -combined.m43, 1.0f);
  outWorld = t850::TransformPoint(meshPosition, Meshes[0].Final);
  return true;
}

bool SandboxScene::SetSkeletonEditBoneWorldPosition(int boneIndex, const XVECTOR3& worldPosition) {
  if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeletonEditCombined.size())) {
    return false;
  }
  XMATRIX44 meshFromWorld;
  Meshes[0].Final.Inverse(&meshFromWorld);
  const XVECTOR3 meshPosition = t850::TransformPoint(worldPosition, meshFromWorld);
  XMATRIX44& combined = m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)];
  combined.m41 = meshPosition.x;
  combined.m42 = meshPosition.y;
  combined.m43 = -meshPosition.z;
  m_skeletonEditDirty = true;
  return ApplySkeletonEditPose();
}

std::array<float, 3> SandboxScene::GetSkeletonEditBoneScale(int boneIndex) const {
  std::array<float, 3> scale = {1.0f, 1.0f, 1.0f};
  if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeletonEditCombined.size())) {
    return scale;
  }
  const XMATRIX44& matrix = m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)];
  for (int row = 0; row < 3; ++row) {
    const float x = matrix.m[row][0];
    const float y = matrix.m[row][1];
    const float z = matrix.m[row][2];
    scale[static_cast<std::size_t>(row)] = std::sqrt(x * x + y * y + z * z);
  }
  return scale;
}

bool SandboxScene::SetSkeletonEditBoneScale(int boneIndex, const std::array<float, 3>& scale) {
  if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeletonEditCombined.size())) {
    return false;
  }
  XMATRIX44& matrix = m_skeletonEditCombined[static_cast<std::size_t>(boneIndex)];
  for (int row = 0; row < 3; ++row) {
    const float x = matrix.m[row][0];
    const float y = matrix.m[row][1];
    const float z = matrix.m[row][2];
    const float length = std::sqrt(x * x + y * y + z * z);
    const float target = (std::max)(0.001f, scale[static_cast<std::size_t>(row)]);
    if (length > 0.000001f) {
      const float factor = target / length;
      matrix.m[row][0] *= factor;
      matrix.m[row][1] *= factor;
      matrix.m[row][2] *= factor;
    }
  }
  m_skeletonEditDirty = true;
  return ApplySkeletonEditPose();
}

bool SandboxScene::GetCurrentRagdollEditCapsuleWorld(int capsuleIndex, XMATRIX44& outWorld) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
    return false;
  }

  if (m_ragdollPhysicsDriven && Meshes[0].HasPhysicsRagdoll()) {
    const int boneIndex = bones[static_cast<std::size_t>(capsuleIndex)].body.boneIndex;
    auto findPhysicsState = [&](const std::vector<t850::PhysicsBodyState>& states) {
      for (const t850::PhysicsBodyState& state : states) {
        if (state.boneIndex == boneIndex) {
          outWorld = state.worldTransform;
          return true;
        }
      }
      return false;
    };

    if (findPhysicsState(m_ragdollPhysicsStates)) {
      return true;
    }

    t850::EngineContext* engineContext = GetEngineContext();
    if (!engineContext) engineContext = &t850::GetEngineContext();
    if (engineContext && engineContext->physics) {
      std::vector<t850::PhysicsBodyState> states;
      if (engineContext->physics->GetRagdollState(Meshes[0].GetPhysicsRagdoll(), states) &&
          findPhysicsState(states)) {
        m_ragdollPhysicsStates = std::move(states);
        return true;
      }
    }
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  t850::PhysicsRagdollDesc pose;
  if (skinned && skinned->HasSkinData() &&
      t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, pose) &&
      capsuleIndex < static_cast<int>(pose.bones.size())) {
    outWorld = pose.bones[static_cast<std::size_t>(capsuleIndex)].body.worldTransform;
    return true;
  }

  outWorld = bones[static_cast<std::size_t>(capsuleIndex)].body.worldTransform;
  return true;
}

bool SandboxScene::SetRagdollEditJointWorldPosition(int childCapsule, const XVECTOR3& worldPosition) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(bones.size()) ||
      GetRagdollEffectiveJointParentCapsule(childCapsule) < 0) {
    return false;
  }
  if (IsRagdollJointFrozen(childCapsule)) {
    return false;
  }

  bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition =
      XVECTOR3(worldPosition.x, worldPosition.y, worldPosition.z, 1.0f);
  if (childCapsule < static_cast<int>(m_ragdollContactJoints.size())) {
    m_ragdollContactJoints[static_cast<std::size_t>(childCapsule)] = 0u;
  }
  UpdateRagdollJointOffsetFromWorld(childCapsule);
  m_ragdollEditDirty = true;
  return true;
}

bool SandboxScene::MoveRagdollEditJointByWorldDelta(int childCapsule, const XVECTOR3& worldDelta) {
  if (childCapsule < 0 || childCapsule >= static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size())) {
    return false;
  }
  if (IsRagdollJointFrozen(childCapsule)) {
    return false;
  }
  XVECTOR3 joint = m_ragdollAnimationBinding.referencePose.bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition;
  joint.x += worldDelta.x;
  joint.y += worldDelta.y;
  joint.z += worldDelta.z;
  return SetRagdollEditJointWorldPosition(childCapsule, joint);
}

bool SandboxScene::RotateRagdollEditJointWorld(int childCapsule, const XVECTOR3& axisWorld, float angleRadians) {
  if (std::fabs(angleRadians) < 0.000001f) {
    return true;
  }
  if (IsRagdollJointFrozen(childCapsule)) {
    return false;
  }

  XVECTOR3 joint;
  XVECTOR3 parentCenter;
  XVECTOR3 childCenter;
  XVECTOR3 parentTwist;
  XVECTOR3 childTwist;
  XVECTOR3 childPlane;
  float size = 0.0f;
  if (!GetRagdollJointVisualFrame(childCapsule, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, size)) {
    return false;
  }

  XMATRIX44 childWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(childCapsule, childWorld)) {
    return false;
  }

  XMATRIX44 toOrigin;
  XMATRIX44 rotation;
  XMATRIX44 fromOrigin;
  XMatTranslation(toOrigin, -joint.x, -joint.y, -joint.z);
  XMatRotationAxis(rotation, Normalize3(axisWorld, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)), angleRadians);
  XMatTranslation(fromOrigin, joint.x, joint.y, joint.z);
  const XMATRIX44 rotatedWorld = childWorld * toOrigin * rotation * fromOrigin;
  return SetRagdollEditCapsuleWorldTransform(childCapsule, rotatedWorld, false);
}

bool SandboxScene::FlipRagdollEditJointLocalAxis(int childCapsule, int axisIndex) {
  if (axisIndex < 0 || axisIndex > 2 || IsRagdollJointFrozen(childCapsule)) {
    return false;
  }
  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollJointGizmoFrame(childCapsule, center, axes, size)) {
    return false;
  }
  (void)center;
  (void)size;
  if (!RotateRagdollEditJointWorld(childCapsule, axes[static_cast<std::size_t>(axisIndex)], xPI)) {
    return false;
  }
  return ApplyRagdollEditPose(true);
}

bool SandboxScene::GetRagdollJointVisualFrame(int childCapsule,
                                              XVECTOR3& outJoint,
                                              XVECTOR3& outParentCenter,
                                              XVECTOR3& outChildCenter,
                                              XVECTOR3& outParentTwistAxis,
                                              XVECTOR3& outChildTwistAxis,
                                              XVECTOR3& outChildPlaneAxis,
                                              float& outSize) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollParentCapsules();
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(bones.size()) ||
      childCapsule >= static_cast<int>(m_ragdollParentCapsules.size())) {
    return false;
  }

  const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
  if (parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size()) || parentCapsule == childCapsule) {
    return false;
  }

  XMATRIX44 parentWorld;
  XMATRIX44 childWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(parentCapsule, parentWorld) ||
      !GetCurrentRagdollEditCapsuleWorld(childCapsule, childWorld)) {
    return false;
  }

  outParentCenter = XVECTOR3(parentWorld.m41, parentWorld.m42, parentWorld.m43, 1.0f);
  outChildCenter = XVECTOR3(childWorld.m41, childWorld.m42, childWorld.m43, 1.0f);
  outJoint = bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition;
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  t850::PhysicsRagdollDesc currentPose;
  if (skinned && skinned->HasSkinData() &&
      t850::BuildRagdollPoseFromAnimation(*skinned, Meshes[0].Final, m_ragdollAnimationBinding, currentPose) &&
      childCapsule < static_cast<int>(currentPose.bones.size())) {
    outJoint = currentPose.bones[static_cast<std::size_t>(childCapsule)].jointWorldPosition;
  }

  outParentTwistAxis = Normalize3(XVECTOR3(parentWorld.m21, parentWorld.m22, parentWorld.m23, 0.0f),
                                  XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  outChildTwistAxis = Normalize3(XVECTOR3(childWorld.m21, childWorld.m22, childWorld.m23, 0.0f),
                                 XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  outChildPlaneAxis = Normalize3(XVECTOR3(childWorld.m11, childWorld.m12, childWorld.m13, 0.0f),
                                 XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));

  const float distanceToCamera = Length3(outJoint - Cam.Eye);
  const float minSize = (std::max)(0.03f, m_modelRadius * 0.06f);
  const float maxSize = (std::max)(minSize, m_modelRadius * 0.35f);
  outSize = (std::max)(minSize, (std::min)(maxSize, distanceToCamera * 0.08f));
  return true;
}

bool SandboxScene::GetRagdollJointGizmoFrame(int childCapsule,
                                             XVECTOR3& outCenter,
                                             std::array<XVECTOR3, 3>& outAxes,
                                             float& outSize) {
  XVECTOR3 parentCenter;
  XVECTOR3 childCenter;
  XVECTOR3 parentTwist;
  XVECTOR3 childTwist;
  XVECTOR3 childPlane;
  if (!GetRagdollJointVisualFrame(childCapsule, outCenter, parentCenter, childCenter, parentTwist, childTwist, childPlane, outSize)) {
    return false;
  }

  outAxes[1] = Normalize3(childTwist, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  outAxes[0] = Normalize3(childPlane, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  outAxes[2] = Normalize3(Cross3(outAxes[0], outAxes[1]), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
  outAxes[0] = Normalize3(Cross3(outAxes[1], outAxes[2]), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  return true;
}

bool SandboxScene::PickRagdollEditJoint(float mouseX, float mouseY, float thresholdPixels, int& outChildCapsule) {
  outChildCapsule = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver || m_ragdollAnimationBinding.referencePose.bones.empty()) {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const ImVec2 mouse(mouseX, mouseY);
  float bestDistanceSq = thresholdPixels * thresholdPixels;
  for (int childCapsule = 0; childCapsule < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()); ++childCapsule) {
    XVECTOR3 joint;
    XVECTOR3 parentCenter;
    XVECTOR3 childCenter;
    XVECTOR3 parentTwist;
    XVECTOR3 childTwist;
    XVECTOR3 childPlane;
    float size = 0.0f;
    if (!GetRagdollJointVisualFrame(childCapsule, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, size)) {
      continue;
    }

    bool jointVisible = false;
    const ImVec2 jointScreen = ProjectWorldToScreen(joint, VP, width, height, jointVisible);
    if (!jointVisible) {
      continue;
    }
    float distanceSq = (jointScreen.x - mouse.x) * (jointScreen.x - mouse.x) +
                       (jointScreen.y - mouse.y) * (jointScreen.y - mouse.y);

    bool parentVisible = false;
    bool childVisible = false;
    const ImVec2 parentScreen = ProjectWorldToScreen(parentCenter, VP, width, height, parentVisible);
    const ImVec2 childScreen = ProjectWorldToScreen(childCenter, VP, width, height, childVisible);
    if (parentVisible) {
      distanceSq = (std::min)(distanceSq, DistancePointToSegmentSq(mouse, jointScreen, parentScreen));
    }
    if (childVisible) {
      distanceSq = (std::min)(distanceSq, DistancePointToSegmentSq(mouse, jointScreen, childScreen));
    }

    if (distanceSq < bestDistanceSq) {
      bestDistanceSq = distanceSq;
      outChildCapsule = childCapsule;
    }
  }

  return outChildCapsule >= 0;
}

bool SandboxScene::PickRagdollEditJointGizmo(float mouseX, float mouseY, int& outAxis) {
  outAxis = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver || m_ragdollEditSelectedJoint < 0 ||
      m_ragdollEditSelectionMode != kRagdollSelectJoints ||
      (m_ragdollEditGizmoMode != kRagdollToolMove && m_ragdollEditGizmoMode != kRagdollToolRotate) ||
      IsRagdollJointFrozen(m_ragdollEditSelectedJoint)) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollJointGizmoFrame(m_ragdollEditSelectedJoint, center, axes, size)) {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const ImVec2 mouse(mouseX, mouseY);
  constexpr float kThresholdPixels = 12.0f;
  float bestDistanceSq = kThresholdPixels * kThresholdPixels;

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      bool startVisible = false;
      bool endVisible = false;
      const ImVec2 start = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * (size * 0.12f),
                                                VP, width, height, startVisible);
      const ImVec2 end = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * (size * 0.75f),
                                              VP, width, height, endVisible);
      if (!startVisible || !endVisible) {
        continue;
      }
      const float distanceSq = DistancePointToSegmentSq(mouse, start, end);
      if (distanceSq < bestDistanceSq) {
        bestDistanceSq = distanceSq;
        outAxis = axisIndex;
      }
    }
  } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
    constexpr int kSegments = 64;
    const float radius = size * 0.62f;
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      const XVECTOR3 u = axes[static_cast<std::size_t>((axisIndex + 1) % 3)];
      const XVECTOR3 v = axes[static_cast<std::size_t>((axisIndex + 2) % 3)];
      ImVec2 previous;
      bool previousVisible = false;
      for (int segment = 0; segment <= kSegments; ++segment) {
        const float t = static_cast<float>(segment) / static_cast<float>(kSegments) * (2.0f * xPI);
        const XVECTOR3 point = center + (u * std::cos(t) + v * std::sin(t)) * radius;
        bool visible = false;
        const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
        if (visible && previousVisible) {
          const float distanceSq = DistancePointToSegmentSq(mouse, previous, screen);
          if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            outAxis = axisIndex;
          }
        }
        previous = screen;
        previousVisible = visible;
      }
    }
  }
  return outAxis >= 0;
}

bool SandboxScene::BeginRagdollEditJointGizmoDrag(float mouseX, float mouseY) {
  int pickedAxis = -1;
  if (!PickRagdollEditJointGizmo(mouseX, mouseY, pickedAxis)) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollJointGizmoFrame(m_ragdollEditSelectedJoint, center, axes, size)) {
    return false;
  }
  (void)size;

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver ? g_pBaseDriver->width : 1);
  const int height = (std::max)(1, g_pBaseDriver ? g_pBaseDriver->height : 1);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const XVECTOR3 axis = axes[static_cast<std::size_t>(pickedAxis)];
  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    if (!ClosestRayAxisParameter(ray, center, axis, m_ragdollEditJointLastParameter)) {
      return false;
    }
  } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
    XVECTOR3 hitPoint;
    if (!RayPlaneIntersection(ray, center, axis, hitPoint)) {
      return false;
    }
    m_ragdollEditJointLastVector = Normalize3(hitPoint - center,
                                              axes[static_cast<std::size_t>((pickedAxis + 1) % 3)]);
  } else {
    return false;
  }

  m_ragdollEditJointAxis = pickedAxis;
  m_ragdollEditJointDragCenter = center;
  m_ragdollEditJointDragAxis = axis;
  m_ragdollEditJointDragging = true;
  m_ragdollEditGizmoDragging = false;
  m_ragdollEditHandleDragging = false;
  m_skeletonEditDragging = false;
  return true;
}

bool SandboxScene::DragRagdollEditJointGizmo(float mouseX, float mouseY) {
  if (!m_ragdollEditJointDragging ||
      m_ragdollEditSelectedJoint < 0 ||
      m_ragdollEditJointAxis < 0 ||
      !g_pBaseDriver) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollJointGizmoFrame(m_ragdollEditSelectedJoint, center, axes, size)) {
    return false;
  }
  (void)center;
  (void)size;

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const XVECTOR3 axis = Normalize3(m_ragdollEditJointDragAxis, axes[static_cast<std::size_t>(m_ragdollEditJointAxis)]);

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    float currentParameter = 0.0f;
    if (!ClosestRayAxisParameter(ray, m_ragdollEditJointDragCenter, axis, currentParameter)) {
      return false;
    }
    const float deltaParameter = currentParameter - m_ragdollEditJointLastParameter;
    m_ragdollEditJointLastParameter = currentParameter;
    if (std::fabs(deltaParameter) <= 0.000001f) {
      return true;
    }
    return MoveRagdollEditJointByWorldDelta(m_ragdollEditSelectedJoint, axis * deltaParameter);
  }

  if (m_ragdollEditGizmoMode != kRagdollToolRotate) {
    return false;
  }

  XVECTOR3 hitPoint;
  if (!RayPlaneIntersection(ray, m_ragdollEditJointDragCenter, axis, hitPoint)) {
    return false;
  }
  const XVECTOR3 currentVector = Normalize3(hitPoint - m_ragdollEditJointDragCenter, m_ragdollEditJointLastVector);
  const float dot = (std::max)(-1.0f, (std::min)(1.0f, Dot3(m_ragdollEditJointLastVector, currentVector)));
  const float signedAngle = std::atan2(Dot3(axis, Cross3(m_ragdollEditJointLastVector, currentVector)), dot);
  m_ragdollEditJointLastVector = currentVector;
  if (std::fabs(signedAngle) <= 0.000001f) {
    return true;
  }
  return RotateRagdollEditJointWorld(m_ragdollEditSelectedJoint, axis, signedAngle);
}

void SandboxScene::DrawRagdollJointGizmos(bool editable) {
  const bool allowEditing = editable && m_skeletonEditMode && m_ragdollEditSelectionMode == kRagdollSelectJoints;
  if (!allowEditing || !g_pBaseDriver || !ImGui::GetCurrentContext()) {
    return;
  }

  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (bones.empty()) {
    return;
  }
  EnsureRagdollParentCapsules();

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  const ImU32 lineColor = IM_COL32(255, 185, 40, 165);
  const ImU32 jointColor = IM_COL32(255, 220, 80, 230);
  const ImU32 selectedColor = IM_COL32(255, 245, 120, 255);
  const ImU32 parentAxisColor = IM_COL32(255, 130, 40, 245);
  const ImU32 childAxisColor = IM_COL32(80, 220, 255, 255);
  const ImU32 planeAxisColor = IM_COL32(255, 90, 220, 245);
  const ImU32 coneColor = IM_COL32(255, 215, 70, 205);
  const ImU32 twistColor = IM_COL32(190, 120, 255, 230);

  for (int childCapsule = 0; childCapsule < static_cast<int>(bones.size()); ++childCapsule) {
    if (GetRagdollEffectiveJointParentCapsule(childCapsule) < 0) {
      continue;
    }

    XVECTOR3 joint;
    XVECTOR3 parentCenter;
    XVECTOR3 childCenter;
    XVECTOR3 parentTwist;
    XVECTOR3 childTwist;
    XVECTOR3 childPlane;
    float size = 0.0f;
    if (!GetRagdollJointVisualFrame(childCapsule, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, size)) {
      continue;
    }

    bool jointVisible = false;
    bool parentVisible = false;
    bool childVisible = false;
    const ImVec2 jointScreen = ProjectWorldToScreen(joint, VP, width, height, jointVisible);
    const ImVec2 parentScreen = ProjectWorldToScreen(parentCenter, VP, width, height, parentVisible);
    const ImVec2 childScreen = ProjectWorldToScreen(childCenter, VP, width, height, childVisible);
    if (!jointVisible) {
      continue;
    }

    const bool selected = allowEditing && childCapsule == m_ragdollEditSelectedJoint;
    if (parentVisible) {
      drawList->AddLine(parentScreen, jointScreen, selected ? selectedColor : lineColor, selected ? 3.0f : 1.6f);
    }
    if (childVisible) {
      drawList->AddLine(jointScreen, childScreen, selected ? selectedColor : lineColor, selected ? 3.0f : 1.6f);
    }
    drawList->AddCircleFilled(jointScreen, selected ? 6.0f : 4.0f, selected ? selectedColor : jointColor, 16);
    drawList->AddCircle(jointScreen, selected ? 11.0f : 7.0f, selected ? selectedColor : jointColor, 20, selected ? 2.5f : 1.5f);

    if (!selected) {
      continue;
    }

    drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 5.0f), selectedColor, "joint");

    auto drawAxis = [&](const XVECTOR3& axis, float length, ImU32 color, const char* label) {
      bool endVisible = false;
      const ImVec2 endScreen = ProjectWorldToScreen(joint + axis * length, VP, width, height, endVisible);
      if (!endVisible) {
        return;
      }
      drawList->AddLine(jointScreen, endScreen, color, 3.0f);
      drawList->AddCircleFilled(endScreen, 4.5f, color, 12);
      drawList->AddText(ImVec2(endScreen.x + 6.0f, endScreen.y - 6.0f), color, label);
    };
    drawAxis(parentTwist, size * 0.85f, parentAxisColor, "parent +Y");
    drawAxis(childTwist, size, childAxisColor, "child +Y twist");
    drawAxis(childPlane, size * 0.7f, planeAxisColor, "child +X plane");

    std::array<XVECTOR3, 3> gizmoAxes = {
        childPlane,
        childTwist,
        Normalize3(Cross3(childPlane, childTwist), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f))};

    if (IsRagdollJointFrozen(childCapsule)) {
      drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 37.0f), selectedColor, "frozen");
    } else if (m_ragdollEditGizmoMode == kRagdollToolMove) {
      drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 37.0f), selectedColor, "move anchor");
      for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
        const bool active = m_ragdollEditJointDragging && m_ragdollEditJointAxis == axisIndex;
        const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
        bool endVisible = false;
        const ImVec2 end = ProjectWorldToScreen(joint + gizmoAxes[static_cast<std::size_t>(axisIndex)] * (size * 0.75f),
                                                VP, width, height, endVisible);
        if (!endVisible) {
          continue;
        }
        drawList->AddLine(jointScreen, end, color, active ? 4.0f : 2.5f);
        drawList->AddCircleFilled(end, active ? 5.5f : 4.0f, color, 12);
      }
    } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
      drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 37.0f), selectedColor, "rotate child frame");
      constexpr int kGizmoSegments = 72;
      const float radius = size * 0.62f;
      for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
        const bool active = m_ragdollEditJointDragging && m_ragdollEditJointAxis == axisIndex;
        const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
        const XVECTOR3 u = gizmoAxes[static_cast<std::size_t>((axisIndex + 1) % 3)];
        const XVECTOR3 v = gizmoAxes[static_cast<std::size_t>((axisIndex + 2) % 3)];
        ImVec2 previous;
        bool previousVisible = false;
        for (int segment = 0; segment <= kGizmoSegments; ++segment) {
          const float t = static_cast<float>(segment) / static_cast<float>(kGizmoSegments) * (2.0f * xPI);
          const XVECTOR3 point = joint + (u * std::cos(t) + v * std::sin(t)) * radius;
          bool visible = false;
          const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
          if (visible && previousVisible) {
            drawList->AddLine(previous, screen, color, active ? 3.5f : 2.5f);
          }
          previous = screen;
          previousVisible = visible;
        }
      }
    }

    if (bones[static_cast<std::size_t>(childCapsule)].jointType == t850::PhysicsRagdollJointType::Fixed) {
      drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 21.0f), selectedColor, "fixed");
      continue;
    }

    const float coneLength = size * 0.75f;
    const float swing = (std::max)(0.0f, (std::min)(Deg2Rad(85.0f), bones[static_cast<std::size_t>(childCapsule)].swingLimitRadians));
    const float coneRadius = (std::min)(size * 1.25f, std::tan(swing) * coneLength);
    const XVECTOR3 coneCenter = joint + childTwist * coneLength;
    const XVECTOR3 coneU = childPlane;
    const XVECTOR3 coneV = Normalize3(Cross3(childTwist, coneU), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    ImVec2 previousCone;
    bool previousConeVisible = false;
    constexpr int kConeSegments = 48;
    for (int segment = 0; segment <= kConeSegments; ++segment) {
      const float t = static_cast<float>(segment) / static_cast<float>(kConeSegments) * (2.0f * xPI);
      const XVECTOR3 point = coneCenter + (coneU * std::cos(t) + coneV * std::sin(t)) * coneRadius;
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (visible && previousConeVisible) {
        drawList->AddLine(previousCone, screen, coneColor, 2.0f);
      }
      previousCone = screen;
      previousConeVisible = visible;
    }
    for (int spoke = 0; spoke < 4; ++spoke) {
      const float t = static_cast<float>(spoke) * (0.5f * xPI);
      const XVECTOR3 point = coneCenter + (coneU * std::cos(t) + coneV * std::sin(t)) * coneRadius;
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (visible) {
        drawList->AddLine(jointScreen, screen, coneColor, 1.4f);
      }
    }

    const float twist = (std::max)(0.0f, (std::min)(Deg2Rad(180.0f), bones[static_cast<std::size_t>(childCapsule)].twistLimitRadians));
    const float twistRadius = size * 0.38f;
    ImVec2 previousTwist;
    bool previousTwistVisible = false;
    constexpr int kTwistSegments = 32;
    for (int segment = 0; segment <= kTwistSegments; ++segment) {
      const float t = -twist + (2.0f * twist * static_cast<float>(segment) / static_cast<float>(kTwistSegments));
      const XVECTOR3 point = joint + (coneU * std::cos(t) + coneV * std::sin(t)) * twistRadius;
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (visible && previousTwistVisible) {
        drawList->AddLine(previousTwist, screen, twistColor, 2.5f);
      }
      previousTwist = screen;
      previousTwistVisible = visible;
    }
  }
}

void SandboxScene::ReleaseRagdollJointDebugBuffers() {
  if (m_ragdollJointVB) {
    m_ragdollJointVB->release();
    m_ragdollJointVB = nullptr;
  }
  if (m_ragdollJointIB) {
    m_ragdollJointIB->release();
    m_ragdollJointIB = nullptr;
  }
  m_ragdollJointVertexCapacity = 0;
  m_ragdollJointIndexCapacity = 0;
  m_ragdollJointIndexCount = 0;
}

bool SandboxScene::UploadRagdollJointDebugGeometry(const std::vector<float>& vertices,
                                                   const std::vector<unsigned int>& indices) {
  const unsigned vertexCount = static_cast<unsigned>(vertices.size() / 4);
  const unsigned indexCount = static_cast<unsigned>(indices.size());
  if (vertexCount == 0 || indexCount == 0) {
    m_ragdollJointIndexCount = 0;
    return false;
  }

  if (!m_ragdollJointVB || !m_ragdollJointIB ||
      vertexCount > m_ragdollJointVertexCapacity ||
      indexCount > m_ragdollJointIndexCapacity) {
    ReleaseRagdollJointDebugBuffers();

    m_ragdollJointVB = t850::LineRenderer::CreatePositionVB(vertices.data(), vertexCount, BufferUsage::DINAMIC);

    BufferDesc indexDesc;
    indexDesc.byteWidth = static_cast<int>(sizeof(unsigned int) * indexCount);
    indexDesc.usage = BufferUsage::DINAMIC;
    m_ragdollJointIB = t850::T8Device
        ? static_cast<IndexBuffer*>(t850::T8Device->CreateBuffer(BufferType::INDEX, indexDesc, const_cast<unsigned int*>(indices.data())))
        : nullptr;

    if (!m_ragdollJointVB || !m_ragdollJointIB) {
      ReleaseRagdollJointDebugBuffers();
      return false;
    }

    m_ragdollJointVertexCapacity = vertexCount;
    m_ragdollJointIndexCapacity = indexCount;
    m_ragdollJointIndexCount = indexCount;
    return true;
  }

  if (!t850::T8DeviceContext) {
    return false;
  }

  std::vector<float> paddedVertices = vertices;
  std::vector<unsigned int> paddedIndices = indices;
  paddedVertices.resize(static_cast<std::size_t>(m_ragdollJointVertexCapacity) * 4u, 0.0f);
  paddedIndices.resize(m_ragdollJointIndexCapacity, 0u);
  m_ragdollJointVB->UpdateFromBuffer(*t850::T8DeviceContext, paddedVertices.data());
  m_ragdollJointIB->UpdateFromBuffer(*t850::T8DeviceContext, paddedIndices.data());
  m_ragdollJointIndexCount = indexCount;
  return true;
}

void SandboxScene::DrawRagdollJointDebugOverlay() {
  if (!m_showPhysics || m_skeletonEditMode || !m_ragdollJointRenderer.IsReady()) {
    return;
  }

  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (bones.empty()) {
    return;
  }

  EnsureRagdollJointState();
  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  vertices.reserve(bones.size() * 32u);
  indices.reserve(bones.size() * 16u);

  auto appendLine = [&](const XVECTOR3& start, const XVECTOR3& end) {
    const unsigned base = static_cast<unsigned>(vertices.size() / 4);
    vertices.push_back(start.x);
    vertices.push_back(start.y);
    vertices.push_back(start.z);
    vertices.push_back(1.0f);
    vertices.push_back(end.x);
    vertices.push_back(end.y);
    vertices.push_back(end.z);
    vertices.push_back(1.0f);
    indices.push_back(base);
    indices.push_back(base + 1);
  };

  for (int childCapsule = 0; childCapsule < static_cast<int>(bones.size()); ++childCapsule) {
    if (GetRagdollEffectiveJointParentCapsule(childCapsule) < 0) {
      continue;
    }

    XVECTOR3 joint;
    XVECTOR3 parentCenter;
    XVECTOR3 childCenter;
    XVECTOR3 parentTwist;
    XVECTOR3 childTwist;
    XVECTOR3 childPlane;
    float size = 0.0f;
    if (!GetRagdollJointVisualFrame(childCapsule, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, size)) {
      continue;
    }

    appendLine(parentCenter, joint);
    appendLine(joint, childCenter);

    const float markerSize = (std::max)(0.01f, size * 0.10f);
    const XVECTOR3 normal = Normalize3(Cross3(childPlane, childTwist), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    appendLine(joint - childPlane * markerSize, joint + childPlane * markerSize);
    appendLine(joint - childTwist * markerSize, joint + childTwist * markerSize);
    appendLine(joint - normal * markerSize, joint + normal * markerSize);
  }

  if (!UploadRagdollJointDebugGeometry(vertices, indices)) {
    return;
  }

  XMATRIX44 identity;
  identity.Identity();
  m_ragdollJointRenderer.SetDepthTestEnabled(false);
  m_ragdollJointRenderer.SetViewport(g_pBaseDriver ? g_pBaseDriver->width : 1,
                                     g_pBaseDriver ? g_pBaseDriver->height : 1);
  m_ragdollJointRenderer.SetFarPlane(Cam.FPlane);
  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
  pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  m_ragdollJointRenderer.DrawLines(identity,
                                   VP,
                                   XVECTOR3(1.0f, 0.72f, 0.12f, 1.0f),
                                   m_ragdollJointVB,
                                   m_ragdollJointIB,
                                   m_ragdollJointIndexCount,
                                   sizeof(float) * 4,
                                   IndexBufferFormat::R32);
}

bool SandboxScene::GetRagdollEditGizmoFrame(int capsuleIndex,
                                            XVECTOR3& outCenter,
                                            std::array<XVECTOR3, 3>& outAxes,
                                            float& outSize) {
  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }

  outCenter = XVECTOR3(bodyWorld.m41, bodyWorld.m42, bodyWorld.m43, 1.0f);
  outAxes[0] = Normalize3(XVECTOR3(bodyWorld.m11, bodyWorld.m12, bodyWorld.m13, 0.0f),
                          XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  outAxes[1] = Normalize3(XVECTOR3(bodyWorld.m21, bodyWorld.m22, bodyWorld.m23, 0.0f),
                          XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  outAxes[2] = Normalize3(XVECTOR3(bodyWorld.m31, bodyWorld.m32, bodyWorld.m33, 0.0f),
                          XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));

  const float distanceToCamera = Length3(outCenter - Cam.Eye);
  const float minSize = (std::max)(0.03f, m_modelRadius * 0.08f);
  const float maxSize = (std::max)(minSize, m_modelRadius * 0.45f);
  outSize = (std::max)(minSize, (std::min)(maxSize, distanceToCamera * 0.10f));
  return true;
}

bool SandboxScene::BuildRagdollEditHandlePoints(int capsuleIndex, std::array<XVECTOR3, 7>& outPoints) {
  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size())) {
    return false;
  }

  const auto& shape = bones[static_cast<std::size_t>(capsuleIndex)].body.shape;
  if (shape.type != t850::PhysicsShapeType::Capsule) {
    return false;
  }

  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }

  const float radius = (std::max)(0.001f, shape.radius);
  const float extent = (std::max)(0.002f, shape.halfHeight + radius);
  outPoints[0] = t850::TransformPoint(XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f), bodyWorld);
  outPoints[1] = t850::TransformPoint(XVECTOR3(0.0f,  extent, 0.0f, 1.0f), bodyWorld);
  outPoints[2] = t850::TransformPoint(XVECTOR3(0.0f, -extent, 0.0f, 1.0f), bodyWorld);
  outPoints[3] = t850::TransformPoint(XVECTOR3( radius, 0.0f, 0.0f, 1.0f), bodyWorld);
  outPoints[4] = t850::TransformPoint(XVECTOR3(-radius, 0.0f, 0.0f, 1.0f), bodyWorld);
  outPoints[5] = t850::TransformPoint(XVECTOR3(0.0f, 0.0f,  radius, 1.0f), bodyWorld);
  outPoints[6] = t850::TransformPoint(XVECTOR3(0.0f, 0.0f, -radius, 1.0f), bodyWorld);
  return true;
}

bool SandboxScene::PickRagdollEditHandle(float mouseX, float mouseY, float thresholdPixels, int& outCapsuleIndex, int& outHandleIndex) {
  outCapsuleIndex = -1;
  outHandleIndex = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver || m_ragdollAnimationBinding.referencePose.bones.empty() ||
      m_ragdollEditSelectionMode != kRagdollSelectCapsules ||
      m_ragdollEditGizmoMode != kRagdollToolEditCapsule) {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  float bestDistanceSq = thresholdPixels * thresholdPixels;
  for (int capsuleIndex = 0; capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()); ++capsuleIndex) {
    if (IsRagdollCapsuleFrozen(capsuleIndex)) {
      continue;
    }
    std::array<XVECTOR3, 7> points;
    if (!BuildRagdollEditHandlePoints(capsuleIndex, points)) {
      continue;
    }
    for (int handleIndex = 0; handleIndex < static_cast<int>(points.size()); ++handleIndex) {
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(points[static_cast<std::size_t>(handleIndex)], VP, width, height, visible);
      if (!visible) {
        continue;
      }
      const float dx = screen.x - mouseX;
      const float dy = screen.y - mouseY;
      const float distanceSq = dx * dx + dy * dy;
      if (distanceSq < bestDistanceSq) {
        bestDistanceSq = distanceSq;
        outCapsuleIndex = capsuleIndex;
        outHandleIndex = handleIndex;
      }
    }
  }
  return outCapsuleIndex >= 0 && outHandleIndex >= 0;
}

bool SandboxScene::PickRagdollEditCapsule(float mouseX, float mouseY, float thresholdPixels, int& outCapsuleIndex) {
  outCapsuleIndex = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver || m_ragdollAnimationBinding.referencePose.bones.empty()) {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const ImVec2 mouse(mouseX, mouseY);
  float bestScore = FLT_MAX;

  for (int capsuleIndex = 0; capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size()); ++capsuleIndex) {
    std::array<XVECTOR3, 7> points;
    if (!BuildRagdollEditHandlePoints(capsuleIndex, points)) {
      continue;
    }

    bool centerVisible = false;
    bool topVisible = false;
    bool bottomVisible = false;
    const ImVec2 center = ProjectWorldToScreen(points[0], VP, width, height, centerVisible);
    const ImVec2 top = ProjectWorldToScreen(points[1], VP, width, height, topVisible);
    const ImVec2 bottom = ProjectWorldToScreen(points[2], VP, width, height, bottomVisible);
    if (!centerVisible || !topVisible || !bottomVisible) {
      continue;
    }

    float radiusPixels = thresholdPixels;
    for (int handleIndex = 3; handleIndex < 7; ++handleIndex) {
      bool sideVisible = false;
      const ImVec2 side = ProjectWorldToScreen(points[static_cast<std::size_t>(handleIndex)], VP, width, height, sideVisible);
      if (!sideVisible) {
        continue;
      }
      const float dx = side.x - center.x;
      const float dy = side.y - center.y;
      radiusPixels = (std::max)(radiusPixels, std::sqrt(dx * dx + dy * dy));
    }

    const float axisDistance = std::sqrt(DistancePointToSegmentSq(mouse, top, bottom));
    const float surfaceDistance = (std::max)(0.0f, axisDistance - radiusPixels);
    const float score = surfaceDistance + axisDistance * 0.001f;
    if (surfaceDistance <= thresholdPixels && score < bestScore) {
      bestScore = score;
      outCapsuleIndex = capsuleIndex;
    }
  }

  return outCapsuleIndex >= 0;
}

bool SandboxScene::PickRagdollEditTransformGizmo(float mouseX, float mouseY, int& outAxis) {
  outAxis = -1;
  if (!m_skeletonEditMode || !g_pBaseDriver || m_ragdollEditSelectedCapsule < 0 ||
      (m_ragdollEditGizmoMode != kRagdollToolMove && m_ragdollEditGizmoMode != kRagdollToolRotate) ||
      IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule)) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollEditGizmoFrame(m_ragdollEditSelectedCapsule, center, axes, size)) {
    return false;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const ImVec2 mouse(mouseX, mouseY);
  constexpr float kThresholdPixels = 12.0f;
  float bestDistanceSq = kThresholdPixels * kThresholdPixels;

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      bool startVisible = false;
      bool endVisible = false;
      const ImVec2 start = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * (size * 0.16f),
                                                VP, width, height, startVisible);
      const ImVec2 end = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * size,
                                              VP, width, height, endVisible);
      if (!startVisible || !endVisible) {
        continue;
      }
      const float distanceSq = DistancePointToSegmentSq(mouse, start, end);
      if (distanceSq < bestDistanceSq) {
        bestDistanceSq = distanceSq;
        outAxis = axisIndex;
      }
    }
  } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
    constexpr int kSegments = 64;
    const float radius = size * 0.78f;
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      const XVECTOR3 u = axes[static_cast<std::size_t>((axisIndex + 1) % 3)];
      const XVECTOR3 v = axes[static_cast<std::size_t>((axisIndex + 2) % 3)];
      ImVec2 previous;
      bool previousVisible = false;
      for (int segment = 0; segment <= kSegments; ++segment) {
        const float t = static_cast<float>(segment) / static_cast<float>(kSegments) * (2.0f * xPI);
        const XVECTOR3 point = center + (u * std::cos(t) + v * std::sin(t)) * radius;
        bool visible = false;
        const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
        if (visible && previousVisible) {
          const float distanceSq = DistancePointToSegmentSq(mouse, previous, screen);
          if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            outAxis = axisIndex;
          }
        }
        previous = screen;
        previousVisible = visible;
      }
    }
  }

  return outAxis >= 0;
}

bool SandboxScene::BeginRagdollEditTransformGizmoDrag(float mouseX, float mouseY) {
  int pickedAxis = -1;
  if (!PickRagdollEditTransformGizmo(mouseX, mouseY, pickedAxis)) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollEditGizmoFrame(m_ragdollEditSelectedCapsule, center, axes, size)) {
    return false;
  }
  (void)size;

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver ? g_pBaseDriver->width : 1);
  const int height = (std::max)(1, g_pBaseDriver ? g_pBaseDriver->height : 1);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const XVECTOR3 axis = axes[static_cast<std::size_t>(pickedAxis)];
  m_ragdollEditGizmoDragCenter = center;
  m_ragdollEditGizmoDragAxis = axis;

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    if (!ClosestRayAxisParameter(ray, m_ragdollEditGizmoDragCenter, m_ragdollEditGizmoDragAxis, m_ragdollEditGizmoLastParameter)) {
      return false;
    }
  } else if (m_ragdollEditGizmoMode == kRagdollToolRotate) {
    XVECTOR3 hitPoint;
    if (!RayPlaneIntersection(ray, m_ragdollEditGizmoDragCenter, m_ragdollEditGizmoDragAxis, hitPoint)) {
      return false;
    }
    m_ragdollEditGizmoLastVector = Normalize3(hitPoint - m_ragdollEditGizmoDragCenter,
                                              axes[static_cast<std::size_t>((pickedAxis + 1) % 3)]);
  } else {
    return false;
  }

  m_ragdollEditGizmoAxis = pickedAxis;
  m_ragdollEditGizmoDragging = true;
  m_ragdollEditHandleDragging = false;
  m_skeletonEditDragging = false;
  return true;
}

bool SandboxScene::DragRagdollEditTransformGizmo(float mouseX, float mouseY) {
  if (!m_ragdollEditGizmoDragging ||
      m_ragdollEditSelectedCapsule < 0 ||
      m_ragdollEditGizmoAxis < 0 ||
      !g_pBaseDriver) {
    return false;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollEditGizmoFrame(m_ragdollEditSelectedCapsule, center, axes, size)) {
    return false;
  }
  (void)center;
  (void)size;

  XMATRIX44 invVP;
  VP.Inverse(&invVP);
  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);
  const XVECTOR3 axis = Normalize3(m_ragdollEditGizmoDragAxis, axes[static_cast<std::size_t>(m_ragdollEditGizmoAxis)]);

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    float currentParameter = 0.0f;
    if (!ClosestRayAxisParameter(ray, m_ragdollEditGizmoDragCenter, axis, currentParameter)) {
      return false;
    }
    const float deltaParameter = currentParameter - m_ragdollEditGizmoLastParameter;
    m_ragdollEditGizmoLastParameter = currentParameter;
    if (std::fabs(deltaParameter) <= 0.000001f) {
      return true;
    }
    return MoveRagdollEditCapsuleByWorldDelta(m_ragdollEditSelectedCapsule, axis * deltaParameter, false);
  }

  if (m_ragdollEditGizmoMode != kRagdollToolRotate) {
    return false;
  }

  XVECTOR3 hitPoint;
  if (!RayPlaneIntersection(ray, m_ragdollEditGizmoDragCenter, axis, hitPoint)) {
    return false;
  }
  const XVECTOR3 currentVector = Normalize3(hitPoint - m_ragdollEditGizmoDragCenter, m_ragdollEditGizmoLastVector);
  const float dot = (std::max)(-1.0f, (std::min)(1.0f, Dot3(m_ragdollEditGizmoLastVector, currentVector)));
  const float signedAngle = std::atan2(Dot3(axis, Cross3(m_ragdollEditGizmoLastVector, currentVector)), dot);
  m_ragdollEditGizmoLastVector = currentVector;
  if (std::fabs(signedAngle) <= 0.000001f) {
    return true;
  }
  return RotateRagdollEditCapsuleWorld(m_ragdollEditSelectedCapsule, axis, signedAngle, false);
}

void SandboxScene::DrawRagdollEditTransformGizmo() {
  if (!m_skeletonEditMode ||
      m_ragdollEditSelectionMode != kRagdollSelectCapsules ||
      m_ragdollEditSelectedCapsule < 0 ||
      !g_pBaseDriver ||
      !ImGui::GetCurrentContext()) {
    return;
  }

  XVECTOR3 center;
  std::array<XVECTOR3, 3> axes;
  float size = 0.0f;
  if (!GetRagdollEditGizmoFrame(m_ragdollEditSelectedCapsule, center, axes, size)) {
    return;
  }

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);
  bool centerVisible = false;
  const ImVec2 centerScreen = ProjectWorldToScreen(center, VP, width, height, centerVisible);
  if (!centerVisible) {
    return;
  }

  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  const ImU32 originColor = IM_COL32(64, 160, 255, 255);
  const ImU32 originFill = IM_COL32(24, 96, 255, 180);
  drawList->AddCircle(centerScreen, 9.0f, originColor, 24, 2.5f);
  drawList->AddCircleFilled(centerScreen, 3.5f, originFill, 16);
  drawList->AddLine(ImVec2(centerScreen.x - 11.0f, centerScreen.y), ImVec2(centerScreen.x + 11.0f, centerScreen.y), originColor, 2.0f);
  drawList->AddLine(ImVec2(centerScreen.x, centerScreen.y - 11.0f), ImVec2(centerScreen.x, centerScreen.y + 11.0f), originColor, 2.0f);
  drawList->AddText(ImVec2(centerScreen.x + 11.0f, centerScreen.y + 5.0f), originColor, "origin");

  const auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (m_ragdollEditSelectedCapsule < static_cast<int>(bones.size())) {
    const auto& shape = bones[static_cast<std::size_t>(m_ragdollEditSelectedCapsule)].body.shape;
    const float capsuleExtent = shape.type == t850::PhysicsShapeType::Capsule
        ? (std::max)(0.002f, shape.halfHeight + shape.radius)
        : size * 0.75f;
    const float markerLength = (std::min)((std::max)(capsuleExtent, size * 0.45f), size * 1.15f);
    const XVECTOR3 localY = axes[1];
    const XVECTOR3 yPositive = center + localY * markerLength;
    const XVECTOR3 yNegative = center - localY * (markerLength * 0.72f);
    bool yPositiveVisible = false;
    bool yNegativeVisible = false;
    const ImVec2 yPositiveScreen = ProjectWorldToScreen(yPositive, VP, width, height, yPositiveVisible);
    const ImVec2 yNegativeScreen = ProjectWorldToScreen(yNegative, VP, width, height, yNegativeVisible);
    if (yPositiveVisible) {
      drawList->AddLine(centerScreen, yPositiveScreen, originColor, 3.0f);
      drawList->AddCircleFilled(yPositiveScreen, 5.0f, originColor, 16);
      drawList->AddText(ImVec2(yPositiveScreen.x + 7.0f, yPositiveScreen.y - 7.0f), originColor, "+Y top");
    }
    if (yNegativeVisible) {
      const ImU32 negativeColor = IM_COL32(80, 110, 180, 230);
      drawList->AddLine(centerScreen, yNegativeScreen, negativeColor, 1.8f);
      drawList->AddCircle(yNegativeScreen, 5.0f, negativeColor, 16, 2.0f);
      drawList->AddText(ImVec2(yNegativeScreen.x + 7.0f, yNegativeScreen.y - 7.0f), negativeColor, "-Y bottom");
    }
  }

  if (IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule)) {
    return;
  }

  if (m_ragdollEditGizmoMode == kRagdollToolMove) {
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
      const bool active = m_ragdollEditGizmoDragging && m_ragdollEditGizmoAxis == axisIndex;
      const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
      bool endVisible = false;
      const ImVec2 end = ProjectWorldToScreen(center + axes[static_cast<std::size_t>(axisIndex)] * size,
                                              VP, width, height, endVisible);
      if (!endVisible) {
        continue;
      }
      drawList->AddLine(centerScreen, end, color, active ? 4.0f : 3.0f);
      const float dx = end.x - centerScreen.x;
      const float dy = end.y - centerScreen.y;
      const float len = std::sqrt(dx * dx + dy * dy);
      if (len > 0.001f) {
        const float ux = dx / len;
        const float uy = dy / len;
        const ImVec2 perp(-uy, ux);
        const ImVec2 base(end.x - ux * 14.0f, end.y - uy * 14.0f);
        drawList->AddTriangleFilled(end,
                                    ImVec2(base.x + perp.x * 5.0f, base.y + perp.y * 5.0f),
                                    ImVec2(base.x - perp.x * 5.0f, base.y - perp.y * 5.0f),
                                    color);
      }
    }
    return;
  }
  if (m_ragdollEditGizmoMode != kRagdollToolRotate) {
    return;
  }

  constexpr int kSegments = 72;
  const float radius = size * 0.78f;
  for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
    const bool active = m_ragdollEditGizmoDragging && m_ragdollEditGizmoAxis == axisIndex;
    const ImU32 color = RagdollGizmoAxisColor(axisIndex, active);
    const XVECTOR3 u = axes[static_cast<std::size_t>((axisIndex + 1) % 3)];
    const XVECTOR3 v = axes[static_cast<std::size_t>((axisIndex + 2) % 3)];
    ImVec2 previous;
    bool previousVisible = false;
    for (int segment = 0; segment <= kSegments; ++segment) {
      const float t = static_cast<float>(segment) / static_cast<float>(kSegments) * (2.0f * xPI);
      const XVECTOR3 point = center + (u * std::cos(t) + v * std::sin(t)) * radius;
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (visible && previousVisible) {
        drawList->AddLine(previous, screen, color, active ? 3.5f : 2.5f);
      }
      previous = screen;
      previousVisible = visible;
    }
  }
}

bool SandboxScene::DragRagdollEditHandle(int capsuleIndex, int handleIndex, const XVECTOR3& worldDelta) {
  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (capsuleIndex < 0 || capsuleIndex >= static_cast<int>(bones.size()) ||
      capsuleIndex >= static_cast<int>(m_ragdollAnimationBinding.bodyFromBone.size()) ||
      handleIndex < 0 || handleIndex >= 7) {
    return false;
  }

  auto& bone = bones[static_cast<std::size_t>(capsuleIndex)];
  auto& local = m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)];
  auto& shape = bone.body.shape;
  if (IsRagdollCapsuleFrozen(capsuleIndex)) {
    return false;
  }
  if (shape.type != t850::PhysicsShapeType::Capsule) {
    return false;
  }

  XMATRIX44 bodyWorld;
  if (!GetCurrentRagdollEditCapsuleWorld(capsuleIndex, bodyWorld)) {
    return false;
  }

  XMATRIX44 inverseLocal;
  if (!InvertAffineNoExit(local, inverseLocal)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot drag capsule %d handle: local capsule frame is singular", capsuleIndex);
    return false;
  }
  XMATRIX44 boneWorld = inverseLocal * bodyWorld;
  XMATRIX44 inverseBoneWorld;
  if (!InvertAffineNoExit(boneWorld, inverseBoneWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot drag capsule %d handle: bone frame is singular", capsuleIndex);
    return false;
  }
  XMATRIX44 inverseBodyWorld;
  if (!InvertAffineNoExit(bodyWorld, inverseBodyWorld)) {
    T8_LOG_ERROR("[RagdollEdit] Cannot drag capsule %d handle: body frame is singular", capsuleIndex);
    return false;
  }

  auto translateCenterByWorld = [&](const XVECTOR3& deltaWorld) {
    const XVECTOR3 deltaBone = TransformVectorNoTranslation(deltaWorld, inverseBoneWorld);
    local.m41 += deltaBone.x;
    local.m42 += deltaBone.y;
    local.m43 += deltaBone.z;
  };

  bool rebuildRagdoll = false;
  if (handleIndex == 0) {
    translateCenterByWorld(worldDelta);
  } else {
    const XVECTOR3 deltaBody = TransformVectorNoTranslation(worldDelta, inverseBodyWorld);
    const float radius = (std::max)(0.001f, shape.radius);
    float extent = (std::max)(radius + 0.001f, shape.halfHeight + radius);

    if (handleIndex == 1 || handleIndex == 2) {
      const float signedDelta = handleIndex == 1 ? deltaBody.y : -deltaBody.y;
      const float centerShiftBodyY = (handleIndex == 1 ? deltaBody.y : deltaBody.y) * 0.5f;
      extent = (std::max)(radius + 0.001f, extent + signedDelta * 0.5f);
      const XVECTOR3 bodyAxisY(bodyWorld.m21, bodyWorld.m22, bodyWorld.m23, 0.0f);
      translateCenterByWorld(bodyAxisY * centerShiftBodyY);
      shape.halfHeight = (std::max)(0.001f, extent - radius);
      rebuildRagdoll = true;
    } else {
      float radiusDelta = 0.0f;
      if (handleIndex == 3) radiusDelta = deltaBody.x;
      else if (handleIndex == 4) radiusDelta = -deltaBody.x;
      else if (handleIndex == 5) radiusDelta = deltaBody.z;
      else if (handleIndex == 6) radiusDelta = -deltaBody.z;
      const float newRadius = (std::max)(0.001f, radius + radiusDelta);
      extent = (std::max)(newRadius + 0.001f, extent);
      shape.radius = newRadius;
      shape.halfHeight = (std::max)(0.001f, extent - newRadius);
      rebuildRagdoll = true;
    }
  }

  UpdateRagdollReferenceBodyFromLocal(capsuleIndex);
  m_ragdollEditDirty = true;
  return ApplyRagdollEditPose(rebuildRagdoll);
}

int SandboxScene::PickSkeletonEditBone(float mouseX, float mouseY, float thresholdPixels) const {
  if (!m_skeletonEditMode || m_skeletonEditCombined.empty() || !g_pBaseDriver) {
    return -1;
  }
  (void)thresholdPixels;

  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);

  XMATRIX44 viewProjection = VP;
  XMATRIX44 invVP;
  viewProjection.Inverse(&invVP);
  const t850::Ray ray = t850::ScreenPointToRay(mouseX, mouseY, 0, 0, width, height, invVP);

  static constexpr int kFaces[8][3] = {
      {0, 2, 4}, {0, 4, 3}, {0, 3, 5}, {0, 5, 2},
      {1, 4, 2}, {1, 3, 4}, {1, 5, 3}, {1, 2, 5}};

  float bestDistance = FLT_MAX;
  int bestBone = -1;
  for (int boneIndex = 0; boneIndex < static_cast<int>(m_skeletonEditCombined.size()); ++boneIndex) {
    std::array<XVECTOR3, 6> points{};
    if (!BuildSkeletonEditBoneOctahedron(boneIndex, 0.18f, points)) {
      continue;
    }
    for (const auto& face : kFaces) {
      float t = 0.0f;
      float u = 0.0f;
      float v = 0.0f;
      if (t850::RayIntersectsTriangle(ray, points[face[0]], points[face[1]], points[face[2]], t, u, v) &&
          t >= 0.0f && t < bestDistance) {
        bestDistance = t;
        bestBone = boneIndex;
      }
    }
  }
  return bestBone;
}

void SandboxScene::PickSkeletonEditBonesInScreenRect(float minX, float minY, float maxX, float maxY, std::vector<int>& outBones) const {
  outBones.clear();
  if (!m_skeletonEditMode || m_skeletonEditCombined.empty() || !g_pBaseDriver) {
    return;
  }

  if (minX > maxX) std::swap(minX, maxX);
  if (minY > maxY) std::swap(minY, maxY);
  const int width = (std::max)(1, g_pBaseDriver->width);
  const int height = (std::max)(1, g_pBaseDriver->height);

  for (int boneIndex = 0; boneIndex < static_cast<int>(m_skeletonEditCombined.size()); ++boneIndex) {
    if (FindRagdollCapsuleControllingBone(boneIndex) >= 0) {
      continue;
    }

    std::array<XVECTOR3, 6> points{};
    if (!BuildSkeletonEditBoneOctahedron(boneIndex, 0.18f, points)) {
      continue;
    }

    bool hasVisiblePoint = false;
    float boneMinX = FLT_MAX;
    float boneMinY = FLT_MAX;
    float boneMaxX = -FLT_MAX;
    float boneMaxY = -FLT_MAX;
    for (const XVECTOR3& point : points) {
      bool visible = false;
      const ImVec2 screen = ProjectWorldToScreen(point, VP, width, height, visible);
      if (!visible) {
        continue;
      }
      hasVisiblePoint = true;
      boneMinX = (std::min)(boneMinX, screen.x);
      boneMinY = (std::min)(boneMinY, screen.y);
      boneMaxX = (std::max)(boneMaxX, screen.x);
      boneMaxY = (std::max)(boneMaxY, screen.y);
    }

    if (!hasVisiblePoint) {
      continue;
    }

    if (!(boneMaxX < minX || boneMinX > maxX || boneMaxY < minY || boneMinY > maxY)) {
      outBones.push_back(boneIndex);
    }
  }
}

bool SandboxScene::HandleSkeletonEditInput(InputManager* input, bool imguiWantsMouse) {
  if (!m_skeletonEditMode || !input) {
    return false;
  }

  if (!input->PressedMouseButton(0)) {
    const bool finishedGizmoDrag = m_ragdollEditGizmoDragging;
    const bool finishedJointDrag = m_ragdollEditJointDragging;
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
    if (finishedGizmoDrag &&
        m_ragdollEditSelectedCapsule >= 0 &&
        m_ragdollEditSelectedCapsule < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size())) {
      ApplyRagdollEditPose(true);
    }
    if (finishedJointDrag &&
        m_ragdollEditSelectedJoint >= 0 &&
        m_ragdollEditSelectedJoint < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size())) {
      ApplyRagdollEditPose(true);
    }
  }

  const bool imguiWantsKeyboard = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
  if (!imguiWantsKeyboard) {
    auto cancelRagdollDrags = [&]() {
      m_ragdollEditHandleDragging = false;
      m_ragdollEditGizmoDragging = false;
      m_ragdollEditJointDragging = false;
      m_skeletonEditDragging = false;
      m_ragdollEditGizmoAxis = -1;
      m_ragdollEditJointAxis = -1;
    };
    if (input->PressedOnceKey(T800K_q)) {
      m_ragdollEditGizmoMode = kRagdollToolSelect;
      cancelRagdollDrags();
    }
    if (input->PressedOnceKey(T800K_r)) {
      m_ragdollEditGizmoMode = kRagdollToolEditCapsule;
      cancelRagdollDrags();
    }
    if (input->PressedOnceKey(T800K_w)) {
      m_ragdollEditGizmoMode = kRagdollToolMove;
      cancelRagdollDrags();
    }
    if (input->PressedOnceKey(T800K_e)) {
      m_ragdollEditGizmoMode = kRagdollToolRotate;
      cancelRagdollDrags();
    }
  }

  if (m_ragdollEditJointDragging && input->PressedMouseButton(0)) {
    DragRagdollEditJointGizmo(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY));
    return true;
  }
  if (m_ragdollEditGizmoDragging && input->PressedMouseButton(0)) {
    DragRagdollEditTransformGizmo(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY));
    return true;
  }
  if (imguiWantsMouse) {
    return false;
  }

  const bool ctrlDown = input->PressedKey(T800K_LCTRL) || input->PressedKey(T800K_RCTRL);
  const bool leftShiftDown = input->PressedKey(T800K_LSHIFT);
  const bool leftAltDown = input->PressedKey(T800K_LALT);
  const bool leftClick = input->PressedOnceMouseButton(0);
  const bool middleClick = input->PressedOnceMouseButton(1);
  const bool rightClick = input->PressedOnceMouseButton(2);

  if (m_ragdollBoneSelectionActive) {
    m_ragdollEditSelectionMode = kRagdollSelectBones;
    m_ragdollEditGizmoMode = kRagdollToolSelect;
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;

    if (leftClick) {
      m_ragdollBoneMarqueeDragging = true;
      m_ragdollBoneMarqueeStartX = static_cast<float>(input->mouseX);
      m_ragdollBoneMarqueeStartY = static_cast<float>(input->mouseY);
      m_ragdollBoneMarqueeCurrentX = m_ragdollBoneMarqueeStartX;
      m_ragdollBoneMarqueeCurrentY = m_ragdollBoneMarqueeStartY;
      return true;
    }

    if (m_ragdollBoneMarqueeDragging && input->PressedMouseButton(0)) {
      m_ragdollBoneMarqueeCurrentX = static_cast<float>(input->mouseX);
      m_ragdollBoneMarqueeCurrentY = static_cast<float>(input->mouseY);
      return true;
    }

    if (m_ragdollBoneMarqueeDragging && !input->PressedMouseButton(0)) {
      m_ragdollBoneMarqueeDragging = false;
      m_ragdollBoneMarqueeCurrentX = static_cast<float>(input->mouseX);
      m_ragdollBoneMarqueeCurrentY = static_cast<float>(input->mouseY);
      const float dxSelect = m_ragdollBoneMarqueeCurrentX - m_ragdollBoneMarqueeStartX;
      const float dySelect = m_ragdollBoneMarqueeCurrentY - m_ragdollBoneMarqueeStartY;

      m_ragdollBoneSelectionPending.clear();
      if (std::fabs(dxSelect) < 5.0f && std::fabs(dySelect) < 5.0f) {
        const int picked = PickSkeletonEditBone(m_ragdollBoneMarqueeCurrentX, m_ragdollBoneMarqueeCurrentY, 18.0f);
        if (picked >= 0 && FindRagdollCapsuleControllingBone(picked) < 0) {
          m_ragdollBoneSelectionPending.push_back(picked);
          m_skeletonEditSelectedBone = picked;
        }
      } else {
        PickSkeletonEditBonesInScreenRect(m_ragdollBoneMarqueeStartX,
                                          m_ragdollBoneMarqueeStartY,
                                          m_ragdollBoneMarqueeCurrentX,
                                          m_ragdollBoneMarqueeCurrentY,
                                          m_ragdollBoneSelectionPending);
        if (!m_ragdollBoneSelectionPending.empty()) {
          m_skeletonEditSelectedBone = m_ragdollBoneSelectionPending.front();
        }
      }
      m_ragdollEditSelectedUnassignedBone = -1;
      m_ragdollEditSelectedAffectedBone = -1;
      return true;
    }

    return true;
  }

  if (m_ragdollEditSelectionMode == kRagdollSelectCapsules &&
      leftAltDown && (leftClick || middleClick || rightClick) && m_ragdollEditSelectedCapsule >= 0) {
    int pickedCapsule = -1;
    if (PickRagdollEditCapsule(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f, pickedCapsule) &&
        pickedCapsule != m_ragdollEditSelectedCapsule) {
      if (middleClick) {
        SetRagdollCapsuleJointAtContact(m_ragdollEditSelectedCapsule, pickedCapsule);
      } else if (leftClick) {
        SetRagdollCapsuleJoint(m_ragdollEditSelectedCapsule, pickedCapsule);
      } else {
        ClearRagdollCapsuleJointBetween(m_ragdollEditSelectedCapsule, pickedCapsule);
      }
    }
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    return true;
  }

  if (m_ragdollEditSelectionMode == kRagdollSelectCapsules &&
      leftShiftDown && (leftClick || rightClick) && m_ragdollEditSelectedCapsule >= 0) {
    int pickedParentCapsule = -1;
    if (PickRagdollEditCapsule(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f, pickedParentCapsule) &&
        pickedParentCapsule != m_ragdollEditSelectedCapsule) {
      if (leftClick) {
        SetRagdollCapsuleParent(m_ragdollEditSelectedCapsule, pickedParentCapsule);
      } else if (m_ragdollEditSelectedCapsule < static_cast<int>(m_ragdollParentCapsules.size()) &&
                 m_ragdollParentCapsules[static_cast<std::size_t>(m_ragdollEditSelectedCapsule)] == pickedParentCapsule) {
        ClearRagdollCapsuleParent(m_ragdollEditSelectedCapsule);
      }
    }
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    return true;
  }

  if (ctrlDown && (leftClick || rightClick)) {
    const int picked = PickSkeletonEditBone(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f);
    if (picked >= 0) {
      m_skeletonEditSelectedBone = picked;
      if (m_ragdollEditSelectedCapsule >= 0 &&
          m_ragdollEditSelectedCapsule < static_cast<int>(m_ragdollAnimationBinding.referencePose.bones.size())) {
        if (leftClick) {
          AddControlledBoneToSelectedCapsule(picked);
        } else {
          RemoveControlledBoneFromSelectedCapsule(picked);
        }
      }
      m_skeletonEditDragging = false;
      m_ragdollEditHandleDragging = false;
      m_ragdollEditGizmoDragging = false;
      m_ragdollEditJointDragging = false;
    }
    return true;
  }

  if (leftClick) {
    if (m_ragdollEditSelectionMode == kRagdollSelectCapsules) {
      if (BeginRagdollEditTransformGizmoDrag(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY))) {
        return true;
      }

      int pickedCapsule = -1;
      int pickedHandle = -1;
      if (m_ragdollEditGizmoMode == kRagdollToolEditCapsule &&
          PickRagdollEditHandle(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f, pickedCapsule, pickedHandle)) {
        SelectRagdollEditCapsule(pickedCapsule, true);
        m_ragdollEditSelectedHandle = pickedHandle;
        m_ragdollEditHandleDragging = true;
        return true;
      }
      if (PickRagdollEditCapsule(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f, pickedCapsule)) {
        SelectRagdollEditCapsule(pickedCapsule, true);
        m_ragdollEditHandleDragging =
            m_ragdollEditGizmoMode == kRagdollToolEditCapsule && !IsRagdollCapsuleFrozen(pickedCapsule);
        return true;
      }
    } else if (m_ragdollEditSelectionMode == kRagdollSelectJoints) {
      if (BeginRagdollEditJointGizmoDrag(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY))) {
        return true;
      }
      int pickedJoint = -1;
      if (PickRagdollEditJoint(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f, pickedJoint)) {
        m_ragdollEditSelectedJoint = pickedJoint;
        SelectRagdollEditCapsule(pickedJoint, true);
        m_ragdollEditSelectedHandle = -1;
        return true;
      }
    } else {
      const int picked = PickSkeletonEditBone(static_cast<float>(input->mouseX), static_cast<float>(input->mouseY), 18.0f);
      if (picked >= 0) {
        m_skeletonEditSelectedBone = picked;
        m_skeletonEditDragging =
            m_ragdollEditGizmoMode == kRagdollToolMove && !m_ragdollAnimationBinding.referencePose.bones.empty();
        return true;
      }
    }
  }

  if (m_ragdollEditHandleDragging &&
      m_ragdollEditSelectionMode == kRagdollSelectCapsules &&
      m_ragdollEditGizmoMode == kRagdollToolEditCapsule &&
      m_ragdollEditSelectedCapsule >= 0 &&
      m_ragdollEditSelectedHandle >= 0 &&
      input->PressedMouseButton(0)) {
    const float dragScale = (std::max)(0.001f, m_orbitDist) * 0.0015f;
    XVECTOR3 worldDelta = Cam.Right * (static_cast<float>(input->xDelta) * dragScale);
    worldDelta += Cam.Up * (-static_cast<float>(input->yDelta) * dragScale);
    if (DragRagdollEditHandle(m_ragdollEditSelectedCapsule, m_ragdollEditSelectedHandle, worldDelta)) {
      return true;
    }
  }

  if (m_skeletonEditDragging && m_skeletonEditSelectedBone >= 0 && input->PressedMouseButton(0)) {
    XVECTOR3 worldPosition;
    if (GetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, worldPosition)) {
      const float dragScale = (std::max)(0.001f, m_orbitDist) * 0.0015f;
      worldPosition += Cam.Right * (static_cast<float>(input->xDelta) * dragScale);
      worldPosition += Cam.Up * (-static_cast<float>(input->yDelta) * dragScale);
      SetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, worldPosition);
      return true;
    }
  }

  return false;
}

void SandboxScene::DrawRagdollCapsuleEditPanel(t850::DevGuiContext& gui) {
  ImGui::Separator();
  gui.Text("Ragdoll Capsules");

  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  EnsureRagdollControlledBones();
  EnsureRagdollFreezeState();
  if (m_ragdollEditSavePath.empty()) {
    m_ragdollEditSavePath = BuildRagdollEditSavePath();
  }

  auto cancelRagdollDrags = [&]() {
    m_skeletonEditDragging = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditJointDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_ragdollEditJointAxis = -1;
  };

  ImGui::Text("Viewport selection:");
  ImGui::SameLine();
  if (ImGui::RadioButton("Capsules", m_ragdollEditSelectionMode == kRagdollSelectCapsules)) {
    m_ragdollEditSelectionMode = kRagdollSelectCapsules;
    cancelRagdollDrags();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Joints", m_ragdollEditSelectionMode == kRagdollSelectJoints)) {
    m_ragdollEditSelectionMode = kRagdollSelectJoints;
    cancelRagdollDrags();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Bones", m_ragdollEditSelectionMode == kRagdollSelectBones)) {
    m_ragdollEditSelectionMode = kRagdollSelectBones;
    cancelRagdollDrags();
  }

  ImGui::Text("Viewport tool:");
  ImGui::SameLine();
  if (ImGui::RadioButton("Select (Q)", m_ragdollEditGizmoMode == kRagdollToolSelect)) {
    m_ragdollEditGizmoMode = kRagdollToolSelect;
    cancelRagdollDrags();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Edit Capsule (R)", m_ragdollEditGizmoMode == kRagdollToolEditCapsule)) {
    m_ragdollEditGizmoMode = kRagdollToolEditCapsule;
    cancelRagdollDrags();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Move (W)", m_ragdollEditGizmoMode == kRagdollToolMove)) {
    m_ragdollEditGizmoMode = kRagdollToolMove;
    cancelRagdollDrags();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Rotate (E)", m_ragdollEditGizmoMode == kRagdollToolRotate)) {
    m_ragdollEditGizmoMode = kRagdollToolRotate;
    cancelRagdollDrags();
  }
  ImGui::Text("Active tool: %s", RagdollToolName(m_ragdollEditGizmoMode));
  ImGui::TextWrapped("Normal clicks affect only the selected target type: Capsules, Joints, or Bones. Select (Q) never transforms.");
  ImGui::TextWrapped("Ctrl+Left on bone: add bone to selected capsule. Ctrl+Right on bone: remove bone from selected capsule.");
  ImGui::TextWrapped("Left Shift+Left on capsule: set it as selected capsule's logical parent. Left Shift+Right on that parent: clear the parent link.");
  ImGui::TextWrapped("Left Alt+Left on capsule: set selected capsule as child of clicked parent and create the physical joint.");
  ImGui::TextWrapped("Left Alt+Middle on capsule: contact-snap the selected child to the clicked parent and place the joint at their meeting point. Left Alt+Right on either linked capsule: delete that joint.");

  if (gui.Button("Load Ragdoll Edits")) {
    if (LoadRagdollEditPose()) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button(m_ragdollEditDirty ? "Save Ragdoll Edits *" : "Save Ragdoll Edits")) {
    SaveRagdollEditPose();
  }

  if (gui.Button("Reset All Capsules")) {
    if (ResetRagdollEditPose()) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button("Reset Selected Capsule", m_ragdollEditSelectedCapsule >= 0 &&
      m_ragdollEditSelectedCapsule < static_cast<int>(bones.size()) &&
      !IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule))) {
    if (ResetSelectedRagdollCapsule()) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }

  if (!m_ragdollEditSavePath.empty()) {
    ImGui::TextWrapped("Ragdoll save path: %s", m_ragdollEditSavePath.c_str());
  }

  const bool selectedBoneValid = m_skeletonEditSelectedBone >= 0 &&
      m_skeletonEditSelectedBone < static_cast<int>(m_skeletonEditCombined.size());
  const int selectedBoneCapsule = selectedBoneValid ? FindRagdollCapsuleControllingBone(m_skeletonEditSelectedBone) : -1;
  const int selectedBonePrimaryCapsule = selectedBoneValid ? FindRagdollCapsuleForBone(m_skeletonEditSelectedBone) : -1;
  const bool canCreateCapsule = selectedBoneValid && selectedBoneCapsule < 0 && selectedBonePrimaryCapsule < 0;
  const bool canDeleteCapsule = m_ragdollEditSelectedCapsule >= 0 &&
      m_ragdollEditSelectedCapsule < static_cast<int>(bones.size()) &&
      !IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule);

  if (selectedBoneValid) {
    if (selectedBoneCapsule >= 0) {
      ImGui::Text("Selected bone %d is controlled by capsule %d.", m_skeletonEditSelectedBone, selectedBoneCapsule);
    } else if (selectedBonePrimaryCapsule >= 0) {
      ImGui::Text("Selected bone %d already owns capsule %d but is not assigned to it.", m_skeletonEditSelectedBone, selectedBonePrimaryCapsule);
    } else {
      ImGui::Text("Selected bone %d has no capsule assignment.", m_skeletonEditSelectedBone);
    }
  } else {
    gui.Text("Select a bone to create a capsule assignment.");
  }
  if (gui.Button("Create Capsule", canCreateCapsule)) {
    if (CreateRagdollCapsuleForBone(m_skeletonEditSelectedBone)) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button("Delete Selected Capsule", canDeleteCapsule)) {
    if (DeleteSelectedRagdollCapsule()) {
      m_ragdollEditTopologyChangedThisFrame = true;
      return;
    }
  }
  ImGui::SameLine();
  if (gui.Button("Clear All Capsules", !bones.empty())) {
    m_ragdollClearRequested = true;
    m_ragdollEditSelectedCapsule = -1;
    m_ragdollEditSelectedJoint = -1;
    m_ragdollEditSelectedHandle = -1;
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
    m_ragdollEditHandleDragging = false;
    m_ragdollEditGizmoDragging = false;
    m_ragdollEditGizmoAxis = -1;
    m_skeletonEditDragging = false;
    m_driveRagdollFromAnimation = false;
    m_ragdollPhysicsDriven = false;
    m_showPhysics = false;
    m_ragdollEditDirty = true;
    m_ragdollEditTopologyChangedThisFrame = true;
    return;
  }

  if (m_ragdollClearRequested) {
    gui.Text("Clearing ragdoll capsules...");
    return;
  }

  if (bones.empty() || bones.size() != m_ragdollAnimationBinding.bodyFromBone.size()) {
    gui.Text("No editable ragdoll capsules are attached to this model.");
    return;
  }
  EnsureRagdollParentCapsules();

  if (m_ragdollEditSelectedCapsule < 0 ||
      m_ragdollEditSelectedCapsule >= static_cast<int>(bones.size())) {
    SelectRagdollEditCapsule(0, true);
  }

  std::vector<std::string> capsuleOptions;
  capsuleOptions.reserve(bones.size());
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const auto& bone = bones[i];
    capsuleOptions.push_back(std::to_string(i) + ": " + std::to_string(bone.body.boneIndex) + " " + bone.body.debugName);
  }

  t850::SelectorDesc capsuleSelector;
  capsuleSelector.name = "ragdoll_edit_capsule";
  capsuleSelector.label = "Capsule";
  int selectedCapsule = m_ragdollEditSelectedCapsule;
  if (gui.Combo(capsuleSelector, selectedCapsule, &capsuleOptions)) {
    SelectRagdollEditCapsule(selectedCapsule, true);
  }

  if (m_ragdollEditSelectedCapsule < 0 ||
      m_ragdollEditSelectedCapsule >= static_cast<int>(bones.size())) {
    return;
  }

  const int capsuleIndex = m_ragdollEditSelectedCapsule;
  auto& bone = bones[static_cast<std::size_t>(capsuleIndex)];
  auto& local = m_ragdollAnimationBinding.bodyFromBone[static_cast<std::size_t>(capsuleIndex)];
  auto& shape = bone.body.shape;
  if (shape.type != t850::PhysicsShapeType::Capsule) {
    gui.Text("Selected ragdoll body is not a capsule.");
    return;
  }
  const bool capsuleFrozen = IsRagdollCapsuleFrozen(capsuleIndex);

  ImGui::PushID(capsuleIndex);
  if (capsuleFrozen && m_ragdollEditRenamingCapsule == capsuleIndex) {
    m_ragdollEditRenamingCapsule = -1;
    m_ragdollEditRenameFocusPending = false;
  }
  if (m_ragdollEditRenamingCapsule == capsuleIndex) {
    if (m_ragdollEditRenameFocusPending) {
      ImGui::SetKeyboardFocusHere();
      m_ragdollEditRenameFocusPending = false;
    }
    ImGui::SetNextItemWidth(260.0f);
    const bool submitted = ImGui::InputText("Capsule name", m_ragdollEditNameBuffer.data(),
                                            m_ragdollEditNameBuffer.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
      std::string newName(m_ragdollEditNameBuffer.data());
      if (newName.empty()) {
        newName = "capsule_" + std::to_string(capsuleIndex);
      }
      if (newName != bone.body.debugName) {
        bone.body.debugName = std::move(newName);
        m_ragdollEditDirty = true;
      }
      m_ragdollEditRenamingCapsule = -1;
    }
  } else {
    ImGui::Text("Capsule name: %s", bone.body.debugName.c_str());
    if (!capsuleFrozen && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
      std::fill(m_ragdollEditNameBuffer.begin(), m_ragdollEditNameBuffer.end(), '\0');
      std::snprintf(m_ragdollEditNameBuffer.data(), m_ragdollEditNameBuffer.size(), "%s", bone.body.debugName.c_str());
      m_ragdollEditRenamingCapsule = capsuleIndex;
      m_ragdollEditRenameFocusPending = true;
    }
  }
  ImGui::Text("Bone: %d", bone.body.boneIndex);
  ImGui::Text("Viewport handle: %s", RagdollCapsuleHandleName(m_ragdollEditSelectedHandle));
  if (gui.Button(capsuleFrozen ? "Unfreeze Capsule" : "Freeze Capsule")) {
    SetRagdollCapsuleFrozen(capsuleIndex, !capsuleFrozen);
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(capsuleFrozen ? "Frozen" : "Editable");
  ImGui::Text("Flip capsule local axis:");
  ImGui::SameLine();
  if (gui.Button("Flip X", !capsuleFrozen)) {
    FlipRagdollEditCapsuleLocalAxis(capsuleIndex, 0);
  }
  ImGui::SameLine();
  if (gui.Button("Flip Y", !capsuleFrozen)) {
    FlipRagdollEditCapsuleLocalAxis(capsuleIndex, 1);
  }
  ImGui::SameLine();
  if (gui.Button("Flip Z", !capsuleFrozen)) {
    FlipRagdollEditCapsuleLocalAxis(capsuleIndex, 2);
  }
  DrawRagdollEditTransformGizmo();
  ImGui::TextWrapped("Capsule shortcuts: Q selects only, R edits capsule handles, W moves, E rotates. Ctrl+Left/Right edits affected bones, Left Shift+Left/Right edits the logical parent, Left Alt+Left/Right creates or removes the physical joint.");
  const int parentCapsule =
      capsuleIndex < static_cast<int>(m_ragdollParentCapsules.size())
          ? m_ragdollParentCapsules[static_cast<std::size_t>(capsuleIndex)]
          : -1;
  if (parentCapsule >= 0 && parentCapsule < static_cast<int>(bones.size())) {
    const auto& parentBone = bones[static_cast<std::size_t>(parentCapsule)];
    ImGui::Text("Parent capsule: %d %s", parentCapsule, parentBone.body.debugName.c_str());
    ImGui::SameLine();
    if (gui.Button("Clear Parent", !capsuleFrozen)) {
      ClearRagdollCapsuleParent(capsuleIndex);
    }
  } else {
    ImGui::Text("Parent capsule: None");
  }
  ImGui::Text("Children capsules:");
  bool hasChildCapsules = false;
  for (int childCapsule = 0; childCapsule < static_cast<int>(m_ragdollParentCapsules.size()); ++childCapsule) {
    if (m_ragdollParentCapsules[static_cast<std::size_t>(childCapsule)] != capsuleIndex ||
        childCapsule >= static_cast<int>(bones.size())) {
      continue;
    }
    hasChildCapsules = true;
    ImGui::Text("  %d %s", childCapsule, bones[static_cast<std::size_t>(childCapsule)].body.debugName.c_str());
  }
  if (!hasChildCapsules) {
    ImGui::Text("  None");
  }
  if (capsuleIndex < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size())) {
    auto& controlledBones = m_ragdollAnimationBinding.controlledBoneIndices[static_cast<std::size_t>(capsuleIndex)];
    RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
    const xF::xSkeleton* skeleton = skinned ? skinned->GetAnimController().GetAnimSkeleton() : nullptr;
    const int boneCount = skeleton
        ? (std::min)(static_cast<int>(skeleton->Bones.size()), static_cast<int>(m_skeletonEditCombined.size()))
        : 0;
    auto containsBone = [](const std::vector<int>& boneList, int boneIndex) {
      return std::find(boneList.begin(), boneList.end(), boneIndex) != boneList.end();
    };
    auto boneLabel = [&](int boneIndex) {
      std::string label = std::to_string(boneIndex) + ": ";
      label += BoneNameOrEmpty(skeleton, boneIndex);
      const int ownerCapsule = FindRagdollCapsuleForBone(boneIndex);
      if (ownerCapsule >= 0) {
        label += "  (capsule ";
        label += std::to_string(ownerCapsule);
        label += ")";
      }
      return label;
    };

    std::vector<int> unassignedBones;
    if (boneCount > 0) {
      unassignedBones.reserve(static_cast<std::size_t>(boneCount));
      for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        if (FindRagdollCapsuleControllingBone(boneIndex) < 0) {
          unassignedBones.push_back(boneIndex);
        }
      }
    }

    if (!containsBone(unassignedBones, m_ragdollEditSelectedUnassignedBone)) {
      m_ragdollEditSelectedUnassignedBone = -1;
    }
    if (!containsBone(controlledBones, m_ragdollEditSelectedAffectedBone)) {
      m_ragdollEditSelectedAffectedBone = -1;
    }
    m_ragdollBoneSelectionPending.erase(
        std::remove_if(m_ragdollBoneSelectionPending.begin(),
                       m_ragdollBoneSelectionPending.end(),
                       [&](int boneIndex) { return !containsBone(unassignedBones, boneIndex); }),
        m_ragdollBoneSelectionPending.end());
    if (capsuleFrozen && m_ragdollBoneSelectionActive) {
      m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
      m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
      cancelRagdollDrags();
      m_ragdollBoneSelectionActive = false;
      m_ragdollBoneMarqueeDragging = false;
      m_ragdollBoneSelectionPending.clear();
    }

    ImGui::Separator();
    ImGui::Text("Affected bone assignment");
    ImGui::TextWrapped("Red overlay shows this capsule's affected bones. Blue previews the single list selection. Select bones enables viewport rectangle selection; pending bones are magenta.");
    if (!skeleton || boneCount <= 0) {
      ImGui::Text("Skeleton bone data is unavailable.");
    } else {
      const float listHeight = (std::max)(140.0f, ImGui::GetTextLineHeightWithSpacing() * 9.0f);
      ImGui::Columns(3, "ragdoll_bone_assignment_columns", false);

      ImGui::Text("Unassigned bones (%zu)", unassignedBones.size());
      ImGui::BeginChild("unassigned_bones", ImVec2(0.0f, listHeight), true);
      for (int unassignedBone : unassignedBones) {
        const std::string label = boneLabel(unassignedBone);
        if (ImGui::Selectable(label.c_str(), m_ragdollEditSelectedUnassignedBone == unassignedBone)) {
          m_ragdollEditSelectedUnassignedBone = unassignedBone;
          m_ragdollEditSelectedAffectedBone = -1;
        }
      }
      ImGui::EndChild();

      ImGui::NextColumn();
      ImGui::Spacing();
      const bool canAddBone = !capsuleFrozen && !m_ragdollBoneSelectionActive && m_ragdollEditSelectedUnassignedBone >= 0;
      if (gui.Button("Add ->", canAddBone)) {
        const int boneToAdd = m_ragdollEditSelectedUnassignedBone;
        if (AddControlledBoneToSelectedCapsule(boneToAdd)) {
          m_ragdollEditSelectedAffectedBone = boneToAdd;
          m_ragdollEditSelectedUnassignedBone = -1;
        }
      }
      const bool canRemoveBone = !capsuleFrozen && m_ragdollEditSelectedAffectedBone >= 0;
      if (gui.Button("<- Remove", canRemoveBone)) {
        const int boneToRemove = m_ragdollEditSelectedAffectedBone;
        if (RemoveControlledBoneFromSelectedCapsule(boneToRemove)) {
          m_ragdollEditSelectedUnassignedBone = boneToRemove;
          m_ragdollEditSelectedAffectedBone = -1;
        }
      }
      ImGui::Spacing();
      if (!m_ragdollBoneSelectionActive) {
        if (gui.Button("Select bones", !capsuleFrozen)) {
          m_ragdollBoneSelectionPreviousSelectionMode = m_ragdollEditSelectionMode;
          m_ragdollBoneSelectionPreviousGizmoMode = m_ragdollEditGizmoMode;
          m_ragdollBoneSelectionActive = true;
          m_ragdollBoneMarqueeDragging = false;
          m_ragdollBoneSelectionPending.clear();
          m_ragdollEditSelectedUnassignedBone = -1;
          m_ragdollEditSelectedAffectedBone = -1;
          m_ragdollEditSelectionMode = kRagdollSelectBones;
          m_ragdollEditGizmoMode = kRagdollToolSelect;
          m_showSkeleton = true;
          cancelRagdollDrags();
        }
      } else {
        ImGui::Text("Viewport selected: %zu", m_ragdollBoneSelectionPending.size());
        const bool canAddSelectedBones = !capsuleFrozen && !m_ragdollBoneSelectionPending.empty();
        if (gui.Button("Add selected bones", canAddSelectedBones)) {
          int lastAddedBone = -1;
          const std::vector<int> bonesToAdd = m_ragdollBoneSelectionPending;
          for (int boneToAdd : bonesToAdd) {
            if (AddControlledBoneToSelectedCapsule(boneToAdd)) {
              lastAddedBone = boneToAdd;
            }
          }
          m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
          m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
          cancelRagdollDrags();
          m_ragdollBoneSelectionActive = false;
          m_ragdollBoneMarqueeDragging = false;
          m_ragdollBoneSelectionPending.clear();
          if (lastAddedBone >= 0) {
            m_ragdollEditSelectedAffectedBone = lastAddedBone;
          }
        }
        if (gui.Button("Cancel")) {
          m_ragdollEditSelectionMode = m_ragdollBoneSelectionPreviousSelectionMode;
          m_ragdollEditGizmoMode = m_ragdollBoneSelectionPreviousGizmoMode;
          cancelRagdollDrags();
          m_ragdollBoneSelectionActive = false;
          m_ragdollBoneMarqueeDragging = false;
          m_ragdollBoneSelectionPending.clear();
        }
        ImGui::TextWrapped("Drag in the viewport to marquee-select unassigned bones. Click empty space to clear the magenta selection.");
      }

      ImGui::NextColumn();
      ImGui::Text("Affected bones (%zu)", controlledBones.size());
      ImGui::BeginChild("affected_bones", ImVec2(0.0f, listHeight), true);
      for (int affectedBone : controlledBones) {
        const std::string label = boneLabel(affectedBone);
        if (ImGui::Selectable(label.c_str(), m_ragdollEditSelectedAffectedBone == affectedBone)) {
          m_ragdollEditSelectedAffectedBone = affectedBone;
          m_ragdollEditSelectedUnassignedBone = -1;
        }
      }
      ImGui::EndChild();
      ImGui::Columns(1);
    }
  }

  std::array<float, 3> translation = MatrixTranslation(local);
  std::array<float, 3> rotation = MatrixEulerDegreesXYZ(local);
  float translationValues[3] = {translation[0], translation[1], translation[2]};
  float rotationValues[3] = {rotation[0], rotation[1], rotation[2]};
  float radius = shape.radius;
  float totalLength = (shape.halfHeight + shape.radius) * 2.0f;
  float swingLimitDeg = Rad2Deg(bone.swingLimitRadians);
  float twistLimitDeg = Rad2Deg(bone.twistLimitRadians);

  bool transformChanged = false;
  bool rebuildRagdoll = false;
  const float moveStep = (std::max)(0.001f, m_modelRadius * 0.0005f);
  if (capsuleFrozen) {
    ImGui::BeginDisabled();
  }
  if (ImGui::DragFloat3("Local translate", translationValues, moveStep, 0.0f, 0.0f, "%.4f")) {
    transformChanged = true;
  }
  if (ImGui::DragFloat3("Local rotate XYZ", rotationValues, 0.25f, -180.0f, 180.0f, "%.2f deg")) {
    transformChanged = true;
  }
  if (ImGui::DragFloat("Capsule radius", &radius, moveStep, 0.001f, (std::max)(0.001f, m_modelRadius), "%.4f")) {
    rebuildRagdoll = true;
  }
  if (ImGui::DragFloat("Capsule total length", &totalLength, moveStep, 0.003f, (std::max)(0.003f, m_modelRadius * 4.0f), "%.4f")) {
    rebuildRagdoll = true;
  }
  if (ImGui::DragFloat("Swing limit", &swingLimitDeg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
    rebuildRagdoll = true;
  }
  if (ImGui::DragFloat("Twist limit", &twistLimitDeg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
    rebuildRagdoll = true;
  }
  if (capsuleFrozen) {
    ImGui::EndDisabled();
  }

  if (!capsuleFrozen && (transformChanged || rebuildRagdoll)) {
    translation = {translationValues[0], translationValues[1], translationValues[2]};
    rotation = {rotationValues[0], rotationValues[1], rotationValues[2]};
    local = MatrixFromTranslationEulerDegreesXYZ(translation, rotation);

    radius = (std::max)(0.001f, radius);
    totalLength = (std::max)(radius * 2.0f + 0.002f, totalLength);
    shape.radius = radius;
    shape.halfHeight = (std::max)(0.001f, totalLength * 0.5f - radius);
    bone.swingLimitRadians = Deg2Rad((std::max)(0.0f, (std::min)(180.0f, swingLimitDeg)));
    bone.twistLimitRadians = Deg2Rad((std::max)(0.0f, (std::min)(180.0f, twistLimitDeg)));
    UpdateRagdollReferenceBodyFromLocal(capsuleIndex);
    ApplyRagdollEditPose(rebuildRagdoll || transformChanged);
    m_ragdollEditDirty = true;
  }

  ImGui::TextWrapped("Viewport: W/E choose move/rotate gizmos. Drag colored axes to move the capsule in its local frame, or colored rings to rotate it. Shape handles still edit center, length, and radius; numeric translate/rotate are stored in bone-local space.");
  ImGui::PopID();
}

void SandboxScene::DrawRagdollJointEditPanel(t850::DevGuiContext& gui) {
  ImGui::Separator();
  gui.Text("Ragdoll Joints");
  ImGui::TextWrapped("Self collision: enabled between capsule bodies in the same ragdoll, including parent/child pairs. A single capsule body never collides with itself.");
  ImGui::TextWrapped("Joint properties: anchor, type, swing aperture, twist angle, and child constraint frame direction. W moves the selected joint anchor; E rotates the child frame around the anchor.");
  ImGui::TextWrapped("Joint shortcuts: Left Alt+Left-click another capsule makes the selected capsule the child, the clicked capsule the parent, and creates the physical joint. Left Alt+Middle-click also contact-snaps the child to the parent and anchors the joint at their meeting point. Left Alt+Right-click a linked capsule removes that joint.");

  auto& bones = m_ragdollAnimationBinding.referencePose.bones;
  if (bones.empty()) {
    gui.Text("No ragdoll capsules are available.");
    return;
  }

  EnsureRagdollJointState();
  EnsureRagdollFreezeState();
  std::vector<int> jointChildren;
  std::vector<std::string> jointOptions;
  jointChildren.reserve(bones.size());
  jointOptions.reserve(bones.size());
  for (int childCapsule = 0; childCapsule < static_cast<int>(bones.size()); ++childCapsule) {
    const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
    if (parentCapsule < 0 ||
        parentCapsule >= static_cast<int>(bones.size()) ||
        parentCapsule == childCapsule) {
      continue;
    }

    jointChildren.push_back(childCapsule);
    jointOptions.push_back(
        std::to_string(childCapsule) + " " + bones[static_cast<std::size_t>(childCapsule)].body.debugName +
        " <- " + std::to_string(parentCapsule) + " " + bones[static_cast<std::size_t>(parentCapsule)].body.debugName);
  }

  if (jointChildren.empty()) {
    ImGui::TextWrapped("No joints are assigned yet. Select a capsule, then Left Alt+Left-click another capsule to create one.");
    m_ragdollEditSelectedJoint = -1;
    return;
  }

  auto findJointOption = [&](int childCapsule) {
    for (int i = 0; i < static_cast<int>(jointChildren.size()); ++i) {
      if (jointChildren[static_cast<std::size_t>(i)] == childCapsule) {
        return i;
      }
    }
    return -1;
  };

  const int selectedCapsuleJointOption = findJointOption(m_ragdollEditSelectedCapsule);
  int optionIndex = selectedCapsuleJointOption >= 0
      ? selectedCapsuleJointOption
      : findJointOption(m_ragdollEditSelectedJoint);

  t850::SelectorDesc jointSelector;
  jointSelector.name = "ragdoll_edit_joint";
  jointSelector.label = "Joint";
  int selectedOption = optionIndex >= 0 ? optionIndex : 0;
  if (gui.Combo(jointSelector, selectedOption, &jointOptions) &&
      selectedOption >= 0 &&
      selectedOption < static_cast<int>(jointChildren.size())) {
    m_ragdollEditSelectedJoint = jointChildren[static_cast<std::size_t>(selectedOption)];
    SelectRagdollEditCapsule(m_ragdollEditSelectedJoint, true);
    optionIndex = selectedOption;
  }
  if (optionIndex < 0) {
    m_ragdollEditSelectedJoint = -1;
    DrawRagdollJointGizmos(true);
    ImGui::TextWrapped("Selected capsule has no joint. Choose an existing joint from the list to edit it.");
    return;
  }
  m_ragdollEditSelectedJoint = jointChildren[static_cast<std::size_t>(optionIndex)];

  DrawRagdollJointGizmos(true);

  const int childCapsule = m_ragdollEditSelectedJoint;
  if (childCapsule < 0 ||
      childCapsule >= static_cast<int>(bones.size()) ||
      childCapsule >= static_cast<int>(m_ragdollJointParentCapsules.size())) {
    return;
  }
  const int parentCapsule = GetRagdollEffectiveJointParentCapsule(childCapsule);
  if (parentCapsule < 0 || parentCapsule >= static_cast<int>(bones.size())) {
    return;
  }

  auto& childBone = bones[static_cast<std::size_t>(childCapsule)];
  const auto& parentBone = bones[static_cast<std::size_t>(parentCapsule)];
  const bool jointFrozen = IsRagdollJointFrozen(childCapsule);
  ImGui::Text("Parent capsule: %d %s", parentCapsule, parentBone.body.debugName.c_str());
  ImGui::Text("Child capsule: %d %s", childCapsule, childBone.body.debugName.c_str());
  ImGui::Text("Joint type: %s", RagdollJointTypeName(childBone.jointType));
  ImGui::Text("Joint gizmo: %s",
              m_ragdollEditGizmoMode == kRagdollToolMove ? "Move anchor (W)" :
              m_ragdollEditGizmoMode == kRagdollToolRotate ? "Rotate child frame (E)" :
              "Select only (Q)");
  ImGui::Text("Constraint axes: child +Y is twist direction; child +X is plane direction.");
  if (gui.Button(jointFrozen ? "Unfreeze Joint" : "Freeze Joint")) {
    SetRagdollJointFrozen(childCapsule, !jointFrozen);
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(jointFrozen ? "Frozen" : "Editable");
  ImGui::Text("Flip joint local axis:");
  ImGui::SameLine();
  if (gui.Button("Flip X", !jointFrozen)) {
    FlipRagdollEditJointLocalAxis(childCapsule, 0);
  }
  ImGui::SameLine();
  if (gui.Button("Flip Y", !jointFrozen)) {
    FlipRagdollEditJointLocalAxis(childCapsule, 1);
  }
  ImGui::SameLine();
  if (gui.Button("Flip Z", !jointFrozen)) {
    FlipRagdollEditJointLocalAxis(childCapsule, 2);
  }

  std::vector<std::string> jointTypeOptions = {"Swing/Twist", "Fixed"};
  int jointTypeOption = RagdollJointTypeToInt(childBone.jointType);
  t850::SelectorDesc jointTypeSelector;
  jointTypeSelector.name = "ragdoll_joint_type";
  jointTypeSelector.label = "Type";
  if (jointFrozen) {
    ImGui::BeginDisabled();
  }
  if (gui.Combo(jointTypeSelector, jointTypeOption, &jointTypeOptions) && !jointFrozen) {
    childBone.jointType = RagdollJointTypeFromInt(jointTypeOption);
    m_ragdollEditDirty = true;
    m_ragdollEditRebuildRequested = true;
  }

  XVECTOR3 joint;
  XVECTOR3 parentCenter;
  XVECTOR3 childCenter;
  XVECTOR3 parentTwist;
  XVECTOR3 childTwist;
  XVECTOR3 childPlane;
  float jointSize = 0.0f;
  if (GetRagdollJointVisualFrame(childCapsule, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, jointSize)) {
    ImGui::Text("Joint anchor: %.3f, %.3f, %.3f", joint.x, joint.y, joint.z);
    float anchorValues[3] = {joint.x, joint.y, joint.z};
    if (ImGui::DragFloat3("Joint anchor world", anchorValues, (std::max)(0.001f, m_modelRadius * 0.001f), 0.0f, 0.0f, "%.3f") &&
        !jointFrozen) {
      SetRagdollEditJointWorldPosition(childCapsule, XVECTOR3(anchorValues[0], anchorValues[1], anchorValues[2], 1.0f));
      m_ragdollEditRebuildRequested = true;
    }
  }

  float swingApertureDeg = Rad2Deg(childBone.swingLimitRadians);
  float twistAngleDeg = Rad2Deg(childBone.twistLimitRadians);
  bool changed = false;
  if (childBone.jointType == t850::PhysicsRagdollJointType::SwingTwist) {
    if (ImGui::DragFloat("Swing aperture cone", &swingApertureDeg, 0.5f, 0.0f, 180.0f, "%.1f deg") && !jointFrozen) {
      changed = true;
    }
    if (ImGui::DragFloat("Twist angle +/-", &twistAngleDeg, 0.5f, 0.0f, 180.0f, "%.1f deg") && !jointFrozen) {
      changed = true;
    }
  } else {
    ImGui::TextWrapped("Fixed joints weld translation and rotation; switch back to Swing/Twist to edit cone and twist limits.");
  }
  if (jointFrozen) {
    ImGui::EndDisabled();
  }

  if (!jointFrozen && changed) {
    childBone.swingLimitRadians = Deg2Rad((std::max)(0.0f, (std::min)(180.0f, swingApertureDeg)));
    childBone.twistLimitRadians = Deg2Rad((std::max)(0.0f, (std::min)(180.0f, twistAngleDeg)));
    m_ragdollEditDirty = true;
    m_ragdollEditRebuildRequested = true;
  }

  if (gui.Button("Select Child Capsule")) {
    SelectRagdollEditCapsule(childCapsule, true);
  }
  ImGui::SameLine();
  if (gui.Button("Select Parent Capsule")) {
    SelectRagdollEditCapsule(parentCapsule, true);
  }
  ImGui::SameLine();
  if (gui.Button("Delete Joint", !jointFrozen)) {
    ClearRagdollCapsuleJoint(childCapsule);
  }
}

void SandboxScene::DrawSkeletonEditPanel(t850::DevGuiContext& gui) {
  if (!gui.BeginSection("Skeleton Edit")) {
    return;
  }

  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (!skinned || !skinned->HasSkinData()) {
    gui.Text("Load a skinned model to edit its skeleton.");
    return;
  }

  gui.Text("Runtime Controls");
  if (gui.Button(m_showPhysics ? "F4 Physics Debug: On" : "F4 Physics Debug: Off")) {
    m_showPhysics = !m_showPhysics;
  }
  ImGui::SameLine();
  if (gui.Button("F5 Start Simulation", Meshes[0].HasPhysicsRagdoll())) {
    SwitchRagdollToPhysics();
  }
  if (gui.Button(m_skeletonEditMode ? "F6 Exit Edit Mode" : "F6 Enter Edit Mode")) {
    if (m_skeletonEditMode) ExitSkeletonEditMode();
    else EnterSkeletonEditMode();
  }
  ImGui::SameLine();
  if (gui.Button("F7 Reset Physics/Animation")) {
    ResetRagdollPhysicsAndAnimation();
  }
  ImGui::Separator();

  gui.Text(m_skeletonEditMode
      ? "Bind-pose edit mode is active. Bone clicks edit the animation skeleton; capsule handle clicks edit the physical ragdoll body. F6 exits."
      : "Enter mode to pause animation and move the model to bind pose. F6 toggles this mode.");

  if (!m_skeletonEditMode) {
    if (gui.Button("Enter Skeleton Edit Mode")) {
      EnterSkeletonEditMode();
    }
    return;
  }

  if (gui.Button("Exit Edit Mode")) {
    ExitSkeletonEditMode();
  }
  ImGui::SameLine();
  if (gui.Button("Reset Bind Pose")) {
    ResetSkeletonEditPose();
  }

  if (gui.Button("Load Saved Edits")) {
    LoadSkeletonEditPose();
  }
  ImGui::SameLine();
  if (gui.Button(m_skeletonEditDirty ? "Save Edits *" : "Save Edits")) {
    SaveSkeletonEditPose();
  }

  if (!m_skeletonEditSavePath.empty()) {
    ImGui::TextWrapped("Save path: %s", m_skeletonEditSavePath.c_str());
  }

  const xF::xSkeleton* skeleton = skinned->GetAnimController().GetAnimSkeleton();
  if (!skeleton || skeleton->Bones.empty() || m_skeletonEditCombined.empty()) {
    gui.Text("Skeleton data is unavailable.");
    return;
  }

  std::vector<std::string> boneOptions;
  const int boneCount = (std::min)(static_cast<int>(skeleton->Bones.size()), static_cast<int>(m_skeletonEditCombined.size()));
  boneOptions.reserve(static_cast<std::size_t>(boneCount));
  for (int i = 0; i < boneCount; ++i) {
    boneOptions.push_back(std::to_string(i) + ": " + skeleton->Bones[i].Name);
  }

  if (m_skeletonEditSelectedBone < 0 || m_skeletonEditSelectedBone >= boneCount) {
    m_skeletonEditSelectedBone = boneCount > 0 ? 0 : -1;
  }

  t850::SelectorDesc boneSelector;
  boneSelector.name = "skeleton_edit_bone";
  boneSelector.label = "Bone";
  int selectedBone = m_skeletonEditSelectedBone;
  if (gui.Combo(boneSelector, selectedBone, &boneOptions)) {
    m_skeletonEditSelectedBone = selectedBone;
  }

  if (m_skeletonEditSelectedBone < 0 || m_skeletonEditSelectedBone >= boneCount) {
    return;
  }

  ImGui::PushID(m_skeletonEditSelectedBone);
  XVECTOR3 worldPosition;
  if (GetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, worldPosition)) {
    float position[3] = {worldPosition.x, worldPosition.y, worldPosition.z};
    if (ImGui::DragFloat3("World position", position, (std::max)(0.001f, m_modelRadius * 0.002f), 0.0f, 0.0f, "%.3f")) {
      SetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, XVECTOR3(position[0], position[1], position[2], 1.0f));
    }
  }

  std::array<float, 3> scale = GetSkeletonEditBoneScale(m_skeletonEditSelectedBone);
  float scaleValues[3] = {scale[0], scale[1], scale[2]};
  if (ImGui::DragFloat3("Bone basis scale", scaleValues, 0.01f, 0.001f, 100.0f, "%.3f")) {
    SetSkeletonEditBoneScale(m_skeletonEditSelectedBone, {scaleValues[0], scaleValues[1], scaleValues[2]});
  }

  if (gui.Button("Reset Selected Bone")) {
    if (m_skeletonEditSelectedBone >= 0 &&
        m_skeletonEditSelectedBone < static_cast<int>(m_skeletonEditBindCombined.size()) &&
        m_skeletonEditSelectedBone < static_cast<int>(m_skeletonEditCombined.size())) {
      m_skeletonEditCombined[static_cast<std::size_t>(m_skeletonEditSelectedBone)] =
          m_skeletonEditBindCombined[static_cast<std::size_t>(m_skeletonEditSelectedBone)];
      m_skeletonEditDirty = true;
      ApplySkeletonEditPose();
    }
  }
  ImGui::SameLine();
  if (gui.Button("Frame Selected")) {
    if (GetSkeletonEditBoneWorldPosition(m_skeletonEditSelectedBone, worldPosition)) {
      m_orbitTarget = worldPosition;
      m_panOffset = XVECTOR3(0.0f, 0.0f, 0.0f);
      ComputeOrbitCamera();
      VP = Cam.VP;
    }
  }

  ImGui::TextWrapped("Viewport: click inside a gray octahedral bone volume to select it. Ctrl+Left-click adds a bone to the selected capsule; Ctrl+Right-click removes it.");
  ImGui::PopID();
  m_ragdollEditTopologyChangedThisFrame = false;
  DrawRagdollCapsuleEditPanel(gui);
  if (!m_ragdollEditTopologyChangedThisFrame) {
    DrawRagdollJointEditPanel(gui);
  }
}

void SandboxScene::DrawSkinningAuthoringPanel(t850::DevGuiContext& gui) {
  ImGui::SetNextWindowSize(ImVec2(460.0f, 680.0f), ImGuiCond_FirstUseEver);
  const bool begun = gui.BeginPanel("Skinning / Bones / Capsules");
  if (begun) {
    DrawSkeletonEditPanel(gui);
  }
  gui.EndPanel();
}

void SandboxScene::OnInput(InputManager* IManager) {
  // Skip mouse-driven camera when replay snapshot is active
  if (m_dumper.IsReplayActive()) return;

  float dx = static_cast<float>(IManager->xDelta);
  float dy = static_cast<float>(IManager->yDelta);

  bool imguiWantsMouse = false;
#ifndef OS_ANDROID
  imguiWantsMouse = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
#endif
  if (HandleSkeletonEditInput(IManager, imguiWantsMouse)) {
    return;
  }
  if (!imguiWantsMouse && IManager->PressedKey(T800K_LCTRL) && IManager->PressedMouseButton(0)) {
    if (AdjustSelectedDirectionalLightFromMouse(dx, dy)) return;
  }

  // Left click + drag: orbit rotate
  if (IManager->PressedMouseButton(0)) {
    m_orbitYaw   += dx * 0.005f;
    m_orbitPitch += dy * 0.005f;
    // Clamp pitch to avoid gimbal lock
    const float maxP = Deg2Rad(89.0f);
    if (m_orbitPitch >  maxP) m_orbitPitch =  maxP;
    if (m_orbitPitch < -maxP) m_orbitPitch = -maxP;
  }

  // Right click + drag: zoom (vertical drag)
  if (IManager->PressedMouseButton(2)) {
    m_orbitDist -= dy * 0.02f * m_modelRadius;
    if (m_orbitDist < m_modelRadius * 0.05f)
      m_orbitDist = m_modelRadius * 0.05f;
  }

  // Middle click + drag: pan
  if (IManager->PressedMouseButton(1)) {
    float panSpeed = m_orbitDist * 0.002f;
    // Pan along camera right and up axes
    m_panOffset += Cam.Right * (-dx * panSpeed);
    m_panOffset += Cam.Up    * ( dy * panSpeed);
  }

  // Mouse wheel: zoom
  if (IManager->scrollDelta != 0.0f) {
    m_orbitDist -= IManager->scrollDelta * 0.15f * m_modelRadius;
    if (m_orbitDist < m_modelRadius * 0.05f)
      m_orbitDist = m_modelRadius * 0.05f;
  }

  // Print camera position
  if (IManager->PressedOnceKey(T800K_k)) {
    T8_LOG_INFO("Orbit: target[%f,%f,%f] dist=%f yaw=%f pitch=%f",
      m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z,
      m_orbitDist, m_orbitYaw, m_orbitPitch);
  }

  // API switching
  if (IManager->PressedOnceKey(T800K_1))
    pFramework->ChangeAPI(GraphicsApi::D3D11);
  if (IManager->PressedOnceKey(T800K_2))
    pFramework->ChangeAPI(GraphicsApi::OPENGL);

  // Debug toggles
  if (IManager->PressedOnceKey(T800K_F2)) {
    m_showCullStats = !m_showCullStats;
    SceneProp.ShowCullingDebug = m_showCullStats;
  }
  if (IManager->PressedOnceKey(T800K_KP6) || IManager->PressedOnceKey(T800K_6)) {
    if (SceneProp.FrustumCullingToggleAllowed) {
      const bool requested = !SceneProp.FrustumCullingEnabled;
      if (!requested || !Meshes[0].pBase || static_cast<RenderMesh*>(Meshes[0].pBase)->EnsureCullingMetadata()) {
        SceneProp.FrustumCullingEnabled = requested;
      }
      T8_LOG_INFO("[CULLING] Frustum culling %s", SceneProp.FrustumCullingEnabled ? "enabled" : "disabled");
    } else {
      SceneProp.FrustumCullingEnabled = false;
      T8_LOG_INFO("[CULLING] Frustum culling locked off by startup policy");
    }
  }
  if (IManager->PressedOnceKey(T800K_F3))
    m_showAABBs = !m_showAABBs;
  if (IManager->PressedOnceKey(T800K_F4))
    m_showPhysics = !m_showPhysics;
  if (IManager->PressedOnceKey(T800K_F5))
    SwitchRagdollToPhysics();
  if (IManager->PressedOnceKey(T800K_F6)) {
    if (m_skeletonEditMode) ExitSkeletonEditMode();
    else EnterSkeletonEditMode();
  }
  if (IManager->PressedOnceKey(T800K_F7)) {
    ResetRagdollPhysicsAndAnimation();
  }

  // Arrow keys: step keyframes when in keyframe mode
  RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
  if (sk && sk->GetKeyframeMode()) {
    if (IManager->PressedOnceKey(T800K_RIGHT))
      sk->StepKeyframe(1);
    if (IManager->PressedOnceKey(T800K_LEFT))
      sk->StepKeyframe(-1);
  }
}

void SandboxScene::FitModelToView() {
  if (!Meshes[0].pBase) return;
  RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);

  // Compute the union of all geometry AABBs
  RenderMesh::AABB total;
  total.Reset();
  for (auto& mi : rm->Info) {
    total.Expand(mi.bounds.min.x, mi.bounds.min.y, mi.bounds.min.z);
    total.Expand(mi.bounds.max.x, mi.bounds.max.y, mi.bounds.max.z);
  }

  m_orbitTarget = XVECTOR3(
    (total.min.x + total.max.x) * 0.5f,
    (total.min.y + total.max.y) * 0.5f,
    (total.min.z + total.max.z) * 0.5f);
  m_panOffset = XVECTOR3(0, 0, 0);

  float ex = (total.max.x - total.min.x) * 0.5f;
  float ey = (total.max.y - total.min.y) * 0.5f;
  float ez = (total.max.z - total.min.z) * 0.5f;
  m_modelRadius = std::sqrt(ex*ex + ey*ey + ez*ez);
  if (m_modelRadius < 1e-4f) m_modelRadius = 1.0f;

  // Place camera at a distance that fits the bounding sphere in the FOV
  float halfFov = Cam.Fov * 0.5f;
  m_orbitDist = m_modelRadius / std::tan(halfFov);
  m_orbitYaw = g_config.orbitYawOverride ? g_config.orbitYaw : 0.0f;
  m_orbitPitch = 0.0f;

  // Adjust near/far planes to the model scale
  Cam.NPlane = m_modelRadius * 0.01f;
  Cam.FPlane = m_modelRadius * 100.0f;
  Cam.CreatePojection();

  T8_LOG_INFO("[SandboxScene] Model center=(%.2f,%.2f,%.2f) radius=%.2f dist=%.2f",
    m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z, m_modelRadius, m_orbitDist);
}

void SandboxScene::ComputeOrbitCamera() {
  // Spherical coordinates around the target
  XVECTOR3 target = m_orbitTarget + m_panOffset;
  float cy = std::cos(m_orbitYaw),   sy = std::sin(m_orbitYaw);
  float cp = std::cos(m_orbitPitch), sp = std::sin(m_orbitPitch);

  XVECTOR3 offset(sy * cp, sp, cy * cp);
  Cam.Eye = target + offset * m_orbitDist;
  // Clear velocity — orbit camera manages Eye directly; any residual
  // velocity from Input would be re-applied inside SetLookAt→Update,
  // corrupting the position we just computed.
  Cam.Velocity = XVECTOR3(0, 0, 0);
  Cam.SetLookAt(target);
}

void SandboxScene::EnsureLightRuntimeState() {
  if (m_lightAttachToCamera.size() < SceneProp.Lights.size())
    m_lightAttachToCamera.resize(SceneProp.Lights.size(), false);
  else if (m_lightAttachToCamera.size() > SceneProp.Lights.size())
    m_lightAttachToCamera.resize(SceneProp.Lights.size());

  if (SceneProp.Lights.empty()) m_selectedLightIndex = 0;
  else if (m_selectedLightIndex < 0 || m_selectedLightIndex >= (int)SceneProp.Lights.size()) m_selectedLightIndex = 0;
  SceneProp.ActiveLights = (std::max)(0, (std::min)(SceneProp.ActiveLights, (int)SceneProp.Lights.size()));
}

void SandboxScene::UpdateAttachedLights() {
  EnsureLightRuntimeState();
  Camera* attachCamera = ActiveCam ? ActiveCam : &Cam;
  for (int i = 0; i < (int)SceneProp.Lights.size(); ++i) {
    if (SceneProp.Lights[i].Type == LIGHT_POINT && m_lightAttachToCamera[i]) {
      SceneProp.Lights[i].Position = attachCamera->Eye;
    }
  }
}

void SandboxScene::SyncLightCameraFromDirectionalLight() {
  for (const Light& light : SceneProp.Lights) {
    if (light.Type != LIGHT_DIRECTIONAL) continue;
    XVECTOR3 direction = light.Direction;
    if (direction.Length() <= 0.0001f) return;
    direction.Normalize();
    LightCam.SetLookAt(LightCam.Eye + direction);
    return;
  }
}

bool SandboxScene::AdjustSelectedDirectionalLightFromMouse(float dx, float dy) {
  EnsureLightRuntimeState();
  if (SceneProp.Lights.empty()) return false;
  Light& light = SceneProp.Lights[m_selectedLightIndex];
  if (light.Type != LIGHT_DIRECTIONAL) return false;
  if (std::fabs(dx) < 0.001f && std::fabs(dy) < 0.001f) return true;

  XVECTOR3 direction = light.Direction;
  if (direction.Length() <= 0.0001f) direction = XVECTOR3(0.0f, -1.0f, 0.0f);
  direction.Normalize();

  const float sensitivity = 0.005f;
  direction += Cam.Right * (dx * sensitivity);
  direction += Cam.Up * (-dy * sensitivity);
  if (direction.Length() <= 0.0001f) return true;
  direction.Normalize();
  light.Direction = direction;
  SyncLightCameraFromDirectionalLight();
  return true;
}

void SandboxScene::DrawSelectedDirectionalLightArrow() {
  EnsureLightRuntimeState();
  if (!m_drawLightDirection) return;
  if (SceneProp.Lights.empty() || !m_lightArrowRenderer.IsReady() || !m_lightArrowVB || !m_lightArrowIB) return;

  const Light& light = SceneProp.Lights[m_selectedLightIndex];
  if (light.Type != LIGHT_DIRECTIONAL) return;

  XVECTOR3 direction = light.Direction;
  if (direction.Length() <= 0.0001f) return;
  direction.Normalize();

  XVECTOR3 origin = m_orbitTarget + m_panOffset;
  float arrowLength = (std::max)(1.0f, m_modelRadius * 0.45f);
  float headLength = arrowLength * 0.22f;
  float headWidth = arrowLength * 0.08f;
  XVECTOR3 tip = origin + direction * arrowLength;

  XVECTOR3 side;
  XVecCross(side, Cam.Up, direction);
  if (side.Length() <= 0.0001f) XVecCross(side, XVECTOR3(0.0f, 1.0f, 0.0f), direction);
  if (side.Length() <= 0.0001f) XVecCross(side, XVECTOR3(1.0f, 0.0f, 0.0f), direction);
  side.Normalize();

  XVECTOR3 up;
  XVecCross(up, direction, side);
  up.Normalize();

  XVECTOR3 headBase = tip - direction * headLength;
  XVECTOR3 points[10] = {
    origin, tip,
    tip, headBase + side * headWidth,
    tip, headBase - side * headWidth,
    tip, headBase + up * headWidth,
    tip, headBase - up * headWidth,
  };

  float verts[10 * 4];
  for (int i = 0; i < 10; ++i) {
    verts[i * 4 + 0] = points[i].x;
    verts[i * 4 + 1] = points[i].y;
    verts[i * 4 + 2] = points[i].z;
    verts[i * 4 + 3] = 1.0f;
  }

  m_lightArrowVB->UpdateFromBuffer(*t850::T8DeviceContext, verts);
  XMATRIX44 identity;
  identity.Identity();
  m_lightArrowRenderer.SetDepthTestEnabled(false);
  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
  pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  m_lightArrowRenderer.DrawLines(identity, Cam.VP, XVECTOR3(1.0f, 0.82f, 0.25f, 1.0f),
                                 m_lightArrowVB, m_lightArrowIB, m_lightArrowIndexCount, 16,
                                 IndexBufferFormat::R16);
}

void SandboxScene::CaptureSandboxProfileState(t850::SandboxProfileDesc& state) {
  state = t850::SandboxProfileDesc{};
  state.model = m_profileModelKey.empty() ? SandboxProfileModelKey(g_config.modelPath) : m_profileModelKey;

  auto addFloat = [&](const char* name, float value) {
    state.sliders.push_back({name, value});
  };
  auto addBool = [&](const char* name, bool value) {
    state.checkboxes.push_back({name, value});
  };
  auto addInt = [&](const char* name, int value) {
    state.selectors.push_back({name, value});
  };

  addFloat("exposure", SceneProp.Exposure);
  addFloat("bloom_factor", SceneProp.BloomFactor);
  addFloat("bloom_threshold", SceneProp.BloomThreshold);
  addFloat("tm_white_level", SceneProp.ToneMapWhiteLevel);
  addFloat("tm_adapt_tau", SceneProp.LuminanceTau);
  addFloat("pcf_radius", SceneProp.PCFScale);
  addFloat("pcf_samples", SceneProp.PCFSamples);
  addFloat("ssao_kernel_size", (float)SceneProp.SSAOKernel.KernelSize);
  addFloat("ssao_radius", SceneProp.SSAOKernel.Radius);
  addFloat("dof_aperture", SceneProp.Aperture);
  addFloat("dof_focal_length", SceneProp.FocalLength);
  addFloat("dof_max_coc", SceneProp.MaxCoc);
  addFloat("dof_far_samples", SceneProp.DOF_Far_Samples_squared);
  addFloat("dof_near_samples", SceneProp.DOF_Near_Samples_squared);
  addFloat("light_volume_steps", SceneProp.LightVolumeSteps);
  addFloat("godrays_factor", SceneProp.GodRaysFactor);
  addFloat("fov", ActiveCam ? Rad2Deg(ActiveCam->Fov) : Rad2Deg(Cam.Fov));
  addFloat("shadow_bias", SceneProp.ShadowBias);
  addFloat("shadow_min", SceneProp.ShadowMin);
  addFloat("env_factor", SceneProp.EnvFactor);
  addFloat("ibl_factor", SceneProp.IBLFactor);
  addFloat("material_emissive_intensity", SceneProp.MaterialEmissiveIntensity);
  addFloat("material_transmission_multiplier", SceneProp.MaterialTransmissionMultiplier);
  addFloat("material_refraction_strength", SceneProp.MaterialRefractionStrength);

  for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
    GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
    if (!kernel) continue;
    std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
    addFloat((prefix + "radius").c_str(), kernel->radius);
    addFloat((prefix + "sigma").c_str(), kernel->sigma);
    addInt((prefix + "kernel_size").c_str(), kernel->kernelSize);
  }

  if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) {
    addFloat("anim_speed", skinned->GetAnimSpeed());
    addInt("anim_select", skinned->GetCurrentAnimSet());
    addInt("anim_mode", skinned->GetKeyframeMode() ? 1 : 0);
    if (skinned->GetKeyframeMode())
      state.current_keyframe = skinned->GetCurrentKeyframe();
  } else {
    addFloat("anim_speed", 1.0f);
    addInt("anim_select", 0);
    addInt("anim_mode", 0);
  }

  addBool("shadow_toggle", SceneProp.ToogleShadow != 0);
  addBool("ssao_toggle", SceneProp.ToogleSSAO != 0);
  addBool("show_wireframe", m_showWireframe);
  addBool("show_skeleton", Meshes[0].GetSkinnedMesh() != nullptr && m_showSkeleton);
  addBool("show_physics", m_showPhysics);
  addBool("draw_direction", m_drawLightDirection);

  addInt("debug_render_target", m_debugRTSelection);
  addInt("cubemap", m_currentCubemapIndex);
  addInt("gauss_kernel_sample_count", 0);
  addInt("active_gauss_kernel", ChangeActiveGaussSelection);
  addInt("active_light", m_selectedLightIndex);

  EnsureLightRuntimeState();
  for (int lightIndex = 0; lightIndex < (int)SceneProp.Lights.size(); ++lightIndex) {
    const Light& light = SceneProp.Lights[lightIndex];
    t850::SandboxLightOverrideDesc lightState;
    lightState.index = lightIndex;
    lightState.position = ToArray(light.Position);
    lightState.direction = ToArray(light.Direction);
    lightState.color = ToArray(light.Color);
    lightState.diameter = light.radius * 2.0f;
    lightState.intensity = light.Intensity;
    lightState.attach_to_camera = light.Type == LIGHT_POINT && m_lightAttachToCamera[lightIndex];
    state.lights.push_back(lightState);
  }

  state.frustum_culling = SceneProp.FrustumCullingEnabled;
  state.show_culling_debug = m_showCullStats;

  t850::SandboxOrbitCameraDesc orbit;
  orbit.target = {m_orbitTarget.x, m_orbitTarget.y, m_orbitTarget.z};
  orbit.pan_offset = {m_panOffset.x, m_panOffset.y, m_panOffset.z};
  orbit.eye = {Cam.Eye.x, Cam.Eye.y, Cam.Eye.z};
  orbit.yaw = m_orbitYaw;
  orbit.pitch = m_orbitPitch;
  orbit.distance = m_orbitDist;
  state.orbit_camera = orbit;
}

void SandboxScene::ApplySandboxProfileState(const t850::SandboxProfileDesc& state) {
  for (const auto& value : state.sliders) {
    if (value.name == "exposure") SceneProp.Exposure = value.value;
    else if (value.name == "bloom_factor") SceneProp.BloomFactor = value.value;
    else if (value.name == "bloom_threshold") SceneProp.BloomThreshold = value.value;
    else if (value.name == "tm_white_level") SceneProp.ToneMapWhiteLevel = value.value;
    else if (value.name == "tm_adapt_tau") SceneProp.LuminanceTau = value.value;
    else if (value.name == "pcf_radius") SceneProp.PCFScale = value.value;
    else if (value.name == "pcf_samples") SceneProp.PCFSamples = value.value;
    else if (value.name == "ssao_kernel_size") { SceneProp.SSAOKernel.KernelSize = (int)value.value; SceneProp.SSAOKernel.Update(); }
    else if (value.name == "ssao_radius") SceneProp.SSAOKernel.Radius = value.value;
    else if (value.name == "dof_aperture") SceneProp.Aperture = value.value;
    else if (value.name == "dof_focal_length") SceneProp.FocalLength = value.value;
    else if (value.name == "dof_max_coc") SceneProp.MaxCoc = value.value;
    else if (value.name == "dof_far_samples") SceneProp.DOF_Far_Samples_squared = value.value;
    else if (value.name == "dof_near_samples") SceneProp.DOF_Near_Samples_squared = value.value;
    else if (value.name == "light_volume_steps") SceneProp.LightVolumeSteps = value.value;
    else if (value.name == "godrays_factor") SceneProp.GodRaysFactor = value.value;
    else if (value.name == "fov" && ActiveCam) { ActiveCam->SetFov(Deg2Rad(value.value)); VP = ActiveCam->VP; }
    else if (value.name == "light_intensity" && !SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = value.value;
    else if (value.name == "shadow_bias") SceneProp.ShadowBias = value.value;
    else if (value.name == "shadow_min") SceneProp.ShadowMin = value.value;
    else if (value.name == "env_factor") SceneProp.EnvFactor = value.value;
    else if (value.name == "ibl_factor") SceneProp.IBLFactor = value.value;
    else if (value.name == "material_emissive_intensity") SceneProp.MaterialEmissiveIntensity = value.value;
    else if (value.name == "material_transmission_multiplier") SceneProp.MaterialTransmissionMultiplier = value.value;
    else if (value.name == "material_refraction_strength") SceneProp.MaterialRefractionStrength = value.value;
    else if (value.name == "anim_speed") { if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) skinned->SetAnimSpeed(value.value); }

    for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
      if (value.name == prefix + "radius") { kernel->radius = value.value; kernel->Update(); }
      else if (value.name == prefix + "sigma") { kernel->sigma = value.value; kernel->Update(); }
    }
  }

  for (const auto& value : state.checkboxes) {
    if (value.name == "shadow_toggle") SceneProp.ToogleShadow = value.value ? 1 : 0;
    else if (value.name == "ssao_toggle") SceneProp.ToogleSSAO = value.value ? 1 : 0;
    else if (value.name == "show_wireframe") m_showWireframe = value.value;
    else if (value.name == "show_skeleton") m_showSkeleton = value.value && (Meshes[0].GetSkinnedMesh() != nullptr);
    else if (value.name == "show_physics") m_showPhysics = value.value;
    else if (value.name == "draw_direction") m_drawLightDirection = value.value;
  }

  for (const auto& value : state.selectors) {
    if (value.name == "debug_render_target") m_debugRTSelection = value.value;
    else if (value.name == "active_light") m_selectedLightIndex = value.value;
    else if (value.name == "cubemap") {
      const t850::SelectorDesc* cubemapDesc = FindSelectorDesc(m_guiSetup.descriptor.selectors, "cubemap");
      if (cubemapDesc && value.value >= 0 && value.value < (int)cubemapDesc->options.size()) {
        m_currentCubemapIndex = value.value;
        m_pendingCubemap = "sky/" + cubemapDesc->options[value.value];
      }
    }
    else if (value.name == "active_gauss_kernel") ChangeActiveGaussSelection = value.value;
    else if (value.name == "anim_select") {
      if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) {
        int guard = skinned->GetNumAnimSets() + 1;
        while (skinned->GetCurrentAnimSet() != value.value && guard-- > 0) skinned->NextAnimation();
      }
    }
    else if (value.name == "anim_mode") {
      if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) {
        bool keyframeMode = (value.value == 1);
        skinned->SetKeyframeMode(keyframeMode);
        if (keyframeMode) skinned->StepKeyframe(0);
      }
    }

    for (int kernelIndex = 0; kernelIndex < (int)SceneProp.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = SceneProp.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      std::string name = "gauss_" + std::to_string(kernelIndex) + "_kernel_size";
      if (value.name == name) { kernel->kernelSize = value.value; kernel->Update(); }
    }
  }

  EnsureLightRuntimeState();
  for (const auto& lightState : state.lights) {
    if (lightState.index < 0 || lightState.index >= (int)SceneProp.Lights.size()) continue;
    Light& light = SceneProp.Lights[lightState.index];
    if (lightState.position.has_value()) light.Position = FromArray(*lightState.position);
    if (lightState.direction.has_value()) {
      XVECTOR3 direction = FromArray(*lightState.direction);
      if (direction.Length() > 0.0001f) {
        direction.Normalize();
        light.Direction = direction;
      }
    }
    if (lightState.color.has_value()) light.Color = FromArray(*lightState.color);
    if (lightState.diameter.has_value()) light.radius = (std::max)(0.001f, *lightState.diameter * 0.5f);
    if (lightState.intensity.has_value()) light.Intensity = *lightState.intensity;
    if (lightState.attach_to_camera.has_value() && light.Type == LIGHT_POINT)
      m_lightAttachToCamera[lightState.index] = *lightState.attach_to_camera;
  }
  UpdateAttachedLights();
  SyncLightCameraFromDirectionalLight();

  if (state.frustum_culling.has_value()) {
    if (!SceneProp.FrustumCullingToggleAllowed) {
      SceneProp.FrustumCullingEnabled = false;
    } else if (g_config.cullingLoadMode == t850::Config::CullingLoadMode::FullOnLoad || !*state.frustum_culling) {
      SceneProp.FrustumCullingEnabled = *state.frustum_culling;
    }
  }
  if (state.show_culling_debug.has_value()) {
    m_showCullStats = *state.show_culling_debug;
    SceneProp.ShowCullingDebug = m_showCullStats;
  }
  if (state.orbit_camera.has_value()) {
    const auto& orbit = *state.orbit_camera;
    m_orbitTarget = XVECTOR3(orbit.target[0], orbit.target[1], orbit.target[2]);
    m_panOffset = XVECTOR3(orbit.pan_offset[0], orbit.pan_offset[1], orbit.pan_offset[2]);
    m_orbitYaw = orbit.yaw;
    m_orbitPitch = orbit.pitch;
    m_orbitDist = orbit.distance;
    Cam.Eye = XVECTOR3(orbit.eye[0], orbit.eye[1], orbit.eye[2]);
    ComputeOrbitCamera();
    VP = Cam.VP;
    UpdateAttachedLights();
  }
  if (state.current_keyframe.has_value()) {
    if (RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh()) {
      int targetKeyframe = *state.current_keyframe;
      int guard = skinned->GetTotalKeyframes() + 1;
      while (skinned->GetCurrentKeyframe() != targetKeyframe && guard-- > 0) {
        int direction = targetKeyframe > skinned->GetCurrentKeyframe() ? 1 : -1;
        skinned->StepKeyframe(direction);
      }
    }
  }
}

t850::SandboxProfileDesc SandboxScene::BuildSparseSandboxProfile(const t850::SandboxProfileDesc& current) const {
  t850::SandboxProfileDesc sparse;
  sparse.name = current.name;
  sparse.platform = current.platform;
  sparse.architecture = current.architecture;
  sparse.gpu_family = current.gpu_family;
  sparse.gpu_name_contains = current.gpu_name_contains;
  sparse.model = current.model;
  for (const auto& value : current.sliders) {
    const auto* baseline = FindFloatOverride(m_profileBaselineState.sliders, value.name);
    if (!baseline || !NearlyEqual(value.value, baseline->value)) sparse.sliders.push_back(value);
  }
  for (const auto& value : current.checkboxes) {
    const auto* baseline = FindBoolOverride(m_profileBaselineState.checkboxes, value.name);
    if (!baseline || value.value != baseline->value) sparse.checkboxes.push_back(value);
  }
  for (const auto& value : current.selectors) {
    const auto* baseline = FindIntOverride(m_profileBaselineState.selectors, value.name);
    if (!baseline || value.value != baseline->value) sparse.selectors.push_back(value);
  }
  for (const auto& value : current.lights) {
    t850::SandboxLightOverrideDesc lightSparse;
    lightSparse.index = value.index;
    const auto* baseline = FindLightOverride(m_profileBaselineState.lights, value.index);
    if (!baseline) {
      lightSparse = value;
    } else {
      if (value.position.has_value() && (!baseline->position.has_value() || !VecNearlyEqual(*value.position, *baseline->position)))
        lightSparse.position = value.position;
      if (value.direction.has_value() && (!baseline->direction.has_value() || !VecNearlyEqual(*value.direction, *baseline->direction)))
        lightSparse.direction = value.direction;
      if (value.color.has_value() && (!baseline->color.has_value() || !VecNearlyEqual(*value.color, *baseline->color)))
        lightSparse.color = value.color;
      if (value.diameter.has_value() && (!baseline->diameter.has_value() || !NearlyEqual(*value.diameter, *baseline->diameter)))
        lightSparse.diameter = value.diameter;
      if (value.intensity.has_value() && (!baseline->intensity.has_value() || !NearlyEqual(*value.intensity, *baseline->intensity)))
        lightSparse.intensity = value.intensity;
      if (value.attach_to_camera.has_value() && (!baseline->attach_to_camera.has_value() || *value.attach_to_camera != *baseline->attach_to_camera))
        lightSparse.attach_to_camera = value.attach_to_camera;
      if (value.attach_to_camera.has_value() && *value.attach_to_camera) {
        lightSparse.position.reset();
      }
    }
    if (lightSparse.position.has_value() || lightSparse.direction.has_value() || lightSparse.color.has_value() ||
        lightSparse.diameter.has_value() || lightSparse.intensity.has_value() || lightSparse.attach_to_camera.has_value()) {
      sparse.lights.push_back(lightSparse);
    }
  }
  if (current.frustum_culling != m_profileBaselineState.frustum_culling) sparse.frustum_culling = current.frustum_culling;
  if (current.show_culling_debug != m_profileBaselineState.show_culling_debug) sparse.show_culling_debug = current.show_culling_debug;
  if (current.current_keyframe != m_profileBaselineState.current_keyframe) sparse.current_keyframe = current.current_keyframe;
  if (current.orbit_camera.has_value() && m_profileBaselineState.orbit_camera.has_value()) {
    const auto& currentOrbit = *current.orbit_camera;
    const auto& baselineOrbit = *m_profileBaselineState.orbit_camera;
    if (!VecNearlyEqual(currentOrbit.target, baselineOrbit.target) ||
        !VecNearlyEqual(currentOrbit.pan_offset, baselineOrbit.pan_offset) ||
        !VecNearlyEqual(currentOrbit.eye, baselineOrbit.eye) ||
        !NearlyEqual(currentOrbit.yaw, baselineOrbit.yaw) ||
        !NearlyEqual(currentOrbit.pitch, baselineOrbit.pitch) ||
        !NearlyEqual(currentOrbit.distance, baselineOrbit.distance)) {
      sparse.orbit_camera = currentOrbit;
    }
  } else if (current.orbit_camera != m_profileBaselineState.orbit_camera) {
    sparse.orbit_camera = current.orbit_camera;
  }
  return sparse;
}

bool SandboxScene::SandboxProfileStatesEqual(const t850::SandboxProfileDesc& lhs, const t850::SandboxProfileDesc& rhs) const {
  return BuildSparseSandboxProfile(lhs).sliders == BuildSparseSandboxProfile(rhs).sliders &&
         BuildSparseSandboxProfile(lhs).checkboxes == BuildSparseSandboxProfile(rhs).checkboxes &&
         BuildSparseSandboxProfile(lhs).selectors == BuildSparseSandboxProfile(rhs).selectors &&
         BuildSparseSandboxProfile(lhs).lights == BuildSparseSandboxProfile(rhs).lights &&
         BuildSparseSandboxProfile(lhs).orbit_camera == BuildSparseSandboxProfile(rhs).orbit_camera &&
         BuildSparseSandboxProfile(lhs).frustum_culling == BuildSparseSandboxProfile(rhs).frustum_culling &&
         BuildSparseSandboxProfile(lhs).show_culling_debug == BuildSparseSandboxProfile(rhs).show_culling_debug &&
         BuildSparseSandboxProfile(lhs).current_keyframe == BuildSparseSandboxProfile(rhs).current_keyframe;
}

void SandboxScene::LoadSandboxProfile() {
  m_profileModelKey = SandboxProfileModelKey(g_config.modelPath);
  m_selectedProfileTargetIndex = t850::DefaultProfileTargetIndex();
  CaptureSandboxProfileState(m_profileBaselineState);
  m_profileSavedState = m_profileBaselineState;
  m_profileReady = true;
  m_profileDirty = false;

  const t850::SandboxProfileDesc* baseProfile = nullptr;
  const t850::SandboxProfileDesc* runtimeProfile = nullptr;
  int bestRuntimeScore = -1;
  for (const auto& profile : m_guiSetup.descriptor.profiles) {
    const bool modelSpecific = !profile.model.empty();
    const bool modelMatches = !modelSpecific || SandboxProfileModelKey(profile.model) == m_profileModelKey;
    if (!modelMatches) continue;

    const bool hasTarget = !profile.name.empty() || !profile.platform.empty() || !profile.architecture.empty() ||
                           !profile.gpu_family.empty() || !profile.gpu_name_contains.empty();
    if (!hasTarget && modelSpecific) {
      baseProfile = &profile;
      continue;
    }

    int score = t850::ScoreSceneProfileMatch(profile, m_profileModelKey);
    if (score > bestRuntimeScore) {
      bestRuntimeScore = score;
      runtimeProfile = &profile;
    }
  }

  if (baseProfile) ApplySandboxProfileState(*baseProfile);
  if (runtimeProfile && runtimeProfile != baseProfile) ApplySandboxProfileState(*runtimeProfile);
  CaptureSandboxProfileState(m_profileSavedState);

  const auto& runtime = t850::GetRuntimeProfileInfo();
  T8_LOG_INFO("[SandboxScene] Profile model='%s' runtime='%s' platform=%s arch=%s gpu='%s' family=%s base=%d runtime=%d",
              m_profileModelKey.c_str(), runtime.recommendedProfile.c_str(), runtime.platform.c_str(), runtime.architecture.c_str(),
              runtime.gpuName.c_str(), runtime.gpuFamily.c_str(), baseProfile ? 1 : 0, runtimeProfile ? 1 : 0);
}

void SandboxScene::SaveSandboxProfile() {
  if (!m_profileReady) return;

  t850::SandboxProfileDesc current;
  CaptureSandboxProfileState(current);
  t850::SandboxProfileDesc sparse = BuildSparseSandboxProfile(current);
  t850::ApplyProfileTarget(sparse, m_selectedProfileTargetIndex);
  sparse.model = m_profileModelKey;

  t850::SandboxProfileDesc target;
  t850::ApplyProfileTarget(target, m_selectedProfileTargetIndex);

  auto& profiles = m_guiSetup.descriptor.profiles;
  auto existing = std::find_if(profiles.begin(), profiles.end(), [&](const t850::SandboxProfileDesc& profile) {
    return SandboxProfileModelKey(profile.model) == m_profileModelKey && profile.name == target.name &&
           profile.platform == target.platform && profile.architecture == target.architecture &&
           profile.gpu_family == target.gpu_family && profile.gpu_name_contains == target.gpu_name_contains;
  });

  bool hasOverrides = !sparse.sliders.empty() || !sparse.checkboxes.empty() || !sparse.selectors.empty() ||
                      !sparse.lights.empty() ||
                      sparse.orbit_camera.has_value() || sparse.frustum_culling.has_value() ||
                      sparse.show_culling_debug.has_value() || sparse.current_keyframe.has_value();
  if (hasOverrides) {
    if (existing == profiles.end()) profiles.push_back(sparse);
    else *existing = sparse;
  } else if (existing != profiles.end()) {
    profiles.erase(existing);
  }

  if (t850::SaveSceneDescriptor("Scenes/SandboxScene.json", m_guiSetup.descriptor)) {
    m_profileSavedState = current;
    m_profileDirty = false;
    T8_LOG_INFO("[SandboxScene] Saved profile '%s' for model '%s'", target.name.empty() ? "pc/base" : target.name.c_str(), m_profileModelKey.c_str());
  }
}

void SandboxScene::OnDraw() {
  SceneProp.ShowCullingDebug = m_showCullStats;
  // FPS logging (every 120 frames)
  static int sFrameCount = 0;
  static float sAccumTime = 0.0f;
  sAccumTime += DtSecs;
  sFrameCount++;
  if (sFrameCount % 120 == 0) {
    float avgFps = (sAccumTime > 0.0f) ? (float)sFrameCount / sAccumTime : 0.0f;
    T8_LOG_INFO("[FPS] %.1f fps (avg over %d frames, dt=%.3f ms)",
                avgFps, sFrameCount, DtSecs * 1000.0f);
  }

  if (Meshes[0].pBase) {
    RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
    if (skinned && skinned->HasSkinData()) {
      UpdateSkeletonFromRagdollPhysics();
      skinned->UploadBoneTexture();
    }
  }

  // Execute the render graph (all passes through HDR Composition)
  m_renderGraph.Execute(
    pFramework->pVideoDriver,
    SceneProp,
    Meshes, 1,
    Quads,
    &Cam,
    &LightCam,
    nullptr,
    EnvMaps
  );

  // RT Dump via FrameDumper
  if (m_dumper.ShouldDump(DtSecs)) {
    t850::SnapshotSkinnedJson skinnedSnapshot;
    const t850::SnapshotSkinnedJson* skinnedSnapshotPtr = nullptr;
    if (Meshes[0].pBase) {
      RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[0].pBase);
      skinnedSnapshot = CaptureSkinnedSnapshot(skinned, m_showWireframe, m_showSkeleton);
      if (skinnedSnapshot.has_skin)
        skinnedSnapshotPtr = &skinnedSnapshot;
    }

    std::vector<t850::RTDumpEntry> rts = {
      {GBufferPass,           BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
      {GBufferPass,           BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {GBufferPass,           BaseDriver::COLOR2_ATTACHMENT, "GBuffer_PBR"},
      {GBufferPass,           BaseDriver::COLOR3_ATTACHMENT, "GBuffer_GeoNormal"},
      {GBufferPass,           BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Emissive"},
      {GBufferPass,           BaseDriver::COLOR5_ATTACHMENT, "GBuffer_Sheen"},
      {GBufferPass,           BaseDriver::COLOR6_ATTACHMENT, "GBuffer_SpecularOcclusion"},
      {GBufferPass,           BaseDriver::DEPTH_ATTACHMENT,  "GBuffer_Depth"},
      {DepthPass,             BaseDriver::DEPTH_ATTACHMENT,  "ShadowMap_Depth"},
      {ShadowAccumPass,       BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {DeferredPass,          BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {Extra16FPass,          BaseDriver::COLOR0_ATTACHMENT, "Extra16F"},
      {ExtraHelperPass,       BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"},
      {BloomAccumPass,        BaseDriver::COLOR0_ATTACHMENT, "Bloom"},
      {LuminanceMapPass,      BaseDriver::COLOR0_ATTACHMENT, "LuminanceMap"},
      {AdaptedLumCurrentPass, BaseDriver::COLOR0_ATTACHMENT, "AdaptedLumCurrent"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs,
                       nullptr, nullptr, skinnedSnapshotPtr);
    if (m_dumper.ShouldExit()) exit(0);
  }

  // Blit final HDR result to backbuffer
  int selected = ExtraHelperPass;
  int attachment = BaseDriver::COLOR0_ATTACHMENT;

  if (m_debugRTSelection > 0) {
    switch (m_debugRTSelection) {
    case 1:  selected = GBufferPass;     attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 2:  selected = GBufferPass;     attachment = BaseDriver::COLOR1_ATTACHMENT; break;
    case 3:  selected = GBufferPass;     attachment = BaseDriver::COLOR2_ATTACHMENT; break;
    case 4:  selected = GBufferPass;     attachment = BaseDriver::COLOR3_ATTACHMENT; break;
    case 5:  selected = GBufferPass;     attachment = BaseDriver::DEPTH_ATTACHMENT;  break;
    case 6:  selected = DepthPass;       attachment = BaseDriver::DEPTH_ATTACHMENT;  break;
    case 7:  selected = ShadowAccumPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 8:  selected = DeferredPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 9:  selected = Extra16FPass;    attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 10: selected = ExtraHelperPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 11: selected = BloomAccumPass;  attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 12: selected = LuminanceMapPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    case 13: selected = AdaptedLumCurrentPass; attachment = BaseDriver::COLOR0_ATTACHMENT; break;
    }
  }

  pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
  Quads[0].SetTexture(pFramework->pVideoDriver->GetRTTexture(selected, attachment), 0);
  ShaderKey finalKey(0);
  finalKey.setPass(PassType::FSQUAD_1_TEX);
  finalKey.bits |= ShaderKey::HAS_TEXCOORD0;
  Quads[0].SetGlobalKey(finalKey);
  Quads[0].Draw();
#ifdef OS_ANDROID
  if (auto* vkDriver = static_cast<VulkanDriver*>(pFramework->pVideoDriver)) {
    vkDriver->SetLatePresentSource(selected, attachment);
  }
#endif

  auto drawMeshDebugOverlays = [this]() {
    if (Meshes[0].pBase) {
      RenderSkinnedMesh* skinned = dynamic_cast<RenderSkinnedMesh*>(Meshes[0].pBase);
      if (skinned && skinned->HasSkinData()) {
        if (m_showWireframe) {
          // Bind GBuffer depth for shader-based depth comparison
          int gbufHandle = GBufferPass;
          if (gbufHandle >= 0 && gbufHandle < (int)pFramework->pVideoDriver->RTs.size()) {
            auto* gbufRT = pFramework->pVideoDriver->RTs[gbufHandle];
            skinned->SetWireframeDepthTex(gbufRT->pDepthTexture);
          }
          skinned->SetWireframeViewport(g_pBaseDriver->width, g_pBaseDriver->height);
          pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
          skinned->DrawWireframe();
        }
        if (m_showSkeleton) {
          pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
          pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
          const std::vector<int>* controlledBones = nullptr;
          std::vector<int> previewBones;
          const std::vector<int>* previewBoneList = nullptr;
          const std::vector<int>* pendingBoneList = nullptr;
          if (m_skeletonEditMode &&
              m_ragdollEditSelectedCapsule >= 0 &&
              m_ragdollEditSelectedCapsule < static_cast<int>(m_ragdollAnimationBinding.controlledBoneIndices.size())) {
            controlledBones = &m_ragdollAnimationBinding.controlledBoneIndices[static_cast<std::size_t>(m_ragdollEditSelectedCapsule)];
            if (m_ragdollEditSelectedUnassignedBone >= 0 &&
                FindRagdollCapsuleControllingBone(m_ragdollEditSelectedUnassignedBone) < 0) {
              previewBones.push_back(m_ragdollEditSelectedUnassignedBone);
              previewBoneList = &previewBones;
            }
            if (m_ragdollBoneSelectionActive && !m_ragdollBoneSelectionPending.empty()) {
              pendingBoneList = &m_ragdollBoneSelectionPending;
            }
          }
          int selectedSkeletonBone = m_skeletonEditMode ? m_skeletonEditSelectedBone : -1;
          if (m_ragdollBoneSelectionActive) {
            selectedSkeletonBone = -1;
          } else if (previewBoneList && selectedSkeletonBone == m_ragdollEditSelectedUnassignedBone) {
            selectedSkeletonBone = -1;
          }
          if (pendingBoneList &&
              std::find(pendingBoneList->begin(), pendingBoneList->end(), selectedSkeletonBone) != pendingBoneList->end()) {
            selectedSkeletonBone = -1;
          }
          skinned->DrawSkeleton(selectedSkeletonBone, controlledBones, previewBoneList, pendingBoneList);
        }
      } else if (m_showWireframe) {
        RenderMesh* mesh = static_cast<RenderMesh*>(Meshes[0].pBase);
        int gbufHandle = GBufferPass;
        if (gbufHandle >= 0 && gbufHandle < (int)pFramework->pVideoDriver->RTs.size()) {
          auto* gbufRT = pFramework->pVideoDriver->RTs[gbufHandle];
          mesh->SetWireframeDepthTex(gbufRT->pDepthTexture);
        }
        mesh->SetWireframeViewport(g_pBaseDriver->width, g_pBaseDriver->height);
        pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
        mesh->DrawWireframe();
      }
    }

    if (m_showPhysics) {
      t850::EngineContext* engineContext = GetEngineContext();
      if (!engineContext) engineContext = &t850::GetEngineContext();
      if (engineContext && engineContext->physics && m_physicsDebugRenderer.IsReady()) {
        m_physicsDebugRenderer.SetDepthTexture(nullptr);
        m_physicsDebugRenderer.SetDepthTestEnabled(false);
        m_physicsDebugRenderer.SetViewport(g_pBaseDriver->width, g_pBaseDriver->height);
        m_physicsDebugRenderer.SetFarPlane(Cam.FPlane);
        pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
        pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
        m_physicsDebugRenderer.Draw(*engineContext->physics, VP);
        DrawRagdollJointDebugOverlay();
      }
    }
  };

#ifdef OS_ANDROID
  if (m_showWireframe || m_showSkeleton || m_showPhysics) {
    if (auto* vkDriver = static_cast<VulkanDriver*>(pFramework->pVideoDriver)) {
      vkDriver->SetPrePresentOverlayCallback(drawMeshDebugOverlays);
    }
  }
#else
  drawMeshDebugOverlays();
#endif

  DrawSelectedDirectionalLightArrow();
  if (m_skeletonEditMode &&
      m_ragdollBoneSelectionActive &&
      m_ragdollBoneMarqueeDragging &&
      ImGui::GetCurrentContext()) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport) {
      ImDrawList* drawList = ImGui::GetBackgroundDrawList(viewport);
      const ImVec2 start(m_ragdollBoneMarqueeStartX, m_ragdollBoneMarqueeStartY);
      const ImVec2 current(m_ragdollBoneMarqueeCurrentX, m_ragdollBoneMarqueeCurrentY);
      drawList->AddRectFilled(start, current, IM_COL32(255, 0, 255, 36));
      drawList->AddRect(start, current, IM_COL32(255, 0, 255, 220), 0.0f, 0, 1.6f);
    }
  }
  if (m_skeletonEditMode &&
      m_ragdollEditSelectionMode == kRagdollSelectCapsules &&
      m_ragdollEditGizmoMode == kRagdollToolEditCapsule &&
      m_ragdollEditSelectedCapsule >= 0 &&
      !IsRagdollCapsuleFrozen(m_ragdollEditSelectedCapsule)) {
    std::array<XVECTOR3, 7> capsuleHandles;
    if (BuildRagdollEditHandlePoints(m_ragdollEditSelectedCapsule, capsuleHandles)) {
      pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
      pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
      const float baseRadius = (std::max)(0.01f, m_modelRadius * 0.014f);
      for (int handleIndex = 0; handleIndex < static_cast<int>(capsuleHandles.size()); ++handleIndex) {
        const float radius = handleIndex == m_ragdollEditSelectedHandle ? baseRadius * 1.45f : baseRadius;
        m_debugSphere.Draw(VP, capsuleHandles[static_cast<std::size_t>(handleIndex)], radius);
      }
      pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    }
  }
  // Debug: draw wireframe AABBs for visible meshes
  if (m_showAABBs && Meshes[0].pBase) {
    RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);
    XVECTOR3 frustumPlanes[6];
    RenderMesh::ExtractFrustumPlanes(Cam.VP, frustumPlanes);

    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    for (std::size_t i = 0; i < rm->Info.size(); i++) {
      RenderMesh::AABB& box = rm->Info[i].bounds;
      if (!RenderMesh::AABBInsideFrustum(box, rm->transform, frustumPlanes))
        continue;
      XVECTOR3 center((box.min.x+box.max.x)*0.5f, (box.min.y+box.max.y)*0.5f, (box.min.z+box.max.z)*0.5f);
      float ex = (box.max.x-box.min.x)*0.5f;
      float ey = (box.max.y-box.min.y)*0.5f;
      float ez = (box.max.z-box.min.z)*0.5f;
      float radius = std::sqrt(ex*ex + ey*ey + ez*ez);
      m_debugSphere.Draw(VP, center, radius);
    }
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  }

  // Debug: on-screen cull stats
  if (m_showCullStats && Meshes[0].pBase) {
    RenderMesh* rm = static_cast<RenderMesh*>(Meshes[0].pBase);
    int w = g_pBaseDriver->width;
    int h = g_pBaseDriver->height;

    pFramework->pVideoDriver->SetBlendState(BaseDriver::ALPHA_BLEND);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);

    char buf[256];
    XVECTOR3 yellow(1.0f, 1.0f, 0.2f);
    XVECTOR3 gray(0.7f, 0.7f, 0.7f);
    const float statScale = 0.56f;
    const float lineHeight = 34.0f * statScale * ((float)h / 720.0f);
    const float bottomMargin = 26.0f * ((float)h / 720.0f);
    float y = (float)h - bottomMargin - lineHeight * 4.0f;
    auto drawCenteredStat = [&](const XVECTOR3& color, const char* text) {
      float textW = m_debugText.MeasurePixel(text, w, h) * statScale;
      float x = ((float)w - textW) * 0.5f;
      m_debugText.DrawPixelScaled(x, y, statScale, statScale, w, h, color, text);
      y += lineHeight;
    };

    snprintf(buf, sizeof(buf), "Meshes: %d/%d  culled %d",
            rm->m_visibleMeshes, rm->m_totalMeshes, rm->m_culledMeshes);
    drawCenteredStat(yellow, buf);

    snprintf(buf, sizeof(buf), "Subsets: %d/%d  culled %d  drawn %d",
            rm->m_visibleSubsets, rm->m_totalSubsets, rm->m_culledSubsets, rm->m_drawnSubsets);
    drawCenteredStat(yellow, buf);

    snprintf(buf, sizeof(buf), "Clusters: %d/%d  culled %d  drawn %d",
            rm->m_visibleClusters, rm->m_totalClusters, rm->m_culledClusters, rm->m_drawnClusters);
    drawCenteredStat(yellow, buf);

        snprintf(buf, sizeof(buf), "GBuffer indices: %llu/%llu  6/KP6: culling %s  F2: stats  F3: AABBs  K: cam pos",
          rm->m_drawnIndices, rm->m_totalIndices,
          SceneProp.FrustumCullingEnabled ? "ON" : "OFF");
    drawCenteredStat(gray, buf);

    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::DEPTH_DEFAULT);
  }
}

void SandboxScene::PopulateGUI(t850::GUIManager& gui) {
  // Load SandboxScene.json for GUI descriptors
  if (m_guiSetup.descriptor.name.empty()) {
    m_guiSetup.Load("Scenes/SandboxScene.json");
  }

  struct SliderMapping { const char* name; int settingIndex; };
  static const SliderMapping mappings[] = {
    {"exposure",              CHANGE_EXPOSURE},
    {"bloom_factor",          CHANGE_BLOOM_FACTOR},
    {"bloom_threshold",       CHANGE_BLOOM_THRESHOLD},
    {"tm_white_level",        CHANGE_TM_WHITE_LEVEL},
    {"tm_adapt_tau",          CHANGE_TM_ADAPT_TAU},
    {"pcf_radius",            CHANGE_PCF_RADIUS},
    {"pcf_samples",           CHANGE_PCF_SAMPLES},
    {"ssao_kernel_size",      CHANGE_SSAO_KERNEL_SIZE},
    {"ssao_radius",           CHANGE_SSAO_RADIUS},
    {"dof_aperture",          CHANGE_DOF_APERTURE},
    {"dof_focal_length",      CHANGE_DOF_FOCAL_LENGHT},
    {"dof_max_coc",           CHANGE_DOF_MAX_COC},
    {"dof_far_samples",       CHANGE_DOF_FAR_SAMPLE},
    {"dof_near_samples",      CHANGE_DOF_NEAR_SAMPLE},
    {"light_volume_steps",    CHANGE_LIGHT_VOLUME_STEPS},
    {"godrays_factor",        CHANGE_GODRAYS_FACTOR},
    {"gauss_kernel_radius",   CHANGE_GAUSS_KERNEL_RADIUS},
    {"gauss_kernel_deviation", CHANGE_GAUSS_KERNEL_DEVIATION},
    {"fov",                   CHANGE_FOV},
    {"shadow_bias",           CHANGE_SHADOW_BIAS},
    {"shadow_min",            CHANGE_SHADOW_MIN},
    {"env_factor",            CHANGE_ENV_FACTOR},
    {"ibl_factor",             CHANGE_IBL_FACTOR},
    {"material_emissive_intensity", CHANGE_MATERIAL_EMISSIVE_INTENSITY},
    {"material_transmission_multiplier", CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER},
    {"material_refraction_strength", CHANGE_MATERIAL_REFRACTION_STRENGTH},
    {"anim_speed",             CHANGE_ANIM_SPEED},
  };

  for (auto& sd : m_guiSetup.descriptor.sliders) {
    int settingIdx = -1;
    for (auto& m : mappings) {
      if (sd.name == m.name) { settingIdx = m.settingIndex; break; }
    }
    gui.AddSlider(sd, settingIdx);
  }

  struct CheckboxMapping { const char* name; int settingIndex; };
  static const CheckboxMapping cbMappings[] = {
    {"shadow_toggle",          CHANGE_PCF_TOOGLE},
    {"ssao_toggle",            CHANGLE_SSAO_TOOGLE},
    {"show_wireframe",         CHANGE_SHOW_WIREFRAME},
    {"show_skeleton",          CHANGE_SHOW_SKELETON},
    {"show_physics",           CHANGE_SHOW_PHYSICS},
  };

  for (auto& cd : m_guiSetup.descriptor.checkboxes) {
    int settingIdx = -1;
    for (auto& m : cbMappings) {
      if (cd.name == m.name) { settingIdx = m.settingIndex; break; }
    }
    gui.AddCheckbox(cd, settingIdx);
  }

  struct SelectorMapping { const char* name; int settingIndex; };
  static const SelectorMapping selMappings[] = {
    {"debug_render_target", CHANGE_DEBUG_RT},
    {"cubemap",             CHANGE_CUBEMAP},
    {"gauss_kernel_sample_count", CHANGE_GAUSS_KERNEL_SAMPLE_COUNT},
    {"active_gauss_kernel",        CHANGE_ACTIVE_GAUSS_KERNEL},
    {"anim_select",                CHANGE_ANIM_SELECT},
    {"anim_mode",                  CHANGE_ANIM_MODE},
  };

  for (auto& sd : m_guiSetup.descriptor.selectors) {
    int settingIdx = -1;
    for (auto& m : selMappings) {
      if (sd.name == m.name) { settingIdx = m.settingIndex; break; }
    }
    gui.AddSelector(sd, settingIdx);
  }

  // Populate animation selector with actual animation names from the loaded model
  RenderSkinnedMesh* skinned = Meshes[0].GetSkinnedMesh();
  if (skinned && skinned->HasSkinData()) {
    for (auto& sp : gui.GetSelectorPairs()) {
      if (sp.selector->settingIndex == CHANGE_ANIM_SELECT) {
        sp.selector->options.clear();
        int numSets = skinned->GetNumAnimSets();
        for (int i = 0; i < numSets; i++) {
          if (skinned->xFile && !skinned->xFile->XMeshDataBase.empty()) {
            auto& anims = skinned->xFile->XMeshDataBase[0]->Animation.Animations;
            if (i < (int)anims.size() && !anims[i].Name.empty()) {
              sp.selector->options.push_back(anims[i].Name);
            } else {
              sp.selector->options.push_back("Anim " + std::to_string(i));
            }
          } else {
            sp.selector->options.push_back("Anim " + std::to_string(i));
          }
        }
        if (sp.selector->options.empty())
          sp.selector->options.push_back("None");
        break;
      }
    }
  }
}

void SandboxScene::DrawDevGui(t850::DevGuiContext& gui) {
  if (m_guiSetup.descriptor.name.empty()) {
    m_guiSetup.Load("Scenes/SandboxScene.json");
  }

  struct Mapping { const char* name; int settingIndex; };

  static const Mapping sliderMappings[] = {
    {"exposure", CHANGE_EXPOSURE},
    {"bloom_factor", CHANGE_BLOOM_FACTOR},
    {"bloom_threshold", CHANGE_BLOOM_THRESHOLD},
    {"tm_white_level", CHANGE_TM_WHITE_LEVEL},
    {"tm_adapt_tau", CHANGE_TM_ADAPT_TAU},
    {"pcf_radius", CHANGE_PCF_RADIUS},
    {"pcf_samples", CHANGE_PCF_SAMPLES},
    {"ssao_kernel_size", CHANGE_SSAO_KERNEL_SIZE},
    {"ssao_radius", CHANGE_SSAO_RADIUS},
    {"dof_aperture", CHANGE_DOF_APERTURE},
    {"dof_focal_length", CHANGE_DOF_FOCAL_LENGHT},
    {"dof_max_coc", CHANGE_DOF_MAX_COC},
    {"dof_far_samples", CHANGE_DOF_FAR_SAMPLE},
    {"dof_near_samples", CHANGE_DOF_NEAR_SAMPLE},
    {"light_volume_steps", CHANGE_LIGHT_VOLUME_STEPS},
    {"godrays_factor", CHANGE_GODRAYS_FACTOR},
    {"gauss_kernel_radius", CHANGE_GAUSS_KERNEL_RADIUS},
    {"gauss_kernel_deviation", CHANGE_GAUSS_KERNEL_DEVIATION},
    {"fov", CHANGE_FOV},
    {"shadow_bias", CHANGE_SHADOW_BIAS},
    {"shadow_min", CHANGE_SHADOW_MIN},
    {"env_factor", CHANGE_ENV_FACTOR},
    {"ibl_factor", CHANGE_IBL_FACTOR},
    {"material_emissive_intensity", CHANGE_MATERIAL_EMISSIVE_INTENSITY},
    {"material_transmission_multiplier", CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER},
    {"material_refraction_strength", CHANGE_MATERIAL_REFRACTION_STRENGTH},
    {"anim_speed", CHANGE_ANIM_SPEED},
  };

  static const Mapping checkboxMappings[] = {
    {"shadow_toggle", CHANGE_PCF_TOOGLE},
    {"ssao_toggle", CHANGLE_SSAO_TOOGLE},
    {"show_wireframe", CHANGE_SHOW_WIREFRAME},
    {"show_skeleton", CHANGE_SHOW_SKELETON},
    {"show_physics", CHANGE_SHOW_PHYSICS},
  };

  static const Mapping selectorMappings[] = {
    {"debug_render_target", CHANGE_DEBUG_RT},
    {"cubemap", CHANGE_CUBEMAP},
    {"gauss_kernel_sample_count", CHANGE_GAUSS_KERNEL_SAMPLE_COUNT},
    {"active_gauss_kernel", CHANGE_ACTIVE_GAUSS_KERNEL},
    {"anim_select", CHANGE_ANIM_SELECT},
    {"anim_mode", CHANGE_ANIM_MODE},
  };

  auto findSetting = [](const std::string& name, const Mapping* mappings, int count) {
    for (int i = 0; i < count; ++i) {
      if (name == mappings[i].name) return mappings[i].settingIndex;
    }
    return -1;
  };

  auto activeKernel = [&]() -> GaussFilter* {
    if (ChangeActiveGaussSelection < 0 || ChangeActiveGaussSelection >= (int)SceneProp.pGaussKernels.size()) return nullptr;
    return SceneProp.pGaussKernels[ChangeActiveGaussSelection];
  };

  auto skinnedMesh = [&]() -> RenderSkinnedMesh* {
    return Meshes[0].GetSkinnedMesh();
  };

  auto buildAnimationOptions = [&]() {
    std::vector<std::string> options;
    RenderSkinnedMesh* skinned = skinnedMesh();
    if (skinned && skinned->HasSkinData()) {
      int numSets = skinned->GetNumAnimSets();
      for (int i = 0; i < numSets; ++i) {
        if (skinned->xFile && !skinned->xFile->XMeshDataBase.empty()) {
          auto& anims = skinned->xFile->XMeshDataBase[0]->Animation.Animations;
          if (i < (int)anims.size() && !anims[i].Name.empty()) {
            options.push_back(anims[i].Name);
            continue;
          }
        }
        options.push_back("Anim " + std::to_string(i));
      }
    }
    if (options.empty()) options.push_back("None");
    return options;
  };

  auto getSliderValue = [&](int settingIndex, float& value) -> bool {
    GaussFilter* kernel = activeKernel();
    switch (settingIndex) {
    case CHANGE_EXPOSURE: value = SceneProp.Exposure; return true;
    case CHANGE_BLOOM_FACTOR: value = SceneProp.BloomFactor; return true;
    case CHANGE_BLOOM_THRESHOLD: value = SceneProp.BloomThreshold; return true;
    case CHANGE_TM_WHITE_LEVEL: value = SceneProp.ToneMapWhiteLevel; return true;
    case CHANGE_TM_ADAPT_TAU: value = SceneProp.LuminanceTau; return true;
    case CHANGE_PCF_RADIUS: value = SceneProp.PCFScale; return true;
    case CHANGE_PCF_SAMPLES: value = SceneProp.PCFSamples; return true;
    case CHANGE_SSAO_KERNEL_SIZE: value = (float)SceneProp.SSAOKernel.KernelSize; return true;
    case CHANGE_SSAO_RADIUS: value = SceneProp.SSAOKernel.Radius; return true;
    case CHANGE_DOF_APERTURE: value = SceneProp.Aperture; return true;
    case CHANGE_DOF_FOCAL_LENGHT: value = SceneProp.FocalLength; return true;
    case CHANGE_DOF_MAX_COC: value = SceneProp.MaxCoc; return true;
    case CHANGE_DOF_FAR_SAMPLE: value = SceneProp.DOF_Far_Samples_squared; return true;
    case CHANGE_DOF_NEAR_SAMPLE: value = SceneProp.DOF_Near_Samples_squared; return true;
    case CHANGE_LIGHT_VOLUME_STEPS: value = SceneProp.LightVolumeSteps; return true;
    case CHANGE_GODRAYS_FACTOR: value = SceneProp.GodRaysFactor; return true;
    case CHANGE_GAUSS_KERNEL_RADIUS: if (!kernel) return false; value = kernel->radius; return true;
    case CHANGE_GAUSS_KERNEL_DEVIATION: if (!kernel) return false; value = kernel->sigma; return true;
    case CHANGE_FOV: if (!ActiveCam) return false; value = Rad2Deg(ActiveCam->Fov); return true;
    case CHANGE_LIGHT_INTENSITY: if (SceneProp.Lights.empty()) return false; value = SceneProp.Lights[0].Intensity; return true;
    case CHANGE_SHADOW_BIAS: value = SceneProp.ShadowBias; return true;
    case CHANGE_SHADOW_MIN: value = SceneProp.ShadowMin; return true;
    case CHANGE_ENV_FACTOR: value = SceneProp.EnvFactor; return true;
    case CHANGE_IBL_FACTOR: value = SceneProp.IBLFactor; return true;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: value = SceneProp.MaterialEmissiveIntensity; return true;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: value = SceneProp.MaterialTransmissionMultiplier; return true;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: value = SceneProp.MaterialRefractionStrength; return true;
    case CHANGE_ANIM_SPEED: if (RenderSkinnedMesh* sk = skinnedMesh()) { value = sk->GetAnimSpeed(); return true; } return false;
    }
    return false;
  };

  auto setSliderValue = [&](int settingIndex, float value) {
    GaussFilter* kernel = activeKernel();
    switch (settingIndex) {
    case CHANGE_EXPOSURE: SceneProp.Exposure = value; break;
    case CHANGE_BLOOM_FACTOR: SceneProp.BloomFactor = value; break;
    case CHANGE_BLOOM_THRESHOLD: SceneProp.BloomThreshold = value; break;
    case CHANGE_TM_WHITE_LEVEL: SceneProp.ToneMapWhiteLevel = value; break;
    case CHANGE_TM_ADAPT_TAU: SceneProp.LuminanceTau = value; break;
    case CHANGE_PCF_RADIUS: SceneProp.PCFScale = value; break;
    case CHANGE_PCF_SAMPLES: SceneProp.PCFSamples = value; break;
    case CHANGE_SSAO_KERNEL_SIZE: SceneProp.SSAOKernel.KernelSize = (int)value; SceneProp.SSAOKernel.Update(); break;
    case CHANGE_SSAO_RADIUS: SceneProp.SSAOKernel.Radius = value; break;
    case CHANGE_DOF_APERTURE: SceneProp.Aperture = value; break;
    case CHANGE_DOF_FOCAL_LENGHT: SceneProp.FocalLength = value; break;
    case CHANGE_DOF_MAX_COC: SceneProp.MaxCoc = value; break;
    case CHANGE_DOF_FAR_SAMPLE: SceneProp.DOF_Far_Samples_squared = value; break;
    case CHANGE_DOF_NEAR_SAMPLE: SceneProp.DOF_Near_Samples_squared = value; break;
    case CHANGE_LIGHT_VOLUME_STEPS: SceneProp.LightVolumeSteps = value; break;
    case CHANGE_GODRAYS_FACTOR: SceneProp.GodRaysFactor = value; break;
    case CHANGE_GAUSS_KERNEL_RADIUS: if (kernel) { kernel->radius = value; kernel->Update(); } break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: if (kernel) { kernel->sigma = value; kernel->Update(); } break;
    case CHANGE_FOV:
      if (ActiveCam) {
        ActiveCam->SetFov(Deg2Rad(value));
        ActiveCam->VP = ActiveCam->View * ActiveCam->Projection;
        VP = ActiveCam->VP;
      }
      break;
    case CHANGE_LIGHT_INTENSITY: if (!SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = value; break;
    case CHANGE_SHADOW_BIAS: SceneProp.ShadowBias = value; break;
    case CHANGE_SHADOW_MIN: SceneProp.ShadowMin = value; break;
    case CHANGE_ENV_FACTOR: SceneProp.EnvFactor = value; break;
    case CHANGE_IBL_FACTOR: SceneProp.IBLFactor = value; break;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: SceneProp.MaterialEmissiveIntensity = value; break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: SceneProp.MaterialTransmissionMultiplier = value; break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: SceneProp.MaterialRefractionStrength = value; break;
    case CHANGE_ANIM_SPEED: if (RenderSkinnedMesh* sk = skinnedMesh()) sk->SetAnimSpeed(value); break;
    }
  };

  auto getCheckboxValue = [&](int settingIndex, bool& value) -> bool {
    switch (settingIndex) {
    case CHANGE_PCF_TOOGLE: value = (SceneProp.ToogleShadow != 0); return true;
    case CHANGLE_SSAO_TOOGLE: value = (SceneProp.ToogleSSAO != 0); return true;
    case CHANGE_SHOW_WIREFRAME: value = m_showWireframe; return true;
    case CHANGE_SHOW_SKELETON: value = (skinnedMesh() != nullptr) && m_showSkeleton; return true;
    case CHANGE_SHOW_PHYSICS: value = m_showPhysics; return true;
    }
    return false;
  };

  auto setCheckboxValue = [&](int settingIndex, bool value) {
    switch (settingIndex) {
    case CHANGE_PCF_TOOGLE: SceneProp.ToogleShadow = value ? 1 : 0; break;
    case CHANGLE_SSAO_TOOGLE: SceneProp.ToogleSSAO = value ? 1 : 0; break;
    case CHANGE_SHOW_WIREFRAME: m_showWireframe = value; break;
    case CHANGE_SHOW_SKELETON: m_showSkeleton = value && (skinnedMesh() != nullptr); break;
    case CHANGE_SHOW_PHYSICS: m_showPhysics = value; break;
    }
  };

  auto getSelectorIndex = [&](const t850::SelectorDesc& desc, int settingIndex, int& selectedIndex) -> bool {
    switch (settingIndex) {
    case CHANGE_DEBUG_RT: selectedIndex = m_debugRTSelection; return true;
    case CHANGE_CUBEMAP: selectedIndex = m_currentCubemapIndex; return true;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      GaussFilter* kernel = activeKernel();
      if (!kernel) return false;
      for (int i = 0; i < (int)desc.options.size(); ++i) {
        if (std::atoi(desc.options[i].c_str()) == kernel->kernelSize) { selectedIndex = i; return true; }
      }
      selectedIndex = desc.default_index;
      return true;
    }
    case CHANGE_ACTIVE_GAUSS_KERNEL: selectedIndex = ChangeActiveGaussSelection; return true;
    case CHANGE_ANIM_SELECT: if (RenderSkinnedMesh* sk = skinnedMesh()) { selectedIndex = sk->GetCurrentAnimSet(); return true; } selectedIndex = 0; return true;
    case CHANGE_ANIM_MODE: if (RenderSkinnedMesh* sk = skinnedMesh()) { selectedIndex = sk->GetKeyframeMode() ? 1 : 0; return true; } selectedIndex = 0; return true;
    }
    return false;
  };

  auto setSelectorIndex = [&](const t850::SelectorDesc& desc, const std::vector<std::string>* options, int settingIndex, int selectedIndex) {
    const std::vector<std::string>& sourceOptions = options ? *options : desc.options;
    if (selectedIndex < 0 || selectedIndex >= (int)sourceOptions.size()) return;
    switch (settingIndex) {
    case CHANGE_DEBUG_RT: m_debugRTSelection = selectedIndex; break;
    case CHANGE_CUBEMAP:
      if (selectedIndex != m_currentCubemapIndex) {
        m_currentCubemapIndex = selectedIndex;
        m_pendingCubemap = "sky/" + sourceOptions[selectedIndex];
        T8_LOG_INFO("[SandboxScene] Cubemap change queued: '%s'", m_pendingCubemap.c_str());
      }
      break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      GaussFilter* kernel = activeKernel();
      if (kernel) { kernel->kernelSize = std::atoi(sourceOptions[selectedIndex].c_str()); kernel->Update(); }
    } break;
    case CHANGE_ACTIVE_GAUSS_KERNEL: ChangeActiveGaussSelection = selectedIndex; break;
    case CHANGE_ANIM_SELECT:
      if (RenderSkinnedMesh* sk = skinnedMesh()) {
        int guard = sk->GetNumAnimSets() + 1;
        while (sk->GetCurrentAnimSet() != selectedIndex && guard-- > 0) {
          sk->NextAnimation();
        }
      }
      break;
    case CHANGE_ANIM_MODE:
      if (RenderSkinnedMesh* sk = skinnedMesh()) {
        bool keyMode = (selectedIndex == 1);
        sk->SetKeyframeMode(keyMode);
        if (keyMode) sk->StepKeyframe(0);
      }
      break;
    }
  };

  if (gui.BeginSection("Controls")) {
    if (RenderSkinnedMesh* sk = skinnedMesh()) {
      if (m_skeletonEditMode) ImGui::BeginDisabled();
      if (gui.Button(sk->IsPlaying() ? "Pause Animation" : "Resume Animation")) {
        if (sk->IsPlaying()) sk->PauseAnimation();
        else sk->PlayAnimation();
      }
      if (m_skeletonEditMode) ImGui::EndDisabled();
    }
    for (const auto& desc : m_guiSetup.descriptor.sliders) {
      int settingIndex = findSetting(desc.name, sliderMappings, (int)(sizeof(sliderMappings) / sizeof(sliderMappings[0])));
      if (settingIndex < 0) continue;
      float value = 0.0f;
      if (getSliderValue(settingIndex, value) && gui.Slider(desc, value)) {
        setSliderValue(settingIndex, value);
      }
    }
  }

  if (gui.BeginSection("Toggles")) {
    for (const auto& desc : m_guiSetup.descriptor.checkboxes) {
      int settingIndex = findSetting(desc.name, checkboxMappings, (int)(sizeof(checkboxMappings) / sizeof(checkboxMappings[0])));
      if (settingIndex < 0) continue;
      bool value = false;
      if (getCheckboxValue(settingIndex, value) && gui.Checkbox(desc, value)) {
        setCheckboxValue(settingIndex, value);
      }
    }
  }

  if (gui.BeginSection("Selectors")) {
    std::vector<std::string> animOptions;
    for (const auto& desc : m_guiSetup.descriptor.selectors) {
      int settingIndex = findSetting(desc.name, selectorMappings, (int)(sizeof(selectorMappings) / sizeof(selectorMappings[0])));
      if (settingIndex < 0) continue;
      const std::vector<std::string>* overrideOptions = nullptr;
      if (settingIndex == CHANGE_ANIM_SELECT) {
        animOptions = buildAnimationOptions();
        overrideOptions = &animOptions;
      }
      int selectedIndex = 0;
      if (getSelectorIndex(desc, settingIndex, selectedIndex) && gui.Combo(desc, selectedIndex, overrideOptions)) {
        setSelectorIndex(desc, overrideOptions, settingIndex, selectedIndex);
      }
    }
  }

  if (gui.BeginSection("Lights")) {
    EnsureLightRuntimeState();
    if (SceneProp.Lights.empty()) {
      gui.Text("No lights");
    } else {
      std::vector<std::string> lightOptions;
      lightOptions.reserve(SceneProp.Lights.size());
      for (int i = 0; i < (int)SceneProp.Lights.size(); ++i) {
        const char* typeName = SceneProp.Lights[i].Type == LIGHT_DIRECTIONAL ? "Directional" : "Point";
        lightOptions.push_back(std::string(typeName) + " " + std::to_string(i + 1));
      }

      t850::SelectorDesc lightSelector;
      lightSelector.name = "active_light";
      lightSelector.label = "Light";
      int selectedLight = m_selectedLightIndex;
      if (gui.Combo(lightSelector, selectedLight, &lightOptions)) {
        m_selectedLightIndex = selectedLight;
      }
      EnsureLightRuntimeState();

      Light& light = SceneProp.Lights[m_selectedLightIndex];
      ImGui::PushID(m_selectedLightIndex);

      float color[3] = {light.Color.x, light.Color.y, light.Color.z};
      if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_PickerHueBar)) {
        light.Color = XVECTOR3(color[0], color[1], color[2]);
      }

      const struct { const char* name; float rgb[3]; } palette[] = {
        {"White", {1.0f, 1.0f, 1.0f}},
        {"Warm", {1.0f, 0.84f, 0.58f}},
        {"Cool", {0.62f, 0.74f, 1.0f}},
        {"Amber", {1.0f, 0.52f, 0.18f}},
        {"Red", {1.0f, 0.18f, 0.15f}},
        {"Green", {0.3f, 1.0f, 0.42f}},
        {"Blue", {0.2f, 0.45f, 1.0f}}
      };
      const int paletteCount = (int)(sizeof(palette) / sizeof(palette[0]));
      for (int paletteIndex = 0; paletteIndex < paletteCount; ++paletteIndex) {
        if (paletteIndex > 0) ImGui::SameLine();
        ImGui::PushID(paletteIndex);
        ImVec4 swatch(palette[paletteIndex].rgb[0], palette[paletteIndex].rgb[1], palette[paletteIndex].rgb[2], 1.0f);
        if (ImGui::ColorButton(palette[paletteIndex].name, swatch, ImGuiColorEditFlags_NoTooltip, ImVec2(20.0f, 20.0f))) {
          light.Color = XVECTOR3(palette[paletteIndex].rgb[0], palette[paletteIndex].rgb[1], palette[paletteIndex].rgb[2]);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", palette[paletteIndex].name);
        ImGui::PopID();
      }

      t850::SliderDesc intensityDesc;
      intensityDesc.name = "light_intensity";
      intensityDesc.label = "Intensity";
      intensityDesc.min_val = 0.0f;
      intensityDesc.max_val = 50.0f;
      intensityDesc.step = 0.1f;
      intensityDesc.default_val = light.Intensity;
      float intensity = light.Intensity;
      if (gui.Slider(intensityDesc, intensity)) light.Intensity = intensity;

      if (light.Type == LIGHT_DIRECTIONAL) {
        t850::CheckboxDesc drawDirectionDesc;
        drawDirectionDesc.name = "draw_direction";
        drawDirectionDesc.label = "Draw direction";
        bool drawDirection = m_drawLightDirection;
        if (gui.Checkbox(drawDirectionDesc, drawDirection)) m_drawLightDirection = drawDirection;

        float direction[3] = {light.Direction.x, light.Direction.y, light.Direction.z};
        if (ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f, "%.3f")) {
          XVECTOR3 newDirection(direction[0], direction[1], direction[2]);
          if (newDirection.Length() > 0.0001f) {
            newDirection.Normalize();
            light.Direction = newDirection;
            SyncLightCameraFromDirectionalLight();
          }
        }
      } else {
        t850::CheckboxDesc attachDesc;
        attachDesc.name = "attach_to_camera";
        attachDesc.label = "Attach to camera";
        bool attachToCamera = m_lightAttachToCamera[m_selectedLightIndex];
        if (gui.Checkbox(attachDesc, attachToCamera)) {
          m_lightAttachToCamera[m_selectedLightIndex] = attachToCamera;
          UpdateAttachedLights();
        }

        float position[3] = {light.Position.x, light.Position.y, light.Position.z};
        if (attachToCamera) ImGui::BeginDisabled();
        if (ImGui::DragFloat3("Position", position, 0.05f, 0.0f, 0.0f, "%.3f")) {
          light.Position = XVECTOR3(position[0], position[1], position[2]);
        }
        if (attachToCamera) ImGui::EndDisabled();

        t850::SliderDesc diameterDesc;
        diameterDesc.name = "light_diameter";
        diameterDesc.label = "Diameter";
        diameterDesc.min_val = 0.01f;
        diameterDesc.max_val = 2000.0f;
        diameterDesc.step = 0.1f;
        diameterDesc.default_val = light.radius * 2.0f;
        float diameter = light.radius * 2.0f;
        if (gui.Slider(diameterDesc, diameter)) light.radius = (std::max)(0.001f, diameter * 0.5f);
      }

      ImGui::PopID();
    }
  }

  if (m_profileReady) {
    t850::SandboxProfileDesc currentProfileState;
    CaptureSandboxProfileState(currentProfileState);
    m_profileDirty = !SandboxProfileStatesEqual(currentProfileState, m_profileSavedState);
    if (gui.BeginSection("Profile")) {
      std::string profileText = "Model profile: " + (m_profileModelKey.empty() ? std::string("none") : m_profileModelKey);
      gui.Text(profileText.c_str());
      const auto& runtime = t850::GetRuntimeProfileInfo();
      std::string gpuText = runtime.gpuName.empty() ? runtime.gpuFamily : runtime.gpuName;
      if (gpuText.empty()) gpuText = "unknown GPU";
      else if (!runtime.gpuFamily.empty() && runtime.gpuFamily != runtime.gpuName) gpuText += " (" + runtime.gpuFamily + ")";
      std::string runtimeText = "Runtime: " + runtime.platform + " / " + runtime.architecture + " / " + gpuText;
      gui.Text(runtimeText.c_str());

      t850::SelectorDesc targetDesc;
      targetDesc.name = "profile_target";
      targetDesc.label = "Save target";
      for (const auto& target : t850::GetProfileTargets()) targetDesc.options.push_back(target.label);
      targetDesc.default_index = m_selectedProfileTargetIndex;
      int targetIndex = m_selectedProfileTargetIndex;
      if (gui.Combo(targetDesc, targetIndex)) {
        m_selectedProfileTargetIndex = targetIndex;
      }
      bool canSaveProfile = m_profileDirty || m_selectedProfileTargetIndex != t850::DefaultProfileTargetIndex();
      if (gui.Button("Save Profile", canSaveProfile)) {
        SaveSandboxProfile();
      }
    }
  }

  if (gui.BeginSection("Culling")) {
    t850::CheckboxDesc cullingDesc;
    cullingDesc.name = "frustum_culling";
    cullingDesc.label = "Frustum culling";
    cullingDesc.enabled = SceneProp.FrustumCullingToggleAllowed;
    bool cullingEnabled = SceneProp.FrustumCullingEnabled;
    if (gui.Checkbox(cullingDesc, cullingEnabled)) {
      if (!cullingEnabled || !Meshes[0].pBase || static_cast<RenderMesh*>(Meshes[0].pBase)->EnsureCullingMetadata()) {
        SceneProp.FrustumCullingEnabled = cullingEnabled;
      }
    }

    t850::CheckboxDesc statsDesc;
    statsDesc.name = "show_culling_debug";
    statsDesc.label = "Culling stats and frustum";
    statsDesc.enabled = SceneProp.FrustumCullingToggleAllowed;
    bool showCulling = m_showCullStats;
    if (gui.Checkbox(statsDesc, showCulling)) {
      m_showCullStats = showCulling;
      SceneProp.ShowCullingDebug = showCulling;
    }
  }

  DrawSkinningAuthoringPanel(gui);
}

void SandboxScene::SyncToGUI(t850::GUIManager& gui) {
  for (auto& sp : gui.GetSliderPairs()) {
    auto* slider = sp.slider;
    switch (slider->settingIndex) {
    case CHANGE_EXPOSURE:        slider->SetValue(SceneProp.Exposure); break;
    case CHANGE_BLOOM_FACTOR:    slider->SetValue(SceneProp.BloomFactor); break;
    case CHANGE_BLOOM_THRESHOLD: slider->SetValue(SceneProp.BloomThreshold); break;
    case CHANGE_TM_WHITE_LEVEL:  slider->SetValue(SceneProp.ToneMapWhiteLevel); break;
    case CHANGE_TM_ADAPT_TAU:    slider->SetValue(SceneProp.LuminanceTau); break;
    case CHANGE_PCF_RADIUS:      slider->SetValue(SceneProp.PCFScale); break;
    case CHANGE_PCF_SAMPLES:     slider->SetValue(SceneProp.PCFSamples); break;
    case CHANGE_SSAO_KERNEL_SIZE: slider->SetValue((float)SceneProp.SSAOKernel.KernelSize); break;
    case CHANGE_SSAO_RADIUS:     slider->SetValue(SceneProp.SSAOKernel.Radius); break;
    case CHANGE_DOF_APERTURE:    slider->SetValue(SceneProp.Aperture); break;
    case CHANGE_DOF_FOCAL_LENGHT: slider->SetValue(SceneProp.FocalLength); break;
    case CHANGE_DOF_MAX_COC:     slider->SetValue(SceneProp.MaxCoc); break;
    case CHANGE_DOF_FAR_SAMPLE:  slider->SetValue(SceneProp.DOF_Far_Samples_squared); break;
    case CHANGE_DOF_NEAR_SAMPLE: slider->SetValue(SceneProp.DOF_Near_Samples_squared); break;
    case CHANGE_LIGHT_VOLUME_STEPS: slider->SetValue(SceneProp.LightVolumeSteps); break;
    case CHANGE_GODRAYS_FACTOR:  slider->SetValue(SceneProp.GodRaysFactor); break;
    case CHANGE_GAUSS_KERNEL_RADIUS: slider->SetValue(SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius); break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: slider->SetValue(SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma); break;
    case CHANGE_FOV:             slider->SetValue(Rad2Deg(ActiveCam->Fov)); break;
    case CHANGE_LIGHT_INTENSITY: if (!SceneProp.Lights.empty()) slider->SetValue(SceneProp.Lights[0].Intensity); break;
    case CHANGE_SHADOW_BIAS:     slider->SetValue(SceneProp.ShadowBias); break;
    case CHANGE_SHADOW_MIN:      slider->SetValue(SceneProp.ShadowMin); break;
    case CHANGE_ENV_FACTOR:      slider->SetValue(SceneProp.EnvFactor); break;
    case CHANGE_IBL_FACTOR:      slider->SetValue(SceneProp.IBLFactor); break;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: slider->SetValue(SceneProp.MaterialEmissiveIntensity); break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: slider->SetValue(SceneProp.MaterialTransmissionMultiplier); break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: slider->SetValue(SceneProp.MaterialRefractionStrength); break;
    case CHANGE_ANIM_SPEED: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk) slider->SetValue(sk->GetAnimSpeed());
    } break;
    }
  }

  for (auto& cp : gui.GetCheckboxPairs()) {
    auto* cb = cp.checkbox;
    switch (cb->settingIndex) {
    case CHANGE_PCF_TOOGLE:   cb->checked = (SceneProp.ToogleShadow != 0); break;
    case CHANGLE_SSAO_TOOGLE: cb->checked = (SceneProp.ToogleSSAO != 0); break;
    case CHANGE_SHOW_WIREFRAME: cb->checked = m_showWireframe; break;
    case CHANGE_SHOW_SKELETON:  cb->checked = (Meshes[0].GetSkinnedMesh() != nullptr) && m_showSkeleton; break;
    case CHANGE_SHOW_PHYSICS:   cb->checked = m_showPhysics; break;
    }
  }

  for (auto& sp : gui.GetSelectorPairs()) {
    auto* sel = sp.selector;
    switch (sel->settingIndex) {
    case CHANGE_DEBUG_RT: sel->selectedIndex = m_debugRTSelection; break;
    case CHANGE_CUBEMAP:  sel->selectedIndex = m_currentCubemapIndex; break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      int ks = SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize;
      std::string ksStr = std::to_string(ks);
      for (int i = 0; i < (int)sel->options.size(); i++) {
        if (sel->options[i] == ksStr) { sel->selectedIndex = i; break; }
      }
    } break;
    case CHANGE_ACTIVE_GAUSS_KERNEL:
      sel->selectedIndex = ChangeActiveGaussSelection;
      break;
    case CHANGE_ANIM_SELECT: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk) sel->selectedIndex = sk->GetCurrentAnimSet();
    } break;
    }
  }
}

void SandboxScene::SyncFromGUI(t850::GUIManager& gui) {
  for (auto& sp : gui.GetSliderPairs()) {
    auto* slider = sp.slider;
    if (!slider->knobDragging && !slider->knobHover) continue;
    switch (slider->settingIndex) {
    case CHANGE_EXPOSURE:        SceneProp.Exposure = slider->value; break;
    case CHANGE_BLOOM_FACTOR:    SceneProp.BloomFactor = slider->value; break;
    case CHANGE_BLOOM_THRESHOLD: SceneProp.BloomThreshold = slider->value; break;
    case CHANGE_TM_WHITE_LEVEL:  SceneProp.ToneMapWhiteLevel = slider->value; break;
    case CHANGE_TM_ADAPT_TAU:    SceneProp.LuminanceTau = slider->value; break;
    case CHANGE_PCF_RADIUS:      SceneProp.PCFScale = slider->value; break;
    case CHANGE_PCF_SAMPLES:     SceneProp.PCFSamples = slider->value; break;
    case CHANGE_SSAO_KERNEL_SIZE: SceneProp.SSAOKernel.KernelSize = (int)slider->value; SceneProp.SSAOKernel.Update(); break;
    case CHANGE_SSAO_RADIUS:     SceneProp.SSAOKernel.Radius = slider->value; break;
    case CHANGE_DOF_APERTURE:    SceneProp.Aperture = slider->value; break;
    case CHANGE_DOF_FOCAL_LENGHT: SceneProp.FocalLength = slider->value; break;
    case CHANGE_DOF_MAX_COC:     SceneProp.MaxCoc = slider->value; break;
    case CHANGE_DOF_FAR_SAMPLE:  SceneProp.DOF_Far_Samples_squared = slider->value; break;
    case CHANGE_DOF_NEAR_SAMPLE: SceneProp.DOF_Near_Samples_squared = slider->value; break;
    case CHANGE_LIGHT_VOLUME_STEPS: SceneProp.LightVolumeSteps = slider->value; break;
    case CHANGE_GODRAYS_FACTOR:  SceneProp.GodRaysFactor = slider->value; break;
    case CHANGE_GAUSS_KERNEL_RADIUS: SceneProp.pGaussKernels[ChangeActiveGaussSelection]->radius = slider->value;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update(); break;
    case CHANGE_GAUSS_KERNEL_DEVIATION: SceneProp.pGaussKernels[ChangeActiveGaussSelection]->sigma = slider->value;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update(); break;
    case CHANGE_FOV:             ActiveCam->Fov = Deg2Rad(slider->value); break;
    case CHANGE_LIGHT_INTENSITY: if (!SceneProp.Lights.empty()) SceneProp.Lights[0].Intensity = slider->value; break;
    case CHANGE_SHADOW_BIAS:     SceneProp.ShadowBias = slider->value; break;
    case CHANGE_SHADOW_MIN:      SceneProp.ShadowMin = slider->value; break;
    case CHANGE_ENV_FACTOR:      SceneProp.EnvFactor = slider->value; break;
    case CHANGE_IBL_FACTOR:      SceneProp.IBLFactor = slider->value; break;
    case CHANGE_MATERIAL_EMISSIVE_INTENSITY: SceneProp.MaterialEmissiveIntensity = slider->value; break;
    case CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER: SceneProp.MaterialTransmissionMultiplier = slider->value; break;
    case CHANGE_MATERIAL_REFRACTION_STRENGTH: SceneProp.MaterialRefractionStrength = slider->value; break;
    case CHANGE_ANIM_SPEED: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk) sk->SetAnimSpeed(slider->value);
    } break;
    }
  }

  for (auto& cp : gui.GetCheckboxPairs()) {
    auto* cb = cp.checkbox;
    if (!cb->justToggled) continue;
    switch (cb->settingIndex) {
    case CHANGE_PCF_TOOGLE:   SceneProp.ToogleShadow = cb->checked ? 1 : 0; break;
    case CHANGLE_SSAO_TOOGLE: SceneProp.ToogleSSAO = cb->checked ? 1 : 0; break;
    case CHANGE_SHOW_WIREFRAME: m_showWireframe = cb->checked; break;
    case CHANGE_SHOW_SKELETON:  m_showSkeleton = cb->checked && (Meshes[0].GetSkinnedMesh() != nullptr); break;
    case CHANGE_SHOW_PHYSICS:   m_showPhysics = cb->checked; break;
    }
  }

  for (auto& sp : gui.GetSelectorPairs()) {
    auto* sel = sp.selector;
    if (!sel->justChanged) continue;
    switch (sel->settingIndex) {
    case CHANGE_DEBUG_RT:
      m_debugRTSelection = sel->selectedIndex;
      break;
    case CHANGE_CUBEMAP: {
      if (sel->selectedIndex != m_currentCubemapIndex) {
        m_currentCubemapIndex = sel->selectedIndex;
        m_pendingCubemap = "sky/" + sel->CurrentOption();
        T8_LOG_INFO("[SandboxScene] Cubemap change queued: '%s'", m_pendingCubemap.c_str());
      }
    } break;
    case CHANGE_GAUSS_KERNEL_SAMPLE_COUNT: {
      int newSize = std::atoi(sel->CurrentOption().c_str());
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->kernelSize = newSize;
      SceneProp.pGaussKernels[ChangeActiveGaussSelection]->Update();
    } break;
    case CHANGE_ACTIVE_GAUSS_KERNEL:
      ChangeActiveGaussSelection = sel->selectedIndex;
      break;
    case CHANGE_ANIM_SELECT: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk && sel->selectedIndex != sk->GetCurrentAnimSet()) {
        // Switch to selected animation set
        while (sk->GetCurrentAnimSet() != sel->selectedIndex) {
          sk->NextAnimation();
        }
      }
    } break;
    case CHANGE_ANIM_MODE: {
      RenderSkinnedMesh* sk = Meshes[0].GetSkinnedMesh();
      if (sk) {
        bool keyMode = (sel->selectedIndex == 1);
        sk->SetKeyframeMode(keyMode);
        if (keyMode) {
          sk->StepKeyframe(0); // snap to current keyframe
        }
      }
    } break;
    }
  }
}
