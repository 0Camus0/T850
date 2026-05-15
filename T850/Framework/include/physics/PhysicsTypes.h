#pragma once

#include <utils/Picking.h>
#include <utils/xMaths.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace t850 {

static constexpr uint32_t kInvalidPhysicsHandle = 0xffffffffu;

struct PhysicsBodyHandle {
  uint32_t value = kInvalidPhysicsHandle;

  bool IsValid() const { return value != kInvalidPhysicsHandle; }
  void Reset() { value = kInvalidPhysicsHandle; }
};

struct PhysicsRagdollHandle {
  uint32_t value = kInvalidPhysicsHandle;

  bool IsValid() const { return value != kInvalidPhysicsHandle; }
  void Reset() { value = kInvalidPhysicsHandle; }
};

enum class PhysicsBodyMotion : uint8_t {
  Static,
  Kinematic,
  Dynamic
};

enum class PhysicsShapeType : uint8_t {
  Box,
  Capsule,
  TriangleMesh
};

enum class PhysicsAnimationMode : uint8_t {
  AnimationDriven,
  PhysicsDriven,
  Blend
};

struct PhysicsShapeDesc {
  PhysicsShapeType type = PhysicsShapeType::Box;
  XVECTOR3 halfExtents = XVECTOR3(0.5f, 0.5f, 0.5f, 0.0f);
  float radius = 0.25f;
  float halfHeight = 0.25f;

  static PhysicsShapeDesc Box(const XVECTOR3& halfExtentsValue) {
    PhysicsShapeDesc desc;
    desc.type = PhysicsShapeType::Box;
    desc.halfExtents = halfExtentsValue;
    return desc;
  }

  static PhysicsShapeDesc Capsule(float radiusValue, float halfHeightValue) {
    PhysicsShapeDesc desc;
    desc.type = PhysicsShapeType::Capsule;
    desc.radius = radiusValue;
    desc.halfHeight = halfHeightValue;
    return desc;
  }

  static PhysicsShapeDesc TriangleMeshBounds(const XVECTOR3& halfExtentsValue) {
    PhysicsShapeDesc desc;
    desc.type = PhysicsShapeType::TriangleMesh;
    desc.halfExtents = halfExtentsValue;
    return desc;
  }
};

enum class PhysicsMeshBuildQuality : uint8_t {
  FavorRuntimePerformance,
  FavorBuildSpeed
};

struct PhysicsTriangleMeshCookSettings {
  uint32_t maxTrianglesPerLeaf = 8;
  PhysicsMeshBuildQuality buildQuality = PhysicsMeshBuildQuality::FavorRuntimePerformance;
  bool useDiskCache = true;
};

struct PhysicsTriangleMeshDesc {
  std::string sourcePath;
  std::vector<XVECTOR3> vertices;
  std::vector<uint32_t> indices;
  AABB localBounds;
  PhysicsTriangleMeshCookSettings settings;
};

struct PhysicsCookStats {
  bool cacheHit = false;
  bool cacheSaved = false;
  uint32_t vertexCount = 0;
  uint32_t triangleCount = 0;
  double extractionMs = 0.0;
  double cacheLoadMs = 0.0;
  double cookMs = 0.0;
  double cacheSaveMs = 0.0;
  double totalMs = 0.0;
  std::string cachePath;
};

struct PhysicsBodyDesc {
  uint32_t entityId = 0;
  int boneIndex = -1;
  std::string debugName;
  PhysicsShapeDesc shape;
  XMATRIX44 worldTransform;
  PhysicsBodyMotion motion = PhysicsBodyMotion::Dynamic;
  float mass = 1.0f;
  float friction = 0.4f;
  float restitution = 0.0f;
  bool sensor = false;
};

struct PhysicsTriangleMeshBodyDesc {
  uint32_t entityId = 0;
  std::string debugName;
  PhysicsTriangleMeshDesc mesh;
  XMATRIX44 worldTransform;
  float friction = 0.6f;
  float restitution = 0.0f;
  bool sensor = false;
};

struct PhysicsBodyState {
  PhysicsBodyHandle handle;
  uint32_t entityId = 0;
  int boneIndex = -1;
  XMATRIX44 worldTransform;
  XVECTOR3 linearVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 angularVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  PhysicsBodyMotion motion = PhysicsBodyMotion::Static;
};

struct PhysicsDebugBody {
  PhysicsBodyState state;
  PhysicsShapeDesc shape;
  std::string debugName;
  std::shared_ptr<const std::vector<XVECTOR3>> debugVertices;
  std::shared_ptr<const std::vector<uint32_t>> debugLineIndices;
};

struct PhysicsRagdollBuildSettings {
  float minBoneLength = 0.05f;
  float radiusScale = 0.18f;
  float minRadius = 0.03f;
  float maxRadius = 0.18f;
  float minSkinWeight = 0.12f;
  float radiusPercentile = 0.88f;
  float projectionStartPercentile = 0.08f;
  float projectionEndPercentile = 0.92f;
  float projectionPadding = 0.18f;
  float jointTrimFraction = 0.10f;
  uint32_t minFitSamples = 8;
  float syntheticBoneLength = 0.02f;
  bool includeLeafBones = true;
  bool useCapsules = true;
  bool fitToSkinnedGeometry = false;
  bool preferHumanoidBones = false;
  bool forceCapsuleForEveryBone = false;
};

enum class PhysicsRagdollJointType {
  SwingTwist = 0,
  Fixed = 1,
};

struct PhysicsRagdollBoneDesc {
  PhysicsBodyDesc body;
  int parentBoneIndex = -1;
  PhysicsRagdollJointType jointType = PhysicsRagdollJointType::SwingTwist;
  XVECTOR3 jointWorldPosition = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 parentJointTwistAxis = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 parentJointPlaneAxis = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 childJointTwistAxis = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 childJointPlaneAxis = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  float swingLimitRadians = Deg2Rad(70.0f);
  float twistLimitRadians = Deg2Rad(35.0f);
};

struct PhysicsRagdollDesc {
  uint32_t entityId = 0;
  PhysicsAnimationMode animationMode = PhysicsAnimationMode::AnimationDriven;
  float animationToPhysicsBlend = 0.0f;
  std::vector<PhysicsRagdollBoneDesc> bones;
};

struct PhysicsRagdollAnimationBinding {
  PhysicsRagdollDesc referencePose;
  std::vector<XMATRIX44> bodyFromBone;
  std::vector<XVECTOR3> jointFromBone;
  std::vector<XVECTOR3> parentJointTwistFromBody;
  std::vector<XVECTOR3> parentJointPlaneFromBody;
  std::vector<XVECTOR3> childJointTwistFromBody;
  std::vector<XVECTOR3> childJointPlaneFromBody;
  std::vector<std::vector<int>> controlledBoneIndices;
  std::vector<std::vector<XMATRIX44>> controlledBodyFromBone;
};

} // namespace t850
