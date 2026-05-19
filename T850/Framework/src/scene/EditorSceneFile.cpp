#include <pch.h>

#include <scene/EditorSceneFile.h>

#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4267)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <filesystem>
#include <fstream>

namespace t850::scene {

namespace {

void SetError(std::string* out, const std::string& message) {
  if (out) *out = message;
}

} // namespace

bool LoadEditorSceneFile(const std::string& path, EditorSceneFile& scene, std::string* error) {
  std::string content;
  if (!ResourceLocator::Instance().ReadText(path, content)) {
    std::string message = "Scene file not found or unreadable: " + path;
    SetError(error, message);
    T8_LOG_ERROR("[SceneFile] %s", message.c_str());
    return false;
  }

  auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(scene, content);
  if (err) {
    std::string message = glz::format_error(err, content);
    SetError(error, message);
    T8_LOG_ERROR("[SceneFile] Failed to parse %s: %s", path.c_str(), message.c_str());
    return false;
  }

  T8_LOG_INFO("[SceneFile] Loaded %s (%zu objects, %zu cameras, %zu lights)",
              path.c_str(), scene.objects.size(), scene.cameras.size(), scene.lights.size());
  return true;
}

bool SaveEditorSceneFile(const EditorSceneFile& scene, const std::string& path, std::string* error) {
  auto result = glz::write<glz::opts{.prettify = true}>(scene);
  if (!result) {
    std::string message = "Failed to serialize scene";
    SetError(error, message);
    T8_LOG_ERROR("[SceneFile] %s", message.c_str());
    return false;
  }

  std::filesystem::path outPath(path);
  std::filesystem::path parent = outPath.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      std::string message = "Cannot create scene directory: " + parent.string();
      SetError(error, message);
      T8_LOG_ERROR("[SceneFile] %s", message.c_str());
      return false;
    }
  }

  std::ofstream ofs(outPath, std::ios::binary);
  if (!ofs.is_open()) {
    std::string message = "Cannot write scene file: " + path;
    SetError(error, message);
    T8_LOG_ERROR("[SceneFile] %s", message.c_str());
    return false;
  }

  ofs << result.value();
  if (!ofs.good()) {
    std::string message = "Failed while writing scene file: " + path;
    SetError(error, message);
    T8_LOG_ERROR("[SceneFile] %s", message.c_str());
    return false;
  }

  T8_LOG_INFO("[SceneFile] Saved %s", path.c_str());
  return true;
}

} // namespace t850::scene
