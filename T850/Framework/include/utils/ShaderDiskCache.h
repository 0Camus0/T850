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
    std::string stage;
    std::string entryPoint;
    std::string sourceName;
    std::string computeName;
    std::string profile;
    bool compute = false;
  };

  namespace ShaderDiskCache {
    constexpr int kCacheFormatVersion = 1;

    std::string ContentHash(const std::string& content);
    ShaderDiskCacheKey MakeStageKey(const std::string& api,
                    const std::string& compilerSignature,
                    uint64_t shaderKeyBits,
                    const std::string& stage,
                    const std::string& entryPoint,
                    const std::string& sourceName,
                    const std::string& source);

    ShaderDiskCacheKey MakeKey(const std::string& api,
                               const std::string& driverSignature,
                               uint64_t shaderKeyBits,
                               const std::string& vsName,
                               const std::string& fsName,
                               const std::string& vsSource,
                               const std::string& fsSource);

        ShaderDiskCacheKey MakeComputeKey(const std::string& api,
                          const std::string& driverSignature,
                          const std::string& computeName,
                          const std::string& entryPoint,
                          const std::string& profile,
                          const std::string& source);

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