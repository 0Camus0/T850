#pragma once

#include <navigation/NavigationSystem.h>
#include <physics/PhysicsTypes.h>
#include <scene/EditorSceneFile.h>
#include <utils/Camera.h>

namespace t850::scene {

void ApplySceneCamera(const SceneCameraDesc& desc, Camera& camera, float aspect);

navigation::NavMeshBuildSettings DefaultSceneNavMeshBuildSettings();
SceneNavMeshBuildSettingsDesc NavMeshBuildSettingsToScene(const navigation::NavMeshBuildSettings& settings);
navigation::NavMeshBuildSettings NavMeshBuildSettingsFromScene(const SceneNavMeshBuildSettingsDesc& desc);
const char* NavLinkTypeName(navigation::NavTraversalType type);
navigation::NavTraversalType NavLinkTypeFromName(const std::string& name);
int NavAreaFromName(const std::string& name);
navigation::NavMeshModifierMode NavModifierModeFromName(const std::string& name);
navigation::NavOffMeshLink NavOffMeshLinkFromScene(const SceneNavMeshLinkDesc& desc);
navigation::NavMeshVolumeModifier NavVolumeModifierFromScene(const SceneNavMeshVolumeDesc& desc);
bool IsFiniteNavPoint(const Vec3f& point);
bool IsUsableAuthoredNavLink(const SceneNavMeshLinkDesc& link);
std::string PhysicsBuildQualityToScene(PhysicsMeshBuildQuality quality);
PhysicsMeshBuildQuality PhysicsBuildQualityFromScene(const std::string& quality);
ScenePhysicsCookSettingsDesc PhysicsCookSettingsToScene(const PhysicsTriangleMeshCookSettings& settings);
PhysicsTriangleMeshCookSettings PhysicsCookSettingsFromScene(const ScenePhysicsCookSettingsDesc& desc);

}