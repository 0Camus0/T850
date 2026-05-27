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

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace t850::scene {

namespace {

void SetError(std::string* out, const std::string& message) {
  if (out) *out = message;
}

std::string ToLowerAsciiCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string NormalizeSceneResourcePath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  const std::string lower = ToLowerAsciiCopy(path);
  const std::string assetsMarker = "/assets/";
  const std::size_t embeddedAssets = lower.rfind(assetsMarker);
  if (embeddedAssets != std::string::npos) {
    path.erase(0, embeddedAssets + 1);
  }
  while (!path.empty() && path.front() == '/') {
    path.erase(path.begin());
  }
  const std::string assetsPrefix = "assets/";
  if (ToLowerAsciiCopy(path).rfind(assetsPrefix, 0) == 0) {
    path.erase(0, assetsPrefix.size());
  }
  return path;
}

std::string ExtensionLower(const std::string& path) {
  return ToLowerAsciiCopy(std::filesystem::path(path).extension().string());
}

bool IsSceneGltfMeshPath(const std::string& path) {
  const std::string ext = ExtensionLower(path);
  return ext == ".glb" || ext == ".gltf";
}

void AddUniqueSearchDirectory(std::vector<std::string>& directories, const std::string& directory) {
  const std::string normalized = NormalizeSceneResourcePath(directory);
  if (normalized.empty()) {
    return;
  }
  if (std::find(directories.begin(), directories.end(), normalized) == directories.end()) {
    directories.push_back(normalized);
  }
}

std::string FirstResourceDirectory(const std::string& path) {
  const std::string normalized = NormalizeSceneResourcePath(path);
  const std::size_t slash = normalized.find('/');
  if (slash == std::string::npos || slash == 0) return {};
  std::string first = normalized.substr(0, slash);
  if (first.find(':') != std::string::npos) return {};
  return first;
}

std::vector<std::string> BuildSceneMeshFallbackDirectories(const std::string& meshPath,
                                                           const std::string& scenePath) {
  std::vector<std::string> directories;
  AddUniqueSearchDirectory(directories, std::filesystem::path(scenePath).parent_path().string());
  AddUniqueSearchDirectory(directories, std::filesystem::path(meshPath).parent_path().string());
  AddUniqueSearchDirectory(directories, FirstResourceDirectory(meshPath));
  AddUniqueSearchDirectory(directories, "Models");
  return directories;
}

void ResolveSceneMeshFallbacks(const std::string& scenePath, EditorSceneFile& scene) {
  ResourceLocator& locator = ResourceLocator::Instance();
  for (SceneObjectDesc& object : scene.objects) {
    const bool meshWasEmpty = object.mesh.empty();
    const std::string originalMeshPath = meshWasEmpty ? object.name : object.mesh;
    std::string normalizedMeshPath = NormalizeSceneResourcePath(originalMeshPath);
    if (normalizedMeshPath.empty() || !IsSceneGltfMeshPath(normalizedMeshPath)) {
      continue;
    }

    if (locator.Exists(normalizedMeshPath)) {
      object.mesh = normalizedMeshPath;
      continue;
    }

    std::string fallbackPath;
    const std::vector<std::string> searchDirectories =
        BuildSceneMeshFallbackDirectories(normalizedMeshPath, scenePath);
    if (locator.FindFileByNameRecursive(normalizedMeshPath, fallbackPath, searchDirectories)) {
      object.mesh = fallbackPath;
      T8_LOG_INFO("[SceneFile] Resolved missing mesh '%s' to recursive fallback '%s'",
                  normalizedMeshPath.c_str(), fallbackPath.c_str());
    } else if (!meshWasEmpty) {
      object.mesh = normalizedMeshPath;
    }
  }
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

  ResolveSceneMeshFallbacks(path, scene);

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
