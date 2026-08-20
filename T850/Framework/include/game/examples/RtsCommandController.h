#pragma once

#include <game/GameIds.h>
#include <utils/xMaths.h>

#include <string>
#include <string_view>

namespace t850::game {
class GameLogicSystem;
}

namespace t850::game::examples {

class GroupManager;

class RtsCommandController {
public:
  void Bind(GroupManager* groups, GameLogicSystem* system);
  bool SelectGroup(std::string_view groupId);
  void ClearSelection();

  bool Move(const XVECTOR3& target);
  bool Attack(RuntimeGameObjectId target);
  bool Hold();

  std::string_view SelectedGroup() const { return selectedGroupId_; }

private:
  void PublishCommand(std::string_view eventType, RuntimeGameObjectId target = kInvalidRuntimeGameObjectId);

  GroupManager* groups_ = nullptr;
  GameLogicSystem* system_ = nullptr;
  std::string selectedGroupId_;
};

} // namespace t850::game::examples