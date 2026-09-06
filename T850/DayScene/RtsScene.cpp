#include <pch.h>

#include <RtsScene.h>

#include <core/Config.h>
#include <utils/Log.h>
#include <utils/Picking.h>
#include <utils/ResourceLocator.h>
#include <scene/IBLResources.h>
#include <video/BaseDriver.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4267)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// ── Vector helpers (the engine's XVECTOR3 has no free Dot/Cross) ───────────
namespace {
float Dot3(const XVECTOR3& a, const XVECTOR3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
XVECTOR3 Cross3(const XVECTOR3& a, const XVECTOR3& b) {
  return XVECTOR3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x, 0.0f);
}
float Clampf(float v, float lo, float hi) { return (std::max)(lo, (std::min)(hi, v)); }
float DistXZ(const XVECTOR3& a, const XVECTOR3& b) {
  float dx = a.x - b.x, dz = a.z - b.z;
  return std::sqrt(dx*dx + dz*dz);
}
XVECTOR3 NormalizedXZ(const XVECTOR3& v) {
  float l = std::sqrt(v.x*v.x + v.z*v.z);
  if (l < 1e-6f) return XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  return XVECTOR3(v.x/l, 0.0f, v.z/l, 0.0f);
}
} // namespace

// ── Glaze JSON metadata for the data-driven rts block ──────────────────────
template <> struct glz::meta<rtsdata::Vec3f> {
  using T = rtsdata::Vec3f;
  static constexpr auto value = glz::object("x", &T::x, "y", &T::y, "z", &T::z);
};
template <> struct glz::meta<rtsdata::Color> {
  using T = rtsdata::Color;
  static constexpr auto value = glz::object("x", &T::x, "y", &T::y, "z", &T::z, "w", &T::w);
};
template <> struct glz::meta<rtsdata::ZoneBox> {
  using T = rtsdata::ZoneBox;
  static constexpr auto value = glz::object("x", &T::x, "z", &T::z, "half_x", &T::half_x, "half_z", &T::half_z);
};
template <> struct glz::meta<rtsdata::Field> {
  using T = rtsdata::Field;
  static constexpr auto value = glz::object("size_x", &T::size_x, "size_z", &T::size_z, "y", &T::y);
};
template <> struct glz::meta<rtsdata::CameraCfg> {
  using T = rtsdata::CameraCfg;
  static constexpr auto value = glz::object("position", &T::position, "target", &T::target,
    "fov_deg", &T::fov_deg, "near", &T::near, "far", &T::far);
};
template <> struct glz::meta<rtsdata::LightCfg> {
  using T = rtsdata::LightCfg;
  static constexpr auto value = glz::object("direction", &T::direction, "color", &T::color,
    "intensity", &T::intensity, "ambient", &T::ambient);
};
template <> struct glz::meta<rtsdata::UnitType> {
  using T = rtsdata::UnitType;
  static constexpr auto value = glz::object("id", &T::id, "health", &T::health, "attack", &T::attack,
    "range", &T::range, "speed", &T::speed, "radius", &T::radius, "scale", &T::scale);
};
template <> struct glz::meta<rtsdata::Team> {
  using T = rtsdata::Team;
  static constexpr auto value = glz::object("id", &T::id, "color", &T::color,
    "shape", &T::shape, "spawn_side", &T::spawn_side);
};
template <> struct glz::meta<rtsdata::GroupMember> {
  using T = rtsdata::GroupMember;
  static constexpr auto value = glz::object("type", &T::type, "count", &T::count);
};
template <> struct glz::meta<rtsdata::GroupCfg> {
  using T = rtsdata::GroupCfg;
  static constexpr auto value = glz::object("id", &T::id, "team", &T::team,
    "home_x", &T::home_x, "formation_cols", &T::formation_cols, "members", &T::members);
};
template <> struct glz::meta<rtsdata::RtsBlock> {
  using T = rtsdata::RtsBlock;
  static constexpr auto value = glz::object(
    "field", &T::field,
    "ground_color", &T::ground_color,
    "nonwalkable", &T::nonwalkable,
    "lane_half_z", &T::lane_half_z,
    "nonwalkable_color", &T::nonwalkable_color,
    "camera", &T::camera,
    "light", &T::light,
    "unit_types", &T::unit_types,
    "teams", &T::teams,
    "groups", &T::groups);
};
template <> struct glz::meta<rtsdata::SceneFile> {
  using T = rtsdata::SceneFile;
  static constexpr auto value = glz::object(
    "render_graph", &T::render_graph,
    "control_descriptor", &T::control_descriptor,
    "rts", &T::rts);
};

// ── Config loading ─────────────────────────────────────────────────────────
void RtsScene::BuildDefaultConfig() {
  // Nexus Wars One Lane layout: the real terrain glb spans X ±112, Z ±32, with a
  // raised central lane and a deep central valley (the authored "Exclude Volume").
  m_rts.field = rtsdata::Field{224.0f, 64.0f, 0.0f};
  m_rts.ground_color = rtsdata::Color{0.40f, 0.44f, 0.40f, 1.0f};
  m_rts.nonwalkable = rtsdata::ZoneBox{0.0f, 0.0f, 51.33f, 35.78f};
  m_rts.nonwalkable_color = rtsdata::Color{0.20f, 0.14f, 0.12f, 1.0f};
  // Higher/wider default view to frame the long Nexus field (units at ±85).
  m_rts.camera = rtsdata::CameraCfg{{0.0f,165.0f,-120.0f}, {0.0f,0.0f,0.0f}, 42.0f, 1.0f, 2000.0f};
  m_rts.light = rtsdata::LightCfg{{0.35f,-1.0f,0.2f}, {1.0f,0.96f,0.88f,1.0f}, 2.4f, {0.16f,0.2f,0.24f,1.0f}};
  m_rts.unit_types = {
    {"soldier",70,9,6,7,0.7f,1.0f}, {"archer",45,7,16,6.5f,0.6f,0.8f}, {"tank",180,14,3,4.5f,1.1f,1.7f},
    {"grunt",55,8,5,7,0.65f,0.9f}, {"brute",150,16,3,4.2f,1.0f,1.6f}};
  m_rts.teams = {
    {"humans", {0.2f,0.5f,1.0f,1.0f}, "sphere", -1},
    {"orcs", {0.85f,0.25f,0.2f,1.0f}, "cube", 1}};
  // Spawn on the walkable sides (outside the central non-walkable zone, which
  // spans |x| < 51.33). ±85 sits comfortably in each walkable half.
  m_rts.groups = {
    {"humans","humans",-85.0f,5,{{"soldier",8},{"archer",6},{"tank",4}}},
    {"orcs","orcs",85.0f,5,{{"grunt",8},{"brute",8}}}};
}

bool RtsScene::LoadConfig() {
  const std::string path = "Scenes/Rts.t8scene";
  std::string content;
  if (!t850::ResourceLocator::Instance().ReadText(path, content)) {
    T8_LOG_ERROR("[Rts] Scene file not found: %s", path.c_str());
    BuildDefaultConfig();
    return false;
  }
  rtsdata::SceneFile file;
  auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(file, content);
  if (err) {
    T8_LOG_ERROR("[Rts] Failed to parse %s: %s", path.c_str(), glz::format_error(err, content).c_str());
    BuildDefaultConfig();
    return false;
  }
  m_renderGraphPath = file.render_graph.empty() ? "Scenes/SceneTemplate_RenderGraph.json" : file.render_graph;
  m_rts = file.rts.value_or(rtsdata::RtsBlock{});
  T8_LOG_INFO("[Rts] Loaded %s: %zu unit types, %zu teams, %zu groups",
    path.c_str(), m_rts.unit_types.size(), m_rts.teams.size(), m_rts.groups.size());
  return true;
}

// ── Geometry builders ──────────────────────────────────────────────────────
t850::MutableMeshSnapshot RtsScene::MakeQuad(float half, const XVECTOR3& normal, const XVECTOR3& color) const {
  t850::MutableMeshSnapshot snap;
  const XVECTOR3 up = std::fabs(normal.y) > 0.99f ? XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f) : XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
  const XVECTOR3 right = Cross3(up, normal);
  const XVECTOR3 upv = Cross3(normal, right);
  auto V = [&](float sx, float sy) {
    t850::MutableMeshVertex v;
    v.position = normal*0.0f + right*(sx*half) + upv*(sy*half);
    v.position.w = 1.0f;
    v.normal = normal;
    v.u = 0.5f + sx*0.5f; v.v = 0.5f + sy*0.5f;
    return v;
  };
  snap.vertices = {V(-1,-1), V(1,-1), V(1,1), V(-1,1)};
  snap.indices = {0,1,2, 0,2,3};
  t850::MutableMeshMaterial mat;
  mat.baseColor = color; mat.roughness = 0.9f; mat.doubleSided = true;
  mat.usesBaseColorTexture = false;
  snap.materials.push_back(mat);
  snap.sections.push_back({0, 6, 0});
  t850::RecalculateMutableMeshBounds(snap);
  return snap;
}

t850::MutableMeshSnapshot RtsScene::MakeCube(float half, const XVECTOR3& color) const {
  t850::MutableMeshSnapshot snap;
  const float h = half;
  // 8 corners
  XVECTOR3 c[8] = {
    XVECTOR3(-h,-h,-h,0.0f),XVECTOR3(h,-h,-h,0.0f),XVECTOR3(h,-h,h,0.0f),XVECTOR3(-h,-h,h,0.0f),
    XVECTOR3(-h,h,-h,0.0f),XVECTOR3(h,h,-h,0.0f),XVECTOR3(h,h,h,0.0f),XVECTOR3(-h,h,h,0.0f)};
  // 6 faces, 4 corners each (CCW from outside)
  int faces[24] = {
    0,1,2,3,   // bottom (-Y)
    4,5,6,7,   // top (+Y)
    0,4,7,3,   // -Z
    1,5,6,2,   // +Z
    0,3,7,4,   // -X
    1,2,6,5    // +X
  };
  XVECTOR3 normals[6] = {
    XVECTOR3(0.0f,-1.0f,0.0f,0.0f),XVECTOR3(0.0f,1.0f,0.0f,0.0f),XVECTOR3(0.0f,0.0f,-1.0f,0.0f),
    XVECTOR3(0.0f,0.0f,1.0f,0.0f),XVECTOR3(-1.0f,0.0f,0.0f,0.0f),XVECTOR3(1.0f,0.0f,0.0f,0.0f)};
  for (int f = 0; f < 6; ++f) {
    int base = f * 4;
    for (int i = 0; i < 4; ++i) {
      t850::MutableMeshVertex v;
      v.position = c[faces[base+i]];
      v.normal = normals[f];
      v.u = (i%2)*0.5f; v.v = (i/2)*0.5f;
      snap.vertices.push_back(v);
    }
    snap.indices.push_back(base+0); snap.indices.push_back(base+1); snap.indices.push_back(base+2);
    snap.indices.push_back(base+0); snap.indices.push_back(base+2); snap.indices.push_back(base+3);
  }
  t850::MutableMeshMaterial mat;
  mat.baseColor = color; mat.roughness = 0.7f;
  mat.usesBaseColorTexture = false;
  snap.materials.push_back(mat);
  snap.sections.push_back({0, static_cast<uint32_t>(snap.indices.size()), 0});
  t850::RecalculateMutableMeshBounds(snap);
  return snap;
}

t850::MutableMeshSnapshot RtsScene::MakeSphere(float radius, int slices, int stacks, const XVECTOR3& color) const {
  t850::MutableMeshSnapshot snap;
  snap.vertices.reserve((slices+1)*(stacks+1));
  for (int s = 0; s <= stacks; ++s) {
    float theta = 3.14159265f * s / stacks;
    for (int c = 0; c <= slices; ++c) {
      float phi = 2.0f * 3.14159265f * c / slices;
      t850::MutableMeshVertex v;
      v.position = XVECTOR3(
        radius * std::sin(theta) * std::cos(phi),
        radius * std::cos(theta),
        radius * std::sin(theta) * std::sin(phi), 1.0f);
      v.normal = XVECTOR3(v.position.x, v.position.y, v.position.z, 0.0f);
      v.normal.Normalize();
      v.u = c / (float)slices; v.v = s / (float)stacks;
      snap.vertices.push_back(v);
    }
  }
  for (int s = 0; s < stacks; ++s) {
    for (int c = 0; c < slices; ++c) {
      int a = s*(slices+1) + c;
      int b = a + slices + 1;
      snap.indices.push_back(a); snap.indices.push_back(b); snap.indices.push_back(a+1);
      snap.indices.push_back(b); snap.indices.push_back(b+1); snap.indices.push_back(a+1);
    }
  }
  t850::MutableMeshMaterial mat;
  mat.baseColor = color; mat.roughness = 0.5f; mat.metallic = 0.1f;
  mat.usesBaseColorTexture = false;
  snap.materials.push_back(mat);
  snap.sections.push_back({0, static_cast<uint32_t>(snap.indices.size()), 0});
  t850::RecalculateMutableMeshBounds(snap);
  return snap;
}

bool RtsScene::CommitMesh(t850::MutableMesh* mesh, t850::MutableMeshSnapshot snap) {
  std::string error;
  if (!mesh->ReplaceSnapshot(std::move(snap), &error)) {
    T8_LOG_ERROR("[Rts] Mesh commit failed: %s", error.c_str());
    return false;
  }
  return true;
}

// ── Setup ──────────────────────────────────────────────────────────────────
// Build a rectangular quad of the given half-extents centered at (cx, cz) at
// ground level, returning its vertices/indices (appended to `snap`) and the
// material index it should use. Winding {a,c,b} gives an up-facing normal.
static void AppendGroundQuad(t850::MutableMeshSnapshot& snap, int materialIndex,
                             float cx, float cz, float hx, float hz,
                             const XVECTOR3& color, float y) {
  const int base = (int)snap.vertices.size();
  auto mk = [&](float sx, float sz) {
    t850::MutableMeshVertex v;
    v.position = XVECTOR3(cx + sx * hx, y, cz + sz * hz, 1.0f);
    v.normal = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
    v.u = 0.5f + sx * 0.5f; v.v = 0.5f + sz * 0.5f;
    return v;
  };
  snap.vertices.push_back(mk(-1.0f, -1.0f)); // a
  snap.vertices.push_back(mk( 1.0f, -1.0f)); // b
  snap.vertices.push_back(mk( 1.0f,  1.0f)); // c
  snap.vertices.push_back(mk(-1.0f,  1.0f)); // d
  const uint32_t firstIndex = (uint32_t)snap.indices.size();
  snap.indices.insert(snap.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
  snap.sections.push_back({firstIndex, (uint32_t)6, (uint32_t)materialIndex});
}

void RtsScene::BuildHeightSampler() {
  // Downsample terrain vertex heights into a regular grid for cheap per-unit
  // surface lookup. Grid: m_terrainHmH cells along X, m_terrainHmW cells along Z.
  const float spanX = m_terrainHmMaxX - m_terrainHmMinX;
  const float spanZ = m_terrainHmMaxZ - m_terrainHmMinZ;
  m_terrainHmH = 128; m_terrainHmW = 64;
  m_terrainHm.assign((size_t)m_terrainHmH * m_terrainHmW, -1e9f);
  const size_t n = m_terrainVerts.size() / 3;
  for (size_t i = 0; i < n; ++i) {
    const float x = m_terrainVerts[i*3], y = m_terrainVerts[i*3+1], z = m_terrainVerts[i*3+2];
    int gx = (int)((x - m_terrainHmMinX) / spanX * m_terrainHmH);
    int gz = (int)((z - m_terrainHmMinZ) / spanZ * m_terrainHmW);
    gx = (std::min)((std::max)(gx, 0), m_terrainHmH - 1);
    gz = (std::min)((std::max)(gz, 0), m_terrainHmW - 1);
    const size_t idx = (size_t)gx * m_terrainHmW + gz;
    if (y > m_terrainHm[idx]) m_terrainHm[idx] = y;
  }
  // Fill holes (cells with no vertex) by nearest filled neighbor.
  for (int gx = 0; gx < m_terrainHmH; ++gx)
    for (int gz = 0; gz < m_terrainHmW; ++gz) {
      const size_t idx = (size_t)gx * m_terrainHmW + gz;
      if (m_terrainHm[idx] > -1e8f) continue;
      float best = 0.0f; bool found = false;
      for (int r = 1; r <= 8 && !found; ++r)
        for (int dx = -r; dx <= r && !found; ++dx)
          for (int dz = -r; dz <= r && !found; ++dz) {
            const int nx = gx + dx, nz = gz + dz;
            if (nx < 0 || nx >= m_terrainHmH || nz < 0 || nz >= m_terrainHmW) continue;
            const float v = m_terrainHm[(size_t)nx * m_terrainHmW + nz];
            if (v > -1e8f) { best = v; found = true; }
          }
      m_terrainHm[idx] = best;
    }
  m_terrainLoaded = true;
}

float RtsScene::TerrainHeight(float x, float z) const {
  if (!m_terrainLoaded) return m_groundY;
  const float spanX = m_terrainHmMaxX - m_terrainHmMinX;
  const float spanZ = m_terrainHmMaxZ - m_terrainHmMinZ;
  float fx = (x - m_terrainHmMinX) / spanX * (m_terrainHmH - 1);
  float fz = (z - m_terrainHmMinZ) / spanZ * (m_terrainHmW - 1);
  fx = Clampf(fx, 0.0f, (float)m_terrainHmH - 1.0f);
  fz = Clampf(fz, 0.0f, (float)m_terrainHmW - 1.0f);
  const int x0 = (int)fx, z0 = (int)fz;
  const int x1 = (std::min)(x0 + 1, m_terrainHmH - 1), z1 = (std::min)(z0 + 1, m_terrainHmW - 1);
  const float tx = fx - x0, tz = fz - z0;
  const float h00 = m_terrainHm[(size_t)x0 * m_terrainHmW + z0];
  const float h10 = m_terrainHm[(size_t)x1 * m_terrainHmW + z0];
  const float h01 = m_terrainHm[(size_t)x0 * m_terrainHmW + z1];
  const float h11 = m_terrainHm[(size_t)x1 * m_terrainHmW + z1];
  const float h0 = h00 * (1.0f - tx) + h10 * tx;
  const float h1 = h01 * (1.0f - tx) + h11 * tx;
  return h0 * (1.0f - tz) + h1 * tz;
}

void RtsScene::BuildTerrainMesh() {
  const char* path = "Models/SC_terrain/nexus_wars_terrain.glb";
  m_terrainPrimIdx = m_unitPrimMgr.CreateMesh(path);
  if (m_terrainPrimIdx < 0) {
    T8_LOG_ERROR("[Rts] Failed to load terrain '%s' — falling back to flat ground", path);
    return;
  }
  auto* rm = dynamic_cast<t850::RenderMesh*>(m_unitPrimMgr.GetPrimitive(m_terrainPrimIdx));
  if (rm && rm->xFile && !rm->xFile->XMeshDataBase.empty()) {
    const auto& g = rm->xFile->XMeshDataBase[0]->Geometry[0];
    m_terrainVerts.reserve(g.Positions.size() * 3);
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto& p : g.Positions) {
      m_terrainVerts.push_back(p.x); m_terrainVerts.push_back(p.y); m_terrainVerts.push_back(p.z);
      minX = (std::min)(minX, p.x); maxX = (std::max)(maxX, p.x);
      minZ = (std::min)(minZ, p.z); maxZ = (std::max)(maxZ, p.z);
    }
    m_terrainHmMinX = minX; m_terrainHmMaxX = maxX;
    m_terrainHmMinZ = minZ; m_terrainHmMaxZ = maxZ;
    BuildHeightSampler();
  }
  // Render it (identity transform — the terrain is authored at the origin).
  t850::PrimitiveInst inst;
  inst.CreateInstance(m_unitPrimMgr.GetPrimitive(m_terrainPrimIdx), &m_camera.VP);
  inst.TranslateAbsolute(0.0f, 0.0f, 0.0f);
  inst.Update();
  m_groundInst = m_renderContainer.AddMeshInstance(inst);
  T8_LOG_INFO("[Rts] Terrain rendered: %s (%zu verts, X[%g..%g] Z[%g..%g])",
              path, m_terrainVerts.size() / 3, m_terrainHmMinX, m_terrainHmMaxX,
              m_terrainHmMinZ, m_terrainHmMaxZ);
}

void RtsScene::BuildGroundMesh() {
  // Real Nexus terrain (elevation + valleys) with its authored material/texture.
  BuildTerrainMesh();
  if (m_terrainLoaded) return;

  // Fallback: a flat two-tone ground plane if the terrain mesh failed to load.
  if (!m_groundMesh) {
    m_groundMesh = std::make_unique<t850::MutableMesh>();
    m_groundMesh->SetEngineContext(pEngineContext);
    m_groundMesh->SetSceneProps(&SceneProp);
    m_groundMesh->Create();
  }
  XVECTOR3 walkCol = m_rts.ground_color
    ? XVECTOR3(m_rts.ground_color->x, m_rts.ground_color->y, m_rts.ground_color->z, 1.0f)
    : XVECTOR3(0.22f, 0.5f, 0.26f, 1.0f);
  t850::MutableMeshSnapshot snap;
  snap.materials.push_back(t850::MutableMeshMaterial{});
  snap.materials[0].baseColor = walkCol;
  snap.materials[0].roughness = 0.95f;
  snap.materials[0].doubleSided = true;
  AppendGroundQuad(snap, 0, 0.0f, 0.0f, m_halfX, m_halfZ, walkCol, m_groundY);
  t850::RecalculateMutableMeshBounds(snap);
  CommitMesh(m_groundMesh.get(), std::move(snap));
  t850::PrimitiveInst inst;
  inst.CreateInstance(m_groundMesh.get(), &m_camera.VP);
  inst.TranslateAbsolute(0.0f, m_groundY, 0.0f);
  inst.Update();
  m_groundInst = m_renderContainer.AddMeshInstance(inst);
}

void RtsScene::BuildUnitMeshes() {
  for (int t = 0; t < 2; ++t) {
    if (t >= static_cast<int>(m_teams.size())) break;
    const Team& team = m_teams[t];
    XVECTOR3 tc = team.color;
    // sphere variant
    int si = t * 2;
    if (!m_unitMeshes[si]) {
      m_unitMeshes[si] = std::make_unique<t850::MutableMesh>();
      m_unitMeshes[si]->SetEngineContext(pEngineContext);
      m_unitMeshes[si]->SetSceneProps(&SceneProp);
      m_unitMeshes[si]->Create();
    }
    CommitMesh(m_unitMeshes[si].get(), MakeSphere(0.5f, 16, 12, tc));
    // cube variant
    int ci = t * 2 + 1;
    if (!m_unitMeshes[ci]) {
      m_unitMeshes[ci] = std::make_unique<t850::MutableMesh>();
      m_unitMeshes[ci]->SetEngineContext(pEngineContext);
      m_unitMeshes[ci]->SetSceneProps(&SceneProp);
      m_unitMeshes[ci]->Create();
    }
    CommitMesh(m_unitMeshes[ci].get(), MakeCube(0.4f, tc));
  }
}

// Load the actual StarCraft unit models (glb) for a visual test, one per unit
// type. The procedural sphere/cube above remain as a fallback if a model fails.
// NOTE: these glbs are STATIC (no walk/attack/idle animations) — see the user
// report. We only use the static pose for now.
void RtsScene::LoadUnitModels() {
  struct Map { const char* type; const char* path; float scale; };
  static const Map kMap[] = {
    { "soldier", "Models/SC_anim/marine.glb",    2.6f },
    { "archer",  "Models/SC_anim/reaper.glb",    2.6f },
    { "tank",    "Models/SC_anim/siegetank.glb", 1.2f },
    { "grunt",   "Models/SC_anim/roach.glb",     2.6f },  // roach (zergling has no anim)
    { "brute",   "Models/SC_anim/hydralisk.glb", 2.0f },
  };
  m_unitPrimMgr.SetEngineContext(pEngineContext);
  m_unitPrimMgr.Init();
  m_unitPrimMgr.SetSceneProps(&SceneProp);
  m_unitModels.resize(m_unitTypes.size());
  for (size_t i = 0; i < m_unitTypes.size(); ++i) {
    UnitModel& um = m_unitModels[i];
    const char* path = nullptr;
    float scale = 2.5f;
    for (const auto& m : kMap)
      if (m.type == m_unitTypes[i].id) { path = m.path; scale = m.scale; break; }
    if (!path) continue;
    int idx = m_unitPrimMgr.CreateMesh(path);
    if (idx < 0) {
      T8_LOG_ERROR("[Rts] Failed to load unit model '%s' — using procedural fallback", path);
      continue;
    }
    um.templatePrimIdx = idx;
    um.path = path;
    um.scale = scale;
    um.lift = 0.5f;   // rest the model roughly at the old sphere-center height
    um.valid = true;
    um.templateSkinned = dynamic_cast<t850::RenderSkinnedMesh*>(m_unitPrimMgr.GetPrimitive(idx));
    if (um.templateSkinned && um.templateSkinned->HasSkinData()) {
      um.animated = true;
      um.setWalk = FindAnimSet(um.templateSkinned, "Walk");
      um.setAttack = FindAnimSet(um.templateSkinned, "Attack");
      um.setStand = FindAnimSet(um.templateSkinned, "Stand");
      um.walkSpeed = 1.0f;
      um.attackSpeed = 1.0f;
      T8_LOG_INFO("[Rts] Unit model ready: %s -> '%s' ANIMATED (walk=%d attack=%d stand=%d, %d sets)",
        m_unitTypes[i].id.c_str(), path, um.setWalk, um.setAttack, um.setStand,
        um.templateSkinned->GetNumAnimSets());
    } else {
      T8_LOG_INFO("[Rts] Unit model ready (static): %s -> '%s'", m_unitTypes[i].id.c_str(), path);
    }
  }
}

// Create one per-unit skinned primitive for the given unit type. Each call makes
// a fresh RenderSkinnedMesh (own AnimationController + bone texture) that shares
// the cached parsed model + GPU geometry, so units animate independently.
int RtsScene::CreateUnitAnimPrimitive(int typeIndex) {
  if (typeIndex < 0 || typeIndex >= static_cast<int>(m_unitModels.size())) return -1;
  const UnitModel& um = m_unitModels[typeIndex];
  if (!um.animated || um.path.empty()) return -1;
  return m_unitPrimMgr.CreateMesh(um.path.c_str());
}

// Find the set index of the named animation (exact, case-sensitive) by reading
// the primitive's parsed model; -1 if absent.
int RtsScene::FindAnimSet(const t850::RenderSkinnedMesh* sk, const char* name) const {
  if (!sk || !sk->xFile || sk->xFile->XMeshDataBase.empty()) return -1;
  const xF::xMeshContainer* mc = sk->xFile->XMeshDataBase[0];
  const auto& anims = mc->Animation.Animations;
  for (size_t i = 0; i < anims.size(); ++i)
    if (anims[i].Name == name) return static_cast<int>(i);
  return -1;
}

// Drive one unit's animation this frame: pick the clip from its state
// (attack > walk > stand), change the clip only on transitions, then advance
// the pose (CPU bone update). The bone texture upload happens in OnDraw.
void RtsScene::ApplyUnitAnimState(Unit& u) {
  if (!u.alive || u.animPrimIdx < 0) return;
  auto* sk = dynamic_cast<t850::RenderSkinnedMesh*>(m_unitPrimMgr.GetPrimitive(u.animPrimIdx));
  if (!sk || !sk->HasSkinData()) return;
  const UnitModel& um = m_unitModels[u.typeIndex];
  t850::AnimationController& ac = sk->GetAnimController();

  // Choose the clip from the unit's current state.
  int target = um.setStand;
  float speed = 1.0f;
  const float speed2 = u.vel.x * u.vel.x + u.vel.z * u.vel.z;
  const bool moving = speed2 > 0.05f;
  if (u.attackFlash > 0.0f && um.setAttack >= 0) { target = um.setAttack; speed = um.attackSpeed; }
  else if (moving && um.setWalk >= 0)           { target = um.setWalk;   speed = um.walkSpeed; }
  if (target < 0) target = 0;

  // Change the clip only when it actually changes (avoid restarting every frame).
  if (ac.GetCurrentSet() != target) {
    const int n = ac.GetNumSets();
    int guard = n + 1;
    while (ac.GetCurrentSet() != target && guard-- > 0) ac.NextAnimationSet();
    ac.ResetAnimationSet();   // restart the clip from its first frame
  }
  ac.SetSpeed(speed);
  ac.SetLooping(true);
  sk->PlayAnimation();
  sk->UpdateAnimationPose();  // advance this frame (uses pScProp->FrameDeltaSec)
}

// Advance every unit's animation pose (call once per frame in OnUpdate).
void RtsScene::UpdateUnitAnimations() {
  if (!m_assetsCreated) return;
  for (auto& u : m_units) ApplyUnitAnimState(u);
}

void RtsScene::BuildSelectionMarker() {
  if (!m_markerMesh) {
    m_markerMesh = std::make_unique<t850::MutableMesh>();
    m_markerMesh->SetEngineContext(pEngineContext);
    m_markerMesh->SetSceneProps(&SceneProp);
    m_markerMesh->Create();
  }
  // Build a ring (annulus) in the XZ plane: outer radius 1.0, inner radius 0.75,
  // 32 segments. This gives a visible circle outline on the ground under units.
  const int N = 32;
  const float R_OUT = 1.0f, R_IN = 0.75f;
  t850::MutableMeshSnapshot snap;
  t850::MutableMeshMaterial mat;
  mat.baseColor = XVECTOR3(0.1f, 1.0f, 0.3f, 1.0f);
  mat.roughness = 0.3f;
  mat.doubleSided = true;
  mat.usesBaseColorTexture = false;
  snap.materials.push_back(mat);
  // Vertices: N outer + N inner
  for (int i = 0; i < N; ++i) {
    float a = (float)i / N * 6.2831853f;
    float ca = std::cos(a), sa = std::sin(a);
    t850::MutableMeshVertex vo, vi;
    vo.position = XVECTOR3(R_OUT * ca, 0.0f, R_OUT * sa, 1.0f);
    vo.normal = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
    vo.u = ca * 0.5f + 0.5f; vo.v = sa * 0.5f + 0.5f;
    vi.position = XVECTOR3(R_IN * ca, 0.0f, R_IN * sa, 1.0f);
    vi.normal = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
    vi.u = ca * 0.5f + 0.5f; vi.v = sa * 0.5f + 0.5f;
    snap.vertices.push_back(vo);
    snap.vertices.push_back(vi);
  }
  // Indices: two triangles per segment
  for (int i = 0; i < N; ++i) {
    int i2 = (i + 1) % N;
    int o0 = i * 2, i0 = i * 2 + 1;
    int o1 = i2 * 2, i1 = i2 * 2 + 1;
    snap.indices.insert(snap.indices.end(), {o0, o1, i1, o0, i1, i0});
  }
  snap.sections.push_back({0, (uint32_t)(N * 6), 0});
  t850::RecalculateMutableMeshBounds(snap);
  CommitMesh(m_markerMesh.get(), std::move(snap));
}

void RtsScene::BuildNavMesh() {
  t850::navigation::NavMeshGeometry geo;
  bool haveTerrain = false;
  // Preferred: build the navmesh from the REAL terrain mesh (elevation + valleys),
  // so the walkable surface follows the actual hills and the central lane.
  if (m_terrainPrimIdx >= 0) {
    auto* rm = dynamic_cast<t850::RenderMesh*>(m_unitPrimMgr.GetPrimitive(m_terrainPrimIdx));
    if (rm && rm->xFile) {
      std::string gerr;
      if (t850::navigation::BuildGeometryFromXDataBase(*rm->xFile, geo, &gerr) && !geo.vertices.empty()) {
        haveTerrain = true;
        T8_LOG_INFO("[Rts] NavMesh source: real terrain (%d verts, %d indices)",
                    (int)geo.vertices.size(), (int)geo.indices.size());
      } else {
        T8_LOG_ERROR("[Rts] BuildGeometryFromXDataBase failed: %s — using flat grid", gerr.c_str());
        geo.vertices.clear();
        geo.indices.clear();
      }
    }
  }
  if (!haveTerrain) {
    // Fallback: a thick flat floor (a single flat plane has zero span thickness,
    // which Recast filters out). Only used if the terrain mesh failed to load.
    const float x0 = -m_halfX, x1 = m_halfX, z0 = -m_halfZ, z1 = m_halfZ;
    const int nx = 132, nz = 32;
    const float dx = (x1 - x0) / nx, dz = (z1 - z0) / nz;
    const int gw = nx + 1;
    const int topBase = 0, botBase = gw * (nz + 1);
    auto topIdx = [gw](int ix, int iz) { return topBase + iz * gw + ix; };
    auto botIdx = [gw](int ix, int iz) { return botBase + iz * gw + ix; };
    for (int iz = 0; iz <= nz; ++iz)
      for (int ix = 0; ix <= nx; ++ix)
        geo.vertices.push_back(XVECTOR3(x0 + ix * dx, m_groundY, z0 + iz * dz, 0.0f));
    for (int iz = 0; iz <= nz; ++iz)
      for (int ix = 0; ix <= nx; ++ix)
        geo.vertices.push_back(XVECTOR3(x0 + ix * dx, m_groundY - 2.0f, z0 + iz * dz, 0.0f));
    for (int iz = 0; iz < nz; ++iz)
      for (int ix = 0; ix < nx; ++ix) {
        int a = topIdx(ix, iz), b = topIdx(ix + 1, iz);
        int c = topIdx(ix + 1, iz + 1), d = topIdx(ix, iz + 1);
        geo.indices.insert(geo.indices.end(), {a, c, b, a, d, c});
      }
  }

  // Authored volumes from Nexus.t8scene:
  //  - Include: bounds the walkable region to the field.
  //  - Exclude: the deep central valley. Its low floor (y~0) is carved out, while
  //    the raised central lane (y~8) and the base plateaus (y~10-12) sit above the
  //    box and remain walkable — this is the "One Lane" connection between bases.
  {
    t850::navigation::NavMeshVolumeModifier include;
    include.name = "Nexus Walkable";
    include.mode = t850::navigation::NavMeshModifierMode::Include;
    include.position = XVECTOR3(0.0f, m_groundY, 0.0f, 0.0f);
    include.halfExtents = XVECTOR3(m_halfX, 16.0f, m_halfZ, 0.0f);
    include.enabled = true;
    geo.volumeModifiers.push_back(include);

    if (m_nonwalkValid) {
      t850::navigation::NavMeshVolumeModifier exclude;
      exclude.name = "Nexus Valley (non-walkable)";
      exclude.mode = t850::navigation::NavMeshModifierMode::Exclude;
      exclude.position = XVECTOR3(m_nonwalkBox.x, m_groundY, m_nonwalkBox.z, 0.0f);
      exclude.halfExtents = XVECTOR3(m_nonwalkBox.half_x, 4.0f, m_nonwalkBox.half_z, 0.0f);
      exclude.enabled = true;
      geo.volumeModifiers.push_back(exclude);
    }
  }

  // Authored build settings (match Nexus.t8scene): cell 0.3/0.2, agent 2.0/0.6,
  // climb 0.9, slope 45 — these are the NavMeshBuildSettings defaults.
  t850::navigation::NavMeshBuildSettings settings;

  std::string error;
  m_navMesh.Clear();
  m_navReady = m_navMesh.Build(geo, settings, &error);
  if (!m_navReady) {
    T8_LOG_ERROR("[Rts] NavMesh build failed: %s", error.c_str());
  } else {
    T8_LOG_INFO("[Rts] NavMesh ready: %d polys (%s%s)", m_navMesh.GetStats().polygonCount,
                haveTerrain ? "terrain" : "flat grid",
                m_nonwalkValid ? " + valley excluded" : "");
    // Sanity-check the One-Lane layout: lane + base walkable, valley not, and a
    // path exists between the two bases through the central lane.
    {
      XVECTOR3 p;
      const XVECTOR3 qe(1.0f, 20.0f, 1.0f, 0.0f);
      const bool laneWalk   = m_navMesh.ProjectPoint(XVECTOR3(0.0f, 8.0f, 0.0f, 0.0f), p, qe);
      const bool valleyWalk = m_navMesh.ProjectPoint(XVECTOR3(0.0f, 0.0f, 20.0f, 0.0f), p, qe);
      const bool baseWalk   = m_navMesh.ProjectPoint(XVECTOR3(-85.0f, 12.0f, 0.0f, 0.0f), p, qe);
      t850::navigation::NavPathRequest req;
      req.start = XVECTOR3(-85.0f, 12.0f, 0.0f, 0.0f);
      req.end   = XVECTOR3( 85.0f, 12.0f, 0.0f, 0.0f);
      const auto r = m_navMesh.FindPath(req);
      T8_LOG_INFO("[Rts] NAVCHECK lane=%d valley=%d base=%d path(-85->85)=%d pts=%d err='%s'",
                  laneWalk ? 1 : 0, valleyWalk ? 1 : 0, baseWalk ? 1 : 0,
                  r.success ? 1 : 0, (int)r.points.size(), r.error.c_str());
    }
  }
}

// ── Units ──────────────────────────────────────────────────────────────────
int RtsScene::AddUnit(int typeIndex, int teamIndex, int groupId, const XVECTOR3& pos) {
  if (static_cast<int>(m_units.size()) >= m_maxUnits) return -1;
  Unit u;
  u.id = static_cast<int>(m_units.size());
  u.typeIndex = typeIndex;
  u.teamIndex = teamIndex;
  u.groupId = groupId;
  u.pos = pos;
  const UnitType& ut = m_unitTypes[typeIndex];
  u.hp = u.maxHp = ut.health;
  u.radius = ut.radius;
  u.scale = ut.scale;
  u.alive = true;
  m_units.push_back(u);
  return u.id;
}

void RtsScene::RemoveUnit(int index) {
  if (index < 0 || index >= static_cast<int>(m_units.size())) return;
  Unit& u = m_units[index];
  if (u.inst.IsValid()) m_renderContainer.RemoveMesh(u.inst);
  if (u.marker.IsValid()) m_renderContainer.RemoveMesh(u.marker);
  u.alive = false;
  m_units.erase(m_units.begin() + index);
  // Re-index (IDs are used for attack targeting, so reassign)
  for (auto& un : m_units) un.id = static_cast<int>(&un - m_units.data());
  // Fix group member lists
  for (auto& g : m_groups) {
    g.units.erase(std::remove_if(g.units.begin(), g.units.end(),
      [&](int oldId) { return oldId == index; }), g.units.end());
  }
}

void RtsScene::SpawnUnits() {
  for (int gi = 0; gi < static_cast<int>(m_rts.groups.size()); ++gi) {
    const rtsdata::GroupCfg& gc = m_rts.groups[gi];
    Group grp;
    grp.id = gc.id;
    grp.homeX = gc.home_x;
    grp.cols = gc.formation_cols;
    grp.center = XVECTOR3(gc.home_x, m_groundY, 0.0f, 1.0f);
    // Find team index
    for (int ti = 0; ti < static_cast<int>(m_teams.size()); ++ti)
      if (m_teams[ti].id == gc.team) grp.teamIndex = ti;
    int unitIdx = 0;
    for (const auto& member : gc.members) {
      int typeIdx = -1;
      for (int ui = 0; ui < static_cast<int>(m_unitTypes.size()); ++ui)
        if (m_unitTypes[ui].id == member.type) typeIdx = ui;
      if (typeIdx < 0) continue;
      for (int i = 0; i < member.count; ++i) {
        // Grid position within group
        int col = unitIdx % grp.cols;
        int row = unitIdx / grp.cols;
        float spacing = 2.5f;
        float sx = (col - (grp.cols - 1) / 2.0f) * spacing;
        float sz = (row - 1.0f) * spacing;
        XVECTOR3 spawnPos(grp.homeX + sx, TerrainHeight(grp.homeX + sx, sz) + 0.5f, sz, 1.0f);
        int uid = AddUnit(typeIdx, grp.teamIndex, gi, spawnPos);
        if (uid >= 0) {
          grp.units.push_back(uid);
          Unit& u = m_units[uid];
          u.home = u.slot = spawnPos;
          u.order = Order::Hold;
        }
        ++unitIdx;
      }
    }
    m_groups.push_back(std::move(grp));
  }
  T8_LOG_INFO("[Rts] Spawned %zu units in %zu groups", m_units.size(), m_groups.size());
}

int RtsScene::NearestUnit(const XVECTOR3& world, int excludeId, float maxDist) const {
  int best = -1; float bestD = maxDist;
  for (const auto& u : m_units) {
    if (!u.alive || u.id == excludeId) continue;
    float d = DistXZ(u.pos, world);
    if (d < bestD) { bestD = d; best = u.id; }
  }
  return best;
}

int RtsScene::UnitAt(const XVECTOR3& world, float radius) const {
  for (const auto& u : m_units) {
    if (!u.alive) continue;
    if (DistXZ(u.pos, world) <= radius + u.radius) return u.id;
  }
  return -1;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────
void RtsScene::InitVars() {
  m_dt = 0.0f;
  m_assetsCreated = false;
  m_configLoaded = false;
  m_units.clear();
  m_groups.clear();
  m_shots.clear();
  m_leftDown = false;
  m_boxActive = false;
  m_navReady = false;
  m_navMesh.Clear();
  for (auto& um : m_unitModels) um.valid = false;

  LoadConfig();
  m_configLoaded = true;

  // Field
  const auto& fld = m_rts.field.value_or(rtsdata::Field{120,80,0});
  m_halfX = fld.size_x * 0.5f;
  m_halfZ = fld.size_z * 0.5f;
  m_groundY = fld.y;
  m_fieldMin = XVECTOR3(-m_halfX, m_groundY, -m_halfZ, 0.0f);
  m_fieldMax = XVECTOR3(m_halfX, m_groundY, m_halfZ, 0.0f);

  // Non-walkable zone: the single authored "Exclude Volume" from Nexus.t8scene.
  // On the real terrain this box (half_y ~4, i.e. y in [-4,4]) carves out only the
  // deep central valley floor (y~0); the raised central lane (y~8) and the base
  // plateaus (y~10-12) survive above it and stay walkable. No lane-splitting is
  // needed — the actual elevation provides the walkable corridor.
  m_nonwalkBox = m_rts.nonwalkable.value_or(rtsdata::ZoneBox{});
  m_laneHalfZ = m_rts.lane_half_z.value_or(8.0f);
  m_nonwalkValid = (m_nonwalkBox.half_x > 0.5f) && (m_nonwalkBox.half_z > 0.5f);

  // Reset real-terrain state (repopulated in CreateAssets from the loaded glb).
  m_terrainPrimIdx = -1;
  m_terrainVerts.clear();
  m_terrainHmW = m_terrainHmH = 0;
  m_terrainLoaded = false;

  // Parse unit types
  m_unitTypes.clear();
  for (const auto& ut : m_rts.unit_types)
    m_unitTypes.push_back({ut.id, ut.health, ut.attack, ut.range, ut.speed, ut.radius, ut.scale});

  // Parse teams
  m_teams.clear();
  for (const auto& t : m_rts.teams) {
    Team tm;
    tm.id = t.id;
    tm.color = XVECTOR3(t.color.x, t.color.y, t.color.z, 1.0f);
    tm.sphere = (t.shape != "cube");
    tm.spawnSide = t.spawn_side;
    m_teams.push_back(std::move(tm));
  }

  // Camera
  const auto& cam = m_rts.camera.value_or(rtsdata::CameraCfg{});
  m_camTarget = XVECTOR3(cam.target.x, cam.target.y, cam.target.z, 1.0f);
  m_camOffset = XVECTOR3(cam.position.x - cam.target.x, cam.position.y - cam.target.y,
                         cam.position.z - cam.target.z, 1.0f);
  m_camOffsetLen = m_camOffset.Length();
  m_fovRad = Deg2Rad(cam.fov_deg);
  float aspect = 16.0f / 9.0f;
  if (pFramework && pFramework->pVideoDriver)
    aspect = static_cast<float>(pFramework->pVideoDriver->width) /
             static_cast<float>((std::max)(1, pFramework->pVideoDriver->height));
  m_camera.InitPerspective(m_camTarget + m_camOffset, m_fovRad, aspect, cam.near, cam.far);
  m_camera.Eye = m_camTarget + m_camOffset;
  m_camera.Speed = 0.0f;
  m_camera.Friction = 1.0f;
  m_camera.SetLookAt(m_camTarget);

  // Light camera — positioned along the light direction, high enough to
  // cover the whole field in a single cascade.
  {
    const auto& lt = m_rts.light.value_or(rtsdata::LightCfg{});
    XVECTOR3 dir = XVECTOR3(lt.direction.x, lt.direction.y, lt.direction.z, 0.0f);
    dir.Normalize();
    float height = 180.0f;
    XVECTOR3 lightPos = dir * (-height); // opposite of light direction = light source
    m_lightCamera.InitPerspective(lightPos, Deg2Rad(70.0f), 1.0f, 1.0f, 800.0f);
    m_lightCamera.Eye = lightPos;
    m_lightCamera.SetLookAt(XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
  }

  // Scene props
  const auto& lt = m_rts.light.value_or(rtsdata::LightCfg{});
  SceneProp = SceneProps{};
  SceneProp.AddCamera(&m_camera);
  SceneProp.AddLightCamera(&m_lightCamera);
  SceneProp.AddDirectionalLight(
    XVECTOR3(lt.direction.x, lt.direction.y, lt.direction.z, 0.0f),
    XVECTOR3(lt.color.x, lt.color.y, lt.color.z, 1.0f),
    lt.intensity, true);
  SceneProp.ActiveLights = 1;
  SceneProp.AmbientColor = XVECTOR3(lt.ambient.x, lt.ambient.y, lt.ambient.z, 1.0f);
  SceneProp.ToogleDOF = 0;
  SceneProp.ToogleParallax = 0;
  SceneProp.IBLFactor = 1.0f;
  SceneProp.FrustumCullingEnabled = true;

  // Post-process kernels (shadow blur / bloom / dof) — same recipe as VoxelScene
  m_shadowFilter.kernelSize = 4;
  m_shadowFilter.radius = 1.0f;
  m_shadowFilter.sigma = 1.0f;
  m_shadowFilter.Update();
  m_bloomFilter.kernelSize = 11;
  m_bloomFilter.radius = 2.5f;
  m_bloomFilter.sigma = 4.5f;
  m_bloomFilter.Update();
  m_dofFilter.kernelSize = 23;
  m_dofFilter.radius = 3.0f;
  m_dofFilter.sigma = 6.0f;
  m_dofFilter.Update();
  SceneProp.AddGaussKernel(&m_shadowFilter);
  SceneProp.AddGaussKernel(&m_bloomFilter);
  SceneProp.AddGaussKernel(&m_dofFilter);

  // Frame dumper
  t850::FrameDumperConfig dumpConfig;
  dumpConfig.dumpEnabled = t850::g_config.flags.dumpEnabled;
  dumpConfig.dumpByFrame = t850::g_config.flags.dumpByFrame;
  dumpConfig.dumpFrame = t850::g_config.dumpFrame;
  dumpConfig.dumpSeconds = t850::g_config.dumpSeconds;
  dumpConfig.debugFrames = t850::g_config.flags.debugFrames;
  dumpConfig.keepRunning = t850::g_config.flags.keepRunning;
  dumpConfig.replaySnapshotPath = t850::g_config.replaySnapshotPath;
  dumpConfig.sceneIndex = kSceneIndex;
  m_dumper.Init(dumpConfig);
}

void RtsScene::CreateAssets() {
  if (m_assetsCreated || !pFramework || !pFramework->pVideoDriver) return;
  SceneProp.SSAOKernel.InitTexture();

  t850::RenderContainerDesc desc;
  desc.name = "RtsScene";
  desc.renderGraphPath = m_renderGraphPath;
  desc.width = pFramework->pVideoDriver->width;
  desc.height = pFramework->pVideoDriver->height;
  desc.sceneProps = &SceneProp;
  if (!m_renderContainer.Initialize(pFramework->pVideoDriver, pEngineContext, desc)) {
    T8_LOG_ERROR("[Rts] Failed to initialize render container");
    return;
  }
  m_renderContainer.SetMainCamera(&m_camera);
  m_renderContainer.SetLightCamera(&m_lightCamera);
  // Seed the resize tracker so the first HandleWindowResize isn't a no-op resize.
  m_appliedResizeW = desc.width;
  m_appliedResizeH = desc.height;

  // Environment map + IBL (Minecraft skybox). The deferred pass samples the
  // cubemap for the background and uses the generated IBL maps for ambient
  // lighting — without this the scene has no sky and no IBL ambient (black).
  m_envMapTexIndex = t850::g_pBaseDriver ? t850::g_pBaseDriver->CreateTexture(m_cubemapPath) : -1;
  if (m_envMapTexIndex >= 0) {
    m_envMaps.SetFallback(m_envMapTexIndex);
    t850::EnvironmentResourcePaths envPaths;
    t850::LoadEnvironmentIBLResources(
      t850::g_pBaseDriver, envPaths, m_envMaps,
      m_diffuseIBLTexIndex, m_specularIBLTexIndex, m_brdfLUTTexIndex,
      m_sheenIBLTexIndex, m_charlieLUTTexIndex, m_sheenELUTTexIndex);
    t850::UpdateSceneIBLSettings(SceneProp, t850::g_pBaseDriver, m_envMaps);
    m_renderContainer.SetEnvironmentMaps(m_envMaps);
    T8_LOG_INFO("[Rts] Environment map loaded: %s (slot=%d)", m_cubemapPath.c_str(), m_envMapTexIndex);
  } else {
    T8_LOG_ERROR("[Rts] Failed to load environment map: %s", m_cubemapPath.c_str());
  }

  BuildGroundMesh();
  BuildUnitMeshes();
  LoadUnitModels();
  BuildSelectionMarker();
  BuildNavMesh();
  SpawnUnits();

  // Create instances for all units (actual SC model when available, else the
  // procedural sphere/cube)
  for (auto& u : m_units) {
    t850::PrimitiveBase* mesh = nullptr;
    float ms = u.scale;
    float lift = 0.0f;
    bool useModel = false;
    if (u.typeIndex >= 0 && u.typeIndex < static_cast<int>(m_unitModels.size()) && m_unitModels[u.typeIndex].valid) {
      const UnitModel& um = m_unitModels[u.typeIndex];
      // Animated units get their OWN per-unit skinned primitive (independent
      // AnimationController) so they can walk/attack out of sync. Static units
      // share the template primitive.
      if (um.animated) {
        int idx = CreateUnitAnimPrimitive(u.typeIndex);
        if (idx >= 0) { u.animPrimIdx = idx; mesh = m_unitPrimMgr.GetPrimitive(idx); }
      }
      if (!mesh) mesh = m_unitPrimMgr.GetPrimitive(um.templatePrimIdx);
      ms = um.scale;
      lift = um.lift;
      useModel = true;
    }
    if (!mesh) {
      int meshIdx = u.teamIndex * 2 + (m_teams[u.teamIndex].sphere ? 0 : 1);
      if (meshIdx < 0 || meshIdx >= 4) continue;
      mesh = m_unitMeshes[meshIdx].get();
      if (!mesh) continue;
    }
    u.meshIdx = u.typeIndex;
    u.modelScale = ms;
    u.modelLift = lift;
    u.useModel = useModel;
    t850::PrimitiveInst inst;
    inst.CreateInstance(mesh, &m_camera.VP);
    inst.TranslateAbsolute(u.pos.x, u.pos.y + lift, u.pos.z);
    inst.ScaleAbsolute(ms, ms, ms);
    inst.Update();
    u.inst = m_renderContainer.AddMeshInstance(inst);

    // Selection marker (hidden by default)
    t850::PrimitiveInst marker;
    marker.CreateInstance(m_markerMesh.get(), &m_camera.VP);
    marker.TranslateAbsolute(u.pos.x, u.pos.y, u.pos.z);
    marker.ScaleAbsolute(u.radius * 2.0f, 1.0f, u.radius * 2.0f);
    marker.SetVisible(false);
    marker.Update();
    u.marker = m_renderContainer.AddMeshInstance(marker);
  }

  // Bind SceneProp to every primitive (templates + per-unit clones) so skinned
  // animation updates read the live frame delta (pScProp->FrameDeltaSec).
  m_unitPrimMgr.SetSceneProps(&SceneProp);

  m_assetsCreated = true;
  T8_LOG_INFO("[Rts] Assets created: %zu units, nav=%s", m_units.size(), m_navReady ? "ready" : "off");
}

void RtsScene::DestroyAssets() {
  if (!m_assetsCreated) return;
  for (auto& u : m_units) {
    if (u.inst.IsValid()) m_renderContainer.RemoveMesh(u.inst);
    if (u.marker.IsValid()) m_renderContainer.RemoveMesh(u.marker);
  }
  m_units.clear();
  m_groups.clear();
  m_shots.clear();
  m_renderContainer.ClearMeshes();
  m_renderContainer.Destroy(pFramework ? pFramework->pVideoDriver : nullptr);
  m_unitPrimMgr.DestroyPrimitives();   // frees the template + per-unit SC glbs
  for (auto& um : m_unitModels) { um.templatePrimIdx = -1; um.templateSkinned = nullptr; um.animated = false; }
  if (m_envMapTexIndex >= 0 && t850::g_pBaseDriver) { t850::g_pBaseDriver->DestroyTexture(m_envMapTexIndex); m_envMapTexIndex = -1; }
  if (m_groundMesh) { m_groundMesh->Destroy(); m_groundMesh.reset(); }
  for (auto& m : m_unitMeshes) { if (m) { m->Destroy(); m.reset(); } }
  if (m_markerMesh) { m_markerMesh->Destroy(); m_markerMesh.reset(); }
  SceneProp.SSAOKernel.Destroy();
  m_navMesh.Clear();
  m_assetsCreated = false;
}

void RtsScene::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void RtsScene::OnDestoryScene() {
  DestroyAssets();
}

RtsScene::~RtsScene() {
  if (m_assetsCreated) DestroyAssets();
}

// ── Camera ─────────────────────────────────────────────────────────────────
void RtsScene::HandleWindowResize() {
  if (!pFramework || !pFramework->pVideoDriver) return;
  const int w = (std::max)(1, pFramework->pVideoDriver->width);
  const int h = (std::max)(1, pFramework->pVideoDriver->height);
  if (m_appliedResizeW == w && m_appliedResizeH == h) return;

  m_appliedResizeW = w;
  m_appliedResizeH = h;

  // Recreate the deferred render targets (GBuffer / Deferred / HDR, etc.) at the
  // new size. Without this the 3D content keeps rendering into RTs sized for the
  // initial window and is stretched onto the resized backbuffer.
  m_renderContainer.Resize(pFramework->pVideoDriver, w, h);

  // Update the camera aspect ratio to the new surface aspect. SetRatio rebuilds
  // the projection; the View*Projection (VP) matrix is recomputed by
  // m_camera.Update() called in UpdateCamera right after this.
  m_camera.SetRatio(static_cast<float>(w) / static_cast<float>(h));
  T8_LOG_INFO("[Rts] Resized render targets + camera aspect to %dx%d (aspect=%.3f)",
              w, h, static_cast<float>(w) / static_cast<float>(h));
}

void RtsScene::UpdateCamera(float dt) {
  const float panSpeed = 40.0f * dt;
  // Pan with WASD / arrow keys (screen-relative)
  if (pFramework && pFramework->pBaseApp) {
    auto& keys = pFramework->pBaseApp->IManager;
    if (keys.PressedKey(T800K_w) || keys.PressedKey(T800K_UP)) m_camTarget.z -= panSpeed;
    if (keys.PressedKey(T800K_s) || keys.PressedKey(T800K_DOWN)) m_camTarget.z += panSpeed;
    if (keys.PressedKey(T800K_a) || keys.PressedKey(T800K_LEFT)) m_camTarget.x -= panSpeed;
    if (keys.PressedKey(T800K_d) || keys.PressedKey(T800K_RIGHT)) m_camTarget.x += panSpeed;
  }
  // Clamp to field
  m_camTarget.x = Clampf(m_camTarget.x, -m_halfX, m_halfX);
  m_camTarget.z = Clampf(m_camTarget.z, -m_halfZ, m_halfZ);
  m_camTarget.y = m_groundY;

  // Zoom with scroll
  if (pFramework && pFramework->pBaseApp) {
    float scroll = pFramework->pBaseApp->IManager.scrollDelta;
    if (std::fabs(scroll) > 0.01f) {
      m_camOffsetLen = Clampf(m_camOffsetLen - scroll * 10.0f, 40.0f, 460.0f);
    }
  }

  m_camera.Eye = m_camTarget + m_camOffset * (m_camOffsetLen / m_camOffset.Length());
  m_camera.Velocity = XVECTOR3(0.0f,0.0f,0.0f,0.0f);
  m_camera.Update(dt);
}

// ── Selection ──────────────────────────────────────────────────────────────
bool RtsScene::ScreenToGround(int mouseX, int mouseY, XVECTOR3& out) const {
  const int w = pFramework->pVideoDriver->width;
  const int h = pFramework->pVideoDriver->height;
  XMATRIX44 vp = m_camera.VP;
  XMATRIX44 invVP;
  vp.Inverse(&invVP);
  const t850::Ray ray = t850::ScreenPointToRay(
    static_cast<float>(mouseX), static_cast<float>(mouseY), 0, 0, w, h, invVP);
  if (std::fabs(ray.direction.y) < 1e-6f) return false;
  float t = (m_groundY - ray.origin.y) / ray.direction.y;
  if (t < 0.0f) return false;
  out = XVECTOR3(ray.origin.x + ray.direction.x * t, m_groundY,
                 ray.origin.z + ray.direction.z * t, 1.0f);
  // Clamp to field
  out.x = Clampf(out.x, m_fieldMin.x, m_fieldMax.x);
  out.z = Clampf(out.z, m_fieldMin.z, m_fieldMax.z);
  return true;
}

void RtsScene::WorldToScreen(const XVECTOR3& world, float& outX, float& outY) const {
  const int w = pFramework->pVideoDriver->width;
  const int h = pFramework->pVideoDriver->height;
  XVECTOR3 clip = t850::TransformPoint(world, m_camera.VP);
  float ndcX = clip.x / (clip.w > 1e-6f ? clip.w : 1.0f);
  float ndcY = clip.y / (clip.w > 1e-6f ? clip.w : 1.0f);
  outX = (ndcX + 1.0f) * 0.5f * (float)w;
  outY = (1.0f - ndcY) * 0.5f * (float)h;
}

bool RtsScene::InRect(int px, int py, int x0, int y0, int x1, int y1) const {
  int minX = (std::min)(x0, x1), maxX = (std::max)(x0, x1);
  int minY = (std::min)(y0, y1), maxY = (std::max)(y0, y1);
  return px >= minX && px <= maxX && py >= minY && py <= maxY;
}

int RtsScene::PickUnitByRay(int mouseX, int mouseY, float maxDist) const {
  const int w = pFramework->pVideoDriver->width;
  const int h = pFramework->pVideoDriver->height;
  XMATRIX44 vp = m_camera.VP;
  XMATRIX44 invVP;
  vp.Inverse(&invVP);
  t850::Ray ray = t850::ScreenPointToRay(
    static_cast<float>(mouseX), static_cast<float>(mouseY), 0, 0, w, h, invVP);
  float rl = std::sqrt(ray.direction.x*ray.direction.x +
                       ray.direction.y*ray.direction.y +
                       ray.direction.z*ray.direction.z);
  if (rl < 1e-6f) return -1;
  ray.direction.x /= rl; ray.direction.y /= rl; ray.direction.z /= rl;
  int best = -1;
  float bestDist = maxDist;
  for (auto& u : m_units) {
    if (!u.alive) continue;
    XVECTOR3 toPoint(u.pos.x - ray.origin.x, u.pos.y - ray.origin.y, u.pos.z - ray.origin.z, 0.0f);
    float t = toPoint.x * ray.direction.x + toPoint.y * ray.direction.y + toPoint.z * ray.direction.z;
    if (t < 0.0f) continue;
    float cx = ray.origin.x + ray.direction.x * t;
    float cy = ray.origin.y + ray.direction.y * t;
    float cz = ray.origin.z + ray.direction.z * t;
    float dx = u.pos.x - cx, dy = u.pos.y - cy, dz = u.pos.z - cz;
    float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (dist < bestDist + u.radius) { bestDist = dist; best = u.id; }
  }
  return best;
}

void RtsScene::PointSelect(int mouseX, int mouseY, bool add) {
  int uid = PickUnitByRay(mouseX, mouseY, 3.0f);
  if (!add)
    for (auto& u : m_units) u.selected = false;
  if (uid >= 0 && uid < static_cast<int>(m_units.size()))
    m_units[uid].selected = true;
}

void RtsScene::BoxSelect(int x0, int y0, int x1, int y1) {
  // Project 4 corners to ground, build a 2D rect in world space
  XVECTOR3 c0, c1, c2, c3;
  if (!ScreenToGround(x0, y0, c0) || !ScreenToGround(x1, y0, c1) ||
      !ScreenToGround(x1, y1, c2) || !ScreenToGround(x0, y1, c3)) return;
  // World-space bounding box from 4 corners
  float wx0 = (std::min)((std::min)(c0.x, c1.x), (std::min)(c2.x, c3.x));
  float wx1 = (std::max)((std::max)(c0.x, c1.x), (std::max)(c2.x, c3.x));
  float wz0 = (std::min)((std::min)(c0.z, c1.z), (std::min)(c2.z, c3.z));
  float wz1 = (std::max)((std::max)(c0.z, c1.z), (std::max)(c2.z, c3.z));

  for (auto& u : m_units) {
    u.selected = u.alive &&
      u.pos.x >= wx0 - u.radius && u.pos.x <= wx1 + u.radius &&
      u.pos.z >= wz0 - u.radius && u.pos.z <= wz1 + u.radius;
  }
}

// ── Commands ───────────────────────────────────────────────────────────────
void RtsScene::ComputeFormationSlots(const XVECTOR3& center, int groupId, const XVECTOR3& facing) {
  if (groupId < 0 || groupId >= static_cast<int>(m_groups.size())) return;
  Group& grp = m_groups[groupId];
  // Only the SELECTED members of this group take part in the new formation.
  // Unselected members keep their current slot so they stay in place instead
  // of marching to the new target.
  std::vector<int> selected;
  for (int uid : grp.units)
    if (uid >= 0 && uid < static_cast<int>(m_units.size()) && m_units[uid].selected)
      selected.push_back(uid);
  if (selected.empty()) { grp.center = center; return; }

  int n = static_cast<int>(selected.size());
  int cols = (std::min)(grp.cols, n);
  cols = (std::max)(1, cols);
  int rows = (n + cols - 1) / cols;
  float spacing = 2.5f;

  // Formation axes: forward = facing, right = cross(forward, up)
  XVECTOR3 fwd = NormalizedXZ(facing);
  XVECTOR3 right = Cross3(XVECTOR3(0.0f,1.0f,0.0f,0.0f), fwd);
  right.Normalize();

  for (int i = 0; i < n; ++i) {
    int col = i % cols;
    int row = i / cols;
    float ox = (col - (cols - 1) / 2.0f) * spacing;
    float oz = (row - (rows - 1) / 2.0f) * spacing;
    XVECTOR3 slot = center + right * ox + fwd * oz;
    slot.y = TerrainHeight(slot.x, slot.z) + 0.5f;
    slot.x = Clampf(slot.x, m_fieldMin.x, m_fieldMax.x);
    slot.z = Clampf(slot.z, m_fieldMin.z, m_fieldMax.z);
    m_units[selected[i]].slot = slot;
  }
  grp.center = center;
}

void RtsScene::IssueCommand(Order order, const XVECTOR3& world, int attackTargetId) {
  bool any = false;
  for (auto& u : m_units) {
    if (!u.alive || !u.selected) continue;
    u.order = order;
    u.attackTargetId = attackTargetId;
    if (order == Order::Move || order == Order::Attack) {
      u.target = world;
    } else if (order == Order::Hold) {
      u.target = u.home;
    } else if (order == Order::Patrol) {
      u.patrolA = u.pos;
      u.patrolB = world;
      u.patrolDir = 1.0f;
      u.target = world;
    }
    u.path.clear();
    u.pathCursor = 0;
    u.repathTimer = 0.0f;
    any = true;
  }
  if (!any) return;

  // Compute formation slots for each affected group
  for (auto& grp : m_groups) {
    bool grpAffected = false;
    for (int uid : grp.units)
      if (uid >= 0 && uid < static_cast<int>(m_units.size()) && m_units[uid].selected)
        grpAffected = true;
    if (!grpAffected) continue;

    XVECTOR3 center = world;
    center.y = m_groundY + 0.5f;
    center.x = Clampf(center.x, m_fieldMin.x, m_fieldMax.x);
    center.z = Clampf(center.z, m_fieldMin.z, m_fieldMax.z);

    XVECTOR3 facing = world - grp.center;
    if (facing.Length() < 0.1f) facing = XVECTOR3(1.0f,0.0f,0.0f,0.0f);
    ComputeFormationSlots(center, static_cast<int>(&grp - m_groups.data()), facing);
  }

  T8_LOG_INFO("[Rts] Command %d issued to selected units", static_cast<int>(order));
}

// ── Simulation ─────────────────────────────────────────────────────────────
XVECTOR3 RtsScene::Separation(const Unit& u) const {
  XVECTOR3 force{0.0f, 0.0f, 0.0f, 0.0f};
  const float sepRadius = 3.0f;
  for (const auto& other : m_units) {
    if (!other.alive || &other == &u || other.teamIndex != u.teamIndex) continue;
    float dx = u.pos.x - other.pos.x;
    float dz = u.pos.z - other.pos.z;
    float d = std::sqrt(dx*dx + dz*dz);
    float minDist = (u.radius + other.radius) * 2.0f;
    if (d < minDist && d > 1e-4f) {
      float push = (minDist - d) / minDist;
      force.x += (dx / d) * push;
      force.z += (dz / d) * push;
    } else if (d < sepRadius && d > 1e-4f) {
      float push = (sepRadius - d) / sepRadius * 0.3f;
      force.x += (dx / d) * push;
      force.z += (dz / d) * push;
    }
  }
  return force;
}

void RtsScene::UpdateUnit(Unit& u, float dt) {
  if (!u.alive) return;
  const UnitType& ut = m_unitTypes[u.typeIndex];
  float speed = ut.speed;

  // Find movement target
  XVECTOR3 dest = u.slot;
  bool hasDest = false;

  switch (u.order) {
    case Order::Move:
      dest = u.slot;
      hasDest = true;
      break;
    case Order::Attack: {
      // If we have a valid attack target, move toward it
      if (u.attackTargetId >= 0 && u.attackTargetId < static_cast<int>(m_units.size())) {
        Unit& target = m_units[u.attackTargetId];
        if (target.alive) {
          dest = target.pos;
          hasDest = true;
        } else {
          u.attackTargetId = -1;
          u.order = Order::Hold;
        }
      } else {
        // Find nearest enemy
        int enemy = NearestUnit(u.pos, u.id, 50.0f);
        if (enemy >= 0) {
          Unit& e = m_units[enemy];
          if (e.teamIndex != u.teamIndex) {
            u.attackTargetId = enemy;
            dest = e.pos;
            hasDest = true;
          }
        } else {
          u.order = Order::Hold;
        }
      }
      break;
    }
    case Order::Hold:
      dest = u.home;
      hasDest = true;
      break;
    case Order::Patrol: {
      XVECTOR3 target = (u.patrolDir > 0.0f) ? u.patrolB : u.patrolA;
      if (DistXZ(u.pos, target) < 2.0f) {
        u.patrolDir = -u.patrolDir;
        target = (u.patrolDir > 0.0f) ? u.patrolB : u.patrolA;
      }
      dest = target;
      hasDest = true;
      break;
    }
    case Order::Idle:
    default:
      break;
  }

  if (!hasDest) {
    u.vel = XVECTOR3(0.0f,0.0f,0.0f,0.0f);
    return;
  }

  float dist = DistXZ(u.pos, dest);

  // Attack range check (for Attack/Hold: stop and fight when in range)
  bool inAttackRange = false;
  if (u.order == Order::Attack || u.order == Order::Hold) {
    if (u.attackTargetId >= 0 && u.attackTargetId < static_cast<int>(m_units.size())) {
      Unit& target = m_units[u.attackTargetId];
      if (target.alive) inAttackRange = (DistXZ(u.pos, target.pos) <= ut.range + 0.5f);
    }
    if (u.order == Order::Hold && u.attackTargetId < 0) {
      // In Hold, check if any enemy is in range
      for (const auto& e : m_units) {
        if (e.alive && e.teamIndex != u.teamIndex && DistXZ(u.pos, e.pos) <= ut.range + 0.5f) {
          inAttackRange = true;
          u.attackTargetId = e.id;
          break;
        }
      }
    }
  }

  // Move toward destination
  if (dist > (inAttackRange ? 0.1f : 0.5f)) {
    XVECTOR3 toDest = NormalizedXZ(dest - u.pos);

    // Use navmesh path if available
    if (m_navReady && dist > 5.0f && (u.repathTimer <= 0.0f || u.pathCursor >= u.path.size())) {
      u.repathTimer = 1.5f; // repath every 1.5s
      t850::navigation::NavPathRequest req;
      req.start = u.pos;
      req.end = dest;
      req.queryExtents = XVECTOR3(1.0f, 2.0f, 1.0f, 0.0f);
      auto result = m_navMesh.FindPath(req);
      if (result.success && !result.points.empty()) {
        u.path = result.points;
        u.pathCursor = 0;
      } else {
        // Destination unreachable (e.g. inside the non-walkable valley): drop
        // any stale path. The unit then heads straight for the target and the
        // per-frame walkability guard below stops it at the edge of the
        // walkable area instead of letting it enter the blocked zone.
        u.path.clear();
        u.pathCursor = 0;
      }
    }
    u.repathTimer -= dt;

    // Follow path or go direct
    XVECTOR3 moveDir = toDest;
    if (!u.path.empty() && u.pathCursor < u.path.size()) {
      XVECTOR3 wp = u.path[u.pathCursor];
      if (DistXZ(u.pos, wp) < 1.0f) u.pathCursor++;
      if (u.pathCursor < u.path.size())
        moveDir = NormalizedXZ(u.path[u.pathCursor] - u.pos);
    }

    // Add separation
    XVECTOR3 sep = Separation(u);
    moveDir = moveDir + sep * 1.5f;
    moveDir.Normalize();

    u.vel = moveDir * speed;
  } else {
    u.vel *= 0.5f; // decelerate when arrived
  }

  // Integrate — but only onto the walkable surface. The navmesh is the
  // authority on where units may stand: the next position must project onto a
  // walkable polygon, otherwise the unit stops at the edge of the walkable
  // area (e.g. the rim of the valley) instead of walking into the blocked zone.
  XVECTOR3 next = u.pos;
  next.x += u.vel.x * dt;
  next.z += u.vel.z * dt;
  if (m_navReady) {
    XVECTOR3 onMesh;
    if (m_navMesh.ProjectPoint(next, onMesh, XVECTOR3(1.0f, 3.0f, 1.0f, 0.0f))) {
      // Snap to the walkable surface (keeps the unit glued to the navmesh).
      next.x = onMesh.x;
      next.z = onMesh.z;
    } else if (DistXZ(u.pos, next) > 0.01f) {
      // The step would leave the walkable surface. If we are somehow already
      // off the mesh (stale position), steer back to the nearest walkable
      // point so we don't get stuck; otherwise hold at the edge.
      XVECTOR3 onMeshFar;
      if (m_navMesh.ProjectPoint(u.pos, onMeshFar, XVECTOR3(12.0f, 8.0f, 12.0f, 0.0f)) &&
          DistXZ(u.pos, onMeshFar) > 1.5f) {
        XVECTOR3 backDir = NormalizedXZ(onMeshFar - u.pos);
        next = u.pos;
        next.x += backDir.x * speed * dt;
        next.z += backDir.z * speed * dt;
      } else {
        next = u.pos;  // hold at the edge of the walkable area
      }
    } else {
      next = u.pos;
    }
  }
  u.pos = next;
  u.pos.y = TerrainHeight(u.pos.x, u.pos.z) + 0.5f * u.scale;

  // Clamp to field
  u.pos.x = Clampf(u.pos.x, m_fieldMin.x + u.radius, m_fieldMax.x - u.radius);
  u.pos.z = Clampf(u.pos.z, m_fieldMin.z + u.radius, m_fieldMax.z - u.radius);

  // Face movement direction. RotateYAbsolute expects DEGREES (calls Deg2Rad
  // internally). The unit GLBs face -Z locally (verified: they walked backwards
  // with the raw atan2), so add PI to point +Z along velocity.
  if (u.vel.Length() > 0.1f) {
    u.yaw = (std::atan2(u.vel.x, u.vel.z) + 3.14159265f) * (180.0f / 3.14159265358979f);
  }
}

void RtsScene::Combat(float dt) {
  for (auto& u : m_units) {
    if (!u.alive) continue;
    const UnitType& ut = m_unitTypes[u.typeIndex];
    u.attackCooldown -= dt;
    u.attackFlash = (std::max)(0.0f, u.attackFlash - dt * 4.0f);

    if (u.attackCooldown > 0.0f) continue;
    if (u.order != Order::Attack && u.order != Order::Hold) continue;

    // Find target
    int targetId = u.attackTargetId;
    if (targetId < 0 || targetId >= static_cast<int>(m_units.size()) || !m_units[targetId].alive) {
      targetId = -1;
      for (const auto& e : m_units) {
        if (e.alive && e.teamIndex != u.teamIndex && DistXZ(u.pos, e.pos) <= ut.range + 0.5f) {
          targetId = e.id;
          break;
        }
      }
    }
    if (targetId < 0) continue;

    Unit& target = m_units[targetId];
    if (!target.alive || target.teamIndex == u.teamIndex) continue;
    if (DistXZ(u.pos, target.pos) > ut.range + 1.0f) continue;

    // Attack!
    target.hp -= ut.attack;
    u.attackCooldown = 1.0f / (ut.attack * 0.15f); // ~1-2 attacks/sec
    u.attackFlash = 1.0f;
    u.attackTargetId = targetId;

    // Shot visual
    m_shots.push_back({u.pos, target.pos, 0.15f});
    if (m_shots.size() > 64) m_shots.erase(m_shots.begin());

    T8_LOG_INFO("[Rts] Unit %d (%s) attacks unit %d for %.0f dmg (hp=%.0f/%.0f)",
      u.id, m_unitTypes[u.typeIndex].id.c_str(), target.id, ut.attack,
      target.hp, target.maxHp);
  }
}

void RtsScene::CleanupDead() {
  for (int i = static_cast<int>(m_units.size()) - 1; i >= 0; --i) {
    if (m_units[i].alive && m_units[i].hp <= 0.0f) {
      T8_LOG_INFO("[Rts] Unit %d (%s) died", m_units[i].id,
        m_unitTypes[m_units[i].typeIndex].id.c_str());
      RemoveUnit(i);
    }
  }
}

void RtsScene::UpdateUnits(float dt) {
  for (auto& u : m_units) UpdateUnit(u, dt);
  Combat(dt);
  CleanupDead();
  // Update shots
  for (auto& s : m_shots) s.life -= dt;
  m_shots.erase(std::remove_if(m_shots.begin(), m_shots.end(),
    [](const Shot& s) { return s.life <= 0.0f; }), m_shots.end());
}

// ── Per-frame ──────────────────────────────────────────────────────────────
void RtsScene::OnUpdate(float dt) {
  m_dt = dt;
  if (!m_assetsCreated) return;
  // Frame delta used by skinned-mesh animation updates (UpdateAnimationPose).
  SceneProp.FrameDeltaSec = dt;
  // Pick up any window resize (RTs + camera aspect) before the camera Update
  // recomputes the VP matrix with the new aspect.
  HandleWindowResize();
  UpdateCamera(dt);
  UpdateUnits(dt);
  // Advance every unit's skeletal animation pose (CPU bone update). The bone
  // texture upload happens in OnDraw where the command buffer is active.
  UpdateUnitAnimations();
}

void RtsScene::SyncInstances() {
  for (auto& u : m_units) {
    if (!u.alive || !u.inst.IsValid()) continue;
    t850::PrimitiveInst* p = m_renderContainer.GetMesh(u.inst);
    if (p) {
      p->TranslateAbsolute(u.pos.x, u.pos.y + u.modelLift, u.pos.z);
      p->ScaleAbsolute(u.modelScale, u.modelScale, u.modelScale);
      p->RotateYAbsolute(u.yaw);
      p->SetBrightness(1.0f + u.attackFlash * 0.8f);
      p->Update();
    }
    if (u.marker.IsValid()) {
      t850::PrimitiveInst* mp = m_renderContainer.GetMesh(u.marker);
      if (mp) {
        // Ring sits ON the terrain surface under the unit (u.pos.y is the unit's
        // center, in the air) — so use the sampled ground height, slightly
        // lifted to avoid z-fighting with the terrain.
        const float groundY = TerrainHeight(u.pos.x, u.pos.z) + 0.08f;
        mp->TranslateAbsolute(u.pos.x, groundY, u.pos.z);
        mp->ScaleAbsolute(u.radius * 2.5f, 1.0f, u.radius * 2.5f);
        mp->SetVisible(u.selected);
        mp->Update();
      }
    }
  }
}

void RtsScene::OnDraw() {
  if (!m_assetsCreated) return;
  SyncInstances();
  // Upload each unit's bone matrices to its bone texture. Must run before the
  // render graph (which samples the textures) and while the frame command
  // buffer is active (BeginFrame precedes OnDraw in the app loop).
  for (auto& u : m_units) {
    if (u.animPrimIdx < 0) continue;
    if (auto* sk = dynamic_cast<t850::RenderSkinnedMesh*>(m_unitPrimMgr.GetPrimitive(u.animPrimIdx)))
      sk->UploadBoneTexture();
  }
  m_renderContainer.Execute(pFramework->pVideoDriver, m_dt);
  if (m_dumper.ShouldDump(m_dt)) {
    std::vector<t850::RTDumpEntry> targets = {
      {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
      {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {m_renderContainer.Graph().GetRTHandle("Deferred"), t850::BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
      {m_renderContainer.Graph().GetRTHandle("ExtraHelper"), t850::BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"}};
    m_dumper.DumpFrame(pFramework->pVideoDriver, m_camera, m_lightCamera, SceneProp, targets, m_dt);
    if (m_dumper.ShouldExit()) std::exit(0);
  }
}

void RtsScene::OnInput(InputManager* input) {
  if (!m_assetsCreated) return;

  const int mx = input->mouseX;
  const int my = input->mouseY;
  const bool lmbDown = input->PressedMouseButton(0);
  const bool rmbDown = input->PressedMouseButton(2);

  // ── Box selection (LMB) ──
  if (input->PressedOnceMouseButton(0)) {
    m_leftDown = true;
    m_selStartX = m_selCurX = mx;
    m_selStartY = m_selCurY = my;
    m_boxActive = false;
  }
  if (m_leftDown && lmbDown) {
    m_selCurX = mx;
    m_selCurY = my;
    if (std::abs(mx - m_selStartX) > 5 || std::abs(my - m_selStartY) > 5)
      m_boxActive = true;
  }
  if (m_leftDown && !lmbDown) {
    m_leftDown = false;
    if (m_boxActive) {
      BoxSelect(m_selStartX, m_selStartY, m_selCurX, m_selCurY);
    } else {
      // Shift+click adds to selection (standard RTS behavior).
      const bool add = input->PressedKey(T800K_LSHIFT) || input->PressedKey(T800K_RSHIFT);
      PointSelect(mx, my, add);
    }
    m_boxActive = false;
  }

  // ── Commands (RMB) ──
  if (input->PressedOnceMouseButton(2)) {
    XVECTOR3 world;
    if (ScreenToGround(mx, my, world)) {
      // Snap destination to terrain height (ScreenToGround projects to y=0).
      world.y = TerrainHeight(world.x, world.z);
      // Ray-based 3D pick: check if clicking on an enemy → attack, else move.
      int clicked = PickUnitByRay(mx, my, 2.5f);
      if (clicked >= 0 && clicked < static_cast<int>(m_units.size())) {
        Unit& cu = m_units[clicked];
        bool isEnemy = false;
        for (const auto& u : m_units) {
          if (u.selected && u.alive && u.teamIndex != cu.teamIndex) { isEnemy = true; break; }
        }
        if (isEnemy) {
          IssueCommand(Order::Attack, world, clicked);
          return;
        }
      }
      IssueCommand(Order::Move, world, -1);
    }
  }

  // ── Hold (H key) ──
  if (input->PressedOnceKey(T800K_h)) {
    IssueCommand(Order::Hold, m_camTarget, -1);
  }

  // ── Patrol (P key) ──
  if (input->PressedOnceKey(T800K_p)) {
    XVECTOR3 world;
    if (ScreenToGround(mx, my, world))
      IssueCommand(Order::Patrol, world, -1);
  }

  // ── Attack all (T key — A is reserved for camera pan) ──
  if (input->PressedOnceKey(T800K_t)) {
    for (auto& u : m_units) {
      if (u.alive && u.selected) {
        int enemy = NearestUnit(u.pos, u.id, 80.0f);
        if (enemy >= 0) {
          Unit& e = m_units[enemy];
          if (e.teamIndex != u.teamIndex) {
            u.order = Order::Attack;
            u.attackTargetId = enemy;
            u.target = e.pos;
            u.path.clear();
            u.pathCursor = 0;
          }
        }
      }
    }
  }

  // ── Deselect (Escape) ──
  if (input->PressedOnceKey(T800K_ESCAPE)) {
    for (auto& u : m_units) u.selected = false;
  }
}

// ── HUD ────────────────────────────────────────────────────────────────────
void RtsScene::DrawHud() {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (!viewport || !dl) return;
  const float W = viewport->Size.x;
  const float H = viewport->Size.y;

  // ── Selection rectangle ──
  if (m_boxActive) {
    ImVec2 a(m_selStartX, m_selStartY);
    ImVec2 b(m_selCurX, m_selCurY);
    dl->AddRect(a, b, IM_COL32(255, 255, 100, 180), 0.0f, 0, 1.5f);
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 100, 25));
  }

  // ── Health bars above selected units ──
  for (const auto& u : m_units) {
    if (!u.alive || !u.selected) continue;
    float sx, sy;
    WorldToScreen(u.pos, sx, sy);
    if (sx < 0 || sx > W || sy < 0 || sy > H) continue;
    float barW = 24.0f, barH = 3.0f;
    float hpFrac = Clampf(u.hp / u.maxHp, 0.0f, 1.0f);
    ImVec2 bg(sx - barW * 0.5f, sy - 18.0f);
    dl->AddRectFilled(bg, ImVec2(bg.x + barW, bg.y + barH), IM_COL32(40, 40, 40, 200));
    ImU32 hpCol = hpFrac > 0.5f ? IM_COL32(80, 220, 80, 230)
                : hpFrac > 0.25f ? IM_COL32(230, 200, 50, 230)
                : IM_COL32(230, 60, 60, 230);
    dl->AddRectFilled(bg, ImVec2(bg.x + barW * hpFrac, bg.y + barH), hpCol);
    dl->AddRect(bg, ImVec2(bg.x + barW, bg.y + barH), IM_COL32(200, 200, 200, 120));
  }

  // ── Shot tracers ──
  for (const auto& s : m_shots) {
    float x0, y0, x1, y1;
    WorldToScreen(s.from, x0, y0);
    WorldToScreen(s.to, x1, y1);
    float alpha = static_cast<int>(255.0f * Clampf(s.life / 0.15f, 0.0f, 1.0f));
    dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 220, 80, alpha), 1.5f);
  }

  // ── Status bar ──
  int selCount = 0;
  for (const auto& u : m_units) if (u.alive && u.selected) selCount++;
  int totalAlive = 0;
  for (const auto& u : m_units) if (u.alive) totalAlive++;

  char status[128];
  snprintf(status, sizeof(status), "RTS  |  Units: %d  |  Selected: %d  |  "
    "LMB=Select  RMB=Move/Attack  T=AttackAll  H=Hold  P=Patrol  WASD=Pan  Scroll=Zoom",
    totalAlive, selCount);
  ImVec2 panelMin(10.0f, H - 32.0f);
  ImVec2 panelMax(W - 10.0f, H - 8.0f);
  dl->AddRectFilled(panelMin, panelMax, IM_COL32(10, 12, 18, 200), 4.0f);
  dl->AddText(ImVec2(panelMin.x + 10.0f, panelMin.y + 8.0f), IM_COL32(220, 220, 230, 255), status);
}
