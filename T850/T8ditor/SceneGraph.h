/*********************************************************
 * T8ditor — Scene Graph: parent-child node hierarchy with
 * recursive world-transform computation.
 *
 * Used for group transforms (multi-select, persistent groups).
 * The root node represents the group pivot (bounding box centroid).
 * Children are leaf nodes whose local transforms are offsets from
 * the pivot. ImGuizmo manipulates the root's local matrix; children
 * inherit the transform via standard scene-graph multiplication:
 *
 *   childWorld = childLocal * parentWorld   (row-vector convention)
 *
 *********************************************************/

#ifndef T8DITOR_SCENEGRAPH_H
#define T8DITOR_SCENEGRAPH_H

#include <utils/xMaths.h>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>

namespace t8ditor {

// ═══════════════════════════════════════════════════════
//  SceneNode — single node in the transform hierarchy
// ═══════════════════════════════════════════════════════
class SceneNode {
public:
  SceneNode() {
    XMatIdentity(m_local);
    XMatIdentity(m_world);
  }

  // ── Local transform (mutable — ImGuizmo writes into it) ──
  void         SetLocal(const XMATRIX44& m) { m_local = m; }
  XMATRIX44&   Local()       { return m_local; }
  const XMATRIX44& Local() const { return m_local; }

  // ── Computed world transform (read-only outside UpdateWorld) ──
  const XMATRIX44& World() const { return m_world; }

  // ── Hierarchy ──
  void SetParent(SceneNode* p) { m_parent = p; }
  SceneNode* Parent() const    { return m_parent; }

  void AddChild(SceneNode* c) {
    c->m_parent = this;
    m_children.push_back(c);
  }

  void ClearChildren() {
    for (auto* c : m_children) c->m_parent = nullptr;
    m_children.clear();
  }

  const std::vector<SceneNode*>& Children() const { return m_children; }

  // ── Recursive world-transform update ──
  // world = local * parent.world   (row-vector / D3DX convention)
  void UpdateWorld() {
    if (m_parent)
      m_world = m_local * m_parent->m_world;
    else
      m_world = m_local;

    for (auto* c : m_children)
      c->UpdateWorld();
  }

  // ── Convenience: extract world-space position ──
  XVECTOR3 WorldPosition() const {
    return XVECTOR3(m_world.m[3][0], m_world.m[3][1], m_world.m[3][2]);
  }

  // ── User payload: index into g_objects (-1 = group/pivot node) ──
  int objectIndex = -1;

private:
  XMATRIX44              m_local;
  XMATRIX44              m_world;
  SceneNode*             m_parent = nullptr;
  std::vector<SceneNode*> m_children;
};

// ═══════════════════════════════════════════════════════
//  GroupTransformHelper — manages a temporary parent node
//  for multi-select group gizmo operations.
//
//  Lifecycle:
//    1. Begin()   — creates root at centroid, children at offsets
//    2. Per frame — ImGuizmo modifies root via RootMatrix()
//    3. Update()  — recompute children's world positions
//    4. End()     — bake results, tear down nodes
// ═══════════════════════════════════════════════════════
class GroupTransformHelper {
public:
  ~GroupTransformHelper() { End(); }

  // Create the node tree: root at centroid, one child per selected object.
  void Begin(const XVECTOR3& centroid,
             const std::map<int, XVECTOR3>& positions,
             const std::map<int, XVECTOR3>& rotations,
             const std::map<int, XVECTOR3>& scales) {
    End();  // clean up any previous state

    m_centroid = centroid;
    m_origScales = scales;

    // Root node: local transform = translation to centroid (identity rotation/scale)
    XMatTranslation(m_root.Local(), centroid.x, centroid.y, centroid.z);

    // Child nodes: local transform = object local S*R plus translation offset from centroid.
    // This preserves each object's original orientation inside the group.
    for (auto& [idx, pos] : positions) {
      auto* child = new SceneNode();
      child->objectIndex = idx;

      XVECTOR3 eul(0, 0, 0);
      auto rit = rotations.find(idx);
      if (rit != rotations.end()) eul = rit->second;

      XVECTOR3 scl(1, 1, 1);
      auto sit = scales.find(idx);
      if (sit != scales.end()) scl = sit->second;

      XMATRIX44 S, Rx, Ry, Rz, T, L;
      XMatScaling(S, scl.x, scl.y, scl.z);
      XMatRotationX(Rx, eul.x);
      XMatRotationY(Ry, eul.y);
      XMatRotationZ(Rz, eul.z);
      XMatTranslation(T,
                      pos.x - centroid.x,
                      pos.y - centroid.y,
                      pos.z - centroid.z);
      L = S * Rx * Ry * Rz * T;
      child->SetLocal(L);

      m_root.AddChild(child);
      m_childNodes.push_back(child);
    }

    // Initial world-transform pass
    m_root.UpdateWorld();
    m_active = true;
  }

  // Pointer to root's local matrix — pass this to ImGuizmo::Manipulate.
  // ImGuizmo reads and writes it in place each frame.
  float* RootMatrix() { return &m_root.Local().m[0][0]; }

  // After ImGuizmo modifies the root matrix, recompute all children.
  void Update() {
    if (!m_active) return;
    m_root.UpdateWorld();
  }

  // Get a child's computed world position.
  XVECTOR3 ChildWorldPosition(int objectIndex) const {
    for (auto* c : m_childNodes)
      if (c->objectIndex == objectIndex)
        return c->WorldPosition();
    return XVECTOR3(0, 0, 0);
  }

  // Get a child's full world matrix (for rotation/scale decomposition).
  XMATRIX44 ChildWorldMatrix(int objectIndex) const {
    for (auto* c : m_childNodes)
      if (c->objectIndex == objectIndex)
        return c->World();
    XMATRIX44 id;
    XMatIdentity(id);
    return id;
  }

  // Extract uniform scale factor from the root's current transform.
  float RootUniformScale() const {
    const auto& m = m_root.Local();
    float sx = std::sqrt(m.m[0][0]*m.m[0][0] +
                         m.m[0][1]*m.m[0][1] +
                         m.m[0][2]*m.m[0][2]);
    return sx;
  }

  // Original scale of an object (before group transform started).
  XVECTOR3 OriginalScale(int idx) const {
    auto it = m_origScales.find(idx);
    return (it != m_origScales.end()) ? it->second : XVECTOR3(1, 1, 1);
  }

  bool IsActive() const { return m_active; }

  void End() {
    m_root.ClearChildren();
    for (auto* c : m_childNodes) delete c;
    m_childNodes.clear();
    m_origScales.clear();
    m_active = false;
  }

private:
  SceneNode               m_root;
  std::vector<SceneNode*> m_childNodes;
  std::map<int, XVECTOR3> m_origScales;
  XVECTOR3                m_centroid;
  bool                    m_active = false;
};

} // namespace t8ditor

#endif // T8DITOR_SCENEGRAPH_H
