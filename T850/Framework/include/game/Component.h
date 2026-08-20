#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace t850::scene {
struct SceneComponentDesc;
struct SceneValidationReport;
}

namespace t850::game {

struct GameObject;
class GameLogicSystem;

enum class ComponentUpdatePhase {
  PrePhysics,
  PostPhysics,
  Logic,
  Late
};

class Component {
public:
  virtual ~Component() = default;
  virtual std::string_view Type() const = 0;
  virtual ComponentUpdatePhase Phase() const { return ComponentUpdatePhase::Logic; }
  virtual void OnAttach(GameObject& owner, GameLogicSystem& system) {
    owner_ = &owner;
    system_ = &system;
  }
  virtual void OnCreate() {}
  virtual void Update(float fixedDt) { (void)fixedDt; }
  virtual void OnDestroy() {}
  virtual bool TryGetFloat(std::string_view name, float& value) const {
    (void)name;
    (void)value;
    return false;
  }
  virtual void OnDetach() {
    owner_ = nullptr;
    system_ = nullptr;
  }

  const std::string& Id() const { return id_; }
  bool Enabled() const { return enabled_; }
  void SetEnabled(bool enabled) { enabled_ = enabled; }

private:
  friend class GameLogicSystem;
  std::string id_;
  bool enabled_ = true;

protected:
  GameObject* owner_ = nullptr;
  GameLogicSystem* system_ = nullptr;
};

struct ComponentTypeInfo {
  std::string type;
  std::vector<std::string> requiredComponentTypes;
  bool allowMultiple = false;
};

struct ComponentLoadContext {
  GameObject* owner = nullptr;
  GameLogicSystem* system = nullptr;
  t850::scene::SceneValidationReport* report = nullptr;
};

using ComponentFactoryFn = std::unique_ptr<Component> (*)(
    const t850::scene::SceneComponentDesc&, ComponentLoadContext&);

} // namespace t850::game