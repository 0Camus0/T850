#pragma once

#include <core/Core.h>
#include <debug/FrameDumper.h>
#include <physics/CharacterController.h>
#include <physics/PhysicsTypes.h>
#include <scene/MutableMesh.h>
#include <scene/RenderContainer.h>
#include <scene/TextRenderer.h>
#include <terrain/BlockRegistry.h>
#include <terrain/VoxelMesher.h>
#include <terrain/VoxelPersistence.h>
#include <terrain/VoxelStreaming.h>
#include <terrain/VoxelWorld.h>
#include <utils/CameraProfiles.h>
#include <utils/xMaths.h>  // complete XVECTOR3 definition (in-class initializers)

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

// A playable Minecraft-style scene built on the T850 voxel terrain foundation.
//
//   * Streaming 16^3 chunks with a seeded multi-octave value-noise heightmap,
//     water + sand waterline, and procedurally-placed oak trees.
//   * A 4x4 procedurally-generated pixel-art block atlas (grass/dirt/stone/sand/
//     cobble/planks/log/leaves/water/bedrock) uploaded as an in-memory texture.
//   * A flat Minecraft-blue sky cubemap supplied to the render container.
//   * First-person character controller with AABB voxel collision, LMB break,
//     RMB place, and a 9-slot hotbar (keys 1..9 / mouse wheel) of placeable
//     blocks.  Edits are persisted to VoxelWorlds/minecraft/edits.t8vox.
class MinecraftScene final : public t850::SceneBase, public t850::CharacterCollisionWorld {
public:
  MinecraftScene();

  void InitVars() override;
  void CreateAssets() override;
  void DestroyAssets() override;
  void OnLoadScene() override;
  void OnDestoryScene() override;
  void OnUpdate(float deltaSeconds) override;
  void OnDraw() override;
  void OnInput(InputManager* input) override;
  void RequestDump() override { m_dumper.RequestDump(); }
  void ResetViewInput() override { m_cameraController.ClearInput(); }

  bool SweepCapsule(const t850::CharacterCollisionSweep& sweep,
                    t850::CharacterCollisionHit& hit) const override;
  bool SweepBox(const t850::CharacterBoxSweep& sweep,
                t850::CharacterCollisionHit& hit) const override;

private:
  struct ChunkRender {
    std::unique_ptr<t850::MutableMesh> mesh;
    t850::RenderInstanceHandle instance;
    t850::PhysicsBodyHandle body;
  };

  struct Mob {
    XVECTOR3 pos = {};  // feet position (default ctor gives w=1)
    float yaw = 0.0f;
    float walkPhase = 0.0f;
    float hopTimer = 0.0f;
    float fuse = -1.0f;            // >= 0 while in the explosion wind-up
    float scale = 1.0f;
    t850::RenderInstanceHandle handle;
  };

  struct Explosion {
    XVECTOR3 origin = {};
    float age = 0.0f;
    float duration = 0.4f;
    float radius = 2.6f;
  };

  // --- Terrain helpers (deterministic, thread-safe: pure functions of the key) ---
  static int Hash2D(int x, int z);
  static int Hash3D(int x, int y, int z);
  static float ValueNoise2D(int x, int z, float scale);
  static float FBM2D(int x, int z, float scale, int octaves);
  static int TerrainHeight(int x, int z);
  static bool TreeAt(int x, int z);
  t850::terrain::BlockId TerrainBlock(int x, int y, int z) const;
  void PlaceTree(t850::terrain::VoxelChunk& chunk, int baseWorldX, int baseWorldZ,
                 int baseWorldY, int trunkH) const;

  // Procedural pixel-art tile index for a block (used to lay out the atlas).
  enum class Tile : int {
    Bedrock = 0, Stone, Dirt, Grass, Sand, Cobble, Planks, Log, Leaves, Water,
    CreeperFace, CreeperSkin,
    Count
  };

  bool SweepAabb(const XVECTOR3& start,
                 const XVECTOR3& displacement,
                 const XVECTOR3& halfExtents,
                 t850::CharacterCollisionHit& hit) const;
  t850::terrain::VoxelChunkBuildResult BuildStreamedChunk(
      const t850::terrain::VoxelChunkBuildRequest& request) const;
  void UpdateStreaming();
  void CommitStreamedChunk(t850::terrain::VoxelChunkBuildResult result);
  void UnloadChunk(t850::terrain::ChunkKey key);
  t850::PhysicsBodyHandle CreateChunkPhysics(
      t850::terrain::ChunkKey key, const t850::MutableMeshSnapshot& snapshot) const;
  void RebuildChunkMeshes();
  t850::terrain::BlockId SampleNeighbor(
      const t850::terrain::ChunkKey& key, int localX, int localY, int localZ) const;

  // Builds the shared creeper box mesh (head + body) as a single unlit snapshot
  // whose UVs point at the creeper tiles in the block atlas.
  t850::MutableMeshSnapshot BuildCreeperMesh() const;

  void SpawnCreepers(int count);
  void UpdateMobs(float dt);
  void UpdateExplosions(float dt);
  void HandleExplosion(const XVECTOR3& origin, float radius);
  void ApplyPlayerDamage(float amount);
  void RespawnPlayer();
  void DrawExplosions();
  void DrawHealthHud();
  void DrawHud();

  static constexpr int kSceneIndex = 6;
  static constexpr int kAtlasColumns = 4;
  static constexpr int kAtlasRows = 4;
  static constexpr int kWaterLevel = 4;
  static constexpr float kFuseDuration = 1.5f;   // creeper wind-up before exploding
  static constexpr float kExplodeRadius = 2.6f;
  static constexpr int kMaxCreepers = 8;

  t850::terrain::BlockRegistry m_blockRegistry;
  t850::terrain::VoxelWorld m_world;
  t850::terrain::VoxelStreamingManager m_streaming;
  t850::terrain::VoxelDeltaStore m_deltas;
  std::string m_deltaPath;

  t850::terrain::BlockId m_bedrock = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_stone = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_dirt = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_grass = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_sand = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_cobble = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_planks = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_log = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_leaves = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_water = t850::terrain::kAirBlock;
  std::vector<t850::terrain::BlockId> m_hotbar;

  std::unordered_map<t850::terrain::ChunkKey, ChunkRender, t850::terrain::ChunkKeyHash> m_chunkRenders;
  t850::RenderContainer m_renderContainer;
  Camera m_camera;
  Camera m_lightCamera;
  t850::CameraController m_cameraController;
  t850::FrameDumper m_dumper;
  t850::TextRenderer m_debugText;
  t850::Texture* m_blockAtlas = nullptr;
  int m_skyCubeIndex = -1;
  GaussFilter m_shadowFilter;
  GaussFilter m_bloomFilter;
  GaussFilter m_dofFilter;
  int m_selectedSlot = 0;

  // --- Enemies / combat ---
  std::vector<Mob> m_creepers;
  std::vector<Explosion> m_explosions;
  std::unique_ptr<t850::MutableMesh> m_creeperMesh;
  int m_health = 20;
  int m_maxHealth = 20;
  float m_damageFlash = 0.0f;
  float m_creeperSpawnTimer = 3.0f;
  int m_spawnX = 8, m_spawnZ = 8, m_spawnY = 10;

  float m_deltaSeconds = 0.0f;
  bool m_remeshRequested = false;
  bool m_assetsCreated = false;
};
