/*********************************************************
 * T8ditor — editor scene serialization helpers. See header.
 *********************************************************/

#include "EditorSceneSerialization.h"

#include <utils/xMaths.h>

#include <algorithm>
#include <cmath>

namespace t8ditor {

t850::navigation::NavMeshBuildSettings DefaultEditorNavMeshBuildSettings() {
  t850::navigation::NavMeshBuildSettings settings;
  settings.enableAutoDropLinks = true;
  settings.enableAutoJumpLinks = true;
  settings.enableHybridJumpLinks = true;
  settings.hybridJumpMaxLinks = 192;
  return settings;
}

t850::scene::SceneNavMeshBuildSettingsDesc NavMeshBuildSettingsToScene(
    const t850::navigation::NavMeshBuildSettings& settings) {
  t850::scene::SceneNavMeshBuildSettingsDesc desc;
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

t850::navigation::NavMeshBuildSettings NavMeshBuildSettingsFromScene(
    const t850::scene::SceneNavMeshBuildSettingsDesc& desc) {
  t850::navigation::NavMeshBuildSettings settings = DefaultEditorNavMeshBuildSettings();
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

const char* NavLinkTypeName(t850::navigation::NavTraversalType type) {
  switch (type) {
    case t850::navigation::NavTraversalType::Drop: return "drop";
    case t850::navigation::NavTraversalType::Jump: return "jump";
    case t850::navigation::NavTraversalType::JumpPad: return "jump_pad";
    case t850::navigation::NavTraversalType::JumpIntent: return "jump_intent";
    case t850::navigation::NavTraversalType::Walk:
    default: return "walk";
  }
}

t850::navigation::NavTraversalType NavLinkTypeFromName(const std::string& name) {
  if (name == "drop") return t850::navigation::NavTraversalType::Drop;
  if (name == "jump_pad") return t850::navigation::NavTraversalType::JumpPad;
  if (name == "jump_intent") return t850::navigation::NavTraversalType::JumpIntent;
  if (name == "jump") return t850::navigation::NavTraversalType::Jump;
  return t850::navigation::NavTraversalType::Jump;
}

t850::navigation::NavOffMeshLink NavOffMeshLinkFromScene(const t850::scene::SceneNavMeshLinkDesc& desc) {
  t850::navigation::NavOffMeshLink link;
  link.start = XVECTOR3(desc.start.x, desc.start.y, desc.start.z, 1.0f);
  link.end = XVECTOR3(desc.end.x, desc.end.y, desc.end.z, 1.0f);
  link.radius = (std::max)(0.05f, desc.radius);
  link.bidirectional = desc.bidirectional;
  link.type = NavLinkTypeFromName(desc.type);
  return link;
}

bool IsFiniteNavPoint(const t850::scene::Vec3f& point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool IsUsableAuthoredNavLink(const t850::scene::SceneNavMeshLinkDesc& link) {
  if (!link.enabled || !IsFiniteNavPoint(link.start) || !IsFiniteNavPoint(link.end)) {
    return false;
  }
  const float dx = link.end.x - link.start.x;
  const float dy = link.end.y - link.start.y;
  const float dz = link.end.z - link.start.z;
  return dx * dx + dy * dy + dz * dz > 0.0001f && link.radius > 0.0f;
}

std::string PhysicsBuildQualityToScene(t850::PhysicsMeshBuildQuality quality) {
  return quality == t850::PhysicsMeshBuildQuality::FavorBuildSpeed ? "build_speed" : "runtime_performance";
}

t850::PhysicsMeshBuildQuality PhysicsBuildQualityFromScene(const std::string& quality) {
  return quality == "build_speed"
      ? t850::PhysicsMeshBuildQuality::FavorBuildSpeed
      : t850::PhysicsMeshBuildQuality::FavorRuntimePerformance;
}

t850::scene::ScenePhysicsCookSettingsDesc PhysicsCookSettingsToScene(const t850::PhysicsTriangleMeshCookSettings& settings) {
  t850::scene::ScenePhysicsCookSettingsDesc desc;
  desc.max_triangles_per_leaf = settings.maxTrianglesPerLeaf;
  desc.build_quality = PhysicsBuildQualityToScene(settings.buildQuality);
  desc.active_edge_cos_threshold_angle = settings.activeEdgeCosThresholdAngle;
  desc.per_triangle_user_data = settings.perTriangleUserData;
  desc.use_disk_cache = settings.useDiskCache;
  return desc;
}

t850::PhysicsTriangleMeshCookSettings PhysicsCookSettingsFromScene(const t850::scene::ScenePhysicsCookSettingsDesc& desc) {
  t850::PhysicsTriangleMeshCookSettings settings;
  settings.maxTrianglesPerLeaf = desc.max_triangles_per_leaf;
  settings.buildQuality = PhysicsBuildQualityFromScene(desc.build_quality);
  settings.activeEdgeCosThresholdAngle = desc.active_edge_cos_threshold_angle;
  settings.perTriangleUserData = desc.per_triangle_user_data;
  settings.useDiskCache = desc.use_disk_cache;
  return settings;
}

} // namespace t8ditor
