#include <pch.h>

#include <terrain/VoxelStreaming.h>

#include <utils/ThreadPool.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace t850::terrain {

VoxelStreamingManager::VoxelStreamingManager(ChunkDimensions dimensions)
    : m_dimensions(dimensions) {}

VoxelStreamingManager::~VoxelStreamingManager() {
  Reset();
}

void VoxelStreamingManager::SetSettings(VoxelStreamingSettings settings) {
  settings.horizontalRadius = (std::max)(0, settings.horizontalRadius);
  settings.verticalRadius = (std::max)(0, settings.verticalRadius);
  settings.maxInFlight = (std::max<std::size_t>)(1, settings.maxInFlight);
  settings.maxLaunchesPerUpdate = (std::max<std::size_t>)(1, settings.maxLaunchesPerUpdate);
  settings.maxCommitsPerUpdate = (std::max<std::size_t>)(1, settings.maxCommitsPerUpdate);
  settings.maxUnloadsPerUpdate = (std::max<std::size_t>)(1, settings.maxUnloadsPerUpdate);
  m_settings = settings;
}

int64_t VoxelStreamingManager::DistanceSquared(ChunkKey left, ChunkKey right) {
  const int64_t dx = static_cast<int64_t>(left.x) - right.x;
  const int64_t dy = static_cast<int64_t>(left.y) - right.y;
  const int64_t dz = static_cast<int64_t>(left.z) - right.z;
  return dx * dx + dy * dy + dz * dz;
}

uint64_t VoxelStreamingManager::NextEpoch(ChunkKey key) {
  uint64_t& epoch = m_epochs[key];
  ++epoch;
  if (epoch == 0) epoch = 1;
  return epoch;
}

void VoxelStreamingManager::CollectReady() {
  for (auto iterator = m_pending.begin(); iterator != m_pending.end();) {
    PendingBuild& pending = iterator->second;
    if (!pending.future.valid() ||
        pending.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++iterator;
      continue;
    }

    VoxelChunkBuildResult result;
    try {
      result = pending.future.get();
    } catch (const std::exception& exception) {
      result.error = exception.what();
    } catch (...) {
      result.error = "unknown voxel chunk build exception";
    }
    result.key = pending.request.key;
    result.epoch = pending.request.epoch;
    const bool cancelled = pending.request.IsCancelled() || result.cancelled;
    const auto epoch = m_epochs.find(result.key);
    const bool stale = epoch == m_epochs.end() || epoch->second != result.epoch || !m_desired.contains(result.key);
    if (cancelled) {
      ++m_stats.cancelled;
    } else if (stale) {
      ++m_stats.stale;
    } else if (!result.Succeeded()) {
      ++m_stats.failed;
    } else {
      m_readyKeys.insert(result.key);
      m_ready.push_back(std::move(result));
      ++m_stats.completed;
    }
    iterator = m_pending.erase(iterator);
  }
}

void VoxelStreamingManager::Update(ChunkKey focus,
                                   std::span<const ChunkKey> loadedChunks,
                                   ThreadPool* threadPool,
                                   const VoxelChunkBuildFunction& buildFunction) {
  m_focus = focus;
  m_desired.clear();
  for (int y = -m_settings.verticalRadius; y <= m_settings.verticalRadius; ++y) {
    for (int z = -m_settings.horizontalRadius; z <= m_settings.horizontalRadius; ++z) {
      for (int x = -m_settings.horizontalRadius; x <= m_settings.horizontalRadius; ++x) {
        m_desired.insert(ChunkKey{focus.x + x, focus.y + y, focus.z + z});
      }
    }
  }

  for (auto& [key, pending] : m_pending) {
    if (!m_desired.contains(key) && pending.request.cancelled) {
      pending.request.cancelled->store(true, std::memory_order_relaxed);
      NextEpoch(key);
    }
  }
  m_queued.erase(
      std::remove_if(m_queued.begin(), m_queued.end(), [&](ChunkKey key) {
        if (m_desired.contains(key)) return false;
        m_queuedSet.erase(key);
        return true;
      }),
      m_queued.end());
  CollectReady();

  std::unordered_set<ChunkKey, ChunkKeyHash> loaded(loadedChunks.begin(), loadedChunks.end());
  for (ChunkKey key : loadedChunks) {
    if (!m_desired.contains(key) && m_unloadSet.insert(key).second) m_unloads.push_back(key);
  }
  for (auto iterator = m_unloads.begin(); iterator != m_unloads.end();) {
    if (!m_desired.contains(*iterator)) {
      ++iterator;
    } else {
      m_unloadSet.erase(*iterator);
      iterator = m_unloads.erase(iterator);
    }
  }

  for (ChunkKey key : m_desired) {
    if (loaded.contains(key) || m_pending.contains(key) || m_queuedSet.contains(key) ||
        m_readyKeys.contains(key)) {
      continue;
    }
    m_queued.push_back(key);
    m_queuedSet.insert(key);
  }
  std::sort(m_queued.begin(), m_queued.end(), [&](ChunkKey left, ChunkKey right) {
    return DistanceSquared(left, m_focus) < DistanceSquared(right, m_focus);
  });

  std::size_t launches = 0;
  while (!m_queued.empty() && launches < m_settings.maxLaunchesPerUpdate &&
         m_pending.size() < m_settings.maxInFlight) {
    const ChunkKey key = m_queued.front();
    m_queued.erase(m_queued.begin());
    m_queuedSet.erase(key);
    VoxelChunkBuildRequest request;
    request.key = key;
    request.dimensions = m_dimensions;
    request.epoch = NextEpoch(key);
    request.cancelled = std::make_shared<std::atomic_bool>(false);
    ++launches;
    ++m_stats.launched;

    if (!threadPool) {
      VoxelChunkBuildResult result = buildFunction(request);
      result.key = request.key;
      result.epoch = request.epoch;
      if (result.Succeeded() && m_desired.contains(result.key)) {
        m_readyKeys.insert(result.key);
        m_ready.push_back(std::move(result));
        ++m_stats.completed;
      } else if (result.cancelled) {
        ++m_stats.cancelled;
      } else {
        ++m_stats.failed;
      }
      continue;
    }

    auto future = threadPool->Submit([request, buildFunction]() mutable {
      if (request.IsCancelled()) {
        VoxelChunkBuildResult cancelled;
        cancelled.cancelled = true;
        return cancelled;
      }
      return buildFunction(request);
    });
    m_pending.emplace(key, PendingBuild{std::move(request), std::move(future)});
  }
  RefreshStats();
}

std::vector<VoxelChunkBuildResult> VoxelStreamingManager::TakeCompleted(std::size_t maximum) {
  CollectReady();
  if (maximum == 0) maximum = m_settings.maxCommitsPerUpdate;
  const std::size_t count = (std::min)(maximum, m_ready.size());
  std::vector<VoxelChunkBuildResult> results;
  results.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    m_readyKeys.erase(m_ready[index].key);
    results.push_back(std::move(m_ready[index]));
  }
  m_ready.erase(m_ready.begin(), m_ready.begin() + static_cast<std::ptrdiff_t>(count));
  RefreshStats();
  return results;
}

std::vector<ChunkKey> VoxelStreamingManager::TakeUnloadRequests(std::size_t maximum) {
  if (maximum == 0) maximum = m_settings.maxUnloadsPerUpdate;
  const std::size_t count = (std::min)(maximum, m_unloads.size());
  std::vector<ChunkKey> results(m_unloads.begin(), m_unloads.begin() + static_cast<std::ptrdiff_t>(count));
  for (ChunkKey key : results) m_unloadSet.erase(key);
  m_unloads.erase(m_unloads.begin(), m_unloads.begin() + static_cast<std::ptrdiff_t>(count));
  RefreshStats();
  return results;
}

void VoxelStreamingManager::RefreshStats() {
  m_stats.desired = m_desired.size();
  m_stats.queued = m_queued.size();
  m_stats.inFlight = m_pending.size();
  m_stats.ready = m_ready.size();
  m_stats.unloadQueued = m_unloads.size();
}

void VoxelStreamingManager::Reset() {
  for (auto& [key, pending] : m_pending) {
    (void)key;
    if (pending.request.cancelled) pending.request.cancelled->store(true, std::memory_order_relaxed);
  }
  for (auto& [key, pending] : m_pending) {
    (void)key;
    if (!pending.future.valid()) continue;
    try {
      pending.future.get();
    } catch (...) {
    }
  }
  m_desired.clear();
  m_queued.clear();
  m_queuedSet.clear();
  m_pending.clear();
  m_ready.clear();
  m_readyKeys.clear();
  m_unloads.clear();
  m_unloadSet.clear();
  m_epochs.clear();
  m_stats = VoxelStreamingStats{};
}

} // namespace t850::terrain
