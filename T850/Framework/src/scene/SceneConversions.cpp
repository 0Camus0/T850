#include <pch.h>
#include <scene/SceneConversions.h>
#include <utils/CameraProfiles.h>
#include <filesystem>
#include <stdexcept>

#include <algorithm>
#include <cmath>

namespace t850::scene {

void ApplySceneObject(const SceneObjectDesc& desc, PrimitiveInst& instance) {
  instance.TranslateAbsolute(desc.position.x, desc.position.y, desc.position.z);
  instance.RotateXAbsolute(desc.rotation.x);
  instance.RotateYAbsolute(desc.rotation.y);
  instance.RotateZAbsolute(desc.rotation.z);
  instance.ScaleAbsolute(desc.scale.x, desc.scale.y, desc.scale.z);
  instance.SetVisible(desc.visible);
  instance.Update();
}

void BuildStreamedVoxelPalette(const SceneStreamedVoxelsDesc& desc, terrain::BlockRegistry& registry) {
  const auto fail = [] { throw std::runtime_error("Invalid streamed voxel scene data"); };
  if (desc.chunk_dimensions.x <= 0 || desc.chunk_dimensions.y <= 0 || desc.chunk_dimensions.z <= 0 ||
      desc.chunk_dimensions.x > 256 || desc.chunk_dimensions.y > 256 || desc.chunk_dimensions.z > 256 ||
      desc.atlas_width <= 0 || desc.atlas_height <= 0 || desc.atlas_width > 4096 || desc.atlas_height > 4096 ||
      desc.atlas_rgba.size() != static_cast<size_t>(desc.atlas_width) * desc.atlas_height * 4 ||
      desc.edits_path.empty() || !std::isfinite(desc.interaction_reach) || desc.interaction_reach <= 0 ||
      desc.camera_profile < 0 || desc.camera_profile >= static_cast<int>(CameraProfileType::Count) ||
      desc.terrain.surface_depth < 0 || desc.palette.empty()) fail();
  const std::filesystem::path edits(desc.edits_path);
  if (edits.is_absolute() || edits.has_root_name()) fail();
  for (const auto& part : edits) if (part == "..") fail();
  terrain::BlockRegistry built;
  for (const auto& authored : desc.palette) {
    if (authored.name.empty() || authored.name == "air" || built.Find(authored.name) ||
        !std::isfinite(authored.roughness) || authored.roughness < 0 || authored.roughness > 1) fail();
    for (const auto value : authored.color) if (!std::isfinite(value) || value < 0 || value > 1) fail();
    for (const auto value : authored.atlas_rect) if (!std::isfinite(value) || value < 0 || value > 1) fail();
    if (authored.atlas_rect[0] >= authored.atlas_rect[2] || authored.atlas_rect[1] >= authored.atlas_rect[3]) fail();
    terrain::BlockDefinition block;
    block.name = authored.name;
    block.color = XVECTOR3(authored.color[0], authored.color[1], authored.color[2], authored.color[3]);
    block.roughness = authored.roughness;
    block.usesBaseColorTexture = true;
    block.atlasU0 = authored.atlas_rect[0];
    block.atlasV0 = authored.atlas_rect[1];
    block.atlasU1 = authored.atlas_rect[2];
    block.atlasV1 = authored.atlas_rect[3];
    built.Register(std::move(block));
  }
  if (!built.Find(desc.deep_block) || !built.Find(desc.fill_block) || !built.Find(desc.surface_block)) fail();
  registry = std::move(built);
}

void ApplySceneCamera(const SceneCameraDesc& desc, Camera& camera, float aspect) {
  const XVECTOR3 eye(desc.position.x, desc.position.y, desc.position.z, 1.0f);
  const float nearPlane = (std::max)(0.0001f, desc.near_plane);
  const float farPlane = (std::max)(nearPlane + 0.01f, desc.far_plane);
  if (desc.type == 1) {
    camera.InitOrtho(eye, (std::max)(0.01f, desc.ortho_w), (std::max)(0.01f, desc.ortho_h), nearPlane, farPlane);
  } else {
    camera.InitPerspective(eye, Deg2Rad(std::clamp(desc.fov_deg, 1.0f, 179.0f)),
        (std::max)(0.01f, aspect), nearPlane, farPlane);
  }
  camera.SetLookAt(XVECTOR3(desc.target.x, desc.target.y, desc.target.z, 1.0f));
  camera.Update(0.0f);
}

navigation::NavMeshBuildSettings DefaultSceneNavMeshBuildSettings() {
  return NavMeshBuildSettingsFromScene(SceneNavMeshBuildSettingsDesc{});
}

SceneNavMeshBuildSettingsDesc NavMeshBuildSettingsToScene(const navigation::NavMeshBuildSettings& settings) {
  SceneNavMeshBuildSettingsDesc desc;
  desc.cell_size = settings.cellSize;
  desc.cell_height = settings.cellHeight;
  desc.agent_height = settings.agentHeight;
  desc.agent_radius = settings.agentRadius;
  desc.agent_max_climb = settings.agentMaxClimb;
  desc.agent_max_slope = settings.agentMaxSlope;
  desc.region_min_size = settings.regionMinSize;
  desc.region_merge_size = settings.regionMergeSize;
  desc.edge_max_len = settings.edgeMaxLen;
  desc.edge_max_error = settings.edgeMaxError;
  desc.verts_per_poly = settings.vertsPerPoly;
  desc.detail_sample_dist = settings.detailSampleDist;
  desc.detail_sample_max_error = settings.detailSampleMaxError;
  desc.query_extents = {settings.queryExtents.x, settings.queryExtents.y, settings.queryExtents.z};
  desc.auto_drop_links = settings.enableAutoDropLinks;
  desc.drop_min_height = settings.dropLinkMinHeight;
  desc.drop_max_height = settings.dropLinkMaxHeight;
  desc.drop_max_horizontal = settings.dropLinkMaxHorizontalDistance;
  desc.drop_sample_spacing = settings.dropLinkSampleSpacing;
  desc.drop_link_radius = settings.dropLinkRadius;
  desc.auto_jump_links = settings.enableAutoJumpLinks;
  desc.jump_max_horizontal = settings.jumpLinkMaxHorizontalDistance;
  desc.jump_sample_spacing = settings.jumpLinkSampleSpacing;
  desc.jump_link_radius = settings.jumpLinkRadius;
  desc.hybrid_jump_links = settings.enableHybridJumpLinks;
  desc.hybrid_max_links = settings.hybridJumpMaxLinks;
  desc.off_mesh_link_validation_key = settings.offMeshLinkValidationKey;
  return desc;
}

navigation::NavMeshBuildSettings NavMeshBuildSettingsFromScene(const SceneNavMeshBuildSettingsDesc& desc) {
  navigation::NavMeshBuildSettings settings;
  settings.cellSize = desc.cell_size;
  settings.cellHeight = desc.cell_height;
  settings.agentHeight = desc.agent_height;
  settings.agentRadius = desc.agent_radius;
  settings.agentMaxClimb = desc.agent_max_climb;
  settings.agentMaxSlope = desc.agent_max_slope;
  settings.regionMinSize = desc.region_min_size;
  settings.regionMergeSize = desc.region_merge_size;
  settings.edgeMaxLen = desc.edge_max_len;
  settings.edgeMaxError = desc.edge_max_error;
  settings.vertsPerPoly = desc.verts_per_poly;
  settings.detailSampleDist = desc.detail_sample_dist;
  settings.detailSampleMaxError = desc.detail_sample_max_error;
  settings.queryExtents = XVECTOR3(desc.query_extents.x, desc.query_extents.y, desc.query_extents.z, 0.0f);
  settings.enableAutoDropLinks = desc.auto_drop_links;
  settings.dropLinkMinHeight = desc.drop_min_height;
  settings.dropLinkMaxHeight = desc.drop_max_height;
  settings.dropLinkMaxHorizontalDistance = desc.drop_max_horizontal;
  settings.dropLinkSampleSpacing = desc.drop_sample_spacing;
  settings.dropLinkRadius = desc.drop_link_radius;
  settings.enableAutoJumpLinks = desc.auto_jump_links;
  settings.jumpLinkMaxHorizontalDistance = desc.jump_max_horizontal;
  settings.jumpLinkSampleSpacing = desc.jump_sample_spacing;
  settings.jumpLinkRadius = desc.jump_link_radius;
  settings.enableHybridJumpLinks = desc.hybrid_jump_links;
  settings.hybridJumpMaxLinks = desc.hybrid_max_links;
  settings.offMeshLinkValidationKey = desc.off_mesh_link_validation_key;
  return settings;
}

const char* NavLinkTypeName(navigation::NavTraversalType type) {
  switch (type) {
    case navigation::NavTraversalType::Drop: return "drop";
    case navigation::NavTraversalType::Jump: return "jump";
    case navigation::NavTraversalType::JumpPad: return "jump_pad";
    case navigation::NavTraversalType::JumpIntent: return "jump_intent";
    case navigation::NavTraversalType::Walk:
    default: return "walk";
  }
}

navigation::NavTraversalType NavLinkTypeFromName(const std::string& name) {
  if (name == "walk") return navigation::NavTraversalType::Walk;
  if (name == "drop") return navigation::NavTraversalType::Drop;
  if (name == "jump_pad") return navigation::NavTraversalType::JumpPad;
  if (name == "jump_intent") return navigation::NavTraversalType::JumpIntent;
  return navigation::NavTraversalType::Jump;
}

int NavAreaFromName(const std::string& name) {
  if (name == "walkable") return 0;
  if (name == "drop") return 1;
  if (name == "jump") return 2;
  if (name == "jump_pad") return 3;
  if (name == "jump_intent") return 4;
  if (name == "water") return 5;
  if (name == "door") return 6;
  if (name == "mud") return 7;
  return 8;
}

navigation::NavMeshModifierMode NavModifierModeFromName(const std::string& name) {
  if (name == "include" || name == "include_bounds" || name == "bounds")
    return navigation::NavMeshModifierMode::Include;
  if (name == "area" || name == "area_cost" || name == "cost")
    return navigation::NavMeshModifierMode::Area;
  if (name == "link_include" || name == "link_add" || name == "add_links")
    return navigation::NavMeshModifierMode::LinkInclude;
  if (name == "link_exclude" || name == "exclude_links")
    return navigation::NavMeshModifierMode::LinkExclude;
  return navigation::NavMeshModifierMode::Exclude;
}

navigation::NavOffMeshLink NavOffMeshLinkFromScene(const SceneNavMeshLinkDesc& desc) {
  navigation::NavOffMeshLink link;
  link.start = XVECTOR3(desc.start.x, desc.start.y, desc.start.z, 1.0f);
  link.end = XVECTOR3(desc.end.x, desc.end.y, desc.end.z, 1.0f);
  link.radius = (std::max)(0.05f, desc.radius);
  link.bidirectional = desc.bidirectional;
  link.type = NavLinkTypeFromName(desc.type);
  return link;
}

navigation::NavMeshVolumeModifier NavVolumeModifierFromScene(const SceneNavMeshVolumeDesc& desc) {
  navigation::NavMeshVolumeModifier modifier;
  modifier.name = desc.name;
  modifier.mode = NavModifierModeFromName(desc.type);
  modifier.position = XVECTOR3(desc.position.x, desc.position.y, desc.position.z, 1.0f);
  modifier.rotation = XVECTOR3(desc.rotation.x, desc.rotation.y, desc.rotation.z, 0.0f);
  modifier.halfExtents = XVECTOR3(
      (std::max)(0.001f, std::abs(desc.half_extents.x)),
      (std::max)(0.001f, std::abs(desc.half_extents.y)),
      (std::max)(0.001f, std::abs(desc.half_extents.z)), 0.0f);
  modifier.area = NavAreaFromName(desc.area);
  modifier.cost = (std::max)(0.01f, desc.cost);
  modifier.enabled = desc.enabled && desc.shape == "box";
  return modifier;
}

bool IsFiniteNavPoint(const Vec3f& point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool IsUsableAuthoredNavLink(const SceneNavMeshLinkDesc& link) {
  if (!link.enabled || !IsFiniteNavPoint(link.start) || !IsFiniteNavPoint(link.end) ||
      !std::isfinite(link.radius) || link.radius <= 0.0f) return false;
  const double dx = static_cast<double>(link.end.x) - link.start.x;
  const double dy = static_cast<double>(link.end.y) - link.start.y;
  const double dz = static_cast<double>(link.end.z) - link.start.z;
  return dx * dx + dy * dy + dz * dz > 0.0001;
}

std::string PhysicsBuildQualityToScene(PhysicsMeshBuildQuality quality) {
  return quality == PhysicsMeshBuildQuality::FavorBuildSpeed ? "build_speed" : "runtime_performance";
}

PhysicsMeshBuildQuality PhysicsBuildQualityFromScene(const std::string& quality) {
  return quality == "build_speed" ? PhysicsMeshBuildQuality::FavorBuildSpeed
                                  : PhysicsMeshBuildQuality::FavorRuntimePerformance;
}

ScenePhysicsCookSettingsDesc PhysicsCookSettingsToScene(const PhysicsTriangleMeshCookSettings& settings) {
  ScenePhysicsCookSettingsDesc desc;
  desc.max_triangles_per_leaf = settings.maxTrianglesPerLeaf;
  desc.build_quality = PhysicsBuildQualityToScene(settings.buildQuality);
  desc.active_edge_cos_threshold_angle = settings.activeEdgeCosThresholdAngle;
  desc.per_triangle_user_data = settings.perTriangleUserData;
  desc.use_disk_cache = settings.useDiskCache;
  return desc;
}

PhysicsTriangleMeshCookSettings PhysicsCookSettingsFromScene(const ScenePhysicsCookSettingsDesc& desc) {
  PhysicsTriangleMeshCookSettings settings;
  settings.maxTrianglesPerLeaf = desc.max_triangles_per_leaf;
  settings.buildQuality = PhysicsBuildQualityFromScene(desc.build_quality);
  settings.activeEdgeCosThresholdAngle = desc.active_edge_cos_threshold_angle;
  settings.perTriangleUserData = desc.per_triangle_user_data;
  settings.useDiskCache = desc.use_disk_cache;
  return settings;
}

}