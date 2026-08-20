#include <pch.h>

#include <game/GameIds.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace t850::game {

std::string MakeStableId(std::string_view prefix) {
  static std::atomic<uint64_t> counter{0};
  const uint64_t timestamp = static_cast<uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  const uint64_t sequence = counter.fetch_add(1, std::memory_order_relaxed);

  std::ostringstream stream;
  stream << prefix << std::hex << std::setfill('0')
         << std::setw(16) << timestamp
         << std::setw(16) << sequence;
  return stream.str();
}

} // namespace t850::game