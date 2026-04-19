/*********************************************************
 * glTF 2.0 — image / texture resolution.
 *
 * Three sources, in spec preference order:
 *   1. images[i].uri references an external file → use the engine's
 *      existing CreateTexture(path) which goes through cil/stb on disk.
 *   2. images[i].uri is a "data:" URI → base64-decode then stb_image
 *      from memory and register via CreateTextureFromMemory.
 *   3. images[i].bufferView references bytes embedded in a buffer
 *      (typical for .glb) → stb_image from memory + register.
 *
 * Embedded images are pre-registered into BaseDriver::Textures with
 * filepath = "Textures/<key>" so a subsequent
 * RenderMesh::LoadTex("<key>") via CreateTexture("<key>") hits the
 * cache and returns the same slot.
 *
 * Returns the engine texture slot ID (>= 0) or -1 on failure.
 *********************************************************/

#include <utils/gltf/GLTFImage.h>
#include <utils/gltf/GLTFAccessor.h>
#include <utils/gltf/GLTFTypes.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>

#include <stb_image.h>

#include <cstring>
#include <string>
#include <vector>

namespace t800 {
// T8Device is the per-API device singleton declared in BaseDriver.cpp.
extern Device* T8Device;
} // namespace t800

namespace t800 {
namespace gltf {

namespace {

// Strip directories from a filename.
std::string Basename(const std::string& p) {
  auto s = p.find_last_of("/\\");
  return (s == std::string::npos) ? p : p.substr(s + 1);
}

// Stem (basename without extension), used as a fallback texture name.
std::string Stem(const std::string& p) {
  std::string b = Basename(p);
  auto d = b.find_last_of('.');
  return (d == std::string::npos) ? b : b.substr(0, d);
}

// Look up an existing texture by filepath; returns its slot or -1.
int FindTextureSlot(const std::string& filepath) {
  auto* drv = g_pBaseDriver;
  for (std::size_t i = 0; i < drv->Textures.size(); ++i) {
    auto* t = drv->Textures[i];
    if (t && t->filepath == filepath) return static_cast<int>(i);
  }
  return -1;
}

// Decode an encoded (PNG/JPEG/...) byte buffer with stb_image and
// register the resulting RGBA8 surface into the driver. The cache key
// is `keyName`; the EffectDefault stored on the material should be the
// same `keyName` so that RenderMesh::LoadTex => CreateTexture("Textures/" + keyName)
// matches the cached filepath.
int RegisterEncoded(const unsigned char* bytes, std::size_t size,
                    const std::string& keyName) {
  std::string filepath = "Textures/" + keyName;
  int existing = FindTextureSlot(filepath);
  if (existing >= 0) return existing;

  int w = 0, h = 0, ch = 0;
  unsigned char* px = stbi_load_from_memory(bytes,
                                            static_cast<int>(size),
                                            &w, &h, &ch, 4);
  if (!px) {
    T8_LOG_ERROR("[glTF] stb_image_from_memory failed for '%s' (%s)",
                 keyName.c_str(), stbi_failure_reason());
    return -1;
  }

  ::t800::Texture* t = ::t800::T8Device->CreateTextureFromMemory(px, w, h, 4, keyName);
  stbi_image_free(px);
  if (!t) {
    T8_LOG_ERROR("[glTF] CreateTextureFromMemory failed for '%s'", keyName.c_str());
    return -1;
  }
  t->filepath = filepath;

  auto* drv = g_pBaseDriver;
  // Reuse a free slot if present.
  for (std::size_t i = 0; i < drv->Textures.size(); ++i) {
    if (drv->Textures[i] == nullptr) {
      drv->Textures[i] = t;
      return static_cast<int>(i);
    }
  }
  drv->Textures.push_back(t);
  return static_cast<int>(drv->Textures.size() - 1);
}

} // namespace

bool ResolveImage(const Document& doc, int imageIndex,
                  std::string& outName, int& outSlot) {
  outSlot = -1;
  outName.clear();
  if (imageIndex < 0 || imageIndex >= static_cast<int>(doc.images.size())) {
    return false;
  }
  const Image& img = doc.images[imageIndex];

  // External file path → defer loading to RenderMesh::LoadTex.
  if (img.uri && img.uri->compare(0, 5, "data:") != 0) {
    outName = *img.uri;       // stored relative to .gltf
    outSlot = -1;             // RenderMesh will CreateTexture(outName)
    return true;
  }

  // Build a stable cache key for the embedded blob.
  std::string baseKey = !img.name.empty()
      ? img.name
      : (Stem(doc._sourcePath) + "_img" + std::to_string(imageIndex));
  // Force a benign extension so any downstream user of `filepath`
  // doesn't trip on missing dots.
  if (baseKey.find('.') == std::string::npos) baseKey += ".png";

  // Source A: data URI.
  if (img.uri && img.uri->compare(0, 5, "data:") == 0) {
    std::vector<unsigned char> decoded;
    if (!Base64Decode(img.uri->c_str() + img.uri->find(',') + 1,
                      img.uri->size() - img.uri->find(',') - 1,
                      decoded)) {
      T8_LOG_ERROR("[glTF] image %d: bad data URI", imageIndex);
      return false;
    }
    int slot = RegisterEncoded(decoded.data(), decoded.size(), baseKey);
    if (slot < 0) return false;
    outName = baseKey;
    outSlot = slot;
    return true;
  }

  // Source B: bufferView blob.
  if (img.bufferView) {
    int bvIdx = *img.bufferView;
    if (bvIdx < 0 || bvIdx >= static_cast<int>(doc.bufferViews.size())) {
      T8_LOG_ERROR("[glTF] image %d: bufferView %d OOR", imageIndex, bvIdx);
      return false;
    }
    const BufferView& bv = doc.bufferViews[bvIdx];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(doc._bufferData.size())) {
      T8_LOG_ERROR("[glTF] image %d: buffer %d OOR", imageIndex, bv.buffer);
      return false;
    }
    const auto& buf = doc._bufferData[bv.buffer];
    if (bv.byteOffset + bv.byteLength > buf.size()) {
      T8_LOG_ERROR("[glTF] image %d: bufferView OOR", imageIndex);
      return false;
    }
    int slot = RegisterEncoded(buf.data() + bv.byteOffset,
                               bv.byteLength, baseKey);
    if (slot < 0) return false;
    outName = baseKey;
    outSlot = slot;
    return true;
  }

  T8_LOG_ERROR("[glTF] image %d has neither uri nor bufferView", imageIndex);
  return false;
}

} // namespace gltf
} // namespace t800
