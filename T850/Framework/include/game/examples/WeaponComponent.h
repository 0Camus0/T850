#pragma once

#include <game/Component.h>
#include <scene/EditorSceneFile.h>

#include <cstdint>
#include <string_view>

namespace t850::game {
class ComponentFactoryRegistry;
}

namespace t850::game::examples {

class WeaponComponent final : public Component {
public:
  explicit WeaponComponent(t850::scene::SceneComponentDesc descriptor);

  std::string_view Type() const override { return "weapon"; }
  ComponentUpdatePhase Phase() const override { return ComponentUpdatePhase::Logic; }
  void OnCreate() override;
  void Update(float fixedDt) override;
  bool TryGetFloat(std::string_view name, float& value) const override;

private:
  t850::scene::SceneComponentDesc descriptor_;
  float range_ = 100.0f;
  float damage_ = 10.0f;
  float cooldownSeconds_ = 0.2f;
  float cooldownRemainingSeconds_ = 0.0f;
  float muzzleHeight_ = 0.0f;
  uint32_t fireActionBit_ = 1u;
};

std::unique_ptr<Component> CreateWeaponComponent(
    const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context);
void RegisterWeaponComponent(ComponentFactoryRegistry& registry);

} // namespace t850::game::examples