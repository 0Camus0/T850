#include <pch.h>

#include <scene/RenderQueue.h>

#include <algorithm>

namespace t850 {

  void RenderQueue::Sort() {
    std::sort(m_items.begin(), m_items.end(),
              [](const DrawItem& a, const DrawItem& b) {
                return a.sortKey < b.sortKey;
              });
  }

}
