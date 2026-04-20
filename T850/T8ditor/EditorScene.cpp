/*********************************************************
 * T8ditor — Scene file save/load implementation.
 *********************************************************/

#include "EditorScene.h"

#include <Config.h>
#include <utils/Log.h>

// Suppress warnings from third-party glaze headers
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4267) // dragonbox.hpp size_t to int conversion
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <fstream>
#include <sstream>
#include <filesystem>

#ifdef OS_WINDOWS
#include <windows.h>
#include <commdlg.h>
#endif

namespace t8ditor {

// ── Save ──────────────────────────────────────────────

bool SaveSceneToFile(const SceneFile& scene, const std::string& path) {
  auto result = glz::write<glz::opts{.prettify = true}>(scene);
  if (!result) {
    T8_LOG_ERROR("[T8ditor] Failed to serialize scene");
    return false;
  }

  // Ensure parent directory exists
  auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);

  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    T8_LOG_ERROR("[T8ditor] Cannot write scene file: %s", path.c_str());
    return false;
  }
  ofs << result.value();
  ofs.close();

  T8_LOG_INFO("[T8ditor] Scene saved to: %s", path.c_str());
  return true;
}

// ── Load ──────────────────────────────────────────────

bool LoadSceneFromFile(const std::string& path, SceneFile& scene) {
  if (!std::filesystem::exists(path)) {
    T8_LOG_ERROR("[T8ditor] Scene file not found: %s", path.c_str());
    return false;
  }

  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    T8_LOG_ERROR("[T8ditor] Cannot open scene file: %s", path.c_str());
    return false;
  }

  std::stringstream buf;
  buf << ifs.rdbuf();
  std::string content = buf.str();

  auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(scene, content);
  if (err) {
    T8_LOG_ERROR("[T8ditor] Scene parse error: %s", glz::format_error(err, content).c_str());
    return false;
  }

  T8_LOG_INFO("[T8ditor] Scene loaded from: %s (%zu objects)", path.c_str(), scene.objects.size());
  return true;
}

// ── Save file dialog ──────────────────────────────────

std::string SaveFileDialog(const wchar_t* filter, const wchar_t* title,
                           const wchar_t* defaultExt) {
#ifdef OS_WINDOWS
  wchar_t path[MAX_PATH] = {};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize  = sizeof(ofn);
  ofn.hwndOwner    = nullptr;
  ofn.lpstrFilter  = filter;
  ofn.lpstrFile    = path;
  ofn.nMaxFile     = MAX_PATH;
  ofn.lpstrTitle   = title;
  ofn.lpstrDefExt  = defaultExt;
  ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

  if (GetSaveFileNameW(&ofn)) {
    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
      std::string result(len - 1, '\0');
      WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), len, nullptr, nullptr);
      return result;
    }
  }
#endif
  return {};
}

} // namespace t8ditor
