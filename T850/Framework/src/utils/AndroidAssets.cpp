#include <pch.h>
#include <utils/AndroidAssets.h>
#include <utils/ResourceLocator.h>

#ifdef OS_ANDROID

namespace t850 {

void SetAndroidAssetManager(AAssetManager* manager) {
  ResourceLocator::Instance().SetAndroidAssetManager(manager);
}

AAssetManager* GetAndroidAssetManager() {
  return ResourceLocator::Instance().GetAndroidAssetManager();
}

bool ReadAndroidAssetBytes(const std::string& path, std::vector<unsigned char>& out) {
  return ResourceLocator::Instance().ReadBinary(path, out);
}

bool ReadAndroidAssetText(const std::string& path, std::string& out) {
  return ResourceLocator::Instance().ReadText(path, out);
}

} // namespace t850

#endif // OS_ANDROID
