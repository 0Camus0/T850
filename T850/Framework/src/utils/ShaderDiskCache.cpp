#include <pch.h>
#include <utils/ShaderDiskCache.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <iomanip>
#include <mutex>
#include <unordered_set>

namespace t850::ShaderDiskCache {
namespace {
  constexpr const char* kRootPath = "Shaders/.t8shadercache";
  constexpr const char* kCacheVersionString = "T850_SHADER_CACHE_V1";

  std::mutex g_cacheMutex;
  std::unordered_set<std::string> g_preparedApis;

  uint32_t RotateLeft(uint32_t value, uint32_t bits) {
    return (value << bits) | (value >> (32u - bits));
  }

  std::string BytesToHex(const uint8_t* bytes, size_t count) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (size_t i = 0; i < count; ++i)
      out << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    return out.str();
  }

  std::string Sha1(const std::string& input) {
    std::vector<uint8_t> data(input.begin(), input.end());
    const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8ull;
    data.push_back(0x80);
    while ((data.size() % 64u) != 56u)
      data.push_back(0);
    for (int i = 7; i >= 0; --i)
      data.push_back(static_cast<uint8_t>((bitLength >> (i * 8)) & 0xFFu));

    uint32_t h0 = 0x67452301u;
    uint32_t h1 = 0xEFCDAB89u;
    uint32_t h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u;
    uint32_t h4 = 0xC3D2E1F0u;

    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
      uint32_t words[80] = {};
      for (int i = 0; i < 16; ++i) {
        const size_t offset = chunk + static_cast<size_t>(i) * 4u;
        words[i] = (static_cast<uint32_t>(data[offset]) << 24)
                 | (static_cast<uint32_t>(data[offset + 1]) << 16)
                 | (static_cast<uint32_t>(data[offset + 2]) << 8)
                 | static_cast<uint32_t>(data[offset + 3]);
      }
      for (int i = 16; i < 80; ++i)
        words[i] = RotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);

      uint32_t a = h0;
      uint32_t b = h1;
      uint32_t c = h2;
      uint32_t d = h3;
      uint32_t e = h4;

      for (int i = 0; i < 80; ++i) {
        uint32_t f = 0;
        uint32_t k = 0;
        if (i < 20) {
          f = (b & c) | ((~b) & d);
          k = 0x5A827999u;
        }
        else if (i < 40) {
          f = b ^ c ^ d;
          k = 0x6ED9EBA1u;
        }
        else if (i < 60) {
          f = (b & c) | (b & d) | (c & d);
          k = 0x8F1BBCDCu;
        }
        else {
          f = b ^ c ^ d;
          k = 0xCA62C1D6u;
        }

        const uint32_t temp = RotateLeft(a, 5) + f + e + k + words[i];
        e = d;
        d = c;
        c = RotateLeft(b, 30);
        b = a;
        a = temp;
      }

      h0 += a;
      h1 += b;
      h2 += c;
      h3 += d;
      h4 += e;
    }

    uint8_t digest[20] = {
      static_cast<uint8_t>((h0 >> 24) & 0xFF), static_cast<uint8_t>((h0 >> 16) & 0xFF), static_cast<uint8_t>((h0 >> 8) & 0xFF), static_cast<uint8_t>(h0 & 0xFF),
      static_cast<uint8_t>((h1 >> 24) & 0xFF), static_cast<uint8_t>((h1 >> 16) & 0xFF), static_cast<uint8_t>((h1 >> 8) & 0xFF), static_cast<uint8_t>(h1 & 0xFF),
      static_cast<uint8_t>((h2 >> 24) & 0xFF), static_cast<uint8_t>((h2 >> 16) & 0xFF), static_cast<uint8_t>((h2 >> 8) & 0xFF), static_cast<uint8_t>(h2 & 0xFF),
      static_cast<uint8_t>((h3 >> 24) & 0xFF), static_cast<uint8_t>((h3 >> 16) & 0xFF), static_cast<uint8_t>((h3 >> 8) & 0xFF), static_cast<uint8_t>(h3 & 0xFF),
      static_cast<uint8_t>((h4 >> 24) & 0xFF), static_cast<uint8_t>((h4 >> 16) & 0xFF), static_cast<uint8_t>((h4 >> 8) & 0xFF), static_cast<uint8_t>(h4 & 0xFF)
    };
    return BytesToHex(digest, sizeof(digest));
  }

  std::string JsonEscape(const std::string& text) {
    std::ostringstream out;
    for (char ch : text) {
      switch (ch) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default: out << ch; break;
      }
    }
    return out.str();
  }

  std::string ExtractDriverSignature(const std::string& metadata, const std::string& api) {
    const std::string needle = "\"" + api + "\": \"";
    const size_t start = metadata.find(needle);
    if (start == std::string::npos)
      return {};
    size_t pos = start + needle.size();
    std::string value;
    while (pos < metadata.size()) {
      char ch = metadata[pos++];
      if (ch == '"')
        break;
      if (ch == '\\' && pos < metadata.size()) {
        char esc = metadata[pos++];
        switch (esc) {
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        default: value.push_back(esc); break;
        }
      }
      else {
        value.push_back(ch);
      }
    }
    return value;
  }

  std::filesystem::path RootDirectory() {
    return ResourceLocator::Instance().ResolveCachePath(kRootPath);
  }

  std::filesystem::path ApiDirectory(const std::string& api) {
    return RootDirectory() / api;
  }

  std::filesystem::path ShaderDirectory(const ShaderDiskCacheKey& key) {
    return ApiDirectory(key.api) / key.sha1;
  }

  std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good())
      return {};
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
  }

  void WriteMetadata(const std::unordered_map<std::string, std::string>& drivers) {
    std::filesystem::create_directories(RootDirectory());
    std::ofstream out(RootDirectory() / "metadata.json", std::ios::binary | std::ios::trunc);
    out << "{\n";
    out << "  \"cacheFormat\": " << kCacheFormatVersion << ",\n";
    out << "  \"cacheVersion\": \"" << kCacheVersionString << "\",\n";
    out << "  \"drivers\": {\n";
    bool first = true;
    for (const auto& pair : drivers) {
      if (!first)
        out << ",\n";
      first = false;
      out << "    \"" << JsonEscape(pair.first) << "\": \"" << JsonEscape(pair.second) << "\"";
    }
    out << "\n  }\n";
    out << "}\n";
  }

  void EnsureApiMetadataLocked(const std::string& api, const std::string& driverSignature) {
    const std::string preparedKey = api + "\n" + driverSignature;
    if (g_preparedApis.find(preparedKey) != g_preparedApis.end())
      return;

    std::filesystem::create_directories(RootDirectory());
    std::unordered_map<std::string, std::string> drivers;
    const std::filesystem::path metadataPath = RootDirectory() / "metadata.json";
    const std::string metadata = ReadTextFile(metadataPath);
    static const char* knownApis[] = { "d3d11", "d3d12", "opengl", "vulkan" };
    for (const char* knownApi : knownApis) {
      std::string value = ExtractDriverSignature(metadata, knownApi);
      if (!value.empty())
        drivers[knownApi] = value;
    }

    const auto existing = drivers.find(api);
    if (existing == drivers.end() || existing->second != driverSignature) {
      if (existing != drivers.end()) {
        T8_LOG_INFO("[ShaderCache] Driver metadata changed for %s; clearing '%s'", api.c_str(), ApiDirectory(api).string().c_str());
        std::error_code ec;
        std::filesystem::remove_all(ApiDirectory(api), ec);
      }
      drivers[api] = driverSignature;
      WriteMetadata(drivers);
    }

    std::filesystem::create_directories(ApiDirectory(api));
    g_preparedApis.insert(preparedKey);
  }
}

void EnsureApiMetadata(const std::string& api, const std::string& driverSignature) {
  std::lock_guard<std::mutex> lock(g_cacheMutex);
  EnsureApiMetadataLocked(api, driverSignature);
}

ShaderDiskCacheKey MakeKey(const std::string& api,
                           const std::string& driverSignature,
                           uint64_t shaderKeyBits,
                           const std::string& vsName,
                           const std::string& fsName,
                           const std::string& vsSource,
                           const std::string& fsSource) {
  EnsureApiMetadata(api, driverSignature);

  std::ostringstream input;
  input << kCacheVersionString << '\n';
  input << "api=" << api << '\n';
  input << "driver=" << driverSignature << '\n';
  input << "key=0x" << std::hex << std::setw(16) << std::setfill('0') << shaderKeyBits << std::dec << '\n';
  input << "vsName=" << vsName << '\n';
  input << "fsName=" << fsName << '\n';
  input << "vsSize=" << vsSource.size() << '\n' << vsSource << '\n';
  input << "fsSize=" << fsSource.size() << '\n' << fsSource << '\n';

  ShaderDiskCacheKey key;
  key.api = api;
  key.sha1 = Sha1(input.str());
  key.shaderKeyBits = shaderKeyBits;
  key.vsName = vsName;
  key.fsName = fsName;
  return key;
}

bool LoadArtifact(const ShaderDiskCacheKey& key, const std::string& artifactName,
                  std::vector<uint8_t>& outBytes) {
  outBytes.clear();
  const std::filesystem::path path = ShaderDirectory(key) / artifactName;
  std::ifstream in(path, std::ios::binary);
  if (!in.good())
    return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size <= 0)
    return false;
  in.seekg(0, std::ios::beg);
  outBytes.resize(static_cast<size_t>(size));
  in.read(reinterpret_cast<char*>(outBytes.data()), size);
  const bool ok = in.good();
  if (ok)
    T8_LOG_DEBUG("[ShaderCache][%s] Loaded %s for %s", key.api.c_str(), artifactName.c_str(), key.sha1.c_str());
  return ok;
}

bool StoreArtifact(const ShaderDiskCacheKey& key, const std::string& artifactName,
                   const void* data, size_t byteCount) {
  if (!data || byteCount == 0)
    return false;

  std::filesystem::create_directories(ShaderDirectory(key));
  const std::filesystem::path path = ShaderDirectory(key) / artifactName;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.good())
    return false;
  out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(byteCount));
  const bool ok = out.good();
  if (ok)
    T8_LOG_DEBUG("[ShaderCache][%s] Stored %s for %s", key.api.c_str(), artifactName.c_str(), key.sha1.c_str());
  return ok;
}

bool LoadApiArtifact(const std::string& api, const std::string& artifactName,
                     std::vector<uint8_t>& outBytes) {
  outBytes.clear();
  const std::filesystem::path path = ApiDirectory(api) / artifactName;
  std::ifstream in(path, std::ios::binary);
  if (!in.good())
    return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size <= 0)
    return false;
  in.seekg(0, std::ios::beg);
  outBytes.resize(static_cast<size_t>(size));
  in.read(reinterpret_cast<char*>(outBytes.data()), size);
  const bool ok = in.good();
  if (ok)
    T8_LOG_DEBUG("[ShaderCache][%s] Loaded API artifact %s", api.c_str(), artifactName.c_str());
  return ok;
}

bool StoreApiArtifact(const std::string& api, const std::string& artifactName,
                      const void* data, size_t byteCount) {
  if (!data || byteCount == 0)
    return false;

  std::filesystem::create_directories(ApiDirectory(api));
  const std::filesystem::path path = ApiDirectory(api) / artifactName;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.good())
    return false;
  out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(byteCount));
  const bool ok = out.good();
  if (ok)
    T8_LOG_DEBUG("[ShaderCache][%s] Stored API artifact %s", api.c_str(), artifactName.c_str());
  return ok;
}

void WriteManifest(const ShaderDiskCacheKey& key, const std::string& driverSignature) {
  std::filesystem::create_directories(ShaderDirectory(key));
  std::ofstream out(ShaderDirectory(key) / "manifest.json", std::ios::binary | std::ios::trunc);
  if (!out.good())
    return;
  out << "{\n";
  out << "  \"cacheFormat\": " << kCacheFormatVersion << ",\n";
  out << "  \"cacheVersion\": \"" << kCacheVersionString << "\",\n";
  out << "  \"api\": \"" << JsonEscape(key.api) << "\",\n";
  out << "  \"sha1\": \"" << JsonEscape(key.sha1) << "\",\n";
  out << "  \"shaderKey\": \"0x" << std::hex << std::setw(16) << std::setfill('0') << key.shaderKeyBits << std::dec << "\",\n";
  out << "  \"vs\": \"" << JsonEscape(key.vsName) << "\",\n";
  out << "  \"fs\": \"" << JsonEscape(key.fsName) << "\",\n";
  out << "  \"driverSignature\": \"" << JsonEscape(driverSignature) << "\"\n";
  out << "}\n";
}
}
