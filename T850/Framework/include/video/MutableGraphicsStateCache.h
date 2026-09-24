#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace t850 {

class MutableGraphicsStateCache {
public:
  enum class Slot : size_t { Blend, Depth, Cull, Count };

  bool Select(Slot slot, uint8_t value) {
    const size_t index = static_cast<size_t>(slot);
    ++m_requests[index];
    int& selected = m_selected[index];
    if (selected == value) {
      ++m_redundant[index];
      return false;
    }
    selected = value;
    ++m_changes[index];
    return true;
  }

  void Reset() {
    m_selected.fill(-1);
    m_requests.fill(0);
    m_changes.fill(0);
    m_redundant.fill(0);
  }

  uint64_t Requests() const { return Sum(m_requests); }
  uint64_t Changes() const { return Sum(m_changes); }
  uint64_t Redundant() const { return Sum(m_redundant); }
  uint64_t Requests(Slot slot) const { return m_requests[static_cast<size_t>(slot)]; }
  uint64_t Changes(Slot slot) const { return m_changes[static_cast<size_t>(slot)]; }
  uint64_t Redundant(Slot slot) const { return m_redundant[static_cast<size_t>(slot)]; }

private:
  static uint64_t Sum(const std::array<uint64_t, static_cast<size_t>(Slot::Count)>& values) {
    return values[0] + values[1] + values[2];
  }
  std::array<int, static_cast<size_t>(Slot::Count)> m_selected{{-1, -1, -1}};
  std::array<uint64_t, static_cast<size_t>(Slot::Count)> m_requests{};
  std::array<uint64_t, static_cast<size_t>(Slot::Count)> m_changes{};
  std::array<uint64_t, static_cast<size_t>(Slot::Count)> m_redundant{};
};

} // namespace t850
