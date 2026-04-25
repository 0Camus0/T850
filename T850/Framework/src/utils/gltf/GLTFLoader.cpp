#include <pch.h>
/*********************************************************
 * glTF 2.0 — top-level loader.
 *
 *  - LoadGLTF()  : sniff GLB vs JSON, read file, parse JSON, resolve
 *                  every Buffer's bytes (file URI / data URI / GLB BIN),
 *                  validate.
 *  - ConvertToXDatabase() : public bridge implemented across
 *                  GLTFMesh.cpp / GLTFMaterial.cpp / GLTFAnimation.cpp.
 *
 *  GLB chunk layout (per spec §3.6):
 *      header { magic=0x46546C67, version=2, length }
 *      chunk0 { length, type=0x4E4F534A "JSON", data }
 *      chunk1 { length, type=0x004E4942 "BIN\0", data }   (optional)
 *      ...   (extra chunks ignored)
 *********************************************************/

#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFAccessor.h>
#include <utils/Log.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace t800 {
namespace gltf {

namespace {

constexpr uint32_t GLB_MAGIC      = 0x46546C67; // "glTF"
constexpr uint32_t GLB_CHUNK_JSON = 0x4E4F534A; // "JSON"
constexpr uint32_t GLB_CHUNK_BIN  = 0x004E4942; // "BIN\0"

bool ReadFileBytes(const std::string& path, std::vector<unsigned char>& out) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.is_open()) return false;
  std::streamsize sz = f.tellg();
  if (sz < 0) return false;
  f.seekg(0, std::ios::beg);
  out.resize(static_cast<std::size_t>(sz));
  if (sz > 0 && !f.read(reinterpret_cast<char*>(out.data()), sz)) return false;
  return true;
}

// Strip the file name from `path`, leaving the directory (with trailing
// slash). Empty string if no separator present.
std::string DirOf(const std::string& path) {
  auto p = path.find_last_of("/\\");
  if (p == std::string::npos) return std::string();
  return path.substr(0, p + 1);
}

// Decode "data:<mime>;base64,<payload>" URIs. Returns false for any
// other scheme (caller should treat as file path).
bool DecodeDataUri(const std::string& uri, std::vector<unsigned char>& out) {
  if (uri.compare(0, 5, "data:") != 0) return false;
  auto comma = uri.find(',');
  if (comma == std::string::npos) return false;
  std::string meta = uri.substr(5, comma - 5);
  bool isB64 = meta.find(";base64") != std::string::npos;
  if (!isB64) {
    // URL-encoded raw payload — uncommon for glTF buffers; not supported.
    T8_LOG_ERROR("[glTF] data URI with non-base64 payload not supported");
    return false;
  }
  return Base64Decode(uri.c_str() + comma + 1,
                      uri.size() - comma - 1, out);
}

// Resolve every Buffer in the document. `glbBin` is the BIN chunk for
// .glb (empty for .gltf). For .gltf, buffer 0 with no uri is invalid.
bool ResolveBuffers(Document& doc,
                    const std::string& sourcePath,
                    std::vector<unsigned char>&& glbBin) {
  doc._bufferData.resize(doc.buffers.size());
  const std::string dir = DirOf(sourcePath);

  for (std::size_t i = 0; i < doc.buffers.size(); ++i) {
    const Buffer& b = doc.buffers[i];
    auto& dst = doc._bufferData[i];

    if (!b.uri) {
      // GLB-embedded: only buffer 0 can have no URI, and only for .glb.
      if (i != 0 || glbBin.empty()) {
        T8_LOG_ERROR("[glTF] buffer %zu has no uri and no GLB bin available", i);
        return false;
      }
      dst = std::move(glbBin);
    } else if (b.uri->compare(0, 5, "data:") == 0) {
      if (!DecodeDataUri(*b.uri, dst)) {
        T8_LOG_ERROR("[glTF] buffer %zu: failed to decode data URI", i);
        return false;
      }
    } else {
      // External file relative to the .gltf.
      std::string p = dir + *b.uri;
      if (!ReadFileBytes(p, dst)) {
        T8_LOG_ERROR("[glTF] buffer %zu: cannot open '%s'", i, p.c_str());
        return false;
      }
    }

    if (dst.size() < b.byteLength) {
      T8_LOG_ERROR("[glTF] buffer %zu: payload %zu < declared byteLength %zu",
                   i, dst.size(), b.byteLength);
      return false;
    }
  }
  return true;
}

} // namespace

bool LoadGLTF(const std::string& path, Document& out) {
  std::vector<unsigned char> raw;
  if (!ReadFileBytes(path, raw)) {
    T8_LOG_ERROR("[glTF] cannot open '%s'", path.c_str());
    return false;
  }
  if (raw.size() < 4) {
    T8_LOG_ERROR("[glTF] file too small: '%s'", path.c_str());
    return false;
  }

  out = Document{};
  out._sourcePath = path;

  std::string json;
  std::vector<unsigned char> glbBin;

  uint32_t magic;
  std::memcpy(&magic, raw.data(), 4);

  if (magic == GLB_MAGIC) {
    if (raw.size() < 12) {
      T8_LOG_ERROR("[glTF] truncated GLB header in '%s'", path.c_str());
      return false;
    }
    uint32_t version, length;
    std::memcpy(&version, raw.data() + 4, 4);
    std::memcpy(&length,  raw.data() + 8, 4);
    if (version != 2) {
      T8_LOG_ERROR("[glTF] unsupported GLB version %u", version);
      return false;
    }
    if (length > raw.size()) {
      T8_LOG_ERROR("[glTF] GLB declared length %u > file size %zu",
                   length, raw.size());
      return false;
    }
    std::size_t off = 12;
    int chunkIdx = 0;
    while (off + 8 <= length) {
      uint32_t cLen, cType;
      std::memcpy(&cLen,  raw.data() + off,     4);
      std::memcpy(&cType, raw.data() + off + 4, 4);
      off += 8;
      if (off + cLen > length) {
        T8_LOG_ERROR("[glTF] GLB chunk %d: length %u overruns container",
                     chunkIdx, cLen);
        return false;
      }
      if (chunkIdx == 0) {
        if (cType != GLB_CHUNK_JSON) {
          T8_LOG_ERROR("[glTF] GLB chunk 0 is not JSON (type=0x%08X)", cType);
          return false;
        }
        json.assign(reinterpret_cast<const char*>(raw.data() + off), cLen);
      } else if (chunkIdx == 1 && cType == GLB_CHUNK_BIN) {
        glbBin.assign(raw.data() + off, raw.data() + off + cLen);
      } // any further chunks: ignored per spec

      off += cLen;
      ++chunkIdx;
    }
    if (json.empty()) {
      T8_LOG_ERROR("[glTF] GLB '%s' has no JSON chunk", path.c_str());
      return false;
    }
  } else {
    // Treat as plain JSON .gltf (UTF-8). Strip a UTF-8 BOM if present.
    std::size_t start = 0;
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
      start = 3;
    }
    json.assign(reinterpret_cast<const char*>(raw.data() + start),
                raw.size() - start);
  }

  if (!ParseJson(json, out)) {
    T8_LOG_ERROR("[glTF] JSON parse failed for '%s'", path.c_str());
    return false;
  }
  out._sourcePath = path;

  // Hard-fail on required extensions we don't implement.
  // Supported extensions are allowed through.
  static const std::vector<std::string> kSupportedExtensions = {
    "KHR_draco_mesh_compression",
  };
  if (!out.extensionsRequired.empty()) {
    std::string unsupported;
    for (auto& e : out.extensionsRequired) {
      bool supported = false;
      for (auto& s : kSupportedExtensions) {
        if (e == s) { supported = true; break; }
      }
      if (!supported) { unsupported += e; unsupported += " "; }
    }
    if (!unsupported.empty()) {
      T8_LOG_ERROR("[glTF] '%s' requires extensions not implemented: %s",
                   path.c_str(), unsupported.c_str());
      return false;
    }
  }

  if (!ResolveBuffers(out, path, std::move(glbBin))) {
    return false;
  }

  if (!ValidateDocument(out)) {
    return false;
  }

  T8_LOG_INFO("[glTF] '%s' loaded: %zu meshes, %zu materials, %zu textures, "
              "%zu images, %zu buffers, %zu animations",
              path.c_str(),
              out.meshes.size(), out.materials.size(),
              out.textures.size(), out.images.size(),
              out.buffers.size(), out.animations.size());
  return true;
}

bool ValidateDocument(const Document& doc) {
  if (doc.asset.version.empty()) {
    T8_LOG_ERROR("[glTF] asset.version is missing");
    return false;
  }
  // Major version of glTF MUST be "2".
  if (doc.asset.version[0] != '2') {
    T8_LOG_ERROR("[glTF] unsupported asset.version '%s' (only 2.x)",
                 doc.asset.version.c_str());
    return false;
  }
  for (std::size_t i = 0; i < doc.bufferViews.size(); ++i) {
    const auto& bv = doc.bufferViews[i];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(doc.buffers.size())) {
      T8_LOG_ERROR("[glTF] bufferView %zu: buffer index %d OOR", i, bv.buffer);
      return false;
    }
  }
  for (std::size_t i = 0; i < doc.accessors.size(); ++i) {
    const auto& a = doc.accessors[i];
    if (ElementCount(a.type) == 0) {
      T8_LOG_ERROR("[glTF] accessor %zu: bad type '%s'", i, a.type.c_str());
      return false;
    }
    if (ComponentSize(a.componentType) == 0) {
      T8_LOG_ERROR("[glTF] accessor %zu: bad componentType %d",
                   i, a.componentType);
      return false;
    }
  }
  return true;
}

} // namespace gltf
} // namespace t800
