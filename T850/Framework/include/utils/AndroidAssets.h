#pragma once

#include <Config.h>
#include <string>
#include <vector>

#ifdef OS_ANDROID
struct AAssetManager;
#endif

namespace t850 {

#ifdef OS_ANDROID
void SetAndroidAssetManager(AAssetManager* manager);
AAssetManager* GetAndroidAssetManager();
bool ReadAndroidAssetBytes(const std::string& path, std::vector<unsigned char>& out);
bool ReadAndroidAssetText(const std::string& path, std::string& out);
#else
inline void SetAndroidAssetManager(void*) {}
#endif

} // namespace t850
