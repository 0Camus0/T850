/*********************************************************
 * T8ditor — Viewport gizmos for cameras and lights.
 *
 * Draws wireframe representations of scene cameras (frustum)
 * and lights (direction arrow / radius sphere) using the
 * EditorLineRenderer. Selected entities are drawn in a
 * highlight color.
 *********************************************************/

#ifndef T8DITOR_EDITOR_SCENE_GIZMOS_H
#define T8DITOR_EDITOR_SCENE_GIZMOS_H

#include "EditorLineRenderer.h"
#include "SceneObject.h"

namespace t8ditor {

// Draw a camera frustum wireframe in the viewport.
void DrawCameraGizmo(EditorLineRenderer& lines, const XMATRIX44& vp,
                     const SceneCamera& cam, bool selected);

// Draw a light gizmo in the viewport (direction arrow or radius sphere).
void DrawLightGizmo(EditorLineRenderer& lines, const XMATRIX44& vp,
                    const SceneLight& lt, bool selected);

} // namespace t8ditor

#endif // T8DITOR_EDITOR_SCENE_GIZMOS_H
