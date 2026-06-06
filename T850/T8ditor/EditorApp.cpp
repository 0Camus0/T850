/*********************************************************
 * T8ditor - EditorApp implementation. See header.
 *********************************************************/

#include "EditorApp.h"
#include "SceneObject.h"
#include "SceneGraph.h"
#include "EditorScene.h"
#include "EditorSceneGizmos.h"
#include "UndoRedo.h"
#include "../DayScene/RagdollEditor.h"
#include "../DayScene/Quake3Mock.h"

#include <core/Core.h>
#include <core/EngineContext.h>
#include <video/BaseDriver.h>
#include <physics/PhysicsAuthoring.h>
#include <physics/Q3BspCollision.h>
#include <physics/RagdollEditorTool.h>
#include <scene/IBLResources.h>
#include <scene/MeshAssetCache.h>
#include <scene/RenderGraph.h>
#include <scene/RenderSkinnedMesh.h>
#include <utils/InputManager.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <utils/RuntimeProfile.h>
#include <utils/xMaths.h>
#include <utils/Picking.h>
#include <debug/FrameDumper.h>
#include <debug/LoadingProgress.h>
#include <imgui/DevGuiContext.h>
#include <imgui/RagdollEditorGui.h>

#include <Descriptors.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <map>
#include <cstring>

#include <imgui.h>
#include <ImGuizmo.h>
#include <SDL3/SDL.h>

#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif

namespace t850 {
  extern Device* T8Device;
}

namespace t8ditor {

EditorApp::~EditorApp() = default;

static ImVec2 WorldToScreen(const XVECTOR3& p, const XMATRIX44& vp, int w, int h);
static t850::Ray BuildEditorCameraRay(const ::Camera& camera,
                                      float mouseX,
                                      float mouseY,
                                      int viewW,
                                      int viewH);

namespace {
  std::string g_startupMeshPath;
  int g_startupDumpFrame = -1;
  const float kRadToDeg = 180.0f / xPI;
  const float kDegToRad = xPI / 180.0f;
  constexpr float kMinEditableScale = 0.000001f;
  constexpr float kScaleDragSpeed = 0.0001f;

  // Persistent skybox (editor backdrop, separate from scene meshes).
  t850::PrimitiveManager g_skyboxMgr;
  t850::PrimitiveInst    g_skyboxInst;
  int                    g_skyboxPrimId = -1;
  bool                   g_skyboxReady  = false;

  // Multi-mesh scene objects (file-scope because EditorApp.h is locked).
  std::vector<SceneObject> g_objects;
  int                      g_selectedIdx = -1;

  // Multi-selection: set of selected mesh indices
  std::set<int>            g_multiSelect;

  // Groups (persistent and temporary)
  std::vector<SceneGroup>  g_groups;           // persistent groups
  SceneGroup               g_tempGroup;        // temporary group from multi-select
  int                      g_activeGroupIdx = -1; // index into g_groups, or -1 for temp/none

  // Cameras and lights in the scene
  std::vector<SceneCamera> g_cameras;
  std::vector<SceneLight>  g_lights;

  // Selection: what type of entity is selected
  // 0=mesh, 1=camera, 2=light. Index is g_selectedIdx into the respective vector.
  int g_selectionType = 0;  // 0=mesh by default

  // Marquee box selection state
  bool     g_marqueeActive = false;
  ImVec2   g_marqueeStart  = {0, 0};

  // Active camera index (-1 = default editor camera)
  int g_activeCameraIdx = -1;

  // Undo/redo
  UndoStack g_undoStack;

  // ImGuizmo drag tracking
  bool           g_gizmoDragging = false;
  TransformState g_gizmoDragStart;

  // Persistent camera for scene camera viewport switching
  ::Camera g_viewCamera;

  // Deferred render graph
  t850::RenderGraph   g_renderGraph;
  t850::PrimitiveInst g_quads[8];
  bool                g_deferredReady = false;
  XMATRIX44           g_quadVP;  // persistent identity matrix for screen-space quads

  // RT debug: which RT attachment to display (-1 = backbuffer)
  int g_debugRT = -1;

  // Dummy 1x1 white texture for shadow slot
  t850::Texture* g_dummyWhiteTex = nullptr;

  // Dummy environment map (1x1 gray cube for skybox matID=0)
  int g_dummyEnvMapIdx = -1;

  // Pending scene load — deferred to execute before next frame's BeginFrame
  std::string g_pendingLoadPath;
  std::string g_pendingDeleteAfterLoadPath;
  SceneFile g_loadedSceneFile;
  bool g_hasLoadedSceneFile = false;
  std::vector<SceneObjectDesc> g_unloadedSceneObjects;
  std::string g_sceneCollisionResourcePath;
  std::vector<t850::SandboxProfileDesc> g_sceneProfiles;
  std::unique_ptr<t850::Q3BspCollisionWorld> g_q3CollisionWorld;

  // Frame dumper for RT snapshot debugging (space key)
  t850::FrameDumper g_dumper;
  bool              g_dumperInited = false;

  std::string FormatLoadingProgressForConsole(const t850::LoadingProgress::Snapshot& snapshot) {
    if (!snapshot.active) {
      return {};
    }
    std::string text = "[Loading] " + snapshot.phase;
    if (!snapshot.item.empty()) {
      text += ": " + snapshot.item;
    }
    if (!snapshot.detail.empty()) {
      text += " - " + snapshot.detail;
    }
    char percentText[32] = {};
    std::snprintf(percentText, sizeof(percentText), " (%.0f%%)", snapshot.percent);
    text += percentText;
    return text;
  }

  void LogEditorLoadingLabel(const char* phase, const char* item) {
    T8_LOG_INFO("[Loading] %s: %s", phase ? phase : "", item ? item : "");
  }
}

void SetStartupMeshPath(const std::string& p) {
  g_startupMeshPath = p;
}

void SetStartupDumpFrame(int frame) {
  g_startupDumpFrame = frame;
}

// Helpers to access current selection
static SceneObject* SelectedObject() {
  if (g_selectionType == 0 && g_selectedIdx >= 0 && g_selectedIdx < (int)g_objects.size())
    return &g_objects[g_selectedIdx];
  return nullptr;
}

static t850::RenderSkinnedMesh* GetSkinnedMesh(SceneObject& obj) {
  return obj.litInst.GetSkinnedMesh();
}

static bool InputTextString(const char* label, std::string& value) {
  char buffer[512] = {};
  std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
  if (ImGui::InputText(label, buffer, sizeof(buffer))) {
    value = buffer;
    return true;
  }
  return false;
}

static t850::scene::SceneObjectPhysicsDesc& EnsurePhysicsMeta(SceneObject& obj) {
  if (!obj.physics) {
    obj.physics = t850::scene::SceneObjectPhysicsDesc{};
  }
  return *obj.physics;
}

static t850::scene::SceneObjectNavigationDesc& EnsureNavigationMeta(SceneObject& obj) {
  if (!obj.navigation) {
    obj.navigation = t850::scene::SceneObjectNavigationDesc{};
  }
  return *obj.navigation;
}

static t850::scene::SceneObjectRagdollDesc& EnsureRagdollMeta(SceneObject& obj) {
  if (!obj.ragdollAuthoringMeta) {
    t850::scene::SceneObjectRagdollDesc meta;
    meta.enabled = false;
    meta.asset = obj.ragdollResourcePath;
    meta.preview = obj.ragdollPreviewEnabled;
    meta.drive_from_animation = obj.ragdollDriveFromAnimation;
    meta.runtime_motion = obj.ragdollSimulating ? "dynamic" : (obj.ragdollPreviewEnabled ? "kinematic" : "disabled");
    obj.ragdollAuthoringMeta = meta;
  }
  return *obj.ragdollAuthoringMeta;
}

static void ExpandEditorAABB(t850::AABB& dst, const t850::AABB& src) {
  if (!src.IsValid()) {
    return;
  }
  if (!dst.IsValid()) {
    dst = src;
    return;
  }
  dst.ExpandToInclude(src.vMin);
  dst.ExpandToInclude(src.vMax);
}

static bool GetSkinnedSkeletonWorldAABB(const SceneObject& object, t850::AABB& outBounds) {
  const t850::RenderSkinnedMesh* skinned = object.litInst.GetSkinnedMesh();
  if (!skinned || !skinned->HasSkinData()) {
    return false;
  }

  t850::RenderMesh::AABB skeletonLocal;
  if (!skinned->GetSkeletonLocalAABB(skeletonLocal)) {
    return false;
  }

  const t850::AABB local(
      XVECTOR3(skeletonLocal.min.x, skeletonLocal.min.y, skeletonLocal.min.z, 1.0f),
      XVECTOR3(skeletonLocal.max.x, skeletonLocal.max.y, skeletonLocal.max.z, 1.0f));
  outBounds = local.Transformed(object.wireframe.BuildWorld());
  return outBounds.IsValid();
}

static bool GetEditorObjectWorldAABB(const SceneObject& object,
                                     t850::AABB& outBounds,
                                     t850::AABB* outWireBounds = nullptr,
                                     t850::AABB* outSkeletonBounds = nullptr,
                                     bool* outHasSkeletonBounds = nullptr) {
  outBounds = t850::AABB{};
  bool haveBounds = false;

  if (object.wireframe.IsLoaded()) {
    const t850::AABB wireBounds = object.wireframe.WorldAABB();
    if (outWireBounds) {
      *outWireBounds = wireBounds;
    }
    ExpandEditorAABB(outBounds, wireBounds);
    haveBounds = outBounds.IsValid();
  }

  t850::AABB skeletonBounds;
  const bool haveSkeleton = GetSkinnedSkeletonWorldAABB(object, skeletonBounds);
  if (outHasSkeletonBounds) {
    *outHasSkeletonBounds = haveSkeleton;
  }
  if (outSkeletonBounds && haveSkeleton) {
    *outSkeletonBounds = skeletonBounds;
  }
  if (haveSkeleton) {
    ExpandEditorAABB(outBounds, skeletonBounds);
    haveBounds = outBounds.IsValid();
  }

  return haveBounds;
}

static std::string ToLowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

static std::string NormalizeEditorResourcePath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  const std::string lower = ToLowerCopy(path);
  const std::string assetsMarker = "/assets/";
  const std::size_t embeddedAssets = lower.rfind(assetsMarker);
  if (embeddedAssets != std::string::npos) {
    path.erase(0, embeddedAssets + 1);
  }
  while (!path.empty() && path.front() == '/') {
    path.erase(path.begin());
  }
  const std::string assetsPrefix = "assets/";
  if (ToLowerCopy(path).rfind(assetsPrefix, 0) == 0) {
    path.erase(0, assetsPrefix.size());
  }
  return path;
}

static std::string MeshEditorProfileModelKey(const std::string& path) {
  std::string key = NormalizeEditorResourcePath(path);
  const std::size_t slash = key.find_last_of('/');
  if (slash != std::string::npos) {
    key = key.substr(slash + 1);
  }
  return ToLowerCopy(key);
}

static bool EditorResourcePathEquals(const std::string& lhs, const std::string& rhs) {
  return ToLowerCopy(NormalizeEditorResourcePath(lhs)) ==
         ToLowerCopy(NormalizeEditorResourcePath(rhs));
}

static const t850::SelectorDesc* FindEditorSelectorDesc(const std::vector<t850::SelectorDesc>& selectors,
                                                        const std::string& name) {
  for (const auto& selector : selectors) {
    if (selector.name == name) {
      return &selector;
    }
  }
  return nullptr;
}

static std::string EditorCubemapPathForSelectorIndex(const t850::SelectorDesc& selector, int selectedIndex) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(selector.options.size())) {
    return {};
  }
  return "sky/" + selector.options[static_cast<std::size_t>(selectedIndex)];
}

static int EditorCubemapSelectorIndexForPath(const t850::SelectorDesc& selector,
                                             const std::string& resourcePath) {
  for (int index = 0; index < static_cast<int>(selector.options.size()); ++index) {
    if (EditorResourcePathEquals(EditorCubemapPathForSelectorIndex(selector, index), resourcePath)) {
      return index;
    }
  }
  return -1;
}

static int EditorCubemapSelectorIndexFromProfile(const t850::SandboxProfileDesc& profile) {
  for (const t850::IntOverrideDesc& selector : profile.selectors) {
    if (selector.name == "cubemap") {
      return selector.value;
    }
  }
  return -1;
}

static XVECTOR3 EditorVec3FromArray(const std::array<float, 3>& value, float w = 0.0f) {
  return XVECTOR3(value[0], value[1], value[2], w);
}

static std::string FileStemFromResourcePath(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
  const std::size_t dot = path.find_last_of('.');
  const std::size_t end = (dot == std::string::npos || dot < begin) ? path.size() : dot;
  return path.substr(begin, end - begin);
}

static std::string InferQ3CollisionFromScenePath(const std::string& scenePath) {
  std::string normalized = NormalizeEditorResourcePath(scenePath);
  const std::string lower = ToLowerCopy(normalized);
  const std::string q3Marker = "scenes/q3/";
  const std::string sceneExt = ".t8scene";
  const std::size_t q3Offset = lower.rfind(q3Marker);
  if (q3Offset == std::string::npos ||
      lower.size() < sceneExt.size() ||
      lower.substr(lower.size() - sceneExt.size()) != sceneExt) {
    return {};
  }

  std::string resourcePath = normalized.substr(q3Offset);
  resourcePath.resize(resourcePath.size() - sceneExt.size());
  resourcePath += ".t8q3clip";
  return resourcePath;
}

static std::string InferQ3CollisionFromMeshPath(const std::string& meshPath) {
  std::string normalized = NormalizeEditorResourcePath(meshPath);
  const std::string lower = ToLowerCopy(normalized);
  const std::string q3Marker = "models/q3/";
  const std::size_t q3Offset = lower.rfind(q3Marker);
  if (q3Offset == std::string::npos) {
    return {};
  }

  const std::string mapName = FileStemFromResourcePath(normalized.substr(q3Offset + q3Marker.size()));
  if (mapName.empty()) {
    return {};
  }
  return "Scenes/Q3/" + mapName + ".t8q3clip";
}

static std::string ResolveSceneCollisionPath(const SceneFile& scene, const std::string& scenePath) {
  std::string collisionPath = NormalizeEditorResourcePath(scene.collision);
  if (!collisionPath.empty()) {
    return collisionPath;
  }

  collisionPath = InferQ3CollisionFromScenePath(scenePath);
  if (!collisionPath.empty() && t850::ResourceLocator::Instance().Exists(collisionPath)) {
    return collisionPath;
  }

  for (const SceneObjectDesc& object : scene.objects) {
    collisionPath = InferQ3CollisionFromMeshPath(object.mesh.empty() ? object.name : object.mesh);
    if (!collisionPath.empty() && t850::ResourceLocator::Instance().Exists(collisionPath)) {
      return collisionPath;
    }
  }
  return {};
}

static void LoadSceneCollisionClip(const std::string& collisionPath) {
  g_q3CollisionWorld.reset();
  if (collisionPath.empty()) {
    return;
  }

  if (!t850::ResourceLocator::Instance().Exists(collisionPath)) {
    T8_LOG_INFO("[T8ditor] Scene collision clip not found: %s", collisionPath.c_str());
    return;
  }

  auto q3CollisionWorld = std::make_unique<t850::Q3BspCollisionWorld>();
  std::string error;
  if (!q3CollisionWorld->Load(collisionPath, &error)) {
    T8_LOG_ERROR("[T8ditor] Failed to load scene collision clip '%s': %s", collisionPath.c_str(), error.c_str());
    return;
  }

  T8_LOG_INFO("[T8ditor] Scene collision clip loaded: %s brushes=%zu jumpPads=%zu",
              collisionPath.c_str(),
              q3CollisionWorld->GetBrushCount(),
              q3CollisionWorld->GetJumpPadCount());
  g_q3CollisionWorld = std::move(q3CollisionWorld);
}

static float EstimateRagdollRadius(const SceneObject& obj) {
  t850::AABB bounds = obj.wireframe.WorldAABB();
  if (!bounds.IsValid()) return 1.0f;
  XVECTOR3 ext = bounds.Extents();
  const float radius = std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
  return (std::max)(radius, 0.01f);
}

static t850::PhysicsRagdollBuildSettings BuildEditorRagdollSettings(const SceneObject& obj) {
  const float modelRadius = EstimateRagdollRadius(obj);
  t850::PhysicsRagdollBuildSettings settings;
  settings.fitToSkinnedGeometry = false;
  settings.preferHumanoidBones = false;
  settings.forceCapsuleForEveryBone = true;
  settings.minBoneLength = (std::max)(0.0002f, modelRadius * 0.0002f);
  settings.syntheticBoneLength = (std::max)(0.001f, modelRadius * 0.001f);
  settings.minRadius = (std::max)(0.0006f, modelRadius * 0.0008f);
  settings.maxRadius = (std::max)(0.02f, modelRadius * 0.035f);
  settings.radiusScale = 0.12f;
  settings.minSkinWeight = 0.08f;
  settings.radiusPercentile = 0.86f;
  settings.jointTrimFraction = 0.0f;
  return settings;
}

static const char* RagdollShapeTypeName(t850::PhysicsShapeType type) {
  return t850::ragdoll_editor::ShapeTypeName(type);
}

static const char* RagdollJointTypeName(t850::PhysicsRagdollJointType type) {
  return t850::ragdoll_editor::JointTypeName(type);
}

static void EnsureEditorRagdollState(t850::PhysicsRagdollAuthoringDesc& authoring) {
  t850::ragdoll_editor::RagdollEditorTool tool(authoring);
  tool.EnsureState();
}

static void RagdollMatrixToComponents(const XMATRIX44& matrix, float translation[3], float rotationDeg[3], float scale[3]) {
  XMATRIX44 copy = matrix;
  ImGuizmo::DecomposeMatrixToComponents(&copy.m[0][0], translation, rotationDeg, scale);
}

static XMATRIX44 RagdollMatrixFromComponents(const float translation[3], const float rotationDeg[3], const float scale[3]) {
  XMATRIX44 matrix;
  ImGuizmo::RecomposeMatrixFromComponents(translation, rotationDeg, scale, &matrix.m[0][0]);
  return matrix;
}

static void SyncRagdollMetaFromObject(SceneObject& obj) {
  t850::scene::SceneObjectRagdollDesc& meta = EnsureRagdollMeta(obj);
  meta.asset = obj.ragdollResourcePath;
  meta.preview = obj.ragdollPreviewEnabled || obj.ragdollDebugDraw;
  meta.drive_from_animation = true;
  meta.runtime_motion = meta.preview ? "kinematic" : "disabled";
}

static float RagdollDot3(const XVECTOR3& a, const XVECTOR3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float RagdollLength3(const XVECTOR3& value) {
  return std::sqrt(RagdollDot3(value, value));
}

static XVECTOR3 RagdollCross3(const XVECTOR3& a, const XVECTOR3& b) {
  return XVECTOR3(
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
      0.0f);
}

static XVECTOR3 RagdollNormalize3(const XVECTOR3& value, const XVECTOR3& fallback) {
  const float lenSq = RagdollDot3(value, value);
  if (lenSq <= 0.00000001f) {
    return fallback;
  }
  const float invLen = 1.0f / std::sqrt(lenSq);
  return XVECTOR3(value.x * invLen, value.y * invLen, value.z * invLen, 0.0f);
}

static XVECTOR3 RagdollMatrixAxis(const XMATRIX44& matrix, int axis) {
  if (axis == 0) return RagdollNormalize3(XVECTOR3(matrix.m11, matrix.m12, matrix.m13, 0.0f), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  if (axis == 1) return RagdollNormalize3(XVECTOR3(matrix.m21, matrix.m22, matrix.m23, 0.0f), XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  return RagdollNormalize3(XVECTOR3(matrix.m31, matrix.m32, matrix.m33, 0.0f), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
}

static XVECTOR3 RagdollMatrixTranslation(const XMATRIX44& matrix) {
  return XVECTOR3(matrix.m41, matrix.m42, matrix.m43, 1.0f);
}

static bool RagdollRayPlaneIntersection(const t850::Ray& ray,
                                        const XVECTOR3& planePoint,
                                        const XVECTOR3& planeNormal,
                                        XVECTOR3& outPoint) {
  const float denom = RagdollDot3(ray.direction, planeNormal);
  if (std::fabs(denom) <= 0.000001f) {
    return false;
  }
  const float t = RagdollDot3(planePoint - ray.origin, planeNormal) / denom;
  if (t < 0.0f) {
    return false;
  }
  outPoint = ray.origin + ray.direction * t;
  outPoint.w = 1.0f;
  return true;
}

static bool RagdollClosestRayAxisParameter(const t850::Ray& ray,
                                           const XVECTOR3& axisOrigin,
                                           const XVECTOR3& axisDirection,
                                           float& outParameter) {
  const XVECTOR3 axis = RagdollNormalize3(axisDirection, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  const XVECTOR3 rayDir = RagdollNormalize3(ray.direction, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
  const XVECTOR3 w0 = ray.origin - axisOrigin;
  const float b = RagdollDot3(rayDir, axis);
  const float d = RagdollDot3(rayDir, w0);
  const float e = RagdollDot3(axis, w0);
  const float denom = 1.0f - b * b;
  if (std::fabs(denom) <= 0.000001f) {
    outParameter = e;
    return true;
  }
  outParameter = (b * d - e) / denom;
  return true;
}

static float RagdollDistancePointToSegmentSq(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
  const float abx = b.x - a.x;
  const float aby = b.y - a.y;
  const float apx = p.x - a.x;
  const float apy = p.y - a.y;
  const float abLenSq = abx * abx + aby * aby;
  float t = abLenSq > 0.000001f ? (apx * abx + apy * aby) / abLenSq : 0.0f;
  t = std::clamp(t, 0.0f, 1.0f);
  const float cx = a.x + abx * t;
  const float cy = a.y + aby * t;
  const float dx = p.x - cx;
  const float dy = p.y - cy;
  return dx * dx + dy * dy;
}

static ImU32 RagdollAxisColor(int axis, bool active) {
  if (axis == 0) return active ? IM_COL32(255, 96, 96, 255) : IM_COL32(220, 70, 70, 240);
  if (axis == 1) return active ? IM_COL32(96, 255, 96, 255) : IM_COL32(80, 210, 80, 240);
  return active ? IM_COL32(96, 160, 255, 255) : IM_COL32(80, 130, 230, 240);
}

constexpr float kRagdollEditorMinShapeExtent = 0.001f;

static float RagdollAxisCoord(const XVECTOR3& value, int axis) {
  if (axis == 0) return value.x;
  if (axis == 1) return value.y;
  return value.z;
}

static void RagdollSetAxisCoord(XVECTOR3& value, int axis, float coord) {
  if (axis == 0) value.x = coord;
  else if (axis == 1) value.y = coord;
  else value.z = coord;
}

static XVECTOR3 RagdollClampBoxHalfExtents(const XVECTOR3& halfExtents) {
  return XVECTOR3(
      (std::max)(kRagdollEditorMinShapeExtent, halfExtents.x),
      (std::max)(kRagdollEditorMinShapeExtent, halfExtents.y),
      (std::max)(kRagdollEditorMinShapeExtent, halfExtents.z),
      0.0f);
}

static XVECTOR3 RagdollTransformVectorNoTranslation(const XVECTOR3& vector, const XMATRIX44& matrix) {
  return XVECTOR3(
      vector.x * matrix.m11 + vector.y * matrix.m21 + vector.z * matrix.m31,
      vector.x * matrix.m12 + vector.y * matrix.m22 + vector.z * matrix.m32,
      vector.x * matrix.m13 + vector.y * matrix.m23 + vector.z * matrix.m33,
      0.0f);
}

static XVECTOR3 RagdollRotateVectorAroundAxis(const XVECTOR3& vector, const XVECTOR3& axisWorld, float angleRadians) {
  XMATRIX44 rotation;
  XMatRotationAxis(rotation, RagdollNormalize3(axisWorld, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)), angleRadians);
  return RagdollNormalize3(RagdollTransformVectorNoTranslation(vector, rotation), vector);
}

static bool RagdollIsValidAxis(const XVECTOR3& axis) {
  return std::isfinite(axis.x) &&
         std::isfinite(axis.y) &&
         std::isfinite(axis.z) &&
         RagdollLength3(axis) > 0.000001f;
}

static void RagdollNormalizeJointFrameAxes(XVECTOR3& twist,
                                           XVECTOR3& plane,
                                           const XVECTOR3& fallbackTwist,
                                           const XVECTOR3& fallbackPlane) {
  twist = RagdollNormalize3(RagdollIsValidAxis(twist) ? twist : fallbackTwist, fallbackTwist);
  plane = RagdollNormalize3(RagdollIsValidAxis(plane) ? plane : fallbackPlane, fallbackPlane);
  const float projection = RagdollDot3(plane, twist);
  plane = RagdollNormalize3(plane - twist * projection, fallbackPlane);
  if (std::fabs(RagdollDot3(plane, twist)) > 0.95f) {
    const XVECTOR3 candidate = std::fabs(twist.y) < 0.9f
        ? XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)
        : XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    plane = RagdollNormalize3(candidate - twist * RagdollDot3(candidate, twist), fallbackPlane);
  }
}

static void* NativeHandleFromImGuiViewport(ImGuiViewport* viewport) {
  if (!viewport) {
    return nullptr;
  }
  if (viewport->PlatformHandleRaw) {
    return viewport->PlatformHandleRaw;
  }
#ifdef OS_WINDOWS
  if (viewport->PlatformHandle) {
    SDL_Window* sdlWindow = static_cast<SDL_Window*>(viewport->PlatformHandle);
    return SDL_GetPointerProperty(
        SDL_GetWindowProperties(sdlWindow),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr);
  }
#endif
  return viewport->PlatformHandle;
}

static bool ShouldDrawPhysicsDebug() {
  for (const SceneObject& obj : g_objects) {
    if (obj.ragdollDebugDraw && obj.litInst.HasPhysicsRagdoll())
      return true;
  }
  return false;
}

void EditorApp::SyncSceneObjectTransforms() {
  for (SceneObject& obj : g_objects) {
    if (obj.primId < 0) continue;
    const XVECTOR3& pos = obj.wireframe.Position();
    const XVECTOR3& eul = obj.wireframe.EulerRadians();
    const XVECTOR3& scl = obj.wireframe.Scale();
    obj.litInst.TranslateAbsolute(pos.x, pos.y, pos.z);
    obj.litInst.RotateXAbsolute(eul.x * kRadToDeg);
    obj.litInst.RotateYAbsolute(eul.y * kRadToDeg);
    obj.litInst.RotateZAbsolute(eul.z * kRadToDeg);
    obj.litInst.ScaleAbsolute(scl.x, scl.y, scl.z);
    obj.litInst.Visible = obj.visible;
    obj.litInst.Update();
  }
}

void EditorApp::DestroyObjectRagdoll(SceneObject& obj) {
  if (obj.litInst.HasPhysicsRagdoll() && m_physics.IsInitialized()) {
    m_physics.DestroyRagdoll(obj.litInst.GetPhysicsRagdoll());
  }
  obj.litInst.ClearPhysicsLinks();
  obj.ragdollPreviewEnabled = false;
  obj.ragdollDriveFromAnimation = false;
  obj.ragdollSimulating = false;
  obj.ragdollPhysicsStates.clear();
  obj.ragdollPhysicsBoneIndices.clear();
  obj.ragdollPhysicsCombinedMatrices.clear();
  if (t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj)) {
    skinned->PlayAnimation();
  }
}

void EditorApp::DestroyAllObjectRagdolls() {
  for (SceneObject& obj : g_objects)
    DestroyObjectRagdoll(obj);
}

bool EditorApp::EnsureObjectRagdollAuthoring(SceneObject& obj) {
  if (obj.ragdollAuthoringReady)
    return true;

  obj.ragdollAuthoringTried = true;
  t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
  if (!skinned || !skinned->HasSkinData()) {
    obj.ragdollStatus = "Selected mesh has no skin/skeleton data.";
    return false;
  }

  SyncSceneObjectTransforms();
  skinned->UpdateAnimationPose();

  const std::string modelPath = obj.meshPath.empty() ? obj.name : obj.meshPath;
  obj.ragdollModelKey = t850::BuildRagdollEditModelKey(modelPath);
  if (obj.ragdollResourcePath.empty())
    obj.ragdollResourcePath = t850::BuildRagdollEditResourcePath(modelPath);

  t850::PhysicsRagdollAuthoringDesc generatedAuthoring;
  const t850::PhysicsRagdollBuildSettings settings = BuildEditorRagdollSettings(obj);
  if (!t850::BuildRagdollAuthoringFromSkeleton(
          *skinned,
          obj.litInst.Final,
          obj.litInst.GetEntityId(),
          settings,
          generatedAuthoring)) {
    obj.ragdollStatus = "Failed to generate a skeleton ragdoll.";
    T8_LOG_ERROR("[T8ditor] Failed to generate ragdoll authoring for '%s'", modelPath.c_str());
    return false;
  }

  obj.ragdollAuthoring = generatedAuthoring;
  obj.ragdollLoadedFromAsset = false;
  int loadedBodyCount = 0;
  if (!obj.ragdollResourcePath.empty() &&
      t850::LoadRagdollAuthoringAsset(
          obj.ragdollResourcePath,
          *skinned,
          obj.litInst.Final,
          generatedAuthoring.binding,
          obj.ragdollAuthoring,
          &loadedBodyCount)) {
    obj.ragdollLoadedFromAsset = true;
    obj.ragdollBodyCount = loadedBodyCount;
    obj.ragdollStatus = "Loaded authored ragdoll asset.";
  } else {
    obj.ragdollBodyCount = (int)obj.ragdollAuthoring.binding.referencePose.bones.size();
    obj.ragdollStatus = "Using generated skeleton ragdoll.";
  }

  obj.ragdollAuthoringReady = !obj.ragdollAuthoring.binding.referencePose.bones.empty();
  if (!obj.ragdollAuthoringReady) {
    obj.ragdollStatus = "Ragdoll authoring produced no bodies.";
    return false;
  }

  T8_LOG_INFO("[T8ditor] Ragdoll authoring ready for '%s': bodies=%d, asset='%s'",
              modelPath.c_str(), obj.ragdollBodyCount, obj.ragdollResourcePath.c_str());
  return true;
}

bool EditorApp::LoadObjectRagdollAuthoringFromFile(SceneObject& obj) {
  obj.ragdollAuthoringTried = true;
  t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
  if (!skinned || !skinned->HasSkinData()) {
    obj.ragdollStatus = "Selected mesh has no skin/skeleton data.";
    return false;
  }
  if (obj.ragdollResourcePath.empty()) {
    obj.ragdollStatus = "Choose a ragdoll file first.";
    return false;
  }

  SyncSceneObjectTransforms();
  skinned->UpdateAnimationPose();

  const std::string modelPath = obj.meshPath.empty() ? obj.name : obj.meshPath;
  obj.ragdollModelKey = t850::BuildRagdollEditModelKey(modelPath);

  t850::PhysicsRagdollAuthoringDesc generatedAuthoring;
  const t850::PhysicsRagdollBuildSettings settings = BuildEditorRagdollSettings(obj);
  if (!t850::BuildRagdollAuthoringFromSkeleton(
          *skinned,
          obj.litInst.Final,
          obj.litInst.GetEntityId(),
          settings,
          generatedAuthoring)) {
    obj.ragdollStatus = "Failed to build a generated binding for the selected skeleton.";
    return false;
  }

  t850::PhysicsRagdollAuthoringDesc loadedAuthoring;
  int loadedBodyCount = 0;
  if (!t850::LoadRagdollAuthoringAsset(
          obj.ragdollResourcePath,
          *skinned,
          obj.litInst.Final,
          generatedAuthoring.binding,
          loadedAuthoring,
          &loadedBodyCount)) {
    obj.ragdollStatus = "Failed to load ragdoll file: " + obj.ragdollResourcePath;
    return false;
  }

  EnsureEditorRagdollState(loadedAuthoring);
  obj.ragdollAuthoring = std::move(loadedAuthoring);
  obj.ragdollLoadedFromAsset = true;
  obj.ragdollBodyCount = loadedBodyCount;
  obj.ragdollAuthoringReady = !obj.ragdollAuthoring.binding.referencePose.bones.empty();
  obj.ragdollStatus = obj.ragdollAuthoringReady
      ? "Loaded authored ragdoll file."
      : "Loaded ragdoll file contained no bodies.";
  return obj.ragdollAuthoringReady;
}

bool EditorApp::RecreateObjectRagdoll(SceneObject& obj, t850::PhysicsBodyMotion motion) {
  if (!m_physics.IsInitialized()) {
    obj.ragdollStatus = "Physics runtime is not initialized.";
    return false;
  }
  if (!EnsureObjectRagdollAuthoring(obj))
    return false;

  DestroyObjectRagdoll(obj);

  t850::PhysicsRagdollDesc desc = obj.ragdollAuthoring.binding.referencePose;
  desc.entityId = obj.litInst.GetEntityId();
  for (t850::PhysicsRagdollBoneDesc& bone : desc.bones)
    bone.body.entityId = obj.litInst.GetEntityId();

  t850::PhysicsRagdollHandle handle = m_physics.CreateRagdoll(desc, motion);
  if (!handle.IsValid()) {
    obj.ragdollStatus = "Failed to create runtime ragdoll.";
    T8_LOG_ERROR("[T8ditor] Failed to create runtime ragdoll for '%s'", obj.name.c_str());
    return false;
  }

  obj.litInst.AttachPhysicsRagdoll(handle);
  obj.ragdollPreviewEnabled = true;
  obj.ragdollDriveFromAnimation = (motion != t850::PhysicsBodyMotion::Dynamic);
  obj.ragdollSimulating = (motion == t850::PhysicsBodyMotion::Dynamic);
  obj.ragdollStatus = obj.ragdollSimulating ? "Dynamic ragdoll simulation active."
                                            : "Kinematic ragdoll preview active.";
  return true;
}

bool EditorApp::ResetObjectRagdollToAnimation(SceneObject& obj) {
  if (!obj.litInst.HasPhysicsRagdoll()) {
    if (!RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic))
      return false;
  } else if (!m_physics.SetRagdollMotion(obj.litInst.GetPhysicsRagdoll(), t850::PhysicsBodyMotion::Kinematic)) {
    obj.ragdollStatus = "Failed to switch ragdoll to kinematic mode.";
    return false;
  }

  t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
  if (skinned) {
    skinned->PlayAnimation();
    skinned->ClearSnapshotBoneMatrices();
  }
  obj.ragdollDriveFromAnimation = true;
  obj.ragdollSimulating = false;
  obj.ragdollPreviewEnabled = true;
  obj.ragdollStatus = "Ragdoll reset to animation drive.";
  return true;
}

bool EditorApp::StartObjectRagdollSimulation(SceneObject& obj) {
  if (!RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic))
    return false;

  t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
  if (!skinned || !skinned->HasSkinData()) {
    obj.ragdollStatus = "Cannot simulate: selected mesh has no skin data.";
    return false;
  }

  t850::PhysicsRagdollDesc pose;
  if (!t850::BuildRagdollPoseFromAnimation(*skinned, obj.litInst.Final, obj.ragdollAuthoring.binding, pose) ||
      !m_physics.DriveRagdollFromPose(obj.litInst.GetPhysicsRagdoll(), pose, 0.0f)) {
    obj.ragdollStatus = "Failed to align ragdoll to current animation pose.";
    return false;
  }

  if (!m_physics.SetRagdollMotion(obj.litInst.GetPhysicsRagdoll(), t850::PhysicsBodyMotion::Dynamic)) {
    obj.ragdollStatus = "Failed to switch ragdoll to dynamic mode.";
    return false;
  }
  m_physics.SetRagdollVelocity(
      obj.litInst.GetPhysicsRagdoll(),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f),
      XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));

  skinned->PauseAnimation();
  skinned->ClearSnapshotBoneMatrices();
  obj.ragdollDriveFromAnimation = false;
  obj.ragdollSimulating = true;
  obj.ragdollPreviewEnabled = true;
  obj.ragdollStatus = "Dynamic ragdoll simulation active.";
  return true;
}

void EditorApp::UpdateSkinnedAnimationAndRagdolls() {
  SyncSceneObjectTransforms();

  for (int objectIndex = 0; objectIndex < static_cast<int>(g_objects.size()); ++objectIndex) {
    if (m_meshEditorOpen && objectIndex == m_meshEditorObjectIndex) {
      continue;
    }
    SceneObject& obj = g_objects[objectIndex];
    if (obj.primId < 0 || !obj.visible) continue;
    t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
    if (!skinned || !skinned->HasSkinData()) continue;

    if (!obj.ragdollSimulating)
      skinned->UpdateAnimationPose();

    if (obj.ragdollDriveFromAnimation && obj.litInst.HasPhysicsRagdoll() && obj.ragdollAuthoringReady) {
      t850::PhysicsRagdollDesc pose;
      if (t850::BuildRagdollPoseFromAnimation(*skinned, obj.litInst.Final, obj.ragdollAuthoring.binding, pose)) {
        m_physics.DriveRagdollFromPose(obj.litInst.GetPhysicsRagdoll(), pose, m_dtSecs);
      }
    }
  }

  if (m_physics.IsInitialized())
    m_physics.Update(m_dtSecs);

  for (int objectIndex = 0; objectIndex < static_cast<int>(g_objects.size()); ++objectIndex) {
    if (m_meshEditorOpen && objectIndex == m_meshEditorObjectIndex) {
      continue;
    }
    SceneObject& obj = g_objects[objectIndex];
    if (!obj.ragdollSimulating || !obj.litInst.HasPhysicsRagdoll() || !obj.ragdollAuthoringReady)
      continue;

    t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
    if (!skinned || !skinned->HasSkinData()) continue;

    if (m_physics.GetRagdollState(obj.litInst.GetPhysicsRagdoll(), obj.ragdollPhysicsStates) &&
        t850::BuildSkeletonPoseFromRagdollState(
            *skinned,
            obj.litInst.Final,
            obj.ragdollAuthoring.binding,
            obj.ragdollPhysicsStates,
            obj.ragdollPhysicsBoneIndices,
            obj.ragdollPhysicsCombinedMatrices) &&
        skinned->GetAnimController().ApplyCombinedPoseOverrides(
            obj.ragdollPhysicsBoneIndices,
            obj.ragdollPhysicsCombinedMatrices)) {
      obj.ragdollStatus = "Skeleton driven by dynamic ragdoll.";
    } else {
      obj.ragdollStatus = "Failed to apply physics pose to skeleton.";
    }
  }
}

void EditorApp::UploadSkinnedBoneTextures() {
  for (SceneObject& obj : g_objects) {
    if (obj.primId < 0 || !obj.visible) continue;
    t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
    if (skinned && skinned->HasSkinData())
      skinned->UploadBoneTexture();
  }
}

void EditorApp::DrawRagdollInspector(SceneObject& obj) {
  t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);
  if (!skinned || !skinned->HasSkinData())
    return;

  ImGui::SeparatorText("Ragdoll Authoring");
  if (obj.ragdollResourcePath.empty()) {
    const std::string modelPath = obj.meshPath.empty() ? obj.name : obj.meshPath;
    obj.ragdollResourcePath = t850::BuildRagdollEditResourcePath(modelPath);
  }
  t850::scene::SceneObjectRagdollDesc& meta = EnsureRagdollMeta(obj);
  meta.asset = obj.ragdollResourcePath;

  if (ImGui::Checkbox("Export With Ragdoll", &meta.enabled)) {
    SyncRagdollMetaFromObject(obj);
    meta.enabled = obj.ragdollAuthoringMeta ? obj.ragdollAuthoringMeta->enabled : meta.enabled;
  }
  ImGui::TextDisabled("When enabled, this scene object will save a reference to the ragdoll asset.");

  if (InputTextString("Ragdoll File", meta.asset)) {
    obj.ragdollResourcePath = meta.asset;
    DestroyObjectRagdoll(obj);
    obj.ragdollAuthoringReady = false;
    obj.ragdollAuthoringTried = false;
    obj.ragdollLoadedFromAsset = false;
    obj.ragdollBodyCount = 0;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Browse")) {
    const std::string path = OpenFileDialog(
        L"Ragdoll JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0",
        L"Load Ragdoll");
    if (!path.empty()) {
      meta.asset = path;
      obj.ragdollResourcePath = path;
      DestroyObjectRagdoll(obj);
      obj.ragdollAuthoringReady = false;
      obj.ragdollAuthoringTried = false;
      obj.ragdollLoadedFromAsset = false;
      obj.ragdollBodyCount = 0;
    }
  }

  if (ImGui::Button("Load Ragdoll From File")) {
    DestroyObjectRagdoll(obj);
    obj.ragdollAuthoringReady = false;
    obj.ragdollAuthoringTried = false;
    if (LoadObjectRagdollAuthoringFromFile(obj)) {
      if (meta.preview) {
        RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic);
        obj.ragdollDebugDraw = true;
      }
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Edit Ragdoll")) {
    for (int i = 0; i < (int)g_objects.size(); ++i) {
      if (&g_objects[i] == &obj) {
        OpenRagdollEditor(i);
        break;
      }
    }
  }

  bool showPhysicsObjects = obj.ragdollDebugDraw && obj.litInst.HasPhysicsRagdoll();
  if (ImGui::Checkbox("Show Capsules / Physics Objects", &showPhysicsObjects)) {
    if (showPhysicsObjects) {
      if (!obj.ragdollAuthoringReady && !LoadObjectRagdollAuthoringFromFile(obj)) {
        EnsureObjectRagdollAuthoring(obj);
      }
      if (obj.ragdollAuthoringReady && RecreateObjectRagdoll(obj, t850::PhysicsBodyMotion::Kinematic)) {
        obj.ragdollDebugDraw = true;
      }
    } else {
      obj.ragdollDebugDraw = false;
      DestroyObjectRagdoll(obj);
    }
    meta.preview = showPhysicsObjects;
    meta.runtime_motion = showPhysicsObjects ? "kinematic" : "disabled";
  }

  ImGui::Text("Bodies: %d", obj.ragdollBodyCount);
  ImGui::Text("Source: %s", obj.ragdollLoadedFromAsset ? "Authored file" : (obj.ragdollAuthoringReady ? "Generated preview" : "Not loaded"));
  if (!obj.ragdollStatus.empty())
    ImGui::TextWrapped("%s", obj.ragdollStatus.c_str());
}

void EditorApp::OpenMeshEditor(int objectIndex) {
  if (objectIndex < 0 || objectIndex >= (int)g_objects.size()) {
    return;
  }

  SceneObject& obj = g_objects[objectIndex];
  if (m_meshEditorScene && m_meshEditorSceneLoaded) {
    m_meshEditorScene->OnDestoryScene();
  }
  m_meshEditorScene.reset();
  m_meshEditorSceneLoaded = false;
  m_meshEditorObjectIndex = objectIndex;
  m_meshEditorNativeHandle = nullptr;
  m_meshEditorLoggedNativeHandle = nullptr;
  m_meshEditorMainViewportLogged = false;
  m_meshEditorImGuiViewportId = 0;
  m_meshEditorViewportPosX = 0.0f;
  m_meshEditorViewportPosY = 0.0f;
  m_meshEditorViewportSizeX = 0.0f;
  m_meshEditorViewportSizeY = 0.0f;
  m_meshEditorViewportImageMinX = 0.0f;
  m_meshEditorViewportImageMinY = 0.0f;
  m_meshEditorViewportImageSizeX = 0.0f;
  m_meshEditorViewportImageSizeY = 0.0f;
  m_meshEditorDockspaceId = 0;
  m_meshEditorDockClassId = 0;
  m_meshEditorOpen = true;
  m_meshEditorOpenRequested = true;
  m_meshEditorCloseRequested = false;
  InvalidateEditorFrozenFrame();
  m_meshEditorGuiVisible = true;
  m_meshEditorViewportInputActive = false;

  g_selectedIdx = objectIndex;
  g_selectionType = 0;
  g_multiSelect.clear();
  g_multiSelect.insert(objectIndex);

  t850::AABB bounds;
  if (GetEditorObjectWorldAABB(obj, bounds) && bounds.IsValid()) {
    m_meshEditorOrbitTarget = bounds.Center();
    const XVECTOR3 ext = bounds.Extents();
    const float radius = (std::max)(0.25f, std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z));
    m_meshEditorOrbitDistance = radius * 2.8f;
  } else {
    m_meshEditorOrbitTarget = obj.wireframe.Position();
    m_meshEditorOrbitDistance = 4.0f;
  }
  m_meshEditorOrbitYaw = -0.75f;
  m_meshEditorOrbitPitch = 0.35f;
  m_meshEditorFovDeg = 45.0f;
  m_meshEditorCameraInitialized = true;
  m_meshEditorSceneReady = false;
  m_meshEditorDebugLogFramesRemaining = 8;

  T8_LOG_INFO("[T8ditor] Requested native editor window title='Mesh Edit' object='%s'", obj.name.c_str());
}

void EditorApp::CloseMeshEditor() {
  if (pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->WaitForGPU();
  }
  if (m_meshEditorScene && m_meshEditorSceneLoaded) {
    m_meshEditorScene->OnDestoryScene();
  }
  if (m_imguiReady) {
    ImGuiLogCaptureStart();
  }
  m_meshEditorScene.reset();
  m_meshEditorSceneLoaded = false;
  m_meshEditorOpen = false;
  m_meshEditorOpenRequested = false;
  m_meshEditorCloseRequested = false;
  m_meshEditorObjectIndex = -1;
  m_meshEditorNativeHandle = nullptr;
  m_meshEditorLoggedNativeHandle = nullptr;
  m_meshEditorMainViewportLogged = false;
  m_meshEditorImGuiViewportId = 0;
  m_meshEditorViewportPosX = 0.0f;
  m_meshEditorViewportPosY = 0.0f;
  m_meshEditorViewportSizeX = 0.0f;
  m_meshEditorViewportSizeY = 0.0f;
  m_meshEditorViewportImageMinX = 0.0f;
  m_meshEditorViewportImageMinY = 0.0f;
  m_meshEditorViewportImageSizeX = 0.0f;
  m_meshEditorViewportImageSizeY = 0.0f;
  m_meshEditorDockspaceId = 0;
  m_meshEditorDockClassId = 0;
  m_meshEditorViewportInputActive = false;
  DestroyMeshEditorViewportTarget();
  IManager.xDelta = 0;
  IManager.yDelta = 0;
  for (int i = 0; i < MAXMOUSEBUTTONS; ++i) {
    IManager.MouseButtonStates[0][i] = false;
    IManager.MouseButtonStates[1][i] = false;
  }
  if (pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
    pFramework->pVideoDriver->SetBlendState(t850::BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetCullFace(t850::BaseDriver::FRONT_FACES);
    int mainW = m_lastW;
    int mainH = m_lastH;
#ifdef OS_WINDOWS
    if (auto* w32 = static_cast<t850::Win32Framework*>(pFramework)) {
      if (w32->m_pWindow) {
        SDL_GetWindowSizeInPixels(w32->m_pWindow, &mainW, &mainH);
      }
    }
#endif
    if (mainW > 0 && mainH > 0) {
      m_lastW = mainW;
      m_lastH = mainH;
      m_camera.SetViewportSize(mainW, mainH);
      pFramework->pVideoDriver->SetViewport(0.0f, 0.0f, static_cast<float>(mainW), static_cast<float>(mainH));
      pFramework->pVideoDriver->SetScissorRect(0, 0, mainW, mainH);
    }
  }
  if (!m_sceneProps.pCameras.empty()) {
    m_sceneProps.SetPrimaryCamera(&m_camera.GetCameraMutable());
  }
}

void EditorApp::InvalidateEditorFrozenFrame() {
  m_editorFrozenFrameValid = false;
}

bool EditorApp::EnsureEditorFrozenFrameTarget(int width, int height) {
  if (!pFramework || !pFramework->pVideoDriver || width <= 0 || height <= 0) {
    return false;
  }
  width = (std::max)(1, width);
  height = (std::max)(1, height);
  if (m_editorFrozenFrameRT >= 0 &&
      m_editorFrozenFrameW == width &&
      m_editorFrozenFrameH == height) {
    return true;
  }

  DestroyEditorFrozenFrameTarget();
  m_editorFrozenFrameRT = pFramework->pVideoDriver->CreateRT(
      1,
      t850::BaseRT::RGBA8,
      t850::BaseRT::F32,
      width,
      height);
  if (m_editorFrozenFrameRT < 0) {
    T8_LOG_ERROR("[T8ditor] Failed to create frozen editor frame RT %dx%d", width, height);
    return false;
  }
  m_editorFrozenFrameW = width;
  m_editorFrozenFrameH = height;
  m_editorFrozenFrameValid = false;
  return true;
}

void EditorApp::DestroyEditorFrozenFrameTarget() {
  if (pFramework && pFramework->pVideoDriver && m_editorFrozenFrameRT >= 0) {
    pFramework->pVideoDriver->DestroyRT(m_editorFrozenFrameRT);
  }
  m_editorFrozenFrameRT = -1;
  m_editorFrozenFrameW = 0;
  m_editorFrozenFrameH = 0;
  m_editorFrozenFrameValid = false;
}

void EditorApp::DrawEditorFrozenFrame(t850::BaseDriver* driver) {
  if (!driver ||
      m_editorFrozenFrameRT < 0 ||
      m_editorFrozenFrameRT >= static_cast<int>(driver->RTs.size()) ||
      m_lastW <= 0 ||
      m_lastH <= 0) {
    return;
  }
  t850::BaseRT* rt = driver->RTs[m_editorFrozenFrameRT];
  if (!rt || rt->vColorTextures.empty() || !rt->vColorTextures[0]) {
    return;
  }

  driver->SetViewport(0.0f, 0.0f, static_cast<float>(m_lastW), static_cast<float>(m_lastH));
  driver->SetScissorRect(0, 0, m_lastW, m_lastH);
  driver->SetBlendState(t850::BaseDriver::BLEND_OPAQUE);
  driver->SetDepthStencilState(t850::BaseDriver::NONE);
  g_quads[7].SetTexture(rt->vColorTextures[0], 0);
  t850::ShaderKey key(0);
  key.setPass(t850::PassType::BACKBUFFER);
  g_quads[7].SetGlobalKey(key);
  g_quads[7].Draw();
  driver->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
}

SceneFile EditorApp::BuildEditorSceneSnapshot(const std::string& scenePath) {
  SceneFile sf = g_hasLoadedSceneFile ? g_loadedSceneFile : SceneFile{};
  sf.editor.camera_target   = { m_camera.GetTarget().x, m_camera.GetTarget().y, m_camera.GetTarget().z };
  sf.editor.camera_yaw      = m_camera.GetYaw();
  sf.editor.camera_pitch    = m_camera.GetPitch();
  sf.editor.camera_distance = m_camera.GetDistance();
  sf.editor.show_skybox     = m_panels.showSkybox;
  sf.editor.show_wireframe  = m_panels.showWireframe;

  sf.objects.clear();
  for (auto& obj : g_objects) {
    SceneObjectDesc od;
    od.name     = obj.name;
    od.mesh     = obj.meshPath.empty() ? obj.name : obj.meshPath;
    od.ragdoll  = obj.ragdollResourcePath;
    od.position = { obj.wireframe.Position().x, obj.wireframe.Position().y, obj.wireframe.Position().z };
    od.rotation = { obj.wireframe.EulerRadians().x * kRadToDeg,
                    obj.wireframe.EulerRadians().y * kRadToDeg,
                    obj.wireframe.EulerRadians().z * kRadToDeg };
    od.scale    = { obj.wireframe.Scale().x, obj.wireframe.Scale().y, obj.wireframe.Scale().z };
    od.visible   = obj.visible;
    od.mobile_visible = obj.mobileVisible;
    od.frozen    = obj.frozen;
    od.show_wire = obj.showWire;
    od.nav_agent_front_yaw_offset_deg = obj.navAgentFrontYawOffsetDeg;
    od.nav_agent_face_yaw_sign = obj.navAgentFaceYawSign;
    od.physics = obj.physics;
    od.navigation = obj.navigation;
    if (obj.ragdollAuthoringMeta) {
      od.ragdoll_authoring = obj.ragdollAuthoringMeta;
      od.ragdoll_authoring->asset = od.ragdoll_authoring->asset.empty()
          ? obj.ragdollResourcePath
          : od.ragdoll_authoring->asset;
    }
    sf.objects.push_back(od);
  }
  for (const SceneObjectDesc& od : g_unloadedSceneObjects) {
    sf.objects.push_back(od);
  }

  sf.cameras.clear();
  auto appendCamera = [&](const SceneCamera& c) {
    SceneCameraDesc cd;
    cd.name       = c.name;
    cd.type       = (int)c.type;
    cd.position   = { c.position.x, c.position.y, c.position.z };
    cd.target     = { c.target.x, c.target.y, c.target.z };
    cd.fov_deg    = c.fovDeg;
    cd.ortho_w    = c.orthoW;
    cd.ortho_h    = c.orthoH;
    cd.near_plane = c.nearPlane;
    cd.far_plane  = c.farPlane;
    cd.visible    = c.visible;
    cd.frozen     = c.frozen;
    sf.cameras.push_back(cd);
  };
  if (g_activeCameraIdx >= 0 && g_activeCameraIdx < (int)g_cameras.size()) {
    appendCamera(g_cameras[(std::size_t)g_activeCameraIdx]);
  }
  for (int cameraIndex = 0; cameraIndex < (int)g_cameras.size(); ++cameraIndex) {
    if (cameraIndex == g_activeCameraIdx) {
      continue;
    }
    appendCamera(g_cameras[(std::size_t)cameraIndex]);
  }

  sf.lights.clear();
  for (auto& l : g_lights) {
    SceneLightDesc ld;
    ld.name      = l.name;
    ld.type      = (int)l.type;
    ld.position  = { l.position.x, l.position.y, l.position.z };
    ld.direction = { l.direction.x, l.direction.y, l.direction.z };
    ld.color     = { l.color.x, l.color.y, l.color.z };
    ld.intensity = l.intensity;
    ld.radius    = l.radius;
    ld.enabled   = l.enabled;
    ld.visible   = l.visible;
    ld.frozen    = l.frozen;
    ld.q3        = l.q3;
    sf.lights.push_back(ld);
  }

  sf.collision = g_sceneCollisionResourcePath;
  sf.profiles = g_sceneProfiles;
  if (sf.collision.empty()) {
    sf.collision = ResolveSceneCollisionPath(sf, scenePath);
  }
  return sf;
}

bool EditorApp::SaveEditorSceneSnapshot(const std::string& path, bool updateLoadedScene) {
  SceneFile sf = BuildEditorSceneSnapshot(path);
  if (!SaveSceneToFile(sf, path)) {
    return false;
  }
  if (updateLoadedScene) {
    g_loadedSceneFile = sf;
    g_hasLoadedSceneFile = true;
    g_sceneCollisionResourcePath = sf.collision;
  }
  return true;
}

bool EditorApp::ExportTemporaryPlayScene(std::string& outPath) {
  std::error_code ec;
  std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec);
  if (ec) {
    tempDir = std::filesystem::current_path();
  }
  tempDir /= "T850";
  tempDir /= "T8ditorPlay";
  std::filesystem::create_directories(tempDir, ec);
  if (ec) {
    T8_LOG_ERROR("[T8ditor] Cannot create play-scene temp directory '%s': %s",
                 tempDir.string().c_str(),
                 ec.message().c_str());
    return false;
  }

  const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  std::filesystem::path tempPath = tempDir / ("play_scene_" + std::to_string(stamp) + ".t8scene");
  outPath = tempPath.string();
  m_playSceneEditorSnapshot = BuildEditorSceneSnapshot(outPath);
  m_playSceneHasVisibleObjects = std::any_of(m_playSceneEditorSnapshot.objects.begin(), m_playSceneEditorSnapshot.objects.end(), [](const SceneObjectDesc& object) {
    const std::string mesh = object.mesh.empty() ? object.name : object.mesh;
    return object.visible && !mesh.empty();
  });
  if (!SaveSceneToFile(m_playSceneEditorSnapshot, outPath)) {
    outPath.clear();
    return false;
  }
  return true;
}

void EditorApp::RestoreEditorStateAfterPlay() {
  const SceneFile& sf = m_playSceneEditorSnapshot;
  m_panels.showSkybox = sf.editor.show_skybox;
  m_panels.showWireframe = sf.editor.show_wireframe;
  m_camera.SetTarget(XVECTOR3(sf.editor.camera_target.x,
                              sf.editor.camera_target.y,
                              sf.editor.camera_target.z));
  m_camera.SetOrbitState(sf.editor.camera_yaw,
                         sf.editor.camera_pitch,
                         sf.editor.camera_distance);
  if (m_lastW > 0 && m_lastH > 0) {
    m_camera.SetViewportSize(m_lastW, m_lastH);
  }

  const std::size_t objectCount = (std::min)(g_objects.size(), sf.objects.size());
  for (std::size_t i = 0; i < objectCount; ++i) {
    SceneObject& obj = g_objects[i];
    const SceneObjectDesc& od = sf.objects[i];
    obj.wireframe.Position() = XVECTOR3(od.position.x, od.position.y, od.position.z);
    obj.wireframe.EulerRadians() = XVECTOR3(
        od.rotation.x * kDegToRad,
        od.rotation.y * kDegToRad,
        od.rotation.z * kDegToRad);
    obj.wireframe.Scale() = XVECTOR3(od.scale.x, od.scale.y, od.scale.z);
    obj.visible = od.visible;
    obj.mobileVisible = od.mobile_visible;
    obj.frozen = od.frozen;
    obj.showWire = od.show_wire;
    obj.navAgentFrontYawOffsetDeg = od.nav_agent_front_yaw_offset_deg;
    obj.navAgentFaceYawSign = od.nav_agent_face_yaw_sign;
    obj.physics = od.physics;
    obj.navigation = od.navigation;
    obj.ragdollAuthoringMeta = od.ragdoll_authoring;
  }

  g_cameras.clear();
  for (const auto& cd : sf.cameras) {
    SceneCamera c;
    c.name = cd.name;
    c.type = (CameraType)cd.type;
    c.position = XVECTOR3(cd.position.x, cd.position.y, cd.position.z);
    c.target = XVECTOR3(cd.target.x, cd.target.y, cd.target.z);
    c.fovDeg = cd.fov_deg;
    c.orthoW = cd.ortho_w;
    c.orthoH = cd.ortho_h;
    c.nearPlane = cd.near_plane;
    c.farPlane = cd.far_plane;
    c.visible = cd.visible;
    c.frozen = cd.frozen;
    g_cameras.push_back(c);
  }
  g_activeCameraIdx =
      (m_playScenePreviousActiveCameraIdx >= 0 &&
       m_playScenePreviousActiveCameraIdx < static_cast<int>(g_cameras.size()))
          ? m_playScenePreviousActiveCameraIdx
          : -1;

  g_lights.clear();
  for (const auto& ld : sf.lights) {
    SceneLight l;
    l.name = ld.name;
    l.type = (EditorLightType)ld.type;
    l.position = XVECTOR3(ld.position.x, ld.position.y, ld.position.z);
    l.direction = XVECTOR3(ld.direction.x, ld.direction.y, ld.direction.z);
    l.color = XVECTOR3(ld.color.x, ld.color.y, ld.color.z);
    l.intensity = ld.intensity;
    l.radius = ld.radius;
    l.enabled = ld.enabled;
    l.visible = ld.visible;
    l.frozen = ld.frozen;
    l.q3 = ld.q3;
    g_lights.push_back(l);
  }
  g_selectedIdx = -1;
  g_selectionType = 0;
  g_multiSelect.clear();
  SyncSceneObjectTransforms();
}

void EditorApp::OpenPlayScene() {
  if (m_meshEditorOpen) {
    CloseMeshEditor();
  }
  if (m_playSceneOpen) {
    ClosePlayScene(false);
  }

  std::string tempPath;
  if (!ExportTemporaryPlayScene(tempPath)) {
    m_playSceneStatus = "Failed to export temporary play scene.";
    T8_LOG_ERROR("[T8ditor] Play Scene failed: temporary scene export failed");
    return;
  }

  m_playScenePreviousConfig = t850::g_config;
  m_playSceneHasPreviousConfig = true;
  m_playScenePreviousActiveCameraIdx = g_activeCameraIdx;
  m_playSceneTempPath = tempPath;
  m_playSceneOpen = true;
  m_playSceneOpenRequested = true;
  m_playSceneCloseRequested = false;
  m_playSceneLoaded = false;
  m_playSceneLaunchFailed = false;
  m_playSceneGuiVisible = false;
  InvalidateEditorFrozenFrame();
  m_playSceneViewportInputActive = false;
  m_playSceneStatus = m_playSceneHasVisibleObjects
      ? "Starting Play Scene..."
      : "Temporary scene has no visible meshes. Add an object before Play.";
  T8_LOG_INFO("[T8ditor] Play Scene exported temporary scene '%s'", m_playSceneTempPath.c_str());
}

void EditorApp::ClosePlayScene(bool restoreEditorScene) {
  const std::string restorePath = m_playSceneTempPath;
  if (pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->WaitForGPU();
  }
  if (m_playScene && m_playSceneLoaded) {
    m_playScene->OnDestoryScene();
  }
  m_playScene.reset();
  m_playSceneLoaded = false;
  m_playSceneOpen = false;
  m_playSceneOpenRequested = false;
  m_playSceneCloseRequested = false;
  m_playSceneHasVisibleObjects = false;
  m_playSceneLaunchFailed = false;
  m_playSceneGuiVisible = false;
  m_playSceneViewportInputActive = false;
  m_playSceneImGuiViewportId = 0;
  m_playSceneDockspaceId = 0;
  m_playSceneDockClassId = 0;
  DestroyPlaySceneViewportTarget();
  if (m_playScenePhysics.IsInitialized()) {
    m_playScenePhysics.Shutdown();
  }
  m_playSceneEngineContext = t850::EngineContext{};

  if (m_playSceneHasPreviousConfig) {
    t850::g_config = m_playScenePreviousConfig;
    m_playSceneHasPreviousConfig = false;
  }
  if (restoreEditorScene) {
    RestoreEditorStateAfterPlay();
  }
  m_playScenePreviousActiveCameraIdx = -1;
  if (!restorePath.empty()) {
    std::error_code ec;
    std::filesystem::remove(restorePath, ec);
  }
  m_playSceneTempPath.clear();
  m_playSceneStatus.clear();
  IManager.xDelta = 0;
  IManager.yDelta = 0;
  for (int i = 0; i < MAXMOUSEBUTTONS; ++i) {
    IManager.MouseButtonStates[0][i] = false;
    IManager.MouseButtonStates[1][i] = false;
  }
  if (pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
    pFramework->pVideoDriver->SetBlendState(t850::BaseDriver::BLEND_DEFAULT);
    pFramework->pVideoDriver->SetCullFace(t850::BaseDriver::FRONT_FACES);
    int mainW = m_lastW;
    int mainH = m_lastH;
#ifdef OS_WINDOWS
    if (auto* w32 = static_cast<t850::Win32Framework*>(pFramework)) {
      if (w32->m_pWindow) {
        SDL_GetWindowSizeInPixels(w32->m_pWindow, &mainW, &mainH);
      }
    }
#endif
    if (mainW > 0 && mainH > 0) {
      m_lastW = mainW;
      m_lastH = mainH;
      m_camera.SetViewportSize(mainW, mainH);
      pFramework->pVideoDriver->SetViewport(0.0f, 0.0f, static_cast<float>(mainW), static_cast<float>(mainH));
      pFramework->pVideoDriver->SetScissorRect(0, 0, mainW, mainH);
    }
  }
  if (!m_sceneProps.pCameras.empty()) {
    m_sceneProps.SetPrimaryCamera(&m_camera.GetCameraMutable());
  }
}

bool EditorApp::EnsurePlaySceneViewportTarget(int width, int height) {
  if (!pFramework || !pFramework->pVideoDriver || width <= 0 || height <= 0) {
    return false;
  }

  t850::RenderViewportDesc desc;
  desc.colorCount = 1;
  desc.colorFormat = t850::BaseRT::RGBA8;
  desc.depthFormat = t850::BaseRT::F32;
  desc.minWidth = 64;
  desc.minHeight = 64;
  if (!m_playSceneViewportTarget.Ensure(pFramework->pVideoDriver, width, height, desc)) {
    T8_LOG_ERROR("[T8ditor] Failed to create Play Scene viewport RT %dx%d", width, height);
    return false;
  }

  T8_LOG_INFO("[T8ditor] Play Scene viewport RT created output=%d size=%dx%d",
              m_playSceneViewportTarget.Handle(),
              m_playSceneViewportTarget.Width(),
              m_playSceneViewportTarget.Height());
  return true;
}

void EditorApp::DestroyPlaySceneViewportTarget() {
  if (pFramework && pFramework->pVideoDriver) {
    m_playSceneViewportTarget.Destroy(pFramework->pVideoDriver);
  }
  m_playSceneViewportImageMinX = 0.0f;
  m_playSceneViewportImageMinY = 0.0f;
  m_playSceneViewportImageSizeX = 0.0f;
  m_playSceneViewportImageSizeY = 0.0f;
}

bool EditorApp::EnsurePlaySceneRuntimeLoaded() {
  if (m_playSceneLoaded) {
    return true;
  }
  if (!pFramework || !pFramework->pVideoDriver || m_playSceneTempPath.empty() || !m_playSceneViewportTarget.IsValid()) {
    return false;
  }
  if (m_playSceneLaunchFailed) {
    return false;
  }
  if (!m_playSceneHasVisibleObjects) {
    return false;
  }

  m_playSceneEngineContext = t850::GetEngineContext();
  m_playSceneEngineContext.physics = &m_playScenePhysics;
  if (!m_playScenePhysics.IsInitialized()) {
    if (!m_playScenePhysics.Initialize() && m_playScenePhysics.IsAvailable()) {
      T8_LOG_ERROR("[T8ditor] Play Scene physics runtime failed to initialize");
    }
  }

  m_playScene = std::make_unique<::Quake3Mock>();
  m_playScene->pFramework = pFramework;
  Quake3MockLaunchDesc launchDesc;
  launchDesc.sceneFilePath = m_playSceneTempPath;
  launchDesc.modelPath = t850::g_config.modelPath;
  launchDesc.width = m_playSceneViewportTarget.Width();
  launchDesc.height = m_playSceneViewportTarget.Height();
  launchDesc.startScene = 2;
  launchDesc.guiOnStart = false;
  m_playScene->SetLaunchDesc(launchDesc);
  m_playScene->SetEngineContext(&m_playSceneEngineContext);
  m_playScene->SetRenderSize(m_playSceneViewportTarget.Width(), m_playSceneViewportTarget.Height());
  m_playScene->SetFinalOutputRT(m_playSceneViewportTarget.Handle());
  m_playScene->OnLoadScene();
  if (m_playScene->m_meshCount <= 0) {
    m_playScene->OnDestoryScene();
    m_playScene.reset();
    m_playSceneStatus = "Play Scene did not load any visible meshes.";
    m_playSceneLaunchFailed = true;
    T8_LOG_ERROR("[T8ditor] Play Scene launch failed: runtime loaded no visible meshes from '%s'",
                 m_playSceneTempPath.c_str());
    return false;
  }
  m_playSceneLoaded = true;
  m_playSceneStatus.clear();
  T8_LOG_INFO("[T8ditor] Play Scene launched Quake3 scene from '%s'", m_playSceneTempPath.c_str());
  return true;
}

void EditorApp::DrawPlaySceneViewport() {
  ImVec2 available = ImGui::GetContentRegionAvail();
  if (m_playSceneLaunchFailed || !m_playSceneHasVisibleObjects) {
    ImGui::Dummy(ImVec2((std::max)(1.0f, available.x), 16.0f));
    ImGui::TextWrapped("%s", m_playSceneStatus.empty()
        ? "Temporary scene has no visible meshes. Add an object before Play."
        : m_playSceneStatus.c_str());
    return;
  }
  const int desiredViewportW = (std::max)(64, (int)std::floor(available.x));
  const int desiredViewportH = (std::max)(64, (int)std::floor(available.y));
  const bool mouseResizingWindow =
      ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
      ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
      ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  t850::RenderViewportDesc viewportDesc;
  viewportDesc.minWidth = 64;
  viewportDesc.minHeight = 64;
  const bool shouldResizeRT =
      m_playSceneViewportTarget.ShouldResize(desiredViewportW, desiredViewportH, mouseResizingWindow, viewportDesc);

  if (shouldResizeRT) {
    if (pFramework && pFramework->pVideoDriver) {
      pFramework->pVideoDriver->WaitForGPU();
    }
    if (!EnsurePlaySceneViewportTarget(desiredViewportW, desiredViewportH)) {
      ImGui::TextDisabled("Play Scene viewport unavailable.");
      return;
    }
    if (m_playScene && m_playSceneLoaded) {
      m_playScene->ResizeRenderTargets(
          m_playSceneViewportTarget.Width(),
          m_playSceneViewportTarget.Height(),
          m_playSceneViewportTarget.Handle());
    }
  }

  if (!pFramework || !pFramework->pVideoDriver || !m_playSceneViewportTarget.IsValid()) {
    ImGui::TextDisabled("Play Scene viewport unavailable.");
    return;
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  const int playViewportRT = m_playSceneViewportTarget.Handle();
  t850::BaseRT* rt = (playViewportRT >= 0 &&
                      playViewportRT < (int)driver->RTs.size())
      ? driver->RTs[playViewportRT]
      : nullptr;
  if (!rt || rt->vColorTextures.empty() || !rt->vColorTextures[0]) {
    ImGui::TextDisabled("Play Scene render target is unavailable.");
    return;
  }

  const ImVec2 imageSize((float)m_playSceneViewportTarget.Width(), (float)m_playSceneViewportTarget.Height());
  const ImVec2 imageMin = ImGui::GetCursorScreenPos();
  const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
  m_playSceneViewportImageMinX = imageMin.x;
  m_playSceneViewportImageMinY = imageMin.y;
  m_playSceneViewportImageSizeX = imageSize.x;
  m_playSceneViewportImageSizeY = imageSize.y;

  if (!EnsurePlaySceneRuntimeLoaded()) {
    ImGui::TextDisabled("%s", m_playSceneStatus.empty() ? "Play Scene is loading..." : m_playSceneStatus.c_str());
    return;
  }

  m_playScene->SetFinalOutputRT(m_playSceneViewportTarget.Handle());
  m_playScene->SetRenderSize(m_playSceneViewportTarget.Width(), m_playSceneViewportTarget.Height());
  m_playScene->OnUpdate(m_dtSecs);
  m_playScene->OnDraw();

  ImTextureID image = ImGuiTextureID(driver, rt->vColorTextures[0]);
  if (!image) {
    ImGui::TextDisabled("Play Scene texture is not available for ImGui.");
    return;
  }
  const bool flipV = driver->NeedsVFlip();
  const ImVec2 uv0(0.0f, flipV ? 1.0f : 0.0f);
  const ImVec2 uv1(1.0f, flipV ? 0.0f : 1.0f);
  ImGui::GetWindowDrawList()->AddImage(image, imageMin, imageMax, uv0, uv1);
  ImGui::InvisibleButton("##PlaySceneViewportInput",
                         imageSize,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  m_playSceneViewportInputActive = ImGui::IsItemHovered() || ImGui::IsItemActive();
}

void EditorApp::DrawPlaySceneWindow() {
  if (!m_playSceneOpen) {
    return;
  }

  ImGuiSetNextNativeEditorWindow(160.0f, 160.0f, 1280.0f, 720.0f);
  m_playSceneOpenRequested = false;
  bool keepOpen = m_playSceneOpen;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const bool rootBegun = ImGui::Begin("Play Scene", &keepOpen, ImGuiWindowFlags_NoDocking);
  if (rootBegun) {
    if (ImGuiViewport* viewport = ImGui::GetWindowViewport()) {
      m_playSceneImGuiViewportId = (unsigned int)viewport->ID;
    }
    m_playSceneDockspaceId = (unsigned int)ImGui::GetID("PlaySceneDockSpace");
    m_playSceneDockClassId = (unsigned int)ImGui::GetID("PlaySceneDockClass");
    ImGuiWindowClass playClass{};
    playClass.ClassId = (ImGuiID)m_playSceneDockClassId;
    playClass.DockingAllowUnclassed = false;
    ImGui::DockSpace(
        (ImGuiID)m_playSceneDockspaceId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_None,
        &playClass);
  }
  ImGui::End();
  ImGui::PopStyleVar();

  if (!keepOpen) {
    m_playSceneOpen = false;
    m_playSceneCloseRequested = true;
    m_playSceneViewportInputActive = false;
    return;
  }

  ImGuiWindowClass playClass{};
  playClass.ClassId = (ImGuiID)m_playSceneDockClassId;
  playClass.DockingAllowUnclassed = false;
  if (m_playSceneImGuiViewportId != 0) {
    ImGui::SetNextWindowViewport((ImGuiID)m_playSceneImGuiViewportId);
  }
  ImGui::SetNextWindowClass(&playClass);
  if (m_playSceneDockspaceId != 0) {
    ImGui::SetNextWindowDockID((ImGuiID)m_playSceneDockspaceId, ImGuiCond_FirstUseEver);
  }
  bool viewportOpen = true;
  if (ImGui::Begin("Play Scene Viewport##PlaySceneViewport",
                   &viewportOpen,
                   ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::Button("Stop")) {
      m_playSceneOpen = false;
      m_playSceneCloseRequested = true;
      m_playSceneViewportInputActive = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Press G for runtime controls.");
    ImGui::SameLine();
    if (m_playSceneLaunchFailed) {
      ImGui::TextDisabled("%s", m_playSceneStatus.c_str());
    } else if (m_playSceneHasVisibleObjects) {
      ImGui::TextDisabled("Temporary scene: %s", m_playSceneTempPath.c_str());
    } else {
      ImGui::TextDisabled("Temporary scene has no visible meshes; nothing to run.");
    }
    ImGui::Separator();
    if (!m_playSceneCloseRequested) {
      DrawPlaySceneViewport();
    }
  }
  ImGui::End();

  if (m_playSceneGuiVisible && m_playScene && m_playSceneLoaded) {
    if (m_playSceneImGuiViewportId != 0) {
      ImGui::SetNextWindowViewport((ImGuiID)m_playSceneImGuiViewportId);
    }
    ImGui::SetNextWindowSize(ImVec2(440.0f, 680.0f), ImGuiCond_FirstUseEver);
    t850::DevGuiContext gui;
    gui.SetIdSuffix("PlaySceneQuake3");
    gui.SetViewportId((ImGuiID)m_playSceneImGuiViewportId);
    gui.SetDockId((ImGuiID)m_playSceneDockspaceId);
    gui.SetWindowClassId((ImGuiID)m_playSceneDockClassId);
    const bool panelBegun = gui.BeginPanel("Scene Controls", &m_playSceneGuiVisible);
    if (panelBegun) {
      ImGui::TextDisabled("Play Scene runtime controls - press G to hide.");
      ImGui::Separator();
      m_playScene->DrawDevGui(gui);
    }
    gui.EndPanel();
  }
}

bool EditorApp::EnsureMeshEditorViewportTarget(int width, int height) {
  if (!pFramework || !pFramework->pVideoDriver || width <= 0 || height <= 0) {
    return false;
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  t850::RenderViewportDesc gbufferDesc;
  gbufferDesc.colorFormats = {
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA16F,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA16F,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA8};
  gbufferDesc.depthFormat = t850::BaseRT::F32;
  gbufferDesc.minWidth = 64;
  gbufferDesc.minHeight = 64;

  t850::RenderViewportDesc outputDesc;
  outputDesc.colorCount = 1;
  outputDesc.colorFormat = t850::BaseRT::RGBA8;
  outputDesc.depthFormat = t850::BaseRT::F32;
  outputDesc.minWidth = 64;
  outputDesc.minHeight = 64;

  if (!m_meshEditorGBufferTarget.Ensure(driver, width, height, gbufferDesc)) {
    DestroyMeshEditorViewportTarget();
    T8_LOG_ERROR("[T8ditor] Failed to create Mesh Edit GBuffer RT %dx%d", width, height);
    return false;
  }

  if (!m_meshEditorViewportTarget.Ensure(driver, width, height, outputDesc)) {
    DestroyMeshEditorViewportTarget();
    T8_LOG_ERROR("[T8ditor] Failed to create Mesh Edit viewport RT %dx%d", width, height);
    return false;
  }

  T8_LOG_INFO("[T8ditor] Mesh Edit viewport RTs created gbuffer=%d output=%d size=%dx%d",
              m_meshEditorGBufferTarget.Handle(),
              m_meshEditorViewportTarget.Handle(),
              m_meshEditorViewportTarget.Width(),
              m_meshEditorViewportTarget.Height());
  return true;
}

void EditorApp::DestroyMeshEditorViewportTarget() {
  if (pFramework && pFramework->pVideoDriver) {
    m_meshEditorGBufferTarget.Destroy(pFramework->pVideoDriver);
    m_meshEditorViewportTarget.Destroy(pFramework->pVideoDriver);
  }
}

void EditorApp::DestroyMeshEditorSceneResources() {
  t850::BaseDriver* driver = (pFramework && pFramework->pVideoDriver) ? pFramework->pVideoDriver : nullptr;
  auto destroyTexture = [&](int& textureIndex) {
    if (driver && textureIndex >= 0) {
      driver->DestroyTexture(textureIndex);
    }
    textureIndex = -1;
  };

  destroyTexture(m_meshEditorEnvMapIdx);
  destroyTexture(m_meshEditorDiffuseIBLTexIndex);
  destroyTexture(m_meshEditorSpecularIBLTexIndex);
  destroyTexture(m_meshEditorBrdfLUTTexIndex);
  destroyTexture(m_meshEditorSheenIBLTexIndex);
  destroyTexture(m_meshEditorCharlieLUTTexIndex);
  destroyTexture(m_meshEditorSheenELUTTexIndex);

  if (m_meshEditorSSAOTextureReady) {
    m_meshEditorSceneProps.SSAOKernel.Destroy();
    m_meshEditorSSAOTextureReady = false;
  }

  m_meshEditorEnvMaps = t850::EnvironmentMapSet{};
  m_meshEditorCurrentCubemapPath.clear();
  m_meshEditorCurrentCubemapIndex = -1;
  m_meshEditorSceneModelKey.clear();
  m_meshEditorSceneReady = false;
  m_meshEditorSceneProps = SceneProps{};
  m_meshEditorSceneProps.SSAOKernel.NoiseTex = nullptr;
}

void EditorApp::SetMeshEditorCubemap(const std::string& cubemapPath) {
  const std::string normalizedPath = NormalizeEditorResourcePath(cubemapPath);
  if (normalizedPath.empty() || !pFramework || !pFramework->pVideoDriver) {
    return;
  }
  if (m_meshEditorEnvMapIdx >= 0 &&
      EditorResourcePathEquals(normalizedPath, m_meshEditorCurrentCubemapPath)) {
    return;
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  auto destroyTexture = [&](int& textureIndex) {
    if (textureIndex >= 0) {
      driver->DestroyTexture(textureIndex);
      textureIndex = -1;
    }
  };

  destroyTexture(m_meshEditorEnvMapIdx);
  if (m_meshEditorSceneSetup.environmentDiffuseIBL.empty()) {
    destroyTexture(m_meshEditorDiffuseIBLTexIndex);
  }
  if (m_meshEditorSceneSetup.environmentSpecularIBL.empty()) {
    destroyTexture(m_meshEditorSpecularIBLTexIndex);
  }
  if (m_meshEditorSceneSetup.environmentSheenIBL.empty()) {
    destroyTexture(m_meshEditorSheenIBLTexIndex);
  }

  m_meshEditorEnvMapIdx = driver->CreateTexture(normalizedPath);
  if (m_meshEditorEnvMapIdx < 0) {
    T8_LOG_ERROR("[T8ditor] Mesh Edit failed to load cubemap '%s'", normalizedPath.c_str());
    m_meshEditorCurrentCubemapPath.clear();
    m_meshEditorEnvMaps = t850::EnvironmentMapSet{};
    return;
  }

  m_meshEditorCurrentCubemapPath = normalizedPath;
  m_meshEditorEnvMaps.SetFallback(m_meshEditorEnvMapIdx);
  t850::LoadEnvironmentIBLResources(
      driver,
      {m_meshEditorSceneSetup.environmentDiffuseIBL,
       m_meshEditorSceneSetup.environmentSpecularIBL,
       m_meshEditorSceneSetup.environmentBrdfLUT,
       m_meshEditorSceneSetup.environmentSheenIBL,
       m_meshEditorSceneSetup.environmentCharlieLUT,
       m_meshEditorSceneSetup.environmentSheenELUT},
      m_meshEditorEnvMaps,
      m_meshEditorDiffuseIBLTexIndex,
      m_meshEditorSpecularIBLTexIndex,
      m_meshEditorBrdfLUTTexIndex,
      m_meshEditorSheenIBLTexIndex,
      m_meshEditorCharlieLUTTexIndex,
      m_meshEditorSheenELUTTexIndex);
  t850::UpdateSceneIBLSettings(m_meshEditorSceneProps, driver, m_meshEditorEnvMaps);

  const t850::SelectorDesc* cubemapDesc =
      FindEditorSelectorDesc(m_meshEditorSceneSetup.descriptor.selectors, "cubemap");
  if (cubemapDesc) {
    m_meshEditorCurrentCubemapIndex = EditorCubemapSelectorIndexForPath(*cubemapDesc, normalizedPath);
  }
}

void EditorApp::ApplyMeshEditorProfileState(SceneObject& obj, const t850::SandboxProfileDesc& state) {
  const bool hasCubemapPath = state.cubemap_path.has_value() &&
      !NormalizeEditorResourcePath(*state.cubemap_path).empty();
  if (hasCubemapPath) {
    SetMeshEditorCubemap(*state.cubemap_path);
  }

  t850::RenderSkinnedMesh* skinned = GetSkinnedMesh(obj);

  for (const auto& value : state.sliders) {
    if (value.name == "exposure") m_meshEditorSceneProps.Exposure = value.value;
    else if (value.name == "bloom_factor") m_meshEditorSceneProps.BloomFactor = value.value;
    else if (value.name == "bloom_threshold") m_meshEditorSceneProps.BloomThreshold = value.value;
    else if (value.name == "tm_white_level") m_meshEditorSceneProps.ToneMapWhiteLevel = value.value;
    else if (value.name == "tm_adapt_tau") m_meshEditorSceneProps.LuminanceTau = value.value;
    else if (value.name == "pcf_radius") m_meshEditorSceneProps.PCFScale = value.value;
    else if (value.name == "pcf_samples") m_meshEditorSceneProps.PCFSamples = value.value;
    else if (value.name == "ssao_kernel_size") { m_meshEditorSceneProps.SSAOKernel.KernelSize = (int)value.value; m_meshEditorSceneProps.SSAOKernel.Update(); }
    else if (value.name == "ssao_radius") m_meshEditorSceneProps.SSAOKernel.Radius = value.value;
    else if (value.name == "dof_aperture") m_meshEditorSceneProps.Aperture = value.value;
    else if (value.name == "dof_focal_length") m_meshEditorSceneProps.FocalLength = value.value;
    else if (value.name == "dof_max_coc") m_meshEditorSceneProps.MaxCoc = value.value;
    else if (value.name == "dof_far_samples") m_meshEditorSceneProps.DOF_Far_Samples_squared = value.value;
    else if (value.name == "dof_near_samples") m_meshEditorSceneProps.DOF_Near_Samples_squared = value.value;
    else if (value.name == "light_volume_steps") m_meshEditorSceneProps.LightVolumeSteps = value.value;
    else if (value.name == "godrays_factor") m_meshEditorSceneProps.GodRaysFactor = value.value;
    else if (value.name == "fov") m_meshEditorFovDeg = value.value;
    else if (value.name == "light_radius_scale") m_meshEditorSceneProps.LightRadiusScale = value.value;
    else if (value.name == "light_intensity_scale") m_meshEditorSceneProps.LightIntensityScale = value.value;
    else if (value.name == "lightmap_intensity") m_meshEditorSceneProps.LightmapIntensity = value.value;
    else if (value.name == "shadow_bias") m_meshEditorSceneProps.ShadowBias = value.value;
    else if (value.name == "shadow_min") m_meshEditorSceneProps.ShadowMin = value.value;
    else if (value.name == "env_factor") m_meshEditorSceneProps.EnvFactor = value.value;
    else if (value.name == "ibl_factor") m_meshEditorSceneProps.IBLFactor = value.value;
    else if (value.name == "ibl_mip_count") m_meshEditorSceneProps.IBLMipCount = (std::max)(0.0f, value.value);
    else if (value.name == "ibl_diffuse_mip_level") m_meshEditorSceneProps.IBLDiffuseMipLevel = (std::max)(0.0f, value.value);
    else if (value.name == "ibl_brdf_lut_enabled") m_meshEditorSceneProps.IBLBRDFLUTEnabled = value.value > 0.5f ? 1.0f : 0.0f;
    else if (value.name == "material_emissive_intensity") m_meshEditorSceneProps.MaterialEmissiveIntensity = value.value;
    else if (value.name == "material_transmission_multiplier") m_meshEditorSceneProps.MaterialTransmissionMultiplier = value.value;
    else if (value.name == "material_refraction_strength") m_meshEditorSceneProps.MaterialRefractionStrength = value.value;

    for (int kernelIndex = 0; kernelIndex < (int)m_meshEditorSceneProps.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = m_meshEditorSceneProps.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      const std::string prefix = "gauss_" + std::to_string(kernelIndex) + "_";
      if (value.name == prefix + "radius") { kernel->radius = value.value; kernel->Update(); }
      else if (value.name == prefix + "sigma") { kernel->sigma = value.value; kernel->Update(); }
    }
  }

  for (const auto& value : state.checkboxes) {
    if (value.name == "shadow_toggle") m_meshEditorSceneProps.ToogleShadow = value.value ? 1 : 0;
    else if (value.name == "ssao_toggle") m_meshEditorSceneProps.ToogleSSAO = value.value ? 1 : 0;
    else if (value.name == "show_wireframe") m_meshEditorShowWireframe = value.value;
    else if (value.name == "show_skeleton") m_meshEditorShowSkeleton = value.value && skinned && skinned->HasSkinData();
    else if (value.name == "point_lights_enabled") m_meshEditorSceneProps.PointLightsEnabled = value.value;
    else if (value.name == "debug_luminance") {
      m_meshEditorSceneProps.DebugLuminanceEnabled = value.value;
      if (!value.value) m_meshEditorSceneProps.DebugAdaptedLuminanceValid = false;
    }
  }

  for (const auto& value : state.selectors) {
    if (value.name == "cubemap" && !hasCubemapPath) {
      const t850::SelectorDesc* cubemapDesc =
          FindEditorSelectorDesc(m_meshEditorSceneSetup.descriptor.selectors, "cubemap");
      if (cubemapDesc && value.value >= 0 && value.value < (int)cubemapDesc->options.size()) {
        m_meshEditorCurrentCubemapIndex = value.value;
        SetMeshEditorCubemap(EditorCubemapPathForSelectorIndex(*cubemapDesc, value.value));
      }
    } else if (value.name == "luminance_mode") {
      m_meshEditorSceneProps.LuminanceMode = value.value;
    }

    for (int kernelIndex = 0; kernelIndex < (int)m_meshEditorSceneProps.pGaussKernels.size(); ++kernelIndex) {
      GaussFilter* kernel = m_meshEditorSceneProps.pGaussKernels[kernelIndex];
      if (!kernel) continue;
      const std::string name = "gauss_" + std::to_string(kernelIndex) + "_kernel_size";
      if (value.name == name) { kernel->kernelSize = value.value; kernel->Update(); }
    }
  }

  for (const auto& lightState : state.lights) {
    if (lightState.index < 0 || lightState.index >= (int)m_meshEditorSceneProps.Lights.size()) {
      continue;
    }
    Light& light = m_meshEditorSceneProps.Lights[lightState.index];
    if (lightState.position.has_value()) light.Position = EditorVec3FromArray(*lightState.position, 1.0f);
    if (lightState.direction.has_value()) {
      XVECTOR3 direction = EditorVec3FromArray(*lightState.direction, 0.0f);
      if (direction.Length() > 0.0001f) {
        direction.Normalize();
        light.Direction = direction;
      }
    }
    if (lightState.color.has_value()) light.Color = EditorVec3FromArray(*lightState.color, 0.0f);
    if (lightState.diameter.has_value()) light.radius = (std::max)(0.001f, *lightState.diameter * 0.5f);
    if (lightState.intensity.has_value()) light.Intensity = *lightState.intensity;
  }

  if (state.frustum_culling.has_value()) {
    m_meshEditorSceneProps.FrustumCullingEnabled = *state.frustum_culling;
  }

  auto applyOrbit = [&](const t850::SandboxOrbitCameraDesc& orbit) {
    const XVECTOR3 target = EditorVec3FromArray(orbit.target, 1.0f);
    const XVECTOR3 panOffset = EditorVec3FromArray(orbit.pan_offset, 0.0f);
    m_meshEditorOrbitTarget = target + panOffset;
    m_meshEditorOrbitTarget.w = 1.0f;
    m_meshEditorOrbitYaw = orbit.yaw;
    m_meshEditorOrbitPitch = orbit.pitch;
    m_meshEditorOrbitDistance = (std::max)(0.001f, orbit.distance);
    m_meshEditorCameraInitialized = true;
  };

  if (state.camera.has_value()) {
    const auto& cameraState = *state.camera;
    m_meshEditorFovDeg = cameraState.fov;
    if (cameraState.orbit.has_value()) {
      applyOrbit(*cameraState.orbit);
    } else {
      m_meshEditorCamera.Eye = EditorVec3FromArray(cameraState.eye, 1.0f);
      m_meshEditorOrbitYaw = cameraState.yaw;
      m_meshEditorOrbitPitch = cameraState.pitch;
      m_meshEditorCameraInitialized = true;
    }
  } else if (state.orbit_camera.has_value()) {
    applyOrbit(*state.orbit_camera);
  }

  (void)state.animations;
  (void)state.current_keyframe;
}

void EditorApp::ApplyMeshEditorProfiles(SceneObject& obj) {
  const t850::SandboxProfileDesc* baseProfile = nullptr;
  const t850::SandboxProfileDesc* runtimeProfile = nullptr;
  int bestRuntimeScore = -1;
  for (const auto& profile : m_meshEditorSceneSetup.descriptor.profiles) {
    const bool modelSpecific = !profile.model.empty();
    const bool modelMatches = !modelSpecific ||
        MeshEditorProfileModelKey(profile.model) == m_meshEditorSceneModelKey;
    if (!modelMatches) {
      continue;
    }

    const bool hasTarget = !profile.name.empty() ||
                           !profile.platform.empty() ||
                           !profile.architecture.empty() ||
                           !profile.gpu_family.empty() ||
                           !profile.gpu_name_contains.empty();
    if (!hasTarget && modelSpecific) {
      baseProfile = &profile;
      continue;
    }

    const int score = t850::ScoreSceneProfileMatch(profile, m_meshEditorSceneModelKey);
    if (score > bestRuntimeScore) {
      bestRuntimeScore = score;
      runtimeProfile = &profile;
    }
  }

  if (baseProfile) {
    ApplyMeshEditorProfileState(obj, *baseProfile);
  }
  if (runtimeProfile && runtimeProfile != baseProfile) {
    ApplyMeshEditorProfileState(obj, *runtimeProfile);
  }
}

bool EditorApp::EnsureMeshEditorSceneState(SceneObject& obj) {
  const std::string meshPath = obj.meshPath.empty() ? obj.name : obj.meshPath;
  const std::string modelKey = MeshEditorProfileModelKey(meshPath);
  if (m_meshEditorSceneReady && m_meshEditorSceneModelKey == modelKey) {
    return true;
  }

  if (!m_meshEditorSceneSetupLoaded) {
    if (!m_meshEditorSceneSetup.Load("Scenes/RagdollEditor.json")) {
      T8_LOG_ERROR("[T8ditor] Mesh Edit failed to load Scenes/RagdollEditor.json");
      return false;
    }
    m_meshEditorSceneSetupLoaded = true;
  }

  if (m_meshEditorSSAOTextureReady) {
    m_meshEditorSceneProps.SSAOKernel.Destroy();
    m_meshEditorSSAOTextureReady = false;
  }
  m_meshEditorSceneProps = SceneProps{};
  m_meshEditorSceneProps.SSAOKernel.NoiseTex = nullptr;
  m_meshEditorSceneProps.SSAOKernel.InitTexture();
  m_meshEditorSSAOTextureReady = true;
  m_meshEditorSceneSetup.Apply(m_meshEditorSceneProps);
  if (!m_meshEditorRenderGraphLoaded) {
    if (!m_meshEditorRenderGraph.Load("Scenes/RagdollEditor_RenderGraph.json")) {
      T8_LOG_ERROR("[T8ditor] Mesh Edit failed to load Scenes/RagdollEditor_RenderGraph.json");
      return false;
    }
    m_meshEditorRenderGraph.CreateRenderTargets(pFramework->pVideoDriver, m_meshEditorSceneProps);
    m_meshEditorRenderGraphLoaded = true;
  }
  if (m_meshEditorSceneProps.pCameras.empty()) {
    m_meshEditorSceneProps.AddCamera(&m_meshEditorCamera);
  }
  if (!m_meshEditorSceneProps.pLightCameras.empty()) {
    m_meshEditorSceneProps.ActiveLightCamera = 0;
  }
  m_meshEditorSceneProps.pCullingCamera = &m_meshEditorCamera;
  m_meshEditorSceneModelKey = modelKey;
  m_meshEditorShowWireframe = false;
  m_meshEditorShowSkeleton = false;

  if (Camera* sceneCamera = m_meshEditorSceneSetup.GetCamera(0)) {
    m_meshEditorFovDeg = Rad2Deg(sceneCamera->Fov);
  } else {
    m_meshEditorFovDeg = 45.0f;
  }

  std::string startupCubemapPath = NormalizeEditorResourcePath(m_meshEditorSceneSetup.environmentMap);
  if (startupCubemapPath.empty()) {
    startupCubemapPath = "sky/Ennis.dds";
  }
  const t850::SelectorDesc* cubemapDesc =
      FindEditorSelectorDesc(m_meshEditorSceneSetup.descriptor.selectors, "cubemap");
  if (cubemapDesc) {
    const int environmentIndex = EditorCubemapSelectorIndexForPath(*cubemapDesc, startupCubemapPath);
    m_meshEditorCurrentCubemapIndex = environmentIndex >= 0
        ? environmentIndex
        : (std::max)(0, (std::min)(cubemapDesc->default_index,
                                   static_cast<int>(cubemapDesc->options.size()) - 1));
    if (environmentIndex < 0 && m_meshEditorCurrentCubemapIndex >= 0) {
      startupCubemapPath = EditorCubemapPathForSelectorIndex(*cubemapDesc, m_meshEditorCurrentCubemapIndex);
    }
  }

  auto applyStartupProfileCubemap = [&](const t850::SandboxProfileDesc& profile) {
    if (profile.cubemap_path.has_value() &&
        !NormalizeEditorResourcePath(*profile.cubemap_path).empty()) {
      startupCubemapPath = NormalizeEditorResourcePath(*profile.cubemap_path);
      if (cubemapDesc) {
        m_meshEditorCurrentCubemapIndex = EditorCubemapSelectorIndexForPath(*cubemapDesc, startupCubemapPath);
      }
      return;
    }

    if (cubemapDesc) {
      const int selectorIndex = EditorCubemapSelectorIndexFromProfile(profile);
      if (selectorIndex >= 0 && selectorIndex < static_cast<int>(cubemapDesc->options.size())) {
        m_meshEditorCurrentCubemapIndex = selectorIndex;
        startupCubemapPath = EditorCubemapPathForSelectorIndex(*cubemapDesc, selectorIndex);
      }
    }
  };

  const t850::SandboxProfileDesc* baseProfile = nullptr;
  const t850::SandboxProfileDesc* runtimeProfile = nullptr;
  int bestRuntimeScore = -1;
  for (const auto& profile : m_meshEditorSceneSetup.descriptor.profiles) {
    const bool modelSpecific = !profile.model.empty();
    const bool modelMatches = !modelSpecific ||
        MeshEditorProfileModelKey(profile.model) == m_meshEditorSceneModelKey;
    if (!modelMatches) {
      continue;
    }
    const bool hasTarget = !profile.name.empty() ||
                           !profile.platform.empty() ||
                           !profile.architecture.empty() ||
                           !profile.gpu_family.empty() ||
                           !profile.gpu_name_contains.empty();
    if (!hasTarget && modelSpecific) {
      baseProfile = &profile;
      continue;
    }
    const int score = t850::ScoreSceneProfileMatch(profile, m_meshEditorSceneModelKey);
    if (score > bestRuntimeScore) {
      bestRuntimeScore = score;
      runtimeProfile = &profile;
    }
  }
  if (baseProfile) {
    applyStartupProfileCubemap(*baseProfile);
  }
  if (runtimeProfile && runtimeProfile != baseProfile) {
    applyStartupProfileCubemap(*runtimeProfile);
  }

  SetMeshEditorCubemap(startupCubemapPath);

  ApplyMeshEditorProfiles(obj);
  m_meshEditorSceneReady = true;
  T8_LOG_INFO("[T8ditor] Mesh Edit scene state ready model='%s' cubemap='%s'",
              m_meshEditorSceneModelKey.c_str(),
              m_meshEditorCurrentCubemapPath.c_str());
  return true;
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
  m_ragdollEditorNativeHandle = nullptr;
  m_ragdollEditorLoggedNativeHandle = nullptr;
  m_ragdollEditorMainViewportLogged = false;
  m_ragdollEditorDirty = false;
  m_ragdollEditorStatus.clear();
  m_ragdollEditorOpen = true;
  m_ragdollEditorOpenRequested = true;
  {
    ImGuiIO& io = ImGui::GetIO();
    T8_LOG_INFO("[T8ditor] Requested native editor window title='Ragdoll Edit' object='%s' configFlags=0x%08X backendFlags=0x%08X",
                obj.name.c_str(),
                (unsigned int)io.ConfigFlags,
                (unsigned int)io.BackendFlags);
  }
  g_selectedIdx = objectIndex;
  g_selectionType = 0;
  g_multiSelect.clear();
  g_multiSelect.insert(objectIndex);

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
  m_ragdollEditorOpen = false;
  m_ragdollEditorOpenRequested = false;
  m_ragdollEditorObjectIndex = -1;
  m_ragdollEditorSelectedBody = -1;
  m_ragdollEditorSelectedJoint = -1;
  m_ragdollEditorSelectedUnassignedBone = -1;
  m_ragdollEditorSelectedAffectedBone = -1;
  m_ragdollEditorNativeHandle = nullptr;
  m_ragdollEditorLoggedNativeHandle = nullptr;
  m_ragdollEditorMainViewportLogged = false;
  m_ragdollEditorDirty = false;
  m_ragdollEditorStatus.clear();
}

bool EditorApp::EnsureRagdollEditorViewportTarget(int width, int height) {
  if (!pFramework || !pFramework->pVideoDriver || width <= 0 || height <= 0) {
    return false;
  }

  width = (std::max)(1, width);
  height = (std::max)(1, height);
  if (m_ragdollEditorGBufferRT >= 0 &&
      m_ragdollEditorViewportRT >= 0 &&
      m_ragdollEditorViewportW == width &&
      m_ragdollEditorViewportH == height) {
    return true;
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  DestroyRagdollEditorViewportTarget();

  std::vector<int> gbufferFormats = {
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA16F,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA16F,
      t850::BaseRT::RGBA8,
      t850::BaseRT::RGBA8};
  m_ragdollEditorGBufferRT = driver->CreateRT(
      7,
      gbufferFormats,
      t850::BaseRT::F32,
      width,
      height);
  if (m_ragdollEditorGBufferRT < 0) {
    m_ragdollEditorViewportW = 0;
    m_ragdollEditorViewportH = 0;
    T8_LOG_ERROR("[T8ditor] Failed to create Ragdoll Edit GBuffer RT %dx%d", width, height);
    return false;
  }

  m_ragdollEditorViewportRT = driver->CreateRT(
      1,
      t850::BaseRT::RGBA8,
      t850::BaseRT::F32,
      width,
      height);
  if (m_ragdollEditorViewportRT < 0) {
    DestroyRagdollEditorViewportTarget();
    m_ragdollEditorViewportW = 0;
    m_ragdollEditorViewportH = 0;
    T8_LOG_ERROR("[T8ditor] Failed to create Ragdoll Edit viewport RT %dx%d", width, height);
    return false;
  }
  m_ragdollEditorViewportW = width;
  m_ragdollEditorViewportH = height;
  T8_LOG_INFO("[T8ditor] Ragdoll Edit viewport RTs created gbuffer=%d output=%d size=%dx%d",
              m_ragdollEditorGBufferRT,
              m_ragdollEditorViewportRT,
              width,
              height);
  return true;
}

void EditorApp::DestroyRagdollEditorViewportTarget() {
  if (pFramework && pFramework->pVideoDriver) {
    if (m_ragdollEditorGBufferRT >= 0) {
      pFramework->pVideoDriver->DestroyRT(m_ragdollEditorGBufferRT);
    }
    if (m_ragdollEditorViewportRT >= 0) {
      pFramework->pVideoDriver->DestroyRT(m_ragdollEditorViewportRT);
    }
  }
  m_ragdollEditorGBufferRT = -1;
  m_ragdollEditorViewportRT = -1;
  m_ragdollEditorViewportW = 0;
  m_ragdollEditorViewportH = 0;
  m_ragdollEditorPendingViewportW = 0;
  m_ragdollEditorPendingViewportH = 0;
  m_ragdollEditorViewportStableFrames = 0;
}

void EditorApp::DrawRagdollEditorViewport(SceneObject& obj) {
  ImVec2 available = ImGui::GetContentRegionAvail();
  const int desiredViewportW = (std::max)(64, (int)std::floor(available.x));
  const int desiredViewportH = (std::max)(64, (int)std::floor(available.y));
  const bool haveViewportRT = m_ragdollEditorGBufferRT >= 0 && m_ragdollEditorViewportRT >= 0;
  bool shouldResizeRT = !haveViewportRT;
  if (haveViewportRT &&
      (desiredViewportW != m_ragdollEditorViewportW || desiredViewportH != m_ragdollEditorViewportH)) {
    const bool mouseResizingWindow =
        ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
        ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
        ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    if (desiredViewportW != m_ragdollEditorPendingViewportW ||
        desiredViewportH != m_ragdollEditorPendingViewportH) {
      m_ragdollEditorPendingViewportW = desiredViewportW;
      m_ragdollEditorPendingViewportH = desiredViewportH;
      m_ragdollEditorViewportStableFrames = 0;
    } else if (!mouseResizingWindow) {
      ++m_ragdollEditorViewportStableFrames;
    }
    shouldResizeRT = !mouseResizingWindow && m_ragdollEditorViewportStableFrames >= 8;
  }

  if (shouldResizeRT) {
    if (pFramework && pFramework->pVideoDriver) {
      pFramework->pVideoDriver->WaitForGPU();
    }
    if (!EnsureRagdollEditorViewportTarget(desiredViewportW, desiredViewportH)) {
      ImGui::TextDisabled("Ragdoll viewport unavailable.");
      return;
    }
    m_ragdollEditorPendingViewportW = desiredViewportW;
    m_ragdollEditorPendingViewportH = desiredViewportH;
    m_ragdollEditorViewportStableFrames = 0;
  }

  if (m_ragdollEditorViewportW <= 0 || m_ragdollEditorViewportH <= 0) {
    ImGui::TextDisabled("Ragdoll viewport unavailable.");
    return;
  }
  const int viewportW = m_ragdollEditorViewportW;
  const int viewportH = m_ragdollEditorViewportH;
  if (!pFramework || !pFramework->pVideoDriver) {
    return;
  }
  if (obj.ragdollAuthoringReady) {
    EnsureEditorRagdollState(obj.ragdollAuthoring);
  }

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  t850::BaseRT* gbufferRT = (m_ragdollEditorGBufferRT >= 0 &&
                             m_ragdollEditorGBufferRT < (int)driver->RTs.size())
      ? driver->RTs[m_ragdollEditorGBufferRT]
      : nullptr;
  t850::BaseRT* rt = (m_ragdollEditorViewportRT >= 0 &&
                      m_ragdollEditorViewportRT < (int)driver->RTs.size())
      ? driver->RTs[m_ragdollEditorViewportRT]
      : nullptr;
  if (!gbufferRT || gbufferRT->vColorTextures.size() < 7 || !gbufferRT->pDepthTexture ||
      !rt || rt->vColorTextures.empty() || !rt->vColorTextures[0]) {
    ImGui::TextDisabled("Ragdoll viewport render targets are unavailable.");
    return;
  }

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
  const float clampedPitch = std::clamp(m_ragdollEditorOrbitPitch, -1.45f, 1.45f);
  m_ragdollEditorOrbitPitch = clampedPitch;
  const float cp = std::cos(m_ragdollEditorOrbitPitch);
  XVECTOR3 eye(
      m_ragdollEditorOrbitTarget.x + std::sin(m_ragdollEditorOrbitYaw) * cp * m_ragdollEditorOrbitDistance,
      m_ragdollEditorOrbitTarget.y + std::sin(m_ragdollEditorOrbitPitch) * m_ragdollEditorOrbitDistance,
      m_ragdollEditorOrbitTarget.z + std::cos(m_ragdollEditorOrbitYaw) * cp * m_ragdollEditorOrbitDistance,
      1.0f);
  const float farPlane = (std::max)(1000.0f, m_ragdollEditorOrbitDistance * 20.0f);
  m_ragdollEditorCamera.InitPerspective(eye, 45.0f * kDegToRad, aspect, 0.01f, farPlane);
  m_ragdollEditorCamera.Eye = eye;
  m_ragdollEditorCamera.SetLookAt(m_ragdollEditorOrbitTarget);
  m_ragdollEditorCamera.Update(0.0f);

  Camera* previousCamera = m_sceneProps.GetPrimaryCamera();
  if (!m_sceneProps.pCameras.empty()) {
    m_sceneProps.SetPrimaryCamera(&m_ragdollEditorCamera);
  }

  driver->PushRT(m_ragdollEditorGBufferRT);
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

  driver->PushRT(m_ragdollEditorViewportRT);
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
      viewportSkinned->DrawWireframe(XVECTOR3(0.42f, 0.42f, 0.42f, 1.0f));
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

  ImTextureID image = ImGuiTextureID(driver, rt->vColorTextures[0]);
  if (!image) {
    ImGui::TextDisabled("Ragdoll viewport texture is not available for ImGui.");
    return;
  }

  const bool flipV = driver->NeedsVFlip();
  const ImVec2 uv0(0.0f, flipV ? 1.0f : 0.0f);
  const ImVec2 uv1(1.0f, flipV ? 0.0f : 1.0f);
  const int displayViewportW = desiredViewportW;
  const int displayViewportH = desiredViewportH;
  const ImVec2 viewportSize((float)displayViewportW, (float)displayViewportH);
  const ImVec2 imageMin = ImGui::GetCursorScreenPos();
  const ImVec2 imageMax(imageMin.x + viewportSize.x, imageMin.y + viewportSize.y);
  ImGui::GetWindowDrawList()->AddImage(image, imageMin, imageMax, uv0, uv1);
  ImGui::InvisibleButton(
      "##RagdollEditorViewportInput",
      viewportSize,
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  const bool viewportHovered = ImGui::IsItemHovered();
  const bool viewportActive = ImGui::IsItemActive();
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
    guiState.simulationSpeedIndex = 3;
    guiState.fixedSimulationDelta = false;
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
    context.callbacks.setSimulationSpeedIndex = [](int) {};
    context.callbacks.setFixedSimulationDelta = [](bool) {};
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

bool EditorApp::EnsureMeshEditorEmbeddedScene(SceneObject& obj) {
  if (m_meshEditorScene && m_meshEditorSceneLoaded) {
    return true;
  }
  if (!pFramework || !pFramework->pVideoDriver || !obj.litInst.pBase) {
    return false;
  }

  m_meshEditorScene = std::make_unique<::RagdollEditor>();
  m_meshEditorScene->pFramework = pFramework;
  m_meshEditorScene->SetEngineContext(&t850::GetEngineContext());
  const std::string meshPath = obj.meshPath.empty() ? obj.name : obj.meshPath;
  m_meshEditorScene->UseExternalMesh(obj.litInst, meshPath);
  m_meshEditorScene->SetRenderViewport(m_meshEditorViewportImageMinX,
                                       m_meshEditorViewportImageMinY,
                                       m_meshEditorViewportTarget.Width(),
                                       m_meshEditorViewportTarget.Height());
  m_meshEditorScene->SetFinalOutputRT(m_meshEditorViewportTarget.Handle());
  m_meshEditorScene->OnLoadScene();
  if (m_meshEditorScene->m_meshCount <= 0 || !m_meshEditorScene->Meshes[0].pBase) {
    T8_LOG_ERROR("[T8ditor] Mesh Edit failed to load embedded RagdollEditor mesh '%s'", meshPath.c_str());
    m_meshEditorScene->OnDestoryScene();
    m_meshEditorScene.reset();
    return false;
  }
  m_meshEditorSceneLoaded = true;
  T8_LOG_INFO("[T8ditor] Mesh Edit is hosting RagdollEditor scene for '%s'", meshPath.c_str());
  return true;
}

void EditorApp::DrawMeshEditorViewport(SceneObject& obj) {
  ImVec2 available = ImGui::GetContentRegionAvail();
  const int desiredViewportW = (std::max)(64, (int)std::floor(available.x));
  const int desiredViewportH = (std::max)(64, (int)std::floor(available.y));
  const bool mouseResizingWindow =
      ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
      ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
      ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  t850::RenderViewportDesc viewportDesc;
  viewportDesc.minWidth = 64;
  viewportDesc.minHeight = 64;
  const bool shouldResizeRT =
      m_meshEditorViewportTarget.ShouldResize(desiredViewportW, desiredViewportH, mouseResizingWindow, viewportDesc);

  if (shouldResizeRT) {
    if (pFramework && pFramework->pVideoDriver) {
      pFramework->pVideoDriver->WaitForGPU();
    }
    if (!EnsureMeshEditorViewportTarget(desiredViewportW, desiredViewportH)) {
      ImGui::TextDisabled("Mesh editor viewport unavailable.");
      return;
    }
    if (m_meshEditorScene && m_meshEditorSceneLoaded) {
      m_meshEditorScene->ResizeRenderTargets(
          m_meshEditorViewportTarget.Width(),
          m_meshEditorViewportTarget.Height(),
          m_meshEditorViewportTarget.Handle());
    }
  }

  if (!m_meshEditorViewportTarget.IsValid() || !pFramework || !pFramework->pVideoDriver) {
    ImGui::TextDisabled("Mesh editor viewport unavailable.");
    return;
  }

  const int viewportW = m_meshEditorViewportTarget.Width();
  const int viewportH = m_meshEditorViewportTarget.Height();
  const ImVec2 embeddedViewportSize((float)viewportW, (float)viewportH);
  const ImVec2 embeddedImageMin = ImGui::GetCursorScreenPos();
  const ImVec2 embeddedImageMax(embeddedImageMin.x + embeddedViewportSize.x, embeddedImageMin.y + embeddedViewportSize.y);
  m_meshEditorViewportImageMinX = embeddedImageMin.x;
  m_meshEditorViewportImageMinY = embeddedImageMin.y;
  m_meshEditorViewportImageSizeX = embeddedViewportSize.x;
  m_meshEditorViewportImageSizeY = embeddedViewportSize.y;

  t850::BaseDriver* driver = pFramework->pVideoDriver;
  const int meshEditorGBufferRT = m_meshEditorGBufferTarget.Handle();
  const int meshEditorViewportRT = m_meshEditorViewportTarget.Handle();
  t850::BaseRT* gbufferRT = (meshEditorGBufferRT >= 0 &&
                             meshEditorGBufferRT < (int)driver->RTs.size())
      ? driver->RTs[meshEditorGBufferRT]
      : nullptr;
  t850::BaseRT* rt = (meshEditorViewportRT >= 0 &&
                      meshEditorViewportRT < (int)driver->RTs.size())
      ? driver->RTs[meshEditorViewportRT]
      : nullptr;
  if (!gbufferRT || gbufferRT->vColorTextures.size() < 7 || !gbufferRT->pDepthTexture ||
      !rt || rt->vColorTextures.empty() || !rt->vColorTextures[0]) {
    ImGui::TextDisabled("Mesh editor render targets are unavailable.");
    return;
  }

  if (!EnsureMeshEditorEmbeddedScene(obj)) {
    ImGui::TextDisabled("RagdollEditor scene host is unavailable.");
    return;
  }

  m_meshEditorScene->SetFinalOutputRT(m_meshEditorViewportTarget.Handle());
  m_meshEditorScene->SetRenderViewport(embeddedImageMin.x, embeddedImageMin.y, viewportW, viewportH);
  m_meshEditorScene->OnUpdate(m_dtSecs);
  m_meshEditorScene->OnDraw();

  ImTextureID embeddedImage = ImGuiTextureID(driver, rt->vColorTextures[0]);
  if (!embeddedImage) {
    ImGui::TextDisabled("Mesh editor viewport texture is not available for ImGui.");
    return;
  }
  const bool embeddedFlipV = driver->NeedsVFlip();
  const ImVec2 embeddedUv0(0.0f, embeddedFlipV ? 1.0f : 0.0f);
  const ImVec2 embeddedUv1(1.0f, embeddedFlipV ? 0.0f : 1.0f);
  ImGui::GetWindowDrawList()->AddImage(embeddedImage, embeddedImageMin, embeddedImageMax, embeddedUv0, embeddedUv1);
  ImGui::InvisibleButton("##MeshEditorViewportInput",
                         embeddedViewportSize,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  m_meshEditorViewportInputActive = ImGui::IsItemHovered() || ImGui::IsItemActive();
}

void EditorApp::DrawMeshEditorWindow() {
  if (!m_meshEditorOpen) {
    return;
  }

  ImGuiSetNextNativeEditorWindow(128.0f, 128.0f, 920.0f, 720.0f);
  m_meshEditorOpenRequested = false;

  bool keepOpen = m_meshEditorOpen;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  if (!ImGui::Begin("Mesh Edit", &keepOpen, ImGuiWindowFlags_NoDocking)) {
    ImGui::End();
    ImGui::PopStyleVar();
    if (!keepOpen) {
      m_meshEditorOpen = false;
      m_meshEditorCloseRequested = true;
      m_meshEditorViewportInputActive = false;
    }
    return;
  }

  if (!keepOpen) {
    ImGui::End();
    ImGui::PopStyleVar();
    m_meshEditorOpen = false;
    m_meshEditorCloseRequested = true;
    m_meshEditorViewportInputActive = false;
    return;
  }

  if (ImGuiViewport* windowViewport = ImGui::GetWindowViewport()) {
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    void* nativeHandle = NativeHandleFromImGuiViewport(windowViewport);
    m_meshEditorNativeHandle = nativeHandle;
    m_meshEditorImGuiViewportId = (unsigned int)windowViewport->ID;
    m_meshEditorViewportPosX = windowViewport->Pos.x;
    m_meshEditorViewportPosY = windowViewport->Pos.y;
    m_meshEditorViewportSizeX = windowViewport->Size.x;
    m_meshEditorViewportSizeY = windowViewport->Size.y;

    if (mainViewport && windowViewport->ID == mainViewport->ID) {
      if (!m_meshEditorMainViewportLogged) {
        ImGuiIO& io = ImGui::GetIO();
        T8_LOG_INFO("[T8ditor] Native editor window pending title='Mesh Edit': still merged with main viewport id=0x%08X hwnd=%p configFlags=0x%08X backendFlags=0x%08X",
                    (unsigned int)windowViewport->ID,
                    nativeHandle,
                    (unsigned int)io.ConfigFlags,
                    (unsigned int)io.BackendFlags);
        m_meshEditorMainViewportLogged = true;
      }
    } else if (nativeHandle && nativeHandle != m_meshEditorLoggedNativeHandle) {
      T8_LOG_INFO("[T8ditor] Native editor window created title='Mesh Edit' viewportId=0x%08X sdlWindow=%p hwnd=%p pos=(%.1f, %.1f) size=(%.1f, %.1f) flags=0x%08X",
                  (unsigned int)windowViewport->ID,
                  windowViewport->PlatformHandle,
                  nativeHandle,
                  windowViewport->Pos.x,
                  windowViewport->Pos.y,
                  windowViewport->Size.x,
                  windowViewport->Size.y,
                  (unsigned int)windowViewport->Flags);
      m_meshEditorLoggedNativeHandle = nativeHandle;
      m_meshEditorMainViewportLogged = false;
    }
  }

  if (m_meshEditorObjectIndex < 0 || m_meshEditorObjectIndex >= (int)g_objects.size()) {
    ImGui::TextWrapped("The mesh editor selection is no longer valid.");
    ImGui::End();
    ImGui::PopStyleVar();
    return;
  }

  SceneObject& obj = g_objects[m_meshEditorObjectIndex];
  m_meshEditorViewportInputActive = false;
  m_meshEditorDockspaceId = (unsigned int)ImGui::GetID("MeshEditDockSpace");
  m_meshEditorDockClassId = (unsigned int)ImGui::GetID("MeshEditDockClass");
  ImGuiWindowClass meshEditClass{};
  meshEditClass.ClassId = (ImGuiID)m_meshEditorDockClassId;
  meshEditClass.DockingAllowUnclassed = false;
  ImGui::DockSpace(
      (ImGuiID)m_meshEditorDockspaceId,
      ImVec2(0.0f, 0.0f),
      ImGuiDockNodeFlags_None,
      &meshEditClass);

  ImGui::End();
  ImGui::PopStyleVar();

  if (m_meshEditorImGuiViewportId != 0) {
    ImGui::SetNextWindowViewport((ImGuiID)m_meshEditorImGuiViewportId);
  }
  ImGui::SetNextWindowClass(&meshEditClass);
  if (m_meshEditorDockspaceId != 0) {
    ImGui::SetNextWindowDockID((ImGuiID)m_meshEditorDockspaceId, ImGuiCond_FirstUseEver);
  }
  bool viewportOpen = true;
  if (ImGui::Begin("Mesh Edit Viewport##MeshEditSceneViewport",
                   &viewportOpen,
                   ImGuiWindowFlags_NoCollapse)) {
    DrawMeshEditorViewport(obj);
  }
  ImGui::End();

  if (m_meshEditorGuiVisible && m_meshEditorScene && m_meshEditorSceneLoaded) {
    ImGuiViewport* fallbackViewport = ImGui::GetMainViewport();
    const float baseX = m_meshEditorViewportSizeX > 0.0f ? m_meshEditorViewportPosX : fallbackViewport->WorkPos.x;
    const float baseY = m_meshEditorViewportSizeY > 0.0f ? m_meshEditorViewportPosY : fallbackViewport->WorkPos.y;
    const float baseW = m_meshEditorViewportSizeX > 0.0f ? m_meshEditorViewportSizeX : fallbackViewport->WorkSize.x;
    const float baseH = m_meshEditorViewportSizeY > 0.0f ? m_meshEditorViewportSizeY : fallbackViewport->WorkSize.y;
    const float panelW = (std::min)(420.0f, (std::max)(320.0f, baseW - 48.0f));
    const float panelH = (std::min)(680.0f, (std::max)(320.0f, baseH - 48.0f));
    const ImVec2 panelPos(
        (std::max)(baseX + 24.0f, baseX + baseW - panelW - 24.0f),
        baseY + 24.0f);
    if (m_meshEditorImGuiViewportId != 0) {
      ImGui::SetNextWindowViewport((ImGuiID)m_meshEditorImGuiViewportId);
    }
    ImGui::SetNextWindowPos(panelPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_FirstUseEver);

    t850::DevGuiContext gui;
    gui.SetViewportId((ImGuiID)m_meshEditorImGuiViewportId);
    gui.SetIdSuffix("MeshEditRagdollEditor");
    gui.SetDockId((ImGuiID)m_meshEditorDockspaceId);
    gui.SetWindowClassId((ImGuiID)m_meshEditorDockClassId);
    const bool panelBegun = gui.BeginPanel("Scene Controls", &m_meshEditorGuiVisible);
    if (panelBegun) {
      ImGui::TextDisabled("Mesh Edit modal - press G to hide/show controls.");
      ImGui::Separator();
      m_meshEditorScene->DrawDevGui(gui);
    }
    gui.EndPanel();
  }
}

void EditorApp::InitVars() {
  m_dtTimer.Init();
  m_dtTimer.Update();
  m_dtSecs = 0.0f;
  m_firstFrame = true;
  T8_LOG_INFO("[T8ditor] EditorApp::InitVars");
}

void EditorApp::CreateAssets() {
  if (m_assetsCreated) return;
  if (!pFramework || !pFramework->pVideoDriver) {
    T8_LOG_ERROR("[T8ditor] CreateAssets called before driver init");
    return;
  }

  ImGuiLogCaptureStart();
  t850::LoadingProgress::Reset(100.0f, "Starting editor", "Preparing renderer");
  {
    auto lastLoadingLine = std::make_shared<std::string>();
    t850::LoadingProgress::SetFrameCallback([lastLoadingLine]() {
      const t850::LoadingProgress::Snapshot snapshot = t850::LoadingProgress::GetSnapshot();
      if (snapshot.detail.empty() && snapshot.percent < 99.5f) {
        return;
      }
      const std::string line = FormatLoadingProgressForConsole(snapshot);
      if (!line.empty() && line != *lastLoadingLine) {
        *lastLoadingLine = line;
        T8_LOG_INFO("%s", line.c_str());
      }
    });
  }

  const auto& desc = pFramework->aplicationDescriptor;
  const int w = (int)desc.width;
  const int h = (int)desc.height;

  {
    LogEditorLoadingLabel("Initializing editor", "Camera and viewport");
    t850::LoadingProgress::ScopedStep cameraStep("Initializing editor", "Camera and viewport", 8.0f);
    m_camera.Init(w, h, 50.0f);
    m_camera.SetTarget(XVECTOR3(0.0f, 0.0f, 0.0f));
    m_camera.Frame();
    m_lastW = w;
    m_lastH = h;
  }

  {
    LogEditorLoadingLabel("Initializing editor", "Viewport helpers");
    t850::LoadingProgress::ScopedStep helpersStep("Initializing editor", "Viewport helpers", 12.0f);
    if (!m_lines.Create())
      T8_LOG_ERROR("[T8ditor] EditorLineRenderer::Create failed");
    m_grid.Create(10, 1.0f);
    m_gizmo.Create();
  }

  {
    LogEditorLoadingLabel("Initializing editor", "Scene properties");
    t850::LoadingProgress::ScopedStep scenePropsStep("Initializing editor", "Scene properties", 8.0f);
    m_sceneProps.AddCamera(&m_camera.GetCameraMutable());
    m_editorLightCamera.InitPerspective(XVECTOR3(0.0f, 100.0f, 10.0f), Deg2Rad(45.0f), 1.0f, 10.0f, 500.0f);
    m_editorLightCamera.Speed = 10.0f;
    m_editorLightCamera.Eye = XVECTOR3(50.0f, 150.0f, -50.0f);
    m_editorLightCamera.Pitch = 1.0f;
    m_editorLightCamera.Roll = 0.0f;
    m_editorLightCamera.Yaw = -1.57f;
    m_editorLightCamera.Update(0.0f);
    m_sceneProps.AddLightCamera(&m_editorLightCamera);
    m_sceneProps.AddDirectionalLight(
      XVECTOR3(0.0f, -1.0f, 0.0f), XVECTOR3(1.0f, 1.0f, 1.0f), 1.5f, true);
    m_sceneProps.ActiveLights = 1;
    m_sceneProps.AmbientColor = XVECTOR3(0.15f, 0.15f, 0.15f);
    m_sceneProps.EnvFactor = 0.3f;  // reduced env reflections (no HDR tone mapping)
    if (m_editorSceneSetup.Load("Scenes/Quake3Mock.json")) {
      m_editorSceneSetup.ApplyQualityAndSettings(m_sceneProps);
      m_sceneProps.DeferredLightVolumesEnabled = true;
    } else {
      T8_LOG_ERROR("[T8ditor] Failed to load runtime render settings from Scenes/Quake3Mock.json");
    }
    m_editorShadowFilter.kernelSize = 4;
    m_editorShadowFilter.radius = 1.0f;
    m_editorShadowFilter.sigma = 1.0f;
    m_editorShadowFilter.Update();
    m_editorBloomFilter.kernelSize = 11;
    m_editorBloomFilter.radius = 2.5f;
    m_editorBloomFilter.sigma = 4.5f;
    m_editorBloomFilter.Update();
    m_editorDofFilter.kernelSize = 23;
    m_editorDofFilter.radius = 3.0f;
    m_editorDofFilter.sigma = 6.0f;
    m_editorDofFilter.Update();
    m_sceneProps.AddGaussKernel(&m_editorShadowFilter);
    m_sceneProps.AddGaussKernel(&m_editorBloomFilter);
    m_sceneProps.AddGaussKernel(&m_editorDofFilter);
    m_sceneProps.SSAOKernel.InitTexture();
    m_sceneProps.pCullingCamera = &m_camera.GetCameraMutable();
  }

  {
    LogEditorLoadingLabel("Initializing editor", "Physics and primitive managers");
    t850::LoadingProgress::ScopedStep physicsStep("Initializing editor", "Physics and primitive managers", 12.0f);
    XMatIdentity(m_vp);
    t850::GetEngineContext().physics = &m_physics;
    if (!m_physics.Initialize() && m_physics.IsAvailable()) {
      T8_LOG_ERROR("[T8ditor] Physics runtime failed to initialize");
    }
    m_primMgr.SetEngineContext(&t850::GetEngineContext());
    m_primMgr.Init();
    m_primMgr.SetVP(&m_vp);
    m_primMgr.SetSceneProps(&m_sceneProps);
    if (!m_physicsDebug.Create()) {
      T8_LOG_ERROR("[T8ditor] Physics debug renderer failed to initialize");
    } else {
      m_physicsDebug.SetDepthTestEnabled(false);
    }
  }

  {
    LogEditorLoadingLabel("Loading editor assets", "Skybox");
    t850::LoadingProgress::ScopedStep skyboxStep("Loading editor assets", "Skybox", 18.0f);
    if (std::filesystem::exists("Models/SkyBox.glb")) {
      g_skyboxMgr.SetEngineContext(&t850::GetEngineContext());
      g_skyboxMgr.Init();
      g_skyboxMgr.SetVP(&m_vp);
      g_skyboxMgr.SetSceneProps(&m_sceneProps);
      int sid = g_skyboxMgr.CreateMesh("Models/SkyBox.glb");
      if (sid >= 0) {
        g_skyboxPrimId = sid;
        g_skyboxInst.CreateInstance(g_skyboxMgr.GetPrimitive(sid), &m_vp);
        g_skyboxInst.Update();
        g_skyboxMgr.SetSceneProps(&m_sceneProps);
        g_skyboxReady = true;
      }
    }
  }

  {
    LogEditorLoadingLabel("Loading editor assets", "Render graph");
    t850::LoadingProgress::ScopedStep graphStep("Loading editor assets", "Render graph", 24.0f);
    if (g_renderGraph.Load("Scenes/T8ditor_RenderGraph.json")) {
      g_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, m_sceneProps);
      XMatIdentity(g_quadVP);
      for (int i = 0; i < 8; ++i) {
        g_quads[i].CreateInstance(m_primMgr.GetPrimitive(t850::PrimitiveManager::QUAD), &g_quadVP);
        g_quads[i].Update();
      }
      // Bind the G-buffer textures to quads[0] — the deferred lighting quad reads from these
      if (!pFramework->pVideoDriver->RTs.empty()) {
        auto* gbufferRT = pFramework->pVideoDriver->RTs[0];
        for (int j = 0; j < (int)gbufferRT->vColorTextures.size() && j < 4; ++j)
          g_quads[0].SetTexture(gbufferRT->vColorTextures[j], j);
        if (gbufferRT->vColorTextures.size() > 4)
          g_quads[0].SetTexture(gbufferRT->vColorTextures[4], 9);
        if (gbufferRT->pDepthTexture)
          g_quads[0].SetTexture(gbufferRT->pDepthTexture, 4);
      }
      g_deferredReady = true;
      m_primMgr.SetSceneProps(&m_sceneProps); // re-set so QUAD gets pScProp

      // Create a 1x1 white texture for shadow slot (deferred shader reads
      // shadow from tex5; without it, Shadow=0 and everything multiplies to black)
      unsigned char white[4] = { 255, 255, 255, 255 };
      g_dummyWhiteTex = t850::T8Device->CreateTextureFromMemory(white, 1, 1, 4, "dummyWhite");

      // Load environment cubemap for skybox (matID=0 in deferred shader samples texEnv)
      g_dummyEnvMapIdx = t850::g_pBaseDriver->CreateTexture("sky/CubeMap_SkyWater.dds");
      if (g_dummyEnvMapIdx >= 0) {
        g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));
        T8_LOG_INFO("[T8ditor] Environment cubemap loaded");
      }

      T8_LOG_INFO("[T8ditor] Deferred render graph ready");
    } else {
      T8_LOG_ERROR("[T8ditor] Render graph load failed — using forward fallback");
    }
  }

  // Initialize frame dumper (space key to dump)
  {
    t850::FrameDumperConfig cfg;
    cfg.debugFrames = g_startupDumpFrame < 0;
    cfg.keepRunning = g_startupDumpFrame < 0;
    if (g_startupDumpFrame >= 0) {
      cfg.dumpEnabled = true;
      cfg.dumpByFrame = true;
      cfg.dumpFrame = g_startupDumpFrame;
    }
    g_dumper.Init(cfg);
    g_dumperInited = true;
  }

  if (!g_startupMeshPath.empty() && g_startupMeshPath != "Models/SkyBox.glb")
    ImportMesh(g_startupMeshPath);

  m_assetsCreated = true;

#ifdef OS_WINDOWS
  {
    auto* w32 = static_cast<t850::Win32Framework*>(pFramework);
    if (w32 && w32->m_pWindow)
      SDL_SetWindowResizable(w32->m_pWindow, true);
  }
#endif

  {
    LogEditorLoadingLabel("Loading editor UI", "ImGui panels");
    t850::LoadingProgress::ScopedStep imguiStep("Loading editor UI", "ImGui panels", 12.0f);
    m_imguiReady = ImGuiInit(pFramework, true);
  }
  if (!m_imguiReady)
    T8_LOG_ERROR("[T8ditor] ImGui init failed");
#ifdef OS_WINDOWS
  if (auto* w32 = static_cast<t850::Win32Framework*>(pFramework)) {
    if (w32->m_pWindow) {
      SDL_RaiseWindow(w32->m_pWindow);
      SDL_SetWindowMouseGrab(w32->m_pWindow, false);
      SDL_PumpEvents();
    }
  }
#endif
  IManager.xDelta = 0;
  IManager.yDelta = 0;
  IManager.scrollDelta = 0.0f;
  for (int i = 0; i < MAXMOUSEBUTTONS; ++i) {
    IManager.MouseButtonStates[0][i] = false;
    IManager.MouseButtonStates[1][i] = false;
  }

  t850::LoadingProgress::Complete("Editor ready", "T8ditor");
  t850::LoadingProgress::ClearFrameCallback();
  t850::LoadingProgress::Clear();
  T8_LOG_INFO("[T8ditor] CreateAssets done (%dx%d)", w, h);
}

void EditorApp::ImportMesh(const std::string& path) {
  const std::string meshPath = NormalizeEditorResourcePath(path);
  if (!t850::ResourceLocator::Instance().Exists(meshPath)) {
    T8_LOG_ERROR("[T8ditor] Mesh file not found: %s", meshPath.c_str());
    return;
  }

  // Create a new scene object (append, don't replace)
  int id = m_primMgr.CreateMesh(meshPath.c_str());
  if (id < 0) {
    T8_LOG_ERROR("[T8ditor] Failed to load mesh: %s", meshPath.c_str());
    return;
  }

  g_objects.emplace_back();
  SceneObject& obj = g_objects.back();
  obj.primId = id;
  obj.name   = meshPath;
  obj.meshPath = meshPath;
  obj.litInst.CreateInstance(m_primMgr.GetPrimitive(id), &m_vp);
  obj.litInst.Update();
  m_renderResources.RegisterMesh(meshPath, obj.litInst.pBase, id);
  if (obj.litInst.GetSkinnedMesh()) {
    obj.ragdollModelKey = t850::BuildRagdollEditModelKey(meshPath);
    obj.ragdollResourcePath = t850::BuildRagdollEditResourcePath(meshPath);
  } else {
    obj.ragdollModelKey.clear();
    obj.ragdollResourcePath.clear();
  }

  m_primMgr.SetSceneProps(&m_sceneProps);

  obj.wireframe.Load(meshPath);

  // Select the newly imported mesh
  g_selectedIdx = (int)g_objects.size() - 1;

  // Frame the camera on it
  m_camera.SetTarget(obj.wireframe.LocalCenter());
  m_camera.Frame();

  T8_LOG_INFO("[T8ditor] Loaded mesh [%d]: %s", g_selectedIdx, meshPath.c_str());
}

void EditorApp::CloneSelected() {
  if (g_selectedIdx < 0) return;

  auto makeUniqueObjectName = [](const std::string& base) {
    const std::string root = base.empty() ? "Object" : base;
    std::string candidate = root + " Clone";
    int suffix = 2;
    auto exists = [](const std::string& name) {
      return std::any_of(g_objects.begin(), g_objects.end(),
        [&](const SceneObject& obj) { return obj.name == name; });
    };
    while (exists(candidate)) {
      candidate = root + " Clone " + std::to_string(suffix++);
    }
    return candidate;
  };

  auto makeUniqueCameraName = [](const std::string& base) {
    const std::string root = base.empty() ? "Camera" : base;
    std::string candidate = root + " Clone";
    int suffix = 2;
    auto exists = [](const std::string& name) {
      return std::any_of(g_cameras.begin(), g_cameras.end(),
        [&](const SceneCamera& cam) { return cam.name == name; });
    };
    while (exists(candidate)) {
      candidate = root + " Clone " + std::to_string(suffix++);
    }
    return candidate;
  };

  auto makeUniqueLightName = [](const std::string& base) {
    const std::string root = base.empty() ? "Light" : base;
    std::string candidate = root + " Clone";
    int suffix = 2;
    auto exists = [](const std::string& name) {
      return std::any_of(g_lights.begin(), g_lights.end(),
        [&](const SceneLight& light) { return light.name == name; });
    };
    while (exists(candidate)) {
      candidate = root + " Clone " + std::to_string(suffix++);
    }
    return candidate;
  };

  if (g_selectionType == 0 && g_selectedIdx < (int)g_objects.size()) {
    SceneObject& src = g_objects[g_selectedIdx];
    const std::string meshPath = src.meshPath.empty() ? src.name : src.meshPath;
    const std::string sourceName = src.name;
    const std::string name = makeUniqueObjectName(sourceName);
    const std::string ragdollResourcePath = src.ragdollResourcePath;
    const bool visible = src.visible;
    const std::optional<bool> mobileVisible = src.mobileVisible;
    const bool frozen = src.frozen;
    const bool showWire = src.showWire;
    const std::optional<float> navAgentFrontYawOffsetDeg = src.navAgentFrontYawOffsetDeg;
    const std::optional<float> navAgentFaceYawSign = src.navAgentFaceYawSign;
    const std::optional<t850::scene::SceneObjectPhysicsDesc> physicsMeta = src.physics;
    const std::optional<t850::scene::SceneObjectNavigationDesc> navigationMeta = src.navigation;
    const std::optional<t850::scene::SceneObjectRagdollDesc> ragdollMeta = src.ragdollAuthoringMeta;
    const XVECTOR3 position = src.wireframe.Position();
    const XVECTOR3 rotation = src.wireframe.EulerRadians();
    const XVECTOR3 scale = src.wireframe.Scale();
    const bool sourceAuthoringReady = src.ragdollAuthoringReady;
    const bool sourceLoadedFromAsset = src.ragdollLoadedFromAsset;
    const bool sourceRuntimeRagdoll = src.litInst.HasPhysicsRagdoll();
    const bool sourcePreviewEnabled = src.ragdollPreviewEnabled && sourceRuntimeRagdoll;
    const bool sourceSimulating = src.ragdollSimulating;
    const bool sourceDebugDraw = src.ragdollDebugDraw;
    const bool sourceWantsRuntimeCapsules = sourceRuntimeRagdoll || sourceDebugDraw || sourcePreviewEnabled;
    const int sourceBodyCount = src.ragdollBodyCount;
    const std::string sourceStatus = src.ragdollStatus;
    const t850::PhysicsRagdollAuthoringDesc sourceAuthoring = src.ragdollAuthoring;

    if (meshPath.empty() || !t850::ResourceLocator::Instance().Exists(meshPath)) {
      T8_LOG_ERROR("[T8ditor] Cannot clone mesh '%s': source mesh path is missing or unreadable",
                   sourceName.c_str());
      return;
    }

    int id = m_primMgr.CreateMesh(meshPath.c_str());
    if (id < 0) {
      T8_LOG_ERROR("[T8ditor] Failed to clone mesh: %s", meshPath.c_str());
      return;
    }
    m_primMgr.SetSceneProps(&m_sceneProps);

    g_objects.emplace_back();
    SceneObject& clone = g_objects.back();
    clone.primId = id;
    clone.name = name;
    clone.meshPath = meshPath;
    clone.visible = visible;
    clone.mobileVisible = mobileVisible;
    clone.frozen = frozen;
    clone.showWire = showWire;
    clone.navAgentFrontYawOffsetDeg = navAgentFrontYawOffsetDeg;
    clone.navAgentFaceYawSign = navAgentFaceYawSign;
    clone.physics = physicsMeta;
    clone.navigation = navigationMeta;
    clone.ragdollAuthoringMeta = ragdollMeta;
    clone.ragdollResourcePath = ragdollResourcePath;
    clone.ragdollDebugDraw = sourceDebugDraw;
    clone.litInst.CreateInstance(m_primMgr.GetPrimitive(id), &m_vp);
    clone.litInst.Update();
    m_renderResources.RegisterMesh(meshPath, clone.litInst.pBase, id);
    if (clone.litInst.GetSkinnedMesh()) {
      clone.ragdollModelKey = t850::BuildRagdollEditModelKey(meshPath);
      if (clone.ragdollResourcePath.empty())
        clone.ragdollResourcePath = t850::BuildRagdollEditResourcePath(meshPath);
    } else {
      clone.ragdollModelKey.clear();
      clone.ragdollResourcePath.clear();
    }
    clone.wireframe.Load(meshPath);
    clone.wireframe.Position() = position;
    clone.wireframe.EulerRadians() = rotation;
    clone.wireframe.Scale() = scale;
    if (clone.litInst.GetSkinnedMesh() && sourceAuthoringReady) {
      clone.ragdollAuthoring = sourceAuthoring;
      clone.ragdollAuthoringReady = true;
      clone.ragdollAuthoringTried = true;
      clone.ragdollLoadedFromAsset = sourceLoadedFromAsset;
      clone.ragdollBodyCount = sourceBodyCount;
      clone.ragdollStatus = sourceStatus.empty() ? "Cloned ragdoll authoring." : sourceStatus;
    }

    g_selectedIdx = (int)g_objects.size() - 1;
    g_selectionType = 0;
    g_multiSelect.clear();
    SyncSceneObjectTransforms();
    if (clone.litInst.GetSkinnedMesh() && !clone.ragdollAuthoringReady) {
      EnsureObjectRagdollAuthoring(clone);
    }
    if (clone.ragdollAuthoringReady && sourceWantsRuntimeCapsules) {
      if (sourceSimulating) {
        StartObjectRagdollSimulation(clone);
      } else {
        RecreateObjectRagdoll(clone, t850::PhysicsBodyMotion::Kinematic);
      }
    }
    T8_LOG_INFO("[T8ditor] Cloned mesh '%s' as '%s'", sourceName.c_str(), clone.name.c_str());
    return;
  }

  if (g_selectionType == 1 && g_selectedIdx < (int)g_cameras.size()) {
    SceneCamera clone = g_cameras[g_selectedIdx];
    clone.name = makeUniqueCameraName(clone.name);
    g_cameras.push_back(clone);
    g_selectedIdx = (int)g_cameras.size() - 1;
    g_selectionType = 1;
    g_multiSelect.clear();
    T8_LOG_INFO("[T8ditor] Cloned camera '%s'", clone.name.c_str());
    return;
  }

  if (g_selectionType == 2 && g_selectedIdx < (int)g_lights.size()) {
    SceneLight clone = g_lights[g_selectedIdx];
    clone.name = makeUniqueLightName(clone.name);
    g_lights.push_back(clone);
    g_selectedIdx = (int)g_lights.size() - 1;
    g_selectionType = 2;
    g_multiSelect.clear();
    T8_LOG_INFO("[T8ditor] Cloned light '%s'", clone.name.c_str());
  }
}

void EditorApp::LoadAssets() {}

void EditorApp::DestroyAssets() {
  ClosePlayScene(false);
  CloseMeshEditor();
  DestroyEditorFrozenFrameTarget();
  DestroyMeshEditorViewportTarget();
  DestroyMeshEditorSceneResources();
  DestroyRagdollEditorViewportTarget();
  if (m_imguiReady) {
    ImGuiLogCaptureStop();
    ImGuiShutdown();
    m_imguiReady = false;
  }
  // Release textures created via CreateTextureFromMemory (not tracked by driver)
  if (g_dummyWhiteTex) { g_dummyWhiteTex->release(); g_dummyWhiteTex = nullptr; }
  // g_dummyEnvMapIdx is tracked in the driver's Textures vector and destroyed by DestroyTextures()
  g_dummyEnvMapIdx = -1;
  m_sceneProps.SSAOKernel.Destroy();

  DestroyAllObjectRagdolls();
  m_physicsDebug.Destroy();
  m_primMgr.DestroyPrimitives();
  g_objects.clear();
  g_cameras.clear();
  g_lights.clear();
  g_selectedIdx = -1;
  g_selectionType = 0;
  g_activeCameraIdx = -1;
  g_multiSelect.clear();
  g_groups.clear();
  g_activeGroupIdx = -1;
  g_loadedSceneFile = SceneFile{};
  g_hasLoadedSceneFile = false;
  g_unloadedSceneObjects.clear();
  g_sceneCollisionResourcePath.clear();
  g_sceneProfiles.clear();
  g_undoStack.Clear();
  if (g_skyboxReady) {
    g_skyboxMgr.DestroyPrimitives();
    g_skyboxPrimId = -1;
    g_skyboxReady = false;
  }
  t850::MeshAssetCache::Get().Clear();
  m_gizmo.Destroy();
  m_grid.Destroy();
  m_lines.Destroy();
  m_physics.Shutdown();
  if (t850::GetEngineContext().physics == &m_physics)
    t850::GetEngineContext().physics = nullptr;
  m_assetsCreated = false;
  T8_LOG_INFO("[T8ditor] DestroyAssets");
}

void EditorApp::CheckResize() {
#ifdef OS_WINDOWS
  auto* w32 = static_cast<t850::Win32Framework*>(pFramework);
  if (!w32 || !w32->m_pWindow) return;
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(w32->m_pWindow, &w, &h);
  if (w > 0 && h > 0 && (w != m_lastW || h != m_lastH)) {
    if (pFramework->pVideoDriver->ResizeSwapchain(w, h)) {
      m_lastW = w;
      m_lastH = h;
      m_camera.SetViewportSize(w, h);
      DestroyEditorFrozenFrameTarget();
      pFramework->aplicationDescriptor.width  = w;
      pFramework->aplicationDescriptor.height = h;

      // Recreate deferred render targets at new resolution
      if (g_deferredReady) {
        T8_LOG_INFO("[T8ditor] Resize: flushing GPU before RT recreation");
        pFramework->pVideoDriver->FlushGPUResources();
        T8_LOG_INFO("[T8ditor] Resize: destroying old RTs");
        // Destroy old RTs
        pFramework->pVideoDriver->DestroyRTs();
        T8_LOG_INFO("[T8ditor] Resize: creating new RTs at %dx%d", w, h);
        // Recreate at new size (CreateRT with w=0,h=0 uses driver width/height)
        g_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, m_sceneProps);
        T8_LOG_INFO("[T8ditor] Resize: rebinding textures");
        // Rebind G-buffer textures to quads
        if (!pFramework->pVideoDriver->RTs.empty()) {
          auto* gbufferRT = pFramework->pVideoDriver->RTs[0];
          for (int j = 0; j < (int)gbufferRT->vColorTextures.size() && j < 4; ++j)
            g_quads[0].SetTexture(gbufferRT->vColorTextures[j], j);
          if (gbufferRT->vColorTextures.size() > 4)
            g_quads[0].SetTexture(gbufferRT->vColorTextures[4], 9);
          if (gbufferRT->pDepthTexture)
            g_quads[0].SetTexture(gbufferRT->pDepthTexture, 4);
        }
        if (g_dummyWhiteTex)
          g_quads[0].SetTexture(g_dummyWhiteTex, 5);
        if (g_dummyEnvMapIdx >= 0)
          g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));
        T8_LOG_INFO("[T8ditor] Render targets recreated at %dx%d", w, h);
      }
    }
  }
#endif
}

void EditorApp::RenderLoadingProgressFrame() {
  if (!pFramework || !pFramework->pVideoDriver || !m_imguiReady)
    return;

  t850::BaseDriver* drv = pFramework->pVideoDriver;
  const t850::LoadingProgress::Snapshot snapshot = t850::LoadingProgress::GetSnapshot();
  if (!snapshot.active)
    return;

  drv->BeginFrame();
  drv->Clear();

  ImGuiNewFrame();

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 viewportSize = viewport ? viewport->Size : ImVec2((float)m_lastW, (float)m_lastH);
  const ImVec2 center = viewport ? viewport->GetCenter() : ImVec2(viewportSize.x * 0.5f, viewportSize.y * 0.5f);
  const ImGuiStyle& style = ImGui::GetStyle();
  const std::string phaseText = snapshot.phase.empty() ? "Loading" : snapshot.phase;
  const std::string itemText = snapshot.item;
  const std::string detailText = snapshot.detail;

  auto measureText = [](const std::string& text) -> float {
    return text.empty() ? 0.0f : ImGui::CalcTextSize(text.c_str()).x;
  };

  const float maxViewportWidth = (std::max)(420.0f, viewportSize.x * 0.85f);
  const float desiredTextWidth = (std::max)({
      measureText(phaseText),
      measureText(itemText),
      measureText(detailText),
      360.0f
  });
  const float windowWidth = std::clamp(desiredTextWidth + style.WindowPadding.x * 2.0f + 32.0f,
                                       420.0f,
                                       maxViewportWidth);
  const float contentWidth = windowWidth - style.WindowPadding.x * 2.0f;
  const float progressBarWidth = std::clamp(contentWidth * 0.86f, 320.0f, contentWidth);

  auto wrappedLineCount = [&](const std::string& text) -> int {
    if (text.empty()) {
      return 0;
    }
    const float textWidth = measureText(text);
    return (std::max)(1, static_cast<int>(std::ceil(textWidth / (std::max)(1.0f, contentWidth))));
  };

  const int labelLines =
      wrappedLineCount(phaseText) +
      wrappedLineCount(itemText) +
      wrappedLineCount(detailText);
  const float progressBarHeight = ImGui::GetFrameHeight();
  const float windowHeight =
      style.WindowPadding.y * 2.0f +
      ImGui::GetFrameHeight() +
      (std::max)(1, labelLines) * ImGui::GetTextLineHeightWithSpacing() +
      progressBarHeight +
      style.ItemSpacing.y * 4.0f;

  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings;

  if (ImGui::Begin("Loading Scene", nullptr, flags)) {
    auto drawCenteredText = [](const std::string& text, bool disabled = false) {
      if (text.empty()) {
        return;
      }
      const float avail = ImGui::GetContentRegionAvail().x;
      std::size_t begin = 0;
      while (begin < text.size()) {
        std::size_t end = text.size();
        while (end > begin + 1 &&
               ImGui::CalcTextSize(text.substr(begin, end - begin).c_str()).x > avail) {
          --end;
        }

        std::string line = text.substr(begin, end - begin);
        const float textWidth = ImGui::CalcTextSize(line.c_str()).x;
        if (textWidth < avail) {
          ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - textWidth) * 0.5f);
        }
        if (disabled) {
          ImGui::TextDisabled("%s", line.c_str());
        } else {
          ImGui::TextUnformatted(line.c_str());
        }

        begin = end;
        while (begin < text.size() && text[begin] == ' ') {
          ++begin;
        }
      }
    };

    drawCenteredText(phaseText);
    drawCenteredText(itemText);
    drawCenteredText(detailText, true);

    ImGui::Spacing();
    const float fraction = std::clamp(snapshot.percent / 100.0f, 0.0f, 1.0f);
    char percentText[32] = {};
    std::snprintf(percentText, sizeof(percentText), "%.0f%%", snapshot.percent);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - progressBarWidth) * 0.5f);
    ImGui::ProgressBar(fraction, ImVec2(progressBarWidth, progressBarHeight), percentText);
  }
  ImGui::End();

  if (m_panels.showConsole) {
    ImGuiDrawConsolePanel();
  }

  ImGuiRender();
  drv->SwapBuffers();
  drv->EndFrame();
}

void EditorApp::OnUpdate() {
  m_dtTimer.Update();
  m_dtSecs = m_dtTimer.GetDTSecs();
  if (m_firstFrame) { m_dtSecs = 1.0f / 60.0f; m_firstFrame = false; }
  m_sceneProps.FrameDeltaSec = m_dtSecs;

  if (m_meshEditorCloseRequested) {
    CloseMeshEditor();
  }
  if (m_playSceneCloseRequested) {
    ClosePlayScene();
  }

  // Execute deferred scene load BEFORE any GPU work this frame
  if (!g_pendingLoadPath.empty()) {
    const std::string loadPath = g_pendingLoadPath;
    auto lastLoadingLine = std::make_shared<std::string>();
    auto loadingCallback = [this, lastLoadingLine]() {
      const t850::LoadingProgress::Snapshot snapshot = t850::LoadingProgress::GetSnapshot();
      const std::string line = FormatLoadingProgressForConsole(snapshot);
      if (!line.empty() && line != *lastLoadingLine) {
        *lastLoadingLine = line;
        T8_LOG_INFO("%s", line.c_str());
      }
      RenderLoadingProgressFrame();
    };

    t850::LoadingProgress::Reset(100.0f, "Loading scene", loadPath, "Reading scene file");
    t850::LoadingProgress::SetFrameCallback(loadingCallback);
    t850::LoadingProgress::RequestFrame(true);

    SceneFile sf;
    bool sceneLoaded = false;
    {
      t850::LoadingProgress::ScopedStep sceneFileStep("Loading scene", loadPath, 4.0f);
      sceneLoaded = LoadSceneFromFile(loadPath, sf);
    }

    if (sceneLoaded) {
      const float objectCount = (std::max)(1.0f, static_cast<float>(sf.objects.size()));
      const float totalWeight = 25.0f + objectCount * 20.0f;
      const float objectWeight = 12.0f;
      t850::LoadingProgress::Reset(totalWeight, "Loading scene", loadPath, "Preparing scene");
      t850::LoadingProgress::SetFrameCallback(loadingCallback);
      t850::LoadingProgress::RequestFrame(true);

      g_loadedSceneFile = sf;
      g_hasLoadedSceneFile = true;

      // Flush all GPU work from previous frames
      {
        t850::LoadingProgress::ScopedStep gpuStep("Preparing scene", "Waiting for GPU", 2.0f);
        pFramework->pVideoDriver->WaitForGPU();
      }

      // Destroy old scene
      {
        t850::LoadingProgress::ScopedStep cleanupStep("Preparing scene", "Clearing current scene", 3.0f);
        DestroyAllObjectRagdolls();
        m_primMgr.DestroyPrimitives();
        g_objects.clear();
        g_cameras.clear();
        g_lights.clear();
        g_selectedIdx = -1;
        g_selectionType = 0;
        g_activeCameraIdx = -1;
        g_multiSelect.clear();
        g_groups.clear();
        g_activeGroupIdx = -1;
        g_undoStack.Clear();
        g_unloadedSceneObjects.clear();
      }

      g_sceneCollisionResourcePath = ResolveSceneCollisionPath(sf, loadPath);
      g_sceneProfiles = sf.profiles;
      {
        t850::LoadingProgress::ScopedStep collisionStep("Loading scene", "Collision data", 4.0f);
        LoadSceneCollisionClip(g_sceneCollisionResourcePath);
      }

      {
        t850::LoadingProgress::ScopedStep managerStep("Preparing scene", "Primitive manager", 3.0f);
        m_primMgr.SetEngineContext(&t850::GetEngineContext());
        m_primMgr.Init();
        m_primMgr.SetVP(&m_vp);
        m_primMgr.SetSceneProps(&m_sceneProps);
      }

      // Recreate deferred quads from fresh QUAD primitive
      if (g_deferredReady) {
        t850::LoadingProgress::ScopedStep graphStep("Preparing scene", "Render graph quads", 3.0f);
        for (int i = 0; i < 8; ++i) {
          g_quads[i].CreateInstance(m_primMgr.GetPrimitive(t850::PrimitiveManager::QUAD), &g_quadVP);
          g_quads[i].Update();
        }
        if (!pFramework->pVideoDriver->RTs.empty()) {
          auto* gbufferRT = pFramework->pVideoDriver->RTs[0];
          for (int j = 0; j < (int)gbufferRT->vColorTextures.size() && j < 4; ++j)
            g_quads[0].SetTexture(gbufferRT->vColorTextures[j], j);
          if (gbufferRT->vColorTextures.size() > 4)
            g_quads[0].SetTexture(gbufferRT->vColorTextures[4], 9);
          if (gbufferRT->pDepthTexture)
            g_quads[0].SetTexture(gbufferRT->pDepthTexture, 4);
        }
        if (g_dummyWhiteTex)
          g_quads[0].SetTexture(g_dummyWhiteTex, 5);
        if (g_dummyEnvMapIdx >= 0)
          g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));
        m_primMgr.SetSceneProps(&m_sceneProps);
      }

      // Load mesh objects
      for (std::size_t objectIndex = 0; objectIndex < sf.objects.size(); ++objectIndex) {
        const auto& od = sf.objects[objectIndex];
        const std::string meshPath = od.mesh.empty() ? od.name : od.mesh;
        t850::LoadingProgress::ScopedStep objectStep(
            "Loading scene object",
            od.name.empty() ? meshPath : od.name,
            objectWeight);
        t850::LoadingProgress::SetDetail(
            "Model " + std::to_string(objectIndex + 1) + "/" + std::to_string(sf.objects.size()) + ": " + meshPath);
        const std::size_t objectCountBeforeImport = g_objects.size();
        ImportMesh(meshPath);
        if (g_objects.size() > objectCountBeforeImport) {
          auto& obj = g_objects.back();
          obj.name = od.name;
          obj.meshPath = meshPath;
          obj.ragdollResourcePath = od.ragdoll;
          if (obj.litInst.GetSkinnedMesh()) {
            if (obj.ragdollResourcePath.empty()) {
              obj.ragdollResourcePath = t850::BuildRagdollEditResourcePath(meshPath);
            }
            obj.ragdollModelKey = t850::BuildRagdollEditModelKey(meshPath);
          } else {
            obj.ragdollModelKey.clear();
          }
          obj.wireframe.Position() = XVECTOR3(od.position.x, od.position.y, od.position.z);
          obj.wireframe.EulerRadians() = XVECTOR3(
            od.rotation.x * kDegToRad, od.rotation.y * kDegToRad, od.rotation.z * kDegToRad);
          obj.wireframe.Scale() = XVECTOR3(od.scale.x, od.scale.y, od.scale.z);
          obj.visible  = od.visible;
          obj.mobileVisible = od.mobile_visible;
          obj.frozen   = od.frozen;
          obj.showWire = od.show_wire;
          obj.navAgentFrontYawOffsetDeg = od.nav_agent_front_yaw_offset_deg;
          obj.navAgentFaceYawSign = od.nav_agent_face_yaw_sign;
          obj.physics = od.physics;
          obj.navigation = od.navigation;
          obj.ragdollAuthoringMeta = od.ragdoll_authoring;
          if (obj.ragdollAuthoringMeta) {
            obj.ragdollResourcePath = obj.ragdollAuthoringMeta->asset.empty()
                ? obj.ragdollResourcePath
                : obj.ragdollAuthoringMeta->asset;
            obj.ragdollPreviewEnabled = obj.ragdollAuthoringMeta->preview;
            obj.ragdollDriveFromAnimation = obj.ragdollAuthoringMeta->drive_from_animation;
            obj.ragdollSimulating = obj.ragdollAuthoringMeta->runtime_motion == "dynamic";
          }
        } else {
          g_unloadedSceneObjects.push_back(od);
          T8_LOG_ERROR("[T8ditor] Preserving unloaded scene object '%s' mesh='%s' for save",
                       od.name.c_str(),
                       meshPath.c_str());
        }
      }

      // Load cameras
      {
        t850::LoadingProgress::ScopedStep cameraStep("Loading scene", "Cameras", 2.0f);
        for (auto& cd : sf.cameras) {
          SceneCamera c;
          c.name = cd.name; c.type = (CameraType)cd.type;
          c.position = XVECTOR3(cd.position.x, cd.position.y, cd.position.z);
          c.target = XVECTOR3(cd.target.x, cd.target.y, cd.target.z);
          c.fovDeg = cd.fov_deg; c.orthoW = cd.ortho_w; c.orthoH = cd.ortho_h;
          c.nearPlane = cd.near_plane; c.farPlane = cd.far_plane;
          c.visible = cd.visible; c.frozen = cd.frozen;
          g_cameras.push_back(c);
        }
      }

      // Load lights
      {
        t850::LoadingProgress::ScopedStep lightsStep("Loading scene", "Lights", 3.0f);
        for (auto& ld : sf.lights) {
          SceneLight l;
          l.name = ld.name; l.type = (EditorLightType)ld.type;
          l.position = XVECTOR3(ld.position.x, ld.position.y, ld.position.z);
          l.direction = XVECTOR3(ld.direction.x, ld.direction.y, ld.direction.z);
          l.color = XVECTOR3(ld.color.x, ld.color.y, ld.color.z);
          l.intensity = ld.intensity; l.radius = ld.radius; l.enabled = ld.enabled;
          l.visible = ld.visible; l.frozen = ld.frozen;
          l.q3 = ld.q3;
          g_lights.push_back(l);
        }
      }

      // Restore editor state
      {
        t850::LoadingProgress::ScopedStep editorStateStep("Loading scene", "Editor state", 3.0f);
        m_panels.showSkybox    = sf.editor.show_skybox;
        m_panels.showWireframe = sf.editor.show_wireframe;
        m_camera.SetTarget(XVECTOR3(sf.editor.camera_target.x,
                                     sf.editor.camera_target.y,
                                     sf.editor.camera_target.z));
        m_camera.SetOrbitState(sf.editor.camera_yaw,
                               sf.editor.camera_pitch,
                               sf.editor.camera_distance);
        g_selectedIdx = -1;
      }

      t850::LoadingProgress::Complete("Scene loaded", loadPath);
    } else {
      t850::LoadingProgress::Complete("Scene load failed", loadPath);
    }
    t850::LoadingProgress::ClearFrameCallback();
    t850::LoadingProgress::Clear();
    g_pendingLoadPath.clear();
    if (!g_pendingDeleteAfterLoadPath.empty() && g_pendingDeleteAfterLoadPath == loadPath) {
      std::error_code ec;
      std::filesystem::remove(g_pendingDeleteAfterLoadPath, ec);
      g_pendingDeleteAfterLoadPath.clear();
    }
  }

  CheckResize();

  OnInput();
  if (!m_meshEditorOpen && !m_playSceneOpen) {
    UpdateSkinnedAnimationAndRagdolls();
  }
  OnDraw();
}

void EditorApp::OnInput() {
  const ImGuiIO& io = ImGui::GetIO();
  const bool imguiWantsMouse    = io.WantCaptureMouse;
  const bool imguiWantsKeyboard = io.WantCaptureKeyboard;

  if (!io.WantTextInput && IManager.PressedOnceKey(T800K_ESCAPE)) {
    if (m_meshEditorOpen) {
      m_meshEditorOpen = false;
      m_meshEditorCloseRequested = true;
      m_meshEditorViewportInputActive = false;
      IManager.xDelta = 0;
      IManager.yDelta = 0;
      return;
    }
    if (m_playSceneOpen) {
      m_playSceneOpen = false;
      m_playSceneCloseRequested = true;
      m_playSceneViewportInputActive = false;
      IManager.xDelta = 0;
      IManager.yDelta = 0;
      return;
    }
    if (m_ragdollEditorOpen) {
      CloseRagdollEditor();
      IManager.xDelta = 0;
      IManager.yDelta = 0;
      return;
    }
  }

  if (m_meshEditorOpen) {
    if (!io.WantTextInput && IManager.PressedOnceKey(T800K_g)) {
      m_meshEditorGuiVisible = !m_meshEditorGuiVisible;
      if (m_meshEditorScene && m_meshEditorSceneLoaded) {
        m_meshEditorScene->ResetViewInput();
      }
      IManager.xDelta = 0;
      IManager.yDelta = 0;
      return;
    }
    if (m_meshEditorScene && m_meshEditorSceneLoaded) {
      const int savedMouseX = IManager.mouseX;
      const int savedMouseY = IManager.mouseY;
      const ImVec2 globalMouse = ImGui::GetMousePos();
      const float globalMouseX = globalMouse.x;
      const float globalMouseY = globalMouse.y;
      const bool mouseOverViewportImage =
          m_meshEditorViewportImageSizeX > 0.0f &&
          m_meshEditorViewportImageSizeY > 0.0f &&
          globalMouseX >= m_meshEditorViewportImageMinX &&
          globalMouseY >= m_meshEditorViewportImageMinY &&
          globalMouseX < m_meshEditorViewportImageMinX + m_meshEditorViewportImageSizeX &&
          globalMouseY < m_meshEditorViewportImageMinY + m_meshEditorViewportImageSizeY;
      IManager.mouseX = static_cast<int>(std::lround(globalMouseX - m_meshEditorViewportImageMinX));
      IManager.mouseY = static_cast<int>(std::lround(globalMouseY - m_meshEditorViewportImageMinY));
      m_meshEditorScene->SetIgnoreImGuiMouseCaptureForInput(mouseOverViewportImage || m_meshEditorViewportInputActive);
      m_meshEditorScene->OnInput(&IManager);
      IManager.mouseX = savedMouseX;
      IManager.mouseY = savedMouseY;
    }
    return;
  }

  if (m_playSceneOpen) {
    const bool playGuiConsumesKeyboard =
        m_playSceneGuiVisible && (io.WantCaptureKeyboard || io.WantTextInput);
    if (!playGuiConsumesKeyboard && IManager.PressedOnceKey(T800K_g)) {
      m_playSceneGuiVisible = !m_playSceneGuiVisible;
      if (m_playScene && m_playSceneLoaded) {
        m_playScene->ResetViewInput();
      }
      IManager.xDelta = 0;
      IManager.yDelta = 0;
      return;
    }

    if (m_playScene && m_playSceneLoaded) {
      if (m_playSceneGuiVisible && io.WantTextInput) {
        IManager.xDelta = 0;
        IManager.yDelta = 0;
        m_playScene->ResetViewInput();
        return;
      }

      const int savedMouseX = IManager.mouseX;
      const int savedMouseY = IManager.mouseY;
      ImGuiViewport* mainViewport = ImGui::GetMainViewport();
      const float globalMouseX = (mainViewport ? mainViewport->Pos.x : 0.0f) + static_cast<float>(savedMouseX);
      const float globalMouseY = (mainViewport ? mainViewport->Pos.y : 0.0f) + static_cast<float>(savedMouseY);
      const bool mouseOverViewportImage =
          m_playSceneViewportImageSizeX > 0.0f &&
          m_playSceneViewportImageSizeY > 0.0f &&
          globalMouseX >= m_playSceneViewportImageMinX &&
          globalMouseY >= m_playSceneViewportImageMinY &&
          globalMouseX < m_playSceneViewportImageMinX + m_playSceneViewportImageSizeX &&
          globalMouseY < m_playSceneViewportImageMinY + m_playSceneViewportImageSizeY;
      IManager.mouseX = static_cast<int>(std::lround(globalMouseX - m_playSceneViewportImageMinX));
      IManager.mouseY = static_cast<int>(std::lround(globalMouseY - m_playSceneViewportImageMinY));
      m_playScene->SetIgnoreImGuiMouseCaptureForInput(mouseOverViewportImage || (!m_playSceneGuiVisible && m_playSceneViewportInputActive));
      m_playScene->OnInput(&IManager);
      IManager.mouseX = savedMouseX;
      IManager.mouseY = savedMouseY;
    }
    return;
  }

  if (!imguiWantsKeyboard) {
    const bool ctrlDown = IManager.PressedKey(T800K_LCTRL) || IManager.PressedKey(T800K_RCTRL);
    const bool shiftDown = IManager.PressedKey(T800K_LSHIFT) || IManager.PressedKey(T800K_RSHIFT);

    if (IManager.PressedOnceKey(T800K_q)) m_gizmo.SetMode(GizmoMode::Select);
    if (IManager.PressedOnceKey(T800K_w)) m_gizmo.SetMode(GizmoMode::Translate);
    if (IManager.PressedOnceKey(T800K_e)) m_gizmo.SetMode(GizmoMode::Rotate);
    if (IManager.PressedOnceKey(T800K_r)) m_gizmo.SetMode(GizmoMode::Scale);

    // Z key behavior:
    // Ctrl+Z = undo, Ctrl+Shift+Z = redo
    // Z alone: if mesh selected → frame camera on it; else reset camera
    if (IManager.PressedOnceKey(T800K_z)) {
      if (ctrlDown && shiftDown)
        g_undoStack.Redo();
      else if (ctrlDown)
        g_undoStack.Undo();
      else {
        FrameSelectedEntity();
      }
    }
    // Ctrl+Y also redoes
    if (ctrlDown && IManager.PressedOnceKey(T800K_y))
      g_undoStack.Redo();

    // Space key — dump frame (all RTs + snapshot)
    if (IManager.PressedOnceKey(T800K_SPACE) && g_dumperInited)
      g_dumper.RequestDump();
  }

  // Delete key — works even when ImGui panels have focus (but not during text input)
  if (!io.WantTextInput && IManager.PressedOnceKey(T800K_DELETE) && g_selectedIdx >= 0) {
    if (g_selectionType == 1 && g_selectedIdx < (int)g_cameras.size()) {
      if (g_activeCameraIdx == g_selectedIdx) g_activeCameraIdx = -1;
      else if (g_activeCameraIdx > g_selectedIdx) g_activeCameraIdx--;
      g_cameras.erase(g_cameras.begin() + g_selectedIdx);
      g_selectedIdx = -1;
      T8_LOG_INFO("[T8ditor] Camera deleted");
    }
    else if (g_selectionType == 2 && g_selectedIdx < (int)g_lights.size()) {
      g_lights.erase(g_lights.begin() + g_selectedIdx);
      g_selectedIdx = -1;
      T8_LOG_INFO("[T8ditor] Light deleted");
    }
    else if (g_selectionType == 0 && g_selectedIdx < (int)g_objects.size()) {
      DestroyObjectRagdoll(g_objects[g_selectedIdx]);
      g_objects.erase(g_objects.begin() + g_selectedIdx);
      g_selectedIdx = -1;
      T8_LOG_INFO("[T8ditor] Mesh deleted");
    }
  }

  float wheel = ImGuiConsumeWheelDelta();
  bool blockWheel = imguiWantsMouse && !ImGuizmo::IsOver();
  m_camera.Update(m_dtSecs, IManager,
                  blockWheel ? 0.0f : wheel,
                  imguiWantsMouse,
                  imguiWantsKeyboard);

  if (!imguiWantsKeyboard)
    ProcessSelectionInput();

  if (!imguiWantsMouse) {
    // Skip mouse pick while multi-select gizmo is active (avoid clearing selection)
    if (!(g_multiSelect.size() > 1 && m_gizmo.Mode() != GizmoMode::Select && ImGuizmo::IsOver()))
      HandleMousePick();
  }
}

void EditorApp::ProcessSelectionInput() {
  SceneObject* sel = SelectedObject();
  if (!sel || !sel->wireframe.IsLoaded()) return;

  const float linRate = 5.0f * m_dtSecs;
  const float rotRate = 1.5f * m_dtSecs;
  const float sclStep = 1.0f + 0.5f * m_dtSecs;

  XVECTOR3& pos = sel->wireframe.Position();
  XVECTOR3& eul = sel->wireframe.EulerRadians();
  XVECTOR3& scl = sel->wireframe.Scale();

  if (IManager.PressedKey(T800K_l)) pos.x += linRate;
  if (IManager.PressedKey(T800K_j)) pos.x -= linRate;
  if (IManager.PressedKey(T800K_u)) pos.y += linRate;
  if (IManager.PressedKey(T800K_o)) pos.y -= linRate;
  if (IManager.PressedKey(T800K_i)) pos.z += linRate;
  if (IManager.PressedKey(T800K_k)) pos.z -= linRate;

  if (IManager.PressedKey(T800K_LEFTBRACKET))  eul.y -= rotRate;
  if (IManager.PressedKey(T800K_RIGHTBRACKET)) eul.y += rotRate;

  if (IManager.PressedKey(T800K_QUOTE)) {
    scl.x *= sclStep; scl.y *= sclStep; scl.z *= sclStep;
  }
  if (IManager.PressedKey(T800K_SEMICOLON)) {
    scl.x /= sclStep; scl.y /= sclStep; scl.z /= sclStep;
  }
}

void EditorApp::FrameSelectedEntity() {
  auto frameSphere = [&](const XVECTOR3& center, float radius, const char* label) {
    m_camera.FrameBounds(center, radius);
    T8_LOG_INFO("[T8ditor] Framed %s at (%.2f, %.2f, %.2f), radius=%.2f",
                label ? label : "selection", center.x, center.y, center.z, radius);
  };

  if (g_selectionType == 0) {
    t850::AABB bounds;
    if (g_selectedIdx >= 0 &&
        g_selectedIdx < static_cast<int>(g_objects.size()) &&
        GetEditorObjectWorldAABB(g_objects[static_cast<std::size_t>(g_selectedIdx)],
                                 bounds)) {
      m_camera.FrameBounds(bounds);
      return;
    }
  } else if (g_selectionType == 1 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_cameras.size())) {
    const SceneCamera& camera = g_cameras[static_cast<std::size_t>(g_selectedIdx)];
    const XVECTOR3 center(
        (camera.position.x + camera.target.x) * 0.5f,
        (camera.position.y + camera.target.y) * 0.5f,
        (camera.position.z + camera.target.z) * 0.5f,
        1.0f);
    const float dx = camera.position.x - camera.target.x;
    const float dy = camera.position.y - camera.target.y;
    const float dz = camera.position.z - camera.target.z;
    frameSphere(center, (std::max)(1.0f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz)), "camera");
    return;
  } else if (g_selectionType == 2 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_lights.size())) {
    const SceneLight& light = g_lights[static_cast<std::size_t>(g_selectedIdx)];
    const float radius = light.type == EditorLightType::Omni
        ? (std::clamp)(light.radius, 1.0f, 100.0f)
        : 3.0f;
    frameSphere(light.position, radius, "light");
    return;
  }

  m_camera.ResetToDefault();
  T8_LOG_INFO("[T8ditor] Reset editor view");
}

// Project a world-space point to screen coordinates.
static ImVec2 WorldToScreen(const XVECTOR3& p, const XMATRIX44& vp, int w, int h) {
  // Row-vector: [x,y,z,1] * VP
  float cx = p.x*vp.m11 + p.y*vp.m21 + p.z*vp.m31 + vp.m41;
  float cy = p.x*vp.m12 + p.y*vp.m22 + p.z*vp.m32 + vp.m42;
  float cw = p.x*vp.m14 + p.y*vp.m24 + p.z*vp.m34 + vp.m44;
  if (std::abs(cw) < 1e-6f) return ImVec2(-1, -1);
  float ndcX = cx / cw;
  float ndcY = cy / cw;
  float sx = (ndcX * 0.5f + 0.5f) * w;
  float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * h;
  return ImVec2(sx, sy);
}

// Test if any part of a world AABB projects inside a screen rectangle.
static bool AABBInScreenRect(const t850::AABB& box, const XMATRIX44& vp,
                              int viewW, int viewH,
                              float rMinX, float rMinY, float rMaxX, float rMaxY) {
  float sMinX = 1e30f, sMinY = 1e30f, sMaxX = -1e30f, sMaxY = -1e30f;
  float bmin[3] = { box.vMin.x, box.vMin.y, box.vMin.z };
  float bmax[3] = { box.vMax.x, box.vMax.y, box.vMax.z };
  for (int c = 0; c < 8; c++) {
    float lx = (c & 1) ? bmax[0] : bmin[0];
    float ly = (c & 2) ? bmax[1] : bmin[1];
    float lz = (c & 4) ? bmax[2] : bmin[2];
    ImVec2 s = WorldToScreen(XVECTOR3(lx, ly, lz), vp, viewW, viewH);
    if (s.x < sMinX) sMinX = s.x;
    if (s.y < sMinY) sMinY = s.y;
    if (s.x > sMaxX) sMaxX = s.x;
    if (s.y > sMaxY) sMaxY = s.y;
  }
  // Overlap test
  return !(sMaxX < rMinX || sMinX > rMaxX || sMaxY < rMinY || sMinY > rMaxY);
}

static bool ProjectAABBToScreenRect(const t850::AABB& box, const XMATRIX44& vp,
                                    int viewW, int viewH,
                                    float& sMinX, float& sMinY,
                                    float& sMaxX, float& sMaxY) {
  if (!box.IsValid() || viewW <= 0 || viewH <= 0) {
    return false;
  }

  sMinX = 1e30f;
  sMinY = 1e30f;
  sMaxX = -1e30f;
  sMaxY = -1e30f;

  float bmin[3] = { box.vMin.x, box.vMin.y, box.vMin.z };
  float bmax[3] = { box.vMax.x, box.vMax.y, box.vMax.z };
  bool anyValid = false;
  for (int c = 0; c < 8; c++) {
    float lx = (c & 1) ? bmax[0] : bmin[0];
    float ly = (c & 2) ? bmax[1] : bmin[1];
    float lz = (c & 4) ? bmax[2] : bmin[2];
    ImVec2 s = WorldToScreen(XVECTOR3(lx, ly, lz), vp, viewW, viewH);
    if (!std::isfinite(s.x) || !std::isfinite(s.y) || s.x < -100000.0f || s.y < -100000.0f) {
      continue;
    }
    anyValid = true;
    if (s.x < sMinX) sMinX = s.x;
    if (s.y < sMinY) sMinY = s.y;
    if (s.x > sMaxX) sMaxX = s.x;
    if (s.y > sMaxY) sMaxY = s.y;
  }
  return anyValid && sMinX <= sMaxX && sMinY <= sMaxY;
}

static t850::Ray BuildEditorCameraRay(const ::Camera& camera,
                                      float mouseX,
                                      float mouseY,
                                      int viewW,
                                      int viewH) {
  const float safeW = (std::max)(1.0f, static_cast<float>(viewW));
  const float safeH = (std::max)(1.0f, static_cast<float>(viewH));
  const float ndcX = 2.0f * ((mouseX + 0.5f) / safeW) - 1.0f;
  const float ndcY = 1.0f - 2.0f * ((mouseY + 0.5f) / safeH);

  t850::Ray ray;
  ray.origin = camera.Eye;

  if (camera.Ortho) {
    const float halfW = camera.Width * 0.5f;
    const float halfH = camera.Height * 0.5f;
    ray.origin = camera.Eye + camera.Right * (ndcX * halfW) + camera.Up * (ndcY * halfH);
    ray.direction = camera.Look;
  } else {
    const float aspect = camera.AspectRatio > 0.0f ? camera.AspectRatio : safeW / safeH;
    const float tanHalfFov = std::tan((std::max)(0.01f, camera.Fov) * 0.5f);
    ray.direction = camera.Look
        + camera.Right * (ndcX * tanHalfFov * aspect)
        + camera.Up * (ndcY * tanHalfFov);
  }

  ray.direction.Normalize();
  return ray;
}

void EditorApp::HandleMousePick() {
  const bool shiftDown = IManager.PressedKey(T800K_LSHIFT) || IManager.PressedKey(T800K_RSHIFT);
  const bool selectMode = (m_gizmo.Mode() == GizmoMode::Select);

  // Marquee drag in Select mode (skip when Alt is held — Alt+left-drag is orbit)
  if (selectMode) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    const bool altDown = IManager.PressedKey(T800K_LALT) || IManager.PressedKey(T800K_RALT);

    // Start marquee on mouse press (only if Alt is not held)
    if (IManager.PressedOnceMouseButton(0) && !altDown) {
      g_marqueeActive = true;
      g_marqueeStart = ImVec2((float)IManager.mouseX, (float)IManager.mouseY);
    }

    // Finish marquee on mouse release
    if (g_marqueeActive && !IManager.PressedMouseButton(0)) {
      g_marqueeActive = false;
      ImVec2 mEnd((float)IManager.mouseX, (float)IManager.mouseY);
      float dx = mEnd.x - g_marqueeStart.x;
      float dy = mEnd.y - g_marqueeStart.y;

      if (std::abs(dx) < 5.0f && std::abs(dy) < 5.0f) {
        // Tiny drag = single click pick
        goto single_pick;
      }

      // Build rect (normalize min/max)
      float rMinX = (g_marqueeStart.x < mEnd.x) ? g_marqueeStart.x : mEnd.x;
      float rMinY = (g_marqueeStart.y < mEnd.y) ? g_marqueeStart.y : mEnd.y;
      float rMaxX = (g_marqueeStart.x > mEnd.x) ? g_marqueeStart.x : mEnd.x;
      float rMaxY = (g_marqueeStart.y > mEnd.y) ? g_marqueeStart.y : mEnd.y;

      if (!shiftDown) g_multiSelect.clear();

      for (int i = 0; i < (int)g_objects.size(); ++i) {
        if (!g_objects[i].wireframe.IsLoaded() || g_objects[i].frozen || !g_objects[i].visible)
          continue;
        t850::AABB worldBox = g_objects[i].wireframe.WorldAABB();
        if (AABBInScreenRect(worldBox, m_vp, m_lastW, m_lastH, rMinX, rMinY, rMaxX, rMaxY)) {
          g_multiSelect.insert(i);
        }
      }

      // Update single selection to match multi-select state
      if (g_multiSelect.size() == 1) {
        g_selectedIdx = *g_multiSelect.begin();
        g_selectionType = 0;
      } else if (g_multiSelect.size() > 1) {
        g_selectedIdx = *g_multiSelect.begin();
        g_selectionType = 0;
      } else {
        g_selectedIdx = -1;
      }
      return;
    }
    return;
  }

single_pick:
  if (!IManager.PressedOnceMouseButton(0) && !selectMode) return;

  const ::Camera* activeCamera = &m_camera.GetCamera();
  if (Camera* primaryCamera = m_sceneProps.GetPrimaryCamera()) {
    activeCamera = primaryCamera;
  }
  t850::Ray ray = BuildEditorCameraRay(*activeCamera,
                                       static_cast<float>(IManager.mouseX),
                                       static_cast<float>(IManager.mouseY),
                                       m_lastW,
                                       m_lastH);

  // Test all objects, pick the closest
  float bestT = FLT_MAX;
  int   bestIdx  = -1;
  int   bestType = 0;

  // Test meshes
  for (int i = 0; i < (int)g_objects.size(); ++i) {
    SceneObject& object = g_objects[i];
    const bool loaded = object.wireframe.IsLoaded();
    const bool skinnedMesh = object.litInst.GetSkinnedMesh() && object.litInst.GetSkinnedMesh()->HasSkinData();
    if (!loaded || object.frozen || !object.visible) {
      continue;
    }

    t850::AABB worldBox;
    const bool haveBounds = GetEditorObjectWorldAABB(object, worldBox);
    if (!haveBounds) {
      continue;
    }

    float triT = 0.0f;
    const bool triHit = object.wireframe.RaycastSurface(ray, triT);
    float sMinX = 0.0f, sMinY = 0.0f, sMaxX = 0.0f, sMaxY = 0.0f;
    const bool projected = ProjectAABBToScreenRect(worldBox, m_vp, m_lastW, m_lastH, sMinX, sMinY, sMaxX, sMaxY);
    const float mouseX = static_cast<float>(IManager.mouseX);
    const float mouseY = static_cast<float>(IManager.mouseY);
    const bool mouseInsideScreenBounds = projected && mouseX >= sMinX && mouseX <= sMaxX && mouseY >= sMinY && mouseY <= sMaxY;
    const float screenArea = (std::max)(1.0f, (sMaxX - sMinX) * (sMaxY - sMinY));
    const float viewportArea = (std::max)(1.0f, static_cast<float>(m_lastW * m_lastH));
    const bool smallProjectedMesh = projected && screenArea < viewportArea * 0.65f;
    float boxT = 0.0f;
    const bool boxHit = t850::RayIntersectsAABB(ray, worldBox, boxT);
    const bool fallbackAllowed = mouseInsideScreenBounds && (skinnedMesh || smallProjectedMesh);
    const bool candidateHit = triHit || (fallbackAllowed && boxHit);
    const float candidateT = triHit ? triT : boxT;

    if (candidateHit && candidateT < bestT) {
      bestT = candidateT; bestIdx = i; bestType = 0;
    }
  }

  // Test cameras (AABB pick — virtual bounding box around position)
  for (int i = 0; i < (int)g_cameras.size(); ++i) {
    if (g_cameras[i].frozen || !g_cameras[i].visible) continue;
    float hs = 2.0f;
    t850::AABB box(
      XVECTOR3(g_cameras[i].position.x - hs, g_cameras[i].position.y - hs, g_cameras[i].position.z - hs),
      XVECTOR3(g_cameras[i].position.x + hs, g_cameras[i].position.y + hs, g_cameras[i].position.z + hs));
    float t = 0.0f;
    const bool hit = t850::RayIntersectsAABB(ray, box, t);
    if (hit && t < bestT) {
      bestT = t; bestIdx = i; bestType = 1;
    }
  }

  // Test lights (AABB pick — virtual bounding box around position)
  for (int i = 0; i < (int)g_lights.size(); ++i) {
    if (g_lights[i].frozen || !g_lights[i].visible) continue;
    float hs = (g_lights[i].type == EditorLightType::Omni) ? 2.5f : 2.0f;
    t850::AABB box(
      XVECTOR3(g_lights[i].position.x - hs, g_lights[i].position.y - hs, g_lights[i].position.z - hs),
      XVECTOR3(g_lights[i].position.x + hs, g_lights[i].position.y + hs, g_lights[i].position.z + hs));
    float t = 0.0f;
    const bool hit = t850::RayIntersectsAABB(ray, box, t);
    if (hit && t < bestT) {
      bestT = t; bestIdx = i; bestType = 2;
    }
  }

  if (bestIdx >= 0) {
    g_selectedIdx   = bestIdx;
    g_selectionType = bestType;

    // Multi-select: shift-click adds/removes from set (meshes only)
    if (bestType == 0) {
      if (shiftDown) {
        if (g_multiSelect.count(bestIdx))
          g_multiSelect.erase(bestIdx);
        else
          g_multiSelect.insert(bestIdx);
      } else {
        g_multiSelect.clear();
        // Check if clicked mesh belongs to a persistent group — select entire group
        bool foundGroup = false;
        for (auto& grp : g_groups) {
          if (grp.persistent && grp.members.count(bestIdx)) {
            g_multiSelect = grp.members;
            foundGroup = true;
            break;
          }
        }
        if (!foundGroup)
          g_multiSelect.insert(bestIdx);
      }
    } else {
      g_multiSelect.clear();
    }
  } else {
    g_selectedIdx = -1;
    // Auto-switch to Select mode when deselecting (hides orphaned gizmo)
    m_gizmo.SetMode(GizmoMode::Select);
    if (!shiftDown)
      g_multiSelect.clear();
  }
}

void EditorApp::OnDraw() {
  if (!pFramework || !pFramework->pVideoDriver) return;

  t850::BaseDriver* drv = pFramework->pVideoDriver;
  T8_LOG_TRACE("[T8ditor] OnDraw: BeginFrame...");
  drv->BeginFrame();
  const bool freezeEditorViewport = m_meshEditorOpen || m_playSceneOpen;
  if (freezeEditorViewport && !m_editorFrozenFrameValid) {
    EnsureEditorFrozenFrameTarget(m_lastW, m_lastH);
  }
  T8_LOG_TRACE("[T8ditor] OnDraw: Clear...");
  drv->Clear();

  const bool captureFrozenEditorFrame =
      freezeEditorViewport &&
      !m_editorFrozenFrameValid &&
      m_editorFrozenFrameRT >= 0;
  bool didCaptureFrozenEditorFrame = false;
  if (m_assetsCreated && (!freezeEditorViewport || captureFrozenEditorFrame)) {
    // Determine which camera drives rendering
    if (g_activeCameraIdx >= 0 && g_activeCameraIdx < (int)g_cameras.size()) {
      // Build a persistent Camera from the scene camera
      SceneCamera& sc = g_cameras[g_activeCameraIdx];
      float aspect = (m_lastW > 0 && m_lastH > 0) ? (float)m_lastW / (float)m_lastH : 16.0f/9.0f;
      if (sc.type == CameraType::Perspective) {
        g_viewCamera.InitPerspective(sc.position, sc.fovDeg * (xPI / 180.0f), aspect, sc.nearPlane, sc.farPlane);
      } else {
        g_viewCamera.InitOrtho(sc.position, sc.orthoW, sc.orthoH, sc.nearPlane, sc.farPlane);
      }
      g_viewCamera.Eye = sc.position;
      g_viewCamera.SetLookAt(sc.target);
      g_viewCamera.Update(0.0f);
      // Point the scene props active camera at our persistent camera
      if (!m_sceneProps.pCameras.empty())
        m_sceneProps.SetPrimaryCamera(&g_viewCamera);
    } else {
      // Editor orbit camera
      if (!m_sceneProps.pCameras.empty())
        m_sceneProps.SetPrimaryCamera(&m_camera.GetCameraMutable());
    }

    const ::Camera& cam = *m_sceneProps.GetPrimaryCamera();
    m_vp = cam.VP;

    // Sync scene lights from editor lights.
    // Use real scene lights by default; keep the camera headlamp only as an explicit/fallback light.
    {
      int enabledCount = 0;
      for (auto& lt : g_lights)
        if (lt.enabled) enabledCount++;

      m_sceneProps.Lights.clear();
      const bool useHeadlamp = m_editorHeadlampEnabled || enabledCount == 0;
      if (useHeadlamp) {
        XVECTOR3 look = cam.Look;
        XVECTOR3 eye = cam.Eye;
        XVECTOR3 dir(look.x - eye.x, look.y - eye.y, look.z - eye.z);
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len > 0.0001f) { dir.x /= len; dir.y /= len; dir.z /= len; }
        m_sceneProps.AddDirectionalLight(dir, XVECTOR3(1.0f, 1.0f, 1.0f), 1.5f, true);
        if (!m_sceneProps.Lights.empty()) {
          m_sceneProps.Lights.back().Position = eye;
          m_sceneProps.Lights.back().radius = 30000.0f;
        }
      }

      for (auto& lt : g_lights) {
        if (!lt.enabled) continue;
        if (lt.type == EditorLightType::Directional) {
          m_sceneProps.AddDirectionalLight(lt.direction, lt.color, lt.intensity, true);
          if (!m_sceneProps.Lights.empty()) {
            m_sceneProps.Lights.back().Position = lt.position;
            m_sceneProps.Lights.back().radius = lt.radius;
          }
        } else {
          m_sceneProps.AddLight(lt.position, lt.color, lt.radius, lt.intensity, LIGHT_POINT, true);
        }
      }
      m_sceneProps.ActiveLights = (int)m_sceneProps.Lights.size();
      for (const Light& light : m_sceneProps.Lights) {
        if (light.Type == LIGHT_DIRECTIONAL && light.Enabled) {
          XVECTOR3 direction = light.Direction;
          if (direction.Length() > 0.0001f) {
            direction.Normalize();
            m_editorLightCamera.Eye = light.Position;
            m_editorLightCamera.SetLookAt(m_editorLightCamera.Eye + direction);
            m_editorLightCamera.Update(0.0f);
          }
          break;
        }
      }
      if (m_sceneProps.pLightCameras.empty()) {
        m_sceneProps.AddLightCamera(&m_editorLightCamera);
      }
      m_sceneProps.ActiveLightCamera = 0;
      m_sceneProps.pCullingCamera = m_sceneProps.GetPrimaryCamera();
    }

    // Update all mesh transforms
    SyncSceneObjectTransforms();
    UploadSkinnedBoneTextures();

    // Render meshes: deferred via render graph on D3D11/D3D12, forward on GL
    bool useDeferred = g_deferredReady
                    && drv->m_currentAPI != t850::GraphicsApi::OPENGL;

    if (useDeferred) {
      // Build mesh array: skybox first (index 0), then scene meshes
      // The render graph JSON controls which indices are drawn in each pass.
      std::vector<t850::PrimitiveInst*> allMeshes;

      // Scene meshes. The deferred graph draws sky/environment from empty GBuffer pixels;
      // do not include the skybox mesh here or it will render into shadow/GBuffer passes.
      for (int objectIndex = 0; objectIndex < (int)g_objects.size(); ++objectIndex) {
        auto& obj = g_objects[objectIndex];
        if (obj.primId >= 0 && obj.visible) {
          allMeshes.push_back(&obj.litInst);
        }
      }

      // Bind shadow dummy and env map to quads[0] before execute
      if (g_dummyWhiteTex)
        g_quads[0].SetTexture(g_dummyWhiteTex, 5);
      if (g_dummyEnvMapIdx >= 0)
        g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));

      // Execute the render graph (GBuffer -> Deferred -> BackBuffer)
      // RenderGraph::Execute needs a contiguous PrimitiveInst array.
      // We copy the instances (shallow — pBase pointer stays valid).
      std::vector<t850::PrimitiveInst> meshArray;
      meshArray.reserve(allMeshes.size());
      for (auto* p : allMeshes) meshArray.push_back(*p);

      ::Camera* mainCam = m_sceneProps.GetPrimaryCamera();
      t850::EnvironmentMapSet editorEnvMaps;
      editorEnvMaps.SetFallback(g_dummyEnvMapIdx);
      T8_LOG_TRACE("[T8ditor] OnDraw: RenderGraph Execute (%d meshes)...", (int)meshArray.size());
      g_renderGraph.Execute(drv, m_sceneProps,
        meshArray.data(), (int)meshArray.size(),
        g_quads, mainCam, nullptr, nullptr,
        editorEnvMaps,
        captureFrozenEditorFrame ? m_editorFrozenFrameRT : -1);
      didCaptureFrozenEditorFrame = captureFrozenEditorFrame;
      T8_LOG_TRACE("[T8ditor] OnDraw: RenderGraph Execute done");

      // RT debug override: if a specific RT is selected, draw it to backbuffer
      if (g_debugRT >= 0) {
        drv->SetBlendState(t850::BaseDriver::BLEND_OPAQUE);
        drv->SetDepthStencilState(t850::BaseDriver::NONE);
        int gi = 0;
        t850::Texture* debugTex = nullptr;
        for (int rtIdx = 0; rtIdx < (int)drv->RTs.size() && !debugTex; ++rtIdx) {
          auto* rt = drv->RTs[rtIdx];
          if (!rt) continue;
          for (int ci = 0; ci < (int)rt->vColorTextures.size(); ++ci) {
            if (gi == g_debugRT) { debugTex = rt->vColorTextures[ci]; break; }
            gi++;
          }
          if (!debugTex && rt->pDepthTexture) {
            if (gi == g_debugRT) debugTex = rt->pDepthTexture;
            gi++;
          }
        }
        if (debugTex) {
          g_quads[7].SetTexture(debugTex, 0);
          t850::ShaderKey bk(0);
          bk.setPass(t850::PassType::BACKBUFFER);
          g_quads[7].SetGlobalKey(bk);
          g_quads[7].Draw();
        }
        drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
      }
    } else {
      // Forward rendering (GL, or deferred not ready)
      // Skybox forward
      if (g_skyboxReady && m_panels.showSkybox) {
        t850::ShaderKey fwdKey(0);
        fwdKey.setPass(t850::PassType::FORWARD);
        g_skyboxInst.SetGlobalKey(fwdKey);
        g_skyboxInst.Update();
        g_skyboxInst.Draw();
      }
      for (int i = 0; i < (int)g_objects.size(); ++i) {
        SceneObject& obj = g_objects[i];
        if (obj.primId < 0 || !obj.visible) continue;
        t850::ShaderKey fwdKey(0);
        fwdKey.setPass(t850::PassType::FORWARD);
        obj.litInst.SetGlobalKey(fwdKey);
        obj.litInst.Draw();
      }
    }

    // Wireframe overlays (drawn after deferred resolve, on backbuffer)
    // Bind GBuffer depth for depth-tested wireframe
    t850::Texture* overlayOpaqueDepth = nullptr;
    t850::Texture* overlayForwardDepth = nullptr;
    if (useDeferred) {
      int gbufHandle = g_renderGraph.GetRTHandle("GBuffer");
      if (gbufHandle >= 0 && gbufHandle < (int)drv->RTs.size()) {
        auto* gbufRT = drv->RTs[gbufHandle];
        overlayOpaqueDepth = gbufRT->pDepthTexture;
      }
      int deferredHandle = g_renderGraph.GetRTHandle("Deferred");
      if (deferredHandle >= 0 && deferredHandle < (int)drv->RTs.size()) {
        auto* deferredRT = drv->RTs[deferredHandle];
        overlayForwardDepth = deferredRT->pDepthTexture;
      }
      m_lines.SetDepthTexture(overlayOpaqueDepth);
      m_lines.SetSecondaryDepthTexture(overlayForwardDepth);
      m_lines.SetViewport(m_lastW, m_lastH);
      m_lines.SetFarPlane(cam.FPlane);
    } else {
      m_lines.SetDepthTexture(nullptr);
      m_lines.SetSecondaryDepthTexture(nullptr);
    }

    for (int i = 0; i < (int)g_objects.size(); ++i) {
      SceneObject& obj = g_objects[i];
      if (obj.primId < 0 || !obj.visible) continue;
      bool isSelected = (g_selectionType == 0 && i == g_selectedIdx) || g_multiSelect.count(i);
      bool showWire = m_panels.showWireframe || isSelected || obj.showWire;
      if (!showWire) continue;

      // For skinned meshes, use GPU-skinned wireframe + skeleton (same as SandBox)
      t850::RenderSkinnedMesh* skinned = nullptr;
      if (obj.litInst.pBase)
        skinned = dynamic_cast<t850::RenderSkinnedMesh*>(obj.litInst.pBase);

      if (skinned && skinned->HasSkinData()) {
        skinned->SetWireframeDepthTex(overlayOpaqueDepth);
        skinned->SetWireframeSecondaryDepthTex(overlayForwardDepth);
        skinned->SetWireframeViewport(m_lastW, m_lastH);
        drv->SetDepthStencilState(t850::BaseDriver::NONE);
        skinned->DrawWireframe();
        drv->SetDepthStencilState(t850::BaseDriver::NONE);
        skinned->DrawSkeleton();
        drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
      } else if (obj.wireframe.IsLoaded() && m_lines.IsReady()) {
        XVECTOR3 savedColor = obj.wireframe.WireColor;
        if (g_multiSelect.count(i) && g_multiSelect.size() > 1)
          obj.wireframe.WireColor = XVECTOR3(0.4f, 0.8f, 1.0f, 1.0f); // cyan for multi-select
        else if (isSelected)
          obj.wireframe.WireColor = XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f);
        else
          obj.wireframe.WireColor = XVECTOR3(0.45f, 0.45f, 0.45f, 1.0f);
        drv->SetDepthStencilState(t850::BaseDriver::READ);
        obj.wireframe.Draw(m_lines, cam.VP);
        drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
        obj.wireframe.WireColor = savedColor;
      }
    }

    // Camera and light viewport gizmos (only if visible)
    if (m_lines.IsReady()) {
      for (int i = 0; i < (int)g_cameras.size(); ++i)
        if (g_cameras[i].visible)
          DrawCameraGizmo(m_lines, cam.VP, g_cameras[i], g_selectionType == 1 && i == g_selectedIdx);
      for (int i = 0; i < (int)g_lights.size(); ++i)
        if (g_lights[i].visible)
          DrawLightGizmo(m_lines, cam.VP, g_lights[i], g_selectionType == 2 && i == g_selectedIdx);
    }

    // Grid
    if (m_lines.IsReady())
      m_grid.Draw(m_lines, cam.VP);

    if (ShouldDrawPhysicsDebug() && m_physicsDebug.IsReady()) {
      m_physicsDebug.SetViewport(m_lastW, m_lastH);
      m_physicsDebug.SetFarPlane(cam.FPlane);
      m_physicsDebug.SetDepthTexture(nullptr);
      m_physicsDebug.Draw(m_physics, cam.VP);
    }
  }
  if (didCaptureFrozenEditorFrame) {
    m_editorFrozenFrameValid = true;
  }
  if (freezeEditorViewport && m_editorFrozenFrameValid) {
    DrawEditorFrozenFrame(drv);
  }

  // ImGui overlay
  if (m_imguiReady) {
    ImGuiNewFrame();

    if (m_meshEditorOpen) {
      DrawMeshEditorWindow();
      T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender...");
      ImGuiRender();
      T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender done");
      goto after_editor_imgui;
    }
    if (m_playSceneOpen) {
      DrawPlaySceneWindow();
      T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender...");
      ImGuiRender();
      T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender done");
      goto after_editor_imgui;
    }

    MenuAction menuAction = ImGuiDrawMenuBar(m_panels);

    int addCamera = -1, addLight = -1;
    bool wantsClone = false, wantsGroup = false, wantsUngroup = false, wantsPlayScene = false;
    int mode = ImGuiDrawToolbar((int)m_gizmo.Mode(), addCamera, addLight,
                                  wantsClone, wantsGroup, wantsUngroup, wantsPlayScene,
                                  g_selectedIdx >= 0, g_multiSelect.size() >= 2);
    m_gizmo.SetMode((GizmoMode)mode);

    // Handle add camera/light from toolbar
    if (addCamera >= 0) {
      SceneCamera cam;
      cam.name = "Camera " + std::to_string(g_cameras.size());
      cam.type = (addCamera == 1) ? CameraType::Orthographic : CameraType::Perspective;
      g_cameras.push_back(cam);
      g_selectedIdx   = (int)g_cameras.size() - 1;
      g_selectionType = 1;
    }
    if (addLight >= 0) {
      SceneLight lt;
      lt.name = "Light " + std::to_string(g_lights.size());
      lt.type = (addLight == 1) ? EditorLightType::Omni : EditorLightType::Directional;
      g_lights.push_back(lt);
      g_selectedIdx   = (int)g_lights.size() - 1;
      g_selectionType = 2;
    }
    if (wantsPlayScene) {
      OpenPlayScene();
    }

    // Sync temp group from multi-select
    g_tempGroup.members = g_multiSelect;
    g_tempGroup.persistent = false;

    // Right-click context menu
    {
      bool hasSel = (g_selectedIdx >= 0) || !g_multiSelect.empty();
      bool hasMulti = g_multiSelect.size() >= 2;
      bool hasGrp = false;
      for (auto& grp : g_groups) {
        if (grp.persistent && grp.members == g_multiSelect) { hasGrp = true; break; }
      }
      ContextAction ctx = ImGuiDrawContextMenu(hasSel, hasMulti, hasGrp);
      if (ctx.setMode >= -1) m_gizmo.SetMode((GizmoMode)ctx.setMode);
      if (ctx.wantsClone) wantsClone = true;
      if (ctx.wantsGroup) wantsGroup = true;
      if (ctx.wantsUngroup) wantsUngroup = true;
      if (ctx.wantsDelete && g_selectedIdx >= 0) {
        if (g_selectionType == 0 && g_selectedIdx < (int)g_objects.size()) {
          DestroyObjectRagdoll(g_objects[g_selectedIdx]);
          g_objects.erase(g_objects.begin() + g_selectedIdx);
          g_multiSelect.erase(g_selectedIdx);
          g_selectedIdx = -1;
        } else if (g_selectionType == 1 && g_selectedIdx < (int)g_cameras.size()) {
          if (g_activeCameraIdx == g_selectedIdx) g_activeCameraIdx = -1;
          else if (g_activeCameraIdx > g_selectedIdx) g_activeCameraIdx--;
          g_cameras.erase(g_cameras.begin() + g_selectedIdx);
          g_selectedIdx = -1;
        } else if (g_selectionType == 2 && g_selectedIdx < (int)g_lights.size()) {
          g_lights.erase(g_lights.begin() + g_selectedIdx);
          g_selectedIdx = -1;
        }
      }
      if (ctx.wantsFrameView) {
        SceneObject* sel = SelectedObject();
        if (sel && sel->wireframe.IsLoaded()) {
          m_camera.SetTarget(sel->wireframe.Position());
          m_camera.ResetViewAngle();
        }
      }
      if (ctx.addCamera >= 0) {
        SceneCamera cam;
        cam.name = "Camera " + std::to_string(g_cameras.size());
        cam.type = (ctx.addCamera == 1) ? CameraType::Orthographic : CameraType::Perspective;
        g_cameras.push_back(cam);
        g_selectedIdx = (int)g_cameras.size() - 1;
        g_selectionType = 1;
      }
      if (ctx.addLight >= 0) {
        SceneLight lt;
        lt.name = "Light " + std::to_string(g_lights.size());
        lt.type = (ctx.addLight == 1) ? EditorLightType::Omni : EditorLightType::Directional;
        g_lights.push_back(lt);
        g_selectedIdx = (int)g_lights.size() - 1;
        g_selectionType = 2;
      }
    }

    if (wantsClone) {
      CloneSelected();
    }

    // Group button / context menu: create persistent group
    if (wantsGroup && g_multiSelect.size() >= 2) {
      bool alreadyGrouped = false;
      for (auto& grp : g_groups) {
        if (grp.members == g_multiSelect) { alreadyGrouped = true; break; }
      }
      if (!alreadyGrouped) {
        SceneGroup grp;
        grp.name = "Group " + std::to_string(g_groups.size());
        grp.members = g_multiSelect;
        grp.persistent = true;
        g_groups.push_back(grp);
        g_activeGroupIdx = (int)g_groups.size() - 1;
        T8_LOG_INFO("[T8ditor] Created group '%s' with %d objects", grp.name.c_str(), (int)grp.members.size());
      }
    }

    // Ungroup button / context menu: dissolve group
    if (wantsUngroup && g_multiSelect.size() >= 2) {
      for (int gi = (int)g_groups.size() - 1; gi >= 0; gi--) {
        if (g_groups[gi].members == g_multiSelect) {
          T8_LOG_INFO("[T8ditor] Ungrouped '%s'", g_groups[gi].name.c_str());
          g_groups.erase(g_groups.begin() + gi);
          if (g_activeGroupIdx == gi) g_activeGroupIdx = -1;
          else if (g_activeGroupIdx > gi) g_activeGroupIdx--;
          break;
        }
      }
    }

    // Draw marquee selection rectangle (Select mode)
    if (g_marqueeActive && m_gizmo.Mode() == GizmoMode::Select) {
      ImVec2 mPos((float)IManager.mouseX, (float)IManager.mouseY);
      ImDrawList* dl = ImGui::GetBackgroundDrawList();
      dl->AddRectFilled(g_marqueeStart, mPos, IM_COL32(100, 100, 255, 40));
      dl->AddRect(g_marqueeStart, mPos, IM_COL32(100, 100, 255, 200), 0.0f, 0, 1.5f);
    }

    // Group / multi-select bounding box (corner brackets)
    if (g_multiSelect.size() > 1) {
      t850::AABB combined;
      bool first = true;
      for (int idx : g_multiSelect) {
        if (idx < 0 || idx >= (int)g_objects.size()) continue;
        if (!g_objects[idx].wireframe.IsLoaded() || !g_objects[idx].visible) continue;
        t850::AABB wb = g_objects[idx].wireframe.WorldAABB();
        if (first) { combined = wb; first = false; }
        else {
          if (wb.vMin.x < combined.vMin.x) combined.vMin.x = wb.vMin.x;
          if (wb.vMin.y < combined.vMin.y) combined.vMin.y = wb.vMin.y;
          if (wb.vMin.z < combined.vMin.z) combined.vMin.z = wb.vMin.z;
          if (wb.vMax.x > combined.vMax.x) combined.vMax.x = wb.vMax.x;
          if (wb.vMax.y > combined.vMax.y) combined.vMax.y = wb.vMax.y;
          if (wb.vMax.z > combined.vMax.z) combined.vMax.z = wb.vMax.z;
        }
      }
      if (!first) {
        const ::Camera& camBB = *m_sceneProps.GetPrimaryCamera();
        float bmin[3] = { combined.vMin.x, combined.vMin.y, combined.vMin.z };
        float bmax[3] = { combined.vMax.x, combined.vMax.y, combined.vMax.z };
        ImVec2 corners[8];
        bool allValid = true;
        for (int c = 0; c < 8; c++) {
          float lx = (c & 1) ? bmax[0] : bmin[0];
          float ly = (c & 2) ? bmax[1] : bmin[1];
          float lz = (c & 4) ? bmax[2] : bmin[2];
          corners[c] = WorldToScreen(XVECTOR3(lx, ly, lz), camBB.VP, m_lastW, m_lastH);
          if (corners[c].x < -5000 || corners[c].y < -5000) allValid = false;
        }
        if (allValid) {
          ImDrawList* dl = ImGui::GetBackgroundDrawList();
          ImU32 col = IM_COL32(100, 200, 255, 200);
          float thickness = 1.5f;
          int edges[12][2] = {
            {0,1},{2,3},{4,5},{6,7},
            {0,2},{1,3},{4,6},{5,7},
            {0,4},{1,5},{2,6},{3,7}
          };
          for (int e = 0; e < 12; e++) {
            ImVec2 a = corners[edges[e][0]];
            ImVec2 b = corners[edges[e][1]];
            float dx = b.x - a.x, dy = b.y - a.y;
            float frac = 0.25f;
            dl->AddLine(a, ImVec2(a.x + dx * frac, a.y + dy * frac), col, thickness);
            dl->AddLine(b, ImVec2(b.x - dx * frac, b.y - dy * frac), col, thickness);
          }
        }
      }
    }

    // ImGuizmo on selected entity
    ImGuizmoBeginFrame(0, 0, m_lastW, m_lastH, false);
    const ::Camera& cam2 = m_sceneProps.GetPrimaryCamera()
        ? *m_sceneProps.GetPrimaryCamera()
        : m_camera.GetCamera();

    // Multi-select group gizmo (meshes only, 2+ selected)
    // Scene-graph approach: root node at centroid, children at offsets.
    // ImGuizmo manipulates the root; children inherit the transform.
    if (g_multiSelect.size() > 1) {
      // Persistent scene-graph helper (survives across frames during drag)
      static GroupTransformHelper s_groupHelper;
      static std::map<int, TransformState> s_undoBeforeState;

      // Determine which group to use (persistent or temp)
      SceneGroup* activeGroup = &g_tempGroup;
      for (auto& grp : g_groups) {
        if (grp.members == g_multiSelect) { activeGroup = &grp; break; }
      }

      XVECTOR3 centroid = activeGroup->Centroid(g_objects);

      // When not dragging, show gizmo at current centroid
      // (use a temp matrix for display — the real one lives in s_groupHelper)
      XMATRIX44 displayMat;
      if (s_groupHelper.IsActive()) {
        // During drag: use the helper's persistent root matrix
        // (ImGuizmo already wrote into it last frame)
      } else {
        XMatTranslation(displayMat, centroid.x, centroid.y, centroid.z);
      }

      bool isUsingNow = ImGuizmo::IsUsing();

      // ── Drag start: build the scene graph ──
      if (isUsingNow && !g_gizmoDragging) {
        g_gizmoDragging = true;

        // Snapshot original state for undo
        std::map<int, XVECTOR3> positions, rotations, scales;
        s_undoBeforeState.clear();
        for (int idx : g_multiSelect) {
          if (idx >= 0 && idx < (int)g_objects.size()) {
            positions[idx] = g_objects[idx].wireframe.Position();
            rotations[idx] = g_objects[idx].wireframe.EulerRadians();
            scales[idx]    = g_objects[idx].wireframe.Scale();
            s_undoBeforeState[idx] = {
              g_objects[idx].wireframe.Position(),
              g_objects[idx].wireframe.EulerRadians(),
              g_objects[idx].wireframe.Scale()
            };
          }
        }

        // Build the node tree: root at centroid, children at offsets
        s_groupHelper.Begin(centroid, positions, rotations, scales);
      }

      // ── ImGuizmo manipulate ──
      int imguizmoMode = mode;
      if (imguizmoMode < 0) imguizmoMode = 0;

      // Get the matrix pointer: persistent root matrix during drag, temp display otherwise
      float* matPtr = s_groupHelper.IsActive()
                    ? s_groupHelper.RootMatrix()
                    : &displayMat.m[0][0];

      XMATRIX44 deltaMatrix;
      XMatIdentity(deltaMatrix);

      bool manipulated = ImGuizmo::Manipulate(
        &cam2.View.m[0][0], &cam2.Projection.m[0][0],
        (ImGuizmo::OPERATION)((imguizmoMode == 0) ? ImGuizmo::TRANSLATE
                            : (imguizmoMode == 1) ? ImGuizmo::ROTATE
                            :                       ImGuizmo::SCALEU),
        ImGuizmo::WORLD,
        matPtr, &deltaMatrix.m[0][0]);

      // ── Apply: recompute children's world positions from the scene graph ──
      if (manipulated && s_groupHelper.IsActive()) {
        // ImGuizmo already modified the root matrix in place via matPtr.
        // Recompute children's world transforms through the tree.
        s_groupHelper.Update();

        // Read back children's world transforms into the scene objects
        for (int idx : g_multiSelect) {
          if (idx < 0 || idx >= (int)g_objects.size()) continue;

          XMATRIX44 childWorld = s_groupHelper.ChildWorldMatrix(idx);
          float t[3], rDeg[3], sComp[3];
          ImGuizmo::DecomposeMatrixToComponents(&childWorld.m[0][0], t, rDeg, sComp);

          g_objects[idx].wireframe.Position() = XVECTOR3(t[0], t[1], t[2]);
          g_objects[idx].wireframe.EulerRadians() = XVECTOR3(
            rDeg[0] * kDegToRad,
            rDeg[1] * kDegToRad,
            rDeg[2] * kDegToRad);

          // For scale mode: also scale child meshes
          if (imguizmoMode == 2) {
            float sf = s_groupHelper.RootUniformScale();
            XVECTOR3 origScale = s_groupHelper.OriginalScale(idx);
            g_objects[idx].wireframe.Scale() = XVECTOR3(
              origScale.x * sf, origScale.y * sf, origScale.z * sf);
          }
        }
      }

      // ── Drag end: bake and tear down ──
      if (!isUsingNow && g_gizmoDragging) {
        g_gizmoDragging = false;
        s_groupHelper.End();

        // Push undo
        std::map<int, TransformState> afterState;
        for (int idx : g_multiSelect) {
          if (idx >= 0 && idx < (int)g_objects.size()) {
            afterState[idx] = {
              g_objects[idx].wireframe.Position(),
              g_objects[idx].wireframe.EulerRadians(),
              g_objects[idx].wireframe.Scale()
            };
          }
        }
        auto cmd = std::make_unique<GroupTransformCommand>(
          s_undoBeforeState, afterState,
          [](int idx, const TransformState& s) {
            if (idx >= 0 && idx < (int)g_objects.size()) {
              g_objects[idx].wireframe.Position()     = s.position;
              g_objects[idx].wireframe.EulerRadians() = s.eulerRad;
              g_objects[idx].wireframe.Scale()         = s.scale;
            }
          });
        g_undoStack.Push(std::move(cmd));
        s_undoBeforeState.clear();
      }
    }
    else if (g_selectionType == 0) {
      // ── Mesh gizmo ──
      SceneObject* sel = SelectedObject();
      if (sel && sel->wireframe.IsLoaded()) {
        XMATRIX44 worldMat = sel->wireframe.BuildWorld();

        bool isUsingNow = ImGuizmo::IsUsing();
        if (isUsingNow && !g_gizmoDragging) {
          g_gizmoDragging = true;
          g_gizmoDragStart = { sel->wireframe.Position(),
                               sel->wireframe.EulerRadians(),
                               sel->wireframe.Scale() };
        }

        bool manipulated = ImGuizmoManipulate(
          &cam2.View.m[0][0], &cam2.Projection.m[0][0],
          mode, &worldMat.m[0][0]);
        if (manipulated) {
          float translation[3], rotation[3], scale[3];
          ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], translation, rotation, scale);
          for (int s = 0; s < 3; s++)
            if (scale[s] < kMinEditableScale && scale[s] > -kMinEditableScale)
              scale[s] = (scale[s] >= 0) ? kMinEditableScale : -kMinEditableScale;
          sel->wireframe.Position() = XVECTOR3(translation[0], translation[1], translation[2]);
          sel->wireframe.EulerRadians() = XVECTOR3(
            rotation[0] * kDegToRad, rotation[1] * kDegToRad, rotation[2] * kDegToRad);
          sel->wireframe.Scale() = XVECTOR3(scale[0], scale[1], scale[2]);
        }

        if (!isUsingNow && g_gizmoDragging) {
          g_gizmoDragging = false;
          TransformState after = { sel->wireframe.Position(),
                                   sel->wireframe.EulerRadians(),
                                   sel->wireframe.Scale() };
          int idx = g_selectedIdx;
          auto cmd = std::make_unique<TransformCommand>(
            idx, g_gizmoDragStart, after,
            [idx](const TransformState& s) {
              if (idx >= 0 && idx < (int)g_objects.size()) {
                g_objects[idx].wireframe.Position()     = s.position;
                g_objects[idx].wireframe.EulerRadians() = s.eulerRad;
                g_objects[idx].wireframe.Scale()         = s.scale;
              }
            });
          g_undoStack.Push(std::move(cmd));
        }
      }
    }
    else if (g_selectionType == 1 && g_selectedIdx >= 0 && g_selectedIdx < (int)g_cameras.size()) {
      // ── Camera gizmo — use translate-only via ImGuizmo ──
      SceneCamera& sc = g_cameras[g_selectedIdx];

      // Camera position gizmo
      ImGuizmo::SetID(0);
      XMATRIX44 worldMat;
      XMatTranslation(worldMat, sc.position.x, sc.position.y, sc.position.z);

      bool manipulated = ImGuizmo::Manipulate(
        &cam2.View.m[0][0], &cam2.Projection.m[0][0],
        ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
        &worldMat.m[0][0], nullptr);

      if (manipulated) {
        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], t, r, s);
        XVECTOR3 delta(t[0] - sc.position.x, t[1] - sc.position.y, t[2] - sc.position.z);
        sc.position = XVECTOR3(t[0], t[1], t[2]);
        sc.target.x += delta.x;
        sc.target.y += delta.y;
        sc.target.z += delta.z;
      }

      // Camera target gizmo (separate ImGuizmo ID so both can coexist)
      ImGuizmo::SetID(1);
      XMATRIX44 targetMat;
      XMatTranslation(targetMat, sc.target.x, sc.target.y, sc.target.z);

      bool targetMoved = ImGuizmo::Manipulate(
        &cam2.View.m[0][0], &cam2.Projection.m[0][0],
        ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
        &targetMat.m[0][0], nullptr);

      if (targetMoved) {
        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(&targetMat.m[0][0], t, r, s);
        sc.target = XVECTOR3(t[0], t[1], t[2]);
      }
      ImGuizmo::SetID(-1); // reset
    }
    else if (g_selectionType == 2 && g_selectedIdx >= 0 && g_selectedIdx < (int)g_lights.size()) {
      // ── Light gizmo ──
      SceneLight& sl = g_lights[g_selectedIdx];

      if (sl.type == EditorLightType::Omni && mode == 2) {
        // Scale mode for omni: use delta matrix to adjust radius
        XMATRIX44 worldMat;
        XMatScaling(worldMat, sl.radius, sl.radius, sl.radius);
        // Set translation
        worldMat.m[3][0] = sl.position.x;
        worldMat.m[3][1] = sl.position.y;
        worldMat.m[3][2] = sl.position.z;

        XMATRIX44 deltaMatrix;
        XMatIdentity(deltaMatrix);

        bool manipulated = ImGuizmo::Manipulate(
          &cam2.View.m[0][0], &cam2.Projection.m[0][0],
          ImGuizmo::SCALEU, ImGuizmo::WORLD,
          &worldMat.m[0][0], &deltaMatrix.m[0][0]);

        if (manipulated) {
          float dt[3], dr[3], ds[3];
          ImGuizmo::DecomposeMatrixToComponents(&deltaMatrix.m[0][0], dt, dr, ds);
          float deltaScale = (ds[0] + ds[1] + ds[2]) / 3.0f;
          sl.radius *= deltaScale;
          if (sl.radius < 0.1f) sl.radius = 0.1f;
        }
      } else {
        // Translate mode
        XMATRIX44 worldMat;
        XMatTranslation(worldMat, sl.position.x, sl.position.y, sl.position.z);

        bool manipulated = ImGuizmo::Manipulate(
          &cam2.View.m[0][0], &cam2.Projection.m[0][0],
          ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
          &worldMat.m[0][0], nullptr);

        if (manipulated) {
          float t[3], r[3], s[3];
          ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], t, r, s);
          sl.position = XVECTOR3(t[0], t[1], t[2]);
        }
      }
    }

    // Menu actions
    if (menuAction.wantsExit) {
#ifdef OS_WINDOWS
      auto* w32fw = static_cast<t850::Win32Framework*>(pFramework);
      w32fw->m_alive = false;
#endif
    }
    if (menuAction.wantsImportX) {
      std::string path = OpenFileDialog(
        L"3D Models (*.glb;*.gltf)\0*.glb;*.gltf\0glTF Binary (*.glb)\0*.glb\0glTF (*.gltf)\0*.gltf\0All Files (*.*)\0*.*\0",
        L"Import Mesh");
      if (!path.empty()) ImportMesh(path);
    }
    if (menuAction.wantsSaveScene) {
      std::string path = SaveFileDialog(
        L"T8ditor Scene (*.t8scene)\0*.t8scene\0JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0",
        L"Save Scene", L"t8scene");
      if (!path.empty()) {
        SaveEditorSceneSnapshot(path, true);
      }
    }
    if (menuAction.wantsLoadScene) {
      std::string path = OpenFileDialog(
        L"T8ditor Scene (*.t8scene)\0*.t8scene\0JSON (*.json)\0*.json\0All Files (*.*)\0*.*\0",
        L"Load Scene");
      if (!path.empty()) {
        // Defer the actual load to the start of the next frame (before BeginFrame)
        // to avoid destroying GPU resources mid-command-list on D3D12.
        g_pendingLoadPath = path;
      }
    }

    // Panels
    if (m_panels.showHierarchy) {
      if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 8.0f, viewport->WorkPos.y + 8.0f), ImGuiCond_FirstUseEver);
      }
      ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Hierarchy")) {
        ImGuiClampCurrentWindowToEditorWorkArea();
        if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen)) {
          // Meshes & Groups
          if (ImGui::TreeNodeEx("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("MeshesBulkControls");
            if (ImGui::SmallButton("Show all")) {
              for (SceneObject& object : g_objects) object.visible = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hide all")) {
              for (SceneObject& object : g_objects) object.visible = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Freeze all")) {
              for (SceneObject& object : g_objects) object.frozen = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Unfreeze all")) {
              for (SceneObject& object : g_objects) object.frozen = false;
            }
            if (ImGui::SmallButton("Wire all")) {
              for (SceneObject& object : g_objects) object.showWire = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Wire none")) {
              for (SceneObject& object : g_objects) object.showWire = false;
            }
            ImGui::TextDisabled("Eye = Show   F = Freeze   W = Wire");
            ImGui::Separator();
            ImGui::PopID();

            // Track which meshes are in persistent groups
            std::set<int> groupedIndices;
            for (auto& grp : g_groups)
              for (int idx : grp.members)
                groupedIndices.insert(idx);

            // Show persistent groups as collapsible parents
            for (int gi = 0; gi < (int)g_groups.size(); ++gi) {
              auto& grp = g_groups[gi];
              ImGui::PushID(gi + 40000);
              bool allSelected = true;
              for (int idx : grp.members)
                if (!g_multiSelect.count(idx)) { allSelected = false; break; }

              ImGuiTreeNodeFlags grpFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
              if (allSelected) grpFlags |= ImGuiTreeNodeFlags_Selected;
              std::string grpLabel = "[G] " + grp.name;
              bool grpOpen = ImGui::TreeNodeEx(grpLabel.c_str(), grpFlags);
              if (ImGui::IsItemClicked()) {
                // Click on group selects all its members
                g_multiSelect = grp.members;
                if (!grp.members.empty()) {
                  g_selectedIdx = *grp.members.begin();
                  g_selectionType = 0;
                }
              }
              if (grpOpen) {
                for (int idx : grp.members) {
                  if (idx < 0 || idx >= (int)g_objects.size()) continue;
                  auto& o = g_objects[idx];
                  ImGui::PushID(idx + 10000);
                  ImGui::Checkbox("##vis", &o.visible); ImGui::SameLine();
                  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
                  if (g_multiSelect.count(idx)) flags |= ImGuiTreeNodeFlags_Selected;
                  ImGui::TreeNodeEx(o.name.c_str(), flags);
                  if (ImGui::IsItemClicked()) {
                    g_selectedIdx = idx; g_selectionType = 0;
                    g_multiSelect.clear();
                    g_multiSelect.insert(idx);
                  }
                  ImGui::TreePop();
                  ImGui::PopID();
                }
                ImGui::TreePop();
              }
              ImGui::PopID();
            }

            // Show ungrouped meshes
            for (int i = 0; i < (int)g_objects.size(); ++i) {
              if (groupedIndices.count(i)) continue; // skip grouped
              auto& o = g_objects[i];
              ImGui::PushID(i + 10000);
              ImGui::Checkbox("##vis", &o.visible); ImGui::SameLine();
              ImGui::Checkbox("##frz", &o.frozen);  ImGui::SameLine();
              ImGui::Checkbox("##wir", &o.showWire); ImGui::SameLine();
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              if ((g_selectionType == 0 && i == g_selectedIdx) || g_multiSelect.count(i))
                flags |= ImGuiTreeNodeFlags_Selected;
              if (o.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
              bool nodeOpen = ImGui::TreeNodeEx(o.name.c_str(), flags);
              if (ImGui::IsItemClicked() && !o.frozen) {
                if (g_selectionType == 0 && g_selectedIdx == i) {
                  g_selectedIdx = -1;
                  g_multiSelect.clear();
                } else {
                  g_selectedIdx = i; g_selectionType = 0;
                  g_multiSelect.clear();
                  g_multiSelect.insert(i);
                }
              }
              if (o.frozen) ImGui::PopStyleColor();
              if (nodeOpen) ImGui::TreePop();
              ImGui::PopID();
            }
            ImGui::TreePop();
          }
          // Cameras
          if (ImGui::TreeNodeEx("Cameras", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("CamerasBulkControls");
            if (ImGui::SmallButton("Show all")) {
              for (SceneCamera& camera : g_cameras) camera.visible = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hide all")) {
              for (SceneCamera& camera : g_cameras) camera.visible = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Freeze all")) {
              for (SceneCamera& camera : g_cameras) camera.frozen = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Unfreeze all")) {
              for (SceneCamera& camera : g_cameras) camera.frozen = false;
            }
            ImGui::TextDisabled("Active   Eye = Show   F = Freeze");
            ImGui::Separator();
            ImGui::PopID();

            {
              bool isDefault = (g_activeCameraIdx < 0);
              ImGui::PushID(20000);
              if (ImGui::RadioButton("##act", isDefault)) g_activeCameraIdx = -1;
              ImGui::SameLine();
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              ImGui::TreeNodeEx("[E] Editor Camera", flags);
              ImGui::TreePop();
              ImGui::PopID();
            }
            for (int i = 0; i < (int)g_cameras.size(); ++i) {
              auto& c = g_cameras[i];
              ImGui::PushID(i + 20001);
              bool isActive = (g_activeCameraIdx == i);
              if (ImGui::RadioButton("##act", isActive))
                g_activeCameraIdx = isActive ? -1 : i;
              ImGui::SameLine();
              ImGui::Checkbox("##vis", &c.visible); ImGui::SameLine();
              ImGui::Checkbox("##frz", &c.frozen);  ImGui::SameLine();
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              if (g_selectionType == 1 && i == g_selectedIdx) flags |= ImGuiTreeNodeFlags_Selected;
              if (c.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
              const char* icon = (c.type == CameraType::Perspective) ? "[P] " : "[O] ";
              std::string label = icon + c.name;
              bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
              if (ImGui::IsItemClicked() && !c.frozen) {
                if (g_selectionType == 1 && g_selectedIdx == i) g_selectedIdx = -1;
                else { g_selectedIdx = i; g_selectionType = 1; }
              }
              if (c.frozen) ImGui::PopStyleColor();
              if (nodeOpen) ImGui::TreePop();
              ImGui::PopID();
            }
            ImGui::TreePop();
          }
          // Lights
          if (ImGui::TreeNodeEx("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("LightsBulkControls");
            if (ImGui::SmallButton("Show all")) {
              for (SceneLight& light : g_lights) light.visible = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hide all")) {
              for (SceneLight& light : g_lights) light.visible = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Freeze all")) {
              for (SceneLight& light : g_lights) light.frozen = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Unfreeze all")) {
              for (SceneLight& light : g_lights) light.frozen = false;
            }
            if (ImGui::SmallButton("Enable All")) {
              for (SceneLight& light : g_lights) light.enabled = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Disable All")) {
              for (SceneLight& light : g_lights) light.enabled = false;
            }
            ImGui::TextDisabled("Enable   Eye = Show   F = Freeze");
            ImGui::Separator();
            ImGui::PopID();

            for (int i = 0; i < (int)g_lights.size(); ++i) {
              auto& l = g_lights[i];
              ImGui::PushID(i + 30000);
              ImGui::Checkbox("##en",  &l.enabled); ImGui::SameLine();
              ImGui::Checkbox("##vis", &l.visible); ImGui::SameLine();
              ImGui::Checkbox("##frz", &l.frozen);  ImGui::SameLine();
              ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
              if (g_selectionType == 2 && i == g_selectedIdx) flags |= ImGuiTreeNodeFlags_Selected;
              if (l.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
              const char* icon = (l.type == EditorLightType::Directional) ? "[D] " : "[O] ";
              std::string label = icon + l.name;
              bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
              if (ImGui::IsItemClicked() && !l.frozen) {
                if (g_selectionType == 2 && g_selectedIdx == i) g_selectedIdx = -1;
                else { g_selectedIdx = i; g_selectionType = 2; }
              }
              if (l.frozen) ImGui::PopStyleColor();
              if (nodeOpen) ImGui::TreePop();
              ImGui::PopID();
            }
            ImGui::TreePop();
          }
          ImGui::TreePop();
        }
      }
      ImGui::End();
    }

    // ── Inspector ──
    SceneObject* sel = SelectedObject();
    if (m_panels.showInspector && g_selectedIdx >= 0) {
      if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 368.0f, viewport->WorkPos.y + 8.0f), ImGuiCond_FirstUseEver);
      }
      ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Inspector")) {
        ImGuiClampCurrentWindowToEditorWorkArea();
        if (g_selectionType == 0 && sel) {
          const bool selectedIsSkinned = sel->litInst.GetSkinnedMesh() != nullptr &&
              sel->litInst.GetSkinnedMesh()->HasSkinData();
          // Mesh inspector
          if (ImGui::Button("Edit Mesh")) {
            for (int i = 0; i < (int)g_objects.size(); ++i) {
              if (&g_objects[i] == sel) {
                OpenMeshEditor(i);
                break;
              }
            }
          }
          ImGui::SameLine();
          ImGui::TextDisabled("Opens a native window using this in-memory mesh instance.");

          ImGui::SeparatorText("Transform");
          XVECTOR3 pos = sel->wireframe.Position();
          XVECTOR3 eulerDeg(sel->wireframe.EulerRadians().x * kRadToDeg,
                            sel->wireframe.EulerRadians().y * kRadToDeg,
                            sel->wireframe.EulerRadians().z * kRadToDeg);
          XVECTOR3 scl = sel->wireframe.Scale();
          float p[3] = {pos.x, pos.y, pos.z};
          float r[3] = {eulerDeg.x, eulerDeg.y, eulerDeg.z};
          float s[3] = {scl.x, scl.y, scl.z};
          if (ImGui::DragFloat3("Position", p, 0.1f)) { pos.x=p[0]; pos.y=p[1]; pos.z=p[2]; }
          if (ImGui::DragFloat3("Rotation", r, 0.5f)) { eulerDeg.x=r[0]; eulerDeg.y=r[1]; eulerDeg.z=r[2]; }
          if (ImGui::DragFloat3("Scale", s, kScaleDragSpeed, kMinEditableScale, 100.0f, "%.6f")) {
            scl.x=s[0]; scl.y=s[1]; scl.z=s[2];
          }
          sel->wireframe.Position() = pos;
          sel->wireframe.EulerRadians() = XVECTOR3(eulerDeg.x*kDegToRad, eulerDeg.y*kDegToRad, eulerDeg.z*kDegToRad);
          sel->wireframe.Scale() = scl;

          auto comboString = [](const char* label, std::string& value, const char* const* options, int count) {
            int selected = 0;
            for (int i = 0; i < count; ++i) {
              if (value == options[i]) {
                selected = i;
                break;
              }
            }
            if (ImGui::Combo(label, &selected, options, count)) {
              value = options[selected];
              return true;
            }
            return false;
          };

          if (ImGui::CollapsingHeader("Physics Authoring", ImGuiTreeNodeFlags_DefaultOpen)) {
            t850::scene::SceneObjectPhysicsDesc& physics = EnsurePhysicsMeta(*sel);
            ImGui::Checkbox("Enable Physics", &physics.enabled);
            const char* bodyTypes[] = {
              "none",
              "static_triangle_mesh",
              "convex_hull",
              "box",
              "sphere",
              "capsule"
            };
            comboString("Body Type", physics.body_type, bodyTypes, (int)(sizeof(bodyTypes) / sizeof(bodyTypes[0])));
            const char* motions[] = { "static", "kinematic", "dynamic" };
            comboString("Motion", physics.motion, motions, (int)(sizeof(motions) / sizeof(motions[0])));
            InputTextString("Collision Layer", physics.collision_layer);
            ImGui::Checkbox("Generate Collision", &physics.generate_collision);
            InputTextString("Collision Asset", physics.collision_asset);
          }

          if (!selectedIsSkinned && ImGui::CollapsingHeader("Navigation Authoring", ImGuiTreeNodeFlags_DefaultOpen)) {
            t850::scene::SceneObjectNavigationDesc& navigation = EnsureNavigationMeta(*sel);
            ImGui::Checkbox("Include in NavMesh", &navigation.include);
            ImGui::Checkbox("Walkable", &navigation.walkable);
            ImGui::Checkbox("Static", &navigation.static_object);
            const char* areas[] = { "walkable", "not_walkable", "water", "door", "jump", "custom" };
            comboString("Area", navigation.area, areas, (int)(sizeof(areas) / sizeof(areas[0])));
            ImGui::DragFloat("Cost", &navigation.cost, 0.05f, 0.0f, 100.0f, "%.2f");
          }

          if (selectedIsSkinned) {
            DrawRagdollInspector(*sel);
          }
        }
        else if (g_selectionType == 1 && g_selectedIdx < (int)g_cameras.size()) {
          // Camera inspector
          SceneCamera& cam = g_cameras[g_selectedIdx];
          ImGui::SeparatorText("Camera");
          const char* types[] = { "Perspective", "Orthographic" };
          int t = (int)cam.type;
          if (ImGui::Combo("Type", &t, types, 2))
            cam.type = (CameraType)t;
          float cp[3] = {cam.position.x, cam.position.y, cam.position.z};
          if (ImGui::DragFloat3("Position", cp, 0.1f))
            cam.position = XVECTOR3(cp[0], cp[1], cp[2]);
          float ct[3] = {cam.target.x, cam.target.y, cam.target.z};
          if (ImGui::DragFloat3("Target", ct, 0.1f))
            cam.target = XVECTOR3(ct[0], ct[1], ct[2]);
          if (cam.type == CameraType::Perspective) {
            ImGui::DragFloat("FOV (deg)", &cam.fovDeg, 0.5f, 5.0f, 170.0f);
          } else {
            ImGui::DragFloat("Ortho Width", &cam.orthoW, 0.1f, 0.1f, 1000.0f);
            ImGui::DragFloat("Ortho Height", &cam.orthoH, 0.1f, 0.1f, 1000.0f);
          }
          ImGui::DragFloat("Near Plane", &cam.nearPlane, 0.01f, 0.001f, cam.farPlane - 0.01f);
          ImGui::DragFloat("Far Plane",  &cam.farPlane,  1.0f, cam.nearPlane + 0.01f, 100000.0f);
        }
        else if (g_selectionType == 2 && g_selectedIdx < (int)g_lights.size()) {
          // Light inspector
          SceneLight& lt = g_lights[g_selectedIdx];
          ImGui::SeparatorText("Light");
          const char* types[] = { "Directional", "Omni" };
          int t = (int)lt.type;
          if (ImGui::Combo("Type", &t, types, 2))
            lt.type = (EditorLightType)t;
          float lp[3] = {lt.position.x, lt.position.y, lt.position.z};
          if (ImGui::DragFloat3("Position", lp, 0.1f))
            lt.position = XVECTOR3(lp[0], lp[1], lp[2]);
          if (lt.type == EditorLightType::Directional) {
            float ld[3] = {lt.direction.x, lt.direction.y, lt.direction.z};
            if (ImGui::DragFloat3("Direction", ld, 0.01f)) {
              lt.direction = XVECTOR3(ld[0], ld[1], ld[2]);
              lt.direction.Normalize();
            }
          } else {
            ImGui::DragFloat("Radius", &lt.radius, 0.1f, 0.1f, 10000.0f);
          }
          float c[3] = {lt.color.x, lt.color.y, lt.color.z};
          if (ImGui::ColorEdit3("Color", c))
            lt.color = XVECTOR3(c[0], c[1], c[2]);
          ImGui::DragFloat("Intensity", &lt.intensity, 0.05f, 0.0f, 100.0f);
          ImGui::Checkbox("Enabled", &lt.enabled);
        }
      }
      ImGui::End();
    }

    if (m_panels.showRendering) {
      if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 676.0f, viewport->WorkPos.y + 8.0f), ImGuiCond_FirstUseEver);
      }
      ImGui::SetNextWindowSize(ImVec2(390, 560), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Rendering", &m_panels.showRendering)) {
        ImGuiClampCurrentWindowToEditorWorkArea();
        ImGui::TextDisabled("Editor viewport uses full deferred rendering on D3D/Vulkan.");
        ImGui::SeparatorText("Lighting");
        ImGui::Checkbox("Camera headlamp fallback", &m_editorHeadlampEnabled);
        bool pointLights = m_sceneProps.PointLightsEnabled;
        if (ImGui::Checkbox("Point lights", &pointLights)) m_sceneProps.PointLightsEnabled = pointLights;
        bool lightVolumes = m_sceneProps.DeferredLightVolumesEnabled;
        if (ImGui::Checkbox("Tiled/deferred light volumes", &lightVolumes)) m_sceneProps.DeferredLightVolumesEnabled = lightVolumes;
        ImGui::DragFloat("Light radius scale", &m_sceneProps.LightRadiusScale, 0.05f, 0.0f, 64.0f, "%.3f");
        ImGui::DragFloat("Light intensity scale", &m_sceneProps.LightIntensityScale, 0.05f, 0.0f, 64.0f, "%.3f");
        ImGui::TextDisabled("Lights: scene=%u packed=%u pointVolumes=%u activeTiles=%u",
                            m_sceneProps.DebugDeferredLightsSceneTotal,
                            m_sceneProps.DebugDeferredLightsPacked,
                            m_sceneProps.DebugDeferredLightsPointVolumes,
                            m_sceneProps.DebugDeferredLightActiveTiles);

        ImGui::SeparatorText("Shadows / SSAO");
        bool shadows = m_sceneProps.ToogleShadow != 0;
        if (ImGui::Checkbox("Shadows", &shadows)) m_sceneProps.ToogleShadow = shadows ? 1 : 0;
        bool ssao = m_sceneProps.ToogleSSAO != 0;
        if (ImGui::Checkbox("SSAO", &ssao)) m_sceneProps.ToogleSSAO = ssao ? 1 : 0;
        ImGui::DragFloat("Shadow bias", &m_sceneProps.ShadowBias, 0.0001f, 0.0f, 0.1f, "%.6f");
        ImGui::DragFloat("Shadow min", &m_sceneProps.ShadowMin, 0.01f, 0.0f, 1.0f, "%.3f");
        ImGui::DragFloat("PCF radius", &m_sceneProps.PCFScale, 0.05f, 0.0f, 16.0f, "%.3f");
        ImGui::DragFloat("PCF samples", &m_sceneProps.PCFSamples, 0.25f, 0.0f, 16.0f, "%.2f");
        int ssaoKernel = m_sceneProps.SSAOKernel.KernelSize;
        if (ImGui::SliderInt("SSAO kernel", &ssaoKernel, 1, 64)) {
          m_sceneProps.SSAOKernel.KernelSize = ssaoKernel;
          m_sceneProps.SSAOKernel.Update();
        }
        if (ImGui::DragFloat("SSAO radius", &m_sceneProps.SSAOKernel.Radius, 0.05f, 0.0f, 16.0f, "%.3f")) {
          m_sceneProps.SSAOKernel.Update();
        }

        ImGui::SeparatorText("HDR / Bloom / IBL");
        ImGui::DragFloat("Exposure", &m_sceneProps.Exposure, 0.05f, -10.0f, 10.0f, "%.3f");
        ImGui::DragFloat("Bloom factor", &m_sceneProps.BloomFactor, 0.025f, 0.0f, 4.0f, "%.3f");
        ImGui::DragFloat("Bloom threshold", &m_sceneProps.BloomThreshold, 0.05f, 0.0f, 10.0f, "%.3f");
        ImGui::DragFloat("Tone-map white", &m_sceneProps.ToneMapWhiteLevel, 0.1f, 0.1f, 32.0f, "%.3f");
        ImGui::DragFloat("Luminance tau", &m_sceneProps.LuminanceTau, 0.05f, 0.0f, 10.0f, "%.3f");
        ImGui::DragFloat("Env factor", &m_sceneProps.EnvFactor, 0.05f, 0.0f, 8.0f, "%.3f");
        ImGui::DragFloat("IBL factor", &m_sceneProps.IBLFactor, 0.05f, 0.0f, 8.0f, "%.3f");
        ImGui::DragFloat("IBL mip count", &m_sceneProps.IBLMipCount, 0.05f, 0.0f, 12.0f, "%.3f");
      }
      ImGui::End();
    }

    if (m_panels.showConsole)
      ImGuiDrawConsolePanel();

    if (m_panels.showRTDebug)
      g_debugRT = ImGuiDrawRTDebugPanel(g_debugRT);

    DrawRagdollEditorWindow();
    DrawMeshEditorWindow();
    DrawPlaySceneWindow();

    T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender...");
    ImGuiRender();
    T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender done");
  }

after_editor_imgui:

  // Frame dump (space key) — dump all render graph RTs
  if (g_dumperInited && g_dumper.ShouldDump(m_dtSecs)) {
    int gbuf = g_renderGraph.GetRTHandle("GBuffer");
    int def  = g_renderGraph.GetRTHandle("Deferred");
    std::vector<t850::RTDumpEntry> rts;
    if (gbuf >= 0) {
      rts.push_back({gbuf, t850::BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR2_ATTACHMENT, "GBuffer_PBR"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR3_ATTACHMENT, "GBuffer_GeoNormals"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Emissive"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR5_ATTACHMENT, "GBuffer_Sheen"});
      rts.push_back({gbuf, t850::BaseDriver::COLOR6_ATTACHMENT, "GBuffer_SpecularOcclusion"});
      rts.push_back({gbuf, t850::BaseDriver::DEPTH_ATTACHMENT,  "GBuffer_Depth"});
    }
    if (def >= 0) {
      rts.push_back({def, t850::BaseDriver::COLOR0_ATTACHMENT, "Deferred_Output"});
    }
    ::Camera dummyLightCam;
    g_dumper.DumpFrame(drv, m_camera.GetCameraMutable(), dummyLightCam, m_sceneProps, rts, m_dtSecs);
    T8_LOG_INFO("[T8ditor] Frame dumped to disk");
    if (g_dumper.ShouldExit()) exit(0);
  }

  T8_LOG_TRACE("[T8ditor] OnDraw: SwapBuffers...");
  drv->SwapBuffers();
  T8_LOG_TRACE("[T8ditor] OnDraw: EndFrame...");
  drv->EndFrame();
  T8_LOG_TRACE("[T8ditor] OnDraw: done");
}

void EditorApp::OnPause()  { bPaused = true;  }
void EditorApp::OnResume() { bPaused = false; }
void EditorApp::OnReset()  {}
void EditorApp::LoadScene(int) {}

} // namespace t8ditor
