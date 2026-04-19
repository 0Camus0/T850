/*********************************************************
* T8ditor — wireframe display of an .x mesh. See header.
*********************************************************/

#include "EditorMesh.h"

#include <utils/Log.h>
#include <utils/XDataBase.h>
#include <utils/xMaths.h>

#include <vector>

namespace t8ditor {

bool EditorMesh::Load(const std::string& path) {
  Destroy();
  m_path = path;

  xF::XDataBase xdb;
  if (!xdb.LoadXFile(path)) {
    T8_LOG_ERROR("[T8ditor] EditorMesh: failed to load '%s'", path.c_str());
    return false;
  }

  // Collect all positions + triangles across every mesh container/geometry.
  // We build a single combined VB and a line-list IB whose entries are the
  // edges of every triangle.
  std::vector<float>          verts; // xyzw per vertex
  std::vector<unsigned short> idx;

  // Bounding box for centre + initial framing.
  float bbMin[3] = {  1e30f,  1e30f,  1e30f };
  float bbMax[3] = { -1e30f, -1e30f, -1e30f };

  for (auto* container : xdb.XMeshDataBase) {
    if (!container) continue;
    for (auto& geom : container->Geometry) {
      const unsigned baseV = (unsigned)(verts.size() / 4);

      // Vertices.
      for (auto& p : geom.Positions) {
        verts.push_back(p.x);
        verts.push_back(p.y);
        verts.push_back(p.z);
        verts.push_back(1.0f);
        if (p.x < bbMin[0]) bbMin[0] = p.x;
        if (p.y < bbMin[1]) bbMin[1] = p.y;
        if (p.z < bbMin[2]) bbMin[2] = p.z;
        if (p.x > bbMax[0]) bbMax[0] = p.x;
        if (p.y > bbMax[1]) bbMax[1] = p.y;
        if (p.z > bbMax[2]) bbMax[2] = p.z;
      }

      // Triangles -> 3 line segments per tri. Indices are 16-bit; this
      // restricts us to 65k verts per geometry block, which matches the
      // .x format's xWORD index width.
      const auto& tris = geom.Triangles; // xWORD = uint16_t
      const std::size_t n = tris.size();
      // Triangles is already a flat list of uint16 indices (3 per tri).
      for (std::size_t t = 0; t + 2 < n; t += 3) {
        const unsigned short a = (unsigned short)(baseV + tris[t + 0]);
        const unsigned short b = (unsigned short)(baseV + tris[t + 1]);
        const unsigned short c = (unsigned short)(baseV + tris[t + 2]);
        idx.push_back(a); idx.push_back(b);
        idx.push_back(b); idx.push_back(c);
        idx.push_back(c); idx.push_back(a);
      }
    }
  }

  if (verts.empty() || idx.empty()) {
    T8_LOG_ERROR("[T8ditor] EditorMesh: '%s' produced no geometry", path.c_str());
    return false;
  }

  // .x format historically uses 16-bit indices. If the merged VB exceeds
  // 65535 verts we'd need 32-bit; bail out clearly rather than overflow.
  if ((verts.size() / 4) > 65535) {
    T8_LOG_ERROR("[T8ditor] EditorMesh: '%s' has >65535 merged verts (%zu); "
                 "32-bit index path not implemented yet, skipping",
                 path.c_str(), verts.size() / 4);
    return false;
  }

  m_vb = EditorLineRenderer::CreatePositionVB(verts.data(), (unsigned)(verts.size() / 4));
  m_ib = EditorLineRenderer::CreateIndexBuffer16(idx.data(), (unsigned)idx.size());
  m_indexCount = (unsigned)idx.size();
  if (!m_vb || !m_ib) {
    T8_LOG_ERROR("[T8ditor] EditorMesh: GPU buffer creation failed for '%s'", path.c_str());
    Destroy();
    return false;
  }

  m_localCenter = XVECTOR3((bbMin[0] + bbMax[0]) * 0.5f,
                           (bbMin[1] + bbMax[1]) * 0.5f,
                           (bbMin[2] + bbMax[2]) * 0.5f);

  T8_LOG_INFO("[T8ditor] EditorMesh: loaded '%s' (%u verts, %u line indices)",
              path.c_str(), (unsigned)(verts.size() / 4), m_indexCount);
  return true;
}

void EditorMesh::Destroy() {
  m_vb = nullptr;
  m_ib = nullptr;
  m_indexCount = 0;
  m_path.clear();
}

XMATRIX44 EditorMesh::BuildWorld() const {
  XMATRIX44 S, Rx, Ry, Rz, T, M;
  XMatScaling(S, m_scale.x, m_scale.y, m_scale.z);
  XMatRotationX(Rx, m_euler.x);
  XMatRotationY(Ry, m_euler.y);
  XMatRotationZ(Rz, m_euler.z);
  XMatTranslation(T, m_position.x, m_position.y, m_position.z);
  // T * R * S — applied right-to-left to local-space points.
  M = S * Rz * Ry * Rx * T;
  return M;
}

void EditorMesh::Draw(EditorLineRenderer& lines, const XMATRIX44& vp) {
  if (!IsLoaded() || !lines.IsReady()) return;
  XMATRIX44 world = BuildWorld();
  lines.DrawLines(world, vp, WireColor, m_vb, m_ib, m_indexCount, /*stride=*/16);
}

} // namespace t8ditor
