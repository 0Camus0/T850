#include <pch.h>

#include <game/GameNavigationService.h>

#include <debug/RuntimeTelemetry.h>
#include <utils/ThreadPool.h>

#include <chrono>
#include <exception>
#include <utility>

namespace t850::game {
namespace {

t850::navigation::NavPathResult FailedPathResult(std::string error) {
  t850::navigation::NavPathResult result;
  result.error = std::move(error);
  return result;
}

} // namespace

GameNavigationService::~GameNavigationService() {
  Reset();
}

void GameNavigationService::Bind(t850::navigation::NavMesh* navMesh, t850::ThreadPool* pool) {
  if (navMesh_ == navMesh && pool_ == pool) return;
  DrainPending();
  navMesh_ = navMesh;
  pool_ = pool;
}

uint64_t GameNavigationService::RequestPath(RuntimeGameObjectId requester,
                                            const XVECTOR3& start,
                                            const XVECTOR3& goal) {
  if (!Available()) return kInvalidRequestId;

  uint64_t requestId = nextRequestId_++;
  if (requestId == kInvalidRequestId) requestId = nextRequestId_++;

  t850::navigation::NavPathRequest request;
  request.start = start;
  request.end = goal;
  queuedRequests_.push_back(QueuedRequest{requestId, requester, request});
  t850::RuntimeTelemetry::AddCounter("game.nav.requests", 1.0);
  return requestId;
}

bool GameNavigationService::TryGetResult(
    uint64_t requestId,
    t850::navigation::NavPathResult& out) {
  if (requestId == kInvalidRequestId) return false;
  const auto found = completedResults_.find(requestId);
  if (found == completedResults_.end()) return false;
  out = std::move(found->second);
  completedResults_.erase(found);
  return true;
}

bool GameNavigationService::ProjectToNavmesh(const XVECTOR3& point, XVECTOR3& out) const {
  return Available() && navMesh_->ProjectPoint(point, out);
}

bool GameNavigationService::Available() const {
  return navMesh_ && navMesh_->IsReady();
}

void GameNavigationService::ResolveCompleted() {
  for (auto batch = pendingBatches_.begin(); batch != pendingBatches_.end();) {
    if (batch->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++batch;
      continue;
    }

    try {
      StoreBatchResults(batch->requestIds, batch->future.get());
    } catch (const std::exception& exception) {
      std::vector<t850::navigation::NavPathResult> failures(
          batch->requestIds.size(), FailedPathResult(exception.what()));
      StoreBatchResults(batch->requestIds, std::move(failures));
    } catch (...) {
      std::vector<t850::navigation::NavPathResult> failures(
          batch->requestIds.size(), FailedPathResult("navigation worker failed"));
      StoreBatchResults(batch->requestIds, std::move(failures));
    }
    batch = pendingBatches_.erase(batch);
  }

  if (queuedRequests_.empty()) return;

  std::vector<QueuedRequest> queued;
  queued.swap(queuedRequests_);
  std::vector<uint64_t> requestIds;
  std::vector<t850::navigation::NavPathRequest> requests;
  requestIds.reserve(queued.size());
  requests.reserve(queued.size());
  for (const QueuedRequest& item : queued) {
    requestIds.push_back(item.requestId);
    requests.push_back(item.request);
  }

  if (!Available()) {
    std::vector<t850::navigation::NavPathResult> failures(
        requestIds.size(), FailedPathResult("navigation mesh is unavailable"));
    StoreBatchResults(requestIds, std::move(failures));
    return;
  }

  if (!pool_) {
    std::vector<t850::navigation::NavPathResult> results;
    navMesh_->FindPaths(requests, results);
    StoreBatchResults(requestIds, std::move(results));
    return;
  }

  t850::navigation::NavMesh* navMesh = navMesh_;
  auto future = pool_->Submit(
      [navMesh, requests = std::move(requests)]() mutable {
        std::vector<t850::navigation::NavPathResult> results;
        navMesh->FindPaths(requests, results);
        return results;
      });
  pendingBatches_.push_back(PendingBatch{std::move(requestIds), std::move(future)});
}

void GameNavigationService::Reset() {
  DrainPending();
  navMesh_ = nullptr;
  pool_ = nullptr;
  nextRequestId_ = 1;
}

void GameNavigationService::PrepareForNavMeshMutation() {
  for (auto& [requestId, result] : completedResults_) {
    (void)requestId;
    result = FailedPathResult("navigation mesh changed");
  }

  std::vector<uint64_t> canceledRequestIds;
  canceledRequestIds.reserve(queuedRequests_.size());
  for (const QueuedRequest& request : queuedRequests_) {
    canceledRequestIds.push_back(request.requestId);
  }
  queuedRequests_.clear();

  for (PendingBatch& batch : pendingBatches_) {
    if (batch.future.valid()) {
      try {
        batch.future.get();
      } catch (...) {
      }
    }
    canceledRequestIds.insert(
        canceledRequestIds.end(), batch.requestIds.begin(), batch.requestIds.end());
  }
  pendingBatches_.clear();

  for (uint64_t requestId : canceledRequestIds) {
    completedResults_.insert_or_assign(
        requestId, FailedPathResult("navigation mesh changed"));
  }
  t850::RuntimeTelemetry::AddCounter(
      "game.nav.completed", static_cast<double>(canceledRequestIds.size()));
}

void GameNavigationService::DrainPending() {
  for (PendingBatch& batch : pendingBatches_) {
    if (!batch.future.valid()) continue;
    try {
      batch.future.get();
    } catch (...) {
    }
  }
  queuedRequests_.clear();
  pendingBatches_.clear();
  completedResults_.clear();
}

void GameNavigationService::StoreBatchResults(
    const std::vector<uint64_t>& requestIds,
    std::vector<t850::navigation::NavPathResult> results) {
  if (results.size() < requestIds.size()) {
    results.resize(requestIds.size(), FailedPathResult("navigation batch returned no result"));
  }
  for (std::size_t index = 0; index < requestIds.size(); ++index) {
    completedResults_.insert_or_assign(requestIds[index], std::move(results[index]));
  }
  t850::RuntimeTelemetry::AddCounter(
      "game.nav.completed", static_cast<double>(requestIds.size()));
}

} // namespace t850::game