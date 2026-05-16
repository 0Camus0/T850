#include <pch.h>

#include <utils/RuntimeProfile.h>
#include <Config.h>
#include <core/Config.h>
#include <scene/SceneDescriptor.h>

#include <algorithm>
#include <cctype>
#include <sstream>

#if defined(OS_WINDOWS)
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace t850 {
  namespace {
    RuntimeProfileInfo g_runtimeProfile;
    bool g_runtimeProfileDirty = true;
    std::string g_androidManufacturer;
    std::string g_androidModel;
    std::string g_androidHardware;
    std::string g_androidBoard;
    std::string g_androidSocModel;
    std::string g_gpuNameOverride;
    uint32_t g_gpuVendorId = 0;
    uint32_t g_gpuDeviceId = 0;

    std::string Lower(std::string value) {
      std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return value;
    }

    bool Contains(const std::string& haystack, const std::string& needle) {
      return Lower(haystack).find(Lower(needle)) != std::string::npos;
    }

    std::string DetectPlatform() {
#if defined(OS_ANDROID)
      return "android";
#elif defined(OS_WINDOWS)
      return "windows";
#elif defined(OS_LINUX)
      return "linux";
#else
      return "unknown";
#endif
    }

    std::string DetectArchitecture() {
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
      return "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
      return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
      return "x86";
#else
      return "unknown";
#endif
    }

#if defined(OS_WINDOWS)
    std::string WideToUtf8(const wchar_t* wide) {
      if (!wide || !*wide) return {};
      int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
      if (needed <= 1) return {};
      std::string out((size_t)needed - 1, '\0');
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
      return out;
    }

    bool DetectWindowsDxgiGpu(std::string& name, uint32_t& vendorId, uint32_t& deviceId) {
      using Microsoft::WRL::ComPtr;
      ComPtr<IDXGIFactory6> factory6;
      if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory6)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT index = 0; factory6->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++index) {
          DXGI_ADAPTER_DESC1 desc = {};
          if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
          name = WideToUtf8(desc.Description);
          vendorId = desc.VendorId;
          deviceId = desc.DeviceId;
          return true;
        }
      }

      ComPtr<IDXGIFactory1> factory;
      if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
      ComPtr<IDXGIAdapter1> adapter;
      for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc = {};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        name = WideToUtf8(desc.Description);
        vendorId = desc.VendorId;
        deviceId = desc.DeviceId;
        return true;
      }
      return false;
    }
#endif

    std::string VendorName(uint32_t vendorId, const std::string& gpuName) {
      if (vendorId == 0x5143) return "qualcomm";
      if (vendorId == 0x13b5) return "arm";
      if (vendorId == 0x1010) return "imagination";
      if (vendorId == 0x10de) return "nvidia";
      if (vendorId == 0x1002 || vendorId == 0x1022) return "amd";
      if (vendorId == 0x8086) return "intel";
      if (Contains(gpuName, "adreno") || Contains(gpuName, "qualcomm")) return "qualcomm";
      if (Contains(gpuName, "mali") || Contains(gpuName, "immortalis")) return "arm";
      if (Contains(gpuName, "powervr")) return "imagination";
      if (Contains(gpuName, "nvidia") || Contains(gpuName, "geforce") || Contains(gpuName, "rtx")) return "nvidia";
      if (Contains(gpuName, "radeon") || Contains(gpuName, "amd")) return "amd";
      if (Contains(gpuName, "intel") || Contains(gpuName, "arc")) return "intel";
      return {};
    }

    int FirstDigitAfterAdreno(const std::string& gpuName) {
      std::string lower = Lower(gpuName);
      size_t pos = lower.find("adreno");
      if (pos == std::string::npos) return -1;
      for (size_t i = pos + 6; i < lower.size(); ++i) {
        if (std::isdigit((unsigned char)lower[i])) return lower[i] - '0';
      }
      return -1;
    }

    std::string GpuFamily(const std::string& gpuName, uint32_t vendorId) {
      const std::string vendor = VendorName(vendorId, gpuName);
      if (vendor == "qualcomm" || Contains(gpuName, "adreno")) {
        int series = FirstDigitAfterAdreno(gpuName);
        if (series >= 8) return "adreno-800";
        if (series == 7) return "adreno-700";
        if (series > 0) return "adreno-" + std::to_string(series) + "00";
        return "adreno";
      }
      if (Contains(gpuName, "mali-g7") || Contains(gpuName, "immortalis-g7")) return "mali-g700";
      if (Contains(gpuName, "mali-g6") || Contains(gpuName, "immortalis-g6")) return "mali-g600";
      if (Contains(gpuName, "mali") || Contains(gpuName, "immortalis")) return "mali";
      if (Contains(gpuName, "powervr")) return "powervr";
      return vendor;
    }

    std::string RecommendedProfile(const RuntimeProfileInfo& info) {
      if (g_config.sceneProfile.size()) return g_config.sceneProfile;
      if (info.platform == "android") {
        if (info.gpuFamily == "adreno-800") return "android/adreno-800";
        if (info.gpuFamily == "adreno-700") return "android/adreno-700";
        return "android/default";
      }
      if (info.architecture == "arm64") return "pc/arm64";
      if (info.architecture == "x64") return "pc/x64";
      return "pc/base";
    }

    bool StringFieldMatches(const std::string& required, const std::string& actual) {
      return required.empty() || Lower(required) == Lower(actual);
    }
  }

  void SetAndroidBuildInfo(std::string manufacturer, std::string model, std::string hardware, std::string board, std::string socModel) {
    g_androidManufacturer = std::move(manufacturer);
    g_androidModel = std::move(model);
    g_androidHardware = std::move(hardware);
    g_androidBoard = std::move(board);
    g_androidSocModel = std::move(socModel);
    g_runtimeProfileDirty = true;
  }

  void SetRuntimeGpuInfo(std::string name, uint32_t vendorId, uint32_t deviceId) {
    g_gpuNameOverride = std::move(name);
    g_gpuVendorId = vendorId;
    g_gpuDeviceId = deviceId;
    g_runtimeProfileDirty = true;
  }

  void RefreshRuntimeProfileInfo() {
    RuntimeProfileInfo info;
    info.platform = DetectPlatform();
    info.architecture = DetectArchitecture();

    uint32_t vendorId = g_gpuVendorId;
    info.gpuName = g_gpuNameOverride;

#if defined(OS_WINDOWS)
    uint32_t deviceId = g_gpuDeviceId;
    DetectWindowsDxgiGpu(info.gpuName, vendorId, deviceId);
#endif

    info.gpuVendor = VendorName(vendorId, info.gpuName);
    info.gpuFamily = GpuFamily(info.gpuName, vendorId);
    if (info.platform == "android") {
      std::ostringstream device;
      bool hasDeviceText = false;
      auto appendDeviceText = [&](const std::string& text, const char* separator) {
        if (text.empty()) return;
        if (hasDeviceText) device << separator;
        device << text;
        hasDeviceText = true;
      };
      appendDeviceText(g_androidManufacturer, "");
      appendDeviceText(g_androidModel, " ");
      if (!g_androidSocModel.empty()) appendDeviceText(g_androidSocModel, " / ");
      else appendDeviceText(g_androidHardware, " / ");
      info.deviceModel = device.str();
    }
    info.recommendedProfile = RecommendedProfile(info);
    g_runtimeProfile = std::move(info);
    g_runtimeProfileDirty = false;
  }

  const RuntimeProfileInfo& GetRuntimeProfileInfo() {
    if (g_runtimeProfileDirty) RefreshRuntimeProfileInfo();
    return g_runtimeProfile;
  }

  const std::vector<ProfileTargetDesc>& GetProfileTargets() {
    static const std::vector<ProfileTargetDesc> targets = {
      {"PC base", "pc/base", "", "", ""},
      {"PC x64", "pc/x64", "windows", "x64", ""},
      {"PC ARM64", "pc/arm64", "windows", "arm64", ""},
      {"Android Adreno 700", "android/adreno-700", "android", "", "adreno-700"},
      {"Android Adreno 800", "android/adreno-800", "android", "", "adreno-800"},
    };
    return targets;
  }

  int ProfileTargetIndexByName(const std::string& name) {
    const auto& targets = GetProfileTargets();
    for (int i = 0; i < (int)targets.size(); ++i) {
      if (targets[i].name == name) return i;
    }
    return 0;
  }

  int DefaultProfileTargetIndex() {
    return ProfileTargetIndexByName(ActiveProfileName());
  }

  std::string ActiveProfileName() {
    return GetRuntimeProfileInfo().recommendedProfile;
  }

  void ApplyProfileTarget(SandboxProfileDesc& profile, int targetIndex) {
    const auto& targets = GetProfileTargets();
    if (targetIndex < 0 || targetIndex >= (int)targets.size()) targetIndex = 0;
    const auto& target = targets[targetIndex];
    profile.name = target.name == "pc/base" ? std::string{} : target.name;
    profile.platform = target.platform;
    profile.architecture = target.architecture;
    profile.gpu_family = target.gpuFamily;
  }

  int ScoreSceneProfileMatch(const SandboxProfileDesc& profile, const std::string& modelKey) {
    const RuntimeProfileInfo& runtime = GetRuntimeProfileInfo();
    if (!profile.model.empty()) {
      std::string profileModel = Lower(profile.model);
      std::replace(profileModel.begin(), profileModel.end(), '\\', '/');
      size_t slash = profileModel.find_last_of('/');
      if (slash != std::string::npos) profileModel = profileModel.substr(slash + 1);
      if (!modelKey.empty() && profileModel != Lower(modelKey)) return -1;
    }
    if (!StringFieldMatches(profile.platform, runtime.platform)) return -1;
    if (!StringFieldMatches(profile.architecture, runtime.architecture)) return -1;
    if (!StringFieldMatches(profile.gpu_family, runtime.gpuFamily)) return -1;
    if (!profile.gpu_name_contains.empty() && !Contains(runtime.gpuName, profile.gpu_name_contains)) return -1;

    int score = profile.model.empty() ? 0 : 4;
    if (!profile.platform.empty()) score += 16;
    if (!profile.architecture.empty()) score += 8;
    if (!profile.gpu_family.empty()) score += 32;
    if (!profile.gpu_name_contains.empty()) score += 24;
    if (!profile.name.empty()) {
      if (profile.name == ActiveProfileName()) score += 64;
      else score += 2;
    }
    return score;
  }
}