/*********************************************************
 * T8ditor - EditorApp implementation. See header.
 *********************************************************/

#include "EditorApp.h"
#include "SceneObject.h"
#include "SceneGraph.h"
#include "EditorScene.h"
#include "EditorSceneGizmos.h"
#include "UndoRedo.h"
#include "EditorMath.h"
#include "EditorUtil.h"
#include "EditorSceneSerialization.h"
#include "EditorWorld.h"
#include "EditorInternal.h"
#include "EditorRagdollSupport.h"
#include "../DayScene/RagdollEditor.h"
#include "../DayScene/SceneTemplate.h"

#include <core/Core.h>
#include <core/EngineContext.h>
#include <video/BaseDriver.h>
#include <physics/PhysicsAuthoring.h>
#include <physics/Q3BspCollision.h>
#include <physics/RagdollEditorTool.h>
#include <scene/IBLResources.h>
#include <scene/MeshAssetCache.h>
#include <scene/RenderGraph.h>
#include <scene/RenderMesh.h>
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
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <set>
#include <map>
#include <unordered_map>
#include <cstring>
#include <thread>

#include <imgui.h>
#include <ImGuizmo.h>
#include <SDL3/SDL.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4267)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef OS_WINDOWS
#include <core/windows/Win32Framework.h>
#endif

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;
}

namespace t8ditor {

EditorApp::~EditorApp() = default;

ImVec2 WorldToScreen(const XVECTOR3& p, const XMATRIX44& vp, int w, int h);
bool ProjectAABBToScreenRect(const t850::AABB& box, const XMATRIX44& vp,
                                    int viewW, int viewH,
                                    float& sMinX, float& sMinY,
                                    float& sMaxX, float& sMaxY);
t850::Ray BuildEditorCameraRay(const ::Camera& camera,
                                      float mouseX,
                                      float mouseY,
                                      int viewW,
                                      int viewH);
static bool BuildRuntimeSpline(const t850::scene::SceneSplineDesc& desc, t850::Spline& outSpline);
static bool ApplyEditorSplineToAttachedCamera(t850::scene::SceneSplineDesc& desc);
static bool ApplyEditorSplineAgentToAttachedCamera(const t850::scene::SceneSplineDesc& desc,
                                                   const t850::SplineAgent& agent);
static float EstimateEditorSplineDurationSeconds(const t850::scene::SceneSplineDesc& desc);
static int FindDefaultTimelineSplineIndex(int preferredIndex);
static void ApplyEditorCameraAnimationsAtTime(float timeSeconds);
static void ApplySceneLightCameraToRuntimeCamera(const t850::scene::SceneLightCameraDesc& desc,
                                                 ::Camera& camera,
                                                 float perspectiveAspect);
static void ApplySceneCameraToRuntimeCamera(const SceneCamera& desc,
                                            ::Camera& camera,
                                            float perspectiveAspect);
static void SyncSceneCameraFromRuntimeCamera(const ::Camera& runtimeCamera, SceneCamera& desc);
static void SyncLightCameraFromRuntimeCamera(const ::Camera& runtimeCamera,
                                             t850::scene::SceneLightCameraDesc& desc);
static bool HandleActiveCameraOrbitControls(InputManager& input,
                                            float dtSecs,
                                            float wheelDelta,
                                            bool skipMouse,
                                            bool skipKeyboard,
                                            float perspectiveAspect);
static bool ManipulateCameraEntityGizmo(const ::Camera& viewCamera,
                                        int mode,
                                        XVECTOR3& position,
                                        XVECTOR3& target,
                                        bool orthographic,
                                        float& orthoW,
                                        float& orthoH,
                                        float& fovDeg);
static bool TranslateCameraPointGizmo(const ::Camera& viewCamera,
                                      int gizmoId,
                                      XVECTOR3& point);

struct EditorUndoState {
  SceneFile scene;
  std::vector<SceneGroup> groups;
  std::set<int> multiSelect;
  int activeGroupIdx = -1;
  int selectionType = 0;
  int selectedIdx = -1;
  int selectedSplinePoint = -1;
  int selectedNavVolume = -1;
  int selectedNavLink = -1;
  int activeCameraIdx = -1;
};

namespace {
  std::string g_startupMeshPath;
  std::string g_startupSceneFilePath;
  int g_startupDumpFrame = -1;
  constexpr float kScaleDragSpeed = 0.0001f;

  // All authored scene data lives in a single EditorWorld instance (EditorWorld.h).
  // The g_* names below are aliases bound to its members, so existing call sites
  // keep working while the editor is split into smaller translation units.
  EditorWorld& g_world = GetEditorWorld();

  // Persistent skybox (editor backdrop, separate from scene meshes).
  t850::PrimitiveManager g_skyboxMgr;
  t850::PrimitiveInst    g_skyboxInst;
  int                    g_skyboxPrimId = -1;
  bool                   g_skyboxReady  = false;

  // Scene data now lives in EditorWorld (EditorWorld.h); g_* are aliases.
  auto& g_objects         = g_world.objects;
  auto& g_selectedIdx     = g_world.selectedIdx;
  auto& g_physicsEntities = g_world.physicsEntities;
  t850::PhysicsTriangleMeshCookSettings g_triangleMeshCookSettings;
  float g_triangleMeshFriction = 0.6f;
  float g_triangleMeshRestitution = 0.0f;
  bool g_triangleMeshSensor = false;
  std::string g_triangleMeshStatus;
  PhysicsSceneEntity g_meshCharacterAuthoringTemplate;
  int g_meshCharacterAuthoringSourceIndex = -1;
  bool g_meshCharacterAuthoringInitialized = false;

  // Multi-selection: legacy mesh set plus typed refs for mixed mesh/physics selection.
  auto& g_multiSelect       = g_world.multiSelect;
  auto& g_multiEntitySelect = g_world.multiEntitySelect;

  // Groups (persistent and temporary)
  auto& g_groups         = g_world.groups;
  auto& g_tempGroup      = g_world.tempGroup;
  auto& g_activeGroupIdx = g_world.activeGroupIdx;

  // Cameras and lights in the scene
  auto& g_cameras = g_world.cameras;
  auto& g_lights  = g_world.lights;
  auto& g_lightCameras = g_world.lightCameras;
  auto& g_cameraAnimations = g_world.cameraAnimations;
  auto& g_lightCameraGizmoCameras = g_world.lightCameraGizmoCameras;
  auto& g_godRaysVolume = g_world.godRaysVolume;
  auto& g_godRaysVolumeGizmo = g_world.godRaysVolumeGizmo;
  std::vector<GizmoCache> g_navMeshVolumeGizmos;
  std::array<GizmoCache, 5> g_navMeshClassificationGizmos;
  auto& g_splines = g_world.splines;
  auto& g_splineGizmos = g_world.splineGizmos;
  auto& g_selectedSplinePoint = g_world.selectedSplinePoint;

  // Selection: 0=mesh, 1=camera, 2=light, 3=physics entity, 4=NavMesh, 5=spline, 6=light camera, 7=spline point, 8=God Rays volume.
  auto& g_selectionType = g_world.selectionType;

  // Marquee box selection state
  bool     g_marqueeActive = false;
  ImVec2   g_marqueeStart  = {0, 0};
  ImVec2   g_marqueeStartScreen = {0, 0};

  // Active camera index (-1 = default editor camera)
  auto& g_activeCameraIdx = g_world.activeCameraIdx;

  // Undo/redo
  auto& g_undoStack = g_world.undoStack;
  bool g_applyingUndoState = false;

  // ImGuizmo drag tracking
  bool           g_gizmoDragging = false;
  TransformState g_gizmoDragStart;
  XVECTOR3       g_physicsGizmoStartHalfExtents = XVECTOR3(16.0f, 32.0f, 16.0f, 0.0f);
  float          g_physicsGizmoStartRadius = 16.0f;
  float          g_physicsGizmoStartHalfHeight = 24.0f;
  std::vector<TransformState> g_lastSceneObjectTransforms;
  std::vector<std::string> g_lastSceneObjectTransformNames;

  // Persistent camera for scene camera viewport switching
  ::Camera g_viewCamera;

  // Deferred render graph
  t850::RenderGraph   g_renderGraph;
  t850::PrimitiveInst g_quads[8];
  bool                g_deferredReady = false;

  bool RenderGraphSupportsGodRays(const t850::RenderGraph& graph) {
    const t850::RenderGraphDesc& desc = graph.GetDescriptor();
    for (const t850::RenderPassDesc& pass : desc.passes) {
      for (const t850::DrawCmd& draw : pass.draws) {
        if (draw.signature == "LIGHT_RAY_MARCHING" || draw.signature == "LIGHT_ADD") {
          return true;
        }
        for (const std::string& extra : draw.extra_signatures) {
          if (extra == "LIGHT_RAY_MARCHING" || extra == "LIGHT_ADD") {
            return true;
          }
        }
      }
    }
    return false;
  }

  bool GodRaysVolumeIsAuthored(const t850::scene::SceneGodRaysVolumeDesc& volume) {
    return volume.authored || volume.enabled || volume.visible || volume.clip_enabled;
  }

  bool IsValidGodRaysLightCamera(int index) {
    return index >= 0 &&
           index < static_cast<int>(g_lightCameras.size()) &&
           g_lightCameras[static_cast<std::size_t>(index)].enabled;
  }

  int FirstEnabledGodRaysLightCamera() {
    for (int i = 0; i < static_cast<int>(g_lightCameras.size()); ++i) {
      if (g_lightCameras[static_cast<std::size_t>(i)].enabled) {
        return i;
      }
    }
    return -1;
  }

  void ClampGodRaysVolumeLightCamera(t850::scene::SceneGodRaysVolumeDesc& volume) {
    if (g_lightCameras.empty()) {
      volume.light_camera = -1;
      return;
    }
    volume.light_camera = std::clamp(volume.light_camera, 0, static_cast<int>(g_lightCameras.size()) - 1);
  }

  bool GodRaysVolumeCanBeActive(const t850::scene::SceneGodRaysVolumeDesc& volume,
                                const t850::RenderGraph& graph) {
    return GodRaysVolumeIsAuthored(volume) &&
           RenderGraphSupportsGodRays(graph) &&
           IsValidGodRaysLightCamera(volume.light_camera);
  }
  XMATRIX44           g_quadVP;  // persistent identity matrix for screen-space quads

  // RT debug: which RT attachment to display (-1 = backbuffer)
  int g_debugRT = -1;
  t850::Texture* g_debugRTTexture = nullptr;

  // Dummy 1x1 white texture for shadow slot
  t850::Texture* g_dummyWhiteTex = nullptr;

  // Dummy environment map (1x1 gray cube for skybox matID=0)
  int g_dummyEnvMapIdx = -1;

  // Pending scene load — deferred to execute before next frame's BeginFrame
  std::string g_pendingLoadPath;
  std::string g_pendingDeleteAfterLoadPath;
  auto& g_loadedSceneFile = g_world.loadedSceneFile;
  auto& g_hasLoadedSceneFile = g_world.hasLoadedSceneFile;
  auto& g_unloadedSceneObjects = g_world.unloadedSceneObjects;
  auto& g_sceneCollisionResourcePath = g_world.sceneCollisionResourcePath;
  auto& g_sceneProfiles = g_world.sceneProfiles;
  auto& g_gameEntities = g_world.gameEntities;
  auto& g_q3CollisionWorld = g_world.q3CollisionWorld;

  // Frame dumper for RT snapshot debugging (space key)
  t850::FrameDumper g_dumper;
  bool              g_dumperInited = false;
  int               g_editorResizeInputTraceFrames = 0;
  bool              g_resetArtistLayout = false;

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

  void AppendEditorUndoGroupKey(std::ostringstream& oss, const EditorUndoState& state) {
    oss << "|groups=" << state.groups.size();
    for (const SceneGroup& group : state.groups) {
      oss << "|g:" << group.name << ':' << (group.persistent ? 1 : 0) << ':';
      for (int member : group.members) {
        oss << member << ',';
      }
    }
    oss << "|activeGroup=" << state.activeGroupIdx;
  }

  std::string EditorUndoStateKey(const EditorUndoState& state) {
    auto serialized = glz::write<glz::opts{.prettify = false}>(state.scene);
    std::ostringstream oss;
    if (serialized) {
      oss << serialized.value();
    } else {
      oss << "scene-serialize-error";
    }

    AppendEditorUndoGroupKey(oss, state);
    return oss.str();
  }

  void ReleaseGizmoCache(GizmoCache& cache) {
    if (cache.vb) {
      cache.vb->release();
      cache.vb = nullptr;
    }
    if (cache.ib) {
      cache.ib->release();
      cache.ib = nullptr;
    }
    cache.count = 0;
    cache.hash = 0;
  }

  void ReleaseGizmoCaches(std::vector<GizmoCache>& caches) {
    for (GizmoCache& cache : caches) {
      ReleaseGizmoCache(cache);
    }
    caches.clear();
  }

  void ReleaseNavMeshClassificationGizmos() {
    for (GizmoCache& cache : g_navMeshClassificationGizmos) {
      ReleaseGizmoCache(cache);
    }
  }

  void ReleaseCameraGizmoCaches(std::vector<SceneCamera>& cameras) {
    for (SceneCamera& camera : cameras) {
      ReleaseGizmoCache(camera.gizmo);
    }
    cameras.clear();
  }

  class EditorSceneStateCommand : public UndoCommand {
  public:
    using ApplyFn = std::function<void(const EditorUndoState&)>;

    EditorSceneStateCommand(std::string description,
                            EditorUndoState before,
                            EditorUndoState after,
                            ApplyFn applyFn)
        : m_description(std::move(description)),
          m_before(std::move(before)),
          m_after(std::move(after)),
          m_applyFn(std::move(applyFn)) {}

    void Apply() override { m_applyFn(m_after); }
    void Undo() override { m_applyFn(m_before); }
    const char* Description() const override { return m_description.c_str(); }

  private:
    std::string m_description;
    EditorUndoState m_before;
    EditorUndoState m_after;
    ApplyFn m_applyFn;
  };
}

void SetStartupMeshPath(const std::string& p) {
  g_startupMeshPath = p;
}

void SetStartupSceneFilePath(const std::string& p) {
  g_startupSceneFilePath = p;
}

void SetStartupDumpFrame(int frame) {
  g_startupDumpFrame = frame;
}

// Accessors exposing the deferred-render scratch globals (anonymous-namespace
// storage above) to RagdollEditorPanel.cpp's hosted viewport.
EditorQuadArray& EditorDeferredQuads() { return g_quads; }
t850::Texture*& EditorDummyWhiteTex() { return g_dummyWhiteTex; }
int& EditorDummyEnvMapIdx() { return g_dummyEnvMapIdx; }

// Helpers to access current selection
static SceneObject* SelectedObject() {
  if (g_selectionType == 0 && g_selectedIdx >= 0 && g_selectedIdx < (int)g_objects.size())
    return &g_objects[g_selectedIdx];
  return nullptr;
}

static PhysicsSceneEntity* SelectedPhysicsEntity() {
  if (g_selectionType == 3 && g_selectedIdx >= 0 && g_selectedIdx < (int)g_physicsEntities.size())
    return &g_physicsEntities[g_selectedIdx];
  return nullptr;
}

static bool IsValidSelectionRef(const SelectionRef& ref) {
  if (ref.type == 0) return ref.index >= 0 && ref.index < static_cast<int>(g_objects.size());
  if (ref.type == 3) return ref.index >= 0 && ref.index < static_cast<int>(g_physicsEntities.size());
  return false;
}

static int EncodeActiveLightCameraIndex(int index) {
  return -2 - index;
}

static bool DecodeActiveLightCameraIndex(int encoded, int& outIndex) {
  if (encoded > -2) {
    outIndex = -1;
    return false;
  }
  outIndex = -2 - encoded;
  return true;
}

static bool IsActiveCameraIndexValid(int activeCameraIdx) {
  if (activeCameraIdx == -1) return true;
  if (activeCameraIdx >= 0) return activeCameraIdx < static_cast<int>(g_cameras.size());
  int lightCameraIndex = -1;
  return DecodeActiveLightCameraIndex(activeCameraIdx, lightCameraIndex) &&
      lightCameraIndex >= 0 &&
      lightCameraIndex < static_cast<int>(g_lightCameras.size());
}

static void AdjustActiveLightCameraAfterErase(int erasedIndex) {
  int activeLightCameraIndex = -1;
  if (!DecodeActiveLightCameraIndex(g_activeCameraIdx, activeLightCameraIndex)) {
    return;
  }
  if (activeLightCameraIndex == erasedIndex) {
    g_activeCameraIdx = -1;
  } else if (activeLightCameraIndex > erasedIndex) {
    g_activeCameraIdx = EncodeActiveLightCameraIndex(activeLightCameraIndex - 1);
  }
}

static void AdjustGodRaysLightCameraAfterErase(int erasedIndex) {
  if (!GodRaysVolumeIsAuthored(g_godRaysVolume)) {
    return;
  }
  if (g_godRaysVolume.light_camera == erasedIndex) {
    g_godRaysVolume.enabled = false;
    g_godRaysVolume.visible = false;
    g_godRaysVolume.light_camera = -1;
  } else if (g_godRaysVolume.light_camera > erasedIndex) {
    --g_godRaysVolume.light_camera;
  }
}

void ClearMixedSelection() {
  g_multiSelect.clear();
  g_multiEntitySelect.clear();
}

void AddMixedSelection(int type, int index) {
  SelectionRef ref{type, index};
  if (!IsValidSelectionRef(ref)) return;
  g_multiEntitySelect.insert(ref);
  if (type == 0) g_multiSelect.insert(index);
}

static void ToggleMixedSelection(int type, int index) {
  SelectionRef ref{type, index};
  if (!IsValidSelectionRef(ref)) return;
  auto it = g_multiEntitySelect.find(ref);
  if (it != g_multiEntitySelect.end()) {
    g_multiEntitySelect.erase(it);
    if (type == 0) g_multiSelect.erase(index);
  } else {
    AddMixedSelection(type, index);
  }
}

static void SetSingleSelection(int type, int index) {
  g_selectedIdx = index;
  g_selectionType = type;
  if (type != 7) {
    g_selectedSplinePoint = -1;
  }
  ClearMixedSelection();
  AddMixedSelection(type, index);
}

static std::vector<SelectionRef> CurrentCloneSelection() {
  std::vector<SelectionRef> refs;
  for (const SelectionRef& ref : g_multiEntitySelect) {
    if (IsValidSelectionRef(ref)) refs.push_back(ref);
  }
  if (refs.empty() && g_selectedIdx >= 0) {
    SelectionRef ref{g_selectionType, g_selectedIdx};
    if (IsValidSelectionRef(ref)) refs.push_back(ref);
  }
  return refs;
}

static bool IsMixedSelected(int type, int index) {
  return g_multiEntitySelect.find(SelectionRef{type, index}) != g_multiEntitySelect.end();
}

void InvalidateSceneObjectTransformSnapshots() {
  g_lastSceneObjectTransforms.clear();
  g_lastSceneObjectTransformNames.clear();
}

static std::string MakeUniquePhysicsEntityName(const std::string& baseName) {
  const std::string base = baseName.empty() ? "Static Triangle Mesh" : baseName;
  std::string candidate = base;
  int suffix = 1;
  auto exists = [&](const std::string& name) {
    for (const PhysicsSceneEntity& entity : g_physicsEntities) {
      if (entity.name == name) return true;
    }
    return false;
  };
  while (exists(candidate)) {
    candidate = base + " " + std::to_string(++suffix);
  }
  return candidate;
}

static int FindPlayerPhysicsEntityIndex() {
  for (int i = 0; i < static_cast<int>(g_physicsEntities.size()); ++i) {
    if (g_physicsEntities[i].type == PhysicsSceneEntityType::Player) {
      return i;
    }
  }
  return -1;
}

static float EstimateSceneScaleForPlayer() {
  t850::AABB combined;
  for (const SceneObject& object : g_objects) {
    if (!object.visible || !object.wireframe.IsLoaded()) {
      continue;
    }
    const t850::AABB bounds = object.wireframe.WorldAABB();
    if (bounds.IsValid()) {
      combined.ExpandToInclude(bounds);
    }
  }
  if (!combined.IsValid()) {
    return 1.0f;
  }
  const XVECTOR3 extents = combined.Extents();
  const float maxExtent = (std::max)(extents.x, (std::max)(extents.y, extents.z));
  if (!std::isfinite(maxExtent) || maxExtent <= 0.001f) {
    return 1.0f;
  }
  return maxExtent;
}

static void ApplyDefaultPlayerSizeFromScene(PhysicsSceneEntity& entity) {
  const float sceneExtent = EstimateSceneScaleForPlayer();
  const float height = std::clamp(sceneExtent * 0.020f, 0.5f, 3.0f);
  const float width = height * 0.32f;
  entity.playerShape = t850::PhysicsShapeType::Box;
  entity.playerHalfExtents = XVECTOR3(width * 0.5f, height * 0.5f, width * 0.5f, 0.0f);
  entity.playerRadius = width * 0.5f;
  entity.playerHalfHeight = (std::max)(1.0f, height * 0.5f - entity.playerRadius);
}

static void AppendBoxTriangles(std::vector<XVECTOR3>& vertices,
                               std::vector<unsigned int>& indices,
                               float minX,
                               float minY,
                               float minZ,
                               float maxX,
                               float maxY,
                               float maxZ) {
  const unsigned int base = static_cast<unsigned int>(vertices.size());
  vertices.emplace_back(minX, minY, minZ, 1.0f);
  vertices.emplace_back(maxX, minY, minZ, 1.0f);
  vertices.emplace_back(maxX, maxY, minZ, 1.0f);
  vertices.emplace_back(minX, maxY, minZ, 1.0f);
  vertices.emplace_back(minX, minY, maxZ, 1.0f);
  vertices.emplace_back(maxX, minY, maxZ, 1.0f);
  vertices.emplace_back(maxX, maxY, maxZ, 1.0f);
  vertices.emplace_back(minX, maxY, maxZ, 1.0f);
  static constexpr unsigned int faces[] = {
      0, 1, 2, 0, 2, 3,
      4, 6, 5, 4, 7, 6,
      0, 4, 5, 0, 5, 1,
      3, 2, 6, 3, 6, 7,
      0, 3, 7, 0, 7, 4,
      1, 5, 6, 1, 6, 2,
  };
  indices.reserve(indices.size() + sizeof(faces) / sizeof(faces[0]));
  for (unsigned int index : faces) {
    indices.push_back(base + index);
  }
}

static bool BuildPlayerSilhouetteMesh(PhysicsSceneEntity& entity) {
  const float height = entity.playerShape == t850::PhysicsShapeType::Capsule
      ? (entity.playerHalfHeight + entity.playerRadius) * 2.0f
      : entity.playerShape == t850::PhysicsShapeType::Sphere
          ? entity.playerRadius * 2.0f
          : entity.playerShape == t850::PhysicsShapeType::Cylinder
              ? entity.playerHalfHeight * 2.0f
      : entity.playerHalfExtents.y * 2.0f;
  const float width = entity.playerShape == t850::PhysicsShapeType::Capsule
      ? entity.playerRadius * 2.0f
      : entity.playerShape == t850::PhysicsShapeType::Sphere || entity.playerShape == t850::PhysicsShapeType::Cylinder
          ? entity.playerRadius * 2.0f
      : entity.playerHalfExtents.x * 2.0f;
  const float depth = entity.playerShape == t850::PhysicsShapeType::Capsule
      ? entity.playerRadius * 1.2f
      : entity.playerShape == t850::PhysicsShapeType::Sphere || entity.playerShape == t850::PhysicsShapeType::Cylinder
          ? entity.playerRadius * 1.2f
      : entity.playerHalfExtents.z * 2.0f;
  if (height <= 0.001f || width <= 0.001f || depth <= 0.001f) {
    return false;
  }

  const float bottom = -height * 0.5f;
  const float legTop = bottom + height * 0.45f;
  const float torsoTop = bottom + height * 0.78f;
  const float top = height * 0.5f;
  const float halfDepth = depth * 0.5f;
  std::vector<XVECTOR3> vertices;
  std::vector<unsigned int> indices;
  vertices.reserve(48);
  indices.reserve(216);

  const float legHalfWidth = width * 0.14f;
  const float legOffset = width * 0.16f;
  AppendBoxTriangles(vertices, indices, -legOffset - legHalfWidth, bottom, -halfDepth * 0.45f, -legOffset + legHalfWidth, legTop, halfDepth * 0.45f);
  AppendBoxTriangles(vertices, indices,  legOffset - legHalfWidth, bottom, -halfDepth * 0.45f,  legOffset + legHalfWidth, legTop, halfDepth * 0.45f);
  AppendBoxTriangles(vertices, indices, -width * 0.32f, legTop, -halfDepth * 0.55f, width * 0.32f, torsoTop, halfDepth * 0.55f);
  AppendBoxTriangles(vertices, indices, -width * 0.18f, torsoTop, -halfDepth * 0.45f, width * 0.18f, top, halfDepth * 0.45f);
  AppendBoxTriangles(vertices, indices, -width * 0.55f, legTop + height * 0.10f, -halfDepth * 0.35f, -width * 0.35f, torsoTop, halfDepth * 0.35f);
  AppendBoxTriangles(vertices, indices,  width * 0.35f, legTop + height * 0.10f, -halfDepth * 0.35f,  width * 0.55f, torsoTop, halfDepth * 0.35f);

  entity.visual = std::make_unique<EditorMesh>();
  const std::string visualName = entity.name.empty() ? "character silhouette" : entity.name + " silhouette";
  if (!entity.visual->LoadFromTriangles(visualName, vertices, indices)) {
    entity.visual.reset();
    return false;
  }
  entity.visual->Position() = entity.position;
  entity.visual->EulerRadians() = entity.eulerRadians;
  entity.visual->WireColor = XVECTOR3(0.2f, 0.8f, 1.0f, 1.0f);
  return true;
}

static XVECTOR3 SceneObjectWorldPosition(SceneObject& object) {
  if (object.primId >= 0) {
    return XVECTOR3(object.litInst.Final.m41,
                    object.litInst.Final.m42,
                    object.litInst.Final.m43,
                    1.0f);
  }
  return object.wireframe.Position();
}

static XVECTOR3 SceneObjectWorldEulerRadians(SceneObject& object) {
  return object.wireframe.EulerRadians();
}

static bool IsRenderMeshBoundsValid(const t850::RenderMesh::AABB& bounds) {
  return bounds.min.x <= bounds.max.x &&
         bounds.min.y <= bounds.max.y &&
         bounds.min.z <= bounds.max.z &&
         std::isfinite(bounds.min.x) &&
         std::isfinite(bounds.min.y) &&
         std::isfinite(bounds.min.z) &&
         std::isfinite(bounds.max.x) &&
         std::isfinite(bounds.max.y) &&
         std::isfinite(bounds.max.z);
}

static t850::AABB RenderMeshAABBToPickingAABB(const t850::RenderMesh::AABB& bounds) {
  return t850::AABB(
      XVECTOR3(bounds.min.x, bounds.min.y, bounds.min.z, 1.0f),
      XVECTOR3(bounds.max.x, bounds.max.y, bounds.max.z, 1.0f));
}

static bool GetSceneObjectWorldAABB(SceneObject& object, t850::AABB& outBounds) {
  outBounds = t850::AABB{};
  if (object.primId >= 0 && object.litInst.pBase) {
    if (auto* skinned = dynamic_cast<t850::RenderSkinnedMesh*>(object.litInst.pBase)) {
      t850::RenderMesh::AABB currentPoseBounds;
      if (skinned->GetCurrentPoseLocalAABB(currentPoseBounds) &&
          IsRenderMeshBoundsValid(currentPoseBounds)) {
        outBounds = RenderMeshAABBToPickingAABB(currentPoseBounds).Transformed(object.litInst.Final);
        return outBounds.IsValid();
      }
    }
    if (auto* renderMesh = dynamic_cast<t850::RenderMesh*>(object.litInst.pBase)) {
      if (renderMesh->EnsureCullingMetadata()) {
        for (const t850::RenderMesh::MeshInfo& info : renderMesh->Info) {
          if (IsRenderMeshBoundsValid(info.bounds)) {
            outBounds.ExpandToInclude(RenderMeshAABBToPickingAABB(info.bounds).Transformed(object.litInst.Final));
          }
        }
        if (outBounds.IsValid()) {
          return true;
        }
      }
    }
    if (auto* skinned = dynamic_cast<t850::RenderSkinnedMesh*>(object.litInst.pBase)) {
      t850::RenderMesh::AABB skeletonBounds;
      if (skinned->GetSkeletonLocalAABB(skeletonBounds) && IsRenderMeshBoundsValid(skeletonBounds)) {
        outBounds = RenderMeshAABBToPickingAABB(skeletonBounds).Transformed(object.litInst.Final);
        return outBounds.IsValid();
      }
    }
  }
  if (object.wireframe.IsLoaded()) {
    outBounds = object.wireframe.WorldAABB();
    return outBounds.IsValid();
  }
  return false;
}

static bool FitCharacterToAABB(PhysicsSceneEntity& entity, const t850::AABB& bounds) {
  if (!bounds.IsValid()) {
    return false;
  }
  const XVECTOR3 center = bounds.Center();
  const XVECTOR3 extents = bounds.Extents();
  entity.position = XVECTOR3(center.x, center.y, center.z, 1.0f);
  entity.eulerRadians = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  entity.playerHalfExtents = XVECTOR3(
      (std::max)(0.001f, extents.x),
      (std::max)(0.001f, extents.y),
      (std::max)(0.001f, extents.z),
      0.0f);
  const float radius = (std::max)(0.001f, (std::max)(extents.x, extents.z));
  entity.playerRadius = radius;
  if (entity.playerShape == t850::PhysicsShapeType::Sphere) {
    entity.playerRadius = (std::max)(radius, extents.y);
    entity.playerHalfHeight = entity.playerRadius;
  } else if (entity.playerShape == t850::PhysicsShapeType::Cylinder) {
    entity.playerHalfHeight = (std::max)(0.001f, extents.y);
  } else if (entity.playerShape == t850::PhysicsShapeType::Capsule) {
    entity.playerHalfHeight = (std::max)(0.001f, extents.y - entity.playerRadius);
  }
  return true;
}

static bool FitCharacterToSceneObject(PhysicsSceneEntity& entity, SceneObject& object) {
  t850::AABB bounds;
  if (!GetSceneObjectWorldAABB(object, bounds)) {
    return false;
  }
  return FitCharacterToAABB(entity, bounds);
}

static const char* CharacterRuntimePathToSceneString(int runtimePath) {
  return runtimePath == static_cast<int>(CharacterRuntimePath::Jolt) ? "jolt" : "kinematic";
}

static int CharacterRuntimePathFromSceneString(const std::string& runtimePath) {
  return runtimePath == "jolt"
      ? static_cast<int>(CharacterRuntimePath::Jolt)
      : static_cast<int>(CharacterRuntimePath::Kinematic);
}

static void CopyCharacterAuthoringSettings(const PhysicsSceneEntity& source, PhysicsSceneEntity& dest) {
  dest.position = source.position;
  dest.eulerRadians = source.eulerRadians;
  dest.playerShape = source.playerShape;
  dest.playerHalfExtents = source.playerHalfExtents;
  dest.playerRadius = source.playerRadius;
  dest.playerHalfHeight = source.playerHalfHeight;
  dest.friction = source.friction;
  dest.restitution = source.restitution;
  dest.sensor = source.sensor;
  dest.characterRuntimePath = source.characterRuntimePath;
  dest.characterImplementation = source.characterImplementation;
  dest.characterMass = source.characterMass;
  dest.characterMaxStrength = source.characterMaxStrength;
  dest.characterMaxSlopeAngleDeg = source.characterMaxSlopeAngleDeg;
  dest.characterEnhancedInternalEdgeRemoval = source.characterEnhancedInternalEdgeRemoval;
  dest.characterSupportingVolumeOffset = source.characterSupportingVolumeOffset;
  dest.characterShapeOffset[0] = source.characterShapeOffset[0];
  dest.characterShapeOffset[1] = source.characterShapeOffset[1];
  dest.characterShapeOffset[2] = source.characterShapeOffset[2];
  dest.characterBackFaceMode = source.characterBackFaceMode;
  dest.characterPredictiveContactDistance = source.characterPredictiveContactDistance;
  dest.characterMaxCollisionIterations = source.characterMaxCollisionIterations;
  dest.characterMaxConstraintIterations = source.characterMaxConstraintIterations;
  dest.characterMinTimeRemaining = source.characterMinTimeRemaining;
  dest.characterCollisionTolerance = source.characterCollisionTolerance;
  dest.characterPadding = source.characterPadding;
  dest.characterMaxNumHits = source.characterMaxNumHits;
  dest.characterHitReductionCosMaxAngle = source.characterHitReductionCosMaxAngle;
  dest.characterPenetrationRecoverySpeed = source.characterPenetrationRecoverySpeed;
  dest.characterGravityFactor = source.characterGravityFactor;
  dest.characterAllowTranslationX = source.characterAllowTranslationX;
  dest.characterAllowTranslationY = source.characterAllowTranslationY;
  dest.characterAllowTranslationZ = source.characterAllowTranslationZ;
  dest.characterInnerBody = source.characterInnerBody;
}

static void RebuildMeshCharacterAuthoringPreview() {
  if (!g_meshCharacterAuthoringInitialized) {
    return;
  }
  BuildPlayerSilhouetteMesh(g_meshCharacterAuthoringTemplate);
}

static PhysicsSceneEntity& EnsureMeshCharacterAuthoringTemplate(int sourceObjectIndex) {
  if (sourceObjectIndex < 0 || sourceObjectIndex >= static_cast<int>(g_objects.size())) {
    g_meshCharacterAuthoringInitialized = false;
    g_meshCharacterAuthoringSourceIndex = -1;
    g_meshCharacterAuthoringTemplate.visual.reset();
    return g_meshCharacterAuthoringTemplate;
  }

  if (!g_meshCharacterAuthoringInitialized ||
      g_meshCharacterAuthoringSourceIndex != sourceObjectIndex) {
    SceneObject& selected = g_objects[static_cast<std::size_t>(sourceObjectIndex)];
    g_meshCharacterAuthoringTemplate = PhysicsSceneEntity{};
    g_meshCharacterAuthoringTemplate.type = PhysicsSceneEntityType::Character;
    g_meshCharacterAuthoringTemplate.name = selected.name + " Character";
    g_meshCharacterAuthoringTemplate.sourceName = selected.name;
    g_meshCharacterAuthoringTemplate.sourceObjectIndex = sourceObjectIndex;
    g_meshCharacterAuthoringTemplate.position = SceneObjectWorldPosition(selected);
    g_meshCharacterAuthoringTemplate.eulerRadians = SceneObjectWorldEulerRadians(selected);
    ApplyDefaultPlayerSizeFromScene(g_meshCharacterAuthoringTemplate);
    g_meshCharacterAuthoringTemplate.playerShape = t850::PhysicsShapeType::Capsule;
    FitCharacterToSceneObject(g_meshCharacterAuthoringTemplate, selected);
    g_meshCharacterAuthoringTemplate.friction = 0.0f;
    g_meshCharacterAuthoringTemplate.restitution = 0.0f;
    g_meshCharacterAuthoringTemplate.sensor = false;
    g_meshCharacterAuthoringInitialized = true;
    g_meshCharacterAuthoringSourceIndex = sourceObjectIndex;
    RebuildMeshCharacterAuthoringPreview();
  }
  return g_meshCharacterAuthoringTemplate;
}

static bool RecreateCharacterPhysicsBody(t850::JoltPhysicsSystem& physics, PhysicsSceneEntity& entity);

static TransformState GetSceneObjectTransformState(SceneObject& object) {
  return TransformState{
      object.wireframe.Position(),
      object.wireframe.EulerRadians(),
      object.wireframe.Scale()
  };
}

static bool IsCharacterPhysicsEntity(const PhysicsSceneEntity& entity) {
  return entity.type == PhysicsSceneEntityType::Player ||
         entity.type == PhysicsSceneEntityType::Character;
}

static float AttachmentScaleRatio(float before, float after, const char* entityName) {
  if (std::fabs(before) < kMinEditableScale) {
    T8_LOG_ERROR("[T8ditor] Cannot scale attached character '%s': source mesh had a near-zero scale.",
                 entityName ? entityName : "");
    return 1.0f;
  }
  return std::fabs(after / before);
}

static bool ScaleAttachedCharacterShape(PhysicsSceneEntity& entity,
                                        const TransformState& beforeMesh,
                                        const TransformState& afterMesh) {
  const float sx = AttachmentScaleRatio(beforeMesh.scale.x, afterMesh.scale.x, entity.name.c_str());
  const float sy = AttachmentScaleRatio(beforeMesh.scale.y, afterMesh.scale.y, entity.name.c_str());
  const float sz = AttachmentScaleRatio(beforeMesh.scale.z, afterMesh.scale.z, entity.name.c_str());
  if (NearlyEqual(sx, 1.0f) && NearlyEqual(sy, 1.0f) && NearlyEqual(sz, 1.0f)) {
    return false;
  }

  if (entity.playerShape == t850::PhysicsShapeType::Capsule ||
      entity.playerShape == t850::PhysicsShapeType::Cylinder) {
    entity.playerRadius = (std::max)(0.001f, entity.playerRadius * (sx + sz) * 0.5f);
    entity.playerHalfHeight = (std::max)(0.001f, entity.playerHalfHeight * sy);
  } else if (entity.playerShape == t850::PhysicsShapeType::Sphere) {
    entity.playerRadius = (std::max)(0.001f, entity.playerRadius * (sx + sy + sz) / 3.0f);
  } else {
    entity.playerHalfExtents.x = (std::max)(0.001f, entity.playerHalfExtents.x * sx);
    entity.playerHalfExtents.y = (std::max)(0.001f, entity.playerHalfExtents.y * sy);
    entity.playerHalfExtents.z = (std::max)(0.001f, entity.playerHalfExtents.z * sz);
  }
  return true;
}

static void SyncCharacterEntityBodyAndVisual(t850::JoltPhysicsSystem& physics,
                                             PhysicsSceneEntity& entity,
                                             bool shapeChanged) {
  if (shapeChanged) {
    if (!RecreateCharacterPhysicsBody(physics, entity)) {
      BuildPlayerSilhouetteMesh(entity);
    }
    return;
  }

  if (entity.visual) {
    entity.visual->Position() = entity.position;
    entity.visual->EulerRadians() = entity.eulerRadians;
  } else {
    BuildPlayerSilhouetteMesh(entity);
  }

  const XMATRIX44 world = MakePhysicsTransform(entity.position, entity.eulerRadians);
  if (entity.body.IsValid()) {
    physics.SetBodyTransform(entity.body, world, true);
  } else if (physics.IsInitialized()) {
    RecreateCharacterPhysicsBody(physics, entity);
  }
}

static void PropagateSceneObjectTransformToAttachedCharacters(t850::JoltPhysicsSystem& physics,
                                                              int sourceObjectIndex,
                                                              const TransformState& before,
                                                              const TransformState& after) {
  if (sourceObjectIndex < 0 || sourceObjectIndex >= static_cast<int>(g_objects.size())) {
    return;
  }
  if (NearlyEqualTransform(before, after)) {
    return;
  }

  XMATRIX44 oldMeshInverse;
  if (!BuildInverseSceneObjectWorldFromTransform(before, oldMeshInverse)) {
    return;
  }
  const XMATRIX44 newMeshWorld = BuildSceneObjectWorldFromTransform(after);
  const XMATRIX44 meshDelta = oldMeshInverse * newMeshWorld;
  SceneObject& sourceObject = g_objects[static_cast<std::size_t>(sourceObjectIndex)];

  for (PhysicsSceneEntity& entity : g_physicsEntities) {
    if (entity.type != PhysicsSceneEntityType::Character) {
      continue;
    }
    if (entity.sourceObjectIndex != sourceObjectIndex && entity.sourceName != sourceObject.name) {
      continue;
    }

    const XMATRIX44 oldCharacterWorld = MakePhysicsTransform(entity.position, entity.eulerRadians);
    const XMATRIX44 newCharacterWorld = oldCharacterWorld * meshDelta;
    float translation[3] = {};
    float rotationDeg[3] = {};
    float scale[3] = {};
    ImGuizmo::DecomposeMatrixToComponents(&newCharacterWorld.m[0][0], translation, rotationDeg, scale);
    entity.position = XVECTOR3(translation[0], translation[1], translation[2], 1.0f);
    entity.eulerRadians = XVECTOR3(rotationDeg[0] * kDegToRad,
                                   rotationDeg[1] * kDegToRad,
                                   rotationDeg[2] * kDegToRad,
                                   0.0f);
    entity.sourceObjectIndex = sourceObjectIndex;
    entity.sourceName = sourceObject.name;

    const bool shapeChanged = ScaleAttachedCharacterShape(entity, before, after);
    SyncCharacterEntityBodyAndVisual(physics, entity, shapeChanged);
  }
}

static XMATRIX44 MakePhysicsGizmoTransform(const PhysicsSceneEntity& entity) {
  XMATRIX44 scale;
  scale.Identity();
  if (g_gizmoDragging && IsCharacterPhysicsEntity(entity)) {
    if (entity.playerShape == t850::PhysicsShapeType::Capsule || entity.playerShape == t850::PhysicsShapeType::Cylinder) {
      const float radiusScale = g_physicsGizmoStartRadius > 0.000001f
          ? entity.playerRadius / g_physicsGizmoStartRadius
          : 1.0f;
      const float heightScale = g_physicsGizmoStartHalfHeight > 0.000001f
          ? entity.playerHalfHeight / g_physicsGizmoStartHalfHeight
          : 1.0f;
      XMatScaling(scale, radiusScale, heightScale, radiusScale);
    } else if (entity.playerShape == t850::PhysicsShapeType::Sphere) {
      const float radiusScale = g_physicsGizmoStartRadius > 0.000001f
          ? entity.playerRadius / g_physicsGizmoStartRadius
          : 1.0f;
      XMatScaling(scale, radiusScale, radiusScale, radiusScale);
    } else {
      XMatScaling(scale,
                  g_physicsGizmoStartHalfExtents.x > 0.000001f ? entity.playerHalfExtents.x / g_physicsGizmoStartHalfExtents.x : 1.0f,
                  g_physicsGizmoStartHalfExtents.y > 0.000001f ? entity.playerHalfExtents.y / g_physicsGizmoStartHalfExtents.y : 1.0f,
                  g_physicsGizmoStartHalfExtents.z > 0.000001f ? entity.playerHalfExtents.z / g_physicsGizmoStartHalfExtents.z : 1.0f);
    }
  }
  return scale * MakePhysicsTransform(entity.position, entity.eulerRadians);
}

static bool RecreateCharacterPhysicsBody(t850::JoltPhysicsSystem& physics, PhysicsSceneEntity& entity) {
  if (!IsCharacterPhysicsEntity(entity) || !physics.IsInitialized()) {
    return false;
  }
  if (entity.body.IsValid()) {
    physics.DestroyBody(entity.body);
    entity.body.Reset();
  }

  t850::PhysicsBodyDesc desc;
  desc.entityId = entity.type == PhysicsSceneEntityType::Player ? 0x504C5952u : 0x43484152u; // PLYR / CHAR
  desc.debugName = entity.name.empty() ? (entity.type == PhysicsSceneEntityType::Player ? "player" : "character") : entity.name;
  desc.worldTransform = MakePhysicsTransform(entity.position, entity.eulerRadians);
  desc.motion = t850::PhysicsBodyMotion::Kinematic;
  desc.friction = entity.friction;
  desc.restitution = entity.restitution;
  desc.sensor = entity.sensor;
  if (entity.playerShape == t850::PhysicsShapeType::Capsule) {
    desc.shape = t850::PhysicsShapeDesc::Capsule((std::max)(0.001f, entity.playerRadius),
                                                 (std::max)(0.001f, entity.playerHalfHeight));
  } else if (entity.playerShape == t850::PhysicsShapeType::Sphere) {
    desc.shape = t850::PhysicsShapeDesc::Sphere((std::max)(0.001f, entity.playerRadius));
  } else if (entity.playerShape == t850::PhysicsShapeType::Cylinder) {
    desc.shape = t850::PhysicsShapeDesc::Cylinder((std::max)(0.001f, entity.playerRadius),
                                                  (std::max)(0.001f, entity.playerHalfHeight));
  } else {
    desc.shape = t850::PhysicsShapeDesc::Box(XVECTOR3(
        (std::max)(0.001f, entity.playerHalfExtents.x),
        (std::max)(0.001f, entity.playerHalfExtents.y),
        (std::max)(0.001f, entity.playerHalfExtents.z),
        0.0f));
  }

  entity.body = physics.CreateBody(desc);
  if (!entity.body.IsValid()) {
    T8_LOG_ERROR("[T8ditor] Failed to create character physics body '%s'.", desc.debugName.c_str());
    return false;
  }
  return BuildPlayerSilhouetteMesh(entity);
}

static bool DrawCharacterRuntimePathControl(PhysicsSceneEntity& entity) {
  int path = std::clamp(entity.characterRuntimePath, 0, 1);
  const char* pathOptions[] = {
      "Kinematic / NavMesh path",
      "Jolt collision path"
  };
  if (ImGui::Combo("Movement Path", &path, pathOptions, 2)) {
    entity.characterRuntimePath = path;
    return true;
  }
  return false;
}

static bool DrawCharacterShapeControls(PhysicsSceneEntity& entity, bool includePosition) {
  bool changed = false;
  if (includePosition) {
    float p[3] = { entity.position.x, entity.position.y, entity.position.z };
    if (ImGui::DragFloat3("Position", p, 0.5f)) {
      entity.position = XVECTOR3(p[0], p[1], p[2], 1.0f);
      changed = true;
    }
  }

  int shape = entity.playerShape == t850::PhysicsShapeType::Capsule ? 1 :
      (entity.playerShape == t850::PhysicsShapeType::Sphere ? 2 :
       (entity.playerShape == t850::PhysicsShapeType::Cylinder ? 3 : 0));
  const char* shapeOptions[] = { "AABB / Box", "Capsule", "Sphere", "Cylinder" };
  if (ImGui::Combo("Shape", &shape, shapeOptions, 4)) {
    entity.playerShape = shape == 1 ? t850::PhysicsShapeType::Capsule :
        (shape == 2 ? t850::PhysicsShapeType::Sphere :
         (shape == 3 ? t850::PhysicsShapeType::Cylinder : t850::PhysicsShapeType::Box));
    changed = true;
  }

  if (entity.playerShape == t850::PhysicsShapeType::Capsule) {
    changed |= ImGui::DragFloat("Radius", &entity.playerRadius, 0.25f, 0.1f, 128.0f, "%.2f");
    changed |= ImGui::DragFloat("Half Height", &entity.playerHalfHeight, 0.25f, 0.1f, 256.0f, "%.2f");
    ImGui::TextDisabled("Total height = 2 * (half height + radius).");
  } else if (entity.playerShape == t850::PhysicsShapeType::Sphere) {
    changed |= ImGui::DragFloat("Radius", &entity.playerRadius, 0.25f, 0.1f, 256.0f, "%.2f");
  } else if (entity.playerShape == t850::PhysicsShapeType::Cylinder) {
    changed |= ImGui::DragFloat("Radius", &entity.playerRadius, 0.25f, 0.1f, 256.0f, "%.2f");
    changed |= ImGui::DragFloat("Half Height", &entity.playerHalfHeight, 0.25f, 0.1f, 256.0f, "%.2f");
  } else {
    float he[3] = { entity.playerHalfExtents.x, entity.playerHalfExtents.y, entity.playerHalfExtents.z };
    if (ImGui::DragFloat3("Half Extents", he, 0.25f, 0.1f, 256.0f, "%.2f")) {
      entity.playerHalfExtents = XVECTOR3(he[0], he[1], he[2], 0.0f);
      changed = true;
    }
  }

  changed |= ImGui::DragFloat("Friction", &entity.friction, 0.01f, 0.0f, 10.0f, "%.2f");
  changed |= ImGui::DragFloat("Restitution", &entity.restitution, 0.01f, 0.0f, 1.0f, "%.2f");
  changed |= ImGui::Checkbox("Sensor", &entity.sensor);
  return changed;
}

static bool DrawJoltCharacterSettingsControls(PhysicsSceneEntity& entity) {
  bool changed = false;
  const char* implementationOptions[] = { "Character rigid body", "CharacterVirtual controller" };
  changed |= ImGui::Combo("Implementation", &entity.characterImplementation, implementationOptions, 2);
  changed |= ImGui::DragFloat("Mass", &entity.characterMass, 0.5f, 0.0f, 1000.0f, "%.2f");
  changed |= ImGui::DragFloat("Max Slope Angle (deg)", &entity.characterMaxSlopeAngleDeg, 0.5f, 0.0f, 89.0f, "%.2f");
  changed |= ImGui::Checkbox("Enhanced Internal Edge Removal", &entity.characterEnhancedInternalEdgeRemoval);
  changed |= ImGui::DragFloat("Supporting Volume Offset", &entity.characterSupportingVolumeOffset, 0.01f, -1.0e10f, 1.0e10f, "%.4f");
  if (entity.characterImplementation == 0) {
    changed |= ImGui::DragFloat("Character Friction", &entity.friction, 0.01f, 0.0f, 10.0f, "%.2f");
    changed |= ImGui::DragFloat("Gravity Factor", &entity.characterGravityFactor, 0.01f, 0.0f, 10.0f, "%.2f");
    changed |= ImGui::Checkbox("Allow Translation X", &entity.characterAllowTranslationX);
    changed |= ImGui::Checkbox("Allow Translation Y", &entity.characterAllowTranslationY);
    changed |= ImGui::Checkbox("Allow Translation Z", &entity.characterAllowTranslationZ);
  } else {
    changed |= ImGui::DragFloat("Max Strength", &entity.characterMaxStrength, 1.0f, 0.0f, 100000.0f, "%.1f");
    changed |= ImGui::DragFloat3("Shape Offset", entity.characterShapeOffset, 0.01f, -256.0f, 256.0f, "%.3f");
    const char* backFaceOptions[] = { "Ignore Back Faces", "Collide With Back Faces" };
    changed |= ImGui::Combo("Back Face Mode", &entity.characterBackFaceMode, backFaceOptions, 2);
    changed |= ImGui::DragFloat("Predictive Contact Distance", &entity.characterPredictiveContactDistance, 0.005f, 0.0f, 10.0f, "%.4f");
    changed |= ImGui::DragInt("Max Collision Iterations", &entity.characterMaxCollisionIterations, 1.0f, 1, 64);
    changed |= ImGui::DragInt("Max Constraint Iterations", &entity.characterMaxConstraintIterations, 1.0f, 1, 128);
    changed |= ImGui::DragFloat("Min Time Remaining", &entity.characterMinTimeRemaining, 0.00001f, 0.0f, 0.1f, "%.6f");
    changed |= ImGui::DragFloat("Collision Tolerance", &entity.characterCollisionTolerance, 0.0001f, 0.0f, 1.0f, "%.5f");
    changed |= ImGui::DragFloat("Character Padding", &entity.characterPadding, 0.001f, 0.0f, 1.0f, "%.4f");
    changed |= ImGui::DragInt("Max Num Hits", &entity.characterMaxNumHits, 1.0f, 1, 4096);
    changed |= ImGui::DragFloat("Hit Reduction Cos Max Angle", &entity.characterHitReductionCosMaxAngle, 0.001f, -1.0f, 1.0f, "%.6f");
    changed |= ImGui::DragFloat("Penetration Recovery Speed", &entity.characterPenetrationRecoverySpeed, 0.01f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::Checkbox("Inner Body", &entity.characterInnerBody);
  }
  return changed;
}

static bool BuildPhysicsDebugBodyBounds(const t850::PhysicsDebugBody& debugBody, t850::AABB& outBounds) {
  outBounds = t850::AABB{};
  if (debugBody.shape.type == t850::PhysicsShapeType::TriangleMesh &&
      debugBody.debugVertices && !debugBody.debugVertices->empty()) {
    for (const XVECTOR3& vertex : *debugBody.debugVertices) {
      outBounds.ExpandToInclude(t850::TransformPoint(vertex, debugBody.state.worldTransform));
    }
    return outBounds.IsValid();
  }

  if (debugBody.shape.type == t850::PhysicsShapeType::Capsule) {
    const float radius = (std::max)(0.001f, debugBody.shape.radius);
    const float halfHeight = (std::max)(0.001f, debugBody.shape.halfHeight);
    t850::AABB local(
        XVECTOR3(-radius, -halfHeight - radius, -radius, 1.0f),
        XVECTOR3( radius,  halfHeight + radius,  radius, 1.0f));
    outBounds = local.Transformed(debugBody.state.worldTransform);
    return outBounds.IsValid();
  }

  if (debugBody.shape.type == t850::PhysicsShapeType::Sphere) {
    const float radius = (std::max)(0.001f, debugBody.shape.radius);
    t850::AABB local(
        XVECTOR3(-radius, -radius, -radius, 1.0f),
        XVECTOR3( radius,  radius,  radius, 1.0f));
    outBounds = local.Transformed(debugBody.state.worldTransform);
    return outBounds.IsValid();
  }

  if (debugBody.shape.type == t850::PhysicsShapeType::Cylinder) {
    const float radius = (std::max)(0.001f, debugBody.shape.radius);
    const float halfHeight = (std::max)(0.001f, debugBody.shape.halfHeight);
    t850::AABB local(
        XVECTOR3(-radius, -halfHeight, -radius, 1.0f),
        XVECTOR3( radius,  halfHeight,  radius, 1.0f));
    outBounds = local.Transformed(debugBody.state.worldTransform);
    return outBounds.IsValid();
  }

  const XVECTOR3 halfExtents(
      (std::max)(0.001f, debugBody.shape.halfExtents.x),
      (std::max)(0.001f, debugBody.shape.halfExtents.y),
      (std::max)(0.001f, debugBody.shape.halfExtents.z),
      0.0f);
  t850::AABB local(
      XVECTOR3(-halfExtents.x, -halfExtents.y, -halfExtents.z, 1.0f),
      XVECTOR3( halfExtents.x,  halfExtents.y,  halfExtents.z, 1.0f));
  outBounds = local.Transformed(debugBody.state.worldTransform);
  return outBounds.IsValid();
}

static bool GetPhysicsEntityPrimitiveWorldAABB(const PhysicsSceneEntity& entity,
                                               t850::AABB& outBounds) {
  outBounds = t850::AABB{};
  if (!IsCharacterPhysicsEntity(entity)) {
    return false;
  }
  if (entity.playerShape == t850::PhysicsShapeType::Capsule) {
    const float radius = (std::max)(0.001f, entity.playerRadius);
    const float halfHeight = (std::max)(0.001f, entity.playerHalfHeight);
    t850::AABB local(
        XVECTOR3(-radius, -halfHeight - radius, -radius, 1.0f),
        XVECTOR3( radius,  halfHeight + radius,  radius, 1.0f));
    outBounds = local.Transformed(MakePhysicsTransform(entity.position, entity.eulerRadians));
    return outBounds.IsValid();
  }
  if (entity.playerShape == t850::PhysicsShapeType::Sphere) {
    const float radius = (std::max)(0.001f, entity.playerRadius);
    t850::AABB local(
        XVECTOR3(-radius, -radius, -radius, 1.0f),
        XVECTOR3( radius,  radius,  radius, 1.0f));
    outBounds = local.Transformed(MakePhysicsTransform(entity.position, entity.eulerRadians));
    return outBounds.IsValid();
  }
  if (entity.playerShape == t850::PhysicsShapeType::Cylinder) {
    const float radius = (std::max)(0.001f, entity.playerRadius);
    const float halfHeight = (std::max)(0.001f, entity.playerHalfHeight);
    t850::AABB local(
        XVECTOR3(-radius, -halfHeight, -radius, 1.0f),
        XVECTOR3( radius,  halfHeight,  radius, 1.0f));
    outBounds = local.Transformed(MakePhysicsTransform(entity.position, entity.eulerRadians));
    return outBounds.IsValid();
  }

  const XVECTOR3 halfExtents(
      (std::max)(0.001f, entity.playerHalfExtents.x),
      (std::max)(0.001f, entity.playerHalfExtents.y),
      (std::max)(0.001f, entity.playerHalfExtents.z),
      0.0f);
  t850::AABB local(
      XVECTOR3(-halfExtents.x, -halfExtents.y, -halfExtents.z, 1.0f),
      XVECTOR3( halfExtents.x,  halfExtents.y,  halfExtents.z, 1.0f));
  outBounds = local.Transformed(MakePhysicsTransform(entity.position, entity.eulerRadians));
  return outBounds.IsValid();
}

static bool GetPhysicsEntityWorldAABB(const PhysicsSceneEntity& entity,
                                      const t850::JoltPhysicsSystem& physics,
                                      t850::AABB& outBounds) {
  outBounds = t850::AABB{};
  if (GetPhysicsEntityPrimitiveWorldAABB(entity, outBounds)) {
    return true;
  }
  if (IsCharacterPhysicsEntity(entity) && entity.visual && entity.visual->IsLoaded()) {
    outBounds = entity.visual->WorldAABB();
    return outBounds.IsValid();
  }
  if (!entity.body.IsValid()) {
    return false;
  }
  t850::PhysicsDebugBody debugBody;
  return physics.GetDebugBody(entity.body, debugBody) && BuildPhysicsDebugBodyBounds(debugBody, outBounds);
}

static bool RaycastPhysicsEntity(const PhysicsSceneEntity& entity,
                                 const t850::JoltPhysicsSystem& physics,
                                 const t850::Ray& ray,
                                 const XMATRIX44& viewProjection,
                                 int viewW,
                                 int viewH,
                                 float mouseX,
                                 float mouseY,
                                 float& outT) {
  outT = FLT_MAX;
  if (!entity.visible || entity.frozen) {
    return false;
  }

  t850::AABB bounds;
  if (!GetPhysicsEntityWorldAABB(entity, physics, bounds)) {
    return false;
  }
  float sMinX = 0.0f, sMinY = 0.0f, sMaxX = 0.0f, sMaxY = 0.0f;
  const bool projected = ProjectAABBToScreenRect(bounds, viewProjection, viewW, viewH, sMinX, sMinY, sMaxX, sMaxY);
  const float screenArea = (std::max)(1.0f, (sMaxX - sMinX) * (sMaxY - sMinY));
  const float viewportArea = (std::max)(1.0f, static_cast<float>(viewW * viewH));
  if (!projected || screenArea >= viewportArea * 0.65f ||
      mouseX < sMinX || mouseX > sMaxX || mouseY < sMinY || mouseY > sMaxY) {
    return false;
  }
  if (t850::RayIntersectsAABB(ray, bounds, outT)) {
    return true;
  }
  if (IsCharacterPhysicsEntity(entity) && entity.visual && entity.visual->IsLoaded()) {
    return entity.visual->RaycastSurface(ray, outT);
  }
  return false;
}

static int CreateOrSelectPlayerPhysicsEntity(t850::JoltPhysicsSystem& physics, const XVECTOR3& spawnPosition) {
  int existing = FindPlayerPhysicsEntityIndex();
  if (existing >= 0) {
    g_selectedIdx = existing;
    g_selectionType = 3;
    ClearMixedSelection();
    AddMixedSelection(3, existing);
    return existing;
  }

  PhysicsSceneEntity entity;
  entity.type = PhysicsSceneEntityType::Player;
  entity.name = "player";
  entity.sourceName = "player";
  entity.sourceObjectIndex = -1;
  entity.position = spawnPosition;
  ApplyDefaultPlayerSizeFromScene(entity);
  entity.friction = 0.0f;
  entity.restitution = 0.0f;
  entity.sensor = false;
  if (!RecreateCharacterPhysicsBody(physics, entity)) {
    return -1;
  }

  g_physicsEntities.push_back(std::move(entity));
  g_selectedIdx = static_cast<int>(g_physicsEntities.size()) - 1;
  g_selectionType = 3;
  ClearMixedSelection();
  AddMixedSelection(3, g_selectedIdx);
  return g_selectedIdx;
}

static bool CreateCharacterPhysicsEntity(t850::JoltPhysicsSystem& physics,
                                         int sourceObjectIndex,
                                         const PhysicsSceneEntity& authoringTemplate) {
  if (sourceObjectIndex < 0 || sourceObjectIndex >= static_cast<int>(g_objects.size())) {
    T8_LOG_ERROR("[T8ditor] Select a loaded mesh before creating a character.");
    return false;
  }
  if (!physics.IsInitialized()) {
    T8_LOG_ERROR("[T8ditor] Physics runtime is not initialized.");
    return false;
  }

  SceneObject& selected = g_objects[sourceObjectIndex];
  PhysicsSceneEntity entity;
  entity.type = PhysicsSceneEntityType::Character;
  entity.name = MakeUniquePhysicsEntityName(selected.name + " Character");
  entity.sourceName = selected.name;
  entity.sourceObjectIndex = sourceObjectIndex;
  entity.position = SceneObjectWorldPosition(selected);
  entity.eulerRadians = SceneObjectWorldEulerRadians(selected);
  ApplyDefaultPlayerSizeFromScene(entity);
  CopyCharacterAuthoringSettings(authoringTemplate, entity);
  if (entity.characterRuntimePath == static_cast<int>(CharacterRuntimePath::Jolt)) {
    FitCharacterToSceneObject(entity, selected);
  }
  if (!RecreateCharacterPhysicsBody(physics, entity)) {
    return false;
  }

  const std::string createdName = entity.name;
  const char* implementationName = entity.characterImplementation == 0 ? "Character rigid-body" : "CharacterVirtual";
  const char* runtimePathName = CharacterRuntimePathToSceneString(entity.characterRuntimePath);
  g_physicsEntities.push_back(std::move(entity));
  g_selectedIdx = static_cast<int>(g_physicsEntities.size()) - 1;
  g_selectionType = 3;
  ClearMixedSelection();
  AddMixedSelection(3, g_selectedIdx);
  T8_LOG_INFO("[T8ditor] Created %s %s physics entity '%s' for mesh '%s'",
              runtimePathName,
              implementationName,
              createdName.c_str(),
              selected.name.c_str());
  return true;
}

static void DestroyPhysicsEntity(t850::JoltPhysicsSystem& physics, int index) {
  if (index < 0 || index >= static_cast<int>(g_physicsEntities.size())) {
    return;
  }
  PhysicsSceneEntity& entity = g_physicsEntities[index];
  if (entity.body.IsValid() && physics.IsInitialized()) {
    physics.DestroyBody(entity.body);
  }
  g_physicsEntities.erase(g_physicsEntities.begin() + index);
  ClearMixedSelection();
  if (g_selectionType == 3) {
    if (g_physicsEntities.empty()) {
      g_selectedIdx = -1;
      g_selectionType = 0;
    } else if (g_selectedIdx >= static_cast<int>(g_physicsEntities.size())) {
      g_selectedIdx = static_cast<int>(g_physicsEntities.size()) - 1;
    }
  }
}

static void DestroyAllPhysicsEntities(t850::JoltPhysicsSystem& physics) {
  if (physics.IsInitialized()) {
    for (PhysicsSceneEntity& entity : g_physicsEntities) {
      if (entity.body.IsValid()) {
        physics.DestroyBody(entity.body);
      }
    }
  }
  g_physicsEntities.clear();
  ClearMixedSelection();
  if (g_selectionType == 3) {
    g_selectedIdx = -1;
    g_selectionType = 0;
  }
}

static int CountPhysicsEntitiesForSourceObject(int sourceObjectIndex) {
  int count = 0;
  for (const PhysicsSceneEntity& entity : g_physicsEntities) {
    if (entity.sourceObjectIndex == sourceObjectIndex) {
      ++count;
    }
  }
  return count;
}

static int FindJoltCharacterPhysicsEntityForSourceObject(int sourceObjectIndex) {
  if (sourceObjectIndex < 0 || sourceObjectIndex >= static_cast<int>(g_objects.size())) {
    return -1;
  }
  const std::string& sourceName = g_objects[static_cast<std::size_t>(sourceObjectIndex)].name;
  for (int i = 0; i < static_cast<int>(g_physicsEntities.size()); ++i) {
    const PhysicsSceneEntity& entity = g_physicsEntities[static_cast<std::size_t>(i)];
    if (entity.type == PhysicsSceneEntityType::Character &&
        entity.characterRuntimePath == static_cast<int>(CharacterRuntimePath::Jolt) &&
        (entity.sourceObjectIndex == sourceObjectIndex || entity.sourceName == sourceName)) {
      return i;
    }
  }
  return -1;
}

static void SyncSceneObjectTransform(SceneObject& object) {
  if (object.primId < 0) {
    return;
  }
  const XVECTOR3& pos = object.wireframe.Position();
  const XVECTOR3& eul = object.wireframe.EulerRadians();
  const XVECTOR3& scl = object.wireframe.Scale();
  object.litInst.TranslateAbsolute(pos.x, pos.y, pos.z);
  object.litInst.RotateXAbsolute(eul.x * kRadToDeg);
  object.litInst.RotateYAbsolute(eul.y * kRadToDeg);
  object.litInst.RotateZAbsolute(eul.z * kRadToDeg);
  object.litInst.ScaleAbsolute(scl.x, scl.y, scl.z);
  object.litInst.Visible = object.visible;
  object.litInst.Update();
}

static void DrawMeshCharacterOrientationMatchControls(t850::JoltPhysicsSystem& physics, int sourceObjectIndex) {
  const int characterIndex = FindJoltCharacterPhysicsEntityForSourceObject(sourceObjectIndex);
  const bool hasJoltCharacter = characterIndex >= 0;
  ImGui::BeginDisabled(!hasJoltCharacter);
  if (ImGui::Button("Match orientation to mesh")) {
    SceneObject& object = g_objects[static_cast<std::size_t>(sourceObjectIndex)];
    PhysicsSceneEntity& entity = g_physicsEntities[static_cast<std::size_t>(characterIndex)];
    entity.eulerRadians = object.wireframe.EulerRadians();
    RecreateCharacterPhysicsBody(physics, entity);
  }
  ImGui::SameLine();
  if (ImGui::Button("Match orientation to Jolt")) {
    SceneObject& object = g_objects[static_cast<std::size_t>(sourceObjectIndex)];
    const PhysicsSceneEntity& entity = g_physicsEntities[static_cast<std::size_t>(characterIndex)];
    object.wireframe.EulerRadians() = entity.eulerRadians;
    SyncSceneObjectTransform(object);
  }
  ImGui::EndDisabled();
  if (!hasJoltCharacter) {
    ImGui::TextDisabled("Requires an authored Jolt character for this mesh.");
  }
}

static t850::scene::ScenePhysicsEntityDesc PhysicsEntityToScene(const PhysicsSceneEntity& entity) {
  t850::scene::ScenePhysicsEntityDesc desc;
  desc.name = entity.name;
  desc.type = entity.type == PhysicsSceneEntityType::Player ? "player" :
      (entity.type == PhysicsSceneEntityType::Character ? "character" : "static_triangle_mesh");
  desc.source_object = entity.sourceName;
  desc.position = { entity.position.x, entity.position.y, entity.position.z };
  desc.rotation = { entity.eulerRadians.x * kRadToDeg, entity.eulerRadians.y * kRadToDeg, entity.eulerRadians.z * kRadToDeg };
  desc.visible = entity.visible;
  desc.frozen = entity.frozen;
  desc.show_wire = entity.showWire;
  desc.show_orientation = entity.showOrientation;
  desc.shape = entity.playerShape == t850::PhysicsShapeType::Capsule ? "capsule" :
      (entity.playerShape == t850::PhysicsShapeType::Sphere ? "sphere" :
       (entity.playerShape == t850::PhysicsShapeType::Cylinder ? "cylinder" : "box"));
  desc.half_extents = { entity.playerHalfExtents.x, entity.playerHalfExtents.y, entity.playerHalfExtents.z };
  desc.radius = entity.playerRadius;
  desc.half_height = entity.playerHalfHeight;
  desc.friction = entity.friction;
  desc.restitution = entity.restitution;
  desc.sensor = entity.sensor;
  desc.cook_settings = PhysicsCookSettingsToScene(entity.cookSettings);
  desc.character.runtime_path = CharacterRuntimePathToSceneString(entity.characterRuntimePath);
  desc.character.implementation = entity.characterImplementation == 0 ? "character" : "virtual";
  desc.character.bot_radius = entity.playerBotRadius;
  desc.character.mass = entity.characterMass;
  desc.character.max_strength = entity.characterMaxStrength;
  desc.character.max_slope_angle_deg = entity.characterMaxSlopeAngleDeg;
  desc.character.enhanced_internal_edge_removal = entity.characterEnhancedInternalEdgeRemoval;
  desc.character.supporting_volume_offset = entity.characterSupportingVolumeOffset;
  desc.character.shape_offset = { entity.characterShapeOffset[0], entity.characterShapeOffset[1], entity.characterShapeOffset[2] };
  desc.character.back_face_mode = entity.characterBackFaceMode == 0 ? "ignore" : "collide";
  desc.character.predictive_contact_distance = entity.characterPredictiveContactDistance;
  desc.character.max_collision_iterations = entity.characterMaxCollisionIterations;
  desc.character.max_constraint_iterations = entity.characterMaxConstraintIterations;
  desc.character.min_time_remaining = entity.characterMinTimeRemaining;
  desc.character.collision_tolerance = entity.characterCollisionTolerance;
  desc.character.character_padding = entity.characterPadding;
  desc.character.max_num_hits = entity.characterMaxNumHits;
  desc.character.hit_reduction_cos_max_angle = entity.characterHitReductionCosMaxAngle;
  desc.character.penetration_recovery_speed = entity.characterPenetrationRecoverySpeed;
  desc.character.gravity_factor = entity.characterGravityFactor;
  desc.character.allow_translation_x = entity.characterAllowTranslationX;
  desc.character.allow_translation_y = entity.characterAllowTranslationY;
  desc.character.allow_translation_z = entity.characterAllowTranslationZ;
  desc.character.inner_body = entity.characterInnerBody;
  return desc;
}

static int FindSceneObjectIndexByName(const std::string& name) {
  for (int i = 0; i < static_cast<int>(g_objects.size()); ++i) {
    if (g_objects[i].name == name) {
      return i;
    }
  }
  return -1;
}

static int FindPhysicsEntityIndexByName(const std::string& name) {
  for (int i = 0; i < static_cast<int>(g_physicsEntities.size()); ++i) {
    if (g_physicsEntities[static_cast<std::size_t>(i)].name == name) {
      return i;
    }
  }
  return -1;
}

static bool GameEntityReferencesMesh(const t850::scene::SceneGameEntityDesc& entity,
                                     const std::string& meshName) {
  return !meshName.empty() && entity.mesh_object == meshName;
}

static bool GameEntityReferencesPhysics(const t850::scene::SceneGameEntityDesc& entity,
                                        const std::string& physicsName) {
  if (physicsName.empty()) {
    return false;
  }
  if (entity.primary_physics_entity == physicsName) {
    return true;
  }
  return std::find(entity.physics_entities.begin(), entity.physics_entities.end(), physicsName) != entity.physics_entities.end();
}

static bool HasGameEntityForMeshOrPhysics(const std::string& meshName,
                                          const std::string& physicsName) {
  for (const t850::scene::SceneGameEntityDesc& entity : g_gameEntities) {
    if (GameEntityReferencesMesh(entity, meshName) ||
        GameEntityReferencesPhysics(entity, physicsName)) {
      return true;
    }
  }
  return false;
}

static int FindGameEntityIndexForSelection(const SelectionRef& ref) {
  for (int i = 0; i < static_cast<int>(g_gameEntities.size()); ++i) {
    const t850::scene::SceneGameEntityDesc& entity = g_gameEntities[static_cast<std::size_t>(i)];
    if (ref.type == 0 &&
        ref.index >= 0 &&
        ref.index < static_cast<int>(g_objects.size()) &&
        GameEntityReferencesMesh(entity, g_objects[static_cast<std::size_t>(ref.index)].name)) {
      return i;
    }
    if (ref.type == 3 &&
        ref.index >= 0 &&
        ref.index < static_cast<int>(g_physicsEntities.size()) &&
        GameEntityReferencesPhysics(entity, g_physicsEntities[static_cast<std::size_t>(ref.index)].name)) {
      return i;
    }
  }
  return -1;
}

static std::string UniqueGameEntityName(const std::string& baseName) {
  const std::string base = baseName.empty() ? "Game Entity" : baseName;
  std::string candidate = base;
  int suffix = 1;
  auto exists = [&](const std::string& name) {
    for (const t850::scene::SceneGameEntityDesc& entity : g_gameEntities) {
      if (entity.name == name) {
        return true;
      }
    }
    return false;
  };
  while (exists(candidate)) {
    candidate = base + " " + std::to_string(++suffix);
  }
  return candidate;
}

static std::string DisplayNameForMeshObject(const std::string& meshName) {
  if (meshName.empty()) {
    return "Game Entity";
  }
  std::filesystem::path path(meshName);
  std::string stem = path.stem().string();
  return stem.empty() ? meshName : stem;
}

static void AddPhysicsNameToGameEntity(t850::scene::SceneGameEntityDesc& entity,
                                       const std::string& physicsName) {
  if (physicsName.empty()) {
    return;
  }
  if (entity.primary_physics_entity.empty()) {
    entity.primary_physics_entity = physicsName;
  }
  if (std::find(entity.physics_entities.begin(), entity.physics_entities.end(), physicsName) == entity.physics_entities.end()) {
    entity.physics_entities.push_back(physicsName);
  }
}

static bool ObjectHasRagdollRelationship(const SceneObject& object) {
  return object.ragdollAuthoringReady ||
         object.ragdollAuthoringMeta.has_value() ||
         !object.ragdollResourcePath.empty();
}

static void EnsureInferredGameEntities() {
  if (!HasGameEntityForMeshOrPhysics({}, "player")) {
    for (const PhysicsSceneEntity& physics : g_physicsEntities) {
      if (physics.type != PhysicsSceneEntityType::Player) {
        continue;
      }
      t850::scene::SceneGameEntityDesc entity;
      entity.name = UniqueGameEntityName(physics.name.empty() ? "Player" : physics.name);
      entity.kind = "player";
      entity.primary_physics_entity = physics.name;
      entity.physics_entities.push_back(physics.name);
      entity.camera = "runtime_player_camera";
      entity.ai = "player";
      g_gameEntities.push_back(std::move(entity));
      break;
    }
  }

  for (int objectIndex = 0; objectIndex < static_cast<int>(g_objects.size()); ++objectIndex) {
    const SceneObject& object = g_objects[static_cast<std::size_t>(objectIndex)];
    if (!object.litInst.GetSkinnedMesh() && !ObjectHasRagdollRelationship(object)) {
      continue;
    }

    std::vector<std::string> physicsNames;
    bool hasCharacterPhysics = false;
    for (const PhysicsSceneEntity& physics : g_physicsEntities) {
      if (physics.sourceObjectIndex == objectIndex || physics.sourceName == object.name) {
        if (physics.type == PhysicsSceneEntityType::Character) {
          hasCharacterPhysics = true;
          physicsNames.push_back(physics.name);
        }
      }
    }
    if (!hasCharacterPhysics && !ObjectHasRagdollRelationship(object)) {
      continue;
    }
    const std::string primaryPhysics = physicsNames.empty() ? std::string{} : physicsNames.front();
    if (HasGameEntityForMeshOrPhysics(object.name, primaryPhysics)) {
      continue;
    }

    t850::scene::SceneGameEntityDesc entity;
    entity.name = UniqueGameEntityName(DisplayNameForMeshObject(object.name));
    entity.kind = hasCharacterPhysics ? "character" : "mesh";
    entity.mesh_object = object.name;
    for (const std::string& physicsName : physicsNames) {
      AddPhysicsNameToGameEntity(entity, physicsName);
    }
    if (ObjectHasRagdollRelationship(object)) {
      entity.ragdoll_object = object.name;
    }
    if (hasCharacterPhysics) {
      entity.ai = "nav_agent";
    }
    g_gameEntities.push_back(std::move(entity));
  }
}

static void DestroyPhysicsEntitiesForSourceObject(t850::JoltPhysicsSystem& physics, int sourceObjectIndex) {
  for (int i = static_cast<int>(g_physicsEntities.size()) - 1; i >= 0; --i) {
    if (g_physicsEntities[i].sourceObjectIndex == sourceObjectIndex) {
      DestroyPhysicsEntity(physics, i);
    }
  }
}

static bool CreateStaticTriangleMeshPhysicsEntity(t850::JoltPhysicsSystem& physics, int sourceObjectIndex) {
  if (sourceObjectIndex < 0 || sourceObjectIndex >= static_cast<int>(g_objects.size())) {
    T8_LOG_ERROR("[T8ditor] Select a loaded render mesh before creating a static triangle mesh.");
    return false;
  }
  SceneObject& selected = g_objects[sourceObjectIndex];
  const auto* renderMesh = dynamic_cast<const t850::RenderMesh*>(selected.litInst.pBase);
  if (!renderMesh) {
    T8_LOG_ERROR("[T8ditor] Selected object '%s' has no render mesh geometry.", selected.name.c_str());
    return false;
  }
  if (!physics.IsInitialized()) {
    T8_LOG_ERROR("[T8ditor] Physics runtime is not initialized.");
    return false;
  }

  t850::PhysicsTriangleMeshBodyDesc desc;
  t850::PhysicsCookStats stats;
  const XMATRIX44 worldFromMesh = selected.wireframe.BuildWorld();
  if (!t850::BuildStaticTriangleMeshBodyDesc(
          *renderMesh,
          worldFromMesh,
          selected.litInst.GetEntityId(),
          g_triangleMeshCookSettings,
          desc,
          &stats)) {
    T8_LOG_ERROR("[T8ditor] Failed to extract static triangle mesh geometry for '%s'.", selected.name.c_str());
    return false;
  }

  desc.debugName = selected.name + " Static Triangle Mesh";
  desc.friction = g_triangleMeshFriction;
  desc.restitution = g_triangleMeshRestitution;
  desc.sensor = g_triangleMeshSensor;

  const double extractionMs = stats.extractionMs;
  t850::PhysicsBodyHandle handle = physics.CreateTriangleMeshBody(desc, &stats);
  stats.extractionMs = extractionMs;
  stats.totalMs += extractionMs;
  if (!handle.IsValid()) {
    T8_LOG_ERROR("[T8ditor] Jolt failed to create static triangle mesh for '%s'.", selected.name.c_str());
    return false;
  }

  PhysicsSceneEntity entity;
  entity.name = MakeUniquePhysicsEntityName(selected.name + " Static Triangle Mesh");
  entity.sourceName = selected.name;
  entity.sourceObjectIndex = sourceObjectIndex;
  entity.body = handle;
  entity.stats = stats;
  entity.cookSettings = g_triangleMeshCookSettings;
  entity.friction = g_triangleMeshFriction;
  entity.restitution = g_triangleMeshRestitution;
  entity.sensor = g_triangleMeshSensor;
  const std::string createdName = entity.name;
  g_physicsEntities.push_back(std::move(entity));
  g_selectedIdx = static_cast<int>(g_physicsEntities.size()) - 1;
  g_selectionType = 3;
  ClearMixedSelection();
  AddMixedSelection(3, g_selectedIdx);
  g_triangleMeshStatus = "Created " + createdName;
  T8_LOG_INFO("[T8ditor] Created physics static triangle mesh '%s': verts=%u tris=%u cacheHit=%d cook=%.2fms total=%.2fms",
              createdName.c_str(),
              stats.vertexCount,
              stats.triangleCount,
              stats.cacheHit ? 1 : 0,
              stats.cookMs,
              stats.totalMs);
  return true;
}

static bool RestorePhysicsEntityFromScene(t850::JoltPhysicsSystem& physics,
                                          const t850::scene::ScenePhysicsEntityDesc& desc) {
  if (desc.type == "player" || desc.type == "character") {
    PhysicsSceneEntity entity;
    entity.type = desc.type == "player" ? PhysicsSceneEntityType::Player : PhysicsSceneEntityType::Character;
    entity.name = desc.name.empty() ? (desc.type == "player" ? "player" : "character") : desc.name;
    entity.sourceName = desc.type == "player" ? "player" : desc.source_object;
    entity.sourceObjectIndex = desc.type == "player" ? -1 : FindSceneObjectIndexByName(desc.source_object);
    entity.visible = desc.visible;
    entity.frozen = desc.frozen;
    entity.showWire = desc.show_wire;
    entity.showOrientation = desc.show_orientation;
    entity.position = XVECTOR3(desc.position.x, desc.position.y, desc.position.z, 1.0f);
    entity.eulerRadians = XVECTOR3(desc.rotation.x * kDegToRad, desc.rotation.y * kDegToRad, desc.rotation.z * kDegToRad, 0.0f);
    entity.playerShape = desc.shape == "capsule" ? t850::PhysicsShapeType::Capsule :
        (desc.shape == "sphere" ? t850::PhysicsShapeType::Sphere :
         (desc.shape == "cylinder" ? t850::PhysicsShapeType::Cylinder : t850::PhysicsShapeType::Box));
    entity.playerHalfExtents = XVECTOR3(desc.half_extents.x, desc.half_extents.y, desc.half_extents.z, 0.0f);
    entity.playerRadius = desc.radius;
    entity.playerHalfHeight = desc.half_height;
    entity.friction = desc.friction;
    entity.restitution = desc.restitution;
    entity.sensor = desc.sensor;
    entity.playerBotRadius = desc.character.bot_radius;
    entity.characterRuntimePath = CharacterRuntimePathFromSceneString(desc.character.runtime_path);
    entity.characterImplementation = desc.character.implementation == "character" ? 0 : 1;
    entity.characterMass = desc.character.mass;
    entity.characterMaxStrength = desc.character.max_strength;
    entity.characterMaxSlopeAngleDeg = desc.character.max_slope_angle_deg;
    entity.characterEnhancedInternalEdgeRemoval = desc.character.enhanced_internal_edge_removal;
    entity.characterSupportingVolumeOffset = desc.character.supporting_volume_offset;
    entity.characterShapeOffset[0] = desc.character.shape_offset.x;
    entity.characterShapeOffset[1] = desc.character.shape_offset.y;
    entity.characterShapeOffset[2] = desc.character.shape_offset.z;
    entity.characterBackFaceMode = desc.character.back_face_mode == "ignore" ? 0 : 1;
    entity.characterPredictiveContactDistance = desc.character.predictive_contact_distance;
    entity.characterMaxCollisionIterations = desc.character.max_collision_iterations;
    entity.characterMaxConstraintIterations = desc.character.max_constraint_iterations;
    entity.characterMinTimeRemaining = desc.character.min_time_remaining;
    entity.characterCollisionTolerance = desc.character.collision_tolerance;
    entity.characterPadding = desc.character.character_padding;
    entity.characterMaxNumHits = desc.character.max_num_hits;
    entity.characterHitReductionCosMaxAngle = desc.character.hit_reduction_cos_max_angle;
    entity.characterPenetrationRecoverySpeed = desc.character.penetration_recovery_speed;
    entity.characterGravityFactor = desc.character.gravity_factor;
    entity.characterAllowTranslationX = desc.character.allow_translation_x;
    entity.characterAllowTranslationY = desc.character.allow_translation_y;
    entity.characterAllowTranslationZ = desc.character.allow_translation_z;
    entity.characterInnerBody = desc.character.inner_body;
    if (!RecreateCharacterPhysicsBody(physics, entity)) {
      return false;
    }
    g_physicsEntities.push_back(std::move(entity));
    return true;
  }

  const int sourceIndex = FindSceneObjectIndexByName(desc.source_object);
  if (sourceIndex < 0) {
    T8_LOG_ERROR("[T8ditor] Cannot restore physics entity '%s': source object '%s' not found",
                 desc.name.c_str(),
                 desc.source_object.c_str());
    return false;
  }

  const t850::PhysicsTriangleMeshCookSettings savedCookSettings = g_triangleMeshCookSettings;
  const float savedFriction = g_triangleMeshFriction;
  const float savedRestitution = g_triangleMeshRestitution;
  const bool savedSensor = g_triangleMeshSensor;
  g_triangleMeshCookSettings = PhysicsCookSettingsFromScene(desc.cook_settings);
  g_triangleMeshFriction = desc.friction;
  g_triangleMeshRestitution = desc.restitution;
  g_triangleMeshSensor = desc.sensor;
  const bool ok = CreateStaticTriangleMeshPhysicsEntity(physics, sourceIndex);
  g_triangleMeshCookSettings = savedCookSettings;
  g_triangleMeshFriction = savedFriction;
  g_triangleMeshRestitution = savedRestitution;
  g_triangleMeshSensor = savedSensor;
  if (!ok || g_physicsEntities.empty()) {
    return false;
  }
  PhysicsSceneEntity& entity = g_physicsEntities.back();
  entity.name = desc.name.empty() ? entity.name : desc.name;
  entity.sourceName = desc.source_object;
  entity.visible = desc.visible;
  entity.frozen = desc.frozen;
  entity.showWire = desc.show_wire;
  entity.showOrientation = desc.show_orientation;
  entity.cookSettings = PhysicsCookSettingsFromScene(desc.cook_settings);
  entity.friction = desc.friction;
  entity.restitution = desc.restitution;
  entity.sensor = desc.sensor;
  return true;
}

t850::RenderSkinnedMesh* GetSkinnedMesh(SceneObject& obj) {
  return obj.litInst.GetSkinnedMesh();
}

bool InputTextString(const char* label, std::string& value) {
  char buffer[512] = {};
  std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
  if (ImGui::InputText(label, buffer, sizeof(buffer))) {
    value = buffer;
    return true;
  }
  return false;
}

t850::scene::SceneObjectPhysicsDesc& EnsurePhysicsMeta(SceneObject& obj) {
  if (!obj.physics) {
    obj.physics = t850::scene::SceneObjectPhysicsDesc{};
  }
  return *obj.physics;
}

t850::scene::SceneObjectNavigationDesc& EnsureNavigationMeta(SceneObject& obj) {
  if (!obj.navigation) {
    obj.navigation = t850::scene::SceneObjectNavigationDesc{};
  }
  return *obj.navigation;
}

t850::scene::SceneObjectRagdollDesc& EnsureRagdollMeta(SceneObject& obj) {
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

void EnsureRagdollHierarchyState(SceneObject& obj) {
  const std::size_t bodyCount = obj.ragdollAuthoring.binding.referencePose.bones.size();
  auto resizeFlags = [](std::vector<uint8_t>& flags, std::size_t count, uint8_t defaultValue) {
    if (flags.size() < count) {
      flags.resize(count, defaultValue);
    } else if (flags.size() > count) {
      flags.resize(count);
    }
  };
  resizeFlags(obj.ragdollBodyVisible, bodyCount, 1);
  resizeFlags(obj.ragdollBodyWire, bodyCount, 1);
  resizeFlags(obj.ragdollJointVisible, bodyCount, 1);
  resizeFlags(obj.ragdollJointWire, bodyCount, 1);
  resizeFlags(obj.ragdollAuthoring.frozenBodies, bodyCount, 0);
  resizeFlags(obj.ragdollAuthoring.frozenJoints, bodyCount, 0);
}

std::string RagdollHierarchyBodyLabel(const SceneObject& obj, int bodyIndex) {
  if (bodyIndex >= 0 &&
      bodyIndex < static_cast<int>(obj.ragdollAuthoring.binding.referencePose.bones.size())) {
    const t850::PhysicsRagdollBoneDesc& bone =
        obj.ragdollAuthoring.binding.referencePose.bones[static_cast<std::size_t>(bodyIndex)];
    if (!bone.body.debugName.empty()) {
      return bone.body.debugName;
    }
    if (bone.body.boneIndex >= 0) {
      return "Bone " + std::to_string(bone.body.boneIndex);
    }
  }
  return "Body " + std::to_string(bodyIndex);
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

bool GetEditorObjectWorldAABB(const SceneObject& object,
                              t850::AABB& outBounds,
                              t850::AABB* outWireBounds,
                              t850::AABB* outSkeletonBounds,
                              bool* outHasSkeletonBounds) {
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

const t850::SelectorDesc* FindEditorSelectorDesc(const std::vector<t850::SelectorDesc>& selectors,
                                                        const std::string& name) {
  for (const auto& selector : selectors) {
    if (selector.name == name) {
      return &selector;
    }
  }
  return nullptr;
}

std::string EditorCubemapPathForSelectorIndex(const t850::SelectorDesc& selector, int selectedIndex) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(selector.options.size())) {
    return {};
  }
  return "sky/" + selector.options[static_cast<std::size_t>(selectedIndex)];
}

int EditorCubemapSelectorIndexForPath(const t850::SelectorDesc& selector,
                                             const std::string& resourcePath) {
  for (int index = 0; index < static_cast<int>(selector.options.size()); ++index) {
    if (EditorResourcePathEquals(EditorCubemapPathForSelectorIndex(selector, index), resourcePath)) {
      return index;
    }
  }
  return -1;
}

int EditorCubemapSelectorIndexFromProfile(const t850::SandboxProfileDesc& profile) {
  for (const t850::IntOverrideDesc& selector : profile.selectors) {
    if (selector.name == "cubemap") {
      return selector.value;
    }
  }
  return -1;
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

float EstimateRagdollRadius(const SceneObject& obj) {
  t850::AABB bounds = obj.wireframe.WorldAABB();
  if (!bounds.IsValid()) return 1.0f;
  XVECTOR3 ext = bounds.Extents();
  const float radius = std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
  return (std::max)(radius, 0.01f);
}

t850::PhysicsRagdollBuildSettings BuildEditorRagdollSettings(const SceneObject& obj) {
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

const char* RagdollShapeTypeName(t850::PhysicsShapeType type) {
  return t850::ragdoll_editor::ShapeTypeName(type);
}

const char* RagdollJointTypeName(t850::PhysicsRagdollJointType type) {
  return t850::ragdoll_editor::JointTypeName(type);
}

void EnsureEditorRagdollState(t850::PhysicsRagdollAuthoringDesc& authoring) {
  t850::ragdoll_editor::RagdollEditorTool tool(authoring);
  tool.EnsureState();
}

void RagdollMatrixToComponents(const XMATRIX44& matrix, float translation[3], float rotationDeg[3], float scale[3]) {
  XMATRIX44 copy = matrix;
  ImGuizmo::DecomposeMatrixToComponents(&copy.m[0][0], translation, rotationDeg, scale);
}

XMATRIX44 RagdollMatrixFromComponents(const float translation[3], const float rotationDeg[3], const float scale[3]) {
  XMATRIX44 matrix;
  ImGuizmo::RecomposeMatrixFromComponents(translation, rotationDeg, scale, &matrix.m[0][0]);
  return matrix;
}

void SyncRagdollMetaFromObject(SceneObject& obj) {
  t850::scene::SceneObjectRagdollDesc& meta = EnsureRagdollMeta(obj);
  meta.asset = obj.ragdollResourcePath;
  meta.preview = obj.ragdollPreviewEnabled || obj.ragdollDebugDraw;
  meta.drive_from_animation = true;
  meta.runtime_motion = meta.preview ? "kinematic" : "disabled";
}

float RagdollDot3(const XVECTOR3& a, const XVECTOR3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float RagdollLength3(const XVECTOR3& value) {
  return std::sqrt(RagdollDot3(value, value));
}

XVECTOR3 RagdollCross3(const XVECTOR3& a, const XVECTOR3& b) {
  return XVECTOR3(
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
      0.0f);
}

XVECTOR3 RagdollNormalize3(const XVECTOR3& value, const XVECTOR3& fallback) {
  const float lenSq = RagdollDot3(value, value);
  if (lenSq <= 0.00000001f) {
    return fallback;
  }
  const float invLen = 1.0f / std::sqrt(lenSq);
  return XVECTOR3(value.x * invLen, value.y * invLen, value.z * invLen, 0.0f);
}

XVECTOR3 RagdollMatrixAxis(const XMATRIX44& matrix, int axis) {
  if (axis == 0) return RagdollNormalize3(XVECTOR3(matrix.m11, matrix.m12, matrix.m13, 0.0f), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  if (axis == 1) return RagdollNormalize3(XVECTOR3(matrix.m21, matrix.m22, matrix.m23, 0.0f), XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  return RagdollNormalize3(XVECTOR3(matrix.m31, matrix.m32, matrix.m33, 0.0f), XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
}

XVECTOR3 RagdollMatrixTranslation(const XMATRIX44& matrix) {
  return XVECTOR3(matrix.m41, matrix.m42, matrix.m43, 1.0f);
}

bool RagdollRayPlaneIntersection(const t850::Ray& ray,
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

bool RagdollClosestRayAxisParameter(const t850::Ray& ray,
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

float RagdollDistancePointToSegmentSq(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
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

ImU32 RagdollAxisColor(int axis, bool active) {
  if (axis == 0) return active ? IM_COL32(255, 96, 96, 255) : IM_COL32(220, 70, 70, 240);
  if (axis == 1) return active ? IM_COL32(96, 255, 96, 255) : IM_COL32(80, 210, 80, 240);
  return active ? IM_COL32(96, 160, 255, 255) : IM_COL32(80, 130, 230, 240);
}

float RagdollAxisCoord(const XVECTOR3& value, int axis) {
  if (axis == 0) return value.x;
  if (axis == 1) return value.y;
  return value.z;
}

void RagdollSetAxisCoord(XVECTOR3& value, int axis, float coord) {
  if (axis == 0) value.x = coord;
  else if (axis == 1) value.y = coord;
  else value.z = coord;
}

XVECTOR3 RagdollClampBoxHalfExtents(const XVECTOR3& halfExtents) {
  return XVECTOR3(
      (std::max)(kRagdollEditorMinShapeExtent, halfExtents.x),
      (std::max)(kRagdollEditorMinShapeExtent, halfExtents.y),
      (std::max)(kRagdollEditorMinShapeExtent, halfExtents.z),
      0.0f);
}

XVECTOR3 RagdollTransformVectorNoTranslation(const XVECTOR3& vector, const XMATRIX44& matrix) {
  return XVECTOR3(
      vector.x * matrix.m11 + vector.y * matrix.m21 + vector.z * matrix.m31,
      vector.x * matrix.m12 + vector.y * matrix.m22 + vector.z * matrix.m32,
      vector.x * matrix.m13 + vector.y * matrix.m23 + vector.z * matrix.m33,
      0.0f);
}

XVECTOR3 RagdollRotateVectorAroundAxis(const XVECTOR3& vector, const XVECTOR3& axisWorld, float angleRadians) {
  XMATRIX44 rotation;
  XMatRotationAxis(rotation, RagdollNormalize3(axisWorld, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)), angleRadians);
  return RagdollNormalize3(RagdollTransformVectorNoTranslation(vector, rotation), vector);
}

bool RagdollIsValidAxis(const XVECTOR3& axis) {
  return std::isfinite(axis.x) &&
         std::isfinite(axis.y) &&
         std::isfinite(axis.z) &&
         RagdollLength3(axis) > 0.000001f;
}

void RagdollNormalizeJointFrameAxes(XVECTOR3& twist,
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

void* NativeHandleFromImGuiViewport(ImGuiViewport* viewport) {
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

void ApplyNativeWindowChrome(ImGuiViewport* viewport, const char* title) {
  if (!viewport || !viewport->PlatformHandle) {
    return;
  }

  viewport->Flags &= ~(ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon);
  SDL_Window* sdlWindow = static_cast<SDL_Window*>(viewport->PlatformHandle);
  SDL_SetWindowTitle(sdlWindow, title ? title : "T8ditor");
  SDL_SetWindowBordered(sdlWindow, true);
  SDL_SetWindowResizable(sdlWindow, true);
}

static bool ShouldDrawPhysicsDebug() {
  for (SceneObject& obj : g_objects) {
    if (obj.ragdollDebugDraw && obj.litInst.HasPhysicsRagdoll())
      return true;
  }
  return false;
}

void EditorApp::ResetEditorNavMeshState(bool keepSettings) {
  m_editorNavMesh.Clear();
  m_editorNavMeshDebugRenderer.ReleaseCachedGeometry();
  m_editorNavMeshSourceStats = t850::navigation::NavSourceBuildStats{};
  m_editorNavMeshStatus.clear();
  m_editorNavMeshAuthored = false;
  m_editorNavMeshVisible = true;
  m_editorNavMeshFrozen = false;
  m_editorNavMeshShowWire = true;
  m_editorNavMeshDebugOffset = 0.01f;
  m_editorNavMeshDebugShapeMode = 0;
  m_editorNavMeshLastBuildMs = 0.0f;
  m_editorNavMeshDirty = false;
  ReleaseGizmoCaches(g_navMeshVolumeGizmos);
  m_editorNavMeshVolumes.clear();
  m_editorNavMeshLinks.clear();
  m_editorNavMeshRuntimeMode = "build_cached";
  m_editorNavMeshBakedAsset.clear();
  m_editorNavMeshNodes.clear();
  m_editorSelectedNavVolume = -1;
  m_editorSelectedNavLink = -1;
  m_editorSelectedNavTriangle = -1;
  m_editorNavLinkPickMode = 0;
  InvalidateEditorNavMeshClassification();
  if (!keepSettings) {
    m_editorNavMeshBuildSettings = DefaultEditorNavMeshBuildSettings();
  }
}

void EditorApp::MarkEditorNavMeshDirty(const std::string& status) {
  m_editorNavMeshDirty = true;
  m_editorNavMeshAuthored = true;
  InvalidateEditorNavMeshClassification();
  m_editorNavMeshStatus = status.empty()
      ? "NavMesh authoring changed. Click Re-generate to update the preview."
      : status;
}

void EditorApp::InvalidateEditorNavMeshClassification() {
  m_editorNavMeshClassificationReady = false;
  m_editorNavMeshClassificationDirty = true;
  m_editorNavMeshClassification = t850::navigation::NavMeshClassificationResult{};
  m_editorSelectedNavTriangle = -1;
  ReleaseNavMeshClassificationGizmos();
}

bool EditorApp::DeleteSelectedNavAuthoringChild() {
  if (m_editorSelectedNavVolume >= 0 &&
      m_editorSelectedNavVolume < static_cast<int>(m_editorNavMeshVolumes.size())) {
    if (m_editorSelectedNavVolume < static_cast<int>(g_navMeshVolumeGizmos.size())) {
      ReleaseGizmoCache(g_navMeshVolumeGizmos[static_cast<std::size_t>(m_editorSelectedNavVolume)]);
      g_navMeshVolumeGizmos.erase(g_navMeshVolumeGizmos.begin() + m_editorSelectedNavVolume);
    }
    m_editorNavMeshVolumes.erase(m_editorNavMeshVolumes.begin() + m_editorSelectedNavVolume);
    m_editorSelectedNavVolume = m_editorNavMeshVolumes.empty()
        ? -1
        : std::clamp(m_editorSelectedNavVolume, 0, static_cast<int>(m_editorNavMeshVolumes.size()) - 1);
    m_editorSelectedNavLink = -1;
    MarkEditorNavMeshDirty("NavMesh volume deleted. Click Re-generate.");
    return true;
  }
  if (m_editorSelectedNavLink >= 0 &&
      m_editorSelectedNavLink < static_cast<int>(m_editorNavMeshLinks.size())) {
    m_editorNavMeshLinks.erase(m_editorNavMeshLinks.begin() + m_editorSelectedNavLink);
    m_editorSelectedNavLink = m_editorNavMeshLinks.empty()
        ? -1
        : std::clamp(m_editorSelectedNavLink, 0, static_cast<int>(m_editorNavMeshLinks.size()) - 1);
    m_editorSelectedNavVolume = -1;
    m_editorSelectedNavTriangle = -1;
    MarkEditorNavMeshDirty("Authored link deleted. Click Re-generate.");
    return true;
  }
  return false;
}

bool EditorApp::UpdateEditorNavMeshClassification() {
  if (!m_editorNavMeshClassificationDirty) {
    return m_editorNavMeshClassificationReady;
  }
  m_editorNavMeshClassificationDirty = false;
  m_editorNavMeshClassificationReady = false;
  m_editorNavMeshClassification = t850::navigation::NavMeshClassificationResult{};
  ReleaseNavMeshClassificationGizmos();

  if (g_objects.empty()) {
    return false;
  }

  SyncSceneObjectTransforms();
  std::vector<t850::navigation::NavSourceInstance> navSources;
  navSources.reserve(g_objects.size());
  for (const SceneObject& object : g_objects) {
    if (object.transient) {
      continue;
    }
    t850::navigation::NavSourceInstance source;
    source.entityId = object.litInst.GetEntityId();
    source.instance = &object.litInst;
    source.worldTransform = object.litInst.Final;
    source.visible = object.visible;
    source.debugName = object.name;
    const t850::scene::SceneObjectNavigationDesc navigation =
        object.navigation.value_or(t850::scene::SceneObjectNavigationDesc{});
    source.includeInNavigation = navigation.include;
    source.navigationStatic = navigation.static_object;
    source.navigationWalkable = navigation.walkable;
    source.area = navigation.walkable ? 0 : -1;
    navSources.push_back(source);
  }

  t850::navigation::NavMeshGeometry geometry;
  t850::navigation::NavSourceBuildStats sourceStats;
  std::string error;
  if (!t850::navigation::BuildGeometryFromNavSources(navSources, geometry, &sourceStats, &error)) {
    m_editorNavMeshStatus = "Source preview skipped: " + error;
    return false;
  }
  for (const t850::scene::SceneNavMeshVolumeDesc& volumeDesc : m_editorNavMeshVolumes) {
    t850::navigation::NavMeshVolumeModifier modifier = NavVolumeModifierFromScene(volumeDesc);
    if (!modifier.enabled) {
      continue;
    }
    geometry.volumeModifiers.push_back(modifier);
    if (modifier.mode == t850::navigation::NavMeshModifierMode::Area) {
      geometry.areaCosts.push_back({modifier.area, modifier.cost});
    }
  }

  if (!t850::navigation::ClassifyNavMeshTriangles(
          geometry,
          m_editorNavMeshBuildSettings,
          m_editorNavMeshClassification,
          &error)) {
    m_editorNavMeshStatus = "Source preview failed: " + error;
    return false;
  }
  m_editorNavMeshClassificationReady = true;
  return true;
}

bool EditorApp::PickEditorNavMeshTriangleFromMouse(int mouseX, int mouseY, int& outTriangleIndex) const {
  outTriangleIndex = -1;
  if (!m_editorNavMeshClassificationReady || !m_sceneProps.GetPrimaryCamera()) {
    return false;
  }
  const Camera& cam = *m_sceneProps.GetPrimaryCamera();
  const t850::Ray ray = BuildEditorCameraRay(cam,
                                             static_cast<float>(mouseX),
                                             static_cast<float>(mouseY),
                                             m_lastW,
                                             m_lastH);
  float bestT = FLT_MAX;
  for (const t850::navigation::NavTriangleClassification& tri : m_editorNavMeshClassification.triangles) {
    float t = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    if (t850::RayIntersectsTriangle(ray, tri.vertices[0], tri.vertices[1], tri.vertices[2], t, u, v) && t < bestT) {
      bestT = t;
      outTriangleIndex = tri.triangleIndex;
    }
  }
  return outTriangleIndex >= 0;
}

bool EditorApp::CreateNavVolumeFromSelectedTriangle(const char* type) {
  if (m_editorSelectedNavTriangle < 0) {
    return false;
  }
  if (!UpdateEditorNavMeshClassification()) {
    return false;
  }
  const auto it = std::find_if(
      m_editorNavMeshClassification.triangles.begin(),
      m_editorNavMeshClassification.triangles.end(),
      [&](const t850::navigation::NavTriangleClassification& tri) {
        return tri.triangleIndex == m_editorSelectedNavTriangle;
      });
  if (it == m_editorNavMeshClassification.triangles.end()) {
    return false;
  }

  t850::AABB bounds;
  for (const XVECTOR3& p : it->vertices) {
    bounds.ExpandToInclude(p);
  }
  const XVECTOR3 center = bounds.Center();
  const XVECTOR3 extents = bounds.Extents();
  t850::scene::SceneNavMeshVolumeDesc volume;
  volume.name = std::string(type && std::strcmp(type, "area_cost") == 0 ? "Area Cost From Triangle " : "Exclude From Triangle ") +
      std::to_string(m_editorNavMeshVolumes.size() + 1);
  volume.type = type && *type ? type : "exclude";
  volume.shape = "box";
  volume.position = {center.x, center.y, center.z};
  volume.half_extents = {
      (std::max)(1.0f, extents.x * 0.5f + 0.5f),
      (std::max)(1.0f, extents.y * 0.5f + m_editorNavMeshBuildSettings.agentHeight * 0.5f),
      (std::max)(1.0f, extents.z * 0.5f + 0.5f)};
  volume.enabled = true;
  volume.visible = true;
  volume.show_wire = true;
  if (volume.type == "area_cost") {
    volume.area = "mud";
    volume.cost = 3.0f;
  }

  m_editorNavMeshVolumes.push_back(volume);
  m_editorSelectedNavVolume = static_cast<int>(m_editorNavMeshVolumes.size()) - 1;
  m_editorSelectedNavLink = -1;
  g_selectionType = 4;
  g_selectedIdx = 0;
  ClearMixedSelection();
  MarkEditorNavMeshDirty(volume.type == "area_cost"
      ? "Area-cost volume created from selected source triangle. Click Re-generate."
      : "Exclude volume created from selected source triangle. Click Re-generate.");
  return true;
}

void EditorApp::DrawNavMeshClassificationOverlay(EditorLineRenderer& lines, const XMATRIX44& vp) {
  if (!m_editorNavMeshShowSourcePreview || !lines.IsReady()) {
    return;
  }
  if (!UpdateEditorNavMeshClassification()) {
    return;
  }

  bool hasCachedGeometry = false;
  for (const GizmoCache& cache : g_navMeshClassificationGizmos) {
    if (cache.vb && cache.ib && cache.count > 0) {
      hasCachedGeometry = true;
      break;
    }
  }

  enum Bucket {
    Included = 0,
    AreaCost = 1,
    ExcludedVolume = 2,
    OutsideInclude = 3,
    ExcludedSlope = 4,
    Selected = 5,
    Count = 6
  };
  struct BucketGeometry {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
  };
  if (!hasCachedGeometry) {
    std::array<BucketGeometry, Bucket::Count> buckets;

    auto appendTriangle = [&](Bucket bucket, const t850::navigation::NavTriangleClassification& tri) {
      BucketGeometry& geometry = buckets[static_cast<std::size_t>(bucket)];
      const unsigned int base = static_cast<unsigned int>(geometry.vertices.size() / 4u);
      for (const XVECTOR3& point : tri.vertices) {
        geometry.vertices.push_back(point.x);
        geometry.vertices.push_back(point.y + m_editorNavMeshDebugOffset + 0.02f);
        geometry.vertices.push_back(point.z);
        geometry.vertices.push_back(1.0f);
      }
      geometry.indices.insert(geometry.indices.end(), {
          base + 0u, base + 1u,
          base + 1u, base + 2u,
          base + 2u, base + 0u});
    };

    for (const t850::navigation::NavTriangleClassification& tri : m_editorNavMeshClassification.triangles) {
      if (tri.triangleIndex == m_editorSelectedNavTriangle) {
        appendTriangle(Selected, tri);
      }
      if (tri.included) {
        appendTriangle(tri.area == 0 ? Included : AreaCost, tri);
        continue;
      }
      if (tri.reason == t850::navigation::NavTriangleClassificationReason::ExcludedByVolume) {
        appendTriangle(ExcludedVolume, tri);
      } else if (tri.reason == t850::navigation::NavTriangleClassificationReason::OutsideIncludeVolume) {
        appendTriangle(OutsideInclude, tri);
      } else {
        appendTriangle(ExcludedSlope, tri);
      }
    }

    for (std::size_t bucketIndex = 0; bucketIndex < buckets.size(); ++bucketIndex) {
      const BucketGeometry& bucket = buckets[bucketIndex];
      if (bucket.vertices.empty() || bucket.indices.empty()) {
        continue;
      }
      GizmoCache& cache = g_navMeshClassificationGizmos[bucketIndex];
      cache.vb = EditorLineRenderer::CreatePositionVB(
          bucket.vertices.data(),
          static_cast<unsigned>(bucket.vertices.size() / 4u));
      cache.ib = EditorLineRenderer::CreateIndexBuffer32(
          bucket.indices.data(),
          static_cast<unsigned>(bucket.indices.size()));
      cache.count = static_cast<unsigned>(bucket.indices.size());
    }
  }

  const XVECTOR3 colors[Bucket::Count] = {
      XVECTOR3(0.18f, 0.42f, 0.95f, 1.0f),
      XVECTOR3(0.95f, 0.45f, 1.0f, 1.0f),
      XVECTOR3(1.0f, 0.15f, 0.08f, 1.0f),
      XVECTOR3(0.22f, 0.22f, 0.22f, 1.0f),
      XVECTOR3(0.55f, 0.55f, 0.55f, 1.0f),
      XVECTOR3(1.0f, 1.0f, 0.1f, 1.0f)
  };
  XMATRIX44 identity;
  XMatIdentity(identity);
  for (std::size_t bucketIndex = 0; bucketIndex < g_navMeshClassificationGizmos.size(); ++bucketIndex) {
    GizmoCache& cache = g_navMeshClassificationGizmos[bucketIndex];
    if (!cache.vb || !cache.ib || cache.count == 0) {
      continue;
    }
    lines.DrawLines(identity,
                    vp,
                    colors[bucketIndex],
                    cache.vb,
                    cache.ib,
                    cache.count,
                    sizeof(float) * 4,
                    t850::IndexBufferFormat::R32);
  }
}

bool EditorApp::CreateEditorNavMesh() {
  if (g_objects.empty()) {
    m_editorNavMeshStatus = "No scene meshes are loaded.";
    T8_LOG_ERROR("[T8ditor] NavMesh build skipped: no scene meshes are loaded.");
    return false;
  }

  const bool wasAuthored = m_editorNavMeshAuthored;
  SyncSceneObjectTransforms();
  m_editorNavMeshDebugRenderer.ReleaseCachedGeometry();

  std::vector<t850::navigation::NavSourceInstance> navSources;
  navSources.reserve(g_objects.size());
  for (const SceneObject& object : g_objects) {
    if (object.transient) {
      continue;
    }
    t850::navigation::NavSourceInstance source;
    source.entityId = object.litInst.GetEntityId();
    source.instance = &object.litInst;
    source.worldTransform = object.litInst.Final;
    source.visible = object.visible;
    source.debugName = object.name;
    const t850::scene::SceneObjectNavigationDesc navigation =
        object.navigation.value_or(t850::scene::SceneObjectNavigationDesc{});
    source.includeInNavigation = navigation.include;
    source.navigationStatic = navigation.static_object;
    source.navigationWalkable = navigation.walkable;
    source.area = navigation.walkable ? 0 : -1;
    navSources.push_back(source);
  }

  t850::navigation::NavMeshGeometry geometry;
  t850::navigation::NavSourceBuildStats sourceStats;
  std::string error;
  const auto buildStart = std::chrono::steady_clock::now();
  if (!t850::navigation::BuildGeometryFromNavSources(navSources, geometry, &sourceStats, &error)) {
    m_editorNavMeshStatus = "Build skipped: " + error;
    T8_LOG_ERROR("[T8ditor] NavMesh build skipped: %s (considered=%d included=%d skippedInvisible=%d skippedSkinned=%d skippedInvalid=%d)",
                 error.c_str(),
                 sourceStats.considered,
                 sourceStats.included,
                 sourceStats.skippedInvisible,
                 sourceStats.skippedSkinned,
                 sourceStats.skippedInvalid);
    return false;
  }
  if (m_physics.IsInitialized()) {
    geometry.offMeshLinkValidator = [this](const t850::navigation::NavOffMeshLink& link) {
      return t850::ValidateNavOffMeshLinkWithPhysics(m_physics, m_editorNavMeshBuildSettings, link);
    };
    geometry.offMeshHybridLinkValidator = geometry.offMeshLinkValidator;
  }
  for (const t850::scene::SceneNavMeshVolumeDesc& volumeDesc : m_editorNavMeshVolumes) {
    t850::navigation::NavMeshVolumeModifier modifier = NavVolumeModifierFromScene(volumeDesc);
    if (!modifier.enabled) {
      continue;
    }
    geometry.volumeModifiers.push_back(modifier);
    if (modifier.mode == t850::navigation::NavMeshModifierMode::Area) {
      geometry.areaCosts.push_back({modifier.area, modifier.cost});
    }
  }
  for (const t850::scene::SceneNavMeshLinkDesc& linkDesc : m_editorNavMeshLinks) {
    if (IsUsableAuthoredNavLink(linkDesc)) {
      geometry.offMeshLinks.push_back(NavOffMeshLinkFromScene(linkDesc));
    }
  }

  if (t850::navigation::ClassifyNavMeshTriangles(
          geometry,
          m_editorNavMeshBuildSettings,
          m_editorNavMeshClassification,
          &error)) {
    m_editorNavMeshClassificationReady = true;
    m_editorNavMeshClassificationDirty = false;
    ReleaseNavMeshClassificationGizmos();
  }

  t850::navigation::NavMesh builtNavMesh;
  if (!builtNavMesh.Build(geometry, m_editorNavMeshBuildSettings, &error)) {
    m_editorNavMeshStatus = "Build failed: " + error;
    T8_LOG_ERROR("[T8ditor] NavMesh build failed: %s", error.c_str());
    return false;
  }

  m_editorNavMesh = std::move(builtNavMesh);
  m_editorNavMeshSourceStats = sourceStats;
  m_editorNavMeshAuthored = true;
  if (!wasAuthored) {
    m_editorNavMeshVisible = true;
    m_editorNavMeshFrozen = false;
    m_editorNavMeshShowWire = true;
  }
  m_editorNavMeshLastBuildMs =
      std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - buildStart).count();
  m_editorNavMeshDebugRenderer.Invalidate();
  RefreshEditorNavMeshNodes();
  DumpEditorNavMeshWireGeometry(wasAuthored ? "regenerate" : "create");

  const t850::navigation::NavMeshBuildStats& stats = m_editorNavMesh.GetStats();
  char status[256] = {};
  std::snprintf(status,
                sizeof(status),
                "Created NavMesh: sources=%d verts=%d tris=%d polys=%d offMesh=%d total=%.2fms",
                sourceStats.included,
                stats.vertexCount,
                stats.triangleCount,
                stats.polygonCount,
                stats.offMeshLinkCount,
                m_editorNavMeshLastBuildMs);
  m_editorNavMeshStatus = status;
  m_editorNavMeshDirty = false;
  return true;
}

void EditorApp::DestroyEditorNavMesh() {
  m_editorNavMesh.Clear();
  m_editorNavMeshDebugRenderer.ReleaseCachedGeometry();
  m_editorNavMeshSourceStats = t850::navigation::NavSourceBuildStats{};
  m_editorNavMeshAuthored = false;
  m_editorNavMeshLastBuildMs = 0.0f;
  m_editorNavMeshDirty = false;
  ReleaseGizmoCaches(g_navMeshVolumeGizmos);
  m_editorNavMeshVolumes.clear();
  m_editorNavMeshLinks.clear();
  m_editorNavMeshRuntimeMode = "build_cached";
  m_editorNavMeshBakedAsset.clear();
  m_editorSelectedNavVolume = -1;
  m_editorSelectedNavLink = -1;
  m_editorNavLinkPickMode = 0;
  InvalidateEditorNavMeshClassification();
  m_editorNavMeshStatus = "NavMesh destroyed.";
  DumpEditorNavMeshWireGeometry("destroy");
  if (g_selectionType == 4) {
    g_selectionType = 0;
    g_selectedIdx = -1;
  }
}

void EditorApp::RestoreEditorNavMeshFromScene(const t850::scene::SceneNavigationMeshDesc& desc) {
  m_editorNavMeshBuildSettings = NavMeshBuildSettingsFromScene(desc.build_settings);
  m_editorNavMeshVisible = desc.visible;
  m_editorNavMeshFrozen = desc.frozen;
  m_editorNavMeshShowWire = desc.show_wire;
  m_editorNavMeshDebugOffset = desc.debug_offset;
  m_editorNavMeshDebugShapeMode = std::clamp(desc.debug_shape_mode, 0, 1);
  m_editorNavMeshRuntimeMode = desc.runtime_mode.empty() ? "build_cached" : desc.runtime_mode;
  m_editorNavMeshBakedAsset = desc.baked_asset;
  m_editorNavMeshVolumes = desc.volumes;
  m_editorNavMeshLinks = desc.authored_links;
  if (m_editorSelectedNavVolume < 0 ||
      m_editorSelectedNavVolume >= static_cast<int>(m_editorNavMeshVolumes.size())) {
    m_editorSelectedNavVolume = -1;
  }
  m_editorNavMeshDirty = false;
  m_editorSelectedNavLink = m_editorNavMeshLinks.empty() ? -1 : std::clamp(m_editorSelectedNavLink, 0, static_cast<int>(m_editorNavMeshLinks.size()) - 1);
  m_editorNavLinkPickMode = 0;
  m_editorNavMeshAuthored = desc.enabled;
  m_editorNavMeshDirty = false;
  m_editorNavMeshStatus.clear();
  m_editorNavMesh.Clear();
  m_editorNavMeshDebugRenderer.ReleaseCachedGeometry();
  if (desc.enabled && !CreateEditorNavMesh()) {
    m_editorNavMeshAuthored = true;
  }
  DumpEditorNavMeshWireGeometry("restore");
}

t850::scene::SceneNavigationMeshDesc EditorApp::BuildEditorNavMeshDesc() const {
  t850::scene::SceneNavigationMeshDesc desc;
  desc.name = "NavMesh";
  desc.enabled = m_editorNavMeshAuthored;
  desc.visible = m_editorNavMeshVisible;
  desc.frozen = m_editorNavMeshFrozen;
  desc.show_wire = m_editorNavMeshShowWire;
  desc.debug_offset = m_editorNavMeshDebugOffset;
  desc.debug_shape_mode = m_editorNavMeshDebugShapeMode;
  desc.runtime_mode = m_editorNavMeshRuntimeMode.empty() ? "build_cached" : m_editorNavMeshRuntimeMode;
  desc.baked_asset = m_editorNavMeshBakedAsset;
  desc.build_settings = NavMeshBuildSettingsToScene(m_editorNavMeshBuildSettings);
  desc.volumes = m_editorNavMeshVolumes;
  desc.authored_links = m_editorNavMeshLinks;
  return desc;
}

bool EditorApp::GetEditorNavMeshWorldAABB(t850::AABB& outBounds) const {
  outBounds = t850::AABB{};
  if (!m_editorNavMesh.IsReady()) {
    return false;
  }

  std::vector<XVECTOR3> vertices;
  std::vector<unsigned int> indices;
  if (!m_editorNavMesh.GetDebugWireframe(vertices, indices, 0.0f) || vertices.empty()) {
    return false;
  }

  bool expanded = false;
  for (const XVECTOR3& vertex : vertices) {
    outBounds.ExpandToInclude(vertex.x, vertex.y, vertex.z);
    expanded = true;
  }
  return expanded && outBounds.IsValid();
}

void EditorApp::DumpEditorNavMeshWireGeometry(const char* reason) const {
  static uint32_t s_dumpSerial = 0;
  const std::string reasonText = reason && reason[0] ? reason : "unknown";
  std::string safeReason;
  safeReason.reserve(reasonText.size());
  for (unsigned char c : reasonText) {
    safeReason.push_back((std::isalnum(c) || c == '_' || c == '-') ? static_cast<char>(c) : '_');
  }

  std::filesystem::path dumpDir("Logs");
  std::error_code ec;
  std::filesystem::create_directories(dumpDir, ec);
  std::ostringstream name;
  name << "navmesh_wire_" << std::setw(4) << std::setfill('0') << (++s_dumpSerial) << "_" << safeReason << ".txt";
  const std::filesystem::path dumpPath = dumpDir / name.str();
  std::ofstream out(dumpPath, std::ios::binary);
  if (!out.is_open()) {
    T8_LOG_DEBUG("[T8ditor][NavMeshDump] Failed to open '%s'", dumpPath.string().c_str());
    return;
  }

  auto dumpLines = [&](const char* label,
                       const std::vector<XVECTOR3>& vertices,
                       const std::vector<unsigned int>& indices) {
    out << "\n[" << label << "] vertices=" << vertices.size()
        << " indices=" << indices.size()
        << " lines=" << (indices.size() / 2) << "\n";
    for (std::size_t i = 0; i < vertices.size(); ++i) {
      const XVECTOR3& v = vertices[i];
      out << "v " << i << " " << v.x << " " << v.y << " " << v.z << " " << v.w << "\n";
    }
    for (std::size_t i = 0; i + 1 < indices.size(); i += 2) {
      out << "l " << (i / 2) << " " << indices[i] << " " << indices[i + 1] << "\n";
    }
  };

  out << "reason=" << reasonText << "\n";
  out << "authored=" << (m_editorNavMeshAuthored ? 1 : 0)
      << " ready=" << (m_editorNavMesh.IsReady() ? 1 : 0)
      << " visible=" << (m_editorNavMeshVisible ? 1 : 0)
      << " wire=" << (m_editorNavMeshShowWire ? 1 : 0)
      << " dirty=" << (m_editorNavMeshDirty ? 1 : 0)
      << " shapeMode=" << m_editorNavMeshDebugShapeMode
      << " pickMode=" << m_editorNavLinkPickMode
      << " selectedLink=" << m_editorSelectedNavLink
      << " nodeCount=" << m_editorNavMeshNodes.size()
      << "\n";
  const t850::navigation::NavMeshBuildStats stats = m_editorNavMesh.GetStats();
  out << "stats polys=" << stats.polygonCount
      << " verts=" << stats.vertexCount
      << " tris=" << stats.triangleCount
      << " offMesh=" << stats.offMeshLinkCount
      << " drop=" << stats.dropLinkCount
      << " jump=" << stats.jumpLinkCount
      << " jumpPad=" << stats.jumpPadLinkCount
      << "\n";

  for (std::size_t i = 0; i < m_editorNavMeshNodes.size(); ++i) {
    const XVECTOR3& node = m_editorNavMeshNodes[i];
    out << "node " << i << " " << node.x << " " << node.y << " " << node.z << " " << node.w << "\n";
  }
  for (std::size_t i = 0; i < m_editorNavMeshLinks.size(); ++i) {
    const auto& link = m_editorNavMeshLinks[i];
    out << "authored_link " << i
        << " name=\"" << link.name << "\""
        << " type=" << link.type
        << " enabled=" << (link.enabled ? 1 : 0)
        << " start_node=" << link.start_node
        << " end_node=" << link.end_node
        << " start=(" << link.start.x << "," << link.start.y << "," << link.start.z << ")"
        << " end=(" << link.end.x << "," << link.end.y << "," << link.end.z << ")"
        << " radius=" << link.radius
        << " bidirectional=" << (link.bidirectional ? 1 : 0)
        << " cost=" << link.cost
        << " usable=" << (IsUsableAuthoredNavLink(link) ? 1 : 0)
        << "\n";
  }

  if (m_editorNavMesh.IsReady()) {
    std::vector<XVECTOR3> vertices;
    std::vector<unsigned int> indices;
    if (m_editorNavMesh.GetDebugWireframe(vertices, indices, m_editorNavMeshDebugOffset)) {
      dumpLines("navmesh_geometry", vertices, indices);
    }
    if (m_editorNavMesh.GetDebugNodeMarkers(vertices, indices, m_editorNavMeshDebugOffset)) {
      dumpLines("navmesh_nodes", vertices, indices);
    }
    if (m_editorNavMesh.GetDebugGraphEdges(vertices, indices, m_editorNavMeshDebugOffset + 0.005f)) {
      dumpLines("navmesh_graph", vertices, indices);
    }
    if (m_editorNavMesh.GetDebugOffMeshLinks(t850::navigation::NavTraversalType::Drop, vertices, indices, m_editorNavMeshDebugOffset + 0.015f)) {
      dumpLines("navmesh_drop_links", vertices, indices);
    }
    if (m_editorNavMesh.GetDebugOffMeshLinks(t850::navigation::NavTraversalType::Jump, vertices, indices, m_editorNavMeshDebugOffset + 0.025f)) {
      dumpLines("navmesh_jump_links", vertices, indices);
    }
    if (m_editorNavMesh.GetDebugOffMeshLinks(t850::navigation::NavTraversalType::JumpPad, vertices, indices, m_editorNavMeshDebugOffset + 0.035f)) {
      dumpLines("navmesh_jump_pad_links", vertices, indices);
    }
  }
  if (m_editorSelectedNavLink >= 0 &&
      m_editorSelectedNavLink < static_cast<int>(m_editorNavMeshLinks.size())) {
    const auto& link = m_editorNavMeshLinks[static_cast<std::size_t>(m_editorSelectedNavLink)];
    std::vector<XVECTOR3> overlayVertices;
    std::vector<unsigned int> overlayIndices;
    if (IsUsableAuthoredNavLink(link)) {
      overlayVertices.emplace_back(link.start.x, link.start.y + m_editorNavMeshDebugOffset + 0.035f, link.start.z, 1.0f);
      overlayVertices.emplace_back(link.end.x, link.end.y + m_editorNavMeshDebugOffset + 0.035f, link.end.z, 1.0f);
      overlayIndices.push_back(0u);
      overlayIndices.push_back(1u);
    }
    dumpLines("selected_authored_link_overlay", overlayVertices, overlayIndices);
  }

  out.close();
  T8_LOG_DEBUG("[T8ditor][NavMeshDump] Wrote '%s'", dumpPath.string().c_str());
}

void EditorApp::RefreshEditorNavMeshNodes() {
  m_editorNavMeshNodes.clear();
  if (m_editorNavMesh.IsReady()) {
    m_editorNavMesh.GetDebugNodePositions(m_editorNavMeshNodes, 0.0f);
  }
  for (t850::scene::SceneNavMeshLinkDesc& link : m_editorNavMeshLinks) {
    if (link.start_node >= 0 && link.start_node < static_cast<int>(m_editorNavMeshNodes.size())) {
      const XVECTOR3& node = m_editorNavMeshNodes[static_cast<std::size_t>(link.start_node)];
      link.start = { node.x, node.y, node.z };
    }
    if (link.end_node >= 0 && link.end_node < static_cast<int>(m_editorNavMeshNodes.size())) {
      const XVECTOR3& node = m_editorNavMeshNodes[static_cast<std::size_t>(link.end_node)];
      link.end = { node.x, node.y, node.z };
    }
  }
}

bool EditorApp::PickEditorNavMeshNodeFromMouse(int mouseX, int mouseY, int& outNodeIndex, XVECTOR3& outNodePosition) const {
  outNodeIndex = -1;
  if (m_editorNavMeshNodes.empty() || !m_sceneProps.GetPrimaryCamera()) {
    return false;
  }

  const Camera& cam = *m_sceneProps.GetPrimaryCamera();
  float bestDistSq = 24.0f * 24.0f;
  for (int i = 0; i < static_cast<int>(m_editorNavMeshNodes.size()); ++i) {
    const ImVec2 screen = WorldToScreen(m_editorNavMeshNodes[static_cast<std::size_t>(i)], cam.VP, m_lastW, m_lastH);
    if (screen.x < -1000.0f || screen.y < -1000.0f) {
      continue;
    }
    const float dx = screen.x - static_cast<float>(mouseX);
    const float dy = screen.y - static_cast<float>(mouseY);
    const float distSq = dx * dx + dy * dy;
    if (distSq < bestDistSq) {
      bestDistSq = distSq;
      outNodeIndex = i;
      outNodePosition = m_editorNavMeshNodes[static_cast<std::size_t>(i)];
    }
  }
  return outNodeIndex >= 0;
}

void EditorApp::DrawSelectedNavLinkOverlay(t850::Texture* depthTexture, t850::Texture* secondaryDepthTexture, const Camera& cam) {
  if (m_editorSelectedNavLink < 0 ||
      m_editorSelectedNavLink >= static_cast<int>(m_editorNavMeshLinks.size()) ||
      !m_navLinkOverlayLines.IsReady()) {
    return;
  }
  const t850::scene::SceneNavMeshLinkDesc& link = m_editorNavMeshLinks[static_cast<std::size_t>(m_editorSelectedNavLink)];
  if (!link.visible ||
      link.frozen ||
      !link.show_wire ||
      link.start_node < 0 ||
      link.end_node < 0 ||
      link.start_node >= static_cast<int>(m_editorNavMeshNodes.size()) ||
      link.end_node >= static_cast<int>(m_editorNavMeshNodes.size()) ||
      !IsUsableAuthoredNavLink(link)) {
    return;
  }

  const float positions[] = {
      link.start.x, link.start.y + m_editorNavMeshDebugOffset + 0.035f, link.start.z, 1.0f,
      link.end.x,   link.end.y   + m_editorNavMeshDebugOffset + 0.035f, link.end.z,   1.0f,
  };
  const unsigned int indices[] = { 0u, 1u };
  if (!m_editorNavLinkOverlayVB) {
    m_editorNavLinkOverlayVB = EditorLineRenderer::CreatePositionVB(
        positions,
        2,
        t850::BufferUsage::DINAMIC);
  } else {
    m_editorNavLinkOverlayVB->UpdateFromBuffer(*t850::T8DeviceContext, const_cast<float*>(positions));
  }
  if (!m_editorNavLinkOverlayIB) {
    m_editorNavLinkOverlayIB = EditorLineRenderer::CreateIndexBuffer32(indices, 2);
  }
  if (!m_editorNavLinkOverlayVB || !m_editorNavLinkOverlayIB) {
    return;
  }

  m_navLinkOverlayLines.SetDepthTexture(depthTexture);
  m_navLinkOverlayLines.SetSecondaryDepthTexture(secondaryDepthTexture);
  m_navLinkOverlayLines.SetViewport(m_lastW, m_lastH);
  m_navLinkOverlayLines.SetFarPlane(cam.FPlane);
  XMATRIX44 identity;
  XMatIdentity(identity);
  if (pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::NONE);
  }
  m_navLinkOverlayLines.DrawLines(identity,
                                  cam.VP,
                                  XVECTOR3(1.0f, 0.45f, 0.0f, 1.0f),
                                  m_editorNavLinkOverlayVB,
                                  m_editorNavLinkOverlayIB,
                                  2,
                                  sizeof(float) * 4,
                                  t850::IndexBufferFormat::R32);
  if (pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
  }
}

void EditorApp::UpdateEditorSplinePreview(float deltaSeconds) {
  if (!m_editorSplinePreviewPlaying ||
      m_editorSplinePreviewIndex < 0 ||
      m_editorSplinePreviewIndex >= static_cast<int>(g_splines.size())) {
    m_editorSplinePreviewPlaying = false;
    m_editorSplinePreviewIndex = -1;
    m_editorTimelineTimeSec = 0.0f;
    return;
  }

  t850::scene::SceneSplineDesc& desc = g_splines[static_cast<std::size_t>(m_editorSplinePreviewIndex)];
  if (m_editorSplinePreviewAgent.m_pSpline != &m_editorSplinePreviewSpline ||
      m_editorSplinePreviewSpline.m_totalLength <= 0.0f) {
    if (!BuildRuntimeSpline(desc, m_editorSplinePreviewSpline)) {
      m_editorSplinePreviewPlaying = false;
      return;
    }
    if (m_editorTimelineDurationSec <= 0.0f) {
      m_editorTimelineDurationSec = EstimateEditorSplineDurationSeconds(desc);
    }
    m_editorSplinePreviewAgent = t850::SplineAgent{};
    m_editorSplinePreviewAgent.m_pSpline = &m_editorSplinePreviewSpline;
    m_editorSplinePreviewAgent.m_moving = true;
    m_editorSplinePreviewAgent.m_velocity = desc.agent_velocity;
    m_editorSplinePreviewAgent.SetOffset(std::fmod((std::max)(0.0f, desc.agent_offset), m_editorSplinePreviewSpline.m_totalLength));
    m_editorSplinePreviewAgent.m_actualPoint =
        m_editorSplinePreviewSpline.GetPoint(m_editorSplinePreviewSpline.GetNormalizedOffset(m_editorSplinePreviewAgent.GetOffset()));
  }
  if (m_editorSplinePreviewSpline.m_totalLength <= 0.0f) {
    m_editorSplinePreviewPlaying = false;
    return;
  }

  m_editorSplinePreviewAgent.Update(deltaSeconds);
  m_editorTimelineTimeSec += deltaSeconds;
  ApplyEditorCameraAnimationsAtTime(m_editorTimelineTimeSec);
  desc.agent_offset = m_editorSplinePreviewAgent.GetOffset();
  ApplyEditorSplineAgentToAttachedCamera(desc, m_editorSplinePreviewAgent);
  if (m_editorSplinePreviewAgent.FinishedJourneyThisUpdate()) {
    if (m_editorTimelineLoop) {
      if (m_editorTimelineDurationSec > 0.0f) {
        m_editorTimelineTimeSec = std::fmod(m_editorTimelineTimeSec, m_editorTimelineDurationSec);
      }
    } else {
      m_editorSplinePreviewPlaying = false;
      m_editorTimelineTimeSec = m_editorTimelineDurationSec;
    }
  }
}

void EditorApp::DrawNavMeshAuthoringPanel() {
  ImGui::PushID("NavMeshAuthoringPanel");
  t850::navigation::NavMeshBuildSettings& settings = m_editorNavMeshBuildSettings;
  ImGui::TextWrapped("Creates a scene-level Recast/Detour NavMesh from visible, included static render meshes. The saved scene controls whether SceneTemplate builds this NavMesh at Play time.");
  auto markNavMeshDirty = [&]() {
    m_editorNavMeshDirty = true;
    m_editorNavMeshAuthored = true;
    m_editorNavMeshStatus = "NavMesh parameters changed. Click Re-generate to update the preview.";
  };

  if (ImGui::Button("Create NavMesh")) {
    CreateEditorNavMesh();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Quake3Mock Defaults")) {
    settings = DefaultEditorNavMeshBuildSettings();
    markNavMeshDirty();
    m_editorNavMeshStatus = "NavMesh settings reset to Quake3Mock defaults. Click Re-generate.";
  }
  ImGui::SameLine();
  if (!m_editorNavMeshDirty) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Re-generate")) {
    CreateEditorNavMesh();
  }
  if (!m_editorNavMeshDirty) {
    ImGui::EndDisabled();
  }
  if (m_editorNavMeshAuthored) {
    ImGui::SameLine();
    if (ImGui::Button("Destroy NavMesh")) {
      DestroyEditorNavMesh();
    }
  }

  ImGui::Checkbox("Visible", &m_editorNavMeshVisible);
  ImGui::SameLine();
  ImGui::Checkbox("Frozen", &m_editorNavMeshFrozen);
  ImGui::SameLine();
  ImGui::Checkbox("Wireframe", &m_editorNavMeshShowWire);
  ImGui::Checkbox("NavMesh Authoring Mode", &m_editorNavMeshAuthoringMode);
  const char* runtimeModes[] = { "build_cached", "build", "baked_asset" };
  int runtimeModeIndex = 0;
  for (int i = 0; i < static_cast<int>(sizeof(runtimeModes) / sizeof(runtimeModes[0])); ++i) {
    if (m_editorNavMeshRuntimeMode == runtimeModes[i]) runtimeModeIndex = i;
  }
  if (ImGui::Combo("Runtime Mode", &runtimeModeIndex, runtimeModes, static_cast<int>(sizeof(runtimeModes) / sizeof(runtimeModes[0])))) {
    m_editorNavMeshRuntimeMode = runtimeModes[runtimeModeIndex];
    MarkEditorNavMeshDirty("NavMesh runtime mode changed. Save the scene to persist it.");
  }
  if (m_editorNavMeshRuntimeMode == "baked_asset") {
    if (InputTextString("Baked Asset", m_editorNavMeshBakedAsset)) {
      MarkEditorNavMeshDirty("NavMesh baked asset path changed. Save the scene to persist it.");
    }
    ImGui::TextDisabled("Baked asset loading/export is metadata-only for now; runtime falls back to build_cached.");
  }

  if (ImGui::SliderFloat("Debug Vertical Offset", &m_editorNavMeshDebugOffset, 0.0f, 0.25f, "%.3f")) {
    ReleaseNavMeshClassificationGizmos();
  }
  const char* debugShapeOptions[] = { "Geometry", "Nodes" };
  ImGui::Combo("Debug Shape", &m_editorNavMeshDebugShapeMode, debugShapeOptions, 2);
  if (ImGui::Checkbox("Source Preview Overlay", &m_editorNavMeshShowSourcePreview)) {
    InvalidateEditorNavMeshClassification();
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh Source Preview")) {
    InvalidateEditorNavMeshClassification();
    UpdateEditorNavMeshClassification();
  }

  if (m_editorNavMesh.IsReady()) {
    const t850::navigation::NavMeshBuildStats& stats = m_editorNavMesh.GetStats();
    ImGui::Text("Ready: sources=%d verts=%d tris=%d polys=%d offMesh=%d drop=%d jump=%d jumpPad=%d",
                m_editorNavMeshSourceStats.included,
                stats.vertexCount,
                stats.triangleCount,
                stats.polygonCount,
                stats.offMeshLinkCount,
                stats.dropLinkCount,
                stats.jumpLinkCount,
                stats.jumpPadLinkCount);
    ImGui::Text("Build time: %.2f ms", m_editorNavMeshLastBuildMs);
  } else if (m_editorNavMeshAuthored) {
    ImGui::TextDisabled("Authored NavMesh exists in the scene, but no runtime NavMesh is currently built.");
  }
  if (!m_editorNavMeshStatus.empty()) {
    ImGui::TextWrapped("%s", m_editorNavMeshStatus.c_str());
  }
  if (m_editorNavMeshClassificationReady) {
    ImGui::TextDisabled("Source preview: included=%d excluded volume=%d outside bounds=%d slope=%d",
                        m_editorNavMeshClassification.includedCount,
                        m_editorNavMeshClassification.excludedByVolumeCount,
                        m_editorNavMeshClassification.outsideIncludeVolumeCount,
                        m_editorNavMeshClassification.excludedBySlopeCount);
  }
  if (m_editorNavMeshAuthoringMode) {
    ImGui::TextColored(ImVec4(0.25f, 0.75f, 1.0f, 1.0f), "NavMesh Authoring Mode: click source preview triangles to select them.");
  }
  if (m_editorSelectedNavTriangle >= 0) {
    ImGui::Text("Selected Source Triangle: %d", m_editorSelectedNavTriangle);
    if (ImGui::Button("Exclude Selected Triangle")) {
      CreateNavVolumeFromSelectedTriangle("exclude");
    }
    ImGui::SameLine();
    if (ImGui::Button("Area Cost Selected Triangle")) {
      CreateNavVolumeFromSelectedTriangle("area_cost");
    }
  }
  if (m_editorNavMeshDirty) {
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.1f, 1.0f), "Preview is stale. Click Re-generate.");
  }

  if (m_editorNavMesh.IsReady() && m_editorNavMeshNodes.empty()) {
    RefreshEditorNavMeshNodes();
  }
  ImGui::SeparatorText("Authoring Volumes");
  ImGui::TextDisabled("Volumes are saved in .t8scene as NavMesh build helpers, not game objects.");
  auto makeDefaultVolume = [&](const char* type, const char* label) {
    t850::scene::SceneNavMeshVolumeDesc volume;
    volume.name = std::string(label) + " " + std::to_string(m_editorNavMeshVolumes.size() + 1);
    volume.type = type;
    volume.shape = "box";
    volume.enabled = true;
    volume.visible = true;
    volume.show_wire = true;
    if (std::strcmp(type, "include_bounds") == 0) {
      t850::AABB bounds;
      if (GetEditorNavMeshWorldAABB(bounds)) {
        const XVECTOR3 center = bounds.Center();
        const XVECTOR3 extents = bounds.Extents();
        volume.position = {center.x, center.y, center.z};
        volume.half_extents = {
            (std::max)(1.0f, extents.x * 0.55f),
            (std::max)(1.0f, extents.y * 0.55f),
            (std::max)(1.0f, extents.z * 0.55f)};
      } else {
        volume.half_extents = {32.0f, 16.0f, 32.0f};
      }
    } else if (std::strcmp(type, "area_cost") == 0) {
      volume.area = "mud";
      volume.cost = 3.0f;
    }
    m_editorNavMeshVolumes.push_back(volume);
    m_editorSelectedNavVolume = static_cast<int>(m_editorNavMeshVolumes.size()) - 1;
    m_editorSelectedNavLink = -1;
    g_selectionType = 4;
    g_selectedIdx = 0;
    ClearMixedSelection();
    MarkEditorNavMeshDirty("NavMesh volume added. Click Re-generate.");
  };
  if (ImGui::Button("Add Include Bounds")) makeDefaultVolume("include_bounds", "Include Bounds");
  ImGui::SameLine();
  if (ImGui::Button("Add Exclude Volume")) makeDefaultVolume("exclude", "Exclude Volume");
  if (ImGui::Button("Add Area Cost Volume")) makeDefaultVolume("area_cost", "Area Cost Volume");

  if (!m_editorNavMeshVolumes.empty()) {
    if (m_editorSelectedNavVolume >= static_cast<int>(m_editorNavMeshVolumes.size())) {
      m_editorSelectedNavVolume = -1;
    }
    const char* volumePreview = m_editorSelectedNavVolume >= 0
        ? m_editorNavMeshVolumes[static_cast<std::size_t>(m_editorSelectedNavVolume)].name.c_str()
        : "None";
    if (ImGui::BeginCombo("Selected Volume", volumePreview)) {
      for (int volumeIndex = 0; volumeIndex < static_cast<int>(m_editorNavMeshVolumes.size()); ++volumeIndex) {
        const bool selected = volumeIndex == m_editorSelectedNavVolume;
        if (ImGui::Selectable(m_editorNavMeshVolumes[static_cast<std::size_t>(volumeIndex)].name.c_str(), selected)) {
          m_editorSelectedNavVolume = volumeIndex;
          m_editorSelectedNavLink = -1;
          g_selectionType = 4;
          g_selectedIdx = 0;
          ClearMixedSelection();
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (m_editorSelectedNavVolume < 0) {
      ImGui::TextDisabled("Select or create a volume to edit it.");
    } else {
      t850::scene::SceneNavMeshVolumeDesc& volume = m_editorNavMeshVolumes[static_cast<std::size_t>(m_editorSelectedNavVolume)];
      bool volumeChanged = false;
      volumeChanged |= InputTextString("Volume Name", volume.name);
      const char* volumeTypes[] = { "include_bounds", "exclude", "area_cost" };
      int typeIndex = 1;
      for (int i = 0; i < 3; ++i) {
        if (volume.type == volumeTypes[i]) typeIndex = i;
      }
      if (ImGui::Combo("Volume Type", &typeIndex, volumeTypes, 3)) {
        volume.type = volumeTypes[typeIndex];
        if (volume.type == "area_cost" && volume.area == "walkable") {
          volume.area = "mud";
          volume.cost = (std::max)(volume.cost, 3.0f);
        }
        volumeChanged = true;
      }
      volumeChanged |= ImGui::Checkbox("Volume Enabled", &volume.enabled);
      volumeChanged |= ImGui::Checkbox("Volume Visible", &volume.visible);
      volumeChanged |= ImGui::Checkbox("Volume Frozen", &volume.frozen);
      volumeChanged |= ImGui::Checkbox("Volume Wireframe", &volume.show_wire);
      float volumePosition[3] = {volume.position.x, volume.position.y, volume.position.z};
      if (ImGui::DragFloat3("Volume Position", volumePosition, 0.1f, -100000.0f, 100000.0f, "%.3f")) {
        volume.position = {volumePosition[0], volumePosition[1], volumePosition[2]};
        volumeChanged = true;
      }
      float volumeRotation[3] = {volume.rotation.x, volume.rotation.y, volume.rotation.z};
      if (ImGui::DragFloat3("Volume Rotation", volumeRotation, 0.01f, -1000.0f, 1000.0f, "%.3f")) {
        volume.rotation = {volumeRotation[0], volumeRotation[1], volumeRotation[2]};
        volumeChanged = true;
      }
      float volumeHalfExtents[3] = {volume.half_extents.x, volume.half_extents.y, volume.half_extents.z};
      if (ImGui::DragFloat3("Volume Half Extents", volumeHalfExtents, 0.1f, 0.001f, 100000.0f, "%.3f")) {
        volume.half_extents = {
            (std::max)(0.001f, volumeHalfExtents[0]),
            (std::max)(0.001f, volumeHalfExtents[1]),
            (std::max)(0.001f, volumeHalfExtents[2])};
        volumeChanged = true;
      }
      if (volume.type == "area_cost") {
        const char* areaOptions[] = { "walkable", "drop", "jump", "jump_pad", "jump_intent", "water", "door", "mud", "custom" };
        int areaIndex = 7;
        for (int i = 0; i < static_cast<int>(sizeof(areaOptions) / sizeof(areaOptions[0])); ++i) {
          if (volume.area == areaOptions[i]) areaIndex = i;
        }
        if (ImGui::Combo("Area", &areaIndex, areaOptions, static_cast<int>(sizeof(areaOptions) / sizeof(areaOptions[0])))) {
          volume.area = areaOptions[areaIndex];
          volumeChanged = true;
        }
        volumeChanged |= ImGui::DragFloat("Area Cost", &volume.cost, 0.05f, 0.01f, 100.0f, "%.2f");
      }
      if (ImGui::Button("Delete Volume")) {
        DeleteSelectedNavAuthoringChild();
      } else if (volumeChanged) {
        MarkEditorNavMeshDirty("NavMesh volume changed. Click Re-generate.");
      }
    }
  } else {
    ImGui::TextDisabled("No authored volumes.");
  }

  ImGui::SeparatorText("Authored Links");
  ImGui::TextDisabled("Links snap to Detour polygon-center nodes. Use Pick Start/Pick End, then click the viewport.");
  if (ImGui::Button("Refresh Nodes")) {
    RefreshEditorNavMeshNodes();
  }
  ImGui::SameLine();
  ImGui::Text("Nodes: %d", static_cast<int>(m_editorNavMeshNodes.size()));

  auto makeDefaultLink = [&](const char* type, const char* label) {
    if (m_editorNavMeshNodes.empty()) {
      RefreshEditorNavMeshNodes();
    }
    if (m_editorNavMeshNodes.size() < 2) {
      m_editorNavMeshStatus = "Build the NavMesh first; at least two Detour nodes are required for an authored link.";
      return;
    }
    t850::scene::SceneNavMeshLinkDesc link;
    link.name = label;
    link.type = type;
    link.radius = type == std::string("jump_pad") ? settings.jumpLinkRadius : (type == std::string("drop") ? settings.dropLinkRadius : settings.jumpLinkRadius);
    link.bidirectional = false;
    link.cost = 1.0f;
    link.enabled = true;
    link.start_node = 0;
    link.end_node = 1;
    const XVECTOR3& start = m_editorNavMeshNodes[static_cast<std::size_t>(link.start_node)];
    const XVECTOR3& end = m_editorNavMeshNodes[static_cast<std::size_t>(link.end_node)];
    link.start = { start.x, start.y, start.z };
    link.end = { end.x, end.y, end.z };
    m_editorNavMeshLinks.push_back(link);
    m_editorSelectedNavLink = static_cast<int>(m_editorNavMeshLinks.size()) - 1;
    m_editorSelectedNavVolume = -1;
    markNavMeshDirty();
    m_editorNavMeshStatus = "Authored link added. Click Re-generate.";
  };

  if (ImGui::Button("Add Drop Link")) makeDefaultLink("drop", "Drop Link");
  ImGui::SameLine();
  if (ImGui::Button("Add Jump Link")) makeDefaultLink("jump", "Jump Link");
  if (ImGui::Button("Add Jump Intent")) makeDefaultLink("jump_intent", "Jump Intent Link");
  ImGui::SameLine();
  if (ImGui::Button("Add Jump Pad")) makeDefaultLink("jump_pad", "Jump Pad Link");

  if (!m_editorNavMeshLinks.empty()) {
    std::vector<const char*> linkLabels;
    linkLabels.reserve(m_editorNavMeshLinks.size());
    for (const auto& link : m_editorNavMeshLinks) {
      linkLabels.push_back(link.name.c_str());
    }
    m_editorSelectedNavLink = std::clamp(m_editorSelectedNavLink, 0, static_cast<int>(m_editorNavMeshLinks.size()) - 1);
    ImGui::Combo("Selected Link", &m_editorSelectedNavLink, linkLabels.data(), static_cast<int>(linkLabels.size()));

    t850::scene::SceneNavMeshLinkDesc& link = m_editorNavMeshLinks[static_cast<std::size_t>(m_editorSelectedNavLink)];
    bool linkChanged = false;
    linkChanged |= InputTextString("Name", link.name);
    const char* typeOptions[] = { "drop", "jump", "jump_intent", "jump_pad" };
    int typeIndex = 1;
    for (int i = 0; i < 4; ++i) {
      if (link.type == typeOptions[i]) typeIndex = i;
    }
    if (ImGui::Combo("Type", &typeIndex, typeOptions, 4)) {
      link.type = typeOptions[typeIndex];
      linkChanged = true;
    }
    linkChanged |= ImGui::Checkbox("Enabled", &link.enabled);
    linkChanged |= ImGui::Checkbox("Visible", &link.visible);
    linkChanged |= ImGui::Checkbox("Frozen", &link.frozen);
    linkChanged |= ImGui::Checkbox("Wireframe", &link.show_wire);
    linkChanged |= ImGui::Checkbox("Bidirectional", &link.bidirectional);
    linkChanged |= ImGui::DragFloat("Radius", &link.radius, 0.05f, 0.05f, 16.0f, "%.2f");
    linkChanged |= ImGui::DragFloat("Cost", &link.cost, 0.05f, 0.0f, 100.0f, "%.2f");

    int startNode = link.start_node;
    int endNode = link.end_node;
    if (ImGui::InputInt("From Node", &startNode)) {
      if (!m_editorNavMeshNodes.empty()) {
        startNode = std::clamp(startNode, 0, static_cast<int>(m_editorNavMeshNodes.size()) - 1);
        const XVECTOR3& node = m_editorNavMeshNodes[static_cast<std::size_t>(startNode)];
        link.start_node = startNode;
        link.start = { node.x, node.y, node.z };
        linkChanged = true;
      }
    }
    if (ImGui::InputInt("To Node", &endNode)) {
      if (!m_editorNavMeshNodes.empty()) {
        endNode = std::clamp(endNode, 0, static_cast<int>(m_editorNavMeshNodes.size()) - 1);
        const XVECTOR3& node = m_editorNavMeshNodes[static_cast<std::size_t>(endNode)];
        link.end_node = endNode;
        link.end = { node.x, node.y, node.z };
        linkChanged = true;
      }
    }
    ImGui::Text("From: node %d (%.2f, %.2f, %.2f)", link.start_node, link.start.x, link.start.y, link.start.z);
    ImGui::Text("To:   node %d (%.2f, %.2f, %.2f)", link.end_node, link.end.x, link.end.y, link.end.z);
    if (ImGui::Button("Pick Start Node")) {
      m_editorNavLinkPickMode = 1;
      g_selectionType = 4;
      g_selectedIdx = 0;
      DumpEditorNavMeshWireGeometry("begin_pick_start");
    }
    ImGui::SameLine();
    if (ImGui::Button("Pick End Node")) {
      m_editorNavLinkPickMode = 2;
      g_selectionType = 4;
      g_selectedIdx = 0;
      DumpEditorNavMeshWireGeometry("begin_pick_end");
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel Pick")) {
      m_editorNavLinkPickMode = 0;
    }
    if (m_editorNavLinkPickMode != 0) {
      ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.1f, 1.0f), "Click viewport to snap %s node.", m_editorNavLinkPickMode == 1 ? "start" : "end");
    }
    if (ImGui::Button("Delete Link")) {
      m_editorNavMeshLinks.erase(m_editorNavMeshLinks.begin() + m_editorSelectedNavLink);
      m_editorSelectedNavLink = m_editorNavMeshLinks.empty() ? -1 : std::clamp(m_editorSelectedNavLink, 0, static_cast<int>(m_editorNavMeshLinks.size()) - 1);
      markNavMeshDirty();
      m_editorNavMeshStatus = "Authored link deleted. Click Re-generate.";
    }
    if (linkChanged) {
      markNavMeshDirty();
    }
  } else {
    ImGui::TextDisabled("No authored links.");
  }

  ImGui::SeparatorText("Recast Build Settings");
  bool buildSettingsChanged = false;
  buildSettingsChanged |= ImGui::DragFloat("Cell Size", &settings.cellSize, 0.01f, 0.01f, 10.0f, "%.3f");
  buildSettingsChanged |= ImGui::DragFloat("Cell Height", &settings.cellHeight, 0.01f, 0.01f, 10.0f, "%.3f");
  buildSettingsChanged |= ImGui::DragFloat("Agent Height", &settings.agentHeight, 0.05f, 0.05f, 64.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Agent Radius", &settings.agentRadius, 0.01f, 0.01f, 32.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Agent Max Climb", &settings.agentMaxClimb, 0.01f, 0.0f, 64.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Agent Max Slope", &settings.agentMaxSlope, 0.5f, 0.0f, 89.0f, "%.1f");
  buildSettingsChanged |= ImGui::DragFloat("Region Min Size", &settings.regionMinSize, 0.25f, 0.0f, 256.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Region Merge Size", &settings.regionMergeSize, 0.25f, 0.0f, 256.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Edge Max Len", &settings.edgeMaxLen, 0.25f, 0.0f, 256.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Edge Max Error", &settings.edgeMaxError, 0.05f, 0.0f, 64.0f, "%.2f");
  buildSettingsChanged |= ImGui::SliderInt("Verts Per Poly", &settings.vertsPerPoly, 3, 12);
  buildSettingsChanged |= ImGui::DragFloat("Detail Sample Dist", &settings.detailSampleDist, 0.25f, 0.0f, 64.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Detail Max Error", &settings.detailSampleMaxError, 0.05f, 0.0f, 64.0f, "%.2f");

  float queryExtents[3] = { settings.queryExtents.x, settings.queryExtents.y, settings.queryExtents.z };
  if (ImGui::DragFloat3("Query Extents", queryExtents, 0.05f, 0.01f, 128.0f, "%.2f")) {
    settings.queryExtents = XVECTOR3(queryExtents[0], queryExtents[1], queryExtents[2], 0.0f);
    buildSettingsChanged = true;
  }

  ImGui::SeparatorText("Traversal Link Generation");
  ImGui::TextWrapped("Auto links are generated from exposed NavMesh polygon edges. Disable Auto Drop Links and Auto Jump Links for a pure connected-surface NavMesh. If static triangle physics bodies exist, drop/jump links are validated with a swept Jolt capsule against those triangle meshes.");
  buildSettingsChanged |= ImGui::Checkbox("Auto Drop Links", &settings.enableAutoDropLinks);
  buildSettingsChanged |= ImGui::DragFloat("Drop Min Height", &settings.dropLinkMinHeight, 0.05f, 0.0f, 128.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Drop Max Height", &settings.dropLinkMaxHeight, 0.05f, 0.0f, 256.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Drop Max Horizontal", &settings.dropLinkMaxHorizontalDistance, 0.05f, 0.0f, 128.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Drop Sample Spacing", &settings.dropLinkSampleSpacing, 0.05f, 0.01f, 64.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Drop Link Radius", &settings.dropLinkRadius, 0.05f, 0.01f, 64.0f, "%.2f");
  buildSettingsChanged |= ImGui::Checkbox("Auto Jump Links", &settings.enableAutoJumpLinks);
  buildSettingsChanged |= ImGui::DragFloat("Jump Max Horizontal", &settings.jumpLinkMaxHorizontalDistance, 0.05f, 0.0f, 256.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Jump Sample Spacing", &settings.jumpLinkSampleSpacing, 0.05f, 0.01f, 64.0f, "%.2f");
  buildSettingsChanged |= ImGui::DragFloat("Jump Link Radius", &settings.jumpLinkRadius, 0.05f, 0.01f, 64.0f, "%.2f");
  buildSettingsChanged |= ImGui::Checkbox("Hybrid Jump Intent Links", &settings.enableHybridJumpLinks);
  buildSettingsChanged |= ImGui::DragInt("Hybrid Max Links", &settings.hybridJumpMaxLinks, 1.0f, 0, 4096);
  unsigned long long validationKey = static_cast<unsigned long long>(settings.offMeshLinkValidationKey);
  if (ImGui::InputScalar("Off-Mesh Validation Key", ImGuiDataType_U64, &validationKey)) {
    settings.offMeshLinkValidationKey = static_cast<uint64_t>(validationKey);
    buildSettingsChanged = true;
  }
  if (buildSettingsChanged) {
    markNavMeshDirty();
  }
  ImGui::PopID();
}

void EditorApp::SyncSceneObjectTransforms() {
  bool attachmentSnapshotsValid =
      g_lastSceneObjectTransforms.size() == g_objects.size() &&
      g_lastSceneObjectTransformNames.size() == g_objects.size();
  if (attachmentSnapshotsValid) {
    for (std::size_t i = 0; i < g_objects.size(); ++i) {
      if (g_lastSceneObjectTransformNames[i] != g_objects[i].name) {
        attachmentSnapshotsValid = false;
        break;
      }
    }
  }
  if (!attachmentSnapshotsValid) {
    g_lastSceneObjectTransforms.clear();
    g_lastSceneObjectTransformNames.clear();
    g_lastSceneObjectTransforms.reserve(g_objects.size());
    g_lastSceneObjectTransformNames.reserve(g_objects.size());
    for (SceneObject& obj : g_objects) {
      g_lastSceneObjectTransforms.push_back(GetSceneObjectTransformState(obj));
      g_lastSceneObjectTransformNames.push_back(obj.name);
    }
  } else {
    for (int i = 0; i < static_cast<int>(g_objects.size()); ++i) {
      const TransformState current = GetSceneObjectTransformState(g_objects[static_cast<std::size_t>(i)]);
      const TransformState previous = g_lastSceneObjectTransforms[static_cast<std::size_t>(i)];
      if (!NearlyEqualTransform(previous, current)) {
        PropagateSceneObjectTransformToAttachedCharacters(m_physics, i, previous, current);
        g_lastSceneObjectTransforms[static_cast<std::size_t>(i)] = current;
      }
    }
  }

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
      meta.enabled = true;
      meta.asset = obj.ragdollResourcePath;
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
        meta.enabled = true;
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

SceneFile EditorApp::BuildEditorSceneSnapshot(const std::string& scenePath, bool captureImGuiLayout) {
  EnsureInferredGameEntities();
  SceneFile sf = g_hasLoadedSceneFile ? g_loadedSceneFile : SceneFile{};
  sf.editor.camera_target   = { m_camera.GetTarget().x, m_camera.GetTarget().y, m_camera.GetTarget().z };
  sf.editor.camera_yaw      = m_camera.GetYaw();
  sf.editor.camera_pitch    = m_camera.GetPitch();
  sf.editor.camera_distance = m_camera.GetDistance();
  sf.editor.show_skybox     = m_panels.showSkybox;
  sf.editor.show_wireframe  = m_panels.showWireframe;
  sf.editor.allow_custom_layout = ImGuiAllowCustomSceneLayout();
  if (sf.editor.allow_custom_layout) {
    if (captureImGuiLayout) {
      sf.editor.imgui_layout = ImGuiCaptureCurrentLayout();
    }
  } else {
    sf.editor.imgui_layout.clear();
  }

  sf.objects.clear();
  for (auto& obj : g_objects) {
    if (obj.transient) {
      continue;
    }
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
    od.show_orientation = obj.showOrientation;
    od.nav_agent_front_yaw_offset_deg = obj.navAgentFrontYawOffsetDeg;
    od.nav_agent_face_yaw_sign = obj.navAgentFaceYawSign;
    od.nav_agent_target_mode = obj.navAgentTargetMode;
    od.nav_agent_follow_distance = obj.navAgentFollowDistance;
    od.nav_agent_side_offset = obj.navAgentSideOffset;
    od.nav_agent_formation_depth_step = obj.navAgentFormationDepthStep;
    od.nav_agent_slot = obj.navAgentSlot;
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

  sf.game_entities = g_gameEntities;
  sf.splines = g_splines;
  sf.light_cameras = g_lightCameras;
  sf.camera_animations = g_cameraAnimations;
  if (GodRaysVolumeIsAuthored(g_godRaysVolume)) {
    sf.god_rays_volume = g_godRaysVolume;
  } else {
    sf.god_rays_volume.reset();
  }

  sf.physics_entities.clear();
  for (const PhysicsSceneEntity& entity : g_physicsEntities) {
    sf.physics_entities.push_back(PhysicsEntityToScene(entity));
  }
  if (m_editorNavMeshAuthored) {
    sf.navigation_mesh = BuildEditorNavMeshDesc();
  } else {
    sf.navigation_mesh.reset();
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
  UpsertEditorSceneProfile(sf.profiles);
  if (sf.collision.empty()) {
    sf.collision = ResolveSceneCollisionPath(sf, scenePath);
  }
  return sf;
}

SceneFile EditorApp::RefreshVirtualEditorScene(const std::string& scenePath) {
  SceneFile sf = BuildEditorSceneSnapshot(scenePath);
  g_loadedSceneFile = sf;
  g_hasLoadedSceneFile = true;
  g_sceneProfiles = sf.profiles;
  LoadEditorSceneProfiles();
  g_sceneCollisionResourcePath = sf.collision;
  g_sceneProfiles = sf.profiles;
  return sf;
}

bool EditorApp::SaveEditorSceneSnapshot(const std::string& path, bool updateLoadedScene) {
  SceneFile sf = BuildEditorSceneSnapshot(path, true);
  if (!SaveSceneToFile(sf, path)) {
    return false;
  }
  if (updateLoadedScene) {
    g_loadedSceneFile = sf;
    g_hasLoadedSceneFile = true;
    g_sceneCollisionResourcePath = sf.collision;
    g_sceneProfiles = sf.profiles;
  }
  return true;
}

EditorUndoState EditorApp::CaptureEditorUndoState(std::string* outKey) {
  EditorUndoState state;
  state.scene = BuildEditorSceneSnapshot({});
  state.groups = g_groups;
  state.multiSelect = g_multiSelect;
  state.activeGroupIdx = g_activeGroupIdx;
  state.selectionType = g_selectionType;
  state.selectedIdx = g_selectedIdx;
  state.selectedSplinePoint = g_selectedSplinePoint;
  state.selectedNavVolume = m_editorSelectedNavVolume;
  state.selectedNavLink = m_editorSelectedNavLink;
  state.activeCameraIdx = g_activeCameraIdx;
  if (outKey) {
    *outKey = EditorUndoStateKey(state);
  }
  return state;
}

void EditorApp::ApplyEditorUndoState(const EditorUndoState& state) {
  if (!pFramework || !pFramework->pVideoDriver) {
    return;
  }

  const bool previousApplying = g_applyingUndoState;
  g_applyingUndoState = true;
  struct UndoApplyingGuard {
    bool previous = false;
    ~UndoApplyingGuard() { g_applyingUndoState = previous; }
  } undoApplyingGuard{previousApplying};

  SceneFile sf = state.scene;
  pFramework->pVideoDriver->WaitForGPU();

  DestroyAllObjectRagdolls();
  DestroyAllPhysicsEntities(m_physics);
  ResetEditorNavMeshState(false);
  m_primMgr.DestroyPrimitives();
  g_objects.clear();
  InvalidateSceneObjectTransformSnapshots();
  g_cameras.clear();
  g_lights.clear();
  ReleaseCameraGizmoCaches(g_lightCameraGizmoCameras);
  g_lightCameras.clear();
  g_cameraAnimations.clear();
  ReleaseGizmoCache(g_godRaysVolumeGizmo);
  g_godRaysVolume = t850::scene::SceneGodRaysVolumeDesc{};
  g_splines.clear();
  ReleaseGizmoCaches(g_splineGizmos);
  g_selectedIdx = -1;
  g_selectedSplinePoint = -1;
  g_selectionType = 0;
  g_activeCameraIdx = -1;
  ClearMixedSelection();
  g_groups.clear();
  g_activeGroupIdx = -1;
  g_unloadedSceneObjects.clear();
  g_gameEntities.clear();
  g_meshCharacterAuthoringInitialized = false;
  g_meshCharacterAuthoringSourceIndex = -1;
  g_meshCharacterAuthoringTemplate.visual.reset();

  g_loadedSceneFile = sf;
  g_hasLoadedSceneFile = true;
  g_sceneCollisionResourcePath = ResolveSceneCollisionPath(sf, {});
  g_sceneProfiles = sf.profiles;
  LoadSceneCollisionClip(g_sceneCollisionResourcePath);

  m_primMgr.SetEngineContext(&t850::GetEngineContext());
  m_primMgr.Init();
  m_primMgr.SetVP(&m_vp);
  m_primMgr.SetSceneProps(&m_sceneProps);

  if (g_deferredReady) {
    for (int i = 0; i < 8; ++i) {
      g_quads[i].CreateInstance(m_primMgr.GetPrimitive(t850::PrimitiveManager::QUAD), &g_quadVP);
      g_quads[i].Update();
    }
    if (!pFramework->pVideoDriver->RTs.empty()) {
      auto* gbufferRT = pFramework->pVideoDriver->RTs[0];
      for (int j = 0; j < (int)gbufferRT->vColorTextures.size() && j < 4; ++j) {
        g_quads[0].SetTexture(gbufferRT->vColorTextures[j], j);
      }
      if (gbufferRT->vColorTextures.size() > 4) {
        g_quads[0].SetTexture(gbufferRT->vColorTextures[4], 9);
      }
      if (gbufferRT->pDepthTexture) {
        g_quads[0].SetTexture(gbufferRT->pDepthTexture, 4);
      }
    }
    if (g_dummyWhiteTex) {
      g_quads[0].SetTexture(g_dummyWhiteTex, 5);
    }
    if (g_dummyEnvMapIdx >= 0) {
      g_quads[0].SetEnvironmentMap(t850::g_pBaseDriver->GetTexture(g_dummyEnvMapIdx));
    }
    m_primMgr.SetSceneProps(&m_sceneProps);
  }

  for (const auto& od : sf.objects) {
    const std::string meshPath = od.mesh.empty() ? od.name : od.mesh;
    const std::size_t objectCountBeforeImport = g_objects.size();
    ImportMesh(meshPath);
    if (g_objects.size() > objectCountBeforeImport) {
      SceneObject& obj = g_objects.back();
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
      obj.visible = od.visible;
      obj.mobileVisible = od.mobile_visible;
      obj.frozen = od.frozen;
      obj.showWire = od.show_wire;
      obj.showOrientation = od.show_orientation;
      obj.navAgentFrontYawOffsetDeg = od.nav_agent_front_yaw_offset_deg;
      obj.navAgentFaceYawSign = od.nav_agent_face_yaw_sign;
      obj.navAgentTargetMode = od.nav_agent_target_mode.empty() ? "direct" : od.nav_agent_target_mode;
      obj.navAgentFollowDistance = od.nav_agent_follow_distance;
      obj.navAgentSideOffset = od.nav_agent_side_offset;
      obj.navAgentFormationDepthStep = od.nav_agent_formation_depth_step;
      obj.navAgentSlot = od.nav_agent_slot;
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
    }
  }

  for (const t850::scene::ScenePhysicsEntityDesc& entityDesc : sf.physics_entities) {
    RestorePhysicsEntityFromScene(m_physics, entityDesc);
  }
  g_gameEntities = sf.game_entities;
  g_splines = sf.splines;
  ReleaseCameraGizmoCaches(g_lightCameraGizmoCameras);
  g_lightCameras = sf.light_cameras;
  g_cameraAnimations = sf.camera_animations;
  g_godRaysVolume = sf.god_rays_volume.value_or(t850::scene::SceneGodRaysVolumeDesc{});
  if (sf.god_rays_volume) {
    g_godRaysVolume.authored = true;
    ClampGodRaysVolumeLightCamera(g_godRaysVolume);
  }
  EnsureInferredGameEntities();
  if (sf.navigation_mesh) {
    RestoreEditorNavMeshFromScene(*sf.navigation_mesh);
  }

  for (const SceneCameraDesc& cd : sf.cameras) {
    SceneCamera c;
    c.name = cd.name;
    c.type = static_cast<CameraType>(cd.type);
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

  for (const SceneLightDesc& ld : sf.lights) {
    SceneLight l;
    l.name = ld.name;
    l.type = static_cast<EditorLightType>(ld.type);
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

  m_panels.showSkybox = sf.editor.show_skybox;
  m_panels.showWireframe = sf.editor.show_wireframe;
  m_camera.SetTarget(XVECTOR3(sf.editor.camera_target.x,
                              sf.editor.camera_target.y,
                              sf.editor.camera_target.z));
  m_camera.SetOrbitState(sf.editor.camera_yaw,
                         sf.editor.camera_pitch,
                         sf.editor.camera_distance);
  LoadEditorSceneProfiles();
  ImGuiApplySceneLayout(sf.editor.allow_custom_layout, sf.editor.imgui_layout);
  SyncSceneObjectTransforms();

  g_groups = state.groups;
  for (SceneGroup& group : g_groups) {
    for (auto it = group.members.begin(); it != group.members.end();) {
      if (*it < 0 || *it >= static_cast<int>(g_objects.size())) {
        it = group.members.erase(it);
      } else {
        ++it;
      }
    }
  }
  g_activeGroupIdx = state.activeGroupIdx >= 0 &&
      state.activeGroupIdx < static_cast<int>(g_groups.size())
          ? state.activeGroupIdx
          : -1;
  ClearMixedSelection();
  for (int member : state.multiSelect) {
    if (member >= 0 && member < static_cast<int>(g_objects.size())) {
      AddMixedSelection(0, member);
    }
  }
  g_selectionType = state.selectionType;
  g_selectedIdx = state.selectedIdx;
  g_selectedSplinePoint = state.selectedSplinePoint;
  m_editorSelectedNavVolume = state.selectedNavVolume;
  m_editorSelectedNavLink = state.selectedNavLink;
  if ((g_selectionType == 0 && (g_selectedIdx < 0 || g_selectedIdx >= static_cast<int>(g_objects.size()))) ||
      (g_selectionType == 1 && (g_selectedIdx < 0 || g_selectedIdx >= static_cast<int>(g_cameras.size()))) ||
      (g_selectionType == 2 && (g_selectedIdx < 0 || g_selectedIdx >= static_cast<int>(g_lights.size()))) ||
      (g_selectionType == 3 && (g_selectedIdx < 0 || g_selectedIdx >= static_cast<int>(g_physicsEntities.size()))) ||
      (g_selectionType == 4 && !m_editorNavMeshAuthored) ||
      (g_selectionType == 5 && (g_selectedIdx < 0 || g_selectedIdx >= static_cast<int>(g_splines.size()))) ||
      (g_selectionType == 6 && (g_selectedIdx < 0 || g_selectedIdx >= static_cast<int>(g_lightCameras.size()))) ||
      (g_selectionType == 7 && (g_selectedIdx < 0 ||
                                g_selectedIdx >= static_cast<int>(g_splines.size()) ||
                                g_selectedSplinePoint < 0 ||
                                g_selectedSplinePoint >= static_cast<int>(g_splines[static_cast<std::size_t>(g_selectedIdx)].points.size()))) ||
      (g_selectionType == 8 && (g_selectedIdx != 0 ||
                                !GodRaysVolumeCanBeActive(g_godRaysVolume, g_renderGraph)))) {
    g_selectedIdx = -1;
    g_selectedSplinePoint = -1;
    g_selectionType = 0;
  }
  if (m_editorSelectedNavVolume < -1 || m_editorSelectedNavVolume >= static_cast<int>(m_editorNavMeshVolumes.size())) {
    m_editorSelectedNavVolume = -1;
  }
  if (m_editorSelectedNavLink < -1 || m_editorSelectedNavLink >= static_cast<int>(m_editorNavMeshLinks.size())) {
    m_editorSelectedNavLink = -1;
  }
  if (g_multiEntitySelect.empty() && (g_selectionType == 0 || g_selectionType == 3)) {
    AddMixedSelection(g_selectionType, g_selectedIdx);
  }
  g_activeCameraIdx = IsActiveCameraIndexValid(state.activeCameraIdx)
      ? state.activeCameraIdx
      : -1;
  ResetMainEditorFrameLimiter();
}

void EditorApp::PushEditorUndoState(const char* label,
                                    const EditorUndoState& before,
                                    const std::string& beforeKey,
                                    const EditorUndoState& after) {
  if (g_applyingUndoState) {
    return;
  }
  const std::string afterKey = EditorUndoStateKey(after);
  if (beforeKey == afterKey) {
    return;
  }
  g_undoStack.Push(std::make_unique<EditorSceneStateCommand>(
      label && *label ? label : "Editor Action",
      before,
      after,
      [this](const EditorUndoState& state) {
        ApplyEditorUndoState(state);
      }));
}

t850::RenderSkinnedMesh* EditorApp::GetSelectedSkinnedMesh() const {
  if (g_selectionType != 0 || g_selectedIdx < 0 || g_selectedIdx >= static_cast<int>(g_objects.size())) {
    return nullptr;
  }
  return g_objects[static_cast<std::size_t>(g_selectedIdx)].litInst.GetSkinnedMesh();
}

t850::SandboxProfileDesc EditorApp::BuildEditorSceneProfile() const {
  t850::SandboxProfileDesc profile;
  t850::ApplyProfileTarget(profile, t850::DefaultProfileTargetIndex());

  SetFloatOverride(profile.sliders, "exposure", m_sceneProps.Exposure);
  SetFloatOverride(profile.sliders, "bloom_factor", m_sceneProps.BloomFactor);
  SetFloatOverride(profile.sliders, "bloom_threshold", m_sceneProps.BloomThreshold);
  SetFloatOverride(profile.sliders, "light_radius_scale", m_sceneProps.LightRadiusScale);
  SetFloatOverride(profile.sliders, "light_intensity_scale", m_sceneProps.LightIntensityScale);
  SetFloatOverride(profile.sliders, "lightmap_intensity", m_sceneProps.LightmapIntensity);
  SetFloatOverride(profile.sliders, "tm_white_level", m_sceneProps.ToneMapWhiteLevel);
  SetFloatOverride(profile.sliders, "tm_adapt_tau", m_sceneProps.LuminanceTau);
  SetFloatOverride(profile.sliders, "shadow_map_resolution", m_sceneProps.ShadowMapResolution);
  SetFloatOverride(profile.sliders, "god_rays_resolution", m_sceneProps.GoodRaysResolution);
  SetFloatOverride(profile.sliders, "pcf_radius", m_sceneProps.PCFScale);
  SetFloatOverride(profile.sliders, "pcf_samples", m_sceneProps.PCFSamples);
  SetFloatOverride(profile.sliders, "ssao_kernel_size", static_cast<float>(m_sceneProps.SSAOKernel.KernelSize));
  SetFloatOverride(profile.sliders, "ssao_radius", m_sceneProps.SSAOKernel.Radius);
  SetFloatOverride(profile.sliders, "dof_aperture", m_sceneProps.Aperture);
  SetFloatOverride(profile.sliders, "dof_focal_length", m_sceneProps.FocalLength);
  SetFloatOverride(profile.sliders, "dof_focus_depth", m_sceneProps.FocusDepth);
  SetFloatOverride(profile.sliders, "dof_max_coc", m_sceneProps.MaxCoc);
  SetFloatOverride(profile.sliders, "dof_far_samples", m_sceneProps.DOF_Far_Samples_squared);
  SetFloatOverride(profile.sliders, "dof_near_samples", m_sceneProps.DOF_Near_Samples_squared);
  SetFloatOverride(profile.sliders, "parallax_low_samples", m_sceneProps.ParallaxLowSamples);
  SetFloatOverride(profile.sliders, "parallax_high_samples", m_sceneProps.ParallaxHighSamples);
  SetFloatOverride(profile.sliders, "parallax_height", m_sceneProps.ParallaxHeight);
  SetFloatOverride(profile.sliders, "parallax_shadow_min_layers", m_sceneProps.ParallaxShadowMinLayers);
  SetFloatOverride(profile.sliders, "parallax_shadow_max_layers", m_sceneProps.ParallaxShadowMaxLayers);
  SetFloatOverride(profile.sliders, "parallax_shadow_softness", m_sceneProps.ParallaxShadowSoftness);
  SetFloatOverride(profile.sliders, "parallax_shadow_strength", m_sceneProps.ParallaxShadowStrength);
  SetFloatOverride(profile.sliders, "light_volume_steps", m_sceneProps.LightVolumeSteps);
  SetFloatOverride(profile.sliders, "godrays_factor", m_sceneProps.GodRaysFactor);
  SetFloatOverride(profile.sliders, "active_lights", static_cast<float>(m_sceneProps.ActiveLights));
  GaussFilter* activeKernel =
      (m_editorActiveGaussSelection >= 0 &&
       m_editorActiveGaussSelection < static_cast<int>(m_sceneProps.pGaussKernels.size()))
          ? m_sceneProps.pGaussKernels[static_cast<std::size_t>(m_editorActiveGaussSelection)]
          : nullptr;
  if (activeKernel) {
    SetFloatOverride(profile.sliders, "gauss_kernel_radius", activeKernel->radius);
    SetFloatOverride(profile.sliders, "gauss_kernel_deviation", activeKernel->sigma);
  }
  if (Camera* camera = m_sceneProps.GetPrimaryCamera()) {
    SetFloatOverride(profile.sliders, "fov", Rad2Deg(camera->Fov));
  }
  SetFloatOverride(profile.sliders, "shadow_bias", m_sceneProps.ShadowBias);
  SetFloatOverride(profile.sliders, "shadow_min", m_sceneProps.ShadowMin);
  SetFloatOverride(profile.sliders, "env_factor", m_sceneProps.EnvFactor);
  SetFloatOverride(profile.sliders, "ibl_factor", m_sceneProps.IBLFactor);
  SetFloatOverride(profile.sliders, "ibl_mip_count", m_sceneProps.IBLMipCount);
  SetFloatOverride(profile.sliders, "ibl_diffuse_mip_level", m_sceneProps.IBLDiffuseMipLevel);
  SetFloatOverride(profile.sliders, "ibl_brdf_lut_enabled", m_sceneProps.IBLBRDFLUTEnabled);
  SetFloatOverride(profile.sliders, "material_emissive_intensity", m_sceneProps.MaterialEmissiveIntensity);
  SetFloatOverride(profile.sliders, "material_transmission_multiplier", m_sceneProps.MaterialTransmissionMultiplier);
  SetFloatOverride(profile.sliders, "material_refraction_strength", m_sceneProps.MaterialRefractionStrength);

  SetBoolOverride(profile.checkboxes, "shadow_toggle", m_sceneProps.ToogleShadow != 0);
  SetBoolOverride(profile.checkboxes, "ssao_toggle", m_sceneProps.ToogleSSAO != 0);
  SetBoolOverride(profile.checkboxes, "dof_toggle", m_sceneProps.ToogleDOF != 0);
  SetBoolOverride(profile.checkboxes, "dof_auto_focus", m_sceneProps.AutoFocus);
  SetBoolOverride(profile.checkboxes, "parallax_toggle", m_sceneProps.ToogleParallax != 0);
  SetBoolOverride(profile.checkboxes, "parallax_shadow_toggle", m_sceneProps.ToogleParallaxShadow != 0);
  SetBoolOverride(profile.checkboxes, "godrays_toggle", m_sceneProps.ToogleGodRays != 0);
  SetBoolOverride(profile.checkboxes, "show_wireframe", m_panels.showWireframe);
  SetBoolOverride(profile.checkboxes, "show_skeleton", m_editorShowSkeleton);
  SetBoolOverride(profile.checkboxes, "show_physics", m_editorShowPhysics);
  SetBoolOverride(profile.checkboxes, "show_light_volumes", m_editorShowLightVolumes);
  SetBoolOverride(profile.checkboxes, "debug_luminance", m_sceneProps.DebugLuminanceEnabled);

  SetIntOverride(profile.selectors, "debug_render_target", m_editorDebugRTSelection);
  SetIntOverride(profile.selectors, "cubemap", m_editorCurrentCubemapIndex);
  if (activeKernel) {
    SetIntOverride(profile.selectors, "gauss_kernel_sample_count", activeKernel->kernelSize);
  }
  SetIntOverride(profile.selectors, "active_gauss_kernel", m_editorActiveGaussSelection);
  SetIntOverride(profile.selectors, "luminance_mode", m_sceneProps.LuminanceMode);

  if (g_selectionType == 0 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_objects.size())) {
    const SceneObject& object = g_objects[static_cast<std::size_t>(g_selectedIdx)];
    if (t850::RenderSkinnedMesh* skinned = object.litInst.GetSkinnedMesh()) {
      if (skinned->HasSkinData()) {
        t850::SandboxAnimationOverrideDesc anim;
        anim.index = g_selectedIdx;
        anim.mesh = object.meshPath.empty() ? object.name : object.meshPath;
        anim.anim_speed = skinned->GetAnimSpeed();
        anim.anim_select = skinned->GetCurrentAnimSet();
        anim.anim_mode = skinned->GetKeyframeMode() ? 1 : 0;
        if (skinned->GetKeyframeMode()) {
          anim.current_keyframe = skinned->GetCurrentKeyframe();
        }
        profile.animations.push_back(anim);
      }
    }
  }

  if (!m_editorCurrentCubemapPath.empty()) {
    profile.cubemap_path = m_editorCurrentCubemapPath;
  }
  return profile;
}

void EditorApp::UpsertEditorSceneProfile(std::vector<t850::SandboxProfileDesc>& profiles) const {
  t850::SandboxProfileDesc profile = BuildEditorSceneProfile();
  auto sameTarget = [&](const t850::SandboxProfileDesc& existing) {
    return existing.name == profile.name &&
           existing.platform == profile.platform &&
           existing.architecture == profile.architecture &&
           existing.gpu_family == profile.gpu_family &&
           existing.gpu_name_contains == profile.gpu_name_contains &&
           existing.model.empty();
  };
  auto found = std::find_if(profiles.begin(), profiles.end(), sameTarget);
  if (found != profiles.end()) {
    *found = std::move(profile);
  } else {
    profiles.push_back(std::move(profile));
  }
}

void EditorApp::ApplyEditorSceneProfile(const t850::SandboxProfileDesc& profile) {
  if (profile.cubemap_path && !NormalizeEditorResourcePath(*profile.cubemap_path).empty()) {
    SetEditorCubemap(*profile.cubemap_path);
  }

  for (const t850::FloatOverrideDesc& value : profile.sliders) {
    if (value.name == "exposure") m_sceneProps.Exposure = value.value;
    else if (value.name == "bloom_factor") m_sceneProps.BloomFactor = value.value;
    else if (value.name == "bloom_threshold") m_sceneProps.BloomThreshold = value.value;
    else if (value.name == "light_radius_scale") m_sceneProps.LightRadiusScale = value.value;
    else if (value.name == "light_intensity_scale") m_sceneProps.LightIntensityScale = value.value;
    else if (value.name == "lightmap_intensity") m_sceneProps.LightmapIntensity = value.value;
    else if (value.name == "tm_white_level") m_sceneProps.ToneMapWhiteLevel = value.value;
    else if (value.name == "tm_adapt_tau") m_sceneProps.LuminanceTau = value.value;
    else if (value.name == "shadow_map_resolution") m_sceneProps.ShadowMapResolution = value.value;
    else if (value.name == "god_rays_resolution") m_sceneProps.GoodRaysResolution = value.value;
    else if (value.name == "pcf_radius") m_sceneProps.PCFScale = value.value;
    else if (value.name == "pcf_samples") m_sceneProps.PCFSamples = value.value;
    else if (value.name == "ssao_kernel_size") {
      m_sceneProps.SSAOKernel.KernelSize = static_cast<int>(value.value);
      m_sceneProps.SSAOKernel.Update();
    } else if (value.name == "ssao_radius") {
      m_sceneProps.SSAOKernel.Radius = value.value;
      m_sceneProps.SSAOKernel.Update();
    } else if (value.name == "dof_aperture") m_sceneProps.Aperture = value.value;
    else if (value.name == "dof_focal_length") m_sceneProps.FocalLength = value.value;
    else if (value.name == "dof_focus_depth") m_sceneProps.FocusDepth = value.value;
    else if (value.name == "dof_max_coc") m_sceneProps.MaxCoc = value.value;
    else if (value.name == "dof_far_samples") m_sceneProps.DOF_Far_Samples_squared = value.value;
    else if (value.name == "dof_near_samples") m_sceneProps.DOF_Near_Samples_squared = value.value;
    else if (value.name == "parallax_low_samples") m_sceneProps.ParallaxLowSamples = value.value;
    else if (value.name == "parallax_high_samples") m_sceneProps.ParallaxHighSamples = value.value;
    else if (value.name == "parallax_height") m_sceneProps.ParallaxHeight = value.value;
    else if (value.name == "parallax_shadow_min_layers") m_sceneProps.ParallaxShadowMinLayers = value.value;
    else if (value.name == "parallax_shadow_max_layers") m_sceneProps.ParallaxShadowMaxLayers = value.value;
    else if (value.name == "parallax_shadow_softness") m_sceneProps.ParallaxShadowSoftness = value.value;
    else if (value.name == "parallax_shadow_strength") m_sceneProps.ParallaxShadowStrength = value.value;
    else if (value.name == "light_volume_steps") m_sceneProps.LightVolumeSteps = value.value;
    else if (value.name == "godrays_factor") m_sceneProps.GodRaysFactor = value.value;
    else if (value.name == "active_lights") m_sceneProps.ActiveLights = (std::max)(0, static_cast<int>(std::round(value.value)));
    else if (value.name == "gauss_kernel_radius") {
      if (m_editorActiveGaussSelection >= 0 &&
          m_editorActiveGaussSelection < static_cast<int>(m_sceneProps.pGaussKernels.size())) {
        if (GaussFilter* kernel = m_sceneProps.pGaussKernels[static_cast<std::size_t>(m_editorActiveGaussSelection)]) {
          kernel->radius = value.value;
          kernel->Update();
        }
      }
    } else if (value.name == "gauss_kernel_deviation") {
      if (m_editorActiveGaussSelection >= 0 &&
          m_editorActiveGaussSelection < static_cast<int>(m_sceneProps.pGaussKernels.size())) {
        if (GaussFilter* kernel = m_sceneProps.pGaussKernels[static_cast<std::size_t>(m_editorActiveGaussSelection)]) {
          kernel->sigma = value.value;
          kernel->Update();
        }
      }
    } else if (value.name == "fov") {
      const float fovRad = Deg2Rad(std::clamp(value.value, 1.0f, 170.0f));
      m_camera.GetCameraMutable().SetFov(fovRad);
      for (SceneCamera& camera : g_cameras) {
        camera.fovDeg = std::clamp(value.value, 1.0f, 170.0f);
      }
      if (Camera* camera = m_sceneProps.GetPrimaryCamera()) {
        camera->SetFov(fovRad);
      }
    } else if (value.name == "shadow_bias") m_sceneProps.ShadowBias = value.value;
    else if (value.name == "shadow_min") m_sceneProps.ShadowMin = value.value;
    else if (value.name == "env_factor") m_sceneProps.EnvFactor = value.value;
    else if (value.name == "ibl_factor") m_sceneProps.IBLFactor = value.value;
    else if (value.name == "ibl_mip_count") m_sceneProps.IBLMipCount = (std::max)(0.0f, value.value);
    else if (value.name == "ibl_diffuse_mip_level") m_sceneProps.IBLDiffuseMipLevel = (std::max)(0.0f, value.value);
    else if (value.name == "ibl_brdf_lut_enabled") m_sceneProps.IBLBRDFLUTEnabled = value.value > 0.5f ? 1.0f : 0.0f;
    else if (value.name == "material_emissive_intensity") m_sceneProps.MaterialEmissiveIntensity = value.value;
    else if (value.name == "material_transmission_multiplier") m_sceneProps.MaterialTransmissionMultiplier = value.value;
    else if (value.name == "material_refraction_strength") m_sceneProps.MaterialRefractionStrength = value.value;
  }

  for (const t850::BoolOverrideDesc& value : profile.checkboxes) {
    if (value.name == "shadow_toggle") m_sceneProps.ToogleShadow = value.value ? 1 : 0;
    else if (value.name == "ssao_toggle") m_sceneProps.ToogleSSAO = value.value ? 1 : 0;
    else if (value.name == "dof_toggle") m_sceneProps.ToogleDOF = value.value ? 1 : 0;
    else if (value.name == "dof_auto_focus") m_sceneProps.AutoFocus = value.value;
    else if (value.name == "parallax_toggle") m_sceneProps.ToogleParallax = value.value ? 1 : 0;
    else if (value.name == "parallax_shadow_toggle") m_sceneProps.ToogleParallaxShadow = value.value ? 1 : 0;
    else if (value.name == "godrays_toggle") m_sceneProps.ToogleGodRays = value.value ? 1 : 0;
    else if (value.name == "show_wireframe") m_panels.showWireframe = value.value;
    else if (value.name == "show_skeleton") m_editorShowSkeleton = value.value;
    else if (value.name == "show_physics") m_editorShowPhysics = value.value;
    else if (value.name == "show_light_volumes") m_editorShowLightVolumes = value.value;
    else if (value.name == "debug_luminance") {
      m_sceneProps.DebugLuminanceEnabled = value.value;
      if (!value.value) m_sceneProps.DebugAdaptedLuminanceValid = false;
    }
  }

  if (const t850::IntOverrideDesc* activeGauss = FindEditorIntOverride(profile.selectors, "active_gauss_kernel")) {
    m_editorActiveGaussSelection = std::clamp(
        activeGauss->value,
        0,
        (std::max)(0, static_cast<int>(m_sceneProps.pGaussKernels.size()) - 1));
  }

  for (const t850::IntOverrideDesc& value : profile.selectors) {
    if (value.name == "active_gauss_kernel") {
      continue;
    } else if (value.name == "debug_render_target") {
      m_editorDebugRTSelection = value.value;
      g_debugRTTexture = nullptr;
    } else if (value.name == "cubemap") {
      const t850::SelectorDesc* cubemapDesc = FindEditorSelectorDesc(m_editorSceneSetup.descriptor.selectors, "cubemap");
      if (cubemapDesc && value.value >= 0 && value.value < static_cast<int>(cubemapDesc->options.size())) {
        SetEditorCubemap(EditorCubemapPathForSelectorIndex(*cubemapDesc, value.value));
      }
    } else if (value.name == "gauss_kernel_sample_count") {
      if (m_editorActiveGaussSelection >= 0 &&
          m_editorActiveGaussSelection < static_cast<int>(m_sceneProps.pGaussKernels.size())) {
        if (GaussFilter* kernel = m_sceneProps.pGaussKernels[static_cast<std::size_t>(m_editorActiveGaussSelection)]) {
          kernel->kernelSize = value.value;
          kernel->Update();
        }
      }
    } else if (value.name == "luminance_mode") {
      m_sceneProps.LuminanceMode = value.value;
    }
  }
}

void EditorApp::LoadEditorSceneProfiles() {
  const t850::SandboxProfileDesc* baseProfile = nullptr;
  const t850::SandboxProfileDesc* runtimeProfile = nullptr;
  int bestRuntimeScore = -1;
  for (const t850::SandboxProfileDesc& profile : g_sceneProfiles) {
    const bool modelSpecific = !profile.model.empty();
    if (modelSpecific) {
      continue;
    }
    const bool hasTarget = !profile.name.empty() ||
                           !profile.platform.empty() ||
                           !profile.architecture.empty() ||
                           !profile.gpu_family.empty() ||
                           !profile.gpu_name_contains.empty();
    if (!hasTarget) {
      baseProfile = &profile;
      continue;
    }
    const int score = t850::ScoreSceneProfileMatch(profile, {});
    if (score > bestRuntimeScore) {
      bestRuntimeScore = score;
      runtimeProfile = &profile;
    }
  }

  if (baseProfile) ApplyEditorSceneProfile(*baseProfile);
  if (runtimeProfile && runtimeProfile != baseProfile) ApplyEditorSceneProfile(*runtimeProfile);
  T8_LOG_INFO("[T8ditor] Applied embedded scene profiles: base=%d runtime=%d",
              baseProfile ? 1 : 0,
              runtimeProfile ? 1 : 0);
}

void EditorApp::DrawSelectedAnimationInspector(SceneObject& obj) {
  t850::RenderSkinnedMesh* skinned = obj.litInst.GetSkinnedMesh();
  if (!skinned || !skinned->HasSkinData()) {
    return;
  }

  ImGui::SeparatorText("Animation");
  const std::string label = "Selected mesh: " + (obj.meshPath.empty() ? obj.name : obj.meshPath);
  ImGui::TextWrapped("%s", label.c_str());
  const uint32_t entityId = obj.litInst.GetEntityId();
  ImGui::PushID(static_cast<int>(entityId));

  std::vector<std::string> options;
  const int numSets = skinned->GetNumAnimSets();
  options.reserve((std::max)(1, numSets));
  for (int i = 0; i < numSets; ++i) {
    if (skinned->xFile && !skinned->xFile->XMeshDataBase.empty()) {
      const auto& animations = skinned->xFile->XMeshDataBase[0]->Animation.Animations;
      if (i < static_cast<int>(animations.size()) && !animations[static_cast<std::size_t>(i)].Name.empty()) {
        options.push_back(animations[static_cast<std::size_t>(i)].Name);
        continue;
      }
    }
    options.push_back("Anim " + std::to_string(i));
  }
  if (options.empty()) {
    options.push_back("None");
  }

  const int currentMeshAnim = std::clamp(skinned->GetCurrentAnimSet(), 0, static_cast<int>(options.size()) - 1);
  if (m_editorAnimationInspectorEntityId != entityId) {
    m_editorAnimationInspectorEntityId = entityId;
    m_editorAnimationInspectorAnimSet = currentMeshAnim;
  }
  int selectedAnim = std::clamp(m_editorAnimationInspectorAnimSet, 0, static_cast<int>(options.size()) - 1);
  if (selectedAnim != currentMeshAnim && !ImGui::IsPopupOpen("Animation")) {
    selectedAnim = currentMeshAnim;
    m_editorAnimationInspectorAnimSet = currentMeshAnim;
  }
  if (ImGui::BeginCombo("Animation", options[static_cast<std::size_t>(selectedAnim)].c_str())) {
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
      const bool selected = i == selectedAnim;
      if (ImGui::Selectable(options[static_cast<std::size_t>(i)].c_str(), selected)) {
        m_editorAnimationInspectorAnimSet = i;
        int guard = skinned->GetNumAnimSets() + 1;
        while (skinned->GetCurrentAnimSet() != i && guard-- > 0) {
          skinned->NextAnimation();
        }
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  const char* modes[] = {"Interpolation", "Keyframe"};
  int animMode = skinned->GetKeyframeMode() ? 1 : 0;
  if (ImGui::Combo("Anim Mode", &animMode, modes, 2)) {
    skinned->SetKeyframeMode(animMode == 1);
    if (animMode == 1) {
      skinned->StepKeyframe(0);
    }
  }

  float animSpeed = skinned->GetAnimSpeed();
  if (ImGui::DragFloat("Anim Speed", &animSpeed, 0.05f, 0.0f, 2.0f, "%.3f")) {
    skinned->SetAnimSpeed(animSpeed);
  }

  if (skinned->GetKeyframeMode()) {
    int frame = skinned->GetCurrentKeyframe();
    const int maxFrame = (std::max)(0, skinned->GetTotalKeyframes() - 1);
    if (ImGui::SliderInt("Keyframe", &frame, 0, maxFrame)) {
      const int delta = frame - skinned->GetCurrentKeyframe();
      if (delta != 0) {
        skinned->StepKeyframe(delta);
      }
    }
  }

  if (ImGui::Button(skinned->IsPlaying() ? "Pause Animation" : "Resume Animation")) {
    if (skinned->IsPlaying()) skinned->PauseAnimation();
    else skinned->PlayAnimation();
  }
  ImGui::PopID();
}

void EditorApp::DrawEditorRenderingPanel() {
  if (m_editorSceneSetup.descriptor.name.empty()) {
    m_editorSceneSetup.Load("Scenes/Quake3Mock.json");
  }

  auto findSlider = [&](const char* name) -> const t850::SliderDesc* {
    for (const auto& desc : m_editorSceneSetup.descriptor.sliders)
      if (desc.name == name) return &desc;
    return nullptr;
  };
  auto findCheckbox = [&](const char* name) -> const t850::CheckboxDesc* {
    for (const auto& desc : m_editorSceneSetup.descriptor.checkboxes)
      if (desc.name == name) return &desc;
    return nullptr;
  };
  auto findSelector = [&](const char* name) -> const t850::SelectorDesc* {
    return FindEditorSelectorDesc(m_editorSceneSetup.descriptor.selectors, name);
  };
  auto activeKernel = [&]() -> GaussFilter* {
    if (m_editorActiveGaussSelection < 0 ||
        m_editorActiveGaussSelection >= static_cast<int>(m_sceneProps.pGaussKernels.size())) {
      return nullptr;
    }
    return m_sceneProps.pGaussKernels[static_cast<std::size_t>(m_editorActiveGaussSelection)];
  };
  auto debugTexture = [&](const char* rtName, int attachment) -> t850::Texture* {
    if (!pFramework || !pFramework->pVideoDriver || !rtName) return nullptr;
    const int rtHandle = g_renderGraph.GetRTHandle(rtName);
    if (rtHandle < 0 || rtHandle >= static_cast<int>(pFramework->pVideoDriver->RTs.size())) return nullptr;
    t850::BaseRT* rt = pFramework->pVideoDriver->RTs[rtHandle];
    if (!rt) return nullptr;
    if (attachment == t850::BaseDriver::DEPTH_ATTACHMENT) return rt->pDepthTexture;
    int colorIndex = 0;
    int mask = attachment;
    while (mask > 1) {
      mask >>= 1;
      ++colorIndex;
    }
    return colorIndex >= 0 && colorIndex < static_cast<int>(rt->vColorTextures.size())
        ? rt->vColorTextures[colorIndex]
        : nullptr;
  };
  auto applyDebugSelection = [&]() {
    g_debugRT = -1;
    g_debugRTTexture = nullptr;
    switch (m_editorDebugRTSelection) {
    case 1:  g_debugRTTexture = debugTexture("GBuffer", t850::BaseDriver::COLOR0_ATTACHMENT); break;
    case 2:  g_debugRTTexture = debugTexture("GBuffer", t850::BaseDriver::COLOR1_ATTACHMENT); break;
    case 3:  g_debugRTTexture = debugTexture("GBuffer", t850::BaseDriver::COLOR2_ATTACHMENT); break;
    case 4:  g_debugRTTexture = debugTexture("GBuffer", t850::BaseDriver::COLOR3_ATTACHMENT); break;
    case 5:  g_debugRTTexture = debugTexture("GBuffer", t850::BaseDriver::DEPTH_ATTACHMENT); break;
    case 6:  g_debugRTTexture = debugTexture("DepthPass", t850::BaseDriver::DEPTH_ATTACHMENT); break;
    case 7:  g_debugRTTexture = debugTexture("ShadowAccum", t850::BaseDriver::COLOR0_ATTACHMENT); break;
    case 8:  g_debugRTTexture = debugTexture("Deferred", t850::BaseDriver::COLOR0_ATTACHMENT); break;
    case 9:  g_debugRTTexture = debugTexture("Extra16F", t850::BaseDriver::COLOR0_ATTACHMENT); break;
    case 10: g_debugRTTexture = debugTexture("ExtraHelper", t850::BaseDriver::COLOR0_ATTACHMENT); break;
    case 11: g_debugRTTexture = debugTexture("BloomAccum", t850::BaseDriver::COLOR0_ATTACHMENT); break;
    case 12: g_debugRTTexture = debugTexture("AdaptedLumCurrent", t850::BaseDriver::COLOR0_ATTACHMENT); break;
    default: break;
    }
  };
  applyDebugSelection();
  bool recreateRenderTargets = false;

  auto drawSlider = [&](const char* name) {
    const t850::SliderDesc* desc = findSlider(name);
    if (!desc) return;
    float value = desc->default_val;
    bool valid = true;
    GaussFilter* kernel = activeKernel();
    if (desc->name == "exposure") value = m_sceneProps.Exposure;
    else if (desc->name == "bloom_factor") value = m_sceneProps.BloomFactor;
    else if (desc->name == "bloom_threshold") value = m_sceneProps.BloomThreshold;
    else if (desc->name == "light_radius_scale") value = m_sceneProps.LightRadiusScale;
    else if (desc->name == "light_intensity_scale") value = m_sceneProps.LightIntensityScale;
    else if (desc->name == "lightmap_intensity") value = m_sceneProps.LightmapIntensity;
    else if (desc->name == "tm_white_level") value = m_sceneProps.ToneMapWhiteLevel;
    else if (desc->name == "tm_adapt_tau") value = m_sceneProps.LuminanceTau;
    else if (desc->name == "shadow_map_resolution") value = m_sceneProps.ShadowMapResolution;
    else if (desc->name == "god_rays_resolution") value = m_sceneProps.GoodRaysResolution;
    else if (desc->name == "pcf_radius") value = m_sceneProps.PCFScale;
    else if (desc->name == "pcf_samples") value = m_sceneProps.PCFSamples;
    else if (desc->name == "ssao_kernel_size") value = static_cast<float>(m_sceneProps.SSAOKernel.KernelSize);
    else if (desc->name == "ssao_radius") value = m_sceneProps.SSAOKernel.Radius;
    else if (desc->name == "dof_aperture") value = m_sceneProps.Aperture;
    else if (desc->name == "dof_focal_length") value = m_sceneProps.FocalLength;
    else if (desc->name == "dof_focus_depth") value = m_sceneProps.FocusDepth;
    else if (desc->name == "dof_max_coc") value = m_sceneProps.MaxCoc;
    else if (desc->name == "dof_far_samples") value = m_sceneProps.DOF_Far_Samples_squared;
    else if (desc->name == "dof_near_samples") value = m_sceneProps.DOF_Near_Samples_squared;
    else if (desc->name == "parallax_low_samples") value = m_sceneProps.ParallaxLowSamples;
    else if (desc->name == "parallax_high_samples") value = m_sceneProps.ParallaxHighSamples;
    else if (desc->name == "parallax_height") value = m_sceneProps.ParallaxHeight;
    else if (desc->name == "parallax_shadow_min_layers") value = m_sceneProps.ParallaxShadowMinLayers;
    else if (desc->name == "parallax_shadow_max_layers") value = m_sceneProps.ParallaxShadowMaxLayers;
    else if (desc->name == "parallax_shadow_softness") value = m_sceneProps.ParallaxShadowSoftness;
    else if (desc->name == "parallax_shadow_strength") value = m_sceneProps.ParallaxShadowStrength;
    else if (desc->name == "light_volume_steps") value = m_sceneProps.LightVolumeSteps;
    else if (desc->name == "godrays_factor") value = m_sceneProps.GodRaysFactor;
    else if (desc->name == "active_lights") value = static_cast<float>(m_sceneProps.ActiveLights);
    else if (desc->name == "gauss_kernel_radius") { if (kernel) value = kernel->radius; else valid = false; }
    else if (desc->name == "gauss_kernel_deviation") { if (kernel) value = kernel->sigma; else valid = false; }
    else if (desc->name == "fov") { if (Camera* cam = m_sceneProps.GetPrimaryCamera()) value = Rad2Deg(cam->Fov); else valid = false; }
    else if (desc->name == "shadow_bias") value = m_sceneProps.ShadowBias;
    else if (desc->name == "shadow_min") value = m_sceneProps.ShadowMin;
    else if (desc->name == "env_factor") value = m_sceneProps.EnvFactor;
    else if (desc->name == "ibl_factor") value = m_sceneProps.IBLFactor;
    else if (desc->name == "ibl_mip_count") value = m_sceneProps.IBLMipCount;
    else if (desc->name == "ibl_diffuse_mip_level") value = m_sceneProps.IBLDiffuseMipLevel;
    else if (desc->name == "ibl_brdf_lut_enabled") value = m_sceneProps.IBLBRDFLUTEnabled;
    else if (desc->name == "material_emissive_intensity") value = m_sceneProps.MaterialEmissiveIntensity;
    else if (desc->name == "material_transmission_multiplier") value = m_sceneProps.MaterialTransmissionMultiplier;
    else if (desc->name == "material_refraction_strength") value = m_sceneProps.MaterialRefractionStrength;
    else return;
    if (!valid) return;
    bool changed = false;
    if (desc->name == "ssao_kernel_size" || desc->name == "pcf_samples" ||
        desc->name == "shadow_map_resolution" || desc->name == "god_rays_resolution" ||
        desc->name == "dof_far_samples" || desc->name == "dof_near_samples" ||
        desc->name == "parallax_low_samples" || desc->name == "parallax_high_samples" ||
        desc->name == "light_volume_steps" || desc->name == "active_lights") {
      int intValue = static_cast<int>(std::round(value));
      changed = ImGui::SliderInt(desc->label.c_str(), &intValue, static_cast<int>(desc->min_val), static_cast<int>(desc->max_val));
      value = static_cast<float>(intValue);
    } else {
      const float range = desc->max_val - desc->min_val;
      const char* format = (std::abs(range) <= 0.001f || std::abs(desc->step) < 0.0001f) ? "%.7f" : "%.3f";
      changed = ImGui::SliderFloat(desc->label.c_str(), &value, desc->min_val, desc->max_val, format);
    }
    if (changed) {
      if (desc->name == "exposure") m_sceneProps.Exposure = value;
      else if (desc->name == "bloom_factor") m_sceneProps.BloomFactor = value;
      else if (desc->name == "bloom_threshold") m_sceneProps.BloomThreshold = value;
      else if (desc->name == "light_radius_scale") m_sceneProps.LightRadiusScale = value;
      else if (desc->name == "light_intensity_scale") m_sceneProps.LightIntensityScale = value;
      else if (desc->name == "lightmap_intensity") m_sceneProps.LightmapIntensity = value;
      else if (desc->name == "tm_white_level") m_sceneProps.ToneMapWhiteLevel = value;
      else if (desc->name == "tm_adapt_tau") m_sceneProps.LuminanceTau = value;
      else if (desc->name == "shadow_map_resolution") m_sceneProps.ShadowMapResolution = value;
      else if (desc->name == "god_rays_resolution") m_sceneProps.GoodRaysResolution = value;
      else if (desc->name == "pcf_radius") m_sceneProps.PCFScale = value;
      else if (desc->name == "pcf_samples") m_sceneProps.PCFSamples = value;
      else if (desc->name == "ssao_kernel_size") { m_sceneProps.SSAOKernel.KernelSize = (int)value; m_sceneProps.SSAOKernel.Update(); }
      else if (desc->name == "ssao_radius") { m_sceneProps.SSAOKernel.Radius = value; m_sceneProps.SSAOKernel.Update(); }
      else if (desc->name == "dof_aperture") m_sceneProps.Aperture = value;
      else if (desc->name == "dof_focal_length") m_sceneProps.FocalLength = value;
      else if (desc->name == "dof_focus_depth") m_sceneProps.FocusDepth = value;
      else if (desc->name == "dof_max_coc") m_sceneProps.MaxCoc = value;
      else if (desc->name == "dof_far_samples") m_sceneProps.DOF_Far_Samples_squared = value;
      else if (desc->name == "dof_near_samples") m_sceneProps.DOF_Near_Samples_squared = value;
      else if (desc->name == "parallax_low_samples") m_sceneProps.ParallaxLowSamples = value;
      else if (desc->name == "parallax_high_samples") m_sceneProps.ParallaxHighSamples = value;
      else if (desc->name == "parallax_height") m_sceneProps.ParallaxHeight = value;
      else if (desc->name == "parallax_shadow_min_layers") m_sceneProps.ParallaxShadowMinLayers = value;
      else if (desc->name == "parallax_shadow_max_layers") m_sceneProps.ParallaxShadowMaxLayers = value;
      else if (desc->name == "parallax_shadow_softness") m_sceneProps.ParallaxShadowSoftness = value;
      else if (desc->name == "parallax_shadow_strength") m_sceneProps.ParallaxShadowStrength = value;
      else if (desc->name == "light_volume_steps") m_sceneProps.LightVolumeSteps = value;
      else if (desc->name == "godrays_factor") m_sceneProps.GodRaysFactor = value;
      else if (desc->name == "active_lights") m_sceneProps.ActiveLights = (std::max)(0, static_cast<int>(std::round(value)));
      else if (desc->name == "gauss_kernel_radius" && kernel) { kernel->radius = value; kernel->Update(); }
      else if (desc->name == "gauss_kernel_deviation" && kernel) { kernel->sigma = value; kernel->Update(); }
      else if (desc->name == "fov") { if (Camera* cam = m_sceneProps.GetPrimaryCamera()) cam->SetFov(Deg2Rad(value)); }
      else if (desc->name == "shadow_bias") m_sceneProps.ShadowBias = value;
      else if (desc->name == "shadow_min") m_sceneProps.ShadowMin = value;
      else if (desc->name == "env_factor") m_sceneProps.EnvFactor = value;
      else if (desc->name == "ibl_factor") m_sceneProps.IBLFactor = value;
      else if (desc->name == "ibl_mip_count") m_sceneProps.IBLMipCount = value;
      else if (desc->name == "ibl_diffuse_mip_level") m_sceneProps.IBLDiffuseMipLevel = value;
      else if (desc->name == "ibl_brdf_lut_enabled") m_sceneProps.IBLBRDFLUTEnabled = value;
      else if (desc->name == "material_emissive_intensity") m_sceneProps.MaterialEmissiveIntensity = value;
      else if (desc->name == "material_transmission_multiplier") m_sceneProps.MaterialTransmissionMultiplier = value;
      else if (desc->name == "material_refraction_strength") m_sceneProps.MaterialRefractionStrength = value;
      if (desc->name == "shadow_map_resolution" || desc->name == "god_rays_resolution") {
        recreateRenderTargets = true;
      }
    }
  };

  auto drawCheckbox = [&](const char* name) {
    const t850::CheckboxDesc* desc = findCheckbox(name);
    if (!desc) return;
    bool value = desc->default_val;
    if (desc->name == "shadow_toggle") value = m_sceneProps.ToogleShadow != 0;
    else if (desc->name == "ssao_toggle") value = m_sceneProps.ToogleSSAO != 0;
    else if (desc->name == "dof_toggle") value = m_sceneProps.ToogleDOF != 0;
    else if (desc->name == "dof_auto_focus") value = m_sceneProps.AutoFocus;
    else if (desc->name == "parallax_toggle") value = m_sceneProps.ToogleParallax != 0;
    else if (desc->name == "parallax_shadow_toggle") value = m_sceneProps.ToogleParallaxShadow != 0;
    else if (desc->name == "godrays_toggle") value = m_sceneProps.ToogleGodRays != 0;
    else if (desc->name == "show_wireframe") value = m_panels.showWireframe;
    else if (desc->name == "show_skeleton") value = m_editorShowSkeleton;
    else if (desc->name == "show_physics") value = m_editorShowPhysics;
    else if (desc->name == "show_light_volumes") value = m_editorShowLightVolumes;
    else if (desc->name == "debug_luminance") value = m_sceneProps.DebugLuminanceEnabled;
    else return;
    if (ImGui::Checkbox(desc->label.c_str(), &value)) {
      if (desc->name == "shadow_toggle") m_sceneProps.ToogleShadow = value ? 1 : 0;
      else if (desc->name == "ssao_toggle") m_sceneProps.ToogleSSAO = value ? 1 : 0;
      else if (desc->name == "dof_toggle") {
        m_sceneProps.ToogleDOF = value ? 1 : 0;
        g_renderGraph.SetPassEnabled("CoC", value);
        g_renderGraph.SetPassEnabled("Combine CoC", value);
        g_renderGraph.SetPassEnabled("DOF", value);
        g_renderGraph.SetPassEnabled("DOF 2", value);
      }
      else if (desc->name == "dof_auto_focus") m_sceneProps.AutoFocus = value;
      else if (desc->name == "parallax_toggle") m_sceneProps.ToogleParallax = value ? 1 : 0;
      else if (desc->name == "parallax_shadow_toggle") m_sceneProps.ToogleParallaxShadow = value ? 1 : 0;
      else if (desc->name == "godrays_toggle") m_sceneProps.ToogleGodRays = value ? 1 : 0;
      else if (desc->name == "show_wireframe") m_panels.showWireframe = value;
      else if (desc->name == "show_skeleton") m_editorShowSkeleton = value;
      else if (desc->name == "show_physics") m_editorShowPhysics = value;
      else if (desc->name == "show_light_volumes") m_editorShowLightVolumes = value;
      else if (desc->name == "debug_luminance") {
        m_sceneProps.DebugLuminanceEnabled = value;
        if (!value) m_sceneProps.DebugAdaptedLuminanceValid = false;
      }
    }
  };

  auto drawSelector = [&](const char* name) {
    const t850::SelectorDesc* desc = findSelector(name);
    if (!desc) return;
    int selected = desc->default_index;
    if (desc->name == "debug_render_target") selected = m_editorDebugRTSelection;
    else if (desc->name == "cubemap") selected = m_editorCurrentCubemapIndex >= 0 ? m_editorCurrentCubemapIndex : desc->default_index;
    else if (desc->name == "gauss_kernel_sample_count") {
      if (GaussFilter* kernel = activeKernel()) {
        for (int i = 0; i < static_cast<int>(desc->options.size()); ++i)
          if (std::atoi(desc->options[static_cast<std::size_t>(i)].c_str()) == kernel->kernelSize) selected = i;
      }
    } else if (desc->name == "active_gauss_kernel") selected = m_editorActiveGaussSelection;
    else if (desc->name == "luminance_mode") selected = m_sceneProps.LuminanceMode;
    else return;
    selected = std::clamp(selected, 0, (std::max)(0, static_cast<int>(desc->options.size()) - 1));
    if (ImGui::BeginCombo(desc->label.c_str(), desc->options[static_cast<std::size_t>(selected)].c_str())) {
      for (int i = 0; i < static_cast<int>(desc->options.size()); ++i) {
        const bool isSelected = selected == i;
        if (ImGui::Selectable(desc->options[static_cast<std::size_t>(i)].c_str(), isSelected)) {
          selected = i;
          if (desc->name == "debug_render_target") { m_editorDebugRTSelection = selected; applyDebugSelection(); }
          else if (desc->name == "cubemap") SetEditorCubemap(EditorCubemapPathForSelectorIndex(*desc, selected));
          else if (desc->name == "gauss_kernel_sample_count") { if (GaussFilter* k = activeKernel()) { k->kernelSize = std::atoi(desc->options[static_cast<std::size_t>(selected)].c_str()); k->Update(); } }
          else if (desc->name == "active_gauss_kernel") m_editorActiveGaussSelection = selected;
          else if (desc->name == "luminance_mode") m_sceneProps.LuminanceMode = selected;
        }
        if (isSelected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  };

  ImGui::TextDisabled("Editor viewport uses the same deferred controls/order as Quake3Mock.");
  ImGui::SeparatorText("Rendering");
  drawSelector("cubemap");
  drawSelector("active_gauss_kernel");
  drawSelector("gauss_kernel_sample_count");
  drawSelector("luminance_mode");
  drawCheckbox("debug_luminance");
  drawSelector("debug_render_target");
  drawCheckbox("shadow_toggle");
  drawCheckbox("ssao_toggle");
  for (const auto& desc : m_editorSceneSetup.descriptor.sliders) {
    if (desc.name == "anim_speed") continue;
    drawSlider(desc.name.c_str());
  }

  auto intSlider = [](const char* label, float& value, int minValue, int maxValue) {
    int intValue = static_cast<int>(std::round(value));
    if (ImGui::SliderInt(label, &intValue, minValue, maxValue)) {
      value = static_cast<float>(intValue);
      return true;
    }
    return false;
  };

  ImGui::SeparatorText("Depth of Field");
  bool dofEnabled = m_sceneProps.ToogleDOF != 0;
  if (ImGui::Checkbox("Depth of Field", &dofEnabled)) {
    m_sceneProps.ToogleDOF = dofEnabled ? 1 : 0;
    g_renderGraph.SetPassEnabled("CoC", dofEnabled);
    g_renderGraph.SetPassEnabled("Combine CoC", dofEnabled);
    g_renderGraph.SetPassEnabled("DOF", dofEnabled);
    g_renderGraph.SetPassEnabled("DOF 2", dofEnabled);
  }
  ImGui::Checkbox("Auto Focus", &m_sceneProps.AutoFocus);
  ImGui::SliderFloat("Aperture", &m_sceneProps.Aperture, 0.0f, 200.0f, "%.3f");
  ImGui::SliderFloat("Focal Length", &m_sceneProps.FocalLength, 0.0f, 100.0f, "%.3f");
  ImGui::SliderFloat("Focus Depth", &m_sceneProps.FocusDepth, 0.0f, 1.0f, "%.6f");
  ImGui::SliderFloat("Max CoC", &m_sceneProps.MaxCoc, 0.0f, 10.0f, "%.3f");
  intSlider("Near Samples", m_sceneProps.DOF_Near_Samples_squared, 0, 16);
  intSlider("Far Samples", m_sceneProps.DOF_Far_Samples_squared, 0, 16);

  ImGui::SeparatorText("God Rays");
  const bool renderGraphHasGodRays = RenderGraphSupportsGodRays(g_renderGraph);
  const int firstEnabledGodRaysLightCamera = FirstEnabledGodRaysLightCamera();
  const bool hasEnabledGodRaysLightCamera = firstEnabledGodRaysLightCamera >= 0;
  bool godRaysEnabled = renderGraphHasGodRays && m_sceneProps.ToogleGodRays != 0;
  if (!renderGraphHasGodRays) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Checkbox("God Rays", &godRaysEnabled)) {
    m_sceneProps.ToogleGodRays = godRaysEnabled ? 1 : 0;
  }
  if (!renderGraphHasGodRays) {
    ImGui::EndDisabled();
    ImGui::TextDisabled("The active render graph has no God Rays pass.");
  }
  intSlider("Ray March Steps", m_sceneProps.LightVolumeSteps, 2, 512);
  ImGui::SliderFloat("God Rays Factor", &m_sceneProps.GodRaysFactor, 0.0f, 5.0f, "%.3f");
  if (intSlider("God Rays Resolution", m_sceneProps.GoodRaysResolution, 0, 4096)) {
    recreateRenderTargets = true;
  }
  const bool hasGodRaysVolume = GodRaysVolumeIsAuthored(g_godRaysVolume);
  const bool canCreateGodRaysVolume = renderGraphHasGodRays &&
                                      hasEnabledGodRaysLightCamera &&
                                      !hasGodRaysVolume;
  if (!canCreateGodRaysVolume) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Create God Rays Volume")) {
    g_godRaysVolume = t850::scene::SceneGodRaysVolumeDesc{};
    g_godRaysVolume.authored = true;
    g_godRaysVolume.enabled = true;
    g_godRaysVolume.visible = true;
    g_godRaysVolume.clip_enabled = true;
    g_godRaysVolume.show_wire = true;
    g_godRaysVolume.light_camera = firstEnabledGodRaysLightCamera;
    g_selectionType = 8;
    g_selectedIdx = 0;
    g_selectedSplinePoint = -1;
    ClearMixedSelection();
  }
  if (!canCreateGodRaysVolume) {
    ImGui::EndDisabled();
  }
  if (hasGodRaysVolume) {
    ClampGodRaysVolumeLightCamera(g_godRaysVolume);
    if (g_lightCameras.size() > 1) {
      const int selectedLightCamera = std::clamp(
          g_godRaysVolume.light_camera,
          0,
          static_cast<int>(g_lightCameras.size()) - 1);
      const std::string preview = g_lightCameras[static_cast<std::size_t>(selectedLightCamera)].name;
      if (ImGui::BeginCombo("Volume Light Camera", preview.c_str())) {
        for (int i = 0; i < static_cast<int>(g_lightCameras.size()); ++i) {
          const auto& lightCamera = g_lightCameras[static_cast<std::size_t>(i)];
          std::string label = lightCamera.name;
          if (!lightCamera.enabled) {
            label += " (disabled)";
          }
          const bool selected = i == g_godRaysVolume.light_camera;
          if (ImGui::Selectable(label.c_str(), selected)) {
            g_godRaysVolume.light_camera = i;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
    } else if (!g_lightCameras.empty()) {
      ImGui::TextDisabled("Volume Light Camera: %s", g_lightCameras.front().name.c_str());
      g_godRaysVolume.light_camera = 0;
    }
    const bool volumeCanBeActive = GodRaysVolumeCanBeActive(g_godRaysVolume, g_renderGraph);
    if (!volumeCanBeActive) {
      ImGui::BeginDisabled();
    }
    ImGui::Checkbox("Volume Enabled", &g_godRaysVolume.enabled);
    ImGui::Checkbox("Clip to authored volume", &g_godRaysVolume.clip_enabled);
    ImGui::Checkbox("Show Volume Cube", &g_godRaysVolume.visible);
    if (!volumeCanBeActive) {
      ImGui::EndDisabled();
    }
    ImGui::TextDisabled("Select Rendering Volumes > God Rays Volume to place and resize the volume.");
    if (!hasEnabledGodRaysLightCamera) {
      ImGui::TextDisabled("A God Rays volume requires an enabled light camera.");
    }
  } else if (renderGraphHasGodRays && !hasEnabledGodRaysLightCamera) {
    ImGui::TextDisabled("Create a light camera before creating a God Rays volume.");
  }

  ImGui::SeparatorText("Advanced Scene Rendering");
  int activeLights = m_sceneProps.ActiveLights;
  if (ImGui::SliderInt("Active Lights", &activeLights, 0, (std::max)(1, static_cast<int>(m_sceneProps.Lights.size())))) {
    m_sceneProps.ActiveLights = activeLights;
  }
  if (intSlider("Shadow Map Resolution", m_sceneProps.ShadowMapResolution, 256, 8192)) {
    recreateRenderTargets = true;
  }
  ImGui::SliderFloat("Shadow Bias", &m_sceneProps.ShadowBias, 0.0f, 0.01f, "%.7f");
  ImGui::SliderFloat("Shadow Min", &m_sceneProps.ShadowMin, 0.0f, 1.0f, "%.3f");
  ImGui::SliderFloat("Environment Factor", &m_sceneProps.EnvFactor, 0.0f, 5.0f, "%.3f");
  ImGui::SliderFloat("IBL Factor", &m_sceneProps.IBLFactor, 0.0f, 5.0f, "%.3f");
  ImGui::SliderFloat("IBL Max Mip", &m_sceneProps.IBLMipCount, 0.0f, 16.0f, "%.3f");
  ImGui::SliderFloat("Diffuse IBL Mip", &m_sceneProps.IBLDiffuseMipLevel, 0.0f, 16.0f, "%.3f");
  ImGui::SliderFloat("BRDF LUT Enabled", &m_sceneProps.IBLBRDFLUTEnabled, 0.0f, 1.0f, "%.0f");
  bool parallaxEnabled = m_sceneProps.ToogleParallax != 0;
  if (ImGui::Checkbox("Parallax", &parallaxEnabled)) m_sceneProps.ToogleParallax = parallaxEnabled ? 1 : 0;
  bool parallaxShadowEnabled = m_sceneProps.ToogleParallaxShadow != 0;
  if (ImGui::Checkbox("Parallax Self-Shadow", &parallaxShadowEnabled)) m_sceneProps.ToogleParallaxShadow = parallaxShadowEnabled ? 1 : 0;
  intSlider("Parallax Low Samples", m_sceneProps.ParallaxLowSamples, 0, 128);
  intSlider("Parallax High Samples", m_sceneProps.ParallaxHighSamples, 0, 128);
  ImGui::SliderFloat("Parallax Height", &m_sceneProps.ParallaxHeight, 0.0f, 0.2f, "%.5f");
  ImGui::SliderFloat("PX Shadow Min Layers", &m_sceneProps.ParallaxShadowMinLayers, 0.0f, 128.0f, "%.3f");
  ImGui::SliderFloat("PX Shadow Max Layers", &m_sceneProps.ParallaxShadowMaxLayers, 0.0f, 128.0f, "%.3f");
  ImGui::SliderFloat("PX Shadow Softness", &m_sceneProps.ParallaxShadowSoftness, 0.0f, 4.0f, "%.3f");
  ImGui::SliderFloat("PX Shadow Strength", &m_sceneProps.ParallaxShadowStrength, 0.0f, 4.0f, "%.3f");
  ImGui::SliderFloat("Material Emissive", &m_sceneProps.MaterialEmissiveIntensity, 0.0f, 10.0f, "%.3f");
  ImGui::SliderFloat("Transmission", &m_sceneProps.MaterialTransmissionMultiplier, 0.0f, 10.0f, "%.3f");
  ImGui::SliderFloat("Refraction", &m_sceneProps.MaterialRefractionStrength, 0.0f, 1.0f, "%.3f");
  if (recreateRenderTargets && pFramework && pFramework->pVideoDriver && g_deferredReady) {
    pFramework->pVideoDriver->WaitForGPU();
    pFramework->pVideoDriver->DestroyRTs();
    g_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, m_sceneProps);
    const bool dofOn = m_sceneProps.ToogleDOF != 0;
    g_renderGraph.SetPassEnabled("CoC", dofOn);
    g_renderGraph.SetPassEnabled("Combine CoC", dofOn);
    g_renderGraph.SetPassEnabled("DOF", dofOn);
    g_renderGraph.SetPassEnabled("DOF 2", dofOn);
    applyDebugSelection();
  }

  ImGui::SeparatorText("Debug Views");
  drawCheckbox("show_wireframe");
  drawCheckbox("show_skeleton");
  drawCheckbox("show_physics");
  drawCheckbox("show_light_volumes");

  ImGui::SeparatorText("Lights");
  ImGui::Checkbox("Camera headlamp fallback", &m_editorHeadlampEnabled);
  bool pointLights = m_sceneProps.PointLightsEnabled;
  if (ImGui::Checkbox("Dynamic point lights", &pointLights)) m_sceneProps.PointLightsEnabled = pointLights;
  bool lightVolumes = m_sceneProps.DeferredLightVolumesEnabled;
  if (ImGui::Checkbox("Deferred light volumes", &lightVolumes)) m_sceneProps.DeferredLightVolumesEnabled = lightVolumes;
  ImGui::TextDisabled("Lights: scene=%u packed=%u pointVolumes=%u activeTiles=%u",
                      m_sceneProps.DebugDeferredLightsSceneTotal,
                      m_sceneProps.DebugDeferredLightsPacked,
                      m_sceneProps.DebugDeferredLightsPointVolumes,
                      m_sceneProps.DebugDeferredLightActiveTiles);

  ImGui::SeparatorText("Culling");
  ImGui::Checkbox("Frustum culling", &m_sceneProps.FrustumCullingEnabled);
  ImGui::Checkbox("Culling stats and frustum", &m_sceneProps.ShowCullingDebug);
}

void EditorApp::SetEditorCubemap(const std::string& cubemapPath) {
  const std::string normalizedPath = NormalizeEditorResourcePath(cubemapPath);
  if (normalizedPath.empty() || !pFramework || !pFramework->pVideoDriver) {
    return;
  }
  if (g_dummyEnvMapIdx >= 0 &&
      m_pendingEditorCubemapPath.empty() &&
      EditorResourcePathEquals(normalizedPath, m_editorCurrentCubemapPath)) {
    return;
  }

  m_pendingEditorCubemapPath = normalizedPath;
  if (const t850::SelectorDesc* cubemapDesc =
          FindEditorSelectorDesc(m_editorSceneSetup.descriptor.selectors, "cubemap")) {
    m_editorCurrentCubemapIndex = EditorCubemapSelectorIndexForPath(*cubemapDesc, normalizedPath);
  }
  ResetMainEditorFrameLimiter();
  T8_LOG_INFO("[T8ditor] Queued editor cubemap change '%s'", normalizedPath.c_str());
}

void EditorApp::DrawEditorTimelinePanel() {
  int timelineSplineIndex = FindDefaultTimelineSplineIndex(m_editorSplinePreviewIndex);
  if (timelineSplineIndex != m_editorSplinePreviewIndex) {
    m_editorSplinePreviewIndex = timelineSplineIndex;
    m_editorTimelineTimeSec = 0.0f;
    m_editorTimelineDurationSec = timelineSplineIndex >= 0
        ? EstimateEditorSplineDurationSeconds(g_splines[static_cast<std::size_t>(timelineSplineIndex)])
        : 0.0f;
  }

  auto applyTimelineTime = [&](int splineIndex, float requestedTime) -> bool {
    if (splineIndex < 0 || splineIndex >= static_cast<int>(g_splines.size())) return false;
    t850::scene::SceneSplineDesc& splineDesc = g_splines[static_cast<std::size_t>(splineIndex)];
    if (splineDesc.attached_camera < 0 || splineDesc.attached_camera >= static_cast<int>(g_cameras.size())) return false;
    if (!BuildRuntimeSpline(splineDesc, m_editorSplinePreviewSpline)) return false;

    m_editorTimelineDurationSec = EstimateEditorSplineDurationSeconds(splineDesc);
    if (m_editorTimelineDurationSec > 0.0f) {
      requestedTime = m_editorTimelineLoop
          ? std::fmod((std::max)(0.0f, requestedTime), m_editorTimelineDurationSec)
          : std::clamp(requestedTime, 0.0f, m_editorTimelineDurationSec);
    } else {
      requestedTime = 0.0f;
    }

    m_editorSplinePreviewAgent = t850::SplineAgent{};
    m_editorSplinePreviewAgent.m_pSpline = &m_editorSplinePreviewSpline;
    m_editorSplinePreviewAgent.m_moving = true;
    m_editorSplinePreviewAgent.m_velocity = splineDesc.agent_velocity;
    m_editorSplinePreviewAgent.SetOffset(0.0f);
    m_editorSplinePreviewAgent.m_actualPoint =
        m_editorSplinePreviewSpline.GetPoint(m_editorSplinePreviewSpline.GetNormalizedOffset(0.0f));

    float remaining = requestedTime;
    constexpr float kStep = 1.0f / 60.0f;
    while (remaining > 0.000001f) {
      const float step = (std::min)(remaining, kStep);
      m_editorSplinePreviewAgent.Update(step);
      remaining -= step;
      if (!m_editorTimelineLoop && m_editorSplinePreviewAgent.FinishedJourneyThisUpdate()) {
        break;
      }
    }

    m_editorSplinePreviewIndex = splineIndex;
    m_editorTimelineTimeSec = requestedTime;
    ApplyEditorCameraAnimationsAtTime(m_editorTimelineTimeSec);
    splineDesc.agent_offset = m_editorSplinePreviewAgent.GetOffset();
    ApplyEditorSplineAgentToAttachedCamera(splineDesc, m_editorSplinePreviewAgent);
    return true;
  };

  ImGui::TextDisabled("Timeline drives authored time-based editor playback. It moves cameras but never changes the active view camera.");
  ImGui::Separator();

  if (g_splines.empty()) {
    ImGui::TextDisabled("No authored splines in this scene.");
    return;
  }

  std::vector<std::string> splineLabels;
  splineLabels.reserve(g_splines.size());
  for (int i = 0; i < static_cast<int>(g_splines.size()); ++i) {
    const t850::scene::SceneSplineDesc& spline = g_splines[static_cast<std::size_t>(i)];
    std::string label = std::to_string(i) + ": " + (spline.name.empty() ? "Spline" : spline.name);
    if (spline.attached_camera < 0 || spline.attached_camera >= static_cast<int>(g_cameras.size())) {
      label += " (no camera)";
    }
    splineLabels.push_back(std::move(label));
  }

  int selectedSpline = m_editorSplinePreviewIndex >= 0 ? m_editorSplinePreviewIndex : 0;
  selectedSpline = std::clamp(selectedSpline, 0, (std::max)(0, static_cast<int>(splineLabels.size()) - 1));
  if (ImGui::BeginCombo("Track", splineLabels[static_cast<std::size_t>(selectedSpline)].c_str())) {
    for (int i = 0; i < static_cast<int>(splineLabels.size()); ++i) {
      const bool selected = i == selectedSpline;
      if (ImGui::Selectable(splineLabels[static_cast<std::size_t>(i)].c_str(), selected)) {
        selectedSpline = i;
        m_editorSplinePreviewPlaying = false;
        applyTimelineTime(selectedSpline, 0.0f);
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  t850::scene::SceneSplineDesc& spline = g_splines[static_cast<std::size_t>(selectedSpline)];
  const bool playable = spline.attached_camera >= 0 &&
      spline.attached_camera < static_cast<int>(g_cameras.size()) &&
      spline.points.size() >= 4;
  if (!playable) {
    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                       "Selected spline needs at least 4 points and an attached camera.");
    return;
  }

  if (m_editorTimelineDurationSec <= 0.0f || selectedSpline != m_editorSplinePreviewIndex) {
    m_editorTimelineDurationSec = EstimateEditorSplineDurationSeconds(spline);
  }

  if (m_editorSplinePreviewPlaying) {
    if (ImGui::Button("Pause")) {
      m_editorSplinePreviewPlaying = false;
    }
  } else {
    if (ImGui::Button("Play")) {
      if (m_editorSplinePreviewIndex != selectedSpline ||
          m_editorSplinePreviewAgent.m_pSpline != &m_editorSplinePreviewSpline) {
        applyTimelineTime(selectedSpline, m_editorTimelineTimeSec);
      }
      m_editorSplinePreviewPlaying = true;
      m_editorSplinePreviewIndex = selectedSpline;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Stop")) {
    m_editorSplinePreviewPlaying = false;
    applyTimelineTime(selectedSpline, 0.0f);
  }
  ImGui::SameLine();
  if (ImGui::Button("End")) {
    m_editorSplinePreviewPlaying = false;
    applyTimelineTime(selectedSpline, m_editorTimelineDurationSec);
  }
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &m_editorTimelineLoop);
  if (!g_cameraAnimations.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("Camera animations: %d", static_cast<int>(g_cameraAnimations.size()));
  }

  const float duration = (std::max)(0.0f, m_editorTimelineDurationSec);
  float timeValue = std::clamp(m_editorTimelineTimeSec, 0.0f, duration);
  if (duration > 0.0f) {
    if (ImGui::SliderFloat("Time", &timeValue, 0.0f, duration, "%.3f s")) {
      m_editorSplinePreviewPlaying = false;
      applyTimelineTime(selectedSpline, timeValue);
    }
  } else {
    ImGui::SliderFloat("Time", &timeValue, 0.0f, 1.0f, "%.3f s");
  }

  const float offset = spline.agent_offset;
  const float length = m_editorSplinePreviewSpline.m_totalLength > 0.0f
      ? m_editorSplinePreviewSpline.m_totalLength
      : 0.0f;
  ImGui::Text("Camera: %d (%s)", spline.attached_camera, g_cameras[static_cast<std::size_t>(spline.attached_camera)].name.c_str());
  ImGui::Text("Time: %.3f / %.3f sec    Offset: %.3f / %.3f", m_editorTimelineTimeSec, duration, offset, length);
}

void EditorApp::ApplyPendingEditorCubemap() {
  if (m_pendingEditorCubemapPath.empty() || !pFramework || !pFramework->pVideoDriver) {
    return;
  }

  const std::string normalizedPath = m_pendingEditorCubemapPath;
  m_pendingEditorCubemapPath.clear();
  if (g_dummyEnvMapIdx >= 0 && EditorResourcePathEquals(normalizedPath, m_editorCurrentCubemapPath)) {
    return;
  }

  ResetMainEditorFrameLimiter();
  t850::BaseDriver* driver = pFramework->pVideoDriver;
  T8_LOG_INFO("[T8ditor] Applying editor cubemap '%s'", normalizedPath.c_str());
  driver->WaitForGPU();

  const int oldEnvMapIdx = g_dummyEnvMapIdx;
  const int newEnvMapIdx = driver->CreateTexture(normalizedPath);
  if (newEnvMapIdx < 0) {
    T8_LOG_ERROR("[T8ditor] Failed to load editor cubemap '%s'", normalizedPath.c_str());
    return;
  }

  g_dummyEnvMapIdx = newEnvMapIdx;
  if (oldEnvMapIdx >= 0 && oldEnvMapIdx != newEnvMapIdx) {
    driver->DestroyTexture(oldEnvMapIdx);
  }

  m_editorCurrentCubemapPath = normalizedPath;
  if (const t850::SelectorDesc* cubemapDesc =
          FindEditorSelectorDesc(m_editorSceneSetup.descriptor.selectors, "cubemap")) {
    m_editorCurrentCubemapIndex = EditorCubemapSelectorIndexForPath(*cubemapDesc, normalizedPath);
  }
  t850::EnvironmentMapSet editorEnvMaps;
  editorEnvMaps.SetFallback(g_dummyEnvMapIdx);
  t850::UpdateSceneIBLSettings(m_sceneProps, driver, editorEnvMaps);
  if (g_deferredReady && g_dummyEnvMapIdx >= 0) {
    g_quads[0].SetEnvironmentMap(driver->GetTexture(g_dummyEnvMapIdx));
  }
  ResetMainEditorFrameLimiter();
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
    m_editorCameraController.AttachCamera(&m_camera.GetCameraMutable());
    m_editorCameraController.SetActiveProfile(t850::CameraProfileType::FreeFly);
    m_lastW = w;
    m_lastH = h;
  }

  {
    LogEditorLoadingLabel("Initializing editor", "Viewport helpers");
    t850::LoadingProgress::ScopedStep helpersStep("Initializing editor", "Viewport helpers", 12.0f);
    if (!m_lines.Create())
      T8_LOG_ERROR("[T8ditor] EditorLineRenderer::Create failed");
    if (!m_navLinkOverlayLines.Create())
      T8_LOG_ERROR("[T8ditor] Nav link overlay line renderer failed to initialize");
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
      m_physicsDebug.SetDepthTestEnabled(true);
    }
    if (!m_editorNavMeshDebugRenderer.Create()) {
      T8_LOG_ERROR("[T8ditor] NavMesh debug renderer failed to initialize");
    }
    ResetEditorNavMeshState(false);
    DumpEditorNavMeshWireGeometry("startup");
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
      const bool dofOn = m_sceneProps.ToogleDOF != 0;
      g_renderGraph.SetPassEnabled("CoC", dofOn);
      g_renderGraph.SetPassEnabled("Combine CoC", dofOn);
      g_renderGraph.SetPassEnabled("DOF", dofOn);
      g_renderGraph.SetPassEnabled("DOF 2", dofOn);
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
      m_editorCurrentCubemapPath = "sky/CubeMap_SkyWater.dds";
      if (const t850::SelectorDesc* cubemapDesc =
              FindEditorSelectorDesc(m_editorSceneSetup.descriptor.selectors, "cubemap")) {
        m_editorCurrentCubemapIndex = EditorCubemapSelectorIndexForPath(*cubemapDesc, m_editorCurrentCubemapPath);
      }
      g_dummyEnvMapIdx = t850::g_pBaseDriver->CreateTexture(m_editorCurrentCubemapPath);
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
  if (!g_startupSceneFilePath.empty())
    g_pendingLoadPath = g_startupSceneFilePath;

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

static bool CloneCharacterPhysicsForMesh(t850::JoltPhysicsSystem& physics,
                                         const PhysicsSceneEntity& source,
                                         int newSourceObjectIndex,
                                         const std::string& newSourceName,
                                         std::string& outNewPhysicsName) {
  if (source.type != PhysicsSceneEntityType::Character ||
      newSourceObjectIndex < 0 ||
      newSourceObjectIndex >= static_cast<int>(g_objects.size())) {
    return false;
  }
  PhysicsSceneEntity clone;
  clone.type = PhysicsSceneEntityType::Character;
  clone.name = MakeUniquePhysicsEntityName(source.name + " Clone");
  clone.sourceName = newSourceName;
  clone.sourceObjectIndex = newSourceObjectIndex;
  clone.visible = source.visible;
  clone.frozen = source.frozen;
  clone.showWire = source.showWire;
  clone.showOrientation = source.showOrientation;
  clone.position = source.position;
  clone.eulerRadians = source.eulerRadians;
  clone.playerShape = source.playerShape;
  clone.playerHalfExtents = source.playerHalfExtents;
  clone.playerRadius = source.playerRadius;
  clone.playerHalfHeight = source.playerHalfHeight;
  clone.friction = source.friction;
  clone.restitution = source.restitution;
  clone.sensor = source.sensor;
  clone.playerBotRadius = source.playerBotRadius;
  clone.characterRuntimePath = source.characterRuntimePath;
  clone.characterImplementation = source.characterImplementation;
  clone.characterMass = source.characterMass;
  clone.characterMaxStrength = source.characterMaxStrength;
  clone.characterMaxSlopeAngleDeg = source.characterMaxSlopeAngleDeg;
  clone.characterEnhancedInternalEdgeRemoval = source.characterEnhancedInternalEdgeRemoval;
  clone.characterSupportingVolumeOffset = source.characterSupportingVolumeOffset;
  clone.characterShapeOffset[0] = source.characterShapeOffset[0];
  clone.characterShapeOffset[1] = source.characterShapeOffset[1];
  clone.characterShapeOffset[2] = source.characterShapeOffset[2];
  clone.characterBackFaceMode = source.characterBackFaceMode;
  clone.characterPredictiveContactDistance = source.characterPredictiveContactDistance;
  clone.characterMaxCollisionIterations = source.characterMaxCollisionIterations;
  clone.characterMaxConstraintIterations = source.characterMaxConstraintIterations;
  clone.characterMinTimeRemaining = source.characterMinTimeRemaining;
  clone.characterCollisionTolerance = source.characterCollisionTolerance;
  clone.characterPadding = source.characterPadding;
  clone.characterMaxNumHits = source.characterMaxNumHits;
  clone.characterHitReductionCosMaxAngle = source.characterHitReductionCosMaxAngle;
  clone.characterPenetrationRecoverySpeed = source.characterPenetrationRecoverySpeed;
  clone.characterGravityFactor = source.characterGravityFactor;
  clone.characterAllowTranslationX = source.characterAllowTranslationX;
  clone.characterAllowTranslationY = source.characterAllowTranslationY;
  clone.characterAllowTranslationZ = source.characterAllowTranslationZ;
  clone.characterInnerBody = source.characterInnerBody;
  if (!RecreateCharacterPhysicsBody(physics, clone)) {
    return false;
  }
  outNewPhysicsName = clone.name;
  g_physicsEntities.push_back(std::move(clone));
  return true;
}

void EditorApp::CloneSelected() {
  if (g_selectedIdx < 0) return;

  const std::vector<SelectionRef> cloneRefs = CurrentCloneSelection();
  if (cloneRefs.size() > 1) {
    std::set<int> sourceObjectIndices;
    for (const SelectionRef& ref : cloneRefs) {
      if (ref.type == 0) {
        sourceObjectIndices.insert(ref.index);
      } else if (ref.type == 3 && ref.index >= 0 && ref.index < static_cast<int>(g_physicsEntities.size())) {
        const PhysicsSceneEntity& entity = g_physicsEntities[static_cast<std::size_t>(ref.index)];
        const int sourceObjectIndex = (entity.sourceObjectIndex >= 0 &&
            entity.sourceObjectIndex < static_cast<int>(g_objects.size()))
                ? entity.sourceObjectIndex
                : FindSceneObjectIndexByName(entity.sourceName);
        if (sourceObjectIndex >= 0) {
          sourceObjectIndices.insert(sourceObjectIndex);
        }
      }
    }
    if (!sourceObjectIndices.empty()) {
      ClearMixedSelection();
      for (int sourceObjectIndex : sourceObjectIndices) {
        if (sourceObjectIndex >= 0 && sourceObjectIndex < static_cast<int>(g_objects.size())) {
          g_selectedIdx = sourceObjectIndex;
          g_selectionType = 0;
          CloneSelected();
        }
      }
      return;
    }
  }

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
    const int sourceObjectIndex = g_selectedIdx;
    SceneObject& src = g_objects[g_selectedIdx];
    const std::string meshPath = src.meshPath.empty() ? src.name : src.meshPath;
    const std::string sourceName = src.name;
    const std::string name = makeUniqueObjectName(sourceName);
    const std::string ragdollResourcePath = src.ragdollResourcePath;
    const bool visible = src.visible;
    const std::optional<bool> mobileVisible = src.mobileVisible;
    const bool frozen = src.frozen;
    const bool showWire = src.showWire;
    const bool showOrientation = src.showOrientation;
    const std::optional<float> navAgentFrontYawOffsetDeg = src.navAgentFrontYawOffsetDeg;
    const std::optional<float> navAgentFaceYawSign = src.navAgentFaceYawSign;
    const std::string navAgentTargetMode = src.navAgentTargetMode;
    const float navAgentFollowDistance = src.navAgentFollowDistance;
    const float navAgentSideOffset = src.navAgentSideOffset;
    const float navAgentFormationDepthStep = src.navAgentFormationDepthStep;
    const int navAgentSlot = src.navAgentSlot;
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

    int id = src.primId;
    if (id < 0 || !m_primMgr.GetPrimitive(id)) {
      id = m_primMgr.CreateMesh(meshPath.c_str());
      if (id < 0) {
        T8_LOG_ERROR("[T8ditor] Failed to clone mesh: %s", meshPath.c_str());
        return;
      }
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
    clone.showOrientation = showOrientation;
    clone.navAgentFrontYawOffsetDeg = navAgentFrontYawOffsetDeg;
    clone.navAgentFaceYawSign = navAgentFaceYawSign;
    clone.navAgentTargetMode = navAgentTargetMode;
    clone.navAgentFollowDistance = navAgentFollowDistance;
    clone.navAgentSideOffset = navAgentSideOffset;
    clone.navAgentFormationDepthStep = navAgentFormationDepthStep;
    clone.navAgentSlot = navAgentSlot;
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
    if (!clone.wireframe.CloneFrom(src.wireframe)) {
      clone.wireframe.Load(meshPath);
    }
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
    ClearMixedSelection();
    AddMixedSelection(0, g_selectedIdx);
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

    std::vector<int> relatedPhysicsIndices;
    const int originalPhysicsCount = static_cast<int>(g_physicsEntities.size());
    for (int physicsIndex = 0; physicsIndex < originalPhysicsCount; ++physicsIndex) {
      const PhysicsSceneEntity& physicsEntity = g_physicsEntities[static_cast<std::size_t>(physicsIndex)];
      if (physicsEntity.sourceObjectIndex == sourceObjectIndex || physicsEntity.sourceName == sourceName) {
        relatedPhysicsIndices.push_back(physicsIndex);
      }
    }

    std::unordered_map<std::string, std::string> clonedPhysicsNames;
    for (int physicsIndex : relatedPhysicsIndices) {
      if (physicsIndex >= 0 && physicsIndex < static_cast<int>(g_physicsEntities.size())) {
        const PhysicsSceneEntity& physicsEntity = g_physicsEntities[static_cast<std::size_t>(physicsIndex)];
        std::string newPhysicsName;
        if (CloneCharacterPhysicsForMesh(m_physics, physicsEntity, static_cast<int>(g_objects.size()) - 1, clone.name, newPhysicsName)) {
          clonedPhysicsNames[physicsEntity.name] = newPhysicsName;
        }
      }
    }

    for (const t850::scene::SceneGameEntityDesc& entity : g_gameEntities) {
      if (!GameEntityReferencesMesh(entity, sourceName) &&
          std::none_of(clonedPhysicsNames.begin(), clonedPhysicsNames.end(), [&](const auto& item) {
            return GameEntityReferencesPhysics(entity, item.first);
          })) {
        continue;
      }
      t850::scene::SceneGameEntityDesc cloneEntity = entity;
      cloneEntity.name = UniqueGameEntityName(entity.name + " Clone");
      if (cloneEntity.mesh_object == sourceName) {
        cloneEntity.mesh_object = clone.name;
      }
      if (cloneEntity.ragdoll_object == sourceName) {
        cloneEntity.ragdoll_object = clone.name;
      }
      if (auto found = clonedPhysicsNames.find(cloneEntity.primary_physics_entity); found != clonedPhysicsNames.end()) {
        cloneEntity.primary_physics_entity = found->second;
      }
      for (std::string& physicsName : cloneEntity.physics_entities) {
        if (auto found = clonedPhysicsNames.find(physicsName); found != clonedPhysicsNames.end()) {
          physicsName = found->second;
        }
      }
      g_gameEntities.push_back(std::move(cloneEntity));
      break;
    }

    T8_LOG_INFO("[T8ditor] Cloned mesh '%s' as '%s'", sourceName.c_str(), clone.name.c_str());
    return;
  }

  if (g_selectionType == 3 && g_selectedIdx < static_cast<int>(g_physicsEntities.size())) {
    const PhysicsSceneEntity& entity = g_physicsEntities[static_cast<std::size_t>(g_selectedIdx)];
    const int sourceObjectIndex = (entity.sourceObjectIndex >= 0 &&
        entity.sourceObjectIndex < static_cast<int>(g_objects.size()))
            ? entity.sourceObjectIndex
            : FindSceneObjectIndexByName(entity.sourceName);
    if (sourceObjectIndex >= 0) {
      g_selectedIdx = sourceObjectIndex;
      g_selectionType = 0;
      ClearMixedSelection();
      AddMixedSelection(0, sourceObjectIndex);
      CloneSelected();
      return;
    }
  }

  if (g_selectionType == 1 && g_selectedIdx < (int)g_cameras.size()) {
    SceneCamera clone = g_cameras[g_selectedIdx];
    clone.name = makeUniqueCameraName(clone.name);
    g_cameras.push_back(clone);
    g_selectedIdx = (int)g_cameras.size() - 1;
    g_selectionType = 1;
    ClearMixedSelection();
    T8_LOG_INFO("[T8ditor] Cloned camera '%s'", clone.name.c_str());
    return;
  }

  if (g_selectionType == 2 && g_selectedIdx < (int)g_lights.size()) {
    SceneLight clone = g_lights[g_selectedIdx];
    clone.name = makeUniqueLightName(clone.name);
    g_lights.push_back(clone);
    g_selectedIdx = (int)g_lights.size() - 1;
    g_selectionType = 2;
    ClearMixedSelection();
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
  DestroyAllPhysicsEntities(m_physics);
  DestroyEditorNavMesh();
  m_editorNavMeshDebugRenderer.Destroy();
  if (m_editorNavLinkOverlayVB) { m_editorNavLinkOverlayVB->release(); m_editorNavLinkOverlayVB = nullptr; }
  if (m_editorNavLinkOverlayIB) { m_editorNavLinkOverlayIB->release(); m_editorNavLinkOverlayIB = nullptr; }
  m_physicsDebug.Destroy();
  m_primMgr.DestroyPrimitives();
  g_objects.clear();
  InvalidateSceneObjectTransformSnapshots();
  g_cameras.clear();
  g_lights.clear();
  ReleaseCameraGizmoCaches(g_lightCameraGizmoCameras);
  g_lightCameras.clear();
  g_cameraAnimations.clear();
  ReleaseGizmoCache(g_godRaysVolumeGizmo);
  g_godRaysVolume = t850::scene::SceneGodRaysVolumeDesc{};
  g_splines.clear();
  ReleaseGizmoCaches(g_splineGizmos);
  g_selectedIdx = -1;
  g_selectedSplinePoint = -1;
  g_selectionType = 0;
  g_activeCameraIdx = -1;
  ClearMixedSelection();
  g_groups.clear();
  g_activeGroupIdx = -1;
  g_loadedSceneFile = SceneFile{};
  g_hasLoadedSceneFile = false;
  g_unloadedSceneObjects.clear();
  g_sceneCollisionResourcePath.clear();
  g_sceneProfiles.clear();
  g_gameEntities.clear();
  g_undoStack.Clear();
  if (g_skyboxReady) {
    g_skyboxMgr.DestroyPrimitives();
    g_skyboxPrimId = -1;
    g_skyboxReady = false;
  }
  t850::MeshAssetCache::Get().Clear();
  m_gizmo.Destroy();
  m_grid.Destroy();
  m_navLinkOverlayLines.Destroy();
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
    const auto now = std::chrono::steady_clock::now();
    if (w == m_lastFailedResizeW && h == m_lastFailedResizeH && now < m_nextResizeRetryTime) {
      return;
    }
    if (pFramework->pVideoDriver->ResizeSwapchain(w, h)) {
      m_lastFailedResizeW = 0;
      m_lastFailedResizeH = 0;
      m_nextResizeRetryTime = {};
      g_editorResizeInputTraceFrames = 12;
      ImGuiIO& io = ImGui::GetIO();
      ImGuiViewport* viewport = ImGui::GetMainViewport();
      int winX = 0, winY = 0, winW = 0, winH = 0;
      SDL_GetWindowPosition(w32->m_pWindow, &winX, &winY);
      SDL_GetWindowSize(w32->m_pWindow, &winW, &winH);
      T8_LOG_TRACE("[T8ditorResizeTrace] beforeApply oldPix=(%d,%d) newPix=(%d,%d) winPos=(%d,%d) winSize=(%d,%d) ioMouse=(%.1f,%.1f) ioDisplay=(%.1f,%.1f) viewportPos=(%.1f,%.1f) viewportSize=(%.1f,%.1f) iMouse=(%d,%d)",
                  m_lastW,
                  m_lastH,
                  w,
                  h,
                  winX,
                  winY,
                  winW,
                  winH,
                  io.MousePos.x,
                  io.MousePos.y,
                  io.DisplaySize.x,
                  io.DisplaySize.y,
                  viewport ? viewport->Pos.x : 0.0f,
                  viewport ? viewport->Pos.y : 0.0f,
                  viewport ? viewport->Size.x : 0.0f,
                  viewport ? viewport->Size.y : 0.0f,
                  IManager.mouseX,
                  IManager.mouseY);
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
    } else {
      m_lastFailedResizeW = w;
      m_lastFailedResizeH = h;
      m_nextResizeRetryTime = now + std::chrono::milliseconds(250);
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

bool EditorApp::HasHostedSceneWindowOpen() const {
  return m_meshEditorOpen || m_meshEditorCloseRequested ||
         m_playSceneOpen || m_playSceneCloseRequested ||
         m_ragdollEditorOpen;
}

void EditorApp::ResetMainEditorFrameLimiter() {
  m_mainEditorFrameLimiterActive = false;
  m_nextMainEditorFrameTime = {};
}

void EditorApp::ThrottleMainEditorFrameIfNeeded() {
  if (HasHostedSceneWindowOpen()) {
    ResetMainEditorFrameLimiter();
    return;
  }

  using Clock = std::chrono::steady_clock;
  static constexpr auto kFrameInterval =
      std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 60.0));
  static constexpr auto kMaxSleep =
      std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(20));
  auto now = Clock::now();
  if (m_mainEditorFrameLimiterActive && now < m_nextMainEditorFrameTime) {
    const auto sleepDuration = m_nextMainEditorFrameTime - now;
    if (sleepDuration <= kMaxSleep) {
      std::this_thread::sleep_until(m_nextMainEditorFrameTime);
      now = Clock::now();
    } else {
      T8_LOG_VERBOSE("[T8ditor] Resetting stale frame limiter target (%.3f ms ahead)",
                     std::chrono::duration<double, std::milli>(sleepDuration).count());
      ResetMainEditorFrameLimiter();
      now = Clock::now();
    }
  }
  m_nextMainEditorFrameTime = now + kFrameInterval;
  m_mainEditorFrameLimiterActive = true;
}

void EditorApp::LoadPendingScene() {
  // Execute deferred scene load BEFORE any GPU work this frame
  if (!g_pendingLoadPath.empty()) {
    ResetMainEditorFrameLimiter();
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
      g_sceneProfiles = sf.profiles;
      LoadEditorSceneProfiles();

      // Flush all GPU work from previous frames
      {
        t850::LoadingProgress::ScopedStep gpuStep("Preparing scene", "Waiting for GPU", 2.0f);
        pFramework->pVideoDriver->WaitForGPU();
      }

      // Destroy old scene
      {
        t850::LoadingProgress::ScopedStep cleanupStep("Preparing scene", "Clearing current scene", 3.0f);
        DestroyAllObjectRagdolls();
        DestroyAllPhysicsEntities(m_physics);
        ResetEditorNavMeshState(false);
        m_primMgr.DestroyPrimitives();
        g_objects.clear();
        InvalidateSceneObjectTransformSnapshots();
        g_cameras.clear();
        g_lights.clear();
        ReleaseCameraGizmoCaches(g_lightCameraGizmoCameras);
        g_lightCameras.clear();
        g_cameraAnimations.clear();
        ReleaseGizmoCache(g_godRaysVolumeGizmo);
        g_godRaysVolume = t850::scene::SceneGodRaysVolumeDesc{};
        g_splines.clear();
        ReleaseGizmoCaches(g_splineGizmos);
        g_selectedIdx = -1;
        g_selectedSplinePoint = -1;
        g_selectionType = 0;
        g_activeCameraIdx = -1;
        ClearMixedSelection();
        g_groups.clear();
        g_activeGroupIdx = -1;
        g_undoStack.Clear();
        g_unloadedSceneObjects.clear();
        g_gameEntities.clear();
      }

      g_sceneCollisionResourcePath = ResolveSceneCollisionPath(sf, loadPath);
      {
        t850::LoadingProgress::ScopedStep collisionStep("Loading scene", "Collision data", 4.0f);
        LoadSceneCollisionClip(g_sceneCollisionResourcePath);
      }

      if (g_deferredReady) {
        const std::string renderGraphPath = sf.render_graph.empty()
            ? "Scenes/T8ditor_RenderGraph.json"
            : sf.render_graph;
        t850::LoadingProgress::ScopedStep graphReloadStep("Preparing scene", "Render graph", 4.0f);
        pFramework->pVideoDriver->DestroyRTs();
        g_renderGraph = t850::RenderGraph{};
        if (g_renderGraph.Load(renderGraphPath)) {
          g_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, m_sceneProps);
          const bool dofOn = m_sceneProps.ToogleDOF != 0;
          g_renderGraph.SetPassEnabled("CoC", dofOn);
          g_renderGraph.SetPassEnabled("Combine CoC", dofOn);
          g_renderGraph.SetPassEnabled("DOF", dofOn);
          g_renderGraph.SetPassEnabled("DOF 2", dofOn);
          T8_LOG_INFO("[T8ditor] Loaded scene render graph '%s'", renderGraphPath.c_str());
        } else {
          g_deferredReady = false;
          T8_LOG_ERROR("[T8ditor] Failed to load scene render graph '%s'; deferred rendering disabled",
                       renderGraphPath.c_str());
        }
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
          obj.showOrientation = od.show_orientation;
          obj.navAgentFrontYawOffsetDeg = od.nav_agent_front_yaw_offset_deg;
          obj.navAgentFaceYawSign = od.nav_agent_face_yaw_sign;
          obj.navAgentTargetMode = od.nav_agent_target_mode.empty() ? "direct" : od.nav_agent_target_mode;
          obj.navAgentFollowDistance = od.nav_agent_follow_distance;
          obj.navAgentSideOffset = od.nav_agent_side_offset;
          obj.navAgentFormationDepthStep = od.nav_agent_formation_depth_step;
          obj.navAgentSlot = od.nav_agent_slot;
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

      if (!sf.physics_entities.empty()) {
        t850::LoadingProgress::ScopedStep physicsStep("Loading scene", "Physics entities", 4.0f);
        for (const t850::scene::ScenePhysicsEntityDesc& entityDesc : sf.physics_entities) {
          RestorePhysicsEntityFromScene(m_physics, entityDesc);
        }
      }
      g_gameEntities = sf.game_entities;
      g_splines = sf.splines;
      ReleaseCameraGizmoCaches(g_lightCameraGizmoCameras);
      g_lightCameras = sf.light_cameras;
      g_cameraAnimations = sf.camera_animations;
      g_godRaysVolume = sf.god_rays_volume.value_or(t850::scene::SceneGodRaysVolumeDesc{});
      if (sf.god_rays_volume) {
        g_godRaysVolume.authored = true;
        ClampGodRaysVolumeLightCamera(g_godRaysVolume);
      }
      EnsureInferredGameEntities();
      if (sf.navigation_mesh) {
        t850::LoadingProgress::ScopedStep navMeshStep("Loading scene", "NavMesh", 4.0f);
        RestoreEditorNavMeshFromScene(*sf.navigation_mesh);
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
      {
        t850::LoadingProgress::ScopedStep profileStep("Loading scene", "Embedded rendering profile", 2.0f);
        LoadEditorSceneProfiles();
      }
      ImGuiApplySceneLayout(sf.editor.allow_custom_layout, sf.editor.imgui_layout);

      t850::LoadingProgress::Complete("Scene loaded", loadPath);
    } else {
      t850::LoadingProgress::Complete("Scene load failed", loadPath);
    }
    t850::LoadingProgress::ClearFrameCallback();
    t850::LoadingProgress::Clear();
    g_pendingLoadPath.clear();
    ResetMainEditorFrameLimiter();
    if (!g_pendingDeleteAfterLoadPath.empty() && g_pendingDeleteAfterLoadPath == loadPath) {
      std::error_code ec;
      std::filesystem::remove(g_pendingDeleteAfterLoadPath, ec);
      g_pendingDeleteAfterLoadPath.clear();
    }
  }
}

void EditorApp::OnUpdate() {
  ThrottleMainEditorFrameIfNeeded();

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

  ApplyPendingEditorCubemap();

  LoadPendingScene();

  CheckResize();
  UpdateEditorSplinePreview(m_dtSecs);

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
  if (g_editorResizeInputTraceFrames > 0) {
#ifdef OS_WINDOWS
    auto* w32 = static_cast<t850::Win32Framework*>(pFramework);
    int winX = 0, winY = 0, winW = 0, winH = 0, pixW = 0, pixH = 0;
    SDL_WindowFlags flags = 0;
    if (w32 && w32->m_pWindow) {
      SDL_GetWindowPosition(w32->m_pWindow, &winX, &winY);
      SDL_GetWindowSize(w32->m_pWindow, &winW, &winH);
      SDL_GetWindowSizeInPixels(w32->m_pWindow, &pixW, &pixH);
      flags = SDL_GetWindowFlags(w32->m_pWindow);
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    T8_LOG_TRACE("[T8ditorInputTrace] framesLeft=%d winPos=(%d,%d) winSize=(%d,%d) pix=(%d,%d) flags=0x%llX iMouse=(%d,%d) iDelta=(%d,%d) iButtons=(%d,%d,%d) ioMouse=(%.1f,%.1f) ioDisplay=(%.1f,%.1f) viewportPos=(%.1f,%.1f) viewportSize=(%.1f,%.1f) wantMouse=%d wantKeyboard=%d",
                g_editorResizeInputTraceFrames,
                winX,
                winY,
                winW,
                winH,
                pixW,
                pixH,
                static_cast<unsigned long long>(flags),
                IManager.mouseX,
                IManager.mouseY,
                IManager.xDelta,
                IManager.yDelta,
                IManager.MouseButtonStates[0][0] ? 1 : 0,
                IManager.MouseButtonStates[0][1] ? 1 : 0,
                IManager.MouseButtonStates[0][2] ? 1 : 0,
                io.MousePos.x,
                io.MousePos.y,
                io.DisplaySize.x,
                io.DisplaySize.y,
                viewport ? viewport->Pos.x : 0.0f,
                viewport ? viewport->Pos.y : 0.0f,
                viewport ? viewport->Size.x : 0.0f,
                viewport ? viewport->Size.y : 0.0f,
                imguiWantsMouse ? 1 : 0,
                imguiWantsKeyboard ? 1 : 0);
#endif
    --g_editorResizeInputTraceFrames;
  }

  if (!io.WantTextInput && IManager.PressedOnceKey(T800K_ESCAPE)) {
    if (m_meshEditorOpen) {
      m_meshEditorWindow.RequestClose();
      IManager.xDelta = 0;
      IManager.yDelta = 0;
      return;
    }
    if (m_playSceneOpen) {
      m_playSceneWindow.RequestClose();
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
      const bool mouseOverViewportImage = m_meshEditorViewport.Contains(globalMouseX, globalMouseY);
      IManager.mouseX = m_meshEditorViewport.LocalX(globalMouseX);
      IManager.mouseY = m_meshEditorViewport.LocalY(globalMouseY);
      m_meshEditorScene->SetIgnoreImGuiMouseCaptureForInput(mouseOverViewportImage || m_meshEditorViewport.InputActive());
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
      const bool mouseOverViewportImage = m_playSceneViewport.Contains(globalMouseX, globalMouseY);
      IManager.mouseX = m_playSceneViewport.LocalX(globalMouseX);
      IManager.mouseY = m_playSceneViewport.LocalY(globalMouseY);
      m_playScene->SetIgnoreImGuiMouseCaptureForInput(mouseOverViewportImage || (!m_playSceneGuiVisible && m_playSceneViewport.InputActive()));
      m_playScene->OnInput(&IManager);
      IManager.mouseX = savedMouseX;
      IManager.mouseY = savedMouseY;
    }
    return;
  }

  const bool ctrlDown = IManager.PressedKey(T800K_LCTRL) || IManager.PressedKey(T800K_RCTRL);
  const bool shiftDown = IManager.PressedKey(T800K_LSHIFT) || IManager.PressedKey(T800K_RSHIFT);
  const bool orbitCameraMode = m_editorCameraMode == EditorCameraMode::Orbit;

  // Z shortcuts must work after selecting from hierarchy panels too; only text entry blocks them.
  if (!io.WantTextInput && IManager.PressedOnceKey(T800K_z)) {
    if (ctrlDown && shiftDown)
      g_undoStack.Redo();
    else if (ctrlDown)
      g_undoStack.Undo();
    else
      FrameSelectedEntity();
  }
  if (!io.WantTextInput && ctrlDown && IManager.PressedOnceKey(T800K_y))
    g_undoStack.Redo();

  if (!imguiWantsKeyboard) {

    if (orbitCameraMode) {
      if (IManager.PressedOnceKey(T800K_q)) m_gizmo.SetMode(GizmoMode::Select);
      if (IManager.PressedOnceKey(T800K_w)) m_gizmo.SetMode(GizmoMode::Translate);
      if (IManager.PressedOnceKey(T800K_e)) m_gizmo.SetMode(GizmoMode::Rotate);
      if (IManager.PressedOnceKey(T800K_r)) m_gizmo.SetMode(GizmoMode::Scale);
    }

    // Space key — dump frame (all RTs + snapshot)
    if (IManager.PressedOnceKey(T800K_SPACE) && g_dumperInited)
      g_dumper.RequestDump();
  }

  // Delete key — works even when ImGui panels have focus (but not during text input)
  if (!io.WantTextInput && IManager.PressedOnceKey(T800K_DELETE) && g_selectedIdx >= 0) {
    std::string undoBeforeKey;
    EditorUndoState undoBefore = CaptureEditorUndoState(&undoBeforeKey);
    const int undoCountBeforeDelete = g_undoStack.UndoCount();
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
    else if (g_selectionType == 6 && g_selectedIdx < static_cast<int>(g_lightCameras.size())) {
      AdjustActiveLightCameraAfterErase(g_selectedIdx);
      AdjustGodRaysLightCameraAfterErase(g_selectedIdx);
      if (g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_lightCameraGizmoCameras.size())) {
        if (t850::g_pBaseDriver) {
          t850::g_pBaseDriver->WaitForGPU();
        }
        ReleaseGizmoCache(g_lightCameraGizmoCameras[static_cast<std::size_t>(g_selectedIdx)].gizmo);
        g_lightCameraGizmoCameras.erase(g_lightCameraGizmoCameras.begin() + g_selectedIdx);
      }
      g_lightCameras.erase(g_lightCameras.begin() + g_selectedIdx);
      g_selectedIdx = -1;
      T8_LOG_INFO("[T8ditor] Light camera deleted");
    }
    else if (g_selectionType == 0 && g_selectedIdx < (int)g_objects.size()) {
      DestroyObjectRagdoll(g_objects[g_selectedIdx]);
      g_objects.erase(g_objects.begin() + g_selectedIdx);
      g_selectedIdx = -1;
      T8_LOG_INFO("[T8ditor] Mesh deleted");
    }
    else if (g_selectionType == 3 && g_selectedIdx < (int)g_physicsEntities.size()) {
      DestroyPhysicsEntity(m_physics, g_selectedIdx);
      T8_LOG_INFO("[T8ditor] Physics entity deleted");
    }
    else if (g_selectionType == 4 && g_selectedIdx == 0) {
      if (!DeleteSelectedNavAuthoringChild()) {
        DestroyEditorNavMesh();
        T8_LOG_INFO("[T8ditor] NavMesh deleted");
      }
    }
    else if (g_selectionType == 8 && g_selectedIdx == 0) {
      g_godRaysVolume.authored = false;
      g_godRaysVolume.enabled = false;
      g_godRaysVolume.visible = false;
      g_godRaysVolume.clip_enabled = false;
      g_selectedIdx = -1;
      g_selectionType = 0;
      T8_LOG_INFO("[T8ditor] God Rays volume disabled");
    }
    if (g_undoStack.UndoCount() == undoCountBeforeDelete) {
      PushEditorUndoState("Delete", undoBefore, undoBeforeKey, CaptureEditorUndoState(nullptr));
    }
  }

  float wheel = ImGuiConsumeWheelDelta();
  bool blockWheel = imguiWantsMouse && !ImGuizmo::IsOver();
  if (orbitCameraMode) {
    const float activeCameraAspect =
        (m_lastW > 0 && m_lastH > 0) ? static_cast<float>(m_lastW) / static_cast<float>(m_lastH) : 16.0f / 9.0f;
    if (!HandleActiveCameraOrbitControls(
            IManager,
            m_dtSecs,
            blockWheel ? 0.0f : wheel,
            imguiWantsMouse,
            imguiWantsKeyboard,
            activeCameraAspect)) {
      m_camera.Update(m_dtSecs, IManager,
                      blockWheel ? 0.0f : wheel,
                      imguiWantsMouse,
                      imguiWantsKeyboard);
    }
  } else {
    Camera* controlledCamera = &m_camera.GetCameraMutable();
    int activeLightCameraIndex = -1;
    const float aspect = (m_lastW > 0 && m_lastH > 0) ? (float)m_lastW / (float)m_lastH : 16.0f / 9.0f;
    if (g_activeCameraIdx >= 0 && g_activeCameraIdx < static_cast<int>(g_cameras.size())) {
      ApplySceneCameraToRuntimeCamera(g_cameras[static_cast<std::size_t>(g_activeCameraIdx)], g_viewCamera, aspect);
      controlledCamera = &g_viewCamera;
    } else if (DecodeActiveLightCameraIndex(g_activeCameraIdx, activeLightCameraIndex) &&
               activeLightCameraIndex >= 0 &&
               activeLightCameraIndex < static_cast<int>(g_lightCameras.size())) {
      ApplySceneLightCameraToRuntimeCamera(
          g_lightCameras[static_cast<std::size_t>(activeLightCameraIndex)],
          g_viewCamera,
          aspect);
      controlledCamera = &g_viewCamera;
    }
    controlledCamera->m_externalControl = false;
    controlledCamera->m_lookAtCenter = false;
    t850::CameraInputState state;
    if (!io.WantTextInput) {
      state.moveForward = IManager.PressedKey(T800K_w);
      state.moveBackward = IManager.PressedKey(T800K_s);
      state.moveLeft = IManager.PressedKey(T800K_a);
      state.moveRight = IManager.PressedKey(T800K_d);
      state.moveUp = IManager.PressedKey(T800K_q);
      state.moveDown = IManager.PressedKey(T800K_e);
      state.sprint = IManager.PressedKey(T800K_LSHIFT) || IManager.PressedKey(T800K_RSHIFT);
    }
    const bool mouseLook = !imguiWantsMouse && IManager.PressedMouseButton(2);
    state.mouseLook = mouseLook;
    state.mouseDeltaX = mouseLook ? static_cast<float>(IManager.xDelta) : 0.0f;
    state.mouseDeltaY = mouseLook ? static_cast<float>(IManager.yDelta) : 0.0f;

    if (m_editorCameraController.GetActiveProfileType() != t850::CameraProfileType::FreeFly) {
      m_editorCameraController.SetActiveProfile(t850::CameraProfileType::FreeFly);
    }
    m_editorCameraController.AttachCamera(controlledCamera);
    m_editorCameraController.HandleInput(state);
    m_editorCameraController.Update(m_dtSecs, t850::CameraUpdateContext{});
    if (controlledCamera == &g_viewCamera) {
      if (g_activeCameraIdx >= 0 && g_activeCameraIdx < static_cast<int>(g_cameras.size())) {
        SyncSceneCameraFromRuntimeCamera(g_viewCamera, g_cameras[static_cast<std::size_t>(g_activeCameraIdx)]);
      } else if (activeLightCameraIndex >= 0 &&
                 activeLightCameraIndex < static_cast<int>(g_lightCameras.size())) {
        SyncLightCameraFromRuntimeCamera(g_viewCamera, g_lightCameras[static_cast<std::size_t>(activeLightCameraIndex)]);
      }
    }
  }

  if (orbitCameraMode && !imguiWantsKeyboard)
    ProcessSelectionInput();

  if (orbitCameraMode && !imguiWantsMouse) {
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

  const bool wantsTransform =
      IManager.PressedKey(T800K_l) || IManager.PressedKey(T800K_j) ||
      IManager.PressedKey(T800K_u) || IManager.PressedKey(T800K_o) ||
      IManager.PressedKey(T800K_i) || IManager.PressedKey(T800K_k) ||
      IManager.PressedKey(T800K_LEFTBRACKET) || IManager.PressedKey(T800K_RIGHTBRACKET) ||
      IManager.PressedKey(T800K_QUOTE) || IManager.PressedKey(T800K_SEMICOLON);
  if (!wantsTransform) {
    return;
  }

  std::string undoBeforeKey;
  EditorUndoState undoBefore = CaptureEditorUndoState(&undoBeforeKey);
  const int undoCountBeforeTransform = g_undoStack.UndoCount();

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
  if (g_undoStack.UndoCount() == undoCountBeforeTransform) {
    PushEditorUndoState("Keyboard Transform", undoBefore, undoBeforeKey, CaptureEditorUndoState(nullptr));
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
    if (g_activeCameraIdx == g_selectedIdx) {
      g_activeCameraIdx = -1;
    }
    const float radius = camera.type == CameraType::Orthographic
        ? std::clamp((std::max)(camera.orthoW, camera.orthoH) * 0.08f, 3.0f, 50.0f)
        : 4.0f;
    frameSphere(camera.position, radius, "camera");
    return;
  } else if (g_selectionType == 2 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_lights.size())) {
    const SceneLight& light = g_lights[static_cast<std::size_t>(g_selectedIdx)];
    const float radius = light.type == EditorLightType::Omni
        ? (std::clamp)(light.radius, 1.0f, 100.0f)
        : 3.0f;
    frameSphere(light.position, radius, "light");
    return;
  } else if (g_selectionType == 3 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_physicsEntities.size())) {
    t850::AABB bounds;
    if (GetPhysicsEntityWorldAABB(g_physicsEntities[static_cast<std::size_t>(g_selectedIdx)], m_physics, bounds)) {
      m_camera.FrameBounds(bounds);
      T8_LOG_INFO("[T8ditor] Framed physics entity '%s'",
                  g_physicsEntities[static_cast<std::size_t>(g_selectedIdx)].name.c_str());
      return;
    }
  } else if (g_selectionType == 4 && g_selectedIdx == 0) {
    t850::AABB bounds;
    if (GetEditorNavMeshWorldAABB(bounds)) {
      m_camera.FrameBounds(bounds);
      T8_LOG_INFO("[T8ditor] Framed NavMesh");
      return;
    }
  } else if (g_selectionType == 5 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_splines.size())) {
    const auto& spline = g_splines[static_cast<std::size_t>(g_selectedIdx)];
    t850::AABB bounds;
    for (const auto& point : spline.points) {
      bounds.ExpandToInclude(point.position.x, point.position.y, point.position.z);
    }
    if (bounds.IsValid()) {
      m_camera.FrameBounds(bounds);
      T8_LOG_INFO("[T8ditor] Framed spline '%s'", spline.name.c_str());
      return;
    }
  } else if (g_selectionType == 6 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_lightCameras.size())) {
    const auto& lightCamera = g_lightCameras[static_cast<std::size_t>(g_selectedIdx)];
    int activeLightCameraIndex = -1;
    if (DecodeActiveLightCameraIndex(g_activeCameraIdx, activeLightCameraIndex) &&
        activeLightCameraIndex == g_selectedIdx) {
      g_activeCameraIdx = -1;
    }
    const XVECTOR3 center(lightCamera.position.x, lightCamera.position.y, lightCamera.position.z, 1.0f);
    const float radius = lightCamera.type == 1
        ? std::clamp((std::max)(lightCamera.ortho_w, lightCamera.ortho_h) * 0.08f, 3.0f, 50.0f)
        : 4.0f;
    frameSphere(center, radius, "light camera");
    return;
  } else if (g_selectionType == 7 &&
             g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_splines.size()) &&
             g_selectedSplinePoint >= 0 &&
             g_selectedSplinePoint < static_cast<int>(g_splines[static_cast<std::size_t>(g_selectedIdx)].points.size())) {
    const auto& point = g_splines[static_cast<std::size_t>(g_selectedIdx)].points[static_cast<std::size_t>(g_selectedSplinePoint)];
    frameSphere(XVECTOR3(point.position.x, point.position.y, point.position.z, 1.0f), 3.0f, "spline point");
    return;
  } else if (g_selectionType == 8 && g_selectedIdx == 0) {
    t850::AABB bounds(
        XVECTOR3(g_godRaysVolume.position.x - std::abs(g_godRaysVolume.half_extents.x),
                 g_godRaysVolume.position.y - std::abs(g_godRaysVolume.half_extents.y),
                 g_godRaysVolume.position.z - std::abs(g_godRaysVolume.half_extents.z)),
        XVECTOR3(g_godRaysVolume.position.x + std::abs(g_godRaysVolume.half_extents.x),
                 g_godRaysVolume.position.y + std::abs(g_godRaysVolume.half_extents.y),
                 g_godRaysVolume.position.z + std::abs(g_godRaysVolume.half_extents.z)));
    m_camera.FrameBounds(bounds);
    T8_LOG_INFO("[T8ditor] Framed God Rays volume");
    return;
  }

  m_camera.ResetToDefault();
  T8_LOG_INFO("[T8ditor] Reset editor view");
}

// Project a world-space point to screen coordinates.
ImVec2 WorldToScreen(const XVECTOR3& p, const XMATRIX44& vp, int w, int h) {
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

bool ProjectAABBToScreenRect(const t850::AABB& box, const XMATRIX44& vp,
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

static XVECTOR3 NormalizeOrForward(XVECTOR3 direction) {
  direction.w = 0.0f;
  if (direction.Length() <= 0.000001f) {
    return XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  }
  direction.Normalize();
  direction.w = 0.0f;
  return direction;
}

static XVECTOR3 OrientationFrontFromWorld(const XMATRIX44& world) {
  return NormalizeOrForward(t850::TransformDirection(XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f), world));
}

static float OrientationArrowLengthFromBounds(const t850::AABB& bounds) {
  if (!bounds.IsValid()) {
    return 1.0f;
  }
  const XVECTOR3 extents = bounds.Extents();
  const float maxExtent = (std::max)(extents.x, (std::max)(extents.y, extents.z));
  return std::clamp(maxExtent * 0.55f, 0.50f, 8.0f);
}

static void DrawOrientationArrow(EditorLineRenderer& lines,
                                 const XMATRIX44& vp,
                                 const XVECTOR3& origin,
                                 const XVECTOR3& frontDirection,
                                 float length,
                                 const XVECTOR3& color) {
  if (!lines.IsReady()) {
    return;
  }
  const XVECTOR3 front = NormalizeOrForward(frontDirection);
  XVECTOR3 up(0.0f, 1.0f, 0.0f, 0.0f);
  XVECTOR3 right;
  XVecCross(right, up, front);
  if (right.Length() <= 0.000001f) {
    up = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
    XVecCross(right, up, front);
  }
  right = NormalizeOrForward(right);
  XVECTOR3 arrowUp;
  XVecCross(arrowUp, front, right);
  arrowUp = NormalizeOrForward(arrowUp);

  const float safeLength = (std::max)(0.05f, length);
  const float headLength = (std::max)(0.12f, safeLength * 0.28f);
  const float headWidth = headLength * 0.55f;
  const XVECTOR3 tip(origin.x + front.x * safeLength,
                     origin.y + front.y * safeLength,
                     origin.z + front.z * safeLength,
                     1.0f);
  const XVECTOR3 headBase(tip.x - front.x * headLength,
                          tip.y - front.y * headLength,
                          tip.z - front.z * headLength,
                          1.0f);

  std::vector<float> verts;
  verts.reserve(6 * 4);
  auto append = [&](const XVECTOR3& p) {
    verts.push_back(p.x);
    verts.push_back(p.y);
    verts.push_back(p.z);
    verts.push_back(1.0f);
  };
  append(origin);
  append(tip);
  append(XVECTOR3(headBase.x + right.x * headWidth,
                  headBase.y + right.y * headWidth,
                  headBase.z + right.z * headWidth,
                  1.0f));
  append(XVECTOR3(headBase.x - right.x * headWidth,
                  headBase.y - right.y * headWidth,
                  headBase.z - right.z * headWidth,
                  1.0f));
  append(XVECTOR3(headBase.x + arrowUp.x * headWidth,
                  headBase.y + arrowUp.y * headWidth,
                  headBase.z + arrowUp.z * headWidth,
                  1.0f));
  append(XVECTOR3(headBase.x - arrowUp.x * headWidth,
                  headBase.y - arrowUp.y * headWidth,
                  headBase.z - arrowUp.z * headWidth,
                  1.0f));

  const unsigned short indices[] = { 0, 1, 1, 2, 1, 3, 1, 4, 1, 5 };
  t850::VertexBuffer* vb = EditorLineRenderer::CreatePositionVB(verts.data(), 6);
  t850::IndexBuffer* ib = EditorLineRenderer::CreateIndexBuffer16(indices, static_cast<unsigned>(sizeof(indices) / sizeof(indices[0])));
  if (vb && ib) {
    XMATRIX44 identity;
    identity.Identity();
    lines.DrawLines(identity, vp, color, vb, ib, static_cast<unsigned>(sizeof(indices) / sizeof(indices[0])), sizeof(float) * 4);
  }
  if (vb) vb->release();
  if (ib) ib->release();
}

static bool BuildRuntimeSpline(const t850::scene::SceneSplineDesc& desc, t850::Spline& outSpline) {
  outSpline.m_points.clear();
  outSpline.m_totalLength = 0.0f;
  outSpline.m_looped = desc.looped;
  std::vector<t850::SplinePoint> points;
  points.reserve(desc.points.size());
  for (const t850::scene::SceneSplinePointDesc& point : desc.points) {
    t850::SplinePoint splinePoint(point.position.x, point.position.y, point.position.z);
    splinePoint.m_velocity = point.velocity;
    splinePoint.m_rotation = XVECTOR3(point.rotation.x, point.rotation.y, point.rotation.z, 0.0f);
    splinePoint.m_LookAtCenter = point.look_at_center;
    points.push_back(splinePoint);
  }
  outSpline.m_points = std::move(points);
  if (outSpline.m_points.size() >= 4) {
    outSpline.Init();
  }
  return outSpline.m_points.size() >= 4 && outSpline.m_totalLength > 0.0f;
}

static float EstimateEditorSplineDurationSeconds(const t850::scene::SceneSplineDesc& desc) {
  t850::Spline spline;
  if (!BuildRuntimeSpline(desc, spline)) {
    return 0.0f;
  }
  t850::SplineAgent agent;
  agent.m_pSpline = &spline;
  agent.m_moving = true;
  agent.m_velocity = desc.agent_velocity;
  agent.SetOffset(0.0f);
  agent.m_actualPoint = spline.GetPoint(spline.GetNormalizedOffset(0.0f));

  constexpr float dt = 1.0f / 60.0f;
  constexpr float maxDuration = 3600.0f;
  float elapsed = 0.0f;
  while (elapsed < maxDuration) {
    agent.Update(dt);
    elapsed += dt;
    if (agent.FinishedJourneyThisUpdate()) {
      return elapsed;
    }
  }
  return spline.m_totalLength > 0.0f && desc.agent_velocity > 0.0f
      ? spline.m_totalLength / (desc.agent_velocity * 2.4f)
      : 0.0f;
}

static int FindDefaultTimelineSplineIndex(int preferredIndex) {
  auto validPlayable = [](int index) {
    return index >= 0 &&
        index < static_cast<int>(g_splines.size()) &&
        g_splines[static_cast<std::size_t>(index)].attached_camera >= 0 &&
        g_splines[static_cast<std::size_t>(index)].attached_camera < static_cast<int>(g_cameras.size()) &&
        g_splines[static_cast<std::size_t>(index)].points.size() >= 4;
  };
  if (validPlayable(preferredIndex)) return preferredIndex;
  if (g_selectionType == 5 && validPlayable(g_selectedIdx)) return g_selectedIdx;
  for (int i = 0; i < static_cast<int>(g_splines.size()); ++i) {
    if (g_splines[static_cast<std::size_t>(i)].play_on_start && validPlayable(i)) return i;
  }
  for (int i = 0; i < static_cast<int>(g_splines.size()); ++i) {
    if (validPlayable(i)) return i;
  }
  return -1;
}

static bool IsLightCameraAnimationTarget(const std::string& target) {
  std::string lower = target;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lower == "light_camera" || lower == "light-camera" || lower == "lightcamera";
}

static XVECTOR3 SceneVec3ToX(const t850::scene::Vec3f& value, float w = 1.0f) {
  return XVECTOR3(value.x, value.y, value.z, w);
}

static t850::scene::Vec3f XToSceneVec3(const XVECTOR3& value) {
  return {value.x, value.y, value.z};
}

static float CameraAnimationLocalTime(const t850::scene::SceneCameraAnimationDesc& animation,
                                      float timeSeconds) {
  float t = (std::max)(0.0f, timeSeconds - animation.start_time);
  if (animation.duration > 0.0f) {
    t = animation.loop ? std::fmod(t, animation.duration) : std::clamp(t, 0.0f, animation.duration);
  }
  return t;
}

static void ApplyCameraAnimationToRuntimeCamera(const t850::scene::SceneCameraAnimationDesc& animation,
                                                float localTime,
                                                ::Camera& runtimeCamera) {
  runtimeCamera.Eye += SceneVec3ToX(animation.linear_velocity, 0.0f) * localTime;
  const XVECTOR3 target = runtimeCamera.Eye + runtimeCamera.Look + SceneVec3ToX(animation.target_velocity, 0.0f) * localTime;
  runtimeCamera.SetLookAt(target);
  runtimeCamera.Pitch += animation.angular_velocity.x * localTime;
  runtimeCamera.Yaw += animation.angular_velocity.y * localTime;
  runtimeCamera.Roll += animation.angular_velocity.z * localTime;
  runtimeCamera.Update(0.0f);
}

static void ApplyEditorCameraAnimationsAtTime(float timeSeconds) {
  if (g_cameraAnimations.empty()) {
    return;
  }

  for (const t850::scene::SceneCameraAnimationDesc& animation : g_cameraAnimations) {
    if (!animation.enabled || !animation.play_on_timeline) continue;
    const float localTime = CameraAnimationLocalTime(animation, timeSeconds);
    const bool lightTarget = IsLightCameraAnimationTarget(animation.target);

    if (lightTarget) {
      if (animation.camera < 0 || animation.camera >= static_cast<int>(g_lightCameras.size())) continue;
      const t850::scene::SceneLightCameraDesc* base = nullptr;
      if (g_loadedSceneFile.light_cameras.size() > static_cast<std::size_t>(animation.camera)) {
        base = &g_loadedSceneFile.light_cameras[static_cast<std::size_t>(animation.camera)];
      }
      t850::scene::SceneLightCameraDesc animated = base ? *base : g_lightCameras[static_cast<std::size_t>(animation.camera)];
      ::Camera runtimeCamera;
      ApplySceneLightCameraToRuntimeCamera(animated, runtimeCamera, 1.0f);
      ApplyCameraAnimationToRuntimeCamera(animation, localTime, runtimeCamera);
      animated.position = XToSceneVec3(runtimeCamera.Eye);
      animated.target = XToSceneVec3(runtimeCamera.Eye + runtimeCamera.Look);
      animated.fov_deg = Rad2Deg(runtimeCamera.Fov);
      animated.ortho_w = runtimeCamera.Width;
      animated.ortho_h = runtimeCamera.Height;
      animated.near_plane = runtimeCamera.NPlane;
      animated.far_plane = runtimeCamera.FPlane;
      g_lightCameras[static_cast<std::size_t>(animation.camera)] = animated;
    } else {
      if (animation.camera < 0 || animation.camera >= static_cast<int>(g_cameras.size())) continue;
      SceneCamera animated;
      if (g_loadedSceneFile.cameras.size() > static_cast<std::size_t>(animation.camera)) {
        const t850::scene::SceneCameraDesc& base = g_loadedSceneFile.cameras[static_cast<std::size_t>(animation.camera)];
        animated.name = base.name;
        animated.type = static_cast<CameraType>(base.type);
        animated.position = SceneVec3ToX(base.position);
        animated.target = SceneVec3ToX(base.target);
        animated.fovDeg = base.fov_deg;
        animated.orthoW = base.ortho_w;
        animated.orthoH = base.ortho_h;
        animated.nearPlane = base.near_plane;
        animated.farPlane = base.far_plane;
        animated.visible = base.visible;
        animated.frozen = base.frozen;
      } else {
        animated = g_cameras[static_cast<std::size_t>(animation.camera)];
      }
      ::Camera runtimeCamera;
      ApplySceneCameraToRuntimeCamera(animated, runtimeCamera, 16.0f / 9.0f);
      ApplyCameraAnimationToRuntimeCamera(animation, localTime, runtimeCamera);
      SyncSceneCameraFromRuntimeCamera(runtimeCamera, animated);
      g_cameras[static_cast<std::size_t>(animation.camera)] = animated;
    }
  }
}

static void SyncLightCameraGizmoCamera(const t850::scene::SceneLightCameraDesc& desc, SceneCamera& camera) {
  camera.name = desc.name;
  camera.type = desc.type == 1 ? CameraType::Orthographic : CameraType::Perspective;
  camera.position = XVECTOR3(desc.position.x, desc.position.y, desc.position.z, 1.0f);
  camera.target = XVECTOR3(desc.target.x, desc.target.y, desc.target.z, 1.0f);
  camera.fovDeg = desc.fov_deg;
  camera.orthoW = desc.ortho_w;
  camera.orthoH = desc.ortho_h;
  camera.nearPlane = desc.near_plane;
  camera.farPlane = desc.far_plane;
  camera.visible = desc.visible && desc.enabled;
  camera.frozen = desc.frozen;
}

static void ApplySceneLightCameraToRuntimeCamera(const t850::scene::SceneLightCameraDesc& desc,
                                                 ::Camera& camera,
                                                 float perspectiveAspect) {
  const XVECTOR3 position(desc.position.x, desc.position.y, desc.position.z, 1.0f);
  const float nearPlane = (std::max)(0.0001f, desc.near_plane);
  const float farPlane = (std::max)(nearPlane + 0.01f, desc.far_plane);
  if (desc.type == 1) {
    camera.InitOrtho(position,
                     (std::max)(0.01f, desc.ortho_w),
                     (std::max)(0.01f, desc.ortho_h),
                     nearPlane,
                     farPlane);
  } else {
    camera.InitPerspective(position,
                           Deg2Rad((std::max)(1.0f, desc.fov_deg)),
                           (std::max)(0.01f, perspectiveAspect),
                           nearPlane,
                           farPlane);
  }
  camera.SetLookAt(XVECTOR3(desc.target.x, desc.target.y, desc.target.z, 1.0f));
  camera.Update(0.0f);
}

static void ApplySceneCameraToRuntimeCamera(const SceneCamera& desc,
                                            ::Camera& camera,
                                            float perspectiveAspect) {
  if (desc.type == CameraType::Orthographic) {
    camera.InitOrtho(desc.position, desc.orthoW, desc.orthoH, desc.nearPlane, desc.farPlane);
  } else {
    camera.InitPerspective(desc.position,
                           Deg2Rad((std::max)(1.0f, desc.fovDeg)),
                           (std::max)(0.01f, perspectiveAspect),
                           desc.nearPlane,
                           desc.farPlane);
  }
  camera.SetLookAt(desc.target);
  camera.Update(0.0f);
}

static void SyncSceneCameraFromRuntimeCamera(const ::Camera& runtimeCamera, SceneCamera& desc) {
  desc.position = runtimeCamera.Eye;
  desc.target = runtimeCamera.Eye + runtimeCamera.Look;
  desc.fovDeg = Rad2Deg(runtimeCamera.Fov);
  desc.orthoW = runtimeCamera.Width;
  desc.orthoH = runtimeCamera.Height;
  desc.nearPlane = runtimeCamera.NPlane;
  desc.farPlane = runtimeCamera.FPlane;
}

static void SyncLightCameraFromRuntimeCamera(const ::Camera& runtimeCamera,
                                             t850::scene::SceneLightCameraDesc& desc) {
  desc.position = { runtimeCamera.Eye.x, runtimeCamera.Eye.y, runtimeCamera.Eye.z };
  const XVECTOR3 target = runtimeCamera.Eye + runtimeCamera.Look;
  desc.target = { target.x, target.y, target.z };
  desc.fov_deg = Rad2Deg(runtimeCamera.Fov);
  desc.ortho_w = runtimeCamera.Width;
  desc.ortho_h = runtimeCamera.Height;
  desc.near_plane = runtimeCamera.NPlane;
  desc.far_plane = runtimeCamera.FPlane;
}

static void ApplyGodRaysVolumeToSceneProps(const t850::scene::SceneGodRaysVolumeDesc& volume,
                                           SceneProps& sceneProps,
                                           bool canBeActive) {
  sceneProps.GodRaysVolumeEnabled = (canBeActive && volume.enabled && volume.clip_enabled) ? 1 : 0;
  if (canBeActive) {
    sceneProps.ActiveLightCamera = volume.light_camera;
  }
  sceneProps.GodRaysVolumeCenter = XVECTOR3(volume.position.x, volume.position.y, volume.position.z, 1.0f);
  sceneProps.GodRaysVolumeHalfExtents = XVECTOR3((std::max)(0.001f, std::abs(volume.half_extents.x)),
                                                 (std::max)(0.001f, std::abs(volume.half_extents.y)),
                                                 (std::max)(0.001f, std::abs(volume.half_extents.z)),
                                                 0.0f);
}

static bool ApplyOrbitStyleControlsToCamera(::Camera& runtimeCamera,
                                            XVECTOR3& eye,
                                            XVECTOR3& target,
                                            bool orthographic,
                                            float& orthoWidth,
                                            float& orthoHeight,
                                            InputManager& input,
                                            float dtSecs,
                                            float wheelDelta,
                                            bool skipMouse,
                                            bool skipKeyboard) {
  bool changed = false;
  runtimeCamera.Update(0.0f);

  XVECTOR3 targetToEye(eye.x - target.x, eye.y - target.y, eye.z - target.z, 0.0f);
  float distance = targetToEye.Length();
  if (distance <= 0.0001f) {
    targetToEye = XVECTOR3(-runtimeCamera.Look.x, -runtimeCamera.Look.y, -runtimeCamera.Look.z, 0.0f);
    distance = 1.0f;
  } else {
    targetToEye.x /= distance;
    targetToEye.y /= distance;
    targetToEye.z /= distance;
  }

  auto dollyOrZoom = [&](float factor) {
    factor = std::clamp(factor, 0.001f, 1000.0f);
    if (orthographic) {
      orthoWidth = std::clamp(orthoWidth * factor, 0.001f, 100000.0f);
      orthoHeight = std::clamp(orthoHeight * factor, 0.001f, 100000.0f);
    } else {
      distance = std::clamp(distance * factor, 0.001f, 1000000.0f);
      eye = XVECTOR3(target.x + targetToEye.x * distance,
                     target.y + targetToEye.y * distance,
                     target.z + targetToEye.z * distance,
                     1.0f);
    }
    changed = true;
  };

  auto orbit = [&](float dx, float dy) {
    float yaw = std::atan2(targetToEye.x, targetToEye.z);
    float pitch = std::asin(std::clamp(targetToEye.y, -1.0f, 1.0f));
    yaw += dx * 0.005f;
    pitch = std::clamp(pitch + dy * 0.005f, -1.55f, 1.55f);
    const float cp = std::cos(pitch);
    targetToEye = XVECTOR3(std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp, 0.0f);
    eye = XVECTOR3(target.x + targetToEye.x * distance,
                   target.y + targetToEye.y * distance,
                   target.z + targetToEye.z * distance,
                   1.0f);
    changed = true;
  };

  if (!skipMouse) {
    const bool middleDown = input.PressedMouseButton(1);
    const bool rightDown = input.PressedMouseButton(2);
    const bool leftDown = input.PressedMouseButton(0);
    const bool altDown = input.PressedKey(T800K_LALT) || input.PressedKey(T800K_RALT);
    const float dx = static_cast<float>(input.xDelta);
    const float dy = static_cast<float>(input.yDelta);

    if (middleDown) {
      const float panScale = 0.01f * (std::max)(1.0f, distance);
      XVECTOR3 delta(
          -runtimeCamera.Right.x * dx * panScale + runtimeCamera.Up.x * dy * panScale,
          -runtimeCamera.Right.y * dx * panScale + runtimeCamera.Up.y * dy * panScale,
          -runtimeCamera.Right.z * dx * panScale + runtimeCamera.Up.z * dy * panScale,
          0.0f);
      eye += delta;
      target += delta;
      changed = true;
    } else if (rightDown || (leftDown && altDown)) {
      orbit(dx, dy);
    }
  }

  if (!skipKeyboard) {
    const float orbitStep = 1.5f * dtSecs;
    if (input.PressedKey(T800K_LEFT)) orbit(-orbitStep / 0.005f, 0.0f);
    if (input.PressedKey(T800K_RIGHT)) orbit(orbitStep / 0.005f, 0.0f);
    if (input.PressedKey(T800K_UP)) orbit(0.0f, -orbitStep / 0.005f);
    if (input.PressedKey(T800K_DOWN)) orbit(0.0f, orbitStep / 0.005f);

    const float zoomStep = 4.0f * dtSecs * (std::max)(1.0f, distance * 0.1f);
    if (input.PressedKey(T800K_PLUS) || input.PressedKey(T800K_EQUALS) || input.PressedKey(T800K_KP_PLUS)) {
      dollyOrZoom(orthographic ? std::pow(1.10f, -4.0f * dtSecs) : (std::max)(0.001f, (distance - zoomStep) / distance));
    }
    if (input.PressedKey(T800K_MINUS) || input.PressedKey(T800K_KP_MINUS)) {
      dollyOrZoom(orthographic ? std::pow(1.10f, 4.0f * dtSecs) : (distance + zoomStep) / distance);
    }
  }

  if (wheelDelta != 0.0f) {
    dollyOrZoom(std::pow(1.10f, -wheelDelta));
  }

  if (changed) {
    runtimeCamera.Eye = eye;
    runtimeCamera.SetLookAt(target);
    if (orthographic) {
      runtimeCamera.Width = orthoWidth;
      runtimeCamera.Height = orthoHeight;
      runtimeCamera.CreatePojection();
      runtimeCamera.Update(0.0f);
    }
  }
  return changed;
}

static bool HandleActiveCameraOrbitControls(InputManager& input,
                                            float dtSecs,
                                            float wheelDelta,
                                            bool skipMouse,
                                            bool skipKeyboard,
                                            float perspectiveAspect) {
  if (g_activeCameraIdx >= 0 && g_activeCameraIdx < static_cast<int>(g_cameras.size())) {
    SceneCamera& sceneCamera = g_cameras[static_cast<std::size_t>(g_activeCameraIdx)];
    ApplySceneCameraToRuntimeCamera(sceneCamera, g_viewCamera, perspectiveAspect);
    XVECTOR3 eye = sceneCamera.position;
    XVECTOR3 target = sceneCamera.target;
    float orthoW = sceneCamera.orthoW;
    float orthoH = sceneCamera.orthoH;
    if (ApplyOrbitStyleControlsToCamera(
            g_viewCamera,
            eye,
            target,
            sceneCamera.type == CameraType::Orthographic,
            orthoW,
            orthoH,
            input,
            dtSecs,
            wheelDelta,
            skipMouse,
            skipKeyboard)) {
      sceneCamera.position = eye;
      sceneCamera.target = target;
      sceneCamera.orthoW = orthoW;
      sceneCamera.orthoH = orthoH;
    }
    return true;
  }

  int activeLightCameraIndex = -1;
  if (DecodeActiveLightCameraIndex(g_activeCameraIdx, activeLightCameraIndex) &&
      activeLightCameraIndex >= 0 &&
      activeLightCameraIndex < static_cast<int>(g_lightCameras.size())) {
    t850::scene::SceneLightCameraDesc& lightCamera = g_lightCameras[static_cast<std::size_t>(activeLightCameraIndex)];
    ApplySceneLightCameraToRuntimeCamera(lightCamera, g_viewCamera, perspectiveAspect);
    XVECTOR3 eye(lightCamera.position.x, lightCamera.position.y, lightCamera.position.z, 1.0f);
    XVECTOR3 target(lightCamera.target.x, lightCamera.target.y, lightCamera.target.z, 1.0f);
    float orthoW = lightCamera.ortho_w;
    float orthoH = lightCamera.ortho_h;
    if (ApplyOrbitStyleControlsToCamera(
            g_viewCamera,
            eye,
            target,
            lightCamera.type == 1,
            orthoW,
            orthoH,
            input,
            dtSecs,
            wheelDelta,
            skipMouse,
            skipKeyboard)) {
      lightCamera.position = {eye.x, eye.y, eye.z};
      lightCamera.target = {target.x, target.y, target.z};
      lightCamera.ortho_w = orthoW;
      lightCamera.ortho_h = orthoH;
    }
    return true;
  }

  return false;
}

static XMATRIX44 BuildCameraEntityGizmoMatrix(const XVECTOR3& position,
                                              const XVECTOR3& target,
                                              float scaleX,
                                              float scaleY,
                                              float scaleZ) {
  XVECTOR3 forward(target.x - position.x, target.y - position.y, target.z - position.z, 0.0f);
  if (forward.Length() <= 0.000001f) {
    forward = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  } else {
    forward.Normalize();
  }

  XVECTOR3 up(0.0f, 1.0f, 0.0f, 0.0f);
  XVECTOR3 right;
  XVecCross(right, up, forward);
  if (right.Length() <= 0.000001f) {
    up = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
    XVecCross(right, up, forward);
  }
  right.Normalize();
  XVecCross(up, forward, right);
  up.Normalize();

  XMATRIX44 world;
  XMatIdentity(world);
  world.m[0][0] = right.x * scaleX;
  world.m[0][1] = right.y * scaleX;
  world.m[0][2] = right.z * scaleX;
  world.m[1][0] = up.x * scaleY;
  world.m[1][1] = up.y * scaleY;
  world.m[1][2] = up.z * scaleY;
  world.m[2][0] = forward.x * scaleZ;
  world.m[2][1] = forward.y * scaleZ;
  world.m[2][2] = forward.z * scaleZ;
  world.m[3][0] = position.x;
  world.m[3][1] = position.y;
  world.m[3][2] = position.z;
  return world;
}

static bool ManipulateCameraEntityGizmo(const ::Camera& viewCamera,
                                        int mode,
                                        XVECTOR3& position,
                                        XVECTOR3& target,
                                        bool orthographic,
                                        float& orthoW,
                                        float& orthoH,
                                        float& fovDeg) {
  (void)fovDeg;
  if (mode < 0 || mode > 2) {
    return false;
  }
  if (!orthographic && mode != 0) {
    return false;
  }

  const float scaleX = mode == 2 ? (orthographic ? (std::max)(0.001f, orthoW) : 1.0f) : 1.0f;
  const float scaleY = mode == 2 ? (orthographic ? (std::max)(0.001f, orthoH) : 1.0f) : 1.0f;
  XMATRIX44 worldMat = BuildCameraEntityGizmoMatrix(position, target, scaleX, scaleY, 1.0f);
  XMATRIX44 deltaMatrix;
  XMatIdentity(deltaMatrix);

  ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
  if (mode == 1) operation = ImGuizmo::ROTATE;
  else if (mode == 2) operation = ImGuizmo::SCALE;

  const bool manipulated = ImGuizmo::Manipulate(
      &viewCamera.View.m[0][0],
      &viewCamera.Projection.m[0][0],
      operation,
      mode == 0 ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
      &worldMat.m[0][0],
      &deltaMatrix.m[0][0]);
  if (!manipulated) {
    return false;
  }

  float translation[3], rotationDeg[3], scale[3];
  ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], translation, rotationDeg, scale);

  if (mode == 0) {
    const XVECTOR3 newPosition(translation[0], translation[1], translation[2], 1.0f);
    const XVECTOR3 delta(newPosition.x - position.x, newPosition.y - position.y, newPosition.z - position.z, 0.0f);
    position = newPosition;
    target += delta;
    return true;
  }

  if (mode == 1) {
    XVECTOR3 direction(target.x - position.x, target.y - position.y, target.z - position.z, 0.0f);
    const float distance = (std::max)(0.001f, direction.Length());
    if (direction.Length() <= 0.000001f) {
      direction = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
    } else {
      direction.Normalize();
    }
    XVECTOR3 rotated = t850::TransformDirection(direction, deltaMatrix);
    if (rotated.Length() <= 0.000001f) {
      return false;
    }
    rotated.Normalize();
    target = XVECTOR3(position.x + rotated.x * distance,
                      position.y + rotated.y * distance,
                      position.z + rotated.z * distance,
                      1.0f);
    return true;
  }

  if (mode == 2) {
    if (orthographic) {
      orthoW = (std::max)(0.001f, std::abs(scale[0]));
      orthoH = (std::max)(0.001f, std::abs(scale[1]));
    }
    return true;
  }

  return false;
}

static bool TranslateCameraPointGizmo(const ::Camera& viewCamera,
                                      int gizmoId,
                                      XVECTOR3& point) {
  ImGuizmo::SetID(gizmoId);
  XMATRIX44 worldMat;
  XMatTranslation(worldMat, point.x, point.y, point.z);
  const bool manipulated = ImGuizmo::Manipulate(
      &viewCamera.View.m[0][0],
      &viewCamera.Projection.m[0][0],
      ImGuizmo::TRANSLATE,
      ImGuizmo::WORLD,
      &worldMat.m[0][0],
      nullptr);
  ImGuizmo::SetID(-1);
  if (!manipulated) {
    return false;
  }

  float translation[3], rotation[3], scale[3];
  ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], translation, rotation, scale);
  point = XVECTOR3(translation[0], translation[1], translation[2], 1.0f);
  return true;
}

static uint64_t HashSplineFloat(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return static_cast<uint64_t>(bits);
}

static uint64_t HashEditorSplineDrawData(const t850::scene::SceneSplineDesc& desc, bool selected, int selectedPoint) {
  uint64_t hash = 1469598103934665603ull;
  auto mix = [&](uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
  };
  mix(desc.looped ? 1ull : 0ull);
  mix(selected ? 1ull : 0ull);
  mix(static_cast<uint64_t>(selectedPoint + 1));
  mix(static_cast<uint64_t>(desc.points.size()));
  for (const t850::scene::SceneSplinePointDesc& point : desc.points) {
    mix(HashSplineFloat(point.position.x));
    mix(HashSplineFloat(point.position.y));
    mix(HashSplineFloat(point.position.z));
    mix(HashSplineFloat(point.velocity));
  }
  return hash;
}

static void DrawGodRaysVolume(EditorLineRenderer& lines,
                              const XMATRIX44& vp,
                              const t850::scene::SceneGodRaysVolumeDesc& desc,
                              bool selected,
                              GizmoCache& cache) {
  if (!lines.IsReady() || !desc.enabled || !desc.visible || !desc.show_wire) {
    return;
  }
  const XVECTOR3 color = selected
      ? XVECTOR3(1.0f, 0.82f, 0.18f, 1.0f)
      : XVECTOR3(1.0f, 0.45f, 0.05f, 1.0f);

  const float verts[] = {
      -1,-1,-1,1,  1,-1,-1,1,  1, 1,-1,1, -1, 1,-1,1,
      -1,-1, 1,1,  1,-1, 1,1,  1, 1, 1,1, -1, 1, 1,1};
  const unsigned short indices[] = {
      0,1, 1,2, 2,3, 3,0,
      4,5, 5,6, 6,7, 7,4,
      0,4, 1,5, 2,6, 3,7};
  const unsigned indexCount = static_cast<unsigned>(sizeof(indices) / sizeof(indices[0]));
  if (!cache.vb || !cache.ib || cache.count != indexCount) {
    if (cache.vb || cache.ib) {
      if (t850::g_pBaseDriver) {
        t850::g_pBaseDriver->WaitForGPU();
      }
      ReleaseGizmoCache(cache);
    }
    cache.vb = EditorLineRenderer::CreatePositionVB(verts, 8);
    cache.ib = EditorLineRenderer::CreateIndexBuffer16(indices, indexCount);
    cache.count = indexCount;
  }
  if (cache.vb && cache.ib) {
    const t850::scene::Vec3f h{
        (std::max)(0.001f, std::abs(desc.half_extents.x)),
        (std::max)(0.001f, std::abs(desc.half_extents.y)),
        (std::max)(0.001f, std::abs(desc.half_extents.z))};
    XMATRIX44 world;
    XMatScaling(world, h.x, h.y, h.z);
    world.m[3][0] = desc.position.x;
    world.m[3][1] = desc.position.y;
    world.m[3][2] = desc.position.z;
    lines.DrawLines(world, vp, color, cache.vb, cache.ib, cache.count, sizeof(float) * 4);
  }
}

static XMATRIX44 BuildNavMeshVolumeWorld(const t850::scene::SceneNavMeshVolumeDesc& volume) {
  XMATRIX44 S, Rx, Ry, Rz, T;
  XMatScaling(S,
              (std::max)(0.001f, std::abs(volume.half_extents.x)),
              (std::max)(0.001f, std::abs(volume.half_extents.y)),
              (std::max)(0.001f, std::abs(volume.half_extents.z)));
  XMatRotationX(Rx, volume.rotation.x);
  XMatRotationY(Ry, volume.rotation.y);
  XMatRotationZ(Rz, volume.rotation.z);
  XMatTranslation(T, volume.position.x, volume.position.y, volume.position.z);
  return S * Rx * Ry * Rz * T;
}

static t850::AABB NavMeshVolumeWorldAABB(const t850::scene::SceneNavMeshVolumeDesc& volume) {
  const t850::AABB local(XVECTOR3(-1.0f, -1.0f, -1.0f, 1.0f),
                         XVECTOR3( 1.0f,  1.0f,  1.0f, 1.0f));
  return local.Transformed(BuildNavMeshVolumeWorld(volume));
}

static XVECTOR3 NavMeshVolumeColor(const t850::scene::SceneNavMeshVolumeDesc& volume, bool selected) {
  if (selected) {
    return XVECTOR3(1.0f, 0.85f, 0.12f, 1.0f);
  }
  if (volume.type == "include_bounds" || volume.type == "include" || volume.type == "bounds") {
    return XVECTOR3(0.2f, 0.55f, 1.0f, 1.0f);
  }
  if (volume.type == "area_cost" || volume.type == "area" || volume.type == "cost") {
    return XVECTOR3(0.9f, 0.35f, 1.0f, 1.0f);
  }
  return XVECTOR3(1.0f, 0.18f, 0.12f, 1.0f);
}

static void DrawNavMeshVolume(EditorLineRenderer& lines,
                              const XMATRIX44& vp,
                              const t850::scene::SceneNavMeshVolumeDesc& volume,
                              bool selected,
                              GizmoCache& cache) {
  if (!lines.IsReady() || !volume.enabled || !volume.visible || !volume.show_wire) {
    return;
  }
  const float verts[] = {
      -1,-1,-1,1,  1,-1,-1,1,  1, 1,-1,1, -1, 1,-1,1,
      -1,-1, 1,1,  1,-1, 1,1,  1, 1, 1,1, -1, 1, 1,1};
  const unsigned short indices[] = {
      0,1, 1,2, 2,3, 3,0,
      4,5, 5,6, 6,7, 7,4,
      0,4, 1,5, 2,6, 3,7};
  const unsigned indexCount = static_cast<unsigned>(sizeof(indices) / sizeof(indices[0]));
  if (!cache.vb || !cache.ib || cache.count != indexCount) {
    if (cache.vb || cache.ib) {
      if (t850::g_pBaseDriver) {
        t850::g_pBaseDriver->WaitForGPU();
      }
      ReleaseGizmoCache(cache);
    }
    cache.vb = EditorLineRenderer::CreatePositionVB(verts, 8);
    cache.ib = EditorLineRenderer::CreateIndexBuffer16(indices, indexCount);
    cache.count = indexCount;
  }
  if (cache.vb && cache.ib) {
    lines.DrawLines(BuildNavMeshVolumeWorld(volume),
                    vp,
                    NavMeshVolumeColor(volume, selected),
                    cache.vb,
                    cache.ib,
                    cache.count,
                    sizeof(float) * 4);
  }
}

static void DrawEditorSpline(EditorLineRenderer& lines,
                             const XMATRIX44& vp,
                             const t850::scene::SceneSplineDesc& desc,
                             bool selected,
                             int selectedPoint,
                             GizmoCache& cache) {
  if (!lines.IsReady() || !desc.visible || !desc.show_wire || desc.points.size() < 2) {
    return;
  }

  const uint64_t drawHash = HashEditorSplineDrawData(desc, selected, selectedPoint);
  if (cache.vb && cache.ib && cache.hash == drawHash) {
    XMATRIX44 identity;
    identity.Identity();
    const XVECTOR3 color = selected
        ? XVECTOR3(1.0f, 0.82f, 0.18f, 1.0f)
        : XVECTOR3(0.15f, 0.90f, 0.95f, 1.0f);
    lines.DrawLines(identity, vp, color, cache.vb, cache.ib, cache.count, sizeof(float) * 4);
    return;
  }

  std::vector<float> verts;
  std::vector<unsigned short> indices;
  auto append = [&](const t850::scene::Vec3f& p) -> unsigned short {
    const unsigned int index = static_cast<unsigned int>(verts.size() / 4);
    if (index > std::numeric_limits<unsigned short>::max()) {
      return std::numeric_limits<unsigned short>::max();
    }
    verts.push_back(p.x);
    verts.push_back(p.y);
    verts.push_back(p.z);
    verts.push_back(1.0f);
    return static_cast<unsigned short>(index);
  };

  t850::Spline spline;
  const bool hasRuntimeSpline = BuildRuntimeSpline(desc, spline);
  if (hasRuntimeSpline) {
    const int sampleCount = (std::min)(512, (std::max)(32, static_cast<int>(spline.m_totalLength * 2.0f)));
    unsigned short previous = append(desc.points.front().position);
    for (int sample = 1; sample < sampleCount; ++sample) {
      const float distance = spline.m_totalLength * (static_cast<float>(sample) / static_cast<float>(sampleCount));
      const t850::SplinePoint point = spline.GetPoint(spline.GetNormalizedOffset(distance));
      t850::scene::Vec3f p{point.x, point.y, point.z};
      const unsigned short current = append(p);
      indices.push_back(previous);
      indices.push_back(current);
      previous = current;
    }
  } else {
    unsigned short previous = append(desc.points.front().position);
    for (std::size_t i = 1; i < desc.points.size(); ++i) {
      const unsigned short current = append(desc.points[i].position);
      indices.push_back(previous);
      indices.push_back(current);
      previous = current;
    }
  }

  for (int pointIndex = 0; pointIndex < static_cast<int>(desc.points.size()); ++pointIndex) {
    const t850::scene::SceneSplinePointDesc& point = desc.points[static_cast<std::size_t>(pointIndex)];
    const float r = pointIndex == selectedPoint ? 0.85f : (selected ? 0.45f : 0.30f);
    const t850::scene::Vec3f p = point.position;
    const unsigned short a = append({p.x - r, p.y, p.z});
    const unsigned short b = append({p.x + r, p.y, p.z});
    const unsigned short c = append({p.x, p.y - r, p.z});
    const unsigned short d = append({p.x, p.y + r, p.z});
    const unsigned short e = append({p.x, p.y, p.z - r});
    const unsigned short f = append({p.x, p.y, p.z + r});
    indices.insert(indices.end(), {a, b, c, d, e, f});
  }

  if (verts.empty() || indices.empty()) {
    return;
  }

  const unsigned indexCount = static_cast<unsigned>(indices.size());
  if (!cache.vb || !cache.ib || cache.count != indexCount) {
    if (cache.vb || cache.ib) {
      if (t850::g_pBaseDriver) {
        t850::g_pBaseDriver->WaitForGPU();
      }
      ReleaseGizmoCache(cache);
    }
    cache.vb = EditorLineRenderer::CreatePositionVB(
        verts.data(),
        static_cast<unsigned>(verts.size() / 4),
        t850::BufferUsage::DINAMIC);
    cache.ib = EditorLineRenderer::CreateIndexBuffer16(indices.data(), indexCount);
    cache.count = indexCount;
  } else {
    cache.vb->UpdateFromBuffer(*t850::T8DeviceContext, verts.data());
  }
  cache.hash = drawHash;

  if (cache.vb && cache.ib) {
    XMATRIX44 identity;
    identity.Identity();
    const XVECTOR3 color = selected
        ? XVECTOR3(1.0f, 0.82f, 0.18f, 1.0f)
        : XVECTOR3(0.15f, 0.90f, 0.95f, 1.0f);
    lines.DrawLines(identity, vp, color, cache.vb, cache.ib, cache.count, sizeof(float) * 4);
  }
}

static bool ApplyEditorSplineToAttachedCamera(t850::scene::SceneSplineDesc& desc) {
  if (desc.attached_camera < 0 ||
      desc.attached_camera >= static_cast<int>(g_cameras.size()) ||
      desc.points.size() < 4) {
    return false;
  }
  t850::Spline spline;
  if (!BuildRuntimeSpline(desc, spline)) return false;
  float safeOffset = (std::max)(0.0f, desc.agent_offset);
  safeOffset = std::fmod(safeOffset, spline.m_totalLength);
  t850::SplineAgent agent;
  agent.m_pSpline = &spline;
  agent.m_moving = true;
  agent.m_velocity = desc.agent_velocity;
  agent.SetOffset(safeOffset);
  agent.m_actualPoint = spline.GetPoint(spline.GetNormalizedOffset(agent.GetOffset()));
  SceneCamera& camera = g_cameras[static_cast<std::size_t>(desc.attached_camera)];
  ::Camera previewCamera;
  if (camera.type == CameraType::Orthographic) {
    previewCamera.InitOrtho(camera.position, camera.orthoW, camera.orthoH, camera.nearPlane, camera.farPlane);
  } else {
    previewCamera.InitPerspective(camera.position, Deg2Rad(camera.fovDeg), 16.0f / 9.0f, camera.nearPlane, camera.farPlane);
  }
  previewCamera.AttachAgent(agent);
  previewCamera.m_lookAtCenter = false;
  previewCamera.Update(0.0f);
  camera.position = previewCamera.Eye;
  camera.target = previewCamera.Eye + previewCamera.Look;
  desc.agent_offset = safeOffset;
  return true;
}

static bool ApplyEditorSplineAgentToAttachedCamera(const t850::scene::SceneSplineDesc& desc,
                                                   const t850::SplineAgent& agent) {
  if (desc.attached_camera < 0 || desc.attached_camera >= static_cast<int>(g_cameras.size())) {
    return false;
  }
  SceneCamera& camera = g_cameras[static_cast<std::size_t>(desc.attached_camera)];
  ::Camera previewCamera;
  if (camera.type == CameraType::Orthographic) {
    previewCamera.InitOrtho(camera.position, camera.orthoW, camera.orthoH, camera.nearPlane, camera.farPlane);
  } else {
    previewCamera.InitPerspective(camera.position, Deg2Rad(camera.fovDeg), 16.0f / 9.0f, camera.nearPlane, camera.farPlane);
  }
  previewCamera.AttachAgent(agent);
  previewCamera.m_lookAtCenter = false;
  previewCamera.Update(0.0f);
  camera.position = previewCamera.Eye;
  camera.target = previewCamera.Eye + previewCamera.Look;
  return true;
}

t850::Ray BuildEditorCameraRay(const ::Camera& camera,
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
  const ImGuiIO& io = ImGui::GetIO();

  if (m_editorNavLinkPickMode != 0) {
    if (io.WantCaptureMouse) {
      return;
    }
    if (IManager.PressedOnceMouseButton(0) &&
        m_editorSelectedNavLink >= 0 &&
        m_editorSelectedNavLink < static_cast<int>(m_editorNavMeshLinks.size())) {
      int nodeIndex = -1;
      XVECTOR3 nodePosition;
      if (PickEditorNavMeshNodeFromMouse(IManager.mouseX, IManager.mouseY, nodeIndex, nodePosition)) {
        const int completedPickMode = m_editorNavLinkPickMode;
        t850::scene::SceneNavMeshLinkDesc& link = m_editorNavMeshLinks[static_cast<std::size_t>(m_editorSelectedNavLink)];
        if (m_editorNavLinkPickMode == 1) {
          link.start_node = nodeIndex;
          link.start = { nodePosition.x, nodePosition.y, nodePosition.z };
        } else {
          link.end_node = nodeIndex;
          link.end = { nodePosition.x, nodePosition.y, nodePosition.z };
        }
        m_editorNavLinkPickMode = 0;
        m_editorNavMeshAuthored = true;
        m_editorNavMeshDirty = true;
        m_editorNavMeshStatus = "Authored link endpoint changed. Click Re-generate.";
        DumpEditorNavMeshWireGeometry(completedPickMode == 1 ? "pick_start_node" : "pick_end_node");
      } else {
        m_editorNavMeshStatus = "No Detour node close enough to the click.";
      }
      return;
    }
    return;
  }

  // Marquee drag in Select mode (skip when Alt is held — Alt+left-drag is orbit)
  if (selectMode) {
    if (io.WantCaptureMouse) return;
    const bool altDown = IManager.PressedKey(T800K_LALT) || IManager.PressedKey(T800K_RALT);

    // Start marquee on mouse press (only if Alt is not held)
    if (IManager.PressedOnceMouseButton(0) && !altDown) {
      g_marqueeActive = true;
      g_marqueeStart = ImVec2((float)IManager.mouseX, (float)IManager.mouseY);
      g_marqueeStartScreen = ImGui::GetMousePos();
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

      if (!shiftDown) ClearMixedSelection();

      for (int i = 0; i < (int)g_objects.size(); ++i) {
        if (!g_objects[i].wireframe.IsLoaded() || g_objects[i].frozen || !g_objects[i].visible)
          continue;
        t850::AABB worldBox = g_objects[i].wireframe.WorldAABB();
        if (AABBInScreenRect(worldBox, m_vp, m_lastW, m_lastH, rMinX, rMinY, rMaxX, rMaxY)) {
          AddMixedSelection(0, i);
        }
      }
      for (int i = 0; i < static_cast<int>(g_physicsEntities.size()); ++i) {
        const PhysicsSceneEntity& entity = g_physicsEntities[static_cast<std::size_t>(i)];
        if (!entity.visible || entity.frozen) {
          continue;
        }
        t850::AABB worldBox;
        if (GetPhysicsEntityWorldAABB(entity, m_physics, worldBox) &&
            AABBInScreenRect(worldBox, m_vp, m_lastW, m_lastH, rMinX, rMinY, rMaxX, rMaxY)) {
          AddMixedSelection(3, i);
        }
      }

      // Update single selection to match multi-select state
      if (!g_multiEntitySelect.empty()) {
        const SelectionRef first = *g_multiEntitySelect.begin();
        g_selectedIdx = first.index;
        g_selectionType = first.type;
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

  if (m_editorNavMeshAuthoringMode && m_editorNavMeshShowSourcePreview) {
    UpdateEditorNavMeshClassification();
    int triangleIndex = -1;
    if (PickEditorNavMeshTriangleFromMouse(IManager.mouseX, IManager.mouseY, triangleIndex)) {
      m_editorSelectedNavTriangle = triangleIndex;
      m_editorSelectedNavVolume = -1;
      m_editorSelectedNavLink = -1;
      g_selectedIdx = 0;
      g_selectionType = 4;
      ClearMixedSelection();
      ReleaseNavMeshClassificationGizmos();
      m_editorNavMeshStatus = "Source triangle selected. Use Exclude Selected Triangle or Area Cost Selected Triangle.";
      return;
    }
  }

  // Test all objects, pick the closest
  float bestT = FLT_MAX;
  int   bestIdx  = -1;
  int   bestType = 0;
  int   bestNavVolume = -1;

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

  // Test physics entities
  for (int i = 0; i < static_cast<int>(g_physicsEntities.size()); ++i) {
    float physicsT = 0.0f;
    const bool hit = RaycastPhysicsEntity(g_physicsEntities[i],
                                          m_physics,
                                          ray,
                                          m_vp,
                                          m_lastW,
                                          m_lastH,
                                          static_cast<float>(IManager.mouseX),
                                          static_cast<float>(IManager.mouseY),
                                          physicsT);
    if (hit && physicsT < bestT) {
      bestT = physicsT;
      bestIdx = i;
      bestType = 3;
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

  // Test authored light cameras.
  for (int i = 0; i < static_cast<int>(g_lightCameras.size()); ++i) {
    const auto& camera = g_lightCameras[static_cast<std::size_t>(i)];
    if (camera.frozen || !camera.visible) continue;
    const float hs = 2.0f;
    t850::AABB box(
        XVECTOR3(camera.position.x - hs, camera.position.y - hs, camera.position.z - hs),
        XVECTOR3(camera.position.x + hs, camera.position.y + hs, camera.position.z + hs));
    float t = 0.0f;
    const bool hit = t850::RayIntersectsAABB(ray, box, t);
    if (hit && t < bestT) {
      bestT = t; bestIdx = i; bestType = 6;
    }
  }

  // Test God Rays volume.
  if (GodRaysVolumeCanBeActive(g_godRaysVolume, g_renderGraph) &&
      g_godRaysVolume.enabled && g_godRaysVolume.visible && !g_godRaysVolume.frozen) {
    const auto& p = g_godRaysVolume.position;
    const auto& h = g_godRaysVolume.half_extents;
    t850::AABB box(
        XVECTOR3(p.x - std::abs(h.x), p.y - std::abs(h.y), p.z - std::abs(h.z)),
        XVECTOR3(p.x + std::abs(h.x), p.y + std::abs(h.y), p.z + std::abs(h.z)));
    float t = 0.0f;
    if (t850::RayIntersectsAABB(ray, box, t) && t < bestT) {
      bestT = t;
      bestIdx = 0;
      bestType = 8;
    }
  }

  // Test NavMesh authoring volumes.
  for (int volumeIndex = 0; volumeIndex < static_cast<int>(m_editorNavMeshVolumes.size()); ++volumeIndex) {
    const auto& volume = m_editorNavMeshVolumes[static_cast<std::size_t>(volumeIndex)];
    if (!volume.enabled || !volume.visible || volume.frozen) {
      continue;
    }
    float t = 0.0f;
    if (t850::RayIntersectsAABB(ray, NavMeshVolumeWorldAABB(volume), t) && t < bestT) {
      bestT = t;
      bestIdx = 0;
      bestType = 4;
      bestNavVolume = volumeIndex;
    }
  }

  // Test spline control points with a screen-space threshold so the small markers are easy to select.
  const float pointPickRadiusPx = 14.0f;
  float bestPointScreenDist2 = pointPickRadiusPx * pointPickRadiusPx;
  int bestSplinePoint = -1;
  for (int splineIndex = 0; splineIndex < static_cast<int>(g_splines.size()); ++splineIndex) {
    const auto& spline = g_splines[static_cast<std::size_t>(splineIndex)];
    if (!spline.visible || spline.frozen) continue;
    for (int pointIndex = 0; pointIndex < static_cast<int>(spline.points.size()); ++pointIndex) {
      const auto& point = spline.points[static_cast<std::size_t>(pointIndex)];
      const XVECTOR3 worldPoint(point.position.x, point.position.y, point.position.z, 1.0f);
      const ImVec2 screen = WorldToScreen(worldPoint, m_vp, m_lastW, m_lastH);
      if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) continue;
      const float dx = screen.x - static_cast<float>(IManager.mouseX);
      const float dy = screen.y - static_cast<float>(IManager.mouseY);
      const float dist2 = dx * dx + dy * dy;
      if (dist2 >= bestPointScreenDist2) continue;

      const float hs = 0.85f;
      t850::AABB box(
          XVECTOR3(point.position.x - hs, point.position.y - hs, point.position.z - hs),
          XVECTOR3(point.position.x + hs, point.position.y + hs, point.position.z + hs));
      float t = 0.0f;
      if (t850::RayIntersectsAABB(ray, box, t) && t < bestT) {
        bestT = t;
        bestIdx = splineIndex;
        bestType = 7;
        bestSplinePoint = pointIndex;
        bestPointScreenDist2 = dist2;
      }
    }
  }

  if (bestIdx >= 0) {
    g_selectedIdx   = bestIdx;
    g_selectionType = bestType;
    g_selectedSplinePoint = (bestType == 7) ? bestSplinePoint : -1;
    if (bestType == 4) {
      m_editorSelectedNavVolume = bestNavVolume;
      m_editorSelectedNavLink = -1;
    } else if (!shiftDown) {
      m_editorSelectedNavVolume = -1;
      m_editorSelectedNavLink = -1;
    }

    if (bestType == 0 || bestType == 3) {
      if (shiftDown) {
        ToggleMixedSelection(bestType, bestIdx);
      } else if (bestType == 0) {
        ClearMixedSelection();
        bool foundGroup = false;
        for (auto& grp : g_groups) {
          if (grp.persistent && grp.members.count(bestIdx)) {
            for (int member : grp.members) {
              AddMixedSelection(0, member);
            }
            foundGroup = true;
            break;
          }
        }
        if (!foundGroup) {
          AddMixedSelection(0, bestIdx);
        }
      } else {
        SetSingleSelection(bestType, bestIdx);
      }
    } else if (!shiftDown) {
      ClearMixedSelection();
    }
  } else {
    g_selectedIdx = -1;
    g_selectedSplinePoint = -1;
    m_editorSelectedNavVolume = -1;
    m_editorSelectedNavLink = -1;
    // Auto-switch to Select mode when deselecting (hides orphaned gizmo)
    m_gizmo.SetMode(GizmoMode::Select);
    if (!shiftDown)
      ClearMixedSelection();
  }
}

void EditorApp::SyncEditorSceneLights(const ::Camera& cam) {
  const int requestedActiveLights = m_sceneProps.ActiveLights;
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
  m_sceneProps.ActiveLights = std::clamp(
      requestedActiveLights,
      0,
      static_cast<int>(m_sceneProps.Lights.size()));
  m_sceneProps.pLightCameras.clear();
  m_editorRuntimeLightCameras.clear();
  m_editorRuntimeLightCameras.resize(g_lightCameras.size());

  int firstEnabledLightCamera = -1;
  for (std::size_t lightCameraIndex = 0; lightCameraIndex < g_lightCameras.size(); ++lightCameraIndex) {
    const auto& lightCamera = g_lightCameras[lightCameraIndex];
    Camera& runtimeLightCamera = m_editorRuntimeLightCameras[lightCameraIndex];
    ApplySceneLightCameraToRuntimeCamera(lightCamera, runtimeLightCamera, 1.0f);
    m_sceneProps.AddLightCamera(&runtimeLightCamera);
    if (!lightCamera.enabled) continue;
    if (firstEnabledLightCamera < 0) {
      firstEnabledLightCamera = static_cast<int>(lightCameraIndex);
      m_editorLightCamera = runtimeLightCamera;
    }
    if (lightCamera.attached_light >= 0 &&
        lightCamera.attached_light < static_cast<int>(m_sceneProps.Lights.size())) {
      Light& attachedLight = m_sceneProps.Lights[static_cast<std::size_t>(lightCamera.attached_light)];
      attachedLight.Position = runtimeLightCamera.Eye;
      attachedLight.Direction = runtimeLightCamera.Look;
    }
  }
  if (firstEnabledLightCamera < 0) {
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
  }
  if (m_sceneProps.pLightCameras.empty()) {
    m_sceneProps.AddLightCamera(&m_editorLightCamera);
  }
  m_sceneProps.ActiveLightCamera = firstEnabledLightCamera >= 0 ? firstEnabledLightCamera : 0;
  m_sceneProps.pCullingCamera = m_sceneProps.GetPrimaryCamera();
}

void EditorApp::RenderEditorSceneFrame(t850::BaseDriver* drv, bool captureFrozenEditorFrame, bool& didCaptureFrozenEditorFrame) {
  // Determine which camera drives rendering
  const float aspect = (m_lastW > 0 && m_lastH > 0) ? (float)m_lastW / (float)m_lastH : 16.0f/9.0f;
  if (g_activeCameraIdx >= 0 && g_activeCameraIdx < (int)g_cameras.size()) {
    // Build a persistent Camera from the scene camera
    SceneCamera& sc = g_cameras[g_activeCameraIdx];
    ApplySceneCameraToRuntimeCamera(sc, g_viewCamera, aspect);
    // Point the scene props active camera at our persistent camera
    if (!m_sceneProps.pCameras.empty())
      m_sceneProps.SetPrimaryCamera(&g_viewCamera);
  } else {
    int activeLightCameraIndex = -1;
    if (DecodeActiveLightCameraIndex(g_activeCameraIdx, activeLightCameraIndex) &&
        activeLightCameraIndex >= 0 &&
        activeLightCameraIndex < static_cast<int>(g_lightCameras.size())) {
      ApplySceneLightCameraToRuntimeCamera(
          g_lightCameras[static_cast<std::size_t>(activeLightCameraIndex)],
          g_viewCamera,
          aspect);
      if (!m_sceneProps.pCameras.empty())
        m_sceneProps.SetPrimaryCamera(&g_viewCamera);
    } else {
      // Editor orbit camera
      if (!m_sceneProps.pCameras.empty())
        m_sceneProps.SetPrimaryCamera(&m_camera.GetCameraMutable());
    }
  }

  const ::Camera& cam = *m_sceneProps.GetPrimaryCamera();
  m_vp = cam.VP;

  // Sync scene lights from editor lights.
  // Use real scene lights by default; keep the camera headlamp only as an explicit/fallback light.
  SyncEditorSceneLights(cam);
  ClampGodRaysVolumeLightCamera(g_godRaysVolume);
  const bool godRaysVolumeCanBeActive = GodRaysVolumeCanBeActive(g_godRaysVolume, g_renderGraph);
  ApplyGodRaysVolumeToSceneProps(g_godRaysVolume, m_sceneProps, godRaysVolumeCanBeActive);

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
        obj.litInst.SetParallaxSettings(
            m_sceneProps.ParallaxLowSamples,
            m_sceneProps.ParallaxHighSamples,
            m_sceneProps.ParallaxHeight);
        obj.litInst.SetParallaxEnabled(m_sceneProps.ToogleParallax != 0);
        obj.litInst.SetParallaxShadowSettings(
            m_sceneProps.ParallaxShadowMinLayers,
            m_sceneProps.ParallaxShadowMaxLayers,
            m_sceneProps.ParallaxShadowSoftness,
            m_sceneProps.ParallaxShadowStrength);
        obj.litInst.SetParallaxShadowEnabled(m_sceneProps.ToogleParallaxShadow != 0);
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
    ::Camera* lightCam = nullptr;
    if (!m_sceneProps.pLightCameras.empty()) {
      m_sceneProps.ActiveLightCamera = std::clamp(
          m_sceneProps.ActiveLightCamera,
          0,
          static_cast<int>(m_sceneProps.pLightCameras.size()) - 1);
      lightCam = m_sceneProps.pLightCameras[static_cast<std::size_t>(m_sceneProps.ActiveLightCamera)];
    }
    t850::EnvironmentMapSet editorEnvMaps;
    editorEnvMaps.SetFallback(g_dummyEnvMapIdx);
    T8_LOG_TRACE("[T8ditor] OnDraw: RenderGraph Execute (%d meshes)...", (int)meshArray.size());
    g_renderGraph.Execute(drv, m_sceneProps,
      meshArray.data(), (int)meshArray.size(),
      g_quads, mainCam, lightCam, nullptr,
      editorEnvMaps,
      captureFrozenEditorFrame ? m_editorFrozenFrameRT : -1);
    didCaptureFrozenEditorFrame = captureFrozenEditorFrame;
    T8_LOG_TRACE("[T8ditor] OnDraw: RenderGraph Execute done");

    // RT debug override: if a specific RT is selected, draw it to backbuffer.
    // Use the directly selected texture instead of a flattened RT index; RT order can change.
    if (g_debugRTTexture) {
      drv->SetBlendState(t850::BaseDriver::BLEND_OPAQUE);
      drv->SetDepthStencilState(t850::BaseDriver::NONE);
      g_quads[7].SetTexture(g_debugRTTexture, 0);
      t850::ShaderKey bk(0);
      bk.setPass(t850::PassType::BACKBUFFER);
      g_quads[7].SetGlobalKey(bk);
      g_quads[7].Draw();
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
      obj.litInst.SetParallaxSettings(
          m_sceneProps.ParallaxLowSamples,
          m_sceneProps.ParallaxHighSamples,
          m_sceneProps.ParallaxHeight);
      obj.litInst.SetParallaxEnabled(m_sceneProps.ToogleParallax != 0);
      obj.litInst.SetParallaxShadowSettings(
          m_sceneProps.ParallaxShadowMinLayers,
          m_sceneProps.ParallaxShadowMaxLayers,
          m_sceneProps.ParallaxShadowSoftness,
          m_sceneProps.ParallaxShadowStrength);
      obj.litInst.SetParallaxShadowEnabled(m_sceneProps.ToogleParallaxShadow != 0);
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
    if (!obj.visible || (obj.primId < 0 && !obj.wireframe.IsLoaded())) continue;
    bool isSelected = (g_selectionType == 0 && i == g_selectedIdx) || g_multiSelect.count(i);
    bool showWire = m_panels.showWireframe || isSelected || obj.showWire;
    t850::RenderSkinnedMesh* skinned = nullptr;
    if (obj.litInst.pBase)
      skinned = dynamic_cast<t850::RenderSkinnedMesh*>(obj.litInst.pBase);
    const bool showSkeleton = m_editorShowSkeleton && skinned && skinned->HasSkinData();
    if (!showWire && !showSkeleton) continue;

    // For skinned meshes, use GPU-skinned wireframe + skeleton (same as SandBox)
    if (skinned && skinned->HasSkinData()) {
      skinned->SetWireframeDepthTex(overlayOpaqueDepth);
      skinned->SetWireframeSecondaryDepthTex(overlayForwardDepth);
      skinned->SetWireframeViewport(m_lastW, m_lastH);
      if (showWire) {
        drv->SetDepthStencilState(t850::BaseDriver::NONE);
        skinned->DrawWireframe(XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f));
      }
      if (showSkeleton) {
        drv->SetDepthStencilState(t850::BaseDriver::NONE);
        skinned->DrawSkeleton();
      }
      drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
    } else if (obj.wireframe.IsLoaded() && m_lines.IsReady()) {
      XVECTOR3 savedColor = obj.wireframe.WireColor;
      obj.wireframe.WireColor = XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f);
      drv->SetDepthStencilState(t850::BaseDriver::NONE);
      obj.wireframe.Draw(m_lines, cam.VP);
      drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
      obj.wireframe.WireColor = savedColor;
    }
  }

  for (int i = 0; i < static_cast<int>(g_physicsEntities.size()); ++i) {
    PhysicsSceneEntity& entity = g_physicsEntities[i];
    if (!entity.visible || !entity.visual || !entity.visual->IsLoaded() || !m_lines.IsReady()) {
      continue;
    }
    const bool isSelected = (g_selectionType == 3 && i == g_selectedIdx) || IsMixedSelected(3, i);
    XVECTOR3 savedColor = entity.visual->WireColor;
    entity.visual->WireColor = isSelected
        ? XVECTOR3(0.2f, 0.55f, 1.0f, 1.0f)
        : XVECTOR3(0.2f, 0.8f, 1.0f, 1.0f);
    drv->SetDepthStencilState(t850::BaseDriver::NONE);
    entity.visual->Draw(m_lines, cam.VP);
    drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
    entity.visual->WireColor = savedColor;
  }

  if (g_selectionType == 0 &&
      g_selectedIdx == g_meshCharacterAuthoringSourceIndex &&
      g_meshCharacterAuthoringInitialized &&
      g_meshCharacterAuthoringTemplate.characterRuntimePath == static_cast<int>(CharacterRuntimePath::Jolt) &&
      g_meshCharacterAuthoringTemplate.visual &&
      g_meshCharacterAuthoringTemplate.visual->IsLoaded() &&
      m_lines.IsReady()) {
    XVECTOR3 savedColor = g_meshCharacterAuthoringTemplate.visual->WireColor;
    g_meshCharacterAuthoringTemplate.visual->WireColor = XVECTOR3(0.2f, 0.55f, 1.0f, 1.0f);
    drv->SetDepthStencilState(t850::BaseDriver::NONE);
    g_meshCharacterAuthoringTemplate.visual->Draw(m_lines, cam.VP);
    drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
    g_meshCharacterAuthoringTemplate.visual->WireColor = savedColor;
  }

  // Camera and light viewport gizmos (only if visible)
  if (m_lines.IsReady()) {
    for (int i = 0; i < static_cast<int>(g_objects.size()); ++i) {
      SceneObject& obj = g_objects[static_cast<std::size_t>(i)];
      if (!obj.visible || !obj.showOrientation) {
        continue;
      }
      const bool selected = g_selectionType == 0 && i == g_selectedIdx;
      XMATRIX44 world = obj.primId >= 0 ? obj.litInst.Final : obj.wireframe.BuildWorld();
      t850::AABB bounds;
      GetEditorObjectWorldAABB(obj, bounds);
      DrawOrientationArrow(
          m_lines,
          cam.VP,
          SceneObjectWorldPosition(obj),
          OrientationFrontFromWorld(world),
          OrientationArrowLengthFromBounds(bounds),
          selected ? XVECTOR3(1.0f, 0.85f, 0.1f, 1.0f) : XVECTOR3(1.0f, 0.55f, 0.0f, 1.0f));
    }
    for (int i = 0; i < static_cast<int>(g_physicsEntities.size()); ++i) {
      const PhysicsSceneEntity& entity = g_physicsEntities[static_cast<std::size_t>(i)];
      if (!entity.visible || !entity.showOrientation || !IsCharacterPhysicsEntity(entity)) {
        continue;
      }
      const bool selected = (g_selectionType == 3 && i == g_selectedIdx) || IsMixedSelected(3, i);
      t850::AABB bounds;
      GetPhysicsEntityWorldAABB(entity, m_physics, bounds);
      DrawOrientationArrow(
          m_lines,
          cam.VP,
          entity.position,
          OrientationFrontFromWorld(MakePhysicsTransform(entity.position, entity.eulerRadians)),
          OrientationArrowLengthFromBounds(bounds),
          selected ? XVECTOR3(0.2f, 0.95f, 1.0f, 1.0f) : XVECTOR3(0.15f, 0.65f, 1.0f, 1.0f));
    }
    for (int i = 0; i < (int)g_cameras.size(); ++i)
      if (g_cameras[i].visible)
        DrawCameraGizmo(m_lines, cam.VP, g_cameras[i], g_selectionType == 1 && i == g_selectedIdx);
    for (int i = 0; i < (int)g_lights.size(); ++i)
      if (g_lights[i].visible)
        DrawLightGizmo(m_lines, cam.VP, g_lights[i], g_selectionType == 2 && i == g_selectedIdx);
    if (g_lightCameraGizmoCameras.size() < g_lightCameras.size())
      g_lightCameraGizmoCameras.resize(g_lightCameras.size());
    for (int i = 0; i < static_cast<int>(g_lightCameras.size()); ++i) {
      const auto& lightCameraDesc = g_lightCameras[static_cast<std::size_t>(i)];
      if (!lightCameraDesc.visible) continue;
      SceneCamera& lightCamera = g_lightCameraGizmoCameras[static_cast<std::size_t>(i)];
      SyncLightCameraGizmoCamera(lightCameraDesc, lightCamera);
      DrawCameraGizmo(m_lines, cam.VP, lightCamera, g_selectionType == 6 && i == g_selectedIdx);
    }
    if (godRaysVolumeCanBeActive) {
      DrawGodRaysVolume(
          m_lines,
          cam.VP,
          g_godRaysVolume,
          g_selectionType == 8,
          g_godRaysVolumeGizmo);
    }
    if (m_editorNavMeshAuthored && m_editorNavMeshVisible) {
      DrawNavMeshClassificationOverlay(m_lines, cam.VP);
    }
    if (g_navMeshVolumeGizmos.size() > m_editorNavMeshVolumes.size()) {
      for (std::size_t i = m_editorNavMeshVolumes.size(); i < g_navMeshVolumeGizmos.size(); ++i) {
        ReleaseGizmoCache(g_navMeshVolumeGizmos[i]);
      }
      g_navMeshVolumeGizmos.resize(m_editorNavMeshVolumes.size());
    } else if (g_navMeshVolumeGizmos.size() < m_editorNavMeshVolumes.size()) {
      g_navMeshVolumeGizmos.resize(m_editorNavMeshVolumes.size());
    }
    for (int i = 0; i < static_cast<int>(m_editorNavMeshVolumes.size()); ++i) {
      DrawNavMeshVolume(
          m_lines,
          cam.VP,
          m_editorNavMeshVolumes[static_cast<std::size_t>(i)],
          g_selectionType == 4 && g_selectedIdx == 0 && i == m_editorSelectedNavVolume,
          g_navMeshVolumeGizmos[static_cast<std::size_t>(i)]);
    }
    if (g_splineGizmos.size() < g_splines.size()) {
      g_splineGizmos.resize(g_splines.size());
    }
    for (int i = 0; i < static_cast<int>(g_splines.size()); ++i)
      DrawEditorSpline(
          m_lines,
          cam.VP,
          g_splines[static_cast<std::size_t>(i)],
          (g_selectionType == 5 || g_selectionType == 7) && i == g_selectedIdx,
          g_selectionType == 7 && i == g_selectedIdx ? g_selectedSplinePoint : -1,
          g_splineGizmos[static_cast<std::size_t>(i)]);
  }

  // Grid
  if (m_lines.IsReady())
    m_grid.Draw(m_lines, cam.VP);

  const bool navAuthored = m_editorNavMeshAuthored;
  const bool navVisible = m_editorNavMeshVisible;
  const bool navWire = m_editorNavMeshShowWire;
  const bool navReady = m_editorNavMesh.IsReady();
  const bool navRendererReady = m_editorNavMeshDebugRenderer.IsReady();
  const bool navHasDepth = overlayOpaqueDepth || overlayForwardDepth;
  const bool navCanDraw = navAuthored && navVisible && navWire && navReady && navRendererReady && navHasDepth;

  if (navCanDraw) {
    m_editorNavMeshDebugRenderer.SetDepthTexture(overlayOpaqueDepth);
    m_editorNavMeshDebugRenderer.SetSecondaryDepthTexture(overlayForwardDepth);
    m_editorNavMeshDebugRenderer.SetViewport(m_lastW, m_lastH);
    m_editorNavMeshDebugRenderer.SetFarPlane(cam.FPlane);
    m_editorNavMeshDebugRenderer.SetVerticalOffset(m_editorNavMeshDebugOffset);
    m_editorNavMeshDebugRenderer.SetGraphVerticalOffset(m_editorNavMeshDebugOffset + 0.005f);
    const bool pickingNavNode = m_editorNavLinkPickMode != 0;
    m_editorNavMeshDebugRenderer.SetShapeMode(
        pickingNavNode || m_editorNavMeshDebugShapeMode == 1
            ? t850::navigation::NavMeshDebugShapeMode::Nodes
            : t850::navigation::NavMeshDebugShapeMode::Geometry);
    m_editorNavMeshDebugRenderer.SetAuxiliaryGeometryEnabled(!pickingNavNode);
    drv->SetDepthStencilState(t850::BaseDriver::NONE);
    m_editorNavMeshDebugRenderer.Draw(m_editorNavMesh, cam.VP);
    drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
    if (!pickingNavNode) {
      DrawSelectedNavLinkOverlay(overlayOpaqueDepth, overlayForwardDepth, cam);
    }
  }

  std::vector<t850::PhysicsDebugBody> physicsWireBodies;
  std::vector<t850::PhysicsDebugBody> selectedPhysicsWireBodies;
  std::vector<t850::PhysicsDebugBody> globalPhysicsWireBodies;
  for (int i = 0; i < static_cast<int>(g_physicsEntities.size()); ++i) {
    const PhysicsSceneEntity& entity = g_physicsEntities[i];
    if (!entity.visible || !entity.showWire || !entity.body.IsValid()) {
      continue;
    }
    t850::PhysicsDebugBody debugBody;
    if (m_physics.GetDebugBody(entity.body, debugBody)) {
      if ((g_selectionType == 3 && i == g_selectedIdx) || IsMixedSelected(3, i)) {
        selectedPhysicsWireBodies.push_back(std::move(debugBody));
      } else {
        physicsWireBodies.push_back(std::move(debugBody));
      }
    }
  }
  const bool drawGlobalPhysicsBodies = m_editorShowPhysics || ShouldDrawPhysicsDebug();
  if (drawGlobalPhysicsBodies) {
    std::vector<t850::PhysicsDebugBody> allPhysicsBodies;
    if (m_physics.GetDebugBodies(allPhysicsBodies)) {
      for (t850::PhysicsDebugBody& body : allPhysicsBodies) {
        const auto authoredBody = std::find_if(
            g_physicsEntities.begin(),
            g_physicsEntities.end(),
            [&](const PhysicsSceneEntity& entity) {
              return entity.body.IsValid() && entity.body.value == body.state.handle.value;
            });
        if (authoredBody != g_physicsEntities.end()) {
          continue;
        }
        globalPhysicsWireBodies.push_back(std::move(body));
      }
    }
  }
  if (m_physicsDebug.IsReady() &&
      (overlayOpaqueDepth || overlayForwardDepth) &&
      (!globalPhysicsWireBodies.empty() || !physicsWireBodies.empty() || !selectedPhysicsWireBodies.empty())) {
    m_physicsDebug.SetViewport(m_lastW, m_lastH);
    m_physicsDebug.SetFarPlane(cam.FPlane);
    m_physicsDebug.SetDepthTestEnabled(true);
    m_physicsDebug.SetDepthTexture(overlayOpaqueDepth);
    m_physicsDebug.SetSecondaryDepthTexture(overlayForwardDepth);
    drv->SetDepthStencilState(t850::BaseDriver::NONE);
    if (!globalPhysicsWireBodies.empty()) {
      m_physicsDebug.DrawBodies(globalPhysicsWireBodies, cam.VP);
    }
    if (!physicsWireBodies.empty()) {
      m_physicsDebug.DrawBodies(physicsWireBodies, cam.VP);
    }
    if (!selectedPhysicsWireBodies.empty()) {
      m_physicsDebug.DrawBodies(selectedPhysicsWireBodies, cam.VP, XVECTOR3(0.2f, 0.55f, 1.0f, 1.0f));
    }
    drv->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
  }
}

void EditorApp::DrawEditorUI(t850::BaseDriver* drv) {
  ImGuiNewFrame();
  EditorUndoState imguiUndoBefore;
  std::string imguiUndoBeforeKey;
  const bool trackImguiUndo = !g_applyingUndoState;
  const int undoCountBeforeImgui = g_undoStack.UndoCount();
  if (trackImguiUndo) {
    imguiUndoBefore = CaptureEditorUndoState(&imguiUndoBeforeKey);
  }
  auto commitImguiUndo = [&](const char* label) {
    if (!trackImguiUndo || g_undoStack.UndoCount() != undoCountBeforeImgui) {
      return;
    }
    PushEditorUndoState(label, imguiUndoBefore, imguiUndoBeforeKey, CaptureEditorUndoState(nullptr));
  };

  if (m_meshEditorOpen) {
    DrawMeshEditorWindow();
    commitImguiUndo("Mesh Editor Action");
    T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender...");
    ImGuiRender();
    T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender done");
    return;
  }
  if (m_playSceneOpen) {
    DrawPlaySceneWindow();
    commitImguiUndo("Play Window Action");
    T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender...");
    ImGuiRender();
    T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender done");
    return;
  }

  MenuAction menuAction = ImGuiDrawMenuBar(m_panels);
  if (menuAction.wantsResetLayout) {
    g_resetArtistLayout = true;
    m_panels.showHierarchy = true;
    m_panels.showInspector = true;
    m_panels.showRendering = true;
    m_panels.showConsole = true;
    m_panels.showRTDebug = false;
  }

  int addCamera = -1, addLight = -1;
  bool wantsClone = false, wantsGroup = false, wantsUngroup = false, wantsPlayScene = false;
  int toolbarCameraMode = static_cast<int>(m_editorCameraMode);
  const bool wasNavMeshAuthoringMode = m_editorNavMeshAuthoringMode;
  int mode = ImGuiDrawToolbar((int)m_gizmo.Mode(), addCamera, addLight,
                                wantsClone, wantsGroup, wantsUngroup, wantsPlayScene,
                                g_selectedIdx >= 0, g_multiSelect.size() >= 2,
                                toolbarCameraMode,
                                m_editorNavMeshAuthoringMode);
  m_gizmo.SetMode((GizmoMode)mode);
  if (m_editorNavMeshAuthoringMode) {
    m_panels.showHierarchy = true;
    m_panels.showInspector = true;
    m_editorNavMeshShowSourcePreview = true;
    if (!wasNavMeshAuthoringMode) {
      g_selectionType = 4;
      g_selectedIdx = 0;
      g_selectedSplinePoint = -1;
      ClearMixedSelection();
    }
  }
  toolbarCameraMode = std::clamp(toolbarCameraMode, 0, 1);
  const EditorCameraMode newCameraMode = static_cast<EditorCameraMode>(toolbarCameraMode);
  if (newCameraMode != m_editorCameraMode) {
    m_editorCameraMode = newCameraMode;
    m_editorCameraController.ClearInput();
    m_camera.GetCameraMutable().Velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  }

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
      } else if (g_selectionType == 6 && g_selectedIdx < static_cast<int>(g_lightCameras.size())) {
        AdjustActiveLightCameraAfterErase(g_selectedIdx);
        AdjustGodRaysLightCameraAfterErase(g_selectedIdx);
        if (g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_lightCameraGizmoCameras.size())) {
          if (t850::g_pBaseDriver) {
            t850::g_pBaseDriver->WaitForGPU();
          }
          ReleaseGizmoCache(g_lightCameraGizmoCameras[static_cast<std::size_t>(g_selectedIdx)].gizmo);
          g_lightCameraGizmoCameras.erase(g_lightCameraGizmoCameras.begin() + g_selectedIdx);
        }
        g_lightCameras.erase(g_lightCameras.begin() + g_selectedIdx);
        g_selectedIdx = -1;
      } else if (g_selectionType == 3 && g_selectedIdx < (int)g_physicsEntities.size()) {
        DestroyPhysicsEntity(m_physics, g_selectedIdx);
      } else if (g_selectionType == 4 && g_selectedIdx == 0) {
        if (!DeleteSelectedNavAuthoringChild()) {
          DestroyEditorNavMesh();
        }
      } else if (g_selectionType == 8 && g_selectedIdx == 0) {
        g_godRaysVolume.authored = false;
        g_godRaysVolume.enabled = false;
        g_godRaysVolume.visible = false;
        g_godRaysVolume.clip_enabled = false;
        g_selectedIdx = -1;
        g_selectionType = 0;
      }
    }
    if (ctx.wantsFrameView) {
      SceneObject* sel = SelectedObject();
      if (sel && sel->wireframe.IsLoaded()) {
        m_camera.SetTarget(sel->wireframe.Position());
        m_camera.ResetViewAngle();
      } else if (PhysicsSceneEntity* physicsEntity = SelectedPhysicsEntity()) {
        t850::AABB bounds;
        if (GetPhysicsEntityWorldAABB(*physicsEntity, m_physics, bounds)) {
          m_camera.FrameBounds(bounds);
        } else {
          m_camera.SetTarget(physicsEntity->position);
          m_camera.ResetViewAngle();
        }
      } else if (g_selectionType == 4 && g_selectedIdx == 0) {
        t850::AABB bounds;
        if (GetEditorNavMeshWorldAABB(bounds)) {
          m_camera.FrameBounds(bounds);
        }
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
    ImVec2 mPos = ImGui::GetMousePos();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(g_marqueeStartScreen, mPos, IM_COL32(80, 140, 255, 42), 2.0f);
    dl->AddRect(g_marqueeStartScreen, mPos, IM_COL32(110, 170, 255, 230), 2.0f, 0, 1.5f);
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
    SceneCamera& sc = g_cameras[g_selectedIdx];
    if (sc.type == CameraType::Perspective) {
      if (mode == 0) {
        const XVECTOR3 oldPosition = sc.position;
        if (TranslateCameraPointGizmo(cam2, 0, sc.position)) {
          const XVECTOR3 delta(sc.position.x - oldPosition.x,
                               sc.position.y - oldPosition.y,
                               sc.position.z - oldPosition.z,
                               0.0f);
          sc.target += delta;
        }
        TranslateCameraPointGizmo(cam2, 1, sc.target);
      }
    } else {
      ManipulateCameraEntityGizmo(
          cam2,
          mode,
          sc.position,
          sc.target,
          true,
          sc.orthoW,
          sc.orthoH,
          sc.fovDeg);
    }
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
  else if (g_selectionType == 6 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_lightCameras.size())) {
    t850::scene::SceneLightCameraDesc& lightCamera = g_lightCameras[static_cast<std::size_t>(g_selectedIdx)];
    if (!lightCamera.frozen) {
      XVECTOR3 position(lightCamera.position.x, lightCamera.position.y, lightCamera.position.z, 1.0f);
      XVECTOR3 target(lightCamera.target.x, lightCamera.target.y, lightCamera.target.z, 1.0f);
      bool changed = false;
      if (lightCamera.type == 0) {
        if (mode == 0) {
          const XVECTOR3 oldPosition = position;
          if (TranslateCameraPointGizmo(cam2, 20, position)) {
            const XVECTOR3 delta(position.x - oldPosition.x,
                                 position.y - oldPosition.y,
                                 position.z - oldPosition.z,
                                 0.0f);
            target += delta;
            changed = true;
          }
          changed = TranslateCameraPointGizmo(cam2, 21, target) || changed;
        }
      } else {
        changed = ManipulateCameraEntityGizmo(
            cam2,
            mode,
            position,
            target,
            true,
            lightCamera.ortho_w,
            lightCamera.ortho_h,
            lightCamera.fov_deg);
      }
      if (changed) {
        lightCamera.position = {position.x, position.y, position.z};
        lightCamera.target = {target.x, target.y, target.z};
      }
    }
  }
  else if (g_selectionType == 7 &&
           g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_splines.size()) &&
           g_selectedSplinePoint >= 0 &&
           g_selectedSplinePoint < static_cast<int>(g_splines[static_cast<std::size_t>(g_selectedIdx)].points.size())) {
    t850::scene::SceneSplineDesc& spline = g_splines[static_cast<std::size_t>(g_selectedIdx)];
    if (!spline.frozen) {
      t850::scene::SceneSplinePointDesc& point = spline.points[static_cast<std::size_t>(g_selectedSplinePoint)];
      XMATRIX44 worldMat;
      XMatTranslation(worldMat, point.position.x, point.position.y, point.position.z);
      const bool manipulated = ImGuizmo::Manipulate(
          &cam2.View.m[0][0], &cam2.Projection.m[0][0],
          ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
          &worldMat.m[0][0], nullptr);
      if (manipulated) {
        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], t, r, s);
        point.position = {t[0], t[1], t[2]};
      }
    }
  }
  else if (g_selectionType == 4 &&
           g_selectedIdx == 0 &&
           m_editorSelectedNavVolume >= 0 &&
           m_editorSelectedNavVolume < static_cast<int>(m_editorNavMeshVolumes.size())) {
    t850::scene::SceneNavMeshVolumeDesc& volume =
        m_editorNavMeshVolumes[static_cast<std::size_t>(m_editorSelectedNavVolume)];
    if (!volume.frozen) {
      XMATRIX44 worldMat = BuildNavMeshVolumeWorld(volume);
      const bool manipulated = ImGuizmoManipulate(
          &cam2.View.m[0][0], &cam2.Projection.m[0][0],
          mode, &worldMat.m[0][0]);
      if (manipulated) {
        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], t, r, s);
        volume.position = {t[0], t[1], t[2]};
        volume.rotation = {r[0] * kDegToRad, r[1] * kDegToRad, r[2] * kDegToRad};
        if (mode == 2) {
          volume.half_extents = {
              (std::max)(0.001f, std::abs(s[0])),
              (std::max)(0.001f, std::abs(s[1])),
              (std::max)(0.001f, std::abs(s[2]))};
        }
        MarkEditorNavMeshDirty("NavMesh volume transformed. Click Re-generate.");
      }
    }
  }
  else if (g_selectionType == 8 && g_selectedIdx == 0 && !g_godRaysVolume.frozen) {
    XMATRIX44 worldMat;
    XMatScaling(worldMat,
                (std::max)(0.001f, g_godRaysVolume.half_extents.x),
                (std::max)(0.001f, g_godRaysVolume.half_extents.y),
                (std::max)(0.001f, g_godRaysVolume.half_extents.z));
    worldMat.m[3][0] = g_godRaysVolume.position.x;
    worldMat.m[3][1] = g_godRaysVolume.position.y;
    worldMat.m[3][2] = g_godRaysVolume.position.z;
    const ImGuizmo::OPERATION operation = mode == 2 ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
    const bool manipulated = ImGuizmo::Manipulate(
        &cam2.View.m[0][0], &cam2.Projection.m[0][0],
        operation, ImGuizmo::WORLD,
        &worldMat.m[0][0], nullptr);
    if (manipulated) {
      float t[3], r[3], s[3];
      ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], t, r, s);
      g_godRaysVolume.position = {t[0], t[1], t[2]};
      if (mode == 2) {
        g_godRaysVolume.half_extents = {
            (std::max)(0.001f, std::abs(s[0])),
            (std::max)(0.001f, std::abs(s[1])),
            (std::max)(0.001f, std::abs(s[2]))};
      }
    }
  }
  else if (g_selectionType == 3 && g_selectedIdx >= 0 && g_selectedIdx < (int)g_physicsEntities.size()) {
    PhysicsSceneEntity& entity = g_physicsEntities[g_selectedIdx];
    if (!entity.frozen && IsCharacterPhysicsEntity(entity)) {
      bool isUsingNow = ImGuizmo::IsUsing();
      if (isUsingNow && !g_gizmoDragging) {
        g_gizmoDragging = true;
        g_physicsGizmoStartHalfExtents = entity.playerHalfExtents;
        g_physicsGizmoStartRadius = entity.playerRadius;
        g_physicsGizmoStartHalfHeight = entity.playerHalfHeight;
      }
      XMATRIX44 worldMat = MakePhysicsGizmoTransform(entity);
      bool manipulated = ImGuizmoManipulate(
        &cam2.View.m[0][0], &cam2.Projection.m[0][0],
        mode, &worldMat.m[0][0]);
      if (manipulated) {
        float translation[3], rotation[3], scale[3];
        ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], translation, rotation, scale);
        entity.position = XVECTOR3(translation[0], translation[1], translation[2], 1.0f);
        entity.eulerRadians = XVECTOR3(rotation[0] * kDegToRad, rotation[1] * kDegToRad, rotation[2] * kDegToRad, 0.0f);
        if (mode == 2) {
          const float sx = std::fabs(scale[0]) > kMinEditableScale ? std::fabs(scale[0]) : 1.0f;
          const float sy = std::fabs(scale[1]) > kMinEditableScale ? std::fabs(scale[1]) : 1.0f;
          const float sz = std::fabs(scale[2]) > kMinEditableScale ? std::fabs(scale[2]) : 1.0f;
          if (entity.playerShape == t850::PhysicsShapeType::Capsule || entity.playerShape == t850::PhysicsShapeType::Cylinder) {
            entity.playerRadius = (std::max)(0.001f, g_physicsGizmoStartRadius * (sx + sz) * 0.5f);
            entity.playerHalfHeight = (std::max)(0.001f, g_physicsGizmoStartHalfHeight * sy);
          } else if (entity.playerShape == t850::PhysicsShapeType::Sphere) {
            entity.playerRadius = (std::max)(0.001f, g_physicsGizmoStartRadius * (sx + sy + sz) / 3.0f);
          } else {
            entity.playerHalfExtents.x = (std::max)(0.001f, g_physicsGizmoStartHalfExtents.x * sx);
            entity.playerHalfExtents.y = (std::max)(0.001f, g_physicsGizmoStartHalfExtents.y * sy);
            entity.playerHalfExtents.z = (std::max)(0.001f, g_physicsGizmoStartHalfExtents.z * sz);
          }
          worldMat = MakePhysicsTransform(entity.position, entity.eulerRadians);
        }
        RecreateCharacterPhysicsBody(m_physics, entity);
      }
      if (!isUsingNow && g_gizmoDragging) {
        g_gizmoDragging = false;
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
      const float margin = 12.0f;
      const ImGuiCond layoutCond = g_resetArtistLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
      const float width = (std::min)(400.0f, (std::max)(340.0f, viewport->WorkSize.x * 0.24f));
      const float height = (std::max)(480.0f, viewport->WorkSize.y - 260.0f);
      ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + margin), layoutCond);
      ImGui::SetNextWindowSize(ImVec2(width, height), layoutCond);
    }
    if (ImGui::Begin("Scene Hierarchy", &m_panels.showHierarchy, ImGuiWindowFlags_NoCollapse)) {
      ImGuiClampCurrentWindowToEditorWorkArea();
      if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen)) {
        EnsureInferredGameEntities();
        if (ImGui::TreeNodeEx("Game Entities", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen)) {
          for (int entityIndex = 0; entityIndex < static_cast<int>(g_gameEntities.size()); ++entityIndex) {
            t850::scene::SceneGameEntityDesc& entity = g_gameEntities[static_cast<std::size_t>(entityIndex)];
            ImGui::PushID(entityIndex + 90000);
            ImGui::Checkbox("##entityVis", &entity.visible); ImGui::SameLine();
            ImGui::Checkbox("##entityFrz", &entity.frozen); ImGui::SameLine();
            ImGui::Checkbox("##entityWire", &entity.show_wire); ImGui::SameLine();
            if (entity.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
            const bool entityOpen = ImGui::TreeNodeEx(("[E] " + entity.name).c_str(),
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            if (entity.frozen) ImGui::PopStyleColor();
            if (entityOpen) {
              char nameBuffer[256] = {};
              std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", entity.name.c_str());
              if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
                entity.name = nameBuffer;
              }
              ImGui::TextDisabled("Kind: %s", entity.kind.c_str());

              const int meshIndex = FindSceneObjectIndexByName(entity.mesh_object);
              if (!entity.mesh_object.empty()) {
                ImGui::PushID("mesh-child");
                const std::string meshLabel = "[M] Mesh: " + entity.mesh_object;
                const bool meshOpen = ImGui::TreeNodeEx(meshLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                if (ImGui::IsItemClicked() && meshIndex >= 0) {
                  g_selectedIdx = meshIndex;
                  g_selectionType = 0;
                  if (ImGui::GetIO().KeyShift) {
                    ToggleMixedSelection(0, meshIndex);
                  } else {
                    ClearMixedSelection();
                    AddMixedSelection(0, meshIndex);
                  }
                }
                if (meshOpen) ImGui::TreePop();
                ImGui::PopID();
              }

              if (!entity.camera.empty()) {
                ImGui::PushID("camera-child");
                const bool cameraOpen = ImGui::TreeNodeEx(("[C] Camera: " + entity.camera).c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                if (cameraOpen) ImGui::TreePop();
                ImGui::PopID();
              }

              for (int physicsRefIndex = 0; physicsRefIndex < static_cast<int>(entity.physics_entities.size()); ++physicsRefIndex) {
                const std::string& physicsName = entity.physics_entities[static_cast<std::size_t>(physicsRefIndex)];
                const int physicsIndex = FindPhysicsEntityIndexByName(physicsName);
                ImGui::PushID(physicsRefIndex + 91000);
                const std::string physicsLabel = "[P] Physics: " + physicsName;
                const bool physicsOpen = ImGui::TreeNodeEx(physicsLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                if (ImGui::IsItemClicked() && physicsIndex >= 0) {
                  g_selectedIdx = physicsIndex;
                  g_selectionType = 3;
                  if (ImGui::GetIO().KeyShift) {
                    ToggleMixedSelection(3, physicsIndex);
                  } else {
                    ClearMixedSelection();
                    AddMixedSelection(3, physicsIndex);
                  }
                }
                if (physicsOpen) ImGui::TreePop();
                ImGui::PopID();
              }

              const int ragdollObjectIndex = FindSceneObjectIndexByName(entity.ragdoll_object);
              if (ragdollObjectIndex >= 0) {
                SceneObject& ragdollObject = g_objects[static_cast<std::size_t>(ragdollObjectIndex)];
                EnsureRagdollHierarchyState(ragdollObject);
                if (ImGui::TreeNodeEx(("[R] Ragdoll: " + entity.ragdoll_object).c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen)) {
                  if (ragdollObject.ragdollAuthoringReady &&
                      !ragdollObject.ragdollAuthoring.binding.referencePose.bones.empty()) {
                    const int bodyCount = static_cast<int>(ragdollObject.ragdollAuthoring.binding.referencePose.bones.size());
                    for (int bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex) {
                      ImGui::PushID(bodyIndex + 92000);
                      bool bodyVisible = ragdollObject.ragdollBodyVisible[static_cast<std::size_t>(bodyIndex)] != 0;
                      bool bodyWire = ragdollObject.ragdollBodyWire[static_cast<std::size_t>(bodyIndex)] != 0;
                      bool bodyFrozen = ragdollObject.ragdollAuthoring.frozenBodies[static_cast<std::size_t>(bodyIndex)] != 0;
                      ImGui::Checkbox("##geBodyVis", &bodyVisible); ImGui::SameLine();
                      ImGui::Checkbox("##geBodyFrz", &bodyFrozen); ImGui::SameLine();
                      ImGui::Checkbox("##geBodyWire", &bodyWire); ImGui::SameLine();
                      const bool bodyOpen = ImGui::TreeNodeEx(("[C] Capsule " + std::to_string(bodyIndex) + " " + RagdollHierarchyBodyLabel(ragdollObject, bodyIndex)).c_str(),
                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                      ragdollObject.ragdollBodyVisible[static_cast<std::size_t>(bodyIndex)] = bodyVisible ? 1 : 0;
                      ragdollObject.ragdollBodyWire[static_cast<std::size_t>(bodyIndex)] = bodyWire ? 1 : 0;
                      ragdollObject.ragdollAuthoring.frozenBodies[static_cast<std::size_t>(bodyIndex)] = bodyFrozen ? 1 : 0;
                      if (bodyOpen) ImGui::TreePop();
                      ImGui::PopID();

                      ImGui::PushID(bodyIndex + 93000);
                      bool jointVisible = ragdollObject.ragdollJointVisible[static_cast<std::size_t>(bodyIndex)] != 0;
                      bool jointWire = ragdollObject.ragdollJointWire[static_cast<std::size_t>(bodyIndex)] != 0;
                      bool jointFrozen = ragdollObject.ragdollAuthoring.frozenJoints[static_cast<std::size_t>(bodyIndex)] != 0;
                      ImGui::Checkbox("##geJointVis", &jointVisible); ImGui::SameLine();
                      ImGui::Checkbox("##geJointFrz", &jointFrozen); ImGui::SameLine();
                      ImGui::Checkbox("##geJointWire", &jointWire); ImGui::SameLine();
                      const bool jointOpen = ImGui::TreeNodeEx(("[J] Joint " + std::to_string(bodyIndex)).c_str(),
                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                      ragdollObject.ragdollJointVisible[static_cast<std::size_t>(bodyIndex)] = jointVisible ? 1 : 0;
                      ragdollObject.ragdollJointWire[static_cast<std::size_t>(bodyIndex)] = jointWire ? 1 : 0;
                      ragdollObject.ragdollAuthoring.frozenJoints[static_cast<std::size_t>(bodyIndex)] = jointFrozen ? 1 : 0;
                      if (jointOpen) ImGui::TreePop();
                      ImGui::PopID();
                    }
                  } else {
                    ImGui::TextDisabled("Ragdoll authoring not loaded.");
                  }
                  ImGui::TreePop();
                }
              }

              if (!entity.ai.empty()) {
                ImGui::TextDisabled("AI: %s", entity.ai.c_str());
              }
              ImGui::TreePop();
            }
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Scene objects", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen)) {
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

          auto drawMeshHierarchyChildren = [&](int objectIndex, SceneObject& object) {
            ImGui::PushID(objectIndex + 70000);
            bool boundsOpen = ImGui::TreeNodeEx("[B] Mesh Bounds", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::SameLine();
            ImGui::Checkbox("##boundsVis", &object.visible); ImGui::SameLine();
            ImGui::Checkbox("##boundsFrz", &object.frozen); ImGui::SameLine();
            ImGui::Checkbox("##boundsWire", &object.showWire);
            if (boundsOpen) ImGui::TreePop();

            if (object.ragdollAuthoringReady &&
                !object.ragdollAuthoring.binding.referencePose.bones.empty()) {
              EnsureRagdollHierarchyState(object);
              if (ImGui::TreeNodeEx("[R] Ragdoll", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Eye = Show   F = Freeze   W = Wire");
                const int bodyCount = static_cast<int>(object.ragdollAuthoring.binding.referencePose.bones.size());
                for (int bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex) {
                  ImGui::PushID(bodyIndex + 71000);
                  bool bodyVisible = object.ragdollBodyVisible[static_cast<std::size_t>(bodyIndex)] != 0;
                  bool bodyWire = object.ragdollBodyWire[static_cast<std::size_t>(bodyIndex)] != 0;
                  bool bodyFrozen = object.ragdollAuthoring.frozenBodies[static_cast<std::size_t>(bodyIndex)] != 0;
                  ImGui::Checkbox("##bodyVis", &bodyVisible); ImGui::SameLine();
                  ImGui::Checkbox("##bodyFrz", &bodyFrozen); ImGui::SameLine();
                  ImGui::Checkbox("##bodyWire", &bodyWire); ImGui::SameLine();
                  const std::string bodyLabel = "[C] Capsule " + std::to_string(bodyIndex) + " " + RagdollHierarchyBodyLabel(object, bodyIndex);
                  const bool bodyOpen = ImGui::TreeNodeEx(bodyLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                  object.ragdollBodyVisible[static_cast<std::size_t>(bodyIndex)] = bodyVisible ? 1 : 0;
                  object.ragdollBodyWire[static_cast<std::size_t>(bodyIndex)] = bodyWire ? 1 : 0;
                  object.ragdollAuthoring.frozenBodies[static_cast<std::size_t>(bodyIndex)] = bodyFrozen ? 1 : 0;
                  if (bodyOpen) ImGui::TreePop();
                  ImGui::PopID();

                  ImGui::PushID(bodyIndex + 72000);
                  bool jointVisible = object.ragdollJointVisible[static_cast<std::size_t>(bodyIndex)] != 0;
                  bool jointWire = object.ragdollJointWire[static_cast<std::size_t>(bodyIndex)] != 0;
                  bool jointFrozen = object.ragdollAuthoring.frozenJoints[static_cast<std::size_t>(bodyIndex)] != 0;
                  ImGui::Checkbox("##jointVis", &jointVisible); ImGui::SameLine();
                  ImGui::Checkbox("##jointFrz", &jointFrozen); ImGui::SameLine();
                  ImGui::Checkbox("##jointWire", &jointWire); ImGui::SameLine();
                  const std::string jointLabel = "[J] Joint " + std::to_string(bodyIndex);
                  const bool jointOpen = ImGui::TreeNodeEx(jointLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth);
                  object.ragdollJointVisible[static_cast<std::size_t>(bodyIndex)] = jointVisible ? 1 : 0;
                  object.ragdollJointWire[static_cast<std::size_t>(bodyIndex)] = jointWire ? 1 : 0;
                  object.ragdollAuthoring.frozenJoints[static_cast<std::size_t>(bodyIndex)] = jointFrozen ? 1 : 0;
                  if (jointOpen) ImGui::TreePop();
                  ImGui::PopID();
                }
                ImGui::TreePop();
              }
            }
            ImGui::PopID();
          };

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
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (g_multiSelect.count(idx)) flags |= ImGuiTreeNodeFlags_Selected;
                bool nodeOpen = ImGui::TreeNodeEx(o.name.c_str(), flags);
                if (ImGui::IsItemClicked()) {
                  g_selectedIdx = idx; g_selectionType = 0;
                  if (ImGui::GetIO().KeyShift) {
                    ToggleMixedSelection(0, idx);
                  } else {
                    ClearMixedSelection();
                    AddMixedSelection(0, idx);
                  }
                }
                if (nodeOpen) {
                  drawMeshHierarchyChildren(idx, o);
                  ImGui::TreePop();
                }
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
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if ((g_selectionType == 0 && i == g_selectedIdx) || g_multiSelect.count(i))
              flags |= ImGuiTreeNodeFlags_Selected;
            if (o.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
            bool nodeOpen = ImGui::TreeNodeEx(o.name.c_str(), flags);
            if (ImGui::IsItemClicked() && !o.frozen) {
              if (!ImGui::GetIO().KeyShift && g_selectionType == 0 && g_selectedIdx == i) {
                g_selectedIdx = -1;
                ClearMixedSelection();
              } else {
                g_selectedIdx = i; g_selectionType = 0;
                if (ImGui::GetIO().KeyShift) {
                  ToggleMixedSelection(0, i);
                } else {
                  ClearMixedSelection();
                  AddMixedSelection(0, i);
                }
              }
            }
            if (o.frozen) ImGui::PopStyleColor();
            if (nodeOpen) {
              drawMeshHierarchyChildren(i, o);
              ImGui::TreePop();
            }
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
        // Cameras
        if (ImGui::TreeNodeEx("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::PushID("PhysicsBulkControls");
          if (ImGui::SmallButton("Create / Select Player")) {
            const ::Camera* spawnCamera = m_sceneProps.GetPrimaryCamera();
            const XVECTOR3 spawnPosition = spawnCamera ? spawnCamera->Eye : m_camera.GetCameraMutable().Eye;
            CreateOrSelectPlayerPhysicsEntity(m_physics, spawnPosition);
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Destroy all") && !g_physicsEntities.empty()) {
            DestroyAllPhysicsEntities(m_physics);
          }
          if (ImGui::SmallButton("Show all")) {
            for (PhysicsSceneEntity& entity : g_physicsEntities) entity.visible = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Hide all")) {
            for (PhysicsSceneEntity& entity : g_physicsEntities) entity.visible = false;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Freeze all")) {
            for (PhysicsSceneEntity& entity : g_physicsEntities) entity.frozen = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Unfreeze all")) {
            for (PhysicsSceneEntity& entity : g_physicsEntities) entity.frozen = false;
          }
          if (ImGui::SmallButton("Wire all")) {
            for (PhysicsSceneEntity& entity : g_physicsEntities) entity.showWire = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Wire none")) {
            for (PhysicsSceneEntity& entity : g_physicsEntities) entity.showWire = false;
          }
          ImGui::TextDisabled("Eye = Show   F = Freeze   W = Wire");
          ImGui::Separator();
          ImGui::PopID();

          for (int i = 0; i < static_cast<int>(g_physicsEntities.size()); ++i) {
            PhysicsSceneEntity& entity = g_physicsEntities[i];
            ImGui::PushID(i + 50000);
            ImGui::Checkbox("##vis", &entity.visible); ImGui::SameLine();
            ImGui::Checkbox("##frz", &entity.frozen); ImGui::SameLine();
            ImGui::Checkbox("##wir", &entity.showWire); ImGui::SameLine();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if ((g_selectionType == 3 && i == g_selectedIdx) || IsMixedSelected(3, i)) flags |= ImGuiTreeNodeFlags_Selected;
            const char* physicsIcon = entity.type == PhysicsSceneEntityType::Player ? "[P] " :
                (entity.type == PhysicsSceneEntityType::Character ? "[C] " : "[J] ");
            const std::string label = std::string(physicsIcon) + entity.name;
            if (entity.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
            const bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked() && !entity.frozen) {
              g_selectedIdx = i;
              g_selectionType = 3;
              if (ImGui::GetIO().KeyShift) {
                ToggleMixedSelection(3, i);
              } else {
                ClearMixedSelection();
                AddMixedSelection(3, i);
              }
            }
            if (entity.frozen) ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton("Destroy")) {
              DestroyPhysicsEntity(m_physics, i);
              if (nodeOpen) ImGui::TreePop();
              ImGui::PopID();
              --i;
              continue;
            }
            if (nodeOpen) ImGui::TreePop();
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
        // Navigation
        if (ImGui::TreeNodeEx("Navigation", ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::PushID("NavigationControls");
          if (ImGui::SmallButton("Create NavMesh")) {
            CreateEditorNavMesh();
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Destroy NavMesh") && m_editorNavMeshAuthored) {
            DestroyEditorNavMesh();
          }
          ImGui::TextDisabled("Eye = Show   F = Freeze   W = Wire");
          ImGui::Separator();
          ImGui::PopID();

          if (m_editorNavMeshAuthored) {
            ImGui::PushID(60000);
            ImGui::Checkbox("##navvis", &m_editorNavMeshVisible); ImGui::SameLine();
            ImGui::Checkbox("##navfrz", &m_editorNavMeshFrozen);  ImGui::SameLine();
            ImGui::Checkbox("##navwir", &m_editorNavMeshShowWire); ImGui::SameLine();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (g_selectionType == 4 && g_selectedIdx == 0) flags |= ImGuiTreeNodeFlags_Selected;
            if (m_editorNavMeshFrozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
            const bool nodeOpen = ImGui::TreeNodeEx("[N] NavMesh", flags);
            if (ImGui::IsItemClicked() && !m_editorNavMeshFrozen) {
              g_selectedIdx = 0;
              g_selectionType = 4;
              m_editorSelectedNavVolume = -1;
              m_editorSelectedNavLink = -1;
              ClearMixedSelection();
            }
            if (m_editorNavMeshFrozen) ImGui::PopStyleColor();
            if (nodeOpen) {
              if (!m_editorNavMeshVolumes.empty() &&
                  ImGui::TreeNodeEx("Volumes", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int volumeIndex = 0; volumeIndex < static_cast<int>(m_editorNavMeshVolumes.size()); ++volumeIndex) {
                  t850::scene::SceneNavMeshVolumeDesc& volume = m_editorNavMeshVolumes[static_cast<std::size_t>(volumeIndex)];
                  ImGui::PushID(volumeIndex + 60500);
                  ImGui::Checkbox("##volumeVis", &volume.visible); ImGui::SameLine();
                  ImGui::Checkbox("##volumeFrz", &volume.frozen); ImGui::SameLine();
                  ImGui::Checkbox("##volumeWir", &volume.show_wire); ImGui::SameLine();
                  ImGuiTreeNodeFlags volumeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
                  if (volumeIndex == m_editorSelectedNavVolume) volumeFlags |= ImGuiTreeNodeFlags_Selected;
                  if (volume.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
                  const std::string volumeLabel = "[V] " + volume.name + " (" + volume.type + ")";
                  const bool volumeOpen = ImGui::TreeNodeEx(volumeLabel.c_str(), volumeFlags);
                  if (ImGui::IsItemClicked() && !volume.frozen) {
                    m_editorSelectedNavVolume = volumeIndex;
                    m_editorSelectedNavLink = -1;
                    g_selectedIdx = 0;
                    g_selectionType = 4;
                    ClearMixedSelection();
                  }
                  if (volume.frozen) ImGui::PopStyleColor();
                  if (volumeOpen) ImGui::TreePop();
                  ImGui::PopID();
                }
                ImGui::TreePop();
              }
              if (!m_editorNavMeshLinks.empty() &&
                  ImGui::TreeNodeEx("Links", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int linkIndex = 0; linkIndex < static_cast<int>(m_editorNavMeshLinks.size()); ++linkIndex) {
                  t850::scene::SceneNavMeshLinkDesc& link = m_editorNavMeshLinks[static_cast<std::size_t>(linkIndex)];
                  ImGui::PushID(linkIndex + 61000);
                  ImGui::Checkbox("##linkVis", &link.visible); ImGui::SameLine();
                  ImGui::Checkbox("##linkFrz", &link.frozen); ImGui::SameLine();
                  ImGui::Checkbox("##linkWir", &link.show_wire); ImGui::SameLine();
                  ImGuiTreeNodeFlags linkFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
                  if (linkIndex == m_editorSelectedNavLink) linkFlags |= ImGuiTreeNodeFlags_Selected;
                  if (link.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
                  const std::string linkLabel = "[L] " + link.name + " (" + link.type + ")";
                  const bool linkOpen = ImGui::TreeNodeEx(linkLabel.c_str(), linkFlags);
                  if (ImGui::IsItemClicked() && !link.frozen) {
                    m_editorSelectedNavLink = linkIndex;
                    m_editorSelectedNavVolume = -1;
                    g_selectedIdx = 0;
                    g_selectionType = 4;
                    ClearMixedSelection();
                  }
                  if (link.frozen) ImGui::PopStyleColor();
                  if (linkOpen) ImGui::TreePop();
                  ImGui::PopID();
                }
                ImGui::TreePop();
              }
              ImGui::TreePop();
            }
            ImGui::PopID();
          } else {
            ImGui::TextDisabled("No authored NavMesh.");
          }
          ImGui::TreePop();
        }
        // Splines
        if (ImGui::TreeNodeEx("Splines", ImGuiTreeNodeFlags_DefaultOpen)) {
          if (ImGui::SmallButton("Add Spline")) {
            t850::scene::SceneSplineDesc spline;
            spline.name = "Spline " + std::to_string(g_splines.size() + 1);
            spline.points = {
              {{0.0f, 5.0f, -10.0f}, 7.0f},
              {{0.0f, 5.0f, -5.0f}, 7.0f},
              {{0.0f, 5.0f, 5.0f}, 7.0f},
              {{0.0f, 5.0f, 10.0f}, 7.0f}
            };
            g_splines.push_back(std::move(spline));
            g_splineGizmos.emplace_back();
            g_selectionType = 5;
            g_selectedIdx = static_cast<int>(g_splines.size()) - 1;
            g_selectedSplinePoint = -1;
            ClearMixedSelection();
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Show all")) {
            for (auto& spline : g_splines) spline.visible = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Hide all")) {
            for (auto& spline : g_splines) spline.visible = false;
          }
          ImGui::TextDisabled("Eye = Show   F = Freeze   W = Wire");
          ImGui::Separator();
          for (int i = 0; i < static_cast<int>(g_splines.size()); ++i) {
            auto& spline = g_splines[static_cast<std::size_t>(i)];
            ImGui::PushID(i + 70000);
            ImGui::Checkbox("##splineVis", &spline.visible); ImGui::SameLine();
            ImGui::Checkbox("##splineFrz", &spline.frozen); ImGui::SameLine();
            ImGui::Checkbox("##splineWir", &spline.show_wire); ImGui::SameLine();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (g_selectionType == 5 && i == g_selectedIdx) flags |= ImGuiTreeNodeFlags_Selected;
            if (spline.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
            const std::string label = "[S] " + spline.name;
            const bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked() && !spline.frozen) {
              g_selectionType = 5;
              g_selectedIdx = i;
              g_selectedSplinePoint = -1;
              ClearMixedSelection();
            }
            if (spline.frozen) ImGui::PopStyleColor();
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
            bool isDefault = (g_activeCameraIdx == -1);
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
        // Light Cameras
        if (ImGui::TreeNodeEx("Light Cameras", ImGuiTreeNodeFlags_DefaultOpen)) {
          if (ImGui::SmallButton("Add Light Camera")) {
            t850::scene::SceneLightCameraDesc lightCamera;
            lightCamera.name = "Light Camera " + std::to_string(g_lightCameras.size() + 1);
            lightCamera.type = 1;
            lightCamera.position = {25.0f, 100.0f, 0.0f};
            lightCamera.target = {0.0f, 0.0f, 0.0f};
            lightCamera.ortho_w = 130.0f;
            lightCamera.ortho_h = 130.0f;
            lightCamera.far_plane = 600.0f;
            lightCamera.attached_light = g_lights.empty() ? -1 : 0;
            g_lightCameras.push_back(std::move(lightCamera));
            g_selectionType = 6;
            g_selectedIdx = static_cast<int>(g_lightCameras.size()) - 1;
            g_selectedSplinePoint = -1;
            ClearMixedSelection();
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Show all")) {
            for (auto& camera : g_lightCameras) camera.visible = true;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Hide all")) {
            for (auto& camera : g_lightCameras) camera.visible = false;
          }
          ImGui::TextDisabled("Active   Enable   Eye = Show   F = Freeze");
          ImGui::Separator();
          for (int i = 0; i < static_cast<int>(g_lightCameras.size()); ++i) {
            auto& camera = g_lightCameras[static_cast<std::size_t>(i)];
            ImGui::PushID(i + 35000);
            const int encodedActive = EncodeActiveLightCameraIndex(i);
            const bool isActive = (g_activeCameraIdx == encodedActive);
            if (ImGui::RadioButton("##act", isActive))
              g_activeCameraIdx = isActive ? -1 : encodedActive;
            ImGui::SameLine();
            ImGui::Checkbox("##en", &camera.enabled); ImGui::SameLine();
            ImGui::Checkbox("##vis", &camera.visible); ImGui::SameLine();
            ImGui::Checkbox("##frz", &camera.frozen); ImGui::SameLine();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (g_selectionType == 6 && i == g_selectedIdx) flags |= ImGuiTreeNodeFlags_Selected;
            if (camera.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
            const char* icon = (camera.type == 1) ? "[LO] " : "[LP] ";
            const std::string label = icon + camera.name;
            const bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked() && !camera.frozen) {
              g_selectionType = 6;
              g_selectedIdx = i;
              g_selectedSplinePoint = -1;
              ClearMixedSelection();
            }
            if (camera.frozen) ImGui::PopStyleColor();
            if (nodeOpen) ImGui::TreePop();
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
        // Rendering Volumes
        if (RenderGraphSupportsGodRays(g_renderGraph) && GodRaysVolumeIsAuthored(g_godRaysVolume) &&
            ImGui::TreeNodeEx("Rendering Volumes", ImGuiTreeNodeFlags_DefaultOpen)) {
          const bool volumeCanBeActive = GodRaysVolumeCanBeActive(g_godRaysVolume, g_renderGraph);
          if (!volumeCanBeActive) ImGui::BeginDisabled();
          ImGui::Checkbox("##godRaysVolumeEnabled", &g_godRaysVolume.enabled); ImGui::SameLine();
          ImGui::Checkbox("##godRaysVolumeVisible", &g_godRaysVolume.visible); ImGui::SameLine();
          if (!volumeCanBeActive) ImGui::EndDisabled();
          ImGui::Checkbox("##godRaysVolumeFrozen", &g_godRaysVolume.frozen); ImGui::SameLine();
          ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
          if (g_selectionType == 8) flags |= ImGuiTreeNodeFlags_Selected;
          if (g_godRaysVolume.frozen) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1));
          const bool nodeOpen = ImGui::TreeNodeEx("[GR] God Rays Volume", flags);
          if (ImGui::IsItemClicked() && !g_godRaysVolume.frozen && volumeCanBeActive) {
            g_selectionType = 8;
            g_selectedIdx = 0;
            g_selectedSplinePoint = -1;
            ClearMixedSelection();
          }
          if (g_godRaysVolume.frozen) ImGui::PopStyleColor();
          if (nodeOpen) ImGui::TreePop();
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
        ImGui::TreePop(); // Scene objects
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
      const float margin = 12.0f;
      const ImGuiCond layoutCond = g_resetArtistLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
      const float width = (std::min)(440.0f, (std::max)(360.0f, viewport->WorkSize.x * 0.27f));
      const float height = (std::max)(480.0f, viewport->WorkSize.y * 0.62f);
      ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - margin,
                                     viewport->WorkPos.y + margin), layoutCond);
      ImGui::SetNextWindowSize(ImVec2(width, height), layoutCond);
    }
    if (ImGui::Begin("Properties", &m_panels.showInspector, ImGuiWindowFlags_NoCollapse)) {
      ImGuiClampCurrentWindowToEditorWorkArea();
      if (g_selectionType == 0 && sel) {
        const bool selectedIsSkinned = sel->litInst.GetSkinnedMesh() != nullptr &&
            sel->litInst.GetSkinnedMesh()->HasSkinData();
        const int selectedMeshIndex = static_cast<int>(sel - g_objects.data());
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
        ImGui::Checkbox("View Orientation", &sel->showOrientation);
        DrawMeshCharacterOrientationMatchControls(m_physics, selectedMeshIndex);

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

        if (selectedIsSkinned && ImGui::CollapsingHeader("Nav Agent Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
          const char* targetModes[] = { "direct", "formation", "random", "furthest" };
          comboString("Target Mode", sel->navAgentTargetMode, targetModes, static_cast<int>(sizeof(targetModes) / sizeof(targetModes[0])));
          ImGui::TextDisabled("Direct chases the player position. Formation uses the scene-authored slot and offsets.");
          float visualFrontYaw = sel->navAgentFrontYawOffsetDeg.value_or(0.0f);
          if (ImGui::DragFloat("Visual Front Yaw Offset", &visualFrontYaw, 0.5f, -720.0f, 720.0f, "%.2f deg")) {
            sel->navAgentFrontYawOffsetDeg = visualFrontYaw;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Clear##frontYaw")) {
            sel->navAgentFrontYawOffsetDeg.reset();
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Flip 180##frontYaw")) {
            sel->navAgentFrontYawOffsetDeg = sel->navAgentFrontYawOffsetDeg.value_or(0.0f) + 180.0f;
          }
          int faceSign = sel->navAgentFaceYawSign.value_or(1.0f) < 0.0f ? 1 : 0;
          const char* faceSignOptions[] = { "Normal", "Inverted" };
          if (ImGui::Combo("Face Yaw Sign", &faceSign, faceSignOptions, 2)) {
            sel->navAgentFaceYawSign = faceSign == 1 ? -1.0f : 1.0f;
          }
          ImGui::TextDisabled("Use Visual Front Yaw Offset when the asset's actual forward is not local +Z.");
          if (sel->navAgentTargetMode == "formation") {
            ImGui::DragInt("Formation Slot", &sel->navAgentSlot, 1.0f, 0, 64);
            ImGui::DragFloat("Follow Distance", &sel->navAgentFollowDistance, 0.05f, 0.0f, 64.0f, "%.2f");
            ImGui::DragFloat("Side Offset", &sel->navAgentSideOffset, 0.05f, -64.0f, 64.0f, "%.2f");
            ImGui::DragFloat("Depth Step", &sel->navAgentFormationDepthStep, 0.05f, -64.0f, 64.0f, "%.2f");
          } else {
            sel->navAgentSlot = -1;
          }
          if (sel->navAgentTargetMode != "direct" &&
              sel->navAgentTargetMode != "formation" &&
              sel->navAgentTargetMode != "random" &&
              sel->navAgentTargetMode != "furthest") {
            sel->navAgentTargetMode = "direct";
          }
        }

        if (ImGui::CollapsingHeader("Physics Authoring", ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::TextWrapped("Create authored Jolt physics from the selected render mesh. Triangle meshes are static collision; characters use the same shape and Character/CharacterVirtual settings as the player.");
          int maxTrianglesPerLeaf = static_cast<int>(g_triangleMeshCookSettings.maxTrianglesPerLeaf);
          if (ImGui::SliderInt("Max Triangles / Leaf", &maxTrianglesPerLeaf, 1, 8)) {
            g_triangleMeshCookSettings.maxTrianglesPerLeaf = static_cast<uint32_t>(maxTrianglesPerLeaf);
          }
          int buildQuality = g_triangleMeshCookSettings.buildQuality == t850::PhysicsMeshBuildQuality::FavorBuildSpeed ? 1 : 0;
          const char* buildQualityOptions[] = { "Favor Runtime Performance", "Favor Build Speed" };
          if (ImGui::Combo("Build Quality", &buildQuality, buildQualityOptions, 2)) {
            g_triangleMeshCookSettings.buildQuality = buildQuality == 1
                ? t850::PhysicsMeshBuildQuality::FavorBuildSpeed
                : t850::PhysicsMeshBuildQuality::FavorRuntimePerformance;
          }
          float activeEdgeCos = g_triangleMeshCookSettings.activeEdgeCosThresholdAngle;
          if (ImGui::DragFloat("Active Edge Cos Threshold", &activeEdgeCos, 0.001f, -1.0f, 1.0f, "%.6f")) {
            g_triangleMeshCookSettings.activeEdgeCosThresholdAngle = activeEdgeCos;
          }
          ImGui::TextDisabled("Default is cos(5 deg)=0.996195. Negative makes all edges active.");
          ImGui::Checkbox("Per-Triangle User Data", &g_triangleMeshCookSettings.perTriangleUserData);
          ImGui::Checkbox("Use Disk Cache", &g_triangleMeshCookSettings.useDiskCache);
          ImGui::DragFloat("Friction", &g_triangleMeshFriction, 0.01f, 0.0f, 10.0f, "%.2f");
          ImGui::DragFloat("Restitution", &g_triangleMeshRestitution, 0.01f, 0.0f, 1.0f, "%.2f");
          ImGui::Checkbox("Sensor", &g_triangleMeshSensor);
          if (ImGui::Button("Create Static Triangle Mesh")) {
            CreateStaticTriangleMeshPhysicsEntity(m_physics, selectedMeshIndex);
          }
          ImGui::SeparatorText("Mesh Character");
          PhysicsSceneEntity& characterTemplate = EnsureMeshCharacterAuthoringTemplate(selectedMeshIndex);
          ImGui::PushID("MeshCharacterAuthoring");
          bool previewChanged = DrawCharacterRuntimePathControl(characterTemplate);
          if (characterTemplate.characterRuntimePath == static_cast<int>(CharacterRuntimePath::Jolt)) {
            ImGui::TextDisabled("Jolt path previews the authored collision object and drives Play through Jolt collision sweeps.");
            previewChanged |= DrawCharacterShapeControls(characterTemplate, true);
            if (ImGui::Button("Fit to Mesh AABB")) {
              if (FitCharacterToSceneObject(characterTemplate, *sel)) {
                previewChanged = true;
              }
            }
            if (ImGui::CollapsingHeader("Jolt Character Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
              previewChanged |= DrawJoltCharacterSettingsControls(characterTemplate);
            }
          } else {
            ImGui::TextDisabled("Kinematic path uses Detour/navmesh movement and projection. Switch to Jolt to edit the character shape/object.");
          }
          if (previewChanged) {
            RebuildMeshCharacterAuthoringPreview();
          }
          if (ImGui::Button(characterTemplate.characterRuntimePath == static_cast<int>(CharacterRuntimePath::Jolt)
              ? "Create Jolt Character"
              : "Create Kinematic Character")) {
            CreateCharacterPhysicsEntity(m_physics, selectedMeshIndex, characterTemplate);
          }
          ImGui::PopID();
          const int physicsCount = CountPhysicsEntitiesForSourceObject(selectedMeshIndex);
          if (physicsCount > 0) {
            ImGui::SameLine();
            if (ImGui::Button("Destroy Mesh Physics")) {
              DestroyPhysicsEntitiesForSourceObject(m_physics, selectedMeshIndex);
            }
            ImGui::TextDisabled("Physics entities for this mesh: %d", physicsCount);
          }
          if (!g_triangleMeshStatus.empty()) {
            ImGui::TextWrapped("%s", g_triangleMeshStatus.c_str());
          }
          ImGui::TextDisabled("Jolt MeshShape settings exposed: max triangles per leaf, active edge threshold, per-triangle user data, build quality, disk cache; body settings: friction, restitution, sensor.");
        }

        if (!selectedIsSkinned && ImGui::CollapsingHeader("Navigation Authoring", ImGuiTreeNodeFlags_DefaultOpen)) {
          t850::scene::SceneObjectNavigationDesc& navigation = EnsureNavigationMeta(*sel);
          bool objectNavChanged = false;
          objectNavChanged |= ImGui::Checkbox("Include in NavMesh", &navigation.include);
          objectNavChanged |= ImGui::Checkbox("Walkable", &navigation.walkable);
          objectNavChanged |= ImGui::Checkbox("Static", &navigation.static_object);
          const char* areas[] = { "walkable", "not_walkable", "water", "door", "jump", "custom" };
          objectNavChanged |= comboString("Area", navigation.area, areas, (int)(sizeof(areas) / sizeof(areas[0])));
          objectNavChanged |= ImGui::DragFloat("Cost", &navigation.cost, 0.05f, 0.0f, 100.0f, "%.2f");
          if (objectNavChanged) {
            m_editorNavMeshAuthored = true;
            m_editorNavMeshDirty = true;
            m_editorNavMeshStatus = "Object navigation settings changed. Click Re-generate.";
          }
          ImGui::SeparatorText("Scene NavMesh");
          DrawNavMeshAuthoringPanel();
        }

        if (selectedIsSkinned) {
          DrawSelectedAnimationInspector(*sel);
          DrawRagdollInspector(*sel);
        }
      }
      else if (g_selectionType == 1 && g_selectedIdx < (int)g_cameras.size()) {
        // Camera inspector
        SceneCamera& cam = g_cameras[g_selectedIdx];
        ImGui::SeparatorText("Camera");
        if (ImGui::Button(g_activeCameraIdx == g_selectedIdx
                              ? "Return to Editor Camera"
                              : "View Through Camera")) {
          g_activeCameraIdx = (g_activeCameraIdx == g_selectedIdx) ? -1 : g_selectedIdx;
        }
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
      else if (g_selectionType == 3 && g_selectedIdx < (int)g_physicsEntities.size()) {
        PhysicsSceneEntity& entity = g_physicsEntities[g_selectedIdx];
        ImGui::SeparatorText("Physics Entity");
        ImGui::TextWrapped("%s", entity.name.c_str());
        if (IsCharacterPhysicsEntity(entity)) {
          bool changed = false;
          ImGui::Text("Type: %s", entity.type == PhysicsSceneEntityType::Player ? "Player" : "Character");
          if (entity.type == PhysicsSceneEntityType::Character) {
            ImGui::Text("Source: %s", entity.sourceName.c_str());
            DrawCharacterRuntimePathControl(entity);
          }
          changed |= DrawCharacterShapeControls(entity, true);
          if (entity.type == PhysicsSceneEntityType::Character) {
            const int sourceIndex = (entity.sourceObjectIndex >= 0 &&
                entity.sourceObjectIndex < static_cast<int>(g_objects.size()))
                    ? entity.sourceObjectIndex
                    : FindSceneObjectIndexByName(entity.sourceName);
            if (sourceIndex >= 0 && sourceIndex < static_cast<int>(g_objects.size())) {
              if (ImGui::Button("Fit to Source Mesh AABB")) {
                if (FitCharacterToSceneObject(entity, g_objects[static_cast<std::size_t>(sourceIndex)])) {
                  entity.sourceObjectIndex = sourceIndex;
                  changed = true;
                }
              }
            }
          }
          ImGui::Checkbox("Visible", &entity.visible);
          ImGui::Checkbox("Frozen", &entity.frozen);
          ImGui::Checkbox("Wireframe", &entity.showWire);
          ImGui::Checkbox("View Orientation", &entity.showOrientation);
          if (entity.type == PhysicsSceneEntityType::Player) {
            ImGui::SeparatorText("Bot Spacing");
            ImGui::DragFloat("Radius##playerBotRadius", &entity.playerBotRadius, 0.05f, 0.0f, 128.0f, "%.2f");
            ImGui::TextDisabled("Authored bots target the closest point on this radius around the player.");
          }
          const bool showJoltSettings = entity.type == PhysicsSceneEntityType::Player ||
              entity.characterRuntimePath == static_cast<int>(CharacterRuntimePath::Jolt);
          if (showJoltSettings &&
              ImGui::CollapsingHeader("Jolt Character Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= DrawJoltCharacterSettingsControls(entity);
            ImGui::TextDisabled("These settings are saved with the scene and used by Play for matching authored mesh characters.");
          } else if (entity.type == PhysicsSceneEntityType::Character) {
            ImGui::TextDisabled("Switch Movement Path to Jolt to edit Character / CharacterVirtual settings.");
          }
          if (changed) {
            RecreateCharacterPhysicsBody(m_physics, entity);
          }
          if (ImGui::Button(entity.type == PhysicsSceneEntityType::Player ? "Destroy Player" : "Destroy Character")) {
            DestroyPhysicsEntity(m_physics, g_selectedIdx);
          }
        } else {
          ImGui::Text("Source: %s", entity.sourceName.c_str());
          ImGui::Text("Type: Static Triangle Mesh");
          ImGui::Text("Vertices: %u", entity.stats.vertexCount);
          ImGui::Text("Triangles: %u", entity.stats.triangleCount);
          ImGui::Text("Max Triangles / Leaf: %u", entity.cookSettings.maxTrianglesPerLeaf);
          ImGui::Text("Active Edge Cos Threshold: %.6f", entity.cookSettings.activeEdgeCosThresholdAngle);
          ImGui::Text("Per-Triangle User Data: %s", entity.cookSettings.perTriangleUserData ? "On" : "Off");
          ImGui::Text("Build Quality: %s",
                      entity.cookSettings.buildQuality == t850::PhysicsMeshBuildQuality::FavorBuildSpeed
                          ? "Favor Build Speed"
                          : "Favor Runtime Performance");
          ImGui::Text("Disk Cache: %s", entity.cookSettings.useDiskCache ? "On" : "Off");
          ImGui::Text("Cache: %s", entity.stats.cacheHit ? "Hit" : (entity.stats.cacheSaved ? "Saved" : "Miss/Off"));
          if (!entity.stats.cachePath.empty()) {
            ImGui::TextWrapped("Cache Path: %s", entity.stats.cachePath.c_str());
          }
          ImGui::Text("Friction: %.2f", entity.friction);
          ImGui::Text("Restitution: %.2f", entity.restitution);
          ImGui::Text("Sensor: %s", entity.sensor ? "Yes" : "No");
          ImGui::Text("Cook %.2f ms, cache load %.2f ms, cache save %.2f ms, total %.2f ms",
                      entity.stats.cookMs,
                      entity.stats.cacheLoadMs,
                      entity.stats.cacheSaveMs,
                      entity.stats.totalMs);
          ImGui::Checkbox("Visible", &entity.visible);
          ImGui::Checkbox("Frozen", &entity.frozen);
          ImGui::Checkbox("Wireframe", &entity.showWire);
          if (ImGui::Button("Destroy Static Triangle Mesh")) {
            DestroyPhysicsEntity(m_physics, g_selectedIdx);
          }
        }
      } else if (g_selectionType == 4 && g_selectedIdx == 0) {
        ImGui::SeparatorText("Navigation Mesh");
        DrawNavMeshAuthoringPanel();
      } else if (g_selectionType == 5 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_splines.size())) {
        t850::scene::SceneSplineDesc& spline = g_splines[static_cast<std::size_t>(g_selectedIdx)];
        ImGui::SeparatorText("Spline");
        char nameBuffer[256] = {};
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", spline.name.c_str());
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
          spline.name = nameBuffer;
        }
        ImGui::Checkbox("Visible", &spline.visible);
        ImGui::Checkbox("Frozen", &spline.frozen);
        ImGui::Checkbox("Wireframe", &spline.show_wire);
        ImGui::Checkbox("Looped", &spline.looped);
        ImGui::Checkbox("Play on SceneTemplate start", &spline.play_on_start);
        ImGui::DragFloat("Agent velocity", &spline.agent_velocity, 0.1f, 0.0f, 100.0f, "%.2f");
        ImGui::DragFloat("Agent offset", &spline.agent_offset, 0.1f, 0.0f, 100000.0f, "%.2f");

        std::vector<std::string> cameraOptions;
        cameraOptions.push_back("None");
        for (int cameraIndex = 0; cameraIndex < static_cast<int>(g_cameras.size()); ++cameraIndex) {
          cameraOptions.push_back(std::to_string(cameraIndex) + ": " + g_cameras[static_cast<std::size_t>(cameraIndex)].name);
        }
        int attachedCameraOption = spline.attached_camera >= 0 ? spline.attached_camera + 1 : 0;
        const char* preview = attachedCameraOption >= 0 && attachedCameraOption < static_cast<int>(cameraOptions.size())
            ? cameraOptions[static_cast<std::size_t>(attachedCameraOption)].c_str()
            : "None";
        if (ImGui::BeginCombo("Attached camera", preview)) {
          for (int option = 0; option < static_cast<int>(cameraOptions.size()); ++option) {
            const bool selected = option == attachedCameraOption;
            if (ImGui::Selectable(cameraOptions[static_cast<std::size_t>(option)].c_str(), selected)) {
              attachedCameraOption = option;
              spline.attached_camera = option == 0 ? -1 : option - 1;
            }
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        if (ImGui::Button(m_editorSplinePreviewPlaying && m_editorSplinePreviewIndex == g_selectedIdx
                              ? "Stop Timeline Preview"
                              : "Play Timeline Preview")) {
          if (m_editorSplinePreviewPlaying && m_editorSplinePreviewIndex == g_selectedIdx) {
            m_editorSplinePreviewPlaying = false;
            m_editorSplinePreviewIndex = -1;
          } else {
            if (BuildRuntimeSpline(spline, m_editorSplinePreviewSpline)) {
              m_editorSplinePreviewAgent = t850::SplineAgent{};
              m_editorSplinePreviewAgent.m_pSpline = &m_editorSplinePreviewSpline;
              m_editorSplinePreviewAgent.m_moving = true;
              m_editorSplinePreviewAgent.m_velocity = spline.agent_velocity;
              m_editorSplinePreviewAgent.SetOffset(
                  std::fmod((std::max)(0.0f, spline.agent_offset), m_editorSplinePreviewSpline.m_totalLength));
              m_editorSplinePreviewAgent.m_actualPoint =
                  m_editorSplinePreviewSpline.GetPoint(m_editorSplinePreviewSpline.GetNormalizedOffset(m_editorSplinePreviewAgent.GetOffset()));
              m_editorSplinePreviewPlaying = true;
              m_editorSplinePreviewIndex = g_selectedIdx;
              ApplyEditorSplineAgentToAttachedCamera(spline, m_editorSplinePreviewAgent);
            }
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply Offset to Camera")) {
          ApplyEditorSplineToAttachedCamera(spline);
        }

        ImGui::SeparatorText("Control Points");
        if (ImGui::Button("Add Point")) {
          t850::scene::SceneSplinePointDesc point;
          if (!spline.points.empty()) {
            point = spline.points.back();
            point.position.z += 5.0f;
          }
          spline.points.push_back(point);
        }
        ImGui::SameLine();
        bool deleteSpline = false;
        if (ImGui::Button("Delete Spline")) {
          deleteSpline = true;
        }
        if (deleteSpline) {
          if (m_editorSplinePreviewIndex == g_selectedIdx) {
            m_editorSplinePreviewPlaying = false;
            m_editorSplinePreviewIndex = -1;
          }
          if (g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_splineGizmos.size())) {
            if (t850::g_pBaseDriver) {
              t850::g_pBaseDriver->WaitForGPU();
            }
            ReleaseGizmoCache(g_splineGizmos[static_cast<std::size_t>(g_selectedIdx)]);
            g_splineGizmos.erase(g_splineGizmos.begin() + g_selectedIdx);
          }
          g_splines.erase(g_splines.begin() + g_selectedIdx);
          g_selectedIdx = -1;
          g_selectedSplinePoint = -1;
          g_selectionType = 0;
        } else {
          for (int pointIndex = 0; pointIndex < static_cast<int>(spline.points.size()); ++pointIndex) {
            t850::scene::SceneSplinePointDesc& point = spline.points[static_cast<std::size_t>(pointIndex)];
            ImGui::PushID(pointIndex);
            const std::string header = "Point " + std::to_string(pointIndex);
            if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
              float pos[3] = {point.position.x, point.position.y, point.position.z};
              if (ImGui::DragFloat3("Position", pos, 0.1f, -100000.0f, 100000.0f, "%.3f")) {
                point.position = {pos[0], pos[1], pos[2]};
              }
              ImGui::DragFloat("Velocity", &point.velocity, 0.1f, 0.0f, 100.0f, "%.2f");
              ImGui::Checkbox("Look at next point", &point.look_at_center);
              float rot[3] = {point.rotation.x, point.rotation.y, point.rotation.z};
              if (ImGui::DragFloat3("Rotation", rot, 0.01f, -1000.0f, 1000.0f, "%.3f")) {
                point.rotation = {rot[0], rot[1], rot[2]};
              }
              if (spline.points.size() > 4 && ImGui::SmallButton("Remove Point")) {
                spline.points.erase(spline.points.begin() + pointIndex);
                ImGui::PopID();
                break;
              }
            }
            ImGui::PopID();
          }
          if (spline.points.size() < 4) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "At least 4 points are needed for Catmull-Rom playback.");
          }
        }
      } else if (g_selectionType == 6 && g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_lightCameras.size())) {
        t850::scene::SceneLightCameraDesc& lightCamera = g_lightCameras[static_cast<std::size_t>(g_selectedIdx)];
        ImGui::SeparatorText("Light Camera");
        char nameBuffer[256] = {};
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", lightCamera.name.c_str());
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
          lightCamera.name = nameBuffer;
        }
        const int encodedActiveLightCamera = EncodeActiveLightCameraIndex(g_selectedIdx);
        if (ImGui::Button(g_activeCameraIdx == encodedActiveLightCamera
                              ? "Return to Editor Camera"
                              : "View Through Light Camera")) {
          g_activeCameraIdx = (g_activeCameraIdx == encodedActiveLightCamera)
              ? -1
              : encodedActiveLightCamera;
        }
        ImGui::Checkbox("Enabled", &lightCamera.enabled);
        ImGui::Checkbox("Visible", &lightCamera.visible);
        ImGui::Checkbox("Frozen", &lightCamera.frozen);

        const char* projectionPreview = lightCamera.type == 1 ? "Orthographic" : "Perspective";
        if (ImGui::BeginCombo("Projection", projectionPreview)) {
          if (ImGui::Selectable("Perspective", lightCamera.type == 0)) lightCamera.type = 0;
          if (ImGui::Selectable("Orthographic", lightCamera.type == 1)) lightCamera.type = 1;
          ImGui::EndCombo();
        }
        float pos[3] = {lightCamera.position.x, lightCamera.position.y, lightCamera.position.z};
        if (ImGui::DragFloat3("Position", pos, 0.1f, -100000.0f, 100000.0f, "%.3f")) {
          lightCamera.position = {pos[0], pos[1], pos[2]};
        }
        float target[3] = {lightCamera.target.x, lightCamera.target.y, lightCamera.target.z};
        if (ImGui::DragFloat3("Target", target, 0.1f, -100000.0f, 100000.0f, "%.3f")) {
          lightCamera.target = {target[0], target[1], target[2]};
        }
        if (lightCamera.type == 1) {
          ImGui::DragFloat("Ortho width", &lightCamera.ortho_w, 0.1f, 0.01f, 100000.0f, "%.3f");
          ImGui::DragFloat("Ortho height", &lightCamera.ortho_h, 0.1f, 0.01f, 100000.0f, "%.3f");
        } else {
          ImGui::DragFloat("FOV degrees", &lightCamera.fov_deg, 0.1f, 1.0f, 179.0f, "%.3f");
        }
        ImGui::DragFloat("Near plane", &lightCamera.near_plane, 0.01f, 0.001f, 10000.0f, "%.3f");
        ImGui::DragFloat("Far plane", &lightCamera.far_plane, 0.1f, 0.001f, 1000000.0f, "%.3f");
        ImGui::DragFloat("Yaw rate", &lightCamera.yaw_rate, 0.0001f, -10.0f, 10.0f, "%.6f");

        std::vector<std::string> lightOptions;
        lightOptions.push_back("None");
        for (int lightIndex = 0; lightIndex < static_cast<int>(g_lights.size()); ++lightIndex) {
          lightOptions.push_back(std::to_string(lightIndex) + ": " + g_lights[static_cast<std::size_t>(lightIndex)].name);
        }
        int attachedLightOption = lightCamera.attached_light >= 0 ? lightCamera.attached_light + 1 : 0;
        const char* attachedPreview = attachedLightOption >= 0 && attachedLightOption < static_cast<int>(lightOptions.size())
            ? lightOptions[static_cast<std::size_t>(attachedLightOption)].c_str()
            : "None";
        if (ImGui::BeginCombo("Attached light", attachedPreview)) {
          for (int option = 0; option < static_cast<int>(lightOptions.size()); ++option) {
            const bool selected = option == attachedLightOption;
            if (ImGui::Selectable(lightOptions[static_cast<std::size_t>(option)].c_str(), selected)) {
              attachedLightOption = option;
              lightCamera.attached_light = option == 0 ? -1 : option - 1;
            }
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        ImGui::TextDisabled("SceneTemplate uses the first enabled light camera as the shadow camera source.");
        if (ImGui::Button("Delete Light Camera")) {
          AdjustActiveLightCameraAfterErase(g_selectedIdx);
          AdjustGodRaysLightCameraAfterErase(g_selectedIdx);
          if (g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_lightCameraGizmoCameras.size())) {
            if (t850::g_pBaseDriver) {
              t850::g_pBaseDriver->WaitForGPU();
            }
            ReleaseGizmoCache(g_lightCameraGizmoCameras[static_cast<std::size_t>(g_selectedIdx)].gizmo);
            g_lightCameraGizmoCameras.erase(g_lightCameraGizmoCameras.begin() + g_selectedIdx);
          }
          g_lightCameras.erase(g_lightCameras.begin() + g_selectedIdx);
          g_selectedIdx = -1;
          g_selectionType = 0;
        }
      } else if (g_selectionType == 8 && g_selectedIdx == 0) {
        ImGui::SeparatorText("God Rays Volume");
        const bool renderGraphHasGodRays = RenderGraphSupportsGodRays(g_renderGraph);
        ClampGodRaysVolumeLightCamera(g_godRaysVolume);
        const bool volumeCanBeActive = GodRaysVolumeCanBeActive(g_godRaysVolume, g_renderGraph);
        char nameBuffer[256] = {};
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", g_godRaysVolume.name.c_str());
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
          g_godRaysVolume.name = nameBuffer;
        }
        if (g_lightCameras.size() > 1) {
          const int selectedLightCamera = std::clamp(
              g_godRaysVolume.light_camera,
              0,
              static_cast<int>(g_lightCameras.size()) - 1);
          const std::string preview = g_lightCameras[static_cast<std::size_t>(selectedLightCamera)].name;
          if (ImGui::BeginCombo("Light Camera", preview.c_str())) {
            for (int i = 0; i < static_cast<int>(g_lightCameras.size()); ++i) {
              const auto& lightCamera = g_lightCameras[static_cast<std::size_t>(i)];
              std::string label = lightCamera.name;
              if (!lightCamera.enabled) label += " (disabled)";
              const bool selected = i == g_godRaysVolume.light_camera;
              if (ImGui::Selectable(label.c_str(), selected)) {
                g_godRaysVolume.light_camera = i;
              }
              if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
          }
        } else if (!g_lightCameras.empty()) {
          ImGui::TextDisabled("Light Camera: %s", g_lightCameras.front().name.c_str());
          g_godRaysVolume.light_camera = 0;
        }
        if (!volumeCanBeActive) {
          ImGui::BeginDisabled();
        }
        ImGui::Checkbox("Enabled", &g_godRaysVolume.enabled);
        ImGui::Checkbox("Clip God Rays to Volume", &g_godRaysVolume.clip_enabled);
        ImGui::Checkbox("Visible", &g_godRaysVolume.visible);
        if (!volumeCanBeActive) {
          ImGui::EndDisabled();
        }
        ImGui::Checkbox("Frozen", &g_godRaysVolume.frozen);
        ImGui::Checkbox("Wireframe", &g_godRaysVolume.show_wire);
        float position[3] = {g_godRaysVolume.position.x, g_godRaysVolume.position.y, g_godRaysVolume.position.z};
        if (ImGui::DragFloat3("Position", position, 0.1f, -100000.0f, 100000.0f, "%.3f")) {
          g_godRaysVolume.position = {position[0], position[1], position[2]};
        }
        float halfExtents[3] = {g_godRaysVolume.half_extents.x, g_godRaysVolume.half_extents.y, g_godRaysVolume.half_extents.z};
        if (ImGui::DragFloat3("Half Extents", halfExtents, 0.1f, 0.001f, 100000.0f, "%.3f")) {
          g_godRaysVolume.half_extents = {
              (std::max)(0.001f, halfExtents[0]),
              (std::max)(0.001f, halfExtents[1]),
              (std::max)(0.001f, halfExtents[2])};
          if (t850::g_pBaseDriver) {
            t850::g_pBaseDriver->WaitForGPU();
          }
          ReleaseGizmoCache(g_godRaysVolumeGizmo);
        }
        if (!renderGraphHasGodRays) {
          ImGui::TextDisabled("The active render graph has no God Rays pass.");
        } else if (!IsValidGodRaysLightCamera(g_godRaysVolume.light_camera)) {
          ImGui::TextDisabled("Enable and assign a light camera to activate this volume.");
        }
        ImGui::TextDisabled("DayScene's original shader has no spatial clip; clip is off by default to preserve parity.");
      } else if (g_selectionType == 7 &&
                 g_selectedIdx >= 0 && g_selectedIdx < static_cast<int>(g_splines.size()) &&
                 g_selectedSplinePoint >= 0 &&
                 g_selectedSplinePoint < static_cast<int>(g_splines[static_cast<std::size_t>(g_selectedIdx)].points.size())) {
        t850::scene::SceneSplineDesc& spline = g_splines[static_cast<std::size_t>(g_selectedIdx)];
        t850::scene::SceneSplinePointDesc& point = spline.points[static_cast<std::size_t>(g_selectedSplinePoint)];
        ImGui::SeparatorText("Spline Point");
        ImGui::Text("Spline: %s", spline.name.c_str());
        ImGui::Text("Point: %d / %d", g_selectedSplinePoint, static_cast<int>(spline.points.size()));
        float pos[3] = {point.position.x, point.position.y, point.position.z};
        if (ImGui::DragFloat3("Position", pos, 0.1f, -100000.0f, 100000.0f, "%.3f")) {
          point.position = {pos[0], pos[1], pos[2]};
        }
        ImGui::DragFloat("Velocity", &point.velocity, 0.1f, 0.0f, 100.0f, "%.2f");
        ImGui::Checkbox("Look at next point", &point.look_at_center);
        float rot[3] = {point.rotation.x, point.rotation.y, point.rotation.z};
        if (ImGui::DragFloat3("Rotation", rot, 0.01f, -1000.0f, 1000.0f, "%.3f")) {
          point.rotation = {rot[0], rot[1], rot[2]};
        }
        if (ImGui::Button("Previous Point") && g_selectedSplinePoint > 0) {
          --g_selectedSplinePoint;
        }
        ImGui::SameLine();
        if (ImGui::Button("Next Point") && g_selectedSplinePoint + 1 < static_cast<int>(spline.points.size())) {
          ++g_selectedSplinePoint;
        }
        if (spline.points.size() > 4 && ImGui::Button("Delete Point")) {
          spline.points.erase(spline.points.begin() + g_selectedSplinePoint);
          if (g_selectedSplinePoint >= static_cast<int>(spline.points.size())) {
            g_selectedSplinePoint = static_cast<int>(spline.points.size()) - 1;
          }
        }
      }
    }
    ImGui::End();
  }

  if (m_panels.showTimeline) {
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
      const float margin = 12.0f;
      const ImGuiCond layoutCond = g_resetArtistLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
      const float rightPanelWidth = (std::min)(440.0f, (std::max)(360.0f, viewport->WorkSize.x * 0.27f));
      const float width = (std::max)(520.0f, viewport->WorkSize.x - rightPanelWidth - margin * 3.0f);
      const float height = 170.0f;
      ImGui::SetNextWindowPos(
          ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + viewport->WorkSize.y - height - margin),
          layoutCond);
      ImGui::SetNextWindowSize(ImVec2(width, height), layoutCond);
    }
    if (ImGui::Begin("Timeline", &m_panels.showTimeline, ImGuiWindowFlags_NoCollapse)) {
      ImGuiClampCurrentWindowToEditorWorkArea();
      DrawEditorTimelinePanel();
    }
    ImGui::End();
  }

  if (m_panels.showRendering) {
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
      const float margin = 12.0f;
      const ImGuiCond layoutCond = g_resetArtistLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
      const float width = (std::min)(440.0f, (std::max)(360.0f, viewport->WorkSize.x * 0.27f));
      const float inspectorHeight = (std::max)(480.0f, viewport->WorkSize.y * 0.62f);
      const float top = viewport->WorkPos.y + margin + inspectorHeight + margin;
      const float height = (std::max)(260.0f, viewport->WorkPos.y + viewport->WorkSize.y - top - margin);
      ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - margin, top), layoutCond);
      ImGui::SetNextWindowSize(ImVec2(width, height), layoutCond);
    }
    if (ImGui::Begin("Look & Lighting", &m_panels.showRendering, ImGuiWindowFlags_NoCollapse)) {
      ImGuiClampCurrentWindowToEditorWorkArea();
      DrawEditorRenderingPanel();
    }
    ImGui::End();
  }

  if (m_panels.showConsole)
    ImGuiDrawConsolePanel();

  if (m_panels.showRTDebug && !m_panels.showRendering)
    g_debugRT = ImGuiDrawRTDebugPanel(g_debugRT);

  DrawRagdollEditorWindow();
  DrawMeshEditorWindow();
  DrawPlaySceneWindow();

  commitImguiUndo("Editor Action");
  g_resetArtistLayout = false;
  T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender...");
  ImGuiRender();
  T8_LOG_TRACE("[T8ditor] OnDraw: ImGuiRender done");
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
    RenderEditorSceneFrame(drv, captureFrozenEditorFrame, didCaptureFrozenEditorFrame);
  }
  if (didCaptureFrozenEditorFrame) {
    m_editorFrozenFrameValid = true;
  }
  if (freezeEditorViewport && m_editorFrozenFrameValid) {
    DrawEditorFrozenFrame(drv);
  }

  // ImGui overlay
  if (m_imguiReady) {
    DrawEditorUI(drv);
  }


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
