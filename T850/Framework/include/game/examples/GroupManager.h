#pragma once

#include <game/GameLogicSystem.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace t850::game::examples {

enum class GroupCommandType {
  Hold,
  Move,
  Attack
};

struct RuntimeGameGroup {
  t850::scene::SceneGroupDesc descriptor;
  std::vector<RuntimeGameObjectId> members;
  RuntimeGameObjectId leader = kInvalidRuntimeGameObjectId;
};

class GroupManager final : public IGameGroupSystem {
public:
  void LoadGroups(const std::vector<t850::scene::SceneGroupDesc>& groups,
                  GameObjectRegistry& registry,
                  GameLogicSystem& system) override;
  void Update(float fixedDt) override;
  void Clear() override;

  bool IssueMove(std::string_view groupId, const XVECTOR3& target);
  bool IssueAttack(std::string_view groupId, RuntimeGameObjectId target);
  bool IssueHold(std::string_view groupId);

  const RuntimeGameGroup* Find(std::string_view groupId) const;
  std::size_t Count() const { return groups_.size(); }

private:
  struct GroupCommand {
    GroupCommandType type = GroupCommandType::Hold;
    XVECTOR3 target = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
    RuntimeGameObjectId targetEntity = kInvalidRuntimeGameObjectId;
    bool dirty = false;
  };

  RuntimeGameGroup* FindMutable(std::string_view groupId);
  void ApplyCommand(RuntimeGameGroup& group, GroupCommand& command);
  std::vector<XVECTOR3> BuildFormationTargets(
      const RuntimeGameGroup& group, const XVECTOR3& anchor) const;
  std::vector<XVECTOR3> BuildFlockTargets(
      const RuntimeGameGroup& group, const XVECTOR3& anchor) const;

  GameObjectRegistry* registry_ = nullptr;
  GameLogicSystem* system_ = nullptr;
  std::vector<RuntimeGameGroup> groups_;
  std::unordered_map<std::string, std::size_t> groupIndices_;
  std::unordered_map<std::string, GroupCommand> commands_;
};

} // namespace t850::game::examples