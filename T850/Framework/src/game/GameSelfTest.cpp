#include <pch.h>

#include <game/GameSelfTest.h>

#include <game/GameIds.h>
#include <game/Controller.h>
#include <game/GameLogicSystem.h>
#include <game/GameNavigationService.h>
#include <game/GameObjectRegistry.h>
#include <game/GamePhysicsService.h>
#include <game/StateMachine.h>
#include <game/GameValidation.h>
#include <physics/JoltPhysicsSystem.h>
#include <physics/PhysicsAuthoring.h>
#include <scene/RenderMesh.h>
#include <scene/EditorSceneFile.h>
#include <scene/SceneConversions.h>
#include <scene/SceneRegions.h>
#include <scene/MutableMeshData.h>
#include <scene/RenderContainer.h>
#include <scene/RenderGraph.h>
#include <scene/RenderQuad.h>
#include <scene/MaterialAsset.h>
#include <terrain/BlockRegistry.h>
#include <terrain/HeightmapTerrain.h>
#include <terrain/TerrainPlacement.h>
#include <terrain/HeightmapMesh.h>
#include <terrain/VoxelChunk.h>
#include <terrain/VoxelMesher.h>
#include <terrain/VoxelWorld.h>
#include <terrain/VoxelStreaming.h>
#include <terrain/VoxelPersistence.h>
#include <terrain/VoxelNavigation.h>
#include <terrain/VoxelCollision.h>
#include <utils/ThreadPool.h>
#include <utils/TextureMipmaps.h>
#include <utils/ConfigRuntime.h>
#include <utils/ShaderPrecompiler.h>
#include <utils/ShaderPermutationDump.h>
#include <utils/ResourceLocator.h>
#include <scene/SceneSetup.h>
#include <core/Core.h>
#include <utils/XDataBase.h>
#include <video/TextureAtlas.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace t850::game {
namespace {

using TestFunction = void (*)();

struct TestCase {
  const char* id;
  TestFunction function;
};

std::vector<std::string>* gLifecycleLog = nullptr;
bool gRemoveDuringUpdate = false;
RuntimeGameObjectId gObservedOwnerId = kInvalidRuntimeGameObjectId;

class LifecycleTestComponent final : public Component {
public:
  std::string_view Type() const override { return "test_lifecycle"; }

  void OnAttach(GameObject& owner, GameLogicSystem& system) override {
    Component::OnAttach(owner, system);
    Log("attach");
  }

  void OnCreate() override { Log("create"); }

  void Update(float fixedDt) override {
    (void)fixedDt;
    Log("update");
    if (gRemoveDuringUpdate && owner_ && system_) {
      Log("during:" + std::to_string(owner_->components.size()));
      gRemoveDuringUpdate = false;
      system_->RequestRemoveComponent(owner_->runtimeId, Id());
    }
  }

  void OnDestroy() override { Log("destroy"); }

  void OnDetach() override {
    Log("detach");
    Component::OnDetach();
  }

private:
  static void Log(std::string value) {
    if (gLifecycleLog) gLifecycleLog->push_back(std::move(value));
  }
};

class OwnerStabilityTestComponent final : public Component {
public:
  std::string_view Type() const override { return "test_owner_stability"; }
  void Update(float fixedDt) override {
    (void)fixedDt;
    gObservedOwnerId = owner_ ? owner_->runtimeId : kInvalidRuntimeGameObjectId;
  }
};

class NullTestPrimitive final : public PrimitiveBase {
public:
  void Load(const char*) override {}
  void Create() override {}
  void Transform(float*) override {}
  void Draw(float*, float*) override {}
  void Destroy() override {}
};

std::unique_ptr<Component> CreateLifecycleTestComponent(
    const scene::SceneComponentDesc& descriptor, ComponentLoadContext& context) {
  (void)descriptor;
  (void)context;
  return std::make_unique<LifecycleTestComponent>();
}

std::unique_ptr<Component> CreateOwnerStabilityTestComponent(
    const scene::SceneComponentDesc& descriptor, ComponentLoadContext& context) {
  (void)descriptor;
  (void)context;
  return std::make_unique<OwnerStabilityTestComponent>();
}

class TempSceneFiles {
public:
  std::filesystem::path Add(std::string_view suffix) {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        (MakeStableId("t850_game_test_") + std::string(suffix));
    paths_.push_back(path);
    return path;
  }

  ~TempSceneFiles() {
    for (const std::filesystem::path& path : paths_) {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  }

private:
  std::vector<std::filesystem::path> paths_;
};

void Require(bool condition, std::string message) {
  if (!condition) throw std::runtime_error(std::move(message));
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  Require(stream.is_open(), "cannot read temporary scene file: " + path.string());
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool HasIssue(const scene::SceneValidationReport& report,
              std::string_view code,
              scene::SceneValidationSeverity severity) {
  for (const scene::SceneValidationIssue& issue : report.issues) {
    if (issue.code == code && issue.severity == severity) return true;
  }
  return false;
}

scene::SceneGameEntityDesc MakeValidEntity(std::string id = "ge_entity") {
  scene::SceneGameEntityDesc entity;
  entity.id = std::move(id);
  entity.name = "Test Entity";
  entity.kind = "pawn";
  return entity;
}

void TestSchemaRoundTrip() {
  TempSceneFiles files;
  const std::filesystem::path firstPath = files.Add("_first.t8scene");
  const std::filesystem::path secondPath = files.Add("_second.t8scene");

  scene::EditorSceneFile source;
  source.version = scene::kSceneSchemaV2_GameLogic;
  source.game_logic_settings = scene::SceneGameLogicSettingsDesc{};

  scene::SceneGameEntityDesc entity = MakeValidEntity("ge_roundtrip");
  entity.team = 3;
  entity.control.mode = "player";
  entity.control.controller = "fps_default";
  entity.control.player_slot = 1;
  entity.group_id = "grp_alpha";

  scene::SceneComponentDesc component;
  component.id = "comp_health";
  component.type = "health";
  component.params["maxHp"] = "100";
  component.config_json = R"({"regen":1.5})";
  entity.components.push_back(component);

  scene::SceneStateMachineDesc behavior;
  behavior.initial_state = "idle";
  behavior.states.push_back(scene::SceneStateDesc{.name = "idle"});
  behavior.states.push_back(scene::SceneStateDesc{.name = "move"});
  behavior.transitions.push_back(scene::SceneTransitionDesc{
      .from_state = "idle", .to_state = "move", .condition = "on_event:move"});
  entity.behavior = behavior;
  source.game_entities.push_back(entity);

  scene::SceneGroupDesc group;
  group.id = "grp_alpha";
  group.name = "Alpha";
  group.member_entity_ids.push_back(entity.id);
  group.formation.leader_entity_id = entity.id;
  source.game_groups.push_back(group);

  std::string error;
  Require(scene::SaveEditorSceneFile(source, firstPath.string(), &error), "first save failed: " + error);

  scene::EditorSceneFile loaded;
  Require(scene::LoadEditorSceneFile(firstPath.string(), loaded, &error), "load failed: " + error);
  Require(scene::SaveEditorSceneFile(loaded, secondPath.string(), &error), "second save failed: " + error);
  Require(ReadFile(firstPath) == ReadFile(secondPath), "scene round-trip was not byte-stable");
  Require(loaded.game_entities.size() == 1, "round-trip lost game entity");
  Require(loaded.game_entities[0].components.size() == 1, "round-trip lost component");
  Require(loaded.game_entities[0].behavior.has_value(), "round-trip lost behavior");
}

void TestMigrationIdsPersist() {
  TempSceneFiles files;
  const std::filesystem::path path = files.Add("_migration.t8scene");

  scene::EditorSceneFile source;
  source.version = scene::kSceneSchemaV1;
  scene::SceneGameEntityDesc entity;
  entity.name = "Legacy";
  scene::SceneComponentDesc component;
  component.type = "health";
  entity.components.push_back(component);
  source.game_entities.push_back(entity);

  std::string migrationLog;
  Require(scene::MigrateEditorSceneGameLogic(source, &migrationLog), "v1 migration reported no change");
  Require(source.version == scene::kSceneSchemaV2_GameLogic, "migration did not set scene version 2");
  Require(source.game_entities[0].id.starts_with("ge_"), "migration did not assign ge_ id");
  Require(source.game_entities[0].components[0].id.starts_with("comp_"), "migration did not assign comp_ id");

  const std::string entityId = source.game_entities[0].id;
  const std::string componentId = source.game_entities[0].components[0].id;
  std::string error;
  Require(scene::SaveEditorSceneFile(source, path.string(), &error), "save failed: " + error);

  scene::EditorSceneFile loaded;
  Require(scene::LoadEditorSceneFile(path.string(), loaded, &error), "load failed: " + error);
  Require(loaded.game_entities[0].id == entityId, "entity id changed after save/load");
  Require(loaded.game_entities[0].components[0].id == componentId, "component id changed after save/load");
  Require(!scene::MigrateEditorSceneGameLogic(loaded), "v2 migration was not idempotent");
}

void TestEnsureIdsForV2Authoring() {
  scene::EditorSceneFile source;
  source.version = scene::kSceneSchemaV2_GameLogic;
  scene::SceneGameEntityDesc entity;
  entity.name = "Authored V2 Entity";
  entity.components.push_back(scene::SceneComponentDesc{.type = "health"});
  source.game_entities.push_back(std::move(entity));
  source.game_groups.push_back(scene::SceneGroupDesc{.name = "Authored V2 Group"});

  Require(scene::EnsureGameEntityIds(source), "v2 authoring ensure reported no change");
  Require(source.game_entities[0].id.starts_with("ge_"), "v2 entity id was not assigned");
  Require(source.game_entities[0].components[0].id.starts_with("comp_"),
          "v2 component id was not assigned");
  Require(source.game_groups[0].id.starts_with("grp_"), "v2 group id was not assigned");

  const std::string entityId = source.game_entities[0].id;
  const std::string componentId = source.game_entities[0].components[0].id;
  const std::string groupId = source.game_groups[0].id;
  Require(!scene::EnsureGameEntityIds(source), "v2 authoring ensure was not idempotent");
  Require(source.game_entities[0].id == entityId &&
          source.game_entities[0].components[0].id == componentId &&
          source.game_groups[0].id == groupId,
          "v2 authoring ensure changed stable ids");
}

void TestLegacyAiMigration() {
  scene::EditorSceneFile source;
  source.version = scene::kSceneSchemaV1;

  scene::SceneGameEntityDesc player;
  player.ai = "player";
  source.game_entities.push_back(player);

  scene::SceneGameEntityDesc navigationAgent;
  navigationAgent.ai = "nav_agent";
  source.game_entities.push_back(navigationAgent);

  scene::SceneGameEntityDesc none;
  source.game_entities.push_back(none);

  Require(scene::MigrateEditorSceneGameLogic(source), "legacy migration reported no change");
  Require(source.game_entities[0].control.mode == "player", "player ai did not map to player control");
  Require(source.game_entities[1].control.mode == "ai", "nav_agent did not map to ai control");
  Require(source.game_entities[1].components.size() == 2, "nav_agent did not gain default components");
  Require(source.game_entities[1].components[0].type == "movement", "nav_agent movement component missing");
  Require(source.game_entities[1].components[1].type == "path_follow", "nav_agent path_follow component missing");
  Require(source.game_entities[2].control.mode == "none", "empty ai did not map to none control");
}

void TestDuplicateEntityId() {
  scene::EditorSceneFile source;
  source.game_entities.push_back(MakeValidEntity("ge_duplicate"));
  source.game_entities.push_back(MakeValidEntity("ge_duplicate"));
  const scene::SceneValidationReport report = scene::ValidateEditorSceneGameLogic(source);
  Require(report.HasErrors(), "duplicate entity id did not report an error");
  Require(HasIssue(report, "game.dup_id", scene::SceneValidationSeverity::Error),
          "duplicate entity id error code missing");
}

void TestDuplicateComponentId() {
  scene::EditorSceneFile source;
  scene::SceneGameEntityDesc entity = MakeValidEntity();
  entity.components.push_back(scene::SceneComponentDesc{.id = "comp_duplicate", .type = "health"});
  entity.components.push_back(scene::SceneComponentDesc{.id = "comp_duplicate", .type = "movement"});
  source.game_entities.push_back(entity);
  const scene::SceneValidationReport report = scene::ValidateEditorSceneGameLogic(source);
  Require(HasIssue(report, "game.component.dup_id", scene::SceneValidationSeverity::Error),
          "duplicate component id error code missing");
}

void TestComponentRegistryValidation() {
  ComponentFactoryRegistry factories;
  Require(factories.Register("test_lifecycle", CreateLifecycleTestComponent, {}), "external registration failed");
  Require(!factories.Register("test_lifecycle", CreateOwnerStabilityTestComponent, {}), "duplicate replaced factory");
  Require(!factories.Register("", CreateLifecycleTestComponent, {}), "empty type registered");
  Require(!factories.Register("invalid", nullptr, {}), "null factory registered");
  Require(factories.Types() == std::vector<std::string>{"test_lifecycle"}, "registry enumeration differs");
  scene::EditorSceneFile scene;
  auto entity = MakeValidEntity();
  entity.components = {{.id = "component_external", .type = "test_lifecycle"}};
  scene.game_entities.push_back(entity);
  auto report = scene::ValidateEditorSceneGameLogic(scene, &factories, true);
  Require(!report.HasErrors(), "registered external component rejected");
  Require(!HasIssue(report, "game.component.unknown_type", scene::SceneValidationSeverity::Warning),
          "registered external component reported unknown");
  ComponentLoadContext context;
  auto component = factories.Create(entity.components.front(), context);
  Require(component->Type() == "test_lifecycle", "duplicate registration changed original factory");
  scene.game_entities.front().components.front().type = "missing_external";
  Require(scene::ValidateEditorSceneGameLogic(scene, &factories, true).HasErrors(), "required missing type accepted");
  Require(!scene::ValidateEditorSceneGameLogic(scene, &factories).HasErrors(), "authoring cannot preserve unknown type");
  scene.game_entities.front().components.front().enabled = false;
  Require(!scene::ValidateEditorSceneGameLogic(scene, &factories, true).HasErrors(), "disabled missing type blocks execution");
}

void TestUnknownComponentWarning() {
  TempSceneFiles files;
  const std::filesystem::path path = files.Add("_unknown.t8scene");

  scene::EditorSceneFile source;
  scene::SceneGameEntityDesc entity = MakeValidEntity();
  scene::SceneComponentDesc component;
  component.id = "comp_unknown";
  component.type = "future_component";
  component.params["value"] = "preserved";
  component.config_json = R"({"nested":true})";
  entity.components.push_back(component);
  source.game_entities.push_back(entity);

  const scene::SceneValidationReport report = scene::ValidateEditorSceneGameLogic(source);
  Require(!report.HasErrors(), "unknown component should warn, not error");
  Require(HasIssue(report, "game.component.unknown_type", scene::SceneValidationSeverity::Warning),
          "unknown component warning code missing");

  std::string error;
  Require(scene::SaveEditorSceneFile(source, path.string(), &error), "unknown component save failed: " + error);
  scene::EditorSceneFile loaded;
  Require(scene::LoadEditorSceneFile(path.string(), loaded, &error), "unknown component load failed: " + error);
  const scene::SceneComponentDesc& loadedComponent = loaded.game_entities[0].components[0];
  Require(loadedComponent.type == component.type, "unknown component type was not preserved");
  Require(loadedComponent.params == component.params, "unknown component params were not preserved");
  Require(loadedComponent.config_json == component.config_json, "unknown component config_json was not preserved");
}

void TestMissingInitialState() {
  scene::EditorSceneFile source;
  scene::SceneGameEntityDesc entity = MakeValidEntity();
  scene::SceneStateMachineDesc behavior;
  behavior.initial_state = "missing";
  behavior.states.push_back(scene::SceneStateDesc{.name = "idle"});
  entity.behavior = behavior;
  source.game_entities.push_back(entity);

  const scene::SceneValidationReport report = scene::ValidateEditorSceneGameLogic(source);
  Require(HasIssue(report, "game.behavior.initial_missing", scene::SceneValidationSeverity::Error),
          "missing initial state error code missing");
}

        void TestGroupStableIdsAfterRename() {
          TempSceneFiles files;
          const std::filesystem::path firstPath = files.Add("_group_before_rename.t8scene");
          const std::filesystem::path secondPath = files.Add("_group_after_rename.t8scene");

          scene::EditorSceneFile source;
          source.version = scene::kSceneSchemaV2_GameLogic;
          source.game_entities.push_back(MakeValidEntity("ge_group_leader"));
          source.game_entities.push_back(MakeValidEntity("ge_group_member"));
          source.game_entities[0].name = "Leader Before Rename";
          source.game_entities[1].name = "Member Before Rename";

          scene::SceneGroupDesc group;
          group.id = "grp_stable";
          group.name = "Stable Squad";
          group.member_entity_ids = {"ge_group_leader", "ge_group_member"};
          group.formation.leader_entity_id = "ge_group_leader";
          source.game_groups.push_back(group);

          std::string error;
          Require(scene::SaveEditorSceneFile(source, firstPath.string(), &error),
            "group pre-rename save failed: " + error);
          scene::EditorSceneFile loaded;
          Require(scene::LoadEditorSceneFile(firstPath.string(), loaded, &error),
            "group pre-rename load failed: " + error);
          loaded.game_entities[0].name = "Leader After Rename";
          loaded.game_entities[1].name = "Member After Rename";
          Require(scene::SaveEditorSceneFile(loaded, secondPath.string(), &error),
            "group post-rename save failed: " + error);

          scene::EditorSceneFile renamed;
          Require(scene::LoadEditorSceneFile(secondPath.string(), renamed, &error),
            "group post-rename load failed: " + error);
          Require(renamed.game_groups.size() == 1, "group was lost after entity rename");
          Require(renamed.game_groups[0].member_entity_ids == group.member_entity_ids,
            "group membership changed with entity names");
          Require(renamed.game_groups[0].formation.leader_entity_id == "ge_group_leader",
            "group leader id changed with entity name");
          Require(!scene::ValidateEditorSceneGameLogic(renamed).HasErrors(),
            "renamed stable-id group failed validation");
        }

        void TestGroupValidationReferences() {
          scene::EditorSceneFile source;
          source.game_entities.push_back(MakeValidEntity("ge_existing"));
          scene::SceneGroupDesc group;
          group.id = "grp_invalid";
          group.member_entity_ids = {"ge_missing"};
          group.formation.leader_entity_id = "ge_existing";
          source.game_groups.push_back(group);

          const scene::SceneValidationReport report = scene::ValidateEditorSceneGameLogic(source);
          Require(HasIssue(report, "game.group.member_missing", scene::SceneValidationSeverity::Error),
            "missing group member did not report an error");
          Require(HasIssue(report, "game.group.leader_not_member", scene::SceneValidationSeverity::Error),
            "non-member group leader did not report an error");
        }

void TestRegistryCreateFindDestroy() {
  GameObjectRegistry registry;
  scene::SceneGameEntityDesc descriptor = MakeValidEntity("ge_registry");
  descriptor.name = "Registry Object";
  GameObject* created = registry.Create(descriptor, GameObjectLinks{});
  Require(created != nullptr, "registry create returned null");
  const RuntimeGameObjectId runtimeId = created->runtimeId;
  Require(runtimeId != kInvalidRuntimeGameObjectId, "registry assigned invalid runtime id");
  Require(registry.Get(runtimeId) == created, "runtime-id lookup failed");
  Require(registry.FindBySceneId(descriptor.id) == created, "scene-id lookup failed");
  registry.RequestDestroy(runtimeId);
  Require(registry.Get(runtimeId) != nullptr, "destroy was not deferred");
  registry.ApplyDeferredDestroys();
  Require(registry.Count() == 0, "destroy did not erase object");
  Require(registry.Get(runtimeId) == nullptr, "runtime index survived destroy");
  Require(registry.FindBySceneId(descriptor.id) == nullptr, "scene index survived destroy");
}

void TestRegistryOwnerPointersStayStable() {
  EngineContext context;
  GameLogicSystem system;
  system.Initialize(context, GameLogicSettings{});
  system.Factories().Register(
      "test_owner_stability",
      CreateOwnerStabilityTestComponent,
      ComponentTypeInfo{.type = "test_owner_stability", .allowMultiple = false});

  scene::EditorSceneFile source;
  scene::SceneGameEntityDesc owner = MakeValidEntity("ge_stable_owner");
  owner.components.push_back(scene::SceneComponentDesc{
      .id = "comp_owner_stability", .type = "test_owner_stability"});
  source.game_entities.push_back(std::move(owner));
  for (int index = 0; index < 128; ++index) {
    source.game_entities.push_back(MakeValidEntity("ge_stable_" + std::to_string(index)));
  }

  scene::SceneValidationReport report;
  Require(system.LoadFromScene(source, GameSceneRuntimeLinks{}, &report),
          "owner-stability scene did not load");
  GameObject* originalOwner = system.Registry().FindBySceneId("ge_stable_owner");
  Require(originalOwner != nullptr, "stable owner was not created");
  const RuntimeGameObjectId originalOwnerId = originalOwner->runtimeId;

  GameObject* transient = system.Registry().FindBySceneId("ge_stable_64");
  Require(transient != nullptr, "transient stability object was not created");
  system.Registry().RequestDestroy(transient->runtimeId);
  system.Registry().ApplyDeferredDestroys();
  Require(system.Registry().Get(originalOwnerId) == originalOwner,
          "registry insertion or middle erase moved an existing object");

  gObservedOwnerId = kInvalidRuntimeGameObjectId;
  system.Update(1.0f / 60.0f);
  Require(gObservedOwnerId == originalOwnerId,
          "component retained an invalid owner pointer after registry mutation");
  gObservedOwnerId = kInvalidRuntimeGameObjectId;
}

void TestBrokenMeshLinkWarning() {
  scene::EditorSceneFile source;
  scene::SceneGameEntityDesc entity = MakeValidEntity("ge_broken_mesh");
  entity.mesh_object = "missing_mesh";
  source.game_entities.push_back(entity);

  EngineContext context;
  GameLogicSystem system;
  system.Initialize(context, GameLogicSettings{});
  GameSceneRuntimeLinks links;
  links.resolveMeshSlot = [](std::string_view) { return -1; };
  links.primitiveForSlot = [](int) { return static_cast<PrimitiveInst*>(nullptr); };
  links.resolveBody = [](std::string_view) { return PhysicsBodyHandle{}; };
  links.resolveCamera = [](std::string_view) { return -1; };

  scene::SceneValidationReport report;
  Require(system.LoadFromScene(source, links, &report), "broken mesh warning prevented scene load");
  Require(system.Registry().Count() == 1, "broken mesh object was discarded");
  Require(HasIssue(report, "game.link.mesh_missing", scene::SceneValidationSeverity::Warning),
          "broken mesh warning missing");
  Require(system.Registry().Objects().front().links.meshSlot == -1, "broken mesh unexpectedly resolved");
}

void TestGameLogicLifecycle() {
  scene::EditorSceneFile source;
  source.game_entities.push_back(MakeValidEntity("ge_lifecycle"));

  EngineContext context;
  GameLogicSystem system;
  system.Initialize(context, GameLogicSettings{});
  GameSceneRuntimeLinks links;
  scene::SceneValidationReport report;
  Require(system.LoadFromScene(source, links, &report), "one-entity scene did not load");
  Require(system.Registry().Count() == 1, "one-entity scene registry count mismatch");
  system.Shutdown();
  Require(system.Registry().Count() == 0, "shutdown did not clear registry");
  Require(system.Stats().tickIndex == 0, "shutdown did not reset tick index");
}

void TestFixedTickCap() {
  EngineContext context;
  GameLogicSettings settings;
  settings.fixedDeltaSeconds = 1.0f / 60.0f;
  settings.maxFrameDeltaSeconds = 0.25f;
  settings.maxStepsPerFrame = 4;

  GameLogicSystem system;
  system.Initialize(context, settings);
  system.Update(10.0f);
  Require(system.Stats().lastFrameSteps == 4, "large dt did not respect maxStepsPerFrame");
  Require(system.Stats().tickIndex == 4, "large dt advanced an unexpected number of ticks");
}

void TestFixedTickPause() {
  EngineContext context;
  GameLogicSystem system;
  system.Initialize(context, GameLogicSettings{});
  system.SetPaused(true);
  system.Update(1.0f);
  Require(system.Paused(), "fixed tick did not report paused state");
  Require(system.Stats().tickIndex == 0, "paused update advanced fixed ticks");
  system.SetPaused(false);
  system.Update(1.0f / 60.0f);
  Require(system.Stats().tickIndex == 1, "unpaused update did not resume with one fixed tick");
}

void TestControllerIntentsDiffer() {
  GameObject playerPawn;
  PlayerController player(0, "test_player");
  player.OnPossess(playerPawn);

  GameObject aiPawn;
  AIController ai("test_ai");
  ai.OnPossess(aiPawn);
  ai.SetNavigationGoal(XVECTOR3(10.0f, 0.0f, 0.0f, 1.0f));

  InputFrame input;
  input.moveAxis = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  const MovementIntent playerIntent = player.SampleIntent(input, 1.0f / 60.0f);
  const MovementIntent aiIntent = ai.SampleIntent(input, 1.0f / 60.0f);

  Require(playerIntent.moveDir.z > 0.9f, "player controller did not consume input frame");
  Require(aiIntent.moveDir.x > 0.9f, "AI controller did not steer toward navigation goal");
  Require(aiIntent.hasNavGoal && aiIntent.navGoal.has_value(), "AI intent did not preserve navigation goal");
  Require(playerIntent.moveDir != aiIntent.moveDir, "player and AI intents were not distinct");
}

void ConfigureLifecycleSystem(
  GameLogicSystem& system,
  EngineContext& context,
  std::vector<std::string>& log,
  bool removeDuringUpdate) {
  gLifecycleLog = &log;
  gRemoveDuringUpdate = removeDuringUpdate;

  system.Initialize(context, GameLogicSettings{});
  system.Factories().Register(
      "test_lifecycle",
      CreateLifecycleTestComponent,
      ComponentTypeInfo{.type = "test_lifecycle", .allowMultiple = false});

  scene::EditorSceneFile source;
  scene::SceneGameEntityDesc entity = MakeValidEntity("ge_component_test");
  entity.components.push_back(scene::SceneComponentDesc{
      .id = "comp_lifecycle", .type = "test_lifecycle"});
  source.game_entities.push_back(std::move(entity));
  scene::SceneValidationReport report;
  Require(system.LoadFromScene(source, GameSceneRuntimeLinks{}, &report),
          "lifecycle scene did not load");
}

void TestComponentLifecycleOrder() {
  EngineContext context;
  std::vector<std::string> log;
  GameLogicSystem system;
  ConfigureLifecycleSystem(system, context, log, false);
  system.Update(1.0f / 60.0f);
  system.Shutdown();
  gLifecycleLog = nullptr;

  const std::vector<std::string> expected = {"attach", "create", "update", "destroy", "detach"};
  Require(log == expected, "component lifecycle order mismatch");
}

void TestDeferredComponentRemoval() {
  EngineContext context;
  std::vector<std::string> log;
  GameLogicSystem system;
  ConfigureLifecycleSystem(system, context, log, true);
  Require(system.Registry().Objects().front().components.size() == 1, "test component was not created");
  system.Update(1.0f / 60.0f);
  Require(system.Registry().Objects().front().components.empty(), "component removal was not applied after tick");
  gLifecycleLog = nullptr;

  const std::vector<std::string> expected = {
      "attach", "create", "update", "during:1", "destroy", "detach"};
  Require(log == expected, "deferred component removal invalidated lifecycle order");
}

void TestEventFifo() {
  EventBus events;
  std::vector<int> received;
  events.Subscribe("ordered", [&](const GameEvent& event) {
    received.push_back(std::stoi(event.params.at("value")));
  });
  GameEvent first;
  first.type = "ordered";
  first.params["value"] = "1";
  events.Publish(std::move(first));
  GameEvent second;
  second.type = "ordered";
  second.params["value"] = "2";
  events.Publish(std::move(second));
  events.DispatchQueued(7);
  Require(received == std::vector<int>({1, 2}), "events were not dispatched FIFO");
  Require(events.RecentEvents().size() == 2, "event log did not retain dispatched events");
  Require(events.RecentEvents()[0].sequence < events.RecentEvents()[1].sequence,
          "event sequence was not monotonic");
}

void TestHandlerPublishNextCycle() {
  EventBus events;
  std::vector<std::string> received;
  events.Subscribe("first", [&](const GameEvent&) {
    received.push_back("first");
    GameEvent next;
    next.type = "second";
    events.Publish(std::move(next));
  });
  events.Subscribe("second", [&](const GameEvent&) { received.push_back("second"); });
  GameEvent first;
  first.type = "first";
  events.Publish(std::move(first));
  events.DispatchQueued(1);
  Require(received == std::vector<std::string>({"first"}),
          "handler-published event dispatched recursively");
  events.DispatchQueued(2);
  Require(received == std::vector<std::string>({"first", "second"}),
          "handler-published event did not dispatch next cycle");
}

void TestDestroyRequestedInHandler() {
  EngineContext context;
  GameLogicSystem system;
  system.Initialize(context, GameLogicSettings{});
  GameObject* object = system.Registry().Create(MakeValidEntity("ge_event_destroy"), GameObjectLinks{});
  Require(object != nullptr, "event-destroy object was not created");
  const RuntimeGameObjectId id = object->runtimeId;
  bool validDuringHandler = false;
  system.Events().Subscribe("destroy", [&](const GameEvent&) {
    validDuringHandler = system.Registry().Get(id) != nullptr;
    system.RequestDestroy(id);
  });
  GameEvent destroy;
  destroy.type = "destroy";
  system.Events().Publish(std::move(destroy));
  system.Update(1.0f / 60.0f);
  Require(validDuringHandler, "object was destroyed during event dispatch");
  Require(system.Registry().Get(id) == nullptr, "deferred object destroy was not applied after tick");
}

scene::SceneStateMachineDesc MakeStateMachineDescriptor() {
  scene::SceneStateMachineDesc descriptor;
  descriptor.initial_state = "idle";
  descriptor.states.push_back(scene::SceneStateDesc{.name = "idle"});
  descriptor.states.push_back(scene::SceneStateDesc{.name = "move"});
  descriptor.states.push_back(scene::SceneStateDesc{.name = "attack"});
  return descriptor;
}

void TestStateMachineInitialState() {
  scene::SceneValidationReport report;
  StateMachine machine;
  const scene::SceneStateMachineDesc descriptor = MakeStateMachineDescriptor();
  Require(machine.Compile(descriptor, &report), "state machine did not compile");
  machine.SetInitialState();
  Require(machine.CurrentStateName() == "idle", "initial state was not honored");
}

void TestStateMachinePriorityAndOrder() {
  EngineContext context;
  GameLogicSystem system;
  system.Initialize(context, GameLogicSettings{});
  GameObject owner;
  owner.sceneId = "ge_state_priority";

  scene::SceneStateMachineDesc descriptor = MakeStateMachineDescriptor();
  descriptor.transitions.push_back(scene::SceneTransitionDesc{
      .from_state = "idle", .to_state = "move", .condition = "always", .priority = 1.0f});
  descriptor.transitions.push_back(scene::SceneTransitionDesc{
      .from_state = "idle", .to_state = "attack", .condition = "always", .priority = 10.0f});
  StateMachine machine;
  Require(machine.Compile(descriptor, nullptr), "priority machine did not compile");
  machine.Evaluate(owner, system, 1.0f / 60.0f);
  Require(machine.CurrentStateName() == "attack", "higher-priority transition did not win");

  descriptor.transitions.clear();
  descriptor.transitions.push_back(scene::SceneTransitionDesc{
      .from_state = "idle", .to_state = "move", .condition = "always", .priority = 5.0f});
  descriptor.transitions.push_back(scene::SceneTransitionDesc{
      .from_state = "idle", .to_state = "attack", .condition = "always", .priority = 5.0f});
  Require(machine.Compile(descriptor, nullptr), "tie-order machine did not compile");
  machine.Evaluate(owner, system, 1.0f / 60.0f);
  Require(machine.CurrentStateName() == "move", "descriptor order did not break priority tie");
}

void TestStateMachineCooldown() {
  EngineContext context;
  GameLogicSystem system;
  system.Initialize(context, GameLogicSettings{});
  GameObject owner;
  owner.sceneId = "ge_state_cooldown";

  scene::SceneStateMachineDesc descriptor = MakeStateMachineDescriptor();
  descriptor.transitions.push_back(scene::SceneTransitionDesc{
      .from_state = "*", .to_state = "move", .condition = "always", .priority = 1.0f, .cooldown = 1.0f});
  StateMachine machine;
  Require(machine.Compile(descriptor, nullptr), "cooldown machine did not compile");
  machine.Evaluate(owner, system, 1.0f / 60.0f);
  Require(system.Events().PendingCount() == 1, "first transition did not publish state_changed");
  machine.Evaluate(owner, system, 1.0f / 60.0f);
  Require(system.Events().PendingCount() == 1, "cooldown did not block immediate repeat");
}

void TestStateMachineEventTiming() {
  EngineContext context;
  GameLogicSystem system;
  system.Initialize(context, GameLogicSettings{});
  GameObject owner;
  owner.sceneId = "ge_state_event";

  scene::SceneStateMachineDesc descriptor = MakeStateMachineDescriptor();
  descriptor.transitions.push_back(scene::SceneTransitionDesc{
      .from_state = "idle", .to_state = "attack", .condition = "on_event:enemy_spotted"});
  StateMachine machine;
  Require(machine.Compile(descriptor, nullptr), "event machine did not compile");

  GameEvent spotted;
  spotted.type = "enemy_spotted";
  spotted.targetEntityId = owner.sceneId;
  system.Events().Publish(std::move(spotted));
  machine.Evaluate(owner, system, 1.0f / 60.0f);
  Require(machine.CurrentStateName() == "idle", "queued event fired before dispatch");
  system.Events().DispatchQueued(system.TickIndex());
  machine.Evaluate(owner, system, 1.0f / 60.0f);
  Require(machine.CurrentStateName() == "attack", "dispatched event did not trigger transition");
}

void TestPhysicsUnavailable() {
  GameObjectRegistry registry;
  GamePhysicsService physics;
  physics.Bind(nullptr, &registry);
  Require(!physics.Available(), "unbound physics service reported available");

  GameHit lineHit;
  Require(!physics.LineOfSight(
              XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f),
              XVECTOR3(1.0f, 0.0f, 0.0f, 1.0f),
              GameQueryFilter{},
              lineHit),
          "unavailable line-of-sight query reported a hit");
  std::vector<GameHit> overlaps;
  Require(physics.OverlapSphere(
              XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f), 1.0f, GameQueryFilter{}, overlaps) == 0,
          "unavailable overlap query returned hits");

  physics.EnqueueSetVelocity(
      1, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f), XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
  XMATRIX44 transform;
  transform.Identity();
  physics.EnqueueKinematicMove(1, transform);
  physics.Flush(1.0f / 60.0f);
  Require(!physics.Available(), "command flush changed unavailable state");
}

void TestPhysicsBodyHandleReuseRejectsStaleHandles() {
  JoltPhysicsSystem physics;
  if (!physics.IsAvailable()) return;
  Require(physics.Initialize(), "Jolt failed to initialize for handle reuse test");

  PhysicsBodyDesc firstDesc;
  firstDesc.entityId = 101;
  firstDesc.debugName = "handle_reuse_first";
  firstDesc.shape = PhysicsShapeDesc::Box(XVECTOR3(0.5f, 0.5f, 0.5f, 0.0f));
  firstDesc.worldTransform.Identity();
  firstDesc.motion = PhysicsBodyMotion::Static;
  const PhysicsBodyHandle first = physics.CreateBody(firstDesc);
  Require(first.IsValid(), "first physics body was not created");
  Require(physics.DestroyBody(first), "first physics body was not destroyed");

  PhysicsBodyDesc secondDesc = firstDesc;
  secondDesc.entityId = 202;
  secondDesc.debugName = "handle_reuse_second";
  const PhysicsBodyHandle second = physics.CreateBody(secondDesc);
  Require(second.IsValid(), "replacement physics body was not created");
  Require(second.value == first.value, "destroyed physics slot was not reused");
  Require(second.generation != first.generation, "reused physics slot kept the stale generation");

  PhysicsBodyState state;
  Require(!physics.GetBodyState(first, state), "stale physics handle resolved to replacement body");
  Require(physics.GetBodyState(second, state), "replacement physics handle did not resolve");
  physics.Shutdown();
}

void TestGeneratedTriangleMeshPhysicsBody() {
  JoltPhysicsSystem physics;
  if (!physics.IsAvailable()) return;
  Require(physics.Initialize(), "Jolt failed to initialize for generated mesh test");

  PhysicsTriangleMeshBodyDesc descriptor;
  descriptor.entityId = 303;
  descriptor.debugName = "generated_triangle_mesh";
  descriptor.mesh.vertices = {
      XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f),
      XVECTOR3(2.0f, 0.0f, 0.0f, 1.0f),
      XVECTOR3(0.0f, 0.0f, 2.0f, 1.0f)};
  descriptor.mesh.indices = {0, 1, 2};
  descriptor.mesh.localBounds.ExpandToInclude(0.0f, 0.0f, 0.0f);
  descriptor.mesh.localBounds.ExpandToInclude(2.0f, 0.0f, 2.0f);
  descriptor.mesh.settings.buildQuality = PhysicsMeshBuildQuality::FavorBuildSpeed;
  descriptor.mesh.settings.useDiskCache = false;
  descriptor.worldTransform.Identity();
  descriptor.gameplayLayer = GameplayLayer::WorldStatic;

  PhysicsCookStats stats;
  const PhysicsBodyHandle handle = physics.CreateTriangleMeshBody(descriptor, &stats);
  Require(handle.IsValid(), "generated triangle mesh body was not created");
  Require(stats.vertexCount == 3 && stats.triangleCount == 1,
          "generated triangle mesh cook stats were incorrect");
  PhysicsBodyState state;
  Require(physics.GetBodyState(handle, state) && state.entityId == descriptor.entityId,
          "generated triangle mesh body state did not resolve");
  Require(physics.DestroyBody(handle), "generated triangle mesh body was not destroyed");
  physics.Shutdown();
}

  MutableMeshSnapshot MakeMutableTriangle() {
    MutableMeshSnapshot snapshot;
    snapshot.version = 1;
    snapshot.vertices = {
        MutableMeshVertex{.position = XVECTOR3(-2.0f, 1.0f, 3.0f, 1.0f)},
        MutableMeshVertex{.position = XVECTOR3(4.0f, -1.0f, 2.0f, 1.0f)},
        MutableMeshVertex{.position = XVECTOR3(0.0f, 5.0f, -3.0f, 1.0f)}};
    snapshot.indices = {0, 1, 2};
    snapshot.materials.push_back(MutableMeshMaterial{});
    snapshot.sections.push_back(MutableMeshSection{.firstIndex = 0, .indexCount = 3, .materialIndex = 0});
    RecalculateMutableMeshBounds(snapshot);
    return snapshot;
  }

  void TestMutableMeshValidationAndBounds() {
    MutableMeshSnapshot snapshot = MakeMutableTriangle();
    std::string error;
    Require(ValidateMutableMeshSnapshot(snapshot, &error), "valid mutable triangle was rejected: " + error);
    Require(snapshot.localBounds.vMin.x == -2.0f && snapshot.localBounds.vMin.y == -1.0f &&
      snapshot.localBounds.vMin.z == -3.0f,
      "mutable mesh minimum bounds were incorrect");
    Require(snapshot.localBounds.vMax.x == 4.0f && snapshot.localBounds.vMax.y == 5.0f &&
      snapshot.localBounds.vMax.z == 3.0f,
      "mutable mesh maximum bounds were incorrect");

    snapshot.indices[2] = 99;
    Require(!ValidateMutableMeshSnapshot(snapshot, &error),
      "mutable mesh accepted an out-of-range vertex index");
  }

  void TestMutableMeshSectionValidation() {
    MutableMeshSnapshot snapshot = MakeMutableTriangle();
    std::string error;
    snapshot.sections[0].indexCount = 6;
    Require(!ValidateMutableMeshSnapshot(snapshot, &error),
      "mutable mesh accepted a section beyond the index buffer");
    snapshot = MakeMutableTriangle();
    snapshot.sections[0].materialIndex = 1;
    Require(!ValidateMutableMeshSnapshot(snapshot, &error),
      "mutable mesh accepted a missing material reference");
  }

void TestRenderContainerStableHandles() {
  RenderContainer container;
  NullTestPrimitive primitive;
  XMATRIX44 viewProjection;
  viewProjection.Identity();
  PrimitiveInst instance;
  instance.CreateInstance(&primitive, &viewProjection);
  const RenderInstanceHandle first = container.AddMeshInstance(instance);
  const RenderInstanceHandle second = container.AddMeshInstance(instance);
  Require(first.IsValid() && second.IsValid(), "render container did not return valid handles");
  Require(container.ActiveMeshCount() == 2, "render container active count was incorrect");
  Require(container.RemoveMesh(first), "render container failed to remove a live handle");
  Require(container.GetMesh(first) == nullptr, "stale render handle resolved after removal");
  const RenderInstanceHandle replacement = container.AddMeshInstance(instance);
  Require(replacement.index == first.index, "render container did not reuse a free slot");
  Require(replacement.generation != first.generation, "reused render slot kept a stale generation");
  Require(container.GetMesh(replacement) != nullptr, "replacement render handle did not resolve");
  Require(!container.RemoveMesh(first), "stale render handle removed a replacement instance");
}

  void TestTextureAtlasGridRegions() {
    TextureAtlas atlas;
    atlas.textureId = 7;
    atlas.widthPx = 64;
    atlas.heightPx = 32;
    atlas.tileWidthPx = 16;
    atlas.tileHeightPx = 8;
    atlas.columns = 4;
    atlas.rows = 4;

    TextureAtlasRegion region;
    Require(atlas.TryGetGridRegion(2, 1, region), "atlas rejected a valid grid region");
    Require(region.xPx == 32 && region.yPx == 8 && region.widthPx == 16 && region.heightPx == 8,
      "atlas returned incorrect pixel bounds");
    Require(std::abs(region.u0 - (32.5f / 64.0f)) < 0.000001f &&
      std::abs(region.v0 - (8.5f / 32.0f)) < 0.000001f &&
      std::abs(region.u1 - (47.5f / 64.0f)) < 0.000001f &&
      std::abs(region.v1 - (15.5f / 32.0f)) < 0.000001f,
      "atlas half-texel UVs were incorrect");
    Require(!atlas.TryGetGridRegion(4, 1, region) && !atlas.TryGetGridRegion(2, 4, region),
      "atlas accepted an out-of-range grid region");
  }

  void TestMaterialTextureVariantIsImmutable() {
    MaterialAsset base;
    base.name = "selftest_material";
    base.textureIds[(int)MatTexSlot::BaseColor] = 3;
    base.textures[(int)MatTexSlot::BaseColor] = reinterpret_cast<Texture*>(1);
    MaterialAsset* cachedBase = MaterialAssetCache::Get().Acquire(base);
    MaterialAsset* variant = MaterialAssetCache::Get().AcquireTextureVariant(
      *cachedBase, MatTexSlot::BaseColor, reinterpret_cast<Texture*>(2), 9);

    Require(cachedBase->textureIds[(int)MatTexSlot::BaseColor] == 3 &&
      cachedBase->textures[(int)MatTexSlot::BaseColor] == reinterpret_cast<Texture*>(1),
      "material texture variant mutated the cached base asset");
    Require(variant != cachedBase && variant->textureIds[(int)MatTexSlot::BaseColor] == 9 &&
      variant->textures[(int)MatTexSlot::BaseColor] == reinterpret_cast<Texture*>(2),
      "material texture variant did not contain the requested binding");

    MaterialAssetCache::Get().Release(variant);
    MaterialAssetCache::Get().Release(cachedBase);
  }

terrain::BlockId RegisterTestStone(terrain::BlockRegistry& registry) {
  terrain::BlockDefinition stone;
  stone.name = "test_stone";
  stone.color = XVECTOR3(0.35f, 0.4f, 0.45f, 1.0f);
  return registry.Register(std::move(stone));
}

void TestVoxelChunkMutation() {
  terrain::BlockRegistry registry;
  const terrain::BlockId stone = RegisterTestStone(registry);
  Require(stone != terrain::kAirBlock && registry.Find("test_stone") == stone,
          "voxel block registration failed");
  terrain::VoxelChunk chunk({}, terrain::ChunkDimensions{2, 2, 2});
  const uint64_t initialVersion = chunk.Version();
  Require(chunk.Set(1, 1, 1, stone), "voxel chunk mutation failed");
  Require(chunk.Get(1, 1, 1) == stone, "voxel chunk lost a stored block");
  Require(chunk.Version() > initialVersion, "voxel chunk mutation did not advance its version");
  Require(!chunk.Set(1, 1, 1, stone), "identical voxel write reported a mutation");
  Require(chunk.Get(-1, 0, 0) == terrain::kAirBlock, "out-of-bounds voxel read was not air");
}

void TestGreedyVoxelMesher() {
  terrain::BlockRegistry registry;
  const terrain::BlockId stone = RegisterTestStone(registry);
  terrain::VoxelChunk chunk({}, terrain::ChunkDimensions{2, 2, 2});
  chunk.Fill(stone);
  MutableMeshSnapshot snapshot;
  std::string error;
  Require(terrain::BuildGreedyVoxelMesh(chunk, registry, {}, snapshot, &error),
          "solid voxel chunk meshing failed: " + error);
  Require(snapshot.vertices.size() == 24, "greedy mesher did not merge a solid chunk into six quads");
  Require(snapshot.indices.size() == 36, "greedy mesher emitted an unexpected solid-chunk index count");
  Require(snapshot.sections.size() == 1, "single-block chunk did not produce one material section");
}

void TestVoxelMesherUsesNeighborBoundary() {
  terrain::BlockRegistry registry;
  const terrain::BlockId stone = RegisterTestStone(registry);
  terrain::VoxelChunk chunk({}, terrain::ChunkDimensions{1, 1, 1});
  chunk.Set(0, 0, 0, stone);
  MutableMeshSnapshot snapshot;
  std::string error;
  const terrain::NeighborBlockSampler neighbor = [stone](int x, int y, int z) {
    return x == 1 && y == 0 && z == 0 ? stone : terrain::kAirBlock;
  };
  Require(terrain::BuildGreedyVoxelMesh(chunk, registry, neighbor, snapshot, &error),
          "neighbor-aware voxel meshing failed: " + error);
  Require(snapshot.vertices.size() == 20 && snapshot.indices.size() == 30,
          "neighbor-aware mesher did not suppress the shared chunk face");
}

        void TestVoxelWorldCoordinatesAndRaycast() {
          terrain::BlockRegistry registry;
          const terrain::BlockId stone = RegisterTestStone(registry);
          terrain::VoxelWorld world(terrain::ChunkDimensions{4, 4, 4});
          Require(world.SetBlock(-1, 0, -1, stone), "negative voxel world write failed");
          Require(world.GetBlock(-1, 0, -1) == stone, "negative voxel world lookup failed");
          const terrain::ChunkKey negativeKey = world.WorldToChunk(-1, 0, -1);
          Require(negativeKey == terrain::ChunkKey{-1, 0, -1}, "negative world coordinate mapped to wrong chunk");

          Require(world.SetBlock(0, 0, 0, stone), "raycast target write failed");
          terrain::VoxelRayHit hit;
          Require(world.Raycast(
                XVECTOR3(0.5f, 0.5f, -2.0f, 1.0f),
                XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f),
                5.0f,
                registry,
                hit),
            "voxel DDA ray missed a solid block");
          Require(hit.blockX == 0 && hit.blockY == 0 && hit.blockZ == 0,
            "voxel DDA returned the wrong block");
          Require(hit.previousX == 0 && hit.previousY == 0 && hit.previousZ == -1,
            "voxel DDA returned the wrong placement cell");
        }

  void TestVoxelStreamingBudgetsAndUnloads() {
    ThreadPool pool(2);
    terrain::VoxelStreamingManager streaming(terrain::ChunkDimensions{4, 4, 4});
    terrain::VoxelStreamingSettings settings;
    settings.horizontalRadius = 1;
    settings.verticalRadius = 0;
    settings.maxInFlight = 9;
    settings.maxLaunchesPerUpdate = 9;
    settings.maxCommitsPerUpdate = 9;
    settings.maxUnloadsPerUpdate = 9;
    streaming.SetSettings(settings);
    const terrain::VoxelChunkBuildFunction build = [](const terrain::VoxelChunkBuildRequest& request) {
      terrain::VoxelChunkBuildResult result;
      result.key = request.key;
      result.epoch = request.epoch;
      if (request.IsCancelled()) {
        result.cancelled = true;
        return result;
      }
      result.chunk = std::make_unique<terrain::VoxelChunk>(request.key, request.dimensions);
      return result;
    };

    const terrain::ChunkKey firstFocus{0, 0, 0};
    streaming.Update(firstFocus, {}, &pool, build);
    Require(streaming.Stats().desired == 9 && streaming.Stats().launched == 9,
            "voxel streamer did not launch the configured radius");
    pool.WaitAll();
    streaming.Update(firstFocus, {}, &pool, build);
    std::vector<terrain::VoxelChunkBuildResult> completed = streaming.TakeCompleted();
    Require(completed.size() == 9, "voxel streamer did not return all completed chunks");
    std::vector<terrain::ChunkKey> loaded;
    loaded.reserve(completed.size());
    for (const auto& result : completed) {
      Require(result.Succeeded(), "voxel streamer returned a failed build result");
      loaded.push_back(result.key);
    }

    const terrain::ChunkKey secondFocus{10, 0, 0};
    streaming.Update(secondFocus, loaded, nullptr, build);
    const std::vector<terrain::ChunkKey> unloads = streaming.TakeUnloadRequests();
    Require(unloads.size() == loaded.size(), "voxel streamer did not unload chunks outside the new radius");
    streaming.Reset();
  }

  void TestVoxelDeltaPersistenceRoundTrip() {
    TempSceneFiles files;
    const std::filesystem::path path = files.Add("_voxel_delta.t8vox");
    terrain::VoxelDeltaStore source;
    Require(source.Record(-1, 2, -1, 7), "voxel delta did not record a negative-coordinate edit");
    Require(source.Record(1, 1, 1, 9), "voxel delta did not record a positive-coordinate edit");
    std::string error;
    Require(source.Save(path.string(), &error), "voxel delta save failed: " + error);

    terrain::VoxelDeltaStore loaded;
    Require(loaded.Load(path.string(), &error), "voxel delta load failed: " + error);
    Require(loaded.Count() == 2, "voxel delta round-trip changed the edit count");
    terrain::BlockId block = terrain::kAirBlock;
    Require(loaded.Find(-1, 2, -1, block) && block == 7,
            "voxel delta round-trip lost a negative-coordinate edit");

    terrain::VoxelChunk negativeChunk({-1, 0, -1}, terrain::ChunkDimensions{4, 4, 4});
    loaded.ApplyToChunk(negativeChunk);
    Require(negativeChunk.Get(3, 2, 3) == 7,
            "voxel delta applied to the wrong local cell in a negative chunk");
  }

  void TestVoxelNavigationAvoidsSolidsAndRejectsPartialPaths() {
    constexpr int width = 7;
    constexpr int depth = 5;
    constexpr int height = 4;
    bool solids[width][height][depth] = {};
    for (int x = 0; x < width; ++x)
      for (int z = 0; z < depth; ++z)
        solids[x][0][z] = true;
    for (int z = 0; z < depth - 1; ++z) {
      solids[3][1][z] = true;
      solids[3][2][z] = true;
    }

    terrain::VoxelNavigationSettings settings;
    settings.minFeetY = 1;
    settings.maxFeetY = height - 1;
    settings.projectionHorizontalRadius = 0;
    settings.projectionVerticalRadius = 0;
    terrain::VoxelNavigationQuery query;
    query.isColumnLoaded = [](int x, int z) {
      return x >= 0 && x < width && z >= 0 && z < depth;
    };
    query.isSolid = [&](int x, int y, int z) {
      return x >= 0 && x < width && y >= 0 && y < height && z >= 0 && z < depth &&
             solids[x][y][z];
    };

    const terrain::VoxelNavigationResult around = terrain::FindVoxelPath(
        XVECTOR3(0.5f, 1.0f, 0.5f, 1.0f),
        XVECTOR3(6.5f, 1.0f, 0.5f, 1.0f), settings, query);
    Require(around.success, "voxel navigation failed to route around a solid wall: " + around.error);
    bool usedGap = false;
    for (const XVECTOR3& point : around.points)
      usedGap = usedGap || point.z > 3.5f;
    Require(usedGap, "voxel navigation crossed a solid wall instead of using its gap");

    query.isColumnLoaded = [](int x, int z) {
      return x >= 0 && x < width && z == 0;
    };
    const terrain::VoxelNavigationResult blocked = terrain::FindVoxelPath(
        XVECTOR3(0.5f, 1.0f, 0.5f, 1.0f),
        XVECTOR3(6.5f, 1.0f, 0.5f, 1.0f), settings, query);
    Require(!blocked.success && blocked.points.empty(),
            "voxel navigation returned a partial path through a blocked corridor");

    std::memset(solids, 0, sizeof(solids));
    solids[0][0][0] = true;
    for (int x = 1; x <= 2; ++x) {
      solids[x][0][0] = true;
      solids[x][1][0] = true;
    }
    query.isColumnLoaded = [](int x, int z) { return x >= 0 && x <= 2 && z == 0; };
    const terrain::VoxelNavigationResult step = terrain::FindVoxelPath(
      XVECTOR3(0.5f, 1.0f, 0.5f, 1.0f),
      XVECTOR3(2.5f, 2.0f, 0.5f, 1.0f), settings, query);
    Require(step.success, "voxel navigation failed a clear one-block step: " + step.error);

    solids[0][3][0] = true;
    const terrain::VoxelNavigationResult lowCeiling = terrain::FindVoxelPath(
      XVECTOR3(0.5f, 1.0f, 0.5f, 1.0f),
      XVECTOR3(2.5f, 2.0f, 0.5f, 1.0f), settings, query);
    Require(!lowCeiling.success,
        "voxel navigation planned a one-block step without lift clearance");
  }

    void TestVoxelCollisionPreventsTunneling() {
      terrain::VoxelCollisionQuery query;
      query.isBlocked = [](int x, int y, int z) {
        return x == 3 && y == 1 && z == 0;
      };
      CharacterBoxSweep sweep;
      sweep.startCenter = XVECTOR3(0.5f, 1.5f, 0.5f, 1.0f);
      sweep.displacement = XVECTOR3(6.0f, 0.0f, 0.0f, 0.0f);
      sweep.halfExtents = XVECTOR3(0.25f, 0.5f, 0.25f, 0.0f);
      CharacterCollisionHit hit;
      Require(terrain::SweepVoxelBox(sweep, query, hit) && hit.hit,
        "voxel box sweep tunneled through a solid block");
      Require(hit.fraction > 0.37f && hit.fraction < 0.38f && hit.normal.x < -0.99f,
        "voxel box sweep returned the wrong impact time or wall normal");

      sweep.displacement = XVECTOR3(0.0f, 0.0f, 4.0f, 0.0f);
      Require(!terrain::SweepVoxelBox(sweep, query, hit),
        "voxel box sweep hit geometry outside its swept broadphase");

      sweep.startCenter = XVECTOR3(2.75f, 1.5f, 0.5f, 1.0f);
      sweep.displacement = XVECTOR3(-1.0f, 0.0f, 0.0f, 0.0f);
      Require(terrain::SweepVoxelBox(sweep, query, hit) && hit.fraction == 0.0f,
        "voxel box sweep missed initial wall contact");
      Require(hit.normal.x < -0.99f &&
        hit.normal.x * sweep.displacement.x > 0.0f,
        "voxel box sweep initial contact normal blocks movement away from a wall");
    }

  void TestNavigationUnavailable() {
    GameNavigationService navigation;
    navigation.Bind(nullptr, nullptr);
    Require(!navigation.Available(), "unbound navigation service reported available");

    const XVECTOR3 start(0.0f, 0.0f, 0.0f, 1.0f);
    const XVECTOR3 goal(10.0f, 0.0f, 0.0f, 1.0f);
    const uint64_t requestId = navigation.RequestPath(1, start, goal);
    Require(requestId == GameNavigationService::kInvalidRequestId,
      "unavailable navigation accepted a path request");

    t850::navigation::NavPathResult result;
    Require(!navigation.TryGetResult(requestId, result),
      "invalid navigation request produced a result");
    XVECTOR3 projected;
    Require(!navigation.ProjectToNavmesh(start, projected),
      "unavailable navigation projected a point");
    navigation.ResolveCompleted();
  }

void TestSceneConversions() {
  scene::SceneCameraDesc cameraDesc;
  cameraDesc.type = 1;
  cameraDesc.ortho_w = 64.0f;
  cameraDesc.ortho_h = 32.0f;
  Camera camera;
  scene::ApplySceneCamera(cameraDesc, camera, 2.0f);
  Require(camera.Ortho && camera.Width == 64.0f && camera.Height == 32.0f,
      "authored orthographic camera was not preserved");
  cameraDesc.type = 0;
  scene::ApplySceneCamera(cameraDesc, camera, 2.0f);
  Require(!camera.Ortho && camera.AspectRatio == 2.0f, "perspective camera application failed");
  for (navigation::NavTraversalType type : {navigation::NavTraversalType::Walk,
     navigation::NavTraversalType::Drop, navigation::NavTraversalType::Jump,
     navigation::NavTraversalType::JumpPad, navigation::NavTraversalType::JumpIntent}) {
  scene::SceneNavMeshLinkDesc link;
  link.type = scene::NavLinkTypeName(type);
  link.end = {2.0f, 0.0f, 0.0f};
  Require(scene::NavOffMeshLinkFromScene(link).type == type, "traversal type did not round trip");
  Require(scene::IsUsableAuthoredNavLink(link), "valid authored link rejected");
  link.radius = std::numeric_limits<float>::infinity();
  Require(!scene::IsUsableAuthoredNavLink(link), "infinite link radius accepted");
  link.radius = 1.0f;
  link.end = link.start;
  Require(!scene::IsUsableAuthoredNavLink(link), "zero-length link accepted");
  }
  navigation::NavMeshBuildSettings settings = scene::DefaultSceneNavMeshBuildSettings();
  settings.agentRadius = 1.25f;
  settings.enableAutoJumpLinks = false;
  settings.queryExtents = XVECTOR3(7.0f, 8.0f, 9.0f, 0.0f);
  settings.offMeshLinkValidationKey = 1234567;
  const auto loaded = scene::NavMeshBuildSettingsFromScene(scene::NavMeshBuildSettingsToScene(settings));
  Require(loaded.agentRadius == settings.agentRadius && !loaded.enableAutoJumpLinks &&
    loaded.queryExtents.z == 9.0f && loaded.offMeshLinkValidationKey == settings.offMeshLinkValidationKey,
    "navigation settings did not round trip");
  scene::SceneNavMeshVolumeDesc volume;
  volume.type = "area_cost";
  volume.area = "mud";
  volume.cost = 3.0f;
  const auto modifier = scene::NavVolumeModifierFromScene(volume);
  Require(modifier.mode == navigation::NavMeshModifierMode::Area && modifier.area == 7 && modifier.cost == 3.0f,
    "authored area volume changed meaning");
  PhysicsTriangleMeshCookSettings cook;
  cook.buildQuality = PhysicsMeshBuildQuality::FavorBuildSpeed;
  cook.useDiskCache = false;
  const auto cooked = scene::PhysicsCookSettingsFromScene(scene::PhysicsCookSettingsToScene(cook));
  Require(cooked.buildQuality == cook.buildQuality && !cooked.useDiskCache,
    "physics cook settings did not round trip");
}

void TestHeightmapTerrain() {
  scene::SceneHeightmapDesc desc;
  desc.samples_x = 3;
  desc.samples_z = 3;
  desc.size_x = 8.0f;
  desc.size_z = 4.0f;
  desc.height_scale = 10.0f;
  desc.height_offset = -2.0f;
  const std::array<float, 4> heights = {0.0f, 1.0f, 0.0f, 1.0f};
  MutableMeshSnapshot mesh;
  std::string error;
  Require(BuildHeightmapTerrain(desc, heights, 2, 2, mesh, &error), error);
  Require(mesh.vertices.size() == 9 && mesh.indices.size() == 24, "incorrect heightmap topology");
    Require(mesh.vertices[4].position.y == 3.0f && mesh.localBounds.vMax.x == 8.0f &&
      mesh.localBounds.vMax.z == 4.0f, "heightmap interpolation or dimensions are wrong");
  Require(mesh.vertices[4].normal.y > 0.0f && mesh.vertices[4].normal.x < 0.0f,
    "heightmap normals do not face up the slope");
    auto database = BuildMeshDatabase(mesh, &error);
    Require(database != nullptr, error);
    navigation::NavMeshGeometry navGeometry;
    Require(navigation::BuildGeometryFromXDataBase(*database, navGeometry, &error), error);
    Require(navGeometry.vertices.size() == mesh.vertices.size() && navGeometry.indices.size() == mesh.indices.size(),
      "navigation did not receive generated terrain geometry");
    Require(BuildMeshDatabase(mesh, &error)->m_name == database->m_name, "generated geometry identity is unstable");
    RenderMesh renderMesh;
    renderMesh.xFile = database.get();
    XMATRIX44 identity;
    identity.Identity();
    PhysicsTriangleMeshBodyDesc collision;
    Require(BuildStaticTriangleMeshBodyDesc(renderMesh, identity, 1, PhysicsTriangleMeshCookSettings{}, collision),
        "generated terrain cannot supply static collision");
  desc.samples_x = 0;
  Require(!BuildHeightmapTerrain(desc, heights, 2, 2, mesh, &error) && mesh.vertices.size() == 9,
    "invalid heightmap replaced existing geometry");
  desc.samples_x = 3;
  desc.height_scale = std::numeric_limits<float>::quiet_NaN();
  Require(!BuildHeightmapTerrain(desc, heights, 2, 2, mesh, &error), "NaN elevation accepted");
  desc.height_scale = 10.0f;
  TempSceneFiles files;
  const auto imagePath = files.Add("_heightmap.bmp");
  std::array<unsigned char, 70> image{};
  image[0] = 'B'; image[1] = 'M'; image[2] = 70; image[10] = 54;
  image[14] = 40; image[18] = 2; image[22] = 2; image[26] = 1; image[28] = 24;
  for (size_t offset : {57u, 58u, 59u, 65u, 66u, 67u}) image[offset] = 255;
  {
    std::ofstream stream(imagePath, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(image.data()), image.size());
  }
  desc.image = imagePath.string();
  MutableMeshSnapshot decoded;
  Require(LoadHeightmapTerrain(desc, decoded, &error), error);
  Require(decoded.vertices[4].position.y == mesh.vertices[4].position.y,
      "image decoder and normalized sample generator disagree");
  const auto path = files.Add("_heightmap.t8scene");
  scene::EditorSceneFile source;
  source.objects.emplace_back();
  source.objects.back().name = "Terrain";
  source.objects.back().heightmap = desc;
  Require(scene::SaveEditorSceneFile(source, path.string(), &error), error);
  scene::EditorSceneFile loaded;
  Require(scene::LoadEditorSceneFile(path.string(), loaded, &error), error);
  Require(loaded.objects.size() == 1 && loaded.objects[0].heightmap &&
    loaded.objects[0].heightmap->size_x == 8.0f && loaded.objects[0].mesh.empty(),
    "heightmap scene descriptor did not round trip");
}

void TestHeightmapNavigationExclusion() {
  scene::SceneHeightmapDesc desc;
  desc.samples_x = 33;
  desc.samples_z = 33;
  desc.size_x = 32;
  desc.size_z = 32;
  const std::array<float, 4> heights{};
  MutableMeshSnapshot mesh;
  std::string error;
  Require(BuildHeightmapTerrain(desc, heights, 2, 2, mesh, &error), error);
  auto database = BuildMeshDatabase(mesh, &error);
  Require(database != nullptr, error);
  navigation::NavMeshGeometry geometry;
  Require(navigation::BuildGeometryFromXDataBase(*database, geometry, &error), error);
  scene::SceneNavMeshVolumeDesc zone;
  zone.type = "exclude";
  zone.position = {16.0f, 0.0f, 16.0f};
  zone.half_extents = {4.0f, 4.0f, 4.0f};
  geometry.volumeModifiers.push_back(scene::NavVolumeModifierFromScene(zone));
  navigation::NavMeshBuildSettings settings;
  settings.regionMinSize = 1;
  settings.regionMergeSize = 2;
  navigation::NavMesh navMesh;
  Require(navMesh.Build(geometry, settings, &error), error);
  XVECTOR3 projected;
  Require(navMesh.ProjectPoint(XVECTOR3(4.0f, 0.0f, 4.0f), projected, XVECTOR3(0.5f, 2.0f, 0.5f), &error),
    "walkable terrain has no navmesh");
  Require(!navMesh.ProjectPoint(XVECTOR3(16.0f, 0.0f, 16.0f), projected, XVECTOR3(0.5f, 2.0f, 0.5f), &error),
    "excluded terrain remained walkable");
  std::vector<XVECTOR3> path;
  Require(navMesh.FindPath(XVECTOR3(4.0f, 0.0f, 16.0f), XVECTOR3(28.0f, 0.0f, 16.0f), path, &error) && path.size() > 2,
    "navigation did not route around the exclusion zone");
}

void TestSceneLoadIsolation() {
  TempSceneFiles files;
  const auto emptyPath = files.Add("_empty.t8scene");
  const auto invalidPath = files.Add("_invalid.t8scene");
  {
    std::ofstream stream(emptyPath);
    stream << "{\"version\":1}";
  }
  {
    std::ofstream stream(invalidPath);
    stream << "{\"version\":999,\"objects\":[";
  }
  scene::EditorSceneFile document;
  document.render_graph = "previous-graph";
  document.objects.emplace_back();
  document.objects.back().heightmap = scene::SceneHeightmapDesc{};
  std::string error;
  Require(scene::LoadEditorSceneFile(emptyPath.string(), document, &error), error);
  Require(document.objects.empty() && document.render_graph.empty(), "loading a scene retained data from the previous file");
  Require(!scene::LoadEditorSceneFile(invalidPath.string(), document, &error), "malformed scene accepted");
  Require(document.version == 1 && document.objects.empty(), "failed scene load partially mutated the destination");
}

void TestTerrainEditing() {
  scene::SceneHeightmapDesc terrain;
  terrain.samples_x = terrain.samples_z = 5;
  terrain.size_x = terrain.size_z = 4;
  terrain.height_offset = 0;
  TerrainBrush brush;
  brush.x = brush.z = 2;
  brush.radius = 1.5f;
  bool changed = false;
  std::string error;
  Require(ApplyTerrainBrush(terrain, brush, &changed, &error) && changed, error);
  Require(terrain.elevations[12] == 1.0f && terrain.elevations[0] == 0.0f, "brush footprint is incorrect");
  const auto raised = terrain;
  brush.mode = TerrainBrushMode::Lower;
  Require(ApplyTerrainBrush(terrain, brush, &changed, &error), error);
  Require(terrain.elevations[12] == 0.0f, "lower brush did not reverse raise");
  terrain = raised;
  brush.mode = TerrainBrushMode::Smooth;
  Require(ApplyTerrainBrush(terrain, brush, &changed, &error) && terrain.elevations[12] < 1.0f, "smooth failed");
  brush.mode = TerrainBrushMode::Flatten;
  brush.targetHeight = -2.0f;
  Require(ApplyTerrainBrush(terrain, brush, &changed, &error) && terrain.elevations[12] == -2.0f, "flatten failed");
  terrain.materials.resize(2);
  terrain.materials[1].color = {0.8f, 0.1f, 0.1f};
  brush.mode = TerrainBrushMode::Material;
  brush.material = 1;
  Require(ApplyTerrainBrush(terrain, brush, &changed, &error) && changed, "material brush failed");
  MutableMeshSnapshot mesh;
  Require(LoadHeightmapTerrain(terrain, mesh, &error) && mesh.sections.size() == 2, "paint did not create material sections");
  MutableMeshSnapshot reduced;
  Require(BuildTerrainLod(terrain, 1, reduced, &error) && reduced.vertices.size() < mesh.vertices.size(), "terrain LOD did not simplify");
  Require(reduced.localBounds.vMax.x == mesh.localBounds.vMax.x && reduced.localBounds.vMax.z == mesh.localBounds.vMax.z,
      "terrain LOD changed its extent");
  Require(SelectTerrainLod(10, 20, 4) == 0 && SelectTerrainLod(85, 20, 4) == 3, "terrain LOD selection failed");
  TempSceneFiles files;
  scene::EditorSceneFile document;
  document.objects.emplace_back();
  document.objects.back().heightmap = terrain;
  const auto path = files.Add("_terrain_edits.t8scene");
  Require(scene::SaveEditorSceneFile(document, path.string(), &error), error);
  scene::EditorSceneFile loaded;
  Require(scene::LoadEditorSceneFile(path.string(), loaded, &error), error);
  Require(loaded.objects[0].heightmap->elevations == terrain.elevations &&
      loaded.objects[0].heightmap->cell_materials == terrain.cell_materials, "terrain edits did not round trip");
  const auto before = terrain;
  brush.radius = 0;
  Require(!ApplyTerrainBrush(terrain, brush, &changed, &error) && terrain.elevations == before.elevations,
      "invalid brush mutated terrain");
}

void TestSceneRegions() {
  scene::SceneRegionDesc region;
  region.id = "region-test";
  region.half_extents = {4.0f, 2.0f, 1.0f};
  region.rotation.y = 90.0f;
  region.tags = {"objective", "buildable"};
  std::vector<scene::SceneRegionDesc> regions{region};
  Require(scene::RegionContainsPoint(region, XVECTOR3(0.0f, 0.0f, 3.0f)), "rotated region rejected inside point");
  Require(!scene::RegionContainsPoint(region, XVECTOR3(3.0f, 0.0f, 0.0f)), "rotated region accepted outside point");
  Require(scene::QuerySceneRegions(regions, XVECTOR3(0.0f, 0.0f, 0.0f), "objective").size() == 1,
      "tagged region query failed");
  regions.push_back(region);
  Require(!scene::ValidateSceneRegions(regions), "duplicate region IDs accepted");
  regions.pop_back();
  regions[0].enabled = false;
  Require(scene::QuerySceneRegions(regions, XVECTOR3(0.0f, 0.0f, 0.0f)).empty(), "disabled region remained active");
  scene::EditorSceneFile document;
  document.regions = {region};
  EngineContext context;
  GameLogicSystem game;
  game.Initialize(context, {});
  Require(game.LoadFromScene(document, {}), "runtime region loading failed");
  Require(game.RegionsAt(XVECTOR3(0.0f, 0.0f, 0.0f), "objective").size() == 1, "gameplay cannot query regions");
  game.Shutdown();
  Require(game.RegionsAt(XVECTOR3(0.0f, 0.0f, 0.0f)).empty(), "region state survived world shutdown");
}

void TestTerrain16BitImage() {
  const unsigned char png[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x07, 0x4D, 0x8E,
    0xBB, 0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x68, 0x60, 0x68, 0x60,
    0x64, 0x60, 0x60, 0xF8, 0xFF, 0x1F, 0x00, 0x0B, 0x0D, 0x03, 0x00, 0x69, 0x23, 0x82, 0x3C, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
  };
  TempSceneFiles files;
  const auto path = files.Add("_precision16.png");
  {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(png), sizeof(png));
  }
  scene::SceneHeightmapDesc terrain;
  terrain.image = path.string();
  terrain.samples_x = terrain.samples_z = 2;
  terrain.height_scale = 65535;
  MutableMeshSnapshot mesh;
  std::string error;
  Require(LoadHeightmapTerrain(terrain, mesh, &error), error);
  Require(std::abs(mesh.vertices[0].position.y - 32768.0f) < 0.01f &&
      std::abs(mesh.vertices[1].position.y - 32769.0f) < 0.01f &&
      mesh.vertices[2].position.y == 0.0f && mesh.vertices[3].position.y == 65535.0f,
      "native 16-bit elevations were quantized or flipped");
}

void TestTerrainPlacementGrid() {
  scene::SceneHeightmapDesc terrain;
  terrain.samples_x = terrain.samples_z = 9;
  terrain.size_x = terrain.size_z = 8;
  terrain.placement_grid.enabled = true;
  Require(InitializeTerrainEditing(terrain), "cannot prepare flat terrain");
  uint32_t columns = 0, rows = 0;
  Require(TerrainGridDimensions(terrain, columns, rows) && columns == 4 && rows == 4, "grid does not use authored world-cell size");
  scene::SceneTerrainPlacementDesc building;
  building.id = "building-1";
  Require(AddTerrainPlacement(terrain, building), "flat 2x2 building rejected");
  auto second = building;
  second.id = "building-2";
  Require(!AddTerrainPlacement(terrain, second), "overlap accepted");
  second.cell_x = 2;
  Require(AddTerrainPlacement(terrain, second), "adjacent footprint rejected");
    MutableMeshSnapshot mesh;
    Require(LoadHeightmapTerrain(terrain, mesh) && mesh.vertices.size() == 81 + 48 && mesh.indices.size() == 384 + 72,
      "building blockout boxes were not generated");
    MutableMeshSnapshot reduced;
    Require(BuildTerrainLod(terrain, 1, reduced) && reduced.vertices.size() == 25 + 48,
      "terrain LOD removed or simplified placed buildings");
    TempSceneFiles files;
    const auto path = files.Add("_placement.t8scene");
    scene::EditorSceneFile document;
    document.objects.emplace_back();
    document.objects[0].heightmap = terrain;
    Require(scene::SaveEditorSceneFile(document, path.string()), "placement save failed");
    scene::EditorSceneFile loaded;
    Require(scene::LoadEditorSceneFile(path.string(), loaded) && loaded.objects[0].heightmap->placements.size() == 2 &&
      loaded.objects[0].heightmap->placements[0].id == building.id, "placement identity did not survive reload");
  second.cell_x = 3;
  Require(!CheckTerrainPlacement(terrain, second).allowed, "out-of-bounds footprint accepted");
  Require(RemoveTerrainPlacement(terrain, building.id), "placement deletion failed");
  terrain.elevations[2 * 9 + 2] = 0.5f;
  Require(!CheckTerrainPlacement(terrain, building).allowed, "interior hill accepted under a building");
  building.kind = "unit";
  building.width = building.depth = 1;
  Require(CheckTerrainPlacement(terrain, building).allowed, "unit marker incorrectly requires flat building ground");
  terrain.placement_grid.cell_size = 0;
  Require(!TerrainGridDimensions(terrain, columns, rows), "zero cell size accepted");
}

void TestPlacementVisualFitting() {
  scene::SceneHeightmapDesc terrain;
  terrain.samples_x = terrain.samples_z = 5;
  terrain.size_x = terrain.size_z = 8;
  terrain.height_offset = 7;
  terrain.placement_grid.enabled = true;
  Require(InitializeTerrainEditing(terrain), "cannot prepare model-fit terrain");
  scene::SceneTerrainPlacementDesc placement;
  placement.id = "model-fit";
  placement.width = 2;
  placement.depth = 1;
  placement.height = 3;
  placement.visual = scene::ScenePlacementVisualDesc{};
  placement.visual->mesh = "Models/building.glb";
  placement.visual->hidden_geometry = {1};
  placement.visual->animation = "Idle";
  placement.visual->animate = false;
  placement.visual->loop = false;
  placement.visual->animation_speed = 0.75f;
  const AABB bounds(XVECTOR3(-10.0f, -2.0f, 4.0f), XVECTOR3(10.0f, 8.0f, 14.0f));
  for (float yaw : {0.0f, 90.0f, 37.0f}) {
    placement.visual->yaw_degrees = yaw;
    XMATRIX44 transform;
    Require(FitPlacementVisual(terrain, placement, bounds, transform), "model fitting failed");
    const auto fitted = bounds.Transformed(transform);
    Require(fitted.vMin.x >= -0.0001f && fitted.vMax.x <= 4.0001f &&
        fitted.vMin.z >= -0.0001f && fitted.vMax.z <= 2.0001f &&
        std::abs(fitted.vMin.y - 7.0f) < 0.0001f && fitted.vMax.y <= 10.0001f,
        "model fit escaped its footprint or was not bottom-aligned");
  }
  XMATRIX44 unused;
  Require(!FitPlacementVisual(terrain, placement, AABB{}, unused), "empty model bounds accepted");
  TempSceneFiles files;
  scene::EditorSceneFile document;
  document.objects.emplace_back();
  document.objects[0].heightmap = terrain;
  document.objects[0].heightmap->placements.push_back(placement);
  const auto path = files.Add("_placement_visual.t8scene");
  Require(scene::SaveEditorSceneFile(document, path.string()), "visual descriptor save failed");
  scene::EditorSceneFile loaded;
  Require(scene::LoadEditorSceneFile(path.string(), loaded) &&
      loaded.objects[0].heightmap->placements[0].visual->mesh == placement.visual->mesh,
      "visual asset reference did not round trip");
    const auto& visual = *loaded.objects[0].heightmap->placements[0].visual;
    Require(visual.hidden_geometry == placement.visual->hidden_geometry && visual.animation == "Idle" &&
      !visual.animate && !visual.loop && visual.animation_speed == 0.75f && visual.yaw_degrees == 37.0f,
      "placement visual parts or playback settings did not round trip");
    placement.visual->animation_speed = -1;
    Require(!CheckTerrainPlacement(terrain, placement).allowed, "negative placement animation speed accepted");
}

void TestLegacyShadowSampling() {
  SceneProps props;
  props.ShadowMapResolution = 2048;
  props.ShadowBias = 0.000005f;
  props.ShadowMin = 0.2f;
  RenderQuad quad;
  XMATRIX44 legacyLightVP;
  XMatTranslation(legacyLightVP, 4.0f, 5.0f, 6.0f);
  quad.CnstBuffer.WVPLight = legacyLightVP;

  const auto requireLegacyPayload = [&]() {
    const auto& payload = quad.ShadowSamplingCB;
    Require(payload.Params0.x == 1.0f && payload.Params0.y == 2048.0f &&
        payload.Params0.z == 2048.0f, "legacy shadow view or dimensions missing");
    Require(std::memcmp(&payload.ViewProjection[0], &legacyLightVP, sizeof(legacyLightVP)) == 0,
        "legacy light matrix was not preserved");
    Require(payload.AtlasScaleBias[0].x == 1.0f && payload.AtlasScaleBias[0].y == 1.0f &&
        payload.AtlasScaleBias[0].z == 0.0f && payload.AtlasScaleBias[0].w == 0.0f,
        "legacy shadow does not cover the whole texture");
    Require(payload.Params1.z == props.ShadowBias && payload.Params1.w == props.ShadowMin,
        "legacy shadow bias or minimum light lost");
    XMATRIX44 emptyMatrix;
    std::memset(&emptyMatrix, 0, sizeof(emptyMatrix));
    for (int view = 1; view < kMaxShadowViewsPerProjection; ++view) {
      Require(std::memcmp(&payload.ViewProjection[view], &emptyMatrix, sizeof(emptyMatrix)) == 0,
          "stale cascade matrix survived legacy fallback");
    }
    Require(payload.SplitDepths[0].x == 0.0f, "stale cascade boundary survived legacy fallback");
  };

  quad.UploadShadowSamplingCB(props);
  requireLegacyPayload();

  auto& projection = props.Shadows.projections.emplace_back();
  projection.resolvedDesc.technique = "csm";
  projection.viewCount = 2;
  projection.atlasWidth = 4096;
  projection.atlasHeight = 2048;
  projection.splitBoundaries[0] = 40.0f;
  XMatIdentity(projection.views[0].viewProjection);
  projection.views[1].viewProjection = legacyLightVP;
  projection.views[0].atlasScaleBias = {0.5f, 1.0f, 0.0f, 0.0f};
  projection.views[1].atlasScaleBias = {0.5f, 1.0f, 0.5f, 0.0f};
  quad.UploadShadowSamplingCB(props);
  Require(quad.ShadowSamplingCB.Params0.x == 2.0f &&
      quad.ShadowSamplingCB.Params0.y == 4096.0f &&
      quad.ShadowSamplingCB.SplitDepths[0].x == 40.0f &&
      quad.ShadowSamplingCB.AtlasScaleBias[1].z == 0.5f &&
      std::memcmp(&quad.ShadowSamplingCB.ViewProjection[1], &legacyLightVP, sizeof(legacyLightVP)) == 0,
      "explicit cascade sampling payload changed");

  props.Shadows.Reset();
  quad.CnstBuffer.WVPLight = legacyLightVP;
  quad.UploadShadowSamplingCB(props);
  requireLegacyPayload();
}

void TestShaderFlowConfiguration() {
  const auto parse = [](Config& config, std::vector<std::string> arguments) {
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    config::ApplyCommandLine(static_cast<int>(pointers.size()), pointers.data(), config);
  };
  Config defaults;
  Require(defaults.webgpuShaderFlow == "auto", "WebGPU shader flow must default to auto");
  for (const auto* mode : {"auto", "wgsl", "spirv"}) {
    Config selected;
    selected.api = "d3d12";
    config::RuntimeConfigJson json;
    json.webgpuShaderFlow = "wgsl";
    config::ApplyConfigJson(json, selected);
    Require(selected.webgpuShaderFlow == "wgsl", "Shader flow JSON setting ignored");
    parse(selected, {"DayScene", "--shaderFlow", mode, "--width", "640"});
    Require(config::ValidateConfig(selected), "Valid shader flow config rejected");
    Require(selected.webgpuShaderFlow == mode && selected.width == 640 && selected.api == "d3d12",
            "Shader flow override changed API or consumed another option");
  }
  parse(defaults, {"DayScene", "--shaderFlow", "SPIRV", "--shaderFlow", "WGSL"});
  Require(defaults.webgpuShaderFlow == "wgsl", "Shader flow case normalization or last override failed");
  for (const auto& arguments : std::vector<std::vector<std::string>>{
         {"DayScene", "--shaderFlow"}, {"DayScene", "--shaderFlow", "--api", "webgpu"},
         {"DayScene", "--shaderFlow", "invalid"}, {"DayScene", "--shaderFlow", ""}}) {
    bool rejected = false;
    try { parse(defaults, arguments); } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected, "Invalid or missing shader flow silently accepted");
  }
  defaults.webgpuShaderFlow = "invalid";
  bool rejected = false;
  try { config::ValidateConfig(defaults); } catch (const std::invalid_argument&) { rejected = true; }
  Require(rejected, "Invalid configured shader flow silently defaulted");
}

class NullTestDriver final : public BaseDriver {
public:
  std::vector<std::string> events;
  std::vector<ComputePipelineDesc> computePipelines;
  void InitDriver() override {}
  void CreateSurfaces() override {}
  void DestroySurfaces() override {}
  void Update() override {}
  void DestroyDriver() override {}
  void SetWindow(void*) override {}
  void SetDimensions(int, int) override {}
  void Clear() override {}
  void SwapBuffers() override {}
  void SetBlendState(BlendStates) override {}
  void SetDepthStencilState(DepthStencilStates) override {}
  void SaveScreenshot(std::string) override {}
  void SetCullFace(FaceCulling) override {}
  void PopRT() override {}
  void FlushGPUResources() override { events.push_back("flush"); }
  bool SupportsComputeShaders() const override { return true; }
  std::unique_ptr<ComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc) override {
    class NullComputePipeline final : public ComputePipeline {};
    computePipelines.push_back(desc);
    return std::make_unique<NullComputePipeline>();
  }
};

class LifecycleTestScene final : public SceneBase {
public:
  explicit LifecycleTestScene(std::vector<std::string>& events) : events(events) {}
  void OnUpdate(float) override {}
  void OnDraw() override {}
  void OnInput(InputManager*) override {}
  void OnLoadScene() override { events.push_back("load"); }
  void OnDestoryScene() override { events.push_back("destroy"); }
  void InitVars() override {}
  void CreateAssets() override {}
  void DestroyAssets() override {}
private:
  std::vector<std::string>& events;
};

class LifecycleTestFramework final : public RootFramework {
public:
  LifecycleTestFramework() : RootFramework(nullptr) {}
  void InitGlobalVars() override {}
  void OnCreateApplication(ApplicationDesc) override {}
  void OnDestroyApplication() override {}
  void OnInterruptApplication() override {}
  void OnResumeApplication() override {}
  void UpdateApplication() override {}
  void ProcessInput() override {}
  void ResetApplication() override {}
  void ChangeAPI(GraphicsApi::E) override {}
};

void TestSceneRuntimeOwnership() {
  TempSceneFiles files;
  const auto scenePath = files.Add("_policy.t8scene");
  const auto descriptorPath = files.Add("_policy.json");
  scene::EditorSceneFile authored;
  authored.mouse_capture = false;
  authored.runtime_setup = SceneDescriptor{};
  authored.runtime_setup->cameras.push_back(CameraDesc{});
  authored.runtime_setup->cameras.front().position = {3, 4, 5};
  Require(scene::SaveEditorSceneFile(authored, scenePath.string()), "cannot save scene input policy");
  scene::EditorSceneFile loaded;
  Require(scene::LoadEditorSceneFile(scenePath.string(), loaded) && loaded.mouse_capture == false,
          "scene input policy did not survive round trip");
  SceneDescriptor descriptor;
  descriptor.runtime_scene = scenePath.string();
  Require(SaveSceneDescriptor(descriptorPath.string(), descriptor), "cannot save descriptor policy reference");
  SceneSetup setup;
  Require(loaded.runtime_setup && setup.Load(*loaded.runtime_setup) && setup.GetCamera()->Eye.x == 3,
      "embedded runtime setup did not round-trip or construct authored camera");
  Require(setup.Load(descriptorPath.string()), "cannot load authored runtime policy");
  NullTestDriver driver;
  LifecycleTestScene runtime(driver.events);
  setup.ApplyInputSettings(runtime.SceneProp);
  Require(!runtime.AllowsMouseCapture(), "scene ignored authored capture policy");
  setup.ApplyInputSettings(runtime.SceneProp, true);
  Require(runtime.AllowsMouseCapture(), "explicit scene policy did not override descriptor default");
  authored.mouse_capture = true;
  Require(scene::SaveEditorSceneFile(authored, scenePath.string()) && setup.Load(descriptorPath.string()),
          "cannot reload changed input policy");
  setup.ApplyInputSettings(runtime.SceneProp);
  Require(runtime.AllowsMouseCapture(), "scene hardcoded capture instead of loading updated data");
  authored.mouse_capture.reset();
  Require(scene::SaveEditorSceneFile(authored, scenePath.string()) && setup.Load(descriptorPath.string()),
          "legacy scene did not load");
  setup.ApplyInputSettings(runtime.SceneProp);
  Require(runtime.AllowsMouseCapture(), "legacy scene retained stale input policy");
  LifecycleTestFramework framework;
  framework.pVideoDriver = &driver;
  framework.UnloadScene(runtime);
  Require(driver.events == std::vector<std::string>{"flush", "destroy"}, "GPU drain must precede scene destruction");
}

void TestAuthoredStreamedVoxels() {
  TempSceneFiles files;
  const auto path = files.Add("_voxels.t8scene");
  scene::EditorSceneFile authored;
  authored.streamed_voxels.emplace();
  auto& fixture = *authored.streamed_voxels;
  fixture.chunk_dimensions = {16, 16, 16};
  fixture.terrain = {3, 3, 13, 7, 2};
  fixture.palette = {{"stone"}, {"dirt"}, {"grass"}};
  fixture.deep_block = "stone";
  fixture.fill_block = "dirt";
  fixture.surface_block = "grass";
  fixture.edits_path = "VoxelWorlds/test/edits.t8vox";
  fixture.interaction_reach = 8;
  fixture.atlas_width = fixture.atlas_height = 1;
  fixture.atlas_rgba = {255, 255, 255, 255};
  Require(scene::SaveEditorSceneFile(authored, path.string()) &&
          scene::LoadEditorSceneFile(path.string(), authored) && authored.streamed_voxels,
          "cannot load authored streamed voxel asset");
  auto data = *authored.streamed_voxels;
  terrain::BlockRegistry registry;
  scene::BuildStreamedVoxelPalette(data, registry);
  Require(registry.Find("stone") == 1 && registry.Find("dirt") == 2 && registry.Find("grass") == 3,
          "authored palette changed persisted block IDs");
  terrain::VoxelChunkBuildRequest request;
  request.key = {-1, 0, -1};
  request.dimensions = data.chunk_dimensions;
  auto chunk = terrain::GenerateLayeredVoxelChunk(request, data.terrain, 3, 2, 1);
  Require(chunk && chunk->Get(0, 0, 0) == 1 && chunk->Get(0, 3, 0) == 3 && chunk->Get(0, 4, 0) == 0,
          "layered generator changed negative-coordinate terrain");
  request.cancelled = std::make_shared<std::atomic_bool>(true);
  Require(!terrain::GenerateLayeredVoxelChunk(request, data.terrain, 3, 2, 1), "generator ignored cancellation");
  for (int invalidCase = 0; invalidCase < 4; ++invalidCase) {
    auto invalid = data;
    if (invalidCase == 0) invalid.atlas_rgba.pop_back();
    if (invalidCase == 1) invalid.palette.push_back(invalid.palette.front());
    if (invalidCase == 2) invalid.surface_block = "unknown";
    if (invalidCase == 3) invalid.edits_path = "../outside.t8vox";
    bool rejected = false;
    try { scene::BuildStreamedVoxelPalette(invalid, registry); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && registry.Count() == 4, "invalid voxel data was accepted or partially replaced palette");
  }
  terrain::VoxelStreamingManager streaming;
  streaming.Reset(data.chunk_dimensions);
  auto settings = data.streaming;
  settings.horizontalRadius = 0;
  streaming.SetSettings(settings);
  bool receivedDimensions = false;
  const auto build = [&](const terrain::VoxelChunkBuildRequest& job) {
    receivedDimensions = job.dimensions.y == data.chunk_dimensions.y;
    terrain::VoxelChunkBuildResult result;
    result.key = job.key;
    result.epoch = job.epoch;
    result.chunk = terrain::GenerateLayeredVoxelChunk(job, data.terrain, 3, 2, 1);
    return result;
  };
  streaming.Update({}, {}, nullptr, build);
  Require(receivedDimensions, "streaming reset retained stale chunk dimensions");
}

void TestShaderPrecompilerContract() {
  TempSceneFiles files;
  const auto manifest = files.Add("_permutations.json");
  const auto computeSourceDirectory = files.Add("_compute_sources");
  std::filesystem::create_directories(computeSourceDirectory);
  {
    std::ofstream output(computeSourceDirectory / "CS_Arithmetic.hlsl");
    output << "[numthreads(1, 1, 1)] void CS() {}\n";
  }
  {
    std::ofstream output(manifest);
    output << R"({"version":2,"permutations":{"0x0000000000000001":{"vertexShader":"missing.vs","fragmentShader":"missing.fs"},"0x0000000000000002":{"vertexShader":"missing.vs","fragmentShader":"missing.fs"}},"compute_permutations":{"CS_Arithmetic.hlsl:CS:base":{"key":"CS_Arithmetic.hlsl:CS:base","kind":"compute","computeShader":"CS_Arithmetic.hlsl","entryPoint":"CS","permutation":"base","defines":[]}}})";
  }
  NullTestDriver driver;
  ShaderPrecompileRequest request;
  request.manifestPath = manifest.string();
  request.sourceDirectory = computeSourceDirectory.string();
  size_t reports = 0;
  request.onProgress = [&](const ShaderPrecompileProgress& progress) {
    ++reports;
    Require(progress.completed == reports && progress.total == 3,
            "precompiler reported an invalid permutation total");
    if (progress.key.starts_with("0x"))
      Require(!progress.error.empty(), "precompiler omitted graphics-entry diagnostics");
    else
      Require(progress.error.empty(), "precompiler rejected a registered compute entry");
  };
  auto result = PrecompileShaders(driver, request);
  Require(!result.Succeeded() && result.failed == 2 && result.succeeded == 1 && reports == 3 &&
          driver.computePipelines.size() == 1 &&
          driver.computePipelines.front().debugName.ends_with("CS_Arithmetic.hlsl") &&
          driver.computePipelines.front().bindings.size() == 2 &&
          driver.computePipelines.front().bindings.front().constantCount == 4,
          "precompiler did not compile the registered compute permutation");
  reports = 0;
  request.cancelRequested = [&] { return reports == 1; };
  result = PrecompileShaders(driver, request);
  Require(result.cancelled && result.failed == 1 && reports == 1, "precompiler did not stop between permutations");
  request.manifestPath = files.Add("_missing.json").string();
  bool rejected = false;
  try { PrecompileShaders(driver, request); } catch (const std::runtime_error&) { rejected = true; }
  Require(rejected, "precompiler accepted a missing manifest");

    const auto recorded = files.Add("_recorded.json").string();
    ShaderPermutationDump::Begin(recorded);
    ShaderKey key;
    key.bits = 1;
    ShaderPermutationDump::Record(key, "quoted\"vertex.hlsl", "fragment.hlsl", "#define FORWARD_PASS\n");
    Require(ShaderPermutationDump::Flush(), "cannot flush recorded manifest");
    std::string original;
    Require(ResourceLocator::Instance().ReadText(recorded, original), "cannot read recorded manifest");
    ShaderPermutationDump::Begin(recorded);
    key.bits = 2;
    ShaderPermutationDump::Record(key, "second.hlsl", "fragment.hlsl", "");
    Require(ShaderPermutationDump::Flush(), "cannot merge recorded manifest");
    std::string merged;
    Require(ResourceLocator::Instance().ReadText(recorded, merged) &&
      merged.find("0x0000000000000001") != std::string::npos &&
      merged.find("0x0000000000000002") != std::string::npos,
      "recording discarded earlier permutations");
    Require(ResourceLocator::Instance().WriteText(recorded, "{broken"), "cannot prepare malformed manifest");
    ShaderPermutationDump::Begin(recorded);
    const bool rejectedMerge = !ShaderPermutationDump::Flush();
    std::string unchanged;
    const bool preserved = ResourceLocator::Instance().ReadText(recorded, unchanged) && unchanged == "{broken";
    Require(ResourceLocator::Instance().WriteText(recorded, original) && ShaderPermutationDump::Flush(),
      "cannot recover recorder after failed merge");
    Require(rejectedMerge && preserved, "recorder overwrote malformed input");
}

void TestTextureMipmaps() {
  Require(CalculateFullMipCount(1, 1) == 1 && CalculateFullMipCount(5, 3) == 3, "mip count mismatch");
  std::vector<unsigned char> output;
  const std::array<unsigned char, 16> alphaPixels{255, 0, 0, 255, 0, 255, 0, 0, 0, 0, 255, 0, 255, 255, 255, 0};
  GenerateMipChain8(alphaPixels.data(), 2, 2, 1, 4, output);
  Require(output.size() == 20 && output[16] == 255 && output[17] == 0 && output[18] == 0 && output[19] == 64,
          "alpha-weighted mip filtering changed");
  const std::array<unsigned char, 3> column{10, 30, 200};
  GenerateMipChain8(column.data(), 1, 3, 1, 1, output);
  Require(output.size() == 4 && output[3] == 20, "odd single-column mip policy changed");
  std::vector<unsigned char> faces(5 * 3 * 6);
  for (unsigned face = 0; face < 6; ++face) std::fill_n(faces.begin() + face * 15, 15, static_cast<unsigned char>(face * 31));
  GenerateMipChain8(faces.data(), 5, 3, 6, 1, output);
  Require(output.size() == 18 * 6, "cube mip chain size mismatch");
  for (unsigned face = 0; face < 6; ++face)
    for (unsigned pixel = 0; pixel < 18; ++pixel) Require(output[face * 18 + pixel] == face * 31, "mip generation mixed cube faces");
}

void TestTypedComputeGraphValidation() {
  TempSceneFiles files;
  constexpr std::array<const char*, 8> maintainedGraphs = {
    "Scenes/DayScene_RenderGraph.json",
    "Scenes/ForwardScene_RenderGraph.json",
    "Scenes/MinecraftScene_RenderGraph.json",
    "Scenes/Quake3Mock_RenderGraph.json",
    "Scenes/RagdollEditor_RenderGraph.json",
    "Scenes/SandboxScene_RenderGraph.json",
    "Scenes/SceneTemplate_RenderGraph.json",
    "Scenes/T8ditor_RenderGraph.json"
  };
  for (const char* path : maintainedGraphs) {
    RenderGraph maintained;
    Require(maintained.Load(path),
            std::string("maintained render graph failed strict validation: ") + path);
  }

  const std::string validGraph = R"({
    "render_targets": [
      {"name":"Input","color_count":1,"color_format":"RGBA8","depth_format":"NONE","size":[7,5]},
      {"name":"Output","color_count":1,"color_format":"RGBA8","depth_format":"NONE","size":[7,5],"storage":true}
    ],
    "passes": [{
      "name":"Typed Blur","target":"Output","execution":"compute_if_supported",
      "compute_shader":"Shaders/CS_Blur.hlsl","compute_entry":"CS",
      "compute_permutation":"horizontal","compute_extent_from":"Output:COLOR0",
      "compute_resources":[
        {"resource":"@kernel_constants","access":"constants","shader_register":0},
        {"resource":"Input:COLOR0","access":"sampled","shader_register":0},
        {"resource":"Input:COLOR0","access":"sampler","shader_register":0},
        {"resource":"Output:COLOR0","access":"storage_write","shader_register":0}
      ],"draws":[]
    }]
  })";
  const auto writeGraph = [&](std::string_view suffix, const std::string& json) {
    const std::filesystem::path path = files.Add(suffix);
    std::ofstream output(path);
    output << json;
    output.close();
    return path;
  };
  const auto replaceOnce = [](std::string input,
                              std::string_view oldValue,
                              std::string_view newValue) {
    const size_t position = input.find(oldValue);
    Require(position != std::string::npos, "compute graph fixture mutation target missing");
    input.replace(position, oldValue.size(), newValue);
    return input;
  };

  RenderGraph graph;
  Require(graph.Load(writeGraph("_valid_compute_graph.json", validGraph).string()),
          "valid typed compute graph was rejected");

  RenderGraphDesc descriptor;
  const std::string unknownKey = replaceOnce(
    validGraph, "\"compute_extent_from\"", "\"compute_extent_typo\"");
  Require(!LoadRenderGraphDescriptor(
            writeGraph("_unknown_compute_key.json", unknownKey).string(), descriptor),
          "unknown compute graph key was ignored");

  const std::string noStorage = replaceOnce(validGraph, ",\"storage\":true", "");
  Require(!graph.Load(writeGraph("_compute_no_storage.json", noStorage).string()),
          "compute graph accepted a non-storage output");

  std::string feedback = replaceOnce(
    validGraph, "\"Input:COLOR0\",\"access\":\"sampled\"",
    "\"Output:COLOR0\",\"access\":\"sampled\"");
  feedback = replaceOnce(
    feedback, "\"Input:COLOR0\",\"access\":\"sampler\"",
    "\"Output:COLOR0\",\"access\":\"sampler\"");
  Require(!graph.Load(writeGraph("_compute_feedback.json", feedback).string()),
          "compute graph accepted read/write feedback");

  const std::string invalidPermutation = replaceOnce(
    validGraph, "\"compute_permutation\":\"horizontal\"",
    "\"compute_permutation\":\"diagonal\"");
  Require(!graph.Load(
            writeGraph("_compute_bad_permutation.json", invalidPermutation).string()),
          "compute graph accepted an unknown permutation");

  const std::string missingBinding = replaceOnce(
    validGraph,
    "        {\"resource\":\"Input:COLOR0\",\"access\":\"sampler\",\"shader_register\":0},\n",
    "");
  Require(!graph.Load(writeGraph("_compute_missing_binding.json", missingBinding).string()),
          "compute graph accepted an incomplete binding layout");
}

constexpr TestCase kTests[] = {
  {"T-VOXEL-AUTHORING-01", TestAuthoredStreamedVoxels},
  {"T-SCENE-RUNTIME-OWNERSHIP-01", TestSceneRuntimeOwnership},
  {"T-SHADER-PRECOMPILER-01", TestShaderPrecompilerContract},
  {"T-COMPUTE-GRAPH-01", TestTypedComputeGraphValidation},
  {"T-SHADER-FLOW-CONFIG-01", TestShaderFlowConfiguration},
  {"T-TEXTURE-MIPS-01", TestTextureMipmaps},
  {"T-SHADOW-LEGACY-01", TestLegacyShadowSampling},
  {"T-PLACEMENT-VISUAL-01", TestPlacementVisualFitting},
  {"T-PLACEMENT-01", TestTerrainPlacementGrid},
  {"T-TERRAIN-16BIT-01", TestTerrain16BitImage},
  {"T-REGION-01", TestSceneRegions},
  {"T-TERRAIN-EDIT-01", TestTerrainEditing},
  {"T-HEIGHTMAP-NAV-01", TestHeightmapNavigationExclusion},
    {"T-SCENE-ISOLATION-01", TestSceneLoadIsolation},
  {"T-HEIGHTMAP-01", TestHeightmapTerrain},
  {"T-SCENE-CONVERSIONS-01", TestSceneConversions},
    {"T-SCHEMA-01", TestSchemaRoundTrip},
    {"T-SCHEMA-02", TestMigrationIdsPersist},
    {"T-SCHEMA-02B", TestEnsureIdsForV2Authoring},
    {"T-SCHEMA-03", TestLegacyAiMigration},
    {"T-VALID-01", TestDuplicateEntityId},
    {"T-VALID-02", TestDuplicateComponentId},
    {"T-VALID-03", TestUnknownComponentWarning},
    {"T-EXTENSION-01", TestComponentRegistryValidation},
    {"T-VALID-04", TestMissingInitialState},
    {"T-GROUP-01", TestGroupStableIdsAfterRename},
    {"T-GROUP-02", TestGroupValidationReferences},
    {"T-REG-01", TestRegistryCreateFindDestroy},
    {"T-REG-03", TestRegistryOwnerPointersStayStable},
    {"T-REG-02", TestBrokenMeshLinkWarning},
    {"T-LIFE-01", TestGameLogicLifecycle},
    {"T-TICK-01", TestFixedTickCap},
    {"T-TICK-02", TestFixedTickPause},
    {"T-CTRL-01", TestControllerIntentsDiffer},
    {"T-COMP-01", TestComponentLifecycleOrder},
    {"T-COMP-02", TestDeferredComponentRemoval},
    {"T-EVENT-01", TestEventFifo},
    {"T-EVENT-02", TestHandlerPublishNextCycle},
    {"T-EVENT-03", TestDestroyRequestedInHandler},
    {"T-SM-01", TestStateMachineInitialState},
    {"T-SM-02", TestStateMachinePriorityAndOrder},
    {"T-SM-03", TestStateMachineCooldown},
    {"T-SM-04", TestStateMachineEventTiming},
    {"T-PHYS-01", TestPhysicsUnavailable},
    {"T-PHYS-02", TestPhysicsBodyHandleReuseRejectsStaleHandles},
    {"T-PHYS-03", TestGeneratedTriangleMeshPhysicsBody},
    {"T-MESH-01", TestMutableMeshValidationAndBounds},
    {"T-MESH-02", TestMutableMeshSectionValidation},
    {"T-MESH-03", TestRenderContainerStableHandles},
    {"T-ATLAS-01", TestTextureAtlasGridRegions},
    {"T-MAT-01", TestMaterialTextureVariantIsImmutable},
    {"T-VOXEL-01", TestVoxelChunkMutation},
    {"T-VOXEL-02", TestGreedyVoxelMesher},
    {"T-VOXEL-03", TestVoxelMesherUsesNeighborBoundary},
    {"T-VOXEL-04", TestVoxelWorldCoordinatesAndRaycast},
    {"T-VOXEL-05", TestVoxelStreamingBudgetsAndUnloads},
    {"T-VOXEL-06", TestVoxelDeltaPersistenceRoundTrip},
    {"T-VOXEL-07", TestVoxelNavigationAvoidsSolidsAndRejectsPartialPaths},
    {"T-VOXEL-08", TestVoxelCollisionPreventsTunneling},
    {"T-NAV-01", TestNavigationUnavailable},
};

} // namespace

int RunGameSelfTests() {
  int failures = 0;
  for (const TestCase& test : kTests) {
    try {
      test.function();
      std::cout << "PASS " << test.id << '\n';
    } catch (const std::exception& exception) {
      ++failures;
      std::cout << "FAIL " << test.id << ": " << exception.what() << '\n';
    } catch (...) {
      ++failures;
      std::cout << "FAIL " << test.id << ": unknown exception\n";
    }
  }
  return failures;
}

} // namespace t850::game