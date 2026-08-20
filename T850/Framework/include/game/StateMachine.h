#pragma once

#include <scene/EditorSceneFile.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace t850::scene {
struct SceneValidationReport;
}

namespace t850::game {

struct GameObject;
class GameLogicSystem;

enum class TransitionConditionKind {
  Always,
  OnEvent,
  TimerElapsed,
  HealthBelow,
  ParamEquals
};

struct CompiledTransition {
  uint32_t fromStateIndex = 0;
  uint32_t toStateIndex = 0;
  TransitionConditionKind kind = TransitionConditionKind::Always;
  uint32_t eventTypeId = 0;
  std::string eventType;
  float threshold = 0.0f;
  std::string paramKey;
  std::string paramValue;
  float priority = 0.0f;
  float cooldown = 0.0f;
  float cooldownRemaining = 0.0f;
  uint32_t descriptorOrder = 0;
};

class StateMachine {
public:
  bool Compile(const t850::scene::SceneStateMachineDesc& descriptor,
               t850::scene::SceneValidationReport* report);
  void SetInitialState();
  void Evaluate(GameObject& owner, GameLogicSystem& system, float fixedDt);
  uint32_t CurrentStateIndex() const;
  std::string_view CurrentStateName() const;
  bool ForceTransition(std::string_view stateName);
  std::span<const std::string> StateNames() const;

private:
  bool EvaluateCondition(
      const CompiledTransition& transition, const GameObject& owner, const GameLogicSystem& system) const;
  bool TransitionTo(uint32_t stateIndex);

  std::vector<t850::scene::SceneStateDesc> states_;
  std::vector<std::string> stateNames_;
  std::vector<CompiledTransition> transitions_;
  uint32_t initialStateIndex_ = 0;
  uint32_t currentStateIndex_ = 0;
  float stateElapsed_ = 0.0f;
  bool compiled_ = false;
};

} // namespace t850::game