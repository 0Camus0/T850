#pragma once
#include <core/Core.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <utils/CameraProfiles.h>
#include <utils/Spline.h>
#include <utils/Timer.h>
#include <scene/SceneSetup.h>
#include <scene/RenderGraph.h>
#include <scene/EditorSceneFile.h>
#include <scene/WireframeSphere.h>
#include <scene/LineRenderer.h>
#include <scene/TextRenderer.h>
#include <physics/PhysicsDebugRenderer.h>
#include <navigation/NavigationDebugRenderer.h>
#include <navigation/NavigationSystem.h>
#include <physics/PhysicsTypes.h>
#include <physics/RagdollEditorTool.h>
#include <debug/FrameDumper.h>
#include <Config.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace t850 {
class JoltPhysicsSystem;
class RenderSkinnedMesh;
}

struct SceneTemplateLaunchDesc {
  std::string sceneFilePath;
  std::string modelPath;
  int width = 0;
  int height = 0;
  int startScene = 4;
  bool guiOnStart = false;
};

class SceneTemplate : public t850::SceneBase, public t850::CameraCollisionWorld
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
    CHANGE_LIGHT_RADIUS_SCALE,
    CHANGE_LIGHT_INTENSITY_SCALE,
    CHANGE_LIGHTMAP_INTENSITY,
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
    CHANGE_LUMINANCE_MODE,
    // Animation controls (only active for skinned meshes)
    CHANGE_ANIM_SPEED,
    CHANGE_ANIM_SELECT,
    CHANGE_ANIM_MODE,     // selector: "Interpolation" / "Keyframe"
    CHANGE_SHOW_WIREFRAME,
    CHANGE_SHOW_SKELETON,
    CHANGE_SHOW_PHYSICS,
    CHANGE_SHOW_NAVMESH,
    CHANGE_SHOW_LIGHT_VOLUMES,
    CHANGE_POINT_LIGHTS_ENABLED,
    CHANGE_DEBUG_LUMINANCE,
    CHANGE_MAX_NUM_OPTIONS
  };
public:
  SceneTemplate() {}
  void OnUpdate(float _DtSecs) override;
  void OnDraw() override;
  void OnInput(InputManager* IManager) override;
  void OnLoadScene() override;
  void OnDestoryScene() override;
  void InitVars() override;
  void CreateAssets() override;
  void DestroyAssets() override;

  void DrawDevGui(t850::DevGuiContext& gui) override;
  void SetLaunchDesc(const SceneTemplateLaunchDesc& desc);
  void SetFinalOutputRT(int rtHandle) { m_finalOutputRT = rtHandle; }
  void SetRenderSize(int width, int height);
  void ResizeRenderTargets(int width, int height, int finalOutputRT);
  void SetIgnoreImGuiMouseCaptureForInput(bool ignore) { m_ignoreImGuiMouseCaptureForInput = ignore; }
  int RenderViewportWidth() const;
  int RenderViewportHeight() const;
  bool EnsureNavMeshBuilt();
  void InitializeNavTestAgents();
  void UpdateNavTestAgents(float dtSecs);
  void PlanNavTestAgentPaths();
#ifdef OS_ANDROID
  bool HandleAndroidVirtualControls(AInputEvent* event);
  bool AndroidVirtualControlsActive() const;
  void DrawAndroidVirtualControls(bool guiVisible);
  void DrawAndroidPhysicsPanel(t850::DevGuiContext& gui);
  void ResetAndroidVirtualControls();
#endif
  void RequestDump() override { m_dumper.RequestDump(); }
  void ResetViewInput() override;
  const std::string& ActiveSceneFilePath() const;
  const std::string& ActiveModelPath() const;
  int ActiveStartScene() const;

  float DtSecs = 0.0f;
  t850::PrimitiveManager PrimitiveMgr;
  static constexpr int kMaxSandboxMeshes = 64;
  t850::PrimitiveInst Meshes[kMaxSandboxMeshes];
  t850::PrimitiveInst Quads[10];
  int m_meshCount = 0;
  int m_finalOutputRT = -1;
  int m_renderWidth = 0;
  int m_renderHeight = 0;
  bool m_ignoreImGuiMouseCaptureForInput = false;
  bool m_hasLaunchDesc = false;
  SceneTemplateLaunchDesc m_launchDesc;

  t850::RenderGraph m_renderGraph;
  t850::SceneSetup m_controlSetup;
  t850::FrameDumper m_dumper;
  int ChangeActiveGaussSelection = 1; // 0=Shadow, 1=Bloom, 2=DOF
  int m_debugRTSelection = 0;

  Camera Cam;
  Camera LightCam;
  Camera* ActiveCam = nullptr;
  t850::CameraController m_cameraController;
  int m_cameraProfileSelection = 0;
  float m_mouseSensitivityX = 1.0f;
  float m_mouseSensitivityY = 1.0f;
#ifdef OS_ANDROID
  bool AndroidVirtualControlsVisible() const;
  int m_androidMovePointerId = -1;
  int m_androidLookPointerId = -1;
  int m_androidJumpPointerId = -1;
  int m_androidRunPointerId = -1;
  XVECTOR2 m_androidMoveAxis;
  XVECTOR2 m_androidLookAxis;
  bool m_androidJump = false;
  bool m_androidRun = false;
#endif

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
  int AdaptedLumCurrentPass = -1;
  int AdaptedLumPrevPass = -1;

  int m_currentCubemapIndex = 0;
  std::string m_currentCubemapPath;
  std::string m_pendingCubemap; // deferred load — set in SyncFromGUI, applied in OnUpdate

  t850::TextRenderer m_debugText;
  t850::WireframeSphere m_debugSphere;
  t850::LineRenderer m_lightArrowRenderer;
  t850::PhysicsDebugRenderer m_physicsDebugRenderer;
  t850::navigation::NavMeshDebugRenderer m_navMeshDebugRenderer;
  t850::navigation::NavMesh m_navMesh;
  t850::navigation::NavMeshBuildSettings m_navMeshBuildSettings;
  float m_navMeshLastBuildMs = 0.0f;
  bool m_navMeshLastBuildFromCache = false;
  t850::VertexBuffer* m_lightArrowVB = nullptr;
  t850::IndexBuffer* m_lightArrowIB = nullptr;
  unsigned m_lightArrowIndexCount = 0;
  bool m_showCullStats = false;
  bool m_showAABBs = false;
  bool m_showWireframe = false;
  bool m_showSkeleton = false;
  bool m_showPhysics = false;
  bool m_showNavMesh = false;
  float m_navMeshDebugOffset = 0.01f;
  int m_navMeshDebugShapeMode = 0;
  bool m_navMeshBuildAttempted = false;
  bool m_showLightVolumes = false;
  bool m_drawLightDirection = false;
  bool m_profileReady = false;
  bool m_profileDirty = false;
  int m_selectedLightIndex = 0;
  std::vector<bool> m_lightAttachToCamera;
  bool m_loadedEditorScene = false;
  std::string m_loadedEditorScenePath;
  std::vector<std::string> m_sceneObjectNames;
  std::string m_primaryRagdollResourcePath;
  std::vector<std::string> m_sceneMeshPaths;
  std::vector<std::string> m_sceneRagdollPaths;
  std::vector<float> m_sceneObjectYawDegrees;
  std::vector<float> m_sceneNavAgentFrontYawOffsets;
  std::vector<float> m_sceneNavAgentFaceYawSigns;
  std::vector<std::string> m_sceneNavAgentTargetModes;
  std::vector<float> m_sceneNavAgentFollowDistances;
  std::vector<float> m_sceneNavAgentSideOffsets;
  std::vector<float> m_sceneNavAgentFormationDepthSteps;
  std::vector<int> m_sceneNavAgentSlots;
  std::vector<t850::scene::SceneObjectPhysicsDesc> m_scenePhysicsAuthoring;
  std::vector<t850::scene::ScenePhysicsEntityDesc> m_scenePhysicsEntities;
  std::vector<t850::scene::SceneObjectNavigationDesc> m_sceneNavigationAuthoring;
  std::vector<t850::scene::SceneObjectRagdollDesc> m_sceneRagdollAuthoring;
  std::vector<t850::scene::SceneSplineDesc> m_sceneSplines;
  t850::Spline m_runtimeSpline;
  t850::SplineAgent m_runtimeSplineAgent;
  bool m_runtimeSplineActive = false;
  int m_runtimeSplineCameraIndex = -1;
  bool m_hasAuthoredLightCamera = false;
  int m_authoredLightCameraAttachedLight = -1;
  XVECTOR3 m_authoredLightCameraLinearVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 m_authoredLightCameraTargetVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 m_authoredLightCameraAngularVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  bool m_hasAuthoredNavMesh = false;
  t850::scene::SceneNavigationMeshDesc m_authoredNavMesh;
  bool m_hasAuthoredPlayer = false;
  t850::scene::ScenePhysicsEntityDesc m_authoredPlayer;
  struct SceneRagdollRuntime {
    int meshIndex = -1;
    std::string resourcePath;
    t850::PhysicsRagdollAnimationBinding binding;
    t850::PhysicsRagdollDesc pose;
    std::vector<t850::PhysicsBodyState> physicsStates;
    std::vector<int> physicsBoneIndices;
    std::vector<XMATRIX44> physicsCombinedMatrices;
    bool driveLogEmitted = false;
    bool physicsDriven = false;
    bool physicsLogEmitted = false;
  };
  std::vector<SceneRagdollRuntime> m_sceneRagdolls;
  struct NavTestAgentRuntime {
    int meshIndex = -1;
    XVECTOR3 home = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 navPosition = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 visualOffset = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
    XVECTOR3 desiredTarget = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 target = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 lastPathStart = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 lastPathEnd = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 lastPathFirst = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 navToOriginOffset = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
    XVECTOR3 authoredCharacterPosition = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 authoredCharacterRotationDeg = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
    t850::KinematicCharacterSettings characterSettings;
    t850::KinematicCharacterController physicsController;
    XVECTOR3 physicsTraversalStart = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 physicsTarget = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    XVECTOR3 physicsLastNavPosition = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    std::vector<XVECTOR3> path;
    std::vector<t850::navigation::NavTraversalType> pathSegmentTypes;
    std::string lastPathError;
    int waypointIndex = 0;
    int physicsTargetWaypointIndex = 0;
    int followSlot = 0;
    unsigned int pathGeneration = 0;
    float repathCooldownSec = 0.0f;
    float physicsTraversalTimeSec = 0.0f;
    float physicsTraversalDurationSec = 0.0f;
    float physicsStuckTimeSec = 0.0f;
    float frontYawOffsetDeg = 0.0f;
    float faceYawSign = 1.0f;
    float visualYawDeg = 0.0f;
    int behaviorMode = 2; // 0=furthest, 1=random, 2=follow player
    float followDistance = 0.0f;
    float sideOffset = 0.0f;
    float formationDepthStep = 0.0f;
    int characterRuntimePath = 0; // 0=kinematic navmesh path, 1=Jolt collision path
    t850::navigation::NavTraversalType physicsTraversalType = t850::navigation::NavTraversalType::Walk;
    bool returning = false;
    bool active = false;
    bool needsPath = false;
    bool physicsTraversalActive = false;
    bool physicsWasAirborne = false;
    bool hasAuthoredCharacter = false;
    bool targetInitialized = false;
    bool lastPathSuccess = false;
  };
  std::vector<NavTestAgentRuntime> m_navTestAgents;
  std::vector<XVECTOR3> m_navTestCandidatePoints;
  bool m_navTestInitialized = false;
  float m_navTestSpeed = 3.0f;
  int m_navTestMode = 2;
  int m_navTestAppliedMode = 2;
  uint32_t m_navTestRandomState = 0x6d2b79f5u;
  float m_navTestDiagAccumSec = 0.0f;
  int m_selectedSkinningMeshIndex = 0;
  int m_selectedAnimationMeshIndex = 0;
  std::string m_profileModelKey;
  bool m_profileEmbeddedInScene = false;
  int m_selectedProfileTargetIndex = 0;
  t850::SandboxProfileDesc m_profileBaselineState;
  t850::SandboxProfileDesc m_profileSavedState;
  t850::PhysicsRagdollAnimationBinding m_ragdollAnimationBinding;
  t850::PhysicsRagdollAnimationBinding m_ragdollGeneratedBinding;
  t850::PhysicsRagdollDesc m_ragdollAnimationPose;
  using RagdollAuthoringUndoSnapshot = t850::ragdoll_editor::AuthoringUndoSnapshot;
  t850::ragdoll_editor::UndoState m_ragdollUndoState;
  std::vector<int> m_ragdollParentCapsules;
  std::vector<int> m_ragdollJointParentCapsules;
  std::vector<t850::PhysicsBodyState> m_ragdollPhysicsStates;
  std::vector<int> m_ragdollPhysicsBoneIndices;
  std::vector<XMATRIX44> m_ragdollPhysicsCombinedMatrices;
  t850::PhysicsBodyHandle m_floorBody;
  t850::PhysicsBodyHandle m_ragdollSimulationGrabHandle;
  bool m_driveRagdollFromAnimation = false;
  bool m_ragdollPhysicsDriven = false;
  bool m_ragdollDriveLogEmitted = false;
  bool m_ragdollPhysicsLogEmitted = false;
  bool m_ragdollFloorRuntimeDiagEmitted = false;
  int m_ragdollSimulationSpeedIndex = 3;
  bool m_ragdollUseFixedSimulationDelta = false;
  bool m_ragdollConfigSpeedApplied = false;
  bool m_ragdollAutoStartAttempted = false;
  bool m_ragdollSimulationGrabActive = false;
  int m_ragdollSimulationGrabBodyIndex = -1;
  float m_ragdollSimulationGrabDepth = 0.0f;
  XVECTOR3 m_ragdollSimulationGrabCenterOffset = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 m_ragdollSimulationGrabPreviousTarget = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 m_ragdollSimulationGrabReleaseVelocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  bool m_ragdollEditDirty = false;
  bool m_ragdollEditHandleDragging = false;
  bool m_ragdollEditGizmoDragging = false;
  bool m_ragdollEditJointDragging = false;
  bool m_ragdollSyncCliAttempted = false;
  int m_ragdollSyncCliWaitFrames = 0;
  bool m_ragdollClearRequested = false;
  bool m_ragdollEditRebuildRequested = false;
  bool m_ragdollEditTopologyChangedThisFrame = false;
  int m_ragdollEditSelectedCapsule = -1;
  int m_ragdollEditSelectedJoint = -1;
  int m_ragdollEditSelectedParentCapsule = -1;
  int m_ragdollEditSelectedJointParentCapsule = -1;
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
  int m_ragdollEditTransformSpace = 0; // 0=local, 1=global transform
  bool m_ragdollContextMenuRequested = false;
  bool m_ragdollContextMenuRightButtonHeld = false;
  float m_ragdollContextMenuX = 0.0f;
  float m_ragdollContextMenuY = 0.0f;
  int m_ragdollEditSelectedHandle = -1;
  int m_ragdollEditGizmoMode = 0; // 0=select, 1=edit capsule, 2=translate, 3=rotate
  std::string m_ragdollLastSyncStatus;
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
  bool m_skeletonEditPrevShowSkeleton = false;
  bool m_skeletonEditPrevShowPhysics = false;
  bool m_skeletonEditDragging = false;
  bool m_skeletonEditDirty = false;
  int m_skeletonEditSelectedBone = -1;
  std::vector<XMATRIX44> m_skeletonEditBindCombined;
  std::vector<XMATRIX44> m_skeletonEditCombined;
  std::string m_skeletonEditSavePath;
  bool m_skeletonPreviewBoneActive = false;
  int m_skeletonPreviewBoneIndex = -1;
  std::vector<XMATRIX44> m_skeletonPreviewOriginalCombined;

  // Orbit camera state
  XVECTOR3 m_orbitTarget;    // center of the model (world space)
  XVECTOR3 m_panOffset;      // accumulated pan offset
  float m_orbitYaw   = 0.0f;
  float m_orbitPitch = 0.3f;
  float m_orbitDist  = 5.0f;
  float m_modelRadius = 1.0f;

  void ComputeOrbitCamera();
  void FitModelToView();
  void UpdateCameraProjectionForRenderViewport();
  bool SetCameraProfile(t850::CameraProfileType type);
  void SyncOrbitProfileFromSandbox();
  void SyncSandboxOrbitFromProfile();
  t850::CameraInputState BuildCameraInputState(InputManager* input, bool imguiWantsMouse) const;
  bool SweepCapsule(const t850::CameraCollisionSweep& sweep, t850::CameraCollisionHit& outHit) const override;
  bool SweepBox(const t850::CharacterBoxSweep& sweep, t850::CameraCollisionHit& outHit) const override;
  bool QueryTriggerTouch(const t850::CharacterTriggerQuery& query, t850::CharacterTriggerTouch& outTouch) const override;
  bool LoadEditorSceneAssets(const std::string& scenePath);
  void ApplyEditorSceneCameraAndLights(const t850::scene::EditorSceneFile& scene);
  int GetRuntimeMeshCount() const;
  t850::RenderSkinnedMesh* GetSkinnedMeshForIndex(int meshIndex) const;
  std::vector<std::string> BuildSkinnedMeshOptions(std::vector<int>* outMeshIndices = nullptr) const;
  int ClampSkinnedMeshSelection(int preferredMeshIndex) const;
  t850::RenderSkinnedMesh* GetSelectedSkinningMesh() const;
  t850::RenderSkinnedMesh* GetSelectedAnimationMesh() const;
  void EnsureLightRuntimeState();
  void UpdateAttachedLights();
  void SyncLightCameraFromDirectionalLight();
  bool AdjustSelectedDirectionalLightFromMouse(float dx, float dy);
  void DrawSelectedDirectionalLightArrow();
  bool AttachSceneObjectRagdoll(int meshIndex, const std::string& meshPath, const std::string& ragdollPath);
  SceneRagdollRuntime* FindSceneRagdollRuntime(int meshIndex);
  const SceneRagdollRuntime* FindSceneRagdollRuntime(int meshIndex) const;
  bool IsSceneRagdollPhysicsDriven(int meshIndex) const;
  void DriveSceneRagdollsFromAnimation(float deltaSeconds);
  bool SwitchSceneRagdollsToPhysics(int meshIndexFilter = -1);
  bool ResetSceneRagdollPhysicsAndAnimation(int meshIndex);
  void UpdateSceneSkeletonsFromRagdollPhysics();
  void InitializeSceneSplinePlayback(const t850::scene::EditorSceneFile& scene);
  bool UpdateSceneSplinePlayback(float deltaSeconds);
  void DriveRagdollFromAnimation(float deltaSeconds);
  void UpdateSkeletonFromRagdollPhysics();
  void SwitchRagdollToPhysics();
  bool ResetRagdollPhysicsAndAnimation();
  bool HandleRagdollSimulationGrabInput(InputManager* input, bool imguiWantsMouse);
  bool PickRagdollSimulationBody(float mouseX,
                                 float mouseY,
                                 int& outBodyIndex,
                                 t850::PhysicsBodyState& outState,
                                 XVECTOR3& outHitPoint,
                                 float& outHitDistance);
  bool BeginRagdollSimulationGrab(float mouseX, float mouseY);
  bool UpdateRagdollSimulationGrab(float mouseX, float mouseY);
  void EndRagdollSimulationGrab(bool applyThrow);
  void CreatePhysicsFloor(t850::JoltPhysicsSystem& physics);
  void LogRagdollFloorDiagnostics(const char* stage);
  bool EnterSkeletonEditMode();
  void ExitSkeletonEditMode();
  void DrawRagdollPhysicsSimulationPanel(t850::DevGuiContext& gui);
  void DrawSkeletonEditPanel(t850::DevGuiContext& gui);
  bool HandleSkeletonEditInput(InputManager* input, bool imguiWantsMouse);
  RagdollAuthoringUndoSnapshot CaptureRagdollUndoSnapshot(const char* label = "") const;
  bool RagdollUndoContentEquals(const RagdollAuthoringUndoSnapshot& a, const RagdollAuthoringUndoSnapshot& b) const;
  void BeginRagdollUndoScope(const char* label);
  void EndRagdollUndoScope(bool gestureActive);
  void PushRagdollUndoSnapshot(const RagdollAuthoringUndoSnapshot& snapshot);
  bool CanUndoRagdollAuthoringEdit() const;
  bool UndoRagdollAuthoringEdit();
  const char* CurrentRagdollUndoLabel() const;
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
  void EnsureRagdollJointFrames();
  void EnsureRagdollFreezeState();
  bool IsRagdollCapsuleFrozen(int capsuleIndex) const;
  bool IsRagdollJointFrozen(int childCapsule) const;
  void SetRagdollCapsuleFrozen(int capsuleIndex, bool frozen);
  void SetRagdollJointFrozen(int childCapsule, bool frozen);
  int GetRagdollEffectiveJointParentCapsule(int childCapsule) const;
  bool UpdateRagdollJointOffsetFromWorld(int childCapsule);
  bool UpdateRagdollJointFrameOffsetsFromWorld(int childCapsule);
  void UpdateRagdollJointFrameOffsetsForBody(int capsuleIndex);
  bool ResetRagdollJointFrameToBodyAxes(int childCapsule);
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
  bool CreateRagdollBoxForBone(int boneIndex);
  bool DeleteSelectedRagdollCapsule();
  bool MorphRagdollBodyToBox(int capsuleIndex);
  bool MorphRagdollBodyToCapsule(int capsuleIndex);
  bool UpdateRagdollReferenceBodyFromLocal(int capsuleIndex);
  bool SetRagdollEditCapsuleWorldTransform(int capsuleIndex, const XMATRIX44& bodyWorld, bool rebuildRagdoll);
  bool MoveRagdollEditCapsuleByWorldDelta(int capsuleIndex, const XVECTOR3& worldDelta, bool rebuildRagdoll);
  bool RotateRagdollEditCapsuleWorld(int capsuleIndex, const XVECTOR3& axisWorld, float angleRadians, bool rebuildRagdoll);
  bool FlipRagdollEditCapsuleLocalAxis(int capsuleIndex, int axisIndex);
  bool AlignRagdollEditCapsuleToWorldAxis(int capsuleIndex, int axisIndex);
  bool SyncRagdollCapsuleSymmetry();
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
  bool GetRagdollEditGizmoFrame(int capsuleIndex, XVECTOR3& outCenter, std::array<XVECTOR3, 3>& outAxes, float& outSize, bool globalAxes = false);
  bool BuildRagdollEditHandlePoints(int capsuleIndex, std::array<XVECTOR3, 7>& outPoints);
  bool PickRagdollEditHandle(float mouseX, float mouseY, float thresholdPixels, int& outCapsuleIndex, int& outHandleIndex);
  bool PickRagdollEditCapsule(float mouseX, float mouseY, float thresholdPixels, int& outCapsuleIndex);
  bool PickRagdollEditTransformGizmo(float mouseX, float mouseY, int& outAxis);
  bool BeginRagdollEditTransformGizmoDrag(float mouseX, float mouseY);
  bool DragRagdollEditTransformGizmo(float mouseX, float mouseY);
  void DrawRagdollEditTransformGizmo();
  bool SelectRagdollContextTargetAt(float mouseX, float mouseY);
  void DrawRagdollViewportContextMenu();
  bool DragRagdollEditHandle(int capsuleIndex, int handleIndex, const XVECTOR3& worldDelta);
  bool GetRagdollAuthoringBoneWorldTransform(int boneIndex, XMATRIX44& outWorld) const;
  bool GetSkeletonEditBoneWorldTransform(int boneIndex, XMATRIX44& outWorld) const;
  int FindSkeletonEditDisplayEndpoint(int boneIndex) const;
  bool BuildSkeletonEditBoneOctahedron(int boneIndex, float widthScale, std::array<XVECTOR3, 6>& outPoints) const;
  int PickSkeletonEditBone(float mouseX, float mouseY, float thresholdPixels) const;
  void PickSkeletonEditBonesInScreenRect(float minX, float minY, float maxX, float maxY, std::vector<int>& outBones) const;
  bool GetSkeletonEditBoneWorldPosition(int boneIndex, XVECTOR3& outWorld) const;
  bool SetSkeletonEditBoneWorldPosition(int boneIndex, const XVECTOR3& worldPosition);
  void SelectSkeletonEditBone(int boneIndex);
  void RestoreSkeletonPreviewBone();
  bool BeginSkeletonPreviewBone(int boneIndex);
  void GatherSkeletonEditBoneSubtree(int boneIndex, std::vector<int>& outBones) const;
  bool SetSkeletonPreviewBoneWorldTransform(int boneIndex, const XMATRIX44& worldTransform);
  bool MoveSkeletonPreviewBoneByWorldDelta(int boneIndex, const XVECTOR3& worldDelta);
  bool RotateSkeletonPreviewBoneWorld(int boneIndex, const XVECTOR3& axisWorld, float angleRadians);
  bool GetSkeletonPreviewBoneGizmoFrame(int boneIndex, XVECTOR3& outCenter, std::array<XVECTOR3, 3>& outAxes, float& outSize, bool globalAxes = false) const;
  void DrawSkeletonPreviewBoneGizmo();
  std::array<float, 3> GetSkeletonEditBoneScale(int boneIndex) const;
  bool SetSkeletonEditBoneScale(int boneIndex, const std::array<float, 3>& scale);
  void LoadSandboxProfile(bool embeddedInScene = false);
  void SaveSandboxProfile();
  void CaptureSandboxProfileState(t850::SandboxProfileDesc& state);
  void ApplySandboxProfileState(const t850::SandboxProfileDesc& state);
  t850::SandboxProfileDesc BuildSparseSandboxProfile(const t850::SandboxProfileDesc& current) const;
  bool SandboxProfileStatesEqual(const t850::SandboxProfileDesc& a, const t850::SandboxProfileDesc& b) const;
};
