#include <pch.h>

#include <game/StateMachine.h>

#include <debug/RuntimeTelemetry.h>
#include <game/Component.h>
#include <game/EventBus.h>
#include <game/GameLogicSystem.h>
#include <game/GameObject.h>
#include <game/GameValidation.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace t850::game {
namespace {

constexpr uint32_t kWildcardState = std::numeric_limits<uint32_t>::max();

uint32_t HashEventType(std::string_view value) {
  uint32_t hash = 2166136261u;
  for (const unsigned char character : value) {
    hash ^= character;
    hash *= 16777619u;
  }
  return hash;
}

bool ParseFloat(std::string_view text, float& value) {
  if (text.empty()) return false;
  std::string input(text);
  char* end = nullptr;
  errno = 0;
  value = std::strtof(input.c_str(), &end);
  return errno != ERANGE && end == input.c_str() + input.size() && std::isfinite(value);
}

void AddCompileError(t850::scene::SceneValidationReport* report,
                     std::string code,
                     std::string message) {
  if (!report) return;
  t850::scene::SceneValidationIssue issue;
  issue.severity = t850::scene::SceneValidationSeverity::Error;
  issue.code = std::move(code);
  issue.message = std::move(message);
  report->issues.push_back(std::move(issue));
}

} // namespace

bool StateMachine::Compile(
    const t850::scene::SceneStateMachineDesc& descriptor,
    t850::scene::SceneValidationReport* report) {
  states_ = descriptor.states;
  stateNames_.clear();
  transitions_.clear();
  compiled_ = false;
  stateElapsed_ = 0.0f;

  std::unordered_map<std::string, uint32_t> stateIndices;
  for (uint32_t index = 0; index < states_.size(); ++index) {
    stateNames_.push_back(states_[index].name);
    if (!states_[index].name.empty()) stateIndices.emplace(states_[index].name, index);
  }

  const auto initial = stateIndices.find(descriptor.initial_state);
  if (initial == stateIndices.end()) {
    AddCompileError(report, "game.behavior.initial_missing",
                    "Initial state '" + descriptor.initial_state + "' was not found.");
    return false;
  }
  initialStateIndex_ = initial->second;

  bool valid = true;
  for (uint32_t order = 0; order < descriptor.transitions.size(); ++order) {
    const t850::scene::SceneTransitionDesc& source = descriptor.transitions[order];
    CompiledTransition transition;
    transition.descriptorOrder = order;
    transition.priority = source.priority;
    transition.cooldown = (std::max)(0.0f, source.cooldown);

    if (source.from_state == "*") {
      transition.fromStateIndex = kWildcardState;
    } else if (const auto found = stateIndices.find(source.from_state); found != stateIndices.end()) {
      transition.fromStateIndex = found->second;
    } else {
      AddCompileError(report, "game.behavior.from_missing",
                      "Transition source state '" + source.from_state + "' was not found.");
      valid = false;
      continue;
    }

    const auto target = stateIndices.find(source.to_state);
    if (target == stateIndices.end()) {
      AddCompileError(report, "game.behavior.to_missing",
                      "Transition target state '" + source.to_state + "' was not found.");
      valid = false;
      continue;
    }
    transition.toStateIndex = target->second;

    if (source.condition == "always") {
      transition.kind = TransitionConditionKind::Always;
    } else if (source.condition == "timer_elapsed") {
      transition.kind = TransitionConditionKind::TimerElapsed;
    } else if (source.condition.starts_with("on_event:")) {
      transition.kind = TransitionConditionKind::OnEvent;
      transition.eventType = source.condition.substr(9);
      transition.eventTypeId = HashEventType(transition.eventType);
      valid = valid && !transition.eventType.empty();
    } else if (source.condition.starts_with("health_below:")) {
      transition.kind = TransitionConditionKind::HealthBelow;
      if (!ParseFloat(std::string_view(source.condition).substr(13), transition.threshold)) valid = false;
    } else if (source.condition.starts_with("param_equals:")) {
      transition.kind = TransitionConditionKind::ParamEquals;
      const std::string_view payload = std::string_view(source.condition).substr(13);
      const std::size_t separator = payload.find(':');
      if (separator == std::string_view::npos || separator == 0 || separator + 1 >= payload.size()) {
        valid = false;
      } else {
        transition.paramKey = payload.substr(0, separator);
        transition.paramValue = payload.substr(separator + 1);
      }
    } else {
      valid = false;
    }

    if (!valid) {
      AddCompileError(report, "game.behavior.invalid_condition",
                      "Invalid transition condition '" + source.condition + "'.");
      continue;
    }
    transitions_.push_back(std::move(transition));
  }

  if (!valid) return false;
  std::stable_sort(transitions_.begin(), transitions_.end(), [](const auto& left, const auto& right) {
    if (left.priority != right.priority) return left.priority > right.priority;
    return left.descriptorOrder < right.descriptorOrder;
  });
  compiled_ = true;
  SetInitialState();
  return true;
}

void StateMachine::SetInitialState() {
  if (!compiled_) return;
  currentStateIndex_ = initialStateIndex_;
  stateElapsed_ = 0.0f;
  for (CompiledTransition& transition : transitions_) transition.cooldownRemaining = 0.0f;
}

void StateMachine::Evaluate(GameObject& owner, GameLogicSystem& system, float fixedDt) {
  if (!compiled_ || currentStateIndex_ >= states_.size()) return;
  stateElapsed_ += (std::max)(0.0f, fixedDt);
  for (CompiledTransition& transition : transitions_) {
    transition.cooldownRemaining = (std::max)(0.0f, transition.cooldownRemaining - fixedDt);
  }

  for (CompiledTransition& transition : transitions_) {
    if (transition.cooldownRemaining > 0.0f) continue;
    if (transition.fromStateIndex != kWildcardState && transition.fromStateIndex != currentStateIndex_) continue;
    if (!EvaluateCondition(transition, owner, system)) continue;

    const std::string previous(CurrentStateName());
    if (!TransitionTo(transition.toStateIndex)) return;
    transition.cooldownRemaining = transition.cooldown;

    GameEvent event;
    event.type = "state_changed";
    event.sourceEntityId = owner.sceneId;
    event.targetEntityId = owner.sceneId;
    event.params["from"] = previous;
    event.params["to"] = std::string(CurrentStateName());
    system.Events().Publish(std::move(event));
    t850::RuntimeTelemetry::AddCounter("game.state_machines.transitions", 1.0);
    return;
  }
}

uint32_t StateMachine::CurrentStateIndex() const {
  return currentStateIndex_;
}

std::string_view StateMachine::CurrentStateName() const {
  return currentStateIndex_ < stateNames_.size() ? stateNames_[currentStateIndex_] : std::string_view{};
}

bool StateMachine::ForceTransition(std::string_view stateName) {
  const auto found = std::find(stateNames_.begin(), stateNames_.end(), stateName);
  if (found == stateNames_.end()) return false;
  return TransitionTo(static_cast<uint32_t>(std::distance(stateNames_.begin(), found)));
}

std::span<const std::string> StateMachine::StateNames() const {
  return stateNames_;
}

bool StateMachine::EvaluateCondition(
    const CompiledTransition& transition,
    const GameObject& owner,
    const GameLogicSystem& system) const {
  switch (transition.kind) {
    case TransitionConditionKind::Always:
      return true;
    case TransitionConditionKind::OnEvent:
      for (const GameEvent& event : system.Events().RecentEvents()) {
        if (event.tick == system.TickIndex() && event.type == transition.eventType &&
            (event.targetEntityId.empty() || event.targetEntityId == owner.sceneId)) {
          return true;
        }
      }
      return false;
    case TransitionConditionKind::TimerElapsed:
      return states_[currentStateIndex_].default_duration >= 0.0f &&
          stateElapsed_ >= states_[currentStateIndex_].default_duration;
    case TransitionConditionKind::HealthBelow:
      for (const std::unique_ptr<Component>& component : owner.components) {
        float health = 0.0f;
        if (component && component->TryGetFloat("currentHp", health) && health < transition.threshold) {
          return true;
        }
      }
      return false;
    case TransitionConditionKind::ParamEquals: {
      const auto found = states_[currentStateIndex_].params.find(transition.paramKey);
      return found != states_[currentStateIndex_].params.end() && found->second == transition.paramValue;
    }
  }
  return false;
}

bool StateMachine::TransitionTo(uint32_t stateIndex) {
  if (!compiled_ || stateIndex >= states_.size()) return false;
  currentStateIndex_ = stateIndex;
  stateElapsed_ = 0.0f;
  return true;
}

} // namespace t850::game