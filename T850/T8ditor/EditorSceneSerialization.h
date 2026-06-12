/*********************************************************
 * T8ditor — editor scene serialization helpers.
 *
 * Pure conversions between Framework runtime types and the
 * on-disk scene descriptor types (t850::scene::*). Extracted
 * from EditorApp.cpp so the app, panels and authoring code
 * share one mapping.
 *********************************************************/

#ifndef T8DITOR_EDITOR_SCENE_SERIALIZATION_H
#define T8DITOR_EDITOR_SCENE_SERIALIZATION_H

#include <navigation/NavigationSystem.h>
#include <physics/PhysicsTypes.h>
#include <scene/EditorSceneFile.h>

#include <string>

namespace t8ditor {

// ── NavMesh build settings <-> scene ─────────────────
t850::navigation::NavMeshBuildSettings DefaultEditorNavMeshBuildSettings();
t850::scene::SceneNavMeshBuildSettingsDesc NavMeshBuildSettingsToScene(
    const t850::navigation::NavMeshBuildSettings& settings);
t850::navigation::NavMeshBuildSettings NavMeshBuildSettingsFromScene(
    const t850::scene::SceneNavMeshBuildSettingsDesc& desc);

// ── NavMesh links <-> scene ──────────────────────────
const char* NavLinkTypeName(t850::navigation::NavTraversalType type);
t850::navigation::NavTraversalType NavLinkTypeFromName(const std::string& name);
t850::navigation::NavOffMeshLink NavOffMeshLinkFromScene(const t850::scene::SceneNavMeshLinkDesc& desc);
bool IsFiniteNavPoint(const t850::scene::Vec3f& point);
bool IsUsableAuthoredNavLink(const t850::scene::SceneNavMeshLinkDesc& link);

// ── Physics triangle-mesh cook settings <-> scene ────
std::string PhysicsBuildQualityToScene(t850::PhysicsMeshBuildQuality quality);
t850::PhysicsMeshBuildQuality PhysicsBuildQualityFromScene(const std::string& quality);
t850::scene::ScenePhysicsCookSettingsDesc PhysicsCookSettingsToScene(const t850::PhysicsTriangleMeshCookSettings& settings);
t850::PhysicsTriangleMeshCookSettings PhysicsCookSettingsFromScene(const t850::scene::ScenePhysicsCookSettingsDesc& desc);

} // namespace t8ditor

#endif // T8DITOR_EDITOR_SCENE_SERIALIZATION_H
