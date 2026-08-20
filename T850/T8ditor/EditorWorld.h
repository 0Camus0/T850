/*********************************************************
 * T8ditor — EditorWorld: the editor's authored-scene data model.
 *
 * Consolidates the scene data that used to live as scattered
 * file-scope globals in EditorApp.cpp (objects, cameras, lights,
 * physics entities, groups, selection, undo, pending scene-load
 * state). A single instance is reachable via GetEditorWorld(),
 * so the app, panels and authoring code share one model.
 *********************************************************/

#ifndef T8DITOR_EDITOR_WORLD_H
#define T8DITOR_EDITOR_WORLD_H

#include "SceneObject.h"   // SceneObject / SceneCamera / SceneLight / SceneGroup / EditorMesh
#include "EditorScene.h"   // SceneFile, SceneObjectDesc
#include "UndoRedo.h"      // UndoStack

#include <physics/PhysicsTypes.h>
#include <physics/Q3BspCollision.h>
#include <scene/EditorSceneFile.h>   // t850::scene::SceneGameEntityDesc
#include <scene/SceneDescriptor.h>   // t850::SandboxProfileDesc
#include <utils/xMaths.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace t8ditor {

// ── Physics authoring entity (editor-side model) ─────
enum class PhysicsSceneEntityType {
  StaticTriangleMesh,
  Player,
  Character
};

enum class CharacterRuntimePath {
  Kinematic = 0,
  Jolt = 1
};

struct PhysicsSceneEntity {
  PhysicsSceneEntityType type = PhysicsSceneEntityType::StaticTriangleMesh;
  std::string name;
  std::string sourceName;
  int sourceObjectIndex = -1;
  t850::PhysicsBodyHandle body;
  t850::PhysicsCookStats stats;
  t850::PhysicsTriangleMeshCookSettings cookSettings;
  std::unique_ptr<EditorMesh> visual;
  bool visible = true;
  bool frozen = false;
  bool showWire = true;
  bool showOrientation = false;
  XVECTOR3 position = XVECTOR3(0.0f, 64.0f, 0.0f, 1.0f);
  XVECTOR3 eulerRadians = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  t850::PhysicsShapeType playerShape = t850::PhysicsShapeType::Box;
  XVECTOR3 playerHalfExtents = XVECTOR3(16.0f, 32.0f, 16.0f, 0.0f);
  float playerRadius = 16.0f;
  float playerHalfHeight = 24.0f;
  float friction = 0.6f;
  float restitution = 0.0f;
  bool sensor = false;
  float playerBotRadius = 2.0f;
  int characterRuntimePath = static_cast<int>(CharacterRuntimePath::Kinematic);
  int characterImplementation = 1; // 0=Character rigid body, 1=CharacterVirtual
  float characterMass = 70.0f;
  float characterMaxStrength = 100.0f;
  float characterMaxSlopeAngleDeg = 50.0f;
  bool characterEnhancedInternalEdgeRemoval = true;
  float characterSupportingVolumeOffset = -1.0e10f;
  float characterShapeOffset[3] = { 0.0f, 0.0f, 0.0f };
  int characterBackFaceMode = 1; // 0=IgnoreBackFaces, 1=CollideWithBackFaces
  float characterPredictiveContactDistance = 0.1f;
  int characterMaxCollisionIterations = 5;
  int characterMaxConstraintIterations = 15;
  float characterMinTimeRemaining = 1.0e-4f;
  float characterCollisionTolerance = 1.0e-3f;
  float characterPadding = 0.02f;
  int characterMaxNumHits = 256;
  float characterHitReductionCosMaxAngle = 0.999f;
  float characterPenetrationRecoverySpeed = 1.0f;
  float characterGravityFactor = 1.0f;
  bool characterAllowTranslationX = true;
  bool characterAllowTranslationY = true;
  bool characterAllowTranslationZ = true;
  bool characterInnerBody = false;
};

// ── Typed selection reference (mesh / camera / light / physics) ──
struct SelectionRef {
  int type = 0;
  int index = -1;
  bool operator<(const SelectionRef& other) const {
    return type != other.type ? type < other.type : index < other.index;
  }
};

// ── Editor world: all authored scene data for one editor session ──
struct EditorWorld {
  // Meshes
  std::vector<SceneObject> objects;
  int selectedIdx = -1;

  // Physics authoring entities
  std::vector<PhysicsSceneEntity> physicsEntities;

  // Selection (legacy mesh set + typed mixed-selection refs)
  std::set<int> multiSelect;
  std::set<SelectionRef> multiEntitySelect;

  // Groups (persistent and temporary)
  std::vector<SceneGroup> groups;
  SceneGroup tempGroup;
  int activeGroupIdx = -1;

  // Cameras and lights
  std::vector<SceneCamera> cameras;
  std::vector<SceneLight> lights;
  std::vector<t850::scene::SceneLightCameraDesc> lightCameras;
  std::vector<t850::scene::SceneCameraAnimationDesc> cameraAnimations;
  std::vector<SceneCamera> lightCameraGizmoCameras;
  t850::scene::SceneGodRaysVolumeDesc godRaysVolume;
  GizmoCache godRaysVolumeGizmo;
  std::vector<t850::scene::SceneSplineDesc> splines;
  std::vector<GizmoCache> splineGizmos;

  // Selection type: 0=mesh, 1=camera, 2=light, 3=physics entity, 4=NavMesh, 5=spline, 6=light camera, 7=spline point, 8=God Rays volume, 9=game entity, 10=game group.
  int selectionType = 0;
  int activeCameraIdx = -1;  // -1 = default editor camera, >=0 = scene camera, <=-2 = light camera (-2-index)
  int selectedSplinePoint = -1;

  // Undo/redo command stack
  UndoStack undoStack;

  // Loaded scene + deferred scene-load state
  SceneFile loadedSceneFile;
  bool hasLoadedSceneFile = false;
  std::vector<SceneObjectDesc> unloadedSceneObjects;
  std::string sceneCollisionResourcePath;
  std::vector<t850::SandboxProfileDesc> sceneProfiles;
  std::vector<t850::scene::SceneGameEntityDesc> gameEntities;
  std::vector<t850::scene::SceneGroupDesc> gameGroups;
  std::optional<t850::scene::SceneGameLogicSettingsDesc> gameLogicSettings;
  std::unique_ptr<t850::Q3BspCollisionWorld> q3CollisionWorld;
};

// Single shared editor-world instance.
EditorWorld& GetEditorWorld();

} // namespace t8ditor

#endif // T8DITOR_EDITOR_WORLD_H
