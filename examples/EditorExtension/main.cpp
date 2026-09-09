#include <t8ditor/EditorHost.h>
#include <game/GameIds.h>
#include <game/GameLogicSystem.h>
#include <imgui.h>

#include <charconv>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

int created = 0;
int ticks = 0;

class Counter final : public t850::game::Component {
public:
  std::string_view Type() const override { return "sample.counter"; }
  void OnCreate() override { ++created; }
  void Update(float) override { ++ticks; }
};

void Require(bool passed, const char* message) {
  if (!passed) throw std::runtime_error(message);
}

void RegisterRuntime(t850::game::ComponentFactoryRegistry& factories) {
  Require(factories.Register("sample.counter",
      [](const t850::scene::SceneComponentDesc&, t850::game::ComponentLoadContext&)
          -> std::unique_ptr<t850::game::Component> { return std::make_unique<Counter>(); }, {}),
      "duplicate sample.counter registration");
}

bool ReadValue(const t850::scene::SceneComponentDesc& component, int& value) {
  const auto found = component.params.find("value");
  if (found == component.params.end()) { value = 0; return true; }
  const auto& text = found->second;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value >= 0;
}

void RegisterEditor(t8ditor::EditorRegistry& registry) {
  registry.RegisterInspector("sample.counter", [](t850::scene::SceneComponentDesc& component) {
    int value = 0;
    ReadValue(component, value);
    if (!ImGui::InputInt("Value", &value)) return false;
    component.params["value"] = std::to_string(value);
    return true;
  });
  registry.RegisterValidator("sample.values", [](const t850::scene::EditorSceneFile& scene,
                                                t850::scene::SceneValidationReport& report) {
    for (const auto& entity : scene.game_entities) {
      for (const auto& component : entity.components) {
        int value = 0;
        if (component.type == "sample.counter" && !ReadValue(component, value)) {
          t850::scene::SceneValidationIssue issue;
          issue.severity = t850::scene::SceneValidationSeverity::Error;
          issue.code = "sample.invalid_value";
          issue.message = "Value must be a nonnegative integer";
          issue.entityId = entity.id;
          issue.componentId = component.id;
          report.issues.push_back(std::move(issue));
        }
      }
    }
  });
  auto addCounter = [](t8ditor::EditorContext& context) {
    const auto snapshot = context.ReadScene();
    t8ditor::GameplayEdit edit{snapshot.revision, "Add Counter", snapshot.scene.game_entities,
        snapshot.scene.game_groups, snapshot.scene.game_logic_settings};
    t850::scene::SceneGameEntityDesc entity;
    entity.id = t850::game::MakeStableId("sample_entity_");
    entity.name = "Counter";
    entity.components.push_back({.id = t850::game::MakeStableId("sample_component_"), .type = "sample.counter"});
    edit.entities.push_back(std::move(entity));
    context.SubmitGameplayEdit(std::move(edit));
  };
  registry.RegisterCommand("sample.add", "Add Counter", addCounter);
  registry.RegisterPanel("sample.panel", "Counters", [addCounter](t8ditor::EditorContext& context) {
    if (ImGui::Button("Add Counter")) addCounter(context);
    ImGui::Text("Created in Play: %d", created);
    ImGui::Text("Fixed ticks: %d", ticks);
    ImGui::TextWrapped("%s", context.LastEditStatus().c_str());
  });
}

int SelfTest() {
  t850::EngineContext context;
  t850::game::GameLogicSystem system;
  system.Initialize(context, {});
  RegisterRuntime(system.Factories());
  t8ditor::EditorRegistry editor;
  RegisterEditor(editor);
  bool rejectedDuplicate = false;
  try { RegisterEditor(editor); } catch (const std::invalid_argument&) { rejectedDuplicate = true; }
  Require(rejectedDuplicate, "duplicate editor registration accepted");

  t850::scene::EditorSceneFile scene;
  t850::scene::SceneGameEntityDesc entity;
  entity.id = "sample_entity";
  entity.components.push_back({.id = "sample_component", .type = "sample.counter",
                               .params = {{"value", "7"}}, .config_json = "{\"version\":1}"});
  scene.game_entities.push_back(entity);
  t850::scene::SceneValidationReport report;
  editor.Validate(scene, report);
  Require(!report.HasErrors(), "external validator rejected valid data");
  scene.game_entities.front().components.front().params["value"] = "-1";
  editor.Validate(scene, report);
  Require(report.HasErrors(), "external validator accepted invalid data");
  scene.game_entities.front() = entity;

  const auto path = std::filesystem::temp_directory_path() / (t850::game::MakeStableId("t850_external_") + ".t8scene");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
  } cleanup{path};
  Require(t850::scene::SaveEditorSceneFile(scene, path.string()), "save failed");
  t850::scene::EditorSceneFile loaded;
  Require(t850::scene::LoadEditorSceneFile(path.string(), loaded), "reload failed");
  Require(loaded.game_entities.front().components.front().config_json == entity.components.front().config_json,
          "opaque component configuration lost");
  Require(system.LoadFromScene(loaded, {}, &report, true), "external runtime load failed");
  system.Update(1.0f / 60.0f);
  Require(created == 1 && ticks == 1, "external component lifecycle did not execute");
  loaded.game_entities.front().components.front().type = "sample.missing";
  Require(!system.LoadFromScene(loaded, {}, &report, true), "missing required component accepted");
  Require(system.Registry().Count() == 1, "failed load discarded active game state");
  system.Initialize(context, {});
  Require(!system.Factories().Info("sample.counter"), "initialization retained the previous host registry");
  RegisterRuntime(system.Factories());
  Require(system.LoadFromScene(scene, {}, &report, true), "reinitialized external runtime load failed");
  system.Update(1.0f / 60.0f);
  Require(created == 2 && ticks == 2, "reinitialized external lifecycle did not execute");
  std::cout << "PASS external editor registration, validation, round-trip, runtime lifecycle, strict load, reinitialization\n";
  return 0;
}

}

int main(int argc, char** argv) {
  try {
    for (int index = 1; index < argc; ++index) {
      if (std::string_view(argv[index]) == "--selftest") return SelfTest();
    }
    t8ditor::EditorHostDesc host;
    host.title = "T850 Extension Example";
    host.requireKnownComponentsForPlay = true;
    host.registerRuntime = RegisterRuntime;
    host.registerEditor = RegisterEditor;
    return t8ditor::RunEditor(argc, argv, std::move(host));
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}