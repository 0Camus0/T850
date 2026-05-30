#include <pch.h>

#include <debug/RuntimeTelemetry.h>

#include <core/Config.h>
#include <utils/Log.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <ctime>
#include <unordered_map>
#include <vector>

namespace t850 {

namespace {

struct ScopeStats {
  double totalMs = 0.0;
  double maxMs = 0.0;
  uint32_t count = 0;
};

struct FrameSample {
  uint64_t frameIndex = 0;
  double timeMs = 0.0;
  double deltaMs = 0.0;
  std::vector<std::pair<std::string, ScopeStats>> scopes;
  std::vector<std::pair<std::string, double>> counters;
};

struct TelemetryState {
  std::mutex mutex;
  bool initialized = false;
  bool enabled = false;
  bool frameActive = false;
  std::atomic_bool enabledAtomic{false};
  std::atomic_bool frameActiveAtomic{false};
  int frequencyFrames = 60;
  std::string outputPath = "logs/perf_telemetry.json";
  std::string outputTimestamp;
  std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
  uint64_t currentFrame = 0;
  double currentDeltaMs = 0.0;
  std::unordered_map<std::string, ScopeStats> frameScopes;
  std::unordered_map<std::string, double> frameCounters;
  std::vector<FrameSample> samples;
};

TelemetryState g_state;

std::string MakeTimestampString() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
  std::tm localTime = {};
#if defined(_WIN32)
  localtime_s(&localTime, &nowTime);
#else
  localtime_r(&nowTime, &localTime);
#endif

  char buffer[32] = {};
  if (std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &localTime) > 0) {
    return buffer;
  }

  const auto ticks = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  return std::to_string(ticks);
}

std::string JsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          std::ostringstream ss;
          ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c));
          out += ss.str();
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

template <typename T>
void SortByName(std::vector<std::pair<std::string, T>>& values) {
  std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
    return a.first < b.first;
  });
}

std::filesystem::path MakeTimestampedOutputPath(const std::filesystem::path& requestedPath,
                                                const std::string& timestamp) {
  const std::filesystem::path parent = requestedPath.parent_path();
  const std::string stem = requestedPath.stem().empty()
    ? requestedPath.filename().string()
    : requestedPath.stem().string();
  const std::string extension = requestedPath.extension().string();
  const std::string timestampedName = stem + "_" + timestamp + extension;
  return parent.empty() ? std::filesystem::path(timestampedName) : parent / timestampedName;
}

void DumpLocked() {
  if (!g_state.enabled || g_state.samples.empty()) return;

  const std::filesystem::path requestedOutputPath = g_state.outputPath.empty()
    ? std::filesystem::path("logs/perf_telemetry.json")
    : std::filesystem::path(g_state.outputPath);
  const std::filesystem::path outputPath =
    MakeTimestampedOutputPath(requestedOutputPath,
                              g_state.outputTimestamp.empty() ? MakeTimestampString() : g_state.outputTimestamp);

  const std::filesystem::path parent = outputPath.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      T8_LOG_ERROR("[RuntimeTelemetry] Failed to create output directory '%s': %s",
                   parent.string().c_str(), ec.message().c_str());
      return;
    }
  }

  std::ofstream out(outputPath, std::ios::out | std::ios::trunc);
  if (!out) {
    T8_LOG_ERROR("[RuntimeTelemetry] Failed to open telemetry output '%s'", outputPath.string().c_str());
    return;
  }

  out << std::fixed << std::setprecision(4);
  out << "{\n";
  out << "  \"version\": 1,\n";
  out << "  \"frequencyFrames\": " << g_state.frequencyFrames << ",\n";
  out << "  \"requestedOutputPath\": \"" << JsonEscape(requestedOutputPath.string()) << "\",\n";
  out << "  \"outputPath\": \"" << JsonEscape(outputPath.string()) << "\",\n";
  out << "  \"sampleCount\": " << g_state.samples.size() << ",\n";
  out << "  \"frames\": [\n";
  for (std::size_t i = 0; i < g_state.samples.size(); ++i) {
    const FrameSample& frame = g_state.samples[i];
    out << "    {\n";
    out << "      \"frame\": " << frame.frameIndex << ",\n";
    out << "      \"timeMs\": " << frame.timeMs << ",\n";
    out << "      \"deltaMs\": " << frame.deltaMs << ",\n";
    out << "      \"scopes\": [\n";
    for (std::size_t s = 0; s < frame.scopes.size(); ++s) {
      const auto& scope = frame.scopes[s];
      out << "        {\"name\": \"" << JsonEscape(scope.first) << "\", \"totalMs\": " << scope.second.totalMs
          << ", \"count\": " << scope.second.count << ", \"maxMs\": " << scope.second.maxMs << "}";
      out << (s + 1 < frame.scopes.size() ? ",\n" : "\n");
    }
    out << "      ],\n";
    out << "      \"counters\": [\n";
    for (std::size_t c = 0; c < frame.counters.size(); ++c) {
      const auto& counter = frame.counters[c];
      out << "        {\"name\": \"" << JsonEscape(counter.first) << "\", \"value\": " << counter.second << "}";
      out << (c + 1 < frame.counters.size() ? ",\n" : "\n");
    }
    out << "      ]\n";
    out << "    }" << (i + 1 < g_state.samples.size() ? "," : "") << "\n";
  }
  out << "  ]\n";
  out << "}\n";

  T8_LOG_INFO("[RuntimeTelemetry] Wrote %zu sampled frames to '%s'",
              g_state.samples.size(), outputPath.string().c_str());
}

} // namespace

RuntimeTelemetry::ScopedTimer::ScopedTimer(const char* name)
  : ScopedTimer(std::string(name ? name : "")) {
}

RuntimeTelemetry::ScopedTimer::ScopedTimer(std::string name) {
  if (name.empty() || !RuntimeTelemetry::IsFrameActive()) return;
  m_name = std::move(name);
  m_start = std::chrono::steady_clock::now();
  m_active = true;
}

RuntimeTelemetry::ScopedTimer::~ScopedTimer() {
  if (!m_active) return;
  const auto end = std::chrono::steady_clock::now();
  const double elapsedMs = std::chrono::duration<double, std::milli>(end - m_start).count();
  RuntimeTelemetry::RecordScope(m_name, elapsedMs);
}

void RuntimeTelemetry::InitializeFromConfig(const Config& cfg) {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  g_state.initialized = true;
  g_state.enabled = cfg.flags.runtimeTelemetry;
  g_state.frameActive = false;
  g_state.enabledAtomic.store(g_state.enabled, std::memory_order_relaxed);
  g_state.frameActiveAtomic.store(false, std::memory_order_relaxed);
  g_state.frequencyFrames = (std::max)(cfg.runtimeTelemetryFrequencyFrames, 0);
  g_state.outputPath = cfg.runtimeTelemetryOutputPath.empty()
    ? std::string("logs/perf_telemetry.json")
    : cfg.runtimeTelemetryOutputPath;
  g_state.outputTimestamp = MakeTimestampString();
  g_state.startTime = std::chrono::steady_clock::now();
  g_state.currentFrame = 0;
  g_state.currentDeltaMs = 0.0;
  g_state.frameScopes.clear();
  g_state.frameCounters.clear();
  g_state.samples.clear();

  if (g_state.enabled) {
    T8_LOG_INFO("[RuntimeTelemetry] Enabled: frequencyFrames=%d output='%s'",
                g_state.frequencyFrames, g_state.outputPath.c_str());
  }
}

void RuntimeTelemetry::Shutdown() {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  if (!g_state.initialized) return;
  if (g_state.frameActive) {
    FrameSample sample;
    sample.frameIndex = g_state.currentFrame;
    sample.deltaMs = g_state.currentDeltaMs;
    sample.timeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_state.startTime).count();
    sample.scopes.assign(g_state.frameScopes.begin(), g_state.frameScopes.end());
    sample.counters.assign(g_state.frameCounters.begin(), g_state.frameCounters.end());
    SortByName(sample.scopes);
    SortByName(sample.counters);
    g_state.samples.push_back(std::move(sample));
    g_state.frameScopes.clear();
    g_state.frameCounters.clear();
    g_state.frameActive = false;
    g_state.frameActiveAtomic.store(false, std::memory_order_relaxed);
  }
  DumpLocked();
  g_state.initialized = false;
  g_state.enabled = false;
  g_state.enabledAtomic.store(false, std::memory_order_relaxed);
  g_state.frameActiveAtomic.store(false, std::memory_order_relaxed);
}

bool RuntimeTelemetry::IsEnabled() {
  return g_state.enabledAtomic.load(std::memory_order_relaxed);
}

bool RuntimeTelemetry::IsFrameActive() {
  return g_state.frameActiveAtomic.load(std::memory_order_relaxed);
}

void RuntimeTelemetry::BeginFrame(uint64_t frameIndex, double deltaSeconds) {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  if (!g_state.enabled) return;

  g_state.frameScopes.clear();
  g_state.frameCounters.clear();
  g_state.currentFrame = frameIndex;
  g_state.currentDeltaMs = deltaSeconds * 1000.0;
  g_state.frameActive =
    g_state.frequencyFrames == 0 ||
    (g_state.frequencyFrames > 0 && (frameIndex % static_cast<uint64_t>(g_state.frequencyFrames)) == 0);
  g_state.frameActiveAtomic.store(g_state.frameActive, std::memory_order_relaxed);
}

void RuntimeTelemetry::EndFrame() {
  std::lock_guard<std::mutex> lock(g_state.mutex);
  if (!g_state.enabled || !g_state.frameActive) return;

  FrameSample sample;
  sample.frameIndex = g_state.currentFrame;
  sample.deltaMs = g_state.currentDeltaMs;
  sample.timeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_state.startTime).count();
  sample.scopes.assign(g_state.frameScopes.begin(), g_state.frameScopes.end());
  sample.counters.assign(g_state.frameCounters.begin(), g_state.frameCounters.end());
  SortByName(sample.scopes);
  SortByName(sample.counters);
  g_state.samples.push_back(std::move(sample));
  g_state.frameScopes.clear();
  g_state.frameCounters.clear();
  g_state.frameActive = false;
  g_state.frameActiveAtomic.store(false, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordScope(const std::string& name, double elapsedMs) {
  if (name.empty()) return;
  std::lock_guard<std::mutex> lock(g_state.mutex);
  if (!g_state.enabled || !g_state.frameActive) return;
  ScopeStats& stats = g_state.frameScopes[name];
  stats.totalMs += elapsedMs;
  stats.maxMs = (std::max)(stats.maxMs, elapsedMs);
  ++stats.count;
}

void RuntimeTelemetry::AddCounter(const char* name, double value) {
  if (!name || !*name) return;
  std::lock_guard<std::mutex> lock(g_state.mutex);
  if (!g_state.enabled || !g_state.frameActive) return;
  g_state.frameCounters[name] += value;
}

void RuntimeTelemetry::SetCounter(const char* name, double value) {
  if (!name || !*name) return;
  std::lock_guard<std::mutex> lock(g_state.mutex);
  if (!g_state.enabled || !g_state.frameActive) return;
  g_state.frameCounters[name] = value;
}

} // namespace t850
