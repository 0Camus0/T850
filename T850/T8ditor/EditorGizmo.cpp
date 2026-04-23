/*********************************************************
* T8ditor — three-axis transform gizmo. See header.
*********************************************************/

#include "EditorGizmo.h"

#include <utils/Log.h>
#include <utils/xMaths.h>

#include <cmath>
#include <vector>

namespace t8ditor {

namespace {

// Helper: push a float4 vertex (xyzw=1).
inline void PushV(std::vector<float>& v, float x, float y, float z) {
  v.push_back(x); v.push_back(y); v.push_back(z); v.push_back(1.0f);
}
inline void PushLine(std::vector<unsigned short>& idx, unsigned a, unsigned b) {
  idx.push_back((unsigned short)a);
  idx.push_back((unsigned short)b);
}

// Build a translate-arrow in local space along one Cartesian axis.
// `axis` chooses which axis (0=X, 1=Y, 2=Z). Vertices are appended to
// `verts`, line indices to `idx`. Returns the number of indices added.
unsigned BuildArrow(std::vector<float>& verts,
                    std::vector<unsigned short>& idx,
                    int axis) {
  const unsigned baseV = (unsigned)(verts.size() / 4);
  const unsigned baseI = (unsigned)idx.size();

  // Endpoints of shaft.
  XVECTOR3 origin(0.0f, 0.0f, 0.0f);
  XVECTOR3 tip(0.0f, 0.0f, 0.0f);
  if (axis == 0)      tip.x = 1.0f;
  else if (axis == 1) tip.y = 1.0f;
  else                tip.z = 1.0f;

  // Two perpendicular axes (for the arrowhead barbs)
  XVECTOR3 perpA(0.0f, 0.0f, 0.0f);
  XVECTOR3 perpB(0.0f, 0.0f, 0.0f);
  if (axis == 0) { perpA.y = 1.0f; perpB.z = 1.0f; }
  else if (axis == 1) { perpA.x = 1.0f; perpB.z = 1.0f; }
  else { perpA.x = 1.0f; perpB.y = 1.0f; }

  const float headStart = 0.85f;
  const float headSize  = 0.08f;

  // Vertex 0: origin
  PushV(verts, origin.x, origin.y, origin.z);
  // Vertex 1: tip
  PushV(verts, tip.x, tip.y, tip.z);
  // Vertices 2..5: 4 barbs at headStart along the axis, offset on perpA/perpB
  XVECTOR3 hs(tip.x * headStart, tip.y * headStart, tip.z * headStart);
  PushV(verts, hs.x + perpA.x * headSize, hs.y + perpA.y * headSize, hs.z + perpA.z * headSize);
  PushV(verts, hs.x - perpA.x * headSize, hs.y - perpA.y * headSize, hs.z - perpA.z * headSize);
  PushV(verts, hs.x + perpB.x * headSize, hs.y + perpB.y * headSize, hs.z + perpB.z * headSize);
  PushV(verts, hs.x - perpB.x * headSize, hs.y - perpB.y * headSize, hs.z - perpB.z * headSize);

  // Lines: shaft + 4 barbs to tip
  PushLine(idx, baseV + 0, baseV + 1); // shaft
  PushLine(idx, baseV + 2, baseV + 1);
  PushLine(idx, baseV + 3, baseV + 1);
  PushLine(idx, baseV + 4, baseV + 1);
  PushLine(idx, baseV + 5, baseV + 1);

  return (unsigned)(idx.size() - baseI);
}

// Build a scale-axis: shaft + small wireframe cube at the tip.
unsigned BuildScaleAxis(std::vector<float>& verts,
                        std::vector<unsigned short>& idx,
                        int axis) {
  const unsigned baseV = (unsigned)(verts.size() / 4);
  const unsigned baseI = (unsigned)idx.size();

  XVECTOR3 tip(0.0f, 0.0f, 0.0f);
  if (axis == 0)      tip.x = 1.0f;
  else if (axis == 1) tip.y = 1.0f;
  else                tip.z = 1.0f;

  PushV(verts, 0.0f, 0.0f, 0.0f);    // 0 origin
  PushV(verts, tip.x, tip.y, tip.z); // 1 tip / cube center

  // 8 cube corners at tip ± (h,h,h)
  const float h = 0.06f;
  float c[8][3] = {
    { tip.x - h, tip.y - h, tip.z - h },
    { tip.x + h, tip.y - h, tip.z - h },
    { tip.x + h, tip.y + h, tip.z - h },
    { tip.x - h, tip.y + h, tip.z - h },
    { tip.x - h, tip.y - h, tip.z + h },
    { tip.x + h, tip.y - h, tip.z + h },
    { tip.x + h, tip.y + h, tip.z + h },
    { tip.x - h, tip.y + h, tip.z + h },
  };
  for (int i = 0; i < 8; ++i) PushV(verts, c[i][0], c[i][1], c[i][2]);

  // Shaft
  PushLine(idx, baseV + 0, baseV + 1);
  // Cube edges (verts at baseV+2..baseV+9). Bottom square, top square, verticals.
  const unsigned cb = baseV + 2;
  PushLine(idx, cb + 0, cb + 1); PushLine(idx, cb + 1, cb + 2);
  PushLine(idx, cb + 2, cb + 3); PushLine(idx, cb + 3, cb + 0);
  PushLine(idx, cb + 4, cb + 5); PushLine(idx, cb + 5, cb + 6);
  PushLine(idx, cb + 6, cb + 7); PushLine(idx, cb + 7, cb + 4);
  PushLine(idx, cb + 0, cb + 4); PushLine(idx, cb + 1, cb + 5);
  PushLine(idx, cb + 2, cb + 6); PushLine(idx, cb + 3, cb + 7);

  return (unsigned)(idx.size() - baseI);
}

// Build a circle of `segments` line segments in the plane perpendicular to
// `axis` (0=X => YZ plane, 1=Y => XZ, 2=Z => XY). Radius=1.
unsigned BuildCircle(std::vector<float>& verts,
                     std::vector<unsigned short>& idx,
                     int axis, int segments) {
  const unsigned baseV = (unsigned)(verts.size() / 4);
  const unsigned baseI = (unsigned)idx.size();
  const float twoPi = 2.0f * xPI;
  for (int i = 0; i < segments; ++i) {
    const float t = (float)i / (float)segments * twoPi;
    const float c = std::cos(t);
    const float s = std::sin(t);
    if (axis == 0)      PushV(verts, 0.0f, c, s);
    else if (axis == 1) PushV(verts, c, 0.0f, s);
    else                PushV(verts, c, s, 0.0f);
  }
  for (int i = 0; i < segments; ++i) {
    PushLine(idx, baseV + i, baseV + ((i + 1) % segments));
  }
  return (unsigned)(idx.size() - baseI);
}

} // namespace

bool EditorGizmo::Create() {
  // ── Translate: build all three arrows into one VB, with per-axis IBs ──
  {
    std::vector<float> verts;
    std::vector<unsigned short> idxX, idxY, idxZ;
    BuildArrow(verts, idxX, 0);
    // For Y/Z we restart the index lists but continue appending verts. Use
    // a single VB but separate IBs whose indices reference the shared VB.
    BuildArrow(verts, idxY, 1);
    BuildArrow(verts, idxZ, 2);

    m_translateVB  = EditorLineRenderer::CreatePositionVB(verts.data(), (unsigned)(verts.size() / 4));
    m_translateIBx = EditorLineRenderer::CreateIndexBuffer16(idxX.data(), (unsigned)idxX.size());
    m_translateIBy = EditorLineRenderer::CreateIndexBuffer16(idxY.data(), (unsigned)idxY.size());
    m_translateIBz = EditorLineRenderer::CreateIndexBuffer16(idxZ.data(), (unsigned)idxZ.size());
    m_translateAxisIdxCount = (unsigned)idxX.size(); // identical for all 3
  }

  // ── Rotate: three axis-aligned circles ──
  {
    std::vector<float> verts;
    std::vector<unsigned short> idxX, idxY, idxZ;
    const int segs = 48;
    BuildCircle(verts, idxX, 0, segs);
    BuildCircle(verts, idxY, 1, segs);
    BuildCircle(verts, idxZ, 2, segs);

    m_rotateVB  = EditorLineRenderer::CreatePositionVB(verts.data(), (unsigned)(verts.size() / 4));
    m_rotateIBx = EditorLineRenderer::CreateIndexBuffer16(idxX.data(), (unsigned)idxX.size());
    m_rotateIBy = EditorLineRenderer::CreateIndexBuffer16(idxY.data(), (unsigned)idxY.size());
    m_rotateIBz = EditorLineRenderer::CreateIndexBuffer16(idxZ.data(), (unsigned)idxZ.size());
    m_rotateAxisIdxCount = (unsigned)idxX.size();
  }

  // ── Scale: shaft + cube ──
  {
    std::vector<float> verts;
    std::vector<unsigned short> idxX, idxY, idxZ;
    BuildScaleAxis(verts, idxX, 0);
    BuildScaleAxis(verts, idxY, 1);
    BuildScaleAxis(verts, idxZ, 2);

    m_scaleVB  = EditorLineRenderer::CreatePositionVB(verts.data(), (unsigned)(verts.size() / 4));
    m_scaleIBx = EditorLineRenderer::CreateIndexBuffer16(idxX.data(), (unsigned)idxX.size());
    m_scaleIBy = EditorLineRenderer::CreateIndexBuffer16(idxY.data(), (unsigned)idxY.size());
    m_scaleIBz = EditorLineRenderer::CreateIndexBuffer16(idxZ.data(), (unsigned)idxZ.size());
    m_scaleAxisIdxCount = (unsigned)idxX.size();
  }

  if (!m_translateVB || !m_rotateVB || !m_scaleVB) {
    T8_LOG_ERROR("[T8ditor] EditorGizmo: VB/IB creation failed");
    Destroy();
    return false;
  }

  T8_LOG_INFO("[T8ditor] EditorGizmo ready");
  return true;
}

void EditorGizmo::Destroy() {
  if (m_translateVB)  m_translateVB->release();
  if (m_translateIBx) m_translateIBx->release();
  if (m_translateIBy) m_translateIBy->release();
  if (m_translateIBz) m_translateIBz->release();
  m_translateVB  = nullptr;
  m_translateIBx = m_translateIBy = m_translateIBz = nullptr;

  if (m_rotateVB)  m_rotateVB->release();
  if (m_rotateIBx) m_rotateIBx->release();
  if (m_rotateIBy) m_rotateIBy->release();
  if (m_rotateIBz) m_rotateIBz->release();
  m_rotateVB     = nullptr;
  m_rotateIBx    = m_rotateIBy = m_rotateIBz = nullptr;

  if (m_scaleVB)  m_scaleVB->release();
  if (m_scaleIBx) m_scaleIBx->release();
  if (m_scaleIBy) m_scaleIBy->release();
  if (m_scaleIBz) m_scaleIBz->release();
  m_scaleVB      = nullptr;
  m_scaleIBx     = m_scaleIBy = m_scaleIBz = nullptr;
}

void EditorGizmo::Draw(EditorLineRenderer& lines,
                       const XMATRIX44& vp,
                       const XMATRIX44& world) {
  if (!lines.IsReady()) return;

  // Build a "size-scaled" world so the unit-arrow geometry becomes Size
  // units long. We do (Scale * world) so the gizmo size is decoupled
  // from the selection's own scale (the gizmo doesn't shrink with the
  // object).
  XMATRIX44 sizeMat;
  XMatScaling(sizeMat, Size, Size, Size);
  XMATRIX44 gizmoWorld = sizeMat * world;

  switch (m_mode) {
    case GizmoMode::Translate:
      if (m_translateVB) {
        if (m_translateIBx)
          lines.DrawLines(gizmoWorld, vp, XColor, m_translateVB, m_translateIBx, m_translateAxisIdxCount, 16);
        if (m_translateIBy)
          lines.DrawLines(gizmoWorld, vp, YColor, m_translateVB, m_translateIBy, m_translateAxisIdxCount, 16);
        if (m_translateIBz)
          lines.DrawLines(gizmoWorld, vp, ZColor, m_translateVB, m_translateIBz, m_translateAxisIdxCount, 16);
      }
      break;
    case GizmoMode::Rotate:
      if (m_rotateVB) {
        if (m_rotateIBx)
          lines.DrawLines(gizmoWorld, vp, XColor, m_rotateVB, m_rotateIBx, m_rotateAxisIdxCount, 16);
        if (m_rotateIBy)
          lines.DrawLines(gizmoWorld, vp, YColor, m_rotateVB, m_rotateIBy, m_rotateAxisIdxCount, 16);
        if (m_rotateIBz)
          lines.DrawLines(gizmoWorld, vp, ZColor, m_rotateVB, m_rotateIBz, m_rotateAxisIdxCount, 16);
      }
      break;
    case GizmoMode::Scale:
      if (m_scaleVB) {
        if (m_scaleIBx)
          lines.DrawLines(gizmoWorld, vp, XColor, m_scaleVB, m_scaleIBx, m_scaleAxisIdxCount, 16);
        if (m_scaleIBy)
          lines.DrawLines(gizmoWorld, vp, YColor, m_scaleVB, m_scaleIBy, m_scaleAxisIdxCount, 16);
        if (m_scaleIBz)
          lines.DrawLines(gizmoWorld, vp, ZColor, m_scaleVB, m_scaleIBz, m_scaleAxisIdxCount, 16);
      }
      break;
  }
}

} // namespace t8ditor
