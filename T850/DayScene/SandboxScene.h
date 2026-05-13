#pragma once
#include <core/Core.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/Timer.h>
#include <scene/SceneSetup.h>
#include <scene/RenderGraph.h>
#include <scene/WireframeSphere.h>
#include <scene/LineRenderer.h>
#include <scene/TextRenderer.h>
#include <physics/PhysicsDebugRenderer.h>
#include <physics/PhysicsTypes.h>
#include <debug/FrameDumper.h>
#include <gui/GUIManager.h>
#include <Config.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace t850 { class JoltPhysicsSystem; }

class SandboxScene : public t850::SceneBase
{
  enum {
    CHANGE_EXPOSURE = 0,
    CHANGE_BLOOM_FACTOR,
    CHANGE_BLOOM_THRESHOLD,
    CHANGE_TM_WHITE_LEVEL,
    CHANGE_TM_ADAPT_TAU,
    CHANGE_PCF_RADIUS,
    CHANGE_PCF_SAMPLES,
    CHANGE_SSAO_KERNEL_SIZE,
    CHANGE_SSAO_RADIUS,
    CHANGE_DOF_APERTURE,
    CHANGE_DOF_FOCAL_LENGHT,
    CHANGE_DOF_MAX_COC,
    CHANGE_DOF_FAR_SAMPLE,
    CHANGE_DOF_NEAR_SAMPLE,
    CHANGE_LIGHT_VOLUME_STEPS,
    CHANGE_GODRAYS_FACTOR,
    CHANGE_GAUSS_KERNEL_RADIUS,
    CHANGE_GAUSS_KERNEL_DEVIATION,
    CHANGE_FOV,
    CHANGE_LIGHT_INTENSITY,
    CHANGE_SHADOW_BIAS,
    CHANGE_SHADOW_MIN,
    CHANGE_ENV_FACTOR,
    CHANGE_IBL_FACTOR,
    CHANGE_MATERIAL_EMISSIVE_INTENSITY,
    CHANGE_MATERIAL_TRANSMISSION_MULTIPLIER,
    CHANGE_MATERIAL_REFRACTION_STRENGTH,
    CHANGE_PCF_TOOGLE,
    CHANGLE_SSAO_TOOGLE,
    CHANGE_DEBUG_RT,
    CHANGE_CUBEMAP,
    CHANGE_GAUSS_KERNEL_SAMPLE_COUNT,
    CHANGE_ACTIVE_GAUSS_KERNEL,
    // Animation controls (only active for skinned meshes)
    CHANGE_ANIM_SPEED,
    CHANGE_ANIM_SELECT,
    CHANGE_ANIM_MODE,     // selector: "Interpolation" / "Keyframe"
    CHANGE_SHOW_WIREFRAME,
    CHANGE_SHOW_SKELETON,
    CHANGE_SHOW_PHYSICS,
    CHANGE_MAX_NUM_OPTIONS
  };
public:
  SandboxScene() {}
  void OnUpdate(float _DtSecs) override;
  void OnDraw() override;
  void OnInput(InputManager* IManager) override;
  void OnLoadScene() override;
  void OnDestoryScene() override;
  void InitVars() override;
  void CreateAssets() override;
  void DestroyAssets() override;

  void PopulateGUI(t850::GUIManager& gui) override;
  void SyncToGUI(t850::GUIManager& gui) override;
  void SyncFromGUI(t850::GUIManager& gui) override;
  void DrawDevGui(t850::DevGuiContext& gui) override;
  void RequestDump() override { m_dumper.RequestDump(); }

  float DtSecs = 0.0f;
  t850::PrimitiveManager PrimitiveMgr;
  t850::PrimitiveInst Meshes[10];
  t850::PrimitiveInst Quads[10];

  t850::RenderGraph m_renderGraph;
  t850::SceneSetup m_guiSetup; // loaded from SandboxScene.json for GUI descriptors
  t850::FrameDumper m_dumper;
  int ChangeActiveGaussSelection = 1; // 0=Shadow, 1=Bloom, 2=DOF
  int m_debugRTSelection = 0;

  Camera Cam;
  Camera LightCam;
  Camera* ActiveCam = nullptr;

  XMATRIX44 VP;
  XMATRIX44 m;

  GaussFilter ShadowFilter;
  GaussFilter BloomFilter;
  GaussFilter NearDOFFilter;

  int EnvMapTexIndex = -1;
  int DiffuseIBLTexIndex = -1;
  int SpecularIBLTexIndex = -1;
  int BrdfLUTTexIndex = -1;
  int SheenIBLTexIndex = -1;
  int CharlieLUTTexIndex = -1;
  int SheenELUTTexIndex = -1;
  t850::EnvironmentMapSet EnvMaps;
  int GBufferPass = -1;
  int DeferredPass = -1;
  int Extra16FPass = -1;
  int DepthPass = -1;
  int ShadowAccumPass = -1;
  int ExtraHelperPass = -1;
  int BloomAccumPass = -1;
  int LuminanceMapPass = -1;
  int AdaptedLumCurrentPass = -1;
  int AdaptedLumPrevPass = -1;

  int m_currentCubemapIndex = 0;
  std::string m_pendingCubemap; // deferred load — set in SyncFromGUI, applied in OnUpdate

  t850::TextRenderer m_debugText;
  t850::WireframeSphere m_debugSphere;
  t850::LineRenderer m_lightArrowRenderer;
  t850::LineRenderer m_ragdollJointRenderer;
  t850::PhysicsDebugRenderer m_physicsDebugRenderer;
  t850::VertexBuffer* m_lightArrowVB = nullptr;
  t850::IndexBuffer* m_lightArrowIB = nullptr;
  unsigned m_lightArrowIndexCount = 0;
  t850::VertexBuffer* m_ragdollJointVB = nullptr;
  t850::IndexBuffer* m_ragdollJointIB = nullptr;
  unsigned m_ragdollJointVertexCapacity = 0;
  unsigned m_ragdollJointIndexCapacity = 0;
  unsigned m_ragdollJointIndexCount = 0;
  bool m_showCullStats = false;
  bool m_showAABBs = false;
  bool m_showWireframe = false;
  bool m_showSkeleton = false;
  bool m_showPhysics = false;
  bool m_drawLightDirection = false;
  bool m_profileReady = false;
  bool m_profileDirty = false;
  int m_selectedLightIndex = 0;
  std::vector<bool> m_lightAttachToCamera;
  std::string m_profileModelKey;
  int m_selectedProfileTargetIndex = 0;
  t850::SandboxProfileDesc m_profileBaselineState;
  t850::SandboxProfileDesc m_profileSavedState;
  t850::PhysicsRagdollAnimationBinding m_ragdollAnimationBinding;
  t850::PhysicsRagdollAnimationBinding m_ragdollGeneratedBinding;
  t850::PhysicsRagdollDesc m_ragdollAnimationPose;
  std::vector<int> m_ragdollParentCapsules;
  std::vector<int> m_ragdollJointParentCapsules;
  std::vector<t850::PhysicsBodyState> m_ragdollPhysicsStates;
  std::vector<int> m_ragdollPhysicsBoneIndices;
  std::vector<XMATRIX44> m_ragdollPhysicsCombinedMatrices;
  t850::PhysicsBodyHandle m_floorBody;
  bool m_driveRagdollFromAnimation = false;
  bool m_ragdollPhysicsDriven = false;
  bool m_ragdollDriveLogEmitted = false;
  bool m_ragdollPhysicsLogEmitted = false;
  bool m_ragdollFloorRuntimeDiagEmitted = false;
  bool m_ragdollEditDirty = false;
  bool m_ragdollEditHandleDragging = false;
  bool m_ragdollEditGizmoDragging = false;
  bool m_ragdollEditJointDragging = false;
  bool m_ragdollClearRequested = false;
  bool m_ragdollEditRebuildRequested = false;
  bool m_ragdollEditTopologyChangedThisFrame = false;
  int m_ragdollEditSelectedCapsule = -1;
  int m_ragdollEditSelectedJoint = -1;
  int m_ragdollEditSelectedUnassignedBone = -1;
  int m_ragdollEditSelectedAffectedBone = -1;
  bool m_ragdollBoneSelectionActive = false;
  bool m_ragdollBoneMarqueeDragging = false;
  float m_ragdollBoneMarqueeStartX = 0.0f;
  float m_ragdollBoneMarqueeStartY = 0.0f;
  float m_ragdollBoneMarqueeCurrentX = 0.0f;
  float m_ragdollBoneMarqueeCurrentY = 0.0f;
  std::vector<int> m_ragdollBoneSelectionPending;
  int m_ragdollBoneSelectionPreviousSelectionMode = 0;
  int m_ragdollBoneSelectionPreviousGizmoMode = 0;
  int m_ragdollEditSelectionMode = 0; // 0=capsules, 1=joints, 2=bones
  int m_ragdollEditSelectedHandle = -1;
  int m_ragdollEditGizmoMode = 0; // 0=select, 1=edit capsule, 2=translate, 3=rotate
  int m_ragdollEditGizmoAxis = -1;
  float m_ragdollEditGizmoLastParameter = 0.0f;
  XVECTOR3 m_ragdollEditGizmoLastVector = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 m_ragdollEditGizmoDragCenter = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 m_ragdollEditGizmoDragAxis = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
  int m_ragdollEditJointAxis = -1;
  float m_ragdollEditJointLastParameter = 0.0f;
  XVECTOR3 m_ragdollEditJointLastVector = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 m_ragdollEditJointDragCenter = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 m_ragdollEditJointDragAxis = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
  int m_ragdollEditRenamingCapsule = -1;
  bool m_ragdollEditRenameFocusPending = false;
  std::array<char, 128> m_ragdollEditNameBuffer{};
  std::string m_ragdollEditSavePath;
  std::vector<uint8_t> m_ragdollFrozenCapsules;
  std::vector<uint8_t> m_ragdollFrozenJoints;
  std::vector<uint8_t> m_ragdollContactJoints;
  bool m_skeletonEditMode = false;
  bool m_skeletonEditWasPlaying = false;
  bool m_skeletonEditDragging = false;
  bool m_skeletonEditDirty = false;
  int m_skeletonEditSelectedBone = -1;
  std::vector<XMATRIX44> m_skeletonEditBindCombined;
  std::vector<XMATRIX44> m_skeletonEditCombined;
  std::string m_skeletonEditSavePath;

  // Orbit camera state
  XVECTOR3 m_orbitTarget;    // center of the model (world space)
  XVECTOR3 m_panOffset;      // accumulated pan offset
  float m_orbitYaw   = 0.0f;
  float m_orbitPitch = 0.3f;
  float m_orbitDist  = 5.0f;
  float m_modelRadius = 1.0f;

  void ComputeOrbitCamera();
  void FitModelToView();
  void EnsureLightRuntimeState();
  void UpdateAttachedLights();
  void SyncLightCameraFromDirectionalLight();
  bool AdjustSelectedDirectionalLightFromMouse(float dx, float dy);
  void DrawSelectedDirectionalLightArrow();
  void DriveRagdollFromAnimation(float deltaSeconds);
  void UpdateSkeletonFromRagdollPhysics();
  void SwitchRagdollToPhysics();
  bool ResetRagdollPhysicsAndAnimation();
  void CreatePhysicsFloor(t850::JoltPhysicsSystem& physics);
  void LogRagdollFloorDiagnostics(const char* stage);
  bool EnterSkeletonEditMode();
  void ExitSkeletonEditMode();
  void DrawSkeletonEditPanel(t850::DevGuiContext& gui);
  void DrawSkinningAuthoringPanel(t850::DevGuiContext& gui);
  bool HandleSkeletonEditInput(InputManager* input, bool imguiWantsMouse);
  bool ApplySkeletonEditPose();
  bool ResetSkeletonEditPose();
  bool LoadSkeletonEditPose();
  bool SaveSkeletonEditPose();
  std::string BuildSkeletonEditSavePath() const;
  std::string BuildRagdollEditSavePath() const;
  void DrawRagdollCapsuleEditPanel(t850::DevGuiContext& gui);
  bool LoadRagdollEditPose();
  bool SaveRagdollEditPose();
  bool ResetRagdollEditPose();
  bool ResetSelectedRagdollCapsule();
  void DrawRagdollJointEditPanel(t850::DevGuiContext& gui);
  int FindRagdollCapsuleForBone(int boneIndex) const;
  int FindRagdollCapsuleControllingBone(int boneIndex) const;
  int FindGeneratedRagdollCapsuleForBone(int boneIndex) const;
  void EnsureRagdollControlledBones();
  void SelectRagdollEditCapsule(int capsuleIndex, bool syncBoneSelection);
  void SyncRagdollParentCapsulesFromBoneLinks();
  void EnsureRagdollParentCapsules();
  void EnsureRagdollJointState();
  void EnsureRagdollFreezeState();
  bool IsRagdollCapsuleFrozen(int capsuleIndex) const;
  bool IsRagdollJointFrozen(int childCapsule) const;
  void SetRagdollCapsuleFrozen(int capsuleIndex, bool frozen);
  void SetRagdollJointFrozen(int childCapsule, bool frozen);
  int GetRagdollEffectiveJointParentCapsule(int childCapsule) const;
  bool UpdateRagdollJointOffsetFromWorld(int childCapsule);
  bool ApplyRagdollParentCapsuleLinks();
  bool SetRagdollCapsuleParent(int childCapsule, int parentCapsule);
  bool ClearRagdollCapsuleParent(int childCapsule);
  bool SetRagdollCapsuleJoint(int childCapsule, int parentCapsule);
  bool SetRagdollCapsuleJointAtContact(int childCapsule, int parentCapsule);
  bool ComputeRagdollCapsuleContactAnchor(int childCapsule, int parentCapsule, XVECTOR3& outAnchor);
  bool ClearRagdollCapsuleJoint(int childCapsule);
  bool ClearRagdollCapsuleJointBetween(int capsuleA, int capsuleB);
  bool AddControlledBoneToSelectedCapsule(int boneIndex);
  bool RemoveControlledBoneFromSelectedCapsule(int boneIndex);
  bool ClearRagdollCapsules();
  bool RebuildRagdollParentLinks();
  bool BuildDefaultRagdollCapsuleForBone(int boneIndex, t850::PhysicsRagdollBoneDesc& outBone, XMATRIX44& outBodyFromBone) const;
  bool CreateRagdollCapsuleForBone(int boneIndex);
  bool DeleteSelectedRagdollCapsule();
  bool UpdateRagdollReferenceBodyFromLocal(int capsuleIndex);
  bool SetRagdollEditCapsuleWorldTransform(int capsuleIndex, const XMATRIX44& bodyWorld, bool rebuildRagdoll);
  bool MoveRagdollEditCapsuleByWorldDelta(int capsuleIndex, const XVECTOR3& worldDelta, bool rebuildRagdoll);
  bool RotateRagdollEditCapsuleWorld(int capsuleIndex, const XVECTOR3& axisWorld, float angleRadians, bool rebuildRagdoll);
  bool FlipRagdollEditCapsuleLocalAxis(int capsuleIndex, int axisIndex);
  bool ApplyRagdollEditPose(bool rebuildRagdoll);
  bool RecreateRagdollFromPose(const t850::PhysicsRagdollDesc& pose);
  bool GetCurrentRagdollEditCapsuleWorld(int capsuleIndex, XMATRIX44& outWorld);
  bool SetRagdollEditJointWorldPosition(int childCapsule, const XVECTOR3& worldPosition);
  bool MoveRagdollEditJointByWorldDelta(int childCapsule, const XVECTOR3& worldDelta);
  bool RotateRagdollEditJointWorld(int childCapsule, const XVECTOR3& axisWorld, float angleRadians);
  bool FlipRagdollEditJointLocalAxis(int childCapsule, int axisIndex);
  bool GetRagdollJointVisualFrame(int childCapsule,
                                  XVECTOR3& outJoint,
                                  XVECTOR3& outParentCenter,
                                  XVECTOR3& outChildCenter,
                                  XVECTOR3& outParentTwistAxis,
                                  XVECTOR3& outChildTwistAxis,
                                  XVECTOR3& outChildPlaneAxis,
                                  float& outSize);
  bool GetRagdollJointGizmoFrame(int childCapsule, XVECTOR3& outCenter, std::array<XVECTOR3, 3>& outAxes, float& outSize);
  bool PickRagdollEditJoint(float mouseX, float mouseY, float thresholdPixels, int& outChildCapsule);
  bool PickRagdollEditJointGizmo(float mouseX, float mouseY, int& outAxis);
  bool BeginRagdollEditJointGizmoDrag(float mouseX, float mouseY);
  bool DragRagdollEditJointGizmo(float mouseX, float mouseY);
  void DrawRagdollJointGizmos(bool editable);
  void ReleaseRagdollJointDebugBuffers();
  bool UploadRagdollJointDebugGeometry(const std::vector<float>& vertices, const std::vector<unsigned int>& indices);
  void DrawRagdollJointDebugOverlay();
  bool GetRagdollEditGizmoFrame(int capsuleIndex, XVECTOR3& outCenter, std::array<XVECTOR3, 3>& outAxes, float& outSize);
  bool BuildRagdollEditHandlePoints(int capsuleIndex, std::array<XVECTOR3, 7>& outPoints);
  bool PickRagdollEditHandle(float mouseX, float mouseY, float thresholdPixels, int& outCapsuleIndex, int& outHandleIndex);
  bool PickRagdollEditCapsule(float mouseX, float mouseY, float thresholdPixels, int& outCapsuleIndex);
  bool PickRagdollEditTransformGizmo(float mouseX, float mouseY, int& outAxis);
  bool BeginRagdollEditTransformGizmoDrag(float mouseX, float mouseY);
  bool DragRagdollEditTransformGizmo(float mouseX, float mouseY);
  void DrawRagdollEditTransformGizmo();
  bool DragRagdollEditHandle(int capsuleIndex, int handleIndex, const XVECTOR3& worldDelta);
  bool GetRagdollAuthoringBoneWorldTransform(int boneIndex, XMATRIX44& outWorld) const;
  bool GetSkeletonEditBoneWorldTransform(int boneIndex, XMATRIX44& outWorld) const;
  int FindSkeletonEditDisplayEndpoint(int boneIndex) const;
  bool BuildSkeletonEditBoneOctahedron(int boneIndex, float widthScale, std::array<XVECTOR3, 6>& outPoints) const;
  int PickSkeletonEditBone(float mouseX, float mouseY, float thresholdPixels) const;
  void PickSkeletonEditBonesInScreenRect(float minX, float minY, float maxX, float maxY, std::vector<int>& outBones) const;
  bool GetSkeletonEditBoneWorldPosition(int boneIndex, XVECTOR3& outWorld) const;
  bool SetSkeletonEditBoneWorldPosition(int boneIndex, const XVECTOR3& worldPosition);
  std::array<float, 3> GetSkeletonEditBoneScale(int boneIndex) const;
  bool SetSkeletonEditBoneScale(int boneIndex, const std::array<float, 3>& scale);
  void LoadSandboxProfile();
  void SaveSandboxProfile();
  void CaptureSandboxProfileState(t850::SandboxProfileDesc& state);
  void ApplySandboxProfileState(const t850::SandboxProfileDesc& state);
  t850::SandboxProfileDesc BuildSparseSandboxProfile(const t850::SandboxProfileDesc& current) const;
  bool SandboxProfileStatesEqual(const t850::SandboxProfileDesc& a, const t850::SandboxProfileDesc& b) const;
};
