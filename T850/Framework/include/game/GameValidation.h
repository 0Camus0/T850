#pragma once

#include <scene/EditorSceneFile.h>

#include <string>
#include <vector>

namespace t850::scene {

constexpr int kSceneSchemaV1 = 1;
constexpr int kSceneSchemaV2_GameLogic = 2;

enum class SceneValidationSeverity {
  Info,
  Warning,
  Error
};

struct SceneValidationIssue {
  SceneValidationSeverity severity = SceneValidationSeverity::Info;
  std::string code;
  std::string message;
  std::string entityId;
  std::string componentId;
  std::string groupId;
  int entityIndex = -1;
};

struct SceneValidationReport {
  std::vector<SceneValidationIssue> issues;

  bool HasErrors() const;
};

SceneValidationReport ValidateEditorSceneGameLogic(const EditorSceneFile& scene);
bool EnsureGameEntityIds(EditorSceneFile& scene);
bool MigrateEditorSceneGameLogic(EditorSceneFile& scene, std::string* log = nullptr);

} // namespace t850::scene