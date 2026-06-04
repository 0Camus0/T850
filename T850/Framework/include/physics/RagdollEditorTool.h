#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <physics/PhysicsTypes.h>

namespace t850::ragdoll_editor {

enum class SelectionMode : int {
  Bodies = 0,
  Joints = 1,
  Bones = 2,
};

enum class ToolMode : int {
  Select = 0,
  Edit = 1,
  Move = 2,
  Rotate = 3,
};

enum class TransformSpace : int {
  Local = 0,
  Global = 1,
};

inline constexpr std::array<float, 9> kSimulationSpeedScales = {
    0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f};

inline constexpr std::array<const char*, 9> kSimulationSpeedLabels = {
    "0.125x", "0.25x", "0.5x", "1x", "2x", "4x", "8x", "16x", "32x"};

struct AuthoringUndoSnapshot {
  PhysicsRagdollAnimationBinding binding;
  PhysicsRagdollDesc animationPose;
  std::vector<int> parentCapsules;
  std::vector<int> jointParentCapsules;
  std::vector<uint8_t> frozenCapsules;
  std::vector<uint8_t> frozenJoints;
  std::vector<uint8_t> contactJoints;
  std::vector<XMATRIX44> skeletonEditCombined;
  int selectedCapsule = -1;
  int selectedJoint = -1;
  int selectedParentCapsule = -1;
  int selectedJointParentCapsule = -1;
  int selectedBone = -1;
  int selectedUnassignedBone = -1;
  int selectedAffectedBone = -1;
  int selectedHandle = -1;
  int selectionMode = static_cast<int>(SelectionMode::Bodies);
  int transformSpace = static_cast<int>(TransformSpace::Local);
  int gizmoMode = static_cast<int>(ToolMode::Select);
  std::string label;
};

struct UndoState {
  std::vector<AuthoringUndoSnapshot> stack;
  AuthoringUndoSnapshot scopeBefore;
  AuthoringUndoSnapshot pendingBefore;
  std::string scopeLabel;
  bool scopeActive = false;
  bool pendingActive = false;
  bool suppressRecording = false;
};

class RagdollEditorTool {
public:
  explicit RagdollEditorTool(PhysicsRagdollAuthoringDesc& authoring);

  PhysicsRagdollAuthoringDesc& Authoring();
  const PhysicsRagdollAuthoringDesc& Authoring() const;

  void EnsureState();
  int FindBodyForBone(int boneIndex) const;
  int FindBodyControllingBone(int boneIndex) const;
  int EffectiveJointParent(int childBody) const;

  bool IsBodyFrozen(int bodyIndex) const;
  bool IsJointFrozen(int childBody) const;
  bool SetBodyFrozen(int bodyIndex, bool frozen);
  bool SetJointFrozen(int childBody, bool frozen);

private:
  PhysicsRagdollAuthoringDesc* m_authoring = nullptr;
};

int ClampSimulationSpeedIndex(int index);
float SimulationSpeedScaleForIndex(int index);
const char* SimulationSpeedLabelForIndex(int index);

const char* SelectionModeName(int selectionMode);
const char* ToolModeName(int toolMode);
const char* ShapeTypeName(PhysicsShapeType type);
const char* JointTypeName(PhysicsRagdollJointType type);

bool SameVector3Exact(const XVECTOR3& a, const XVECTOR3& b);
bool SameMatrixExact(const XMATRIX44& a, const XMATRIX44& b);
bool SameMatrixVectorExact(const std::vector<XMATRIX44>& a, const std::vector<XMATRIX44>& b);
bool SameNestedMatrixVectorExact(const std::vector<std::vector<XMATRIX44>>& a,
                                 const std::vector<std::vector<XMATRIX44>>& b);
bool SameVector3VectorExact(const std::vector<XVECTOR3>& a, const std::vector<XVECTOR3>& b);
bool SamePhysicsShapeDesc(const PhysicsShapeDesc& a, const PhysicsShapeDesc& b);
bool SamePhysicsBodyDesc(const PhysicsBodyDesc& a, const PhysicsBodyDesc& b);
bool SamePhysicsRagdollDesc(const PhysicsRagdollDesc& a, const PhysicsRagdollDesc& b);
bool SameRagdollAnimationBinding(const PhysicsRagdollAnimationBinding& a,
                                 const PhysicsRagdollAnimationBinding& b);
bool SameAuthoringUndoContent(const AuthoringUndoSnapshot& a, const AuthoringUndoSnapshot& b);

int FindBodyForBone(const PhysicsRagdollAnimationBinding& binding, int boneIndex);
int FindBodyControllingBone(const PhysicsRagdollAnimationBinding& binding, int boneIndex);
void EnsureControlledBones(PhysicsRagdollAnimationBinding& binding);
void SyncParentBodiesFromBoneLinks(const PhysicsRagdollAnimationBinding& binding,
                                   std::vector<int>& parentBodies);
void EnsureParentBodies(const PhysicsRagdollAnimationBinding& binding,
                        std::vector<int>& parentBodies);
std::vector<int> EnsureJointState(PhysicsRagdollAnimationBinding& binding,
                                  std::vector<int>& parentBodies,
                                  std::vector<int>& jointParentBodies,
                                  std::vector<uint8_t>& contactJoints);
void EnsureFreezeState(std::size_t bodyCount,
                       std::vector<uint8_t>& frozenBodies,
                       std::vector<uint8_t>& frozenJoints);
bool IsFrozen(const std::vector<uint8_t>& frozenValues, int index);
bool SetFrozen(std::vector<uint8_t>& frozenValues, int index, bool frozen);
int EffectiveJointParent(int childBody,
                         std::size_t bodyCount,
                         const std::vector<int>& parentBodies,
                         const std::vector<int>& jointParentBodies);

} // namespace t850::ragdoll_editor
