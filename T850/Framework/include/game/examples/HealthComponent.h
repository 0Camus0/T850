#pragma once

#include <game/Component.h>
#include <game/EventBus.h>
#include <scene/EditorSceneFile.h>

#include <string_view>

namespace t850::game {
class ComponentFactoryRegistry;
}

namespace t850::game::examples {

class HealthComponent final : public Component {
public:
  explicit HealthComponent(t850::scene::SceneComponentDesc descriptor);

  std::string_view Type() const override { return "health"; }
  void OnCreate() override;
  void OnDestroy() override;
  bool TryGetFloat(std::string_view name, float& value) const override;

  float MaximumHealth() const { return maximumHealth_; }
  float CurrentHealth() const { return currentHealth_; }
  float Armor() const { return armor_; }

private:
  void HandleDamage(const GameEvent& event);

  t850::scene::SceneComponentDesc descriptor_;
  Subscription damageSubscription_;
  float maximumHealth_ = 100.0f;
  float currentHealth_ = 100.0f;
  float armor_ = 0.0f;
  bool died_ = false;
};

std::unique_ptr<Component> CreateHealthComponent(
    const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context);
void RegisterHealthComponent(ComponentFactoryRegistry& registry);

} // namespace t850::game::examples