#include <pch.h>
/*********************************************************
 * T850 Picking & Geometry — implementation.
 * See Framework/include/utils/Picking.h for API docs.
 *********************************************************/

#include <utils/Picking.h>
#include <cmath>
#include <algorithm>

namespace t850 {

// ── AABB helpers ──────────────────────────────────────

XVECTOR3 AABB::Center() const {
  return XVECTOR3((vMin.x + vMax.x) * 0.5f,
                  (vMin.y + vMax.y) * 0.5f,
                  (vMin.z + vMax.z) * 0.5f);
}

XVECTOR3 AABB::Extents() const {
  return XVECTOR3((vMax.x - vMin.x) * 0.5f,
                  (vMax.y - vMin.y) * 0.5f,
                  (vMax.z - vMin.z) * 0.5f);
}

void AABB::ExpandToInclude(float px, float py, float pz) {
  if (px < vMin.x) vMin.x = px;
  if (py < vMin.y) vMin.y = py;
  if (pz < vMin.z) vMin.z = pz;
  if (px > vMax.x) vMax.x = px;
  if (py > vMax.y) vMax.y = py;
  if (pz > vMax.z) vMax.z = pz;
}

void AABB::ExpandToInclude(const XVECTOR3& p) {
  ExpandToInclude(p.x, p.y, p.z);
}

void AABB::ExpandToInclude(const AABB& other) {
  ExpandToInclude(other.vMin);
  ExpandToInclude(other.vMax);
}

AABB AABB::Transformed(const XMATRIX44& m) const {
  float cx[2] = { vMin.x, vMax.x };
  float cy[2] = { vMin.y, vMax.y };
  float cz[2] = { vMin.z, vMax.z };

  AABB result;
  for (int i = 0; i < 8; ++i) {
    XVECTOR3 corner(cx[i & 1], cy[(i >> 1) & 1], cz[(i >> 2) & 1]);
    XVECTOR3 world = TransformPoint(corner, m);
    result.ExpandToInclude(world);
  }
  return result;
}

// ── Point / direction transform ───────────────────────

XVECTOR3 TransformPoint(const XVECTOR3& p, const XMATRIX44& m) {
  // Row-vector convention: result = [px py pz 1] * M
  float x = p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0];
  float y = p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1];
  float z = p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2];
  float w = p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3];
  if (std::fabs(w) > 1e-7f) {
    x /= w; y /= w; z /= w;
  }
  return XVECTOR3(x, y, z);
}

XVECTOR3 TransformDirection(const XVECTOR3& d, const XMATRIX44& m) {
  float x = d.x * m.m[0][0] + d.y * m.m[1][0] + d.z * m.m[2][0];
  float y = d.x * m.m[0][1] + d.y * m.m[1][1] + d.z * m.m[2][1];
  float z = d.x * m.m[0][2] + d.y * m.m[1][2] + d.z * m.m[2][2];
  return XVECTOR3(x, y, z);
}

// ── ScreenPointToRay ──────────────────────────────────

Ray ScreenPointToRay(float mouseX, float mouseY,
                     int vpX, int vpY, int vpW, int vpH,
                     const XMATRIX44& invVP) {
  float ndcX =  2.0f * ((mouseX - vpX + 0.5f) / (float)vpW) - 1.0f;
  float ndcY =  1.0f - 2.0f * ((mouseY - vpY + 0.5f) / (float)vpH);

  XVECTOR3 nearNDC(ndcX, ndcY, 0.0f);
  XVECTOR3 farNDC (ndcX, ndcY, 1.0f);

  XVECTOR3 nearWorld = TransformPoint(nearNDC, invVP);
  XVECTOR3 farWorld  = TransformPoint(farNDC,  invVP);

  XVECTOR3 dir(farWorld.x - nearWorld.x,
               farWorld.y - nearWorld.y,
               farWorld.z - nearWorld.z);
  dir.Normalize();

  Ray ray;
  ray.origin    = nearWorld;
  ray.direction = dir;
  return ray;
}

// ── Ray-AABB (slab method) ────────────────────────────

bool RayIntersectsAABB(const Ray& ray, const AABB& box, float& tOut) {
  float tmin = -FLT_MAX;
  float tmax =  FLT_MAX;

  const float* origin = &ray.origin.x;
  const float* dir    = &ray.direction.x;
  const float* bmin   = &box.vMin.x;
  const float* bmax   = &box.vMax.x;

  for (int i = 0; i < 3; ++i) {
    if (std::fabs(dir[i]) < 1e-8f) {
      if (origin[i] < bmin[i] || origin[i] > bmax[i])
        return false;
    } else {
      float invD = 1.0f / dir[i];
      float t1 = (bmin[i] - origin[i]) * invD;
      float t2 = (bmax[i] - origin[i]) * invD;
      if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
      if (t1 > tmin) tmin = t1;
      if (t2 < tmax) tmax = t2;
      if (tmin > tmax) return false;
    }
  }

  if (tmax < 0.0f) return false;
  tOut = (tmin >= 0.0f) ? tmin : tmax;
  return true;
}

// ── Ray-Sphere ────────────────────────────────────────

bool RayIntersectsSphere(const Ray& ray, const BoundingSphere& sphere, float& tOut) {
  XVECTOR3 oc(ray.origin.x - sphere.center.x,
              ray.origin.y - sphere.center.y,
              ray.origin.z - sphere.center.z);

  float a, b, c;
  XVecDot(a, ray.direction, ray.direction);
  XVecDot(b, oc, ray.direction);
  XVecDot(c, oc, oc);
  c -= sphere.radius * sphere.radius;
  b *= 2.0f;

  float disc = b * b - 4.0f * a * c;
  if (disc < 0.0f) return false;

  float sqrtDisc = std::sqrt(disc);
  float t1 = (-b - sqrtDisc) / (2.0f * a);
  float t2 = (-b + sqrtDisc) / (2.0f * a);

  if (t1 >= 0.0f) { tOut = t1; return true; }
  if (t2 >= 0.0f) { tOut = t2; return true; }
  return false;
}

// ── Ray-Triangle (Moller-Trumbore, double-sided) ──────

bool RayIntersectsTriangle(const Ray& ray,
                           const XVECTOR3& v0,
                           const XVECTOR3& v1,
                           const XVECTOR3& v2,
                           float& tOut, float& uOut, float& vOut) {
  XVECTOR3 e1(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
  XVECTOR3 e2(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);

  XVECTOR3 h;
  XVecCross(h, ray.direction, e2);

  float a;
  XVecDot(a, e1, h);
  if (std::fabs(a) < 1e-8f) return false;

  float f = 1.0f / a;
  XVECTOR3 s(ray.origin.x - v0.x, ray.origin.y - v0.y, ray.origin.z - v0.z);

  float u;
  XVecDot(u, s, h);
  u *= f;
  if (u < 0.0f || u > 1.0f) return false;

  XVECTOR3 q;
  XVecCross(q, s, e1);

  float v;
  XVecDot(v, ray.direction, q);
  v *= f;
  if (v < 0.0f || u + v > 1.0f) return false;

  float t;
  XVecDot(t, e2, q);
  t *= f;
  if (t < 1e-6f) return false;

  tOut = t;
  uOut = u;
  vOut = v;
  return true;
}

} // namespace t850