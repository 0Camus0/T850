#include <pch.h>

#include <game/ComponentFactory.h>

#include <game/GameObject.h>
#include <game/GameValidation.h>

#include <algorithm>
#include <utility>

namespace t850::game {

UnknownComponent::UnknownComponent(t850::scene::SceneComponentDesc descriptor)
    : descriptor_(std::move(descriptor)) {}

std::string_view UnknownComponent::Type() const {
  return descriptor_.type;
}

const t850::scene::SceneComponentDesc& UnknownComponent::Descriptor() const {
  return descriptor_;
}

bool ComponentFactoryRegistry::Register(
    std::string type, ComponentFactoryFn function, ComponentTypeInfo info) {
  if (type.empty() || !function || entries_.contains(type)) return false;
  info.type = type;
  entries_.emplace(std::move(type), Entry{function, std::move(info)});
  return true;
}

std::vector<std::string> ComponentFactoryRegistry::Types() const {
  std::vector<std::string> types;
  types.reserve(entries_.size());
  for (const auto& [type, entry] : entries_) types.push_back(type);
  std::sort(types.begin(), types.end());
  return types;
}

std::unique_ptr<Component> ComponentFactoryRegistry::Create(
    const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context) const {
  const auto found = entries_.find(descriptor.type);
  if (found != entries_.end() && found->second.function) {
    return found->second.function(descriptor, context);
  }

  if (context.report) {
    t850::scene::SceneValidationIssue issue;
    issue.severity = t850::scene::SceneValidationSeverity::Warning;
    issue.code = "game.component.unknown_type";
    issue.message = "Unknown component type '" + descriptor.type + "' will be preserved.";
    issue.entityId = context.owner ? context.owner->sceneId : std::string{};
    issue.componentId = descriptor.id;
    context.report->issues.push_back(std::move(issue));
  }
  return std::make_unique<UnknownComponent>(descriptor);
}

const ComponentTypeInfo* ComponentFactoryRegistry::Info(std::string_view type) const {
  const auto found = entries_.find(std::string(type));
  return found == entries_.end() ? nullptr : &found->second.info;
}

} // namespace t850::game