/*********************************************************
* MinecraftScene — a voxel world built on the T850 engine.
* Procedural chunk meshes, Perlin terrain, trees, player
* FPS controller with Jolt physics, block breaking/placing.
*********************************************************/

#include <MinecraftScene.h>
#include <SandboxRenderGraphUtils.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>
#include <utils/RuntimeProfile.h>
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include <scene/RenderMesh.h>
#include <scene/MutableMesh.h>
#include <scene/MeshAssetCache.h>
#include <scene/IBLResources.h>
#include <scene/ShadowSystem.h>
#include <video/TextureAtlas.h>
#include <core/Config.h>
#include <core/EngineContext.h>
#include <physics/CharacterController.h>
#include <utils/ResourceLocator.h>
#include <utils/ThreadPool.h>
#include <debug/RuntimeTelemetry.h>
#include <imgui/DevGuiContext.h>
#if defined(USING_VULKAN) || defined(USING_VULKAN_ONLY)
#endif

#include <array>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <string>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>
#include <functional>
#include <limits>
#include <mutex>
#include <chrono>

using namespace t850;
using std::string;

extern std::vector<std::string> g_args;

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;
}

namespace {

  // ── Block definitions ──────────────────────────────────────────────
  // Atlas: 16x16 tiles in a 256x256 texture. Tile (u,v) with u,v in [0,15].
  // Face order: 0=+X, 1=-X, 2=+Y(top), 3=-Y(bottom), 4=+Z, 5=-Z
  // ── Math helpers (local, matching engine conventions) ──────────────
  float Dot3(const XVECTOR3& a, const XVECTOR3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
  }
  XVECTOR3 Normalize3(const XVECTOR3& v, const XVECTOR3& fallback = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f)) {
    const float len = std::sqrt(Dot3(v, v));
    if (len <= 0.000001f) return fallback;
    return XVECTOR3(v.x / len, v.y / len, v.z / len, 0.0f);
  }
  float Length3(const XVECTOR3& v) { return std::sqrt(Dot3(v, v)); }

  // ── Deterministic hash-based noise (Perlin-ish) ────────────────────
  inline float Hash2D(int x, int z) {
    unsigned int h = (unsigned int)(x * 374761393 + z * 668265263) ^ 0x85ebca6bu;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFF) / 65535.0f; // [0,1]
  }

  inline float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

  float ValueNoise2D(float x, float z) {
    const int ix = (int)std::floor(x);
    const int iz = (int)std::floor(z);
    const float fx = x - ix;
    const float fz = z - iz;
    const float sx = SmoothStep(fx);
    const float sz = SmoothStep(fz);
    const float v00 = Hash2D(ix, iz);
    const float v10 = Hash2D(ix + 1, iz);
    const float v01 = Hash2D(ix, iz + 1);
    const float v11 = Hash2D(ix + 1, iz + 1);
    const float a = v00 + (v10 - v00) * sx;
    const float b = v01 + (v11 - v01) * sx;
    return a + (b - a) * sz; // [0,1]
  }

  float Fbm2D(float x, float z, int octaves, float lacunarity, float gain) {
    float amp = 1.0f;
    float freq = 1.0f;
    float sum = 0.0f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
      sum += amp * ValueNoise2D(x * freq, z * freq);
      norm += amp;
      amp *= gain;
      freq *= lacunarity;
    }
    return sum / norm; // [0,1]
  }

  float ValueNoise3D(int x, int y, int z) {
    // Simple 3D hash noise for cave carving
    unsigned int h = (unsigned int)(x * 374761393 + y * 668265263 + z * 1442695041) ^ 0x85ebca6bu;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFF) / 65535.0f;
  }

  // ── Face geometry tables ───────────────────────────────────────────
  // Each face: 4 corners (CCW when viewed from outside), normal, and
  // the UV mapping. Block-local coords in [0,1].
  struct FaceDef {
    XVECTOR3 corners[4];
    XVECTOR3 normal;
  };

  const FaceDef kFaces[6] = {
    // +X
    { { XVECTOR3(1,0,0), XVECTOR3(1,1,0), XVECTOR3(1,1,1), XVECTOR3(1,0,1) }, XVECTOR3(1,0,0,0) },
    // -X
    { { XVECTOR3(0,0,1), XVECTOR3(0,1,1), XVECTOR3(0,1,0), XVECTOR3(0,0,0) }, XVECTOR3(-1,0,0,0) },
    // +Y (top)
    { { XVECTOR3(0,1,0), XVECTOR3(0,1,1), XVECTOR3(1,1,1), XVECTOR3(1,1,0) }, XVECTOR3(0,1,0,0) },
    // -Y (bottom)
    { { XVECTOR3(0,0,1), XVECTOR3(0,0,0), XVECTOR3(1,0,0), XVECTOR3(1,0,1) }, XVECTOR3(0,-1,0,0) },
    // +Z (CCW when viewed from +Z)
    { { XVECTOR3(0,0,1), XVECTOR3(1,0,1), XVECTOR3(1,1,1), XVECTOR3(0,1,1) }, XVECTOR3(0,0,1,0) },
    // -Z (CCW when viewed from -Z)
    { { XVECTOR3(1,0,0), XVECTOR3(0,0,0), XVECTOR3(0,1,0), XVECTOR3(1,1,0) }, XVECTOR3(0,0,-1,0) },
  };

  // UV corners for a face (u0,v0) bottom-left .. (u1,v1) top-right
  struct UVQuad { float u0, v0, u1, v1; };

  // Half-texel inset: with NEAREST sampling this keeps face UVs strictly
  // inside their tile, so atlas bleed is impossible even if a backend
  // rounds a coordinate onto the tile boundary.
  UVQuad TileUV(const t850::TextureAtlas& atlas, int tileU, int tileV) {
    t850::TextureAtlasRegion region;
    if (!atlas.TryGetGridRegion(tileU, tileV, region)) return {};
    return { region.u0, region.v0, region.u1, region.v1 };
  }

} // namespace

bool MinecraftScene::LoadAuthoredScene() {
  std::string error;
  if (!t850::scene::LoadEditorSceneFile(m_sceneFilePath, m_sceneFile, &error)) {
    T8_LOG_ERROR("[Minecraft] Failed to load authored scene '%s': %s",
                 m_sceneFilePath.c_str(), error.c_str());
    return false;
  }
  if (!m_sceneFile.voxel_world.has_value()) {
    T8_LOG_ERROR("[Minecraft] Scene '%s' has no voxel_world component", m_sceneFilePath.c_str());
    return false;
  }
  m_voxelSettings = *m_sceneFile.voxel_world;
  if (m_voxelSettings.chunk_size < 1 || m_voxelSettings.chunk_size > kMaxChunkSize ||
      m_voxelSettings.world_height < 8 || m_voxelSettings.world_height > kMaxWorldHeight ||
      m_voxelSettings.render_distance < 1 || m_voxelSettings.render_distance > kMaxRenderDistance ||
      m_voxelSettings.streaming_recenter_threshold < 0 ||
      m_voxelSettings.streaming_recenter_threshold >= m_voxelSettings.render_distance ||
      m_voxelSettings.water_level < 1 || m_voxelSettings.water_level >= m_voxelSettings.world_height - 1 ||
      m_voxelSettings.atlas_size < 16 || m_voxelSettings.atlas_tiles_per_axis < 1 ||
      m_voxelSettings.atlas_size % m_voxelSettings.atlas_tiles_per_axis != 0 ||
      m_voxelSettings.atlas_tile_px < 1 || m_voxelSettings.atlas_pixelation_factor < 1 ||
      m_voxelSettings.atlas_pixelation_factor > 16 ||
      m_voxelSettings.cascade_debug_colors.size() != 6 ||
      m_voxelSettings.debug_render_targets.empty() ||
      !m_voxelSettings.debug_render_targets[0].source.empty() ||
      m_voxelSettings.player.look_pitch_limit <= 0.0f ||
      m_voxelSettings.player.collision_sweep_step <= 0.0f ||
      m_voxelSettings.mob.vertical_follow_speed <= 0.0f ||
      m_voxelSettings.navmesh_rebuild_seconds <= 0.0f ||
      m_voxelSettings.sun_debug_size <= 0.0f ||
      m_voxelSettings.dof.focus_range < 0.0f ||
      m_voxelSettings.dof.focus_falloff <= 0.0f ||
      m_voxelSettings.dof.auto_focus_radius < 0.0f ||
      m_voxelSettings.dof.auto_focus_radius > 0.5f ||
      m_voxelSettings.day_night.day_length_seconds <= 0.0f ||
      m_voxelSettings.day_night.animation_speed < 0.0f ||
      m_voxelSettings.day_night.animation_speed > 10.0f ||
      m_voxelSettings.day_night.orbit_radius < 0.0f ||
      m_voxelSettings.day_night.horizon_offset <= -1.0f) {
    T8_LOG_ERROR("[Minecraft] Invalid voxel_world dimensions in '%s'", m_sceneFilePath.c_str());
    return false;
  }
  ApplyVoxelSettings();
  if (m_blockDefs.empty()) return false;
  return true;
}

void MinecraftScene::ApplyVoxelSettings() {
  m_chunkSize = m_voxelSettings.chunk_size;
  m_worldHeight = m_voxelSettings.world_height;
  m_waterLevel = m_voxelSettings.water_level;
  m_renderDistance = m_voxelSettings.render_distance;
  m_streamingRecenterThreshold = (std::min)(
    m_voxelSettings.streaming_recenter_threshold, m_renderDistance - 1);
  m_chunkCountX = kMaxChunkCount;
  m_chunkCountZ = m_chunkCountX;
  m_maxChunks = m_chunkCountX * m_chunkCountZ;
  m_mobMeshIndex = m_maxChunks;
  m_weaponMeshIndex = m_maxChunks + 1;
  m_renderMeshCount = m_maxChunks + 2;
  m_seed = m_voxelSettings.seed;
  m_maxUploadsPerFrame = (std::max)(1, m_voxelSettings.max_uploads_per_frame);
  m_asyncStreaming = m_voxelSettings.async_streaming;
  m_atlasSize = m_voxelSettings.atlas_size;
  m_atlasTiles = m_voxelSettings.atlas_tiles_per_axis;
  m_atlasTexturePath = m_voxelSettings.atlas_texture;
  m_atlasTilePx = m_voxelSettings.atlas_tile_px;
  m_atlasPixelationFactor = m_voxelSettings.atlas_pixelation_factor;
  m_currentCubemapPath = m_voxelSettings.environment_map;
  m_showPhysics = m_voxelSettings.show_physics;
  m_showChunkBounds = m_voxelSettings.show_chunk_bounds;
  m_showLights = m_voxelSettings.show_lights;
  m_showNavMesh = m_voxelSettings.show_navmesh;
  SceneProp.FrustumCullingToggleAllowed =
    g_config.cullingLoadMode != t850::Config::CullingLoadMode::Disabled;
  SceneProp.FrustumCullingEnabled =
    SceneProp.FrustumCullingToggleAllowed && m_voxelSettings.frustum_culling;
  SceneProp.ShowCullingDebug = m_voxelSettings.show_culling_debug;
  m_showCascadeFrustums = m_voxelSettings.show_cascade_debug;
  m_cascadeDebugMode = (std::max)(0, (std::min)(2, m_voxelSettings.cascade_debug_mode));
  m_cascadeDebugOpacity = (std::max)(0.01f, (std::min)(0.75f, m_voxelSettings.cascade_debug_opacity));
  for (std::size_t i = 0; i < m_cascadeDebugColors.size(); ++i) {
    const auto& color = m_voxelSettings.cascade_debug_colors[i];
    m_cascadeDebugColors[i] = XVECTOR3(color.x, color.y, color.z, 1.0f);
    SceneProp.CascadeDebugColors[i] = m_cascadeDebugColors[i];
  }
  m_cameraMode = (std::max)(0, (std::min)(2, m_voxelSettings.camera_mode));
  m_debugCascadeIndex = (std::max)(0, m_voxelSettings.debug_cascade_index);
  m_debugRTSelection = (std::max)(0, (std::min)(
    (int)m_voxelSettings.debug_render_targets.size() - 1,
    m_voxelSettings.debug_render_target));
  m_timeOfDay = m_voxelSettings.day_night.time_of_day;
  m_dayLengthSecs = (std::max)(1.0f, m_voxelSettings.day_night.day_length_seconds);
  m_dayNightEnabled = m_voxelSettings.day_night.enabled;
  m_sunTrajectoryPaused = m_voxelSettings.day_night.trajectory_paused;
  SceneProp.DOFNormalizedFocus = m_voxelSettings.dof.normalized_focus;
  SceneProp.DOFFocusRange = m_voxelSettings.dof.focus_range;
  SceneProp.DOFFocusFalloff = m_voxelSettings.dof.focus_falloff;
  SceneProp.DOFAutoFocusRadius = m_voxelSettings.dof.auto_focus_radius;
  m_mouseSensitivity = (std::max)(0.00001f, m_voxelSettings.player.mouse_sensitivity);
  m_debugCameraSpeed = (std::max)(0.1f, m_voxelSettings.player.debug_camera_speed);
  m_mob.position = XVECTOR3(m_voxelSettings.mob.spawn.x,
                            m_voxelSettings.mob.spawn.y,
                            m_voxelSettings.mob.spawn.z, 1.0f);

  m_blockDefs.clear();
  m_blockDefs.reserve(m_voxelSettings.blocks.size());
  for (const auto& source : m_voxelSettings.blocks) {
    BlockDef block;
    block.name = source.name;
    block.opaque = source.opaque;
    block.solid = source.solid;
    for (int face = 0; face < 6; ++face) {
      block.tiles[face] = {source.tiles[face * 2], source.tiles[face * 2 + 1]};
    }
    for (int component = 0; component < 4; ++component) {
      block.color[component] = static_cast<unsigned char>(
        (std::max)(0, (std::min)(255, source.color[component])));
    }
    m_blockDefs.push_back(std::move(block));
  }
  if (m_blockDefs.empty() || m_blockDefs.size() > 256) {
    T8_LOG_ERROR("[Minecraft] voxel_world.blocks must contain 1..256 entries");
    return;
  }
  m_blockIds.clear();
  for (std::size_t index = 0; index < m_blockDefs.size(); ++index)
    m_blockIds[m_blockDefs[index].name] = static_cast<uint8_t>(index);
  m_hotbar.clear();
  for (const std::string& name : m_voxelSettings.hotbar) {
    auto found = m_blockIds.find(name);
    if (found != m_blockIds.end()) m_hotbar.push_back(found->second);
  }
  if (!m_hotbar.empty()) m_selectedBlock = m_hotbar.front();
}

uint8_t MinecraftScene::BlockId(const std::string& name, uint8_t fallback) const {
  const auto found = m_blockIds.find(name);
  return found != m_blockIds.end() ? found->second : fallback;
}

int MinecraftScene::WorldToChunk(int wx) const {
  return (int)std::floor((float)wx / (float)m_chunkSize);
}

int MinecraftScene::WorldToLocal(int wx) const {
  int local = wx % m_chunkSize;
  if (local < 0) local += m_chunkSize;
  return local;
}

// ── Chunk index (relative to current center) ────────────────────────
int MinecraftScene::ChunkIndex(int cx, int cz) const {
  if (cx < m_centerChunkX - m_renderDistance || cx > m_centerChunkX + m_renderDistance ||
      cz < m_centerChunkZ - m_renderDistance || cz > m_centerChunkZ + m_renderDistance) return -1;
  const int gx = ((cx % m_chunkCountX) + m_chunkCountX) % m_chunkCountX;
  const int gz = ((cz % m_chunkCountZ) + m_chunkCountZ) % m_chunkCountZ;
  return gz * m_chunkCountX + gx;
}

// ── Noise accessors (member, use seed) ───────────────────────────────
float MinecraftScene::Noise2D(float x, float z) const {
  const auto& terrain = m_voxelSettings.terrain;
  return Fbm2D(x * terrain.base_frequency + m_seed * 0.001f,
               z * terrain.base_frequency + m_seed * 0.001f,
               terrain.base_octaves, terrain.base_lacunarity, terrain.base_gain);
}

float MinecraftScene::Noise3D(float x, float y, float z) const {
  return ValueNoise3D((int)x, (int)y, (int)z);
}

int MinecraftScene::HeightAt(int wx, int wz) const {
  const auto& terrain = m_voxelSettings.terrain;
  const float n = Noise2D((float)wx, (float)wz);
  const int base = terrain.base_height + (int)(n * terrain.base_amplitude);
  const float mountainNoise = Fbm2D(
    (float)wx * terrain.mountain_frequency + m_seed * 0.002f,
    (float)wz * terrain.mountain_frequency + m_seed * 0.002f,
    terrain.mountain_octaves, terrain.mountain_lacunarity, terrain.mountain_gain);
  return (std::min)(m_worldHeight - 2,
    base + (int)(mountainNoise * mountainNoise * terrain.mountain_amplitude));
}

// ── Block access ─────────────────────────────────────────────────────
uint8_t MinecraftScene::GetBlock(int wx, int wy, int wz) const {
  if (wy < 0 || wy >= m_worldHeight) return BlockId("air", 0);
  const int cx = WorldToChunk(wx);
  const int cz = WorldToChunk(wz);
  const int idx = ChunkIndex(cx, cz);
  if (idx < 0) return BlockId("air", 0);
  const int lx = WorldToLocal(wx);
  const int lz = WorldToLocal(wz);
  return m_blocks[idx % m_chunkCountX][wy][idx / m_chunkCountX][lx][lz];
}

void MinecraftScene::SetBlock(int wx, int wy, int wz, uint8_t block) {
  if (wy < 0 || wy >= m_worldHeight) return;
  const int cx = WorldToChunk(wx);
  const int cz = WorldToChunk(wz);
  const int idx = ChunkIndex(cx, cz);
  if (idx < 0) return;
  const int lx = WorldToLocal(wx);
  const int lz = WorldToLocal(wz);
  m_blocks[idx % m_chunkCountX][wy][idx / m_chunkCountX][lx][lz] = block;
  // The world changed, so the navmesh is stale. Rebuild it (throttled in
  // OnUpdate) so enemies re-path around the new/removed block.
  m_navMeshDirty = true;
  m_navMeshRebuildTimer = 0.0f;
  QueueChunkRemesh(cx, cz);
  if (lx == 0) QueueChunkRemesh(cx - 1, cz);
  if (lx == m_chunkSize - 1) QueueChunkRemesh(cx + 1, cz);
  if (lz == 0) QueueChunkRemesh(cx, cz - 1);
  if (lz == m_chunkSize - 1) QueueChunkRemesh(cx, cz + 1);
}

bool MinecraftScene::IsBlockOpaque(uint8_t block) const {
  if (block >= m_blockDefs.size()) return false;
  return m_blockDefs[block].opaque;
}

bool MinecraftScene::IsBlockSolid(uint8_t block) const {
  if (block >= m_blockDefs.size()) return false;
  return m_blockDefs[block].solid;
}

// Simple AABB box-vs-voxel collision. All world geometry is axis-aligned
// boxes, so a box-to-box overlap test against the solid blocks is enough.
bool MinecraftScene::MobBoxCollides(float minX, float minY, float minZ,
                                    float maxX, float maxY, float maxZ) const {
  const int bx0 = (int)std::floor(minX);
  const int bx1 = (int)std::floor(maxX);
  const int by0 = (int)std::floor(minY);
  const int by1 = (int)std::floor(maxY);
  const int bz0 = (int)std::floor(minZ);
  const int bz1 = (int)std::floor(maxZ);
  for (int by = by0; by <= by1; ++by) {
    for (int bz = bz0; bz <= bz1; ++bz) {
      for (int bx = bx0; bx <= bx1; ++bx) {
        if (IsBlockSolid(GetBlock(bx, by, bz))) return true;
      }
    }
  }
  return false;
}

// ── World generation ─────────────────────────────────────────────────
void MinecraftScene::GenerateChunkData(
    int cx, int cz, std::vector<uint8_t>& blocks) const {
  const int baseX = cx * m_chunkSize;
  const int baseZ = cz * m_chunkSize;
  const auto& terrain = m_voxelSettings.terrain;
  const uint8_t air = BlockId(terrain.air_block, 0);
  const uint8_t bedrock = BlockId(terrain.bedrock_block, air);
  const uint8_t stone = BlockId(terrain.stone_block, air);
  const uint8_t dirt = BlockId(terrain.dirt_block, stone);
  const uint8_t grass = BlockId(terrain.grass_block, dirt);
  const uint8_t sand = BlockId(terrain.sand_block, dirt);
  const uint8_t water = BlockId(terrain.water_block, air);
  blocks.assign(static_cast<std::size_t>(m_worldHeight) * m_chunkSize * m_chunkSize, air);
  auto blockAt = [&](int wy, int lx, int lz) -> uint8_t& {
    return blocks[(static_cast<std::size_t>(wy) * m_chunkSize + lx) * m_chunkSize + lz];
  };

  for (int lx = 0; lx < m_chunkSize; ++lx) {
    for (int lz = 0; lz < m_chunkSize; ++lz) {
      const int wx = baseX + lx;
      const int wz = baseZ + lz;
      const int height = HeightAt(wx, wz);

      for (int wy = 0; wy < m_worldHeight; ++wy) {
        uint8_t block = air;
        if (wy == 0) {
          block = bedrock;
        } else if (wy < height - terrain.surface_depth) {
          block = stone;
        } else if (wy < height) {
          block = dirt;
        } else if (wy == height) {
          block = (height <= m_waterLevel) ? sand : grass;
        }

        if (block != air && wy > terrain.cave_min_y && wy < height - 1) {
          const float cave = Noise3D(wx * terrain.cave_frequency,
                                     wy * terrain.cave_frequency,
                                     wz * terrain.cave_frequency);
          if (cave > terrain.cave_threshold) block = air;
        }

        if (block == stone) {
          const float oreNoise = Hash2D(wx * 31 + wy * 17, wz * 31 + wy * 13);
          const int depthBelowSurface = height - wy;
          for (const auto& ore : terrain.ores) {
            if (oreNoise > ore.threshold && depthBelowSurface > ore.min_depth) {
              block = BlockId(ore.block, stone);
              break;
            }
          }
        }

        blockAt(wy, lx, lz) = block;
      }

      // Fill water in low areas up to the water level
      if (height < m_waterLevel) {
        for (int wy = height + 1; wy <= m_waterLevel; ++wy) {
          if (wy < m_worldHeight) {
            blockAt(wy, lx, lz) = water;
          }
        }
      }

    }
  }
}

void MinecraftScene::GenerateChunk(int cx, int cz, bool markState) {
  const int idx = ChunkIndex(cx, cz);
  if (idx < 0) return;
  const int gx = idx % m_chunkCountX;
  const int gz = idx / m_chunkCountX;
  std::vector<uint8_t> blocks;
  GenerateChunkData(cx, cz, blocks);
  for (int wy = 0; wy < m_worldHeight; ++wy) {
    for (int lx = 0; lx < m_chunkSize; ++lx) {
      for (int lz = 0; lz < m_chunkSize; ++lz) {
        const std::size_t source =
          (static_cast<std::size_t>(wy) * m_chunkSize + lx) * m_chunkSize + lz;
        m_blocks[gx][wy][gz][lx][lz] = blocks[source];
      }
    }
  }
  if (markState) {
    m_chunkBuilt[gz][gx] = true;
    m_chunkDirty[gz][gx] = true;
  }
}

void MinecraftScene::GenerateChunkTrees(int cx, int cz, bool markState) {
  const int idx = ChunkIndex(cx, cz);
  if (idx < 0) return;
  const int gx = idx % m_chunkCountX;
  const int gz = idx / m_chunkCountX;
  const int baseX = cx * m_chunkSize;
  const int baseZ = cz * m_chunkSize;
  const auto& terrain = m_voxelSettings.terrain;
  const uint8_t air = BlockId(terrain.air_block, 0);
  const uint8_t stone = BlockId(terrain.stone_block, air);
  const uint8_t grass = BlockId(terrain.grass_block, stone);
  const uint8_t log = BlockId(terrain.log_block, stone);
  const uint8_t leaves = BlockId(terrain.leaves_block, grass);
  bool changed = false;

  auto writeBlock = [&](int wx, int wy, int wz, uint8_t block, bool requireAir) {
    if (wy < 0 || wy >= m_worldHeight ||
        WorldToChunk(wx) != cx || WorldToChunk(wz) != cz) return;
    uint8_t& target = m_blocks[gx][wy][gz][WorldToLocal(wx)][WorldToLocal(wz)];
    if ((requireAir && target != air) || target == block) return;
    target = block;
    changed = true;
  };

  // Include tree centers just outside this chunk so their canopy is generated
  // symmetrically inside this chunk regardless of chunk traversal order.
  constexpr int kCanopyRadius = 2;
  for (int wx = baseX - kCanopyRadius; wx < baseX + m_chunkSize + kCanopyRadius; ++wx) {
    for (int wz = baseZ - kCanopyRadius; wz < baseZ + m_chunkSize + kCanopyRadius; ++wz) {
      const int height = HeightAt(wx, wz);
      if (height <= m_waterLevel || height >= terrain.tree_max_surface_height) continue;
      if (Hash2D(wx * 7 + 13, wz * 7 + 29) <= terrain.tree_threshold) continue;

      const int treeHeight = terrain.tree_min_height +
        (int)(Hash2D(wx * 3, wz * 3) * terrain.tree_height_variation);
      for (int t = 1; t <= treeHeight; ++t)
        writeBlock(wx, height + t, wz, log, false);

      const int topY = height + treeHeight;
      for (int dy = -2; dy <= 1; ++dy) {
        const int radius = (dy >= 0) ? 1 : 2;
        for (int dx = -radius; dx <= radius; ++dx) {
          for (int dz = -radius; dz <= radius; ++dz) {
            if (dx == 0 && dz == 0 && dy < 0) continue;
            writeBlock(wx + dx, topY + dy, wz + dz, leaves, true);
          }
        }
      }
    }
  }

  if (changed && markState) m_chunkDirty[gz][gx] = true;
}

void MinecraftScene::GenerateWorld() {
  std::memset(m_blocks, 0, sizeof(m_blocks));
  std::memset(m_chunkDirty, 0, sizeof(m_chunkDirty));
  std::memset(m_chunkBuilt, 0, sizeof(m_chunkBuilt));
  m_navMeshReady = false;
  m_mob.pathReady = false;
  for (int cz = -m_renderDistance; cz <= m_renderDistance; ++cz) {
    for (int cx = -m_renderDistance; cx <= m_renderDistance; ++cx) {
      GenerateChunk(cx, cz);
    }
  }
  for (int cz = -m_renderDistance; cz <= m_renderDistance; ++cz) {
    for (int cx = -m_renderDistance; cx <= m_renderDistance; ++cx) {
      GenerateChunkTrees(cx, cz);
    }
  }
  // Count blocks for verification
  int waterCount = 0, coalCount = 0, ironCount = 0, goldCount = 0, diamondCount = 0;
  for (int cz = 0; cz < m_chunkCountZ; ++cz)
    for (int cx = 0; cx < m_chunkCountX; ++cx)
      for (int wy = 0; wy < m_worldHeight; ++wy)
        for (int lz = 0; lz < m_chunkSize; ++lz)
          for (int lx = 0; lx < m_chunkSize; ++lx) {
            const uint8_t b = m_blocks[cx][wy][cz][lx][lz];
            if (b == BlockId("water", 0)) ++waterCount;
            else if (b == BlockId("coal_ore", 0)) ++coalCount;
            else if (b == BlockId("iron_ore", 0)) ++ironCount;
            else if (b == BlockId("gold_ore", 0)) ++goldCount;
            else if (b == BlockId("diamond_ore", 0)) ++diamondCount;
          }
  T8_LOG_INFO("[Minecraft] World generated: water=%d coal=%d iron=%d gold=%d diamond=%d",
              waterCount, coalCount, ironCount, goldCount, diamondCount);
}

void MinecraftScene::BuildNavigationMesh() {
  std::vector<uint8_t> blockSnapshot(sizeof(m_blocks));
  std::memcpy(blockSnapshot.data(), m_blocks, sizeof(m_blocks));
  const int centerChunkX = m_centerChunkX;
  const int centerChunkZ = m_centerChunkZ;
  t850::navigation::NavMeshGeometry geometry;
  BuildNavigationGeometry(blockSnapshot, centerChunkX, centerChunkZ, geometry);

  m_navMeshSettings = t850::navigation::NavMeshBuildSettings{};
  if (m_sceneFile.navigation_mesh) {
    const auto& authored = m_sceneFile.navigation_mesh->build_settings;
    m_navMeshSettings.cellSize = authored.cell_size;
    m_navMeshSettings.cellHeight = authored.cell_height;
    m_navMeshSettings.agentHeight = authored.agent_height;
    m_navMeshSettings.agentRadius = authored.agent_radius;
    m_navMeshSettings.agentMaxClimb = authored.agent_max_climb;
    m_navMeshSettings.agentMaxSlope = authored.agent_max_slope;
    m_navMeshSettings.regionMinSize = authored.region_min_size;
    m_navMeshSettings.regionMergeSize = authored.region_merge_size;
    m_navMeshSettings.edgeMaxLen = authored.edge_max_len;
    m_navMeshSettings.edgeMaxError = authored.edge_max_error;
    m_navMeshSettings.vertsPerPoly = authored.verts_per_poly;
    m_navMeshSettings.detailSampleDist = authored.detail_sample_dist;
    m_navMeshSettings.detailSampleMaxError = authored.detail_sample_max_error;
    m_navMeshSettings.queryExtents = XVECTOR3(authored.query_extents.x,
                                               authored.query_extents.y,
                                               authored.query_extents.z, 0.0f);
  }

  const auto buildStart = std::chrono::steady_clock::now();
  std::string error;
  m_navMesh.Clear();
  m_navMeshReady = !geometry.indices.empty() && m_navMesh.Build(geometry, m_navMeshSettings, &error);
  m_navMeshDebugRenderer.Invalidate();
  m_navMeshBuildMs = std::chrono::duration<float, std::milli>(
      std::chrono::steady_clock::now() - buildStart).count();
  if (!m_navMeshReady) {
    T8_LOG_ERROR("[Minecraft] NavMesh build failed: %s", error.c_str());
    return;
  }

  const t850::navigation::NavMeshBuildStats& stats = m_navMesh.GetStats();
  T8_LOG_INFO("[Minecraft] NavMesh ready: %.2fms verts=%d tris=%d polys=%d",
              m_navMeshBuildMs, stats.vertexCount, stats.triangleCount, stats.polygonCount);
  m_navMeshCenterChunkX = centerChunkX;
  m_navMeshCenterChunkZ = centerChunkZ;
  m_mob.repathTimer = 0.0f;
  m_mob.pathReady = false;
}

void MinecraftScene::BuildNavigationGeometry(
    const std::vector<uint8_t>& blockSnapshot,
    int centerChunkX, int centerChunkZ,
    t850::navigation::NavMeshGeometry& geometry) const {
  if (blockSnapshot.size() != sizeof(m_blocks)) return;
  using BlockStorage = uint8_t[kMaxChunkCount][kMaxWorldHeight]
                              [kMaxChunkCount][kMaxChunkSize][kMaxChunkSize];
  const auto& blocks = *reinterpret_cast<const BlockStorage*>(blockSnapshot.data());
  const int worldMinX = (centerChunkX - m_renderDistance) * m_chunkSize;
  const int worldMaxX = (centerChunkX + m_renderDistance + 1) * m_chunkSize;
  const int worldMinZ = (centerChunkZ - m_renderDistance) * m_chunkSize;
  const int worldMaxZ = (centerChunkZ + m_renderDistance + 1) * m_chunkSize;
  geometry.vertices.reserve(static_cast<std::size_t>(
    (worldMaxX - worldMinX) * (worldMaxZ - worldMinZ) * 4));
  geometry.indices.reserve(static_cast<std::size_t>(
    (worldMaxX - worldMinX) * (worldMaxZ - worldMinZ) * 6));
  const uint8_t leaves = BlockId(m_voxelSettings.terrain.leaves_block, 0);
  const uint8_t logs = BlockId(m_voxelSettings.terrain.log_block, 0);

  for (int wz = worldMinZ; wz < worldMaxZ; ++wz) {
    for (int wx = worldMinX; wx < worldMaxX; ++wx) {
      const int chunkX = (int)std::floor((float)wx / (float)m_chunkSize);
      const int chunkZ = (int)std::floor((float)wz / (float)m_chunkSize);
      const int gx = ((chunkX % m_chunkCountX) + m_chunkCountX) % m_chunkCountX;
      const int gz = ((chunkZ % m_chunkCountZ) + m_chunkCountZ) % m_chunkCountZ;
      int localX = wx % m_chunkSize;
      int localZ = wz % m_chunkSize;
      if (localX < 0) localX += m_chunkSize;
      if (localZ < 0) localZ += m_chunkSize;
      int groundY = -1;
      for (int wy = m_worldHeight - 1; wy >= 0; --wy) {
        const uint8_t block = blocks[gx][wy][gz][localX][localZ];
        if (IsBlockSolid(block) && block != leaves && block != logs) {
          groundY = wy;
          break;
        }
      }
      if (groundY < 0) continue;

      const int base = static_cast<int>(geometry.vertices.size());
      const float y = static_cast<float>(groundY + 1);
      geometry.vertices.emplace_back(static_cast<float>(wx), y, static_cast<float>(wz));
      geometry.vertices.emplace_back(static_cast<float>(wx), y, static_cast<float>(wz + 1));
      geometry.vertices.emplace_back(static_cast<float>(wx + 1), y, static_cast<float>(wz + 1));
      geometry.vertices.emplace_back(static_cast<float>(wx + 1), y, static_cast<float>(wz));
      geometry.indices.push_back(base + 0);
      geometry.indices.push_back(base + 1);
      geometry.indices.push_back(base + 2);
      geometry.indices.push_back(base + 0);
      geometry.indices.push_back(base + 2);
      geometry.indices.push_back(base + 3);
    }
  }
}

void MinecraftScene::StartNavigationMeshBuild() {
  if (m_navMeshBuildFuture.valid()) return;
  if (!t850::g_threadPool || t850::g_threadPool->NumWorkers() <= 0) {
    BuildNavigationMesh();
    return;
  }

  std::vector<uint8_t> blockSnapshot(sizeof(m_blocks));
  std::memcpy(blockSnapshot.data(), m_blocks, sizeof(m_blocks));
  const int centerChunkX = m_centerChunkX;
  const int centerChunkZ = m_centerChunkZ;
  const t850::navigation::NavMeshBuildSettings settings = m_navMeshSettings;
  m_pendingNavMeshBuild = std::make_shared<PendingNavMeshBuild>();
  const auto pending = m_pendingNavMeshBuild;
  pending->centerChunkX = centerChunkX;
  pending->centerChunkZ = centerChunkZ;
  m_navMeshBuildFuture = t850::g_threadPool->Submit(
    [this, blockSnapshot = std::move(blockSnapshot), centerChunkX, centerChunkZ,
     settings, pending]() {
      t850::navigation::NavMeshGeometry geometry;
      BuildNavigationGeometry(blockSnapshot, centerChunkX, centerChunkZ, geometry);
      const auto start = std::chrono::steady_clock::now();
      pending->success = !geometry.indices.empty() &&
        pending->navMesh.Build(geometry, settings, &pending->error);
      pending->buildMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    });
}

void MinecraftScene::ProcessNavigationMeshBuild() {
  if (!m_navMeshBuildFuture.valid() ||
      m_navMeshBuildFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return;

  try {
    m_navMeshBuildFuture.get();
    const bool staleCenter = m_pendingNavMeshBuild &&
      (m_pendingNavMeshBuild->centerChunkX != m_centerChunkX ||
       m_pendingNavMeshBuild->centerChunkZ != m_centerChunkZ);
    if (staleCenter) {
      m_navMeshDirty = true;
      m_navMeshRebuildTimer = m_voxelSettings.navmesh_rebuild_seconds;
    } else if (m_pendingNavMeshBuild && m_pendingNavMeshBuild->success) {
      m_navMesh = std::move(m_pendingNavMeshBuild->navMesh);
      m_navMeshReady = true;
      m_navMeshBuildMs = m_pendingNavMeshBuild->buildMs;
      m_navMeshDebugRenderer.Invalidate();
      const auto& stats = m_navMesh.GetStats();
      T8_LOG_INFO("[Minecraft] Background NavMesh ready: %.2fms verts=%d tris=%d polys=%d",
                  m_navMeshBuildMs, stats.vertexCount, stats.triangleCount, stats.polygonCount);
      m_navMeshCenterChunkX = m_pendingNavMeshBuild->centerChunkX;
      m_navMeshCenterChunkZ = m_pendingNavMeshBuild->centerChunkZ;
      m_mob.repathTimer = 0.0f;
      m_mob.pathReady = false;
    } else if (m_pendingNavMeshBuild) {
      T8_LOG_ERROR("[Minecraft] Background NavMesh build failed: %s",
                   m_pendingNavMeshBuild->error.c_str());
    }
  } catch (const std::exception& error) {
    T8_LOG_ERROR("[Minecraft] Background NavMesh worker failed: %s", error.what());
  }
  m_pendingNavMeshBuild.reset();
}

void MinecraftScene::UpdateMob(float dt) {
  if (!m_navMeshReady) return;

  if (m_navMeshCenterChunkX != m_centerChunkX ||
      m_navMeshCenterChunkZ != m_centerChunkZ) {
    m_mob.path.clear();
    m_mob.pathCursor = 0;
    m_mob.pathReady = false;
    m_mob.repathTimer = 0.0f;
    return;
  }

  m_mob.repathTimer -= dt;
  if (m_mob.repathTimer <= 0.0f) {
    const XVECTOR3 playerPosition = m_player.GetPosition();
    XVECTOR3 projectedStart;
    if (!m_navMesh.ProjectPoint(m_mob.position, projectedStart,
                                m_navMeshSettings.queryExtents, nullptr)) {
      const XVECTOR3 towardPreviousPosition(
        m_mob.position.x - playerPosition.x, 0.0f,
        m_mob.position.z - playerPosition.z, 0.0f);
      const XVECTOR3 spawnDirection = Normalize3(
        towardPreviousPosition, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
      const float recoveryDistance = (std::max)(4.0f, m_chunkSize * 0.5f);
      const XVECTOR3 recoveryCandidate(
        playerPosition.x + spawnDirection.x * recoveryDistance,
        playerPosition.y,
        playerPosition.z + spawnDirection.z * recoveryDistance, 1.0f);
      if (!m_navMesh.ProjectPoint(recoveryCandidate, projectedStart,
                                  m_navMeshSettings.queryExtents, nullptr) &&
          !m_navMesh.ProjectPoint(playerPosition, projectedStart,
                                  m_navMeshSettings.queryExtents, nullptr)) {
        m_mob.path.clear();
        m_mob.pathCursor = 0;
        m_mob.pathReady = false;
        m_mob.repathTimer = m_voxelSettings.mob.repath_seconds;
        return;
      }
      m_mob.position = projectedStart;
      UpdateMobInstance();
      T8_LOG_INFO("[Minecraft] Rehomed mob onto streamed navmesh at (%.1f, %.1f, %.1f)",
                  m_mob.position.x, m_mob.position.y, m_mob.position.z);
    }

    XVECTOR3 projectedEnd;
    if (!m_navMesh.ProjectPoint(playerPosition, projectedEnd,
                                m_navMeshSettings.queryExtents, nullptr)) {
      m_mob.path.clear();
      m_mob.pathCursor = 0;
      m_mob.pathReady = false;
      m_mob.repathTimer = m_voxelSettings.mob.repath_seconds;
      return;
    }

    t850::navigation::NavPathRequest request;
    request.start = projectedStart;
    request.end = projectedEnd;
    request.queryExtents = m_navMeshSettings.queryExtents;
    const t850::navigation::NavPathResult result = m_navMesh.FindPath(request);
    m_mob.path = result.points;
    m_mob.pathCursor = m_mob.path.size() > 1 ? 1 : 0;
    m_mob.pathReady = result.success && m_mob.path.size() > 1;
    m_mob.repathTimer = m_voxelSettings.mob.repath_seconds;
    if (!m_mob.pathReady && !result.error.empty()) {
      T8_LOG_VERBOSE("[Minecraft] Mob path unavailable: %s", result.error.c_str());
    }
  }

  if (m_mob.pathReady && m_mob.pathCursor < m_mob.path.size()) {
    const XVECTOR3 target = m_mob.path[m_mob.pathCursor];
    const XVECTOR3 delta(target.x - m_mob.position.x, 0.0f, target.z - m_mob.position.z, 0.0f);
    const float distance = Length3(delta);
    if (distance < m_voxelSettings.mob.waypoint_distance) {
      ++m_mob.pathCursor;
    } else {
      const float step = (std::min)(distance, m_voxelSettings.mob.move_speed * dt);
      const XVECTOR3 direction = Normalize3(delta, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));

      // Simple AABB collision for the mob. m_mob.position is the FEET
      // position (ground surface Y from the navmesh). The mob's body is a
      // box ~0.5 wide and ~1.4 tall, centered on the feet.
      const float halfW = m_voxelSettings.mob.half_width;
      const float height = m_voxelSettings.mob.height;
      auto boxFree = [&](float px, float pz) -> bool {
        return !MobBoxCollides(px - halfW, m_mob.position.y, pz - halfW,
                               px + halfW, m_mob.position.y + height, pz + halfW);
      };

      // Move along X and Z separately so the mob slides around obstacles.
      const float mx = direction.x * step;
      const float mz = direction.z * step;
      if (boxFree(m_mob.position.x + mx, m_mob.position.z)) {
        m_mob.position.x += mx;
      }
      if (boxFree(m_mob.position.x, m_mob.position.z + mz)) {
        m_mob.position.z += mz;
      }

      m_mob.position.y += (target.y - m_mob.position.y) *
              (std::min)(1.0f, dt * m_voxelSettings.mob.vertical_follow_speed);
    }
  }
  UpdateMobInstance();

  // Periodic mob position log (every ~2 seconds)
  static float s_mobLogTimer = 0.0f;
  s_mobLogTimer += dt;
  if (s_mobLogTimer > 2.0f) {
    s_mobLogTimer = 0.0f;
    T8_LOG_INFO("[Minecraft] Mob pos=(%.1f, %.1f, %.1f) pathReady=%d pathPts=%d",
                m_mob.position.x, m_mob.position.y, m_mob.position.z,
                m_mob.pathReady ? 1 : 0, (int)m_mob.path.size());
  }
}

void MinecraftScene::UpdateDayNight(float dt) {
  if (!m_dayNightEnabled || m_sunTrajectoryPaused) return;

  const auto& dayNight = m_voxelSettings.day_night;
  constexpr float kTwoPi = 6.28318530717958647692f;

  // Advance normalized orbit time. The authored phase controls where 0 lands.
  m_timeOfDay += (dt / m_dayLengthSecs) * dayNight.animation_speed;
  if (m_timeOfDay >= 1.0f) m_timeOfDay -= 1.0f;

  const float angle = (m_timeOfDay + dayNight.orbit_phase) * kTwoPi;
  const XVECTOR3 horizontal = Normalize3(XVECTOR3(
    dayNight.orbit_horizontal.x, dayNight.orbit_horizontal.y,
    dayNight.orbit_horizontal.z, 0.0f), XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  const XVECTOR3 verticalSource(
    dayNight.orbit_vertical.x, dayNight.orbit_vertical.y,
    dayNight.orbit_vertical.z, 0.0f);
  const XVECTOR3 vertical = Normalize3(
    verticalSource - horizontal * Dot3(verticalSource, horizontal),
    XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  const XVECTOR3 sunVector = Normalize3(
    horizontal * std::cos(angle) + vertical * std::sin(angle));
  const float sunHeight = Dot3(sunVector, vertical);

  // Day factor: 0 at night, 1 at noon (smooth transition)
  float dayFactor = (sunHeight + dayNight.horizon_offset) /
                    (1.0f + dayNight.horizon_offset);
  dayFactor = (std::max)(0.0f, (std::min)(1.0f, dayFactor));

  const XVECTOR3 lowColor(dayNight.sun_color_low.x, dayNight.sun_color_low.y,
                          dayNight.sun_color_low.z, 1.0f);
  const XVECTOR3 highColor(dayNight.sun_color_high.x, dayNight.sun_color_high.y,
                           dayNight.sun_color_high.z, 1.0f);
  const XVECTOR3 sunColor = lowColor + (highColor - lowColor) * dayFactor;

  const float ambient = dayNight.ambient_night +
                        (dayNight.ambient_day - dayNight.ambient_night) * dayFactor;
  const XVECTOR3 ambientColor(
    ambient * dayNight.ambient_tint.x,
    ambient * dayNight.ambient_tint.y,
    ambient * dayNight.ambient_tint.z, 1.0f);

  if (m_sunLightIndex >= 0 && m_sunLightIndex < (int)SceneProp.Lights.size()) {
    Light& sun = SceneProp.Lights[m_sunLightIndex];
    const XVECTOR3 center(dayNight.orbit_center.x, dayNight.orbit_center.y,
                          dayNight.orbit_center.z, 1.0f);
    sun.Position = center + sunVector * dayNight.orbit_radius;
    sun.Direction = XVECTOR3(-sunVector.x, -sunVector.y, -sunVector.z, 0.0f);
    sun.Color = sunColor;
    sun.Intensity = dayNight.sun_intensity_night +
      (dayNight.sun_intensity_day - dayNight.sun_intensity_night) * dayFactor;
    SyncLightCameraFromSun();
  }
  SceneProp.AmbientColor = ambientColor;

  // Periodic day/night log (every ~5 seconds)
  static float s_dayLogTimer = 0.0f;
  s_dayLogTimer += dt;
  if (s_dayLogTimer > 5.0f) {
    s_dayLogTimer = 0.0f;
    T8_LOG_INFO("[Minecraft] Time=%.2f dayFactor=%.2f sun=(%.2f,%.2f,%.2f) intensity=%.1f",
                m_timeOfDay, dayFactor, sunColor.x, sunColor.y, sunColor.z,
                m_sunLightIndex >= 0 ? SceneProp.Lights[m_sunLightIndex].Intensity : 0.0f);
  }
}

void MinecraftScene::SyncLightCameraFromSun() {
  if (m_sunLightIndex < 0 || m_sunLightIndex >= (int)SceneProp.Lights.size()) return;
  const Light& sun = SceneProp.Lights[m_sunLightIndex];
  LightCam.Eye = sun.Position;
  LightCam.SetLookAt(LightCam.Eye + Normalize3(sun.Direction));
  m_lightYaw = LightCam.Yaw;
  m_lightPitch = LightCam.Pitch;
}

void MinecraftScene::SyncSunFromLightCamera() {
  if (m_sunLightIndex < 0 || m_sunLightIndex >= (int)SceneProp.Lights.size()) return;
  Light& sun = SceneProp.Lights[m_sunLightIndex];
  sun.Position = LightCam.Eye;
  sun.Direction = Normalize3(LightCam.Look, sun.Direction);
}

void MinecraftScene::SetLightCameraEditMode(bool enabled) {
  m_lightCameraEditMode = enabled;
  if (enabled) {
    m_cameraMode = 2;
    m_sunTrajectoryPaused = true;
    SyncSunFromLightCamera();
    T8_LOG_INFO("[Minecraft] Light camera editing enabled; Sun trajectory paused");
  } else {
    T8_LOG_INFO("[Minecraft] Light camera editing finished; Sun remains paused");
  }
}

void MinecraftScene::CreateMobMesh() {
  xF::XDataBase db;
  xF::xMeshContainer* mc = new xF::xMeshContainer;
  mc->FileName = "MinecraftMob";
  db.XMeshDataBase.push_back(mc);
  mc->Geometry.resize(1);
  xF::xMeshGeometry& geom = mc->Geometry[0];
  geom.VertexAttributes = xF::xMeshGeometry::HAS_POSITION | xF::xMeshGeometry::HAS_NORMAL | xF::xMeshGeometry::HAS_TEXCOORD0;
  geom.NumChannelsTexCoords = 1;

  // Build a humanoid zombie-like figure from boxes (real dimensions, not
  // unit cubes). Uses the same addBox helper as the weapon.
  auto addBox = [&](float x0, float y0, float z0, float x1, float y1, float z1, uint8_t block) {
    const BlockDef& def = m_blockDefs[block];
    const XVECTOR3 c[8] = {
      XVECTOR3(x0, y0, z0), XVECTOR3(x1, y0, z0), XVECTOR3(x1, y1, z0), XVECTOR3(x0, y1, z0),
      XVECTOR3(x0, y0, z1), XVECTOR3(x1, y0, z1), XVECTOR3(x1, y1, z1), XVECTOR3(x0, y1, z1),
    };
    auto faceUV = [&](int face) {
      const BlockTile& tile = def.tiles[face];
      return TileUV(m_textureAtlas, tile.u, tile.v);
    };
    { UVQuad uv = faceUV(0); AddQuad(geom, c[1], c[5], c[6], c[2], XVECTOR3(1,0,0), uv.u0, uv.v0, uv.u1, uv.v1); }
    { UVQuad uv = faceUV(1); AddQuad(geom, c[4], c[0], c[3], c[7], XVECTOR3(-1,0,0), uv.u0, uv.v0, uv.u1, uv.v1); }
    { UVQuad uv = faceUV(2); AddQuad(geom, c[3], c[2], c[6], c[7], XVECTOR3(0,1,0), uv.u0, uv.v0, uv.u1, uv.v1); }
    { UVQuad uv = faceUV(3); AddQuad(geom, c[0], c[4], c[5], c[1], XVECTOR3(0,-1,0), uv.u0, uv.v0, uv.u1, uv.v1); }
    { UVQuad uv = faceUV(4); AddQuad(geom, c[5], c[4], c[7], c[6], XVECTOR3(0,0,1), uv.u0, uv.v0, uv.u1, uv.v1); }
    { UVQuad uv = faceUV(5); AddQuad(geom, c[0], c[1], c[2], c[3], XVECTOR3(0,0,-1), uv.u0, uv.v0, uv.u1, uv.v1); }
  };

  for (const auto& part : m_voxelSettings.mob.parts) {
    addBox(part.min.x, part.min.y, part.min.z,
           part.max.x, part.max.y, part.max.z,
           BlockId(part.block, 0));
  }

  geom.NumVertices = static_cast<xDWORD>(geom.Positions.size());
  geom.NumTriangles = static_cast<xDWORD>(geom.Triangles.size() / 3);
  geom.NumIndices = static_cast<xDWORD>(geom.Triangles.size());
  geom.VertexSize = 40;
  geom.MaterialList.Materials.resize(1);
  xF::xMaterial& mat = geom.MaterialList.Materials[0];
  mat.Name = "minecraft_mob";
  mat.bEffects = true;
  mat.EffectInstance.pDefaults.resize(2);
  mat.EffectInstance.pDefaults[0].Type = xF::xEFFECTENUM::STDX_STRINGS;
  mat.EffectInstance.pDefaults[0].NameParam = "diffuseMap";
  mat.EffectInstance.pDefaults[0].CaseString = m_voxelSettings.material.diffuse_texture;
  mat.EffectInstance.pDefaults[1].Type = xF::xEFFECTENUM::STDX_FLOATS;
  mat.EffectInstance.pDefaults[1].NameParam = "pbrRoughness";
  mat.EffectInstance.pDefaults[1].CaseFloat.push_back(m_voxelSettings.material.roughness);
  geom.MaterialList.FaceIndices.assign(geom.NumTriangles, 0);
  geom.MaterialList.NumMatProcess = 1;

  xF::xFinalGeometry finalGeometry;
  finalGeometry.VertexSize = 40;
  finalGeometry.NumVertex = geom.NumVertices;
  finalGeometry.pData = new float[10 * geom.NumVertices];
  finalGeometry.pDataDest = new float[10 * geom.NumVertices];
  unsigned int cursor = 0;
  for (unsigned int i = 0; i < geom.NumVertices; ++i) {
    finalGeometry.pData[cursor++] = geom.Positions[i].x;
    finalGeometry.pData[cursor++] = geom.Positions[i].y;
    finalGeometry.pData[cursor++] = geom.Positions[i].z;
    finalGeometry.pData[cursor++] = 1.0f;
    finalGeometry.pData[cursor++] = geom.Normals[i].x;
    finalGeometry.pData[cursor++] = geom.Normals[i].y;
    finalGeometry.pData[cursor++] = geom.Normals[i].z;
    finalGeometry.pData[cursor++] = 0.0f;
    finalGeometry.pData[cursor++] = geom.TexCoordinates[0][i].x;
    finalGeometry.pData[cursor++] = geom.TexCoordinates[0][i].y;
  }
  std::copy(finalGeometry.pData, finalGeometry.pData + cursor, finalGeometry.pDataDest);
  xF::xSubsetInfo subset;
  subset.NumTris = geom.NumTriangles;
  subset.NumVertex = geom.NumVertices;
  subset.VertexSize = 40;
  subset.VertexAttrib = geom.VertexAttributes;
  subset.bAlignedVertex = true;
  finalGeometry.Subsets.push_back(subset);
  db.MeshInfo.push_back(std::move(finalGeometry));

  RenderMesh* mesh = new RenderMesh();
  mesh->SetEngineContext(pEngineContext);
  mesh->SetSceneProps(&SceneProp);
  mesh->xFile = new xF::XDataBase(std::move(db));
  mesh->m_sourcePath = "MinecraftMob";
  bool created = false;
  mesh->m_asset = MeshAssetCache::Get().Acquire(mesh->m_sourcePath, &created);
  mesh->Create();
  if (mesh->Info.empty()) {
    delete mesh;
    return;
  }
  for (auto& info : mesh->Info) {
    for (auto& subsetInfo : info.SubSets) {
      subsetInfo.DiffuseTex = m_atlasTexture;
      subsetInfo.DiffuseId = m_atlasTexIndex;
      if (subsetInfo.matAsset) {
        MaterialAsset* previous = subsetInfo.matAsset;
        subsetInfo.matAsset = MaterialAssetCache::Get().AcquireTextureVariant(
          *previous, MatTexSlot::BaseColor, m_atlasTexture, m_atlasTexIndex);
        MaterialAssetCache::Get().Release(previous);
      }
    }
  }
  Meshes[m_mobMeshIndex].CreateInstance(mesh, &VP);
  UpdateMobInstance();
}

void MinecraftScene::UpdateMobInstance() {
  if (!Meshes[m_mobMeshIndex].pBase) return;
  Meshes[m_mobMeshIndex].TranslateAbsolute(m_mob.position.x, m_mob.position.y, m_mob.position.z);
  Meshes[m_mobMeshIndex].Update();
}

// ── First-person weapon (sword) ──────────────────────────────────────
// Build a simple sword from box primitives: blade, crossguard, handle.
// The sword is modeled pointing up (+Y); UpdateWeapon positions it in
// front of the camera.
void MinecraftScene::CreateWeaponMesh() {
  xF::XDataBase db;
  xF::xMeshContainer* mc = new xF::xMeshContainer;
  mc->FileName = "MinecraftWeapon";
  db.XMeshDataBase.push_back(mc);
  mc->Geometry.resize(1);
  xF::xMeshGeometry& geom = mc->Geometry[0];
  geom.VertexAttributes = xF::xMeshGeometry::HAS_POSITION | xF::xMeshGeometry::HAS_NORMAL | xF::xMeshGeometry::HAS_TEXCOORD0;
  geom.NumChannelsTexCoords = 1;

  // Helper to add an axis-aligned box with arbitrary dimensions (in world
  // units, not block units). Each face uses the block's tile for that face.
  auto addBox = [&](float x0, float y0, float z0, float x1, float y1, float z1, uint8_t block) {
    const BlockDef& def = m_blockDefs[block];
    const XVECTOR3 c[8] = {
      XVECTOR3(x0, y0, z0), XVECTOR3(x1, y0, z0), XVECTOR3(x1, y1, z0), XVECTOR3(x0, y1, z0), // -Z ring
      XVECTOR3(x0, y0, z1), XVECTOR3(x1, y0, z1), XVECTOR3(x1, y1, z1), XVECTOR3(x0, y1, z1), // +Z ring
    };
    auto faceUV = [&](int face) {
      const BlockTile& tile = def.tiles[face];
      return TileUV(m_textureAtlas, tile.u, tile.v);
    };
    // +X
    { UVQuad uv = faceUV(0); AddQuad(geom, c[1], c[5], c[6], c[2], XVECTOR3(1,0,0), uv.u0, uv.v0, uv.u1, uv.v1); }
    // -X
    { UVQuad uv = faceUV(1); AddQuad(geom, c[4], c[0], c[3], c[7], XVECTOR3(-1,0,0), uv.u0, uv.v0, uv.u1, uv.v1); }
    // +Y
    { UVQuad uv = faceUV(2); AddQuad(geom, c[3], c[2], c[6], c[7], XVECTOR3(0,1,0), uv.u0, uv.v0, uv.u1, uv.v1); }
    // -Y
    { UVQuad uv = faceUV(3); AddQuad(geom, c[0], c[4], c[5], c[1], XVECTOR3(0,-1,0), uv.u0, uv.v0, uv.u1, uv.v1); }
    // +Z
    { UVQuad uv = faceUV(4); AddQuad(geom, c[5], c[4], c[7], c[6], XVECTOR3(0,0,1), uv.u0, uv.v0, uv.u1, uv.v1); }
    // -Z
    { UVQuad uv = faceUV(5); AddQuad(geom, c[0], c[1], c[2], c[3], XVECTOR3(0,0,-1), uv.u0, uv.v0, uv.u1, uv.v1); }
  };

  for (const auto& part : m_voxelSettings.weapon.parts) {
    addBox(part.min.x, part.min.y, part.min.z,
           part.max.x, part.max.y, part.max.z,
           BlockId(part.block, 0));
  }

  geom.NumVertices = static_cast<xDWORD>(geom.Positions.size());
  geom.NumTriangles = static_cast<xDWORD>(geom.Triangles.size() / 3);
  geom.NumIndices = static_cast<xDWORD>(geom.Triangles.size());
  geom.VertexSize = 40;
  geom.MaterialList.Materials.resize(1);
  xF::xMaterial& mat = geom.MaterialList.Materials[0];
  mat.Name = "minecraft_weapon";
  mat.bEffects = true;
  mat.EffectInstance.pDefaults.resize(2);
  mat.EffectInstance.pDefaults[0].Type = xF::xEFFECTENUM::STDX_STRINGS;
  mat.EffectInstance.pDefaults[0].NameParam = "diffuseMap";
  mat.EffectInstance.pDefaults[0].CaseString = m_voxelSettings.material.diffuse_texture;
  mat.EffectInstance.pDefaults[1].Type = xF::xEFFECTENUM::STDX_FLOATS;
  mat.EffectInstance.pDefaults[1].NameParam = "pbrRoughness";
  mat.EffectInstance.pDefaults[1].CaseFloat.push_back(m_voxelSettings.material.roughness);
  geom.MaterialList.FaceIndices.assign(geom.NumTriangles, 0);
  geom.MaterialList.NumMatProcess = 1;

  xF::xFinalGeometry finalGeometry;
  finalGeometry.VertexSize = 40;
  finalGeometry.NumVertex = geom.NumVertices;
  finalGeometry.pData = new float[10 * geom.NumVertices];
  finalGeometry.pDataDest = new float[10 * geom.NumVertices];
  unsigned int cursor = 0;
  for (unsigned int i = 0; i < geom.NumVertices; ++i) {
    finalGeometry.pData[cursor++] = geom.Positions[i].x;
    finalGeometry.pData[cursor++] = geom.Positions[i].y;
    finalGeometry.pData[cursor++] = geom.Positions[i].z;
    finalGeometry.pData[cursor++] = 1.0f;
    finalGeometry.pData[cursor++] = geom.Normals[i].x;
    finalGeometry.pData[cursor++] = geom.Normals[i].y;
    finalGeometry.pData[cursor++] = geom.Normals[i].z;
    finalGeometry.pData[cursor++] = 0.0f;
    finalGeometry.pData[cursor++] = geom.TexCoordinates[0][i].x;
    finalGeometry.pData[cursor++] = geom.TexCoordinates[0][i].y;
  }
  std::copy(finalGeometry.pData, finalGeometry.pData + cursor, finalGeometry.pDataDest);
  xF::xSubsetInfo subset;
  subset.NumTris = geom.NumTriangles;
  subset.NumVertex = geom.NumVertices;
  subset.VertexSize = 40;
  subset.VertexAttrib = geom.VertexAttributes;
  subset.bAlignedVertex = true;
  finalGeometry.Subsets.push_back(subset);
  db.MeshInfo.push_back(std::move(finalGeometry));

  RenderMesh* mesh = new RenderMesh();
  mesh->SetEngineContext(pEngineContext);
  mesh->SetSceneProps(&SceneProp);
  mesh->xFile = new xF::XDataBase(std::move(db));
  mesh->m_sourcePath = "MinecraftWeapon";
  bool created = false;
  mesh->m_asset = MeshAssetCache::Get().Acquire(mesh->m_sourcePath, &created);
  mesh->Create();
  if (mesh->Info.empty()) {
    delete mesh;
    return;
  }
  for (auto& info : mesh->Info) {
    for (auto& subsetInfo : info.SubSets) {
      subsetInfo.DiffuseTex = m_atlasTexture;
      subsetInfo.DiffuseId = m_atlasTexIndex;
      if (subsetInfo.matAsset) {
        MaterialAsset* previous = subsetInfo.matAsset;
        subsetInfo.matAsset = MaterialAssetCache::Get().AcquireTextureVariant(
          *previous, MatTexSlot::BaseColor, m_atlasTexture, m_atlasTexIndex);
        MaterialAssetCache::Get().Release(previous);
      }
    }
  }
  Meshes[m_weaponMeshIndex].CreateInstance(mesh, &VP);
  Meshes[m_weaponMeshIndex].SetVisible(false);
}

// Position the weapon in front of the camera (first-person view) and
// animate a swing when the player attacks.
void MinecraftScene::UpdateWeapon(float dt) {
  if (!Meshes[m_weaponMeshIndex].pBase) return;

  // Walk bob phase advances when the player is moving.
  const bool moving = m_playerInput.moveForward || m_playerInput.moveBackward ||
                      m_playerInput.moveLeft || m_playerInput.moveRight;
  if (moving && m_player.IsGrounded()) {
    m_weaponBob += dt * m_voxelSettings.weapon.bob_speed;
  }

  // Swing animation: triggered by attack, eases back to rest.
  if (m_weaponSwinging) {
    m_weaponSwing += dt * m_voxelSettings.weapon.swing_speed;
    if (m_weaponSwing >= 1.0f) {
      m_weaponSwing = 0.0f;
      m_weaponSwinging = false;
    }
  }

  // Base position: bottom-right of the view, in front of the camera.
  // We place it relative to the camera eye + look/right/up vectors.
  const XVECTOR3 eye = m_playerEye;
  const XVECTOR3 look = Cam.Look;
  const XVECTOR3 right = Cam.Right;
  const XVECTOR3 up = Cam.Up;

  // Scale the sword down to a proper first-person size (model is ~1.9 units tall).
  const float weaponScale = m_voxelSettings.weapon.scale;

  // Rest offset: to the right and down, slightly forward.
  float offsetRight = m_voxelSettings.weapon.offset_right;
  float offsetDown = -m_voxelSettings.weapon.offset_down;
  float offsetForward = m_voxelSettings.weapon.offset_forward;

  // Walk bob: small vertical + horizontal sway.
  const float bob = std::sin(m_weaponBob) * m_voxelSettings.weapon.bob_vertical;
  offsetDown += bob;
  offsetRight += std::cos(m_weaponBob) * m_voxelSettings.weapon.bob_horizontal;

  // Swing: rotate the sword forward/down during the attack.
  float swingPitch = 0.0f;
  if (m_weaponSwinging) {
    const float t = m_weaponSwing;
    // Swing down then back up.
    swingPitch = -m_voxelSettings.weapon.swing_angle * std::sin(t * 3.14159265f);
  }

  const XVECTOR3 pos = eye + look * offsetForward + right * offsetRight + up * offsetDown;

  Meshes[m_weaponMeshIndex].SetVisible(true);
  Meshes[m_weaponMeshIndex].TranslateAbsolute(pos.x, pos.y, pos.z);
  Meshes[m_weaponMeshIndex].ScaleAbsolute(weaponScale, weaponScale, weaponScale);

  // Keep the sword in camera space like a conventional FPS viewmodel.
  // PrimitiveInst uses row-vector transforms, so each row maps one local
  // model axis into the camera basis. The blade's local +Y remains upright.
  {
    XMATRIX44 rot;
    rot.Identity();
    rot.m[0][0] = right.x; rot.m[0][1] = right.y; rot.m[0][2] = right.z;
    rot.m[1][0] = up.x;    rot.m[1][1] = up.y;    rot.m[1][2] = up.z;
    rot.m[2][0] = look.x;  rot.m[2][1] = look.y;  rot.m[2][2] = look.z;

    // Apply the swing as a rotation around the sword's local X (crossguard)
    // axis so the blade swings forward/down.
    XMATRIX44 swing;
    swing.Identity();
    const float sa = Deg2Rad(swingPitch);
    swing.m[1][1] = std::cos(sa); swing.m[1][2] = -std::sin(sa);
    swing.m[2][1] = std::sin(sa); swing.m[2][2] =  std::cos(sa);

    Meshes[m_weaponMeshIndex].RotationX = swing * rot;
    Meshes[m_weaponMeshIndex].RotationY.Identity();
    Meshes[m_weaponMeshIndex].RotationZ.Identity();
  }
  Meshes[m_weaponMeshIndex].Update();
}

// ── Mesh building ────────────────────────────────────────────────────
void MinecraftScene::AddVertex(xF::xMeshGeometry& geom, float x, float y, float z,
                               float nx, float ny, float nz, float u, float v) {
  geom.Positions.push_back(XVECTOR3(x, y, z));
  geom.Normals.push_back(XVECTOR3(nx, ny, nz));
  geom.TexCoordinates[0].push_back(XVECTOR2(u, v));
}

void MinecraftScene::AddQuad(xF::xMeshGeometry& geom,
                             const XVECTOR3& a, const XVECTOR3& b, const XVECTOR3& c, const XVECTOR3& d,
                             const XVECTOR3& n, float u0, float v0, float u1, float v1) {
  const unsigned int base = (unsigned int)geom.Positions.size();
  AddVertex(geom, a.x, a.y, a.z, n.x, n.y, n.z, u0, v0);
  AddVertex(geom, b.x, b.y, b.z, n.x, n.y, n.z, u0, v1);
  AddVertex(geom, c.x, c.y, c.z, n.x, n.y, n.z, u1, v1);
  AddVertex(geom, d.x, d.y, d.z, n.x, n.y, n.z, u1, v0);
  // Two triangles (CCW)
  geom.Triangles.push_back((xWORD)base);
  geom.Triangles.push_back((xWORD)(base + 1));
  geom.Triangles.push_back((xWORD)(base + 2));
  geom.Triangles.push_back((xWORD)base);
  geom.Triangles.push_back((xWORD)(base + 2));
  geom.Triangles.push_back((xWORD)(base + 3));
}

void MinecraftScene::AddFace(xF::xMeshGeometry& geom, int x, int y, int z, int face, uint8_t block) {
  const BlockDef& def = m_blockDefs[block];
  const BlockTile& tile = def.tiles[face];
  const UVQuad uv = TileUV(m_textureAtlas, tile.u, tile.v);
  const FaceDef& f = kFaces[face];

  XVECTOR3 corners[4];
  for (int i = 0; i < 4; ++i) {
    corners[i] = XVECTOR3(x + f.corners[i].x, y + f.corners[i].y, z + f.corners[i].z);
  }
  // UV mapping: corners[0]=(u0,v0), [1]=(u0,v1), [2]=(u1,v1), [3]=(u1,v0)
  AddQuad(geom, corners[0], corners[1], corners[2], corners[3], f.normal, uv.u0, uv.v0, uv.u1, uv.v1);
}

void MinecraftScene::CreateChunkMesh(int cx, int cz, xF::XDataBase& outDb) {
  static int s_waterFaces = 0;
  const int idx = ChunkIndex(cx, cz);
  if (idx < 0) return;
  const int gx = idx % m_chunkCountX;
  const int gz = idx / m_chunkCountX;
  const int baseX = cx * m_chunkSize;
  const int baseZ = cz * m_chunkSize;

  xF::xMeshContainer* mc = new xF::xMeshContainer;
  mc->FileName = "MinecraftChunk";
  outDb.XMeshDataBase.push_back(mc);
  mc->Geometry.resize(1);
  xF::xMeshGeometry& geom = mc->Geometry[0];

  geom.VertexAttributes = xF::xMeshGeometry::HAS_POSITION | xF::xMeshGeometry::HAS_NORMAL | xF::xMeshGeometry::HAS_TEXCOORD0;
  geom.NumChannelsTexCoords = 1;

  for (int lx = 0; lx < m_chunkSize; ++lx) {
    for (int lz = 0; lz < m_chunkSize; ++lz) {
      for (int wy = 0; wy < m_worldHeight; ++wy) {
        const uint8_t block = m_blocks[gx][wy][gz][lx][lz];
        if (block == BlockId(m_voxelSettings.terrain.air_block, 0)) continue;
        const int wx = baseX + lx;
        const int wz = baseZ + lz;

        // Water: render only the top surface (flat translucent plane)
        if (block == BlockId(m_voxelSettings.terrain.water_block, 0)) {
          const uint8_t above = GetBlock(wx, wy + 1, wz);
          if (above != BlockId(m_voxelSettings.terrain.water_block, 0)) {
            AddFace(geom, wx, wy, wz, 2, block); // +Y top face
            ++s_waterFaces;
          }
          continue;
        }

        // Check each face; only emit if neighbor is non-opaque
        const uint8_t px = GetBlock(wx + 1, wy, wz);
        const uint8_t nx = GetBlock(wx - 1, wy, wz);
        const uint8_t py = GetBlock(wx, wy + 1, wz);
        const uint8_t ny = GetBlock(wx, wy - 1, wz);
        const uint8_t pz = GetBlock(wx, wy, wz + 1);
        const uint8_t nz = GetBlock(wx, wy, wz - 1);

        if (!IsBlockOpaque(px)) AddFace(geom, wx, wy, wz, 0, block);
        if (!IsBlockOpaque(nx)) AddFace(geom, wx, wy, wz, 1, block);
        if (!IsBlockOpaque(py)) AddFace(geom, wx, wy, wz, 2, block);
        if (!IsBlockOpaque(ny)) AddFace(geom, wx, wy, wz, 3, block);
        if (!IsBlockOpaque(pz)) AddFace(geom, wx, wy, wz, 4, block);
        if (!IsBlockOpaque(nz)) AddFace(geom, wx, wy, wz, 5, block);
      }
    }
  }

  geom.NumVertices = (xDWORD)geom.Positions.size();
  geom.NumTriangles = (xDWORD)(geom.Triangles.size() / 3);
  geom.NumIndices = (xDWORD)geom.Triangles.size();

  // Material
  geom.MaterialList.Materials.resize(1);
  xF::xMaterial& mat = geom.MaterialList.Materials[0];
  mat.Name = "minecraft_block";
  mat.FaceColor = xF::STDRGBAColor(1.0f, 1.0f, 1.0f, 1.0f);
  mat.Power = 0.0f;
  const float materialSpecular = m_voxelSettings.material.specular;
  mat.Specular = xF::STDRGBAColor(materialSpecular, materialSpecular, materialSpecular, 1.0f);
  mat.Emissive = xF::STDRGBAColor(0.0f, 0.0f, 0.0f, 1.0f);
  mat.bEffects = true;
  mat.EffectInstance.NumDefaults = 2;
  mat.EffectInstance.pDefaults.resize(2);
  // diffuseMap (string) — sets DIFFUSE_MAP in shader key
  mat.EffectInstance.pDefaults[0].Type = xF::xEFFECTENUM::STDX_STRINGS;
  mat.EffectInstance.pDefaults[0].NameParam = "diffuseMap";
  mat.EffectInstance.pDefaults[0].CaseString = m_voxelSettings.material.diffuse_texture;
  // pbrRoughness (float)
  mat.EffectInstance.pDefaults[1].Type = xF::xEFFECTENUM::STDX_FLOATS;
  mat.EffectInstance.pDefaults[1].NameParam = "pbrRoughness";
  mat.EffectInstance.pDefaults[1].CaseFloat.push_back(m_voxelSettings.material.roughness);

  geom.MaterialList.FaceIndices.assign(geom.NumTriangles, 0);
  geom.MaterialList.NumMatProcess = 1;

  // Build xFinalGeometry (interleaved)
  xF::xFinalGeometry fg;
  const unsigned int v4 = 16;
  unsigned int vsz = v4 + v4 + 8; // pos + normal + uv
  geom.VertexSize = vsz;
  fg.VertexSize = vsz;
  fg.NumVertex = geom.NumVertices;
  const unsigned int floatsPerVertex = vsz / 4;
  const unsigned int totalFloats = floatsPerVertex * geom.NumVertices;
  fg.pData = new float[totalFloats];
  fg.pDataDest = new float[totalFloats];
  unsigned int c = 0;
  for (unsigned int j = 0; j < geom.NumVertices; ++j) {
    fg.pData[c++] = geom.Positions[j].x;
    fg.pData[c++] = geom.Positions[j].y;
    fg.pData[c++] = geom.Positions[j].z;
    fg.pData[c++] = 1.0f;
    fg.pData[c++] = geom.Normals[j].x;
    fg.pData[c++] = geom.Normals[j].y;
    fg.pData[c++] = geom.Normals[j].z;
    fg.pData[c++] = 0.0f;
    fg.pData[c++] = geom.TexCoordinates[0][j].x;
    fg.pData[c++] = geom.TexCoordinates[0][j].y;
  }
  for (unsigned int j = 0; j < c; ++j) fg.pDataDest[j] = fg.pData[j];

  xF::xSubsetInfo si;
  si.NumTris = geom.NumTriangles;
  si.NumVertex = geom.NumVertices;
  si.VertexSize = vsz;
  si.VertexAttrib = geom.VertexAttributes;
  si.bAlignedVertex = true;
  fg.Subsets.push_back(si);

  outDb.MeshInfo.push_back(std::move(fg));

  // Log water face count once per full world build
  static int s_buildCount = 0;
  ++s_buildCount;
  if (s_buildCount == m_maxChunks) {
    T8_LOG_INFO("[Minecraft] Chunk meshes built: water faces=%d", s_waterFaces);
    s_waterFaces = 0;
    s_buildCount = 0;
  }
}

// ── Chunk mesh rebuild ───────────────────────────────────────────────
void MinecraftScene::BuildChunkMesh(int cx, int cz) {
  const int idx = ChunkIndex(cx, cz);
  if (idx < 0) return;

  xF::XDataBase db;
  CreateChunkMesh(cx, cz, db);
  t850::MutableMeshSnapshot snapshot;
  ConvertChunkDatabase(db, snapshot);

  t850::MutableMesh* mesh = dynamic_cast<t850::MutableMesh*>(Meshes[idx].pBase);
  if (!mesh) {
    if (Meshes[idx].pBase) {
      Meshes[idx].pBase->Destroy();
      delete Meshes[idx].pBase;
    }
    mesh = new t850::MutableMesh();
    mesh->SetEngineContext(pEngineContext);
    mesh->SetSceneProps(&SceneProp);
    mesh->Create();
    Meshes[idx].CreateInstance(mesh, &VP);
    Meshes[idx].SetTexture(m_atlasTexture, 0);
  }

  std::string error;
  if (!mesh->ReplaceSnapshot(std::move(snapshot), &error, false)) {
    T8_LOG_ERROR("[Minecraft] Chunk (%d,%d) mutable mesh commit failed: %s",
                 cx, cz, error.c_str());
    Meshes[idx].SetVisible(false);
  } else {
    Meshes[idx].SetVisible(mesh->Ready());
    Meshes[idx].Update();
  }
  m_chunkBuilt[idx / m_chunkCountX][idx % m_chunkCountX] = true;
  m_chunkDirty[idx / m_chunkCountX][idx % m_chunkCountX] = false;
}

void MinecraftScene::RebuildDirtyChunks() {
  for (int cz = m_centerChunkZ - m_renderDistance; cz <= m_centerChunkZ + m_renderDistance; ++cz) {
    for (int cx = m_centerChunkX - m_renderDistance; cx <= m_centerChunkX + m_renderDistance; ++cx) {
      const int idx = ChunkIndex(cx, cz);
      if (idx < 0) continue;
      if (m_chunkDirty[idx / m_chunkCountX][idx % m_chunkCountX]) {
        BuildChunkMesh(cx, cz);
      }
    }
  }
}

void MinecraftScene::ApplyPendingRenderDistance() {
  if (m_pendingRenderDistance <= 0) return;
  if (m_pendingRenderDistance == m_renderDistance) {
    m_pendingRenderDistance = 0;
    return;
  }
  if (m_chunkGenerationFuture.valid() || m_navMeshBuildFuture.valid() ||
      !m_chunksAwaitingMesh.empty()) return;
  {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    if (!m_pendingChunks.empty()) return;
  }

  const int oldDistance = m_renderDistance;
  const int newDistance = (std::max)(1, (std::min)(kMaxRenderDistance, m_pendingRenderDistance));
  m_pendingRenderDistance = 0;

  for (int cz = m_centerChunkZ - oldDistance; cz <= m_centerChunkZ + oldDistance; ++cz) {
    for (int cx = m_centerChunkX - oldDistance; cx <= m_centerChunkX + oldDistance; ++cx) {
      if (std::abs(cx - m_centerChunkX) <= newDistance &&
          std::abs(cz - m_centerChunkZ) <= newDistance) continue;
      const int gx = ((cx % m_chunkCountX) + m_chunkCountX) % m_chunkCountX;
      const int gz = ((cz % m_chunkCountZ) + m_chunkCountZ) % m_chunkCountZ;
      const int idx = gz * m_chunkCountX + gx;
      if (Meshes[idx].pBase) Meshes[idx].SetVisible(false);
      m_chunkBuilt[gz][gx] = false;
      m_chunkDirty[gz][gx] = false;
    }
  }

  m_renderDistance = newDistance;
  m_renderDistanceBuildTarget = newDistance;
  m_voxelSettings.render_distance = newDistance;
  m_streamingRecenterThreshold = (std::min)(
    m_voxelSettings.streaming_recenter_threshold, newDistance - 1);

  std::vector<std::pair<int, int>> newChunks;
  if (newDistance > oldDistance) {
    for (int cz = m_centerChunkZ - newDistance; cz <= m_centerChunkZ + newDistance; ++cz) {
      for (int cx = m_centerChunkX - newDistance; cx <= m_centerChunkX + newDistance; ++cx) {
        if (std::abs(cx - m_centerChunkX) <= oldDistance &&
            std::abs(cz - m_centerChunkZ) <= oldDistance) continue;
        const int idx = ChunkIndex(cx, cz);
        if (idx < 0) continue;
        if (Meshes[idx].pBase) Meshes[idx].SetVisible(false);
        m_chunkBuilt[idx / m_chunkCountX][idx % m_chunkCountX] = false;
        newChunks.emplace_back(cx, cz);
      }
    }
  }

  const bool useBackgroundGeneration = !newChunks.empty() && m_asyncStreaming &&
    t850::g_threadPool && t850::g_threadPool->NumWorkers() > 0;
  if (useBackgroundGeneration) {
    m_generatedChunkBatch =
      std::make_shared<std::vector<GeneratedChunkData>>(newChunks.size());
    const auto generatedBatch = m_generatedChunkBatch;
    m_chunkGenerationFuture = t850::g_threadPool->Submit(
      [this, newChunks = std::move(newChunks), generatedBatch]() {
        for (std::size_t index = 0; index < newChunks.size(); ++index) {
          GeneratedChunkData& generated = (*generatedBatch)[index];
          generated.cx = newChunks[index].first;
          generated.cz = newChunks[index].second;
          GenerateChunkData(generated.cx, generated.cz, generated.blocks);
        }
      });
  } else if (!newChunks.empty()) {
    for (const auto& chunk : newChunks) GenerateChunk(chunk.first, chunk.second);
    for (int cz = m_centerChunkZ - newDistance; cz <= m_centerChunkZ + newDistance; ++cz)
      for (int cx = m_centerChunkX - newDistance; cx <= m_centerChunkX + newDistance; ++cx)
        GenerateChunkTrees(cx, cz);
    RebuildDirtyChunks();
  } else {
    for (int offset = -newDistance; offset <= newDistance; ++offset) {
      QueueChunkRemesh(m_centerChunkX - newDistance, m_centerChunkZ + offset);
      QueueChunkRemesh(m_centerChunkX + newDistance, m_centerChunkZ + offset);
      QueueChunkRemesh(m_centerChunkX + offset, m_centerChunkZ - newDistance);
      QueueChunkRemesh(m_centerChunkX + offset, m_centerChunkZ + newDistance);
    }
  }

  m_navMeshDirty = true;
  m_navMeshRebuildTimer = m_voxelSettings.navmesh_rebuild_seconds;
  m_mob.path.clear();
  m_mob.pathReady = false;
  T8_LOG_INFO("[Minecraft] Draw distance changed %d -> %d chunks", oldDistance, newDistance);
}

void MinecraftScene::ReportRenderDistanceReady() {
  if (m_renderDistanceBuildTarget <= 0 || m_chunkGenerationFuture.valid() ||
      !m_chunksAwaitingMesh.empty()) return;
  {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    if (!m_pendingChunks.empty()) return;
  }

  int chunkMeshes = 0;
  for (int cz = m_centerChunkZ - m_renderDistance; cz <= m_centerChunkZ + m_renderDistance; ++cz) {
    for (int cx = m_centerChunkX - m_renderDistance; cx <= m_centerChunkX + m_renderDistance; ++cx) {
      const int idx = ChunkIndex(cx, cz);
      if (idx >= 0 && Meshes[idx].pBase && Meshes[idx].Visible) ++chunkMeshes;
    }
  }
  const int expected = (m_renderDistance * 2 + 1) * (m_renderDistance * 2 + 1);
  T8_LOG_INFO("[Minecraft] Draw distance ready: radius=%d chunkMeshes=%d expected=%d",
              m_renderDistance, chunkMeshes, expected);
  m_renderDistanceBuildTarget = 0;
}

void MinecraftScene::UpdateChunkStreaming() {
  if (m_chunkGenerationFuture.valid()) return;
  // Rebuild chunks around the player as they move between chunk centers
  const int pcx = WorldToChunk((int)std::floor(m_playerEye.x));
  const int pcz = WorldToChunk((int)std::floor(m_playerEye.z));
  const int dx = pcx - m_centerChunkX;
  const int dz = pcz - m_centerChunkZ;
  if (std::abs(dx) > m_streamingRecenterThreshold ||
      std::abs(dz) > m_streamingRecenterThreshold) {
    const int oldCx = m_centerChunkX;
    const int oldCz = m_centerChunkZ;
    m_centerChunkX = pcx;
    m_centerChunkZ = pcz;
    ShiftWorldAndStream(oldCx, oldCz);
  }
}

// Incremental chunk streaming: when the player crosses a chunk boundary,
// shift the existing world data + meshes by the delta and only generate
// + mesh the newly-entered edge chunks. This avoids rebuilding all 81
// chunks (and re-uploading all GPU buffers) on every boundary crossing,
// which caused a long freeze.
void MinecraftScene::ShiftWorldAndStream(int oldCx, int oldCz) {
  const int dx = m_centerChunkX - oldCx; // chunk delta
  const int dz = m_centerChunkZ - oldCz;
  const bool useBackgroundGeneration = m_asyncStreaming && t850::g_threadPool &&
    t850::g_threadPool->NumWorkers() > 0;
  m_navMeshDirty = true;
  m_navMeshRebuildTimer = m_voxelSettings.navmesh_rebuild_seconds;

  // Ring-mapped slots keep resident chunks in place. Only entering world
  // chunks reuse outgoing slots, so crossing a boundary never copies the
  // full block grid or mesh-instance array.
  std::memset(m_chunkDirty, 0, sizeof(m_chunkDirty));
  std::memset(m_chunkBuilt, 0, sizeof(m_chunkBuilt));
  std::vector<std::pair<int, int>> newChunks;
  int newChunkCount = 0;
  for (int cz = m_centerChunkZ - m_renderDistance; cz <= m_centerChunkZ + m_renderDistance; ++cz) {
    for (int cx = m_centerChunkX - m_renderDistance; cx <= m_centerChunkX + m_renderDistance; ++cx) {
      const int idx = ChunkIndex(cx, cz);
      if (idx < 0) continue;
      const int gx = idx % m_chunkCountX;
      const int gz = idx / m_chunkCountX;
      // A chunk is newly entered if it was NOT in the old center's range.
      const bool wasInRange =
          (cx >= oldCx - m_renderDistance && cx <= oldCx + m_renderDistance &&
           cz >= oldCz - m_renderDistance && cz <= oldCz + m_renderDistance);
      if (!wasInRange) {
        if (Meshes[idx].pBase) Meshes[idx].SetVisible(false);
        if (useBackgroundGeneration) {
          m_chunkBuilt[gz][gx] = false;
          newChunks.emplace_back(cx, cz);
        } else {
          GenerateChunk(cx, cz);
          m_chunkDirty[gz][gx] = true;
        }
        ++newChunkCount;
      } else {
        m_chunkBuilt[gz][gx] = true;
      }
    }
  }

  if (useBackgroundGeneration) {
    m_generatedChunkBatch =
      std::make_shared<std::vector<GeneratedChunkData>>(newChunks.size());
    const auto generatedBatch = m_generatedChunkBatch;
    m_chunkGenerationFuture = t850::g_threadPool->Submit(
      [this, newChunks = std::move(newChunks), generatedBatch]() {
        for (std::size_t index = 0; index < newChunks.size(); ++index) {
          GeneratedChunkData& generated = (*generatedBatch)[index];
          generated.cx = newChunks[index].first;
          generated.cz = newChunks[index].second;
          GenerateChunkData(generated.cx, generated.cz, generated.blocks);
        }
      });
  } else {
    for (int cz = m_centerChunkZ - m_renderDistance; cz <= m_centerChunkZ + m_renderDistance; ++cz) {
      for (int cx = m_centerChunkX - m_renderDistance; cx <= m_centerChunkX + m_renderDistance; ++cx)
        GenerateChunkTrees(cx, cz);
    }
    RebuildDirtyChunks();
  }

  T8_LOG_INFO("[Minecraft] Streamed center (%d,%d)->(%d,%d) dx=%d dz=%d newChunks=%d",
              oldCx, oldCz, m_centerChunkX, m_centerChunkZ, dx, dz, newChunkCount);
}

// ── Async chunk streaming ────────────────────────────────────────────
// Snapshot the chunk's block data (with a 1-block border) so the
// background thread can build geometry without racing the render thread
// (which may shift m_blocks when the player moves).
void MinecraftScene::EnqueueChunkBuild(int cx, int cz) {
  const int idx = ChunkIndex(cx, cz);
  if (idx < 0) return;
  const int gx = idx % m_chunkCountX;
  const int gz = idx / m_chunkCountX;
  const int baseX = cx * m_chunkSize;
  const int baseZ = cz * m_chunkSize;

  PendingChunk pc;
  pc.cx = cx;
  pc.cz = cz;
  pc.gx = gx;
  pc.gz = gz;
  const uint8_t airBlock = BlockId(m_voxelSettings.terrain.air_block, 0);
  pc.blockSnapshot.resize(static_cast<std::size_t>(m_worldHeight) * (m_chunkSize + 2) * (m_chunkSize + 2), airBlock);

  // Copy the chunk + 1-block border into the snapshot.
  for (int wy = 0; wy < m_worldHeight; ++wy) {
    for (int lz = -1; lz <= m_chunkSize; ++lz) {
      for (int lx = -1; lx <= m_chunkSize; ++lx) {
        const int wx = baseX + lx;
        const int wz = baseZ + lz;
        uint8_t b = airBlock;
        if (wy >= 0 && wy < m_worldHeight) {
          const int ncx = WorldToChunk(wx);
          const int ncz = WorldToChunk(wz);
          const int nidx = ChunkIndex(ncx, ncz);
          if (nidx >= 0) {
            const int llx = WorldToLocal(wx);
            const int llz = WorldToLocal(wz);
            b = m_blocks[nidx % m_chunkCountX][wy][nidx / m_chunkCountX][llx][llz];
          }
        }
        const std::size_t si = static_cast<std::size_t>(wy) * (m_chunkSize + 2) * (m_chunkSize + 2)
                 + static_cast<std::size_t>(lz + 1) * (m_chunkSize + 2)
                             + static_cast<std::size_t>(lx + 1);
        pc.blockSnapshot[si] = b;
      }
    }
  }

  // Submit terrain meshing and snapshot conversion to the worker pool. Only
  // MutableMesh::ReplaceSnapshot touches the graphics API on the render thread.
  pc.meshSnapshot = std::make_shared<t850::MutableMeshSnapshot>();
  if (t850::g_threadPool && t850::g_threadPool->NumWorkers() > 0) {
    // Capture the snapshot + shared db by value so the worker never
    // touches the render thread's m_blocks.
    const int buildCx = pc.cx;
    const int buildCz = pc.cz;
    const std::vector<uint8_t> snapshot = pc.blockSnapshot;
    const std::shared_ptr<t850::MutableMeshSnapshot> meshSnapshot = pc.meshSnapshot;
    pc.future = t850::g_threadPool->Submit([this, buildCx, buildCz, snapshot, meshSnapshot]() {
      xF::XDataBase db;
      BuildChunkGeometryFromSnapshot(buildCx, buildCz, snapshot, db);
      ConvertChunkDatabase(db, *meshSnapshot);
    });
  } else {
    xF::XDataBase db;
    BuildChunkGeometryFromSnapshot(pc.cx, pc.cz, pc.blockSnapshot, db);
    ConvertChunkDatabase(db, *pc.meshSnapshot);
    pc.geometryReady = true;
  }

  {
    std::lock_guard<std::mutex> lk(m_pendingMutex);
    m_pendingChunks.push_back(std::move(pc));
  }
}

void MinecraftScene::QueueChunkRemesh(int cx, int cz) {
  const int idx = ChunkIndex(cx, cz);
  if (idx < 0) return;
  CancelPendingChunk(cx, cz);
  if (!m_asyncStreaming) {
    m_chunkDirty[idx / m_chunkCountX][idx % m_chunkCountX] = true;
    return;
  }
  const std::pair<int, int> chunk{cx, cz};
  if (std::find(m_chunksAwaitingMesh.begin(), m_chunksAwaitingMesh.end(), chunk) ==
      m_chunksAwaitingMesh.end()) {
    m_chunksAwaitingMesh.push_front(chunk);
  }
  m_chunkDirty[idx / m_chunkCountX][idx % m_chunkCountX] = false;
}

// Build the chunk's XDataBase geometry from the snapshot (background thread).
void MinecraftScene::BuildChunkGeometryFromSnapshot(int cx, int cz,
                                                    const std::vector<uint8_t>& snapshot,
                                                    xF::XDataBase& outDb) {
  const int baseX = cx * m_chunkSize;
  const int baseZ = cz * m_chunkSize;
  const uint8_t airBlock = BlockId(m_voxelSettings.terrain.air_block, 0);

  auto snapBlock = [&](int wx, int wy, int wz) -> uint8_t {
    if (wy < 0 || wy >= m_worldHeight) return airBlock;
    const int lx = wx - baseX;
    const int lz = wz - baseZ;
    if (lx < -1 || lx > m_chunkSize || lz < -1 || lz > m_chunkSize) return airBlock;
    const std::size_t si = static_cast<std::size_t>(wy) * (m_chunkSize + 2) * (m_chunkSize + 2)
               + static_cast<std::size_t>(lz + 1) * (m_chunkSize + 2)
                         + static_cast<std::size_t>(lx + 1);
    return snapshot[si];
  };

  xF::XDataBase& db = outDb;
  xF::xMeshContainer* mc = new xF::xMeshContainer;
  mc->FileName = "MinecraftChunk";
  db.XMeshDataBase.push_back(mc);
  mc->Geometry.resize(1);
  xF::xMeshGeometry& geom = mc->Geometry[0];
  geom.VertexAttributes = xF::xMeshGeometry::HAS_POSITION | xF::xMeshGeometry::HAS_NORMAL | xF::xMeshGeometry::HAS_TEXCOORD0;
  geom.NumChannelsTexCoords = 1;

  for (int lx = 0; lx < m_chunkSize; ++lx) {
    for (int lz = 0; lz < m_chunkSize; ++lz) {
      for (int wy = 0; wy < m_worldHeight; ++wy) {
        const int wx = baseX + lx;
        const int wz = baseZ + lz;
        const uint8_t block = snapBlock(wx, wy, wz);
        if (block == airBlock) continue;

        if (block == BlockId(m_voxelSettings.terrain.water_block, 0)) {
          if (snapBlock(wx, wy + 1, wz) != BlockId(m_voxelSettings.terrain.water_block, 0)) {
            AddFace(geom, wx, wy, wz, 2, block);
          }
          continue;
        }

        const uint8_t px = snapBlock(wx + 1, wy, wz);
        const uint8_t nx = snapBlock(wx - 1, wy, wz);
        const uint8_t py = snapBlock(wx, wy + 1, wz);
        const uint8_t ny = snapBlock(wx, wy - 1, wz);
        const uint8_t pz = snapBlock(wx, wy, wz + 1);
        const uint8_t nz = snapBlock(wx, wy, wz - 1);

        if (!IsBlockOpaque(px)) AddFace(geom, wx, wy, wz, 0, block);
        if (!IsBlockOpaque(nx)) AddFace(geom, wx, wy, wz, 1, block);
        if (!IsBlockOpaque(py)) AddFace(geom, wx, wy, wz, 2, block);
        if (!IsBlockOpaque(ny)) AddFace(geom, wx, wy, wz, 3, block);
        if (!IsBlockOpaque(pz)) AddFace(geom, wx, wy, wz, 4, block);
        if (!IsBlockOpaque(nz)) AddFace(geom, wx, wy, wz, 5, block);
      }
    }
  }

  geom.NumVertices = (xDWORD)geom.Positions.size();
  geom.NumTriangles = (xDWORD)(geom.Triangles.size() / 3);
  geom.NumIndices = (xDWORD)geom.Triangles.size();

  geom.MaterialList.Materials.resize(1);
  xF::xMaterial& mat = geom.MaterialList.Materials[0];
  mat.Name = "minecraft_block";
  mat.FaceColor = xF::STDRGBAColor(1.0f, 1.0f, 1.0f, 1.0f);
  mat.Power = 0.0f;
  const float materialSpecular = m_voxelSettings.material.specular;
  mat.Specular = xF::STDRGBAColor(materialSpecular, materialSpecular, materialSpecular, 1.0f);
  mat.Emissive = xF::STDRGBAColor(0.0f, 0.0f, 0.0f, 1.0f);
  mat.bEffects = true;
  mat.EffectInstance.NumDefaults = 2;
  mat.EffectInstance.pDefaults.resize(2);
  mat.EffectInstance.pDefaults[0].Type = xF::xEFFECTENUM::STDX_STRINGS;
  mat.EffectInstance.pDefaults[0].NameParam = "diffuseMap";
  mat.EffectInstance.pDefaults[0].CaseString = m_voxelSettings.material.diffuse_texture;
  mat.EffectInstance.pDefaults[1].Type = xF::xEFFECTENUM::STDX_FLOATS;
  mat.EffectInstance.pDefaults[1].NameParam = "pbrRoughness";
  mat.EffectInstance.pDefaults[1].CaseFloat.push_back(m_voxelSettings.material.roughness);
  geom.MaterialList.FaceIndices.assign(geom.NumTriangles, 0);
  geom.MaterialList.NumMatProcess = 1;

  xF::xFinalGeometry fg;
  const unsigned int v4 = 16;
  unsigned int vsz = v4 + v4 + 8;
  geom.VertexSize = vsz;
  fg.VertexSize = vsz;
  fg.NumVertex = geom.NumVertices;
  const unsigned int floatsPerVertex = vsz / 4;
  const unsigned int totalFloats = floatsPerVertex * geom.NumVertices;
  fg.pData = new float[totalFloats];
  fg.pDataDest = new float[totalFloats];
  unsigned int c = 0;
  for (unsigned int j = 0; j < geom.NumVertices; ++j) {
    fg.pData[c++] = geom.Positions[j].x;
    fg.pData[c++] = geom.Positions[j].y;
    fg.pData[c++] = geom.Positions[j].z;
    fg.pData[c++] = 1.0f;
    fg.pData[c++] = geom.Normals[j].x;
    fg.pData[c++] = geom.Normals[j].y;
    fg.pData[c++] = geom.Normals[j].z;
    fg.pData[c++] = 0.0f;
    fg.pData[c++] = geom.TexCoordinates[0][j].x;
    fg.pData[c++] = geom.TexCoordinates[0][j].y;
  }
  for (unsigned int j = 0; j < c; ++j) fg.pDataDest[j] = fg.pData[j];

  xF::xSubsetInfo si;
  si.NumTris = geom.NumTriangles;
  si.NumVertex = geom.NumVertices;
  si.VertexSize = vsz;
  si.VertexAttrib = geom.VertexAttributes;
  si.bAlignedVertex = true;
  fg.Subsets.push_back(si);
  db.MeshInfo.push_back(std::move(fg));
}

void MinecraftScene::ConvertChunkDatabase(
    const xF::XDataBase& db, t850::MutableMeshSnapshot& snapshot) const {
  snapshot = {};
  if (db.XMeshDataBase.empty() || !db.XMeshDataBase[0] ||
      db.XMeshDataBase[0]->Geometry.empty()) return;
  const xF::xMeshGeometry& geometry = db.XMeshDataBase[0]->Geometry[0];
  snapshot.vertices.reserve(geometry.Positions.size());
  for (std::size_t index = 0; index < geometry.Positions.size(); ++index) {
    t850::MutableMeshVertex vertex;
    vertex.position = geometry.Positions[index];
    if (index < geometry.Normals.size()) vertex.normal = geometry.Normals[index];
    if (geometry.NumChannelsTexCoords > 0 && index < geometry.TexCoordinates[0].size()) {
      vertex.u = geometry.TexCoordinates[0][index].x;
      vertex.v = geometry.TexCoordinates[0][index].y;
    }
    snapshot.vertices.push_back(vertex);
  }
  snapshot.indices.reserve(geometry.Triangles.size());
  for (xWORD index : geometry.Triangles) snapshot.indices.push_back(index);
  if (!snapshot.indices.empty()) {
    t850::MutableMeshMaterial material;
    material.baseColor = XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f);
    material.roughness = m_voxelSettings.material.roughness;
    material.usesBaseColorTexture = true;
    snapshot.materials.push_back(material);
    snapshot.sections.push_back({0, static_cast<uint32_t>(snapshot.indices.size()), 0});
    t850::RecalculateMutableMeshBounds(snapshot);
  }
}

// Upload a ready chunk's mesh to the GPU and create the instance (render thread).
void MinecraftScene::UploadChunkMesh(PendingChunk& pc) {
  const int idx = ChunkIndex(pc.cx, pc.cz);
  if (idx < 0) return;

  t850::MutableMesh* mesh = dynamic_cast<t850::MutableMesh*>(Meshes[idx].pBase);
  if (!mesh) {
    if (Meshes[idx].pBase) {
      Meshes[idx].pBase->Destroy();
      delete Meshes[idx].pBase;
    }
    mesh = new t850::MutableMesh();
    mesh->SetEngineContext(pEngineContext);
    mesh->SetSceneProps(&SceneProp);
    mesh->Create();
    Meshes[idx].CreateInstance(mesh, &VP);
    Meshes[idx].SetTexture(m_atlasTexture, 0);
  }
  std::string error;
  if (!pc.meshSnapshot || !mesh->ReplaceSnapshot(std::move(*pc.meshSnapshot), &error, false)) {
    T8_LOG_ERROR("[Minecraft] Chunk (%d,%d) mutable mesh upload failed: %s",
                 pc.cx, pc.cz, error.c_str());
    Meshes[idx].SetVisible(false);
  } else {
    Meshes[idx].SetVisible(mesh->Ready());
    Meshes[idx].Update();
  }
  m_chunkBuilt[idx / m_chunkCountX][idx % m_chunkCountX] = true;
  m_chunkDirty[idx / m_chunkCountX][idx % m_chunkCountX] = false;
}

// Called each frame: upload a limited number of ready chunks to the GPU.
void MinecraftScene::ProcessPendingChunks() {
  if (m_chunkGenerationFuture.valid() &&
      m_chunkGenerationFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    try {
      m_chunkGenerationFuture.get();
      if (m_generatedChunkBatch) {
        std::vector<std::pair<int, int>> chunksToRemesh;
        for (const GeneratedChunkData& generated : *m_generatedChunkBatch) {
          const int idx = ChunkIndex(generated.cx, generated.cz);
          if (idx < 0) continue;
          const int gx = idx % m_chunkCountX;
          const int gz = idx / m_chunkCountX;
          for (int wy = 0; wy < m_worldHeight; ++wy) {
            for (int lx = 0; lx < m_chunkSize; ++lx) {
              for (int lz = 0; lz < m_chunkSize; ++lz) {
                const std::size_t source =
                  (static_cast<std::size_t>(wy) * m_chunkSize + lx) * m_chunkSize + lz;
                m_blocks[gx][wy][gz][lx][lz] = generated.blocks[source];
              }
            }
          }
          m_chunkBuilt[gz][gx] = false;
          m_chunkDirty[gz][gx] = false;
          for (int neighborZ = generated.cz - 1; neighborZ <= generated.cz + 1; ++neighborZ) {
            for (int neighborX = generated.cx - 1; neighborX <= generated.cx + 1; ++neighborX) {
              const std::pair<int, int> chunk{neighborX, neighborZ};
              if (ChunkIndex(neighborX, neighborZ) >= 0 &&
                  std::find(chunksToRemesh.begin(), chunksToRemesh.end(), chunk) == chunksToRemesh.end())
                chunksToRemesh.push_back(chunk);
            }
          }
        }
        for (const auto& chunk : chunksToRemesh) {
          GenerateChunkTrees(chunk.first, chunk.second, false);
          m_chunksAwaitingMesh.push_back(chunk);
        }
        m_generatedChunkBatch.reset();
      }
    } catch (const std::exception& error) {
      T8_LOG_ERROR("[Minecraft] Background chunk generation failed: %s", error.what());
    }
  }

  for (auto it = m_retiredChunkBuilds.begin(); it != m_retiredChunkBuilds.end();) {
    if (it->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++it;
      continue;
    }
    try { it->get(); }
    catch (const std::exception& error) {
      T8_LOG_ERROR("[Minecraft] Canceled chunk build failed: %s", error.what());
    }
    it = m_retiredChunkBuilds.erase(it);
  }

  const int maxDispatches = (std::max)(2, m_maxUploadsPerFrame * 4);
  for (int dispatched = 0;
       dispatched < maxDispatches && !m_chunksAwaitingMesh.empty(); ++dispatched) {
    const auto chunk = m_chunksAwaitingMesh.front();
    m_chunksAwaitingMesh.pop_front();
    if (ChunkIndex(chunk.first, chunk.second) >= 0)
      EnqueueChunkBuild(chunk.first, chunk.second);
  }
  int uploaded = 0;
  std::deque<PendingChunk> notReady;
  for (;;) {
    PendingChunk pc;
    {
      std::lock_guard<std::mutex> lk(m_pendingMutex);
      if (m_pendingChunks.empty()) break;
      pc = std::move(m_pendingChunks.front());
      m_pendingChunks.pop_front();
    }
    // Only upload chunks whose background geometry build has finished.
    // Non-blocking check so the render thread is never stalled.
    if (pc.future.valid() && pc.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      notReady.push_back(std::move(pc));
      continue;
    }
    if (pc.future.valid()) {
      try { pc.future.get(); }
      catch (const std::exception& error) {
        T8_LOG_ERROR("[Minecraft] Chunk (%d,%d) mesh build failed: %s",
                     pc.cx, pc.cz, error.what());
        continue;
      }
    }
    // The background build finished; the geometry is ready to upload.
    pc.geometryReady = true;
    if (pc.geometryReady) {
      UploadChunkMesh(pc);
      ++uploaded;
      if (uploaded >= m_maxUploadsPerFrame) {
        // Put the rest back for later frames.
        {
          std::lock_guard<std::mutex> lk(m_pendingMutex);
          while (!m_pendingChunks.empty()) {
            notReady.push_back(std::move(m_pendingChunks.front()));
            m_pendingChunks.pop_front();
          }
        }
        break;
      }
    }
  }
  // Re-queue anything not yet uploaded.
  if (!notReady.empty()) {
    std::lock_guard<std::mutex> lk(m_pendingMutex);
    while (!notReady.empty()) {
      m_pendingChunks.push_front(std::move(notReady.back()));
      notReady.pop_back();
    }
  }
}

// Remove a chunk from the async pending queue. Called when a block is
// edited in that chunk so the synchronous RebuildDirtyChunks path (which
// has the up-to-date block data) is the ONLY path that rebuilds/upload
// the chunk's mesh. Without this, ProcessPendingChunks could upload a
// STALE snapshot mesh for the same chunk in the same frame, racing with
// BuildChunkMesh (both destroy+recreate Meshes[idx].pBase) -> double
// free / use-after-free of GPU resources -> DXGI_ERROR_DEVICE_REMOVED.
void MinecraftScene::CancelPendingChunk(int cx, int cz) {
  std::lock_guard<std::mutex> lk(m_pendingMutex);
  for (auto it = m_pendingChunks.begin(); it != m_pendingChunks.end();) {
    if (it->cx == cx && it->cz == cz) {
      if (it->future.valid()) m_retiredChunkBuilds.push_back(std::move(it->future));
      it = m_pendingChunks.erase(it);
    } else {
      ++it;
    }
  }
  m_chunksAwaitingMesh.erase(
    std::remove(m_chunksAwaitingMesh.begin(), m_chunksAwaitingMesh.end(), std::pair<int, int>{cx, cz}),
    m_chunksAwaitingMesh.end());
}

// ── Texture atlas ────────────────────────────────────────────────────
// When voxel_world.atlas_texture names an image file, load the real tile
// atlas through the backend-neutral Framework loader (works on every API).
// Otherwise fall back to the procedural solid-color atlas so scenes and
// selftests without a texture file keep working unchanged.
bool MinecraftScene::BuildRealTextureAtlas() {
  const t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->driver) return false;

  t850::TextureAtlasDesc desc;
  desc.texturePath = m_atlasTexturePath;
  desc.tileWidthPx = m_atlasTilePx;
  desc.tileHeightPx = m_atlasTilePx;
  desc.pixelationFactor = m_atlasPixelationFactor;
  std::string error;
  t850::TextureAtlas atlas = t850::LoadTextureAtlas(engineContext->driver, desc, &error);
  if (!atlas.IsValid()) {
    T8_LOG_ERROR("[Minecraft] Could not load atlas texture '%s': %s; using procedural fallback",
                 m_atlasTexturePath.c_str(), error.c_str());
    return false;
  }

  m_textureAtlas = std::move(atlas);
  m_atlasTexIndex = m_textureAtlas.textureId;
  m_atlasTexture = engineContext->driver->GetTexture(m_atlasTexIndex);
  m_atlasSize = m_textureAtlas.widthPx;
  m_atlasTiles = m_textureAtlas.columns;
  return m_atlasTexture != nullptr;
}

bool MinecraftScene::BuildTextureAtlas() {
  if (!m_atlasTexturePath.empty() && BuildRealTextureAtlas()) {
    for (const BlockDef& block : m_blockDefs) {
      for (const BlockTile& tile : block.tiles) {
        t850::TextureAtlasRegion region;
        if (!m_textureAtlas.TryGetGridRegion(tile.u, tile.v, region)) {
          T8_LOG_ERROR("[Minecraft] Block '%s' references atlas tile (%d,%d) outside %dx%d grid",
                       block.name.c_str(), tile.u, tile.v,
                       m_textureAtlas.columns, m_textureAtlas.rows);
          return false;
        }
      }
    }
    return true;
  }

  if (m_atlasTiles <= 0 || m_atlasSize <= 0 || m_atlasSize % m_atlasTiles != 0) {
    T8_LOG_ERROR("[Minecraft] Invalid procedural atlas layout size=%d tiles=%d",
                 m_atlasSize, m_atlasTiles);
    return false;
  }
  const int tilePx = m_atlasSize / m_atlasTiles;
  for (const BlockDef& block : m_blockDefs) {
    for (const BlockTile& tile : block.tiles) {
      if (tile.u < 0 || tile.u >= m_atlasTiles || tile.v < 0 || tile.v >= m_atlasTiles) {
        T8_LOG_ERROR("[Minecraft] Block '%s' references fallback atlas tile (%d,%d) outside %dx%d grid",
                     block.name.c_str(), tile.u, tile.v, m_atlasTiles, m_atlasTiles);
        return false;
      }
    }
  }
  std::vector<unsigned char> pixels(m_atlasSize * m_atlasSize * 4, 0);

  for (const BlockDef& block : m_blockDefs) {
    for (const BlockTile& tile : block.tiles) {
      const int px0 = tile.u * tilePx;
      const int py0 = tile.v * tilePx;
      for (int y = 0; y < tilePx; ++y) {
        for (int x = 0; x < tilePx; ++x) {
          const int idx = ((py0 + y) * m_atlasSize + (px0 + x)) * 4;
          pixels[idx + 0] = block.color[0];
          pixels[idx + 1] = block.color[1];
          pixels[idx + 2] = block.color[2];
          pixels[idx + 3] = block.color[3];
        }
      }
    }
  }

  // Register the fallback through the same Framework ownership path.
  const t850::EngineContext* engineContext = GetEngineContext();
  if (!engineContext) engineContext = &t850::GetEngineContext();
  if (!engineContext || !engineContext->driver) return false;

  std::string error;
  m_textureAtlas = t850::CreateTextureAtlas(
    engineContext->driver, "minecraft_procedural", pixels.data(),
    m_atlasSize, m_atlasSize, tilePx, tilePx, 0, &error);
  if (!m_textureAtlas.IsValid()) {
    T8_LOG_ERROR("[Minecraft] Could not create procedural atlas: %s", error.c_str());
    return false;
  }
  m_atlasTexIndex = m_textureAtlas.textureId;
  m_atlasTexture = engineContext->driver->GetTexture(m_atlasTexIndex);
  T8_LOG_INFO("[Minecraft] Built procedural texture atlas %dx%d", m_atlasSize, m_atlasSize);
  return m_atlasTexture != nullptr;
}

// ── Player ───────────────────────────────────────────────────────────
void MinecraftScene::UpdatePlayer(float dt) {
  // Use the input captured in OnInput
  t850::CharacterControllerContext ctx;
  ctx.collisionWorld = this;
  m_player.UpdateFps(dt, m_playerInput, ctx);

  // Stabilize the Y position when grounded: snap the feet to rest exactly
  // on the ground block so the capsule doesn't oscillate (vibrate) between
  // the collision resolution and gravity each frame.
  if (m_player.IsGrounded()) {
    const XVECTOR3 center = m_player.GetPosition();
    const float feetY = center.y - (m_playerSettings.capsuleHalfHeight + m_playerSettings.capsuleRadius);
    const int groundBlockY = (int)std::floor(feetY - 0.001f);
    if (groundBlockY >= 0 && groundBlockY < m_worldHeight) {
      const int bx = (int)std::floor(center.x);
      const int bz = (int)std::floor(center.z);
      if (IsBlockSolid(GetBlock(bx, groundBlockY, bz))) {
        const float targetFeetY = (float)(groundBlockY + 1);
        const float targetCenterY = targetFeetY + (m_playerSettings.capsuleHalfHeight + m_playerSettings.capsuleRadius);
        if (std::fabs(targetCenterY - center.y) < 0.5f) {
          m_player.SetPosition(XVECTOR3(center.x, targetCenterY, center.z, 1.0f));
        }
      }
    }
  }

  // Update camera from player
  const XVECTOR3 eye = m_player.GetEyePosition();
  m_playerEye = eye;
  Cam.Eye = eye;
  Cam.Yaw = m_playerYaw;
  Cam.Pitch = m_playerPitch;
  Cam.Roll = 0.0f;
  Cam.Update(dt);
  VP = Cam.VP;
}

// ── Raycast for block interaction ────────────────────────────────────
void MinecraftScene::RaycastBlocks(const XVECTOR3& origin, const XVECTOR3& dir, float maxDist,
                                   int& outX, int& outY, int& outZ,
                                   int& outPrevX, int& outPrevY, int& outPrevZ) const {
  // DDA voxel traversal
  int x = (int)std::floor(origin.x);
  int y = (int)std::floor(origin.y);
  int z = (int)std::floor(origin.z);
  const int stepX = (dir.x > 0) ? 1 : (dir.x < 0 ? -1 : 0);
  const int stepY = (dir.y > 0) ? 1 : (dir.y < 0 ? -1 : 0);
  const int stepZ = (dir.z > 0) ? 1 : (dir.z < 0 ? -1 : 0);
  const float tDeltaX = (dir.x != 0) ? std::fabs(1.0f / dir.x) : 1e30f;
  const float tDeltaY = (dir.y != 0) ? std::fabs(1.0f / dir.y) : 1e30f;
  const float tDeltaZ = (dir.z != 0) ? std::fabs(1.0f / dir.z) : 1e30f;
  float tMaxX = (dir.x != 0) ? ((stepX > 0 ? (x + 1 - origin.x) : (origin.x - x)) * tDeltaX) : 1e30f;
  float tMaxY = (dir.y != 0) ? ((stepY > 0 ? (y + 1 - origin.y) : (origin.y - y)) * tDeltaY) : 1e30f;
  float tMaxZ = (dir.z != 0) ? ((stepZ > 0 ? (z + 1 - origin.z) : (origin.z - z)) * tDeltaZ) : 1e30f;

  int prevX = x, prevY = y, prevZ = z;
  float t = 0.0f;
  while (t <= maxDist) {
    if (GetBlock(x, y, z) != BlockId(m_voxelSettings.terrain.air_block, 0)) {
      outX = x; outY = y; outZ = z;
      outPrevX = prevX; outPrevY = prevY; outPrevZ = prevZ;
      return;
    }
    prevX = x; prevY = y; prevZ = z;
    if (tMaxX < tMaxY && tMaxX < tMaxZ) {
      x += stepX; t = tMaxX; tMaxX += tDeltaX;
    } else if (tMaxY < tMaxZ) {
      y += stepY; t = tMaxY; tMaxY += tDeltaY;
    } else {
      z += stepZ; t = tMaxZ; tMaxZ += tDeltaZ;
    }
  }
  outX = outY = outZ = -1;
  outPrevX = outPrevY = outPrevZ = -1;
}

void MinecraftScene::HandleBlockInteraction(InputManager* IManager) {
  // Raycast from eye along look direction
  const XVECTOR3 dir = Cam.Look;
  int bx, by, bz, px, py, pz;
  RaycastBlocks(m_playerEye, dir, m_voxelSettings.interaction.reach, bx, by, bz, px, py, pz);

  m_highlightVisible = (bx >= 0);
  if (m_highlightVisible) {
    m_highlightX = bx; m_highlightY = by; m_highlightZ = bz;
    m_lastHighlightX = bx; m_lastHighlightY = by; m_lastHighlightZ = bz;
    m_lastHighlightValid = true;
  }

  m_breakCooldown -= DtSecs;
  m_placeCooldown -= DtSecs;

  const bool gamepadActive = IManager->Gamepad.connected && IManager->Gamepad.enabled;
  const bool breakInput = IManager->PressedMouseButton(0) ||
    (gamepadActive && IManager->Gamepad.rightTrigger > 0.5f);
  const bool placeInput = IManager->PressedMouseButton(2) ||
    (gamepadActive && IManager->Gamepad.leftTrigger > 0.5f);

  // Left click / right trigger: break block.
  if (breakInput && m_breakCooldown <= 0.0f) {
    if (m_highlightVisible) {
      const uint8_t removedBlock = GetBlock(bx, by, bz);
      const std::string removed = removedBlock < m_blockDefs.size()
        ? m_blockDefs[removedBlock].name : std::string{};
      SetBlock(bx, by, bz, BlockId(m_voxelSettings.terrain.air_block, 0));
      m_breakCooldown = m_voxelSettings.interaction.break_cooldown;
      m_interactionMessage = removed.empty() ? "Block removed" : "Removed " + removed;
      m_interactionMessageTime = 1.25f;
      if (!m_weaponSwinging) { m_weaponSwinging = true; m_weaponSwing = 0.0f; }
    } else {
      m_interactionMessage = "No block within reach";
      m_interactionMessageTime = 0.75f;
    }
  }
  // Right click / left trigger: place block.
  if (placeInput && m_placeCooldown <= 0.0f && m_highlightVisible) {
    // Don't place inside the player
    const int ex = (int)std::floor(m_playerEye.x);
    const int ey = (int)std::floor(m_playerEye.y);
    const int ez = (int)std::floor(m_playerEye.z);
    if (!(px == ex && py == ey && pz == ez) &&
        !(px == ex && py == ey - 1 && pz == ez)) {
      SetBlock(px, py, pz, (uint8_t)m_selectedBlock);
      m_placeCooldown = m_voxelSettings.interaction.place_cooldown;
      m_interactionMessage = "Placed " + m_blockDefs[m_selectedBlock].name;
      m_interactionMessageTime = 1.25f;
      if (!m_weaponSwinging) { m_weaponSwinging = true; m_weaponSwing = 0.0f; }
      T8_LOG_INFO("[Minecraft] Placed %s at (%d,%d,%d) (hit %d,%d,%d)",
                  m_blockDefs[m_selectedBlock].name.c_str(), px, py, pz, bx, by, bz);
    } else {
      m_interactionMessage = "Cannot place inside player";
      m_interactionMessageTime = 1.25f;
      T8_LOG_INFO("[Minecraft] Place rejected: cell (%d,%d,%d) inside player (eye %d,%d,%d)",
                  px, py, pz, ex, ey, ez);
    }
  } else if (placeInput && m_placeCooldown <= 0.0f && !m_highlightVisible) {
    m_interactionMessage = "No block within reach";
    m_interactionMessageTime = 0.75f;
  }
}

// Draw a wireframe box around the currently targeted block so the player
// can see where they are aiming (break/place feedback).
// ── Collision world (CharacterCollisionWorld) ────────────────────────
bool MinecraftScene::SweepCapsule(const t850::CharacterCollisionSweep& sweep, t850::CharacterCollisionHit& outHit) const {
  outHit = t850::CharacterCollisionHit{};
  // Voxel-based capsule sweep: sample the AABB of the capsule along the
  // displacement and find the first solid block.
  const float radius = sweep.radius;
  const float halfH = sweep.halfHeight;
  const XVECTOR3 start = sweep.startCenter;
  const XVECTOR3 disp = sweep.displacement;
  const float dist = Length3(disp);
  if (dist < 0.0001f) return false;

  const XVECTOR3 dir = disp / dist;
  // Fine sampling for precise collision (reduces Y-axis jitter when the
  // capsule rests on the ground).
  const int steps = (int)std::ceil(dist / 0.1f) + 1;
  float bestFraction = 1.0f;
  bool hit = false;
  XVECTOR3 hitNormal(0, 1, 0, 0);

  for (int s = 0; s <= steps; ++s) {
    const float t = (float)s / (float)steps;
    const XVECTOR3 center = start + dir * (dist * t);
    // Capsule AABB: center +/- (radius, halfH+radius, radius)
    const int minX = (int)std::floor(center.x - radius);
    const int maxX = (int)std::floor(center.x + radius);
    const int minY = (int)std::floor(center.y - halfH - radius);
    const int maxY = (int)std::floor(center.y + halfH + radius);
    const int minZ = (int)std::floor(center.z - radius);
    const int maxZ = (int)std::floor(center.z + radius);

    for (int by = minY; by <= maxY; ++by) {
      for (int bz = minZ; bz <= maxZ; ++bz) {
        for (int bx = minX; bx <= maxX; ++bx) {
          if (IsBlockSolid(GetBlock(bx, by, bz))) {
            // Found a hit at this sample; compute fraction
            const float frac = t;
            if (frac < bestFraction) {
              bestFraction = frac;
              hit = true;
              // Approximate normal from the block face the center is closest to
              const float cx = center.x - (bx + 0.5f);
              const float cy = center.y - (by + 0.5f);
              const float cz = center.z - (bz + 0.5f);
              const float ax = std::fabs(cx), ay = std::fabs(cy), az = std::fabs(cz);
              if (ax >= ay && ax >= az) hitNormal = XVECTOR3(cx > 0 ? 1 : -1, 0, 0, 0);
              else if (ay >= az) hitNormal = XVECTOR3(0, cy > 0 ? 1 : -1, 0, 0);
              else hitNormal = XVECTOR3(0, 0, cz > 0 ? 1 : -1, 0);
            }
            break;
          }
        }
      }
    }
  }

  if (hit) {
    outHit.hit = true;
    outHit.fraction = bestFraction;
    outHit.position = start + dir * (dist * bestFraction);
    outHit.normal = hitNormal;
  }
  return hit;
}

bool MinecraftScene::SweepBox(const t850::CharacterBoxSweep& sweep, t850::CharacterCollisionHit& outHit) const {
  outHit = t850::CharacterCollisionHit{};
  const XVECTOR3 half = sweep.halfExtents;
  const XVECTOR3 start = sweep.startCenter;
  const XVECTOR3 disp = sweep.displacement;
  const float dist = Length3(disp);
  if (dist < 0.0001f) return false;
  const XVECTOR3 dir = disp / dist;
  const int steps = (int)std::ceil(dist / m_voxelSettings.player.collision_sweep_step) + 1;
  float bestFraction = 1.0f;
  bool hit = false;
  XVECTOR3 hitNormal(0, 1, 0, 0);

  for (int s = 0; s <= steps; ++s) {
    const float t = (float)s / (float)steps;
    const XVECTOR3 center = start + dir * (dist * t);
    const int minX = (int)std::floor(center.x - half.x);
    const int maxX = (int)std::floor(center.x + half.x);
    const int minY = (int)std::floor(center.y - half.y);
    const int maxY = (int)std::floor(center.y + half.y);
    const int minZ = (int)std::floor(center.z - half.z);
    const int maxZ = (int)std::floor(center.z + half.z);

    for (int by = minY; by <= maxY; ++by) {
      for (int bz = minZ; bz <= maxZ; ++bz) {
        for (int bx = minX; bx <= maxX; ++bx) {
          if (IsBlockSolid(GetBlock(bx, by, bz))) {
            if (t < bestFraction) {
              bestFraction = t;
              hit = true;
              const float cx = center.x - (bx + 0.5f);
              const float cy = center.y - (by + 0.5f);
              const float cz = center.z - (bz + 0.5f);
              const float ax = std::fabs(cx), ay = std::fabs(cy), az = std::fabs(cz);
              if (ax >= ay && ax >= az) hitNormal = XVECTOR3(cx > 0 ? 1 : -1, 0, 0, 0);
              else if (ay >= az) hitNormal = XVECTOR3(0, cy > 0 ? 1 : -1, 0, 0);
              else hitNormal = XVECTOR3(0, 0, cz > 0 ? 1 : -1, 0);
            }
            break;
          }
        }
      }
    }
  }

  if (hit) {
    outHit.hit = true;
    outHit.fraction = bestFraction;
    outHit.position = start + dir * (dist * bestFraction);
    outHit.normal = hitNormal;
  }
  return hit;
}

bool MinecraftScene::QueryTriggerTouch(const t850::CharacterTriggerQuery& query, t850::CharacterTriggerTouch& outTouch) const {
  (void)query;
  outTouch = t850::CharacterTriggerTouch{};
  return false;
}

// ── Scene lifecycle ──────────────────────────────────────────────────
void MinecraftScene::InitVars() {
  if (!g_config.sceneFilePath.empty())
    m_sceneFilePath = g_config.sceneFilePath;
  if (!LoadAuthoredScene())
    return;

  T8_LOG_INFO("[Minecraft] Authored scene '%s': chunk=%d height=%d water=%d renderDistance=%d blocks=%zu",
              m_sceneFilePath.c_str(), m_chunkSize, m_worldHeight, m_waterLevel,
              m_renderDistance, m_blockDefs.size());

  if (!m_sceneFile.control_descriptor.empty() && m_controlSetup.Load(m_sceneFile.control_descriptor)) {
    m_controlSetup.ApplyQualityAndSettings(SceneProp);
  } else {
    T8_LOG_ERROR("[Minecraft] Failed to load control descriptor '%s'",
                 m_sceneFile.control_descriptor.c_str());
    return;
  }

  if (!m_sceneFile.profiles.empty()) {
    const auto& profile = m_sceneFile.profiles.front();
    for (const auto& entry : profile.sliders) {
      if (entry.name == "exposure") SceneProp.Exposure = entry.value;
      else if (entry.name == "bloom_factor") SceneProp.BloomFactor = entry.value;
      else if (entry.name == "bloom_threshold") SceneProp.BloomThreshold = entry.value;
      else if (entry.name == "tm_white_level") SceneProp.ToneMapWhiteLevel = entry.value;
      else if (entry.name == "tm_adapt_tau") SceneProp.LuminanceTau = entry.value;
      else if (entry.name == "pcf_radius") SceneProp.PCFScale = entry.value;
      else if (entry.name == "pcf_samples") SceneProp.PCFSamples = entry.value;
      else if (entry.name == "ssao_kernel_size") SceneProp.SSAOKernel.KernelSize = (int)entry.value;
      else if (entry.name == "ssao_radius") SceneProp.SSAOKernel.Radius = entry.value;
      else if (entry.name == "dof_focus_range") SceneProp.DOFFocusRange = entry.value;
      else if (entry.name == "dof_focus_falloff") SceneProp.DOFFocusFalloff = entry.value;
      else if (entry.name == "dof_auto_focus_radius") SceneProp.DOFAutoFocusRadius = entry.value;
      else if (entry.name == "dof_max_coc") SceneProp.MaxCoc = entry.value;
      else if (entry.name == "dof_far_samples") SceneProp.DOF_Far_Samples_squared = entry.value;
      else if (entry.name == "dof_near_samples") SceneProp.DOF_Near_Samples_squared = entry.value;
      else if (entry.name == "parallax_low_samples") SceneProp.ParallaxLowSamples = entry.value;
      else if (entry.name == "parallax_high_samples") SceneProp.ParallaxHighSamples = entry.value;
      else if (entry.name == "parallax_height") SceneProp.ParallaxHeight = entry.value;
      else if (entry.name == "shadow_bias") SceneProp.ShadowBias = entry.value;
      else if (entry.name == "shadow_min") SceneProp.ShadowMin = entry.value;
      else if (entry.name == "env_factor") SceneProp.EnvFactor = entry.value;
      else if (entry.name == "ibl_factor") SceneProp.IBLFactor = entry.value;
      else if (entry.name == "light_radius_scale") SceneProp.LightRadiusScale = entry.value;
      else if (entry.name == "light_intensity_scale") SceneProp.LightIntensityScale = entry.value;
      else if (entry.name == "lightmap_intensity") SceneProp.LightmapIntensity = entry.value;
      else if (entry.name == "material_emissive_intensity") SceneProp.MaterialEmissiveIntensity = entry.value;
      else if (entry.name == "material_transmission_multiplier") SceneProp.MaterialTransmissionMultiplier = entry.value;
      else if (entry.name == "material_refraction_strength") SceneProp.MaterialRefractionStrength = entry.value;
      else if (entry.name == "parallax_shadow_min_layers") SceneProp.ParallaxShadowMinLayers = entry.value;
      else if (entry.name == "parallax_shadow_max_layers") SceneProp.ParallaxShadowMaxLayers = entry.value;
      else if (entry.name == "parallax_shadow_softness") SceneProp.ParallaxShadowSoftness = entry.value;
      else if (entry.name == "parallax_shadow_strength") SceneProp.ParallaxShadowStrength = entry.value;
    }
    for (const auto& entry : profile.checkboxes) {
      if (entry.name == "shadow_toggle") SceneProp.ToogleShadow = entry.value;
      else if (entry.name == "ssao_toggle") SceneProp.ToogleSSAO = entry.value;
      else if (entry.name == "dof_toggle") SceneProp.ToogleDOF = entry.value;
      else if (entry.name == "dof_auto_focus") SceneProp.AutoFocus = entry.value;
      else if (entry.name == "parallax_toggle") SceneProp.ToogleParallax = entry.value;
      else if (entry.name == "parallax_shadow_toggle") SceneProp.ToogleParallaxShadow = entry.value;
      else if (entry.name == "point_lights_enabled") SceneProp.PointLightsEnabled = entry.value;
    }
    for (const auto& entry : profile.selectors) {
      if (entry.name == "luminance_mode") SceneProp.LuminanceMode = entry.value;
      else if (entry.name == "active_gauss_kernel") {
        m_selectedGaussKernel = entry.value;
        SceneProp.ActiveGaussKernel = entry.value;
      }
    }
    SceneProp.SSAOKernel.Update();
  }

  auto initCamera = [](const t850::scene::SceneCameraDesc& source, Camera& camera) {
    const XVECTOR3 eye(source.position.x, source.position.y, source.position.z, 1.0f);
    if (source.type == 1)
      camera.InitOrtho(eye, source.ortho_w, source.ortho_h,
                       source.near_plane, source.far_plane, true);
    else
      camera.InitPerspective(eye, Deg2Rad(source.fov_deg), 16.0f / 9.0f,
                             source.near_plane, source.far_plane, true);
    camera.SetLookAt(XVECTOR3(source.target.x, source.target.y, source.target.z, 1.0f));
  };
  auto initLightCamera = [](const t850::scene::SceneLightCameraDesc& source, Camera& camera) {
    const XVECTOR3 eye(source.position.x, source.position.y, source.position.z, 1.0f);
    if (source.type == 1)
      camera.InitOrtho(eye, source.ortho_w, source.ortho_h,
                       source.near_plane, source.far_plane, true);
    else
      camera.InitPerspective(eye, Deg2Rad(source.fov_deg), 16.0f / 9.0f,
                             source.near_plane, source.far_plane, true);
    camera.SetLookAt(XVECTOR3(source.target.x, source.target.y, source.target.z, 1.0f));
  };

  if (m_sceneFile.cameras.empty() || m_sceneFile.light_cameras.empty()) {
    T8_LOG_ERROR("[Minecraft] Authored scene requires a main camera and debug light camera");
    return;
  }
  if (m_sceneFile.cameras.size() < 2) {
    auto spectator = m_sceneFile.cameras[0];
    spectator.name = "Minecraft Free Camera";
    spectator.position.y += 20.0f;
    spectator.position.z -= 30.0f;
    spectator.target.y += 10.0f;
    m_sceneFile.cameras.push_back(spectator);
    T8_LOG_INFO("[Minecraft] Scene has no free camera; created a spectator fallback");
  }
  initCamera(m_sceneFile.cameras[0], Cam);
  initCamera(m_sceneFile.cameras[1], SpectatorCam);
  initLightCamera(m_sceneFile.light_cameras[0], LightCam);
  m_debugCameraOrtho = m_sceneFile.light_cameras[0].type == 1;

  // Sync shadow panel state from the loaded descriptor.
  m_spectatorYaw = SpectatorCam.Yaw;
  m_spectatorPitch = SpectatorCam.Pitch;
  m_lightYaw = LightCam.Yaw;
  m_lightPitch = LightCam.Pitch;
  m_shadowResolution = SceneProp.ShadowMapResolution;
  m_shadowBias = SceneProp.ShadowBias;
  m_shadowMin = SceneProp.ShadowMin;
  m_shadowsEnabled = SceneProp.ToogleShadow != 0;

  ActiveCam = &Cam;
  SceneProp.AddCamera(ActiveCam);
  SceneProp.pCullingCamera = &Cam;
  SceneProp.AddLightCamera(&LightCam);

  for (const auto& source : m_sceneFile.lights) {
    const XVECTOR3 position(source.position.x, source.position.y, source.position.z, 1.0f);
    const XVECTOR3 direction(source.direction.x, source.direction.y, source.direction.z, 0.0f);
    const XVECTOR3 color(source.color.x, source.color.y, source.color.z, 1.0f);
    if (source.type == 0)
      SceneProp.AddDirectionalLight(direction, color, source.intensity, source.enabled);
    else
      SceneProp.AddLight(position, color, source.radius, source.intensity, LIGHT_POINT, source.enabled);
    Light& light = SceneProp.Lights.back();
    light.Id = source.id;
    light.Name = source.name;
    light.Position = position;
  }
  SceneProp.ActiveLights = (std::max)(0, (std::min)(
    m_voxelSettings.active_lights, (int)SceneProp.Lights.size()));

  const std::string sunId = m_sceneFile.light_cameras[0].attached_light_id;
  for (int i = 0; i < (int)SceneProp.Lights.size(); ++i) {
    if ((!sunId.empty() && SceneProp.Lights[i].Id == sunId) ||
        (sunId.empty() && SceneProp.Lights[i].Type == LIGHT_DIRECTIONAL)) {
      m_sunLightIndex = i;
      break;
    }
  }
  if (m_sunLightIndex < 0) {
    T8_LOG_ERROR("[Minecraft] Light camera has no matching directional Sun light");
    return;
  }
  if (m_sunTrajectoryPaused) {
    const auto& ambient = m_voxelSettings.day_night.manual_ambient;
    SceneProp.AmbientColor = XVECTOR3(ambient.x, ambient.y, ambient.z, 1.0f);
    SyncSunFromLightCamera();
  } else {
    UpdateDayNight(0.0f);
  }

  auto configureFilter = [&](GaussFilter& filter, std::size_t index) {
    if (index >= m_controlSetup.gaussFilters.size()) return false;
    filter = m_controlSetup.gaussFilters[index];
    filter.Update();
    return true;
  };
  if (!configureFilter(ShadowFilter, 0) || !configureFilter(BloomFilter, 1) ||
      !configureFilter(NearDOFFilter, 2)) {
    T8_LOG_ERROR("[Minecraft] Control descriptor requires three gauss_filters");
    return;
  }
  SceneProp.AddGaussKernel(&ShadowFilter);
  SceneProp.AddGaussKernel(&BloomFilter);
  SceneProp.AddGaussKernel(&NearDOFFilter);
  if (!m_sceneFile.profiles.empty()) {
    const auto& profile = m_sceneFile.profiles.front();
    GaussFilter* filters[] = {&ShadowFilter, &BloomFilter, &NearDOFFilter};
    for (int index = 0; index < 3; ++index) {
      const std::string prefix = "gauss_" + std::to_string(index) + "_";
      for (const auto& entry : profile.sliders) {
        if (entry.name == prefix + "radius") filters[index]->radius = entry.value;
        else if (entry.name == prefix + "sigma") filters[index]->sigma = entry.value;
      }
      for (const auto& entry : profile.selectors) {
        if (entry.name == prefix + "kernel_size") filters[index]->kernelSize = entry.value;
      }
      filters[index]->Update();
    }
  }

  const auto& player = m_voxelSettings.player;
  m_playerSettings.collisionShape = t850::KinematicCharacterSettings::CollisionShape::Capsule;
  m_playerSettings.walkSpeed = player.walk_speed;
  m_playerSettings.sprintSpeed = player.sprint_speed;
  m_playerSettings.groundAcceleration = player.ground_acceleration;
  m_playerSettings.airAcceleration = player.air_acceleration;
  m_playerSettings.friction = player.friction;
  m_playerSettings.stopSpeed = player.stop_speed;
  m_playerSettings.gravity = player.gravity;
  m_playerSettings.jumpSpeed = player.jump_speed;
  m_playerSettings.capsuleRadius = player.capsule_radius;
  m_playerSettings.capsuleHalfHeight = player.capsule_half_height;
  m_playerSettings.eyeHeight = player.eye_height;
  m_playerSettings.groundProbeDistance = player.ground_probe_distance;
  m_playerSettings.stepHeight = player.step_height;
  m_playerSettings.minWalkNormalY = player.min_walk_normal_y;
  m_playerSettings.allowSprint = player.allow_sprint;
  m_playerSettings.airControl = player.air_control;
  m_player.SetSettings(m_playerSettings);

  m_playerEye = XVECTOR3(player.spawn.x, player.spawn.y, player.spawn.z, 1.0f);
  m_player.SetPosition(m_playerEye - XVECTOR3(0.0f, m_playerSettings.eyeHeight, 0.0f, 0.0f));
  m_playerYaw = Cam.Yaw;
  m_playerPitch = Cam.Pitch;

  m_centerChunkX = WorldToChunk((int)std::floor(player.spawn.x));
  m_centerChunkZ = WorldToChunk((int)std::floor(player.spawn.z));

  t850::FrameDumperConfig dumpCfg;
  dumpCfg.dumpEnabled        = g_config.flags.dumpEnabled;
  dumpCfg.dumpByFrame        = g_config.flags.dumpByFrame;
  dumpCfg.dumpFrame          = g_config.dumpFrame;
  dumpCfg.dumpSeconds        = g_config.dumpSeconds;
  dumpCfg.debugFrames        = g_config.flags.debugFrames;
  dumpCfg.keepRunning        = g_config.flags.keepRunning;
  dumpCfg.replaySnapshotPath = g_config.replaySnapshotPath;
  dumpCfg.sceneIndex         = g_config.startScene;
  // In benchmark mode with --benchmarkFinalFrameDump, capture one frame when
  // the benchmark duration elapses, then exit. (DayScene has a dedicated
  // final-frame capture path; other scenes reuse the FrameDumper timed dump.)
  if (g_config.flags.benchmark && g_config.flags.benchmarkFinalFrameDump &&
      g_config.benchmarkDurationSeconds > 0) {
    dumpCfg.dumpEnabled = true;
    dumpCfg.dumpByFrame = false;
    dumpCfg.dumpSeconds = static_cast<float>(g_config.benchmarkDurationSeconds);
    dumpCfg.keepRunning = false;
  }
  m_dumper.Init(dumpCfg);
}

void MinecraftScene::CreateAssets() {
  if (!m_renderGraph.Load(m_sceneFile.render_graph)) {
    T8_LOG_ERROR("[Minecraft] Failed to load render graph");
    return;
  }
  std::string cfgError;
  std::vector<t850::ShadowProjectionOverrideDesc> shadowOverrides;
  if (!m_sceneFile.profiles.empty())
    shadowOverrides = m_sceneFile.profiles.front().shadow_projections;
  if (!shadowOverrides.empty()) {
    const auto& shadow = shadowOverrides.front();
    if (shadow.resolution) m_shadowResolution = (float)*shadow.resolution;
    if (shadow.cascade_count) m_cascadeCount = *shadow.cascade_count;
    if (shadow.split_lambda) m_splitLambda = *shadow.split_lambda;
  }
  if (!m_renderGraph.Configure(SceneProp, shadowOverrides, {}, &cfgError)) {
    T8_LOG_ERROR("[Minecraft] Render graph configure failed: %s", cfgError.c_str());
    return;
  }
  m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp);

  t850::sandbox::RefreshDeferredPassHandles(
      m_renderGraph,
      GBufferPass, DeferredPass, Extra16FPass, DepthPass,
      ShadowAccumPass, ExtraHelperPass, BloomAccumPass,
      AdaptedLumCurrentPass, AdaptedLumPrevPass);
  BrightPass = m_renderGraph.GetRTHandle("BrightPass");
  CoCPass = m_renderGraph.GetRTHandle("CoC");
  const bool dofEnabled = SceneProp.ToogleDOF != 0;
  m_renderGraph.SetPassEnabled("CoC", dofEnabled);
  m_renderGraph.SetPassEnabled("Combine CoC", dofEnabled);
  m_renderGraph.SetPassEnabled("DOF", dofEnabled);
  m_renderGraph.SetPassEnabled("DOF 2", dofEnabled);

  PrimitiveMgr.SetEngineContext(pEngineContext);
  PrimitiveMgr.Init();
  PrimitiveMgr.SetVP(&VP);

  SceneProp.SSAOKernel.InitTexture();

  EnvMapTexIndex = g_pBaseDriver->CreateTexture(m_currentCubemapPath);
  EnvMaps.SetFallback(EnvMapTexIndex);
  t850::EnvironmentResourcePaths envPaths;
  LoadEnvironmentIBLResources(
    g_pBaseDriver, envPaths, EnvMaps,
    DiffuseIBLTexIndex, SpecularIBLTexIndex, BrdfLUTTexIndex,
    SheenIBLTexIndex, CharlieLUTTexIndex, SheenELUTTexIndex);
  UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);

  // Fullscreen quad
  m.Identity();
  Quads[0].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[0], 0);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[1], 1);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[2], 2);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->vColorTextures[3], 3);
  Quads[0].SetTexture(pFramework->pVideoDriver->RTs[0]->pDepthTexture, 4);
  Quads[0].SetEnvironmentMap(g_pBaseDriver->GetTexture(EnvMapTexIndex));
  for (int i = 1; i <= 7; i++)
    Quads[i].CreateInstance(PrimitiveMgr.GetPrimitive(PrimitiveManager::QUAD), &m);
  PrimitiveMgr.SetSceneProps(&SceneProp);
  Quads[0].TranslateAbsolute(0.0f, 0.0f, 0.0f);
  Quads[0].Update();

  // Debug text
  m_debugText.LoadFromFile(24, "Fonts/Martius-LV9L4.ttf", 512.0f);
  m_lineRenderer.Create();
  m_navMeshDebugRenderer.Create();

  // Build the texture atlas
  if (!BuildTextureAtlas()) {
    T8_LOG_ERROR("[Minecraft] Texture atlas creation failed");
    return;
  }

  // Generate the world and build chunk meshes
  GenerateWorld();
  RebuildDirtyChunks();

  // Build the navigation mesh and spawn the mob
  BuildNavigationMesh();
  CreateMobMesh();

  // Create the first-person weapon (sword)
  CreateWeaponMesh();

  if (g_config.minecraftDrawDistance > 0 &&
      g_config.minecraftDrawDistance != m_renderDistance) {
    m_pendingRenderDistance = g_config.minecraftDrawDistance;
  }
}

void MinecraftScene::OnLoadScene() {
  InitVars();
  CreateAssets();
}

void MinecraftScene::OnDestoryScene() {
  DestroyAssets();
}

void MinecraftScene::DestroyAssets() {
  if (m_navMeshBuildFuture.valid()) m_navMeshBuildFuture.wait();
  m_pendingNavMeshBuild.reset();
  if (m_chunkGenerationFuture.valid()) m_chunkGenerationFuture.wait();
  {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    for (PendingChunk& chunk : m_pendingChunks)
      if (chunk.future.valid()) chunk.future.wait();
    m_pendingChunks.clear();
  }
  for (std::future<void>& build : m_retiredChunkBuilds)
    if (build.valid()) build.wait();
  m_retiredChunkBuilds.clear();
  SceneProp.SSAOKernel.Destroy();
  for (int i = 0; i < m_renderMeshCount; ++i) {
    if (Meshes[i].pBase) {
      Meshes[i].pBase->Destroy();
      delete Meshes[i].pBase;
      Meshes[i].pBase = nullptr;
    }
  }
  m_debugText.Destroy();
  m_lineRenderer.Destroy();
  m_navMeshDebugRenderer.Destroy();
  if (m_cascadeDebugVB) { m_cascadeDebugVB->release(); m_cascadeDebugVB = nullptr; }
  if (m_cascadeDebugIB) { m_cascadeDebugIB->release(); m_cascadeDebugIB = nullptr; }
  if (m_cascadeDebugSolidIB) { m_cascadeDebugSolidIB->release(); m_cascadeDebugSolidIB = nullptr; }
  if (m_voxelDebugVB) { m_voxelDebugVB->release(); m_voxelDebugVB = nullptr; }
  if (m_voxelDebugIB) { m_voxelDebugIB->release(); m_voxelDebugIB = nullptr; }
  PrimitiveMgr.DestroyPrimitives();
  if (pFramework && pFramework->pVideoDriver) {
    m_renderGraph.DestroyRenderTargets(pFramework->pVideoDriver);
  }
  m_atlasTexture = nullptr;
  m_atlasTexIndex = -1;
  m_textureAtlas = {};
}

void MinecraftScene::OnUpdate(float _DtSecs) {
  T8_TELEMETRY_SCOPE("minecraft.update");
  DtSecs = _DtSecs;
  SceneProp.FrameDeltaSec = DtSecs;
  m_interactionMessageTime = (std::max)(0.0f, m_interactionMessageTime - DtSecs);

  if (m_cameraMode != 0)
    m_playerInput = {};

  // Update player (input was captured in OnInput)
  UpdatePlayer(DtSecs);

  // Recenter the resident grid before accepting a completed navmesh so a
  // worker result can never publish against a center that changed this frame.
  UpdateChunkStreaming();
  ProcessNavigationMeshBuild();

  // Update the first-person weapon (sword) position + swing
  UpdateWeapon(DtSecs);

  // Update the path-following mob
  UpdateMob(DtSecs);

  // Update the day/night cycle (sun position, light color, ambient)
  UpdateDayNight(DtSecs);

  // Apply shadow panel settings (bias, min, lambda, cascade count, light cam)
  ApplyShadowSettings();

  for (int mesh = 0; mesh < m_renderMeshCount; ++mesh) {
    Meshes[mesh].SetParallaxEnabled(SceneProp.ToogleParallax != 0);
    Meshes[mesh].SetParallaxSettings(
      SceneProp.ParallaxLowSamples, SceneProp.ParallaxHighSamples, SceneProp.ParallaxHeight);
    Meshes[mesh].SetParallaxShadowEnabled(SceneProp.ToogleParallaxShadow != 0);
    Meshes[mesh].SetParallaxShadowSettings(
      SceneProp.ParallaxShadowMinLayers, SceneProp.ParallaxShadowMaxLayers,
      SceneProp.ParallaxShadowSoftness, SceneProp.ParallaxShadowStrength);
  }

  // Apply any queued skybox change before rendering
  ApplyPendingCubemap();

  // Periodic player position log (every ~2 seconds)
  static float s_posLogTimer = 0.0f;
  s_posLogTimer += DtSecs;
  if (s_posLogTimer > 2.0f) {
    s_posLogTimer = 0.0f;
    T8_LOG_INFO("[Minecraft] Player pos=(%.1f, %.1f, %.1f) grounded=%d",
                m_playerEye.x, m_playerEye.y, m_playerEye.z, m_player.IsGrounded() ? 1 : 0);
  }

  // Upload ready async chunks to the GPU (a few per frame)
  ProcessPendingChunks();
  ReportRenderDistanceReady();

  // Legacy APIs can still opt out of asynchronous chunk remeshing.
  if (!m_asyncStreaming) RebuildDirtyChunks();

  ApplyPendingRenderDistance();

  // Rebuild the navmesh (throttled) when blocks changed, so enemies
  // re-path around newly placed/removed blocks.
  if (m_navMeshDirty) {
    m_navMeshRebuildTimer += DtSecs;
    if (m_navMeshRebuildTimer >= m_voxelSettings.navmesh_rebuild_seconds &&
      !m_navMeshBuildFuture.valid() && !m_chunkGenerationFuture.valid()) {
      m_navMeshRebuildTimer = 0.0f;
      m_navMeshDirty = false;
      StartNavigationMeshBuild();
    }
  }

  VP = ActiveCam ? ActiveCam->VP : Cam.VP;
}

void MinecraftScene::OnInput(InputManager* IManager) {
  const GamepadInputState& gamepad = IManager->Gamepad;
  const bool gamepadActive = gamepad.connected && gamepad.enabled;
  constexpr float kGamepadMoveThreshold = 0.12f;
  constexpr float kGamepadLookThreshold = 0.08f;
  constexpr float kGamepadYawSpeed = 2.6f;
  constexpr float kGamepadPitchSpeed = 2.2f;
  if (gamepadActive && !m_gamepadControlsLogged) {
    m_gamepadControlsLogged = true;
    T8_LOG_INFO("[Minecraft] Gamepad controls active: '%s'", gamepad.name.c_str());
  }

  // Mouse look
  if (m_mouseCaptured) {
    if (m_cameraMode == 2 && m_lightCameraEditMode) {
      m_lightYaw += IManager->xDelta * m_mouseSensitivity;
      m_lightPitch += IManager->yDelta * m_mouseSensitivity;
      const float pitchLimit = m_voxelSettings.player.look_pitch_limit;
      m_lightPitch = (std::max)(-pitchLimit, (std::min)(pitchLimit, m_lightPitch));
    } else if (m_cameraMode == 1) {
      m_spectatorYaw += IManager->xDelta * m_mouseSensitivity;
      m_spectatorPitch += IManager->yDelta * m_mouseSensitivity;
      const float pitchLimit = m_voxelSettings.player.look_pitch_limit;
      m_spectatorPitch = (std::max)(-pitchLimit, (std::min)(pitchLimit, m_spectatorPitch));
    } else if (m_cameraMode == 0) {
      // Moving the mouse right (xDelta positive) should turn right (yaw up).
      m_playerYaw += IManager->xDelta * m_mouseSensitivity;
      // Engine convention: positive pitch = look down, negative = look up.
      // Moving the mouse up (yDelta negative) should look up (pitch negative),
      // so we ADD yDelta (inverted from the naive -=).
      m_playerPitch += IManager->yDelta * m_mouseSensitivity;
      const float pitchLimit = m_voxelSettings.player.look_pitch_limit;
      m_playerPitch = (std::max)(-pitchLimit, (std::min)(pitchLimit, m_playerPitch));
    }
  }
  if (gamepadActive && m_cameraMode == 0 &&
      (std::fabs(gamepad.rightX) > kGamepadLookThreshold ||
       std::fabs(gamepad.rightY) > kGamepadLookThreshold)) {
    m_playerYaw += gamepad.rightX * kGamepadYawSpeed * DtSecs;
    m_playerPitch += gamepad.rightY * kGamepadPitchSpeed * DtSecs;
    const float pitchLimit = m_voxelSettings.player.look_pitch_limit;
    m_playerPitch = (std::max)(-pitchLimit, (std::min)(pitchLimit, m_playerPitch));
  }

  if (m_cameraMode == 1 || (m_cameraMode == 2 && m_lightCameraEditMode)) {
    Camera& movableCamera = (m_cameraMode == 1) ? SpectatorCam : LightCam;
    float& yaw = (m_cameraMode == 1) ? m_spectatorYaw : m_lightYaw;
    float& pitch = (m_cameraMode == 1) ? m_spectatorPitch : m_lightPitch;
    const float speed = m_debugCameraSpeed * DtSecs;
    const float fwd = (IManager->PressedKey(T800K_w) ? 1.0f : 0.0f) - (IManager->PressedKey(T800K_s) ? 1.0f : 0.0f);
    const float strafe = (IManager->PressedKey(T800K_d) ? 1.0f : 0.0f) - (IManager->PressedKey(T800K_a) ? 1.0f : 0.0f);
    const float upInput = (IManager->PressedKey(T800K_SPACE) ? 1.0f : 0.0f) - (IManager->PressedKey(T800K_LSHIFT) ? 1.0f : 0.0f);
    XVECTOR3 look(std::sin(yaw) * std::cos(pitch),
                  -std::sin(pitch),
                  std::cos(yaw) * std::cos(pitch));
    look.Normalize();
    XVECTOR3 right(std::cos(yaw), 0.0f, -std::sin(yaw));
    right.Normalize();
    XVECTOR3 upVec(0.0f, 1.0f, 0.0f, 0.0f);
    movableCamera.Eye += look * (fwd * speed) + right * (strafe * speed) + upVec * (upInput * speed);
    movableCamera.Yaw = yaw;
    movableCamera.Pitch = pitch;
    movableCamera.Update(0.0f);
    if (m_cameraMode == 2)
      SyncSunFromLightCamera();
    m_playerInput = {};
  } else if (m_cameraMode == 0) {
    // Movement input (player)
    m_playerInput.forward = XVECTOR3(std::sin(m_playerYaw), 0.0f, std::cos(m_playerYaw), 0.0f);
    m_playerInput.right = XVECTOR3(std::cos(m_playerYaw), 0.0f, -std::sin(m_playerYaw), 0.0f);
    m_playerInput.moveForward = IManager->PressedKey(T800K_w);
    m_playerInput.moveBackward = IManager->PressedKey(T800K_s);
    m_playerInput.moveLeft = IManager->PressedKey(T800K_a);
    m_playerInput.moveRight = IManager->PressedKey(T800K_d);
    m_playerInput.jump = IManager->PressedKey(T800K_SPACE);
    m_playerInput.sprint = IManager->PressedKey(T800K_LSHIFT);
    m_playerInput.moveForwardAmount = (m_playerInput.moveForward ? 1.0f : 0.0f) - (m_playerInput.moveBackward ? 1.0f : 0.0f);
    m_playerInput.moveRightAmount = (m_playerInput.moveRight ? 1.0f : 0.0f) - (m_playerInput.moveLeft ? 1.0f : 0.0f);
    if (gamepadActive) {
      m_playerInput.moveForwardAmount = std::clamp(
        m_playerInput.moveForwardAmount - gamepad.leftY, -1.0f, 1.0f);
      m_playerInput.moveRightAmount = std::clamp(
        m_playerInput.moveRightAmount + gamepad.leftX, -1.0f, 1.0f);
      m_playerInput.moveForward = m_playerInput.moveForward || gamepad.leftY < -kGamepadMoveThreshold;
      m_playerInput.moveBackward = m_playerInput.moveBackward || gamepad.leftY > kGamepadMoveThreshold;
      m_playerInput.moveLeft = m_playerInput.moveLeft || gamepad.leftX < -kGamepadMoveThreshold;
      m_playerInput.moveRight = m_playerInput.moveRight || gamepad.leftX > kGamepadMoveThreshold;
      m_playerInput.jump = m_playerInput.jump || gamepad.buttonSouth;
      m_playerInput.sprint = m_playerInput.sprint || gamepad.leftStick;
    }
  }

  if (m_cameraMode == 0)
    HandleBlockInteraction(IManager);

  // Block selection (number keys 1-9)
  if (IManager->PressedOnceKey(T800K_1) && m_hotbar.size() > 0) m_selectedBlock = m_hotbar[0];
  if (IManager->PressedOnceKey(T800K_2) && m_hotbar.size() > 1) m_selectedBlock = m_hotbar[1];
  if (IManager->PressedOnceKey(T800K_3) && m_hotbar.size() > 2) m_selectedBlock = m_hotbar[2];
  if (IManager->PressedOnceKey(T800K_4) && m_hotbar.size() > 3) m_selectedBlock = m_hotbar[3];
  if (IManager->PressedOnceKey(T800K_5) && m_hotbar.size() > 4) m_selectedBlock = m_hotbar[4];
  if (IManager->PressedOnceKey(T800K_6) && m_hotbar.size() > 5) m_selectedBlock = m_hotbar[5];
  if (IManager->PressedOnceKey(T800K_7) && m_hotbar.size() > 6) m_selectedBlock = m_hotbar[6];
  if (IManager->PressedOnceKey(T800K_8) && m_hotbar.size() > 7) m_selectedBlock = m_hotbar[7];
  if (IManager->PressedOnceKey(T800K_9) && m_hotbar.size() > 8) m_selectedBlock = m_hotbar[8];
  if (gamepadActive && !m_hotbar.empty()) {
    const bool previousBlock = gamepad.dpadLeftPressed || gamepad.leftShoulderPressed;
    const bool nextBlock = gamepad.dpadRightPressed || gamepad.rightShoulderPressed;
    auto selected = std::find(m_hotbar.begin(), m_hotbar.end(), (uint8_t)m_selectedBlock);
    int selectedIndex = selected == m_hotbar.end()
      ? 0 : static_cast<int>(std::distance(m_hotbar.begin(), selected));
    if (previousBlock)
      selectedIndex = (selectedIndex + (int)m_hotbar.size() - 1) % (int)m_hotbar.size();
    if (nextBlock) selectedIndex = (selectedIndex + 1) % (int)m_hotbar.size();
    if (previousBlock || nextBlock) m_selectedBlock = m_hotbar[selectedIndex];
  }
  // Extra blocks via Q/E cycle (Q = previous, E toggles cursor, so use R/F)
  if (IManager->PressedOnceKey(T800K_r)) m_selectedBlock = BlockId("brick", m_selectedBlock);
  if (IManager->PressedOnceKey(T800K_f)) m_selectedBlock = BlockId("glass", m_selectedBlock);
  if (IManager->PressedOnceKey(T800K_v)) m_selectedBlock = BlockId("stone_bricks", m_selectedBlock);
  if (IManager->PressedOnceKey(T800K_b)) m_selectedBlock = BlockId("snow", m_selectedBlock);
  if (IManager->PressedOnceKey(T800K_n)) m_selectedBlock = BlockId("gravel", m_selectedBlock);

  // Toggle mouse capture with E
  if (IManager->PressedOnceKey(T800K_e)) {
    m_mouseCaptured = !m_mouseCaptured;
  }
}

void MinecraftScene::OnDraw() {
  // Cascade selection always follows the actual player, never a debug view.
  const Camera& csmViewCamera = Cam;
  for (auto& projection : SceneProp.Shadows.projections) {
    int tileRes = projection.resolvedDesc.resolution > 0
      ? projection.resolvedDesc.resolution
      : (int)SceneProp.ShadowMapResolution;
    std::string err;
    if (!t850::ShadowSystem::UpdateProjection(projection, SceneProp, csmViewCamera, tileRes, &err)) {
      T8_LOG_ERROR("[Minecraft][CSM] Projection update failed: %s", err.c_str());
    }
  }

  // Execute the render graph
  SceneProp.CascadeDebugRegionsEnabled =
    m_showCascadeFrustums && (m_cascadeDebugMode == 0 || m_cascadeDebugMode == 2);
  SceneProp.CascadeDebugOpacity = m_cascadeDebugOpacity;
  m_renderGraph.Execute(
    pFramework->pVideoDriver,
    SceneProp,
    Meshes, m_renderMeshCount,
    Quads,
    ActiveCam ? ActiveCam : &Cam,
    &LightCam,
    nullptr,
    EnvMaps,
    -1,
    [this](const std::string& callback) {
      if (callback == "cascade_debug_volumes" && m_showCascadeFrustums && m_cascadeDebugMode != 0)
        DrawCascadeLightBounds();
      else if (callback == "voxel_debug_bounds" &&
               (m_showChunkBounds || m_showPhysics || m_showLights ||
                m_highlightVisible || m_lastHighlightValid))
        DrawVoxelDebugBounds();
    }
  );

  // Keep a private copy of the offscreen final image every frame so the
  // benchmark dump can save it safely (see BlitOffscreenToCaptureRT).
  if (pFramework->pVideoDriver->IsOffscreenEnabled()) {
    BlitOffscreenToCaptureRT(pFramework->pVideoDriver);
  }

  if (m_debugRTSelection > 0) {
    const auto& debugTarget = m_voxelSettings.debug_render_targets[m_debugRTSelection];
    const std::size_t separator = debugTarget.source.find(':');
    const std::string targetName = debugTarget.source.substr(0, separator);
    const std::string attachmentName = separator == std::string::npos
      ? std::string{} : debugTarget.source.substr(separator + 1);
    const int selected = m_renderGraph.GetRTHandle(targetName);
    int attachment = BaseDriver::COLOR0_ATTACHMENT;
    if (attachmentName == "DEPTH") attachment = BaseDriver::DEPTH_ATTACHMENT;
    else if (attachmentName.rfind("COLOR", 0) == 0)
      attachment = BaseDriver::COLOR0_ATTACHMENT + std::atoi(attachmentName.c_str() + 5);
    if (selected >= 0) {
      Quads[7].SetTexture(pFramework->pVideoDriver->GetRTTexture(selected, attachment), 0);
      ShaderKey debugKey(0);
      debugKey.setPass(PassType::FSQUAD_1_TEX);
      debugKey.bits |= ShaderKey::HAS_TEXCOORD0;
      Quads[7].SetGlobalKey(debugKey);
      Quads[7].Draw();
#ifdef OS_ANDROID
  pFramework->pVideoDriver->SetLatePresentSource(selected, attachment);
#endif
    }
  }

  auto drawNavMeshOverlay = [this]() {
    if (!m_showNavMesh || !m_navMeshReady || !m_navMeshDebugRenderer.IsReady()) return;
    Texture* depthTexture = nullptr;
    if (GBufferPass >= 0 && GBufferPass < (int)pFramework->pVideoDriver->RTs.size()) {
      if (auto* gBuffer = pFramework->pVideoDriver->RTs[GBufferPass])
        depthTexture = gBuffer->pDepthTexture;
    }
    if (!depthTexture) return;

    float debugOffset = 0.01f;
    int shapeMode = 0;
    if (m_sceneFile.navigation_mesh) {
      debugOffset = m_sceneFile.navigation_mesh->debug_offset;
      shapeMode = m_sceneFile.navigation_mesh->debug_shape_mode;
    }
    const Camera* viewCamera = ActiveCam ? ActiveCam : &Cam;
    m_navMeshDebugRenderer.SetVerticalOffset(debugOffset);
    m_navMeshDebugRenderer.SetGraphVerticalOffset(debugOffset + 0.005f);
    m_navMeshDebugRenderer.SetShapeMode(shapeMode == 1
      ? t850::navigation::NavMeshDebugShapeMode::Nodes
      : t850::navigation::NavMeshDebugShapeMode::Geometry);
    m_navMeshDebugRenderer.SetDepthTexture(depthTexture);
    m_navMeshDebugRenderer.SetViewport(g_pBaseDriver->width, g_pBaseDriver->height);
    m_navMeshDebugRenderer.SetFarPlane(viewCamera->FPlane);
    pFramework->pVideoDriver->SetDepthStencilState(BaseDriver::NONE);
    pFramework->pVideoDriver->SetBlendState(BaseDriver::BLEND_DEFAULT);
    m_navMeshDebugRenderer.Draw(m_navMesh, viewCamera->VP);
  };

#ifdef OS_ANDROID
  pFramework->pVideoDriver->SetPrePresentOverlayCallback(
    m_showNavMesh ? drawNavMeshOverlay : std::function<void()>{});
#else
  drawNavMeshOverlay();
#endif
  // Frame dump
  if (m_dumper.ShouldDump(DtSecs)) {
    std::vector<t850::RTDumpEntry> rts = {
      {GBufferPass, BaseDriver::COLOR0_ATTACHMENT, "GBuffer_Albedo"},
      {GBufferPass, BaseDriver::COLOR1_ATTACHMENT, "GBuffer_Normals"},
      {GBufferPass, BaseDriver::COLOR2_ATTACHMENT, "GBuffer_PBR"},
      {GBufferPass, BaseDriver::COLOR3_ATTACHMENT, "GBuffer_GeoMaterial"},
      {GBufferPass, BaseDriver::COLOR4_ATTACHMENT, "GBuffer_Emissive"},
      {GBufferPass, BaseDriver::COLOR5_ATTACHMENT, "GBuffer_Sheen"},
      {GBufferPass, BaseDriver::COLOR6_ATTACHMENT, "GBuffer_F0Occlusion"},
      {GBufferPass, BaseDriver::DEPTH_ATTACHMENT,  "GBuffer_Depth"},
      {DepthPass,   BaseDriver::DEPTH_ATTACHMENT,  "ShadowMap_Depth"},
      {ShadowAccumPass, BaseDriver::COLOR0_ATTACHMENT, "ShadowAccum"},
      {DeferredPass,BaseDriver::COLOR0_ATTACHMENT, "Deferred"},
    };
    m_dumper.DumpFrame(pFramework->pVideoDriver, Cam, LightCam, SceneProp, rts, DtSecs,
                       nullptr, nullptr, nullptr);
    // In offscreen mode the final image lands in the driver's offscreen RT,
    // not the swapchain backbuffer (which stays black). Save the private
    // per-frame copy (kept current by BlitOffscreenToCaptureRT above).
    auto* driver = pFramework->pVideoDriver;
    if (driver->IsOffscreenEnabled() && m_offscreenCaptureRT >= 0) {
      driver->SaveRTToFile(m_offscreenCaptureRT, BaseDriver::COLOR0_ATTACHMENT, "offscreen_final");
      T8_LOG_INFO("[Minecraft] Offscreen final frame captured from private RT %d -> offscreen_final.ppm",
                  m_offscreenCaptureRT);
    }
    if (m_dumper.ShouldExit()) exit(0);
  }

  // HUD text (drawn in DrawDevGui via ImGui so it scales with resolution)
}

// Offscreen benchmark capture: the swapchain backbuffer stays black in
// offscreen mode, so the final image lives in the driver's offscreen RT.
// Reading a just-completed offscreen RT directly can crash (shader-resource
// state while the frame's command list is still open), so every frame we
// blit the active offscreen RT into a private RT with a fullscreen quad
// (safe: the RT is bound and its texture was already sampled this frame),
// and the dump saves the private copy instead.
void MinecraftScene::BlitOffscreenToCaptureRT(t850::BaseDriver* driver) {
  const int srcRT = driver->GetActiveOffscreenRT();
  if (srcRT < 0 || srcRT >= (int)driver->RTs.size() || !driver->RTs[srcRT])
    return;

  auto* src = static_cast<BaseRT*>(driver->RTs[srcRT]);
  if (m_offscreenCaptureRT < 0) {
    m_offscreenCaptureRT = driver->CreateRT(1, BaseRT::RGBA8, BaseRT::F32, src->w, src->h, false);
  }
  if (m_offscreenCaptureRT < 0 || m_offscreenCaptureRT >= (int)driver->RTs.size() ||
      !driver->RTs[m_offscreenCaptureRT])
    return;

  driver->PushRT(m_offscreenCaptureRT);
  driver->SetViewport(0.0f, 0.0f, (float)src->w, (float)src->h);
  driver->SetScissorRect(0, 0, src->w, src->h);
  driver->SetBlendState(BaseDriver::BLEND_DEFAULT);
  driver->SetDepthStencilState(BaseDriver::NONE);

  Quads[8].SetTexture(driver->GetRTTexture(srcRT, BaseDriver::COLOR0_ATTACHMENT), 0);
  ShaderKey debugKey(0);
  debugKey.setPass(PassType::FSQUAD_1_TEX);
  debugKey.bits |= ShaderKey::HAS_TEXCOORD0;
  Quads[8].SetGlobalKey(debugKey);
  Quads[8].Draw();
}

void MinecraftScene::DrawCascadeLightBounds() {
  // Only draw when shadows are enabled and a projection exists.
  if (SceneProp.ToogleShadow == 0)
    return;
  if (SceneProp.Shadows.projections.empty())
    return;

  const auto& projection = SceneProp.Shadows.projections[0];
  if (projection.viewCount <= 0)
    return;

  // Box edges: 12 edges connecting the 8 corners.
  static const unsigned short kBoxEdges[24] = {
    0,1, 1,2, 2,3, 3,0,   // near face
    4,5, 5,6, 6,7, 7,4,   // far face
    0,4, 1,5, 2,6, 3,7    // connecting edges
  };

  XMATRIX44 identity;
  identity.Identity();
  m_lineRenderer.SetDepthTestEnabled(false);  // flat shader; graph pass owns depth testing
  m_lineRenderer.SetViewport(pFramework->pVideoDriver->width,
                             pFramework->pVideoDriver->height);

  // Lazily create the shared VB/IB once (never per-frame — that causes device
  // removal). One fitted light-space box is stored per cascade.
  const int kMaxCascades = 6;
  const int kMaxDebugBoxes = kMaxCascades;
  const int kMaxVertices = kMaxDebugBoxes * 8;
  if (!m_cascadeDebugVB || !m_cascadeDebugIB || !m_cascadeDebugSolidIB) {
    float dummy[kMaxVertices * 4] = {};
    m_cascadeDebugVB = t850::LineRenderer::CreatePositionVB(dummy, kMaxVertices, BufferUsage::DINAMIC);
    m_cascadeDebugIB = t850::LineRenderer::CreateIndexBuffer16(kBoxEdges, 24);
    static const unsigned short kBoxTriangles[36] = {
      0,2,1, 0,3,2,
      4,5,6, 4,6,7,
      0,1,5, 0,5,4,
      1,2,6, 1,6,5,
      2,3,7, 2,7,6,
      3,0,4, 3,4,7
    };
    m_cascadeDebugSolidIB = t850::LineRenderer::CreateIndexBuffer16(kBoxTriangles, 36);
    m_cascadeDebugVBCapacity = kMaxVertices;
  }
  if (!m_cascadeDebugVB || !m_cascadeDebugIB || !m_cascadeDebugSolidIB)
    return;

  float positions[kMaxVertices * 4] = {};
  int count = (projection.viewCount < kMaxCascades) ? projection.viewCount : kMaxCascades;
  int boxCount = 0;
  for (int c = 0; c < count; ++c) {
    const auto& view = projection.views[c];
    auto appendBox = [&](const XVECTOR3 corners[8]) {
      for (int k = 0; k < 8; ++k) {
        const int idx = (boxCount * 8 + k) * 4;
        positions[idx + 0] = corners[k].x;
        positions[idx + 1] = corners[k].y;
        positions[idx + 2] = corners[k].z;
        positions[idx + 3] = 1.0f;
      }
      ++boxCount;
    };

    const Camera& shadowCamera = view.camera;
    const float halfW = shadowCamera.Width * 0.5f;
    const float halfH = shadowCamera.Height * 0.5f;
    const XVECTOR3 nearCenter = shadowCamera.Eye + shadowCamera.Look * shadowCamera.NPlane;
    const XVECTOR3 farCenter = shadowCamera.Eye + shadowCamera.Look * shadowCamera.FPlane;
    const XVECTOR3 volumeCorners[8] = {
      nearCenter - shadowCamera.Right * halfW - shadowCamera.Up * halfH,
      nearCenter + shadowCamera.Right * halfW - shadowCamera.Up * halfH,
      nearCenter + shadowCamera.Right * halfW + shadowCamera.Up * halfH,
      nearCenter - shadowCamera.Right * halfW + shadowCamera.Up * halfH,
      farCenter - shadowCamera.Right * halfW - shadowCamera.Up * halfH,
      farCenter + shadowCamera.Right * halfW - shadowCamera.Up * halfH,
      farCenter + shadowCamera.Right * halfW + shadowCamera.Up * halfH,
      farCenter - shadowCamera.Right * halfW + shadowCamera.Up * halfH,
    };
    appendBox(volumeCorners);
  }
  if (t850::T8DeviceContext)
    m_cascadeDebugVB->UpdateFromBuffer(*t850::T8DeviceContext, positions);

  // Blend filled volumes far-to-near, then add a stronger outline. The render
  // graph callback supplies alpha blend + depth-read/no-write state.
  const Camera* debugCamera = ActiveCam ? ActiveCam : &Cam;
  for (int box = boxCount - 1; box >= 0; --box) {
    const int cascade = box;
    const XVECTOR3 fillColor(m_cascadeDebugColors[cascade].x,
                 m_cascadeDebugColors[cascade].y,
                 m_cascadeDebugColors[cascade].z,
                             m_cascadeDebugOpacity);
    m_lineRenderer.DrawTriangles(identity, debugCamera->VP, fillColor,
                                 m_cascadeDebugVB, m_cascadeDebugSolidIB, 36,
                                 sizeof(float) * 4, IndexBufferFormat::R16,
                                 box * 8);
  }
  for (int box = boxCount - 1; box >= 0; --box) {
    const int cascade = box;
    const XVECTOR3 outlineColor(m_cascadeDebugColors[cascade].x,
                  m_cascadeDebugColors[cascade].y,
                  m_cascadeDebugColors[cascade].z,
                                (std::min)(1.0f, m_cascadeDebugOpacity * 4.0f));
    m_lineRenderer.DrawLines(identity, debugCamera->VP, outlineColor,
                             m_cascadeDebugVB, m_cascadeDebugIB, 24,
                             sizeof(float) * 4, IndexBufferFormat::R16,
                             box * 8);
  }
}

void MinecraftScene::DrawVoxelDebugBounds() {
  static const unsigned short kBoxEdges[24] = {
    0,1, 1,2, 2,3, 3,0,
    4,5, 5,6, 6,7, 7,4,
    0,4, 1,5, 2,6, 3,7
  };
  constexpr int kMaxBoxes = kMaxChunks + 5;
  constexpr int kMaxVertices = kMaxBoxes * 8;

  if (!m_voxelDebugVB || !m_voxelDebugIB) {
    float dummy[kMaxVertices * 4] = {};
    m_voxelDebugVB = t850::LineRenderer::CreatePositionVB(
      dummy, kMaxVertices, BufferUsage::DINAMIC);
    m_voxelDebugIB = t850::LineRenderer::CreateIndexBuffer16(kBoxEdges, 24);
  }
  if (!m_voxelDebugVB || !m_voxelDebugIB) return;

  std::vector<float> positions(kMaxVertices * 4, 0.0f);
  int boxCount = 0;
  auto appendBox = [&](float minX, float minY, float minZ,
                       float maxX, float maxY, float maxZ) {
    if (boxCount >= kMaxBoxes) return;
    const XVECTOR3 corners[8] = {
      XVECTOR3(minX, minY, minZ, 1.0f), XVECTOR3(maxX, minY, minZ, 1.0f),
      XVECTOR3(maxX, maxY, minZ, 1.0f), XVECTOR3(minX, maxY, minZ, 1.0f),
      XVECTOR3(minX, minY, maxZ, 1.0f), XVECTOR3(maxX, minY, maxZ, 1.0f),
      XVECTOR3(maxX, maxY, maxZ, 1.0f), XVECTOR3(minX, maxY, maxZ, 1.0f)
    };
    for (int corner = 0; corner < 8; ++corner) {
      const int offset = (boxCount * 8 + corner) * 4;
      positions[offset + 0] = corners[corner].x;
      positions[offset + 1] = corners[corner].y;
      positions[offset + 2] = corners[corner].z;
      positions[offset + 3] = 1.0f;
    }
    ++boxCount;
  };

  int chunkBoxCount = 0;
  if (m_showChunkBounds) {
    for (int cz = m_centerChunkZ - m_renderDistance;
         cz <= m_centerChunkZ + m_renderDistance; ++cz) {
      for (int cx = m_centerChunkX - m_renderDistance;
           cx <= m_centerChunkX + m_renderDistance; ++cx) {
        const float minX = (float)(cx * m_chunkSize);
        const float minZ = (float)(cz * m_chunkSize);
        appendBox(minX, 0.0f, minZ,
                  minX + m_chunkSize, (float)m_worldHeight, minZ + m_chunkSize);
      }
    }
    chunkBoxCount = boxCount;
  }

  if (m_showPhysics) {
    const XVECTOR3 playerCenter = m_player.GetPosition();
    const float radius = m_playerSettings.capsuleRadius;
    const float verticalExtent = m_playerSettings.capsuleHalfHeight + radius;
    appendBox(playerCenter.x - radius, playerCenter.y - verticalExtent, playerCenter.z - radius,
              playerCenter.x + radius, playerCenter.y + verticalExtent, playerCenter.z + radius);
    const float mobHalfWidth = m_voxelSettings.mob.half_width;
    appendBox(m_mob.position.x - mobHalfWidth, m_mob.position.y, m_mob.position.z - mobHalfWidth,
              m_mob.position.x + mobHalfWidth, m_mob.position.y + m_voxelSettings.mob.height,
              m_mob.position.z + mobHalfWidth);
  }
  const int collisionBoxEnd = boxCount;

  if (m_showLights && m_sunLightIndex >= 0 && m_sunLightIndex < (int)SceneProp.Lights.size()) {
    const XVECTOR3 sunPosition = SceneProp.Lights[m_sunLightIndex].Position;
    const float halfSize = m_voxelSettings.sun_debug_size * 0.5f;
    appendBox(sunPosition.x - halfSize, sunPosition.y - halfSize, sunPosition.z - halfSize,
              sunPosition.x + halfSize, sunPosition.y + halfSize, sunPosition.z + halfSize);
  }
  const int targetBoxStart = boxCount;
  if (m_highlightVisible) {
    appendBox((float)m_highlightX - 0.002f, (float)m_highlightY - 0.002f,
              (float)m_highlightZ - 0.002f, (float)m_highlightX + 1.002f,
              (float)m_highlightY + 1.002f, (float)m_highlightZ + 1.002f);
  } else if (m_lastHighlightValid) {
    appendBox((float)m_lastHighlightX - 0.002f, (float)m_lastHighlightY - 0.002f,
              (float)m_lastHighlightZ - 0.002f, (float)m_lastHighlightX + 1.002f,
              (float)m_lastHighlightY + 1.002f, (float)m_lastHighlightZ + 1.002f);
  }

  if (boxCount == 0 || !t850::T8DeviceContext) return;
  m_voxelDebugVB->UpdateFromBuffer(*t850::T8DeviceContext, positions.data());

  XMATRIX44 identity;
  identity.Identity();
  const Camera* viewCamera = ActiveCam ? ActiveCam : &Cam;
  m_lineRenderer.SetDepthTestEnabled(false);
  m_lineRenderer.SetViewport(pFramework->pVideoDriver->width,
                             pFramework->pVideoDriver->height);
  const auto& chunkColor = m_voxelSettings.chunk_debug_color;
  const auto& collisionColor = m_voxelSettings.collision_debug_color;
  for (int box = 0; box < boxCount; ++box) {
    const bool isChunk = box < chunkBoxCount;
    const bool isCollision = box < collisionBoxEnd;
    XVECTOR3 color;
    if (isChunk)
      color = XVECTOR3(chunkColor.x, chunkColor.y, chunkColor.z, 1.0f);
    else if (isCollision)
      color = XVECTOR3(collisionColor.x, collisionColor.y, collisionColor.z, 1.0f);
    else if (box >= targetBoxStart)
      color = m_highlightVisible
        ? XVECTOR3(0.95f, 0.98f, 1.0f, 1.0f)
        : XVECTOR3(1.0f, 0.58f, 0.16f, 0.65f);
    else
      color = SceneProp.Lights[m_sunLightIndex].Color;
    m_lineRenderer.DrawLines(identity, viewCamera->VP, color,
                             m_voxelDebugVB, m_voxelDebugIB, 24,
                             sizeof(float) * 4, IndexBufferFormat::R16,
                             box * 8);
  }
}

void MinecraftScene::ApplyShadowSettings() {
  // Master toggle
  SceneProp.ToogleShadow = m_shadowsEnabled ? 1 : 0;

  // Bias / min
  SceneProp.ShadowBias = m_shadowBias;
  SceneProp.ShadowMin = m_shadowMin;

  // Apply to the active projection's resolved descriptor.
  if (!SceneProp.Shadows.projections.empty()) {
    auto& proj = SceneProp.Shadows.projections[0];
    proj.resolvedDesc.split_lambda = m_splitLambda;
    proj.resolvedDesc.cascade_count = m_cascadeCount;
  }

  // If resolution or cascade count changed, recreate the render targets.
  if (m_shadowConfigDirty) {
    m_shadowConfigDirty = false;
    SceneProp.ShadowMapResolution = m_shadowResolution;
    if (pFramework && pFramework->pVideoDriver) {
      pFramework->pVideoDriver->WaitForGPU();
      m_renderGraph.DestroyRenderTargets(pFramework->pVideoDriver);
      std::string cfgError;
      const std::string projectionId = SceneProp.Shadows.projections.empty()
        ? std::string{} : SceneProp.Shadows.projections[0].resolvedDesc.id;
      t850::ShadowProjectionOverrideDesc shadowOverride;
      shadowOverride.projection_id = projectionId;
      shadowOverride.resolution = static_cast<int>(m_shadowResolution);
      shadowOverride.cascade_count = m_cascadeCount;
      shadowOverride.split_lambda = m_splitLambda;
      if (!m_renderGraph.Configure(SceneProp, {shadowOverride}, {}, &cfgError)) {
        T8_LOG_ERROR("[Minecraft] Reconfigure failed: %s", cfgError.c_str());
        return;
      }
      m_renderGraph.CreateRenderTargets(pFramework->pVideoDriver, SceneProp);
      t850::sandbox::RefreshDeferredPassHandles(
        m_renderGraph, GBufferPass, DeferredPass, Extra16FPass, DepthPass,
        ShadowAccumPass, ExtraHelperPass, BloomAccumPass,
        AdaptedLumCurrentPass, AdaptedLumPrevPass);
      BrightPass = m_renderGraph.GetRTHandle("BrightPass");
      CoCPass = m_renderGraph.GetRTHandle("CoC");
    }
  }

  if (m_cameraMode == 2) {
    LightCam.Ortho = m_debugCameraOrtho;
    if (m_lightCameraEditMode) {
      LightCam.Yaw = m_lightYaw;
      LightCam.Pitch = m_lightPitch;
    }
    LightCam.AspectRatio = (float)pFramework->pVideoDriver->width /
                           (float)(std::max)(1, pFramework->pVideoDriver->height);
    LightCam.CreatePojection();
    LightCam.Update(0.0f);
    if (m_lightCameraEditMode)
      SyncSunFromLightCamera();
    ActiveCam = &LightCam;
    SceneProp.SetPrimaryCamera(ActiveCam);
  } else if (m_cameraMode == 1) {
    SpectatorCam.Yaw = m_spectatorYaw;
    SpectatorCam.Pitch = m_spectatorPitch;
    SpectatorCam.AspectRatio = (float)pFramework->pVideoDriver->width /
                               (float)(std::max)(1, pFramework->pVideoDriver->height);
    SpectatorCam.CreatePojection();
    SpectatorCam.Update(0.0f);
    ActiveCam = &SpectatorCam;
    SceneProp.SetPrimaryCamera(ActiveCam);
  } else {
    ActiveCam = &Cam;
    SceneProp.SetPrimaryCamera(&Cam);
  }
  SceneProp.pCullingCamera = &Cam;
}

void MinecraftScene::SaveSceneSettings() {
  if (!m_sceneFile.cameras.empty()) {
    auto& camera = m_sceneFile.cameras[0];
    camera.type = Cam.Ortho ? 1 : 0;
    camera.position = {Cam.Eye.x, Cam.Eye.y, Cam.Eye.z};
    camera.target = {Cam.Eye.x + Cam.Look.x, Cam.Eye.y + Cam.Look.y, Cam.Eye.z + Cam.Look.z};
    camera.fov_deg = Rad2Deg(Cam.Fov);
    camera.ortho_w = Cam.Width;
    camera.ortho_h = Cam.Height;
    camera.near_plane = Cam.NPlane;
    camera.far_plane = Cam.FPlane;
  }
  if (m_sceneFile.cameras.size() >= 2) {
    auto& camera = m_sceneFile.cameras[1];
    camera.type = SpectatorCam.Ortho ? 1 : 0;
    camera.position = {SpectatorCam.Eye.x, SpectatorCam.Eye.y, SpectatorCam.Eye.z};
    camera.target = {SpectatorCam.Eye.x + SpectatorCam.Look.x,
                     SpectatorCam.Eye.y + SpectatorCam.Look.y,
                     SpectatorCam.Eye.z + SpectatorCam.Look.z};
    camera.fov_deg = Rad2Deg(SpectatorCam.Fov);
    camera.ortho_w = SpectatorCam.Width;
    camera.ortho_h = SpectatorCam.Height;
    camera.near_plane = SpectatorCam.NPlane;
    camera.far_plane = SpectatorCam.FPlane;
  }
  if (!m_sceneFile.light_cameras.empty()) {
    auto& camera = m_sceneFile.light_cameras[0];
    camera.type = m_debugCameraOrtho ? 1 : 0;
    camera.position = {LightCam.Eye.x, LightCam.Eye.y, LightCam.Eye.z};
    camera.target = {LightCam.Eye.x + LightCam.Look.x,
                     LightCam.Eye.y + LightCam.Look.y,
                     LightCam.Eye.z + LightCam.Look.z};
    camera.fov_deg = Rad2Deg(LightCam.Fov);
    camera.ortho_w = LightCam.Width;
    camera.ortho_h = LightCam.Height;
    camera.near_plane = LightCam.NPlane;
    camera.far_plane = LightCam.FPlane;
  }
  for (auto& authored : m_sceneFile.lights) {
    auto runtime = std::find_if(SceneProp.Lights.begin(), SceneProp.Lights.end(),
      [&](const Light& light) { return light.Id == authored.id; });
    if (runtime == SceneProp.Lights.end()) continue;
    authored.position = {runtime->Position.x, runtime->Position.y, runtime->Position.z};
    authored.direction = {runtime->Direction.x, runtime->Direction.y, runtime->Direction.z};
    authored.color = {runtime->Color.x, runtime->Color.y, runtime->Color.z};
    authored.intensity = runtime->Intensity;
    authored.radius = runtime->radius;
    authored.enabled = runtime->Enabled;
  }

  m_voxelSettings.environment_map = m_currentCubemapPath;
  m_voxelSettings.player.spawn = {Cam.Eye.x, Cam.Eye.y, Cam.Eye.z};
  m_voxelSettings.show_physics = m_showPhysics;
  m_voxelSettings.show_chunk_bounds = m_showChunkBounds;
  m_voxelSettings.show_lights = m_showLights;
  m_voxelSettings.show_navmesh = m_showNavMesh;
  m_voxelSettings.frustum_culling = SceneProp.FrustumCullingEnabled;
  m_voxelSettings.show_culling_debug = SceneProp.ShowCullingDebug;
  m_voxelSettings.show_cascade_debug = m_showCascadeFrustums;
  m_voxelSettings.cascade_debug_mode = m_cascadeDebugMode;
  m_voxelSettings.cascade_debug_opacity = m_cascadeDebugOpacity;
  m_voxelSettings.camera_mode = m_cameraMode;
  m_voxelSettings.debug_cascade_index = m_debugCascadeIndex;
  m_voxelSettings.debug_render_target = m_debugRTSelection;
  m_voxelSettings.active_lights = SceneProp.ActiveLights;
  m_voxelSettings.day_night.enabled = m_dayNightEnabled;
  m_voxelSettings.day_night.trajectory_paused = m_sunTrajectoryPaused;
  m_voxelSettings.day_night.time_of_day = m_timeOfDay;
  m_voxelSettings.day_night.day_length_seconds = m_dayLengthSecs;
  m_voxelSettings.day_night.manual_ambient = {
    SceneProp.AmbientColor.x, SceneProp.AmbientColor.y, SceneProp.AmbientColor.z};
  m_voxelSettings.dof.normalized_focus = SceneProp.DOFNormalizedFocus;
  m_voxelSettings.dof.focus_range = SceneProp.DOFFocusRange;
  m_voxelSettings.dof.focus_falloff = SceneProp.DOFFocusFalloff;
  m_voxelSettings.dof.auto_focus_radius = SceneProp.DOFAutoFocusRadius;
  if (m_sceneFile.navigation_mesh)
    m_sceneFile.navigation_mesh->visible = m_showNavMesh;
  m_sceneFile.voxel_world = m_voxelSettings;

  if (m_sceneFile.profiles.empty()) m_sceneFile.profiles.push_back({});
  auto& profile = m_sceneFile.profiles[0];
  if (profile.name.empty()) profile.name = "shared";
  auto setFloat = [&](const char* name, float value) {
    auto found = std::find_if(profile.sliders.begin(), profile.sliders.end(),
      [&](const t850::FloatOverrideDesc& entry) { return entry.name == name; });
    if (found == profile.sliders.end()) profile.sliders.push_back({name, value});
    else found->value = value;
  };
  auto setBool = [&](const char* name, bool value) {
    auto found = std::find_if(profile.checkboxes.begin(), profile.checkboxes.end(),
      [&](const t850::BoolOverrideDesc& entry) { return entry.name == name; });
    if (found == profile.checkboxes.end()) profile.checkboxes.push_back({name, value});
    else found->value = value;
  };
  auto setInt = [&](const char* name, int value) {
    auto found = std::find_if(profile.selectors.begin(), profile.selectors.end(),
      [&](const t850::IntOverrideDesc& entry) { return entry.name == name; });
    if (found == profile.selectors.end()) profile.selectors.push_back({name, value});
    else found->value = value;
  };
  setFloat("exposure", SceneProp.Exposure);
  setFloat("bloom_factor", SceneProp.BloomFactor);
  setFloat("bloom_threshold", SceneProp.BloomThreshold);
  setFloat("tm_white_level", SceneProp.ToneMapWhiteLevel);
  setFloat("tm_adapt_tau", SceneProp.LuminanceTau);
  setFloat("pcf_radius", SceneProp.PCFScale);
  setFloat("pcf_samples", SceneProp.PCFSamples);
  setFloat("ssao_kernel_size", (float)SceneProp.SSAOKernel.KernelSize);
  setFloat("ssao_radius", SceneProp.SSAOKernel.Radius);
  setFloat("dof_focus_range", SceneProp.DOFFocusRange);
  setFloat("dof_focus_falloff", SceneProp.DOFFocusFalloff);
  setFloat("dof_auto_focus_radius", SceneProp.DOFAutoFocusRadius);
  setFloat("dof_max_coc", SceneProp.MaxCoc);
  setFloat("dof_far_samples", SceneProp.DOF_Far_Samples_squared);
  setFloat("dof_near_samples", SceneProp.DOF_Near_Samples_squared);
  setFloat("parallax_low_samples", SceneProp.ParallaxLowSamples);
  setFloat("parallax_high_samples", SceneProp.ParallaxHighSamples);
  setFloat("parallax_height", SceneProp.ParallaxHeight);
  setFloat("shadow_bias", m_shadowBias);
  setFloat("shadow_min", m_shadowMin);
  setFloat("env_factor", SceneProp.EnvFactor);
  setFloat("ibl_factor", SceneProp.IBLFactor);
  setFloat("light_radius_scale", SceneProp.LightRadiusScale);
  setFloat("light_intensity_scale", SceneProp.LightIntensityScale);
  setFloat("lightmap_intensity", SceneProp.LightmapIntensity);
  setFloat("material_emissive_intensity", SceneProp.MaterialEmissiveIntensity);
  setFloat("material_transmission_multiplier", SceneProp.MaterialTransmissionMultiplier);
  setFloat("material_refraction_strength", SceneProp.MaterialRefractionStrength);
  setFloat("parallax_shadow_min_layers", SceneProp.ParallaxShadowMinLayers);
  setFloat("parallax_shadow_max_layers", SceneProp.ParallaxShadowMaxLayers);
  setFloat("parallax_shadow_softness", SceneProp.ParallaxShadowSoftness);
  setFloat("parallax_shadow_strength", SceneProp.ParallaxShadowStrength);
  setBool("shadow_toggle", m_shadowsEnabled);
  setBool("ssao_toggle", SceneProp.ToogleSSAO != 0);
  setBool("dof_toggle", SceneProp.ToogleDOF != 0);
  setBool("dof_auto_focus", SceneProp.AutoFocus);
  setBool("parallax_toggle", SceneProp.ToogleParallax != 0);
  setBool("parallax_shadow_toggle", SceneProp.ToogleParallaxShadow != 0);
  setBool("point_lights_enabled", SceneProp.PointLightsEnabled);
  setInt("luminance_mode", SceneProp.LuminanceMode);
  setInt("active_gauss_kernel", m_selectedGaussKernel);
  GaussFilter* filters[] = {&ShadowFilter, &BloomFilter, &NearDOFFilter};
  for (int index = 0; index < 3; ++index) {
    const std::string prefix = "gauss_" + std::to_string(index) + "_";
    setFloat((prefix + "radius").c_str(), filters[index]->radius);
    setFloat((prefix + "sigma").c_str(), filters[index]->sigma);
    setInt((prefix + "kernel_size").c_str(), filters[index]->kernelSize);
  }

  if (profile.shadow_projections.empty()) profile.shadow_projections.push_back({});
  auto& shadow = profile.shadow_projections[0];
  if (shadow.projection_id.empty() && !SceneProp.Shadows.projections.empty())
    shadow.projection_id = SceneProp.Shadows.projections[0].resolvedDesc.id;
  shadow.enabled = m_shadowsEnabled;
  shadow.resolution = (int)m_shadowResolution;
  shadow.cascade_count = m_cascadeCount;
  shadow.split_lambda = m_splitLambda;

  std::string error;
  if (t850::scene::SaveEditorSceneFile(m_sceneFile, m_sceneFilePath, &error)) {
    T8_LOG_INFO("[Minecraft] Saved authored scene to %s", m_sceneFilePath.c_str());
  } else {
    T8_LOG_ERROR("[Minecraft] Failed to save authored scene: %s", error.c_str());
  }
}

void MinecraftScene::DrawGameplayHud() {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImDrawList* drawList = ImGui::GetForegroundDrawList();
  if (!viewport || !drawList || m_blockDefs.empty() ||
      m_selectedBlock < 0 || m_selectedBlock >= static_cast<int>(m_blockDefs.size())) return;

  const ImVec2 workMin = viewport->WorkPos;
  const ImVec2 workSize = viewport->WorkSize;
  const ImVec2 center(workMin.x + workSize.x * 0.5f,
                      workMin.y + workSize.y * 0.5f);
  const ImU32 textColor = IM_COL32(245, 247, 250, 255);
  const ImU32 mutedColor = IM_COL32(190, 198, 206, 255);
  const ImU32 panelColor = IM_COL32(16, 20, 24, 190);
  const ImU32 outlineColor = IM_COL32(8, 10, 12, 230);
  const ImU32 accentColor = IM_COL32(238, 238, 220, 255);

  // Crosshair. The active target state is visible without opening a panel.
  const ImU32 crosshairColor = m_highlightVisible
    ? IM_COL32(242, 245, 236, 255)
    : (m_lastHighlightValid ? IM_COL32(255, 154, 54, 235) : IM_COL32(150, 158, 164, 220));
  constexpr float crosshairGap = 3.0f;
  constexpr float crosshairLength = 8.0f;
  drawList->AddLine(ImVec2(center.x - crosshairLength, center.y),
                    ImVec2(center.x - crosshairGap, center.y), outlineColor, 4.0f);
  drawList->AddLine(ImVec2(center.x + crosshairGap, center.y),
                    ImVec2(center.x + crosshairLength, center.y), outlineColor, 4.0f);
  drawList->AddLine(ImVec2(center.x, center.y - crosshairLength),
                    ImVec2(center.x, center.y - crosshairGap), outlineColor, 4.0f);
  drawList->AddLine(ImVec2(center.x, center.y + crosshairGap),
                    ImVec2(center.x, center.y + crosshairLength), outlineColor, 4.0f);
  drawList->AddLine(ImVec2(center.x - crosshairLength, center.y),
                    ImVec2(center.x - crosshairGap, center.y), crosshairColor, 2.0f);
  drawList->AddLine(ImVec2(center.x + crosshairGap, center.y),
                    ImVec2(center.x + crosshairLength, center.y), crosshairColor, 2.0f);
  drawList->AddLine(ImVec2(center.x, center.y - crosshairLength),
                    ImVec2(center.x, center.y - crosshairGap), crosshairColor, 2.0f);
  drawList->AddLine(ImVec2(center.x, center.y + crosshairGap),
                    ImVec2(center.x, center.y + crosshairLength), crosshairColor, 2.0f);

  auto displayName = [](std::string name) {
    std::replace(name.begin(), name.end(), '_', ' ');
    if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
  };

  // Compact world/target status in the upper-left corner.
  char positionText[128];
  snprintf(positionText, sizeof(positionText), "XYZ  %.1f  %.1f  %.1f",
           m_playerEye.x, m_playerEye.y, m_playerEye.z);
  char targetText[128];
  if (m_highlightVisible) {
    snprintf(targetText, sizeof(targetText), "Target  %d  %d  %d",
             m_highlightX, m_highlightY, m_highlightZ);
  } else if (m_lastHighlightValid) {
    snprintf(targetText, sizeof(targetText), "Last target  %d  %d  %d  |  Out of reach",
             m_lastHighlightX, m_lastHighlightY, m_lastHighlightZ);
  } else {
    snprintf(targetText, sizeof(targetText), "No target  |  Reach %.0fm",
             m_voxelSettings.interaction.reach);
  }
  const ImVec2 statusMin(workMin.x + 12.0f, workMin.y + 62.0f);
  const ImVec2 statusMax(statusMin.x + 238.0f, statusMin.y + 54.0f);
  drawList->AddRectFilled(statusMin, statusMax, panelColor, 4.0f);
  drawList->AddText(ImVec2(statusMin.x + 10.0f, statusMin.y + 8.0f), textColor, positionText);
  drawList->AddText(ImVec2(statusMin.x + 10.0f, statusMin.y + 29.0f),
                    m_highlightVisible ? textColor
                      : (m_lastHighlightValid ? IM_COL32(255, 174, 76, 255) : mutedColor),
                    targetText);

  // Keep controls separate from the graphics panel and out of the hotbar.
  if (workSize.x >= 760.0f) {
    const ImVec2 controlsMin(workMin.x + 12.0f, statusMax.y + 8.0f);
    const ImVec2 controlsMax(controlsMin.x + 330.0f, controlsMin.y + 66.0f);
    drawList->AddRectFilled(controlsMin, controlsMax, panelColor, 4.0f);
    drawList->AddText(ImVec2(controlsMin.x + 10.0f, controlsMin.y + 8.0f), textColor,
                      "WASD Move   Shift Sprint   Space Jump");
    drawList->AddText(ImVec2(controlsMin.x + 10.0f, controlsMin.y + 29.0f), textColor,
                      "LMB Break   RMB Place   E Cursor");
    drawList->AddText(ImVec2(controlsMin.x + 10.0f, controlsMin.y + 50.0f), mutedColor,
                      "1-9 Hotbar   R Brick   F Glass   V/B/N More");
  }

  // Minecraft-style hotbar with authored block colors and stable slot sizes.
  const int slotCount = static_cast<int>(m_hotbar.size());
  if (slotCount <= 0) return;
  const float slotSize = (std::max)(30.0f,
    (std::min)(48.0f, (workSize.x - 24.0f) / static_cast<float>(slotCount)));
  const float barWidth = slotSize * slotCount + 8.0f;
  const float barHeight = slotSize + 8.0f;
  const ImVec2 barMin(center.x - barWidth * 0.5f,
                      workMin.y + workSize.y - barHeight - 12.0f);
  const ImVec2 barMax(barMin.x + barWidth, barMin.y + barHeight);
  drawList->AddRectFilled(barMin, barMax, IM_COL32(12, 15, 18, 215), 4.0f);

  for (int slot = 0; slot < slotCount; ++slot) {
    const int blockIndex = m_hotbar[slot];
    if (blockIndex < 0 || blockIndex >= static_cast<int>(m_blockDefs.size())) continue;
    const BlockDef& block = m_blockDefs[blockIndex];
    const ImVec2 slotMin(barMin.x + 4.0f + slot * slotSize, barMin.y + 4.0f);
    const ImVec2 slotMax(slotMin.x + slotSize, slotMin.y + slotSize);
    const bool selected = blockIndex == m_selectedBlock;
    drawList->AddRectFilled(slotMin, slotMax,
      selected ? IM_COL32(88, 92, 88, 245) : IM_COL32(35, 40, 44, 230), 2.0f);
    drawList->AddRect(slotMin, slotMax,
      selected ? accentColor : IM_COL32(98, 105, 110, 220), 2.0f, 0,
      selected ? 3.0f : 1.0f);
    const ImVec2 swatchMin(slotMin.x + 9.0f, slotMin.y + 10.0f);
    const ImVec2 swatchMax(slotMax.x - 9.0f, slotMax.y - 8.0f);
    drawList->AddRectFilled(swatchMin, swatchMax,
      IM_COL32(block.color[0], block.color[1], block.color[2], 255), 2.0f);
    drawList->AddRect(swatchMin, swatchMax, outlineColor, 2.0f, 0, 1.0f);
    char keyLabel[4];
    snprintf(keyLabel, sizeof(keyLabel), "%d", slot + 1);
    drawList->AddText(ImVec2(slotMin.x + 4.0f, slotMin.y + 2.0f), textColor, keyLabel);
  }

  const std::string selectedName = displayName(m_blockDefs[m_selectedBlock].name);
  const ImVec2 selectedSize = ImGui::CalcTextSize(selectedName.c_str());
  const float selectedY = barMin.y - selectedSize.y - 8.0f;
  drawList->AddText(ImVec2(center.x - selectedSize.x * 0.5f, selectedY),
                    textColor, selectedName.c_str());
  if (m_interactionMessageTime > 0.0f && !m_interactionMessage.empty()) {
    const std::string message = displayName(m_interactionMessage);
    const ImVec2 messageSize = ImGui::CalcTextSize(message.c_str());
    const ImVec2 messageMin(center.x - messageSize.x * 0.5f - 8.0f,
                            selectedY - messageSize.y - 12.0f);
    const ImVec2 messageMax(messageMin.x + messageSize.x + 16.0f,
                            messageMin.y + messageSize.y + 8.0f);
    drawList->AddRectFilled(messageMin, messageMax, panelColor, 4.0f);
    drawList->AddText(ImVec2(messageMin.x + 8.0f, messageMin.y + 4.0f),
                      textColor, message.c_str());
  }
}

void MinecraftScene::DrawDevGui(t850::DevGuiContext& gui) {
  if (gui.BeginSection("Minecraft")) {
    gui.Text("Voxel world demo");
    gui.Separator();
    char pos[128];
    snprintf(pos, sizeof(pos), "Player: %.1f, %.1f, %.1f", m_playerEye.x, m_playerEye.y, m_playerEye.z);
    gui.Text(pos);
    char chunk[128];
    snprintf(chunk, sizeof(chunk), "Chunk: %d, %d", m_centerChunkX, m_centerChunkZ);
    gui.Text(chunk);
    char distance[128];
    snprintf(distance, sizeof(distance), "Active draw radius: %d chunks%s",
         m_renderDistance, m_renderDistanceBuildTarget > 0 ? " (streaming)" : "");
    gui.Text(distance);
    t850::SliderDesc drawDistance;
    drawDistance.name = "draw_distance";
    drawDistance.label = "Draw distance (chunks)";
    drawDistance.min_val = 1.0f;
    drawDistance.max_val = (float)kMaxRenderDistance;
    drawDistance.step = 1.0f;
    float drawDistanceValue = (float)(m_pendingRenderDistance > 0
      ? m_pendingRenderDistance : m_renderDistance);
    if (gui.Slider(drawDistance, drawDistanceValue))
      m_pendingRenderDistance = (int)drawDistanceValue;
    gui.Separator();

    static int s_skyIndex = 0;
    for (int i = 0; i < (int)m_voxelSettings.environment_options.size(); ++i)
      if (m_voxelSettings.environment_options[i] == m_currentCubemapPath) s_skyIndex = i;
    t850::SelectorDesc skyDesc;
    skyDesc.name = "skybox";
    skyDesc.label = "Skybox";
    skyDesc.options = m_voxelSettings.environment_options;
    skyDesc.default_index = s_skyIndex;
    if (gui.Combo(skyDesc, s_skyIndex)) {
      const std::string newPath = m_voxelSettings.environment_options[s_skyIndex];
      if (newPath != m_currentCubemapPath) {
        m_pendingCubemap = newPath;
        T8_LOG_INFO("[Minecraft] Skybox change queued: '%s'", m_pendingCubemap.c_str());
      }
    }

    gui.Separator();
    t850::CheckboxDesc showPhysics;
    showPhysics.name = "show_physics";
    showPhysics.label = "Show collision bounds";
    gui.Checkbox(showPhysics, m_showPhysics);
    t850::CheckboxDesc showBounds;
    showBounds.name = "show_bounds";
    showBounds.label = "Show chunk bounds";
    gui.Checkbox(showBounds, m_showChunkBounds);
    t850::CheckboxDesc showNavMesh;
    showNavMesh.name = "show_navmesh";
    showNavMesh.label = "Show navigation mesh";
    gui.Checkbox(showNavMesh, m_showNavMesh);
    t850::CheckboxDesc showLights;
    showLights.name = "show_lights";
    showLights.label = "Show Sun marker";
    gui.Checkbox(showLights, m_showLights);
  }

  if (gui.BeginSection("Rendering")) {
    auto activeKernel = [&]() -> GaussFilter* {
       return m_selectedGaussKernel >= 0 &&
           m_selectedGaussKernel < (int)SceneProp.pGaussKernels.size()
         ? SceneProp.pGaussKernels[m_selectedGaussKernel] : nullptr;
    };
    auto sliderValue = [&](const std::string& name, float& value) -> bool {
      if (name == "exposure") value = SceneProp.Exposure;
      else if (name == "bloom_factor") value = SceneProp.BloomFactor;
      else if (name == "bloom_threshold") value = SceneProp.BloomThreshold;
      else if (name == "tm_white_level") value = SceneProp.ToneMapWhiteLevel;
      else if (name == "tm_adapt_tau") value = SceneProp.LuminanceTau;
      else if (name == "pcf_radius") value = SceneProp.PCFScale;
      else if (name == "pcf_samples") value = SceneProp.PCFSamples;
      else if (name == "ssao_kernel_size") value = (float)SceneProp.SSAOKernel.KernelSize;
      else if (name == "ssao_radius") value = SceneProp.SSAOKernel.Radius;
      else if (name == "dof_focus_range") value = SceneProp.DOFFocusRange;
      else if (name == "dof_focus_falloff") value = SceneProp.DOFFocusFalloff;
      else if (name == "dof_auto_focus_radius") value = SceneProp.DOFAutoFocusRadius;
      else if (name == "dof_max_coc") value = SceneProp.MaxCoc;
      else if (name == "dof_far_samples") value = SceneProp.DOF_Far_Samples_squared;
      else if (name == "dof_near_samples") value = SceneProp.DOF_Near_Samples_squared;
      else if (name == "parallax_low_samples") value = SceneProp.ParallaxLowSamples;
      else if (name == "parallax_high_samples") value = SceneProp.ParallaxHighSamples;
      else if (name == "parallax_height") value = SceneProp.ParallaxHeight;
      else if (name == "parallax_shadow_min_layers") value = SceneProp.ParallaxShadowMinLayers;
      else if (name == "parallax_shadow_max_layers") value = SceneProp.ParallaxShadowMaxLayers;
      else if (name == "parallax_shadow_softness") value = SceneProp.ParallaxShadowSoftness;
      else if (name == "parallax_shadow_strength") value = SceneProp.ParallaxShadowStrength;
      else if (name == "light_volume_steps") value = SceneProp.LightVolumeSteps;
      else if (name == "godrays_factor") value = SceneProp.GodRaysFactor;
      else if (name == "gauss_kernel_radius" && activeKernel()) value = activeKernel()->radius;
      else if (name == "gauss_kernel_deviation" && activeKernel()) value = activeKernel()->sigma;
      else if (name == "fov") value = Rad2Deg(Cam.Fov);
      else if (name == "sun_intensity_night") value = m_voxelSettings.day_night.sun_intensity_night;
      else if (name == "sun_intensity_day") value = m_voxelSettings.day_night.sun_intensity_day;
      else if (name == "light_radius_scale") value = SceneProp.LightRadiusScale;
      else if (name == "light_intensity_scale") value = SceneProp.LightIntensityScale;
      else if (name == "shadow_bias") value = m_shadowBias;
      else if (name == "shadow_min") value = m_shadowMin;
      else if (name == "env_factor") value = SceneProp.EnvFactor;
      else if (name == "ibl_factor") value = SceneProp.IBLFactor;
      else if (name == "lightmap_intensity") value = SceneProp.LightmapIntensity;
      else if (name == "material_emissive_intensity") value = SceneProp.MaterialEmissiveIntensity;
      else if (name == "material_transmission_multiplier") value = SceneProp.MaterialTransmissionMultiplier;
      else if (name == "material_refraction_strength") value = SceneProp.MaterialRefractionStrength;
      else return false;
      return true;
    };
    auto setSliderValue = [&](const std::string& name, float value) {
      if (name == "exposure") SceneProp.Exposure = value;
      else if (name == "bloom_factor") SceneProp.BloomFactor = value;
      else if (name == "bloom_threshold") SceneProp.BloomThreshold = value;
      else if (name == "tm_white_level") SceneProp.ToneMapWhiteLevel = value;
      else if (name == "tm_adapt_tau") SceneProp.LuminanceTau = value;
      else if (name == "pcf_radius") SceneProp.PCFScale = value;
      else if (name == "pcf_samples") SceneProp.PCFSamples = value;
      else if (name == "ssao_kernel_size") { SceneProp.SSAOKernel.KernelSize = (int)value; SceneProp.SSAOKernel.Update(); }
      else if (name == "ssao_radius") SceneProp.SSAOKernel.Radius = value;
      else if (name == "dof_focus_range") SceneProp.DOFFocusRange = value;
      else if (name == "dof_focus_falloff") SceneProp.DOFFocusFalloff = value;
      else if (name == "dof_auto_focus_radius") SceneProp.DOFAutoFocusRadius = value;
      else if (name == "dof_max_coc") SceneProp.MaxCoc = value;
      else if (name == "dof_far_samples") SceneProp.DOF_Far_Samples_squared = value;
      else if (name == "dof_near_samples") SceneProp.DOF_Near_Samples_squared = value;
      else if (name == "parallax_low_samples") SceneProp.ParallaxLowSamples = value;
      else if (name == "parallax_high_samples") SceneProp.ParallaxHighSamples = value;
      else if (name == "parallax_height") SceneProp.ParallaxHeight = value;
      else if (name == "parallax_shadow_min_layers") SceneProp.ParallaxShadowMinLayers = value;
      else if (name == "parallax_shadow_max_layers") SceneProp.ParallaxShadowMaxLayers = value;
      else if (name == "parallax_shadow_softness") SceneProp.ParallaxShadowSoftness = value;
      else if (name == "parallax_shadow_strength") SceneProp.ParallaxShadowStrength = value;
      else if (name == "light_volume_steps") SceneProp.LightVolumeSteps = value;
      else if (name == "godrays_factor") SceneProp.GodRaysFactor = value;
      else if (name == "gauss_kernel_radius" && activeKernel()) {
        activeKernel()->radius = value;
        activeKernel()->Update();
      }
      else if (name == "gauss_kernel_deviation" && activeKernel()) {
        activeKernel()->sigma = value;
        activeKernel()->Update();
      }
      else if (name == "fov") Cam.SetFov(Deg2Rad(value));
      else if (name == "sun_intensity_night") {
        m_voxelSettings.day_night.sun_intensity_night = value;
        UpdateDayNight(0.0f);
      }
      else if (name == "sun_intensity_day") {
        m_voxelSettings.day_night.sun_intensity_day = value;
        UpdateDayNight(0.0f);
      }
      else if (name == "light_radius_scale") SceneProp.LightRadiusScale = value;
      else if (name == "light_intensity_scale") SceneProp.LightIntensityScale = value;
      else if (name == "shadow_bias") m_shadowBias = value;
      else if (name == "shadow_min") m_shadowMin = value;
      else if (name == "env_factor") SceneProp.EnvFactor = value;
      else if (name == "ibl_factor") SceneProp.IBLFactor = value;
      else if (name == "lightmap_intensity") SceneProp.LightmapIntensity = value;
      else if (name == "material_emissive_intensity") SceneProp.MaterialEmissiveIntensity = value;
      else if (name == "material_transmission_multiplier") SceneProp.MaterialTransmissionMultiplier = value;
      else if (name == "material_refraction_strength") SceneProp.MaterialRefractionStrength = value;
    };
    for (const auto& desc : m_controlSetup.descriptor.sliders) {
      float value = 0.0f;
      if (sliderValue(desc.name, value) && gui.Slider(desc, value))
        setSliderValue(desc.name, value);
    }

    auto checkboxValue = [&](const std::string& name, bool& value) -> bool {
      if (name == "shadow_toggle") value = SceneProp.ToogleShadow != 0;
      else if (name == "ssao_toggle") value = SceneProp.ToogleSSAO != 0;
      else if (name == "dof_toggle") value = SceneProp.ToogleDOF != 0;
      else if (name == "dof_auto_focus") value = SceneProp.AutoFocus;
      else if (name == "parallax_toggle") value = SceneProp.ToogleParallax != 0;
      else if (name == "parallax_shadow_toggle") value = SceneProp.ToogleParallaxShadow != 0;
      else if (name == "godrays_toggle") value = SceneProp.ToogleGodRays != 0;
      else if (name == "point_lights_enabled") value = SceneProp.PointLightsEnabled;
      else return false;
      return true;
    };
    for (const auto& desc : m_controlSetup.descriptor.checkboxes) {
      bool value = false;
      if (!checkboxValue(desc.name, value) || !gui.Checkbox(desc, value)) continue;
      if (desc.name == "shadow_toggle") { SceneProp.ToogleShadow = value; m_shadowsEnabled = value; }
      else if (desc.name == "ssao_toggle") SceneProp.ToogleSSAO = value;
      else if (desc.name == "dof_toggle") {
        SceneProp.ToogleDOF = value;
        m_renderGraph.SetPassEnabled("CoC", value);
        m_renderGraph.SetPassEnabled("Combine CoC", value);
        m_renderGraph.SetPassEnabled("DOF", value);
        m_renderGraph.SetPassEnabled("DOF 2", value);
      }
      else if (desc.name == "dof_auto_focus") SceneProp.AutoFocus = value;
      else if (desc.name == "parallax_toggle") SceneProp.ToogleParallax = value;
      else if (desc.name == "parallax_shadow_toggle") SceneProp.ToogleParallaxShadow = value;
      else if (desc.name == "godrays_toggle") SceneProp.ToogleGodRays = value;
      else if (desc.name == "point_lights_enabled") SceneProp.PointLightsEnabled = value;
    }

    for (const auto& desc : m_controlSetup.descriptor.selectors) {
      int selected = desc.default_index;
      if (desc.name == "num_lights") {
        for (int option = 0; option < (int)desc.options.size(); ++option)
          if (std::atoi(desc.options[option].c_str()) == SceneProp.ActiveLights) selected = option;
      }
      else if (desc.name == "active_gauss_kernel") selected = m_selectedGaussKernel;
      else if (desc.name == "luminance_mode") selected = SceneProp.LuminanceMode;
      else if (desc.name == "gauss_kernel_sample_count") {
        GaussFilter* kernel = activeKernel();
        if (kernel) {
          for (int option = 0; option < (int)desc.options.size(); ++option)
            if (std::atoi(desc.options[option].c_str()) == kernel->kernelSize) selected = option;
        }
      } else continue;
      if (!gui.Combo(desc, selected)) continue;
      if (desc.name == "num_lights" && selected >= 0 && selected < (int)desc.options.size())
        SceneProp.ActiveLights = std::atoi(desc.options[selected].c_str());
      else if (desc.name == "active_gauss_kernel") m_selectedGaussKernel = selected;
      else if (desc.name == "luminance_mode") SceneProp.LuminanceMode = selected;
      else if (desc.name == "gauss_kernel_sample_count" && selected >= 0 && selected < (int)desc.options.size()) {
        GaussFilter* kernel = activeKernel();
        if (kernel) { kernel->kernelSize = std::atoi(desc.options[selected].c_str()); kernel->Update(); }
      }
    }

    t850::SelectorDesc debugTarget;
    debugTarget.name = "debug_render_target";
    debugTarget.label = "Debug RT";
    debugTarget.default_index = 0;
    for (const auto& target : m_voxelSettings.debug_render_targets)
      debugTarget.options.push_back(target.label);
    gui.Combo(debugTarget, m_debugRTSelection);
  }

  if (gui.BeginSection("Culling")) {
    t850::CheckboxDesc enabled;
    enabled.name = "frustum_culling";
    enabled.label = "Player frustum culling";
    enabled.enabled = SceneProp.FrustumCullingToggleAllowed;
    gui.Checkbox(enabled, SceneProp.FrustumCullingEnabled);

    t850::CheckboxDesc debug;
    debug.name = "show_culling_debug";
    debug.label = "Show culling debug";
    gui.Checkbox(debug, SceneProp.ShowCullingDebug);
  }

  // Shadow controls live in the shared Scene Controls panel, matching DayScene.
  if (gui.BeginSection("Shadows")) {
    gui.Text("Directional CSM controls");
    gui.Separator();

    t850::CheckboxDesc shadowEnable;
    shadowEnable.name = "shadow_enable";
    shadowEnable.label = "Enable shadows";
    if (gui.Checkbox(shadowEnable, m_shadowsEnabled)) {
      SceneProp.ToogleShadow = m_shadowsEnabled ? 1 : 0;
    }

    gui.BeginSection("Cascades");
    {
      t850::SliderDesc cascadeCount;
      cascadeCount.name = "cascade_count";
      cascadeCount.label = "Cascade count";
      cascadeCount.min_val = 1.0f;
      cascadeCount.max_val = 6.0f;
      cascadeCount.step = 1.0f;
      float cc = (float)m_cascadeCount;
      if (gui.Slider(cascadeCount, cc)) {
        m_cascadeCount = (int)cc;
        m_shadowConfigDirty = true;
      }

      t850::SliderDesc splitLambda;
      splitLambda.name = "split_lambda";
      splitLambda.label = "Split lambda";
      splitLambda.min_val = 0.0f;
      splitLambda.max_val = 1.0f;
      splitLambda.step = 0.01f;
      gui.Slider(splitLambda, m_splitLambda);

      t850::SliderDesc shadowBias;
      shadowBias.name = "shadow_bias";
      shadowBias.label = "Shadow bias";
      shadowBias.min_val = 0.0f;
      shadowBias.max_val = 0.01f;
      shadowBias.step = 0.0001f;
      gui.Slider(shadowBias, m_shadowBias);

      t850::SliderDesc shadowMin;
      shadowMin.name = "shadow_min";
      shadowMin.label = "Shadow min";
      shadowMin.min_val = 0.0f;
      shadowMin.max_val = 1.0f;
      shadowMin.step = 0.01f;
      gui.Slider(shadowMin, m_shadowMin);
    }

    gui.BeginSection("Resolution");
    {
      t850::SliderDesc res;
      res.name = "shadow_resolution";
      res.label = "Shadow map resolution";
      res.min_val = 256.0f;
      res.max_val = 4096.0f;
      res.step = 256.0f;
      if (gui.Slider(res, m_shadowResolution)) {
        m_shadowConfigDirty = true;
      }
    }

    gui.BeginSection("Debug");
    {
      t850::CheckboxDesc showFrustums;
      showFrustums.name = "show_frustums";
      showFrustums.label = "Show cascade debug";
      gui.Checkbox(showFrustums, m_showCascadeFrustums);

      t850::SelectorDesc cascadeDebugMode;
      cascadeDebugMode.name = "cascade_debug_mode";
      cascadeDebugMode.label = "Cascade debug geometry";
      cascadeDebugMode.options = {"Cascade regions", "Light bounds (overlap expected)", "Both"};
      cascadeDebugMode.default_index = 0;
      gui.Combo(cascadeDebugMode, m_cascadeDebugMode);

      t850::SliderDesc cascadeOpacity;
      cascadeOpacity.name = "cascade_debug_opacity";
      cascadeOpacity.label = "Cascade volume opacity";
      cascadeOpacity.min_val = 0.01f;
      cascadeOpacity.max_val = 0.75f;
      cascadeOpacity.step = 0.01f;
      gui.Slider(cascadeOpacity, m_cascadeDebugOpacity);
    }

    gui.BeginSection("Cameras");
    {
      t850::SelectorDesc cameraMode;
      cameraMode.name = "camera_mode";
      cameraMode.label = "View camera";
      cameraMode.options = {"Player", "Free spectator", "Light"};
      cameraMode.default_index = 0;
      if (gui.Combo(cameraMode, m_cameraMode) && m_cameraMode != 2 && m_lightCameraEditMode)
        SetLightCameraEditMode(false);

      if (gui.Button(m_lightCameraEditMode ? "Finish moving light camera" : "Move light camera"))
        SetLightCameraEditMode(!m_lightCameraEditMode);

      if (gui.Button("Resume sun trajectory", m_sunTrajectoryPaused)) {
        m_lightCameraEditMode = false;
        m_sunTrajectoryPaused = false;
        UpdateDayNight(0.0f);
        T8_LOG_INFO("[Minecraft] Sun trajectory resumed");
      }

      t850::CheckboxDesc animateSun;
      animateSun.name = "animate_sun";
      animateSun.label = "Animate sun trajectory";
      gui.Checkbox(animateSun, m_dayNightEnabled);

      t850::SliderDesc sunSpeed;
      sunSpeed.name = "sun_animation_speed";
      sunSpeed.label = "Sun animation speed";
      sunSpeed.min_val = 0.0f;
      sunSpeed.max_val = 2.0f;
      sunSpeed.step = 0.01f;
      gui.Slider(sunSpeed, m_voxelSettings.day_night.animation_speed);

      t850::CheckboxDesc debugOrtho;
      debugOrtho.name = "debug_camera_ortho";
      debugOrtho.label = "Light camera orthographic";
      if (gui.Checkbox(debugOrtho, m_debugCameraOrtho)) {
        LightCam.Ortho = m_debugCameraOrtho;
        LightCam.CreatePojection();
        LightCam.Update(0.0f);
      }

      if (m_lightCameraEditMode) {
        t850::SliderDesc yaw;
        yaw.name = "light_yaw";
        yaw.label = "Light yaw";
        yaw.min_val = -3.14159f;
        yaw.max_val = 3.14159f;
        yaw.step = 0.01f;
        if (gui.Slider(yaw, m_lightYaw)) {
          LightCam.Yaw = m_lightYaw;
          LightCam.Pitch = m_lightPitch;
          LightCam.Update(0.0f);
          SyncSunFromLightCamera();
        }

        t850::SliderDesc pitch;
        pitch.name = "light_pitch";
        pitch.label = "Light pitch";
        pitch.min_val = -1.5f;
        pitch.max_val = 1.5f;
        pitch.step = 0.01f;
        if (gui.Slider(pitch, m_lightPitch)) {
          LightCam.Yaw = m_lightYaw;
          LightCam.Pitch = m_lightPitch;
          LightCam.Update(0.0f);
          SyncSunFromLightCamera();
        }
      }

    }

    gui.Separator();
    if (gui.Button("Save scene and cameras")) {
      SaveSceneSettings();
    }
  }
}

// Apply a queued skybox change. Must run on the render thread before
// rendering (D3D12 texture upload submits a temp command list + fence).
void MinecraftScene::ApplyPendingCubemap() {
  if (m_pendingCubemap.empty()) return;
  T8_LOG_INFO("[Minecraft] Loading cubemap '%s' (old slot=%d)", m_pendingCubemap.c_str(), EnvMapTexIndex);
  g_pBaseDriver->WaitForGPU();
  int newEnvMapTexIndex = g_pBaseDriver->CreateTexture(m_pendingCubemap);
  if (newEnvMapTexIndex >= 0) {
    if (EnvMapTexIndex >= 0 && EnvMapTexIndex != newEnvMapTexIndex)
      g_pBaseDriver->DestroyTexture(EnvMapTexIndex);
    EnvMapTexIndex = newEnvMapTexIndex;
    m_currentCubemapPath = m_pendingCubemap;
    EnvMaps.SetFallback(EnvMapTexIndex);
    LoadEnvironmentIBLResources(
      g_pBaseDriver, {}, EnvMaps,
      DiffuseIBLTexIndex, SpecularIBLTexIndex, BrdfLUTTexIndex,
      SheenIBLTexIndex, CharlieLUTTexIndex, SheenELUTTexIndex);
    UpdateSceneIBLSettings(SceneProp, g_pBaseDriver, EnvMaps);
    Texture* newTex = g_pBaseDriver->GetTexture(EnvMapTexIndex);
    T8_LOG_INFO("[Minecraft] Cubemap loaded: slot=%d tex=%p", EnvMapTexIndex, newTex);
    Quads[0].SetEnvironmentMap(newTex);
    for (int i = 0; i < m_maxChunks; ++i) {
      if (Meshes[i].pBase) Meshes[i].SetEnvironmentMap(newTex);
    }
  } else {
    T8_LOG_ERROR("[Minecraft] Failed to load cubemap '%s'; keeping previous", m_pendingCubemap.c_str());
  }
  m_pendingCubemap.clear();
}

void MinecraftScene::ResetViewInput() {
  // Nothing to reset (input is read directly)
}
