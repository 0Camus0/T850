#include <pch.h>
#include <scene/SceneDescriptor.h>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <sstream>
#include <cstdio>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

namespace t850 {

bool LoadSceneDescriptor(const std::string& path, SceneDescriptor& desc) {
  std::string json;
  if (!ResourceLocator::Instance().ReadText(path, json)) {
    T8_LOG_ERROR("[SceneDescriptor] Cannot open '%s'", path.c_str());
    return false;
  }

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

  if (!ResourceLocator::Instance().WriteText(path, result.value())) {
    T8_LOG_ERROR("[SceneDescriptor] Cannot write '%s'", path.c_str());
    return false;
  }

  T8_LOG_INFO("[SceneDescriptor] Saved '%s'", path.c_str());
  return true;
}

} // namespace t850
