#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace t850 {

class LoadingProgress {
public:
  struct Snapshot {
    std::string phase;
    std::string item;
    std::string detail;
    float completed = 0.0f;
    float total = 1.0f;
    float percent = 0.0f;
    bool active = false;
  };

  using FrameCallback = std::function<void()>;

  class ScopedStep {
  public:
    ScopedStep(std::string phase, std::string item, float weight = 1.0f, bool pumpOnBegin = true)
      : m_weight(weight),
        m_active(LoadingProgress::BeginStep(std::move(phase), std::move(item), pumpOnBegin)) {
    }

    ScopedStep(const char* phase, const std::string& item, float weight = 1.0f, bool pumpOnBegin = true)
      : ScopedStep(std::string(phase ? phase : ""), item, weight, pumpOnBegin) {
    }

    ScopedStep(const ScopedStep&) = delete;
    ScopedStep& operator=(const ScopedStep&) = delete;

    ScopedStep(ScopedStep&& other) noexcept
      : m_weight(other.m_weight),
        m_active(other.m_active) {
      other.m_active = false;
    }

    ScopedStep& operator=(ScopedStep&& other) noexcept {
      if (this != &other) {
        Finish();
        m_weight = other.m_weight;
        m_active = other.m_active;
        other.m_active = false;
      }
      return *this;
    }

    ~ScopedStep() {
      Finish();
    }

  private:
    void Finish() {
      if (!m_active) return;
      m_active = false;
      LoadingProgress::Advance(m_weight);
    }

    float m_weight = 0.0f;
    bool m_active = false;
  };

  static void Reset(float totalWeight, std::string phase = "Starting", std::string item = "", std::string detail = "") {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_total = (std::max)(totalWeight, 1.0f);
    s_completed = 0.0f;
    s_phase = std::move(phase);
    s_item = std::move(item);
    s_detail = std::move(detail);
    s_active = true;
    s_lastFrame = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  }

  static void Clear() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_active = false;
    s_completed = 0.0f;
    s_total = 1.0f;
    s_phase.clear();
    s_item.clear();
    s_detail.clear();
  }

  static void SetFrameCallback(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_frameCallback = std::move(callback);
  }

  static void ClearFrameCallback() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_frameCallback = nullptr;
  }

  static void SetCurrent(std::string phase, std::string item, bool pumpFrame = true) {
    SetCurrent(std::move(phase), std::move(item), std::string(), pumpFrame);
  }

  static void SetCurrent(std::string phase, std::string item, std::string detail, bool pumpFrame = true) {
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (!s_active) return;
      s_phase = std::move(phase);
      s_item = std::move(item);
      s_detail = std::move(detail);
    }
    if (pumpFrame) RequestFrame();
  }

  static void SetDetail(std::string detail, bool pumpFrame = true) {
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (!s_active) return;
      s_detail = std::move(detail);
    }
    if (pumpFrame) RequestFrame();
  }

  static void Advance(float weight, bool pumpFrame = true) {
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (!s_active) return;
      s_completed = std::clamp(s_completed + (std::max)(weight, 0.0f), 0.0f, s_total);
    }
    if (pumpFrame) RequestFrame();
  }

  static void Complete(std::string phase = "Ready", std::string item = "") {
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (!s_active) return;
      s_completed = s_total;
      s_phase = std::move(phase);
      s_item = std::move(item);
      s_detail.clear();
    }
    RequestFrame(true);
  }

  static Snapshot GetSnapshot() {
    std::lock_guard<std::mutex> lock(s_mutex);
    Snapshot snapshot;
    snapshot.phase = s_phase;
    snapshot.item = s_item;
    snapshot.detail = s_detail;
    snapshot.completed = s_completed;
    snapshot.total = (std::max)(s_total, 1.0f);
    snapshot.percent = std::clamp((snapshot.completed / snapshot.total) * 100.0f, 0.0f, 100.0f);
    snapshot.active = s_active;
    return snapshot;
  }

  static bool IsActive() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_active;
  }

  static void RequestFrame(bool force = false) {
    FrameCallback callback;
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      callback = s_frameCallback;
      if (!callback) return;
      const auto now = std::chrono::steady_clock::now();
      if (!force && now - s_lastFrame < std::chrono::milliseconds(33)) return;
      s_lastFrame = now;
    }

    bool expected = false;
    if (!s_pumping.compare_exchange_strong(expected, true)) return;
    callback();
    s_pumping.store(false);
  }

private:
  static bool BeginStep(std::string phase, std::string item, bool pumpFrame) {
    {
      std::lock_guard<std::mutex> lock(s_mutex);
      if (!s_active) return false;
      s_phase = std::move(phase);
      s_item = std::move(item);
      s_detail.clear();
    }
    if (pumpFrame) RequestFrame();
    return true;
  }

  inline static std::mutex s_mutex;
  inline static FrameCallback s_frameCallback;
  inline static std::atomic_bool s_pumping{false};
  inline static bool s_active = false;
  inline static float s_completed = 0.0f;
  inline static float s_total = 1.0f;
  inline static std::chrono::steady_clock::time_point s_lastFrame = std::chrono::steady_clock::now();
  inline static std::string s_phase;
  inline static std::string s_item;
  inline static std::string s_detail;
};

} // namespace t850
