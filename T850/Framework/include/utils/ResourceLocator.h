#pragma once

#include <Config.h>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#ifdef OS_ANDROID
struct AAssetManager;
#endif

namespace t850 {

class ResourceLocator {
public:
  static ResourceLocator& Instance();

  static std::string NormalizePath(std::string path);

  void SetBasePath(const std::filesystem::path& basePath);
  const std::filesystem::path& GetBasePath() const;
  void SetCachePath(const std::filesystem::path& cachePath);
  const std::filesystem::path& GetCachePath() const;

  bool Exists(const std::string& path) const;
  bool ReadBinary(const std::string& path, std::vector<unsigned char>& out) const;
  bool ReadText(const std::string& path, std::string& out) const;
  bool WriteText(const std::string& path, const std::string& text) const;
  bool WriteBinaryAtomic(const std::string& path, std::span<const unsigned char> bytes) const;
  std::vector<std::string> List(const std::string& directory, bool recursive = false) const;
  bool FindFileByNameRecursive(const std::string& requestedPath,
                               std::string& outPath,
                               const std::vector<std::string>& searchDirectories = {}) const;

  // Returns a filesystem path for platforms/assets that are directly addressable on disk.
  // Android APK assets are not filesystem files, so callers must use ReadBinary/ReadText.
  std::filesystem::path ResolveFilePath(const std::string& path) const;
  std::filesystem::path ResolveCachePath(const std::string& path) const;

#ifdef OS_ANDROID
  void SetAndroidAssetManager(AAssetManager* manager);
  AAssetManager* GetAndroidAssetManager() const;
#endif

private:
  ResourceLocator();

  std::filesystem::path m_basePath;
  std::filesystem::path m_cachePath;

#ifdef OS_ANDROID
  AAssetManager* m_assetManager = nullptr;
#endif
};

} // namespace t850
