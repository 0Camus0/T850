/*********************************************************
* T8ditor — three-axis transform gizmo for the active selection.
*
* Visualisation only in this slice (no mouse-drag interaction yet —
* that lands when ImGuizmo arrives in the next ImGui-based phase).
* Mode is toggled with the W/E/R hot-keys, mirroring Blender/Unity:
*
*   Translate (W) : three colored arrows along ±X/Y/Z
*   Rotate    (E) : three axis-aligned circles (X-circle in YZ plane, etc.)
*   Scale     (R) : three colored shafts ending in cube tips
*
* The selection's transform (position/rotation/scale) is supplied by the
* caller and used to build the gizmo's world matrix. The gizmo is sized
* in world units (Size); a future improvement is to scale-by-distance so
* it stays roughly screen-constant.
*********************************************************/

#ifndef T8DITOR_EDITOR_GIZMO_H
#define T8DITOR_EDITOR_GIZMO_H

#include "EditorLineRenderer.h"

namespace t8ditor {

enum class GizmoMode {
  Select = -1,
  Translate = 0,
  Rotate,
  Scale,
};

class EditorGizmo {
public:
  EditorGizmo()  = default;
  ~EditorGizmo() { Destroy(); }

  bool Create();
  void Destroy();

  void SetMode(GizmoMode m) { m_mode = m; }
  GizmoMode Mode() const    { return m_mode; }

  // Draw the gizmo at `world` (the selection's transform). VP is the
  // current camera view-projection.
  void Draw(EditorLineRenderer& lines,
            const XMATRIX44& vp,
            const XMATRIX44& world);

  // Tunables.
  float Size = 2.0f;

  XVECTOR3 XColor = XVECTOR3(0.95f, 0.20f, 0.20f, 1.0f);
  XVECTOR3 YColor = XVECTOR3(0.20f, 0.85f, 0.20f, 1.0f);
  XVECTOR3 ZColor = XVECTOR3(0.30f, 0.45f, 0.95f, 1.0f);

private:
  // Translate-mode geometry: three arrows in local space (per-axis IB so
  // each axis takes a coloured draw). Vertices are unit-length along +X,
  // +Y, +Z (with arrowhead barbs); world matrix scales by Size.
  t800::VertexBuffer* m_translateVB = nullptr;
  t800::IndexBuffer*  m_translateIBx = nullptr;
  t800::IndexBuffer*  m_translateIBy = nullptr;
  t800::IndexBuffer*  m_translateIBz = nullptr;
  unsigned m_translateAxisIdxCount = 0;

  // Rotate-mode geometry: three axis-aligned circles, also one shared VB
  // with per-axis IBs.
  t800::VertexBuffer* m_rotateVB = nullptr;
  t800::IndexBuffer*  m_rotateIBx = nullptr;
  t800::IndexBuffer*  m_rotateIBy = nullptr;
  t800::IndexBuffer*  m_rotateIBz = nullptr;
  unsigned m_rotateAxisIdxCount = 0;

  // Scale-mode geometry: shaft + cube tip, per axis.
  t800::VertexBuffer* m_scaleVB = nullptr;
  t800::IndexBuffer*  m_scaleIBx = nullptr;
  t800::IndexBuffer*  m_scaleIBy = nullptr;
  t800::IndexBuffer*  m_scaleIBz = nullptr;
  unsigned m_scaleAxisIdxCount = 0;

  GizmoMode m_mode = GizmoMode::Translate;
};

} // namespace t8ditor

#endif // T8DITOR_EDITOR_GIZMO_H
