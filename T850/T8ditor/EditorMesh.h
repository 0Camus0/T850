/*********************************************************
* T8ditor — wireframe display of an .x mesh in the editor viewport.
*
* Loads a `.x` file via Framework's `xF::XDataBase` and builds a single
* wireframe VB + IB (line-list of triangle edges) for display through
* EditorLineRenderer. This intentionally bypasses Framework's heavy
* PBR `RenderMesh` path: the editor only needs to *see* the geometry,
* not light it. Once ImGui + the in-editor scene-graph land, the
* full-render path can replace this for textured/lit preview.
*
* The editor owns the mesh's transform (position/rotation/scale); the
* user manipulates it via keyboard while a single-mesh selection is
* active.
*********************************************************/

#ifndef T8DITOR_EDITOR_MESH_H
#define T8DITOR_EDITOR_MESH_H

#include "EditorLineRenderer.h"

#include <string>

namespace t8ditor {

class EditorMesh {
public:
  EditorMesh()  = default;
  ~EditorMesh() { Destroy(); }

  // Load the .x file at `path` and build the wireframe buffers. Returns
  // false on any parse/IO/buffer-creation failure (caller can log + skip).
  bool Load(const std::string& path);
  void Destroy();

  bool IsLoaded() const { return m_vb != nullptr && m_ib != nullptr; }
  const std::string& Path() const { return m_path; }

  // Transform — owned by the mesh, mutated by the editor.
  XVECTOR3& Position()      { return m_position; }
  XVECTOR3& EulerRadians()  { return m_euler; }
  XVECTOR3& Scale()         { return m_scale; }

  // Center of the mesh's loaded geometry in *local* space (used by the
  // editor camera's "frame selection" so it can centre on the model).
  const XVECTOR3& LocalCenter() const { return m_localCenter; }

  // Compose the world matrix from position/rotation/scale (TRS).
  XMATRIX44 BuildWorld() const;

  // Draw as wireframe.
  void Draw(EditorLineRenderer& lines, const XMATRIX44& vp);

  XVECTOR3 WireColor = XVECTOR3(0.85f, 0.85f, 0.65f, 1.0f);

private:
  std::string m_path;

  t800::VertexBuffer* m_vb = nullptr;
  t800::IndexBuffer*  m_ib = nullptr;
  unsigned m_indexCount = 0;

  XVECTOR3 m_position    = XVECTOR3(0.0f, 0.0f, 0.0f);
  XVECTOR3 m_euler       = XVECTOR3(0.0f, 0.0f, 0.0f); // radians, XYZ
  XVECTOR3 m_scale       = XVECTOR3(1.0f, 1.0f, 1.0f);
  XVECTOR3 m_localCenter = XVECTOR3(0.0f, 0.0f, 0.0f);
};

} // namespace t8ditor

#endif // T8DITOR_EDITOR_MESH_H
