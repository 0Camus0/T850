#pragma once

#include <game/GameIds.h>
#include <navigation/NavigationSystem.h>

#include <cstdint>
#include <future>
#include <unordered_map>
#include <vector>

namespace t850 {
class ThreadPool;
}

namespace t850::game {

class GameNavigationService {
public:
  static constexpr uint64_t kInvalidRequestId = 0;

  GameNavigationService() = default;
  ~GameNavigationService();
  GameNavigationService(const GameNavigationService&) = delete;
  GameNavigationService& operator=(const GameNavigationService&) = delete;

  void Bind(t850::navigation::NavMesh* navMesh, t850::ThreadPool* pool);
  uint64_t RequestPath(RuntimeGameObjectId requester,
                       const XVECTOR3& start,
                       const XVECTOR3& goal);
  bool TryGetResult(uint64_t requestId, t850::navigation::NavPathResult& out);
  bool ProjectToNavmesh(const XVECTOR3& point, XVECTOR3& out) const;
  bool Available() const;

  void ResolveCompleted();
  void PrepareForNavMeshMutation();
  void Reset();

private:
  struct QueuedRequest {
    uint64_t requestId = kInvalidRequestId;
    RuntimeGameObjectId requester = kInvalidRuntimeGameObjectId;
    t850::navigation::NavPathRequest request;
  };

  struct PendingBatch {
    std::vector<uint64_t> requestIds;
    std::future<std::vector<t850::navigation::NavPathResult>> future;
  };

  void DrainPending();
  void StoreBatchResults(const std::vector<uint64_t>& requestIds,
                         std::vector<t850::navigation::NavPathResult> results);

  t850::navigation::NavMesh* navMesh_ = nullptr;
  t850::ThreadPool* pool_ = nullptr;
  uint64_t nextRequestId_ = 1;
  std::vector<QueuedRequest> queuedRequests_;
  std::vector<PendingBatch> pendingBatches_;
  std::unordered_map<uint64_t, t850::navigation::NavPathResult> completedResults_;
};

} // namespace t850::game