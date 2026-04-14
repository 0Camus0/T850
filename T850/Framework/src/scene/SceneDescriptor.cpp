#include <scene/SceneDescriptor.h>
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#include <glaze/glaze.hpp>
#pragma warning(pop)
#include <fstream>
#include <sstream>
#include <cstdio>
#include <utils/Log.h>

namespace t800 {

bool LoadSceneDescriptor(const std::string& path, SceneDescriptor& desc) {
  std::ifstream file(path);
  if (!file.is_open()) {
    T8_LOG_ERROR("[SceneDescriptor] Cannot open '%s'", path.c_str());
    return false;
  }

  std::stringstream ss;
  ss << file.rdbuf();
  std::string json = ss.str();

  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(desc, json);
  if (ec) {
    std::string err = glz::format_error(ec, json);
    T8_LOG_ERROR("[SceneDescriptor] Parse error '%s': %s", path.c_str(), err.c_str());
    return false;
  }

  T8_LOG_INFO("[SceneDescriptor] Loaded '%s': %zu cameras, %zu light cameras, %zu lights, %zu splines, %zu meshes",
              path.c_str(), desc.cameras.size(), desc.light_cameras.size(),
              desc.lights.size(), desc.splines.size(), desc.meshes.size());
  return true;
}

bool SaveSceneDescriptor(const std::string& path, const SceneDescriptor& desc) {
  auto result = glz::write<glz::opts{.prettify = true}>(desc);
  if (!result) {
    T8_LOG_ERROR("[SceneDescriptor] Failed to serialize scene");
    return false;
  }

  std::ofstream file(path);
  if (!file.is_open()) {
    T8_LOG_ERROR("[SceneDescriptor] Cannot write '%s'", path.c_str());
    return false;
  }

  file << result.value();
  T8_LOG_INFO("[SceneDescriptor] Saved '%s'", path.c_str());
  return true;
}

} // namespace t800
