#include <pch.h>
#include <utils/AndroidAssets.h>

#ifdef OS_ANDROID

#include <android/asset_manager.h>
#include <utils/Log.h>

namespace t850 {
namespace {
  AAssetManager* g_assetManager = nullptr;

  std::string NormalizeAssetPath(std::string path) {
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
      path.erase(path.begin());
    }
    return path;
  }
}

void SetAndroidAssetManager(AAssetManager* manager) {
  g_assetManager = manager;
}

AAssetManager* GetAndroidAssetManager() {
  return g_assetManager;
}

bool ReadAndroidAssetBytes(const std::string& path, std::vector<unsigned char>& out) {
  if (!g_assetManager) return false;
  const std::string assetPath = NormalizeAssetPath(path);
  AAsset* asset = AAssetManager_open(g_assetManager, assetPath.c_str(), AASSET_MODE_BUFFER);
  if (!asset) return false;
  const off_t length = AAsset_getLength(asset);
  out.resize(static_cast<size_t>(length));
  const int read = AAsset_read(asset, out.data(), static_cast<size_t>(length));
  const bool ok = read >= 0 && read == length;
  if (!ok) {
    T8_LOG_ERROR("[AndroidAssets] Failed to read asset '%s'", assetPath.c_str());
    out.clear();
  }
  AAsset_close(asset);
  if (!ok) return false;
  return true;
}

bool ReadAndroidAssetText(const std::string& path, std::string& out) {
  std::vector<unsigned char> bytes;
  if (!ReadAndroidAssetBytes(path, bytes)) return false;
  out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return true;
}

} // namespace t850

#endif // OS_ANDROID
