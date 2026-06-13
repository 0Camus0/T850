/*********************************************************
 * T8ditor — Ragdoll Editor hosted window (extracted from EditorApp.cpp).
 *
 * Defines the EditorApp methods for the embedded "Ragdoll Edit" window:
 * the orbit viewport (gizmo body/joint editing) and the body/joint
 * authoring panel. Object-ragdoll authoring (Ensure/Recreate/Start/Reset)
 * remains in EditorApp.cpp and is invoked via the EditorApp instance.
 * Behaviour is identical to the original; only the file location changed
 * (Phase 4c of the editor refactor).
 *********************************************************/

#include "EditorApp.h"
#include "EditorWorld.h"
#include "EditorInternal.h"
#include "EditorRagdollSupport.h"
#include "EditorMath.h"
#include "EditorUtil.h"
#include "EditorViewportUtil.h"
#include "EditorScene.h"
#include "EditorSceneGizmos.h"
#include "EditorImGui.h"
#include "UndoRedo.h"

#include <physics/PhysicsAuthoring.h>
#include <physics/RagdollEditorTool.h>
#include <physics/PhysicsTypes.h>
#include <scene/RenderSkinnedMesh.h>
#include <core/EngineContext.h>
#include <utils/InputManager.h>
#include <utils/Picking.h>
#include <utils/Log.h>
#include <video/BaseDriver.h>

#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui/DevGuiContext.h>
#include <imgui/RagdollEditorGui.h>
#include <SDL3/SDL.h>

#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace t8ditor {
namespace {
  // Aliases into the shared EditorWorld (same storage as EditorApp.cpp).
  auto& g_objects           = GetEditorWorld().objects;
  auto& g_selectedIdx       = GetEditorWorld().selectedIdx;
  auto& g_selectionType     = GetEditorWorld().selectionType;
  auto& g_physicsEntities   = GetEditorWorld().physicsEntities;
  auto& g_cameras           = GetEditorWorld().cameras;
  auto& g_lights            = GetEditorWorld().lights;
  auto& g_groups            = GetEditorWorld().groups;
  auto& g_tempGroup         = GetEditorWorld().tempGroup;
  auto& g_activeGroupIdx    = GetEditorWorld().activeGroupIdx;
  auto& g_multiSelect       = GetEditorWorld().multiSelect;
  auto& g_multiEntitySelect = GetEditorWorld().multiEntitySelect;
  auto& g_undoStack         = GetEditorWorld().undoStack;
  auto& g_activeCameraIdx   = GetEditorWorld().activeCameraIdx;
}
void EditorApp::OpenRagdollEditor(int objectIndex) {
  if (objectIndex < 0 || objectIndex >= (int)g_objects.size()) {
    return;
  }
  SceneObject& obj = g_objects[objectIndex];
  if (!GetSkinnedMesh(obj) || !GetSkinnedMesh(obj)->HasSkinData()) {
    obj.ragdollStatus = "Ragdoll editor requires a skinned mesh.";
    return;
  }
  m_ragdollEditorObjectIndex = objectIndex;
  m_ragdollEditorSelectedBody = 0;
  m_ragdollEditorSelectedJoint = -1;
  m_ragdollEditorSelectedUnassignedBone = -1;
  m_ragdollEditorSelectedAffectedBone = -1;
  m_ragdollEditorDirty = false;
  m_ragdollEditorStatus.clear();
  m_ragdollEditorWindow.Open(true);
  {
    ImGuiIO& io = ImGui::GetIO();
    T8_LOG_INFO("[T8ditor] Requested native editor window title='Ragdoll Edit' object='%s' configFlags=0x%08X backendFlags=0x%08X",
                obj.name.c_str(),
                (unsigned int)io.ConfigFlags,
                (unsigned int)io.BackendFlags);
  }
  g_selectedIdx = objectIndex;
  g_selectionType = 0;
  ClearMixedSelection();
  AddMixedSelection(0, objectIndex);

  if (!obj.ragdollAuthoringReady && !LoadObjectRagdollAuthoringFromFile(obj)) {
    EnsureObjectRagdollAuthoring(obj);
  }
  if (obj.ragdollAuthoringReady) {
    EnsureEditorRagdollState(obj.ragdollAuthoring);
    obj.ragdollDebugDraw = true;
    RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
  }

  t850::AABB bounds;
  if (GetEditorObjectWorldAABB(obj, bounds) && bounds.IsValid()) {
    m_ragdollEditorOrbitTarget = bounds.Center();
    const XVECTOR3 ext = bounds.Extents();
    const float radius = (std::max)(0.25f, std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z));
    m_ragdollEditorOrbitDistance = radius * 2.8f;
  } else {
    m_ragdollEditorOrbitTarget = obj.wireframe.Position();
    m_ragdollEditorOrbitDistance = 4.0f;
  }
  m_ragdollEditorOrbitYaw = -0.75f;
  m_ragdollEditorOrbitPitch = 0.35f;
  m_ragdollEditorCameraInitialized = true;
}

void EditorApp::CloseRagdollEditor() {
  if (m_ragdollEditorObjectIndex >= 0 && m_ragdollEditorObjectIndex < (int)g_objects.size()) {
    SceneObject& obj = g_objects[m_ragdollEditorObjectIndex];
    SyncRagdollMetaFromObject(obj);
  }
  m_ragdollEditorWindow.Reset(true);
  m_ragdollEditorObjectIndex = -1;
  m_ragdollEditorSelectedBody = -1;
  m_ragdollEditorSelectedJoint = -1;
  m_ragdollEditorSelectedUnassignedBone = -1;
  m_ragdollEditorSelectedAffectedBone = -1;
  m_ragdollEditorDirty = false;
  m_ragdollEditorStatus.clear();
}

bool EditorApp::EnsureRagdollEditorViewportTarget(int width, int height) {
  if (!pFramework || !pFramework->pVideoDriver || width <= 0 || height <= 0) {
    return false;
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  t850::RenderViewportDesc gbufferDesc;
  std::vector<int> gbufferFormats = {
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA16F,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA16F,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA8};
  gbufferDesc.colorFormats = gbufferFormats;
  gbufferDesc.depthFormat = t850::BaseRT::F32;
  gbufferDesc.minWidth = 64;
  gbufferDesc.minHeight = 64;
  if (!m_ragdollEditorGBufferTarget.Ensure(driver, width, height, gbufferDesc)) {
    T8_LOG_ERROR("[T8ditor] Failed to create Ragdoll Edit GBuffer RT %dx%d", width, height);
    return false;
  }

  t850::RenderViewportDesc outputDesc;
  outputDesc.colorCount = 1;
  outputDesc.colorFormat = t850::BaseRT::RGBA8;
  outputDesc.depthFormat = t850::BaseRT::F32;
  outputDesc.minWidth = 64;
  outputDesc.minHeight = 64;
  if (!m_ragdollEditorViewportTarget.Ensure(driver, width, height, outputDesc)) {
    DestroyRagdollEditorViewportTarget();
    T8_LOG_ERROR("[T8ditor] Failed to create Ragdoll Edit viewport RT %dx%d", width, height);
    return false;
  }
  T8_LOG_INFO("[T8ditor] Ragdoll Edit viewport RTs created gbuffer=%d output=%d size=%dx%d",
              m_ragdollEditorGBufferTarget.Handle(),
              m_ragdollEditorViewportTarget.Handle(),
              m_ragdollEditorViewportTarget.Width(),
              m_ragdollEditorViewportTarget.Height());
  return true;
}

void EditorApp::DestroyRagdollEditorViewportTarget() {
  if (pFramework && pFramework->pVideoDriver) {
    m_ragdollEditorGBufferTarget.Destroy(pFramework->pVideoDriver);
    m_ragdollEditorViewportTarget.Destroy(pFramework->pVideoDriver);
  }
}

void EditorApp::DrawRagdollEditorViewport(SceneObject& obj) {
  const EditorViewportSize desiredViewport = EditorViewportDesiredSize(ImGui::GetContentRegionAvail());
  t850::RenderViewportDesc viewportDesc;
  viewportDesc.minWidth = 64;
  viewportDesc.minHeight = 64;
  const bool shouldResizeRT =
      !m_ragdollEditorGBufferTarget.IsValid() ||
      EditorViewportShouldResize(m_ragdollEditorViewportTarget,
                                 desiredViewport.width,
                                 desiredViewport.height,
                                 viewportDesc);

  if (shouldResizeRT) {
    if (pFramework && pFramework->pVideoDriver) {
      pFramework->pVideoDriver->WaitForGPU();
    }
    if (!EnsureRagdollEditorViewportTarget(desiredViewport.width, desiredViewport.height)) {
      ImGui::TextDisabled("Ragdoll viewport unavailable.");
      return;
    }
  }

  if (!m_ragdollEditorViewportTarget.IsValid()) {
    ImGui::TextDisabled("Ragdoll viewport unavailable.");
    return;
  }
  const int viewportW = m_ragdollEditorViewportTarget.Width();
  const int viewportH = m_ragdollEditorViewportTarget.Height();
  if (!pFramework || !pFramework->pVideoDriver) {
    return;
  }
  if (obj.ragdollAuthoringReady) {
    EnsureEditorRagdollState(obj.ragdollAuthoring);
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  t850::BaseRT* gbufferRT = EditorRenderTarget(driver, m_ragdollEditorGBufferTarget.Handle());
  t850::BaseRT* rt = EditorRenderTarget(driver, m_ragdollEditorViewportTarget.Handle());
  if (!EditorRenderTargetReady(gbufferRT, 7, true) ||
      !EditorRenderTargetReady(rt, 1, false)) {
    ImGui::TextDisabled("Ragdoll viewport render targets are unavailable.");
    return;
  }
  auto& g_quads = EditorDeferredQuads();
  t850::Texture* const g_dummyWhiteTex = EditorDummyWhiteTex();
  const int g_dummyEnvMapIdx = EditorDummyEnvMapIdx();

  if (!m_ragdollEditorCameraInitialized) {
    t850::AABB bounds;
    if (GetEditorObjectWorldAABB(obj, bounds) && bounds.IsValid()) {
      m_ragdollEditorOrbitTarget = bounds.Center();
      const XVECTOR3 ext = bounds.Extents();
      m_ragdollEditorOrbitDistance = (std::max)(1.0f, std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z) * 2.8f);
    }
    m_ragdollEditorCameraInitialized = true;
  }
  const float aspect = viewportH > 0 ? (float)viewportW / (float)viewportH : 16.0f / 9.0f;
  const float farPlane = (std::max)(1000.0f, m_ragdollEditorOrbitDistance * 20.0f);
  ConfigureEditorOrbitCamera(m_ragdollEditorCamera,
                             m_ragdollEditorOrbitTarget,
                             m_ragdollEditorOrbitYaw,
                             m_ragdollEditorOrbitPitch,
                             m_ragdollEditorOrbitDistance,
                             45.0f * kDegToRad,
                             aspect,
                             0.01f,
                             farPlane,
                             -1.45f,
                             1.45f);

  Camera* previousCamera = m_sceneProps.GetPrimaryCamera();
  if (!m_sceneProps.pCameras.empty()) {
    m_sceneProps.SetPrimaryCamera(&m_ragdollEditorCamera);
  }

  driver->PushRT(m_ragdollEditorGBufferTarget.Handle());
  driver->ClearWithColor(0.0f, 0.0f, 0.0f, 0.0f);
  driver->SetBlendState(t850::BaseDriver::BLEND_OPAQUE);
  driver->SetDepthStencilState(t850::BaseDriver::READ_WRITE);
  driver->SetCullFace(t850::BaseDriver::FRONT_FACES);

  t850::RenderSkinnedMesh* viewportSkinned = GetSkinnedMesh(obj);
  if (viewportSkinned) {
    viewportSkinned->UploadBoneTexture();
  }
  t850::PhysicsRagdollDesc viewportRagdollPose;
  const t850::PhysicsRagdollDesc* visualRagdollPose =
      obj.ragdollAuthoringReady ? &obj.ragdollAuthoring.binding.referencePose : nullptr;
  if (viewportSkinned &&
      viewportSkinned->HasSkinData() &&
      obj.ragdollAuthoringReady &&
      t850::BuildRagdollPoseFromAnimation(*viewportSkinned,
                                           obj.litInst.Final,
                                           obj.ragdollAuthoring.binding,
                                           viewportRagdollPose)) {
    visualRagdollPose = &viewportRagdollPose;
  }

  t850::ShaderKey gbufferKey(0);
  gbufferKey.setPass(t850::PassType::GBUFFER);
  obj.litInst.SetGlobalKey(gbufferKey);
  obj.litInst.Update();
  obj.litInst.Draw();

  driver->PopRT();

  driver->PushRT(m_ragdollEditorViewportTarget.Handle());
  driver->ClearWithColor(0.10f, 0.105f, 0.115f, 1.0f);
  driver->SetBlendState(t850::BaseDriver::BLEND_OPAQUE);
  driver->SetDepthStencilState(t850::BaseDriver::NONE);
  for (int j = 0; j < 4; ++j) {
    g_quads[0].SetTexture(gbufferRT->vColorTextures[j], j);
  }
  g_quads[0].SetTexture(gbufferRT->vColorTextures[4], 9);
  g_quads[0].SetTexture(gbufferRT->vColorTextures[5], 7);
  g_quads[0].SetTexture(gbufferRT->vColorTextures[6], 8);
  g_quads[0].SetTexture(gbufferRT->pDepthTexture, 4);
  if (g_dummyWhiteTex) {
    g_quads[0].SetTexture(g_dummyWhiteTex, 5);
  }
  if (g_dummyEnvMapIdx >= 0) {
    g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));
  }
  t850::ShaderKey deferredKey(0);
  deferredKey.setPass(t850::PassType::DEFERRED_LDR);
  g_quads[0].SetGlobalKey(deferredKey);
  g_quads[0].Draw();

  if (viewportSkinned && viewportSkinned->HasSkinData()) {
    if (m_ragdollEditorShowWireframe) {
      viewportSkinned->SetWireframeDepthTex(gbufferRT->pDepthTexture);
      viewportSkinned->SetWireframeSecondaryDepthTex(nullptr);
      viewportSkinned->SetWireframeViewport(viewportW, viewportH);
      driver->SetDepthStencilState(t850::BaseDriver::NONE);
      viewportSkinned->DrawWireframe(XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f));
    }
    int selectedSkeletonBone = -1;
    const std::vector<int>* selectedControlledBones = nullptr;
    if (obj.ragdollAuthoringReady &&
        m_ragdollEditorSelectedBody >= 0 &&
        m_ragdollEditorSelectedBody < (int)obj.ragdollAuthoring.binding.referencePose.bones.size()) {
      selectedSkeletonBone = obj.ragdollAuthoring.binding.referencePose.bones[(std::size_t)m_ragdollEditorSelectedBody].body.boneIndex;
      if (m_ragdollEditorSelectedBody < (int)obj.ragdollAuthoring.binding.controlledBoneIndices.size()) {
        selectedControlledBones = &obj.ragdollAuthoring.binding.controlledBoneIndices[(std::size_t)m_ragdollEditorSelectedBody];
      }
    }
    if (m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Bones)) {
      if (m_ragdollEditorSelectedAffectedBone >= 0) {
        selectedSkeletonBone = m_ragdollEditorSelectedAffectedBone;
      } else if (m_ragdollEditorSelectedUnassignedBone >= 0) {
        selectedSkeletonBone = m_ragdollEditorSelectedUnassignedBone;
      }
    }
    driver->SetDepthStencilState(t850::BaseDriver::NONE);
    viewportSkinned->DrawSkeleton(selectedSkeletonBone, selectedControlledBones, nullptr, nullptr);
    driver->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
  }

  if (m_lines.IsReady()) {
    m_lines.SetDepthTexture(gbufferRT->pDepthTexture);
    m_lines.SetSecondaryDepthTexture(nullptr);
    m_lines.SetViewport(viewportW, viewportH);
    m_lines.SetFarPlane(farPlane);
    m_grid.Draw(m_lines, m_ragdollEditorCamera.VP);
  }

  if (obj.ragdollDebugDraw && obj.litInst.HasPhysicsRagdoll() && m_physicsDebug.IsReady()) {
    m_physicsDebug.SetViewport(viewportW, viewportH);
    m_physicsDebug.SetFarPlane(farPlane);
    m_physicsDebug.SetDepthTexture(gbufferRT->pDepthTexture);
    m_physicsDebug.Draw(m_physics, m_ragdollEditorCamera.VP);
  }

  driver->PopRT();
  if (!m_sceneProps.pCameras.empty()) {
    m_sceneProps.SetPrimaryCamera(previousCamera);
  }

  const int displayViewportW = desiredViewport.width;
  const int displayViewportH = desiredViewport.height;
  const ImVec2 viewportSize((float)displayViewportW, (float)displayViewportH);
  const ImVec2 imageMin = ImGui::GetCursorScreenPos();
  bool viewportInputActive = false;
  if (!DrawEditorViewportTexture(driver,
                                 EditorRenderTargetColor(rt),
                                 imageMin,
                                 viewportSize,
                                 "##RagdollEditorViewportInput",
                                 "Ragdoll viewport texture is not available for ImGui.",
                                 &viewportInputActive)) {
    return;
  }
  const bool viewportHovered = viewportInputActive && ImGui::IsItemHovered();
  const bool viewportActive = viewportInputActive && ImGui::IsItemActive();
  auto projectToImage = [&](const XVECTOR3& worldPoint, bool& visible) {
    ImVec2 screen = WorldToScreen(worldPoint, m_ragdollEditorCamera.VP, viewportW, viewportH);
    visible = std::isfinite(screen.x) && std::isfinite(screen.y) &&
              screen.x >= -100000.0f && screen.y >= -100000.0f;
    return ImVec2(
        imageMin.x + screen.x * ((float)displayViewportW / (float)(std::max)(1, viewportW)),
        imageMin.y + screen.y * ((float)displayViewportH / (float)(std::max)(1, viewportH)));
  };
  auto visualBoneForBody = [&](int bodyIndex) -> const t850::PhysicsRagdollBoneDesc* {
    if (!visualRagdollPose ||
        bodyIndex < 0 ||
        bodyIndex >= (int)visualRagdollPose->bones.size()) {
      return nullptr;
    }
    return &visualRagdollPose->bones[(std::size_t)bodyIndex];
  };
  auto bodyGizmoFrame = [&](int bodyIndex, XVECTOR3& outCenter, std::array<XVECTOR3, 3>& outAxes, float& outSize) {
    if (!obj.ragdollAuthoringReady ||
        bodyIndex < 0 ||
        bodyIndex >= (int)obj.ragdollAuthoring.binding.referencePose.bones.size()) {
      return false;
    }
    const auto* visualBone = visualBoneForBody(bodyIndex);
    if (!visualBone) {
      return false;
    }
    const auto& body = visualBone->body;
    outCenter = RagdollMatrixTranslation(body.worldTransform);
    outAxes = {
        RagdollMatrixAxis(body.worldTransform, 0),
        RagdollMatrixAxis(body.worldTransform, 1),
        RagdollMatrixAxis(body.worldTransform, 2)};
    const float modelRadius = (std::max)(EstimateRagdollRadius(obj), 0.001f);
    const float distanceToCamera = RagdollLength3(outCenter - m_ragdollEditorCamera.Eye);
    const float minSize = (std::max)(0.03f, modelRadius * 0.08f);
    const float maxSize = (std::max)(minSize, modelRadius * 0.45f);
    outSize = (std::max)(minSize, (std::min)(maxSize, distanceToCamera * 0.10f));
    return true;
  };
  std::function<bool(int, std::array<XVECTOR3, 7>&)> buildBodyHandlePoints;
  std::function<bool(int, bool)> updateBodyFromLocal;
  std::function<bool(int)> updateJointOffsetFromWorld;
  std::function<bool(int)> updateJointFrameOffsetsFromWorld;
  auto dragBodyHandle = [&](int bodyIndex, int handleIndex, const XVECTOR3& worldDelta) {
    auto& authoring = obj.ragdollAuthoring;
    if (bodyIndex < 0 ||
        bodyIndex >= (int)authoring.binding.referencePose.bones.size() ||
        bodyIndex >= (int)authoring.binding.bodyFromBone.size() ||
        handleIndex < 0 ||
        handleIndex >= 7) {
      return false;
    }
    t850::ragdoll_editor::RagdollEditorTool tool(authoring);
    tool.EnsureState();
    if (tool.IsBodyFrozen(bodyIndex)) {
      return false;
    }
    auto& bone = authoring.binding.referencePose.bones[(std::size_t)bodyIndex];
    const auto* visualBone = visualBoneForBody(bodyIndex);
    if (!visualBone) {
      return false;
    }
    auto& local = authoring.binding.bodyFromBone[(std::size_t)bodyIndex];
    auto& shape = bone.body.shape;
    XMATRIX44 bodyWorld = visualBone->body.worldTransform;

    XMATRIX44 inverseLocal;
    local.Inverse(&inverseLocal);
    XMATRIX44 boneWorld = inverseLocal * bodyWorld;
    XMATRIX44 inverseBoneWorld;
    boneWorld.Inverse(&inverseBoneWorld);
    XMATRIX44 inverseBodyWorld;
    bodyWorld.Inverse(&inverseBodyWorld);

    auto translateCenterByWorld = [&](const XVECTOR3& deltaWorld) {
      const XVECTOR3 deltaBone = RagdollTransformVectorNoTranslation(deltaWorld, inverseBoneWorld);
      local.m41 += deltaBone.x;
      local.m42 += deltaBone.y;
      local.m43 += deltaBone.z;
    };

    bool rebuildPreview = false;
    if (handleIndex == 0) {
      translateCenterByWorld(worldDelta);
    } else {
      const XVECTOR3 deltaBody = RagdollTransformVectorNoTranslation(worldDelta, inverseBodyWorld);
      auto bodyAxis = [&](int axis) {
        return RagdollMatrixAxis(bodyWorld, axis);
      };
      auto deltaCoord = [&](int axis) {
        return axis == 0 ? deltaBody.x : (axis == 1 ? deltaBody.y : deltaBody.z);
      };

      if (shape.type == t850::PhysicsShapeType::Box) {
        XVECTOR3 halfExtents = RagdollClampBoxHalfExtents(shape.halfExtents);
        int axis = 1;
        float sign = 1.0f;
        if (handleIndex == 2) {
          axis = 1;
          sign = -1.0f;
        } else if (handleIndex == 3) {
          axis = 0;
        } else if (handleIndex == 4) {
          axis = 0;
          sign = -1.0f;
        } else if (handleIndex == 5) {
          axis = 2;
        } else if (handleIndex == 6) {
          axis = 2;
          sign = -1.0f;
        }
        const float delta = deltaCoord(axis);
        const float newExtent =
            (std::max)(kRagdollEditorMinShapeExtent, RagdollAxisCoord(halfExtents, axis) + sign * delta * 0.5f);
        RagdollSetAxisCoord(halfExtents, axis, newExtent);
        translateCenterByWorld(bodyAxis(axis) * (delta * 0.5f));
        shape.halfExtents = halfExtents;
        rebuildPreview = true;
      } else {
        const float radius = (std::max)(kRagdollEditorMinShapeExtent, shape.radius);
        float extent = (std::max)(radius + kRagdollEditorMinShapeExtent, shape.halfHeight + radius);
        if (handleIndex == 1 || handleIndex == 2) {
          const float signedDelta = handleIndex == 1 ? deltaBody.y : -deltaBody.y;
          const float centerShiftBodyY = deltaBody.y * 0.5f;
          extent = (std::max)(radius + kRagdollEditorMinShapeExtent, extent + signedDelta * 0.5f);
          translateCenterByWorld(bodyAxis(1) * centerShiftBodyY);
          shape.halfHeight = (std::max)(kRagdollEditorMinShapeExtent, extent - radius);
          rebuildPreview = true;
        } else {
          float radiusDelta = 0.0f;
          if (handleIndex == 3) radiusDelta = deltaBody.x;
          else if (handleIndex == 4) radiusDelta = -deltaBody.x;
          else if (handleIndex == 5) radiusDelta = deltaBody.z;
          else if (handleIndex == 6) radiusDelta = -deltaBody.z;
          const float newRadius = (std::max)(kRagdollEditorMinShapeExtent, radius + radiusDelta);
          extent = (std::max)(newRadius + kRagdollEditorMinShapeExtent, extent);
          shape.radius = newRadius;
          shape.halfHeight = (std::max)(kRagdollEditorMinShapeExtent, extent - newRadius);
          rebuildPreview = true;
        }
      }
    }

    if (!updateBodyFromLocal(bodyIndex, false)) {
      return false;
    }
    updateJointFrameOffsetsFromWorld(bodyIndex);
    for (int child = 0; child < (int)authoring.binding.referencePose.bones.size(); ++child) {
      if (tool.EffectiveJointParent(child) == bodyIndex) {
        updateJointFrameOffsetsFromWorld(child);
      }
    }
    if (obj.ragdollDebugDraw && (rebuildPreview || handleIndex == 0)) {
      RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
    }
    return true;
  };
  auto pickBodyByScreen = [&](float localX, float localY, float thresholdPixels, int& outBody) {
    outBody = -1;
    ImVec2 mouse(imageMin.x + localX * ((float)displayViewportW / (float)(std::max)(1, viewportW)),
                 imageMin.y + localY * ((float)displayViewportH / (float)(std::max)(1, viewportH)));
    float bestScore = FLT_MAX;
    const auto& bodies = obj.ragdollAuthoring.binding.referencePose.bones;
    for (int bodyIndex = 0; bodyIndex < (int)bodies.size(); ++bodyIndex) {
      std::array<XVECTOR3, 7> points;
      if (!buildBodyHandlePoints(bodyIndex, points)) continue;
      bool centerVisible = false;
      bool topVisible = false;
      bool bottomVisible = false;
      const ImVec2 center = projectToImage(points[0], centerVisible);
      const ImVec2 top = projectToImage(points[1], topVisible);
      const ImVec2 bottom = projectToImage(points[2], bottomVisible);
      if (!centerVisible || !topVisible || !bottomVisible) continue;
      float radiusPixels = thresholdPixels;
      for (int handleIndex = 3; handleIndex < 7; ++handleIndex) {
        bool sideVisible = false;
        const ImVec2 side = projectToImage(points[(std::size_t)handleIndex], sideVisible);
        if (!sideVisible) continue;
        const float dx = side.x - center.x;
        const float dy = side.y - center.y;
        radiusPixels = (std::max)(radiusPixels, std::sqrt(dx * dx + dy * dy));
      }
      const float axisDistance = std::sqrt(RagdollDistancePointToSegmentSq(mouse, top, bottom));
      const float surfaceDistance = (std::max)(0.0f, axisDistance - radiusPixels);
      const float score = surfaceDistance + axisDistance * 0.001f;
      if (surfaceDistance <= thresholdPixels && score < bestScore) {
        bestScore = score;
        outBody = bodyIndex;
      }
    }
    return outBody >= 0;
  };
  auto pickBodyHandle = [&](float localX, float localY, float thresholdPixels, int& outBody, int& outHandle) {
    outBody = -1;
    outHandle = -1;
    if (m_ragdollEditorSelectionMode != static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies) ||
        m_ragdollEditorToolMode != static_cast<int>(t850::ragdoll_editor::ToolMode::Edit)) {
      return false;
    }
    ImVec2 mouse(imageMin.x + localX * ((float)displayViewportW / (float)(std::max)(1, viewportW)),
                 imageMin.y + localY * ((float)displayViewportH / (float)(std::max)(1, viewportH)));
    t850::ragdoll_editor::RagdollEditorTool tool(obj.ragdollAuthoring);
    tool.EnsureState();
    int bestPriority = (std::numeric_limits<int>::max)();
    float bestDistanceSq = FLT_MAX;
    const float modelRadius = (std::max)(EstimateRagdollRadius(obj), 0.001f);
    const float handleWorldRadius = (std::max)(0.01f, modelRadius * 0.014f);
    auto pickRadiusPixels = [&](const XVECTOR3& worldPoint) {
      float radiusPixels = thresholdPixels;
      bool centerVisible = false;
      bool edgeVisible = false;
      const ImVec2 center = projectToImage(worldPoint, centerVisible);
      const ImVec2 edge = projectToImage(worldPoint + m_ragdollEditorCamera.Right * handleWorldRadius, edgeVisible);
      if (centerVisible && edgeVisible) {
        const float dx = edge.x - center.x;
        const float dy = edge.y - center.y;
        radiusPixels = (std::max)(radiusPixels, std::sqrt(dx * dx + dy * dy) * 1.2f);
      }
      return radiusPixels;
    };
    const auto& bodies = obj.ragdollAuthoring.binding.referencePose.bones;
    for (int bodyIndex = 0; bodyIndex < (int)bodies.size(); ++bodyIndex) {
      if (tool.IsBodyFrozen(bodyIndex)) continue;
      std::array<XVECTOR3, 7> points;
      if (!buildBodyHandlePoints(bodyIndex, points)) continue;
      for (int handleIndex = 0; handleIndex < (int)points.size(); ++handleIndex) {
        bool visible = false;
        const ImVec2 screen = projectToImage(points[(std::size_t)handleIndex], visible);
        if (!visible) continue;
        const float dx = screen.x - mouse.x;
        const float dy = screen.y - mouse.y;
        const float distanceSq = dx * dx + dy * dy;
        const float radiusPixels = pickRadiusPixels(points[(std::size_t)handleIndex]);
        if (distanceSq > radiusPixels * radiusPixels) continue;
        const bool selectedBody = bodyIndex == m_ragdollEditorSelectedBody;
        const bool centerHandle = handleIndex == 0;
        const int priority = (selectedBody ? 0 : 2) + (centerHandle ? 1 : 0);
        if (priority < bestPriority || (priority == bestPriority && distanceSq < bestDistanceSq)) {
          bestPriority = priority;
          bestDistanceSq = distanceSq;
          outBody = bodyIndex;
          outHandle = handleIndex;
        }
      }
    }
    return outBody >= 0 && outHandle >= 0;
  };
  buildBodyHandlePoints = [&](int bodyIndex, std::array<XVECTOR3, 7>& outPoints) {
    if (!obj.ragdollAuthoringReady ||
        bodyIndex < 0 ||
        bodyIndex >= (int)obj.ragdollAuthoring.binding.referencePose.bones.size()) {
      return false;
    }
    const auto* visualBone = visualBoneForBody(bodyIndex);
    if (!visualBone) {
      return false;
    }
    const auto& body = visualBone->body;
    const auto& shape = obj.ragdollAuthoring.binding.referencePose.bones[(std::size_t)bodyIndex].body.shape;
    const float radius = (std::max)(kRagdollEditorMinShapeExtent, shape.radius);
    const float extent = (std::max)(0.002f, shape.halfHeight + radius);
    const XVECTOR3 boxHalfExtents = RagdollClampBoxHalfExtents(shape.halfExtents);
    const float extentX = shape.type == t850::PhysicsShapeType::Box ? boxHalfExtents.x : radius;
    const float extentY = shape.type == t850::PhysicsShapeType::Box ? boxHalfExtents.y : extent;
    const float extentZ = shape.type == t850::PhysicsShapeType::Box ? boxHalfExtents.z : radius;
    const XMATRIX44& bodyWorld = body.worldTransform;
    outPoints[0] = t850::TransformPoint(XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f), bodyWorld);
    outPoints[1] = t850::TransformPoint(XVECTOR3(0.0f,  extentY, 0.0f, 1.0f), bodyWorld);
    outPoints[2] = t850::TransformPoint(XVECTOR3(0.0f, -extentY, 0.0f, 1.0f), bodyWorld);
    outPoints[3] = t850::TransformPoint(XVECTOR3( extentX, 0.0f, 0.0f, 1.0f), bodyWorld);
    outPoints[4] = t850::TransformPoint(XVECTOR3(-extentX, 0.0f, 0.0f, 1.0f), bodyWorld);
    outPoints[5] = t850::TransformPoint(XVECTOR3(0.0f, 0.0f,  extentZ, 1.0f), bodyWorld);
    outPoints[6] = t850::TransformPoint(XVECTOR3(0.0f, 0.0f, -extentZ, 1.0f), bodyWorld);
    return true;
  };
  updateBodyFromLocal = [&](int bodyIndex, bool rebuildPreview) {
    auto& authoring = obj.ragdollAuthoring;
    if (t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj)) {
      if (!t850::UpdateRagdollAuthoringBodyFromLocal(authoring, *skinned, obj.litInst.Final, bodyIndex)) {
        obj.ragdollStatus = "Failed to update ragdoll body transform from viewport edit.";
        return false;
      }
    }
    m_ragdollEditorDirty = true;
    if (obj.ragdollDebugDraw && rebuildPreview) {
      RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
    }
    return true;
  };
  updateJointOffsetFromWorld = [&](int childBody) {
    auto& authoring = obj.ragdollAuthoring;
    auto& binding = authoring.binding;
    if (childBody < 0 ||
        childBody >= (int)binding.referencePose.bones.size() ||
        childBody >= (int)binding.bodyFromBone.size()) {
      return false;
    }
    if (binding.jointFromBone.size() != binding.referencePose.bones.size()) {
      binding.jointFromBone.resize(binding.referencePose.bones.size(), XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
    }
    XMATRIX44 local = binding.bodyFromBone[(std::size_t)childBody];
    XMATRIX44 inverseLocal;
    local.Inverse(&inverseLocal);
    const auto* visualBone = visualBoneForBody(childBody);
    if (!visualBone) {
      return false;
    }
    XMATRIX44 boneWorld = inverseLocal * visualBone->body.worldTransform;
    XMATRIX44 inverseBoneWorld;
    boneWorld.Inverse(&inverseBoneWorld);
    binding.jointFromBone[(std::size_t)childBody] =
        t850::TransformPoint(binding.referencePose.bones[(std::size_t)childBody].jointWorldPosition, inverseBoneWorld);
    return true;
  };
  updateJointFrameOffsetsFromWorld = [&](int childBody) {
    auto& authoring = obj.ragdollAuthoring;
    auto& binding = authoring.binding;
    auto& bones = binding.referencePose.bones;
    if (childBody < 0 || childBody >= (int)bones.size()) {
      return false;
    }
    t850::ragdoll_editor::RagdollEditorTool tool(authoring);
    tool.EnsureState();
    const int parentBody = tool.EffectiveJointParent(childBody);
    const auto* visualChildBone = visualBoneForBody(childBody);
    const auto* visualParentBone = visualBoneForBody(parentBody);
    if (!visualChildBone) {
      return false;
    }
    const XMATRIX44& childWorld = visualChildBone->body.worldTransform;
    const XMATRIX44& parentWorld =
        parentBody >= 0 && parentBody < (int)bones.size() && visualParentBone
            ? visualParentBone->body.worldTransform
            : childWorld;

    auto& bone = bones[(std::size_t)childBody];
    XVECTOR3 parentTwist = bone.parentJointTwistAxis;
    XVECTOR3 parentPlane = bone.parentJointPlaneAxis;
    XVECTOR3 childTwist = bone.childJointTwistAxis;
    XVECTOR3 childPlane = bone.childJointPlaneAxis;
    RagdollNormalizeJointFrameAxes(parentTwist, parentPlane, RagdollMatrixAxis(parentWorld, 1), RagdollMatrixAxis(parentWorld, 0));
    RagdollNormalizeJointFrameAxes(childTwist, childPlane, RagdollMatrixAxis(childWorld, 1), RagdollMatrixAxis(childWorld, 0));
    bone.parentJointTwistAxis = parentTwist;
    bone.parentJointPlaneAxis = parentPlane;
    bone.childJointTwistAxis = childTwist;
    bone.childJointPlaneAxis = childPlane;

    XMATRIX44 inverseParentWorld;
    XMATRIX44 inverseChildWorld;
    XMATRIX44 parentCopy = parentWorld;
    XMATRIX44 childCopy = childWorld;
    parentCopy.Inverse(&inverseParentWorld);
    childCopy.Inverse(&inverseChildWorld);
    binding.parentJointTwistFromBody[(std::size_t)childBody] =
        RagdollTransformVectorNoTranslation(parentTwist, inverseParentWorld);
    binding.parentJointPlaneFromBody[(std::size_t)childBody] =
        RagdollTransformVectorNoTranslation(parentPlane, inverseParentWorld);
    binding.childJointTwistFromBody[(std::size_t)childBody] =
        RagdollTransformVectorNoTranslation(childTwist, inverseChildWorld);
    binding.childJointPlaneFromBody[(std::size_t)childBody] =
        RagdollTransformVectorNoTranslation(childPlane, inverseChildWorld);
    return true;
  };
  auto jointVisualFrame = [&](int childBody,
                              XVECTOR3& outJoint,
                              XVECTOR3& outParentCenter,
                              XVECTOR3& outChildCenter,
                              XVECTOR3& outParentTwistAxis,
                              XVECTOR3& outChildTwistAxis,
                              XVECTOR3& outChildPlaneAxis,
                              float& outSize) {
    auto& authoring = obj.ragdollAuthoring;
    t850::ragdoll_editor::RagdollEditorTool tool(authoring);
    tool.EnsureState();
    const auto& bones = authoring.binding.referencePose.bones;
    if (childBody < 0 || childBody >= (int)bones.size()) {
      return false;
    }
    const int parentBody = tool.EffectiveJointParent(childBody);
    if (parentBody < 0 || parentBody >= (int)bones.size() || parentBody == childBody) {
      return false;
    }
    const auto* visualParentBone = visualBoneForBody(parentBody);
    const auto* visualChildBone = visualBoneForBody(childBody);
    if (!visualParentBone || !visualChildBone) {
      return false;
    }
    const XMATRIX44& parentWorld = visualParentBone->body.worldTransform;
    const XMATRIX44& childWorld = visualChildBone->body.worldTransform;
    outParentCenter = RagdollMatrixTranslation(parentWorld);
    outChildCenter = RagdollMatrixTranslation(childWorld);
    outJoint = visualChildBone->jointWorldPosition;
    outParentTwistAxis = RagdollNormalize3(visualChildBone->parentJointTwistAxis, RagdollMatrixAxis(parentWorld, 1));
    outChildTwistAxis = RagdollNormalize3(visualChildBone->childJointTwistAxis, RagdollMatrixAxis(childWorld, 1));
    outChildPlaneAxis = RagdollNormalize3(visualChildBone->childJointPlaneAxis, RagdollMatrixAxis(childWorld, 0));
    const float modelRadius = (std::max)(EstimateRagdollRadius(obj), 0.001f);
    const float distanceToCamera = RagdollLength3(outJoint - m_ragdollEditorCamera.Eye);
    const float minSize = (std::max)(0.03f, modelRadius * 0.06f);
    const float maxSize = (std::max)(minSize, modelRadius * 0.35f);
    outSize = (std::max)(minSize, (std::min)(maxSize, distanceToCamera * 0.08f));
    return true;
  };
  auto jointGizmoFrame = [&](int childBody, XVECTOR3& outCenter, std::array<XVECTOR3, 3>& outAxes, float& outSize) {
    XVECTOR3 parentCenter;
    XVECTOR3 childCenter;
    XVECTOR3 parentTwist;
    XVECTOR3 childTwist;
    XVECTOR3 childPlane;
    if (!jointVisualFrame(childBody, outCenter, parentCenter, childCenter, parentTwist, childTwist, childPlane, outSize)) {
      return false;
    }
    outAxes[1] = RagdollNormalize3(childTwist, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    outAxes[0] = RagdollNormalize3(childPlane, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    outAxes[2] = RagdollNormalize3(RagdollCross3(outAxes[0], outAxes[1]), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    outAxes[0] = RagdollNormalize3(RagdollCross3(outAxes[1], outAxes[2]), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    return true;
  };
  auto setBodyWorldTransform = [&](int bodyIndex, const XMATRIX44& desiredWorld) {
    if (!obj.ragdollAuthoringReady ||
        bodyIndex < 0 ||
        bodyIndex >= (int)obj.ragdollAuthoring.binding.referencePose.bones.size() ||
        bodyIndex >= (int)obj.ragdollAuthoring.binding.bodyFromBone.size()) {
      return false;
    }
    auto& authoring = obj.ragdollAuthoring;
    t850::ragdoll_editor::RagdollEditorTool tool(authoring);
    tool.EnsureState();
    if (tool.IsBodyFrozen(bodyIndex)) {
      return false;
    }
    XMATRIX44 local = authoring.binding.bodyFromBone[(std::size_t)bodyIndex];
    XMATRIX44 inverseLocal;
    local.Inverse(&inverseLocal);
    const auto* visualBone = visualBoneForBody(bodyIndex);
    if (!visualBone) {
      return false;
    }
    const XMATRIX44 oldWorld = visualBone->body.worldTransform;
    XMATRIX44 boneWorld = inverseLocal * oldWorld;
    XMATRIX44 inverseBoneWorld;
    boneWorld.Inverse(&inverseBoneWorld);
    authoring.binding.bodyFromBone[(std::size_t)bodyIndex] = desiredWorld * inverseBoneWorld;
    if (!updateBodyFromLocal(bodyIndex, false)) {
      return false;
    }
    updateJointFrameOffsetsFromWorld(bodyIndex);
    for (int child = 0; child < (int)authoring.binding.referencePose.bones.size(); ++child) {
      if (tool.EffectiveJointParent(child) == bodyIndex) {
        updateJointFrameOffsetsFromWorld(child);
      }
    }
    if (obj.ragdollDebugDraw) {
      RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
    }
    return true;
  };
  auto drawBodyGizmo = [&]() {
    if (m_ragdollEditorSelectionMode != static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies) ||
        m_ragdollEditorSelectedBody < 0 ||
        m_ragdollEditorSelectedBody >= (int)obj.ragdollAuthoring.binding.referencePose.bones.size() ||
        (m_ragdollEditorToolMode != static_cast<int>(t850::ragdoll_editor::ToolMode::Edit) &&
        (m_ragdollEditorToolMode != static_cast<int>(t850::ragdoll_editor::ToolMode::Move) &&
         m_ragdollEditorToolMode != static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate)))) {
      return;
    }
    XVECTOR3 center;
    std::array<XVECTOR3, 3> axes;
    float size = 0.0f;
    if (!bodyGizmoFrame(m_ragdollEditorSelectedBody, center, axes, size)) return;
    bool centerVisible = false;
    ImVec2 centerScreen = projectToImage(center, centerVisible);
    if (!centerVisible) return;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 originColor = IM_COL32(64, 160, 255, 255);
    const ImU32 originFill = IM_COL32(24, 96, 255, 180);
    drawList->AddCircle(centerScreen, 9.0f, originColor, 24, 2.5f);
    drawList->AddCircleFilled(centerScreen, 3.5f, originFill, 16);
    drawList->AddLine(ImVec2(centerScreen.x - 11.0f, centerScreen.y), ImVec2(centerScreen.x + 11.0f, centerScreen.y), originColor, 2.0f);
    drawList->AddLine(ImVec2(centerScreen.x, centerScreen.y - 11.0f), ImVec2(centerScreen.x, centerScreen.y + 11.0f), originColor, 2.0f);
    drawList->AddText(ImVec2(centerScreen.x + 11.0f, centerScreen.y + 5.0f), originColor, "origin");

    const auto& selectedShape =
        obj.ragdollAuthoring.binding.referencePose.bones[(std::size_t)m_ragdollEditorSelectedBody].body.shape;
    const float capsuleExtent = selectedShape.type == t850::PhysicsShapeType::Capsule
        ? (std::max)(0.002f, selectedShape.halfHeight + selectedShape.radius)
        : RagdollClampBoxHalfExtents(selectedShape.halfExtents).y;
    const float markerLength = (std::min)((std::max)(capsuleExtent, size * 0.45f), size * 1.15f);
    bool yPositiveVisible = false;
    bool yNegativeVisible = false;
    const ImVec2 yPositiveScreen = projectToImage(center + axes[1] * markerLength, yPositiveVisible);
    const ImVec2 yNegativeScreen = projectToImage(center - axes[1] * (markerLength * 0.72f), yNegativeVisible);
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

    t850::ragdoll_editor::RagdollEditorTool tool(obj.ragdollAuthoring);
    tool.EnsureState();
    if (tool.IsBodyFrozen(m_ragdollEditorSelectedBody)) {
      drawList->AddText(ImVec2(centerScreen.x + 11.0f, centerScreen.y + 23.0f), IM_COL32(255, 245, 120, 255), "frozen");
      return;
    }

    if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Edit)) {
      std::array<XVECTOR3, 7> points;
      if (buildBodyHandlePoints(m_ragdollEditorSelectedBody, points)) {
        static constexpr const char* kHandleLabels[7] = {"center", "+Y", "-Y", "+X", "-X", "+Z", "-Z"};
        for (int handle = 0; handle < (int)points.size(); ++handle) {
          bool visible = false;
          const ImVec2 screen = projectToImage(points[(std::size_t)handle], visible);
          if (!visible) continue;
          const bool active = m_ragdollEditorHandleDragging && m_ragdollEditorSelectedHandle == handle;
          const ImU32 color = active ? IM_COL32(255, 245, 120, 255) : (handle == 0 ? originColor : IM_COL32(100, 210, 255, 230));
          drawList->AddCircleFilled(screen, active ? 6.0f : 4.5f, color, 16);
          drawList->AddCircle(screen, active ? 10.0f : 7.0f, color, 16, active ? 2.5f : 1.5f);
          if (handle != 0) {
            drawList->AddLine(centerScreen, screen, IM_COL32(100, 210, 255, 105), 1.2f);
          }
          if (active || handle == m_ragdollEditorSelectedHandle) {
            drawList->AddText(ImVec2(screen.x + 7.0f, screen.y - 7.0f), color, kHandleLabels[handle]);
          }
        }
      }
      return;
    }

    if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move)) {
      for (int axis = 0; axis < 3; ++axis) {
        const bool active = m_ragdollEditorGizmoDragging && m_ragdollEditorGizmoAxis == axis;
        bool endVisible = false;
        ImVec2 endScreen = projectToImage(center + axes[(std::size_t)axis] * size, endVisible);
        if (!endVisible) continue;
        const ImU32 color = RagdollAxisColor(axis, active);
        drawList->AddLine(centerScreen, endScreen, color, active ? 4.0f : 3.0f);
        const float dx = endScreen.x - centerScreen.x;
        const float dy = endScreen.y - centerScreen.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.001f) {
          const float ux = dx / len;
          const float uy = dy / len;
          const ImVec2 perp(-uy, ux);
          const ImVec2 base(endScreen.x - ux * 14.0f, endScreen.y - uy * 14.0f);
          drawList->AddTriangleFilled(endScreen,
                                      ImVec2(base.x + perp.x * 5.0f, base.y + perp.y * 5.0f),
                                      ImVec2(base.x - perp.x * 5.0f, base.y - perp.y * 5.0f),
                                      color);
        }
      }
    } else {
      constexpr int kSegments = 72;
      const float radius = size * 0.78f;
      for (int axis = 0; axis < 3; ++axis) {
        const bool active = m_ragdollEditorGizmoDragging && m_ragdollEditorGizmoAxis == axis;
        const XVECTOR3 u = axes[(std::size_t)((axis + 1) % 3)];
        const XVECTOR3 v = axes[(std::size_t)((axis + 2) % 3)];
        const ImU32 color = RagdollAxisColor(axis, active);
        ImVec2 previous;
        bool previousVisible = false;
        for (int segment = 0; segment <= kSegments; ++segment) {
          const float t = ((float)segment / (float)kSegments) * (2.0f * xPI);
          bool visible = false;
          ImVec2 current = projectToImage(center + (u * std::cos(t) + v * std::sin(t)) * radius, visible);
          if (visible && previousVisible) drawList->AddLine(previous, current, color, active ? 3.5f : 2.0f);
          previous = current;
          previousVisible = visible;
        }
      }
    }
  };
  auto pickBodyGizmoAxis = [&](float localX, float localY, int& outAxis) {
    outAxis = -1;
    if (m_ragdollEditorSelectedBody < 0) return false;
    t850::ragdoll_editor::RagdollEditorTool tool(obj.ragdollAuthoring);
    tool.EnsureState();
    if (tool.IsBodyFrozen(m_ragdollEditorSelectedBody)) return false;
    XVECTOR3 center;
    std::array<XVECTOR3, 3> axes;
    float size = 0.0f;
    if (!bodyGizmoFrame(m_ragdollEditorSelectedBody, center, axes, size)) return false;
    ImVec2 mouse(imageMin.x + localX * ((float)displayViewportW / (float)(std::max)(1, viewportW)),
                 imageMin.y + localY * ((float)displayViewportH / (float)(std::max)(1, viewportH)));
    bool centerVisible = false;
    ImVec2 centerScreen = projectToImage(center, centerVisible);
    if (!centerVisible) return false;
    float best = 18.0f * 18.0f;
    if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move)) {
      for (int axis = 0; axis < 3; ++axis) {
        bool endVisible = false;
        ImVec2 endScreen = projectToImage(center + axes[(std::size_t)axis] * size, endVisible);
        if (!endVisible) continue;
        float d = RagdollDistancePointToSegmentSq(mouse, centerScreen, endScreen);
        if (d < best) {
          best = d;
          outAxis = axis;
        }
      }
    } else if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate)) {
      constexpr int kSegments = 72;
      const float radius = size * 0.78f;
      for (int axis = 0; axis < 3; ++axis) {
        const XVECTOR3 u = axes[(std::size_t)((axis + 1) % 3)];
        const XVECTOR3 v = axes[(std::size_t)((axis + 2) % 3)];
        ImVec2 previous;
        bool previousVisible = false;
        for (int segment = 0; segment <= kSegments; ++segment) {
          const float t = ((float)segment / (float)kSegments) * (2.0f * xPI);
          bool visible = false;
          ImVec2 current = projectToImage(center + (u * std::cos(t) + v * std::sin(t)) * radius, visible);
          if (visible && previousVisible) {
            float d = RagdollDistancePointToSegmentSq(mouse, previous, current);
            if (d < best) {
              best = d;
              outAxis = axis;
            }
          }
          previous = current;
          previousVisible = visible;
        }
      }
    }
    return outAxis >= 0;
  };
  auto beginBodyGizmoDrag = [&](const t850::Ray& ray, int axis) {
    XVECTOR3 center;
    std::array<XVECTOR3, 3> axes;
    float size = 0.0f;
    if (!bodyGizmoFrame(m_ragdollEditorSelectedBody, center, axes, size)) return false;
    m_ragdollEditorGizmoAxis = axis;
    m_ragdollEditorGizmoDragCenter = center;
    m_ragdollEditorGizmoDragAxis = axes[(std::size_t)axis];
    if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move)) {
      if (!RagdollClosestRayAxisParameter(ray, center, axes[(std::size_t)axis], m_ragdollEditorGizmoLastParameter)) return false;
    } else {
      XVECTOR3 hitPoint;
      if (!RagdollRayPlaneIntersection(ray, center, axes[(std::size_t)axis], hitPoint)) return false;
      m_ragdollEditorGizmoLastVector = RagdollNormalize3(hitPoint - center, axes[(std::size_t)((axis + 1) % 3)]);
    }
    m_ragdollEditorGizmoDragging = true;
    m_ragdollEditorHandleDragging = false;
    return true;
  };
  auto setJointWorldPosition = [&](int childBody, const XVECTOR3& worldPosition) {
    auto& authoring = obj.ragdollAuthoring;
    t850::ragdoll_editor::RagdollEditorTool tool(authoring);
    tool.EnsureState();
    auto& bones = authoring.binding.referencePose.bones;
    if (childBody < 0 || childBody >= (int)bones.size() || tool.EffectiveJointParent(childBody) < 0) {
      return false;
    }
    if (tool.IsJointFrozen(childBody)) {
      return false;
    }
    bones[(std::size_t)childBody].jointWorldPosition = XVECTOR3(worldPosition.x, worldPosition.y, worldPosition.z, 1.0f);
    if (childBody < (int)authoring.contactJoints.size()) {
      authoring.contactJoints[(std::size_t)childBody] = 0u;
    }
    if (!updateJointOffsetFromWorld(childBody)) {
      return false;
    }
    m_ragdollEditorDirty = true;
    if (obj.ragdollDebugDraw) {
      RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
    }
    return true;
  };
  auto moveJointByWorldDelta = [&](int childBody, const XVECTOR3& worldDelta) {
    if (childBody < 0 || childBody >= (int)obj.ragdollAuthoring.binding.referencePose.bones.size()) {
      return false;
    }
    XVECTOR3 joint = obj.ragdollAuthoring.binding.referencePose.bones[(std::size_t)childBody].jointWorldPosition;
    joint.x += worldDelta.x;
    joint.y += worldDelta.y;
    joint.z += worldDelta.z;
    return setJointWorldPosition(childBody, joint);
  };
  auto rotateJointWorld = [&](int childBody, const XVECTOR3& axisWorld, float angleRadians) {
    if (std::fabs(angleRadians) < 0.000001f) {
      return true;
    }
    auto& authoring = obj.ragdollAuthoring;
    t850::ragdoll_editor::RagdollEditorTool tool(authoring);
    tool.EnsureState();
    auto& bones = authoring.binding.referencePose.bones;
    if (childBody < 0 || childBody >= (int)bones.size() || tool.IsJointFrozen(childBody)) {
      return false;
    }
    const int parentBody = tool.EffectiveJointParent(childBody);
    if (parentBody < 0 || parentBody >= (int)bones.size() || parentBody == childBody) {
      return false;
    }
    auto& bone = bones[(std::size_t)childBody];
    bone.parentJointTwistAxis = RagdollRotateVectorAroundAxis(bone.parentJointTwistAxis, axisWorld, angleRadians);
    bone.parentJointPlaneAxis = RagdollRotateVectorAroundAxis(bone.parentJointPlaneAxis, axisWorld, angleRadians);
    bone.childJointTwistAxis = RagdollRotateVectorAroundAxis(bone.childJointTwistAxis, axisWorld, angleRadians);
    bone.childJointPlaneAxis = RagdollRotateVectorAroundAxis(bone.childJointPlaneAxis, axisWorld, angleRadians);
    if (!updateJointFrameOffsetsFromWorld(childBody)) {
      return false;
    }
    m_ragdollEditorDirty = true;
    if (obj.ragdollDebugDraw) {
      RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
    }
    return true;
  };
  auto pickJointByScreen = [&](float localX, float localY, float thresholdPixels, int& outJoint) {
    outJoint = -1;
    ImVec2 mouse(imageMin.x + localX * ((float)displayViewportW / (float)(std::max)(1, viewportW)),
                 imageMin.y + localY * ((float)displayViewportH / (float)(std::max)(1, viewportH)));
    float bestDistanceSq = thresholdPixels * thresholdPixels;
    const auto& bones = obj.ragdollAuthoring.binding.referencePose.bones;
    for (int childBody = 0; childBody < (int)bones.size(); ++childBody) {
      XVECTOR3 joint;
      XVECTOR3 parentCenter;
      XVECTOR3 childCenter;
      XVECTOR3 parentTwist;
      XVECTOR3 childTwist;
      XVECTOR3 childPlane;
      float size = 0.0f;
      if (!jointVisualFrame(childBody, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, size)) {
        continue;
      }
      bool jointVisible = false;
      bool parentVisible = false;
      bool childVisible = false;
      const ImVec2 jointScreen = projectToImage(joint, jointVisible);
      const ImVec2 parentScreen = projectToImage(parentCenter, parentVisible);
      const ImVec2 childScreen = projectToImage(childCenter, childVisible);
      if (!jointVisible) continue;
      float distanceSq = (jointScreen.x - mouse.x) * (jointScreen.x - mouse.x) +
                         (jointScreen.y - mouse.y) * (jointScreen.y - mouse.y);
      if (parentVisible) distanceSq = (std::min)(distanceSq, RagdollDistancePointToSegmentSq(mouse, jointScreen, parentScreen));
      if (childVisible) distanceSq = (std::min)(distanceSq, RagdollDistancePointToSegmentSq(mouse, jointScreen, childScreen));
      if (distanceSq < bestDistanceSq) {
        bestDistanceSq = distanceSq;
        outJoint = childBody;
      }
    }
    return outJoint >= 0;
  };
  auto pickJointGizmoAxis = [&](float localX, float localY, int& outAxis) {
    outAxis = -1;
    if (m_ragdollEditorSelectionMode != static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints) ||
        m_ragdollEditorSelectedJoint < 0 ||
        (m_ragdollEditorToolMode != static_cast<int>(t850::ragdoll_editor::ToolMode::Edit) &&
         m_ragdollEditorToolMode != static_cast<int>(t850::ragdoll_editor::ToolMode::Move) &&
         m_ragdollEditorToolMode != static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate))) {
      return false;
    }
    t850::ragdoll_editor::RagdollEditorTool tool(obj.ragdollAuthoring);
    tool.EnsureState();
    if (tool.IsJointFrozen(m_ragdollEditorSelectedJoint)) {
      return false;
    }
    XVECTOR3 center;
    std::array<XVECTOR3, 3> axes;
    float size = 0.0f;
    if (!jointGizmoFrame(m_ragdollEditorSelectedJoint, center, axes, size)) return false;
    ImVec2 mouse(imageMin.x + localX * ((float)displayViewportW / (float)(std::max)(1, viewportW)),
                 imageMin.y + localY * ((float)displayViewportH / (float)(std::max)(1, viewportH)));
    float best = 12.0f * 12.0f;
    if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Edit) ||
        m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move)) {
      for (int axis = 0; axis < 3; ++axis) {
        bool startVisible = false;
        bool endVisible = false;
        const ImVec2 start = projectToImage(center + axes[(std::size_t)axis] * (size * 0.12f), startVisible);
        const ImVec2 end = projectToImage(center + axes[(std::size_t)axis] * (size * 0.75f), endVisible);
        if (!startVisible || !endVisible) continue;
        const float d = RagdollDistancePointToSegmentSq(mouse, start, end);
        if (d < best) {
          best = d;
          outAxis = axis;
        }
      }
    } else {
      constexpr int kSegments = 64;
      const float radius = size * 0.62f;
      for (int axis = 0; axis < 3; ++axis) {
        const XVECTOR3 u = axes[(std::size_t)((axis + 1) % 3)];
        const XVECTOR3 v = axes[(std::size_t)((axis + 2) % 3)];
        ImVec2 previous;
        bool previousVisible = false;
        for (int segment = 0; segment <= kSegments; ++segment) {
          const float t = ((float)segment / (float)kSegments) * (2.0f * xPI);
          bool visible = false;
          const ImVec2 current = projectToImage(center + (u * std::cos(t) + v * std::sin(t)) * radius, visible);
          if (visible && previousVisible) {
            const float d = RagdollDistancePointToSegmentSq(mouse, previous, current);
            if (d < best) {
              best = d;
              outAxis = axis;
            }
          }
          previous = current;
          previousVisible = visible;
        }
      }
    }
    return outAxis >= 0;
  };
  auto beginJointGizmoDrag = [&](const t850::Ray& ray, int axis) {
    XVECTOR3 center;
    std::array<XVECTOR3, 3> axes;
    float size = 0.0f;
    if (!jointGizmoFrame(m_ragdollEditorSelectedJoint, center, axes, size)) return false;
    m_ragdollEditorGizmoAxis = axis;
    m_ragdollEditorGizmoDragCenter = center;
    m_ragdollEditorGizmoDragAxis = axes[(std::size_t)axis];
    if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Edit) ||
        m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move)) {
      if (!RagdollClosestRayAxisParameter(ray, center, axes[(std::size_t)axis], m_ragdollEditorGizmoLastParameter)) return false;
    } else {
      XVECTOR3 hitPoint;
      if (!RagdollRayPlaneIntersection(ray, center, axes[(std::size_t)axis], hitPoint)) return false;
      m_ragdollEditorGizmoLastVector = RagdollNormalize3(hitPoint - center, axes[(std::size_t)((axis + 1) % 3)]);
    }
    m_ragdollEditorGizmoDragging = true;
    m_ragdollEditorHandleDragging = false;
    return true;
  };
  auto drawJointGizmos = [&]() {
    if (m_ragdollEditorSelectionMode != static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints)) {
      return;
    }
    const auto& bones = obj.ragdollAuthoring.binding.referencePose.bones;
    if (bones.empty()) return;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 lineColor = IM_COL32(255, 185, 40, 165);
    const ImU32 jointColor = IM_COL32(255, 220, 80, 230);
    const ImU32 selectedColor = IM_COL32(255, 245, 120, 255);
    const ImU32 parentAxisColor = IM_COL32(255, 130, 40, 245);
    const ImU32 childAxisColor = IM_COL32(80, 220, 255, 255);
    const ImU32 planeAxisColor = IM_COL32(255, 90, 220, 245);
    const ImU32 coneColor = IM_COL32(255, 215, 70, 205);
    const ImU32 twistColor = IM_COL32(190, 120, 255, 230);
    t850::ragdoll_editor::RagdollEditorTool tool(obj.ragdollAuthoring);
    tool.EnsureState();
    for (int childBody = 0; childBody < (int)bones.size(); ++childBody) {
      XVECTOR3 joint;
      XVECTOR3 parentCenter;
      XVECTOR3 childCenter;
      XVECTOR3 parentTwist;
      XVECTOR3 childTwist;
      XVECTOR3 childPlane;
      float size = 0.0f;
      if (!jointVisualFrame(childBody, joint, parentCenter, childCenter, parentTwist, childTwist, childPlane, size)) {
        continue;
      }
      bool jointVisible = false;
      bool parentVisible = false;
      bool childVisible = false;
      const ImVec2 jointScreen = projectToImage(joint, jointVisible);
      const ImVec2 parentScreen = projectToImage(parentCenter, parentVisible);
      const ImVec2 childScreen = projectToImage(childCenter, childVisible);
      if (!jointVisible) continue;
      const bool selected = childBody == m_ragdollEditorSelectedJoint;
      if (parentVisible) drawList->AddLine(parentScreen, jointScreen, selected ? selectedColor : lineColor, selected ? 3.0f : 1.6f);
      if (childVisible) drawList->AddLine(jointScreen, childScreen, selected ? selectedColor : lineColor, selected ? 3.0f : 1.6f);
      drawList->AddCircleFilled(jointScreen, selected ? 6.0f : 4.0f, selected ? selectedColor : jointColor, 16);
      drawList->AddCircle(jointScreen, selected ? 11.0f : 7.0f, selected ? selectedColor : jointColor, 20, selected ? 2.5f : 1.5f);
      if (!selected) continue;
      drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 5.0f), selectedColor, "joint");
      auto drawAxis = [&](const XVECTOR3& axis, float length, ImU32 color, const char* label) {
        bool endVisible = false;
        const ImVec2 end = projectToImage(joint + axis * length, endVisible);
        if (!endVisible) return;
        drawList->AddLine(jointScreen, end, color, 3.0f);
        drawList->AddCircleFilled(end, 4.5f, color, 12);
        drawList->AddText(ImVec2(end.x + 6.0f, end.y - 6.0f), color, label);
      };
      drawAxis(parentTwist, size * 0.85f, parentAxisColor, "parent +Y");
      drawAxis(childTwist, size, childAxisColor, "child +Y twist");
      drawAxis(childPlane, size * 0.7f, planeAxisColor, "child +X plane");
      std::array<XVECTOR3, 3> axes = {
          childPlane,
          childTwist,
          RagdollNormalize3(RagdollCross3(childPlane, childTwist), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f))};
      if (tool.IsJointFrozen(childBody)) {
        drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 37.0f), selectedColor, "frozen");
      } else if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Edit) ||
                 m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move)) {
        drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 37.0f), selectedColor, "edit anchor");
        for (int axis = 0; axis < 3; ++axis) {
          const bool active = m_ragdollEditorGizmoDragging && m_ragdollEditorGizmoAxis == axis;
          const ImU32 color = RagdollAxisColor(axis, active);
          bool endVisible = false;
          const ImVec2 end = projectToImage(joint + axes[(std::size_t)axis] * (size * 0.75f), endVisible);
          if (!endVisible) continue;
          drawList->AddLine(jointScreen, end, color, active ? 4.0f : 2.5f);
          drawList->AddCircleFilled(end, active ? 5.5f : 4.0f, color, 12);
        }
      } else if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate)) {
        drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 37.0f), selectedColor, "rotate child frame");
        constexpr int kSegments = 72;
        const float radius = size * 0.62f;
        for (int axis = 0; axis < 3; ++axis) {
          const bool active = m_ragdollEditorGizmoDragging && m_ragdollEditorGizmoAxis == axis;
          const ImU32 color = RagdollAxisColor(axis, active);
          const XVECTOR3 u = axes[(std::size_t)((axis + 1) % 3)];
          const XVECTOR3 v = axes[(std::size_t)((axis + 2) % 3)];
          ImVec2 previous;
          bool previousVisible = false;
          for (int segment = 0; segment <= kSegments; ++segment) {
            const float t = ((float)segment / (float)kSegments) * (2.0f * xPI);
            bool visible = false;
            const ImVec2 screen = projectToImage(joint + (u * std::cos(t) + v * std::sin(t)) * radius, visible);
            if (visible && previousVisible) drawList->AddLine(previous, screen, color, active ? 3.5f : 2.5f);
            previous = screen;
            previousVisible = visible;
          }
        }
      }
      if (bones[(std::size_t)childBody].jointType == t850::PhysicsRagdollJointType::Fixed) {
        drawList->AddText(ImVec2(jointScreen.x + 10.0f, jointScreen.y + 21.0f), selectedColor, "fixed");
        continue;
      }
      const float coneLength = size * 0.75f;
      const float swing = (std::max)(0.0f, (std::min)(85.0f * kDegToRad, bones[(std::size_t)childBody].swingLimitRadians));
      const float coneRadius = (std::min)(size * 1.25f, std::tan(swing) * coneLength);
      const XVECTOR3 coneCenter = joint + childTwist * coneLength;
      const XVECTOR3 coneU = childPlane;
      const XVECTOR3 coneV = RagdollNormalize3(RagdollCross3(childTwist, coneU), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
      ImVec2 previousCone;
      bool previousConeVisible = false;
      constexpr int kConeSegments = 48;
      for (int segment = 0; segment <= kConeSegments; ++segment) {
        const float t = ((float)segment / (float)kConeSegments) * (2.0f * xPI);
        bool visible = false;
        const ImVec2 screen = projectToImage(coneCenter + (coneU * std::cos(t) + coneV * std::sin(t)) * coneRadius, visible);
        if (visible && previousConeVisible) drawList->AddLine(previousCone, screen, coneColor, 2.0f);
        previousCone = screen;
        previousConeVisible = visible;
      }
      const float twist = (std::max)(0.0f, (std::min)(180.0f * kDegToRad, bones[(std::size_t)childBody].twistLimitRadians));
      const float twistRadius = size * 0.38f;
      ImVec2 previousTwist;
      bool previousTwistVisible = false;
      constexpr int kTwistSegments = 32;
      for (int segment = 0; segment <= kTwistSegments; ++segment) {
        const float t = -twist + (2.0f * twist * (float)segment / (float)kTwistSegments);
        bool visible = false;
        const ImVec2 screen = projectToImage(joint + (coneU * std::cos(t) + coneV * std::sin(t)) * twistRadius, visible);
        if (visible && previousTwistVisible) drawList->AddLine(previousTwist, screen, twistColor, 2.5f);
        previousTwist = screen;
        previousTwistVisible = visible;
      }
    }
  };
  auto drawSelectedBodyWireframe = [&]() {
    if (!obj.ragdollAuthoringReady ||
        m_ragdollEditorSelectedBody < 0 ||
        m_ragdollEditorSelectedBody >= (int)obj.ragdollAuthoring.binding.referencePose.bones.size()) {
      return;
    }

    const auto* visualBone = visualBoneForBody(m_ragdollEditorSelectedBody);
    if (!visualBone) {
      return;
    }
    const auto& body = visualBone->body;
    const auto& shape =
        obj.ragdollAuthoring.binding.referencePose.bones[(std::size_t)m_ragdollEditorSelectedBody].body.shape;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    t850::ragdoll_editor::RagdollEditorTool tool(obj.ragdollAuthoring);
    tool.EnsureState();
    const bool frozen = tool.IsBodyFrozen(m_ragdollEditorSelectedBody);
    const ImU32 shadowColor = IM_COL32(0, 0, 0, 210);
    const ImU32 lineColor = frozen ? IM_COL32(160, 190, 255, 255) : IM_COL32(255, 245, 120, 255);
    const float shadowThickness = 4.2f;
    const float lineThickness = 2.4f;

    auto localPoint = [&](float x, float y, float z) {
      return t850::TransformPoint(XVECTOR3(x, y, z, 1.0f), body.worldTransform);
    };
    auto drawWorldLine = [&](const XVECTOR3& a, const XVECTOR3& b) {
      bool aVisible = false;
      bool bVisible = false;
      const ImVec2 pa = projectToImage(a, aVisible);
      const ImVec2 pb = projectToImage(b, bVisible);
      if (!aVisible || !bVisible) {
        return;
      }
      drawList->AddLine(pa, pb, shadowColor, shadowThickness);
      drawList->AddLine(pa, pb, lineColor, lineThickness);
    };
    auto drawCircleXZ = [&](float y, float radius) {
      constexpr int kSegments = 32;
      for (int i = 0; i < kSegments; ++i) {
        const float a0 = (2.0f * xPI * (float)i) / (float)kSegments;
        const float a1 = (2.0f * xPI * (float)(i + 1)) / (float)kSegments;
        drawWorldLine(localPoint(std::cos(a0) * radius, y, std::sin(a0) * radius),
                      localPoint(std::cos(a1) * radius, y, std::sin(a1) * radius));
      }
    };
    auto drawCapsuleArc = [&](bool yzPlane, float centerY, float startAngle, float endAngle, float radius) {
      constexpr int kSegments = 16;
      for (int i = 0; i < kSegments; ++i) {
        const float t0 = (float)i / (float)kSegments;
        const float t1 = (float)(i + 1) / (float)kSegments;
        const float a0 = startAngle + (endAngle - startAngle) * t0;
        const float a1 = startAngle + (endAngle - startAngle) * t1;
        const float c0 = std::cos(a0) * radius;
        const float s0 = std::sin(a0) * radius;
        const float c1 = std::cos(a1) * radius;
        const float s1 = std::sin(a1) * radius;
        if (yzPlane) {
          drawWorldLine(localPoint(0.0f, centerY + s0, c0),
                        localPoint(0.0f, centerY + s1, c1));
        } else {
          drawWorldLine(localPoint(c0, centerY + s0, 0.0f),
                        localPoint(c1, centerY + s1, 0.0f));
        }
      }
    };

    if (shape.type == t850::PhysicsShapeType::Box) {
      const XVECTOR3 halfExtents = RagdollClampBoxHalfExtents(shape.halfExtents);
      const float x = halfExtents.x;
      const float y = halfExtents.y;
      const float z = halfExtents.z;
      const XVECTOR3 corners[8] = {
          localPoint(-x, -y, -z), localPoint( x, -y, -z),
          localPoint( x,  y, -z), localPoint(-x,  y, -z),
          localPoint(-x, -y,  z), localPoint( x, -y,  z),
          localPoint( x,  y,  z), localPoint(-x,  y,  z),
      };
      static constexpr int kEdges[12][2] = {
          {0, 1}, {1, 2}, {2, 3}, {3, 0},
          {4, 5}, {5, 6}, {6, 7}, {7, 4},
          {0, 4}, {1, 5}, {2, 6}, {3, 7},
      };
      for (const auto& edge : kEdges) {
        drawWorldLine(corners[edge[0]], corners[edge[1]]);
      }
    } else {
      const float radius = (std::max)(kRagdollEditorMinShapeExtent, shape.radius);
      const float halfHeight = (std::max)(kRagdollEditorMinShapeExtent, shape.halfHeight);
      drawCircleXZ(halfHeight, radius);
      drawCircleXZ(-halfHeight, radius);
      drawWorldLine(localPoint( radius, -halfHeight, 0.0f), localPoint( radius, halfHeight, 0.0f));
      drawWorldLine(localPoint(-radius, -halfHeight, 0.0f), localPoint(-radius, halfHeight, 0.0f));
      drawWorldLine(localPoint(0.0f, -halfHeight,  radius), localPoint(0.0f, halfHeight,  radius));
      drawWorldLine(localPoint(0.0f, -halfHeight, -radius), localPoint(0.0f, halfHeight, -radius));
      drawCapsuleArc(false, halfHeight, 0.0f, xPI, radius);
      drawCapsuleArc(false, -halfHeight, xPI, 2.0f * xPI, radius);
      drawCapsuleArc(true, halfHeight, 0.0f, xPI, radius);
      drawCapsuleArc(true, -halfHeight, xPI, 2.0f * xPI, radius);
    }
  };
  drawSelectedBodyWireframe();
  drawJointGizmos();
  drawBodyGizmo();
  if (viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    ImGuiIO& io = ImGui::GetIO();
    const float localDisplayX = io.MousePos.x - imageMin.x;
    const float localDisplayY = io.MousePos.y - imageMin.y;
    const float localX = localDisplayX * ((float)viewportW / (float)(std::max)(1, displayViewportW));
    const float localY = localDisplayY * ((float)viewportH / (float)(std::max)(1, displayViewportH));
    t850::Ray ray = BuildEditorCameraRay(m_ragdollEditorCamera, localX, localY, viewportW, viewportH);
    auto& authoring = obj.ragdollAuthoring;
    int gizmoAxis = -1;
    if (m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints) &&
        pickJointGizmoAxis(localX, localY, gizmoAxis) &&
        beginJointGizmoDrag(ray, gizmoAxis)) {
      return;
    }
    if (m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies) &&
        m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Edit)) {
      int pickedBody = -1;
      int pickedHandle = -1;
      if (pickBodyHandle(localX, localY, 18.0f, pickedBody, pickedHandle)) {
        m_ragdollEditorSelectedBody = pickedBody;
        m_ragdollEditorSelectedJoint = -1;
        m_ragdollEditorSelectedHandle = pickedHandle;
        m_ragdollEditorHandleDragging = true;
        m_ragdollEditorGizmoDragging = false;
        return;
      }
    }
    if (m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies) &&
        (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move) ||
         m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate)) &&
        pickBodyGizmoAxis(localX, localY, gizmoAxis) &&
        beginBodyGizmoDrag(ray, gizmoAxis)) {
      return;
    }
    auto& bodies = authoring.binding.referencePose.bones;
    auto selectBody = [&](int bodyIndex) {
      if (bodyIndex >= 0 && bodyIndex < (int)bodies.size()) {
        m_ragdollEditorSelectedBody = bodyIndex;
        m_ragdollEditorSelectionMode = static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies);
        m_ragdollEditorSelectedJoint = -1;
        m_ragdollEditorSelectedAffectedBone = -1;
        m_ragdollEditorSelectedUnassignedBone = -1;
        m_ragdollEditorSelectedHandle = -1;
      }
    };
    if (m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints)) {
      int bestJoint = -1;
      pickJointByScreen(localX, localY, 18.0f, bestJoint);
      if (bestJoint >= 0) {
        m_ragdollEditorSelectedJoint = bestJoint;
        m_ragdollEditorSelectedBody = bestJoint;
        m_ragdollEditorSelectedHandle = -1;
      }
    } else if (m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Bones) && viewportSkinned) {
      const xF::xSkeleton* skeleton = viewportSkinned->GetAnimController().GetAnimSkeleton();
      int bestBone = -1;
      float bestDistSq = 12.0f * 12.0f;
      if (skeleton) {
        for (int i = 0; i < (int)skeleton->Bones.size(); ++i) {
          const xF::xBone& bone = skeleton->Bones[(std::size_t)i];
          XVECTOR3 localBone(bone.Combined.m[3][0], bone.Combined.m[3][1], -bone.Combined.m[3][2], 1.0f);
          XVECTOR3 worldBone = t850::TransformPoint(localBone, obj.litInst.Final);
          ImVec2 screen = WorldToScreen(worldBone, m_ragdollEditorCamera.VP, viewportW, viewportH);
          const float dx = screen.x - localX;
          const float dy = screen.y - localY;
          const float distSq = dx * dx + dy * dy;
          if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestBone = i;
          }
        }
      }
      if (bestBone >= 0) {
        t850::ragdoll_editor::RagdollEditorTool tool(authoring);
        int ownerBody = tool.FindBodyControllingBone(bestBone);
        if (ownerBody >= 0) {
          m_ragdollEditorSelectedBody = ownerBody;
          m_ragdollEditorSelectedAffectedBone = bestBone;
          m_ragdollEditorSelectedUnassignedBone = -1;
        } else {
          m_ragdollEditorSelectedUnassignedBone = bestBone;
          m_ragdollEditorSelectedAffectedBone = -1;
        }
      }
    } else {
      int bestBody = -1;
      pickBodyByScreen(localX, localY, 18.0f, bestBody);
      selectBody(bestBody);
    }
  }
  auto cancelRagdollViewportDrags = [&]() {
    m_ragdollEditorGizmoDragging = false;
    m_ragdollEditorHandleDragging = false;
    m_ragdollEditorGizmoAxis = -1;
    m_ragdollEditorSelectedHandle = -1;
  };
  if (viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    ImGuiIO& io = ImGui::GetIO();
    const float localDisplayX = io.MousePos.x - imageMin.x;
    const float localDisplayY = io.MousePos.y - imageMin.y;
    const float localX = localDisplayX * ((float)viewportW / (float)(std::max)(1, displayViewportW));
    const float localY = localDisplayY * ((float)viewportH / (float)(std::max)(1, displayViewportH));
    auto selectBodyAt = [&]() {
      int pickedBody = -1;
      if (pickBodyByScreen(localX, localY, 18.0f, pickedBody)) {
        m_ragdollEditorSelectedBody = pickedBody;
        m_ragdollEditorSelectedJoint = -1;
        m_ragdollEditorSelectionMode = static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies);
        return true;
      }
      return false;
    };
    auto selectJointAt = [&]() {
      int pickedJoint = -1;
      if (pickJointByScreen(localX, localY, 18.0f, pickedJoint)) {
        m_ragdollEditorSelectedJoint = pickedJoint;
        m_ragdollEditorSelectedBody = pickedJoint;
        m_ragdollEditorSelectionMode = static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints);
        return true;
      }
      return false;
    };
    if (m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints)) {
      (void)(selectJointAt() || selectBodyAt());
    } else {
      (void)(selectBodyAt() || selectJointAt());
    }
    cancelRagdollViewportDrags();
    ImGui::OpenPopup("RagdollEditorViewportContextMenu");
  }
  if (ImGui::BeginPopup("RagdollEditorViewportContextMenu")) {
    ImGui::TextDisabled("Target");
    if (ImGui::Selectable("Bodies", m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies))) {
      m_ragdollEditorSelectionMode = static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies);
      cancelRagdollViewportDrags();
    }
    if (ImGui::Selectable("Joints", m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints))) {
      m_ragdollEditorSelectionMode = static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints);
      if (m_ragdollEditorSelectedJoint < 0 && m_ragdollEditorSelectedBody >= 0) {
        m_ragdollEditorSelectedJoint = m_ragdollEditorSelectedBody;
      }
      cancelRagdollViewportDrags();
    }
    if (ImGui::Selectable("Bones", m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Bones))) {
      m_ragdollEditorSelectionMode = static_cast<int>(t850::ragdoll_editor::SelectionMode::Bones);
      cancelRagdollViewportDrags();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Tool");
    if (ImGui::Selectable("Select", m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Select))) {
      m_ragdollEditorToolMode = static_cast<int>(t850::ragdoll_editor::ToolMode::Select);
      cancelRagdollViewportDrags();
    }
    if (ImGui::Selectable(m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints) ? "Edit Joint" : "Edit Body",
                          m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Edit))) {
      m_ragdollEditorToolMode = static_cast<int>(t850::ragdoll_editor::ToolMode::Edit);
      cancelRagdollViewportDrags();
    }
    if (ImGui::Selectable("Move", m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move))) {
      m_ragdollEditorToolMode = static_cast<int>(t850::ragdoll_editor::ToolMode::Move);
      cancelRagdollViewportDrags();
    }
    if (ImGui::Selectable("Rotate", m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate))) {
      m_ragdollEditorToolMode = static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate);
      cancelRagdollViewportDrags();
    }
    ImGui::EndPopup();
  }
  if (viewportHovered || viewportActive) {
    ImGuiIO& io = ImGui::GetIO();
    if (m_ragdollEditorHandleDragging &&
        m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies) &&
        m_ragdollEditorSelectedBody >= 0 &&
        m_ragdollEditorSelectedHandle >= 0 &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const float dragScale = (std::max)(0.001f, m_ragdollEditorOrbitDistance) * 0.0015f;
      XVECTOR3 worldDelta = m_ragdollEditorCamera.Right * (io.MouseDelta.x * dragScale);
      worldDelta += m_ragdollEditorCamera.Up * (-io.MouseDelta.y * dragScale);
      dragBodyHandle(m_ragdollEditorSelectedBody, m_ragdollEditorSelectedHandle, worldDelta);
    } else if (m_ragdollEditorGizmoDragging &&
        (m_ragdollEditorSelectedBody >= 0 || m_ragdollEditorSelectedJoint >= 0) &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const float localDisplayX = io.MousePos.x - imageMin.x;
      const float localDisplayY = io.MousePos.y - imageMin.y;
      const float localX = localDisplayX * ((float)viewportW / (float)(std::max)(1, displayViewportW));
      const float localY = localDisplayY * ((float)viewportH / (float)(std::max)(1, displayViewportH));
      t850::Ray ray = BuildEditorCameraRay(m_ragdollEditorCamera, localX, localY, viewportW, viewportH);
      if (m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints) &&
          m_ragdollEditorSelectedJoint >= 0) {
        if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Edit) ||
            m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move)) {
          float currentParameter = 0.0f;
          if (RagdollClosestRayAxisParameter(ray, m_ragdollEditorGizmoDragCenter, m_ragdollEditorGizmoDragAxis, currentParameter)) {
            const float delta = currentParameter - m_ragdollEditorGizmoLastParameter;
            m_ragdollEditorGizmoLastParameter = currentParameter;
            moveJointByWorldDelta(m_ragdollEditorSelectedJoint, m_ragdollEditorGizmoDragAxis * delta);
          }
        } else if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate)) {
          XVECTOR3 hitPoint;
          if (RagdollRayPlaneIntersection(ray, m_ragdollEditorGizmoDragCenter, m_ragdollEditorGizmoDragAxis, hitPoint)) {
            const XVECTOR3 currentVector = RagdollNormalize3(hitPoint - m_ragdollEditorGizmoDragCenter, m_ragdollEditorGizmoLastVector);
            const float dot = std::clamp(RagdollDot3(m_ragdollEditorGizmoLastVector, currentVector), -1.0f, 1.0f);
            const float angle = std::atan2(RagdollDot3(m_ragdollEditorGizmoDragAxis, RagdollCross3(m_ragdollEditorGizmoLastVector, currentVector)), dot);
            m_ragdollEditorGizmoLastVector = currentVector;
            rotateJointWorld(m_ragdollEditorSelectedJoint, m_ragdollEditorGizmoDragAxis, angle);
          }
        }
      } else if (m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies) &&
                 m_ragdollEditorSelectedBody >= 0 &&
                 m_ragdollEditorSelectedBody < (int)obj.ragdollAuthoring.binding.referencePose.bones.size()) {
        const auto* visualBone = visualBoneForBody(m_ragdollEditorSelectedBody);
        if (!visualBone) {
          return;
        }
        const auto& body = visualBone->body;
        XMATRIX44 desiredWorld = body.worldTransform;
        if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Move)) {
        float currentParameter = 0.0f;
        if (RagdollClosestRayAxisParameter(ray, m_ragdollEditorGizmoDragCenter, m_ragdollEditorGizmoDragAxis, currentParameter)) {
          const float delta = currentParameter - m_ragdollEditorGizmoLastParameter;
          m_ragdollEditorGizmoLastParameter = currentParameter;
          desiredWorld.m41 += m_ragdollEditorGizmoDragAxis.x * delta;
          desiredWorld.m42 += m_ragdollEditorGizmoDragAxis.y * delta;
          desiredWorld.m43 += m_ragdollEditorGizmoDragAxis.z * delta;
          setBodyWorldTransform(m_ragdollEditorSelectedBody, desiredWorld);
        }
        } else if (m_ragdollEditorToolMode == static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate)) {
        XVECTOR3 hitPoint;
        if (RagdollRayPlaneIntersection(ray, m_ragdollEditorGizmoDragCenter, m_ragdollEditorGizmoDragAxis, hitPoint)) {
          const XVECTOR3 currentVector = RagdollNormalize3(hitPoint - m_ragdollEditorGizmoDragCenter, m_ragdollEditorGizmoLastVector);
          const float dot = std::clamp(RagdollDot3(m_ragdollEditorGizmoLastVector, currentVector), -1.0f, 1.0f);
          const float angle = std::atan2(RagdollDot3(m_ragdollEditorGizmoDragAxis, RagdollCross3(m_ragdollEditorGizmoLastVector, currentVector)), dot);
          m_ragdollEditorGizmoLastVector = currentVector;
          if (std::fabs(angle) > 0.000001f) {
            const XVECTOR3 center(body.worldTransform.m41, body.worldTransform.m42, body.worldTransform.m43, 1.0f);
            XMATRIX44 toOrigin;
            XMATRIX44 rotation;
            XMATRIX44 fromOrigin;
            XMatTranslation(toOrigin, -center.x, -center.y, -center.z);
            XMatRotationAxis(rotation, RagdollNormalize3(m_ragdollEditorGizmoDragAxis, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)), angle);
            XMatTranslation(fromOrigin, center.x, center.y, center.z);
            desiredWorld = body.worldTransform * toOrigin * rotation * fromOrigin;
            setBodyWorldTransform(m_ragdollEditorSelectedBody, desiredWorld);
          }
        }
        }
      }
    } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      m_ragdollEditorOrbitYaw += io.MouseDelta.x * 0.005f;
      m_ragdollEditorOrbitPitch += io.MouseDelta.y * 0.005f;
      m_ragdollEditorOrbitPitch = std::clamp(m_ragdollEditorOrbitPitch, -1.45f, 1.45f);
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      const float panScale = m_ragdollEditorOrbitDistance * 0.002f;
      m_ragdollEditorOrbitTarget -= m_ragdollEditorCamera.Right * (io.MouseDelta.x * panScale);
      m_ragdollEditorOrbitTarget += m_ragdollEditorCamera.Up * (io.MouseDelta.y * panScale);
    }
    if (std::fabs(io.MouseWheel) > 0.0001f) {
      const float modelRadius = (std::max)(EstimateRagdollRadius(obj), 0.001f);
      m_ragdollEditorOrbitDistance =
          (std::max)(modelRadius * 0.05f, m_ragdollEditorOrbitDistance - io.MouseWheel * 0.15f * modelRadius);
    }
  }
  if (m_ragdollEditorGizmoDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    m_ragdollEditorGizmoDragging = false;
    m_ragdollEditorGizmoAxis = -1;
  }
  if (m_ragdollEditorHandleDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    m_ragdollEditorHandleDragging = false;
  }

  ImGui::TextDisabled("Orbit: LMB drag  Pan: MMB drag  Zoom: wheel  Menu: RMB");
}

void EditorApp::DrawRagdollEditorBodyPanel(SceneObject& obj) {
  if (!obj.ragdollAuthoringReady) {
    ImGui::TextWrapped("No editable ragdoll is loaded. Load a file or generate an initial ragdoll.");
    return;
  }
  EnsureEditorRagdollState(obj.ragdollAuthoring);
  {
    auto& authoring = obj.ragdollAuthoring;
    t850::ragdoll_editor::RagdollEditorTool tool(authoring);
    tool.EnsureState();

    t850::ragdoll_editor::GuiState guiState;
    guiState.selectedBody = m_ragdollEditorSelectedBody;
    guiState.selectedJoint = m_ragdollEditorSelectedJoint;
    guiState.selectedUnassignedBone = m_ragdollEditorSelectedUnassignedBone;
    guiState.selectedAffectedBone = m_ragdollEditorSelectedAffectedBone;
    guiState.selectionMode = m_ragdollEditorSelectionMode;
    guiState.toolMode = m_ragdollEditorToolMode;
    guiState.showWireframe = m_ragdollEditorShowWireframe;
    guiState.physicsDebug = obj.ragdollDebugDraw && obj.litInst.HasPhysicsRagdoll();
    guiState.skeletonDebug = true;
    guiState.skeletonEditMode = true;
    guiState.simulationSpeedIndex = t850::ragdoll_editor::ClampSimulationSpeedIndex(m_ragdollEditorSimulationSpeedIndex);
    guiState.fixedSimulationDelta = m_ragdollEditorUseFixedSimulationDelta;
    guiState.undoCount = 0;
    guiState.undoLabel = "Undo";
    guiState.dirty = m_ragdollEditorDirty;

    std::vector<std::string> skeletonBoneNames;
    if (t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj)) {
      if (const xF::xSkeleton* skeleton = skinned->GetAnimController().GetAnimSkeleton()) {
        skeletonBoneNames.reserve(skeleton->Bones.size());
        for (const xF::xBone& bone : skeleton->Bones) {
          skeletonBoneNames.push_back(bone.Name);
        }
      }
    }

    auto refreshPreview = [&]() {
      tool.EnsureState();
      obj.ragdollBodyCount = (int)authoring.binding.referencePose.bones.size();
      if (obj.ragdollDebugDraw && obj.ragdollBodyCount > 0) {
        RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
      }
    };
    auto deleteBody = [&](int bodyIndex) {
      if (bodyIndex < 0 || bodyIndex >= (int)authoring.binding.referencePose.bones.size()) {
        return false;
      }
      auto eraseAt = [&](auto& values) {
        if (bodyIndex < (int)values.size()) {
          values.erase(values.begin() + bodyIndex);
        }
      };
      eraseAt(authoring.binding.referencePose.bones);
      eraseAt(authoring.binding.bodyFromBone);
      eraseAt(authoring.binding.jointFromBone);
      eraseAt(authoring.binding.parentJointTwistFromBody);
      eraseAt(authoring.binding.parentJointPlaneFromBody);
      eraseAt(authoring.binding.childJointTwistFromBody);
      eraseAt(authoring.binding.childJointPlaneFromBody);
      eraseAt(authoring.binding.controlledBoneIndices);
      eraseAt(authoring.binding.controlledBodyFromBone);
      eraseAt(authoring.parentBodyIndices);
      eraseAt(authoring.jointParentBodyIndices);
      eraseAt(authoring.frozenBodies);
      eraseAt(authoring.frozenJoints);
      eraseAt(authoring.contactJoints);
      auto fixIndex = [&](int& index) {
        if (index == bodyIndex) index = -1;
        else if (index > bodyIndex) --index;
      };
      for (int& index : authoring.parentBodyIndices) fixIndex(index);
      for (int& index : authoring.jointParentBodyIndices) {
        if (index >= 0) fixIndex(index);
      }
      m_ragdollEditorSelectedBody = (std::min)(m_ragdollEditorSelectedBody, (int)authoring.binding.referencePose.bones.size() - 1);
      m_ragdollEditorSelectedJoint = -1;
      m_ragdollEditorDirty = true;
      refreshPreview();
      return true;
    };
    auto createBodyForBone = [&](int boneIndex, t850::PhysicsShapeType shapeType) {
      t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
      if (!skinned || !skinned->HasSkinData() || boneIndex < 0) {
        obj.ragdollStatus = "Select an unassigned skeleton bone first.";
        return false;
      }
      t850::PhysicsRagdollAuthoringDesc generated;
      if (!t850::BuildRagdollAuthoringFromSkeleton(
              *skinned,
              obj.litInst.Final,
              obj.litInst.GetEntityId(),
              BuildEditorRagdollSettings(obj),
              generated)) {
        obj.ragdollStatus = "Failed to generate a source body for the selected bone.";
        return false;
      }
      EnsureEditorRagdollState(generated);
      t850::ragdoll_editor::RagdollEditorTool generatedTool(generated);
      const int generatedIndex = generatedTool.FindBodyForBone(boneIndex);
      if (generatedIndex < 0) {
        obj.ragdollStatus = "Generated ragdoll has no body for the selected bone.";
        return false;
      }
      t850::PhysicsRagdollBoneDesc newBone = generated.binding.referencePose.bones[(std::size_t)generatedIndex];
      if (shapeType == t850::PhysicsShapeType::Box && newBone.body.shape.type == t850::PhysicsShapeType::Capsule) {
        const float extent = (std::max)(newBone.body.shape.radius, newBone.body.shape.halfHeight);
        newBone.body.shape = t850::PhysicsShapeDesc::Box(XVECTOR3(extent, extent, extent, 0.0f));
      } else {
        newBone.body.shape.type = t850::PhysicsShapeType::Capsule;
      }
      authoring.binding.referencePose.bones.push_back(newBone);
      authoring.binding.bodyFromBone.push_back(generated.binding.bodyFromBone[(std::size_t)generatedIndex]);
      authoring.binding.jointFromBone.push_back(
          generatedIndex < (int)generated.binding.jointFromBone.size()
              ? generated.binding.jointFromBone[(std::size_t)generatedIndex]
              : XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
      authoring.binding.parentJointTwistFromBody.push_back(XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
      authoring.binding.parentJointPlaneFromBody.push_back(XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
      authoring.binding.childJointTwistFromBody.push_back(XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
      authoring.binding.childJointPlaneFromBody.push_back(XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
      authoring.binding.controlledBoneIndices.push_back({boneIndex});
      authoring.binding.controlledBodyFromBone.push_back({generated.binding.bodyFromBone[(std::size_t)generatedIndex]});
      authoring.parentBodyIndices.push_back(t850::kPhysicsRagdollJointInheritParent);
      authoring.jointParentBodyIndices.push_back(t850::kPhysicsRagdollJointInheritParent);
      authoring.frozenBodies.push_back(0);
      authoring.frozenJoints.push_back(0);
      authoring.contactJoints.push_back(0);
      m_ragdollEditorSelectedBody = (int)authoring.binding.referencePose.bones.size() - 1;
      m_ragdollEditorSelectedUnassignedBone = -1;
      m_ragdollEditorSelectedAffectedBone = boneIndex;
      m_ragdollEditorDirty = true;
      refreshPreview();
      return true;
    };

    t850::ragdoll_editor::GuiContext context;
    context.tool = &tool;
    context.state = &guiState;
    context.skeletonBoneNames = std::move(skeletonBoneNames);
    context.modelRadius = EstimateRagdollRadius(obj);
    context.status = obj.ragdollStatus;
    context.callbacks.loadEdits = [&]() {
      DestroyObjectRagdoll(obj);
      obj.ragdollAuthoringReady = false;
      if (LoadObjectRagdollAuthoringFromFile(obj)) {
        EnsureEditorRagdollState(authoring);
        refreshPreview();
        m_ragdollEditorDirty = false;
      }
    };
    context.callbacks.saveEdits = [&]() {
      std::filesystem::path resolvedPath;
      if (t850::SaveRagdollAuthoringAsset(obj.ragdollResourcePath, obj.ragdollModelKey, authoring, &resolvedPath)) {
        obj.ragdollStatus = "Saved ragdoll to " + resolvedPath.string();
        m_ragdollEditorDirty = false;
      } else {
        obj.ragdollStatus = "Failed to save ragdoll asset.";
      }
    };
    context.callbacks.resetAllBodies = [&]() {
      DestroyObjectRagdoll(obj);
      obj.ragdollAuthoringReady = false;
      obj.ragdollAuthoringTried = false;
      if (EnsureObjectRagdollAuthoring(obj)) {
        EnsureEditorRagdollState(authoring);
        refreshPreview();
        m_ragdollEditorDirty = true;
      }
    };
    context.callbacks.clearAllBodies = [&]() {
      DestroyObjectRagdoll(obj);
      obj.ragdollBodyCount = 0;
    };
    context.callbacks.deleteBody = deleteBody;
    context.callbacks.createBodyForBone = createBodyForBone;
    context.callbacks.bodyChanged = [&](int bodyIndex) {
      if (t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj)) {
        if (!t850::UpdateRagdollAuthoringBodyFromLocal(authoring, *skinned, obj.litInst.Final, bodyIndex)) {
          obj.ragdollStatus = "Failed to update ragdoll body transform from local edit.";
        }
      }
      refreshPreview();
    };
    context.callbacks.startSimulation = [&]() { StartObjectRagdollSimulation(obj); };
    context.callbacks.resetPhysicsAnimation = [&]() { ResetObjectRagdollToAnimation(obj); };
    context.callbacks.togglePhysicsDebug = [&]() {
      obj.ragdollDebugDraw = !obj.ragdollDebugDraw;
      if (obj.ragdollDebugDraw) {
        if (!obj.ragdollAuthoringReady && !LoadObjectRagdollAuthoringFromFile(obj)) {
          EnsureObjectRagdollAuthoring(obj);
        }
        if (obj.ragdollAuthoringReady) {
          RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
        }
      } else {
        DestroyObjectRagdoll(obj);
      }
    };
    context.callbacks.toggleSkeletonDebug = [&]() {
      // T8ditor's Ragdoll Edit viewport always draws the skeleton overlay.
    };
    context.callbacks.toggleSkeletonEditMode = [&]() {
      // T8ditor opens this native window already in dedicated edit mode.
    };
    context.callbacks.setSimulationSpeedIndex = [&](int index) {
      m_ragdollEditorSimulationSpeedIndex = t850::ragdoll_editor::ClampSimulationSpeedIndex(index);
      if (m_physics.IsInitialized()) {
        m_physics.SetSimulationSpeedScale(
            t850::ragdoll_editor::SimulationSpeedScaleForIndex(m_ragdollEditorSimulationSpeedIndex));
      }
    };
    context.callbacks.setFixedSimulationDelta = [&](bool fixedDelta) {
      m_ragdollEditorUseFixedSimulationDelta = fixedDelta;
      if (m_physics.IsInitialized()) {
        m_physics.SetUseFixedSimulationDelta(m_ragdollEditorUseFixedSimulationDelta);
      }
    };
    context.callbacks.undo = []() {};

    const int previousSelectionMode = m_ragdollEditorSelectionMode;
    const int previousToolMode = m_ragdollEditorToolMode;
    const int previousSelectedBody = m_ragdollEditorSelectedBody;
    t850::ragdoll_editor::DrawRagdollEditorGui(context);

    m_ragdollEditorSelectedBody = guiState.selectedBody;
    m_ragdollEditorSelectedJoint = guiState.selectedJoint;
    m_ragdollEditorSelectedUnassignedBone = guiState.selectedUnassignedBone;
    m_ragdollEditorSelectedAffectedBone = guiState.selectedAffectedBone;
    m_ragdollEditorSelectionMode = guiState.selectionMode;
    m_ragdollEditorToolMode = guiState.toolMode;
    if (previousSelectionMode != m_ragdollEditorSelectionMode ||
        previousToolMode != m_ragdollEditorToolMode ||
        previousSelectedBody != m_ragdollEditorSelectedBody) {
      m_ragdollEditorGizmoDragging = false;
      m_ragdollEditorHandleDragging = false;
      m_ragdollEditorGizmoAxis = -1;
      m_ragdollEditorSelectedHandle = -1;
    }
    m_ragdollEditorShowWireframe = guiState.showWireframe;
    m_ragdollEditorDirty = guiState.dirty;
    return;
  }

  auto& authoring = obj.ragdollAuthoring;
  t850::ragdoll_editor::RagdollEditorTool tool(authoring);
  tool.EnsureState();
  auto& bones = authoring.binding.referencePose.bones;
  auto recreatePreview = [&]() {
    tool.EnsureState();
    obj.ragdollBodyCount = (int)authoring.binding.referencePose.bones.size();
    if (obj.ragdollDebugDraw && obj.ragdollBodyCount > 0) {
      RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
    }
  };
  auto eraseBody = [&](int bodyIndex) {
    if (bodyIndex < 0 || bodyIndex >= (int)authoring.binding.referencePose.bones.size()) {
      return false;
    }
    auto eraseAt = [&](auto& values) {
      if (bodyIndex < (int)values.size()) {
        values.erase(values.begin() + bodyIndex);
      }
    };
    eraseAt(authoring.binding.referencePose.bones);
    eraseAt(authoring.binding.bodyFromBone);
    eraseAt(authoring.binding.jointFromBone);
    eraseAt(authoring.binding.parentJointTwistFromBody);
    eraseAt(authoring.binding.parentJointPlaneFromBody);
    eraseAt(authoring.binding.childJointTwistFromBody);
    eraseAt(authoring.binding.childJointPlaneFromBody);
    eraseAt(authoring.binding.controlledBoneIndices);
    eraseAt(authoring.binding.controlledBodyFromBone);
    eraseAt(authoring.parentBodyIndices);
    eraseAt(authoring.jointParentBodyIndices);
    eraseAt(authoring.frozenBodies);
    eraseAt(authoring.frozenJoints);
    eraseAt(authoring.contactJoints);
    auto fixIndex = [&](int& index) {
      if (index == bodyIndex) index = -1;
      else if (index > bodyIndex) --index;
    };
    for (int& index : authoring.parentBodyIndices) fixIndex(index);
    for (int& index : authoring.jointParentBodyIndices) {
      if (index >= 0) fixIndex(index);
    }
    m_ragdollEditorSelectedBody = (std::min)(m_ragdollEditorSelectedBody, (int)authoring.binding.referencePose.bones.size() - 1);
    m_ragdollEditorSelectedJoint = -1;
    m_ragdollEditorDirty = true;
    recreatePreview();
    return true;
  };
  auto createBodyForBone = [&](int boneIndex, t850::PhysicsShapeType shapeType) {
    t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
    if (!skinned || !skinned->HasSkinData() || boneIndex < 0) {
      obj.ragdollStatus = "Select an unassigned skeleton bone first.";
      return false;
    }
    t850::PhysicsRagdollAuthoringDesc generated;
    if (!t850::BuildRagdollAuthoringFromSkeleton(
            *skinned,
            obj.litInst.Final,
            obj.litInst.GetEntityId(),
            BuildEditorRagdollSettings(obj),
            generated)) {
      obj.ragdollStatus = "Failed to generate a source body for the selected bone.";
      return false;
    }
    EnsureEditorRagdollState(generated);
    t850::ragdoll_editor::RagdollEditorTool generatedTool(generated);
    int generatedIndex = generatedTool.FindBodyForBone(boneIndex);
    if (generatedIndex < 0) {
      obj.ragdollStatus = "Generated ragdoll has no body for the selected bone.";
      return false;
    }
    t850::PhysicsRagdollBoneDesc newBone = generated.binding.referencePose.bones[(std::size_t)generatedIndex];
    if (shapeType == t850::PhysicsShapeType::Box && newBone.body.shape.type == t850::PhysicsShapeType::Capsule) {
      const float extent = (std::max)(newBone.body.shape.radius, newBone.body.shape.halfHeight);
      newBone.body.shape = t850::PhysicsShapeDesc::Box(XVECTOR3(extent, extent, extent, 0.0f));
    } else {
      newBone.body.shape.type = t850::PhysicsShapeType::Capsule;
    }
    authoring.binding.referencePose.bones.push_back(newBone);
    authoring.binding.bodyFromBone.push_back(generated.binding.bodyFromBone[(std::size_t)generatedIndex]);
    authoring.binding.jointFromBone.push_back(
        generatedIndex < (int)generated.binding.jointFromBone.size()
            ? generated.binding.jointFromBone[(std::size_t)generatedIndex]
            : XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
    authoring.binding.parentJointTwistFromBody.push_back(XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    authoring.binding.parentJointPlaneFromBody.push_back(XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    authoring.binding.childJointTwistFromBody.push_back(XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    authoring.binding.childJointPlaneFromBody.push_back(XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    authoring.binding.controlledBoneIndices.push_back({boneIndex});
    authoring.binding.controlledBodyFromBone.push_back({generated.binding.bodyFromBone[(std::size_t)generatedIndex]});
    authoring.parentBodyIndices.push_back(t850::kPhysicsRagdollJointInheritParent);
    authoring.jointParentBodyIndices.push_back(t850::kPhysicsRagdollJointInheritParent);
    authoring.frozenBodies.push_back(0);
    authoring.frozenJoints.push_back(0);
    authoring.contactJoints.push_back(0);
    m_ragdollEditorSelectedBody = (int)authoring.binding.referencePose.bones.size() - 1;
    m_ragdollEditorSelectedUnassignedBone = -1;
    m_ragdollEditorSelectedAffectedBone = boneIndex;
    m_ragdollEditorDirty = true;
    recreatePreview();
    return true;
  };

  if (ImGui::CollapsingHeader("Viewport Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Selection:");
    ImGui::SameLine();
    ImGui::RadioButton("Bodies", &m_ragdollEditorSelectionMode, static_cast<int>(t850::ragdoll_editor::SelectionMode::Bodies));
    ImGui::SameLine();
    ImGui::RadioButton("Joints", &m_ragdollEditorSelectionMode, static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints));
    ImGui::SameLine();
    ImGui::RadioButton("Bones", &m_ragdollEditorSelectionMode, static_cast<int>(t850::ragdoll_editor::SelectionMode::Bones));
    ImGui::Text("Tool:");
    ImGui::SameLine();
    ImGui::RadioButton("Select", &m_ragdollEditorToolMode, static_cast<int>(t850::ragdoll_editor::ToolMode::Select));
    ImGui::SameLine();
    ImGui::RadioButton(
        m_ragdollEditorSelectionMode == static_cast<int>(t850::ragdoll_editor::SelectionMode::Joints) ? "Edit Joint" : "Edit Body",
        &m_ragdollEditorToolMode,
        static_cast<int>(t850::ragdoll_editor::ToolMode::Edit));
    ImGui::SameLine();
    ImGui::RadioButton("Move", &m_ragdollEditorToolMode, static_cast<int>(t850::ragdoll_editor::ToolMode::Move));
    ImGui::SameLine();
    ImGui::RadioButton("Rotate", &m_ragdollEditorToolMode, static_cast<int>(t850::ragdoll_editor::ToolMode::Rotate));
    ImGui::Checkbox("Show subtle wireframe", &m_ragdollEditorShowWireframe);
  }

  if (ImGui::CollapsingHeader("Body Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Button("Load Ragdoll Edits")) {
      DestroyObjectRagdoll(obj);
      obj.ragdollAuthoringReady = false;
      if (LoadObjectRagdollAuthoringFromFile(obj)) {
        EnsureEditorRagdollState(authoring);
        recreatePreview();
        m_ragdollEditorDirty = false;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(m_ragdollEditorDirty ? "Save Ragdoll Edits *" : "Save Ragdoll Edits")) {
      std::filesystem::path resolvedPath;
      if (t850::SaveRagdollAuthoringAsset(obj.ragdollResourcePath, obj.ragdollModelKey, authoring, &resolvedPath)) {
        obj.ragdollStatus = "Saved ragdoll to " + resolvedPath.string();
        m_ragdollEditorDirty = false;
      } else {
        obj.ragdollStatus = "Failed to save ragdoll asset.";
      }
    }
    if (ImGui::Button("Reset All Bodies")) {
      DestroyObjectRagdoll(obj);
      obj.ragdollAuthoringReady = false;
      obj.ragdollAuthoringTried = false;
      if (EnsureObjectRagdollAuthoring(obj)) {
        EnsureEditorRagdollState(authoring);
        recreatePreview();
        m_ragdollEditorDirty = true;
      }
    }
    ImGui::SameLine();
    const bool canDeleteBody = m_ragdollEditorSelectedBody >= 0 && m_ragdollEditorSelectedBody < (int)bones.size();
    if (!canDeleteBody) ImGui::BeginDisabled();
    if (ImGui::Button("Delete Selected Body")) {
      eraseBody(m_ragdollEditorSelectedBody);
    }
    if (!canDeleteBody) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear All Bodies")) {
      authoring.binding.referencePose.bones.clear();
      authoring.binding.bodyFromBone.clear();
      authoring.binding.jointFromBone.clear();
      authoring.binding.parentJointTwistFromBody.clear();
      authoring.binding.parentJointPlaneFromBody.clear();
      authoring.binding.childJointTwistFromBody.clear();
      authoring.binding.childJointPlaneFromBody.clear();
      authoring.binding.controlledBoneIndices.clear();
      authoring.binding.controlledBodyFromBone.clear();
      authoring.parentBodyIndices.clear();
      authoring.jointParentBodyIndices.clear();
      authoring.frozenBodies.clear();
      authoring.frozenJoints.clear();
      authoring.contactJoints.clear();
      m_ragdollEditorSelectedBody = -1;
      m_ragdollEditorDirty = true;
      DestroyObjectRagdoll(obj);
      obj.ragdollBodyCount = 0;
    }
    const bool canCreateBody = m_ragdollEditorSelectedUnassignedBone >= 0;
    if (!canCreateBody) ImGui::BeginDisabled();
    if (ImGui::Button("Create Capsule")) {
      createBodyForBone(m_ragdollEditorSelectedUnassignedBone, t850::PhysicsShapeType::Capsule);
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Box")) {
      createBodyForBone(m_ragdollEditorSelectedUnassignedBone, t850::PhysicsShapeType::Box);
    }
    if (!canCreateBody) ImGui::EndDisabled();
  }

  if (ImGui::CollapsingHeader("Runtime Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Button("Start Simulation")) {
      StartObjectRagdollSimulation(obj);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Physics/Animation")) {
      ResetObjectRagdollToAnimation(obj);
    }
    ImGui::TextDisabled("Preview controls affect only this editor session. Export is controlled by the scene-level ragdoll toggle.");
  }

  if (bones.empty()) {
    ImGui::TextWrapped("The ragdoll has no bodies.");
    return;
  }
  if (m_ragdollEditorSelectedBody < 0 || m_ragdollEditorSelectedBody >= (int)bones.size()) {
    m_ragdollEditorSelectedBody = 0;
  }

  std::vector<std::string> bodyLabels;
  bodyLabels.reserve(bones.size());
  for (int i = 0; i < (int)bones.size(); ++i) {
    const auto& body = bones[(std::size_t)i].body;
    bodyLabels.push_back(std::to_string(i) + ": " + RagdollShapeTypeName(body.shape.type) +
                         " bone " + std::to_string(body.boneIndex) + " " + body.debugName);
  }
  std::vector<const char*> bodyItems;
  bodyItems.reserve(bodyLabels.size());
  for (const std::string& label : bodyLabels) bodyItems.push_back(label.c_str());
  ImGui::SetNextItemWidth(360.0f);
  ImGui::Combo("Body", &m_ragdollEditorSelectedBody, bodyItems.data(), (int)bodyItems.size());

  const int bodyIndex = m_ragdollEditorSelectedBody;
  auto& bone = bones[(std::size_t)bodyIndex];
  auto& shape = bone.body.shape;
  auto& localBodyFromBone = authoring.binding.bodyFromBone[(std::size_t)bodyIndex];
  bool previewNeedsRebuild = false;
  bool bodyFrozen = tool.IsBodyFrozen(bodyIndex);
  if (ImGui::Checkbox("Freeze Body", &bodyFrozen)) {
    tool.SetBodyFrozen(bodyIndex, bodyFrozen);
    m_ragdollEditorDirty = true;
  }

  char nameBuffer[128] = {};
  std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", bone.body.debugName.c_str());
  if (ImGui::InputText("Body Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
    bone.body.debugName = nameBuffer[0] ? nameBuffer : ("body_" + std::to_string(bodyIndex));
    m_ragdollEditorDirty = true;
  }
  ImGui::Text("Skeleton bone: %d", bone.body.boneIndex);

  if (ImGui::CollapsingHeader("Body Relationships", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto buildBodyOptions = [&]() {
      std::vector<std::string> labels;
      labels.emplace_back("<none>");
      for (int i = 0; i < (int)bones.size(); ++i) {
        if (i == bodyIndex) continue;
        labels.push_back(std::to_string(i) + ": " + bones[(std::size_t)i].body.debugName);
      }
      return labels;
    };
    auto bodyOptionToIndex = [&](int option) {
      if (option <= 0) return -1;
      int current = 1;
      for (int i = 0; i < (int)bones.size(); ++i) {
        if (i == bodyIndex) continue;
        if (current == option) return i;
        ++current;
      }
      return -1;
    };
    auto indexToBodyOption = [&](int index) {
      if (index < 0) return 0;
      int current = 1;
      for (int i = 0; i < (int)bones.size(); ++i) {
        if (i == bodyIndex) continue;
        if (i == index) return current;
        ++current;
      }
      return 0;
    };

    std::vector<std::string> bodyOptions = buildBodyOptions();
    std::vector<const char*> bodyOptionItems;
    for (const std::string& label : bodyOptions) bodyOptionItems.push_back(label.c_str());

    int parentOption = indexToBodyOption(authoring.parentBodyIndices[(std::size_t)bodyIndex]);
    if (ImGui::Combo("Logical Parent", &parentOption, bodyOptionItems.data(), (int)bodyOptionItems.size())) {
      authoring.parentBodyIndices[(std::size_t)bodyIndex] = bodyOptionToIndex(parentOption);
      m_ragdollEditorDirty = true;
    }

    std::vector<std::string> jointOptions;
    jointOptions.emplace_back("Inherit Logical Parent");
    jointOptions.emplace_back("Disabled");
    for (int i = 0; i < (int)bones.size(); ++i) {
      if (i == bodyIndex) continue;
      jointOptions.push_back(std::to_string(i) + ": " + bones[(std::size_t)i].body.debugName);
    }
    std::vector<const char*> jointItems;
    for (const std::string& label : jointOptions) jointItems.push_back(label.c_str());
    auto jointParentToOption = [&](int parent) {
      if (parent == t850::kPhysicsRagdollJointDisabled) return 1;
      if (parent == t850::kPhysicsRagdollJointInheritParent) return 0;
      int current = 2;
      for (int i = 0; i < (int)bones.size(); ++i) {
        if (i == bodyIndex) continue;
        if (i == parent) return current;
        ++current;
      }
      return 0;
    };
    auto jointOptionToParent = [&](int option) {
      if (option == 1) return t850::kPhysicsRagdollJointDisabled;
      if (option <= 0) return t850::kPhysicsRagdollJointInheritParent;
      int current = 2;
      for (int i = 0; i < (int)bones.size(); ++i) {
        if (i == bodyIndex) continue;
        if (current == option) return i;
        ++current;
      }
      return t850::kPhysicsRagdollJointInheritParent;
    };
    int jointOption = jointParentToOption(authoring.jointParentBodyIndices[(std::size_t)bodyIndex]);
    if (ImGui::Combo("Joint Parent", &jointOption, jointItems.data(), (int)jointItems.size())) {
      authoring.jointParentBodyIndices[(std::size_t)bodyIndex] = jointOptionToParent(jointOption);
      m_ragdollEditorDirty = true;
      previewNeedsRebuild = true;
    }
    bool contactJoint = authoring.contactJoints[(std::size_t)bodyIndex] != 0;
    if (ImGui::Checkbox("Contact Joint", &contactJoint)) {
      authoring.contactJoints[(std::size_t)bodyIndex] = contactJoint ? 1 : 0;
      m_ragdollEditorDirty = true;
      previewNeedsRebuild = true;
    }
  }

  if (ImGui::CollapsingHeader("Affected Bones", ImGuiTreeNodeFlags_DefaultOpen)) {
    t850::RenderSkinnedMesh* selectedSkinned = GetSkinnedMesh(obj);
    const xF::xSkeleton* skeleton = selectedSkinned ? selectedSkinned->GetAnimController().GetAnimSkeleton() : nullptr;
    auto& controlledBones = authoring.binding.controlledBoneIndices[(std::size_t)bodyIndex];
    auto containsBone = [](const std::vector<int>& list, int boneIndex) {
      return std::find(list.begin(), list.end(), boneIndex) != list.end();
    };
    if (!skeleton) {
      ImGui::TextDisabled("Skeleton bone names are unavailable.");
    } else {
      std::vector<int> unassignedBones;
      for (int boneIndex = 0; boneIndex < (int)skeleton->Bones.size(); ++boneIndex) {
        if (tool.FindBodyControllingBone(boneIndex) < 0) unassignedBones.push_back(boneIndex);
      }
      const float listHeight = (std::max)(120.0f, ImGui::GetTextLineHeightWithSpacing() * 7.0f);
      ImGui::Columns(3, "t8ditor_ragdoll_bone_assignment", false);
      ImGui::Text("Unassigned (%zu)", unassignedBones.size());
      ImGui::BeginChild("unassigned_bones", ImVec2(0.0f, listHeight), true);
      for (int boneIndex : unassignedBones) {
        std::string label = std::to_string(boneIndex) + ": " + skeleton->Bones[(std::size_t)boneIndex].Name;
        if (ImGui::Selectable(label.c_str(), m_ragdollEditorSelectedUnassignedBone == boneIndex)) {
          m_ragdollEditorSelectedUnassignedBone = boneIndex;
          m_ragdollEditorSelectedAffectedBone = -1;
        }
      }
      ImGui::EndChild();

      ImGui::NextColumn();
      const bool canAdd = m_ragdollEditorSelectedUnassignedBone >= 0;
      if (!canAdd) ImGui::BeginDisabled();
      if (ImGui::Button("Add ->")) {
        controlledBones.push_back(m_ragdollEditorSelectedUnassignedBone);
        authoring.binding.controlledBodyFromBone[(std::size_t)bodyIndex].push_back(authoring.binding.bodyFromBone[(std::size_t)bodyIndex]);
        m_ragdollEditorSelectedAffectedBone = m_ragdollEditorSelectedUnassignedBone;
        m_ragdollEditorSelectedUnassignedBone = -1;
        m_ragdollEditorDirty = true;
        previewNeedsRebuild = true;
      }
      if (!canAdd) ImGui::EndDisabled();
      const bool canRemove = containsBone(controlledBones, m_ragdollEditorSelectedAffectedBone);
      if (!canRemove) ImGui::BeginDisabled();
      if (ImGui::Button("<- Remove")) {
        const int removeBone = m_ragdollEditorSelectedAffectedBone;
        for (int i = 0; i < (int)controlledBones.size(); ++i) {
          if (controlledBones[(std::size_t)i] == removeBone) {
            controlledBones.erase(controlledBones.begin() + i);
            auto& offsets = authoring.binding.controlledBodyFromBone[(std::size_t)bodyIndex];
            if (i < (int)offsets.size()) offsets.erase(offsets.begin() + i);
            break;
          }
        }
        m_ragdollEditorSelectedUnassignedBone = removeBone;
        m_ragdollEditorSelectedAffectedBone = -1;
        m_ragdollEditorDirty = true;
        previewNeedsRebuild = true;
      }
      if (!canRemove) ImGui::EndDisabled();

      ImGui::NextColumn();
      ImGui::Text("Affected (%zu)", controlledBones.size());
      ImGui::BeginChild("affected_bones", ImVec2(0.0f, listHeight), true);
      for (int boneIndex : controlledBones) {
        std::string label = std::to_string(boneIndex);
        if (boneIndex >= 0 && boneIndex < (int)skeleton->Bones.size()) {
          label += ": " + skeleton->Bones[(std::size_t)boneIndex].Name;
        }
        if (ImGui::Selectable(label.c_str(), m_ragdollEditorSelectedAffectedBone == boneIndex)) {
          m_ragdollEditorSelectedAffectedBone = boneIndex;
          m_ragdollEditorSelectedUnassignedBone = -1;
        }
      }
      ImGui::EndChild();
      ImGui::Columns(1);
    }
  }

  if (bodyFrozen) ImGui::BeginDisabled();
  int shapeType = shape.type == t850::PhysicsShapeType::Box ? 1 : 0;
  if (ImGui::RadioButton("Capsule", shapeType == 0)) {
    shapeType = 0;
    shape.type = t850::PhysicsShapeType::Capsule;
    m_ragdollEditorDirty = true;
    previewNeedsRebuild = true;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Box", shapeType == 1)) {
    shapeType = 1;
    shape.type = t850::PhysicsShapeType::Box;
    m_ragdollEditorDirty = true;
    previewNeedsRebuild = true;
  }

  const float modelRadius = EstimateRagdollRadius(obj);
  const float dragStep = (std::max)(0.0005f, modelRadius * 0.0005f);
  if (shape.type == t850::PhysicsShapeType::Capsule) {
    float radius = shape.radius;
    float totalLength = (shape.radius + shape.halfHeight) * 2.0f;
    if (ImGui::DragFloat("Capsule Radius", &radius, dragStep, 0.0001f, (std::max)(0.001f, modelRadius), "%.4f")) {
      shape.radius = (std::max)(0.0001f, radius);
      shape.halfHeight = (std::max)(0.0001f, totalLength * 0.5f - shape.radius);
      m_ragdollEditorDirty = true;
      previewNeedsRebuild = true;
    }
    if (ImGui::DragFloat("Capsule Total Length", &totalLength, dragStep, 0.0003f, (std::max)(0.003f, modelRadius * 4.0f), "%.4f")) {
      totalLength = (std::max)(shape.radius * 2.0f + 0.0002f, totalLength);
      shape.halfHeight = (std::max)(0.0001f, totalLength * 0.5f - shape.radius);
      m_ragdollEditorDirty = true;
      previewNeedsRebuild = true;
    }
  } else if (shape.type == t850::PhysicsShapeType::Box) {
    float halfExtents[3] = {shape.halfExtents.x, shape.halfExtents.y, shape.halfExtents.z};
    if (ImGui::DragFloat3("Box Half Extents", halfExtents, dragStep, 0.0001f, (std::max)(0.001f, modelRadius * 2.0f), "%.4f")) {
      shape.halfExtents = XVECTOR3(
          (std::max)(0.0001f, halfExtents[0]),
          (std::max)(0.0001f, halfExtents[1]),
          (std::max)(0.0001f, halfExtents[2]),
          0.0f);
      m_ragdollEditorDirty = true;
      previewNeedsRebuild = true;
    }
  }

  float translation[3], rotationDeg[3], scale[3];
  RagdollMatrixToComponents(localBodyFromBone, translation, rotationDeg, scale);
  bool transformChanged = false;
  transformChanged |= ImGui::DragFloat3("Local Translate", translation, dragStep, 0.0f, 0.0f, "%.4f");
  transformChanged |= ImGui::DragFloat3("Local Rotate XYZ", rotationDeg, 0.25f, -180.0f, 180.0f, "%.2f deg");
  if (transformChanged) {
    localBodyFromBone = RagdollMatrixFromComponents(translation, rotationDeg, scale);
    if (t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj)) {
      if (!t850::UpdateRagdollAuthoringBodyFromLocal(authoring, *skinned, obj.litInst.Final, bodyIndex)) {
        obj.ragdollStatus = "Failed to update ragdoll body transform from local edit.";
      }
    }
    m_ragdollEditorDirty = true;
    previewNeedsRebuild = true;
  }

  float swingDeg = bone.swingLimitRadians * kRadToDeg;
  float twistDeg = bone.twistLimitRadians * kRadToDeg;
  if (ImGui::DragFloat("Swing Limit", &swingDeg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
    bone.swingLimitRadians = std::clamp(swingDeg, 0.0f, 180.0f) * kDegToRad;
    m_ragdollEditorDirty = true;
    previewNeedsRebuild = true;
  }
  if (ImGui::DragFloat("Twist Limit", &twistDeg, 0.5f, 0.0f, 180.0f, "%.1f deg")) {
    bone.twistLimitRadians = std::clamp(twistDeg, 0.0f, 180.0f) * kDegToRad;
    m_ragdollEditorDirty = true;
    previewNeedsRebuild = true;
  }
  if (bodyFrozen) ImGui::EndDisabled();

  if (previewNeedsRebuild && obj.ragdollDebugDraw) {
    RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
  }

  if (ImGui::CollapsingHeader("Joints", ImGuiTreeNodeFlags_DefaultOpen)) {
    std::vector<int> jointBodies;
    std::vector<std::string> jointLabels;
    for (int i = 0; i < (int)bones.size(); ++i) {
      const int parent = (i < (int)authoring.jointParentBodyIndices.size()) ? authoring.jointParentBodyIndices[(std::size_t)i] : -1;
      if (parent >= 0 && parent < (int)bones.size() && parent != i) {
        jointBodies.push_back(i);
        jointLabels.push_back(std::to_string(i) + " " + bones[(std::size_t)i].body.debugName +
                              " <- " + std::to_string(parent) + " " + bones[(std::size_t)parent].body.debugName);
      }
    }
    if (jointBodies.empty()) {
      ImGui::TextDisabled("No explicit joints are assigned in this authored file.");
    } else {
      if (m_ragdollEditorSelectedJoint < 0) m_ragdollEditorSelectedJoint = jointBodies.front();
      int jointOption = 0;
      for (int i = 0; i < (int)jointBodies.size(); ++i) {
        if (jointBodies[(std::size_t)i] == m_ragdollEditorSelectedJoint) jointOption = i;
      }
      std::vector<const char*> jointItems;
      for (const std::string& label : jointLabels) jointItems.push_back(label.c_str());
      if (ImGui::Combo("Joint", &jointOption, jointItems.data(), (int)jointItems.size())) {
        m_ragdollEditorSelectedJoint = jointBodies[(std::size_t)jointOption];
      }
      const int child = m_ragdollEditorSelectedJoint;
      auto& childBone = bones[(std::size_t)child];
      bool jointFrozen = tool.IsJointFrozen(child);
      if (ImGui::Checkbox("Freeze Joint", &jointFrozen)) {
        tool.SetJointFrozen(child, jointFrozen);
        m_ragdollEditorDirty = true;
      }
      if (jointFrozen) ImGui::BeginDisabled();
      int jointType = childBone.jointType == t850::PhysicsRagdollJointType::Fixed ? 1 : 0;
      const char* jointTypes[] = {"Swing/Twist", "Fixed"};
      if (ImGui::Combo("Joint Type", &jointType, jointTypes, 2)) {
        childBone.jointType = jointType == 1 ? t850::PhysicsRagdollJointType::Fixed : t850::PhysicsRagdollJointType::SwingTwist;
        m_ragdollEditorDirty = true;
        if (obj.ragdollDebugDraw) {
          RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
        }
      }
      ImGui::Text("Current: %s", RagdollJointTypeName(childBone.jointType));
      if (jointFrozen) ImGui::EndDisabled();
    }
  }
}

void EditorApp::DrawRagdollEditorWindow() {
  if (!m_ragdollEditorOpen) {
    return;
  }

  ImGuiSetNextNativeEditorWindow(96.0f, 96.0f, 840.0f, 720.0f);
  m_ragdollEditorOpenRequested = false;

  bool keepOpen = m_ragdollEditorOpen;
  if (!ImGui::Begin("Ragdoll Edit", &keepOpen, ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    if (!keepOpen) CloseRagdollEditor();
    return;
  }

  if (!keepOpen) {
    ImGui::End();
    CloseRagdollEditor();
    return;
  }

  if (ImGuiViewport* windowViewport = ImGui::GetWindowViewport()) {
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ApplyNativeWindowChrome(windowViewport, "Ragdoll Edit");
    void* nativeHandle = NativeHandleFromImGuiViewport(windowViewport);
    m_ragdollEditorNativeHandle = nativeHandle;

    if (mainViewport && windowViewport->ID == mainViewport->ID) {
      if (!m_ragdollEditorMainViewportLogged) {
        ImGuiIO& io = ImGui::GetIO();
        T8_LOG_INFO("[T8ditor] Native editor window pending title='Ragdoll Edit': still merged with main viewport id=0x%08X hwnd=%p configFlags=0x%08X backendFlags=0x%08X",
                    (unsigned int)windowViewport->ID,
                    nativeHandle,
                    (unsigned int)io.ConfigFlags,
                    (unsigned int)io.BackendFlags);
        m_ragdollEditorMainViewportLogged = true;
      }
    } else if (nativeHandle && nativeHandle != m_ragdollEditorLoggedNativeHandle) {
      T8_LOG_INFO("[T8ditor] Native editor window created title='Ragdoll Edit' viewportId=0x%08X sdlWindow=%p hwnd=%p pos=(%.1f, %.1f) size=(%.1f, %.1f) flags=0x%08X",
                  (unsigned int)windowViewport->ID,
                  windowViewport->PlatformHandle,
                  nativeHandle,
                  windowViewport->Pos.x,
                  windowViewport->Pos.y,
                  windowViewport->Size.x,
                  windowViewport->Size.y,
                  (unsigned int)windowViewport->Flags);
      m_ragdollEditorLoggedNativeHandle = nativeHandle;
      m_ragdollEditorMainViewportLogged = false;
    }
  }

  if (m_ragdollEditorObjectIndex < 0 || m_ragdollEditorObjectIndex >= (int)g_objects.size()) {
    ImGui::TextWrapped("The ragdoll editor selection is no longer valid.");
    if (ImGui::Button("Close")) {
      CloseRagdollEditor();
    }
    ImGui::End();
    return;
  }

  SceneObject& obj = g_objects[m_ragdollEditorObjectIndex];
  ImGui::Text("Editing: %s", obj.name.c_str());
  if (ImGui::CollapsingHeader("Scene Ragdoll Metadata")) {
    ImGui::TextDisabled("Native handle: %p", m_ragdollEditorNativeHandle);
    t850::scene::SceneObjectRagdollDesc& meta = EnsureRagdollMeta(obj);
    if (ImGui::Checkbox("Export With Ragdoll", &meta.enabled)) {
      SyncRagdollMetaFromObject(obj);
    }
    bool showPhysicsObjects = obj.ragdollDebugDraw && obj.litInst.HasPhysicsRagdoll();
    if (ImGui::Checkbox("Show Capsules / Physics Objects", &showPhysicsObjects)) {
      obj.ragdollDebugDraw = showPhysicsObjects;
      if (showPhysicsObjects) {
        if (!obj.ragdollAuthoringReady && !LoadObjectRagdollAuthoringFromFile(obj)) {
          EnsureObjectRagdollAuthoring(obj);
        }
        if (obj.ragdollAuthoringReady) {
          RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
        }
      } else {
        DestroyObjectRagdoll(obj);
      }
      meta.preview = showPhysicsObjects;
      meta.runtime_motion = showPhysicsObjects ? "kinematic" : "disabled";
    }

    if (InputTextString("Ragdoll File", meta.asset)) {
      obj.ragdollResourcePath = meta.asset;
      obj.ragdollAuthoringReady = false;
      obj.ragdollLoadedFromAsset = false;
      m_ragdollEditorDirty = false;
    }
    if (ImGui::Button("Browse")) {
      const std::string path = OpenFileDialog(
          L"Ragdoll JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0",
          L"Load Ragdoll");
      if (!path.empty()) {
        meta.asset = path;
        obj.ragdollResourcePath = path;
        obj.ragdollAuthoringReady = false;
        obj.ragdollLoadedFromAsset = false;
        m_ragdollEditorDirty = false;
      }
    }
  }

  ImGui::Separator();
  const float footerHeight = ImGui::GetFrameHeightWithSpacing() + 10.0f;
  const ImVec2 editorAvail = ImGui::GetContentRegionAvail();
  const float splitWidth = (std::max)(360.0f, editorAvail.x * 0.58f);
  ImGui::Columns(2, "RagdollEditViewportColumns", true);
  ImGui::SetColumnWidth(0, splitWidth);
  ImGui::BeginChild("RagdollEditorViewport", ImVec2(0.0f, -footerHeight), true, ImGuiWindowFlags_NoScrollbar);
  DrawRagdollEditorViewport(obj);
  ImGui::EndChild();

  ImGui::NextColumn();
  ImGui::BeginChild("RagdollEditorBodyPanel", ImVec2(0.0f, -footerHeight), true);
  DrawRagdollEditorBodyPanel(obj);
  ImGui::EndChild();
  ImGui::Columns(1);

  if (!obj.ragdollStatus.empty()) {
    ImGui::TextWrapped("%s", obj.ragdollStatus.c_str());
    ImGui::SameLine();
  }
  if (ImGui::Button("Close")) {
    CloseRagdollEditor();
  }
  ImGui::End();
}

} // namespace t8ditor
