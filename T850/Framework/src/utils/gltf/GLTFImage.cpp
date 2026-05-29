#include <pch.h>
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
#include <debug/RenderTrace.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#define STBIR_INCLUDE_STB_IMAGE_RESIZE_H // skip bundled resize impl (defined in cil.cpp)
#include <stb_image.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utils/ThreadPool.h>
#include <utils/Log.h>

namespace t850 {
// T8Device is the per-API device singleton declared in BaseDriver.cpp.
extern Device* T8Device;
} // namespace t850

namespace t850 {
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
  if (!drv) return -1;
  for (std::size_t i = 0; i < drv->Textures.size(); ++i) {
    auto* t = drv->Textures[i];
    if (t && t->filepath == filepath) return static_cast<int>(i);
  }
  return -1;
}

#if defined(OS_ANDROID)
constexpr int kAndroidGltfMaxTextureDimension = 1024;

bool DownsampleAndroidRgba8(unsigned char*& pixels, int& width, int& height, const std::string& keyName) {
  if (!pixels || width <= 0 || height <= 0)
    return false;

  const int maxDimension = std::max(width, height);
  if (maxDimension <= kAndroidGltfMaxTextureDimension)
    return false;

  const float scale = static_cast<float>(kAndroidGltfMaxTextureDimension) / static_cast<float>(maxDimension);
  const int targetWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) * scale)));
  const int targetHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * scale)));
  const std::size_t targetBytes = static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight) * 4u;
  unsigned char* resized = static_cast<unsigned char*>(std::malloc(targetBytes));
  if (!resized) {
    T8_LOG_ERROR("[glTF] Android texture downsample allocation failed for '%s' (%dx%d -> %dx%d)",
                 keyName.c_str(), width, height, targetWidth, targetHeight);
    return false;
  }

  for (int y = 0; y < targetHeight; ++y) {
    const int srcY = std::min(height - 1, (y * height + targetHeight / 2) / targetHeight);
    for (int x = 0; x < targetWidth; ++x) {
      const int srcX = std::min(width - 1, (x * width + targetWidth / 2) / targetWidth);
      const unsigned char* src = pixels + (static_cast<std::size_t>(srcY) * width + srcX) * 4u;
      unsigned char* dst = resized + (static_cast<std::size_t>(y) * targetWidth + x) * 4u;
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = src[3];
    }
  }

  T8_LOG_INFO("[glTF] Android downscaled texture '%s': %dx%d -> %dx%d",
              keyName.c_str(), width, height, targetWidth, targetHeight);
  stbi_image_free(pixels);
  pixels = resized;
  width = targetWidth;
  height = targetHeight;
  return true;
}
#endif

// Decode an encoded (PNG/JPEG/...) byte buffer with stb_image and
// register the resulting RGBA8 surface into the driver. The cache key
// is `keyName`; the EffectDefault stored on the material should be the
// same `keyName` so that RenderMesh::LoadTex => CreateTexture("Textures/" + keyName)
// matches the cached filepath.
int RegisterEncoded(const unsigned char* bytes, std::size_t size,
                    const std::string& keyName) {
  if (!g_pBaseDriver || !::t850::T8Device) return -1;
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

#if defined(OS_ANDROID)
  DownsampleAndroidRgba8(px, w, h, keyName);
#endif

  ::t850::Texture* t = ::t850::T8Device->CreateTextureFromMemory(px, w, h, 4, keyName);
  stbi_image_free(px);
  if (!t) {
    T8_LOG_ERROR("[glTF] CreateTextureFromMemory failed for '%s'", keyName.c_str());
    return -1;
  }
  t->filepath = filepath;
  std::strncpy(t->optname, keyName.c_str(), sizeof(t->optname) - 1);
  t->optname[sizeof(t->optname) - 1] = '\0';

  auto* drv = g_pBaseDriver;
  // Reuse a free slot if present.
  for (std::size_t i = 0; i < drv->Textures.size(); ++i) {
    if (drv->Textures[i] == nullptr) {
      drv->Textures[i] = t;
      T8_TRACE_REGISTER_TEXTURE(t, "tex2d");
      return static_cast<int>(i);
    }
  }
  drv->Textures.push_back(t);
  T8_TRACE_REGISTER_TEXTURE(t, "tex2d");
  return static_cast<int>(drv->Textures.size() - 1);
}

int RegisterPlaceholder(const std::string& keyName, const char* reason) {
  if (!g_pBaseDriver || !::t850::T8Device || keyName.empty()) return -1;

  std::string filepath = "Textures/" + keyName;
  int existing = FindTextureSlot(filepath);
  if (existing >= 0) return existing;

  const unsigned char pixel[4] = { 255, 0, 255, 255 };
  ::t850::Texture* t = ::t850::T8Device->CreateTextureFromMemory(pixel, 1, 1, 4, keyName);
  if (!t) return -1;
  t->filepath = filepath;
  std::strncpy(t->optname, keyName.c_str(), sizeof(t->optname) - 1);
  t->optname[sizeof(t->optname) - 1] = '\0';

  auto* drv = g_pBaseDriver;
  for (std::size_t i = 0; i < drv->Textures.size(); ++i) {
    if (drv->Textures[i] == nullptr) {
      drv->Textures[i] = t;
      T8_TRACE_REGISTER_TEXTURE(t, "tex2d");
      T8_LOG_INFO("[glTF] image fallback '%s': %s", keyName.c_str(), reason ? reason : "decode failed");
      return static_cast<int>(i);
    }
  }
  drv->Textures.push_back(t);
  T8_TRACE_REGISTER_TEXTURE(t, "tex2d");
  T8_LOG_INFO("[glTF] image fallback '%s': %s", keyName.c_str(), reason ? reason : "decode failed");
  return static_cast<int>(drv->Textures.size() - 1);
}

bool ResolvePlaceholder(const std::string& keyName,
                        const char* reason,
                        std::string& outName,
                        int& outSlot) {
  int slot = RegisterPlaceholder(keyName, reason);
  if (slot < 0) return false;
  outName = keyName;
  outSlot = slot;
  return true;
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

  // External file path → load relative to the .gltf, decode with
  // stb_image, and pre-register under "Textures/<basename>" so a later
  // RenderMesh::LoadTex(<basename>) finds it via the existing driver
  // texture cache. Doing this in the loader (rather than deferring to
  // RenderMesh) is required because BaseDriver::CreateTexture prepends
  // "Textures/" to every path it receives — but typical glTF asset
  // bundles ship their textures alongside the .gltf, not under
  // Textures/. By materialising the bytes here we keep the existing
  // per-backend disk loader (cil) untouched and make path resolution
  // self-contained.
  if (img.uri && img.uri->compare(0, 5, "data:") != 0) {
    std::string dir;
    {
      auto s = doc._sourcePath.find_last_of("/\\");
      if (s != std::string::npos) dir = doc._sourcePath.substr(0, s + 1);
    }
    std::string fullPath = dir + *img.uri;
    std::vector<unsigned char> bytes;
    if (!ResourceLocator::Instance().ReadBinary(fullPath, bytes)) {
      T8_LOG_ERROR("[glTF] image %d: cannot open '%s'", imageIndex, fullPath.c_str());
      return ResolvePlaceholder(*img.uri, "external image missing", outName, outSlot);
    }
    if (bytes.empty()) {
      T8_LOG_ERROR("[glTF] image %d: empty file '%s'", imageIndex, fullPath.c_str());
      return ResolvePlaceholder(*img.uri, "external image empty", outName, outSlot);
    }
    // Cache key: keep the original URI so multiple primitives that
    // share the same texture hit the same driver slot.
    std::string keyName = *img.uri;
    int slot = RegisterEncoded(bytes.data(), bytes.size(), keyName);
    if (slot < 0) return ResolvePlaceholder(keyName, "external image decode failed", outName, outSlot);
    outName = keyName;
    outSlot = slot;
    return true;
  }

  // Build a stable cache key for the embedded blob.
  // We include imageIndex to guarantee uniqueness: glTF does not require image
  // names to be unique (they exist only for tooling display).  Without the
  // index, multiple materials that each embed their own "normal.png" would all
  // collide on the same cache slot and every surface would sample the first
  // loaded image.  Two glTF textures that intentionally share the same image
  // (same imageIndex) still generate the same key, so deduplication is preserved.
  std::string baseKey = !img.name.empty()
      ? img.name + "_img" + std::to_string(imageIndex)
      : (Stem(doc._sourcePath) + "_img" + std::to_string(imageIndex));
  // Force a benign extension so any downstream user of `filepath`
  // doesn't trip on missing dots.
  if (baseKey.find('.') == std::string::npos) baseKey += ".png";

  // Source A: data URI.
  if (img.uri && img.uri->compare(0, 5, "data:") == 0) {
    std::vector<unsigned char> decoded;
    const std::size_t comma = img.uri->find(',');
    if (comma == std::string::npos
        || !Base64Decode(img.uri->c_str() + comma + 1,
                      img.uri->size() - comma - 1,
                      decoded)) {
      T8_LOG_ERROR("[glTF] image %d: bad data URI", imageIndex);
      return ResolvePlaceholder(baseKey, "bad image data URI", outName, outSlot);
    }
    int slot = RegisterEncoded(decoded.data(), decoded.size(), baseKey);
    if (slot < 0) return ResolvePlaceholder(baseKey, "data URI image decode failed", outName, outSlot);
    outName = baseKey;
    outSlot = slot;
    return true;
  }

  // Source B: bufferView blob.
  if (img.bufferView) {
    int bvIdx = *img.bufferView;
    if (bvIdx < 0 || bvIdx >= static_cast<int>(doc.bufferViews.size())) {
      T8_LOG_ERROR("[glTF] image %d: bufferView %d OOR", imageIndex, bvIdx);
      return ResolvePlaceholder(baseKey, "image bufferView OOR", outName, outSlot);
    }
    const BufferView& bv = doc.bufferViews[bvIdx];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(doc._bufferData.size())) {
      T8_LOG_ERROR("[glTF] image %d: buffer %d OOR", imageIndex, bv.buffer);
      return ResolvePlaceholder(baseKey, "image buffer OOR", outName, outSlot);
    }
    const auto& buf = doc._bufferData[bv.buffer];
    if (bv.byteOffset + bv.byteLength > buf.size()) {
      T8_LOG_ERROR("[glTF] image %d: bufferView OOR", imageIndex);
      return ResolvePlaceholder(baseKey, "image bufferView range OOR", outName, outSlot);
    }
    int slot = RegisterEncoded(buf.data() + bv.byteOffset,
                               bv.byteLength, baseKey);
    if (slot < 0) return ResolvePlaceholder(baseKey, "bufferView image decode failed", outName, outSlot);
    outName = baseKey;
    outSlot = slot;
    return true;
  }

  T8_LOG_ERROR("[glTF] image %d has neither uri nor bufferView", imageIndex);
  return ResolvePlaceholder(baseKey, "image has neither uri nor bufferView", outName, outSlot);
}

// ── Batch-parallel image resolution ────────────────────────────────
void ResolveAllImages(const Document& doc,
                      std::vector<std::string>& outNames,
                      std::vector<int>& outSlots) {
  int numImages = static_cast<int>(doc.images.size());
  outNames.resize(numImages);
  outSlots.resize(numImages, -1);
  if (numImages == 0) return;

  // Per-image CPU decode result. GPU upload still happens serially after
  // all disk/base64/bufferView reads and stb decodes complete.
  struct DecodeResult {
    std::string keyName;
    std::vector<unsigned char> rawBytes; // encoded bytes for stbi
    unsigned char* pixels = nullptr;     // decoded RGBA (needs stbi_image_free)
    int w = 0, h = 0;
    bool ok = false;
    std::string error;
  };
  std::vector<DecodeResult> results(numImages);
  std::string sourceDir;
  {
    auto s = doc._sourcePath.find_last_of("/\\");
    if (s != std::string::npos) sourceDir = doc._sourcePath.substr(0, s + 1);
  }

  auto gatherImageBytes = [&](int i) {
    const Image& img = doc.images[i];
    DecodeResult& r = results[i];

    // Build cache key — include image index for uniqueness (see ResolveImage comment).
    std::string baseKey = !img.name.empty()
        ? img.name + "_img" + std::to_string(i)
        : (Stem(doc._sourcePath) + "_img" + std::to_string(i));
    if (baseKey.find('.') == std::string::npos) baseKey += ".png";

    if (img.uri && img.uri->compare(0, 5, "data:") != 0) {
      // External file relative to the glTF. Read the encoded payload now;
      // it will be decoded in the CPU-only phase and uploaded serially.
      const std::string fullPath = sourceDir + *img.uri;
      if (!ResourceLocator::Instance().ReadBinary(fullPath, r.rawBytes)) {
        T8_LOG_ERROR("[glTF] image %d: cannot open '%s'", i, fullPath.c_str());
        r.keyName = *img.uri;
        r.error = "external image missing";
        return;
      }
      if (r.rawBytes.empty()) {
        T8_LOG_ERROR("[glTF] image %d: empty file '%s'", i, fullPath.c_str());
        r.keyName = *img.uri;
        r.error = "external image empty";
        return;
      }
      r.keyName = *img.uri;
      r.ok = true;
      return;
    }

    if (img.uri && img.uri->compare(0, 5, "data:") == 0) {
      // Data URI — base64 decode
      std::vector<unsigned char> decoded;
      const std::size_t comma = img.uri->find(',');
      if (comma != std::string::npos
          && Base64Decode(img.uri->c_str() + comma + 1,
                        img.uri->size() - comma - 1,
                        decoded)) {
        r.keyName = baseKey;
        r.rawBytes = std::move(decoded);
        r.ok = true;
      } else {
        r.keyName = baseKey;
        r.error = "bad image data URI";
      }
      return;
    }

    if (img.bufferView) {
      int bvIdx = *img.bufferView;
      if (bvIdx >= 0 && bvIdx < static_cast<int>(doc.bufferViews.size())) {
        const BufferView& bv = doc.bufferViews[bvIdx];
        if (bv.buffer >= 0 && bv.buffer < static_cast<int>(doc._bufferData.size())) {
          const auto& buf = doc._bufferData[bv.buffer];
          if (bv.byteOffset + bv.byteLength <= buf.size()) {
            r.keyName = baseKey;
            // Reference buffer data directly — no copy needed, buffer outlives decode
            r.rawBytes.assign(buf.data() + bv.byteOffset,
                              buf.data() + bv.byteOffset + bv.byteLength);
            r.ok = true;
            return;
          }
        }
      }
      r.keyName = baseKey;
      r.error = "image bufferView range invalid";
      return;
    }

    r.keyName = baseKey;
    r.error = "image has neither uri nor bufferView";
  };

  // Decode one encoded image byte buffer with stb_image (CPU-only, thread-safe).
  auto decodeImage = [&](int i) {
      DecodeResult& r = results[i];
      if (!r.ok || r.rawBytes.empty()) return;

      int ch = 0;
      r.pixels = stbi_load_from_memory(r.rawBytes.data(),
                                        static_cast<int>(r.rawBytes.size()),
                                        &r.w, &r.h, &ch, 4);
      if (!r.pixels) {
        r.ok = false;
        r.error = stbi_failure_reason() ? stbi_failure_reason() : "stbi decode failed";
      }
      // Free raw bytes now that we have decoded pixels
      r.rawBytes.clear();
      r.rawBytes.shrink_to_fit();
  };

  auto uploadImage = [&](int i) {
    DecodeResult& r = results[i];
    if (!r.ok) {
      int slot = RegisterPlaceholder(r.keyName, r.error.c_str());
      if (slot >= 0) {
        outNames[i] = r.keyName;
        outSlots[i] = slot;
      }
      return;
    }

    if (!r.pixels) {
      int slot = RegisterPlaceholder(r.keyName, "image decode produced no pixels");
      if (slot >= 0) {
        outNames[i] = r.keyName;
        outSlots[i] = slot;
      }
      return;
    }

    std::string filepath = "Textures/" + r.keyName;
    int existing = FindTextureSlot(filepath);
    if (existing >= 0) {
      outNames[i] = r.keyName;
      outSlots[i] = existing;
      stbi_image_free(r.pixels);
      r.pixels = nullptr;
      return;
    }

#if defined(OS_ANDROID)
    DownsampleAndroidRgba8(r.pixels, r.w, r.h, r.keyName);
#endif

    ::t850::Texture* t = ::t850::T8Device->CreateTextureFromMemory(
        r.pixels, r.w, r.h, 4, r.keyName);
    stbi_image_free(r.pixels);
    r.pixels = nullptr;

    if (!t) return;
    t->filepath = filepath;
    std::strncpy(t->optname, r.keyName.c_str(), sizeof(t->optname) - 1);
    t->optname[sizeof(t->optname) - 1] = '\0';

    auto* drv = g_pBaseDriver;
    bool reused = false;
    for (std::size_t s = 0; s < drv->Textures.size(); ++s) {
      if (drv->Textures[s] == nullptr) {
        drv->Textures[s] = t;
        outSlots[i] = static_cast<int>(s);
        reused = true;
        break;
      }
    }
    if (!reused) {
      drv->Textures.push_back(t);
      outSlots[i] = static_cast<int>(drv->Textures.size() - 1);
    }
    T8_TRACE_REGISTER_TEXTURE(t, "tex2d");
    outNames[i] = r.keyName;
  };

#if defined(OS_ANDROID)
  constexpr bool resolveImagesSerially = true;
#else
  constexpr bool resolveImagesSerially = sizeof(void*) < 8;
#endif
  if (resolveImagesSerially) {
#if defined(OS_ANDROID)
    T8_LOG_INFO("[glTF] Resolving %d images serially for Android", numImages);
#else
    T8_LOG_INFO("[glTF] Resolving %d images serially for 32-bit process", numImages);
#endif
    for (int i = 0; i < numImages; i++) {
      gatherImageBytes(i);
      decodeImage(i);
      uploadImage(i);
    }
    T8_LOG_INFO("[glTF] Image resolution complete: %d images", numImages);
    return;
  }

  // Phase 1: Gather encoded bytes (disk I/O + base64 decode + bufferView copy).
  if (g_threadPool && numImages > 1) {
    T8_LOG_INFO("[glTF] Reading %d images with %u global worker threads", numImages, g_threadPool->NumWorkers());
    g_threadPool->ParallelFor(0, numImages, gatherImageBytes);
  } else {
    for (int i = 0; i < numImages; i++) {
      gatherImageBytes(i);
    }
  }

  if (g_threadPool && numImages > 1) {
    T8_LOG_INFO("[glTF] Decoding %d images with %u global worker threads", numImages, g_threadPool->NumWorkers());
    g_threadPool->ParallelFor(0, numImages, decodeImage);
  } else {
    for (int i = 0; i < numImages; i++) {
      decodeImage(i);
    }
  }

  // Phase 3: Serial GPU upload + driver cache insertion
  if (g_pBaseDriver)
    g_pBaseDriver->BeginResourceUploadBatch();
  for (int i = 0; i < numImages; i++) {
    uploadImage(i);
  }
  if (g_pBaseDriver)
    g_pBaseDriver->EndResourceUploadBatch();

  T8_LOG_INFO("[glTF] Image resolution complete: %d images", numImages);
}

} // namespace gltf
} // namespace t850
