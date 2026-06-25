/*********************************************************
* T8ditor — wireframe display of a mesh in the editor viewport.
*
* Loads a `.x`, `.glb`, or `.gltf` file and builds a single wireframe
* VB + IB (line-list of triangle edges) for display through
* EditorLineRenderer. For glTF files, the Framework's GLTFLoader
* converts to XDataBase format first. This intentionally bypasses
* Framework's heavy PBR `RenderMesh` path: the editor only needs to
* *see* the geometry, not light it.
*
* The editor owns the mesh's transform (position/rotation/scale); the
* user manipulates it via keyboard while a single-mesh selection is
* active.
*********************************************************/

#ifndef T8DITOR_EDITOR_MESH_H
#define T8DITOR_EDITOR_MESH_H

#include "EditorLineRenderer.h"

#include <scene/WireframeGeometry.h>
#include <utils/Picking.h>
#include <string>
#include <vector>

namespace t8ditor {

class EditorMesh {
public:
  EditorMesh()  = default;
  ~EditorMesh() { Destroy(); }
  EditorMesh(const EditorMesh&) = delete;
  EditorMesh& operator=(const EditorMesh&) = delete;
  EditorMesh(EditorMesh&&) noexcept = default;
  EditorMesh& operator=(EditorMesh&&) noexcept = default;

  // Load a mesh file (.x, .glb, .gltf) and build the wireframe buffers.
  // Returns false on any parse/IO/buffer-creation failure.
  bool Load(const std::string& path);
  bool LoadFromTriangles(const std::string& name,
                         const std::vector<XVECTOR3>& vertices,
                         const std::vector<unsigned int>& triangleIndices);
  bool CloneFrom(const EditorMesh& source);
  void Destroy();

  bool IsLoaded() const { return m_geometry.IsReady(); }
  const std::string& Path() const { return m_path; }

  // Transform — owned by the mesh, mutated by the editor.
  XVECTOR3& Position()      { return m_position; }
  XVECTOR3& EulerRadians()  { return m_euler; }
  XVECTOR3& Scale()         { return m_scale; }

  // Center of the mesh's loaded geometry in *local* space.
  const XVECTOR3& LocalCenter() const { return m_localCenter; }

  // Local-space axis-aligned bounding box.
  const t850::AABB& LocalAABB() const { return m_localAABB; }

  // World-space AABB (local AABB transformed by current TRS).
  t850::AABB WorldAABB() const { return m_localAABB.Transformed(BuildWorld()); }

  std::size_t PickVertexCount() const { return m_pickVertices.size(); }
  std::size_t PickTriangleCount() const { return m_pickIndices.size() / 3; }

  bool RaycastSurface(const t850::Ray& ray, float& tOut) const;

  // Compose the world matrix from position/rotation/scale (TRS).
  XMATRIX44 BuildWorld() const;

  // Draw as wireframe.
  void Draw(EditorLineRenderer& lines, const XMATRIX44& vp);

  XVECTOR3 WireColor = XVECTOR3(0.85f, 0.85f, 0.65f, 1.0f);

private:
  std::string m_path;

  t850::WireframeGeometry m_geometry;
  std::vector<XVECTOR3> m_pickVertices;
  std::vector<unsigned int> m_pickIndices;

  XVECTOR3 m_position    = XVECTOR3(0.0f, 0.0f, 0.0f);
  XVECTOR3 m_euler       = XVECTOR3(0.0f, 0.0f, 0.0f); // radians, XYZ
  XVECTOR3 m_scale       = XVECTOR3(1.0f, 1.0f, 1.0f);
  XVECTOR3 m_localCenter = XVECTOR3(0.0f, 0.0f, 0.0f);
  t850::AABB m_localAABB;
};

} // namespace t8ditor

#endif // T8DITOR_EDITOR_MESH_H
