#include <pch.h>

#include <game/examples/RtsCommandController.h>

#include <game/EventBus.h>
#include <game/GameLogicSystem.h>
#include <game/GameObject.h>
#include <game/examples/GroupManager.h>

namespace t850::game::examples {

void RtsCommandController::Bind(GroupManager* groups, GameLogicSystem* system) {
  groups_ = groups;
  system_ = system;
  if (!groups_ || !system_) selectedGroupId_.clear();
}

bool RtsCommandController::SelectGroup(std::string_view groupId) {
  if (!groups_ || !groups_->Find(groupId)) return false;
  selectedGroupId_ = groupId;
  return true;
}

void RtsCommandController::ClearSelection() {
  selectedGroupId_.clear();
}

bool RtsCommandController::Move(const XVECTOR3& target) {
  if (!groups_ || selectedGroupId_.empty() || !groups_->IssueMove(selectedGroupId_, target)) {
    return false;
  }
  PublishCommand("move_command");
  return true;
}

bool RtsCommandController::Attack(RuntimeGameObjectId target) {
  if (!groups_ || selectedGroupId_.empty() || !groups_->IssueAttack(selectedGroupId_, target)) {
    return false;
  }
  PublishCommand("attack_command", target);
  return true;
}

bool RtsCommandController::Hold() {
  if (!groups_ || selectedGroupId_.empty() || !groups_->IssueHold(selectedGroupId_)) {
    return false;
  }
  PublishCommand("hold_command");
  return true;
}

void RtsCommandController::PublishCommand(
    std::string_view eventType, RuntimeGameObjectId target) {
  if (!groups_ || !system_) return;
  const RuntimeGameGroup* group = groups_->Find(selectedGroupId_);
  if (!group) return;

  const GameObject* targetObject = system_->Registry().Get(target);
  for (RuntimeGameObjectId member : group->members) {
    const GameObject* object = system_->Registry().Get(member);
    if (!object) continue;
    GameEvent event;
    event.type = std::string(eventType);
    event.targetEntityId = object->sceneId;
    event.params["group_id"] = selectedGroupId_;
    if (targetObject) event.params["target_entity_id"] = targetObject->sceneId;
    system_->Events().Publish(std::move(event));
  }
}

} // namespace t850::game::examples