#pragma once

#include <core/Core.h>
#include <debug/FrameDumper.h>
#include <imgui/DevGuiContext.h>
#include <navigation/NavigationSystem.h>
#include <scene/MutableMesh.h>
#include <scene/RenderContainer.h>
#include <scene/RenderSkinnedMesh.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

// ── Data-driven configuration (parsed from the "rts" block of Rts.t8scene) ──
namespace rtsdata {
struct Vec3f { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Color { float x = 1.0f, y = 1.0f, z = 1.0f, w = 1.0f; };
struct Field { float size_x = 120.0f, size_z = 80.0f, y = 0.0f; };
struct CameraCfg { Vec3f position; Vec3f target; float fov_deg = 40.0f; float near = 1.0f; float far = 2000.0f; };
struct LightCfg { Vec3f direction; Color color; float intensity = 2.4f; Color ambient; };
struct UnitType { std::string id; float health = 100.0f, attack = 10.0f, range = 6.0f, speed = 6.0f, radius = 0.7f, scale = 1.0f; };
struct Team { std::string id; Color color; std::string shape = "sphere"; int spawn_side = -1; };
struct GroupMember { std::string type; int count = 1; };
struct GroupCfg { std::string id; std::string team; float home_x = 0.0f; int formation_cols = 5; std::vector<GroupMember> members; };
// An authored rectangular (XZ) box: center (x,z) + half extents. Used for the
// Nexus non-walkable zone (mirrors the "Exclude Volume" box in Nexus.t8scene).
struct ZoneBox { float x = 0.0f, z = 0.0f, half_x = 0.0f, half_z = 0.0f; };

// Mirrors the nested "rts" block of Rts.t8scene
struct RtsBlock {
  std::optional<Field> field;
  std::optional<Color> ground_color;
  std::optional<ZoneBox> nonwalkable;      // central non-walkable region (navmesh exclude)
  std::optional<float> lane_half_z;        // walkable corridor half-width through the center
  std::optional<Color> nonwalkable_color;  // visual tint for the non-walkable zone
  std::optional<CameraCfg> camera;
  std::optional<LightCfg> light;
  std::vector<UnitType> unit_types;
  std::vector<Team> teams;
  std::vector<GroupCfg> groups;
};

struct SceneFile {
  std::string render_graph;
  std::string control_descriptor;
  std::optional<RtsBlock> rts;
};
} // namespace rtsdata

class RtsScene : public t850::SceneBase {
public:
  RtsScene() = default;
  ~RtsScene() override;

  void InitVars() override;
  void CreateAssets() override;
  void DestroyAssets() override;
  void OnLoadScene() override;
  void OnDestoryScene() override;
  void OnUpdate(float deltaSeconds) override;
  void OnDraw() override;
  void OnInput(InputManager* input) override;
  void RequestDump() override { m_dumper.RequestDump(); }
  void ResetViewInput() override {}
  void DrawGameplayGui(t850::DevGuiContext& /*gui*/) override { DrawHud(); }
  bool AllowsInputWhenRuntimeGuiVisible() const override { return true; }

private:
  enum class Order : int { Idle = 0, Move = 1, Attack = 2, Hold = 3, Patrol = 4 };

  struct UnitType {
    std::string id;
    float health = 100.0f, attack = 10.0f, range = 6.0f, speed = 6.0f, radius = 0.7f, scale = 1.0f;
  };
  struct Team {
    std::string id;
    XVECTOR3 color;
    bool sphere = true;
    int spawnSide = -1;
  };

  struct Unit {
    int id = -1;
    int typeIndex = 0;
    int teamIndex = 0;
    int groupId = -1;
    XVECTOR3 pos;
    XVECTOR3 vel;
    XVECTOR3 heading;
    bool hasHeading = false;
    float hp = 0.0f, maxHp = 1.0f;
    float radius = 0.7f;
    float scale = 1.0f;
    float yaw = 0.0f;
    bool alive = true;
    bool selected = false;

    Order order = Order::Idle;
    XVECTOR3 target;
    int attackTargetId = -1;
    XVECTOR3 slot;                   // formation slot (absolute world)
    XVECTOR3 home;                   // formation home slot
    XVECTOR3 patrolA;
    XVECTOR3 patrolB;
    float patrolDir = 1.0f;

    std::vector<XVECTOR3> path;
    std::size_t pathCursor = 0;
    float repathTimer = 0.0f;

    float attackCooldown = 0.0f;
    float attackFlash = 0.0f;

    t850::RenderInstanceHandle inst;
    t850::RenderInstanceHandle marker;

    // Resolved render model (actual SC glb for the visual test); set in CreateAssets.
    int meshIdx = -1;
    float modelScale = 1.0f;
    float modelLift = 0.0f;
    bool useModel = false;
    // Per-unit skinned primitive (index into m_unitPrimMgr). Each unit owns its
    // own RenderSkinnedMesh so it can animate independently (shared primitives
    // would all play the same pose). -1 = not a skinned unit.
    int animPrimIdx = -1;
  };

  struct Group {
    std::string id;
    int teamIndex = 0;
    float homeX = 0.0f;
    int cols = 5;
    std::vector<int> units;   // indices into m_units
    XVECTOR3 center;
  };

  struct Shot {
    XVECTOR3 from;
    XVECTOR3 to;
    float life = 0.0f;
  };

  // ── Config load ──
  bool LoadConfig();
  void BuildDefaultConfig();

  // ── Geometry ──
  void BuildGroundMesh();
  void BuildTerrainMesh();     // render the real Nexus terrain glb (elevation/valleys)
  void BuildHeightSampler();   // downsample terrain heights for unit Y placement
  float TerrainHeight(float x, float z) const;  // nearest-sampled surface height
  void BuildUnitMeshes();
  void LoadUnitModels();
  void BuildSelectionMarker();
  void BuildNavMesh();

  t850::MutableMeshSnapshot MakeQuad(float half, const XVECTOR3& normal, const XVECTOR3& color) const;
  t850::MutableMeshSnapshot MakeCube(float half, const XVECTOR3& color) const;
  t850::MutableMeshSnapshot MakeSphere(float radius, int slices, int stacks, const XVECTOR3& color) const;
  bool CommitMesh(t850::MutableMesh* mesh, t850::MutableMeshSnapshot snap);

  // ── Units ──
  void SpawnUnits();
  int AddUnit(int typeIndex, int teamIndex, int groupId, const XVECTOR3& pos);
  void RemoveUnit(int index);
  int NearestUnit(const XVECTOR3& world, int excludeId, float maxDist) const;
  int UnitAt(const XVECTOR3& world, float radius) const;

  // ── Unit animation ──
  // Create one per-unit skinned primitive (a fresh RenderSkinnedMesh that shares
  // the cached parsed model + GPU geometry, but owns its own
  // AnimationController/bone texture so it animates independently). Returns the
  // primitive index (>=0) or -1 if the type has no animated model.
  int CreateUnitAnimPrimitive(int typeIndex);
  // Find the set index of the named animation (exact, case-sensitive) by reading
  // the primitive's parsed model; -1 if absent.
  int FindAnimSet(const t850::RenderSkinnedMesh* sk, const char* name) const;
  // Drive one unit's animation this frame: advance the pose (CPU bone update),
  // picking the right clip (walk / attack / stand) from the unit's state.
  void ApplyUnitAnimState(Unit& u);
  // Advance all units' animation poses (call once per frame in OnUpdate).
  void UpdateUnitAnimations();

  // ── Selection ──
  bool ScreenToGround(int mouseX, int mouseY, XVECTOR3& out) const;
  // Ray-based 3D picking: closest unit to the camera ray through (mouseX,mouseY),
  // or -1. Works on elevated terrain (unlike the flat-plane ScreenToGround).
  int PickUnitByRay(int mouseX, int mouseY, float maxDist) const;
  void WorldToScreen(const XVECTOR3& world, float& outX, float& outY) const;
  // add=true (Shift+click) keeps the existing selection and adds the picked
  // unit; add=false clears the selection first.
  void PointSelect(int mouseX, int mouseY, bool add = false);
  void BoxSelect(int x0, int y0, int x1, int y1);
  bool InRect(int px, int py, int x0, int y0, int x1, int y1) const;

  // ── Commands ──
  void IssueCommand(Order order, const XVECTOR3& world, int attackTargetId);
  void ComputeFormationSlots(const XVECTOR3& center, int groupId, const XVECTOR3& facing);

  // ── Simulation ──
  void UpdateUnits(float dt);
  void UpdateUnit(Unit& u, float dt);
  void SeparationForces();
  void Combat(float dt);
  XVECTOR3 Separation(const Unit& u) const;
  void CleanupDead();

  // ── Camera ──
  void UpdateCamera(float dt);
  // Called once per frame (before UpdateCamera). When the window/surface size
  // changes it recreates the deferred render targets at the new size (so the 3D
  // content isn't stretched onto the new backbuffer) and updates the camera
  // aspect ratio to match.
  void HandleWindowResize();

  // ── Render sync ──
  void SyncInstances();
  void DrawHud();

  // ── State ──
  static constexpr int kSceneIndex = 7;
  rtsdata::RtsBlock m_rts;
  std::string m_renderGraphPath = "Scenes/SceneTemplate_RenderGraph.json";
  std::vector<UnitType> m_unitTypes;
  std::vector<Team> m_teams;
  std::vector<Group> m_groups;
  std::vector<Unit> m_units;
  std::vector<Shot> m_shots;
  int m_maxUnits = 128;

  XVECTOR3 m_fieldMin;
  XVECTOR3 m_fieldMax;
  float m_groundY = 0.0f;
  float m_halfX = 60.0f;
  float m_halfZ = 40.0f;
  // Nexus non-walkable zone: the single authored "Exclude Volume" (the deep
  // central valley). On the real terrain its low floor (y~0) is carved from the
  // navmesh while the raised lane (y~8) and base plateaus (y~10-12) stay
  // walkable above it. Valid = the zone has real size.
  rtsdata::ZoneBox m_nonwalkBox;
  float m_laneHalfZ = 8.0f;  // legacy config field, no longer used for splitting
  bool m_nonwalkValid = false;

  // Real Nexus terrain (elevation + valleys). Rendered from the authored glb and
  // sampled to place units on the surface.
  int m_terrainPrimIdx = -1;
  std::vector<float> m_terrainVerts;   // x,y,z floats (terrain local space)
  std::vector<float> m_terrainHm;      // heightmap, size m_terrainHmH * m_terrainHmW
  int m_terrainHmW = 0, m_terrainHmH = 0;  // heightmap dims (W=Z cells, H=X cells)
  float m_terrainHmMinX = 0.0f, m_terrainHmMaxX = 0.0f;
  float m_terrainHmMinZ = 0.0f, m_terrainHmMaxZ = 0.0f;
  bool m_terrainLoaded = false;

  // Camera
  Camera m_camera;
  Camera m_lightCamera;
  XVECTOR3 m_camTarget;
  XVECTOR3 m_camOffset;
  float m_camOffsetLen = 142.0f;

  // Last window/surface size the scene sized its RTs + camera aspect for.
  // Updated by HandleWindowResize so we only rebuild RTs / reproject when the
  // size actually changes (window drag fires many resize events).
  int m_appliedResizeW = 0;
  int m_appliedResizeH = 0;

  // Input
  bool m_leftDown = false;
  int m_selStartX = 0, m_selStartY = 0;
  int m_selCurX = 0, m_selCurY = 0;
  bool m_boxActive = false;

  // Post-process kernels (shadow blur / bloom / dof)
  GaussFilter m_shadowFilter;
  GaussFilter m_bloomFilter;
  GaussFilter m_dofFilter;

  // Rendering
  t850::RenderContainer m_renderContainer;
  // Environment map + IBL (Minecraft skybox): the deferred pass samples the
  // cubemap for the background and uses the IBL maps for ambient lighting.
  int m_envMapTexIndex = -1;
  int m_diffuseIBLTexIndex = -1;
  int m_specularIBLTexIndex = -1;
  int m_brdfLUTTexIndex = -1;
  int m_sheenIBLTexIndex = -1;
  int m_charlieLUTTexIndex = -1;
  int m_sheenELUTTexIndex = -1;
  t850::EnvironmentMapSet m_envMaps;
  // Texture loader prepends "Textures/", so omit it here (matches DayScene/Minecraft).
  std::string m_cubemapPath = "sky/CubeMap_SkyWater.dds";
  std::unique_ptr<t850::MutableMesh> m_groundMesh;
  std::array<std::unique_ptr<t850::MutableMesh>, 4> m_unitMeshes; // [team0sphere, team0cube, team1sphere, team1cube]
  std::unique_ptr<t850::MutableMesh> m_markerMesh;
  // Actual SC unit models (animated glb) for the visual test; the procedural
  // sphere/cube above remain as a fallback if a model fails to load. One
  // "template" skinned primitive is created per type (used only to discover
  // the animation set indices); each unit then clones it into its own
  // per-unit primitive so units animate independently.
  struct UnitModel {
    std::string path;                          // animated glb path ("" = none)
    int templatePrimIdx = -1;                  // per-type template (for set discovery)
    t850::RenderSkinnedMesh* templateSkinned = nullptr; // template skinned mesh
    float scale = 1.0f;
    float lift = 0.0f;      // world-Y lift so the model's feet sit on the ground
    bool valid = false;
    bool animated = false;  // true if the template has skin/animation data
    // Discovered animation set indices (exact name match); -1 = absent.
    int setWalk = -1;
    int setAttack = -1;
    int setStand = -1;
    float walkSpeed = 1.0f;   // playback speed for the walk clip
    float attackSpeed = 1.0f; // playback speed for the attack clip
  };
  std::vector<UnitModel> m_unitModels;    // one per unit type (by typeIndex)
  t850::PrimitiveManager m_unitPrimMgr;
  t850::RenderInstanceHandle m_groundInst;
  t850::navigation::NavMesh m_navMesh;
  bool m_navReady = false;
  t850::FrameDumper m_dumper;
  float m_dt = 0.0f;
  bool m_assetsCreated = false;
  bool m_configLoaded = false;
  float m_fovRad = 0.7f;
};
