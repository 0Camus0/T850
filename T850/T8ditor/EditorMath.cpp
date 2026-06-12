/*********************************************************
 * T8ditor — shared editor math helpers. See header.
 *********************************************************/

#include "EditorMath.h"

#include <utils/Log.h>

#include <cmath>

namespace t8ditor {

bool NearlyEqual(float a, float b, float epsilon) {
  return std::fabs(a - b) <= epsilon;
}

bool NearlyEqualVec3(const XVECTOR3& a, const XVECTOR3& b, float epsilon) {
  return NearlyEqual(a.x, b.x, epsilon) &&
         NearlyEqual(a.y, b.y, epsilon) &&
         NearlyEqual(a.z, b.z, epsilon);
}

bool NearlyEqualTransform(const TransformState& a, const TransformState& b) {
  return NearlyEqualVec3(a.position, b.position) &&
         NearlyEqualVec3(a.eulerRad, b.eulerRad) &&
         NearlyEqualVec3(a.scale, b.scale);
}

XMATRIX44 MakePhysicsTransform(const XVECTOR3& position, const XVECTOR3& eulerRadians) {
  XMATRIX44 rx, ry, rz, rotation, translation;
  XMatRotationX(rx, eulerRadians.x);
  XMatRotationY(ry, eulerRadians.y);
  XMatRotationZ(rz, eulerRadians.z);
  XMatTranslation(translation, position.x, position.y, position.z);
  rotation = rx * ry * rz;
  return rotation * translation;
}

XMATRIX44 BuildSceneObjectWorldFromTransform(const TransformState& state) {
  XMATRIX44 scale, rx, ry, rz, translation;
  XMatScaling(scale, state.scale.x, state.scale.y, state.scale.z);
  XMatRotationX(rx, state.eulerRad.x);
  XMatRotationY(ry, state.eulerRad.y);
  XMatRotationZ(rz, state.eulerRad.z);
  XMatTranslation(translation, state.position.x, state.position.y, state.position.z);
  return scale * rx * ry * rz * translation;
}

bool BuildInverseSceneObjectWorldFromTransform(const TransformState& state, XMATRIX44& outInverse) {
  if (std::fabs(state.scale.x) < kMinEditableScale ||
      std::fabs(state.scale.y) < kMinEditableScale ||
      std::fabs(state.scale.z) < kMinEditableScale) {
    T8_LOG_ERROR("[T8ditor] Cannot update attached character: source mesh has a near-zero scale.");
    XMatIdentity(outInverse);
    return false;
  }

  XMATRIX44 invTranslation, invRz, invRy, invRx, invScale;
  XMatTranslation(invTranslation, -state.position.x, -state.position.y, -state.position.z);
  XMatRotationZ(invRz, -state.eulerRad.z);
  XMatRotationY(invRy, -state.eulerRad.y);
  XMatRotationX(invRx, -state.eulerRad.x);
  XMatScaling(invScale, 1.0f / state.scale.x, 1.0f / state.scale.y, 1.0f / state.scale.z);
  outInverse = invTranslation * invRz * invRy * invRx * invScale;
  return true;
}

void ExpandEditorAABB(t850::AABB& dst, const t850::AABB& src) {
  if (!src.IsValid()) {
    return;
  }
  if (!dst.IsValid()) {
    dst = src;
    return;
  }
  dst.ExpandToInclude(src.vMin);
  dst.ExpandToInclude(src.vMax);
}

XVECTOR3 EditorVec3FromArray(const std::array<float, 3>& value, float w) {
  return XVECTOR3(value[0], value[1], value[2], w);
}

} // namespace t8ditor
