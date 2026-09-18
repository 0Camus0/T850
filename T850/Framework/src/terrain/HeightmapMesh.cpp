#include <pch.h>
#include <debug/RuntimeTelemetry.h>
#include <terrain/HeightmapMesh.h>
#include <terrain/TerrainPlacement.h>
#include <debug/RuntimeTelemetry.h>
#include <physics/PhysicsAuthoring.h>
#include <game/GameNavigationService.h>
#include <scene/PrimitiveManager.h>
#include <core/EngineContext.h>
#include <video/BaseDriver.h>
#include <scene/RenderSkinnedMesh.h>
#include <utils/ResourceLocator.h>
#include <utils/Log.h>

#include <algorithm>
#include <cmath>

namespace t850 {

struct HeightmapMesh::PlacementModel {
  PrimitiveManager primitives;
  RenderMesh* mesh = nullptr;
  scene::ScenePlacementVisualDesc descriptor;
  std::vector<std::string> animations;
  t850::AABB referenceBounds;
  ~PlacementModel() { primitives.DestroyPrimitives(); }
};

uint32_t SelectTerrainLod(float distance, float threshold, uint32_t count) {
  if (!count || !std::isfinite(distance) || !std::isfinite(threshold) || threshold <= 0) return 0;
  uint32_t level = 0;
  while (level + 1 < count && distance >= threshold) {
    ++level;
    threshold *= 2.0f;
  }
  return level;
}

bool BuildTerrainLod(const scene::SceneHeightmapDesc& desc, uint32_t level,
    MutableMeshSnapshot& output, std::string* error) {
  auto source = desc;
  if (!InitializeTerrainEditing(source, error)) return false;
  if (level == 0) return LoadHeightmapTerrain(source, output, error);
  level = (std::min)(level, 4u);
  auto reduced = source;
  reduced.placements.clear();
  reduced.samples_x = (std::max)(2u, ((source.samples_x - 1) >> level) + 1);
  reduced.samples_z = (std::max)(2u, ((source.samples_z - 1) >> level) + 1);
  reduced.elevations.clear();
  reduced.cell_materials.clear();
  for (uint32_t row = 0; row < reduced.samples_z; ++row) {
    for (uint32_t column = 0; column < reduced.samples_x; ++column) {
      const float sourceX = static_cast<float>(column) * (source.samples_x - 1) / (reduced.samples_x - 1);
      const float sourceZ = static_cast<float>(row) * (source.samples_z - 1) / (reduced.samples_z - 1);
      const uint32_t left = static_cast<uint32_t>(sourceX);
      const uint32_t top = static_cast<uint32_t>(sourceZ);
      const uint32_t right = (std::min)(left + 1, source.samples_x - 1);
      const uint32_t bottom = (std::min)(top + 1, source.samples_z - 1);
      reduced.elevations.push_back(std::lerp(
          std::lerp(source.elevations[top * source.samples_x + left], source.elevations[top * source.samples_x + right], sourceX - left),
          std::lerp(source.elevations[bottom * source.samples_x + left], source.elevations[bottom * source.samples_x + right], sourceX - left), sourceZ - top));
    }
  }
  if (!source.cell_materials.empty()) {
    for (uint32_t row = 0; row + 1 < reduced.samples_z; ++row)
      for (uint32_t column = 0; column + 1 < reduced.samples_x; ++column) {
        const uint32_t sourceX = static_cast<uint32_t>((column + 0.5f) * (source.samples_x - 1) / (reduced.samples_x - 1));
        const uint32_t sourceZ = static_cast<uint32_t>((row + 0.5f) * (source.samples_z - 1) / (reduced.samples_z - 1));
        reduced.cell_materials.push_back(source.cell_materials[sourceZ * (source.samples_x - 1) + sourceX]);
      }
  }
  MutableMeshSnapshot mesh;
  if (!LoadHeightmapTerrain(reduced, mesh, error) || !AppendTerrainBlockouts(source, mesh, error)) return false;
  output = std::move(mesh);
  return true;
}

bool HeightmapMesh::ReplaceTerrain(const scene::SceneHeightmapDesc& desc, std::string* error) {
  auto authored = desc;
  if (!InitializeTerrainEditing(authored, error)) return false;
  MutableMeshSnapshot full;
  if (!LoadHeightmapTerrain(authored, full, error)) return false;
  auto database = BuildMeshDatabase(full, error);
  if (!database) return false;
  std::vector<PlacementInstance> visuals;
  for (const auto& placement : authored.placements) {
    if (!placement.visual || placement.visual->mesh.empty()) continue;
    const auto& visual = *placement.visual;
    if (!ResourceLocator::Instance().Exists(visual.mesh)) {
      if (error) *error = "Placement visual model is missing: " + visual.mesh;
      return false;
    }
    std::shared_ptr<PlacementModel> model;
    for (const auto& existing : m_placementVisuals) {
      const auto& previous = existing.model->descriptor;
        if (existing.id == placement.id && previous.mesh == visual.mesh && previous.animation == visual.animation &&
          previous.hidden_geometry == visual.hidden_geometry) {
        model = existing.model;
        break;
      }
    }
    if (!model) {
      model = std::make_shared<PlacementModel>();
      model->descriptor = visual;
      model->primitives.SetEngineContext(GetEngineContext());
      const int index = model->primitives.CreateMesh(visual.mesh.c_str());
      model->mesh = index < 0 ? nullptr : dynamic_cast<RenderMesh*>(model->primitives.GetPrimitive(index));
      if (!model->mesh || model->mesh->Info.empty()) {
        if (error) *error = "Placement visual could not create render geometry: " + visual.mesh;
        return false;
      }
      for (const auto index : visual.hidden_geometry) {
        if (index >= model->mesh->Info.size()) {
          if (error) *error = "Placement visual hidden part is outside the model geometry range";
          return false;
        }
        model->mesh->Info[index].visible = false;
      }
      if (auto* skinned = dynamic_cast<RenderSkinnedMesh*>(model->mesh)) {
        if (skinned->xFile && !skinned->xFile->XMeshDataBase.empty()) {
          for (const auto& clip : skinned->xFile->XMeshDataBase[0]->Animation.Animations) model->animations.push_back(clip.Name);
        }
        if (visual.animation.empty()) {
          skinned->GetAnimController().ApplyBindPose();
        } else {
          const auto clip = std::find(model->animations.begin(), model->animations.end(), visual.animation);
          if (clip == model->animations.end()) {
            if (error) *error = "Placement animation not found: " + visual.animation;
            return false;
          }
          const int selected = static_cast<int>(clip - model->animations.begin());
          while (skinned->GetCurrentAnimSet() != selected) skinned->NextAnimation();
          skinned->ResetAnimation();
          skinned->GetAnimController().Update(0.0f);
        }
        skinned->SetAnimSpeed(visual.animation_speed);
        skinned->SetLooping(visual.loop);
        skinned->GetAnimController().SetUseSlerp(true);
        RenderMesh::AABB posed;
        if (skinned->GetCurrentPoseLocalAABB(posed)) model->referenceBounds = t850::AABB(posed.min, posed.max);
        T8_LOG_INFO("[PlacementVisual] Loaded '%s': bones=%d clips=%d preview='%s'", visual.mesh.c_str(),
            skinned->GetNumBones(), skinned->GetNumAnimSets(), visual.animation.empty() ? "Bind Pose" : visual.animation.c_str());
      } else if (!visual.animation.empty()) {
        if (error) *error = "An animation was assigned to a static placement model";
        return false;
      }
      if (!model->referenceBounds.IsValid()) {
        for (const auto& geometry : model->mesh->Info) {
          if (!geometry.visible) continue;
          model->referenceBounds.ExpandToInclude(geometry.bounds.min);
          model->referenceBounds.ExpandToInclude(geometry.bounds.max);
        }
      }
    }
    PlacementInstance instance;
    instance.id = placement.id;
    instance.model = model;
    if (!FitPlacementVisual(authored, placement, model->referenceBounds, instance.localTransform, error)) return false;
    visuals.push_back(std::move(instance));
  }
  std::vector<std::unique_ptr<MutableMesh>> lods;
  for (uint32_t level = 0; level < desc.lod_levels; ++level) {
    MutableMeshSnapshot snapshot;
    if (level == 0) snapshot = full;
    else if (!BuildTerrainLod(authored, level, snapshot, error)) return false;
    const size_t terrainMaterials = (std::max)(size_t{1}, authored.materials.size());
    std::erase_if(snapshot.sections, [&](const MutableMeshSection& section) {
      if (section.materialIndex < terrainMaterials) return false;
      const size_t placementIndex = section.materialIndex - terrainMaterials;
      return placementIndex < authored.placements.size() && authored.placements[placementIndex].visual &&
          !authored.placements[placementIndex].visual->mesh.empty();
    });
    auto mesh = std::make_unique<MutableMesh>();
    mesh->SetEngineContext(GetEngineContext());
    if (!mesh->ReplaceSnapshot(std::move(snapshot), error, false)) return false;
    lods.push_back(std::move(mesh));
  }
  const bool retiringVisual = std::any_of(m_placementVisuals.begin(), m_placementVisuals.end(), [&](const auto& previous) {
    return std::none_of(visuals.begin(), visuals.end(), [&](const auto& next) { return previous.model == next.model; });
  });
  if (retiringVisual && GetEngineContext() && GetEngineContext()->driver) GetEngineContext()->driver->WaitForGPU();
  m_lods = std::move(lods);
  m_placementVisuals = std::move(visuals);
  for (auto& visual : m_placementVisuals) {
    const auto placement = std::find_if(authored.placements.begin(), authored.placements.end(),
        [&](const auto& candidate) { return candidate.id == visual.id; });
    visual.model->descriptor = *placement->visual;
    if (auto* skinned = dynamic_cast<RenderSkinnedMesh*>(visual.model->mesh)) {
      skinned->SetAnimSpeed(placement->visual->animation_speed);
      skinned->SetLooping(placement->visual->loop);
    }
  }
  m_placementObstacles.clear();
  const float minimumHeight = *std::min_element(authored.elevations.begin(), authored.elevations.end());
  for (const auto& placement : authored.placements) {
    const auto support = CheckTerrainPlacement(authored, placement, placement.id);
    const float cell = authored.placement_grid.cell_size;
    m_placementObstacles.emplace_back(
        XVECTOR3(placement.cell_x * cell, minimumHeight - 1.0f, placement.cell_z * cell, 1.0f),
        XVECTOR3((placement.cell_x + placement.width) * cell, support.baseHeight + placement.height + 1.0f,
            (placement.cell_z + placement.depth) * cell, 1.0f));
  }
  m_authoredTerrain = std::move(authored);
  m_database = std::move(database);
  xFile = m_database.get();
  m_sourcePath = xFile->m_name;
  Info.clear();
  Info.emplace_back();
  Info[0].VertexSize = sizeof(MutableMeshVertex);
  Info[0].NumVertex = static_cast<uint32_t>(full.vertices.size());
  Info[0].bounds.min = full.localBounds.vMin;
  Info[0].bounds.max = full.localBounds.vMax;
  m_lodDistance = desc.lod_distance;
  m_activeLod = 0;
  return true;
}

void HeightmapMesh::Draw(float* world, float* viewProjection) {
  if (m_lods.empty() || !pScProp) return;
  const Camera* camera = pScProp->GetPrimaryCamera();
  float distance = 0.0f;
  if (camera && world) {
    XMATRIX44 matrix;
    std::copy_n(world, 16, &matrix.m[0][0]);
    const auto center = t850::TransformPoint(m_lods.front()->LocalBounds().Center(), matrix);
    distance = std::hypot(std::hypot(center.x - camera->Eye.x, center.z - camera->Eye.z), center.y - camera->Eye.y);
  }
  m_activeLod = m_editing ? 0 : SelectTerrainLod(distance, m_lodDistance, static_cast<uint32_t>(m_lods.size()));
  auto& mesh = *m_lods[m_activeLod];
  mesh.CopyRenderStateFrom(*this);
  mesh.Draw(world, viewProjection);
  XMATRIX44 terrainWorld;
  terrainWorld.Identity();
  if (world) std::copy_n(world, 16, &terrainWorld.m[0][0]);
  for (const auto& visual : m_placementVisuals) {
    auto transform = visual.localTransform * terrainWorld;
    auto& model = *visual.model->mesh;
    model.CopyRenderStateFrom(*this);
    model.Draw(&transform.m[0][0], viewProjection);
  }
  T8_TELEMETRY_SET("terrain.render_lod", m_activeLod);
  T8_TELEMETRY_ADD("terrain.render_triangles", static_cast<double>(mesh.IndexCount() / 3));
}

void HeightmapMesh::UpdatePlacementAnimations(float deltaSeconds) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds < 0) return;
  for (const auto& visual : m_placementVisuals) {
    const auto& descriptor = visual.model->descriptor;
    if (!descriptor.animate || descriptor.animation.empty()) continue;
    if (auto* skinned = dynamic_cast<RenderSkinnedMesh*>(visual.model->mesh)) skinned->GetAnimController().Update(deltaSeconds);
  }
}

void HeightmapMesh::UploadPlacementBones() {
  for (const auto& visual : m_placementVisuals)
    if (auto* skinned = dynamic_cast<RenderSkinnedMesh*>(visual.model->mesh)) skinned->UploadBoneTexture();
}

std::vector<std::string> HeightmapMesh::PlacementAnimationNames(std::string_view id) const {
  for (const auto& visual : m_placementVisuals) if (visual.id == id) return visual.model->animations;
  return {};
}

RenderSkinnedMesh* HeightmapMesh::PlacementSkinnedMesh(std::string_view id) const {
  for (const auto& visual : m_placementVisuals) if (visual.id == id) return dynamic_cast<RenderSkinnedMesh*>(visual.model->mesh);
  return nullptr;
}

RenderMesh* HeightmapMesh::PlacementMesh(std::string_view id) const {
  for (const auto& visual : m_placementVisuals) if (visual.id == id) return visual.model->mesh;
  return nullptr;
}

bool HeightmapMesh::PlacementVisualTransform(std::string_view id, XMATRIX44& transform) const {
  for (const auto& visual : m_placementVisuals) if (visual.id == id) { transform = visual.localTransform; return true; }
  return false;
}

bool CommitTerrainRevision(HeightmapMesh& mesh, const scene::SceneHeightmapDesc& desc,
    JoltPhysicsSystem& physics, std::span<TerrainCollisionBinding> collision,
    navigation::NavMesh* navigation, game::GameNavigationService* queries, std::string* error) {
  T8_TELEMETRY_SCOPE("terrain.heightmap.commit");
  T8_UPLOAD_SOURCE(RuntimeTelemetry::UploadSource::Streaming);
  MutableMeshSnapshot snapshot;
  if (!LoadHeightmapTerrain(desc, snapshot, error)) return false;
  auto database = BuildMeshDatabase(snapshot, error);
  if (!database) return false;
  RenderMesh source;
  source.xFile = database.get();
  source.m_sourcePath = database->m_name;
  std::vector<PhysicsBodyHandle> replacements;
  auto discard = [&]() { for (const auto& body : replacements) physics.DestroyBody(body); };
  for (const auto& binding : collision) {
    PhysicsTriangleMeshBodyDesc body;
    auto settings = binding.settings;
    settings.useDiskCache = false;
    if (!BuildStaticTriangleMeshBodyDesc(source, binding.world, binding.entityId, settings, body)) {
      discard();
      if (error) *error = "Cannot prepare terrain collision";
      return false;
    }
    body.friction = binding.friction;
    body.restitution = binding.restitution;
    body.sensor = binding.sensor;
    const auto replacement = physics.CreateTriangleMeshBody(body);
    if (!replacement.IsValid()) {
      discard();
      if (error) *error = "Cannot cook terrain collision";
      return false;
    }
    replacements.push_back(replacement);
  }
  if (queries) queries->PrepareForNavMeshMutation();
  if (!mesh.ReplaceTerrain(desc, error)) {
    discard();
    return false;
  }
  for (size_t index = 0; index < collision.size(); ++index) {
    physics.DestroyBody(collision[index].body);
    collision[index].body = replacements[index];
  }
  if (navigation) navigation->Clear();
  return true;
}

void HeightmapMesh::Destroy() {
  if (!m_placementVisuals.empty() && GetEngineContext() && GetEngineContext()->driver) GetEngineContext()->driver->WaitForGPU();
  m_placementVisuals.clear();
  m_placementObstacles.clear();
  m_authoredTerrain = scene::SceneHeightmapDesc{};
  m_lods.clear();
  xFile = nullptr;
  m_database.reset();
  Info.clear();
}

}