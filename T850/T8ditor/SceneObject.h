// SceneObject — per-mesh data for multi-mesh editor scenes.
// Separated from EditorApp.h to allow independent modification.

#ifndef T8DITOR_SCENE_OBJECT_H
#define T8DITOR_SCENE_OBJECT_H

#include "EditorMesh.h"
#include <scene/PrimitiveInstance.h>
#include <string>

namespace t8ditor {

struct SceneObject {
  EditorMesh            wireframe;
  t800::PrimitiveInst   litInst;
  int                   primId = -1;
  std::string           name;
};

} // namespace t8ditor

#endif // T8DITOR_SCENE_OBJECT_H
