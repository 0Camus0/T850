#include <pch.h>
/*********************************************************
 * glTF 2.0 — JSON parsing via glaze.
 *
 * The POD types in GLTFTypes.h are aggregate-initializable, so glaze
 * uses pure compile-time reflection to populate them by field name. We
 * pass error_on_unknown_keys=false so that unknown spec extensions and
 * application "extras" are silently ignored (per glTF spec §3.5).
 *********************************************************/

#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFTypes.h>
#include <utils/Log.h>

#include <cctype>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4267)
#endif
#include <glaze/glaze.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace t850 {
namespace gltf {

namespace {

bool IsJsonDigit(char c) {
  return c >= '0' && c <= '9';
}

std::string NormalizeTinyFloatLiterals(const std::string& json, std::size_t& normalizedCount) {
  normalizedCount = 0;
  std::string out;
  out.reserve(json.size());

  bool inString = false;
  bool escaped = false;
  for (std::size_t i = 0; i < json.size();) {
    const char c = json[i];
    if (inString) {
      out.push_back(c);
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      ++i;
      continue;
    }

    if (c == '"') {
      inString = true;
      out.push_back(c);
      ++i;
      continue;
    }

    if (c != '-' && !IsJsonDigit(c)) {
      out.push_back(c);
      ++i;
      continue;
    }

    const std::size_t start = i;
    if (json[i] == '-') ++i;
    if (i >= json.size() || !IsJsonDigit(json[i])) {
      out.append(json, start, i - start);
      continue;
    }

    if (json[i] == '0') {
      ++i;
    } else {
      while (i < json.size() && IsJsonDigit(json[i])) ++i;
    }

    if (i < json.size() && json[i] == '.') {
      ++i;
      while (i < json.size() && IsJsonDigit(json[i])) ++i;
    }

    bool tinyNegativeExponent = false;
    if (i < json.size() && (json[i] == 'e' || json[i] == 'E')) {
      std::size_t exp = i + 1;
      bool negativeExponent = false;
      if (exp < json.size() && (json[exp] == '+' || json[exp] == '-')) {
        negativeExponent = json[exp] == '-';
        ++exp;
      }
      int exponentValue = 0;
      bool hasExponentDigits = false;
      while (exp < json.size() && IsJsonDigit(json[exp])) {
        hasExponentDigits = true;
        if (exponentValue <= 1000)
          exponentValue = exponentValue * 10 + (json[exp] - '0');
        ++exp;
      }
      if (hasExponentDigits) {
        tinyNegativeExponent = negativeExponent && exponentValue > 45;
        i = exp;
      }
    }

    if (tinyNegativeExponent) {
      out += "0.0";
      ++normalizedCount;
    } else {
      out.append(json, start, i - start);
    }
  }

  return out;
}

} // namespace

bool ParseJson(const std::string& json, Document& out) {
  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(out, json);
  if (ec) {
    std::size_t normalizedCount = 0;
    std::string normalizedJson = NormalizeTinyFloatLiterals(json, normalizedCount);
    if (normalizedCount > 0) {
      Document retry;
      auto retryEc = glz::read<glz::opts{.error_on_unknown_keys = false}>(retry, normalizedJson);
      if (!retryEc) {
        out = std::move(retry);
        T8_LOG_INFO("[glTF] Normalized %zu tiny float literal(s) while parsing JSON", normalizedCount);
        return true;
      }
    }

    std::string err = glz::format_error(ec, json);
    T8_LOG_ERROR("[glTF] JSON parse error: %s", err.c_str());
    return false;
  }
  return true;
}

} // namespace gltf
} // namespace t850
