#include "pch.h"
/*********************************************************
 * glTF 2.0 — JSON parsing via glaze.
 *
 * The POD types in GLTFTypes.h are aggregate-initializable, so glaze
 * uses pure compile-time reflection to populate them by field name. We
 * pass error_on_unknown_keys=false so that unknown spec extensions and
 * application "extras" are silently ignored (per glTF spec §3.5).
 *********************************************************/

#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFTypes.h>
#include <utils/Log.h>

#pragma warning(push)
#pragma warning(disable: 4244 4267)
#include <glaze/glaze.hpp>
#pragma warning(pop)

namespace t800 {
namespace gltf {

bool ParseJson(const std::string& json, Document& out) {
  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(out, json);
  if (ec) {
    std::string err = glz::format_error(ec, json);
    T8_LOG_ERROR("[glTF] JSON parse error: %s", err.c_str());
    return false;
  }
  return true;
}

} // namespace gltf
} // namespace t800
