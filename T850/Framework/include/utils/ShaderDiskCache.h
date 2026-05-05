#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace t850 {

  struct ShaderDiskCacheKey {
    std::string api;
    std::string sha1;
    uint64_t shaderKeyBits = 0;
    std::string vsName;
    std::string fsName;
  };

  namespace ShaderDiskCache {
    constexpr int kCacheFormatVersion = 1;

    ShaderDiskCacheKey MakeKey(const std::string& api,
                               const std::string& driverSignature,
                               uint64_t shaderKeyBits,
                               const std::string& vsName,
                               const std::string& fsName,
                               const std::string& vsSource,
                               const std::string& fsSource);

    bool LoadArtifact(const ShaderDiskCacheKey& key, const std::string& artifactName,
                      std::vector<uint8_t>& outBytes);
    bool StoreArtifact(const ShaderDiskCacheKey& key, const std::string& artifactName,
                       const void* data, size_t byteCount);

    bool LoadApiArtifact(const std::string& api, const std::string& artifactName,
                         std::vector<uint8_t>& outBytes);
    bool StoreApiArtifact(const std::string& api, const std::string& artifactName,
                          const void* data, size_t byteCount);

    void WriteManifest(const ShaderDiskCacheKey& key, const std::string& driverSignature);
    void EnsureApiMetadata(const std::string& api, const std::string& driverSignature);
  }
}