#include <pch.h>
/*********************************************************
 * Minimal base64 decoder for `data:` URIs in glTF buffers / images.
 * No allocation beyond the output vector. Tolerates whitespace.
 *********************************************************/

#include <utils/gltf/GLTFTypes.h>
#include <cstdint>
#include <vector>

namespace t850 {
namespace gltf {

static int B64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

bool Base64Decode(const char* src, std::size_t len,
                  std::vector<unsigned char>& out) {
  out.clear();
  out.reserve((len * 3) / 4);

  uint32_t buf = 0;
  int bits = 0;
  for (std::size_t i = 0; i < len; ++i) {
    char c = src[i];
    if (c == '=' || c == '\0') break;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
    int v = B64Val(c);
    if (v < 0) return false;
    buf = (buf << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<unsigned char>((buf >> bits) & 0xFF));
    }
  }
  return true;
}

} // namespace gltf
} // namespace t850
