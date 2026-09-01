#pragma once

#include <string>

namespace t850 {

class BaseDriver;

struct TextureAtlasRegion {
  int xPx = 0;
  int yPx = 0;
  int widthPx = 0;
  int heightPx = 0;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
};

struct TextureAtlasDesc {
  std::string texturePath;
  int tileWidthPx = 16;
  int tileHeightPx = 16;
  int pixelationFactor = 1;
  unsigned int textureParams = 0;
};

// Immutable grid metadata over a BaseDriver-managed texture. The driver owns
// the GPU texture; atlas users retain only its stable registry ID.
struct TextureAtlas {
  std::string key;
  int textureId = -1;
  int widthPx = 0;
  int heightPx = 0;
  int tileWidthPx = 0;
  int tileHeightPx = 0;
  int columns = 0;
  int rows = 0;

  bool IsValid() const;
  bool TryGetGridRegion(int column, int row, TextureAtlasRegion& outRegion) const;
};

TextureAtlas CreateTextureAtlas(BaseDriver* driver,
                                const std::string& key,
                                const unsigned char* rgbaPixels,
                                int widthPx,
                                int heightPx,
                                int tileWidthPx,
                                int tileHeightPx,
                                unsigned int textureParams = 0,
                                std::string* error = nullptr);

TextureAtlas LoadTextureAtlas(BaseDriver* driver,
                              const TextureAtlasDesc& desc,
                              std::string* error = nullptr);

} // namespace t850
