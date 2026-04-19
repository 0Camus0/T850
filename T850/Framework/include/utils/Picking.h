/*********************************************************
 * T850 Picking & Geometry Primitives
 * -----------------------------------
 * Reusable ray-casting, bounding-volume, and intersection
 * utilities for both editor tools and runtime gameplay.
 *
 * Convention notes (match the rest of xMaths):
 *   - Row-vector, row-major matrices: v_clip = v_world * VP
 *   - NDC depth range is 0..1 (D3D-style) for both LH and RH
 *   - XVECTOR3 carries a .w component; picking code ignores it
 *     for AABB storage and treats it as 1.0 for point transforms
 *     and 0.0 for direction transforms.
 *********************************************************/

#ifndef T800_PICKING_H
#define T800_PICKING_H

#include <utils/xMaths.h>
#include <cfloat>

namespace t800 {

// ── Geometric primitives ─────────────────────────────

struct Ray {
  XVECTOR3 origin;     // world-space start point
  XVECTOR3 direction;  // normalized world-space direction
};

struct AABB {
  XVECTOR3 vMin;
  XVECTOR3 vMax;

  AABB() : vMin(FLT_MAX, FLT_MAX, FLT_MAX), vMax(-FLT_MAX, -FLT_MAX, -FLT_MAX) {}
  AABB(const XVECTOR3& mn, const XVECTOR3& mx) : vMin(mn), vMax(mx) {}

  XVECTOR3 Center()  const;
  XVECTOR3 Extents() const;

  void ExpandToInclude(float px, float py, float pz);
  void ExpandToInclude(const XVECTOR3& p);
  void ExpandToInclude(const AABB& other);

  bool IsValid() const { return vMin.x <= vMax.x; }

  // Transform this AABB by a world matrix (8-corner method).
  // Returns a new axis-aligned box that encloses the transformed corners.
  AABB Transformed(const XMATRIX44& m) const;
};

struct BoundingSphere {
  XVECTOR3 center;
  float    radius = 0.0f;
};

// Pure geometric hit result — no scene/object IDs.
struct HitResult {
  bool     hit      = false;
  float    distance = FLT_MAX;  // ray parameter t (hit = origin + direction * t)
  XVECTOR3 point;               // world-space hit point
};

// ── Ray construction ─────────────────────────────────

// Build a world-space ray from a screen-space pixel coordinate.
// (mouseX, mouseY) are in pixels; (vpX, vpY, vpW, vpH) define the
// viewport rectangle in pixels. Adds +0.5 pixel-center offset internally.
// `invVP` is the inverse of (View * Projection).
// Works correctly for both perspective and orthographic cameras.
Ray ScreenPointToRay(float mouseX, float mouseY,
                     int vpX, int vpY, int vpW, int vpH,
                     const XMATRIX44& invVP);

// ── Intersection tests ───────────────────────────────
// All return true on hit and fill `tOut` with the ray parameter.

bool RayIntersectsAABB(const Ray& ray, const AABB& box, float& tOut);

bool RayIntersectsSphere(const Ray& ray, const BoundingSphere& sphere, float& tOut);

// Möller–Trumbore ray-triangle test (double-sided by default).
bool RayIntersectsTriangle(const Ray& ray,
                           const XVECTOR3& v0,
                           const XVECTOR3& v1,
                           const XVECTOR3& v2,
                           float& tOut, float& uOut, float& vOut);

// ── Point transform helpers ──────────────────────────
// (The engine has no XVECTOR3*XMATRIX44 operator.)

// Transform a point (w=1) by a 4×4 matrix, with perspective divide.
XVECTOR3 TransformPoint(const XVECTOR3& p, const XMATRIX44& m);

// Transform a direction (w=0) by a 4×4 matrix (no translation, no divide).
XVECTOR3 TransformDirection(const XVECTOR3& d, const XMATRIX44& m);

} // namespace t800

#endif // T800_PICKING_H
