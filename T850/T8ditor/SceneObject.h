// SceneObject / SceneCamera / SceneLight — per-entity data for editor scenes.

#ifndef T8DITOR_SCENE_OBJECT_H
#define T8DITOR_SCENE_OBJECT_H

#include "EditorMesh.h"
#include <scene/PrimitiveInstance.h>
#include <utils/Camera.h>
#include <video/BaseDriver.h>
#include <string>

namespace t8ditor {

// Per-entity gizmo VB/IB cache to avoid per-frame GPU allocation
struct GizmoCache {
  t800::VertexBuffer* vb    = nullptr;
  t800::IndexBuffer*  ib    = nullptr;
  unsigned            count = 0;
  uint64_t            hash  = 0;  // dirty check key
};

// ── Mesh object ──────────────────────────────────────

struct SceneObject {
  EditorMesh            wireframe;
  t800::PrimitiveInst   litInst;
  int                   primId = -1;
  std::string           name;
  bool                  visible = true;
  bool                  frozen  = false;  // visible but not selectable
  bool                  showWire = false; // per-object wireframe override
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
  mutable GizmoCache gizmo;
};

} // namespace t8ditor

#endif // T8DITOR_SCENE_OBJECT_H
