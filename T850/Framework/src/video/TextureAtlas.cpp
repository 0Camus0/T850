#include <pch.h>

#include <video/TextureAtlas.h>

#include <Descriptors.h>
#include <utils/Log.h>
#include <utils/cil.h>
#include <video/BaseDriver.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace t850 {
namespace {

void SetError(std::string* error, std::string message) {
  if (error) *error = std::move(message);
}

std::string NormalizeTexturePath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  if (lower.find('/') == std::string::npos && lower.rfind("assets/", 0) != 0)
    path = "Textures/" + path;
  return path;
}

unsigned int ResolveTextureParams(unsigned int params) {
  return params != 0
    ? params
    : static_cast<unsigned int>(TextBasicParams::CLAMP_TO_EDGE |
                                TextBasicParams::NEAREST_FILTER);
}

bool ValidateGrid(int widthPx, int heightPx, int tileWidthPx, int tileHeightPx,
                  std::string* error) {
  if (widthPx <= 0 || heightPx <= 0 || tileWidthPx <= 0 || tileHeightPx <= 0) {
    SetError(error, "atlas and tile dimensions must be positive");
    return false;
  }
  if (widthPx % tileWidthPx != 0 || heightPx % tileHeightPx != 0) {
    SetError(error, "atlas dimensions must be exact multiples of tile dimensions");
    return false;
  }
  return true;
}

uint64_t HashPixels(const unsigned char* pixels, std::size_t size) {
  uint64_t hash = 14695981039346656037ull;
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= pixels[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

} // namespace

bool TextureAtlas::IsValid() const {
  return textureId >= 0 && widthPx > 0 && heightPx > 0 &&
         tileWidthPx > 0 && tileHeightPx > 0 && columns > 0 && rows > 0;
}

bool TextureAtlas::TryGetGridRegion(int column, int row, TextureAtlasRegion& outRegion) const {
  if (!IsValid() || column < 0 || column >= columns || row < 0 || row >= rows)
    return false;

  outRegion.xPx = column * tileWidthPx;
  outRegion.yPx = row * tileHeightPx;
  outRegion.widthPx = tileWidthPx;
  outRegion.heightPx = tileHeightPx;
  outRegion.u0 = (static_cast<float>(outRegion.xPx) + 0.5f) / static_cast<float>(widthPx);
  outRegion.v0 = (static_cast<float>(outRegion.yPx) + 0.5f) / static_cast<float>(heightPx);
  outRegion.u1 = (static_cast<float>(outRegion.xPx + outRegion.widthPx) - 0.5f) /
                 static_cast<float>(widthPx);
  outRegion.v1 = (static_cast<float>(outRegion.yPx + outRegion.heightPx) - 0.5f) /
                 static_cast<float>(heightPx);
  return true;
}

TextureAtlas CreateTextureAtlas(BaseDriver* driver,
                                const std::string& key,
                                const unsigned char* rgbaPixels,
                                int widthPx,
                                int heightPx,
                                int tileWidthPx,
                                int tileHeightPx,
                                unsigned int textureParams,
                                std::string* error) {
  TextureAtlas atlas;
  if (!driver || !rgbaPixels || key.empty()) {
    SetError(error, "atlas creation requires a driver, key, and RGBA pixels");
    return atlas;
  }
  if (!ValidateGrid(widthPx, heightPx, tileWidthPx, tileHeightPx, error))
    return atlas;

  const unsigned int resolvedParams = ResolveTextureParams(textureParams);
  const uint64_t contentHash = HashPixels(
    rgbaPixels, static_cast<std::size_t>(widthPx) * heightPx * 4);
  const std::string registryKey = "atlas:" + key + "|" +
    std::to_string(widthPx) + "x" + std::to_string(heightPx) + "|tile=" +
    std::to_string(tileWidthPx) + "x" + std::to_string(tileHeightPx) + "|params=" +
    std::to_string(resolvedParams) + "|content=" + std::to_string(contentHash);
  const int textureId = driver->CreateTextureFromMemory(
    registryKey, rgbaPixels, widthPx, heightPx, 4);
  Texture* texture = driver->GetTexture(textureId);
  if (!texture) {
    SetError(error, "failed to create managed atlas texture");
    return atlas;
  }

  texture->params = resolvedParams;
  texture->SetTextureParams();

  atlas.key = key;
  atlas.textureId = textureId;
  atlas.widthPx = widthPx;
  atlas.heightPx = heightPx;
  atlas.tileWidthPx = tileWidthPx;
  atlas.tileHeightPx = tileHeightPx;
  atlas.columns = widthPx / tileWidthPx;
  atlas.rows = heightPx / tileHeightPx;
  return atlas;
}

TextureAtlas LoadTextureAtlas(BaseDriver* driver,
                              const TextureAtlasDesc& desc,
                              std::string* error) {
  TextureAtlas atlas;
  if (!driver || desc.texturePath.empty()) {
    SetError(error, "atlas load requires a driver and texture path");
    return atlas;
  }

  const std::string path = NormalizeTexturePath(desc.texturePath);
  int widthPx = 0;
  int heightPx = 0;
  unsigned int mipmaps = 0;
  unsigned int props = 0;
  unsigned int bufferSize = 0;
  const int pixelationFactor = (std::max)(1, desc.pixelationFactor);
  unsigned char* pixels = cil_load(
    path.c_str(), &widthPx, &heightPx, &mipmaps, &props, &bufferSize,
    static_cast<unsigned int>(pixelationFactor));
  if (!pixels) {
    SetError(error, "failed to decode atlas image '" + path + "'");
    return atlas;
  }
  if (!(props & CIL_RGBA) || (props & CIL_COMPRESSED)) {
    cil_free_buffer(pixels, props);
    SetError(error, "atlas image must decode to uncompressed RGBA");
    return atlas;
  }

  if (pixelationFactor > 1) {
    const int sourceWidth = widthPx;
    widthPx *= pixelationFactor;
    heightPx *= pixelationFactor;
    std::vector<unsigned char> expanded(static_cast<std::size_t>(widthPx) * heightPx * 4);
    for (int y = 0; y < heightPx; ++y) {
      const int sourceY = y / pixelationFactor;
      for (int x = 0; x < widthPx; ++x) {
        const int sourceX = x / pixelationFactor;
        const unsigned char* source = pixels +
          (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) * 4;
        unsigned char* destination = expanded.data() +
          (static_cast<std::size_t>(y) * widthPx + x) * 4;
        std::copy_n(source, 4, destination);
      }
    }
    cil_free_buffer(pixels, props);
    pixels = new unsigned char[expanded.size()];
    std::copy(expanded.begin(), expanded.end(), pixels);
    props = CIL_RAW | CIL_RGBA;
  }

  atlas = CreateTextureAtlas(driver, path, pixels, widthPx, heightPx,
                             desc.tileWidthPx, desc.tileHeightPx,
                             desc.textureParams, error);
  cil_free_buffer(pixels, props);
  if (atlas.IsValid()) {
    T8_LOG_INFO("[TextureAtlas] Loaded '%s' %dx%d, tile %dx%d, grid %dx%d, pixelation=%dx",
                path.c_str(), widthPx, heightPx,
          atlas.tileWidthPx, atlas.tileHeightPx, atlas.columns, atlas.rows,
          pixelationFactor);
  }
  return atlas;
}

} // namespace t850
