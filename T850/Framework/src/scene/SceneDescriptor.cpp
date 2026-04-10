#include <scene/SceneDescriptor.h>
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#include <glaze/glaze.hpp>
#pragma warning(pop)
#include <fstream>
#include <sstream>
#include <cstdio>

namespace t800 {

bool LoadSceneDescriptor(const std::string& path, SceneDescriptor& desc) {
  std::ifstream file(path);
  if (!file.is_open()) {
    printf("[SceneDescriptor] ERROR: cannot open '%s'\n", path.c_str());
    return false;
  }

  std::stringstream ss;
  ss << file.rdbuf();
  std::string json = ss.str();

  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(desc, json);
  if (ec) {
    std::string err = glz::format_error(ec, json);
    printf("[SceneDescriptor] ERROR parsing '%s': %s\n", path.c_str(), err.c_str());
    return false;
  }

  printf("[SceneDescriptor] Loaded '%s': %zu cameras, %zu light cameras, %zu lights, %zu splines, %zu meshes\n",
         path.c_str(),
         desc.cameras.size(),
         desc.light_cameras.size(),
         desc.lights.size(),
         desc.splines.size(),
         desc.meshes.size());
  return true;
}

bool SaveSceneDescriptor(const std::string& path, const SceneDescriptor& desc) {
  auto result = glz::write<glz::opts{.prettify = true}>(desc);
  if (!result) {
    printf("[SceneDescriptor] ERROR: failed to serialize scene\n");
    return false;
  }

  std::ofstream file(path);
  if (!file.is_open()) {
    printf("[SceneDescriptor] ERROR: cannot write '%s'\n", path.c_str());
    return false;
  }

  file << result.value();
  printf("[SceneDescriptor] Saved '%s'\n", path.c_str());
  return true;
}

} // namespace t800
