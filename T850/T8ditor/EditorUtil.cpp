/*********************************************************
 * T8ditor — shared editor string / path / override helpers. See header.
 *********************************************************/

#include "EditorUtil.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#ifdef OS_WINDOWS
#include <windows.h>
#endif

namespace t8ditor {

std::string ToLowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string NormalizeEditorResourcePath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  const std::string lower = ToLowerCopy(path);
  const std::string assetsMarker = "/assets/";
  const std::size_t embeddedAssets = lower.rfind(assetsMarker);
  if (embeddedAssets != std::string::npos) {
    path.erase(0, embeddedAssets + 1);
  }
  while (!path.empty() && path.front() == '/') {
    path.erase(path.begin());
  }
  const std::string assetsPrefix = "assets/";
  if (ToLowerCopy(path).rfind(assetsPrefix, 0) == 0) {
    path.erase(0, assetsPrefix.size());
  }
  return path;
}

std::string MeshEditorProfileModelKey(const std::string& path) {
  std::string key = NormalizeEditorResourcePath(path);
  const std::size_t slash = key.find_last_of('/');
  if (slash != std::string::npos) {
    key = key.substr(slash + 1);
  }
  return ToLowerCopy(key);
}

bool EditorResourcePathEquals(const std::string& lhs, const std::string& rhs) {
  return ToLowerCopy(NormalizeEditorResourcePath(lhs)) ==
         ToLowerCopy(NormalizeEditorResourcePath(rhs));
}

std::string FileStemFromResourcePath(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
  const std::size_t dot = path.find_last_of('.');
  const std::size_t end = (dot == std::string::npos || dot < begin) ? path.size() : dot;
  return path.substr(begin, end - begin);
}

void SetFloatOverride(std::vector<t850::FloatOverrideDesc>& values, std::string name, float value) {
  for (auto& entry : values) {
    if (entry.name == name) {
      entry.value = value;
      return;
    }
  }
  values.push_back({std::move(name), value});
}

void SetBoolOverride(std::vector<t850::BoolOverrideDesc>& values, std::string name, bool value) {
  for (auto& entry : values) {
    if (entry.name == name) {
      entry.value = value;
      return;
    }
  }
  values.push_back({std::move(name), value});
}

void SetIntOverride(std::vector<t850::IntOverrideDesc>& values, std::string name, int value) {
  for (auto& entry : values) {
    if (entry.name == name) {
      entry.value = value;
      return;
    }
  }
  values.push_back({std::move(name), value});
}

const t850::FloatOverrideDesc* FindEditorFloatOverride(const std::vector<t850::FloatOverrideDesc>& values,
                                                       const std::string& name) {
  for (const auto& value : values) {
    if (value.name == name) {
      return &value;
    }
  }
  return nullptr;
}

const t850::BoolOverrideDesc* FindEditorBoolOverride(const std::vector<t850::BoolOverrideDesc>& values,
                                                     const std::string& name) {
  for (const auto& value : values) {
    if (value.name == name) {
      return &value;
    }
  }
  return nullptr;
}

const t850::IntOverrideDesc* FindEditorIntOverride(const std::vector<t850::IntOverrideDesc>& values,
                                                   const std::string& name) {
  for (const auto& value : values) {
    if (value.name == name) {
      return &value;
    }
  }
  return nullptr;
}

std::string GetSolutionDir() {
  // Derive solution directory from the executable path.
  // Expected layout:  <solution>/bin/<arch>/<config>/T8ditor.exe
  // We walk up until we find a "bin" segment, then return its parent.
  auto exeDir = []() -> std::filesystem::path {
#ifdef OS_WINDOWS
    wchar_t buf[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf))) {
      std::error_code ec;
      std::filesystem::path p(buf, ec);
      if (!ec) return p.parent_path();
    }
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec);
  };

  std::filesystem::path dir = exeDir();
  // Walk up until we find a "bin" folder
  for (int iterations = 0; iterations < 8; ++iterations) {
    std::string stem = dir.stem().string();
    if (ToLowerCopy(stem) == "bin") {
      return dir.parent_path().string();
    }
    dir = dir.parent_path();
  }
  // Fallback: return the original exe directory's parent
  return exeDir().parent_path().string();
}

} // namespace t8ditor
