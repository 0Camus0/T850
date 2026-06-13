/*********************************************************
 * T8ditor — Ragdoll editor support helpers (shared).
 *
 * Declarations for the ragdoll authoring / gizmo-math helpers and a
 * few editor projection helpers that are defined in EditorApp.cpp but
 * also used by RagdollEditorPanel.cpp. Also exposes the deferred-render
 * scratch globals (quads + dummy textures) the hosted ragdoll viewport
 * reuses, via accessors so the moved code is unchanged.
 *********************************************************/

#ifndef T8DITOR_EDITOR_RAGDOLL_SUPPORT_H
#define T8DITOR_EDITOR_RAGDOLL_SUPPORT_H

#include "SceneObject.h"   // SceneObject, t850::AABB/Ray, PrimitiveInst, scene descs, PhysicsShapeType

#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <physics/PhysicsAuthoring.h>
#include <physics/PhysicsTypes.h>
#include <scene/PrimitiveInstance.h>

#include <imgui.h>
#include <string>

namespace t850 { class Texture; }

namespace t8ditor {

// Minimum authored shape extent for ragdoll bodies.
constexpr float kRagdollEditorMinShapeExtent = 0.001f;

// ── Ragdoll authoring / meta ─────────────────────────
float EstimateRagdollRadius(const SceneObject& obj);
t850::PhysicsRagdollBuildSettings BuildEditorRagdollSettings(const SceneObject& obj);
const char* RagdollShapeTypeName(t850::PhysicsShapeType type);
const char* RagdollJointTypeName(t850::PhysicsRagdollJointType type);
void EnsureEditorRagdollState(t850::PhysicsRagdollAuthoringDesc& authoring);
void SyncRagdollMetaFromObject(SceneObject& obj);
bool InputTextString(const char* label, std::string& value);
t850::scene::SceneObjectPhysicsDesc& EnsurePhysicsMeta(SceneObject& obj);
t850::scene::SceneObjectNavigationDesc& EnsureNavigationMeta(SceneObject& obj);
t850::scene::SceneObjectRagdollDesc& EnsureRagdollMeta(SceneObject& obj);
void EnsureRagdollHierarchyState(SceneObject& obj);
std::string RagdollHierarchyBodyLabel(const SceneObject& obj, int bodyIndex);

// ── Ragdoll gizmo / vector math ──────────────────────
void RagdollMatrixToComponents(const XMATRIX44& matrix, float translation[3], float rotationDeg[3], float scale[3]);
XMATRIX44 RagdollMatrixFromComponents(const float translation[3], const float rotationDeg[3], const float scale[3]);
float RagdollDot3(const XVECTOR3& a, const XVECTOR3& b);
float RagdollLength3(const XVECTOR3& value);
XVECTOR3 RagdollCross3(const XVECTOR3& a, const XVECTOR3& b);
XVECTOR3 RagdollNormalize3(const XVECTOR3& value, const XVECTOR3& fallback);
XVECTOR3 RagdollMatrixAxis(const XMATRIX44& matrix, int axis);
XVECTOR3 RagdollMatrixTranslation(const XMATRIX44& matrix);
bool RagdollRayPlaneIntersection(const t850::Ray& ray, const XVECTOR3& planePoint,
                                 const XVECTOR3& planeNormal, XVECTOR3& outPoint);
bool RagdollClosestRayAxisParameter(const t850::Ray& ray, const XVECTOR3& axisOrigin,
                                    const XVECTOR3& axisDirection, float& outParameter);
float RagdollDistancePointToSegmentSq(const ImVec2& p, const ImVec2& a, const ImVec2& b);
ImU32 RagdollAxisColor(int axis, bool active);
float RagdollAxisCoord(const XVECTOR3& value, int axis);
void RagdollSetAxisCoord(XVECTOR3& value, int axis, float coord);
XVECTOR3 RagdollClampBoxHalfExtents(const XVECTOR3& halfExtents);
XVECTOR3 RagdollTransformVectorNoTranslation(const XVECTOR3& vector, const XMATRIX44& matrix);
XVECTOR3 RagdollRotateVectorAroundAxis(const XVECTOR3& vector, const XVECTOR3& axisWorld, float angleRadians);
bool RagdollIsValidAxis(const XVECTOR3& axis);
void RagdollNormalizeJointFrameAxes(XVECTOR3& twist, XVECTOR3& plane,
                                    const XVECTOR3& fallbackTwist, const XVECTOR3& fallbackPlane);

// ── Editor projection helpers ────────────────────────
ImVec2 WorldToScreen(const XVECTOR3& p, const XMATRIX44& vp, int w, int h);
bool ProjectAABBToScreenRect(const t850::AABB& box, const XMATRIX44& vp,
                             int viewW, int viewH,
                             float& sMinX, float& sMinY,
                             float& sMaxX, float& sMaxY);
t850::Ray BuildEditorCameraRay(const ::Camera& camera, float mouseX, float mouseY, int viewW, int viewH);

// ── Deferred-render scratch globals (shared with EditorApp OnDraw) ──
using EditorQuadArray = t850::PrimitiveInst[8];
EditorQuadArray& EditorDeferredQuads();
t850::Texture*& EditorDummyWhiteTex();
int& EditorDummyEnvMapIdx();

} // namespace t8ditor

#endif // T8DITOR_EDITOR_RAGDOLL_SUPPORT_H
