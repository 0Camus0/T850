#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace t850 {

class Config;

class RuntimeTelemetry {
public:
  class ScopedTimer {
  public:
    explicit ScopedTimer(const char* name);
    explicit ScopedTimer(std::string name);
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ~ScopedTimer();

  private:
    std::string m_name;
    std::chrono::steady_clock::time_point m_start;
    bool m_active = false;
  };

  static void InitializeFromConfig(const Config& cfg);
  static void Shutdown();

  static bool IsEnabled();
  static bool IsFrameActive();

  static void BeginFrame(uint64_t frameIndex, double deltaSeconds);
  static void EndFrame();
  static void RecordScope(const std::string& name, double elapsedMs);
  static void AddCounter(const char* name, double value);
  static void SetCounter(const char* name, double value);
};

} // namespace t850

#define T8_TELEMETRY_JOIN_IMPL(a, b) a##b
#define T8_TELEMETRY_JOIN(a, b) T8_TELEMETRY_JOIN_IMPL(a, b)
#define T8_TELEMETRY_SCOPE(name) ::t850::RuntimeTelemetry::ScopedTimer T8_TELEMETRY_JOIN(_t8TelemetryScope_, __LINE__)(name)
