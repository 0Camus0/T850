#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace t850 {
  struct SandboxProfileDesc;

  struct RuntimeProfileInfo {
    std::string platform;
    std::string architecture;
    std::string gpuName;
    std::string gpuVendor;
    std::string gpuFamily;
    std::string deviceModel;
    std::string recommendedProfile;
  };

  struct ProfileTargetDesc {
    std::string label;
    std::string name;
    std::string platform;
    std::string architecture;
    std::string gpuFamily;
  };

  void SetAndroidBuildInfo(std::string manufacturer, std::string model, std::string hardware, std::string board, std::string socModel);
  void SetRuntimeGpuInfo(std::string name, uint32_t vendorId, uint32_t deviceId);
  const RuntimeProfileInfo& GetRuntimeProfileInfo();
  void RefreshRuntimeProfileInfo();

  const std::vector<ProfileTargetDesc>& GetProfileTargets();
  int DefaultProfileTargetIndex();
  int ProfileTargetIndexByName(const std::string& name);
  std::string ActiveProfileName();
  void ApplyProfileTarget(SandboxProfileDesc& profile, int targetIndex);
  int ScoreSceneProfileMatch(const SandboxProfileDesc& profile, const std::string& modelKey = {});
}