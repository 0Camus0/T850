/*********************************************************
* T8ditor — wireframe display of a mesh in the editor viewport.
*********************************************************/

#include "EditorMesh.h"

#include <utils/Log.h>
#include <utils/XDataBase.h>
#include <utils/xMaths.h>
#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFAccessor.h>

#include <vector>
#include <algorithm>
#include <cfloat>
#include <filesystem>

namespace t8ditor {

// Helper: lowercase file extension without dot
static std::string FileExtLower(const std::string& path) {
  auto p = std::filesystem::path(path).extension().string();
  if (!p.empty() && p[0] == '.') p = p.substr(1);
  std::transform(p.begin(), p.end(), p.begin(), ::tolower);
  return p;
}

bool EditorMesh::Load(const std::string& path) {
  Destroy();
  m_path = path;

  xF::XDataBase xdb;
  const std::string ext = FileExtLower(path);

  if (ext == "glb" || ext == "gltf") {
    // glTF path: parse and convert to XDataBase (same as ResourceManager)
    t850::gltf::Document doc;
    if (!t850::gltf::LoadGLTF(path, doc) ||
        !t850::gltf::ConvertToXDatabase(doc, xdb, path)) {
      T8_LOG_ERROR("[T8ditor] EditorMesh: failed to load glTF '%s'", path.c_str());
      return false;
    }
  } else {
    // Legacy .X path
    if (!xdb.LoadXFile(path)) {
      T8_LOG_ERROR("[T8ditor] EditorMesh: failed to load '%s'", path.c_str());
      return false;
    }
  }

  // Collect all positions + triangles across every mesh container/geometry.
  // We build a single combined VB and a line-list IB whose entries are the
  // edges of every triangle. Use 32-bit indices to support large glTF meshes.
  std::vector<float>        verts; // xyzw per vertex
  std::vector<unsigned int> idx;
  m_pickVertices.clear();
  m_pickIndices.clear();

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
        m_pickVertices.emplace_back(p.x, p.y, p.z, 1.0f);
        if (p.x < bbMin[0]) bbMin[0] = p.x;
        if (p.y < bbMin[1]) bbMin[1] = p.y;
        if (p.z < bbMin[2]) bbMin[2] = p.z;
        if (p.x > bbMax[0]) bbMax[0] = p.x;
        if (p.y > bbMax[1]) bbMax[1] = p.y;
        if (p.z > bbMax[2]) bbMax[2] = p.z;
      }

      // Triangles -> 3 line segments per tri.
      // Handle both 16-bit and 32-bit index arrays from XDataBase.
      if (geom.Indices32Bit && !geom.Triangles32.empty()) {
        const auto& tris = geom.Triangles32;
        for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
          unsigned int a = baseV + tris[t + 0];
          unsigned int b = baseV + tris[t + 1];
          unsigned int c = baseV + tris[t + 2];
          m_pickIndices.push_back(a);
          m_pickIndices.push_back(b);
          m_pickIndices.push_back(c);
          idx.push_back(a); idx.push_back(b);
          idx.push_back(b); idx.push_back(c);
          idx.push_back(c); idx.push_back(a);
        }
      } else {
        const auto& tris = geom.Triangles;
        for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
          unsigned int a = baseV + tris[t + 0];
          unsigned int b = baseV + tris[t + 1];
          unsigned int c = baseV + tris[t + 2];
          m_pickIndices.push_back(a);
          m_pickIndices.push_back(b);
          m_pickIndices.push_back(c);
          idx.push_back(a); idx.push_back(b);
          idx.push_back(b); idx.push_back(c);
          idx.push_back(c); idx.push_back(a);
        }
      }
    }
  }

  if (verts.empty() || idx.empty()) {
    T8_LOG_ERROR("[T8ditor] EditorMesh: '%s' produced no geometry", path.c_str());
    return false;
  }

  unsigned numVerts = (unsigned)(verts.size() / 4);
  m_vb = EditorLineRenderer::CreatePositionVB(verts.data(), numVerts);

  // Use 16-bit IB when possible (saves memory), 32-bit for large meshes
  if (numVerts <= 65535) {
    std::vector<unsigned short> idx16(idx.size());
    for (size_t i = 0; i < idx.size(); i++)
      idx16[i] = (unsigned short)idx[i];
    m_ib = EditorLineRenderer::CreateIndexBuffer16(idx16.data(), (unsigned)idx16.size());
    m_use32BitIB = false;
  } else {
    m_ib = EditorLineRenderer::CreateIndexBuffer32(idx.data(), (unsigned)idx.size());
    m_use32BitIB = true;
  }

  m_indexCount = (unsigned)idx.size();
  if (!m_vb || !m_ib) {
    T8_LOG_ERROR("[T8ditor] EditorMesh: GPU buffer creation failed for '%s'", path.c_str());
    Destroy();
    return false;
  }

  m_localCenter = XVECTOR3((bbMin[0] + bbMax[0]) * 0.5f,
                           (bbMin[1] + bbMax[1]) * 0.5f,
                           (bbMin[2] + bbMax[2]) * 0.5f);

  m_localAABB = t850::AABB(
    XVECTOR3(bbMin[0], bbMin[1], bbMin[2]),
    XVECTOR3(bbMax[0], bbMax[1], bbMax[2])
  );

  T8_LOG_INFO("[T8ditor] EditorMesh: loaded '%s' (%u verts, %u line indices)",
              path.c_str(), (unsigned)(verts.size() / 4), m_indexCount);
  return true;
}

bool EditorMesh::LoadFromTriangles(const std::string& name,
                                   const std::vector<XVECTOR3>& vertices,
                                   const std::vector<unsigned int>& triangleIndices) {
  Destroy();
  m_path = name;
  if (vertices.empty() || triangleIndices.size() < 3) {
    T8_LOG_ERROR("[T8ditor] EditorMesh: generated mesh '%s' has no triangles", name.c_str());
    return false;
  }

  std::vector<float> verts;
  std::vector<unsigned int> idx;
  verts.reserve(vertices.size() * 4u);
  idx.reserve((triangleIndices.size() / 3u) * 6u);
  m_pickVertices.clear();
  m_pickIndices.clear();
  m_pickVertices.reserve(vertices.size());
  m_pickIndices.reserve((triangleIndices.size() / 3u) * 3u);

  float bbMin[3] = {  1e30f,  1e30f,  1e30f };
  float bbMax[3] = { -1e30f, -1e30f, -1e30f };
  for (const XVECTOR3& p : vertices) {
    verts.push_back(p.x);
    verts.push_back(p.y);
    verts.push_back(p.z);
    verts.push_back(1.0f);
    m_pickVertices.emplace_back(p.x, p.y, p.z, 1.0f);
    if (p.x < bbMin[0]) bbMin[0] = p.x;
    if (p.y < bbMin[1]) bbMin[1] = p.y;
    if (p.z < bbMin[2]) bbMin[2] = p.z;
    if (p.x > bbMax[0]) bbMax[0] = p.x;
    if (p.y > bbMax[1]) bbMax[1] = p.y;
    if (p.z > bbMax[2]) bbMax[2] = p.z;
  }

  for (std::size_t t = 0; t + 2 < triangleIndices.size(); t += 3) {
    const unsigned int a = triangleIndices[t + 0];
    const unsigned int b = triangleIndices[t + 1];
    const unsigned int c = triangleIndices[t + 2];
    if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) {
      continue;
    }
    m_pickIndices.push_back(a);
    m_pickIndices.push_back(b);
    m_pickIndices.push_back(c);
    idx.push_back(a); idx.push_back(b);
    idx.push_back(b); idx.push_back(c);
    idx.push_back(c); idx.push_back(a);
  }

  if (idx.empty()) {
    T8_LOG_ERROR("[T8ditor] EditorMesh: generated mesh '%s' has invalid indices", name.c_str());
    Destroy();
    return false;
  }

  const unsigned numVerts = static_cast<unsigned>(verts.size() / 4u);
  m_vb = EditorLineRenderer::CreatePositionVB(verts.data(), numVerts);
  if (numVerts <= 65535) {
    std::vector<unsigned short> idx16(idx.size());
    for (std::size_t i = 0; i < idx.size(); ++i) {
      idx16[i] = static_cast<unsigned short>(idx[i]);
    }
    m_ib = EditorLineRenderer::CreateIndexBuffer16(idx16.data(), static_cast<unsigned>(idx16.size()));
    m_use32BitIB = false;
  } else {
    m_ib = EditorLineRenderer::CreateIndexBuffer32(idx.data(), static_cast<unsigned>(idx.size()));
    m_use32BitIB = true;
  }

  m_indexCount = static_cast<unsigned>(idx.size());
  if (!m_vb || !m_ib) {
    T8_LOG_ERROR("[T8ditor] EditorMesh: GPU buffer creation failed for generated mesh '%s'", name.c_str());
    Destroy();
    return false;
  }

  m_localCenter = XVECTOR3((bbMin[0] + bbMax[0]) * 0.5f,
                           (bbMin[1] + bbMax[1]) * 0.5f,
                           (bbMin[2] + bbMax[2]) * 0.5f);
  m_localAABB = t850::AABB(
      XVECTOR3(bbMin[0], bbMin[1], bbMin[2]),
      XVECTOR3(bbMax[0], bbMax[1], bbMax[2]));
  T8_LOG_INFO("[T8ditor] EditorMesh: generated '%s' (%zu verts, %u line indices)",
              name.c_str(), vertices.size(), m_indexCount);
  return true;
}

void EditorMesh::Destroy() {
  m_vb = nullptr;
  m_ib = nullptr;
  m_indexCount = 0;
  m_path.clear();
  m_pickVertices.clear();
  m_pickIndices.clear();
}

XMATRIX44 EditorMesh::BuildWorld() const {
  XMATRIX44 S, Rx, Ry, Rz, T, M;
  XMatScaling(S, m_scale.x, m_scale.y, m_scale.z);
  XMatRotationX(Rx, m_euler.x);
  XMatRotationY(Ry, m_euler.y);
  XMatRotationZ(Rz, m_euler.z);
  XMatTranslation(T, m_position.x, m_position.y, m_position.z);
  // The xMaths system uses row-vector / row-major matrices (D3DX-style):
  // a point row `v` is transformed as `v * M`, so reading the chain left to
  // right gives the order in which transforms are applied. Match
  // PrimitiveInst::Update (Scale, RotationX, RotationY, RotationZ, Position).
  M = S * Rx * Ry * Rz * T;
  return M;
}

bool EditorMesh::RaycastSurface(const t850::Ray& ray, float& tOut) const {
  tOut = FLT_MAX;
  if (!IsLoaded() || m_pickVertices.empty() || m_pickIndices.size() < 3) {
    return false;
  }

  float aabbT = 0.0f;
  if (!t850::RayIntersectsAABB(ray, WorldAABB(), aabbT)) {
    return false;
  }

  const XMATRIX44 world = BuildWorld();
  bool hit = false;
  for (std::size_t i = 0; i + 2 < m_pickIndices.size(); i += 3) {
    const unsigned int i0 = m_pickIndices[i + 0];
    const unsigned int i1 = m_pickIndices[i + 1];
    const unsigned int i2 = m_pickIndices[i + 2];
    if (i0 >= m_pickVertices.size() || i1 >= m_pickVertices.size() || i2 >= m_pickVertices.size()) {
      continue;
    }

    const XVECTOR3 v0 = t850::TransformPoint(m_pickVertices[i0], world);
    const XVECTOR3 v1 = t850::TransformPoint(m_pickVertices[i1], world);
    const XVECTOR3 v2 = t850::TransformPoint(m_pickVertices[i2], world);
    float t = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    if (t850::RayIntersectsTriangle(ray, v0, v1, v2, t, u, v) && t < tOut) {
      tOut = t;
      hit = true;
    }
  }
  return hit;
}

void EditorMesh::Draw(EditorLineRenderer& lines, const XMATRIX44& vp) {
  if (!IsLoaded() || !lines.IsReady()) return;
  XMATRIX44 world = BuildWorld();
  auto ibFmt = m_use32BitIB ? t850::IndexBufferFormat::R32 : t850::IndexBufferFormat::R16;
  lines.DrawLines(world, vp, WireColor, m_vb, m_ib, m_indexCount, /*stride=*/16, ibFmt);
}

} // namespace t8ditor
