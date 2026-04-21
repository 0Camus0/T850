#pragma once
// ─── GUI Texture Atlas Generator ──────────────────────────
// Packs individual GUI control textures into a single atlas.
// Uses stb_image for loading source PNGs, stb_image_write for
// saving the atlas, and glaze for JSON metadata output.
//
// Usage (CLI):
//   DayScene.exe --createAtlas   # generate atlas from source textures
//   DayScene.exe --updateAtlas   # same (alias)
//
// Usage (code):
//   GUIAtlasGenerator gen;
//   gen.AddImage("SliderBar", "path/to/SliderBar.png");
//   ...
//   gen.Generate(4096, 256, 2);  // maxAtlas, maxSprite, padding
//   gen.Save("gui_atlas.png", "gui_atlas.json");
//
// Runtime loading (metadata only):
//   auto entries = GUIAtlasGenerator::LoadMetadata("gui_atlas.json");

#include <string>
#include <vector>
#include <cstdint>

namespace t800 {

  // UV region within the atlas (top-left origin, normalised to [0,1])
  struct AtlasRegion {
    float u0 = 0.0f, v0 = 0.0f;
    float u1 = 1.0f, v1 = 1.0f;
  };

  // Per-sprite metadata stored in the atlas JSON
  struct AtlasEntry {
    std::string name;          // control name (e.g. "SliderBar")
    int   px = 0, py = 0;     // pixel position in atlas (top-left)
    int   pw = 0, ph = 0;     // pixel size in atlas (after downscale)
    int   srcW = 0, srcH = 0; // original source dimensions
    float u0 = 0, v0 = 0;     // UV top-left
    float u1 = 1, v1 = 1;     // UV bottom-right
  };

  class GUIAtlasGenerator {
  public:
    // Add a source image to be packed.
    // name: logical control name (e.g. "SliderBar")
    // path: filesystem path to PNG file
    void AddImage(const std::string& name, const std::string& path);

    // Pack all added images into an atlas.
    // maxAtlasSize: max atlas dimension (must be power-of-2, e.g. 4096)
    // maxSpriteSize: sprites larger than this in any dimension get downscaled
    // padding: pixels of edge-extrusion padding around each sprite
    // Returns true on success.
    bool Generate(int maxAtlasSize = 4096, int maxSpriteSize = 256, int padding = 2);

    // Save the atlas image (PNG) and metadata (JSON).
    bool Save(const std::string& pngPath, const std::string& jsonPath) const;

    // ── Runtime: load metadata from JSON (no pixel data) ──
    static bool LoadMetadata(const std::string& jsonPath,
                             std::vector<AtlasEntry>& entries,
                             int& atlasW, int& atlasH);

    // Access result after Generate()
    const std::vector<AtlasEntry>& GetEntries() const { return m_entries; }
    const uint8_t* GetPixels() const { return m_atlas.data(); }
    int GetWidth()  const { return m_atlasW; }
    int GetHeight() const { return m_atlasH; }

  private:
    // Source image (loaded from disk)
    struct SourceImage {
      std::string name;
      std::string path;
      int w = 0, h = 0;           // original dimensions
      std::vector<uint8_t> pixels; // RGBA
    };

    // Downscale an RGBA image using bilinear filtering
    static std::vector<uint8_t> Downscale(const uint8_t* src,
                                           int srcW, int srcH,
                                           int dstW, int dstH);

    // Extrude edge pixels into padding area
    void ExtrudeEdges(int entryIdx, int padding);

    std::vector<SourceImage> m_sources;
    std::vector<AtlasEntry>  m_entries;
    std::vector<uint8_t>     m_atlas;   // RGBA pixels
    int m_atlasW = 0;
    int m_atlasH = 0;
  };

} // namespace t800
