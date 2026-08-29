/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include <pch.h>

#include <terrain/VoxelAtlas.h>

#include <Descriptors.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <utils/cil.h>
#include <video/BaseDriver.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace t850::terrain {

namespace {

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

// Mirrors Texture::LoadTexture: bare names live under Textures/.
std::string NormalizeAtlasPath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  const std::string lower = ToLowerAscii(path);
  const bool hasFolder = lower.find('/') != std::string::npos;
  const bool underAssets = lower.rfind("assets/", 0) == 0;
  if (!hasFolder && !underAssets) {
    path = "Textures/" + path;
  }
  return path;
}

// Reads the intrinsic pixel dimensions of a PNG/JPG/BMP/TGA file header.
// Returns 0x0 when the format is unrecognized. Used to detect the global
// FORCE_LOW_RES_TEXTURES downscale that cil_load applies after decoding.
void ReadImageHeaderSize(const std::string& path, int& outW, int& outH) {
  outW = 0;
  outH = 0;
  std::vector<unsigned char> head(34, 0);
  if (!t850::ResourceLocator::Instance().ReadBinary(path, head)) return;
  if (head.size() < 18) return;
  const unsigned char* b = head.data();
  if (std::memcmp(b, "\x89PNG\r\n\x1a\n", 8) == 0) {
    outW = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
    outH = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
    return;
  }
  if (b[0] == 'B' && b[1] == 'M') {
    outW = b[18] | (b[19] << 8) | (b[20] << 16) | (b[21] << 24);
    outH = std::abs((int)(b[22] | (b[23] << 8) | (b[24] << 16) | (b[25] << 24)));
    return;
  }
  if (b[0] == 0xFF && b[1] == 0xD8) { // JPEG: scan for SOFn markers
    size_t pos = 2;
    while (pos + 9 < head.size()) {
      if (b[pos] != 0xFF) { ++pos; continue; }
      const unsigned char marker = b[pos + 1];
      if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
        outH = (b[pos + 5] << 8) | b[pos + 6];
        outW = (b[pos + 7] << 8) | b[pos + 8];
        return;
      }
      if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { pos += 2; continue; }
      const size_t len = ((size_t)b[pos + 2] << 8) | b[pos + 3];
      pos += 2 + len;
    }
    return;
  }
  if ((b[1] & 0x08) != 0 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0 &&
      (b[1] & 0x02) == 0 && (b[1] & 0x10) == 0) { // uncompressed TGA
    outW = b[12] | (b[13] << 8);
    outH = b[14] | (b[15] << 8);
    return;
  }
}

} // namespace

VoxelAtlas LoadVoxelAtlas(Device* device, const std::string& relativePath, int tilePx) {
  VoxelAtlas atlas;
  if (!device || relativePath.empty() || tilePx < 1) {
    T8_LOG_ERROR("[VoxelAtlas] Invalid load request (path='%s', tilePx=%d)",
                 relativePath.c_str(), tilePx);
    return atlas;
  }

  const std::string path = NormalizeAtlasPath(relativePath);
  int width = 0;
  int height = 0;
  unsigned int mipmaps = 0;
  unsigned int props = 0;
  unsigned int bufferSize = 0;
  unsigned char* pixels = cil_load(path.c_str(), &width, &height, &mipmaps, &props, &bufferSize);
  if (!pixels || width < tilePx || height < tilePx) {
    T8_LOG_ERROR("[VoxelAtlas] Failed to decode atlas image '%s'", path.c_str());
    if (pixels) cil_free_buffer(pixels, props);
    return atlas;
  }
  if (!(props & CIL_RGBA) || (props & CIL_COMPRESSED)) {
    T8_LOG_ERROR("[VoxelAtlas] Atlas '%s' must decode to uncompressed RGBA", path.c_str());
    cil_free_buffer(pixels, props);
    return atlas;
  }

  // cil_load applies the global FORCE_LOW_RES_TEXTURES downscale, which would
  // break tile-grid alignment. Undo it with nearest-neighbor sampling back to
  // the file's intrinsic size (read from the image header), then pad up to
  // power-of-two dimensions so every backend builds a full mip chain. NEAREST
  // sampling with CLAMP_TO_EDGE never reads the black pad region.
  {
    int fileW = 0;
    int fileH = 0;
    ReadImageHeaderSize(path, fileW, fileH);
    // Only undo an exact integer downscale (FORCE_LOW_RES_TEXTURES uses a
    // single factor for both axes), so nearest-neighbor sampling recovers
    // whole source texels instead of blending.
    int targetW = width;
    int targetH = height;
    if (fileW >= width && fileH >= height && fileW % width == 0 && fileH % height == 0 &&
        fileW / width == fileH / height) {
      targetW = fileW;
      targetH = fileH;
    }
    int potW = 1; while (potW < targetW) potW <<= 1;
    int potH = 1; while (potH < targetH) potH <<= 1;
    if (potW != width || potH != height) {
      std::vector<unsigned char> resampled((size_t)potW * potH * 4, 0);
      for (int y = 0; y < targetH; ++y) {
        const int sy = (int)(((int64_t)y * height) / targetH);
        for (int x = 0; x < targetW; ++x) {
          const int sx = (int)(((int64_t)x * width) / targetW);
          const unsigned char* src = pixels + ((size_t)sy * width + sx) * 4;
          unsigned char* dst = &resampled[((size_t)y * potW + x) * 4];
          dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        }
      }
      cil_free_buffer(pixels, props);
      pixels = new unsigned char[resampled.size()];
      std::copy(resampled.begin(), resampled.end(), pixels);
      props = CIL_RAW; // plain delete[] release path from here on
      width = potW;
      height = potH;
    }
  }

  atlas.texture = device->CreateTextureFromMemory(pixels, width, height, 4, "voxel_atlas");
  cil_free_buffer(pixels, props);
  if (!atlas.texture) {
    T8_LOG_ERROR("[VoxelAtlas] Device upload failed for atlas '%s'", path.c_str());
    return atlas;
  }

  atlas.texture->params = TextBasicParams::CLAMP_TO_EDGE | TextBasicParams::NEAREST_FILTER;
  atlas.texture->SetTextureParams();

  atlas.widthPx = width;
  atlas.heightPx = height;
  atlas.tilePx = tilePx;
  atlas.tilesPerAxisX = width / tilePx;
  atlas.tilesPerAxisY = height / tilePx;
  T8_LOG_INFO("[VoxelAtlas] Loaded '%s' %dx%d, tile %dpx, grid %dx%d", path.c_str(),
              width, height, tilePx, atlas.tilesPerAxisX, atlas.tilesPerAxisY);
  return atlas;
}

} // namespace t850::terrain
