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
#include <scene/EditorSceneFile.h>
#include <scene/MutableMeshData.h>
#include <scene/RenderContainer.h>
#include <scene/MaterialAsset.h>
#include <terrain/BlockRegistry.h>
#include <terrain/VoxelChunk.h>
#include <terrain/VoxelMesher.h>
#include <terrain/VoxelWorld.h>
#include <terrain/VoxelStreaming.h>
#include <terrain/VoxelPersistence.h>
#include <terrain/VoxelNavigation.h>
#include <terrain/VoxelCollision.h>
#include <utils/ThreadPool.h>
#include <video/TextureAtlas.h>

#include <filesystem>
#include <fstream>
#include <iostream>
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
      std::filesystem::remove(path, error);
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

constexpr TestCase kTests[] = {
    {"T-SCHEMA-01", TestSchemaRoundTrip},
    {"T-SCHEMA-02", TestMigrationIdsPersist},
    {"T-SCHEMA-02B", TestEnsureIdsForV2Authoring},
    {"T-SCHEMA-03", TestLegacyAiMigration},
    {"T-VALID-01", TestDuplicateEntityId},
    {"T-VALID-02", TestDuplicateComponentId},
    {"T-VALID-03", TestUnknownComponentWarning},
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