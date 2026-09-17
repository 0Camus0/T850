#include <pch.h>

#include <utils/ShaderPermutationDump.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <glaze/glaze.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace t850::ShaderPermutationDump {
struct ManifestEntry {
  std::string key;
  std::string bits;
  uint32_t pass = 0;
  std::string vertexShader;
  std::string fragmentShader;
  std::vector<std::string> defines;
};

struct ComputeManifestEntry {
  std::string key;
  std::string kind;
  std::string computeShader;
  std::string entryPoint;
  std::string permutation;
  std::vector<std::string> defines;
};

struct Manifest {
  int version = 2;
  std::map<std::string, ManifestEntry> permutations;
  std::map<std::string, ComputeManifestEntry> compute_permutations;
};

namespace {

struct Entry {
  bool compute = false;
  std::string identity;
  std::string keyHex;
  uint64_t bits = 0;
  uint32_t pass = 0;
  std::string vertexShader;
  std::string fragmentShader;
  std::string computeShader;
  std::string entryPoint;
  std::string permutationName;
  std::vector<std::string> defines;
};

std::mutex g_mutex;
bool g_enabled = false;
std::filesystem::path g_outputPath;
std::map<std::string, Entry> g_entries;

std::string KeyHex(uint64_t bits) {
  std::ostringstream out;
  out << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << bits;
  return out.str();
}

std::string JsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

std::vector<std::string> ParseDefines(const std::string& defines) {
  std::vector<std::string> out;
  std::istringstream input(defines);
  std::string line;
  while (std::getline(input, line)) {
    constexpr const char* kDefine = "#define ";
    if (line.rfind(kDefine, 0) != 0) continue;
    std::string name = line.substr(std::char_traits<char>::length(kDefine));
    auto end = std::find_if(name.begin(), name.end(), [](unsigned char ch) {
      return std::isspace(ch) != 0;
    });
    name.erase(end, name.end());
    if (!name.empty()) out.push_back(name);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<std::string> NormalizeDefines(std::vector<std::string> defines) {
  defines.erase(std::remove_if(defines.begin(), defines.end(), [](const std::string& value) {
    return value.empty();
  }), defines.end());
  std::sort(defines.begin(), defines.end());
  defines.erase(std::unique(defines.begin(), defines.end()), defines.end());
  return defines;
}

bool IsHexKey(const std::string& value) {
  if (value.size() != 18 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X')) return false;
  for (size_t i = 2; i < value.size(); ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
  }
  return true;
}
std::string EntryToJson(const Entry& entry) {
  std::ostringstream out;
  out << "{\n";
  if (entry.compute) {
    out << "      \"key\": \"" << JsonEscape(entry.identity) << "\",\n";
    out << "      \"kind\": \"compute\",\n";
    out << "      \"computeShader\": \"" << JsonEscape(entry.computeShader) << "\",\n";
    out << "      \"entryPoint\": \"" << JsonEscape(entry.entryPoint) << "\",\n";
    out << "      \"permutation\": \"" << JsonEscape(entry.permutationName) << "\",\n";
  } else {
    out << "      \"key\": \"" << entry.keyHex << "\",\n";
    out << "      \"bits\": \"" << entry.keyHex << "\",\n";
    out << "      \"pass\": " << entry.pass << ",\n";
    out << "      \"vertexShader\": \"" << JsonEscape(entry.vertexShader) << "\",\n";
    out << "      \"fragmentShader\": \"" << JsonEscape(entry.fragmentShader) << "\",\n";
  }
  out << "      \"defines\": [";
  for (size_t i = 0; i < entry.defines.size(); ++i) {
    if (i > 0) out << ", ";
    out << "\"" << JsonEscape(entry.defines[i]) << "\"";
  }
  out << "]\n";
  out << "    }";
  return out.str();
}

} // namespace

void Begin(const std::string& outputPath) {
  static const bool registered = std::atexit([] {
    if (!Flush()) std::_Exit(EXIT_FAILURE);
  }) == 0;
  if (!registered) throw std::runtime_error("Cannot register shader permutation flush at exit");
  std::lock_guard<std::mutex> lock(g_mutex);
  g_outputPath = std::filesystem::absolute(outputPath.empty() ? "shader_permutations.json" : outputPath);
  g_entries.clear();
  g_enabled = true;
  T8_LOG_INFO("[ShaderPermutationDump] Recording shader permutations to '%s'", g_outputPath.string().c_str());
}

bool IsEnabled() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_enabled;
}

void Record(const ShaderKey& key,
            const std::string& vertexShader,
            const std::string& fragmentShader,
            const std::string& defines) {
  if (!key.isValid()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_enabled) return;

  Entry entry;
  entry.keyHex = KeyHex(key.bits);
  entry.bits = key.bits;
  entry.pass = key.getPass();
  entry.vertexShader = vertexShader;
  entry.fragmentShader = fragmentShader;
  entry.defines = ParseDefines(defines);
  g_entries[entry.keyHex] = std::move(entry);
}

void RecordCompute(const std::string& computeShader,
                   const std::string& entryPoint,
                   const std::string& permutationName,
                   const std::vector<std::string>& defines) {
  if (computeShader.empty() || entryPoint.empty()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_enabled) return;

  Entry entry;
  entry.compute = true;
  entry.computeShader = std::filesystem::path(computeShader).filename().string();
  entry.entryPoint = entryPoint;
  entry.permutationName = permutationName.empty() ? "base" : permutationName;
  entry.defines = NormalizeDefines(defines);
  entry.identity = entry.computeShader + ":" + entry.entryPoint + ":" + entry.permutationName;
  g_entries[entry.identity] = std::move(entry);
}

bool Flush() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_enabled) return true;

  Manifest manifest;
  std::error_code ec;
  const bool exists = std::filesystem::exists(g_outputPath, ec);
  if (ec) return false;
  if (exists) {
    std::string text;
    if (!ResourceLocator::Instance().ReadText(g_outputPath.string(), text) ||
        glz::read_json(manifest, text) ||
        (manifest.version != 1 && manifest.version != 2)) {
      T8_LOG_ERROR("[ShaderPermutationDump] Refusing to replace unreadable or invalid manifest '%s'", g_outputPath.string().c_str());
      return false;
    }
  }
  std::map<std::string, std::string> merged;
  for (const auto& [key, entry] : manifest.permutations) {
    if (!IsHexKey(key) || entry.key != key || entry.bits != key) return false;
    auto json = glz::write_json(entry);
    if (!json) return false;
    merged[key] = std::move(json.value());
  }
  std::map<std::string, std::string> computeMerged;
  for (const auto& [key, entry] : manifest.compute_permutations) {
    const std::string identity = std::filesystem::path(entry.computeShader).filename().string()
      + ":" + entry.entryPoint + ":" + entry.permutation;
    if (key != identity || entry.key != key || entry.kind != "compute") return false;
    auto json = glz::write_json(entry);
    if (!json) return false;
    computeMerged[key] = std::move(json.value());
  }
  for (const auto& it : g_entries) {
    if (it.second.compute)
      computeMerged[it.first] = EntryToJson(it.second);
    else
      merged[it.first] = EntryToJson(it.second);
  }

  std::ostringstream file;

  file << "{\n";
  file << "  \"version\": 2,\n";
  file << "  \"permutations\": {\n";
  size_t index = 0;
  for (const auto& it : merged) {
    file << "    \"" << JsonEscape(it.first) << "\": " << it.second;
    if (++index < merged.size()) file << ",";
    file << "\n";
  }
  file << "  },\n";
  file << "  \"compute_permutations\": {\n";
  index = 0;
  for (const auto& it : computeMerged) {
    file << "    \"" << JsonEscape(it.first) << "\": " << it.second;
    if (++index < computeMerged.size()) file << ",";
    file << "\n";
  }
  file << "  }\n";
  file << "}\n";

  const auto text = file.str();
  if (!ResourceLocator::Instance().WriteBinaryAtomic(g_outputPath.string(),
      std::span(reinterpret_cast<const unsigned char*>(text.data()), text.size()))) {
    T8_LOG_ERROR("[ShaderPermutationDump] Failed to atomically write '%s'", g_outputPath.string().c_str());
    return false;
  }
  T8_LOG_INFO("[ShaderPermutationDump] Wrote %zu graphics and %zu compute permutations to '%s'",
              merged.size(), computeMerged.size(), g_outputPath.string().c_str());
  g_enabled = false;
  return true;
}

} // namespace t850::ShaderPermutationDump
