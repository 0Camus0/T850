#include <pch.h>

#include <physics/RagdollEditorTool.h>

#include <algorithm>

namespace t850::ragdoll_editor {

RagdollEditorTool::RagdollEditorTool(PhysicsRagdollAuthoringDesc& authoring)
    : m_authoring(&authoring) {}

PhysicsRagdollAuthoringDesc& RagdollEditorTool::Authoring() {
  return *m_authoring;
}

const PhysicsRagdollAuthoringDesc& RagdollEditorTool::Authoring() const {
  return *m_authoring;
}

void RagdollEditorTool::EnsureState() {
  if (!m_authoring) return;
  EnsureControlledBones(m_authoring->binding);
  EnsureParentBodies(m_authoring->binding, m_authoring->parentBodyIndices);
  (void)EnsureJointState(
      m_authoring->binding,
      m_authoring->parentBodyIndices,
      m_authoring->jointParentBodyIndices,
      m_authoring->contactJoints);
  EnsureFreezeState(
      m_authoring->binding.referencePose.bones.size(),
      m_authoring->frozenBodies,
      m_authoring->frozenJoints);
}

int RagdollEditorTool::FindBodyForBone(int boneIndex) const {
  return m_authoring ? t850::ragdoll_editor::FindBodyForBone(m_authoring->binding, boneIndex) : -1;
}

int RagdollEditorTool::FindBodyControllingBone(int boneIndex) const {
  return m_authoring ? t850::ragdoll_editor::FindBodyControllingBone(m_authoring->binding, boneIndex) : -1;
}

int RagdollEditorTool::EffectiveJointParent(int childBody) const {
  return m_authoring
      ? t850::ragdoll_editor::EffectiveJointParent(
            childBody,
            m_authoring->binding.referencePose.bones.size(),
            m_authoring->parentBodyIndices,
            m_authoring->jointParentBodyIndices)
      : -1;
}

bool RagdollEditorTool::IsBodyFrozen(int bodyIndex) const {
  return m_authoring && IsFrozen(m_authoring->frozenBodies, bodyIndex);
}

bool RagdollEditorTool::IsJointFrozen(int childBody) const {
  return m_authoring && IsFrozen(m_authoring->frozenJoints, childBody);
}

bool RagdollEditorTool::SetBodyFrozen(int bodyIndex, bool frozen) {
  return m_authoring && SetFrozen(m_authoring->frozenBodies, bodyIndex, frozen);
}

bool RagdollEditorTool::SetJointFrozen(int childBody, bool frozen) {
  return m_authoring && SetFrozen(m_authoring->frozenJoints, childBody, frozen);
}

int ClampSimulationSpeedIndex(int index) {
  if (index < 0) return 0;
  const int maxIndex = static_cast<int>(kSimulationSpeedScales.size()) - 1;
  return index > maxIndex ? maxIndex : index;
}

float SimulationSpeedScaleForIndex(int index) {
  return kSimulationSpeedScales[static_cast<std::size_t>(ClampSimulationSpeedIndex(index))];
}

const char* SimulationSpeedLabelForIndex(int index) {
  return kSimulationSpeedLabels[static_cast<std::size_t>(ClampSimulationSpeedIndex(index))];
}

const char* SelectionModeName(int selectionMode) {
  switch (static_cast<SelectionMode>(selectionMode)) {
  case SelectionMode::Bodies: return "Bodies";
  case SelectionMode::Joints: return "Joints";
  case SelectionMode::Bones: return "Bones";
  default: return "Unknown";
  }
}

const char* ToolModeName(int toolMode) {
  switch (static_cast<ToolMode>(toolMode)) {
  case ToolMode::Select: return "Select";
  case ToolMode::Edit: return "Edit";
  case ToolMode::Move: return "Move";
  case ToolMode::Rotate: return "Rotate";
  default: return "Unknown";
  }
}

const char* ShapeTypeName(PhysicsShapeType type) {
  switch (type) {
  case PhysicsShapeType::Capsule: return "Capsule";
  case PhysicsShapeType::Box: return "Box";
  case PhysicsShapeType::TriangleMesh: return "Triangle Mesh";
  default: return "Unknown";
  }
}

const char* JointTypeName(PhysicsRagdollJointType type) {
  switch (type) {
  case PhysicsRagdollJointType::SwingTwist: return "Swing/Twist";
  case PhysicsRagdollJointType::Fixed: return "Fixed";
  default: return "Unknown";
  }
}

bool SameVector3Exact(const XVECTOR3& a, const XVECTOR3& b) {
  return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

bool SameMatrixExact(const XMATRIX44& a, const XMATRIX44& b) {
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      if (a.m[row][column] != b.m[row][column]) {
        return false;
      }
    }
  }
  return true;
}

bool SameMatrixVectorExact(const std::vector<XMATRIX44>& a, const std::vector<XMATRIX44>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (!SameMatrixExact(a[i], b[i])) {
      return false;
    }
  }
  return true;
}

bool SameNestedMatrixVectorExact(const std::vector<std::vector<XMATRIX44>>& a,
                                 const std::vector<std::vector<XMATRIX44>>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (!SameMatrixVectorExact(a[i], b[i])) {
      return false;
    }
  }
  return true;
}

bool SameVector3VectorExact(const std::vector<XVECTOR3>& a, const std::vector<XVECTOR3>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (!SameVector3Exact(a[i], b[i])) {
      return false;
    }
  }
  return true;
}

bool SamePhysicsShapeDesc(const PhysicsShapeDesc& a, const PhysicsShapeDesc& b) {
  return a.type == b.type &&
         SameVector3Exact(a.halfExtents, b.halfExtents) &&
         a.radius == b.radius &&
         a.halfHeight == b.halfHeight;
}

bool SamePhysicsBodyDesc(const PhysicsBodyDesc& a, const PhysicsBodyDesc& b) {
  return a.entityId == b.entityId &&
         a.boneIndex == b.boneIndex &&
         a.debugName == b.debugName &&
         SamePhysicsShapeDesc(a.shape, b.shape) &&
         SameMatrixExact(a.worldTransform, b.worldTransform) &&
         a.motion == b.motion &&
         a.mass == b.mass &&
         a.friction == b.friction &&
         a.restitution == b.restitution &&
         a.sensor == b.sensor;
}

bool SamePhysicsRagdollDesc(const PhysicsRagdollDesc& a, const PhysicsRagdollDesc& b) {
  if (a.entityId != b.entityId ||
      a.animationMode != b.animationMode ||
      a.animationToPhysicsBlend != b.animationToPhysicsBlend ||
      a.bones.size() != b.bones.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.bones.size(); ++i) {
    const auto& left = a.bones[i];
    const auto& right = b.bones[i];
    if (!SamePhysicsBodyDesc(left.body, right.body) ||
        left.parentBoneIndex != right.parentBoneIndex ||
        left.jointType != right.jointType ||
        !SameVector3Exact(left.jointWorldPosition, right.jointWorldPosition) ||
        !SameVector3Exact(left.parentJointTwistAxis, right.parentJointTwistAxis) ||
        !SameVector3Exact(left.parentJointPlaneAxis, right.parentJointPlaneAxis) ||
        !SameVector3Exact(left.childJointTwistAxis, right.childJointTwistAxis) ||
        !SameVector3Exact(left.childJointPlaneAxis, right.childJointPlaneAxis) ||
        left.swingLimitRadians != right.swingLimitRadians ||
        left.twistLimitRadians != right.twistLimitRadians) {
      return false;
    }
  }
  return true;
}

bool SameRagdollAnimationBinding(const PhysicsRagdollAnimationBinding& a,
                                 const PhysicsRagdollAnimationBinding& b) {
  return SamePhysicsRagdollDesc(a.referencePose, b.referencePose) &&
         SameMatrixVectorExact(a.bodyFromBone, b.bodyFromBone) &&
         SameVector3VectorExact(a.jointFromBone, b.jointFromBone) &&
         SameVector3VectorExact(a.parentJointTwistFromBody, b.parentJointTwistFromBody) &&
         SameVector3VectorExact(a.parentJointPlaneFromBody, b.parentJointPlaneFromBody) &&
         SameVector3VectorExact(a.childJointTwistFromBody, b.childJointTwistFromBody) &&
         SameVector3VectorExact(a.childJointPlaneFromBody, b.childJointPlaneFromBody) &&
         a.controlledBoneIndices == b.controlledBoneIndices &&
         SameNestedMatrixVectorExact(a.controlledBodyFromBone, b.controlledBodyFromBone);
}

bool SameAuthoringUndoContent(const AuthoringUndoSnapshot& a, const AuthoringUndoSnapshot& b) {
  return SameRagdollAnimationBinding(a.binding, b.binding) &&
         SamePhysicsRagdollDesc(a.animationPose, b.animationPose) &&
         a.parentCapsules == b.parentCapsules &&
         a.jointParentCapsules == b.jointParentCapsules &&
         a.frozenCapsules == b.frozenCapsules &&
         a.frozenJoints == b.frozenJoints &&
         a.contactJoints == b.contactJoints &&
         SameMatrixVectorExact(a.skeletonEditCombined, b.skeletonEditCombined);
}

int FindBodyForBone(const PhysicsRagdollAnimationBinding& binding, int boneIndex) {
  const auto& bones = binding.referencePose.bones;
  for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
    if (bones[static_cast<std::size_t>(i)].body.boneIndex == boneIndex) {
      return i;
    }
  }
  return -1;
}

int FindBodyControllingBone(const PhysicsRagdollAnimationBinding& binding, int boneIndex) {
  for (int bodyIndex = 0; bodyIndex < static_cast<int>(binding.controlledBoneIndices.size()); ++bodyIndex) {
    const std::vector<int>& controlledBones = binding.controlledBoneIndices[static_cast<std::size_t>(bodyIndex)];
    if (std::find(controlledBones.begin(), controlledBones.end(), boneIndex) != controlledBones.end()) {
      return bodyIndex;
    }
  }
  return -1;
}

void EnsureControlledBones(PhysicsRagdollAnimationBinding& binding) {
  const std::size_t bodyCount = binding.referencePose.bones.size();
  const std::size_t previousControlledCount = binding.controlledBoneIndices.size();
  const std::size_t previousFrameCount = binding.controlledBodyFromBone.size();
  if (binding.controlledBoneIndices.size() != bodyCount) {
    binding.controlledBoneIndices.resize(bodyCount);
  }
  if (binding.controlledBodyFromBone.size() != bodyCount) {
    binding.controlledBodyFromBone.resize(bodyCount);
  }
  for (std::size_t i = 0; i < bodyCount; ++i) {
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

void SyncParentBodiesFromBoneLinks(const PhysicsRagdollAnimationBinding& binding,
                                   std::vector<int>& parentBodies) {
  const auto& bones = binding.referencePose.bones;
  parentBodies.assign(bones.size(), -1);
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const int parentBody = FindBodyForBone(binding, bones[i].parentBoneIndex);
    if (parentBody >= 0 && parentBody != static_cast<int>(i)) {
      parentBodies[i] = parentBody;
    }
  }
}

void EnsureParentBodies(const PhysicsRagdollAnimationBinding& binding,
                        std::vector<int>& parentBodies) {
  const std::size_t bodyCount = binding.referencePose.bones.size();
  if (parentBodies.size() == bodyCount) {
    return;
  }
  if (parentBodies.empty()) {
    SyncParentBodiesFromBoneLinks(binding, parentBodies);
    return;
  }
  std::vector<int> previous = std::move(parentBodies);
  parentBodies.assign(bodyCount, -1);
  const std::size_t copyCount = (std::min)(previous.size(), bodyCount);
  for (std::size_t i = 0; i < copyCount; ++i) {
    const int parentBody = previous[i];
    if (parentBody >= 0 && parentBody < static_cast<int>(bodyCount) && parentBody != static_cast<int>(i)) {
      parentBodies[i] = parentBody;
    }
  }
}

std::vector<int> EnsureJointState(PhysicsRagdollAnimationBinding& binding,
                                  std::vector<int>& parentBodies,
                                  std::vector<int>& jointParentBodies,
                                  std::vector<uint8_t>& contactJoints) {
  EnsureParentBodies(binding, parentBodies);
  const std::size_t bodyCount = binding.referencePose.bones.size();

  if (jointParentBodies.size() != bodyCount) {
    const std::size_t previousSize = jointParentBodies.size();
    jointParentBodies.resize(bodyCount, kPhysicsRagdollJointInheritParent);
    for (std::size_t i = 0; i < (std::min)(previousSize, bodyCount); ++i) {
      int& jointParent = jointParentBodies[i];
      if (jointParent >= static_cast<int>(bodyCount) || jointParent == static_cast<int>(i)) {
        jointParent = kPhysicsRagdollJointInheritParent;
      }
    }
  }

  contactJoints.resize(bodyCount, 0u);

  std::vector<int> bodiesNeedingJointOffset;
  if (binding.jointFromBone.size() != bodyCount) {
    const std::size_t previousSize = binding.jointFromBone.size();
    binding.jointFromBone.resize(bodyCount, XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
    for (std::size_t i = previousSize; i < bodyCount; ++i) {
      bodiesNeedingJointOffset.push_back(static_cast<int>(i));
    }
  }

  binding.parentJointTwistFromBody.resize(bodyCount, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  binding.parentJointPlaneFromBody.resize(bodyCount, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  binding.childJointTwistFromBody.resize(bodyCount, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  binding.childJointPlaneFromBody.resize(bodyCount, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  return bodiesNeedingJointOffset;
}

void EnsureFreezeState(std::size_t bodyCount,
                       std::vector<uint8_t>& frozenBodies,
                       std::vector<uint8_t>& frozenJoints) {
  frozenBodies.resize(bodyCount, 0u);
  frozenJoints.resize(bodyCount, 0u);
}

bool IsFrozen(const std::vector<uint8_t>& frozenValues, int index) {
  return index >= 0 &&
         index < static_cast<int>(frozenValues.size()) &&
         frozenValues[static_cast<std::size_t>(index)] != 0u;
}

bool SetFrozen(std::vector<uint8_t>& frozenValues, int index, bool frozen) {
  if (index < 0 || index >= static_cast<int>(frozenValues.size())) {
    return false;
  }
  frozenValues[static_cast<std::size_t>(index)] = frozen ? 1u : 0u;
  return true;
}

int EffectiveJointParent(int childBody,
                         std::size_t bodyCount,
                         const std::vector<int>& parentBodies,
                         const std::vector<int>& jointParentBodies) {
  if (childBody < 0 || childBody >= static_cast<int>(bodyCount)) {
    return -1;
  }
  if (childBody < static_cast<int>(jointParentBodies.size())) {
    const int jointParent = jointParentBodies[static_cast<std::size_t>(childBody)];
    if (jointParent == kPhysicsRagdollJointDisabled) {
      return -1;
    }
    if (jointParent >= 0 && jointParent < static_cast<int>(bodyCount) && jointParent != childBody) {
      return jointParent;
    }
  }
  if (childBody < static_cast<int>(parentBodies.size())) {
    const int parentBody = parentBodies[static_cast<std::size_t>(childBody)];
    if (parentBody >= 0 && parentBody < static_cast<int>(bodyCount) && parentBody != childBody) {
      return parentBody;
    }
  }
  return -1;
}

} // namespace t850::ragdoll_editor
