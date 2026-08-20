#include <pch.h>

#include <game/examples/WeaponComponent.h>

#include <game/ComponentFactory.h>
#include <game/Controller.h>
#include <game/EventBus.h>
#include <game/GameLogicSystem.h>
#include <game/GameObject.h>
#include <scene/PrimitiveInstance.h>

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

uint32_t ParseActionBit(const std::map<std::string, std::string>& params, uint32_t fallback) {
  const auto found = params.find("fireActionBit");
  if (found == params.end()) return fallback;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(found->second.c_str(), &end, 0);
  return end == found->second.c_str() + found->second.size() && parsed <= UINT32_MAX
      ? static_cast<uint32_t>(parsed)
      : fallback;
}

XVECTOR3 NormalizedDirection(XVECTOR3 direction) {
  direction.w = 0.0f;
  const float length = direction.Length();
  if (length <= 0.000001f) return XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  return direction / length;
}

} // namespace

WeaponComponent::WeaponComponent(t850::scene::SceneComponentDesc descriptor)
    : descriptor_(std::move(descriptor)) {}

void WeaponComponent::OnCreate() {
  range_ = (std::max)(0.0f, ParseFloat(descriptor_.params, "range", range_));
  damage_ = (std::max)(0.0f, ParseFloat(descriptor_.params, "damage", damage_));
  cooldownSeconds_ = (std::max)(0.0f, ParseFloat(
      descriptor_.params, "cooldown", cooldownSeconds_));
  muzzleHeight_ = ParseFloat(descriptor_.params, "muzzleHeight", muzzleHeight_);
  fireActionBit_ = ParseActionBit(descriptor_.params, fireActionBit_);
}

void WeaponComponent::Update(float fixedDt) {
  cooldownRemainingSeconds_ = (std::max)(0.0f, cooldownRemainingSeconds_ - fixedDt);
  if (!owner_ || !system_ || !owner_->links.primitive || cooldownRemainingSeconds_ > 0.0f) {
    return;
  }

  const MovementIntent& intent = system_->IntentFor(owner_->runtimeId);
  if (fireActionBit_ == 0 || (intent.actionBits & fireActionBit_) == 0) return;
  cooldownRemainingSeconds_ = cooldownSeconds_;

  const XMATRIX44& transform = owner_->links.primitive->Position;
  const XVECTOR3 start(
      transform.m[3][0], transform.m[3][1] + muzzleHeight_, transform.m[3][2], 1.0f);
  const XVECTOR3 end = start + NormalizedDirection(intent.lookDir) * range_;
  GameQueryFilter filter;
  filter.ignore = owner_->runtimeId;
  filter.excludeLayers = t850::GameplayLayerBit(t850::GameplayLayer::Trigger);

  GameHit hit;
  if (!system_->Physics().LineOfSight(start, end, filter, hit) ||
      hit.entity == kInvalidRuntimeGameObjectId) {
    return;
  }

  const GameObject* target = system_->Registry().Get(hit.entity);
  if (!target || (owner_->team >= 0 && target->team == owner_->team)) return;

  GameEvent event;
  event.type = "damage";
  event.sourceEntityId = owner_->sceneId;
  event.targetEntityId = target->sceneId;
  event.params["amount"] = std::to_string(damage_);
  event.params["weapon_component_id"] = Id();
  system_->Events().Publish(std::move(event));
}

bool WeaponComponent::TryGetFloat(std::string_view name, float& value) const {
  if (name == "range") value = range_;
  else if (name == "damage") value = damage_;
  else if (name == "cooldown") value = cooldownSeconds_;
  else if (name == "cooldownRemaining") value = cooldownRemainingSeconds_;
  else return false;
  return true;
}

std::unique_ptr<Component> CreateWeaponComponent(
    const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context) {
  (void)context;
  return std::make_unique<WeaponComponent>(descriptor);
}

void RegisterWeaponComponent(ComponentFactoryRegistry& registry) {
  registry.Register("weapon", CreateWeaponComponent,
                    ComponentTypeInfo{.type = "weapon", .allowMultiple = false});
}

} // namespace t850::game::examples