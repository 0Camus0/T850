#pragma once

#include <game/Component.h>
#include <scene/EditorSceneFile.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace t850::game {

class UnknownComponent final : public Component {
public:
  explicit UnknownComponent(t850::scene::SceneComponentDesc descriptor);

  std::string_view Type() const override;
  const t850::scene::SceneComponentDesc& Descriptor() const;

private:
  t850::scene::SceneComponentDesc descriptor_;
};

class ComponentFactoryRegistry {
public:
  void Register(std::string type, ComponentFactoryFn function, ComponentTypeInfo info);
  std::unique_ptr<Component> Create(
      const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context) const;
  const ComponentTypeInfo* Info(std::string_view type) const;

private:
  struct Entry {
    ComponentFactoryFn function = nullptr;
    ComponentTypeInfo info;
  };

  std::unordered_map<std::string, Entry> entries_;
};

} // namespace t850::game