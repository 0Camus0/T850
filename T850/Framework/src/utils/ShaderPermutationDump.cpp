#include <pch.h>

#include <utils/ShaderPermutationDump.h>
#include <utils/Log.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace t850::ShaderPermutationDump {
namespace {

struct Entry {
  std::string keyHex;
  uint64_t bits = 0;
  uint32_t pass = 0;
  std::string vertexShader;
  std::string fragmentShader;
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

bool IsHexKey(const std::string& value) {
  if (value.size() != 18 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X')) return false;
  for (size_t i = 2; i < value.size(); ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
  }
  return true;
}

bool ParseJsonString(const std::string& text, size_t& pos, std::string& out) {
  if (pos >= text.size() || text[pos] != '"') return false;
  ++pos;
  out.clear();
  while (pos < text.size()) {
    char c = text[pos++];
    if (c == '"') return true;
    if (c == '\\' && pos < text.size()) {
      char escaped = text[pos++];
      switch (escaped) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        default: out += escaped; break;
      }
    } else {
      out += c;
    }
  }
  return false;
}

void SkipWhitespace(const std::string& text, size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
}

size_t FindMatchingBrace(const std::string& text, size_t openPos) {
  bool inString = false;
  bool escaped = false;
  int depth = 0;
  for (size_t i = openPos; i < text.size(); ++i) {
    char c = text[i];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) return i;
    }
  }
  return std::string::npos;
}

std::map<std::string, std::string> LoadExistingRawEntries(const std::filesystem::path& path) {
  std::map<std::string, std::string> entries;
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) return entries;

  std::ostringstream buffer;
  buffer << file.rdbuf();
  const std::string text = buffer.str();
  const size_t permutationsPos = text.find("\"permutations\"");
  if (permutationsPos == std::string::npos) return entries;
  size_t pos = text.find('{', permutationsPos);
  if (pos == std::string::npos) return entries;
  ++pos;

  while (pos < text.size()) {
    SkipWhitespace(text, pos);
    if (pos >= text.size() || text[pos] == '}') break;
    if (text[pos] == ',') {
      ++pos;
      continue;
    }

    std::string key;
    if (!ParseJsonString(text, pos, key) || !IsHexKey(key)) break;
    SkipWhitespace(text, pos);
    if (pos >= text.size() || text[pos] != ':') break;
    ++pos;
    SkipWhitespace(text, pos);
    if (pos >= text.size() || text[pos] != '{') break;

    const size_t objectStart = pos;
    const size_t objectEnd = FindMatchingBrace(text, objectStart);
    if (objectEnd == std::string::npos) break;
    entries[key] = text.substr(objectStart, objectEnd - objectStart + 1);
    pos = objectEnd + 1;
  }

  return entries;
}

std::string EntryToJson(const Entry& entry) {
  std::ostringstream out;
  out << "{\n";
  out << "      \"key\": \"" << entry.keyHex << "\",\n";
  out << "      \"bits\": \"" << entry.keyHex << "\",\n";
  out << "      \"pass\": " << entry.pass << ",\n";
  out << "      \"vertexShader\": \"" << JsonEscape(entry.vertexShader) << "\",\n";
  out << "      \"fragmentShader\": \"" << JsonEscape(entry.fragmentShader) << "\",\n";
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
  std::lock_guard<std::mutex> lock(g_mutex);
  g_outputPath = outputPath.empty() ? std::filesystem::path("shader_permutations.json")
                                    : std::filesystem::path(outputPath);
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

bool Flush() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_enabled) return true;

  std::map<std::string, std::string> merged = LoadExistingRawEntries(g_outputPath);
  for (const auto& it : g_entries) {
    merged[it.first] = EntryToJson(it.second);
  }

  std::error_code ec;
  const std::filesystem::path parent = g_outputPath.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      T8_LOG_ERROR("[ShaderPermutationDump] Failed to create '%s': %s",
                   parent.string().c_str(), ec.message().c_str());
      return false;
    }
  }

  std::ofstream file(g_outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    T8_LOG_ERROR("[ShaderPermutationDump] Failed to open '%s' for writing", g_outputPath.string().c_str());
    return false;
  }

  file << "{\n";
  file << "  \"version\": 1,\n";
  file << "  \"permutations\": {\n";
  size_t index = 0;
  for (const auto& it : merged) {
    file << "    \"" << it.first << "\": " << it.second;
    if (++index < merged.size()) file << ",";
    file << "\n";
  }
  file << "  }\n";
  file << "}\n";

  T8_LOG_INFO("[ShaderPermutationDump] Wrote %zu shader permutations to '%s'",
              merged.size(), g_outputPath.string().c_str());
  g_enabled = false;
  return true;
}

} // namespace t850::ShaderPermutationDump
