#include <pch.h>

#include <game/examples/HealthComponent.h>

#include <game/ComponentFactory.h>
#include <game/GameLogicSystem.h>
#include <game/GameObject.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace t850::game::examples {
namespace {

float ParseFloat(const std::map<std::string, std::string>& params,
                 std::string_view key,
                 float fallback) {
  const auto found = params.find(std::string(key));
  if (found == params.end()) return fallback;
  char* end = nullptr;
  const float parsed = std::strtof(found->second.c_str(), &end);
  return end == found->second.c_str() + found->second.size() && std::isfinite(parsed)
      ? parsed
      : fallback;
}

} // namespace

HealthComponent::HealthComponent(t850::scene::SceneComponentDesc descriptor)
    : descriptor_(std::move(descriptor)) {}

void HealthComponent::OnCreate() {
  maximumHealth_ = (std::max)(0.0f, ParseFloat(descriptor_.params, "maxHp", maximumHealth_));
  currentHealth_ = std::clamp(
      ParseFloat(descriptor_.params, "currentHp", maximumHealth_), 0.0f, maximumHealth_);
  armor_ = (std::max)(0.0f, ParseFloat(descriptor_.params, "armor", armor_));
  died_ = currentHealth_ <= 0.0f;
  if (system_) {
    damageSubscription_ = system_->Events().Subscribe("damage", [this](const GameEvent& event) {
      HandleDamage(event);
    });
  }
}

void HealthComponent::OnDestroy() {
  if (system_ && damageSubscription_.IsValid()) {
    system_->Events().Unsubscribe(damageSubscription_);
  }
  damageSubscription_ = Subscription{};
}

bool HealthComponent::TryGetFloat(std::string_view name, float& value) const {
  if (name == "health" || name == "currentHp") {
    value = currentHealth_;
    return true;
  }
  if (name == "maxHp") {
    value = maximumHealth_;
    return true;
  }
  if (name == "armor") {
    value = armor_;
    return true;
  }
  return false;
}

void HealthComponent::HandleDamage(const GameEvent& event) {
  if (!owner_ || died_) return;
  if (!event.targetEntityId.empty() && event.targetEntityId != owner_->sceneId) return;

  const auto amount = event.params.find("amount");
  if (amount == event.params.end()) return;
  char* end = nullptr;
  const float requestedDamage = std::strtof(amount->second.c_str(), &end);
  if (end != amount->second.c_str() + amount->second.size() || !std::isfinite(requestedDamage)) return;

  const float appliedDamage = (std::max)(0.0f, requestedDamage - armor_);
  currentHealth_ = (std::max)(0.0f, currentHealth_ - appliedDamage);
  if (currentHealth_ <= 0.0f) {
    died_ = true;
    GameEvent diedEvent;
    diedEvent.type = "died";
    diedEvent.sourceEntityId = owner_->sceneId;
    diedEvent.targetEntityId = owner_->sceneId;
    diedEvent.params["damage_source"] = event.sourceEntityId;
    system_->Events().Publish(std::move(diedEvent));
  }
}

std::unique_ptr<Component> CreateHealthComponent(
    const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context) {
  (void)context;
  return std::make_unique<HealthComponent>(descriptor);
}

void RegisterHealthComponent(ComponentFactoryRegistry& registry) {
  registry.Register("health", CreateHealthComponent,
                    ComponentTypeInfo{.type = "health", .allowMultiple = false});
}

} // namespace t850::game::examples