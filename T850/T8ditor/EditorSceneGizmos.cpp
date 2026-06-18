/*********************************************************
 * T8ditor — Viewport gizmos for cameras and lights.
 *********************************************************/

#include "EditorSceneGizmos.h"
#include <utils/xMaths.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace t8ditor {

namespace {

const XVECTOR3 kSelectedColor(1.0f, 1.0f, 0.3f, 1.0f);   // yellow highlight
const XVECTOR3 kSelectedCameraColor(0.1f, 1.0f, 0.2f, 1.0f); // green frustum highlight
const XVECTOR3 kCameraColor(0.4f, 0.7f, 1.0f, 1.0f);     // light blue
const XVECTOR3 kDirLightColor(1.0f, 0.95f, 0.5f, 1.0f);  // warm yellow
const XVECTOR3 kOmniLightColor(1.0f, 0.85f, 0.3f, 1.0f); // orange

// Build a frustum wireframe (8 corners + 12 edges) from camera params.
// Returns VB/IB for the line list. Caller draws via EditorLineRenderer.
void BuildFrustumGeometry(const SceneCamera& cam,
                          std::vector<float>& verts,
                          std::vector<unsigned short>& idx) {
  // Compute the 8 frustum corners in world space.
  // Camera looks from position toward target.
  XVECTOR3 forward(cam.target.x - cam.position.x,
                   cam.target.y - cam.position.y,
                   cam.target.z - cam.position.z);
  float flen = forward.Length();
  if (flen < 0.0001f) forward = XVECTOR3(0, 0, 1);
  else { forward.x /= flen; forward.y /= flen; forward.z /= flen; }

  // Right = cross(forward, worldUp)
  XVECTOR3 worldUp(0, 1, 0);
  XVECTOR3 right;
  XVecCross(right, forward, worldUp);
  if (right.Length() < 0.0001f) {
    worldUp = XVECTOR3(0, 0, 1);
    XVecCross(right, forward, worldUp);
  }
  right.Normalize();

  XVECTOR3 up;
  XVecCross(up, right, forward);
  up.Normalize();

  float nearHalfH, nearHalfW, farHalfH, farHalfW;

  if (cam.type == CameraType::Perspective) {
    float fovRad = cam.fovDeg * (xPI / 180.0f);
    float aspect = 16.0f / 9.0f; // default aspect
    nearHalfH = cam.nearPlane * std::tan(fovRad * 0.5f);
    nearHalfW = nearHalfH * aspect;
    farHalfH  = cam.farPlane * std::tan(fovRad * 0.5f);
    farHalfW  = farHalfH * aspect;
  } else {
    nearHalfW = farHalfW = cam.orthoW * 0.5f;
    nearHalfH = farHalfH = cam.orthoH * 0.5f;
  }

  // Clamp far plane for visualization (don't draw a frustum to infinity)
  float visFar = (cam.farPlane > 50.0f) ? 50.0f : cam.farPlane;
  if (cam.type == CameraType::Perspective) {
    farHalfH = visFar * std::tan(cam.fovDeg * (xPI / 180.0f) * 0.5f);
    farHalfW = farHalfH * (16.0f / 9.0f);
  }

  // Near plane corners (in world space)
  XVECTOR3 nc(cam.position.x + forward.x * cam.nearPlane,
              cam.position.y + forward.y * cam.nearPlane,
              cam.position.z + forward.z * cam.nearPlane);
  XVECTOR3 fc(cam.position.x + forward.x * visFar,
              cam.position.y + forward.y * visFar,
              cam.position.z + forward.z * visFar);

  auto Corner = [&](const XVECTOR3& center, float hw, float hh, int cx, int cy) -> XVECTOR3 {
    float sx = (cx == 0) ? -hw : hw;
    float sy = (cy == 0) ? -hh : hh;
    return XVECTOR3(center.x + right.x * sx + up.x * sy,
                    center.y + right.y * sx + up.y * sy,
                    center.z + right.z * sx + up.z * sy);
  };

  // 8 corners: near (0-3), far (4-7)
  XVECTOR3 corners[8] = {
    Corner(nc, nearHalfW, nearHalfH, 0, 0), // 0: near bottom-left
    Corner(nc, nearHalfW, nearHalfH, 1, 0), // 1: near bottom-right
    Corner(nc, nearHalfW, nearHalfH, 1, 1), // 2: near top-right
    Corner(nc, nearHalfW, nearHalfH, 0, 1), // 3: near top-left
    Corner(fc, farHalfW,  farHalfH,  0, 0), // 4: far bottom-left
    Corner(fc, farHalfW,  farHalfH,  1, 0), // 5: far bottom-right
    Corner(fc, farHalfW,  farHalfH,  1, 1), // 6: far top-right
    Corner(fc, farHalfW,  farHalfH,  0, 1), // 7: far top-left
  };

  unsigned short base = (unsigned short)(verts.size() / 4);
  for (int i = 0; i < 8; ++i) {
    verts.push_back(corners[i].x);
    verts.push_back(corners[i].y);
    verts.push_back(corners[i].z);
    verts.push_back(1.0f);
  }

  // 12 edges: 4 near, 4 far, 4 connecting
  auto L = [&](int a, int b) { idx.push_back(base + a); idx.push_back(base + b); };
  L(0,1); L(1,2); L(2,3); L(3,0); // near quad
  L(4,5); L(5,6); L(6,7); L(7,4); // far quad
  L(0,4); L(1,5); L(2,6); L(3,7); // connecting edges

  // Mini wire camera body at the camera origin, oriented with the frustum.
  const float targetDistance = (std::max)(0.001f,
      XVECTOR3(cam.target.x - cam.position.x,
               cam.target.y - cam.position.y,
               cam.target.z - cam.position.z).Length());
  const float cameraScale = std::clamp(
      cam.type == CameraType::Orthographic
          ? (std::max)(cam.orthoW, cam.orthoH) * 0.02f
          : targetDistance * 0.035f,
      0.5f,
      4.0f);
  const float bodyHalfW = cameraScale * 0.55f;
  const float bodyHalfH = cameraScale * 0.35f;
  const float bodyHalfD = cameraScale * 0.30f;
  const XVECTOR3 bodyCenter(
      cam.position.x - forward.x * bodyHalfD,
      cam.position.y - forward.y * bodyHalfD,
      cam.position.z - forward.z * bodyHalfD);
  auto BodyPoint = [&](float rx, float uy, float fz) -> XVECTOR3 {
    return XVECTOR3(bodyCenter.x + right.x * rx + up.x * uy + forward.x * fz,
                    bodyCenter.y + right.y * rx + up.y * uy + forward.y * fz,
                    bodyCenter.z + right.z * rx + up.z * uy + forward.z * fz);
  };
  const unsigned short bodyBase = static_cast<unsigned short>(verts.size() / 4);
  const XVECTOR3 bodyCorners[8] = {
      BodyPoint(-bodyHalfW, -bodyHalfH, -bodyHalfD), BodyPoint( bodyHalfW, -bodyHalfH, -bodyHalfD),
      BodyPoint( bodyHalfW,  bodyHalfH, -bodyHalfD), BodyPoint(-bodyHalfW,  bodyHalfH, -bodyHalfD),
      BodyPoint(-bodyHalfW, -bodyHalfH,  bodyHalfD), BodyPoint( bodyHalfW, -bodyHalfH,  bodyHalfD),
      BodyPoint( bodyHalfW,  bodyHalfH,  bodyHalfD), BodyPoint(-bodyHalfW,  bodyHalfH,  bodyHalfD)
  };
  for (const XVECTOR3& c : bodyCorners) {
    verts.push_back(c.x); verts.push_back(c.y); verts.push_back(c.z); verts.push_back(1.0f);
  }
  L(bodyBase+0,bodyBase+1); L(bodyBase+1,bodyBase+2); L(bodyBase+2,bodyBase+3); L(bodyBase+3,bodyBase+0);
  L(bodyBase+4,bodyBase+5); L(bodyBase+5,bodyBase+6); L(bodyBase+6,bodyBase+7); L(bodyBase+7,bodyBase+4);
  L(bodyBase+0,bodyBase+4); L(bodyBase+1,bodyBase+5); L(bodyBase+2,bodyBase+6); L(bodyBase+3,bodyBase+7);

  const unsigned short lensBase = static_cast<unsigned short>(verts.size() / 4);
  const XVECTOR3 lensCenter(
      cam.position.x + forward.x * cameraScale * 0.38f,
      cam.position.y + forward.y * cameraScale * 0.38f,
      cam.position.z + forward.z * cameraScale * 0.38f);
  const XVECTOR3 lensTop(
      lensCenter.x + up.x * cameraScale * 0.20f,
      lensCenter.y + up.y * cameraScale * 0.20f,
      lensCenter.z + up.z * cameraScale * 0.20f);
  const XVECTOR3 lensBottom(
      lensCenter.x - up.x * cameraScale * 0.20f,
      lensCenter.y - up.y * cameraScale * 0.20f,
      lensCenter.z - up.z * cameraScale * 0.20f);
  const XVECTOR3 lensLeft(
      lensCenter.x - right.x * cameraScale * 0.20f,
      lensCenter.y - right.y * cameraScale * 0.20f,
      lensCenter.z - right.z * cameraScale * 0.20f);
  const XVECTOR3 lensRight(
      lensCenter.x + right.x * cameraScale * 0.20f,
      lensCenter.y + right.y * cameraScale * 0.20f,
      lensCenter.z + right.z * cameraScale * 0.20f);
  const XVECTOR3 lensTip(
      cam.position.x + forward.x * cameraScale * 0.75f,
      cam.position.y + forward.y * cameraScale * 0.75f,
      cam.position.z + forward.z * cameraScale * 0.75f);
  const XVECTOR3 lensPoints[5] = { lensTop, lensRight, lensBottom, lensLeft, lensTip };
  for (const XVECTOR3& p : lensPoints) {
    verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z); verts.push_back(1.0f);
  }
  L(lensBase+0,lensBase+1); L(lensBase+1,lensBase+2); L(lensBase+2,lensBase+3); L(lensBase+3,lensBase+0);
  L(lensBase+0,lensBase+4); L(lensBase+1,lensBase+4); L(lensBase+2,lensBase+4); L(lensBase+3,lensBase+4);

  const unsigned short handleBase = static_cast<unsigned short>(verts.size() / 4);
  const XVECTOR3 handleA = BodyPoint(-bodyHalfW * 0.45f, bodyHalfH, -bodyHalfD * 0.20f);
  const XVECTOR3 handleB = BodyPoint( bodyHalfW * 0.45f, bodyHalfH, -bodyHalfD * 0.20f);
  const XVECTOR3 handleC = BodyPoint( bodyHalfW * 0.30f, bodyHalfH + cameraScale * 0.28f, -bodyHalfD * 0.10f);
  const XVECTOR3 handleD = BodyPoint(-bodyHalfW * 0.30f, bodyHalfH + cameraScale * 0.28f, -bodyHalfD * 0.10f);
  const XVECTOR3 handlePoints[4] = { handleA, handleB, handleC, handleD };
  for (const XVECTOR3& p : handlePoints) {
    verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z); verts.push_back(1.0f);
  }
  L(handleBase+0,handleBase+1); L(handleBase+1,handleBase+2); L(handleBase+2,handleBase+3); L(handleBase+3,handleBase+0);

  if (cam.type == CameraType::Perspective) {
    // Small wireframe cube at target position (8 corners + 12 edges)
    float th = 0.3f; // half-size
    XVECTOR3 tgt = cam.target;
    unsigned short tb = (unsigned short)(verts.size() / 4);
    float tc[8][3] = {
      {tgt.x-th, tgt.y-th, tgt.z-th}, {tgt.x+th, tgt.y-th, tgt.z-th},
      {tgt.x+th, tgt.y+th, tgt.z-th}, {tgt.x-th, tgt.y+th, tgt.z-th},
      {tgt.x-th, tgt.y-th, tgt.z+th}, {tgt.x+th, tgt.y-th, tgt.z+th},
      {tgt.x+th, tgt.y+th, tgt.z+th}, {tgt.x-th, tgt.y+th, tgt.z+th},
    };
    for (int i = 0; i < 8; ++i) {
      verts.push_back(tc[i][0]); verts.push_back(tc[i][1]); verts.push_back(tc[i][2]); verts.push_back(1.0f);
    }
    L(tb+0,tb+1); L(tb+1,tb+2); L(tb+2,tb+3); L(tb+3,tb+0);
    L(tb+4,tb+5); L(tb+5,tb+6); L(tb+6,tb+7); L(tb+7,tb+4);
    L(tb+0,tb+4); L(tb+1,tb+5); L(tb+2,tb+6); L(tb+3,tb+7);

    // Line from camera position to target
    unsigned short pl = (unsigned short)(verts.size() / 4);
    verts.push_back(cam.position.x); verts.push_back(cam.position.y); verts.push_back(cam.position.z); verts.push_back(1.0f);
    verts.push_back(cam.target.x); verts.push_back(cam.target.y); verts.push_back(cam.target.z); verts.push_back(1.0f);
    idx.push_back(pl); idx.push_back(pl+1);
  }
}

// Build a circle of line segments in world space.
void BuildWireCircle(const XVECTOR3& center, float radius,
                     const XVECTOR3& axis1, const XVECTOR3& axis2,
                     int segments,
                     std::vector<float>& verts,
                     std::vector<unsigned short>& idx) {
  unsigned short base = (unsigned short)(verts.size() / 4);
  const float twoPi = 2.0f * xPI;
  for (int i = 0; i < segments; ++i) {
    float t = (float)i / (float)segments * twoPi;
    float c = std::cos(t) * radius;
    float s = std::sin(t) * radius;
    verts.push_back(center.x + axis1.x * c + axis2.x * s);
    verts.push_back(center.y + axis1.y * c + axis2.y * s);
    verts.push_back(center.z + axis1.z * c + axis2.z * s);
    verts.push_back(1.0f);
  }
  for (int i = 0; i < segments; ++i) {
    idx.push_back(base + i);
    idx.push_back(base + ((i + 1) % segments));
  }
}

// Build a directional light arrow (long line + arrowhead).
void BuildDirLightArrow(const SceneLight& lt,
                        std::vector<float>& verts,
                        std::vector<unsigned short>& idx) {
  XVECTOR3 dir = lt.direction;
  dir.Normalize();
  float arrowLen = 3.0f;

  XVECTOR3 tip(lt.position.x + dir.x * arrowLen,
               lt.position.y + dir.y * arrowLen,
               lt.position.z + dir.z * arrowLen);

  // Perpendicular vectors for arrowhead
  XVECTOR3 perp(0, 1, 0);
  XVECTOR3 cross;
  XVecCross(cross, dir, perp);
  if (cross.Length() < 0.001f) { perp = XVECTOR3(1, 0, 0); XVecCross(cross, dir, perp); }
  cross.Normalize();
  XVECTOR3 cross2;
  XVecCross(cross2, dir, cross);
  cross2.Normalize();

  float headStart = 0.75f * arrowLen;
  float headSize = 0.3f;
  XVECTOR3 hs(lt.position.x + dir.x * headStart,
              lt.position.y + dir.y * headStart,
              lt.position.z + dir.z * headStart);

  unsigned short base = (unsigned short)(verts.size() / 4);
  // Origin
  verts.push_back(lt.position.x); verts.push_back(lt.position.y); verts.push_back(lt.position.z); verts.push_back(1.0f);
  // Tip
  verts.push_back(tip.x); verts.push_back(tip.y); verts.push_back(tip.z); verts.push_back(1.0f);
  // 4 barbs
  for (int i = 0; i < 4; ++i) {
    float cx = (i < 2) ? headSize : -headSize;
    float cy = (i % 2 == 0) ? headSize : -headSize;
    XVECTOR3* a = (i < 2) ? &cross : &cross2;
    float sign = (i % 2 == 0) ? 1.0f : -1.0f;
    verts.push_back(hs.x + a->x * sign * headSize);
    verts.push_back(hs.y + a->y * sign * headSize);
    verts.push_back(hs.z + a->z * sign * headSize);
    verts.push_back(1.0f);
  }

  // Shaft + 4 barbs
  idx.push_back(base); idx.push_back(base + 1);
  for (int i = 0; i < 4; ++i) {
    idx.push_back(base + 2 + i); idx.push_back(base + 1);
  }
}

} // anonymous namespace

// Simple hash combiner for dirty checking
static uint64_t HashFloats(const float* f, int n) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (int i = 0; i < n; i++) {
    uint32_t bits;
    memcpy(&bits, &f[i], 4);
    h ^= bits;
    h *= 0x100000001b3ULL;
  }
  return h;
}

// ── Camera gizmo ──────────────────────────────────────

void DrawCameraGizmo(EditorLineRenderer& lines, const XMATRIX44& vp,
                     const SceneCamera& cam, bool selected) {
  if (!lines.IsReady()) return;

  // Per-entity cache via cam.gizmo
  float params[] = { cam.fovDeg, cam.orthoW, cam.orthoH, cam.nearPlane, cam.farPlane,
                     cam.position.x, cam.position.y, cam.position.z,
                     cam.target.x, cam.target.y, cam.target.z,
                     (float)cam.type };
  uint64_t h = HashFloats(params, 12);

  if (cam.gizmo.vb == nullptr || cam.gizmo.hash != h) {
    std::vector<float> verts;
    std::vector<unsigned short> idx;
    BuildFrustumGeometry(cam, verts, idx);
    if (verts.empty() || idx.empty()) return;
    cam.gizmo.vb = EditorLineRenderer::CreatePositionVB(verts.data(), (unsigned)(verts.size() / 4));
    cam.gizmo.ib = EditorLineRenderer::CreateIndexBuffer16(idx.data(), (unsigned)idx.size());
    cam.gizmo.count = (unsigned)idx.size();
    cam.gizmo.hash = h;
  }
  if (!cam.gizmo.vb || !cam.gizmo.ib) return;

  XMATRIX44 identity;
  XMatIdentity(identity);
  const XVECTOR3& color = selected ? kSelectedCameraColor : kCameraColor;
  lines.DrawLines(identity, vp, color, cam.gizmo.vb, cam.gizmo.ib, cam.gizmo.count, 16);
}

// ── Light gizmo ───────────────────────────────────────

void DrawLightGizmo(EditorLineRenderer& lines, const XMATRIX44& vp,
                    const SceneLight& lt, bool selected) {
  if (!lines.IsReady()) return;

  // Per-entity cache via lt.gizmo
  float params[] = { lt.position.x, lt.position.y, lt.position.z,
                     lt.direction.x, lt.direction.y, lt.direction.z,
                     lt.radius, (float)lt.type };
  uint64_t h = HashFloats(params, 8);

  if (lt.gizmo.vb == nullptr || lt.gizmo.hash != h) {
    std::vector<float> verts;
    std::vector<unsigned short> idx;

    if (lt.type == EditorLightType::Directional) {
      BuildDirLightArrow(lt, verts, idx);
    } else {
      XVECTOR3 ax(1,0,0), ay(0,1,0), az(0,0,1);
      BuildWireCircle(lt.position, lt.radius, ax, ay, 32, verts, idx);
      BuildWireCircle(lt.position, lt.radius, ax, az, 32, verts, idx);
      BuildWireCircle(lt.position, lt.radius, ay, az, 32, verts, idx);

      float cs = 0.3f;
      unsigned short base = (unsigned short)(verts.size() / 4);
      verts.push_back(lt.position.x - cs); verts.push_back(lt.position.y); verts.push_back(lt.position.z); verts.push_back(1.0f);
      verts.push_back(lt.position.x + cs); verts.push_back(lt.position.y); verts.push_back(lt.position.z); verts.push_back(1.0f);
      verts.push_back(lt.position.x); verts.push_back(lt.position.y - cs); verts.push_back(lt.position.z); verts.push_back(1.0f);
      verts.push_back(lt.position.x); verts.push_back(lt.position.y + cs); verts.push_back(lt.position.z); verts.push_back(1.0f);
      verts.push_back(lt.position.x); verts.push_back(lt.position.y); verts.push_back(lt.position.z - cs); verts.push_back(1.0f);
      verts.push_back(lt.position.x); verts.push_back(lt.position.y); verts.push_back(lt.position.z + cs); verts.push_back(1.0f);
      idx.push_back(base); idx.push_back(base + 1);
      idx.push_back(base + 2); idx.push_back(base + 3);
      idx.push_back(base + 4); idx.push_back(base + 5);
    }

    if (verts.empty() || idx.empty()) return;
    lt.gizmo.vb = EditorLineRenderer::CreatePositionVB(verts.data(), (unsigned)(verts.size() / 4));
    lt.gizmo.ib = EditorLineRenderer::CreateIndexBuffer16(idx.data(), (unsigned)idx.size());
    lt.gizmo.count = (unsigned)idx.size();
    lt.gizmo.hash = h;
  }
  if (!lt.gizmo.vb || !lt.gizmo.ib) return;

  XMATRIX44 identity;
  XMatIdentity(identity);

  const XVECTOR3& baseColor = (lt.type == EditorLightType::Directional) ? kDirLightColor : kOmniLightColor;
  const XVECTOR3& color = selected ? kSelectedColor : baseColor;
  lines.DrawLines(identity, vp, color, lt.gizmo.vb, lt.gizmo.ib, lt.gizmo.count, 16);
}

} // namespace t8ditor
