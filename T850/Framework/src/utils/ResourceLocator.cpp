#include <pch.h>
#include <utils/ResourceLocator.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

#ifdef OS_ANDROID
#include <android/asset_manager.h>
#endif

namespace t850 {
namespace {

bool IsAbsolutePath(const std::string& path) {
  std::filesystem::path p(path);
  return p.is_absolute();
}

std::vector<std::filesystem::path> DiskCandidates(const std::filesystem::path& basePath,
                                                  const std::string& originalPath,
                                                  const std::string& normalizedPath) {
  std::vector<std::filesystem::path> candidates;
  std::filesystem::path original(originalPath);
  if (original.is_absolute()) {
    candidates.push_back(original);
    return candidates;
  }

  candidates.push_back(original);
  if (originalPath != normalizedPath) {
    candidates.emplace_back(normalizedPath);
  }

  if (!basePath.empty()) {
    candidates.push_back(basePath / normalizedPath);
  }

  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    candidates.push_back(cwd / normalizedPath);
    candidates.push_back(cwd / "Assets" / normalizedPath);
    candidates.push_back(cwd / "T850" / "Assets" / normalizedPath);
  }

  return candidates;
}

bool ReadDiskBinary(const std::filesystem::path& path, std::vector<unsigned char>& out) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;

  std::streamsize size = file.tellg();
  if (size < 0) return false;
  file.seekg(0, std::ios::beg);

  out.resize(static_cast<std::size_t>(size));
  if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size)) {
    out.clear();
    return false;
  }
  return true;
}

bool IsRegularFile(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
}

bool IsDirectory(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_directory(path, ec);
}

std::string ToResourcePath(const std::string& directory, const std::filesystem::path& relative) {
  std::string dir = ResourceLocator::NormalizePath(directory);
  std::string rel = ResourceLocator::NormalizePath(relative.string());
  if (dir.empty()) return rel;
  if (rel.empty()) return dir;
  return dir + "/" + rel;
}

std::string ToLowerAsciiCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

void AddUniqueResourceDirectory(std::vector<std::string>& directories, const std::string& directory) {
  const std::string normalized = ResourceLocator::NormalizePath(directory);
  if (normalized.empty()) {
    return;
  }
  if (std::find(directories.begin(), directories.end(), normalized) == directories.end()) {
    directories.push_back(normalized);
  }
}

std::string ParentResourceDirectory(const std::string& path) {
  const std::filesystem::path parent = std::filesystem::path(ResourceLocator::NormalizePath(path)).parent_path();
  return ResourceLocator::NormalizePath(parent.string());
}

std::string FirstResourceDirectory(const std::string& path) {
  const std::string normalized = ResourceLocator::NormalizePath(path);
  const std::size_t slash = normalized.find('/');
  if (slash == std::string::npos || slash == 0) return {};
  std::string first = normalized.substr(0, slash);
  if (first.find(':') != std::string::npos) return {};
  return first;
}

#ifdef OS_ANDROID
std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string ResolveAndroidAssetPathCaseInsensitive(AAssetManager* manager, const std::string& path) {
  if (!manager || path.empty() || IsAbsolutePath(path)) return {};

  const std::size_t slash = path.find_last_of('/');
  if (slash != std::string::npos && slash + 1 < path.size()) {
    const std::string parent = path.substr(0, slash);
    const std::string filename = path.substr(slash + 1);
    AAssetDir* dir = AAssetManager_openDir(manager, parent.c_str());
    if (dir) {
      const std::string target = ToLowerAscii(filename);
      const char* entry = nullptr;
      while ((entry = AAssetDir_getNextFileName(dir)) != nullptr) {
        if (ToLowerAscii(entry) == target) {
          std::string resolved = parent + "/" + entry;
          AAssetDir_close(dir);
          return resolved;
        }
      }
      AAssetDir_close(dir);
    }
  }

  std::string current;
  std::size_t segmentStart = 0;
  while (segmentStart <= path.size()) {
    const std::size_t segmentEnd = path.find('/', segmentStart);
    std::string segment = path.substr(segmentStart,
      segmentEnd == std::string::npos ? std::string::npos : segmentEnd - segmentStart);
    if (!segment.empty()) {
      AAssetDir* dir = AAssetManager_openDir(manager, current.c_str());
      if (!dir) return {};

      const std::string target = ToLowerAscii(segment);
      const char* entry = nullptr;
      std::string matched;
      while ((entry = AAssetDir_getNextFileName(dir)) != nullptr) {
        if (ToLowerAscii(entry) == target) {
          matched = entry;
          break;
        }
      }
      AAssetDir_close(dir);
      if (matched.empty()) return {};
      current = current.empty() ? matched : current + "/" + matched;
    }

    if (segmentEnd == std::string::npos) break;
    segmentStart = segmentEnd + 1;
  }

  return current;
}

bool ReadAndroidAssetBinary(AAssetManager* manager, const std::string& path, std::vector<unsigned char>& out) {
  if (!manager || path.empty() || IsAbsolutePath(path)) return false;

  std::string resolvedPath = path;
  AAsset* asset = AAssetManager_open(manager, resolvedPath.c_str(), AASSET_MODE_BUFFER);
  if (!asset) {
    resolvedPath = ResolveAndroidAssetPathCaseInsensitive(manager, path);
    if (!resolvedPath.empty()) {
      asset = AAssetManager_open(manager, resolvedPath.c_str(), AASSET_MODE_BUFFER);
    }
  }
  if (!asset) return false;

  const off_t length = AAsset_getLength(asset);
  if (length < 0) {
    AAsset_close(asset);
    return false;
  }

  out.resize(static_cast<std::size_t>(length));
  std::size_t totalRead = 0;
  while (totalRead < out.size()) {
    const int read = AAsset_read(asset, out.data() + totalRead, out.size() - totalRead);
    if (read <= 0) {
      out.clear();
      AAsset_close(asset);
      return false;
    }
    totalRead += static_cast<std::size_t>(read);
  }

  AAsset_close(asset);
  return true;
}

bool AndroidAssetExists(AAssetManager* manager, const std::string& path) {
  if (!manager || path.empty() || IsAbsolutePath(path)) return false;
  AAsset* asset = AAssetManager_open(manager, path.c_str(), AASSET_MODE_BUFFER);
  if (asset) {
    AAsset_close(asset);
    return true;
  }

  const std::string resolvedPath = ResolveAndroidAssetPathCaseInsensitive(manager, path);
  if (!resolvedPath.empty()) {
    asset = AAssetManager_open(manager, resolvedPath.c_str(), AASSET_MODE_BUFFER);
    if (asset) {
      AAsset_close(asset);
      return true;
    }

    AAssetDir* resolvedDir = AAssetManager_openDir(manager, resolvedPath.c_str());
    if (resolvedDir) {
      const char* resolvedEntry = AAssetDir_getNextFileName(resolvedDir);
      AAssetDir_close(resolvedDir);
      if (resolvedEntry) return true;
    }
  }

  AAssetDir* dir = AAssetManager_openDir(manager, path.c_str());
  if (!dir) return false;
  const char* entry = AAssetDir_getNextFileName(dir);
  AAssetDir_close(dir);
  return entry != nullptr;
}

void ListAndroidAssets(AAssetManager* manager,
                       const std::string& directory,
                       bool recursive,
                       std::vector<std::string>& out) {
  AAssetDir* dir = AAssetManager_openDir(manager, directory.c_str());
  if (!dir) return;

  const char* entry = nullptr;
  while ((entry = AAssetDir_getNextFileName(dir)) != nullptr) {
    std::string child = directory.empty() ? entry : directory + "/" + entry;
    AAsset* asset = AAssetManager_open(manager, child.c_str(), AASSET_MODE_BUFFER);
    if (asset) {
      out.push_back(child);
      AAsset_close(asset);
      continue;
    }

    if (recursive) {
      ListAndroidAssets(manager, child, recursive, out);
    }
  }

  AAssetDir_close(dir);
}
#endif

} // namespace

ResourceLocator& ResourceLocator::Instance() {
  static ResourceLocator instance;
  return instance;
}

ResourceLocator::ResourceLocator() {
  std::error_code ec;
  m_basePath = std::filesystem::current_path(ec);
  if (ec) {
    m_basePath.clear();
  }
  m_cachePath = m_basePath;
}

std::string ResourceLocator::NormalizePath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  while (!path.empty() && path.front() == '/') {
    path.erase(path.begin());
  }
  while (path.rfind("./", 0) == 0) {
    path.erase(0, 2);
  }

  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (lower.rfind("assets/", 0) == 0) {
    path.erase(0, 7);
  }
  return path;
}

void ResourceLocator::SetBasePath(const std::filesystem::path& basePath) {
  m_basePath = basePath;
}

const std::filesystem::path& ResourceLocator::GetBasePath() const {
  return m_basePath;
}

void ResourceLocator::SetCachePath(const std::filesystem::path& cachePath) {
  m_cachePath = cachePath;
}

const std::filesystem::path& ResourceLocator::GetCachePath() const {
  return m_cachePath;
}

std::filesystem::path ResourceLocator::ResolveFilePath(const std::string& path) const {
  const std::string normalized = NormalizePath(path);
  for (const auto& candidate : DiskCandidates(m_basePath, path, normalized)) {
    if (IsRegularFile(candidate)) {
      return candidate;
    }
  }
  return std::filesystem::path(path);
}

std::filesystem::path ResourceLocator::ResolveCachePath(const std::string& path) const {
  std::filesystem::path cachePath(path);
  if (cachePath.is_absolute()) {
    return cachePath;
  }

  const std::string normalized = NormalizePath(path);
  if (m_cachePath.empty()) {
    return std::filesystem::path(normalized);
  }
  return m_cachePath / std::filesystem::path(normalized);
}

bool ResourceLocator::Exists(const std::string& path) const {
  const std::string normalized = NormalizePath(path);
#ifdef OS_ANDROID
  if (AndroidAssetExists(m_assetManager, normalized)) return true;
#endif
  for (const auto& candidate : DiskCandidates(m_basePath, path, normalized)) {
    if (IsRegularFile(candidate) || IsDirectory(candidate)) {
      return true;
    }
  }
  return false;
}

bool ResourceLocator::ReadBinary(const std::string& path, std::vector<unsigned char>& out) const {
  out.clear();
  const std::string normalized = NormalizePath(path);

  for (const auto& candidate : DiskCandidates(m_basePath, path, normalized)) {
    if (ReadDiskBinary(candidate, out)) {
      return true;
    }
  }

#ifdef OS_ANDROID
  if (ReadAndroidAssetBinary(m_assetManager, normalized, out)) return true;
#endif

  return false;
}

bool ResourceLocator::ReadText(const std::string& path, std::string& out) const {
  std::vector<unsigned char> bytes;
  if (!ReadBinary(path, bytes)) {
    out.clear();
    return false;
  }
  out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return true;
}

bool ResourceLocator::WriteText(const std::string& path, const std::string& text) const {
  const std::filesystem::path requested(path);
  if (requested.is_absolute()) {
    std::error_code ec;
    const std::filesystem::path parent = requested.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, ec);
      if (ec) return false;
    }
    std::ofstream file(requested, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
  }

  const std::string normalized = NormalizePath(path);
  std::filesystem::path target = ResolveFilePath(normalized);
  if (target == std::filesystem::path(normalized) && !std::filesystem::path(normalized).is_absolute()) {
    target = ResolveCachePath(normalized);
  }

  std::error_code ec;
  const std::filesystem::path parent = target.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) return false;
  }

  std::ofstream file(target, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) return false;

  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  return file.good();
}

std::vector<std::string> ResourceLocator::List(const std::string& directory, bool recursive) const {
  std::vector<std::string> out;
  const std::string normalized = NormalizePath(directory);

#ifdef OS_ANDROID
  if (m_assetManager) {
    ListAndroidAssets(m_assetManager, normalized, recursive, out);
  }
#endif

  const std::filesystem::path dirPath = ResolveFilePath(normalized);
  std::filesystem::path diskDir = dirPath;
  if (!IsDirectory(diskDir)) {
    for (const auto& candidate : DiskCandidates(m_basePath, directory, normalized)) {
      if (IsDirectory(candidate)) {
        diskDir = candidate;
        break;
      }
    }
  }
  if (!IsDirectory(diskDir)) {
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
  }

  std::error_code ec;
  if (recursive) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(diskDir, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec)) continue;
      out.push_back(ToResourcePath(normalized, std::filesystem::relative(entry.path(), diskDir, ec)));
    }
  } else {
    for (const auto& entry : std::filesystem::directory_iterator(diskDir, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec)) continue;
      out.push_back(ToResourcePath(normalized, entry.path().filename()));
    }
  }

  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

bool ResourceLocator::FindFileByNameRecursive(const std::string& requestedPath,
                                              std::string& outPath,
                                              const std::vector<std::string>& searchDirectories) const {
  outPath.clear();

  const std::string normalized = NormalizePath(requestedPath);
  const std::filesystem::path requested(normalized);
  const std::string filename = requested.filename().string();
  if (filename.empty()) {
    return false;
  }

  const std::string targetFilename = ToLowerAsciiCopy(filename);
  std::vector<std::string> directories;
  for (const std::string& directory : searchDirectories) {
    AddUniqueResourceDirectory(directories, directory);
  }

  AddUniqueResourceDirectory(directories, ParentResourceDirectory(normalized));
  AddUniqueResourceDirectory(directories, FirstResourceDirectory(normalized));

  const std::string ext = ToLowerAsciiCopy(requested.extension().string());
  if (ext == ".glb" || ext == ".gltf") {
    AddUniqueResourceDirectory(directories, "Models");
  }

  if (std::find(directories.begin(), directories.end(), std::string()) == directories.end()) {
    directories.push_back(std::string());
  }

  for (const std::string& directory : directories) {
    const std::vector<std::string> entries = List(directory, true);
    for (const std::string& entry : entries) {
      const std::string candidateFilename = std::filesystem::path(entry).filename().string();
      if (ToLowerAsciiCopy(candidateFilename) == targetFilename) {
        outPath = NormalizePath(entry);
        return true;
      }
    }
  }

  return false;
}

#ifdef OS_ANDROID
void ResourceLocator::SetAndroidAssetManager(AAssetManager* manager) {
  m_assetManager = manager;
}

AAssetManager* ResourceLocator::GetAndroidAssetManager() const {
  return m_assetManager;
}
#endif

} // namespace t850
