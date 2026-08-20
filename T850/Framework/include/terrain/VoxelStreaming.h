#pragma once

#include <terrain/VoxelWorld.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace t850 {
class ThreadPool;
}

namespace t850::terrain {

struct VoxelChunkBuildRequest {
  ChunkKey key;
  ChunkDimensions dimensions;
  uint64_t epoch = 0;
  std::shared_ptr<std::atomic_bool> cancelled;

  bool IsCancelled() const {
    return cancelled && cancelled->load(std::memory_order_relaxed);
  }
};

struct VoxelChunkBuildResult {
  ChunkKey key;
  uint64_t epoch = 0;
  std::unique_ptr<VoxelChunk> chunk;
  MutableMeshSnapshot mesh;
  bool cancelled = false;
  std::string error;

  bool Succeeded() const { return chunk != nullptr && !cancelled && error.empty(); }
};

using VoxelChunkBuildFunction =
    std::function<VoxelChunkBuildResult(const VoxelChunkBuildRequest& request)>;

struct VoxelStreamingSettings {
  int horizontalRadius = 2;
  int verticalRadius = 0;
  std::size_t maxInFlight = 4;
  std::size_t maxLaunchesPerUpdate = 2;
  std::size_t maxCommitsPerUpdate = 2;
  std::size_t maxUnloadsPerUpdate = 2;
};

struct VoxelStreamingStats {
  std::size_t desired = 0;
  std::size_t queued = 0;
  std::size_t inFlight = 0;
  std::size_t ready = 0;
  std::size_t unloadQueued = 0;
  uint64_t launched = 0;
  uint64_t completed = 0;
  uint64_t cancelled = 0;
  uint64_t stale = 0;
  uint64_t failed = 0;
};

class VoxelStreamingManager {
public:
  explicit VoxelStreamingManager(ChunkDimensions dimensions = {});
  ~VoxelStreamingManager();

  VoxelStreamingManager(const VoxelStreamingManager&) = delete;
  VoxelStreamingManager& operator=(const VoxelStreamingManager&) = delete;

  void SetSettings(VoxelStreamingSettings settings);
  const VoxelStreamingSettings& Settings() const { return m_settings; }

  void Update(ChunkKey focus,
              std::span<const ChunkKey> loadedChunks,
              ThreadPool* threadPool,
              const VoxelChunkBuildFunction& buildFunction);
  std::vector<VoxelChunkBuildResult> TakeCompleted(std::size_t maximum = 0);
  std::vector<ChunkKey> TakeUnloadRequests(std::size_t maximum = 0);
  void Reset();

  const VoxelStreamingStats& Stats() const { return m_stats; }
  bool IsDesired(ChunkKey key) const { return m_desired.contains(key); }

private:
  struct PendingBuild {
    VoxelChunkBuildRequest request;
    std::future<VoxelChunkBuildResult> future;
  };

  void CollectReady();
  void RefreshStats();
  uint64_t NextEpoch(ChunkKey key);
  static int64_t DistanceSquared(ChunkKey left, ChunkKey right);

  ChunkDimensions m_dimensions;
  VoxelStreamingSettings m_settings;
  ChunkKey m_focus;
  std::unordered_set<ChunkKey, ChunkKeyHash> m_desired;
  std::vector<ChunkKey> m_queued;
  std::unordered_set<ChunkKey, ChunkKeyHash> m_queuedSet;
  std::unordered_map<ChunkKey, PendingBuild, ChunkKeyHash> m_pending;
  std::unordered_map<ChunkKey, uint64_t, ChunkKeyHash> m_epochs;
  std::vector<VoxelChunkBuildResult> m_ready;
  std::unordered_set<ChunkKey, ChunkKeyHash> m_readyKeys;
  std::vector<ChunkKey> m_unloads;
  std::unordered_set<ChunkKey, ChunkKeyHash> m_unloadSet;
  VoxelStreamingStats m_stats;
};

} // namespace t850::terrain
