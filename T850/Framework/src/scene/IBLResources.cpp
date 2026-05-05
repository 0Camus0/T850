#include <pch.h>
#include <scene/IBLResources.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <video/BaseDriver.h>
#include <utils/cil.h>
#include <utils/Log.h>

namespace t850 {
namespace {

  struct Vec3 {
    float x, y, z;
  };

  struct ImportanceSample {
    Vec3 direction;
    float pdf;
  };

  struct SourceCubemap {
    int size = 0;
    int mipCount = 0;
    std::vector<size_t> mipOffsets;
    std::vector<float> pixels;
  };

  constexpr float PI = 3.14159265358979323846f;
  constexpr int GeneratedDiffuseSize = 32;
  constexpr int GeneratedSpecularSize = 128;
  constexpr int GeneratedLowestMipLevel = 4;
  constexpr int GeneratedLambertianSamples = 128;
  constexpr int GeneratedGGXSamples = 128;
  constexpr int GeneratedCharlieSamples = 128;
  constexpr uint32_t IBLCacheVersion = 1;

  enum class IBLCacheKind : uint32_t {
    DiffuseCube = 1,
    SpecularCube = 2,
    CharlieCube = 3,
    GGXBrdfLUT = 4,
    CharlieLUT = 5,
    SheenELUT = 6
  };

  constexpr std::array<char, 8> IBLCacheMagic = {'T', '8', 'I', 'B', 'L', 'F', '3', '2'};

  float Saturate(float value) {
    return std::max(0.0f, std::min(1.0f, value));
  }

  Vec3 Normalize(Vec3 value) {
    float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.000001f)
      return {0.0f, 0.0f, 1.0f};
    float invLength = 1.0f / length;
    return {value.x * invLength, value.y * invLength, value.z * invLength};
  }

  Vec3 Add(Vec3 lhs, Vec3 rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
  }

  Vec3 Scale(Vec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
  }

  Vec3 Lerp(Vec3 lhs, Vec3 rhs, float t) {
    return {
      lhs.x + (rhs.x - lhs.x) * t,
      lhs.y + (rhs.y - lhs.y) * t,
      lhs.z + (rhs.z - lhs.z) * t
    };
  }

  float Dot(Vec3 lhs, Vec3 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
  }

  Vec3 Cross(Vec3 lhs, Vec3 rhs) {
    return {
      lhs.y * rhs.z - lhs.z * rhs.y,
      lhs.z * rhs.x - lhs.x * rhs.z,
      lhs.x * rhs.y - lhs.y * rhs.x
    };
  }

  Vec3 Reflect(Vec3 incident, Vec3 normal) {
    float scale = 2.0f * Dot(normal, incident);
    return {incident.x - scale * normal.x, incident.y - scale * normal.y, incident.z - scale * normal.z};
  }

  int ClampInt(int value, int minValue, int maxValue) {
    return std::max(minValue, std::min(maxValue, value));
  }

  int MipSize(int baseSize, int mip) {
    int size = baseSize >> mip;
    return size > 0 ? size : 1;
  }

  int GeneratedSpecularMipCount(int size) {
    int log2Size = 0;
    int value = size;
    while (value > 1) {
      value >>= 1;
      ++log2Size;
    }
    return std::max(1, log2Size + 2 - GeneratedLowestMipLevel);
  }

  size_t TotalFloatCubeTexelCount(int size, int mipCount) {
    size_t total = 0;
    for (int mip = 0; mip < mipCount; ++mip) {
      int mipSize = MipSize(size, mip);
      total += size_t(mipSize) * size_t(mipSize) * 6u * 4u;
    }
    return total;
  }

  size_t TotalFloatImageTexelCount(int width, int height) {
    return size_t(width) * size_t(height) * 4u;
  }

  uint64_t FNV1a64(const void* data, size_t size, uint64_t seed = 0xcbf29ce484222325ull) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = seed;
    for (size_t i = 0; i < size; ++i) {
      hash ^= uint64_t(bytes[i]);
      hash *= 0x100000001b3ull;
    }
    return hash;
  }

  template <typename T>
  uint64_t HashValue(uint64_t seed, const T& value) {
    return FNV1a64(&value, sizeof(T), seed);
  }

  uint64_t HashString(uint64_t seed, const std::string& value) {
    return FNV1a64(value.data(), value.size(), seed);
  }

  const char* CacheKindName(IBLCacheKind kind) {
    switch (kind) {
    case IBLCacheKind::DiffuseCube: return "diffuse_cube";
    case IBLCacheKind::SpecularCube: return "ggx_specular_cube";
    case IBLCacheKind::CharlieCube: return "charlie_sheen_cube";
    case IBLCacheKind::GGXBrdfLUT: return "ggx_brdf_lut";
    case IBLCacheKind::CharlieLUT: return "charlie_lut";
    case IBLCacheKind::SheenELUT: return "sheen_e_lut";
    default: return "unknown";
    }
  }

  std::filesystem::path IBLCacheRoot() {
    return std::filesystem::path("Textures") / "GeneratedIBLCache";
  }

  uint64_t BuildIBLCacheKey(
    IBLCacheKind kind,
    const std::string& sourcePath,
    int width,
    int height,
    int faceCount,
    int mipCount,
    int sampleCount) {
    uint64_t hash = 0xcbf29ce484222325ull;
    const uint32_t kindValue = static_cast<uint32_t>(kind);
    hash = HashValue(hash, IBLCacheVersion);
    hash = HashValue(hash, kindValue);
    hash = HashValue(hash, width);
    hash = HashValue(hash, height);
    hash = HashValue(hash, faceCount);
    hash = HashValue(hash, mipCount);
    hash = HashValue(hash, sampleCount);
    hash = HashValue(hash, GeneratedDiffuseSize);
    hash = HashValue(hash, GeneratedSpecularSize);
    hash = HashValue(hash, GeneratedLowestMipLevel);
    hash = HashValue(hash, GeneratedLambertianSamples);
    hash = HashValue(hash, GeneratedGGXSamples);
    hash = HashValue(hash, GeneratedCharlieSamples);

    if (!sourcePath.empty()) {
      std::error_code ec;
      std::filesystem::path path(sourcePath);
      std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
      const std::string normalized = ec ? path.generic_string() : canonical.generic_string();
      hash = HashString(hash, normalized);

      ec.clear();
      uint64_t fileSize = 0;
      if (std::filesystem::exists(path, ec)) {
        ec.clear();
        const auto measuredSize = std::filesystem::file_size(path, ec);
        if (!ec)
          fileSize = static_cast<uint64_t>(measuredSize);
      }
      hash = HashValue(hash, fileSize);

      ec.clear();
      const auto writeTime = std::filesystem::last_write_time(path, ec);
      const int64_t writeTicks = ec ? 0ll : static_cast<int64_t>(writeTime.time_since_epoch().count());
      hash = HashValue(hash, writeTicks);
    }

    return hash;
  }

  std::filesystem::path IBLCachePath(
    IBLCacheKind kind,
    const std::string& sourcePath,
    int width,
    int height,
    int faceCount,
    int mipCount,
    int sampleCount) {
    const uint64_t key = BuildIBLCacheKey(kind, sourcePath, width, height, faceCount, mipCount, sampleCount);
    std::ostringstream name;
    name << CacheKindName(kind) << "_v" << IBLCacheVersion << "_"
         << std::hex << std::setw(16) << std::setfill('0') << key << ".t8ibl";
    return IBLCacheRoot() / name.str();
  }

  template <typename T>
  bool ReadPod(std::ifstream& file, T& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(T));
    return file.good();
  }

  template <typename T>
  void WritePod(std::ofstream& file, const T& value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(T));
  }

  bool LoadFloatCache(
    const std::filesystem::path& path,
    IBLCacheKind expectedKind,
    int expectedWidth,
    int expectedHeight,
    int expectedFaceCount,
    int expectedMipCount,
    int expectedSampleCount,
    std::vector<float>& outData) {
    std::error_code existsError;
    if (!std::filesystem::exists(path, existsError))
      return false;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      T8_LOG_INFO("[IBL] Cache exists but cannot be opened: '%s'", path.string().c_str());
      return false;
    }

    std::array<char, 8> magic = {};
    file.read(magic.data(), magic.size());

    uint32_t version = 0;
    uint32_t kind = 0;
    int width = 0;
    int height = 0;
    int faceCount = 0;
    int mipCount = 0;
    int channels = 0;
    int sampleCount = 0;
    uint64_t dataBytes = 0;
    const bool headerOk =
      file.good()
      && magic == IBLCacheMagic
      && ReadPod(file, version)
      && ReadPod(file, kind)
      && ReadPod(file, width)
      && ReadPod(file, height)
      && ReadPod(file, faceCount)
      && ReadPod(file, mipCount)
      && ReadPod(file, channels)
      && ReadPod(file, sampleCount)
      && ReadPod(file, dataBytes);

    const size_t expectedFloats = expectedFaceCount == 6
      ? TotalFloatCubeTexelCount(expectedWidth, expectedMipCount)
      : TotalFloatImageTexelCount(expectedWidth, expectedHeight);
    const uint64_t expectedBytes = uint64_t(expectedFloats) * uint64_t(sizeof(float));

    if (!headerOk
        || version != IBLCacheVersion
        || kind != static_cast<uint32_t>(expectedKind)
        || width != expectedWidth
        || height != expectedHeight
        || faceCount != expectedFaceCount
        || mipCount != expectedMipCount
        || channels != 4
        || sampleCount != expectedSampleCount
        || dataBytes != expectedBytes) {
      T8_LOG_INFO("[IBL] Ignoring stale generated cache '%s'", path.string().c_str());
      return false;
    }

    outData.resize(expectedFloats);
    file.read(reinterpret_cast<char*>(outData.data()), static_cast<std::streamsize>(dataBytes));
    if (!file.good()) {
      outData.clear();
      T8_LOG_INFO("[IBL] Ignoring truncated generated cache '%s'", path.string().c_str());
      return false;
    }

    return true;
  }

  bool SaveFloatCache(
    const std::filesystem::path& path,
    IBLCacheKind kind,
    int width,
    int height,
    int faceCount,
    int mipCount,
    int sampleCount,
    const std::vector<float>& data) {
    const size_t expectedFloats = faceCount == 6
      ? TotalFloatCubeTexelCount(width, mipCount)
      : TotalFloatImageTexelCount(width, height);
    if (data.size() != expectedFloats)
      return false;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      T8_LOG_INFO("[IBL] Cannot create generated cache dir '%s': %s",
                  path.parent_path().string().c_str(), ec.message().c_str());
      return false;
    }

    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";

    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      T8_LOG_INFO("[IBL] Cannot write generated cache '%s'", tmpPath.string().c_str());
      return false;
    }

    const uint32_t version = IBLCacheVersion;
    const uint32_t kindValue = static_cast<uint32_t>(kind);
    const int channels = 4;
    const uint64_t dataBytes = uint64_t(data.size()) * uint64_t(sizeof(float));

    file.write(IBLCacheMagic.data(), IBLCacheMagic.size());
    WritePod(file, version);
    WritePod(file, kindValue);
    WritePod(file, width);
    WritePod(file, height);
    WritePod(file, faceCount);
    WritePod(file, mipCount);
    WritePod(file, channels);
    WritePod(file, sampleCount);
    WritePod(file, dataBytes);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(dataBytes));
    file.close();

    if (!file.good()) {
      T8_LOG_INFO("[IBL] Failed while writing generated cache '%s'", tmpPath.string().c_str());
      std::filesystem::remove(tmpPath, ec);
      return false;
    }

    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
      T8_LOG_INFO("[IBL] Cannot finalize generated cache '%s': %s", path.string().c_str(), ec.message().c_str());
      std::filesystem::remove(tmpPath, ec);
      return false;
    }

    T8_LOG_INFO("[IBL] Cached generated %s data: '%s'", CacheKindName(kind), path.string().c_str());
    return true;
  }

  bool LoadCachedFloatCube(
    IBLCacheKind kind,
    const std::string& sourcePath,
    int size,
    int mipCount,
    int sampleCount,
    std::vector<float>& outData) {
    const auto path = IBLCachePath(kind, sourcePath, size, size, 6, mipCount, sampleCount);
    return LoadFloatCache(path, kind, size, size, 6, mipCount, sampleCount, outData);
  }

  void SaveCachedFloatCube(
    IBLCacheKind kind,
    const std::string& sourcePath,
    int size,
    int mipCount,
    int sampleCount,
    const std::vector<float>& data) {
    const auto path = IBLCachePath(kind, sourcePath, size, size, 6, mipCount, sampleCount);
    SaveFloatCache(path, kind, size, size, 6, mipCount, sampleCount, data);
  }

  bool LoadCachedFloatImage(
    IBLCacheKind kind,
    int width,
    int height,
    int sampleCount,
    std::vector<float>& outData) {
    const auto path = IBLCachePath(kind, std::string(), width, height, 1, 1, sampleCount);
    return LoadFloatCache(path, kind, width, height, 1, 1, sampleCount, outData);
  }

  void SaveCachedFloatImage(
    IBLCacheKind kind,
    int width,
    int height,
    int sampleCount,
    const std::vector<float>& data) {
    const auto path = IBLCachePath(kind, std::string(), width, height, 1, 1, sampleCount);
    SaveFloatCache(path, kind, width, height, 1, 1, sampleCount, data);
  }

  void BuildMipOffsets(SourceCubemap& cubemap) {
    cubemap.mipOffsets.assign(size_t(6) * size_t(cubemap.mipCount), 0);
    size_t offset = 0;
    for (int face = 0; face < 6; ++face) {
      for (int mip = 0; mip < cubemap.mipCount; ++mip) {
        cubemap.mipOffsets[size_t(face) * size_t(cubemap.mipCount) + size_t(mip)] = offset;
        int mipSize = MipSize(cubemap.size, mip);
        offset += size_t(mipSize) * size_t(mipSize) * 4u;
      }
    }
  }

  float HalfToFloat(uint16_t value) {
    float sign = (value & 0x8000u) ? -1.0f : 1.0f;
    int exponent = int((value >> 10u) & 0x1Fu);
    int mantissa = int(value & 0x03FFu);
    if (exponent == 0)
      return sign * std::ldexp(float(mantissa), -24);
    if (exponent == 31)
      return mantissa ? std::numeric_limits<float>::quiet_NaN() : sign * std::numeric_limits<float>::infinity();
    return sign * std::ldexp(float(1024 + mantissa), exponent - 25);
  }

  float RadicalInverseVdC(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
  }

  float DistributionGGX(float ndotH, float roughness) {
    float a = ndotH * roughness;
    float k = roughness / std::max(1.0f - ndotH * ndotH + a * a, 0.000001f);
    return k * k * (1.0f / PI);
  }

  float DistributionCharlie(float alpha, float ndotH) {
    alpha = std::max(alpha, 0.000001f);
    float invR = 1.0f / alpha;
    float cos2h = ndotH * ndotH;
    float sin2h = std::max(1.0f - cos2h, 0.0f);
    return (2.0f + invR) * std::pow(sin2h, invR * 0.5f) / (2.0f * PI);
  }

  ImportanceSample ImportanceSampleLambertian(float xiX, float xiY) {
    float cosTheta = std::sqrt(1.0f - xiY);
    float sinTheta = std::sqrt(xiY);
    float phi = 2.0f * PI * xiX;
    return {{std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta}, cosTheta / PI};
  }

  ImportanceSample ImportanceSampleGGX(float xiX, float xiY, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float cosTheta = std::sqrt((1.0f - xiY) / std::max(1.0f + (alpha2 - 1.0f) * xiY, 0.000001f));
    float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
    float phi = 2.0f * PI * xiX;
    float pdf = DistributionGGX(cosTheta, alpha) / 4.0f;
    return {{std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta}, pdf};
  }

  ImportanceSample ImportanceSampleCharlie(float xiX, float xiY, float roughness) {
    float alpha = roughness * roughness;
    float sinTheta = std::pow(xiY, alpha / std::max(2.0f * alpha + 1.0f, 0.000001f));
    float cosTheta = std::sqrt(std::max(1.0f - sinTheta * sinTheta, 0.0f));
    float phi = 2.0f * PI * xiX;
    float pdf = DistributionCharlie(alpha, cosTheta) / 4.0f;
    return {{std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta}, pdf};
  }

  void GenerateTBN(Vec3 normal, Vec3& tangent, Vec3& bitangent) {
    bitangent = {0.0f, 1.0f, 0.0f};
    float ndotUp = Dot(normal, bitangent);
    if (1.0f - std::fabs(ndotUp) <= 0.0000001f) {
      bitangent = ndotUp > 0.0f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 0.0f, -1.0f};
    }
    tangent = Normalize(Cross(bitangent, normal));
    bitangent = Cross(normal, tangent);
  }

  ImportanceSample GetImportanceSample(int sampleIndex, int sampleCount, Vec3 normal, float roughness, bool ggx) {
    float xiX = float(sampleIndex) / float(sampleCount);
    float xiY = RadicalInverseVdC(uint32_t(sampleIndex));
    ImportanceSample sample = ggx ? ImportanceSampleGGX(xiX, xiY, roughness) : ImportanceSampleLambertian(xiX, xiY);

    Vec3 tangent, bitangent;
    GenerateTBN(normal, tangent, bitangent);
    Vec3 local = Normalize(sample.direction);
    sample.direction = Normalize(Add(Add(Scale(tangent, local.x), Scale(bitangent, local.y)), Scale(normal, local.z)));
    return sample;
  }

  ImportanceSample GetCharlieImportanceSample(int sampleIndex, int sampleCount, Vec3 normal, float roughness) {
    float xiX = float(sampleIndex) / float(sampleCount);
    float xiY = RadicalInverseVdC(uint32_t(sampleIndex));
    ImportanceSample sample = ImportanceSampleCharlie(xiX, xiY, roughness);

    Vec3 tangent, bitangent;
    GenerateTBN(normal, tangent, bitangent);
    Vec3 local = Normalize(sample.direction);
    sample.direction = Normalize(Add(Add(Scale(tangent, local.x), Scale(bitangent, local.y)), Scale(normal, local.z)));
    return sample;
  }

  Vec3 DirectionFromFaceUV(int face, float uvX, float uvY) {
    switch (face) {
    case 0: return Normalize({ 1.0f, -uvY, -uvX});
    case 1: return Normalize({-1.0f, -uvY,  uvX});
    case 2: return Normalize({ uvX,  1.0f,  uvY});
    case 3: return Normalize({ uvX, -1.0f, -uvY});
    case 4: return Normalize({ uvX, -uvY,  1.0f});
    default: return Normalize({-uvX, -uvY, -1.0f});
    }
  }

  void DirectionToFaceUV(Vec3 direction, int& face, float& u, float& v) {
    float absX = std::fabs(direction.x);
    float absY = std::fabs(direction.y);
    float absZ = std::fabs(direction.z);
    float uvX = 0.0f;
    float uvY = 0.0f;

    if (absX >= absY && absX >= absZ) {
      if (direction.x >= 0.0f) {
        face = 0;
        uvX = -direction.z / absX;
        uvY = -direction.y / absX;
      } else {
        face = 1;
        uvX = direction.z / absX;
        uvY = -direction.y / absX;
      }
    } else if (absY >= absZ) {
      if (direction.y >= 0.0f) {
        face = 2;
        uvX = direction.x / absY;
        uvY = direction.z / absY;
      } else {
        face = 3;
        uvX = direction.x / absY;
        uvY = -direction.z / absY;
      }
    } else {
      if (direction.z >= 0.0f) {
        face = 4;
        uvX = direction.x / absZ;
        uvY = -direction.y / absZ;
      } else {
        face = 5;
        uvX = -direction.x / absZ;
        uvY = -direction.y / absZ;
      }
    }

    u = Saturate(uvX * 0.5f + 0.5f);
    v = Saturate(uvY * 0.5f + 0.5f);
  }

  float SmithGGXCorrelated(float ndotV, float ndotL, float roughness) {
    float alpha2 = roughness * roughness * roughness * roughness;
    float ggxV = ndotL * std::sqrt(ndotV * ndotV * (1.0f - alpha2) + alpha2);
    float ggxL = ndotV * std::sqrt(ndotL * ndotL * (1.0f - alpha2) + alpha2);
    return 0.5f / std::max(ggxV + ggxL, 0.000001f);
  }

  Vec3 FetchCubemapTexel(const SourceCubemap& source, int face, int mip, int x, int y) {
    int size = MipSize(source.size, mip);
    x = ClampInt(x, 0, size - 1);
    y = ClampInt(y, 0, size - 1);
    size_t offset = source.mipOffsets[size_t(face) * size_t(source.mipCount) + size_t(mip)] + (size_t(y) * size_t(size) + size_t(x)) * 4u;
    return {source.pixels[offset + 0], source.pixels[offset + 1], source.pixels[offset + 2]};
  }

  Vec3 SampleCubemapMip(const SourceCubemap& source, Vec3 direction, int mip) {
    int face = 0;
    float u = 0.0f;
    float v = 0.0f;
    DirectionToFaceUV(Normalize(direction), face, u, v);

    int size = MipSize(source.size, mip);
    float x = u * float(size - 1);
    float y = v * float(size - 1);
    int x0 = int(std::floor(x));
    int y0 = int(std::floor(y));
    int x1 = std::min(x0 + 1, size - 1);
    int y1 = std::min(y0 + 1, size - 1);
    float tx = x - float(x0);
    float ty = y - float(y0);

    Vec3 c00 = FetchCubemapTexel(source, face, mip, x0, y0);
    Vec3 c10 = FetchCubemapTexel(source, face, mip, x1, y0);
    Vec3 c01 = FetchCubemapTexel(source, face, mip, x0, y1);
    Vec3 c11 = FetchCubemapTexel(source, face, mip, x1, y1);
    return Lerp(Lerp(c00, c10, tx), Lerp(c01, c11, tx), ty);
  }

  Vec3 SampleCubemapLod(const SourceCubemap& source, Vec3 direction, float lod) {
    float clampedLod = std::max(0.0f, std::min(lod, float(source.mipCount - 1)));
    int mip0 = int(std::floor(clampedLod));
    int mip1 = std::min(mip0 + 1, source.mipCount - 1);
    float blend = clampedLod - float(mip0);
    return Lerp(SampleCubemapMip(source, direction, mip0), SampleCubemapMip(source, direction, mip1), blend);
  }

  float ComputeSourceLod(float pdf, int sourceWidth, int sampleCount) {
    if (pdf <= 0.000001f)
      return float(std::max(0, int(std::log2(float(sourceWidth)))));
    return 0.5f * std::log2(6.0f * float(sourceWidth) * float(sourceWidth) / (float(sampleCount) * pdf));
  }

  bool ConvertLoadedCubemapToFloat(unsigned char* buffer, int size, unsigned int mipCount, unsigned int props, SourceCubemap& source) {
    if (!buffer || size <= 0 || mipCount == 0 || (props & CIL_CUBE_MAP) == 0 || (props & CIL_COMPRESSED) != 0)
      return false;

    source.size = size;
    source.mipCount = int(mipCount);
    source.pixels.assign(TotalFloatCubeTexelCount(size, source.mipCount), 0.0f);
    BuildMipOffsets(source);

    const unsigned char* src = buffer;
    for (int face = 0; face < 6; ++face) {
      for (int mip = 0; mip < source.mipCount; ++mip) {
        int mipSize = MipSize(size, mip);
        size_t dstOffset = source.mipOffsets[size_t(face) * size_t(source.mipCount) + size_t(mip)];
        size_t texelCount = size_t(mipSize) * size_t(mipSize);

        if (props & CIL_HALF_FLOAT) {
          const uint16_t* srcHalf = reinterpret_cast<const uint16_t*>(src);
          for (size_t texel = 0; texel < texelCount; ++texel) {
            source.pixels[dstOffset + texel * 4u + 0] = HalfToFloat(srcHalf[texel * 4u + 0]);
            source.pixels[dstOffset + texel * 4u + 1] = HalfToFloat(srcHalf[texel * 4u + 1]);
            source.pixels[dstOffset + texel * 4u + 2] = HalfToFloat(srcHalf[texel * 4u + 2]);
            source.pixels[dstOffset + texel * 4u + 3] = HalfToFloat(srcHalf[texel * 4u + 3]);
          }
          src += texelCount * 8u;
        } else if (props & CIL_RGBA) {
          for (size_t texel = 0; texel < texelCount; ++texel) {
            source.pixels[dstOffset + texel * 4u + 0] = float(src[texel * 4u + 0]) / 255.0f;
            source.pixels[dstOffset + texel * 4u + 1] = float(src[texel * 4u + 1]) / 255.0f;
            source.pixels[dstOffset + texel * 4u + 2] = float(src[texel * 4u + 2]) / 255.0f;
            source.pixels[dstOffset + texel * 4u + 3] = float(src[texel * 4u + 3]) / 255.0f;
          }
          src += texelCount * 4u;
        } else if (props & CIL_RGB) {
          for (size_t texel = 0; texel < texelCount; ++texel) {
            source.pixels[dstOffset + texel * 4u + 0] = float(src[texel * 3u + 0]) / 255.0f;
            source.pixels[dstOffset + texel * 4u + 1] = float(src[texel * 3u + 1]) / 255.0f;
            source.pixels[dstOffset + texel * 4u + 2] = float(src[texel * 3u + 2]) / 255.0f;
            source.pixels[dstOffset + texel * 4u + 3] = 1.0f;
          }
          src += texelCount * 3u;
        } else {
          return false;
        }
      }
    }

    return true;
  }

  bool LoadSourceCubemap(BaseDriver* driver, int skyTextureIndex, SourceCubemap& source) {
    if (!driver || skyTextureIndex < 0)
      return false;

    Texture* skyTexture = driver->GetTexture(skyTextureIndex);
    if (!skyTexture || skyTexture->filepath.empty())
      return false;

    int width = 0;
    int height = 0;
    unsigned int mipmaps = 0;
    unsigned int props = 0;
    unsigned int bufferSize = 0;
    unsigned char* buffer = cil_load(skyTexture->filepath.c_str(), &width, &height, &mipmaps, &props, &bufferSize);
    if (!buffer) {
      T8_LOG_INFO("[IBL] Cannot generate filtered IBL: failed to load '%s'", skyTexture->filepath.c_str());
      return false;
    }

    bool converted = width == height && ConvertLoadedCubemapToFloat(buffer, width, mipmaps, props, source);
    cil_free_buffer(buffer, props);

    if (!converted) {
      T8_LOG_INFO("[IBL] Cannot generate filtered IBL from '%s' (cube=%d compressed=%d size=%dx%d mips=%u)",
                  skyTexture->filepath.c_str(), (props & CIL_CUBE_MAP) != 0, (props & CIL_COMPRESSED) != 0, width, height, mipmaps);
      return false;
    }

    T8_LOG_INFO("[IBL] Source cubemap loaded for filtering: '%s' %dx%d mips=%d", skyTexture->filepath.c_str(), source.size, source.size, source.mipCount);
    return true;
  }

  Vec3 FilterLambertian(const SourceCubemap& source, Vec3 normal, int sampleCount) {
    Vec3 color = {0.0f, 0.0f, 0.0f};
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
      ImportanceSample sample = GetImportanceSample(sampleIndex, sampleCount, normal, 0.0f, false);
      float lod = ComputeSourceLod(sample.pdf, source.size, sampleCount);
      color = Add(color, SampleCubemapLod(source, sample.direction, lod));
    }
    return Scale(color, 1.0f / float(sampleCount));
  }

  Vec3 FilterGGX(const SourceCubemap& source, Vec3 normal, float roughness, int sampleCount) {
    Vec3 color = {0.0f, 0.0f, 0.0f};
    float weight = 0.0f;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
      ImportanceSample sample = GetImportanceSample(sampleIndex, sampleCount, normal, roughness, true);
      Vec3 light = Normalize(Reflect(Scale(normal, -1.0f), sample.direction));
      float ndotL = Dot(normal, light);
      if (ndotL > 0.0f) {
        float lod = roughness <= 0.0f ? 0.0f : ComputeSourceLod(sample.pdf, source.size, sampleCount);
        color = Add(color, Scale(SampleCubemapLod(source, light, lod), ndotL));
        weight += ndotL;
      }
    }

    if (weight > 0.0f)
      return Scale(color, 1.0f / weight);
    return Scale(color, 1.0f / float(sampleCount));
  }

  Vec3 FilterCharlie(const SourceCubemap& source, Vec3 normal, float roughness, int sampleCount) {
    Vec3 color = {0.0f, 0.0f, 0.0f};
    float weight = 0.0f;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
      ImportanceSample sample = GetCharlieImportanceSample(sampleIndex, sampleCount, normal, roughness);
      Vec3 light = Normalize(Reflect(Scale(normal, -1.0f), sample.direction));
      float ndotL = Dot(normal, light);
      if (ndotL > 0.0f) {
        float lod = roughness <= 0.0f ? 0.0f : ComputeSourceLod(sample.pdf, source.size, sampleCount);
        color = Add(color, Scale(SampleCubemapLod(source, light, lod), ndotL));
        weight += ndotL;
      }
    }

    if (weight > 0.0f)
      return Scale(color, 1.0f / weight);
    return Scale(color, 1.0f / float(sampleCount));
  }

  void StoreColor(std::vector<float>& data, size_t offset, Vec3 color) {
    data[offset + 0] = std::max(color.x, 0.0f);
    data[offset + 1] = std::max(color.y, 0.0f);
    data[offset + 2] = std::max(color.z, 0.0f);
    data[offset + 3] = 1.0f;
  }

  void GenerateFilteredDiffuseCubemap(const SourceCubemap& source, int size, int sampleCount, std::vector<float>& data) {
    data.assign(TotalFloatCubeTexelCount(size, 1), 0.0f);
    for (int face = 0; face < 6; ++face) {
      for (int y = 0; y < size; ++y) {
        float uvY = (float(y) + 0.5f) / float(size) * 2.0f - 1.0f;
        for (int x = 0; x < size; ++x) {
          float uvX = (float(x) + 0.5f) / float(size) * 2.0f - 1.0f;
          Vec3 normal = DirectionFromFaceUV(face, uvX, uvY);
          size_t offset = (size_t(face) * size_t(size) * size_t(size) + size_t(y) * size_t(size) + size_t(x)) * 4u;
          StoreColor(data, offset, FilterLambertian(source, normal, sampleCount));
        }
      }
    }
  }

  void GenerateFilteredSpecularCubemap(const SourceCubemap& source, int size, int mipCount, int sampleCount, std::vector<float>& data) {
    data.assign(TotalFloatCubeTexelCount(size, mipCount), 0.0f);
    std::vector<size_t> offsets(size_t(6) * size_t(mipCount), 0);
    size_t offset = 0;
    for (int face = 0; face < 6; ++face) {
      for (int mip = 0; mip < mipCount; ++mip) {
        offsets[size_t(face) * size_t(mipCount) + size_t(mip)] = offset;
        int mipSize = MipSize(size, mip);
        offset += size_t(mipSize) * size_t(mipSize) * 4u;
      }
    }

    for (int face = 0; face < 6; ++face) {
      for (int mip = 0; mip < mipCount; ++mip) {
        int mipSize = MipSize(size, mip);
        float roughness = mipCount > 1 ? float(mip) / float(mipCount - 1) : 0.0f;
        for (int y = 0; y < mipSize; ++y) {
          float uvY = (float(y) + 0.5f) / float(mipSize) * 2.0f - 1.0f;
          for (int x = 0; x < mipSize; ++x) {
            float uvX = (float(x) + 0.5f) / float(mipSize) * 2.0f - 1.0f;
            Vec3 normal = DirectionFromFaceUV(face, uvX, uvY);
            size_t pixelOffset = offsets[size_t(face) * size_t(mipCount) + size_t(mip)] + (size_t(y) * size_t(mipSize) + size_t(x)) * 4u;
            StoreColor(data, pixelOffset, FilterGGX(source, normal, roughness, sampleCount));
          }
        }
      }
    }
  }

  void GenerateFilteredCharlieCubemap(const SourceCubemap& source, int size, int mipCount, int sampleCount, std::vector<float>& data) {
    data.assign(TotalFloatCubeTexelCount(size, mipCount), 0.0f);
    std::vector<size_t> offsets(size_t(6) * size_t(mipCount), 0);
    size_t offset = 0;
    for (int face = 0; face < 6; ++face) {
      for (int mip = 0; mip < mipCount; ++mip) {
        offsets[size_t(face) * size_t(mipCount) + size_t(mip)] = offset;
        int mipSize = MipSize(size, mip);
        offset += size_t(mipSize) * size_t(mipSize) * 4u;
      }
    }

    for (int face = 0; face < 6; ++face) {
      for (int mip = 0; mip < mipCount; ++mip) {
        int mipSize = MipSize(size, mip);
        float roughness = mipCount > 1 ? float(mip) / float(mipCount - 1) : 0.0f;
        for (int y = 0; y < mipSize; ++y) {
          float uvY = (float(y) + 0.5f) / float(mipSize) * 2.0f - 1.0f;
          for (int x = 0; x < mipSize; ++x) {
            float uvX = (float(x) + 0.5f) / float(mipSize) * 2.0f - 1.0f;
            Vec3 normal = DirectionFromFaceUV(face, uvX, uvY);
            size_t pixelOffset = offsets[size_t(face) * size_t(mipCount) + size_t(mip)] + (size_t(y) * size_t(mipSize) + size_t(x)) * 4u;
            StoreColor(data, pixelOffset, FilterCharlie(source, normal, roughness, sampleCount));
          }
        }
      }
    }
  }

  float VisibilityAshikhmin(float ndotL, float ndotV) {
    return Saturate(1.0f / std::max(4.0f * (ndotL + ndotV - ndotL * ndotV), 0.000001f));
  }

  float LerpScalar(float lhs, float rhs, float t) {
    return lhs + (rhs - lhs) * t;
  }

  float LambdaSheenNumericHelper(float x, float alphaG) {
    float oneMinusAlphaSq = (1.0f - alphaG) * (1.0f - alphaG);
    float a = LerpScalar(21.5473f, 25.3245f, oneMinusAlphaSq);
    float b = LerpScalar(3.82987f, 3.32435f, oneMinusAlphaSq);
    float c = LerpScalar(0.19823f, 0.16801f, oneMinusAlphaSq);
    float d = LerpScalar(-1.97760f, -1.27393f, oneMinusAlphaSq);
    float e = LerpScalar(-4.32054f, -4.85967f, oneMinusAlphaSq);
    return a / (1.0f + b * std::pow(std::max(x, 0.0f), c)) + d * x + e;
  }

  float LambdaSheen(float cosTheta, float alphaG) {
    cosTheta = Saturate(cosTheta);
    if (std::fabs(cosTheta) < 0.5f)
      return std::exp(LambdaSheenNumericHelper(cosTheta, alphaG));
    return std::exp(2.0f * LambdaSheenNumericHelper(0.5f, alphaG)
                    - LambdaSheenNumericHelper(1.0f - cosTheta, alphaG));
  }

  float VisibilitySheen(float ndotL, float ndotV, float sheenRoughness) {
    sheenRoughness = std::max(sheenRoughness, 0.000001f);
    float alphaG = sheenRoughness * sheenRoughness;
    float denom = std::max((1.0f + LambdaSheen(ndotV, alphaG) + LambdaSheen(ndotL, alphaG))
                           * (4.0f * ndotV * ndotL), 0.000001f);
    return Saturate(1.0f / denom);
  }

  float DistributionCharlieRoughness(float sheenRoughness, float ndotH) {
    sheenRoughness = std::max(sheenRoughness, 0.000001f);
    float alphaG = sheenRoughness * sheenRoughness;
    float invR = 1.0f / alphaG;
    float cos2h = ndotH * ndotH;
    float sin2h = std::max(1.0f - cos2h, 0.0f);
    return (2.0f + invR) * std::pow(sin2h, invR * 0.5f) / (2.0f * PI);
  }

  void GenerateGGXBrdfLUT(std::vector<float>& data, int resolution, int sampleCount) {
    data.assign(size_t(resolution) * size_t(resolution) * 4u, 0.0f);
    for (int y = 0; y < resolution; ++y) {
      float roughness = (float(y) + 0.5f) / float(resolution);
      for (int x = 0; x < resolution; ++x) {
        float ndotV = (float(x) + 0.5f) / float(resolution);
        Vec3 view = {std::sqrt(std::max(1.0f - ndotV * ndotV, 0.0f)), 0.0f, ndotV};

        float a = 0.0f;
        float b = 0.0f;
        for (int i = 0; i < sampleCount; ++i) {
          float xiX = float(i) / float(sampleCount);
          float xiY = RadicalInverseVdC(uint32_t(i));
          Vec3 halfVector = ImportanceSampleGGX(xiX, xiY, roughness).direction;
          Vec3 light = Normalize(Reflect({-view.x, -view.y, -view.z}, halfVector));

          float ndotL = Saturate(light.z);
          float ndotH = Saturate(halfVector.z);
          float vdotH = Saturate(Dot(view, halfVector));
          if (ndotL > 0.0f && ndotH > 0.0f) {
            float visibilityPdf = SmithGGXCorrelated(ndotV, ndotL, roughness) * vdotH * ndotL / ndotH;
            float fresnel = std::pow(1.0f - vdotH, 5.0f);
            a += (1.0f - fresnel) * visibilityPdf;
            b += fresnel * visibilityPdf;
          }
        }

        size_t offset = (size_t(y) * size_t(resolution) + size_t(x)) * 4u;
        data[offset + 0] = 4.0f * a / float(sampleCount);
        data[offset + 1] = 4.0f * b / float(sampleCount);
        data[offset + 2] = 0.0f;
        data[offset + 3] = 1.0f;
      }
    }
  }

  void GenerateCharlieLUT(std::vector<float>& data, int resolution, int sampleCount) {
    data.assign(size_t(resolution) * size_t(resolution) * 4u, 0.0f);
    for (int y = 0; y < resolution; ++y) {
      float roughness = (float(y) + 0.5f) / float(resolution);
      for (int x = 0; x < resolution; ++x) {
        float ndotV = (float(x) + 0.5f) / float(resolution);
        Vec3 view = {std::sqrt(std::max(1.0f - ndotV * ndotV, 0.0f)), 0.0f, ndotV};

        float c = 0.0f;
        for (int i = 0; i < sampleCount; ++i) {
          float xiX = float(i) / float(sampleCount);
          float xiY = RadicalInverseVdC(uint32_t(i));
          Vec3 halfVector = ImportanceSampleCharlie(xiX, xiY, roughness).direction;
          Vec3 light = Normalize(Reflect({-view.x, -view.y, -view.z}, halfVector));

          float ndotL = Saturate(light.z);
          float ndotH = Saturate(halfVector.z);
          float vdotH = Saturate(Dot(view, halfVector));
          if (ndotL > 0.0f) {
            float sheenDistribution = DistributionCharlie(roughness, ndotH);
            float sheenVisibility = VisibilityAshikhmin(ndotL, ndotV);
            c += sheenVisibility * sheenDistribution * ndotL * vdotH;
          }
        }

        size_t offset = (size_t(y) * size_t(resolution) + size_t(x)) * 4u;
        data[offset + 0] = 0.0f;
        data[offset + 1] = 0.0f;
        data[offset + 2] = 4.0f * 2.0f * PI * c / float(sampleCount);
        data[offset + 3] = 1.0f;
      }
    }
  }

  void GenerateSheenELUT(std::vector<float>& data, int resolution, int sampleCount) {
    data.assign(size_t(resolution) * size_t(resolution) * 4u, 0.0f);
    for (int y = 0; y < resolution; ++y) {
      float sheenRoughness = (float(y) + 0.5f) / float(resolution);
      for (int x = 0; x < resolution; ++x) {
        float ndotV = (float(x) + 0.5f) / float(resolution);
        Vec3 view = {std::sqrt(std::max(1.0f - ndotV * ndotV, 0.0f)), 0.0f, ndotV};

        float e = 0.0f;
        for (int i = 0; i < sampleCount; ++i) {
          float xiX = float(i) / float(sampleCount);
          float xiY = RadicalInverseVdC(uint32_t(i));
          ImportanceSample sample = ImportanceSampleLambertian(xiX, xiY);
          Vec3 light = sample.direction;
          float ndotL = Saturate(light.z);
          if (ndotL > 0.0f) {
            Vec3 halfVector = Normalize(Add(view, light));
            float ndotH = Saturate(halfVector.z);
            float brdf = DistributionCharlieRoughness(sheenRoughness, ndotH)
                       * VisibilitySheen(ndotL, ndotV, sheenRoughness);
            e += brdf * ndotL / std::max(sample.pdf, 0.000001f);
          }
        }

        size_t offset = (size_t(y) * size_t(resolution) + size_t(x)) * 4u;
        data[offset + 0] = Saturate(e / float(sampleCount));
        data[offset + 1] = 0.0f;
        data[offset + 2] = 0.0f;
        data[offset + 3] = 1.0f;
      }
    }
  }

}

int CreateGGXBrdfLUTTexture(BaseDriver* driver, int resolution, int sampleCount) {
  if (!driver || resolution <= 0 || sampleCount <= 0)
    return -1;

  std::vector<float> lutData;
  bool fromCache = LoadCachedFloatImage(IBLCacheKind::GGXBrdfLUT, resolution, resolution, sampleCount, lutData);
  if (!fromCache) {
    GenerateGGXBrdfLUT(lutData, resolution, sampleCount);
    SaveCachedFloatImage(IBLCacheKind::GGXBrdfLUT, resolution, resolution, sampleCount, lutData);
  }
  int textureIndex = driver->CreateFloatTexture(resolution, resolution, lutData.data());
  if (textureIndex >= 0)
    T8_LOG_INFO("[IBL] %s GGX BRDF LUT: slot=%d %dx%d samples=%d",
                fromCache ? "Loaded cached" : "Generated", textureIndex, resolution, resolution, sampleCount);
  return textureIndex;
}

int CreateCachedGeneratedDiffuseIBLTexture(BaseDriver* driver, const std::string& sourcePath) {
  if (!driver || sourcePath.empty())
    return -1;

  std::vector<float> cubemapData;
  if (!LoadCachedFloatCube(IBLCacheKind::DiffuseCube, sourcePath, GeneratedDiffuseSize, 1, GeneratedLambertianSamples, cubemapData))
    return -1;

  int textureIndex = driver->CreateFloatCubeMap(GeneratedDiffuseSize, 1, cubemapData.data());
  if (textureIndex >= 0)
    T8_LOG_INFO("[IBL] Loaded cached Lambertian diffuse cubemap: slot=%d %dx%d samples=%d", textureIndex, GeneratedDiffuseSize, GeneratedDiffuseSize, GeneratedLambertianSamples);
  return textureIndex;
}

int CreateGeneratedDiffuseIBLTexture(BaseDriver* driver, const SourceCubemap& source, const std::string& sourcePath) {
  std::vector<float> cubemapData;
  GenerateFilteredDiffuseCubemap(source, GeneratedDiffuseSize, GeneratedLambertianSamples, cubemapData);
  if (!sourcePath.empty())
    SaveCachedFloatCube(IBLCacheKind::DiffuseCube, sourcePath, GeneratedDiffuseSize, 1, GeneratedLambertianSamples, cubemapData);
  int textureIndex = driver->CreateFloatCubeMap(GeneratedDiffuseSize, 1, cubemapData.data());
  if (textureIndex >= 0) {
    T8_LOG_INFO("[IBL] Generated Lambertian diffuse cubemap: slot=%d %dx%d samples=%d", textureIndex, GeneratedDiffuseSize, GeneratedDiffuseSize, GeneratedLambertianSamples);
  }
  return textureIndex;
}

int CreateCachedGeneratedSpecularIBLTexture(BaseDriver* driver, const std::string& sourcePath) {
  if (!driver || sourcePath.empty())
    return -1;

  int mipCount = GeneratedSpecularMipCount(GeneratedSpecularSize);
  std::vector<float> cubemapData;
  if (!LoadCachedFloatCube(IBLCacheKind::SpecularCube, sourcePath, GeneratedSpecularSize, mipCount, GeneratedGGXSamples, cubemapData))
    return -1;

  int textureIndex = driver->CreateFloatCubeMap(GeneratedSpecularSize, mipCount, cubemapData.data());
  if (textureIndex >= 0)
    T8_LOG_INFO("[IBL] Loaded cached GGX specular cubemap: slot=%d %dx%d mips=%d samples=%d", textureIndex, GeneratedSpecularSize, GeneratedSpecularSize, mipCount, GeneratedGGXSamples);
  return textureIndex;
}

int CreateGeneratedSpecularIBLTexture(BaseDriver* driver, const SourceCubemap& source, const std::string& sourcePath) {
  int mipCount = GeneratedSpecularMipCount(GeneratedSpecularSize);
  std::vector<float> cubemapData;
  GenerateFilteredSpecularCubemap(source, GeneratedSpecularSize, mipCount, GeneratedGGXSamples, cubemapData);
  if (!sourcePath.empty())
    SaveCachedFloatCube(IBLCacheKind::SpecularCube, sourcePath, GeneratedSpecularSize, mipCount, GeneratedGGXSamples, cubemapData);
  int textureIndex = driver->CreateFloatCubeMap(GeneratedSpecularSize, mipCount, cubemapData.data());
  if (textureIndex >= 0) {
    T8_LOG_INFO("[IBL] Generated GGX specular cubemap: slot=%d %dx%d mips=%d samples=%d", textureIndex, GeneratedSpecularSize, GeneratedSpecularSize, mipCount, GeneratedGGXSamples);
  }
  return textureIndex;
}

int CreateCachedGeneratedCharlieIBLTexture(BaseDriver* driver, const std::string& sourcePath) {
  if (!driver || sourcePath.empty())
    return -1;

  int mipCount = GeneratedSpecularMipCount(GeneratedSpecularSize);
  std::vector<float> cubemapData;
  if (!LoadCachedFloatCube(IBLCacheKind::CharlieCube, sourcePath, GeneratedSpecularSize, mipCount, GeneratedCharlieSamples, cubemapData))
    return -1;

  int textureIndex = driver->CreateFloatCubeMap(GeneratedSpecularSize, mipCount, cubemapData.data());
  if (textureIndex >= 0)
    T8_LOG_INFO("[IBL] Loaded cached Charlie sheen cubemap: slot=%d %dx%d mips=%d samples=%d", textureIndex, GeneratedSpecularSize, GeneratedSpecularSize, mipCount, GeneratedCharlieSamples);
  return textureIndex;
}

int CreateGeneratedCharlieIBLTexture(BaseDriver* driver, const SourceCubemap& source, const std::string& sourcePath) {
  int mipCount = GeneratedSpecularMipCount(GeneratedSpecularSize);
  std::vector<float> cubemapData;
  GenerateFilteredCharlieCubemap(source, GeneratedSpecularSize, mipCount, GeneratedCharlieSamples, cubemapData);
  if (!sourcePath.empty())
    SaveCachedFloatCube(IBLCacheKind::CharlieCube, sourcePath, GeneratedSpecularSize, mipCount, GeneratedCharlieSamples, cubemapData);
  int textureIndex = driver->CreateFloatCubeMap(GeneratedSpecularSize, mipCount, cubemapData.data());
  if (textureIndex >= 0) {
    T8_LOG_INFO("[IBL] Generated Charlie sheen cubemap: slot=%d %dx%d mips=%d samples=%d", textureIndex, GeneratedSpecularSize, GeneratedSpecularSize, mipCount, GeneratedCharlieSamples);
  }
  return textureIndex;
}

int CreateCharlieLUTTexture(BaseDriver* driver, int resolution = 256, int sampleCount = 256) {
  if (!driver || resolution <= 0 || sampleCount <= 0)
    return -1;

  std::vector<float> lutData;
  bool fromCache = LoadCachedFloatImage(IBLCacheKind::CharlieLUT, resolution, resolution, sampleCount, lutData);
  if (!fromCache) {
    GenerateCharlieLUT(lutData, resolution, sampleCount);
    SaveCachedFloatImage(IBLCacheKind::CharlieLUT, resolution, resolution, sampleCount, lutData);
  }
  int textureIndex = driver->CreateFloatTexture(resolution, resolution, lutData.data());
  if (textureIndex >= 0)
    T8_LOG_INFO("[IBL] %s Charlie LUT: slot=%d %dx%d samples=%d",
                fromCache ? "Loaded cached" : "Generated", textureIndex, resolution, resolution, sampleCount);
  return textureIndex;
}

int CreateSheenELUTTexture(BaseDriver* driver, int resolution = 256, int sampleCount = 128) {
  if (!driver || resolution <= 0 || sampleCount <= 0)
    return -1;

  std::vector<float> lutData;
  bool fromCache = LoadCachedFloatImage(IBLCacheKind::SheenELUT, resolution, resolution, sampleCount, lutData);
  if (!fromCache) {
    GenerateSheenELUT(lutData, resolution, sampleCount);
    SaveCachedFloatImage(IBLCacheKind::SheenELUT, resolution, resolution, sampleCount, lutData);
  }
  int textureIndex = driver->CreateFloatTexture(resolution, resolution, lutData.data());
  if (textureIndex >= 0)
    T8_LOG_INFO("[IBL] %s sheen E LUT: slot=%d %dx%d samples=%d",
                fromCache ? "Loaded cached" : "Generated", textureIndex, resolution, resolution, sampleCount);
  return textureIndex;
}

void LoadEnvironmentIBLResources(
  BaseDriver* driver,
  const EnvironmentResourcePaths& paths,
  EnvironmentMapSet& envMaps,
  int& diffuseTextureIndex,
  int& specularTextureIndex,
  int& brdfTextureIndex,
  int& sheenTextureIndex,
  int& charlieLUTTextureIndex,
  int& sheenELUTTextureIndex)
{
  if (!driver)
    return;

  auto loadTextureOnce = [&](int& textureIndex, const std::string& path, const char* label) {
    if (textureIndex >= 0) {
      T8_LOG_INFO("[IBL] Reusing %s: slot=%d path='%s'", label, textureIndex, path.c_str());
      return;
    }
    textureIndex = driver->CreateTexture(path);
    if (textureIndex >= 0) {
      T8_LOG_INFO("[IBL] Loaded %s: slot=%d path='%s'", label, textureIndex, path.c_str());
    } else {
      T8_LOG_INFO("[IBL] %s load failed: path='%s'", label, path.c_str());
    }
  };

  if (!paths.diffuseIBL.empty()) {
    loadTextureOnce(diffuseTextureIndex, paths.diffuseIBL, "explicit diffuse IBL");
    if (diffuseTextureIndex >= 0)
      envMaps.DiffuseIBL = diffuseTextureIndex;
  }

  if (!paths.specularIBL.empty()) {
    loadTextureOnce(specularTextureIndex, paths.specularIBL, "explicit specular IBL");
    if (specularTextureIndex >= 0)
      envMaps.SpecularIBL = specularTextureIndex;
  }

  if (!paths.sheenIBL.empty()) {
    loadTextureOnce(sheenTextureIndex, paths.sheenIBL, "explicit Charlie sheen IBL");
    if (sheenTextureIndex >= 0)
      envMaps.CharlieIBL = sheenTextureIndex;
  }

  std::string skyTexturePath;
  if (driver && envMaps.Sky >= 0) {
    if (Texture* skyTexture = driver->GetTexture(envMaps.Sky))
      skyTexturePath = skyTexture->filepath;
  }

  if (diffuseTextureIndex < 0) {
    diffuseTextureIndex = CreateCachedGeneratedDiffuseIBLTexture(driver, skyTexturePath);
    if (diffuseTextureIndex >= 0)
      envMaps.DiffuseIBL = diffuseTextureIndex;
  }
  if (specularTextureIndex < 0) {
    specularTextureIndex = CreateCachedGeneratedSpecularIBLTexture(driver, skyTexturePath);
    if (specularTextureIndex >= 0)
      envMaps.SpecularIBL = specularTextureIndex;
  }
  if (sheenTextureIndex < 0) {
    sheenTextureIndex = CreateCachedGeneratedCharlieIBLTexture(driver, skyTexturePath);
    if (sheenTextureIndex >= 0)
      envMaps.CharlieIBL = sheenTextureIndex;
  }

  if (diffuseTextureIndex < 0 || specularTextureIndex < 0 || sheenTextureIndex < 0) {
    SourceCubemap source;
    if (LoadSourceCubemap(driver, envMaps.Sky, source)) {
      if (diffuseTextureIndex < 0) {
        diffuseTextureIndex = CreateGeneratedDiffuseIBLTexture(driver, source, skyTexturePath);
        if (diffuseTextureIndex >= 0)
          envMaps.DiffuseIBL = diffuseTextureIndex;
      }
      if (specularTextureIndex < 0) {
        specularTextureIndex = CreateGeneratedSpecularIBLTexture(driver, source, skyTexturePath);
        if (specularTextureIndex >= 0)
          envMaps.SpecularIBL = specularTextureIndex;
      }
      if (sheenTextureIndex < 0) {
        sheenTextureIndex = CreateGeneratedCharlieIBLTexture(driver, source, skyTexturePath);
        if (sheenTextureIndex >= 0)
          envMaps.CharlieIBL = sheenTextureIndex;
      }
    }
  }

  if (!paths.brdfLUT.empty()) {
    loadTextureOnce(brdfTextureIndex, paths.brdfLUT, "explicit GGX BRDF LUT");
  }
  if (brdfTextureIndex < 0) {
    brdfTextureIndex = CreateGGXBrdfLUTTexture(driver);
  }

  if (!paths.charlieLUT.empty()) {
    loadTextureOnce(charlieLUTTextureIndex, paths.charlieLUT, "explicit Charlie LUT");
  }
  if (charlieLUTTextureIndex < 0) {
    charlieLUTTextureIndex = CreateCharlieLUTTexture(driver);
  }

  if (!paths.sheenELUT.empty()) {
    loadTextureOnce(sheenELUTTextureIndex, paths.sheenELUT, "explicit sheen E LUT");
  }
  if (sheenELUTTextureIndex < 0) {
    sheenELUTTextureIndex = CreateSheenELUTTexture(driver);
  }

  if (brdfTextureIndex >= 0)
    envMaps.BrdfLUT = brdfTextureIndex;
  if (charlieLUTTextureIndex >= 0)
    envMaps.CharlieLUT = charlieLUTTextureIndex;
  if (sheenELUTTextureIndex >= 0)
    envMaps.SheenELUT = sheenELUTTextureIndex;
}

void UpdateSceneIBLSettings(SceneProps& props, BaseDriver* driver, const EnvironmentMapSet& envMaps) {
  props.IBLMipCount = 4.0f;
  props.IBLDiffuseMipLevel = 0.0f;
  props.IBLBRDFLUTEnabled = envMaps.BrdfLUT >= 0 ? 1.0f : 0.0f;

  if (!driver)
    return;

  int specularIndex = envMaps.SpecularIBL >= 0 ? envMaps.SpecularIBL : envMaps.Sky;
  if (specularIndex < 0)
    return;

  Texture* specular = driver->GetTexture(specularIndex);
  if (specular && specular->mipmaps > 1)
    props.IBLMipCount = float(specular->mipmaps - 1);

  bool diffuseUsesSkyFallback = envMaps.DiffuseIBL < 0 || envMaps.DiffuseIBL == envMaps.Sky;
  if (diffuseUsesSkyFallback)
    props.IBLDiffuseMipLevel = std::min(6.0f, props.IBLMipCount);
}

}
