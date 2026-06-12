/*********************************************************
 * T8ditor — shared editor math helpers.
 *
 * Stateless transform / comparison / AABB utilities used
 * across the editor (app, panels, renderer). Extracted from
 * EditorApp.cpp so the future split translation units can
 * share one implementation.
 *********************************************************/

#ifndef T8DITOR_EDITOR_MATH_H
#define T8DITOR_EDITOR_MATH_H

#include <utils/xMaths.h>
#include <utils/Picking.h>   // t850::AABB
#include "UndoRedo.h"        // TransformState

#include <array>

namespace t8ditor {

// Shared editor constants (internal linkage per translation unit).
const float     kRadToDeg = 180.0f / xPI;
const float     kDegToRad = xPI / 180.0f;
constexpr float kMinEditableScale = 0.000001f;

// ── Float / vector / transform comparison ────────────
bool NearlyEqual(float a, float b, float epsilon = 0.00001f);
bool NearlyEqualVec3(const XVECTOR3& a, const XVECTOR3& b, float epsilon = 0.00001f);
bool NearlyEqualTransform(const TransformState& a, const TransformState& b);

// ── Transform builders (row-vector / D3DX convention) ──
// Rotation*Translation (no scale) — used for physics body transforms.
XMATRIX44 MakePhysicsTransform(const XVECTOR3& position, const XVECTOR3& eulerRadians);
// Scale*Rotation*Translation world matrix from a TransformState.
XMATRIX44 BuildSceneObjectWorldFromTransform(const TransformState& state);
// Inverse of BuildSceneObjectWorldFromTransform. Returns false (and identity)
// when any scale component is below kMinEditableScale.
bool BuildInverseSceneObjectWorldFromTransform(const TransformState& state, XMATRIX44& outInverse);

// ── AABB / vector helpers ────────────────────────────
void ExpandEditorAABB(t850::AABB& dst, const t850::AABB& src);
XVECTOR3 EditorVec3FromArray(const std::array<float, 3>& value, float w = 0.0f);

} // namespace t8ditor

#endif // T8DITOR_EDITOR_MATH_H
