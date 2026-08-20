#include <pch.h>

#include <game/GameValidation.h>

#include <game/GameIds.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <unordered_set>

namespace t850::scene {
namespace {

constexpr std::array<std::string_view, 12> kKnownComponentTypes = {
    "combat", "debug_name", "formation_slot", "health", "movement", "path_follow",
    "resource", "sensor", "state_machine", "status_effect", "transform", "weapon"};

void AddIssue(SceneValidationReport& report,
              SceneValidationSeverity severity,
              std::string code,
              std::string message,
              const SceneGameEntityDesc* entity = nullptr,
              const SceneComponentDesc* component = nullptr,
              int entityIndex = -1,
              const SceneGroupDesc* group = nullptr) {
  SceneValidationIssue issue;
  issue.severity = severity;
  issue.code = std::move(code);
  issue.message = std::move(message);
  issue.entityId = entity ? entity->id : std::string{};
  issue.componentId = component ? component->id : std::string{};
  issue.groupId = group ? group->id : std::string{};
  issue.entityIndex = entityIndex;
  report.issues.push_back(std::move(issue));
}

bool IsKnownComponentType(std::string_view type) {
  for (std::string_view known : kKnownComponentTypes) {
    if (known == type) return true;
  }
  return false;
}

bool IsValidFloat(std::string_view text) {
  if (text.empty()) return false;
  std::string value(text);
  char* end = nullptr;
  errno = 0;
  const float parsed = std::strtof(value.c_str(), &end);
  return errno != ERANGE && end == value.c_str() + value.size() && std::isfinite(parsed);
}

bool IsValidCondition(std::string_view condition) {
  if (condition == "always" || condition == "timer_elapsed") return true;

  constexpr std::string_view eventPrefix = "on_event:";
  if (condition.starts_with(eventPrefix)) {
    return condition.size() > eventPrefix.size();
  }

  constexpr std::string_view healthPrefix = "health_below:";
  if (condition.starts_with(healthPrefix)) {
    return IsValidFloat(condition.substr(healthPrefix.size()));
  }

  constexpr std::string_view parameterPrefix = "param_equals:";
  if (condition.starts_with(parameterPrefix)) {
    const std::string_view payload = condition.substr(parameterPrefix.size());
    const std::size_t separator = payload.find(':');
    return separator != std::string_view::npos && separator > 0 && separator + 1 < payload.size();
  }

  return false;
}

void AppendMigrationLog(std::string* log, std::string_view message) {
  if (!log) return;
  if (!log->empty()) log->push_back('\n');
  log->append(message);
}

} // namespace

bool SceneValidationReport::HasErrors() const {
  for (const SceneValidationIssue& issue : issues) {
    if (issue.severity == SceneValidationSeverity::Error) return true;
  }
  return false;
}

SceneValidationReport ValidateEditorSceneGameLogic(const EditorSceneFile& scene) {
  SceneValidationReport report;
  std::unordered_set<std::string> entityIds;
  std::unordered_set<std::string> objectNames;
  std::unordered_set<std::string> physicsNames;
  std::unordered_set<std::string> cameraNames;

  for (const SceneObjectDesc& object : scene.objects) {
    if (!object.name.empty()) objectNames.insert(object.name);
  }
  for (const ScenePhysicsEntityDesc& physics : scene.physics_entities) {
    if (!physics.name.empty()) physicsNames.insert(physics.name);
  }
  for (const SceneCameraDesc& camera : scene.cameras) {
    if (!camera.name.empty()) cameraNames.insert(camera.name);
  }

  for (int entityIndex = 0; entityIndex < static_cast<int>(scene.game_entities.size()); ++entityIndex) {
    const SceneGameEntityDesc& entity = scene.game_entities[static_cast<std::size_t>(entityIndex)];
    if (entity.id.empty()) {
      AddIssue(report, SceneValidationSeverity::Error, "game.missing_id",
               "Game entity is missing a stable id.", &entity, nullptr, entityIndex);
    } else if (!entityIds.insert(entity.id).second) {
      AddIssue(report, SceneValidationSeverity::Error, "game.dup_id",
               "Duplicate game entity id '" + entity.id + "'.", &entity, nullptr, entityIndex);
    }

    if (entity.control.mode != "none" && entity.control.mode != "player" && entity.control.mode != "ai") {
      AddIssue(report, SceneValidationSeverity::Error, "game.control.invalid_mode",
               "Invalid control mode '" + entity.control.mode + "'.", &entity, nullptr, entityIndex);
    }

    if (!entity.mesh_object.empty() && !objectNames.contains(entity.mesh_object)) {
      AddIssue(report, SceneValidationSeverity::Warning, "game.link.mesh_missing",
               "Mesh object '" + entity.mesh_object + "' was not found.", &entity, nullptr, entityIndex);
    }
    if (!entity.primary_physics_entity.empty() && !physicsNames.contains(entity.primary_physics_entity)) {
      AddIssue(report, SceneValidationSeverity::Warning, "game.link.physics_missing",
               "Primary physics entity '" + entity.primary_physics_entity + "' was not found.", &entity, nullptr, entityIndex);
    }
    for (const std::string& physicsEntity : entity.physics_entities) {
      if (!physicsEntity.empty() && !physicsNames.contains(physicsEntity)) {
        AddIssue(report, SceneValidationSeverity::Warning, "game.link.physics_missing",
                 "Physics entity '" + physicsEntity + "' was not found.", &entity, nullptr, entityIndex);
      }
    }
    if (!entity.camera.empty() && !cameraNames.contains(entity.camera)) {
      AddIssue(report, SceneValidationSeverity::Warning, "game.link.camera_missing",
               "Camera '" + entity.camera + "' was not found.", &entity, nullptr, entityIndex);
    }
    if (!entity.ragdoll_object.empty() && !objectNames.contains(entity.ragdoll_object)) {
      AddIssue(report, SceneValidationSeverity::Warning, "game.link.ragdoll_missing",
               "Ragdoll object '" + entity.ragdoll_object + "' was not found.", &entity, nullptr, entityIndex);
    }

    std::unordered_set<std::string> componentIds;
    for (const SceneComponentDesc& component : entity.components) {
      if (component.id.empty()) {
        AddIssue(report, SceneValidationSeverity::Error, "game.component.missing_id",
                 "Component is missing a stable id.", &entity, &component, entityIndex);
      } else if (!componentIds.insert(component.id).second) {
        AddIssue(report, SceneValidationSeverity::Error, "game.component.dup_id",
                 "Duplicate component id '" + component.id + "'.", &entity, &component, entityIndex);
      }
      if (component.type.empty()) {
        AddIssue(report, SceneValidationSeverity::Error, "game.component.empty_type",
                 "Component type must not be empty.", &entity, &component, entityIndex);
      } else if (!IsKnownComponentType(component.type)) {
        AddIssue(report, SceneValidationSeverity::Warning, "game.component.unknown_type",
                 "Unknown component type '" + component.type + "' will be preserved.", &entity, &component, entityIndex);
      }
    }

    if (entity.behavior.has_value()) {
      const SceneStateMachineDesc& behavior = *entity.behavior;
      std::unordered_set<std::string> stateNames;
      for (const SceneStateDesc& state : behavior.states) {
        if (!state.name.empty()) stateNames.insert(state.name);
      }
      if (!stateNames.contains(behavior.initial_state)) {
        AddIssue(report, SceneValidationSeverity::Error, "game.behavior.initial_missing",
                 "Initial state '" + behavior.initial_state + "' was not found.", &entity, nullptr, entityIndex);
      }
      for (const SceneTransitionDesc& transition : behavior.transitions) {
        if (transition.from_state != "*" && !stateNames.contains(transition.from_state)) {
          AddIssue(report, SceneValidationSeverity::Error, "game.behavior.from_missing",
                   "Transition source state '" + transition.from_state + "' was not found.", &entity, nullptr, entityIndex);
        }
        if (!stateNames.contains(transition.to_state)) {
          AddIssue(report, SceneValidationSeverity::Error, "game.behavior.to_missing",
                   "Transition target state '" + transition.to_state + "' was not found.", &entity, nullptr, entityIndex);
        }
        if (!IsValidCondition(transition.condition)) {
          AddIssue(report, SceneValidationSeverity::Error, "game.behavior.invalid_condition",
                   "Invalid transition condition '" + transition.condition + "'.", &entity, nullptr, entityIndex);
        }
      }
    }
  }

  for (const SceneGroupDesc& group : scene.game_groups) {
    for (const std::string& memberId : group.member_entity_ids) {
      if (!entityIds.contains(memberId)) {
        AddIssue(report, SceneValidationSeverity::Error, "game.group.member_missing",
                 "Group '" + group.id + "' references missing entity id '" + memberId + "'.",
                 nullptr, nullptr, -1, &group);
      }
    }
    if (!group.formation.leader_entity_id.empty()) {
      bool leaderIsMember = false;
      for (const std::string& memberId : group.member_entity_ids) {
        if (memberId == group.formation.leader_entity_id) {
          leaderIsMember = true;
          break;
        }
      }
      if (!leaderIsMember) {
        AddIssue(report, SceneValidationSeverity::Error, "game.group.leader_not_member",
                 "Group '" + group.id + "' leader is not a member of the group.",
                 nullptr, nullptr, -1, &group);
      }
    }
  }

  return report;
}

bool EnsureGameEntityIds(EditorSceneFile& scene) {
  bool changed = false;
  for (SceneGameEntityDesc& entity : scene.game_entities) {
    if (entity.id.empty()) {
      entity.id = t850::game::MakeStableId("ge_");
      changed = true;
    }
    for (SceneComponentDesc& component : entity.components) {
      if (component.id.empty()) {
        component.id = t850::game::MakeStableId("comp_");
        changed = true;
      }
    }
  }
  for (SceneGroupDesc& group : scene.game_groups) {
    if (group.id.empty()) {
      group.id = t850::game::MakeStableId("grp_");
      changed = true;
    }
  }
  return changed;
}

bool MigrateEditorSceneGameLogic(EditorSceneFile& scene, std::string* log) {
  if (scene.version >= kSceneSchemaV2_GameLogic) return false;

  int entityIdsAdded = 0;
  int componentIdsAdded = 0;
  int legacyControlsMapped = 0;
  int defaultComponentsAdded = 0;

  for (SceneGameEntityDesc& entity : scene.game_entities) {
    if (entity.id.empty()) {
      entity.id = t850::game::MakeStableId("ge_");
      ++entityIdsAdded;
    }

    if (entity.ai == "player") {
      entity.control.mode = "player";
      ++legacyControlsMapped;
    } else if (entity.ai == "nav_agent") {
      entity.control.mode = "ai";
      ++legacyControlsMapped;
      if (entity.components.empty()) {
        SceneComponentDesc movement;
        movement.id = t850::game::MakeStableId("comp_");
        movement.type = "movement";
        entity.components.push_back(std::move(movement));

        SceneComponentDesc pathFollow;
        pathFollow.id = t850::game::MakeStableId("comp_");
        pathFollow.type = "path_follow";
        entity.components.push_back(std::move(pathFollow));
        defaultComponentsAdded += 2;
        componentIdsAdded += 2;
      }
    } else if (entity.ai.empty()) {
      entity.control.mode = "none";
      ++legacyControlsMapped;
    }

    for (SceneComponentDesc& component : entity.components) {
      if (component.id.empty()) {
        component.id = t850::game::MakeStableId("comp_");
        ++componentIdsAdded;
      }
    }
  }

  scene.version = kSceneSchemaV2_GameLogic;
  AppendMigrationLog(log, "Migrated game logic schema from v1 to v2.");
  AppendMigrationLog(log, "Assigned " + std::to_string(entityIdsAdded) + " entity id(s).");
  AppendMigrationLog(log, "Assigned " + std::to_string(componentIdsAdded) + " component id(s).");
  AppendMigrationLog(log, "Mapped " + std::to_string(legacyControlsMapped) + " legacy control value(s).");
  AppendMigrationLog(log, "Added " + std::to_string(defaultComponentsAdded) + " default component(s).");
  return true;
}

} // namespace t850::scene