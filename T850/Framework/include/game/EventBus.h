#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace t850::game {

struct GameEvent {
  uint64_t sequence = 0;
  uint64_t tick = 0;
  std::string type;
  std::string sourceEntityId;
  std::string targetEntityId;
  std::map<std::string, std::string> params;
};

class Subscription {
public:
  bool IsValid() const { return id_ != 0; }

private:
  friend class EventBus;
  uint64_t id_ = 0;
  std::string eventType_;
};

class EventBus {
public:
  using Handler = std::function<void(const GameEvent&)>;

  Subscription Subscribe(std::string_view eventType, Handler handler);
  void Unsubscribe(const Subscription& subscription);
  void Publish(GameEvent event);
  void DispatchQueued(uint64_t tickIndex);
  std::span<const GameEvent> RecentEvents() const;
  std::size_t PendingCount() const;
  std::size_t LastDispatchCount() const;
  void Clear();

private:
  struct HandlerEntry {
    uint64_t id = 0;
    Handler handler;
  };

  static constexpr std::size_t kRecentEventCapacity = 512;
  uint64_t nextSubscriptionId_ = 1;
  uint64_t nextSequence_ = 1;
  std::unordered_map<std::string, std::vector<HandlerEntry>> handlers_;
  std::vector<GameEvent> nextQueue_;
  std::vector<GameEvent> recentEvents_;
  std::size_t lastDispatchCount_ = 0;
};

} // namespace t850::game