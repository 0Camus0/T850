#include <pch.h>

#include <MinecraftScene.h>

#include <core/Config.h>
#include <core/EngineContext.h>
#include <debug/RuntimeTelemetry.h>
#include <imgui/DevGuiContext.h>
#include <physics/JoltPhysicsSystem.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

bool SweepPointAgainstExpandedBlock(const XVECTOR3& start,
                                    const XVECTOR3& displacement,
                                    const XVECTOR3& minimum,
                                    const XVECTOR3& maximum,
                                    float& fraction,
                                    XVECTOR3& normal) {
  float enter = 0.0f;
  float exit = 1.0f;
  XVECTOR3 enterNormal(0.0f, 0.0f, 0.0f, 0.0f);
  for (int axis = 0; axis < 3; ++axis) {
    const float origin = axis == 0 ? start.x : (axis == 1 ? start.y : start.z);
    const float delta = axis == 0 ? displacement.x : (axis == 1 ? displacement.y : displacement.z);
    const float low = axis == 0 ? minimum.x : (axis == 1 ? minimum.y : minimum.z);
    const float high = axis == 0 ? maximum.x : (axis == 1 ? maximum.y : maximum.z);
    if (std::abs(delta) <= 0.000001f) {
      if (origin < low || origin > high) return false;
      continue;
    }
    float nearTime = (low - origin) / delta;
    float farTime = (high - origin) / delta;
    float nearSign = -1.0f;
    if (nearTime > farTime) {
      std::swap(nearTime, farTime);
      nearSign = 1.0f;
    }
    if (nearTime > enter) {
      enter = nearTime;
      enterNormal = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      if (axis == 0) enterNormal.x = nearSign;
      else if (axis == 1) enterNormal.y = nearSign;
      else enterNormal.z = nearSign;
    }
    exit = (std::min)(exit, farTime);
    if (enter > exit) return false;
  }
  if (enter < 0.0f || enter > 1.0f) return false;
  fraction = enter;
  normal = enterNormal;
  return true;
}

// Deterministic value noise for terrain. Returns a value in [0,1].
float HashNoise(int x, int z) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h = h ^ (h >> 16);
  return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

// Bilinear value noise at integer lattice, smoothed.
float ValueNoise2D(float x, float z) {
  const int x0 = static_cast<int>(std::floor(x));
  const int z0 = static_cast<int>(std::floor(z));
  const float fx = x - static_cast<float>(x0);
  const float fz = z - static_cast<float>(z0);
  const float sx = SmoothStep(fx);
  const float sz = SmoothStep(fz);
  const float n00 = HashNoise(x0, z0);
  const float n10 = HashNoise(x0 + 1, z0);
  const float n01 = HashNoise(x0, z0 + 1);
  const float n11 = HashNoise(x0 + 1, z0 + 1);
  const float nx0 = n00 + (n10 - n00) * sx;
  const float nx1 = n01 + (n11 - n01) * sx;
  return nx0 + (nx1 - nx0) * sz;
}

// Fractal (fBm) noise for rolling hills.
float FractalNoise(float x, float z) {
  float sum = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float total = 0.0f;
  for (int octave = 0; octave < 4; ++octave) {
    sum += amplitude * ValueNoise2D(x * frequency, z * frequency);
    total += amplitude;
    amplitude *= 0.5f;
    frequency *= 2.0f;
  }
  return sum / total;
}

// Atlas tile helpers. The atlas is a grid of 16x16 pixel tiles.
constexpr int kTileSize = 16;
constexpr int kAtlasCols = 8;
constexpr int kAtlasRows = 8;
constexpr int kAtlasWidth = kTileSize * kAtlasCols;   // 128
constexpr int kAtlasHeight = kTileSize * kAtlasRows;  // 128

struct Rgba { unsigned char r, g, b, a; };

// Build a unit box (1x1x1, centered at origin) mesh snapshot with a solid
// base color. Used for the first-person sword and the enemy bodies.
void BuildBoxMesh(t850::MutableMeshSnapshot& snapshot,
                  const XVECTOR3& color,
                  float metallic = 0.0f,
                  float roughness = 0.6f) {
  snapshot = t850::MutableMeshSnapshot{};
  snapshot.version = 1;
  const float h = 0.5f;
  // 6 faces, 4 verts each, 2 triangles per face.
  const XVECTOR3 positions[6][4] = {
      // +X
      {{h, -h, -h, 1.0f}, {h, -h, h, 1.0f}, {h, h, h, 1.0f}, {h, h, -h, 1.0f}},
      // -X
      {{-h, -h, h, 1.0f}, {-h, -h, -h, 1.0f}, {-h, h, -h, 1.0f}, {-h, h, h, 1.0f}},
      // +Y
      {{-h, h, -h, 1.0f}, {h, h, -h, 1.0f}, {h, h, h, 1.0f}, {-h, h, h, 1.0f}},
      // -Y
      {{-h, -h, h, 1.0f}, {h, -h, h, 1.0f}, {h, -h, -h, 1.0f}, {-h, -h, -h, 1.0f}},
      // +Z
      {{-h, -h, h, 1.0f}, {h, -h, h, 1.0f}, {h, h, h, 1.0f}, {-h, h, h, 1.0f}},
      // -Z
      {{h, -h, -h, 1.0f}, {-h, -h, -h, 1.0f}, {-h, h, -h, 1.0f}, {h, h, -h, 1.0f}},
  };
  const XVECTOR3 normals[6] = {
      {1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f},
  };
  for (int face = 0; face < 6; ++face) {
    const uint32_t base = static_cast<uint32_t>(snapshot.vertices.size());
    for (int v = 0; v < 4; ++v) {
      t850::MutableMeshVertex vertex;
      vertex.position = positions[face][v];
      vertex.normal = normals[face];
      vertex.u = (v == 1 || v == 2) ? 1.0f : 0.0f;
      vertex.v = (v == 2 || v == 3) ? 1.0f : 0.0f;
      snapshot.vertices.push_back(vertex);
    }
    snapshot.indices.insert(snapshot.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
  }
  t850::MutableMeshMaterial material;
  material.baseColor = color;
  material.metallic = metallic;
  material.roughness = roughness;
  material.alphaMode = t850::MutableMeshAlphaMode::Opaque;
  material.usesBaseColorTexture = false;
  snapshot.materials.push_back(material);
  snapshot.sections.push_back(t850::MutableMeshSection{
      .firstIndex = 0,
      .indexCount = static_cast<uint32_t>(snapshot.indices.size()),
      .materialIndex = 0});
  t850::RecalculateMutableMeshBounds(snapshot);
}

void SetPixel(std::vector<unsigned char>& pixels, int x, int y, Rgba color) {
  if (x < 0 || x >= kAtlasWidth || y < 0 || y >= kAtlasHeight) return;
  const int index = (y * kAtlasWidth + x) * 4;
  pixels[static_cast<std::size_t>(index) + 0] = color.r;
  pixels[static_cast<std::size_t>(index) + 1] = color.g;
  pixels[static_cast<std::size_t>(index) + 2] = color.b;
  pixels[static_cast<std::size_t>(index) + 3] = color.a;
}

// Fill a tile (tileX, tileY) with a solid color plus per-pixel noise variation.
void FillTile(std::vector<unsigned char>& pixels, int tileX, int tileY,
              Rgba base, float variation, int seed) {
  for (int y = 0; y < kTileSize; ++y) {
    for (int x = 0; x < kTileSize; ++x) {
      const float n = HashNoise(tileX * 100 + x + seed, tileY * 100 + y + seed);
      const float scale = 1.0f + (n - 0.5f) * 2.0f * variation;
      Rgba c;
      c.r = static_cast<unsigned char>(std::clamp(static_cast<int>(base.r * scale), 0, 255));
      c.g = static_cast<unsigned char>(std::clamp(static_cast<int>(base.g * scale), 0, 255));
      c.b = static_cast<unsigned char>(std::clamp(static_cast<int>(base.b * scale), 0, 255));
      c.a = base.a;
      SetPixel(pixels, tileX * kTileSize + x, tileY * kTileSize + y, c);
    }
  }

}

// Grass side: dirt base with a green top strip.
void FillGrassSide(std::vector<unsigned char>& pixels, int tileX, int tileY) {
  FillTile(pixels, tileX, tileY, Rgba{116, 76, 42, 255}, 0.12f, 7);
  for (int x = 0; x < kTileSize; ++x) {
    for (int y = 0; y < 4; ++y) {
      const float n = HashNoise(tileX * 100 + x, tileY * 100 + y + 3);
      const float scale = 1.0f + (n - 0.5f) * 0.3f;
      Rgba c;
      c.r = static_cast<unsigned char>(std::clamp(static_cast<int>(96 * scale), 0, 255));
      c.g = static_cast<unsigned char>(std::clamp(static_cast<int>(160 * scale), 0, 255));
      c.b = static_cast<unsigned char>(std::clamp(static_cast<int>(72 * scale), 0, 255));
      c.a = 255;
      SetPixel(pixels, tileX * kTileSize + x, tileY * kTileSize + y, c);
    }
  }
}

// Log side: brown bark with vertical darker lines.
void FillLogSide(std::vector<unsigned char>& pixels, int tileX, int tileY) {
  FillTile(pixels, tileX, tileY, Rgba{110, 84, 50, 255}, 0.1f, 11);
  for (int x = 0; x < kTileSize; x += 4) {
    for (int y = 0; y < kTileSize; ++y) {
      SetPixel(pixels, tileX * kTileSize + x, tileY * kTileSize + y, Rgba{80, 58, 34, 255});
      SetPixel(pixels, tileX * kTileSize + x + 1, tileY * kTileSize + y, Rgba{80, 58, 34, 255});
    }
  }
}

// Log top/bottom: concentric wood rings.
void FillLogEnd(std::vector<unsigned char>& pixels, int tileX, int tileY) {
  FillTile(pixels, tileX, tileY, Rgba{150, 116, 70, 255}, 0.05f, 13);
  const int cx = tileX * kTileSize + kTileSize / 2;
  const int cy = tileY * kTileSize + kTileSize / 2;
  for (int y = 0; y < kTileSize; ++y) {
    for (int x = 0; x < kTileSize; ++x) {
      const int dx = x - kTileSize / 2;
      const int dy = y - kTileSize / 2;
      const int dist = static_cast<int>(std::sqrt(static_cast<float>(dx * dx + dy * dy)));
      if (dist >= 6 && dist <= 7) {
        SetPixel(pixels, cx - kTileSize / 2 + x, cy - kTileSize / 2 + y, Rgba{96, 70, 40, 255});
      }
    }
  }
}

// Leaves: green with scattered transparent holes (mask).
void FillLeaves(std::vector<unsigned char>& pixels, int tileX, int tileY) {
  for (int y = 0; y < kTileSize; ++y) {
    for (int x = 0; x < kTileSize; ++x) {
      const float n = HashNoise(tileX * 100 + x + 5, tileY * 100 + y + 5);
      const float scale = 1.0f + (n - 0.5f) * 0.4f;
      Rgba c;
      c.r = static_cast<unsigned char>(std::clamp(static_cast<int>(60 * scale), 0, 255));
      c.g = static_cast<unsigned char>(std::clamp(static_cast<int>(140 * scale), 0, 255));
      c.b = static_cast<unsigned char>(std::clamp(static_cast<int>(50 * scale), 0, 255));
      c.a = (n > 0.82f) ? 0 : 255;
      SetPixel(pixels, tileX * kTileSize + x, tileY * kTileSize + y, c);
    }
  }
}

// Cobblestone: gray with darker mortar lines.
void FillCobble(std::vector<unsigned char>& pixels, int tileX, int tileY) {
  FillTile(pixels, tileX, tileY, Rgba{110, 110, 110, 255}, 0.15f, 17);
  for (int y = 0; y < kTileSize; y += 8) {
    for (int x = 0; x < kTileSize; ++x) {
      SetPixel(pixels, tileX * kTileSize + x, tileY * kTileSize + y, Rgba{70, 70, 70, 255});
    }
  }
  for (int x = 0; x < kTileSize; x += 8) {
    for (int y = 0; y < kTileSize; ++y) {
      SetPixel(pixels, tileX * kTileSize + x, tileY * kTileSize + y, Rgba{70, 70, 70, 255});
    }
  }
}

// Planks: light brown horizontal boards.
void FillPlanks(std::vector<unsigned char>& pixels, int tileX, int tileY) {
  FillTile(pixels, tileX, tileY, Rgba{160, 130, 80, 255}, 0.08f, 19);
  for (int y = 0; y < kTileSize; y += 4) {
    for (int x = 0; x < kTileSize; ++x) {
      SetPixel(pixels, tileX * kTileSize + x, tileY * kTileSize + y, Rgba{120, 95, 55, 255});
    }
  }
}

} // namespace

MinecraftScene::MinecraftScene()
    : m_world(t850::terrain::ChunkDimensions{16, 16, 16}),
      m_streaming(t850::terrain::ChunkDimensions{16, 16, 16}) {}

void MinecraftScene::RegisterBlocks() {
  m_blockRegistry = t850::terrain::BlockRegistry{};

  // Grass: per-face textures (top green, side grass, bottom dirt).
  t850::terrain::BlockDefinition grass;
  grass.name = "grass";
  grass.color = XVECTOR3(0.45f, 0.8f, 0.35f, 1.0f);
  grass.roughness = 1.0f;
  grass.usesBaseColorTexture = true;
  grass.hasPerFaceTextures = true;
  // Tile 0 = grass top, tile 1 = grass side, tile 2 = dirt.
  grass.topU0 = 0.0f; grass.topV0 = 0.0f; grass.topU1 = 1.0f / 8.0f; grass.topV1 = 1.0f / 8.0f;
  grass.sideU0 = 1.0f / 8.0f; grass.sideV0 = 0.0f; grass.sideU1 = 2.0f / 8.0f; grass.sideV1 = 1.0f / 8.0f;
  grass.bottomU0 = 2.0f / 8.0f; grass.bottomV0 = 0.0f; grass.bottomU1 = 3.0f / 8.0f; grass.bottomV1 = 1.0f / 8.0f;
  m_grass = m_blockRegistry.Register(std::move(grass));

  t850::terrain::BlockDefinition dirt;
  dirt.name = "dirt";
  dirt.color = XVECTOR3(0.55f, 0.38f, 0.2f, 1.0f);
  dirt.roughness = 1.0f;
  dirt.usesBaseColorTexture = true;
  dirt.atlasU0 = 2.0f / 8.0f; dirt.atlasV0 = 0.0f;
  dirt.atlasU1 = 3.0f / 8.0f; dirt.atlasV1 = 1.0f / 8.0f;
  m_dirt = m_blockRegistry.Register(std::move(dirt));

  t850::terrain::BlockDefinition stone;
  stone.name = "stone";
  stone.color = XVECTOR3(0.6f, 0.6f, 0.6f, 1.0f);
  stone.roughness = 0.9f;
  stone.usesBaseColorTexture = true;
  stone.atlasU0 = 3.0f / 8.0f; stone.atlasV0 = 0.0f;
  stone.atlasU1 = 4.0f / 8.0f; stone.atlasV1 = 1.0f / 8.0f;
  m_stone = m_blockRegistry.Register(std::move(stone));

  t850::terrain::BlockDefinition cobble;
  cobble.name = "cobblestone";
  cobble.color = XVECTOR3(0.45f, 0.45f, 0.45f, 1.0f);
  cobble.roughness = 1.0f;
  cobble.usesBaseColorTexture = true;
  cobble.atlasU0 = 4.0f / 8.0f; cobble.atlasV0 = 0.0f;
  cobble.atlasU1 = 5.0f / 8.0f; cobble.atlasV1 = 1.0f / 8.0f;
  m_cobble = m_blockRegistry.Register(std::move(cobble));

  t850::terrain::BlockDefinition sand;
  sand.name = "sand";
  sand.color = XVECTOR3(0.95f, 0.9f, 0.65f, 1.0f);
  sand.roughness = 1.0f;
  sand.usesBaseColorTexture = true;
  sand.atlasU0 = 5.0f / 8.0f; sand.atlasV0 = 0.0f;
  sand.atlasU1 = 6.0f / 8.0f; sand.atlasV1 = 1.0f / 8.0f;
  m_sand = m_blockRegistry.Register(std::move(sand));

  t850::terrain::BlockDefinition water;
  water.name = "water";
  water.color = XVECTOR3(0.2f, 0.4f, 0.8f, 0.6f);
  water.alphaMode = t850::MutableMeshAlphaMode::Blend;
  water.roughness = 0.1f;
  water.usesBaseColorTexture = true;
  water.atlasU0 = 6.0f / 8.0f; water.atlasV0 = 0.0f;
  water.atlasU1 = 7.0f / 8.0f; water.atlasV1 = 1.0f / 8.0f;
  water.occludes = false;
  water.collidable = false;
  m_water = m_blockRegistry.Register(std::move(water));

  t850::terrain::BlockDefinition bedrock;
  bedrock.name = "bedrock";
  bedrock.color = XVECTOR3(0.2f, 0.2f, 0.2f, 1.0f);
  bedrock.roughness = 1.0f;
  bedrock.usesBaseColorTexture = true;
  bedrock.atlasU0 = 7.0f / 8.0f; bedrock.atlasV0 = 0.0f;
  bedrock.atlasU1 = 8.0f / 8.0f; bedrock.atlasV1 = 1.0f / 8.0f;
  m_bedrock = m_blockRegistry.Register(std::move(bedrock));

  // Log: per-face textures (top/bottom rings, side bark).
  t850::terrain::BlockDefinition log;
  log.name = "log";
  log.color = XVECTOR3(0.5f, 0.4f, 0.25f, 1.0f);
  log.roughness = 0.9f;
  log.usesBaseColorTexture = true;
  log.hasPerFaceTextures = true;
  // Row 1: tile 0 = log top, tile 1 = log side.
  log.topU0 = 0.0f; log.topV0 = 1.0f / 8.0f; log.topU1 = 1.0f / 8.0f; log.topV1 = 2.0f / 8.0f;
  log.bottomU0 = 0.0f; log.bottomV0 = 1.0f / 8.0f; log.bottomU1 = 1.0f / 8.0f; log.bottomV1 = 2.0f / 8.0f;
  log.sideU0 = 1.0f / 8.0f; log.sideV0 = 1.0f / 8.0f; log.sideU1 = 2.0f / 8.0f; log.sideV1 = 2.0f / 8.0f;
  m_log = m_blockRegistry.Register(std::move(log));

  t850::terrain::BlockDefinition leaves;
  leaves.name = "leaves";
  leaves.color = XVECTOR3(0.25f, 0.55f, 0.2f, 1.0f);
  leaves.alphaMode = t850::MutableMeshAlphaMode::Mask;
  leaves.alphaCutoff = 0.4f;
  leaves.roughness = 0.9f;
  leaves.usesBaseColorTexture = true;
  leaves.atlasU0 = 2.0f / 8.0f; leaves.atlasV0 = 1.0f / 8.0f;
  leaves.atlasU1 = 3.0f / 8.0f; leaves.atlasV1 = 2.0f / 8.0f;
  leaves.occludes = false;
  m_leaves = m_blockRegistry.Register(std::move(leaves));

  t850::terrain::BlockDefinition planks;
  planks.name = "planks";
  planks.color = XVECTOR3(0.65f, 0.5f, 0.3f, 1.0f);
  planks.roughness = 0.9f;
  planks.usesBaseColorTexture = true;
  planks.atlasU0 = 3.0f / 8.0f; planks.atlasV0 = 1.0f / 8.0f;
  planks.atlasU1 = 4.0f / 8.0f; planks.atlasV1 = 2.0f / 8.0f;
  m_planks = m_blockRegistry.Register(std::move(planks));

  m_hotbar = {m_grass, m_dirt, m_stone, m_cobble, m_sand, m_planks, m_log, m_leaves, m_water};
  m_selectedHotbar = 0;
}

void MinecraftScene::BuildAtlas() {
  std::vector<unsigned char> pixels(static_cast<std::size_t>(kAtlasWidth * kAtlasHeight * 4), 0);

  // Row 0: grass_top, grass_side, dirt, stone, cobble, sand, water, bedrock.
  FillTile(pixels, 0, 0, Rgba{110, 190, 80, 255}, 0.15f, 1);
  FillGrassSide(pixels, 1, 0);
  FillTile(pixels, 2, 0, Rgba{140, 96, 52, 255}, 0.12f, 2);
  FillTile(pixels, 3, 0, Rgba{150, 150, 150, 255}, 0.1f, 3);
  FillCobble(pixels, 4, 0);
  FillTile(pixels, 5, 0, Rgba{220, 210, 150, 255}, 0.08f, 4);
  FillTile(pixels, 6, 0, Rgba{40, 90, 200, 255}, 0.05f, 5);
  FillTile(pixels, 7, 0, Rgba{60, 60, 60, 255}, 0.2f, 6);

  // Row 1: log_top, log_side, leaves, planks, (rest unused).
  FillLogEnd(pixels, 0, 1);
  FillLogSide(pixels, 1, 1);
  FillLeaves(pixels, 2, 1);
  FillPlanks(pixels, 3, 1);

  if (pEngineContext && pEngineContext->device) {
    m_blockAtlas = pEngineContext->device->CreateTextureFromMemory(
        pixels.data(), kAtlasWidth, kAtlasHeight, 4, "minecraft_block_atlas");
    if (m_blockAtlas) {
      m_blockAtlas->params = t850::TextBasicParams::CLAMP_TO_EDGE |
          t850::TextBasicParams::NEAREST_FILTER;
      m_blockAtlas->SetTextureParams();
    }
  }
}

void MinecraftScene::BuildSkyCubemap() {
  if (!pEngineContext || !pEngineContext->device) return;
  // Generate a bright blue sky cubemap (6 faces of 64x64 RGBA) with a lighter
  // horizon, matching the Minecraft sky look. Each face is a vertical gradient
  // from a deep blue at the top to a pale blue/white at the horizon.
  constexpr int kSkySize = 64;
  std::vector<unsigned char> pixels(static_cast<std::size_t>(kSkySize * kSkySize * 6 * 4), 0);
  for (int face = 0; face < 6; ++face) {
    for (int y = 0; y < kSkySize; ++y) {
      const float t = static_cast<float>(y) / static_cast<float>(kSkySize - 1);
      // Top: bright Minecraft blue (80, 160, 255). Horizon: pale blue/white (200, 230, 255).
      const int r = static_cast<int>(80 + (200 - 80) * t);
      const int g = static_cast<int>(160 + (230 - 160) * t);
      const int b = static_cast<int>(255 + (255 - 255) * t);
      for (int x = 0; x < kSkySize; ++x) {
        const std::size_t offset =
            (static_cast<std::size_t>(face) * kSkySize * kSkySize +
             static_cast<std::size_t>(y) * kSkySize + static_cast<std::size_t>(x)) * 4;
        pixels[offset + 0] = static_cast<unsigned char>(r);
        pixels[offset + 1] = static_cast<unsigned char>(g);
        pixels[offset + 2] = static_cast<unsigned char>(b);
        pixels[offset + 3] = 255;
      }
    }
  }
  if (pFramework && pFramework->pVideoDriver) {
    m_envMapTexIndex = pFramework->pVideoDriver->CreateCubeMap(pixels.data(), kSkySize, kSkySize);
  }
}

int MinecraftScene::TerrainHeight(int worldX, int worldZ) const {
  // Rolling hills between ~8 and ~40 blocks, plus a base floor.
  const float hills = FractalNoise(static_cast<float>(worldX) * 0.02f,
                                   static_cast<float>(worldZ) * 0.02f);
  const float detail = ValueNoise2D(static_cast<float>(worldX) * 0.1f,
                                    static_cast<float>(worldZ) * 0.1f);
  const int base = 12;
  int height = base + static_cast<int>(hills * 24.0f) + static_cast<int>(detail * 3.0f);
  // Create lakes: in some areas, carve the terrain down to the water level so
  // water fills the depression (a Minecraft lake).
  const float lakeNoise = ValueNoise2D(static_cast<float>(worldX) * 0.01f,
                                       static_cast<float>(worldZ) * 0.01f);
  if (lakeNoise > 0.55f) {
    height = static_cast<int>(height * 0.4f);
  }
  return std::clamp(height, 4, 48);
}

void MinecraftScene::PlaceTree(int worldX, int worldY, int worldZ,
                               t850::terrain::VoxelChunk& chunk) const {
  const int trunkHeight = 4 + static_cast<int>(HashNoise(worldX, worldZ) * 2.0f);
  // Trunk.
  for (int i = 1; i <= trunkHeight; ++i) {
    const int y = worldY + i;
    if (y >= 0 && y < 256) chunk.Set(worldX, y, worldZ, m_log);
  }
  // Leaves canopy.
  const int topY = worldY + trunkHeight;
  for (int dy = -2; dy <= 1; ++dy) {
    const int y = topY + dy;
    if (y < 0 || y >= 256) continue;
    const int radius = (dy >= 0) ? 1 : 2;
    for (int dx = -radius; dx <= radius; ++dx) {
      for (int dz = -radius; dz <= radius; ++dz) {
        if (dx == 0 && dz == 0 && dy >= 0) continue;  // keep trunk top clear
        if (std::abs(dx) == radius && std::abs(dz) == radius && (dx * dz) > 0) continue;
        const int x = worldX + dx;
        const int z = worldZ + dz;
        if (chunk.Get(x, y, z) == t850::terrain::kAirBlock) {
          chunk.Set(x, y, z, m_leaves);
        }
      }
    }
  }
  // Top leaf.
  if (topY + 1 < 256) chunk.Set(worldX, topY + 1, worldZ, m_leaves);
}

t850::terrain::VoxelChunkBuildResult MinecraftScene::BuildStreamedChunk(
    const t850::terrain::VoxelChunkBuildRequest& request) const {
  t850::terrain::VoxelChunkBuildResult result;
  result.key = request.key;
  result.epoch = request.epoch;
  result.chunk = std::make_unique<t850::terrain::VoxelChunk>(request.key, request.dimensions);
  const int dimX = request.dimensions.x;
  const int dimY = request.dimensions.y;
  const int dimZ = request.dimensions.z;
  constexpr int kWaterLevel = 16;

  for (int z = 0; z < dimZ; ++z) {
    if (request.IsCancelled()) {
      result.cancelled = true;
      result.chunk.reset();
      return result;
    }
    for (int x = 0; x < dimX; ++x) {
      const int worldX = request.key.x * dimX + x;
      const int worldZ = request.key.z * dimZ + z;
      const int height = TerrainHeight(worldX, worldZ);
      const bool beach = height <= kWaterLevel + 1;
      for (int y = 0; y < dimY; ++y) {
        const int worldY = request.key.y * dimY + y;
        if (worldY > height) {
          // Water fills the space between terrain and the water level.
          if (worldY <= kWaterLevel && height < kWaterLevel) {
            result.chunk->Set(x, y, z, m_water);
          }
          continue;
        }
        t850::terrain::BlockId block;
        if (worldY == 0) {
          block = m_bedrock;
        } else if (worldY == height) {
          block = beach ? m_sand : m_grass;
        } else if (worldY >= height - 3) {
          block = beach ? m_sand : m_dirt;
        } else {
          block = m_stone;
        }
        result.chunk->Set(x, y, z, block);
      }
      // Place a tree on grass with a higher density for a Minecraft look.
      if (!beach && height > kWaterLevel + 2 &&
          HashNoise(worldX * 31 + 17, worldZ * 31 + 23) > 0.90f) {
        PlaceTree(worldX, height, worldZ, *result.chunk);
      }
    }
  }
  m_deltas.ApplyToChunk(*result.chunk);
  if (!t850::terrain::BuildGreedyVoxelMesh(
          *result.chunk, m_blockRegistry, {}, result.mesh, &result.error)) {
    result.chunk.reset();
  }
  return result;
}

void MinecraftScene::InitVars() {
  m_deltaSeconds = 0.0f;
  m_remeshRequested = false;
  m_assetsCreated = false;
  m_chunkRenders.clear();
  m_world.Clear();
  m_streaming.Reset();
  m_deltas.Clear();
  RegisterBlocks();

  m_deltaPath = t850::ResourceLocator::Instance()
      .ResolveCachePath("VoxelWorlds/minecraft/edits.t8vox")
      .string();
  if (t850::g_config.regressionFixedDt <= 0.0f && std::filesystem::exists(m_deltaPath)) {
    std::string error;
    if (!m_deltas.Load(m_deltaPath, &error)) {
      T8_LOG_ERROR("[MinecraftScene] Ignoring invalid saved edits: %s", error.c_str());
      m_deltas.Clear();
    }
  }

  // Position the camera to view the terrain from a distance with the sky in
  // the upper part of the frame, like a Minecraft screenshot.
  const float spawnX = 16.0f;
  const float spawnZ = -40.0f;
  const float spawnY = static_cast<float>(TerrainHeight(static_cast<int>(spawnX), static_cast<int>(spawnZ))) + 8.0f;
  m_camera.InitPerspective(XVECTOR3(spawnX, spawnY, spawnZ), Deg2Rad(70.0f), 1280.0f / 720.0f, 0.05f, 1000.0f);
  m_camera.Eye = XVECTOR3(spawnX, spawnY, spawnZ, 1.0f);
  m_camera.Yaw = 0.0f;
  m_camera.Pitch = -0.1f;
  m_camera.Update(0.0f);
  m_cameraController.SetActiveProfile(t850::CameraProfileType::FreeFly);
  m_cameraController.AttachCamera(&m_camera);

  m_lightCamera.InitPerspective(XVECTOR3(0.0f, 100.0f, 10.0f), Deg2Rad(45.0f), 1.0f, 10.0f, 500.0f);
  m_lightCamera.Eye = XVECTOR3(50.0f, 150.0f, -50.0f, 1.0f);
  m_lightCamera.Pitch = 1.0f;
  m_lightCamera.Yaw = -1.57f;
  m_lightCamera.Update(0.0f);

  SceneProp = SceneProps{};
  SceneProp.AddCamera(&m_camera);
  SceneProp.AddLightCamera(&m_lightCamera);
  SceneProp.AddDirectionalLight(m_lightCamera.Look, XVECTOR3(1.0f, 0.98f, 0.92f, 1.0f), 20.0f, true);
  SceneProp.ActiveLights = 1;
  SceneProp.AmbientColor = XVECTOR3(0.6f, 0.65f, 0.7f, 1.0f);
  SceneProp.Exposure = 3.0f;
  SceneProp.ToogleDOF = 0;
  SceneProp.ToogleParallax = 0;
  SceneProp.IBLFactor = 0.8f;
  SceneProp.ShadowMin = 0.85f;
  SceneProp.FrustumCullingEnabled = true;
  if (!SceneProp.Lights.empty()) {
    SceneProp.Lights[0].Position = m_lightCamera.Eye;
    SceneProp.Lights[0].Direction = m_lightCamera.Look;
    T8_LOG_DEBUG("[MinecraftScene] Light dir=(%.3f,%.3f,%.3f) eye=(%.1f,%.1f,%.1f)",
                 SceneProp.Lights[0].Direction.x, SceneProp.Lights[0].Direction.y,
                 SceneProp.Lights[0].Direction.z,
                 m_lightCamera.Eye.x, m_lightCamera.Eye.y, m_lightCamera.Eye.z);
  }

  m_shadowFilter.kernelSize = 4;
  m_shadowFilter.radius = 1.0f;
  m_shadowFilter.sigma = 1.0f;
  m_shadowFilter.Update();
  m_bloomFilter.kernelSize = 11;
  m_bloomFilter.radius = 2.5f;
  m_bloomFilter.sigma = 4.5f;
  m_bloomFilter.Update();
  m_dofFilter.kernelSize = 23;
  m_dofFilter.radius = 3.0f;
  m_dofFilter.sigma = 6.0f;
  m_dofFilter.Update();
  SceneProp.AddGaussKernel(&m_shadowFilter);
  SceneProp.AddGaussKernel(&m_bloomFilter);
  SceneProp.AddGaussKernel(&m_dofFilter);

  t850::terrain::VoxelStreamingSettings streamingSettings;
  streamingSettings.horizontalRadius = 3;
  streamingSettings.verticalRadius = 1;
  streamingSettings.maxInFlight = 8;
  streamingSettings.maxLaunchesPerUpdate = 6;
  streamingSettings.maxCommitsPerUpdate = 6;
  streamingSettings.maxUnloadsPerUpdate = 6;
  m_streaming.SetSettings(streamingSettings);

  t850::FrameDumperConfig dumpConfig;
  dumpConfig.dumpEnabled = t850::g_config.flags.dumpEnabled;
  dumpConfig.dumpByFrame = t850::g_config.flags.dumpByFrame;
  dumpConfig.dumpFrame = t850::g_config.dumpFrame;
  dumpConfig.dumpSeconds = t850::g_config.dumpSeconds;
  dumpConfig.debugFrames = t850::g_config.flags.debugFrames;
  dumpConfig.keepRunning = t850::g_config.flags.keepRunning;
  dumpConfig.replaySnapshotPath = t850::g_config.replaySnapshotPath;
  dumpConfig.sceneIndex = kSceneIndex;
  m_dumper.Init(dumpConfig);
}

void MinecraftScene::CreateAssets() {
  if (m_assetsCreated || !pFramework || !pFramework->pVideoDriver) return;
  SceneProp.SSAOKernel.InitTexture();
  t850::RenderContainerDesc descriptor;
  descriptor.name = "MinecraftScene";
  descriptor.renderGraphPath = "Scenes/SceneTemplate_RenderGraph.json";
  descriptor.width = pFramework->pVideoDriver->width;
  descriptor.height = pFramework->pVideoDriver->height;
  descriptor.sceneProps = &SceneProp;
  if (!m_renderContainer.Initialize(pFramework->pVideoDriver, pEngineContext, descriptor)) {
    T8_LOG_ERROR("[MinecraftScene] Failed to initialize render container");
    return;
  }
  m_renderContainer.SetMainCamera(&m_camera);
  m_renderContainer.SetLightCamera(&m_lightCamera);
  m_renderContainer.Graph().DisablePass("Light Add");

  // Generate a bright blue sky cubemap and set up IBL resources so cleared
  // GBuffer pixels (sky) sample the environment map in the deferred pass.
  BuildSkyCubemap();
  if (m_envMapTexIndex >= 0) {
    m_envMaps.SetFallback(m_envMapTexIndex);
    t850::LoadEnvironmentIBLResources(
        pFramework->pVideoDriver,
        {},
        m_envMaps,
        m_diffuseIBLTexIndex,
        m_specularIBLTexIndex,
        m_brdfLUTTexIndex,
        m_sheenIBLTexIndex,
        m_charlieLUTTexIndex,
        m_sheenELUTTexIndex);
    t850::UpdateSceneIBLSettings(SceneProp, pFramework->pVideoDriver, m_envMaps);
    m_renderContainer.SetEnvironmentMaps(m_envMaps);
  } else {
    T8_LOG_ERROR("[MinecraftScene] Failed to create sky cubemap");
  }

  BuildAtlas();
  m_assetsCreated = true;
  CreateSword();
  CreateEnemies();
  UpdateStreaming();
}

void MinecraftScene::DestroyAssets() {
  if (!m_assetsCreated) return;
  DestroyDynamicMeshes();
  m_streaming.Reset();
  if (t850::g_config.regressionFixedDt <= 0.0f && !m_deltaPath.empty() && m_deltas.Count() > 0) {
    std::string error;
    if (!m_deltas.Save(m_deltaPath, &error)) {
      T8_LOG_ERROR("[MinecraftScene] Failed to save edits during teardown: %s", error.c_str());
    }
  }
  m_renderContainer.ClearMeshes();
  m_renderContainer.Destroy(pFramework ? pFramework->pVideoDriver : nullptr);
  for (auto& [key, render] : m_chunkRenders) {
    (void)key;
    if (render.body.IsValid() && pEngineContext && pEngineContext->physics) {
      pEngineContext->physics->DestroyBody(render.body);
    }
    if (render.mesh) render.mesh->Destroy();
  }
  m_chunkRenders.clear();
  if (m_envMapTexIndex >= 0 && pFramework && pFramework->pVideoDriver) {
    pFramework->pVideoDriver->WaitForGPU();
    pFramework->pVideoDriver->DestroyTexture(m_envMapTexIndex);
    m_envMapTexIndex = -1;
  }
  if (m_blockAtlas) {
    if (pFramework && pFramework->pVideoDriver) pFramework->pVideoDriver->WaitForGPU();
    m_blockAtlas->release();
    m_blockAtlas = nullptr;
  }
  SceneProp.SSAOKernel.Destroy();
  m_assetsCreated = false;
}

void MinecraftScene::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void MinecraftScene::OnDestoryScene() {
  DestroyAssets();
}

t850::terrain::BlockId MinecraftScene::SampleNeighbor(
    const t850::terrain::ChunkKey& key, int localX, int localY, int localZ) const {
  const auto dimensions = m_world.Dimensions();
  return m_world.GetBlock(
      key.x * dimensions.x + localX,
      key.y * dimensions.y + localY,
      key.z * dimensions.z + localZ);
}

void MinecraftScene::CommitStreamedChunk(t850::terrain::VoxelChunkBuildResult result) {
  if (!result.Succeeded() || !m_streaming.IsDesired(result.key)) return;
  const t850::terrain::ChunkKey key = result.key;
  const t850::PhysicsBodyHandle replacementBody = CreateChunkPhysics(key, result.mesh);

  auto found = m_chunkRenders.find(key);
  if (found != m_chunkRenders.end()) {
    std::string error;
    if (!found->second.mesh->ReplaceSnapshot(std::move(result.mesh), &error)) {
      T8_LOG_ERROR("[MinecraftScene] Streamed chunk replacement failed: %s", error.c_str());
      if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
        pEngineContext->physics->DestroyBody(replacementBody);
      }
      return;
    }
    if (!m_world.AdoptChunk(std::move(result.chunk))) {
      if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
        pEngineContext->physics->DestroyBody(replacementBody);
      }
      return;
    }
    if (pEngineContext && pEngineContext->physics) {
      if (found->second.body.IsValid()) pEngineContext->physics->DestroyBody(found->second.body);
      found->second.body = replacementBody;
    }
    return;
  }

  ChunkRender render;
  render.mesh = std::make_unique<t850::MutableMesh>();
  render.mesh->SetEngineContext(pEngineContext);
  render.mesh->Create();
  std::string error;
  if (!render.mesh->ReplaceSnapshot(std::move(result.mesh), &error)) {
    T8_LOG_ERROR("[MinecraftScene] Streamed chunk GPU commit failed: %s", error.c_str());
    if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
      pEngineContext->physics->DestroyBody(replacementBody);
    }
    return;
  }
  t850::PrimitiveInst instance;
  instance.CreateInstance(render.mesh.get(), &m_camera.VP);
  const auto dimensions = m_world.Dimensions();
  instance.TranslateAbsolute(
      static_cast<float>(key.x * dimensions.x),
      static_cast<float>(key.y * dimensions.y),
      static_cast<float>(key.z * dimensions.z));
  instance.SetTexture(m_blockAtlas, 0);
  instance.Update();
  render.instance = m_renderContainer.AddMeshInstance(instance);
  render.body = replacementBody;
  if (!render.instance.IsValid() || !m_world.AdoptChunk(std::move(result.chunk))) {
    if (render.instance.IsValid()) m_renderContainer.RemoveMesh(render.instance);
    if (render.body.IsValid() && pEngineContext && pEngineContext->physics) {
      pEngineContext->physics->DestroyBody(render.body);
    }
    render.mesh->Destroy();
    return;
  }
  m_chunkRenders.emplace(key, std::move(render));
}

void MinecraftScene::UnloadChunk(t850::terrain::ChunkKey key) {
  const auto found = m_chunkRenders.find(key);
  if (found != m_chunkRenders.end()) {
    m_renderContainer.RemoveMesh(found->second.instance);
    if (found->second.body.IsValid() && pEngineContext && pEngineContext->physics) {
      pEngineContext->physics->DestroyBody(found->second.body);
    }
    if (found->second.mesh) found->second.mesh->Destroy();
    m_chunkRenders.erase(found);
  }
  m_world.RemoveChunk(key);
}

t850::PhysicsBodyHandle MinecraftScene::CreateChunkPhysics(
    t850::terrain::ChunkKey key, const t850::MutableMeshSnapshot& snapshot) const {
  if (snapshot.Empty() || !pEngineContext || !pEngineContext->physics ||
      !pEngineContext->physics->IsInitialized()) {
    return {};
  }
  t850::PhysicsTriangleMeshBodyDesc descriptor;
  uint32_t entityId = 2166136261u;
  auto mix = [&](int value) {
    entityId ^= static_cast<uint32_t>(value);
    entityId *= 16777619u;
  };
  mix(key.x);
  mix(key.y);
  mix(key.z);
  descriptor.entityId = entityId == 0 ? 1u : entityId;
  descriptor.debugName = "minecraft_chunk_" + std::to_string(key.x) + "_" +
      std::to_string(key.y) + "_" + std::to_string(key.z);
  descriptor.mesh.vertices.reserve(snapshot.vertices.size());
  for (const t850::MutableMeshVertex& vertex : snapshot.vertices) {
    descriptor.mesh.vertices.push_back(vertex.position);
  }
  descriptor.mesh.indices = snapshot.indices;
  descriptor.mesh.localBounds = snapshot.localBounds;
  descriptor.mesh.settings.buildQuality = t850::PhysicsMeshBuildQuality::FavorBuildSpeed;
  descriptor.mesh.settings.useDiskCache = false;
  descriptor.worldTransform.Identity();
  const auto dimensions = m_world.Dimensions();
  descriptor.worldTransform.m41 = static_cast<float>(key.x * dimensions.x);
  descriptor.worldTransform.m42 = static_cast<float>(key.y * dimensions.y);
  descriptor.worldTransform.m43 = static_cast<float>(key.z * dimensions.z);
  descriptor.gameplayLayer = t850::GameplayLayer::WorldStatic;
  return pEngineContext->physics->CreateTriangleMeshBody(descriptor);
}

void MinecraftScene::UpdateStreaming() {
  if (!m_assetsCreated) return;
  const int worldX = static_cast<int>(std::floor(m_camera.Eye.x));
  const int worldY = static_cast<int>(std::floor(m_camera.Eye.y));
  const int worldZ = static_cast<int>(std::floor(m_camera.Eye.z));
  const t850::terrain::ChunkKey focus = m_world.WorldToChunk(worldX, worldY, worldZ);
  const std::vector<t850::terrain::ChunkKey> loaded = m_world.LoadedChunkKeys();
  t850::ThreadPool* threadPool = pEngineContext ? pEngineContext->threadPool : nullptr;
  const t850::terrain::VoxelChunkBuildFunction build = [this](
      const t850::terrain::VoxelChunkBuildRequest& request) {
    return BuildStreamedChunk(request);
  };
  m_streaming.Update(focus, loaded, threadPool, build);
  for (auto& result : m_streaming.TakeCompleted()) CommitStreamedChunk(std::move(result));
  for (const t850::terrain::ChunkKey key : m_streaming.TakeUnloadRequests()) UnloadChunk(key);
  const auto& stats = m_streaming.Stats();
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.desired_chunks", static_cast<double>(stats.desired));
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.queued_chunks", static_cast<double>(stats.queued));
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.in_flight_chunks", static_cast<double>(stats.inFlight));
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.loaded_chunks", static_cast<double>(m_world.ChunkCount()));
}

void MinecraftScene::RebuildChunkMeshes() {
  if (!m_assetsCreated) return;
  for (const t850::terrain::ChunkKey& key : m_world.LoadedChunkKeys()) {
    const t850::terrain::VoxelChunk* chunk = m_world.FindChunk(key);
    if (!chunk) continue;
    t850::MutableMeshSnapshot snapshot;
    std::string error;
    const t850::terrain::NeighborBlockSampler sample = [this, key](int x, int y, int z) {
      return SampleNeighbor(key, x, y, z);
    };
    if (!t850::terrain::BuildGreedyVoxelMesh(*chunk, m_blockRegistry, sample, snapshot, &error)) {
      T8_LOG_ERROR("[MinecraftScene] Chunk (%d,%d,%d) meshing failed: %s", key.x, key.y, key.z, error.c_str());
      continue;
    }
    const t850::PhysicsBodyHandle replacementBody = CreateChunkPhysics(key, snapshot);
    auto found = m_chunkRenders.find(key);
    if (found == m_chunkRenders.end()) {
      ChunkRender render;
      render.mesh = std::make_unique<t850::MutableMesh>();
      render.mesh->SetEngineContext(pEngineContext);
      render.mesh->Create();
      if (!render.mesh->ReplaceSnapshot(std::move(snapshot), &error)) {
        T8_LOG_ERROR("[MinecraftScene] Chunk GPU commit failed: %s", error.c_str());
        if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
          pEngineContext->physics->DestroyBody(replacementBody);
        }
        continue;
      }
      t850::PrimitiveInst instance;
      instance.CreateInstance(render.mesh.get(), &m_camera.VP);
      const auto dimensions = m_world.Dimensions();
      instance.TranslateAbsolute(
          static_cast<float>(key.x * dimensions.x),
          static_cast<float>(key.y * dimensions.y),
          static_cast<float>(key.z * dimensions.z));
      instance.SetTexture(m_blockAtlas, 0);
      instance.Update();
      render.instance = m_renderContainer.AddMeshInstance(instance);
      render.body = replacementBody;
      if (!render.instance.IsValid()) {
        if (render.body.IsValid() && pEngineContext && pEngineContext->physics) {
          pEngineContext->physics->DestroyBody(render.body);
        }
        render.mesh->Destroy();
        continue;
      }
      m_chunkRenders.emplace(key, std::move(render));
    } else {
      if (!found->second.mesh->ReplaceSnapshot(std::move(snapshot), &error)) {
        T8_LOG_ERROR("[MinecraftScene] Chunk GPU replacement failed: %s", error.c_str());
        if (replacementBody.IsValid() && pEngineContext && pEngineContext->physics) {
          pEngineContext->physics->DestroyBody(replacementBody);
        }
        continue;
      }
      if (pEngineContext && pEngineContext->physics) {
        if (found->second.body.IsValid()) pEngineContext->physics->DestroyBody(found->second.body);
        found->second.body = replacementBody;
      }
    }
  }
  m_remeshRequested = false;
  t850::RuntimeTelemetry::SetCounter("terrain.voxel.loaded_chunks", static_cast<double>(m_world.ChunkCount()));
}

void MinecraftScene::OnUpdate(float deltaSeconds) {
  m_deltaSeconds = deltaSeconds;
  if (!m_dumper.SkipCameraUpdates()) {
    m_cameraController.Update(deltaSeconds, t850::CameraUpdateContext{this});
  }
  m_dumper.UpdateReplayState();
  UpdateStreaming();
  if (m_remeshRequested) RebuildChunkMeshes();
  UpdateSword(deltaSeconds);
  UpdateEnemies(deltaSeconds);
}

void MinecraftScene::SelectHotbarBlock(int index) {
  if (index < 0 || index >= kHotbarSize) return;
  m_selectedHotbar = index;
}

void MinecraftScene::SyncLightCameraFromDirectionalLight() {
  for (const Light& light : SceneProp.Lights) {
    if (light.Type != LIGHT_DIRECTIONAL) continue;
    XVECTOR3 direction = light.Direction;
    if (direction.Length() <= 0.0001f) return;
    direction.Normalize();
    m_lightCamera.SetLookAt(m_lightCamera.Eye + direction);
    m_lightCamera.Update(0.0f);
    return;
  }
}

namespace {
// Append an axis-aligned box (centered at `center`, half extents `half`) to a
// mesh snapshot with the given material color. Used to compose the sword and
// enemy bodies from multiple boxes.
void AppendBoxToSnapshot(t850::MutableMeshSnapshot& snapshot,
                         const XVECTOR3& center,
                         const XVECTOR3& half,
                         const XVECTOR3& color,
                         float metallic = 0.0f,
                         float roughness = 0.6f) {
  const XVECTOR3 positions[6][4] = {
      {{half.x, -half.y, -half.z, 1.0f}, {half.x, -half.y, half.z, 1.0f},
       {half.x, half.y, half.z, 1.0f}, {half.x, half.y, -half.z, 1.0f}},
      {{-half.x, -half.y, half.z, 1.0f}, {-half.x, -half.y, -half.z, 1.0f},
       {-half.x, half.y, -half.z, 1.0f}, {-half.x, half.y, half.z, 1.0f}},
      {{-half.x, half.y, -half.z, 1.0f}, {half.x, half.y, -half.z, 1.0f},
       {half.x, half.y, half.z, 1.0f}, {-half.x, half.y, half.z, 1.0f}},
      {{-half.x, -half.y, half.z, 1.0f}, {half.x, -half.y, half.z, 1.0f},
       {half.x, -half.y, -half.z, 1.0f}, {-half.x, -half.y, -half.z, 1.0f}},
      {{-half.x, -half.y, half.z, 1.0f}, {half.x, -half.y, half.z, 1.0f},
       {half.x, half.y, half.z, 1.0f}, {-half.x, half.y, half.z, 1.0f}},
      {{half.x, -half.y, -half.z, 1.0f}, {-half.x, -half.y, -half.z, 1.0f},
       {-half.x, half.y, -half.z, 1.0f}, {half.x, half.y, -half.z, 1.0f}},
  };
  const XVECTOR3 normals[6] = {
      {1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 0.0f},
  };
  const uint32_t base = static_cast<uint32_t>(snapshot.vertices.size());
  const uint32_t indexBase = static_cast<uint32_t>(snapshot.indices.size());
  for (int face = 0; face < 6; ++face) {
    for (int v = 0; v < 4; ++v) {
      t850::MutableMeshVertex vertex;
      vertex.position = positions[face][v] + center;
      vertex.normal = normals[face];
      vertex.u = (v == 1 || v == 2) ? 1.0f : 0.0f;
      vertex.v = (v == 2 || v == 3) ? 1.0f : 0.0f;
      snapshot.vertices.push_back(vertex);
    }
    snapshot.indices.insert(snapshot.indices.end(),
                            {base + face * 4, base + face * 4 + 1,
                             base + face * 4 + 2, base + face * 4,
                             base + face * 4 + 2, base + face * 4 + 3});
  }
  t850::MutableMeshMaterial material;
  material.baseColor = color;
  material.metallic = metallic;
  material.roughness = roughness;
  material.alphaMode = t850::MutableMeshAlphaMode::Opaque;
  material.usesBaseColorTexture = false;
  snapshot.materials.push_back(material);
  snapshot.sections.push_back(t850::MutableMeshSection{
      .firstIndex = indexBase,
      .indexCount = 36,
      .materialIndex = static_cast<uint32_t>(snapshot.materials.size() - 1)});
}
}  // namespace

void MinecraftScene::CreateSword() {
  if (m_swordCreated || !pEngineContext) return;
  t850::MutableMeshSnapshot snapshot;
  // Blade (silver), crossguard (brown), handle (brown), pommel (dark).
  AppendBoxToSnapshot(snapshot, XVECTOR3(0.0f, 0.0f, 0.55f, 1.0f),
                      XVECTOR3(0.06f, 0.06f, 0.55f, 1.0f),
                      XVECTOR3(0.85f, 0.87f, 0.9f, 1.0f), 0.9f, 0.2f);
  AppendBoxToSnapshot(snapshot, XVECTOR3(0.0f, 0.0f, -0.05f, 1.0f),
                      XVECTOR3(0.16f, 0.05f, 0.05f, 1.0f),
                      XVECTOR3(0.45f, 0.3f, 0.15f, 1.0f), 0.0f, 0.7f);
  AppendBoxToSnapshot(snapshot, XVECTOR3(0.0f, 0.0f, -0.3f, 1.0f),
                      XVECTOR3(0.05f, 0.05f, 0.2f, 1.0f),
                      XVECTOR3(0.4f, 0.25f, 0.1f, 1.0f), 0.0f, 0.7f);
  AppendBoxToSnapshot(snapshot, XVECTOR3(0.0f, 0.0f, -0.5f, 1.0f),
                      XVECTOR3(0.07f, 0.07f, 0.07f, 1.0f),
                      XVECTOR3(0.3f, 0.2f, 0.08f, 1.0f), 0.0f, 0.7f);
  t850::RecalculateMutableMeshBounds(snapshot);

  m_swordMesh = std::make_unique<t850::MutableMesh>();
  m_swordMesh->SetEngineContext(pEngineContext);
  m_swordMesh->Create();
  std::string error;
  if (!m_swordMesh->ReplaceSnapshot(std::move(snapshot), &error)) {
    T8_LOG_ERROR("[MinecraftScene] Sword mesh failed: %s", error.c_str());
    m_swordMesh->Destroy();
    m_swordMesh.reset();
    return;
  }
  t850::PrimitiveInst instance;
  instance.CreateInstance(m_swordMesh.get(), &m_camera.VP);
  instance.SetVisible(true);
  instance.Update();
  m_swordInstance = m_renderContainer.AddMeshInstance(instance);
  m_swordCreated = true;
}

void MinecraftScene::CreateEnemies() {
  if (m_enemiesCreated || !pEngineContext) return;
  // Spawn a handful of enemies in front of the camera at ground level so they
  // are clearly visible in the initial screenshot.
  const int count = 6;
  const float spacing = 3.0f;
  const XVECTOR3 forward = m_camera.Look;
  const XVECTOR3 right = m_camera.Right;
  // Use the camera's ground position (ignore vertical look) so enemies spawn
  // on the terrain in front of the player.
  XVECTOR3 groundEye = m_camera.Eye;
  groundEye.y = 0.0f;
  XVECTOR3 groundForward = forward;
  groundForward.y = 0.0f;
  if (groundForward.Length() > 0.0001f) groundForward.Normalize();
  for (int i = 0; i < count; ++i) {
    const float depth = 4.0f + static_cast<float>(i) * spacing;
    const float side = (i % 2 == 0 ? 1.0f : -1.0f) * 1.2f;
    XVECTOR3 spawn = groundEye + groundForward * depth + right * side;
    spawn.y = 0.0f;
    const int wx = static_cast<int>(std::floor(spawn.x));
    const int wz = static_cast<int>(std::floor(spawn.z));
    const float groundY = static_cast<float>(TerrainHeight(wx, wz)) + 1.0f;

    Enemy enemy;
    enemy.position = XVECTOR3(spawn.x, groundY, spawn.z, 1.0f);
    enemy.yaw = 0.0f;

    t850::MutableMeshSnapshot snapshot;
    // Zombie-like enemy: teal shirt, green head, dark legs. Larger and
    // brighter so they stand out against the terrain.
    AppendBoxToSnapshot(snapshot, XVECTOR3(0.0f, 0.9f, 0.0f, 1.0f),
              XVECTOR3(0.5f, 0.6f, 0.4f, 1.0f),
              XVECTOR3(0.1f, 0.6f, 0.6f, 1.0f), 0.0f, 0.6f);
    AppendBoxToSnapshot(snapshot, XVECTOR3(0.0f, 1.8f, 0.0f, 1.0f),
              XVECTOR3(0.45f, 0.45f, 0.45f, 1.0f),
              XVECTOR3(0.3f, 0.8f, 0.3f, 1.0f), 0.0f, 0.6f);
    AppendBoxToSnapshot(snapshot, XVECTOR3(-0.25f, 0.1f, 0.0f, 1.0f),
              XVECTOR3(0.25f, 0.1f, 0.3f, 1.0f),
              XVECTOR3(0.1f, 0.35f, 0.1f, 1.0f), 0.0f, 0.8f);
    AppendBoxToSnapshot(snapshot, XVECTOR3(0.25f, 0.1f, 0.0f, 1.0f),
              XVECTOR3(0.25f, 0.1f, 0.3f, 1.0f),
              XVECTOR3(0.1f, 0.35f, 0.1f, 1.0f), 0.0f, 0.8f);
    t850::RecalculateMutableMeshBounds(snapshot);

    enemy.mesh = std::make_unique<t850::MutableMesh>();
    enemy.mesh->SetEngineContext(pEngineContext);
    enemy.mesh->Create();
    std::string error;
    if (!enemy.mesh->ReplaceSnapshot(std::move(snapshot), &error)) {
      T8_LOG_ERROR("[MinecraftScene] Enemy mesh failed: %s", error.c_str());
      enemy.mesh->Destroy();
      continue;
    }
    t850::PrimitiveInst instance;
    instance.CreateInstance(enemy.mesh.get(), &m_camera.VP);
    instance.TranslateAbsolute(enemy.position.x, enemy.position.y, enemy.position.z);
    instance.SetVisible(true);
    instance.Update();
    enemy.instance = m_renderContainer.AddMeshInstance(instance);
    m_enemies.push_back(std::move(enemy));
  }
  m_enemiesCreated = true;
  T8_LOG_INFO("[MinecraftScene] Created %d enemies. Camera eye=(%.1f,%.1f,%.1f) look=(%.2f,%.2f,%.2f)",
              static_cast<int>(m_enemies.size()),
              m_camera.Eye.x, m_camera.Eye.y, m_camera.Eye.z,
              m_camera.Look.x, m_camera.Look.y, m_camera.Look.z);
  for (const Enemy& enemy : m_enemies) {
    T8_LOG_INFO("[MinecraftScene]   enemy at (%.1f,%.1f,%.1f) valid=%d",
                enemy.position.x, enemy.position.y, enemy.position.z,
                enemy.instance.IsValid() ? 1 : 0);
  }
}

void MinecraftScene::UpdateSword(float deltaSeconds) {
  if (!m_swordCreated || !m_swordInstance.IsValid()) return;
  t850::PrimitiveInst* sword = m_renderContainer.GetMesh(m_swordInstance);
  if (!sword) return;

  // Position the sword in the lower-right of the view, attached to the camera.
  const XVECTOR3 eye = m_camera.Eye;
  const XVECTOR3 look = m_camera.Look;
  const XVECTOR3 right = m_camera.Right;
  const XVECTOR3 up = m_camera.Up;
  XVECTOR3 base = eye + look * 0.7f + right * 0.35f - up * 0.35f;
  base.w = 1.0f;

  // Swing animation: rotate the sword down and back up over ~0.4s.
  float pitchOffset = 0.0f;
  float rollOffset = 0.0f;
  if (m_swingTime >= 0.0f) {
    const float t = m_swingTime / 0.4f;
    if (t >= 1.0f) {
      m_swingTime = -1.0f;
    } else {
      const float swing = std::sin(t * 3.14159265f);
      pitchOffset = -0.9f * swing;
      rollOffset = 0.5f * swing;
    }
  }

  sword->TranslateAbsolute(base.x, base.y, base.z);
  // Face the camera forward, then apply the swing pitch/roll.
  sword->RotateYAbsolute(Rad2Deg(std::atan2(look.x, look.z)));
  sword->RotateXAbsolute(Rad2Deg(std::asin(-look.y)) + Rad2Deg(pitchOffset));
  sword->RotateZAbsolute(Rad2Deg(rollOffset));
  sword->Update();
}

void MinecraftScene::UpdateEnemies(float deltaSeconds) {
  if (!m_enemiesCreated) return;
  const float speed = 2.2f;
  for (Enemy& enemy : m_enemies) {
    if (!enemy.instance.IsValid()) continue;
    t850::PrimitiveInst* inst = m_renderContainer.GetMesh(enemy.instance);
    if (!inst) continue;

    XVECTOR3 toPlayer = m_camera.Eye - enemy.position;
    toPlayer.y = 0.0f;
    const float dist = toPlayer.Length();
    if (dist > 0.01f) {
      XVECTOR3 dir = toPlayer;
      dir.Normalize();
      enemy.position += dir * speed * deltaSeconds;
      enemy.yaw = std::atan2(dir.x, dir.z);
    }

    // Keep the enemy on the terrain surface.
    const int wx = static_cast<int>(std::floor(enemy.position.x));
    const int wz = static_cast<int>(std::floor(enemy.position.z));
    const float groundY = static_cast<float>(TerrainHeight(wx, wz)) + 1.0f;
    enemy.position.y = groundY;
    enemy.position.w = 1.0f;

    inst->TranslateAbsolute(enemy.position.x, enemy.position.y, enemy.position.z);
    inst->RotateYAbsolute(Rad2Deg(enemy.yaw));
    inst->Update();
  }
}

void MinecraftScene::DestroyDynamicMeshes() {
  if (m_swordCreated && m_swordInstance.IsValid()) {
    m_renderContainer.RemoveMesh(m_swordInstance);
    m_swordInstance = {};
  }
  if (m_swordMesh) {
    m_swordMesh->Destroy();
    m_swordMesh.reset();
  }
  m_swordCreated = false;

  for (Enemy& enemy : m_enemies) {
    if (enemy.instance.IsValid()) m_renderContainer.RemoveMesh(enemy.instance);
    if (enemy.mesh) enemy.mesh->Destroy();
  }
  m_enemies.clear();
  m_enemiesCreated = false;
}

void MinecraftScene::OnInput(InputManager* input) {
  t850::CameraInputState state;
  state.moveForward = input->PressedKey(T800K_w);
  state.moveBackward = input->PressedKey(T800K_s);
  state.moveLeft = input->PressedKey(T800K_a);
  state.moveRight = input->PressedKey(T800K_d);
  state.jump = input->PressedKey(T800K_SPACE);
  state.crouch = input->PressedKey(T800K_LCTRL);
  state.sprint = input->PressedKey(T800K_LSHIFT);
  state.mouseLook = true;
  state.mouseDeltaX = static_cast<float>(input->xDelta);
  state.mouseDeltaY = static_cast<float>(-input->yDelta);
  t850::ApplyGamepadToCameraInput(state, *input, m_deltaSeconds, true);
  m_cameraController.HandleInput(state);

  // Hotbar selection: number keys 1-9 and mouse wheel.
  for (int i = 0; i < kHotbarSize; ++i) {
    if (input->PressedOnceKey(static_cast<int>(T800K_1) + i)) {
      SelectHotbarBlock(i);
    }
  }
  if (input->scrollDelta != 0.0f) {
    const int delta = input->scrollDelta > 0.0f ? -1 : 1;
    SelectHotbarBlock((m_selectedHotbar + delta + kHotbarSize) % kHotbarSize);
  }

  if (input->PressedOnceMouseButton(0) || input->PressedOnceMouseButton(1)) {
    // Left click also swings the sword.
    if (input->PressedOnceMouseButton(0)) {
      m_swingTime = 0.0f;
    }
    t850::terrain::VoxelRayHit hit;
    if (m_world.Raycast(m_camera.Eye, m_camera.Look, 8.0f, m_blockRegistry, hit)) {
      bool changed = false;
      if (input->PressedOnceMouseButton(0)) {
        changed = m_world.SetBlock(hit.blockX, hit.blockY, hit.blockZ, t850::terrain::kAirBlock);
        if (changed) m_deltas.Record(hit.blockX, hit.blockY, hit.blockZ, t850::terrain::kAirBlock);
      } else {
        const t850::terrain::BlockId place = m_hotbar[static_cast<std::size_t>(m_selectedHotbar)];
        changed = m_world.SetBlock(hit.previousX, hit.previousY, hit.previousZ, place);
        if (changed) m_deltas.Record(hit.previousX, hit.previousY, hit.previousZ, place);
      }
      if (changed) {
        m_remeshRequested = true;
        std::string error;
        if (!m_deltas.Save(m_deltaPath, &error)) {
          T8_LOG_ERROR("[MinecraftScene] Failed to save block edit: %s", error.c_str());
        }
      }
    }
  }
}

void MinecraftScene::DrawDevGui(t850::DevGuiContext& gui) {
  auto slider = [&](const char* name, const char* label, float& value,
                    float minVal, float maxVal, float step, float defVal) {
    t850::SliderDesc desc;
    desc.name = name;
    desc.label = label;
    desc.min_val = minVal;
    desc.max_val = maxVal;
    desc.step = step;
    desc.default_val = defVal;
    gui.Slider(desc, value);
  };
  auto checkbox = [&](const char* name, const char* label, bool& value) {
    t850::CheckboxDesc desc;
    desc.name = name;
    desc.label = label;
    desc.default_val = value;
    gui.Checkbox(desc, value);
  };

  if (gui.BeginSection("HDR")) {
    slider("exposure", "Exposure", SceneProp.Exposure, 0.0f, 2.0f, 0.01f, 0.0f);
    slider("bloom_factor", "Bloom factor", SceneProp.BloomFactor, 0.0f, 2.0f, 0.01f, 0.35f);
    slider("bloom_threshold", "Bloom threshold", SceneProp.BloomThreshold, 0.0f, 4.0f, 0.01f, 2.0f);
    slider("tm_white_level", "Tone map white level", SceneProp.ToneMapWhiteLevel, 0.0f, 8.0f, 0.1f, 4.0f);
    slider("tm_adapt_tau", "Luminance tau", SceneProp.LuminanceTau, 0.0f, 4.0f, 0.01f, 1.1f);
  }

  if (gui.BeginSection("Shadows")) {
    bool shadowEnabled = SceneProp.ToogleShadow != 0;
    checkbox("shadow_toggle", "Shadows", shadowEnabled);
    SceneProp.ToogleShadow = shadowEnabled ? 1 : 0;
    slider("shadow_bias", "Shadow bias", SceneProp.ShadowBias, 0.0f, 0.001f, 0.000001f, 0.000005f);
    slider("shadow_min", "Shadow min", SceneProp.ShadowMin, 0.0f, 1.0f, 0.01f, 0.75f);
    slider("pcf_radius", "PCF radius", SceneProp.PCFScale, 0.0f, 8.0f, 0.1f, 1.7f);
    slider("pcf_samples", "PCF samples", SceneProp.PCFSamples, 1.0f, 16.0f, 1.0f, 1.0f);
    slider("shadow_resolution", "Shadow map res", SceneProp.ShadowMapResolution, 256.0f, 4096.0f, 256.0f, 1024.0f);
  }

  if (gui.BeginSection("SSAO")) {
    bool ssaoEnabled = SceneProp.ToogleSSAO != 0;
    checkbox("ssao_toggle", "SSAO", ssaoEnabled);
    SceneProp.ToogleSSAO = ssaoEnabled ? 1 : 0;
    float kernelSize = static_cast<float>(SceneProp.SSAOKernel.KernelSize);
    slider("ssao_kernel_size", "SSAO kernel size", kernelSize, 4.0f, 32.0f, 2.0f, 10.0f);
    SceneProp.SSAOKernel.KernelSize = static_cast<int>(kernelSize);
    SceneProp.SSAOKernel.Update();
    slider("ssao_radius", "SSAO radius", SceneProp.SSAOKernel.Radius, 0.0f, 4.0f, 0.1f, 1.0f);
  }

  if (gui.BeginSection("Lighting")) {
    if (!SceneProp.Lights.empty()) {
      slider("light_intensity", "Light intensity", SceneProp.Lights[0].Intensity, 0.0f, 20.0f, 0.1f, 4.5f);
    }
    slider("ibl_factor", "IBL factor", SceneProp.IBLFactor, 0.0f, 2.0f, 0.01f, 0.6f);
    slider("env_factor", "Env factor", SceneProp.EnvFactor, 0.0f, 2.0f, 0.01f, 1.0f);
    slider("lightmap_intensity", "Lightmap intensity", SceneProp.LightmapIntensity, 0.0f, 2.0f, 0.01f, 1.0f);
    float ambient = SceneProp.AmbientColor.x;
    slider("ambient", "Ambient", ambient, 0.0f, 1.0f, 0.01f, 0.5f);
    SceneProp.AmbientColor = XVECTOR3(ambient, ambient, ambient, 1.0f);
  }

  if (gui.BeginSection("Toggles")) {
    bool dof = SceneProp.ToogleDOF != 0;
    checkbox("dof_toggle", "DOF", dof);
    SceneProp.ToogleDOF = dof ? 1 : 0;
    bool parallax = SceneProp.ToogleParallax != 0;
    checkbox("parallax_toggle", "Parallax", parallax);
    SceneProp.ToogleParallax = parallax ? 1 : 0;
    bool godRays = SceneProp.ToogleGodRays != 0;
    checkbox("godrays_toggle", "God rays", godRays);
    SceneProp.ToogleGodRays = godRays ? 1 : 0;
  }

  if (gui.BeginSection("Sky")) {
    static const char* kSkyOptions[] = {
        "Minecraft Blue", "SkyWater", "SkyDawn", "Mountains", "Ennis", "Glacier"};
    static int skySelection = 0;
    t850::SelectorDesc skyDesc;
    skyDesc.name = "cubemap";
    skyDesc.label = "Sky cubemap";
    for (const char* opt : kSkyOptions) skyDesc.options.push_back(opt);
    if (gui.Combo(skyDesc, skySelection)) {
      if (skySelection == 0) {
        BuildSkyCubemap();
      } else {
        const char* path = nullptr;
        switch (skySelection) {
          case 1: path = "sky/CubeMap_SkyWater.dds"; break;
          case 2: path = "sky/CubeMap_SkyDawn.dds"; break;
          case 3: path = "sky/CubeMap_Mountains.dds"; break;
          case 4: path = "sky/Ennis.dds"; break;
          case 5: path = "sky/Glacier.dds"; break;
        }
        if (path && pFramework && pFramework->pVideoDriver) {
          if (m_envMapTexIndex >= 0) pFramework->pVideoDriver->DestroyTexture(m_envMapTexIndex);
          m_envMapTexIndex = pFramework->pVideoDriver->CreateTexture(path);
          if (m_envMapTexIndex >= 0) {
            m_envMaps.SetFallback(m_envMapTexIndex);
            m_renderContainer.SetEnvironmentMaps(m_envMaps);
          }
        }
      }
    }
  }
}

void MinecraftScene::OnDraw() {
  if (!m_assetsCreated) return;
  m_renderContainer.Execute(pFramework->pVideoDriver, m_deltaSeconds);
  if (m_dumper.ShouldDump(m_deltaSeconds)) {
    std::vector<t850::RTDumpEntry> targets = {
        {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
        {m_renderContainer.Graph().GetRTHandle("GBuffer"), t850::BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
        {m_renderContainer.Graph().GetRTHandle("Deferred"), t850::BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
        {m_renderContainer.Graph().GetRTHandle("ExtraHelper"), t850::BaseDriver::COLOR0_ATTACHMENT, "HDR_Final"}};
    m_dumper.DumpFrame(
        pFramework->pVideoDriver, m_camera, m_lightCamera, SceneProp, targets, m_deltaSeconds);
    if (m_dumper.ShouldExit()) std::exit(0);
  }
}

bool MinecraftScene::SweepAabb(const XVECTOR3& start,
                               const XVECTOR3& displacement,
                               const XVECTOR3& halfExtents,
                               t850::CharacterCollisionHit& hit) const {
  hit = t850::CharacterCollisionHit{};
  const XVECTOR3 end = start + displacement;
  const int minX = static_cast<int>(std::floor((std::min)(start.x, end.x) - halfExtents.x)) - 1;
  const int minY = static_cast<int>(std::floor((std::min)(start.y, end.y) - halfExtents.y)) - 1;
  const int minZ = static_cast<int>(std::floor((std::min)(start.z, end.z) - halfExtents.z)) - 1;
  const int maxX = static_cast<int>(std::ceil((std::max)(start.x, end.x) + halfExtents.x)) + 1;
  const int maxY = static_cast<int>(std::ceil((std::max)(start.y, end.y) + halfExtents.y)) + 1;
  const int maxZ = static_cast<int>(std::ceil((std::max)(start.z, end.z) + halfExtents.z)) + 1;
  float bestFraction = 1.0f;
  XVECTOR3 bestNormal;
  bool found = false;
  for (int z = minZ; z <= maxZ; ++z) {
    for (int y = minY; y <= maxY; ++y) {
      for (int x = minX; x <= maxX; ++x) {
        const auto block = m_world.GetBlock(x, y, z);
        if (!m_blockRegistry.Get(block).collidable) continue;
        const XVECTOR3 minimum(
            static_cast<float>(x) - halfExtents.x,
            static_cast<float>(y) - halfExtents.y,
            static_cast<float>(z) - halfExtents.z,
            1.0f);
        const XVECTOR3 maximum(
            static_cast<float>(x + 1) + halfExtents.x,
            static_cast<float>(y + 1) + halfExtents.y,
            static_cast<float>(z + 1) + halfExtents.z,
            1.0f);
        float fraction = 1.0f;
        XVECTOR3 normal;
        if (SweepPointAgainstExpandedBlock(start, displacement, minimum, maximum, fraction, normal) &&
            fraction < bestFraction) {
          bestFraction = fraction;
          bestNormal = normal;
          found = true;
        }
      }
    }
  }
  if (!found) return false;
  hit.hit = true;
  hit.fraction = bestFraction;
  hit.position = start + displacement * bestFraction;
  hit.normal = bestNormal;
  return true;
}

bool MinecraftScene::SweepCapsule(
    const t850::CharacterCollisionSweep& sweep, t850::CharacterCollisionHit& hit) const {
  return SweepAabb(
      sweep.startCenter,
      sweep.displacement,
      XVECTOR3(sweep.radius, sweep.halfHeight + sweep.radius, sweep.radius, 0.0f),
      hit);
}

bool MinecraftScene::SweepBox(
    const t850::CharacterBoxSweep& sweep, t850::CharacterCollisionHit& hit) const {
  return SweepAabb(sweep.startCenter, sweep.displacement, sweep.halfExtents, hit);
}
