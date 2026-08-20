#pragma once

#include <core/Core.h>
#include <debug/FrameDumper.h>
#include <physics/CharacterController.h>
#include <physics/PhysicsTypes.h>
#include <scene/MutableMesh.h>
#include <scene/RenderContainer.h>
#include <terrain/BlockRegistry.h>
#include <terrain/VoxelMesher.h>
#include <terrain/VoxelPersistence.h>
#include <terrain/VoxelStreaming.h>
#include <terrain/VoxelWorld.h>
#include <utils/CameraProfiles.h>

#include <memory>
#include <unordered_map>

class VoxelScene final : public t850::SceneBase, public t850::CharacterCollisionWorld {
public:
  VoxelScene();

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

  static constexpr int kSceneIndex = 5;
  t850::terrain::BlockRegistry m_blockRegistry;
  t850::terrain::VoxelWorld m_world;
  t850::terrain::VoxelStreamingManager m_streaming;
  t850::terrain::VoxelDeltaStore m_deltas;
  std::string m_deltaPath;
  t850::terrain::BlockId m_stone = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_dirt = t850::terrain::kAirBlock;
  t850::terrain::BlockId m_grass = t850::terrain::kAirBlock;
  std::unordered_map<t850::terrain::ChunkKey, ChunkRender, t850::terrain::ChunkKeyHash> m_chunkRenders;
  t850::RenderContainer m_renderContainer;
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
};
