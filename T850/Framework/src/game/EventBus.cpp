#include <pch.h>

#include <game/EventBus.h>

#include <algorithm>
#include <utility>

namespace t850::game {

Subscription EventBus::Subscribe(std::string_view eventType, Handler handler) {
  Subscription subscription;
  if (eventType.empty() || !handler) return subscription;

  subscription.id_ = nextSubscriptionId_++;
  subscription.eventType_ = eventType;
  handlers_[subscription.eventType_].push_back(HandlerEntry{subscription.id_, std::move(handler)});
  return subscription;
}

void EventBus::Unsubscribe(const Subscription& subscription) {
  if (!subscription.IsValid()) return;
  const auto found = handlers_.find(subscription.eventType_);
  if (found == handlers_.end()) return;

  std::vector<HandlerEntry>& entries = found->second;
  entries.erase(
      std::remove_if(entries.begin(), entries.end(), [&](const HandlerEntry& entry) {
        return entry.id == subscription.id_;
      }),
      entries.end());
  if (entries.empty()) handlers_.erase(found);
}

void EventBus::Publish(GameEvent event) {
  event.sequence = nextSequence_++;
  nextQueue_.push_back(std::move(event));
}

void EventBus::DispatchQueued(uint64_t tickIndex) {
  std::vector<GameEvent> currentQueue;
  currentQueue.swap(nextQueue_);
  std::stable_sort(currentQueue.begin(), currentQueue.end(), [](const GameEvent& left, const GameEvent& right) {
    return left.sequence < right.sequence;
  });

  lastDispatchCount_ = currentQueue.size();
  for (GameEvent& event : currentQueue) {
    event.tick = tickIndex;
    recentEvents_.push_back(event);
    if (recentEvents_.size() > kRecentEventCapacity) {
      const std::size_t overflow = recentEvents_.size() - kRecentEventCapacity;
      recentEvents_.erase(recentEvents_.begin(), recentEvents_.begin() + static_cast<std::ptrdiff_t>(overflow));
    }

    const auto found = handlers_.find(event.type);
    if (found == handlers_.end()) continue;
    const std::vector<HandlerEntry> handlers = found->second;
    for (const HandlerEntry& entry : handlers) {
      if (entry.handler) entry.handler(event);
    }
  }
}

std::span<const GameEvent> EventBus::RecentEvents() const {
  return recentEvents_;
}

std::size_t EventBus::PendingCount() const {
  return nextQueue_.size();
}

std::size_t EventBus::LastDispatchCount() const {
  return lastDispatchCount_;
}

void EventBus::Clear() {
  handlers_.clear();
  nextQueue_.clear();
  recentEvents_.clear();
  lastDispatchCount_ = 0;
  nextSubscriptionId_ = 1;
  nextSequence_ = 1;
}

} // namespace t850::game