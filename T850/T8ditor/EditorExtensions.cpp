#include <t8ditor/EditorHost.h>

#include <stdexcept>
#include <utility>

namespace t8ditor {

void EditorRegistry::RegisterPanel(std::string id, std::string title, Panel draw) {
  if (id.empty() || title.empty() || !draw || panels_.contains(id))
    throw std::invalid_argument("Invalid or duplicate editor panel: " + id);
  panels_.emplace(std::move(id), PanelEntry{std::move(title), std::move(draw)});
}

void EditorRegistry::RegisterCommand(std::string id, std::string title, Panel execute) {
  if (id.empty() || title.empty() || !execute || commands_.contains(id))
    throw std::invalid_argument("Invalid or duplicate editor command: " + id);
  commands_.emplace(std::move(id), PanelEntry{std::move(title), std::move(execute)});
}

void EditorRegistry::RegisterInspector(std::string type, Inspector draw) {
  if (type.empty() || !draw || inspectors_.contains(type))
    throw std::invalid_argument("Invalid or duplicate component inspector: " + type);
  inspectors_.emplace(std::move(type), std::move(draw));
}

void EditorRegistry::RegisterValidator(std::string id, Validator validate) {
  if (id.empty() || !validate || validators_.contains(id))
    throw std::invalid_argument("Invalid or duplicate editor validator: " + id);
  validators_.emplace(std::move(id), std::move(validate));
}

void EditorRegistry::Validate(const t850::scene::EditorSceneFile& scene,
                              t850::scene::SceneValidationReport& report) const {
  for (const auto& [id, validate] : validators_) validate(scene, report);
}

const EditorRegistry::Inspector* EditorRegistry::FindInspector(const std::string& type) const {
  const auto found = inspectors_.find(type);
  return found == inspectors_.end() ? nullptr : &found->second;
}

}