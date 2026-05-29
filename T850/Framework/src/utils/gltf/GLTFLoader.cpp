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
#include <utils/ThreadPool.h>
#include <utils/ResourceLocator.h>
#include <debug/LoadingProgress.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace t850 {
namespace gltf {

namespace {

constexpr uint32_t GLB_MAGIC      = 0x46546C67; // "glTF"
constexpr uint32_t GLB_CHUNK_JSON = 0x4E4F534A; // "JSON"
constexpr uint32_t GLB_CHUNK_BIN  = 0x004E4942; // "BIN\0"
constexpr std::size_t kParallelBufferByteThreshold = 4ull * 1024ull * 1024ull;

bool ReadFileBytes(const std::string& path, std::vector<unsigned char>& out) {
  return ResourceLocator::Instance().ReadBinary(path, out);
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

// Resolve every Buffer in the document. `glbBin` is either the BIN chunk for
// .glb or, for large GLBs, the whole GLB container with bufferView offsets
// already rebased to the BIN chunk. For .gltf, buffer 0 with no uri is invalid.
bool ResolveBuffers(Document& doc,
                    const std::string& sourcePath,
                    std::vector<unsigned char>&& glbBin) {
  doc._bufferData.resize(doc.buffers.size());
  const std::string dir = DirOf(sourcePath);

  struct BufferResult {
    bool ok = false;
    std::string error;
  };
  std::vector<BufferResult> results(doc.buffers.size());
  std::size_t uriBufferCount = 0;
  std::size_t uriBufferBytes = 0;

  for (std::size_t i = 0; i < doc.buffers.size(); ++i) {
    const Buffer& b = doc.buffers[i];
    if (!b.uri) {
      // GLB-embedded: only buffer 0 can have no URI, and only for .glb.
      if (i != 0 || glbBin.empty()) {
        results[i].error = "has no uri and no GLB bin available";
        continue;
      }
      doc._bufferData[i] = std::move(glbBin);
      results[i].ok = true;
    } else {
      ++uriBufferCount;
      uriBufferBytes += b.byteLength;
    }
  }

  auto resolveBufferBytes = [&](int i) {
    const Buffer& b = doc.buffers[static_cast<std::size_t>(i)];
    if (!b.uri)
      return;

    auto& dst = doc._bufferData[static_cast<std::size_t>(i)];
    auto& result = results[static_cast<std::size_t>(i)];
    if (b.uri->compare(0, 5, "data:") == 0) {
      if (!DecodeDataUri(*b.uri, dst)) {
        result.error = "failed to decode data URI";
        return;
      }
    } else {
      // External file relative to the .gltf.
      std::string p = dir + *b.uri;
      if (!ReadFileBytes(p, dst)) {
        result.error = "cannot open '" + p + "'";
        return;
      }
    }
    result.ok = true;
  };

  const bool useParallelBufferLoad = g_threadPool
    && g_threadPool->NumWorkers() > 1
    && uriBufferCount > 1
    && uriBufferBytes >= kParallelBufferByteThreshold;
  if (useParallelBufferLoad) {
    T8_LOG_INFO("[glTF] Reading %zu URI buffers (%zu declared bytes) with %u global worker threads",
                uriBufferCount, uriBufferBytes, g_threadPool->NumWorkers());
    g_threadPool->ParallelFor(0, static_cast<int>(doc.buffers.size()), resolveBufferBytes);
  } else {
    if (uriBufferCount > 1) {
      T8_LOG_INFO("[glTF] Reading %zu URI buffers serially (%zu declared bytes)",
                  uriBufferCount, uriBufferBytes);
    }
    for (int i = 0; i < static_cast<int>(doc.buffers.size()); ++i) {
      resolveBufferBytes(i);
    }
  }

  for (std::size_t i = 0; i < doc.buffers.size(); ++i) {
    const Buffer& b = doc.buffers[i];
    const auto& dst = doc._bufferData[i];
    if (!results[i].ok) {
      T8_LOG_ERROR("[glTF] buffer %zu: %s", i, results[i].error.c_str());
      return false;
    }

    if (dst.size() < b.byteLength) {
      T8_LOG_ERROR("[glTF] buffer %zu: payload %zu < declared byteLength %zu",
                   i, dst.size(), b.byteLength);
      return false;
    }
  }
  return true;
}

bool RebaseGlbBufferViews(Document& doc,
                          std::size_t binOffset,
                          std::size_t binLength,
                          const std::string& sourcePath) {
  for (std::size_t i = 0; i < doc.bufferViews.size(); ++i) {
    BufferView& bv = doc.bufferViews[i];
    if (bv.buffer != 0) {
      continue;
    }
    if (bv.byteOffset > binLength || bv.byteLength > binLength - bv.byteOffset) {
      T8_LOG_ERROR("[glTF] GLB '%s' bufferView %zu range %zu+%zu exceeds BIN chunk length %zu",
                   sourcePath.c_str(), i, bv.byteOffset, bv.byteLength, binLength);
      return false;
    }
    bv.byteOffset += binOffset;
  }
  return true;
}

} // namespace

bool LoadGLTF(const std::string& path, Document& out) {
  LoadingProgress::SetCurrent("Loading model", path, "Reading glTF/GLB file");
  std::vector<unsigned char> raw;
  if (!ReadFileBytes(path, raw)) {
    T8_LOG_ERROR("[glTF] cannot open '%s'", path.c_str());
    return false;
  }
  if (raw.size() < 4) {
    T8_LOG_ERROR("[glTF] file too small: '%s'", path.c_str());
    return false;
  }
  LoadingProgress::Advance(0.8f);

  out = Document{};
  out._sourcePath = path;

  std::string json;
  std::vector<unsigned char> glbBin;
  std::size_t glbBinOffset = 0;
  std::size_t glbBinLength = 0;
  bool hasGlbBin = false;

  uint32_t magic;
  std::memcpy(&magic, raw.data(), 4);

  if (magic == GLB_MAGIC) {
    LoadingProgress::SetDetail("Parsing GLB chunks");
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
        glbBinOffset = off;
        glbBinLength = cLen;
        hasGlbBin = true;
      } // any further chunks: ignored per spec

      off += cLen;
      ++chunkIdx;
    }
    if (json.empty()) {
      T8_LOG_ERROR("[glTF] GLB '%s' has no JSON chunk", path.c_str());
      return false;
    }
  } else {
    LoadingProgress::SetDetail("Reading .gltf JSON");
    // Treat as plain JSON .gltf (UTF-8). Strip a UTF-8 BOM if present.
    std::size_t start = 0;
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
      start = 3;
    }
    json.assign(reinterpret_cast<const char*>(raw.data() + start),
                raw.size() - start);
  }

  LoadingProgress::SetCurrent("Loading model", path, "Parsing glTF JSON");
  if (!ParseJson(json, out)) {
    T8_LOG_ERROR("[glTF] JSON parse failed for '%s'", path.c_str());
    return false;
  }
  LoadingProgress::Advance(0.8f);
  out._sourcePath = path;

  // Hard-fail on required extensions we don't implement.
  // Supported extensions are allowed through.
  static const std::vector<std::string> kSupportedExtensions = {
    "KHR_draco_mesh_compression",
    "KHR_materials_anisotropy",
    "KHR_materials_clearcoat",
    "KHR_materials_diffuse_transmission",
    "KHR_materials_dispersion",
    "KHR_materials_emissive_strength",
    "KHR_materials_ior",
    "KHR_materials_iridescence",
    "KHR_materials_pbrSpecularGlossiness",
    "KHR_materials_sheen",
    "KHR_materials_specular",
    "KHR_materials_transmission",
    "KHR_materials_unlit",
    "KHR_materials_volume",
    "KHR_materials_volume_scatter",
    "MOZ_lightmap",
    "KHR_texture_transform"
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

  if (magic == GLB_MAGIC && hasGlbBin) {
    LoadingProgress::SetDetail("Rebasing GLB buffer views");
    if (!RebaseGlbBufferViews(out, glbBinOffset, glbBinLength, path)) {
      return false;
    }
    glbBin = std::move(raw);
  }

  LoadingProgress::SetCurrent("Loading model", path, "Resolving buffers: " + std::to_string(out.buffers.size()));
  if (!ResolveBuffers(out, path, std::move(glbBin))) {
    return false;
  }
  LoadingProgress::Advance(0.8f);

  LoadingProgress::SetCurrent("Loading model", path, "Validating meshes/materials/accessors");
  if (!ValidateDocument(out)) {
    return false;
  }
  LoadingProgress::Advance(0.5f);

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
} // namespace t850
