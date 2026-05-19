/*********************************************************
 * T8ditor — Scene file save/load implementation.
 *********************************************************/

#include "EditorScene.h"

#include <Config.h>
#include <utils/Log.h>

#include <filesystem>

#ifdef OS_WINDOWS
#include <windows.h>
#include <commdlg.h>
#endif

namespace t8ditor {

// ── Save ──────────────────────────────────────────────

bool SaveSceneToFile(const SceneFile& scene, const std::string& path) {
  return t850::scene::SaveEditorSceneFile(scene, path);
}

// ── Load ──────────────────────────────────────────────

bool LoadSceneFromFile(const std::string& path, SceneFile& scene) {
  return t850::scene::LoadEditorSceneFile(path, scene);
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
