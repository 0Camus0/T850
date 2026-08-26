#include <pch.h>

#include <MinecraftScene.h>

#include <core/Config.h>
#include <core/EngineContext.h>
#include <debug/RuntimeTelemetry.h>
#include <physics/JoltPhysicsSystem.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>

namespace {

// ---------------------------------------------------------------------------
// Deterministic hashing / value noise.  Pure functions of the block coords so
// chunk builds are thread-safe and reproducible across the whole world.
// ---------------------------------------------------------------------------
constexpr int kSeed = 0x51ed270b;

// Small, cheap, deterministic per-cell random in [0,1).
float CellRandom(int x, int y, int z) {
  uint32_t h = static_cast<uint32_t>(x) * 1973u ^ static_cast<uint32_t>(y) * 9277u ^
      static_cast<uint32_t>(z) * 6889u ^ static_cast<uint32_t>(kSeed);
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return static_cast<float>(h % 1024u) / 1024.0f;
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

// Convert sRGB 0..255 to linear 0..1 for the sky cubemap (HDR pipeline).
float SrgbToLinear(int v) {
  const float c = static_cast<float>(v) / 255.0f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Small uniform random in [0,1) for mob spawn placement (thread-unsafe is fine:
// only called from the main update thread).
static std::mt19937 s_rng(0x51ed270bu);
float RandF() {
  static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(s_rng);
}

// ---------------------------------------------------------------------------
// Procedural 16x16 pixel-art tile painter.  Fills one tile of the atlas with
// flat Minecraft-ish colors plus per-pixel variance so it reads as texture.
// `px` points at the tile's first byte (row-major, 64 px wide atlas).
// ---------------------------------------------------------------------------
void PaintTile(unsigned char* px, int tile) {
  auto put = [&](int u, int v, int r, int g, int b, int a = 255) {
    unsigned char* p = px + v * 64 + u * 4;
    p[0] = static_cast<unsigned char>(std::clamp(r, 0, 255));
    p[1] = static_cast<unsigned char>(std::clamp(g, 0, 255));
    p[2] = static_cast<unsigned char>(std::clamp(b, 0, 255));
    p[3] = static_cast<unsigned char>(a);
  };
  auto jitter = [&](int x0, int y0, int x1, int y1, int r, int g, int b, int amt) {
    for (int v = y0; v < y1; ++v)
      for (int u = x0; u < x1; ++u) {
        int j = static_cast<int>(std::round(
            (CellRandom(x0 + u * 7 + v * 13, tile * 31, v * 5) * 2.0f - 1.0f) * amt));
        put(u, v, r + j, g + j, b + j);
      }
  };

  switch (tile) {
    case 0:  // Bedrock
      jitter(0, 0, 16, 16, 40, 40, 42, 26);
      break;
    case 1:  // Stone
      jitter(0, 0, 16, 16, 125, 125, 128, 16);
      for (int v = 0; v < 16; ++v)
        for (int u = 0; u < 16; ++u)
          if (CellRandom(u, tile * 17, v) < 0.10f) put(u, v, 96, 96, 98);
      break;
    case 2:  // Dirt
      jitter(0, 0, 16, 16, 134, 96, 67, 16);
      for (int i = 0; i < 40; ++i) {
        int u = static_cast<int>(CellRandom(i, tile * 3, 1) * 16);
        int v = static_cast<int>(CellRandom(i, tile * 3, 2) * 16);
        put(u, v, 100, 70, 46);
      }
      break;
    case 3:  // Grass (dirt base + wavy green cap)
      for (int v = 0; v < 16; ++v)
        for (int u = 0; u < 16; ++u) {
          int j = static_cast<int>(std::round(
              (CellRandom(u, v, tile) * 2.0f - 1.0f) * 10));
          put(u, v, 134 + j, 96 + j, 67 + j);
        }
      for (int v = 0; v < 4; ++v)
        for (int u = 0; u < 16; ++u) {
          int j = static_cast<int>(std::round(
              (CellRandom(u * 3 + 1, v * 5 + 2, tile) * 2.0f - 1.0f) * 13));
          put(u, v, 88 + j, 150 + j, 60 + j);
        }
      for (int u = 0; u < 16; ++u) {
        int depth = static_cast<int>(CellRandom(u, tile * 11, 4) * 3);
        for (int d = 0; d < depth; ++d) {
          int j = static_cast<int>(std::round(
              (CellRandom(u * 5 + 3, 4 + d, tile) * 2.0f - 1.0f) * 13));
          put(u, 4 + d, 88 + j, 150 + j, 60 + j);
        }
      }
      break;
    case 4:  // Sand
      jitter(0, 0, 16, 16, 219, 207, 163, 12);
      for (int i = 0; i < 30; ++i) {
        int u = static_cast<int>(CellRandom(i, tile * 7, 3) * 16);
        int v = static_cast<int>(CellRandom(i, tile * 7, 4) * 16);
        put(u, v, 190, 178, 138);
      }
      break;
    case 5:  // Cobblestone
      jitter(0, 0, 16, 16, 112, 112, 114, 10);
      for (int v = 0; v < 16; v += 4)
        for (int u = 0; u < 16; ++u) put(u, v, 70, 70, 72);
      for (int u = 0; u < 16; u += 4)
        for (int v = 0; v < 16; ++v) put(u, v, 70, 70, 72);
      for (int v = 0; v < 16; ++v)
        for (int u = 0; u < 16; ++u)
          if (CellRandom(u * 11 + 5, v * 7 + 9, tile) < 0.12f) put(u, v, 88, 88, 90);
      break;
    case 6:  // Oak planks
      for (int v = 0; v < 16; ++v)
        for (int u = 0; u < 16; ++u) {
          int j = static_cast<int>(std::round(
              (CellRandom(u, v, tile) * 2.0f - 1.0f) * 7));
          put(u, v, 170 + j, 128 + j, 78 + j);
        }
      for (int v = 3; v < 16; v += 4)
        for (int u = 0; u < 16; ++u) put(u, v, 110, 82, 48);
      for (int v = 0; v < 16; ++v) {
        int seam = (v / 4) % 2 == 0 ? 8 : 4;
        put(seam, v, 110, 82, 48);
        put((seam + 11) % 16, v, 110, 82, 48);
      }
      break;
    case 7:  // Oak log (side)
      for (int v = 0; v < 16; ++v)
        for (int u = 0; u < 16; ++u) {
          int streak = (u % 4 == 0) ? -24 : (u % 4 == 2 ? 10 : 0);
          int j = static_cast<int>(std::round(
              (CellRandom(u, v, tile) * 2.0f - 1.0f) * 6));
          put(u, v, 104 + streak + j, 78 + streak + j, 44 + streak + j);
        }
      break;
    case 8:  // Leaves
      for (int v = 0; v < 16; ++v)
        for (int u = 0; u < 16; ++u) {
          int j = static_cast<int>(std::round(
              (CellRandom(u * 3, v * 3, tile) * 2.0f - 1.0f) * 20));
          put(u, v, 58 + j, 116 + j, 46 + j);
        }
      for (int i = 0; i < 40; ++i) {
        int u = static_cast<int>(CellRandom(i, tile * 5, 1) * 16);
        int v = static_cast<int>(CellRandom(i, tile * 5, 2) * 16);
        put(u, v, 30, 64, 24);
      }
      break;
    case 9:  // Water
      jitter(0, 0, 16, 16, 48, 96, 190, 14);
      for (int v = 0; v < 16; v += 3)
        for (int u = 0; u < 16; ++u)
          if (CellRandom(u, v, tile) < 0.4f) put(u, v, 70, 120, 210);
      break;
    case 10:  // Creeper face (baked colors; white baseColor so the tex shows true)
      for (int v = 0; v < 16; ++v)
        for (int u = 0; u < 16; ++u) {
          int j = static_cast<int>((CellRandom(u, v, tile) * 2.0f - 1.0f) * 16);
          put(u, v, 92 + j, 170 + j, 78 + j);
        }
      for (int e = 0; e < 2; ++e) {
        int u0 = e == 0 ? 2 : 10;
        for (int v = 3; v <= 6; ++v)
          for (int u = u0; u <= u0 + 3; ++u) put(u, v, 12, 12, 12);
      }
      for (int v = 7; v <= 11; ++v)
        for (int u = 6; u <= 9; ++u) put(u, v, 12, 12, 12);
      for (int v = 12; v <= 15; ++v) {
        for (int u = 5; u <= 10; ++u) put(u, v, 12, 12, 12);
        if (v >= 13) { put(4, v, 12, 12, 12); put(11, v, 12, 12, 12); }
      }
      break;
    case 11:  // Creeper skin (mottled green)
      for (int v = 0; v < 16; ++v)
        for (int u = 0; u < 16; ++u) {
          int j = static_cast<int>((CellRandom(u, v, tile) * 2.0f - 1.0f) * 22);
          put(u, v, 78 + j, 150 + j, 70 + j);
        }
      for (int i = 0; i < 30; ++i) {
        int u = static_cast<int>(CellRandom(i, tile * 3, 1) * 16);
        int v = static_cast<int>(CellRandom(i, tile * 3, 2) * 16);
        put(u, v, 130, 205, 120);
      }
      for (int i = 0; i < 30; ++i) {
        int u = static_cast<int>(CellRandom(i, tile * 3, 3) * 16);
        int v = static_cast<int>(CellRandom(i, tile * 3, 4) * 16);
        put(u, v, 38, 82, 34);
      }
      break;
    default:
      jitter(0, 0, 16, 16, 200, 40, 200, 20);
      break;
  }
}

// ---------------------------------------------------------------------------
// Box-face helpers for the creeper mesh: one flat quad per face, UVs span the
// whole 16x16 tile (NEAREST filtering gives the pixelated look).  The front
// face (+z) is routed to a separate index list so head/body can use different
// tiles; the material is doubleSided so winding can never cull a face.
// ---------------------------------------------------------------------------
void PushBoxFace(std::vector<t850::MutableMeshVertex>& verts, std::vector<uint32_t>& idx,
                 const XVECTOR3& a, const XVECTOR3& b, const XVECTOR3& c, const XVECTOR3& d,
                 const XVECTOR3& n, const t850::terrain::BlockDefinition& m) {
  const uint32_t base = static_cast<uint32_t>(verts.size());
  verts.push_back(t850::MutableMeshVertex{a, n, m.atlasU0, m.atlasV0});
  verts.push_back(t850::MutableMeshVertex{b, n, m.atlasU1, m.atlasV0});
  verts.push_back(t850::MutableMeshVertex{c, n, m.atlasU1, m.atlasV1});
  verts.push_back(t850::MutableMeshVertex{d, n, m.atlasU0, m.atlasV1});
  idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void PushBox(std::vector<t850::MutableMeshVertex>& verts, std::vector<uint32_t>& sideIdx,
             std::vector<uint32_t>& frontIdx, float x0, float y0, float z0, float x1,
             float y1, float z1, const t850::terrain::BlockDefinition& skin,
             const t850::terrain::BlockDefinition& front) {
  auto P = [](float x, float y, float z) { return XVECTOR3(x, y, z, 1.0f); };
  XVECTOR3 up(0, 1, 0, 0), dn(0, -1, 0, 0), lf(-1, 0, 0, 0), rt(1, 0, 0, 0),
      bk(0, 0, -1, 0), pf(0, 0, 1, 0);
  PushBoxFace(verts, sideIdx, P(x0, y1, z1), P(x1, y1, z1), P(x1, y1, z0), P(x0, y1, z0), up, skin);
  PushBoxFace(verts, sideIdx, P(x0, y0, z0), P(x1, y0, z0), P(x1, y0, z1), P(x0, y0, z1), dn, skin);
  PushBoxFace(verts, sideIdx, P(x0, y0, z1), P(x0, y1, z1), P(x1, y1, z1), P(x1, y0, z1), lf, skin);
  PushBoxFace(verts, sideIdx, P(x1, y0, z0), P(x1, y1, z0), P(x0, y1, z0), P(x0, y0, z0), rt, skin);
  PushBoxFace(verts, sideIdx, P(x0, y0, z0), P(x1, y0, z0), P(x1, y1, z0), P(x0, y1, z0), bk, skin);
  PushBoxFace(verts, frontIdx, P(x1, y0, z1), P(x0, y0, z1), P(x0, y1, z1), P(x1, y1, z1), pf, front);
}

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

} // namespace

MinecraftScene::MinecraftScene()
    : m_world(t850::terrain::ChunkDimensions{16, 16, 16}),
      m_streaming(t850::terrain::ChunkDimensions{16, 16, 16}) {}

int MinecraftScene::Hash2D(int x, int z) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u ^ static_cast<uint32_t>(z) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return static_cast<int>(h ^ (h >> 16));
}

int MinecraftScene::Hash3D(int x, int y, int z) { return Hash2D(Hash2D(x, z), y); }

float MinecraftScene::ValueNoise2D(int x, int z, float scale) {
  const int gx = static_cast<int>(std::floor(x * scale));
  const int gz = static_cast<int>(std::floor(z * scale));
  const float fx = x * scale - gx;
  const float fz = z * scale - gz;
  const float v00 = CellRandom(gx, 0, gz);
  const float v10 = CellRandom(gx + 1, 0, gz);
  const float v01 = CellRandom(gx, 0, gz + 1);
  const float v11 = CellRandom(gx + 1, 0, gz + 1);
  const float sx = SmoothStep(fx);
  const float sz = SmoothStep(fz);
  const float a = v00 + (v10 - v00) * sx;
  const float b = v01 + (v11 - v01) * sx;
  return a + (b - a) * sz;
}

float MinecraftScene::FBM2D(int x, int z, float scale, int octaves) {
  float amplitude = 1.0f;
  float frequency = scale;
  float sum = 0.0f;
  float norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += ValueNoise2D(x, z, frequency) * amplitude;
    norm += amplitude;
    amplitude *= 0.5f;
    frequency *= 2.0f;
  }
  return sum / norm;
}

int MinecraftScene::TerrainHeight(int x, int z) {
  const float n = FBM2D(x, z, 0.05f, 4);     // rolling hills
  const float n2 = FBM2D(x, z, 0.015f, 3);   // large-scale variation
  const float h = 3.0f + n * 10.0f + (n2 - 0.5f) * 8.0f;
  return static_cast<int>(std::floor(h));
}

bool MinecraftScene::TreeAt(int x, int z) {
  if (TerrainHeight(x, z) < kWaterLevel + 1) return false;
  const int h = Hash2D(x * 5 + 3, z * 5 + 7);
  return (h % 1000) < 15;  // ~1.5% of land columns
}

t850::terrain::BlockId MinecraftScene::TerrainBlock(int x, int y, int z) const {
  const int height = TerrainHeight(x, z);
  if (y <= 0) return m_bedrock;
  if (y < height - 3) return m_stone;
  if (y < height) return m_dirt;
  if (y == height) {
    if (height <= kWaterLevel) return m_sand;
    return m_grass;
  }
  // above the solid ground
  if (y <= kWaterLevel) return m_water;
  return t850::terrain::kAirBlock;
}

void MinecraftScene::PlaceTree(t850::terrain::VoxelChunk& chunk, int baseWorldX,
                               int baseWorldZ, int baseWorldY, int trunkH) const {
  const auto dimensions = chunk.Dimensions();
  const int baseLocalX = baseWorldX - chunk.Key().x * dimensions.x;
  const int baseLocalZ = baseWorldZ - chunk.Key().z * dimensions.z;

  // Trunk.
  for (int i = 0; i < trunkH; ++i) {
    const int wy = baseWorldY + 1 + i;
    const int ly = wy - chunk.Key().y * dimensions.y;
    if (ly >= 0 && ly < dimensions.y) chunk.Set(baseLocalX, ly, baseLocalZ, m_log);
  }
  // Leaf canopy: a rounded 5x5x3 blob centred on the top of the trunk.
  // Only set air->leaves so we never overwrite the trunk.
  const int topY = baseWorldY + 1 + trunkH;
  for (int dy = 1; dy <= 3; ++dy) {
    const int radius = (dy == 3) ? 1 : 2;
    for (int dz = -radius; dz <= radius; ++dz) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (dy == 3 && (std::abs(dx) == 1 && std::abs(dz) == 1)) continue;
        const int wy = topY + dy;
        const int wx = baseWorldX + dx;
        const int wz = baseWorldZ + dz;
        const int lx = wx - chunk.Key().x * dimensions.x;
        const int lz = wz - chunk.Key().z * dimensions.z;
        const int ly = wy - chunk.Key().y * dimensions.y;
        if (lx < 0 || lx >= dimensions.x || lz < 0 || lz >= dimensions.z ||
            ly < 0 || ly >= dimensions.y)
          continue;
        if (chunk.Get(lx, ly, lz) == t850::terrain::kAirBlock) chunk.Set(lx, ly, lz, m_leaves);
      }
    }
  }
}

void MinecraftScene::InitVars() {
  m_deltaSeconds = 0.0f;
  m_remeshRequested = false;
  m_assetsCreated = false;
  m_selectedSlot = 0;
  m_skyCubeIndex = -1;
  m_chunkRenders.clear();
  m_creepers.clear();
  m_explosions.clear();
  m_creeperMesh.reset();
  m_health = m_maxHealth;
  m_damageFlash = 0.0f;
  m_creeperSpawnTimer = 3.0f;
  m_world.Clear();
  m_streaming.Reset();
  m_deltas.Clear();
  m_blockRegistry = t850::terrain::BlockRegistry{};

  // Real 1024x1024 Minecraft texture atlas: a 64x64 grid of 16x16 tiles.
  // NOTE: the engine downscales textures by FORCED_FACTOR (Config.h, =2) so the
  // atlas uploads as 512x512. We therefore address tiles by their grid cell
  // (col,row) in the 1024 grid and derive UVs from the cell index — this is
  // resolution-independent and lands on the correct tile at any upload size.
  // A 0.5-px inset (in the 1024 space) keeps NEAREST sampling from bleeding.
  auto tileRect = [](int col, int row, t850::terrain::BlockDefinition& d) {
    constexpr float kInv = 1.0f / 1024.0f;
    const float in = 0.5f * kInv;
    const float x = col * 16.0f, y = row * 16.0f;
    d.atlasU0 = (x + in) * kInv;
    d.atlasV0 = (y + in) * kInv;
    d.atlasU1 = (x + 16.0f - in) * kInv;
    d.atlasV1 = (y + 16.0f - in) * kInv;
  };

  t850::terrain::BlockDefinition def;

  def = t850::terrain::BlockDefinition{};
  def.name = "bedrock";
  def.unlit = true;
  def.color = XVECTOR3(0.16f, 0.16f, 0.17f, 1.0f);
  def.usesBaseColorTexture = true;
  tileRect(20, 13, def);
  m_bedrock = m_blockRegistry.Register(std::move(def));

  def = t850::terrain::BlockDefinition{};
  def.name = "stone";
  def.unlit = true;
  def.color = XVECTOR3(0.49f, 0.49f, 0.50f, 1.0f);
  def.roughness = 0.95f;
  def.usesBaseColorTexture = true;
  tileRect(30, 18, def);
  m_stone = m_blockRegistry.Register(std::move(def));

  def = t850::terrain::BlockDefinition{};
  def.name = "dirt";
  def.unlit = true;
  def.color = XVECTOR3(0.53f, 0.38f, 0.26f, 1.0f);
  def.roughness = 1.0f;
  def.usesBaseColorTexture = true;
  tileRect(17, 10, def);
  m_dirt = m_blockRegistry.Register(std::move(def));

  def = t850::terrain::BlockDefinition{};
  def.name = "grass";
  def.unlit = true;
  def.color = XVECTOR3(0.35f, 0.59f, 0.24f, 1.0f);
  def.roughness = 1.0f;
  def.usesBaseColorTexture = true;
  // Sides = grass_side (main rect); top = grass_top; bottom = dirt.
  def.faceTiles = true;
  tileRect(21, 4, def);                          // top = grass_top
  def.atlasTopU0 = def.atlasU0; def.atlasTopV0 = def.atlasV0;
  def.atlasTopU1 = def.atlasU1; def.atlasTopV1 = def.atlasV1;
  tileRect(17, 10, def);                         // bottom = dirt
  def.atlasBottomU0 = def.atlasU0; def.atlasBottomV0 = def.atlasV0;
  def.atlasBottomU1 = def.atlasU1; def.atlasBottomV1 = def.atlasV1;
  tileRect(20, 6, def);                          // main (all sides) = grass_side
  m_grass = m_blockRegistry.Register(std::move(def));

  def = t850::terrain::BlockDefinition{};
  def.unlit = true;
  def.name = "sand";
  def.color = XVECTOR3(0.86f, 0.81f, 0.64f, 1.0f);
  def.roughness = 1.0f;
  def.usesBaseColorTexture = true;
  tileRect(28, 17, def);
  m_sand = m_blockRegistry.Register(std::move(def));

  def = t850::terrain::BlockDefinition{};
  def.unlit = true;
  def.name = "cobblestone";
  def.color = XVECTOR3(0.44f, 0.44f, 0.45f, 1.0f);
  def.roughness = 0.95f;
  def.usesBaseColorTexture = true;
  tileRect(30, 5, def);
  m_cobble = m_blockRegistry.Register(std::move(def));

  def = t850::terrain::BlockDefinition{};
  def.unlit = true;
  def.name = "oak_planks";
  def.color = XVECTOR3(0.67f, 0.50f, 0.31f, 1.0f);
  def.roughness = 0.9f;
  def.usesBaseColorTexture = true;
  tileRect(12, 8, def);
  m_planks = m_blockRegistry.Register(std::move(def));

  def = t850::terrain::BlockDefinition{};
  def.unlit = true;
  def.name = "oak_log";
  def.color = XVECTOR3(0.41f, 0.31f, 0.17f, 1.0f);
  def.roughness = 0.9f;
  def.usesBaseColorTexture = true;
  tileRect(12, 13, def);
  m_log = m_blockRegistry.Register(std::move(def));

  def = t850::terrain::BlockDefinition{};
  def.unlit = true;
  def.name = "leaves";
  def.color = XVECTOR3(0.23f, 0.45f, 0.18f, 1.0f);
  def.roughness = 1.0f;
  def.usesBaseColorTexture = true;
  tileRect(21, 7, def);
  m_leaves = m_blockRegistry.Register(std::move(def));

  def = t850::terrain::BlockDefinition{};
  def.unlit = true;
  def.name = "water";
  def.color = XVECTOR3(0.19f, 0.38f, 0.75f, 1.0f);
  def.alphaMode = t850::MutableMeshAlphaMode::Blend;
  def.roughness = 0.15f;
  def.metallic = 0.0f;
  def.usesBaseColorTexture = true;
  def.renderable = true;
  def.occludes = false;   // see-through: neighbours draw their faces through it
  def.collidable = false;
  def.doubleSided = true;
  tileRect(3, 9, def);
  m_water = m_blockRegistry.Register(std::move(def));

  m_hotbar = {m_grass, m_dirt, m_stone, m_cobble, m_planks, m_log, m_leaves, m_sand, m_water};

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

  // Find a dry, tree-free, and *flat* spot near the origin so the opening view
  // is a grassy plain rather than a cliff, lake, or tree canopy.
  const auto flatEnough = [](int cx, int cz) {
    int lo = 1000000, hi = -1000000;
    for (int dz = -2; dz <= 2; ++dz)
      for (int dx = -2; dx <= 2; ++dx) {
        const int h = TerrainHeight(cx + dx, cz + dz);
        lo = (std::min)(lo, h);
        hi = (std::max)(hi, h);
      }
    return (hi - lo) <= 2;
  };
  int spawnX = 8, spawnZ = 8;
  int spawnH = TerrainHeight(8, 8);
  bool spawnOk = (spawnH >= kWaterLevel + 1) && !TreeAt(8, 8) && flatEnough(8, 8);
  for (int radius = 1; radius <= 20 && !spawnOk; ++radius) {
    for (int dz = -radius; dz <= radius && !spawnOk; ++dz) {
      for (int dx = -radius; dx <= radius; ++dx) {
        const int cx = 8 + dx, cz = 8 + dz;
        const int h = TerrainHeight(cx, cz);
        if (h >= kWaterLevel + 1 && !TreeAt(cx, cz) && flatEnough(cx, cz)) {
          spawnX = cx; spawnZ = cz; spawnH = h; spawnOk = true;
          break;
        }
      }
    }
  }
  if (!spawnOk) {
    // Fallback: any dry, tree-free spot.
    for (int radius = 1; radius <= 20 && !spawnOk; ++radius) {
      for (int dz = -radius; dz <= radius && !spawnOk; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
          const int cx = 8 + dx, cz = 8 + dz;
          if (TerrainHeight(cx, cz) >= kWaterLevel + 1 && !TreeAt(cx, cz)) {
            spawnX = cx; spawnZ = cz; spawnH = TerrainHeight(cx, cz); spawnOk = true;
            break;
          }
        }
      }
    }
  }
  const int spawnY = spawnH + 4;
  m_spawnX = spawnX; m_spawnZ = spawnZ; m_spawnY = spawnY;
  m_camera.InitPerspective(XVECTOR3(static_cast<float>(spawnX), static_cast<float>(spawnY), static_cast<float>(spawnZ)),
                           Deg2Rad(70.0f), 1280.0f / 720.0f, 0.05f, 1000.0f);
  m_camera.Eye = XVECTOR3(static_cast<float>(spawnX), static_cast<float>(spawnY), static_cast<float>(spawnZ), 1.0f);
  m_camera.Yaw = 0.0f;
  // Gentle downward tilt (Pitch>0 looks down: Pitch=asin(-Look.y)) reveals the
  // textured ground (grass tops, dirt, trees) instead of nearby hill walls.
  m_camera.Pitch = 0.25f;
  m_camera.Update(0.0f);
  m_cameraController.SetActiveProfile(t850::CameraProfileType::GroundedFps);
  m_cameraController.AttachCamera(&m_camera);

  t850::KinematicCharacterSettings settings;
  settings.walkSpeed = 5.0f;
  settings.sprintSpeed = 8.0f;
  settings.jumpSpeed = 8.0f;
  settings.gravity = 24.0f;
  settings.mouseSensitivity = 0.0022f;
  settings.capsuleRadius = 0.30f;
  settings.capsuleHalfHeight = 0.55f;
  settings.eyeHeight = 1.62f;
  m_cameraController.SetKinematicProfileSettings(t850::CameraProfileType::GroundedFps, settings);

  m_lightCamera.InitPerspective(
      XVECTOR3(static_cast<float>(spawnX + 10), 70.0f, static_cast<float>(spawnZ - 8)),
      Deg2Rad(65.0f), 1.0f, 0.1f, 400.0f);
  m_lightCamera.Eye = XVECTOR3(static_cast<float>(spawnX + 10), 70.0f, static_cast<float>(spawnZ - 8), 1.0f);
  m_lightCamera.Pitch = 1.05f;
  m_lightCamera.Yaw = 0.0f;
  m_lightCamera.Update(0.0f);

  SceneProp = SceneProps{};
  SceneProp.AddCamera(&m_camera);
  SceneProp.AddLightCamera(&m_lightCamera);
  // Bright, mostly-shadowless lighting like Minecraft: a warm sun plus the flat
  // blue sky cubemap supplying diffuse sky-light (IBL).  The deferred shader has
  // no constant ambient floor, so enabling IBLFactor is what fills the shadowed
  // faces instead of leaving them black.
  SceneProp.AddDirectionalLight(XVECTOR3(-0.30f, -1.0f, 0.20f, 0.0f),
                                XVECTOR3(1.0f, 0.98f, 0.90f, 1.0f), 2.6f, true);
  SceneProp.ActiveLights = 1;
  SceneProp.AmbientColor = XVECTOR3(0.60f, 0.64f, 0.72f, 1.0f);
  SceneProp.ToogleDOF = 0;
  SceneProp.ToogleParallax = 0;
  SceneProp.IBLFactor = 1.25f;  // diffuse IBL from the blue sky cube = skylight fill
  SceneProp.IBLBRDFLUTEnabled = 0.0f;
  SceneProp.FrustumCullingEnabled = true;

  m_shadowFilter.kernelSize = 4;
  m_shadowFilter.radius = 1.0f;
  m_shadowFilter.sigma = 1.0f;
  m_shadowFilter.Update();
  m_bloomFilter.kernelSize = 11;
  m_bloomFilter.radius = 2.0f;
  m_bloomFilter.sigma = 3.5f;
  m_bloomFilter.Update();
  m_dofFilter.kernelSize = 23;
  m_dofFilter.radius = 3.0f;
  m_dofFilter.sigma = 6.0f;
  m_dofFilter.Update();
  SceneProp.AddGaussKernel(&m_shadowFilter);
  SceneProp.AddGaussKernel(&m_bloomFilter);
  SceneProp.AddGaussKernel(&m_dofFilter);

  t850::terrain::VoxelStreamingSettings streamingSettings;
  streamingSettings.horizontalRadius = 2;
  streamingSettings.verticalRadius = 0;
  streamingSettings.maxInFlight = 4;
  streamingSettings.maxLaunchesPerUpdate = 4;
  streamingSettings.maxCommitsPerUpdate = 4;
  streamingSettings.maxUnloadsPerUpdate = 4;
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

t850::terrain::VoxelChunkBuildResult MinecraftScene::BuildStreamedChunk(
    const t850::terrain::VoxelChunkBuildRequest& request) const {
  t850::terrain::VoxelChunkBuildResult result;
  result.key = request.key;
  result.epoch = request.epoch;
  result.chunk = std::make_unique<t850::terrain::VoxelChunk>(request.key, request.dimensions);
  const auto& dimensions = request.dimensions;

  for (int z = 0; z < dimensions.z; ++z) {
    for (int x = 0; x < dimensions.x; ++x) {
      const int worldX = request.key.x * dimensions.x + x;
      const int worldZ = request.key.z * dimensions.z + z;
      for (int y = 0; y < dimensions.y; ++y) {
        if (request.IsCancelled()) {
          result.cancelled = true;
          result.chunk.reset();
          return result;
        }
        const int worldY = request.key.y * dimensions.y + y;
        result.chunk->Set(x, y, z, TerrainBlock(worldX, worldY, worldZ));
      }
    }
  }

  // Place trees whose base sits inside this chunk.  Canopies that spill into
  // neighbour chunks are clipped to the chunk bounds; those neighbour chunks
  // place their own copies where the base is local, so the overlap is harmless.
  for (int z = 0; z < dimensions.z; ++z) {
    for (int x = 0; x < dimensions.x; ++x) {
      const int worldX = request.key.x * dimensions.x + x;
      const int worldZ = request.key.z * dimensions.z + z;
      if (!TreeAt(worldX, worldZ)) continue;
      const int groundY = TerrainHeight(worldX, worldZ);
      const int trunkH = 4 + (std::abs(Hash3D(worldX, 0, worldZ)) % 2);  // 4 or 5
      PlaceTree(*result.chunk, worldX, worldZ, groundY, trunkH);
    }
  }

  m_deltas.ApplyToChunk(*result.chunk);
  if (!t850::terrain::BuildGreedyVoxelMesh(
          *result.chunk, m_blockRegistry, {}, result.mesh, &result.error)) {
    result.chunk.reset();
  }
  return result;
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

  // --- Block atlas: the real 1024x1024 Minecraft texture atlas, uploaded as an
  //     sRGB texture so the GPU decodes samples to linear. NEAREST + CLAMP keeps
  //     the crisp pixel-art look. (Resolves to Textures/minecraft_atlas.png.) ---
  if (pEngineContext && pEngineContext->device) {
    m_blockAtlas = pEngineContext->device->CreateTextureSrgb("minecraft_atlas.png");
    if (m_blockAtlas) {
      m_blockAtlas->params = t850::TextBasicParams::CLAMP_TO_EDGE |
          t850::TextBasicParams::NEAREST_FILTER;
      m_blockAtlas->SetTextureParams();
      T8_LOG_INFO("[MinecraftScene] Loaded real atlas %dx%d srgb=1",
                  m_blockAtlas->x, m_blockAtlas->y);
    } else {
      T8_LOG_ERROR("[MinecraftScene] Failed to load minecraft_atlas.png");
    }
  }

  // --- Sky: a flat Minecraft-blue cubemap (linear) supplied to the composite. ---
  if (pFramework && pFramework->pVideoDriver) {
    std::vector<unsigned char> cube(6 * 16 * 16 * 4, 0);
    // Bright Minecraft-blue sky.  This same cubemap is the diffuse IBL source,
    // so its brightness sets the ambient "sky light" that fills every face.
    const unsigned char r = static_cast<unsigned char>(std::round(SrgbToLinear(120) * 255.0f));
    const unsigned char g = static_cast<unsigned char>(std::round(SrgbToLinear(185) * 255.0f));
    const unsigned char b = static_cast<unsigned char>(std::round(SrgbToLinear(255) * 255.0f));
    for (int i = 0; i < 6 * 16 * 16; ++i) {
      unsigned char* p = cube.data() + i * 4;
      p[0] = r;
      p[1] = g;
      p[2] = b;
      p[3] = 255;
    }
    m_skyCubeIndex = pFramework->pVideoDriver->CreateCubeMap(cube.data(), 16, 16);
    t850::EnvironmentMapSet envMaps;
    if (m_skyCubeIndex >= 0) envMaps.SetFallback(m_skyCubeIndex);
    m_renderContainer.SetEnvironmentMaps(envMaps);
  }

  // --- Creeper mesh: boxy head + body sharing one unlit material that samples
  //     the creeper tiles from the block atlas. ---
  if (pEngineContext && m_blockAtlas && !m_creeperMesh) {
    m_creeperMesh = std::make_unique<t850::MutableMesh>();
    m_creeperMesh->SetEngineContext(pEngineContext);
    m_creeperMesh->Create();
    m_creeperMesh->SetBaseColorTexture(m_blockAtlas);
    std::string error;
    t850::MutableMeshSnapshot snapshot = BuildCreeperMesh();
    if (!m_creeperMesh->ReplaceSnapshot(std::move(snapshot), &error)) {
      T8_LOG_ERROR("[MinecraftScene] Creeper mesh commit failed: %s", error.c_str());
      m_creeperMesh->Destroy();
      m_creeperMesh.reset();
    }
  }

  m_debugText.LoadFromFile(22.0f, "Fonts/Martius-LV9L4.ttf", 512.0f);

  m_assetsCreated = true;
  UpdateStreaming();
}

void MinecraftScene::DestroyAssets() {
  if (!m_assetsCreated) return;
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
  m_debugText.Destroy();
  if (m_creeperMesh) {
    if (pFramework && pFramework->pVideoDriver) pFramework->pVideoDriver->WaitForGPU();
    m_creeperMesh->Destroy();
    m_creeperMesh.reset();
  }
  m_creepers.clear();
  m_explosions.clear();
  if (m_blockAtlas) {
    if (pFramework && pFramework->pVideoDriver) pFramework->pVideoDriver->WaitForGPU();
    m_blockAtlas->release();
    m_blockAtlas = nullptr;
  }
  m_skyCubeIndex = -1;
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
  render.mesh->SetBaseColorTexture(m_blockAtlas);
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
  descriptor.debugName = "mc_chunk_" + std::to_string(key.x) + "_" +
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
      render.mesh->SetBaseColorTexture(m_blockAtlas);
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
          pEngineContext->physics->DestroyBody(replacementBody);
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

t850::MutableMeshSnapshot MinecraftScene::BuildCreeperMesh() const {
  // The creeper now samples the real atlas: a mottled-green skin tile for the
  // body/sides and the classic creeper face tile for the head's front. The face
  // tile already contains the painted eyes/nose/mouth, so no extra feature quads
  // are needed. Everything is unlit for the flat Minecraft look.
  // Grid-cell based tile rect (see InitVars for explanation).
  auto tileRect = [](int col, int row, t850::terrain::BlockDefinition& d) {
    constexpr float kInv = 1.0f / 1024.0f;
    const float in = 0.5f * kInv;
    const float x = col * 16.0f, y = row * 16.0f;
    d.atlasU0 = (x + in) * kInv;
    d.atlasV0 = (y + in) * kInv;
    d.atlasU1 = (x + 16.0f - in) * kInv;
    d.atlasV1 = (y + 16.0f - in) * kInv;
    d.usesBaseColorTexture = true;
    d.unlit = true;
    d.doubleSided = true;
    d.color = XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f);
  };
  t850::terrain::BlockDefinition skinDef;
  tileRect(19, 2, skinDef);            // solid mottled-green creeper skin
  t850::terrain::BlockDefinition faceDef;
  tileRect(18, 0, faceDef);            // classic creeper face

  t850::MutableMeshSnapshot snapshot;
  std::vector<uint32_t> sideIdx, faceIdx;
  // Body: 0.5 x 0.7 x 0.25, feet at y=0 (all green).
  PushBox(snapshot.vertices, sideIdx, faceIdx, -0.25f, 0.0f, -0.125f, 0.25f, 0.7f, 0.125f,
          skinDef, skinDef);
  // Head: 0.5 x 0.5 x 0.5, creeper face on +z.
  PushBox(snapshot.vertices, sideIdx, faceIdx, -0.25f, 0.7f, -0.25f, 0.25f, 1.2f, 0.25f,
          skinDef, faceDef);

  t850::MutableMeshMaterial mat;
  mat.baseColor = XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f);
  mat.usesBaseColorTexture = true;
  mat.unlit = true;
  mat.doubleSided = true;
  mat.roughness = 1.0f;
  snapshot.materials.push_back(mat);

  uint32_t first = 0;
  snapshot.sections.push_back({first, (uint32_t)sideIdx.size(), 0});
  first += (uint32_t)sideIdx.size();
  snapshot.sections.push_back({first, (uint32_t)faceIdx.size(), 0});

  snapshot.indices.insert(snapshot.indices.end(), sideIdx.begin(), sideIdx.end());
  snapshot.indices.insert(snapshot.indices.end(), faceIdx.begin(), faceIdx.end());
  t850::RecalculateMutableMeshBounds(snapshot);
  return snapshot;
}

void MinecraftScene::SpawnCreepers(int count) {
  if (!m_creeperMesh || !m_blockAtlas) return;
  for (int n = 0; n < count; ++n) {
    if (static_cast<int>(m_creepers.size()) >= kMaxCreepers) break;
    // Try up to 12 rings around the player for a dry, tree-free, flat spawn 15-30m away.
    for (int attempt = 0; attempt < 12; ++attempt) {
      const float ang = RandF() * 6.2831853f;
      const float dist = 15.0f + RandF() * 15.0f;
      const float px = m_camera.Eye.x + std::cos(ang) * dist;
      const float pz = m_camera.Eye.z + std::sin(ang) * dist;
      const int bx = static_cast<int>(std::floor(px));
      const int bz = static_cast<int>(std::floor(pz));
      const int h = TerrainHeight(bx, bz);
      if (h <= kWaterLevel || TreeAt(bx, bz)) continue;
      // Flat enough: max height delta within +/-2.
      bool flat = true;
      for (int dz = -2; dz <= 2 && flat; ++dz)
        for (int dx = -2; dx <= 2 && flat; ++dx) {
          if (std::abs(TerrainHeight(bx + dx, bz + dz) - h) > 2) flat = false;
        }
      if (!flat) continue;
      Mob mob;
      mob.pos = XVECTOR3(px, static_cast<float>(h) + 1.0f, pz, 1.0f);
      mob.yaw = std::atan2(m_camera.Eye.x - px, m_camera.Eye.z - pz);
      t850::PrimitiveInst instance;
      instance.CreateInstance(m_creeperMesh.get(), &m_camera.VP);
      instance.SetTexture(m_blockAtlas, 0);
      instance.TranslateAbsolute(px, mob.pos.y, pz);
      instance.RotateYAbsolute(mob.yaw);
      instance.Update();
      mob.handle = m_renderContainer.AddMeshInstance(instance);
      if (!mob.handle.IsValid()) continue;
      m_creepers.push_back(std::move(mob));
      break;
    }
  }
}

void MinecraftScene::UpdateMobs(float dt) {
  const XVECTOR3 eye = m_camera.Eye;
  for (auto it = m_creepers.begin(); it != m_creepers.end();) {
    Mob& mob = *it;
    const float dx = eye.x - mob.pos.x;
    const float dz = eye.z - mob.pos.z;
    const float dist = std::sqrt(dx * dx + dz * dz);
    mob.yaw = std::atan2(dx, dz);

    if (mob.fuse < 0.0f) {
      // Approach the player; stop and start the fuse inside ~2.5m.
      if (dist > 2.5f) {
        const float speed = 2.5f;
        const float nx = dx / (dist + 1e-6f), nz = dz / (dist + 1e-6f);
        const int bx = static_cast<int>(std::floor(mob.pos.x + nx * 0.5f));
        const int bz = static_cast<int>(std::floor(mob.pos.z + nz * 0.5f));
        const int groundY = TerrainHeight(bx, bz);
        // Follow the heightmap; only step up 1 block at a time (simple hill climb).
        float targetY = static_cast<float>(groundY) + 1.0f;
        if (targetY > mob.pos.y + 1.1f) targetY = mob.pos.y + 1.1f;
        if (targetY < mob.pos.y - 1.5f) targetY = mob.pos.y;  // no falling off cliffs
        mob.pos.x += nx * speed * dt;
        mob.pos.z += nz * speed * dt;
        mob.pos.y += (targetY - mob.pos.y) * std::min(1.0f, 8.0f * dt);
        mob.walkPhase += dt * 8.0f;
      } else {
        mob.fuse = 0.0f;
      }
    } else {
      // Fuse wind-up: swell, then explode.
      mob.fuse += dt;
      mob.scale = 1.0f + 0.5f * (mob.fuse / kFuseDuration);
      if (mob.fuse >= kFuseDuration) {
        HandleExplosion(mob.pos + XVECTOR3(0.0f, 0.5f, 0.0f, 0.0f), kExplodeRadius);
        m_renderContainer.RemoveMesh(mob.handle);
        it = m_creepers.erase(it);
        continue;
      }
    }

    if (auto* prim = m_renderContainer.GetMesh(mob.handle)) {
      prim->TranslateAbsolute(mob.pos.x, mob.pos.y, mob.pos.z);
      prim->RotateYAbsolute(mob.yaw);
      prim->ScaleAbsolute(mob.scale, mob.scale * (1.0f - 0.3f * (std::sin(mob.walkPhase) * 0.5f + 0.5f)),
                          mob.scale);
      prim->Update();
    }
  }

  m_creeperSpawnTimer -= dt;
  if (m_creeperSpawnTimer <= 0.0f) {
    m_creeperSpawnTimer = 4.0f + RandF() * 4.0f;
    SpawnCreepers(1 + static_cast<int>(RandF() * 2.0f));
  }
}

void MinecraftScene::UpdateExplosions(float dt) {
  for (auto it = m_explosions.begin(); it != m_explosions.end();) {
    it->age += dt;
    if (it->age >= it->duration) it = m_explosions.erase(it);
    else ++it;
  }
  m_damageFlash = std::max(0.0f, m_damageFlash - dt * 2.0f);
}

void MinecraftScene::HandleExplosion(const XVECTOR3& origin, float radius) {
  m_explosions.push_back(Explosion{origin, 0.0f, 0.4f, radius});

  const int r = static_cast<int>(std::ceil(radius));
  const int cx = static_cast<int>(std::floor(origin.x));
  const int cy = static_cast<int>(std::floor(origin.y));
  const int cz = static_cast<int>(std::floor(origin.z));
  for (int dz = -r; dz <= r; ++dz)
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        const float d2 = (static_cast<float>(dx) + 0.5f) * (static_cast<float>(dx) + 0.5f) +
            (static_cast<float>(dy) + 0.5f) * (static_cast<float>(dy) + 0.5f) +
            (static_cast<float>(dz) + 0.5f) * (static_cast<float>(dz) + 0.5f);
        if (d2 > radius * radius) continue;
        const int x = cx + dx, y = cy + dy, z = cz + dz;
        if (y <= 0) continue;  // never carve bedrock
        const t850::terrain::BlockId cur = m_world.GetBlock(x, y, z);
        if (cur == t850::terrain::kAirBlock) continue;
        if (!m_world.SetBlock(x, y, z, t850::terrain::kAirBlock)) continue;
        m_deltas.Record(x, y, z, t850::terrain::kAirBlock);
      }
  m_remeshRequested = true;

  // Damage the player by distance.
  const XVECTOR3 toPlayer = m_camera.Eye - origin;
  const float d = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y +
                            toPlayer.z * toPlayer.z);
  if (d < radius + 3.0f) {
    const float t = std::max(0.0f, 1.0f - d / (radius + 3.0f));
    ApplyPlayerDamage(2.0f + 8.0f * t);  // 2..10 health (1..5 hearts)
  }
}

void MinecraftScene::ApplyPlayerDamage(float amount) {
  m_health = std::max(0, m_health - static_cast<int>(std::lround(amount)));
  m_damageFlash = std::min(1.0f, m_damageFlash + 0.5f);
  if (m_health <= 0) RespawnPlayer();
}

void MinecraftScene::RespawnPlayer() {
  // Clear every creeper (they all die with the player) and the pending blasts.
  for (Mob& mob : m_creepers) m_renderContainer.RemoveMesh(mob.handle);
  m_creepers.clear();
  m_explosions.clear();
  m_health = m_maxHealth;
  m_damageFlash = 1.0f;
  m_creeperSpawnTimer = 8.0f;  // brief spawn protection
  // The FPS profile re-reads camera.Eye every frame, so resetting the camera
  // is a complete respawn.
  m_camera.Eye = XVECTOR3(static_cast<float>(m_spawnX), static_cast<float>(m_spawnY),
                          static_cast<float>(m_spawnZ), 1.0f);
  m_camera.Yaw = 0.0f;
  m_camera.Pitch = -0.03f;
  m_camera.Update(0.0f);
}

void MinecraftScene::DrawExplosions() {
  if (!pFramework || !pFramework->pVideoDriver || !t850::g_pBaseDriver) return;
  const int w = t850::g_pBaseDriver->width;
  const int h = t850::g_pBaseDriver->height;
  if (m_explosions.empty()) return;
  pFramework->pVideoDriver->SetBlendState(t850::BaseDriver::ALPHA_BLEND);
  pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::NONE);
  for (const Explosion& ex : m_explosions) {
    const float t = ex.age / ex.duration;
    XVECTOR3 clip = ex.origin;
    XVecTransform(clip, ex.origin, m_camera.VP);
    if (clip.w <= 0.0f) continue;
    const float sx = (clip.x / clip.w * 0.5f + 0.5f) * w;
    const float sy = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * h;
    const float rad = ex.radius * (0.4f + t * 1.2f) * (h / 720.0f) * 8.0f;
    char label[8];
    snprintf(label, sizeof(label), "o");
    const float s = rad * 0.06f;
    XVECTOR3 hot(1.0f, 0.75f - 0.5f * t, 0.2f - 0.15f * t, 1.0f);
    for (int i = 0; i < 14; ++i) {
      const float a = (static_cast<float>(i) / 14.0f) * 6.2831853f + t * 3.0f;
      const float rr = rad * (0.4f + 0.6f * t) * (0.5f + 0.5f * std::sin(a * 3.0f + 1.0f));
      m_debugText.DrawPixelScaled(sx + std::cos(a) * rr + 1.0f, sy + std::sin(a) * rr + 1.0f,
                                  s, s, w, h, XVECTOR3(0.1f, 0.05f, 0.0f, 1.0f), label);
      m_debugText.DrawPixelScaled(sx + std::cos(a) * rr, sy + std::sin(a) * rr, s, s, w, h, hot,
                                  label);
    }
  }
  pFramework->pVideoDriver->SetBlendState(t850::BaseDriver::BLEND_DEFAULT);
  pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
}

void MinecraftScene::DrawHealthHud() {
  if (!pFramework || !pFramework->pVideoDriver || !t850::g_pBaseDriver) return;
  const int w = t850::g_pBaseDriver->width;
  const int h = t850::g_pBaseDriver->height;
  pFramework->pVideoDriver->SetBlendState(t850::BaseDriver::ALPHA_BLEND);
  pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::NONE);
  const float s = 0.5f * ((float)h / 720.0f);
  const int fullHearts = m_health / 2;
  const bool halfHeart = (m_health % 2) != 0;
  const int hearts = (std::min)(10, (fullHearts + (halfHeart ? 1 : 0)));
  const float gap = 18.0f * s;
  const float x0 = 14.0f * s;
  const float y = (float)h - gap - 12.0f * s;
  XVECTOR3 shadow(0.0f, 0.0f, 0.0f, 1.0f);
  for (int i = 0; i < hearts; ++i) {
    const bool full = (i < fullHearts);
    const XVECTOR3 col = full ? XVECTOR3(0.9f, 0.15f, 0.15f, 1.0f)
                              : XVECTOR3(0.55f, 0.12f, 0.12f, 1.0f);
    const char* glyph = "\xe2\x99\xa5";  // heart; half hearts are drawn dimmer
    float tw = m_debugText.MeasurePixel(glyph, w, h) * s;
    float x = x0 + i * gap - tw * 0.5f + gap * 0.5f;
    m_debugText.DrawPixelScaled(x + 1.0f, y + 1.0f, s, s, w, h, shadow, glyph);
    m_debugText.DrawPixelScaled(x, y, s, s, w, h, col, glyph);
  }
  if (m_damageFlash > 0.0f) {
    char msg[32];
    snprintf(msg, sizeof(msg), "You were hurt by a creeper explosion!");
    float tw = m_debugText.MeasurePixel(msg, w, h) * s;
    XVECTOR3 red(1.0f, 0.3f, 0.3f, 1.0f);
    m_debugText.DrawPixelScaled(((float)w - tw) * 0.5f + 1.0f, 40.0f * s + 1.0f, s, s, w, h, shadow,
                                msg);
    m_debugText.DrawPixelScaled(((float)w - tw) * 0.5f, 40.0f * s, s, s, w, h, red, msg);
  }
  pFramework->pVideoDriver->SetBlendState(t850::BaseDriver::BLEND_DEFAULT);
  pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
}

void MinecraftScene::OnUpdate(float deltaSeconds) {
  m_deltaSeconds = deltaSeconds;
  if (!m_dumper.SkipCameraUpdates()) {
    m_cameraController.Update(deltaSeconds, t850::CameraUpdateContext{this});
  }
  m_dumper.UpdateReplayState();
  UpdateStreaming();
  if (m_remeshRequested) RebuildChunkMeshes();
  UpdateMobs(deltaSeconds);
  UpdateExplosions(deltaSeconds);
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

  // Hotbar selection: keys 1..9 and the mouse wheel.
  for (int slot = 0; slot < 9; ++slot) {
    if (input->PressedOnceKey(T800K_1 + slot)) m_selectedSlot = slot;
  }
  if (input->scrollDelta != 0.0f) {
    const int dir = input->scrollDelta > 0.0f ? 1 : -1;
    m_selectedSlot = ((m_selectedSlot + dir) % 9 + 9) % 9;
  }

  // Break (LMB) / place (RMB).
  if (input->PressedOnceMouseButton(0) || input->PressedOnceMouseButton(1)) {
    t850::terrain::VoxelRayHit hit;
    if (m_world.Raycast(m_camera.Eye, m_camera.Look, 6.0f, m_blockRegistry, hit)) {
      bool changed = false;
      if (input->PressedOnceMouseButton(0)) {
        // Don't break bedrock.
        if (hit.blockY > 0) {
          changed = m_world.SetBlock(hit.blockX, hit.blockY, hit.blockZ, t850::terrain::kAirBlock);
          if (changed) m_deltas.Record(hit.blockX, hit.blockY, hit.blockZ, t850::terrain::kAirBlock);
        }
      } else {
        // Only place into air / water, not into an existing solid.
        const t850::terrain::BlockId target = m_world.GetBlock(hit.previousX, hit.previousY, hit.previousZ);
        const bool empty = (target == t850::terrain::kAirBlock) ||
            (m_blockRegistry.Get(target).alphaMode == t850::MutableMeshAlphaMode::Blend);
        if (empty) {
          const t850::terrain::BlockId block = m_hotbar[m_selectedSlot];
          changed = m_world.SetBlock(hit.previousX, hit.previousY, hit.previousZ, block);
          if (changed) m_deltas.Record(hit.previousX, hit.previousY, hit.previousZ, block);
        }
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

void MinecraftScene::DrawHud() {
  if (!pFramework || !pFramework->pVideoDriver || !t850::g_pBaseDriver) return;
  const int w = t850::g_pBaseDriver->width;
  const int h = t850::g_pBaseDriver->height;
  const float s = 0.5f * ((float)h / 720.0f);

  pFramework->pVideoDriver->SetBlendState(t850::BaseDriver::ALPHA_BLEND);
  pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::NONE);

  XVECTOR3 white(1.0f, 1.0f, 1.0f, 1.0f);
  XVECTOR3 shadow(0.0f, 0.0f, 0.0f, 1.0f);

  // Crosshair.
  float cx = (float)w * 0.5f;
  float cy = (float)h * 0.5f;
  m_debugText.DrawPixelScaled(cx + 1.0f, cy + 1.0f, s, s, w, h, shadow, "+");
  m_debugText.DrawPixelScaled(cx, cy, s, s, w, h, white, "+");

  // Hotbar: 9 numbered slots across the bottom.
  const int count = (std::min)(9, static_cast<int>(m_hotbar.size()));
  const float slotW = 46.0f * s;
  const float totalW = slotW * count;
  float x = ((float)w - totalW) * 0.5f;
  const float y = (float)h - slotW - 10.0f * s;
  char label[16];
  for (int i = 0; i < count; ++i) {
    const bool selected = (i == m_selectedSlot);
    snprintf(label, sizeof(label), "%d", i + 1);
    XVECTOR3 col = selected ? XVECTOR3(1.0f, 0.9f, 0.2f, 1.0f)
                            : XVECTOR3(0.85f, 0.85f, 0.85f, 1.0f);
    float tw = m_debugText.MeasurePixel(label, w, h) * s;
    m_debugText.DrawPixelScaled(x + slotW * 0.5f - tw * 0.5f + 1.0f, y + 1.0f, s, s, w, h, shadow, label);
    m_debugText.DrawPixelScaled(x + slotW * 0.5f - tw * 0.5f, y, s, s, w, h, col, label);
    x += slotW;
  }

  // Selected block name.
  const std::string& name = m_blockRegistry.Get(m_hotbar[m_selectedSlot]).name;
  snprintf(label, sizeof(label), "  [%s]  ", name.c_str());
  float tw = m_debugText.MeasurePixel(label, w, h) * s;
  m_debugText.DrawPixelScaled(((float)w - tw) * 0.5f + 1.0f, y - 20.0f * s + 1.0f, s, s, w, h, shadow, label);
  m_debugText.DrawPixelScaled(((float)w - tw) * 0.5f, y - 20.0f * s, s, s, w, h, white, label);

  // Controls hint.
  const char* hint = "WASD move  Space jump  Shift sprint  LMB break  RMB place  1-9 / wheel select";
  tw = m_debugText.MeasurePixel(hint, w, h) * s;
  m_debugText.DrawPixelScaled(((float)w - tw) * 0.5f + 1.0f, 12.0f * s + 1.0f, s, s, w, h, shadow, hint);
  m_debugText.DrawPixelScaled(((float)w - tw) * 0.5f, 12.0f * s, s, s, w, h, XVECTOR3(0.9f, 0.9f, 0.9f, 1.0f), hint);

  pFramework->pVideoDriver->SetBlendState(t850::BaseDriver::BLEND_DEFAULT);
  pFramework->pVideoDriver->SetDepthStencilState(t850::BaseDriver::DEPTH_DEFAULT);
}

void MinecraftScene::OnDraw() {
  if (!m_assetsCreated) return;
  m_renderContainer.Execute(pFramework->pVideoDriver, m_deltaSeconds);
  DrawExplosions();
  DrawHealthHud();
  DrawHud();
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
