#pragma once

#include <terrain/HeightmapTerrain.h>
#include <scene/MutableMesh.h>
#include <physics/JoltPhysicsSystem.h>

namespace t850::game { class GameNavigationService; }
namespace t850::navigation { class NavMesh; }

namespace t850 {

class RenderSkinnedMesh;

bool BuildTerrainLod(const scene::SceneHeightmapDesc& desc, uint32_t level,
    MutableMeshSnapshot& output, std::string* error = nullptr);
uint32_t SelectTerrainLod(float distance, float threshold, uint32_t count);

class HeightmapMesh final : public RenderMesh {
public:
  ~HeightmapMesh() override { Destroy(); }
  bool ReplaceTerrain(const scene::SceneHeightmapDesc& desc, std::string* error = nullptr);
  void Draw(float* world, float* viewProjection) override;
  void Destroy() override;
  uint32_t ActiveLod() const { return m_activeLod; }
  void SetEditing(bool editing) { m_editing = editing; }
  const scene::SceneHeightmapDesc& AuthoredTerrain() const { return m_authoredTerrain; }
  const std::vector<t850::AABB>& PlacementObstacles() const { return m_placementObstacles; }
  void UpdatePlacementAnimations(float deltaSeconds);
  void UploadPlacementBones();
  std::vector<std::string> PlacementAnimationNames(std::string_view id) const;
  RenderSkinnedMesh* PlacementSkinnedMesh(std::string_view id) const;
  RenderMesh* PlacementMesh(std::string_view id) const;
  bool PlacementVisualTransform(std::string_view id, XMATRIX44& transform) const;

private:
  struct PlacementModel;
  struct PlacementInstance {
    std::string id;
    std::shared_ptr<PlacementModel> model;
    XMATRIX44 localTransform;
  };
  std::vector<PlacementInstance> m_placementVisuals;
  scene::SceneHeightmapDesc m_authoredTerrain;
  std::vector<t850::AABB> m_placementObstacles;
  std::unique_ptr<xF::XDataBase> m_database;
  std::vector<std::unique_ptr<MutableMesh>> m_lods;
  float m_lodDistance = 150.0f;
  uint32_t m_activeLod = 0;
  bool m_editing = false;
};

struct TerrainCollisionBinding {
  PhysicsBodyHandle body;
  PhysicsTriangleMeshCookSettings settings;
  XMATRIX44 world;
  uint32_t entityId = 0;
  float friction = 0.6f;
  float restitution = 0.0f;
  bool sensor = false;
};

bool CommitTerrainRevision(HeightmapMesh& mesh, const scene::SceneHeightmapDesc& desc,
    JoltPhysicsSystem& physics, std::span<TerrainCollisionBinding> collision,
    navigation::NavMesh* navigation = nullptr, game::GameNavigationService* queries = nullptr,
    std::string* error = nullptr);

}