/*********************************************************
 * T8ditor — editor scene serialization helpers.
 *
 * Compatibility facade for Framework-owned scene conversions.
 * Runtime and editor callers share the same implementation.
 *********************************************************/

#ifndef T8DITOR_EDITOR_SCENE_SERIALIZATION_H
#define T8DITOR_EDITOR_SCENE_SERIALIZATION_H

#include <scene/SceneConversions.h>

namespace t8ditor {

// ── NavMesh build settings <-> scene ─────────────────
inline t850::navigation::NavMeshBuildSettings DefaultEditorNavMeshBuildSettings() {
    return t850::scene::DefaultSceneNavMeshBuildSettings();
}
using t850::scene::NavMeshBuildSettingsToScene;
using t850::scene::NavMeshBuildSettingsFromScene;

// ── NavMesh links <-> scene ──────────────────────────
using t850::scene::NavLinkTypeName;
using t850::scene::NavLinkTypeFromName;
using t850::scene::NavOffMeshLinkFromScene;
using t850::scene::NavVolumeModifierFromScene;
using t850::scene::IsFiniteNavPoint;
using t850::scene::IsUsableAuthoredNavLink;

// ── Physics triangle-mesh cook settings <-> scene ────
using t850::scene::PhysicsBuildQualityToScene;
using t850::scene::PhysicsBuildQualityFromScene;
using t850::scene::PhysicsCookSettingsToScene;
using t850::scene::PhysicsCookSettingsFromScene;

} // namespace t8ditor

#endif // T8DITOR_EDITOR_SCENE_SERIALIZATION_H
