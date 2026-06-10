// SceneObject / SceneCamera / SceneLight / SceneGroup — per-entity data for editor scenes.

#ifndef T8DITOR_SCENE_OBJECT_H
#define T8DITOR_SCENE_OBJECT_H

#include "EditorMesh.h"
#include <physics/PhysicsTypes.h>
#include <scene/PrimitiveInstance.h>
#include <scene/EditorSceneFile.h>
#include <utils/Camera.h>
#include <utils/Picking.h>
#include <video/BaseDriver.h>
#include <optional>
#include <string>
#include <vector>
#include <set>

namespace t8ditor {

// Per-entity gizmo VB/IB cache to avoid per-frame GPU allocation
struct GizmoCache {
  t850::VertexBuffer* vb    = nullptr;
  t850::IndexBuffer*  ib    = nullptr;
  unsigned            count = 0;
  uint64_t            hash  = 0;  // dirty check key
};

// ── Mesh object ──────────────────────────────────────

struct SceneObject {
  EditorMesh            wireframe;
  t850::PrimitiveInst   litInst;
  int                   primId = -1;
  std::string           name;
  std::string           meshPath;
  bool                  visible = true;
  bool                  transient = false;
  std::optional<bool>   mobileVisible;
  bool                  frozen  = false;  // visible but not selectable
  bool                  showWire = false; // per-object wireframe override
  bool                  showOrientation = false;
  std::optional<float>  navAgentFrontYawOffsetDeg;
  std::optional<float>  navAgentFaceYawSign;
  std::string           navAgentTargetMode = "direct";
  float                 navAgentFollowDistance = 0.0f;
  float                 navAgentSideOffset = 0.0f;
  float                 navAgentFormationDepthStep = 0.0f;
  int                   navAgentSlot = -1;
  std::optional<t850::scene::SceneObjectPhysicsDesc> physics;
  std::optional<t850::scene::SceneObjectNavigationDesc> navigation;
  std::optional<t850::scene::SceneObjectRagdollDesc> ragdollAuthoringMeta;

  std::string                         ragdollModelKey;
  std::string                         ragdollResourcePath;
  t850::PhysicsRagdollAuthoringDesc   ragdollAuthoring;
  std::vector<t850::PhysicsBodyState> ragdollPhysicsStates;
  std::vector<int>                    ragdollPhysicsBoneIndices;
  std::vector<XMATRIX44>              ragdollPhysicsCombinedMatrices;
  bool                                ragdollAuthoringTried = false;
  bool                                ragdollAuthoringReady = false;
  bool                                ragdollLoadedFromAsset = false;
  bool                                ragdollPreviewEnabled = false;
  bool                                ragdollDriveFromAnimation = false;
  bool                                ragdollSimulating = false;
  bool                                ragdollDebugDraw = false;
  int                                 ragdollBodyCount = 0;
  std::string                         ragdollStatus;
  std::vector<uint8_t>                 ragdollBodyVisible;
  std::vector<uint8_t>                 ragdollBodyWire;
  std::vector<uint8_t>                 ragdollJointVisible;
  std::vector<uint8_t>                 ragdollJointWire;
};

// ── Camera ───────────────────────────────────────────

enum class CameraType { Perspective = 0, Orthographic };

struct SceneCamera {
  std::string name    = "Camera";
  CameraType  type    = CameraType::Perspective;
  XVECTOR3    position = XVECTOR3(0.0f, 5.0f, -10.0f);
  XVECTOR3    target   = XVECTOR3(0.0f, 0.0f, 0.0f);
  float       fovDeg   = 50.0f;
  float       orthoW   = 20.0f;
  float       orthoH   = 15.0f;
  float       nearPlane = 0.1f;
  float       farPlane  = 1000.0f;
  bool        visible   = true;
  bool        frozen    = false;
  mutable GizmoCache gizmo;
};

// ── Light ────────────────────────────────────────────

enum class EditorLightType { Directional = 0, Omni };

struct SceneLight {
  std::string     name      = "Light";
  EditorLightType type      = EditorLightType::Directional;
  XVECTOR3        position  = XVECTOR3(0.0f, 10.0f, 0.0f);
  XVECTOR3        direction = XVECTOR3(0.0f, -1.0f, 0.0f);
  XVECTOR3        color     = XVECTOR3(1.0f, 1.0f, 1.0f);
  float           intensity = 1.5f;
  float           radius    = 10.0f;  // omni only
  bool            enabled   = true;
  bool            visible   = true;
  bool            frozen    = false;
  std::optional<t850::scene::SceneQ3LightDesc> q3;
  mutable GizmoCache gizmo;
};

// ── Group (persistent or temporary) ──────────────────

struct SceneGroup {
  std::string     name;
  std::set<int>   members;       // indices into g_objects
  bool            persistent = false;  // true = user clicked "Group", survives click-away

  // Compute combined AABB from all member objects
  t850::AABB ComputeAABB(const std::vector<SceneObject>& objects) const {
    t850::AABB combined;
    bool first = true;
    for (int idx : members) {
      if (idx < 0 || idx >= (int)objects.size()) continue;
      if (!objects[idx].wireframe.IsLoaded()) continue;
      t850::AABB wb = objects[idx].wireframe.WorldAABB();
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
    return combined;
  }

  // Centroid of the combined AABB
  XVECTOR3 Centroid(const std::vector<SceneObject>& objects) const {
    t850::AABB bb = ComputeAABB(objects);
    return XVECTOR3(
      (bb.vMin.x + bb.vMax.x) * 0.5f,
      (bb.vMin.y + bb.vMax.y) * 0.5f,
      (bb.vMin.z + bb.vMax.z) * 0.5f);
  }
};

} // namespace t8ditor

#endif // T8DITOR_SCENE_OBJECT_H
