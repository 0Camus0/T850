#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <array>
#include <vector>

#ifndef T850_ENABLE_PROFILING
#define T850_ENABLE_PROFILING 1
#endif

namespace t850 {

class Config;

class RuntimeTelemetry {
public:
  using ScopeId = uint16_t;
  static constexpr ScopeId InvalidId = UINT16_MAX;
  static constexpr size_t MaxMetrics = 512;
  enum class UploadResource : uint8_t { Vertex, Index, Uniform, Texture, Count };
  enum class UploadSource : uint8_t { PerFrame, Streaming, AssetLoad, Count };
  struct UploadStats {
    uint64_t logicalBytes = 0;
    uint64_t stagingBytes = 0;
    uint64_t calls = 0;
    uint64_t nanoseconds = 0;
    uint64_t reallocations = 0;
    uint64_t largestBytes = 0;
  };
  struct ScopeStats { double totalMs = 0; double maxMs = 0; uint64_t count = 0; bool accumulatedWork = false; };
  struct FrameSample {
    uint64_t frameIndex = 0;
    double timeMs = 0;
    double deltaMs = 0;
    double cpuFrameMs = 0;
    uint64_t latePublications = 0;
    bool uploadBudgetExceeded = false;
    bool detailed = true;
    bool complete = false;
    std::vector<std::pair<std::string, ScopeStats>> scopes;
    std::vector<std::pair<std::string, double>> counters;
    std::array<UploadStats, 12> uploads{};
  };
  static ScopeId RegisterScope(std::string_view name);
  static ScopeId RegisterCounter(std::string_view name);
  static std::string ScopeName(ScopeId id);
  static void RecordScope(ScopeId id, double elapsedMs, uint64_t token = 0);
  static void AddCounter(ScopeId id, double value);
  static void SetCounter(ScopeId id, double value);
  static void PublishThread();
  static std::vector<FrameSample> Snapshot();
  static uint64_t DroppedRecords();
  static uint64_t SlowPathEntries();
  static void RecordStaging(UploadResource resource, uint64_t bytes, uint64_t reallocations = 0);
  static void RecordActiveStaging(uint64_t bytes, uint64_t reallocations = 0);
  static UploadSource CurrentUploadSource();
  static uint64_t TextureUploadBytes(unsigned width, unsigned height, unsigned levels, unsigned layers, unsigned bytesPerPixel);
  static void RecordRingOverflow();
  static void RecordDraw(uint32_t indices);
  class DrawCounterScope {
  public:
    DrawCounterScope(ScopeId draws, ScopeId indices);
    ~DrawCounterScope();
    DrawCounterScope(const DrawCounterScope&) = delete;
    DrawCounterScope& operator=(const DrawCounterScope&) = delete;
  private:
    ScopeId m_previousDraws;
    ScopeId m_previousIndices;
  };
  class ScopedTimer {
  public:
    explicit ScopedTimer(ScopeId id, bool accumulatedWork = false);
    explicit ScopedTimer(const char* name);
    explicit ScopedTimer(std::string name);
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ~ScopedTimer();

  private:
    ScopeId m_id = InvalidId;
    uint64_t m_token = 0;
    std::chrono::steady_clock::time_point m_start;
    bool m_active = false;
    bool m_accumulatedWork = false;
  };

  class UploadSourceGuard {
  public:
    explicit UploadSourceGuard(UploadSource source);
    ~UploadSourceGuard();
    UploadSourceGuard(const UploadSourceGuard&) = delete;
    UploadSourceGuard& operator=(const UploadSourceGuard&) = delete;
  private:
    UploadSource m_previous;
  };
  class UploadGuard {
  public:
    UploadGuard(UploadResource resource, uint64_t logicalBytes, uint64_t stagingBytes = 0);
    ~UploadGuard();
    UploadGuard(const UploadGuard&) = delete;
    UploadGuard& operator=(const UploadGuard&) = delete;
    void AddStaging(uint64_t bytes) { m_stats.stagingBytes += bytes; }
    void Reallocated(uint64_t count = 1) { m_stats.reallocations += count; }
  private:
    uint64_t m_token = 0;
    size_t m_slot = 0;
    UploadStats m_stats;
    std::chrono::steady_clock::time_point m_start;
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
#if T850_ENABLE_PROFILING
#define T8_TELEMETRY_CALL(name, ...) ([&]() -> decltype(auto) { T8_TELEMETRY_SCOPE(name); return (__VA_ARGS__); }())
#define T8_TELEMETRY_SCOPE(name) \
  static const auto T8_TELEMETRY_JOIN(_t8TelemetryId_, __LINE__) = ::t850::RuntimeTelemetry::RegisterScope(name); \
  ::t850::RuntimeTelemetry::ScopedTimer T8_TELEMETRY_JOIN(_t8TelemetryScope_, __LINE__)(T8_TELEMETRY_JOIN(_t8TelemetryId_, __LINE__))
#define T8_TELEMETRY_ADD(name, value) do { \
  if (::t850::RuntimeTelemetry::IsFrameActive()) { \
    static const auto T8_TELEMETRY_JOIN(_t8Counter_, __LINE__) = ::t850::RuntimeTelemetry::RegisterCounter(name); \
    ::t850::RuntimeTelemetry::AddCounter(T8_TELEMETRY_JOIN(_t8Counter_, __LINE__), value); \
  } } while (false)
#define T8_TELEMETRY_SET(name, value) do { \
  if (::t850::RuntimeTelemetry::IsFrameActive()) { \
    static const auto T8_TELEMETRY_JOIN(_t8Counter_, __LINE__) = ::t850::RuntimeTelemetry::RegisterCounter(name); \
    ::t850::RuntimeTelemetry::SetCounter(T8_TELEMETRY_JOIN(_t8Counter_, __LINE__), value); \
  } } while (false)
#define T8_UPLOAD_SCOPE(resource, bytes, staging) \
  ::t850::RuntimeTelemetry::UploadGuard T8_TELEMETRY_JOIN(_t8Upload_, __LINE__)(resource, bytes, staging)
#define T8_UPLOAD_SOURCE(source) ::t850::RuntimeTelemetry::UploadSourceGuard T8_TELEMETRY_JOIN(_t8Source_, __LINE__)(source)
#define T8_TELEMETRY_HANDLE(id) ::t850::RuntimeTelemetry::ScopedTimer T8_TELEMETRY_JOIN(_t8Handle_, __LINE__)(id)
#define T8_CPU_WORK(name) \
  static const auto T8_TELEMETRY_JOIN(_t8WorkId_, __LINE__) = ::t850::RuntimeTelemetry::RegisterScope(name); \
  ::t850::RuntimeTelemetry::ScopedTimer T8_TELEMETRY_JOIN(_t8Work_, __LINE__)(T8_TELEMETRY_JOIN(_t8WorkId_, __LINE__), true)
#define T8_DRAW_WORK(name) \
  static const auto T8_TELEMETRY_JOIN(_t8Draws_, __LINE__) = ::t850::RuntimeTelemetry::RegisterCounter(name ".draws"); \
  static const auto T8_TELEMETRY_JOIN(_t8Indices_, __LINE__) = ::t850::RuntimeTelemetry::RegisterCounter(name ".indices"); \
  ::t850::RuntimeTelemetry::DrawCounterScope T8_TELEMETRY_JOIN(_t8DrawScope_, __LINE__)(T8_TELEMETRY_JOIN(_t8Draws_, __LINE__), T8_TELEMETRY_JOIN(_t8Indices_, __LINE__))
#else
#define T8_TELEMETRY_CALL(name, ...) (__VA_ARGS__)
#define T8_TELEMETRY_SCOPE(name) ((void)0)
#define T8_TELEMETRY_ADD(name, value) ((void)0)
#define T8_TELEMETRY_SET(name, value) ((void)0)
#define T8_UPLOAD_SCOPE(resource, bytes, staging) ((void)0)
#define T8_UPLOAD_SOURCE(source) ((void)0)
#define T8_TELEMETRY_HANDLE(id) ((void)0)
#define T8_CPU_WORK(name) ((void)0)
#define T8_DRAW_WORK(name) ((void)0)
#endif
