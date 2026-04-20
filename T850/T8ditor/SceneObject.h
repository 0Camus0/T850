// SceneObject / SceneCamera / SceneLight — per-entity data for editor scenes.

#ifndef T8DITOR_SCENE_OBJECT_H
#define T8DITOR_SCENE_OBJECT_H

#include "EditorMesh.h"
#include <scene/PrimitiveInstance.h>
#include <utils/Camera.h>
#include <string>

namespace t8ditor {

// ── Mesh object ──────────────────────────────────────

struct SceneObject {
  EditorMesh            wireframe;
  t800::PrimitiveInst   litInst;
  int                   primId = -1;
  std::string           name;
  bool                  visible = true;
};

// ── Camera ───────────────────────────────────────────

enum class CameraType { Perspective = 0, Orthographic };

struct SceneCamera {
  std::string name    = "Camera";
  CameraType  type    = CameraType::Perspective;
  XVECTOR3    position = XVECTOR3(0.0f, 5.0f, -10.0f);
  XVECTOR3    target   = XVECTOR3(0.0f, 0.0f, 0.0f);
  float       fovDeg   = 50.0f;     // perspective only
  float       orthoW   = 20.0f;     // orthographic only
  float       orthoH   = 15.0f;
  float       nearPlane = 0.1f;
  float       farPlane  = 1000.0f;
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
};

} // namespace t8ditor

#endif // T8DITOR_SCENE_OBJECT_H
