#pragma once

#include <core/Core.h>
#include <debug/FrameDumper.h>
#include <physics/CharacterController.h>
#include <physics/PhysicsTypes.h>
#include <scene/IBLResources.h>
#include <scene/MutableMesh.h>
#include <scene/RenderContainer.h>
#include <scene/SceneSetup.h>
#include <terrain/BlockRegistry.h>
#include <terrain/VoxelMesher.h>
#include <terrain/VoxelPersistence.h>
#include <terrain/VoxelStreaming.h>
#include <terrain/VoxelWorld.h>
#include <utils/CameraProfiles.h>

#include <array>
#include <memory>
#include <unordered_map>

// A Minecraft-like scene built on the shared voxel terrain infrastructure.
// Reuses the streaming, greedy meshing, persistence, and character-collision
// systems from VoxelScene but adds a richer block set with per-face textures,
// noise-based terrain with hills/water/trees/bedrock, and a selectable hotbar
// for placing different block types.
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
  void DrawDevGui(t850::DevGuiContext& gui) override;
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
  void RegisterBlocks();
  void BuildAtlas();
  void BuildSkyCubemap();
  void PlaceTree(int worldX, int worldY, int worldZ, t850::terrain::VoxelChunk& chunk) const;
  int TerrainHeight(int worldX, int worldZ) const;
  void SelectHotbarBlock(int index);
  void SyncLightCameraFromDirectionalLight();
  void CreateSword();
  void CreateEnemies();
  void UpdateSword(float deltaSeconds);
  void UpdateEnemies(float deltaSeconds);
  void DestroyDynamicMeshes();
  void ApplySkySelection(int index);

  static constexpr int kSceneIndex = 6;
  static constexpr int kHotbarSize = 9;

  struct Enemy {
    XVECTOR3 position;
    float yaw = 0.0f;
    std::unique_ptr<t850::MutableMesh> mesh;
    t850::RenderInstanceHandle instance;
  };

  t850::terrain::BlockRegistry m_blockRegistry;
  t850::terrain::VoxelWorld m_world;
  t850::terrain::VoxelStreamingManager m_streaming;
  t850::terrain::VoxelDeltaStore m_deltas;
  std::string m_deltaPath;
  t850::terrain::BlockId m_grass = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_dirt = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_stone = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_cobble = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_sand = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_water = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_bedrock = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_log = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_leaves = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_planks = t850::terrain::kAirBlock;
  std::array<t850::terrain::BlockId, kHotbarSize> m_hotbar{};
  int m_selectedHotbar = 0;
  std::unordered_map<t850::terrain::ChunkKey, ChunkRender, t850::terrain::ChunkKeyHash> m_chunkRenders;
  t850::RenderContainer m_renderContainer;
  t850::EnvironmentMapSet m_envMaps;
  int m_envMapTexIndex = -1;
  int m_diffuseIBLTexIndex = -1;
  int m_specularIBLTexIndex = -1;
  int m_brdfLUTTexIndex = -1;
  int m_sheenIBLTexIndex = -1;
  int m_charlieLUTTexIndex = -1;
  int m_sheenELUTTexIndex = -1;
  Camera m_camera;
  Camera m_lightCamera;
  t850::CameraController m_cameraController;
  t850::FrameDumper m_dumper;
  t850::Texture* m_blockAtlas = nullptr;
  GaussFilter m_shadowFilter;
  GaussFilter m_bloomFilter;
  GaussFilter m_dofFilter;
  float m_deltaSeconds = 0.0f;
  bool m_remeshRequested = false;
  bool m_assetsCreated = false;

  // First-person sword.
  std::unique_ptr<t850::MutableMesh> m_swordMesh;
  t850::RenderInstanceHandle m_swordInstance;
  float m_swingTime = -1.0f;  // -1 = not swinging, else seconds since swing start
  bool m_swordCreated = false;

  // Enemies that follow the player.
  std::vector<Enemy> m_enemies;
  bool m_enemiesCreated = false;

  // Debug render target selector (0 = final output, >0 = a specific pass).
  int m_debugRTSelection = 0;
  // Active gauss kernel for the "Gauss" section (0=shadow, 1=bloom, 2=dof).
  int m_activeGaussKernel = 0;
  // Sky cubemap selection (0 = procedural Minecraft blue, >0 = a .dds file).
  int m_skySelection = 0;
  // Descriptor-driven scene controls (loaded from MinecraftScene.json).
  t850::SceneSetup m_controlSetup;
};
