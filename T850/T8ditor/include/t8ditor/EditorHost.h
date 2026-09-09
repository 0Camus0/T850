#pragma once

#include <game/ComponentFactory.h>
#include <game/GameValidation.h>
#include <scene/EditorSceneFile.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace t8ditor {

struct EditorSnapshot {
  uint64_t revision = 0;
  t850::scene::EditorSceneFile scene;
};

struct GameplayEdit {
  uint64_t expectedRevision = 0;
  std::string label;
  std::vector<t850::scene::SceneGameEntityDesc> entities;
  std::vector<t850::scene::SceneGroupDesc> groups;
  std::optional<t850::scene::SceneGameLogicSettingsDesc> settings;
};

class EditorContext {
public:
  virtual ~EditorContext() = default;
  virtual EditorSnapshot ReadScene() = 0;
  virtual void SubmitGameplayEdit(GameplayEdit edit) = 0;
  virtual const std::string& LastEditStatus() const = 0;
};

class EditorRegistry {
public:
  using Panel = std::function<void(EditorContext&)>;
  using Inspector = std::function<bool(t850::scene::SceneComponentDesc&)>;
  using Validator = std::function<void(const t850::scene::EditorSceneFile&,
                                      t850::scene::SceneValidationReport&)>;
  struct PanelEntry {
    std::string title;
    Panel draw;
    bool visible = true;
  };
  void RegisterPanel(std::string id, std::string title, Panel draw);
  void RegisterInspector(std::string type, Inspector draw);
  void RegisterValidator(std::string id, Validator validate);
  void RegisterCommand(std::string id, std::string title, Panel execute);
  void Validate(const t850::scene::EditorSceneFile& scene,
                t850::scene::SceneValidationReport& report) const;
  const Inspector* FindInspector(const std::string& type) const;

private:
  friend class EditorApp;
  std::map<std::string, PanelEntry> panels_;
  std::map<std::string, PanelEntry> commands_;
  std::map<std::string, Inspector> inspectors_;
  std::map<std::string, Validator> validators_;
};

struct EditorHostDesc {
  std::string title = "T8ditor";
  std::string projectRoot;
  std::string cacheRoot;
  std::string startupScene;
  bool requireKnownComponentsForPlay = false;
  std::function<void(t850::game::ComponentFactoryRegistry&)> registerRuntime;
  std::function<void(EditorRegistry&)> registerEditor;
};

int RunEditor(int argc, char** argv, EditorHostDesc host = {});

}