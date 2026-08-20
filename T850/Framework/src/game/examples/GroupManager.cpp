#include <pch.h>

#include <game/examples/GroupManager.h>

#include <game/Controller.h>
#include <game/GameObject.h>
#include <scene/PrimitiveInstance.h>

#include <algorithm>
#include <cmath>

namespace t850::game::examples {
namespace {

constexpr float kPi = 3.14159265358979323846f;

XVECTOR3 ObjectPosition(const GameObject* object) {
  if (!object || !object->links.primitive) return XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  const XMATRIX44& matrix = object->links.primitive->Position;
  return XVECTOR3(matrix.m[3][0], matrix.m[3][1], matrix.m[3][2], 1.0f);
}

XVECTOR3 OffsetTarget(const XVECTOR3& anchor, float x, float z) {
  return XVECTOR3(anchor.x + x, anchor.y, anchor.z + z, 1.0f);
}

} // namespace

void GroupManager::LoadGroups(
    const std::vector<t850::scene::SceneGroupDesc>& groups,
    GameObjectRegistry& registry,
    GameLogicSystem& system) {
  Clear();
  registry_ = &registry;
  system_ = &system;
  groups_.reserve(groups.size());

  for (const t850::scene::SceneGroupDesc& descriptor : groups) {
    RuntimeGameGroup runtime;
    runtime.descriptor = descriptor;
    runtime.members.reserve(descriptor.member_entity_ids.size());
    for (const std::string& sceneId : descriptor.member_entity_ids) {
      if (const GameObject* object = registry.FindBySceneId(sceneId)) {
        runtime.members.push_back(object->runtimeId);
      }
    }
    if (!descriptor.formation.leader_entity_id.empty()) {
      if (const GameObject* leader = registry.FindBySceneId(descriptor.formation.leader_entity_id)) {
        runtime.leader = leader->runtimeId;
      }
    }
    if (runtime.leader == kInvalidRuntimeGameObjectId && !runtime.members.empty()) {
      runtime.leader = runtime.members.front();
    }
    groupIndices_.insert_or_assign(descriptor.id, groups_.size());
    commands_.insert_or_assign(descriptor.id, GroupCommand{});
    groups_.push_back(std::move(runtime));
  }
}

void GroupManager::Update(float fixedDt) {
  (void)fixedDt;
  if (!registry_ || !system_) return;
  for (RuntimeGameGroup& group : groups_) {
    auto command = commands_.find(group.descriptor.id);
    if (command == commands_.end()) continue;
    if (command->second.type == GroupCommandType::Attack) {
      const GameObject* target = registry_->Get(command->second.targetEntity);
      if (!target || !target->links.primitive) {
        command->second.type = GroupCommandType::Hold;
        command->second.dirty = true;
      } else {
        const XVECTOR3 position = ObjectPosition(target);
        if ((position - command->second.target).Length() > 0.05f) {
          command->second.target = position;
          command->second.dirty = true;
        }
      }
    }
    if (command->second.dirty) ApplyCommand(group, command->second);
  }
}

void GroupManager::Clear() {
  registry_ = nullptr;
  system_ = nullptr;
  groups_.clear();
  groupIndices_.clear();
  commands_.clear();
}

bool GroupManager::IssueMove(std::string_view groupId, const XVECTOR3& target) {
  if (!Find(groupId)) return false;
  GroupCommand& command = commands_[std::string(groupId)];
  command.type = GroupCommandType::Move;
  command.target = target;
  command.target.w = 1.0f;
  command.targetEntity = kInvalidRuntimeGameObjectId;
  command.dirty = true;
  return true;
}

bool GroupManager::IssueAttack(std::string_view groupId, RuntimeGameObjectId target) {
  if (!Find(groupId) || !registry_ || !registry_->Get(target)) return false;
  GroupCommand& command = commands_[std::string(groupId)];
  command.type = GroupCommandType::Attack;
  command.targetEntity = target;
  command.target = ObjectPosition(registry_->Get(target));
  command.dirty = true;
  return true;
}

bool GroupManager::IssueHold(std::string_view groupId) {
  RuntimeGameGroup* group = FindMutable(groupId);
  if (!group || !system_) return false;
  GroupCommand& command = commands_[std::string(groupId)];
  command.type = GroupCommandType::Hold;
  command.targetEntity = kInvalidRuntimeGameObjectId;
  command.dirty = false;
  for (RuntimeGameObjectId member : group->members) {
    GameObject* object = registry_ ? registry_->Get(member) : nullptr;
    if (object && object->controller && object->controller->Kind() == ControllerKind::AI) {
      static_cast<AIController*>(object->controller)->ClearNavigationGoal();
    }
  }
  return true;
}

const RuntimeGameGroup* GroupManager::Find(std::string_view groupId) const {
  const auto found = groupIndices_.find(std::string(groupId));
  return found == groupIndices_.end() ? nullptr : &groups_[found->second];
}

RuntimeGameGroup* GroupManager::FindMutable(std::string_view groupId) {
  const auto found = groupIndices_.find(std::string(groupId));
  return found == groupIndices_.end() ? nullptr : &groups_[found->second];
}

void GroupManager::ApplyCommand(RuntimeGameGroup& group, GroupCommand& command) {
  command.dirty = false;
  if (!system_ || command.type == GroupCommandType::Hold) return;
  const std::vector<XVECTOR3> targets = group.descriptor.strategy == "flock"
      ? BuildFlockTargets(group, command.target)
      : BuildFormationTargets(group, command.target);
  const std::size_t count = (std::min)(group.members.size(), targets.size());
  for (std::size_t index = 0; index < count; ++index) {
    system_->SetAINavigationGoal(group.members[index], targets[index]);
  }
}

std::vector<XVECTOR3> GroupManager::BuildFormationTargets(
    const RuntimeGameGroup& group, const XVECTOR3& anchor) const {
  std::vector<XVECTOR3> targets(group.members.size(), anchor);
  const float spacing = (std::max)(0.01f, group.descriptor.formation.spacing);
  const float depth = (std::max)(0.01f, group.descriptor.formation.depth_step);
  const std::string& type = group.descriptor.formation.type;

  std::size_t follower = 0;
  for (std::size_t index = 0; index < group.members.size(); ++index) {
    if (group.members[index] == group.leader) {
      targets[index] = anchor;
      continue;
    }
    ++follower;
    if (type == "line") {
      targets[index] = OffsetTarget(anchor, static_cast<float>(follower) * spacing, 0.0f);
    } else if (type == "column") {
      targets[index] = OffsetTarget(anchor, 0.0f, -static_cast<float>(follower) * depth);
    } else if (type == "box") {
      const int width = (std::max)(1, static_cast<int>(std::ceil(std::sqrt(group.members.size()))));
      const int slot = static_cast<int>(follower - 1);
      targets[index] = OffsetTarget(
          anchor,
          static_cast<float>((slot % width) - width / 2) * spacing,
          -static_cast<float>(slot / width + 1) * depth);
    } else if (type == "circle") {
      const float angle = 2.0f * kPi * static_cast<float>(follower - 1) /
          static_cast<float>((std::max)(std::size_t{1}, group.members.size() - 1));
      targets[index] = OffsetTarget(anchor, std::cos(angle) * spacing, std::sin(angle) * spacing);
    } else {
      const float row = static_cast<float>((follower + 1) / 2);
      const float side = follower % 2 == 1 ? -1.0f : 1.0f;
      targets[index] = OffsetTarget(anchor, side * row * spacing, -row * depth);
    }
  }
  return targets;
}

std::vector<XVECTOR3> GroupManager::BuildFlockTargets(
    const RuntimeGameGroup& group, const XVECTOR3& anchor) const {
  std::vector<XVECTOR3> targets;
  targets.reserve(group.members.size());
  const float radius = (std::max)(0.25f, group.descriptor.flock.separation_radius);
  const float cohesion = (std::max)(0.0f, group.descriptor.flock.cohesion_weight);
  const float blend = cohesion / (1.0f + cohesion);

  XVECTOR3 center(0.0f, 0.0f, 0.0f, 1.0f);
  int positionedMembers = 0;
  for (RuntimeGameObjectId member : group.members) {
    const GameObject* object = registry_ ? registry_->Get(member) : nullptr;
    if (!object || !object->links.primitive) continue;
    center += ObjectPosition(object);
    ++positionedMembers;
  }
  if (positionedMembers > 0) center /= static_cast<float>(positionedMembers);

  for (std::size_t index = 0; index < group.members.size(); ++index) {
    const float angle = 2.0f * kPi * static_cast<float>(index) /
        static_cast<float>((std::max)(std::size_t{1}, group.members.size()));
    const GameObject* object = registry_ ? registry_->Get(group.members[index]) : nullptr;
    const XVECTOR3 relative = object ? ObjectPosition(object) - center : XVECTOR3();
    targets.push_back(OffsetTarget(
        anchor,
        relative.x * blend + std::cos(angle) * radius,
        relative.z * blend + std::sin(angle) * radius));
  }
  return targets;
}

} // namespace t850::game::examples