#include "pch.h"
#include "utils/GUIAtlasGenerator.h"
#include <utils/Log.h>

#include <algorithm>
#include <fstream>
#include <cstring>
#include <cmath>
#include <cstdio>

// stb_image: use extern declarations (implementation lives in cil.cpp;
// our bundled stb_image.h includes stb_image_resize without an impl guard,
// so including the header here would produce duplicate symbols).
extern "C" {
  unsigned char* stbi_load(const char* filename, int* x, int* y,
                           int* channels_in_file, int desired_channels);
  void stbi_image_free(void* retval_from_stbi_load);
}

// stb_image_write for saving atlas PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// glaze for JSON
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#include <glaze/glaze.hpp>
#pragma warning(pop)

namespace t800 {

// ─── JSON schema for atlas metadata ──────────────────────
struct AtlasEntryJson {
  std::string name;
  int px, py, pw, ph;
  int srcW, srcH;
  float u0, v0, u1, v1;
};

struct AtlasMetadataJson {
  int atlasWidth;
  int atlasHeight;
  std::vector<AtlasEntryJson> entries;
};

// ─── AddImage ────────────────────────────────────────────
void GUIAtlasGenerator::AddImage(const std::string& name,
                                  const std::string& path) {
  SourceImage img;
  img.name = name;
  img.path = path;
  m_sources.push_back(std::move(img));
}

// ─── Bilinear downscale ──────────────────────────────────
std::vector<uint8_t> GUIAtlasGenerator::Downscale(
    const uint8_t* src, int srcW, int srcH, int dstW, int dstH) {
  std::vector<uint8_t> dst(dstW * dstH * 4);

  for (int dy = 0; dy < dstH; dy++) {
    float sy = ((float)dy + 0.5f) * srcH / dstH - 0.5f;
    int sy0 = (std::max)(0, (int)std::floor(sy));
    int sy1 = (std::min)(srcH - 1, sy0 + 1);
    float fy = sy - sy0;

    for (int dx = 0; dx < dstW; dx++) {
      float sx = ((float)dx + 0.5f) * srcW / dstW - 0.5f;
      int sx0 = (std::max)(0, (int)std::floor(sx));
      int sx1 = (std::min)(srcW - 1, sx0 + 1);
      float fx = sx - sx0;

      for (int c = 0; c < 4; c++) {
        float v00 = src[(sy0 * srcW + sx0) * 4 + c];
        float v10 = src[(sy0 * srcW + sx1) * 4 + c];
        float v01 = src[(sy1 * srcW + sx0) * 4 + c];
        float v11 = src[(sy1 * srcW + sx1) * 4 + c];

        float top = v00 + (v10 - v00) * fx;
        float bot = v01 + (v11 - v01) * fx;
        float val = top + (bot - top) * fy;

        dst[(dy * dstW + dx) * 4 + c] =
            (uint8_t)(std::max)(0.0f, (std::min)(255.0f, val + 0.5f));
      }
    }
  }
  return dst;
}

// ─── Edge extrusion (prevents atlas bleeding at mip boundaries) ──
void GUIAtlasGenerator::ExtrudeEdges(int entryIdx, int padding) {
  if (padding <= 0) return;
  const auto& e = m_entries[entryIdx];
  int aw = m_atlasW;

  // Extrude left edge
  for (int py = e.py; py < e.py + e.ph; py++) {
    const uint8_t* edgePixel = &m_atlas[(py * aw + e.px) * 4];
    for (int p = 1; p <= padding && e.px - p >= 0; p++) {
      memcpy(&m_atlas[(py * aw + e.px - p) * 4], edgePixel, 4);
    }
  }
  // Extrude right edge
  for (int py = e.py; py < e.py + e.ph; py++) {
    const uint8_t* edgePixel = &m_atlas[(py * aw + e.px + e.pw - 1) * 4];
    for (int p = 1; p <= padding && e.px + e.pw - 1 + p < aw; p++) {
      memcpy(&m_atlas[(py * aw + e.px + e.pw - 1 + p) * 4], edgePixel, 4);
    }
  }
  // Extrude top edge (including already-extruded corners)
  int xStart = (std::max)(0, e.px - padding);
  int xEnd   = (std::min)(aw, e.px + e.pw + padding);
  for (int px = xStart; px < xEnd; px++) {
    const uint8_t* edgePixel = &m_atlas[(e.py * aw + px) * 4];
    for (int p = 1; p <= padding && e.py - p >= 0; p++) {
      memcpy(&m_atlas[((e.py - p) * aw + px) * 4], edgePixel, 4);
    }
  }
  // Extrude bottom edge
  int ah = m_atlasH;
  for (int px = xStart; px < xEnd; px++) {
    const uint8_t* edgePixel = &m_atlas[((e.py + e.ph - 1) * aw + px) * 4];
    for (int p = 1; p <= padding && e.py + e.ph - 1 + p < ah; p++) {
      memcpy(&m_atlas[((e.py + e.ph - 1 + p) * aw + px) * 4], edgePixel, 4);
    }
  }
}

// ─── Shelf-packing algorithm ─────────────────────────────
// Sorts sprites by height (tallest first), packs left-to-right
// in horizontal shelves, starting new shelves as needed.

bool GUIAtlasGenerator::Generate(int maxAtlasSize,
                                  int maxSpriteSize,
                                  int padding) {
  // Load all source images
  for (auto& img : m_sources) {
    int w, h, channels;
    uint8_t* data = stbi_load(img.path.c_str(), &w, &h, &channels, 4);
    if (!data) {
      T8_LOG_ERROR("[AtlasGen] Failed to load '%s'", img.path.c_str());
      return false;
    }
    img.w = w;
    img.h = h;
    img.pixels.assign(data, data + w * h * 4);
    stbi_image_free(data);
    T8_LOG_INFO("[AtlasGen] Loaded '%s' (%dx%d)", img.name.c_str(), w, h);
  }

  // Downscale sprites exceeding maxSpriteSize
  for (auto& img : m_sources) {
    int maxDim = (std::max)(img.w, img.h);
    if (maxDim > maxSpriteSize) {
      float scale = (float)maxSpriteSize / (float)maxDim;
      int newW = (std::max)(1, (int)(img.w * scale));
      int newH = (std::max)(1, (int)(img.h * scale));
      T8_LOG_INFO("[AtlasGen] Downscaling '%s' %dx%d -> %dx%d",
                  img.name.c_str(), img.w, img.h, newW, newH);
      img.pixels = Downscale(img.pixels.data(), img.w, img.h, newW, newH);
      img.w = newW;
      img.h = newH;
    }
  }

  // Build sort indices by height (tallest first, then widest)
  std::vector<int> order(m_sources.size());
  for (int i = 0; i < (int)order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    if (m_sources[a].h != m_sources[b].h)
      return m_sources[a].h > m_sources[b].h;
    return m_sources[a].w > m_sources[b].w;
  });

  // Try progressively larger atlas sizes
  int atlasSize = 256;
  while (atlasSize <= maxAtlasSize) {
    // Attempt shelf pack at this size
    int cursorX = padding;
    int cursorY = padding;
    int shelfH  = 0;
    bool fits   = true;

    m_entries.clear();
    m_entries.resize(m_sources.size());

    for (int idx : order) {
      auto& img = m_sources[idx];
      int paddedW = img.w + padding;
      int paddedH = img.h + padding;

      // Move to next shelf if doesn't fit horizontally
      if (cursorX + paddedW > atlasSize) {
        cursorX = padding;
        cursorY += shelfH + padding;
        shelfH = 0;
      }
      // Check vertical fit
      if (cursorY + paddedH > atlasSize) {
        fits = false;
        break;
      }

      auto& e  = m_entries[idx];
      e.name   = img.name;
      e.px     = cursorX;
      e.py     = cursorY;
      e.pw     = img.w;
      e.ph     = img.h;
      e.srcW   = m_sources[idx].w; // post-downscale size is what goes in atlas
      e.srcH   = m_sources[idx].h;

      cursorX += paddedW;
      shelfH   = (std::max)(shelfH, img.h);
    }

    if (fits) {
      atlasSize = (std::max)(atlasSize, cursorY + shelfH + padding);
      // Round up to next power-of-2
      int pot = 1;
      while (pot < atlasSize) pot *= 2;
      atlasSize = (std::min)(pot, maxAtlasSize);
      break;
    }

    atlasSize *= 2;
    if (atlasSize > maxAtlasSize) {
      T8_LOG_ERROR("[AtlasGen] Sprites don't fit in %dx%d atlas",
                   maxAtlasSize, maxAtlasSize);
      return false;
    }
  }

  // Square atlas
  m_atlasW = atlasSize;
  m_atlasH = atlasSize;
  m_atlas.assign(m_atlasW * m_atlasH * 4, 0);

  T8_LOG_INFO("[AtlasGen] Atlas size: %dx%d (%d sprites)",
              m_atlasW, m_atlasH, (int)m_sources.size());

  // Blit sprites and compute UVs
  for (int i = 0; i < (int)m_sources.size(); i++) {
    auto& img = m_sources[i];
    auto& e   = m_entries[i];

    // Blit into atlas
    for (int row = 0; row < img.h; row++) {
      const uint8_t* srcRow = &img.pixels[row * img.w * 4];
      uint8_t* dstRow = &m_atlas[((e.py + row) * m_atlasW + e.px) * 4];
      memcpy(dstRow, srcRow, img.w * 4);
    }

    // Compute normalised UVs
    e.u0 = (float)e.px / (float)m_atlasW;
    e.v0 = (float)e.py / (float)m_atlasH;
    e.u1 = (float)(e.px + e.pw) / (float)m_atlasW;
    e.v1 = (float)(e.py + e.ph) / (float)m_atlasH;

    // Extrude edges for mip safety
    ExtrudeEdges(i, padding);

    T8_LOG_INFO("[AtlasGen]   '%s' at (%d,%d) %dx%d  UV(%.4f,%.4f)-(%.4f,%.4f)",
                e.name.c_str(), e.px, e.py, e.pw, e.ph,
                e.u0, e.v0, e.u1, e.v1);
  }

  // Free source pixel data (no longer needed)
  for (auto& img : m_sources) {
    img.pixels.clear();
    img.pixels.shrink_to_fit();
  }

  return true;
}

// ─── Save atlas PNG + JSON ───────────────────────────────
bool GUIAtlasGenerator::Save(const std::string& pngPath,
                              const std::string& jsonPath) const {
  // Write PNG
  int ok = stbi_write_png(pngPath.c_str(), m_atlasW, m_atlasH, 4,
                           m_atlas.data(), m_atlasW * 4);
  if (!ok) {
    T8_LOG_ERROR("[AtlasGen] Failed to write '%s'", pngPath.c_str());
    return false;
  }
  T8_LOG_INFO("[AtlasGen] Wrote atlas image: %s (%dx%d)",
              pngPath.c_str(), m_atlasW, m_atlasH);

  // Build JSON metadata
  AtlasMetadataJson meta;
  meta.atlasWidth  = m_atlasW;
  meta.atlasHeight = m_atlasH;
  for (auto& e : m_entries) {
    AtlasEntryJson ej;
    ej.name = e.name;
    ej.px   = e.px;   ej.py  = e.py;
    ej.pw   = e.pw;   ej.ph  = e.ph;
    ej.srcW = e.srcW;  ej.srcH = e.srcH;
    ej.u0   = e.u0;   ej.v0  = e.v0;
    ej.u1   = e.u1;   ej.v1  = e.v1;
    meta.entries.push_back(ej);
  }

  auto json = glz::write_json(meta);
  if (!json) {
    T8_LOG_ERROR("[AtlasGen] Failed to serialise JSON");
    return false;
  }

  std::ofstream ofs(jsonPath);
  if (!ofs.is_open()) {
    T8_LOG_ERROR("[AtlasGen] Failed to open '%s' for writing", jsonPath.c_str());
    return false;
  }
  ofs << json.value();
  ofs.close();

  T8_LOG_INFO("[AtlasGen] Wrote atlas metadata: %s", jsonPath.c_str());
  return true;
}

// ─── LoadMetadata (runtime) ──────────────────────────────
bool GUIAtlasGenerator::LoadMetadata(const std::string& jsonPath,
                                      std::vector<AtlasEntry>& entries,
                                      int& atlasW, int& atlasH) {
  std::ifstream ifs(jsonPath);
  if (!ifs.is_open()) return false;

  std::string content((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
  ifs.close();

  AtlasMetadataJson meta;
  auto err = glz::read_json(meta, content);
  if (err) {
    T8_LOG_ERROR("[AtlasGen] Failed to parse '%s': %s",
                 jsonPath.c_str(), glz::format_error(err, content).c_str());
    return false;
  }

  atlasW = meta.atlasWidth;
  atlasH = meta.atlasHeight;
  entries.clear();
  for (auto& ej : meta.entries) {
    AtlasEntry e;
    e.name = ej.name;
    e.px   = ej.px;   e.py  = ej.py;
    e.pw   = ej.pw;   e.ph  = ej.ph;
    e.srcW = ej.srcW;  e.srcH = ej.srcH;
    e.u0   = ej.u0;   e.v0  = ej.v0;
    e.u1   = ej.u1;   e.v1  = ej.v1;
    entries.push_back(e);
  }

  T8_LOG_INFO("[AtlasGen] Loaded metadata: %dx%d, %zu sprites",
              atlasW, atlasH, entries.size());
  return true;
}

} // namespace t800
