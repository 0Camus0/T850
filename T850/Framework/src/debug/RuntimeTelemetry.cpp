#include <pch.h>

#include <debug/RuntimeTelemetry.h>

#include <core/Config.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <map>
#include <optional>
#include <sstream>
#include <ctime>
#include <unordered_map>
#include <vector>
#include <memory>
#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace t850 {

namespace {

using ScopeStats = RuntimeTelemetry::ScopeStats;
using FrameSample = RuntimeTelemetry::FrameSample;
using UploadStats = RuntimeTelemetry::UploadStats;
enum BufferState : unsigned { Free, Writing, Published, Reading };
struct Metric {
  ScopeStats scope;
  double counter = 0;
  bool hasCounter = false;
  bool setCounter = false;
};
struct ThreadBuffer {
  std::atomic<unsigned> state{Free};
  uint64_t token = 0;
  uint64_t ringOverflows = 0;
  std::array<Metric, RuntimeTelemetry::MaxMetrics> metrics{};
  std::array<RuntimeTelemetry::UploadStats, 12> uploads{};
};
struct ThreadState {
  std::array<ThreadBuffer, 8> buffers;
  ThreadBuffer* current = nullptr;
};
struct RegistryEntry { std::string name; bool counter; };

struct TelemetryState {
  std::mutex mutex;
  bool initialized = false;
  bool enabled = false;
  bool frameActive = false;
  std::atomic_bool enabledAtomic{false};
  std::atomic_bool frameActiveAtomic{false};
  std::atomic<uint64_t> activeToken{0};
  std::atomic<uint64_t> uploadToken{0};
  std::atomic<uint64_t> firstToken{0};
  std::atomic<uint64_t> session{0};
  std::atomic<uint64_t> dropped{0};
  std::atomic<uint64_t> slowPaths{0};
  uint64_t nextToken = 0;
  uint64_t uploadBudgetBytes = 0;
  std::thread::id frameThread;
  std::vector<RegistryEntry> registry;
  std::unordered_map<std::string, RuntimeTelemetry::ScopeId> names;
  std::vector<std::shared_ptr<ThreadState>> threads;
  std::unordered_map<uint64_t, size_t> frameSlots;
  std::map<uint64_t, FrameSample> budgetFrames;
  int frequencyFrames = 60;
  std::string outputPath = "logs/perf_telemetry.json";
  std::string outputTimestamp;
  std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
  uint64_t currentFrame = 0;
  double currentDeltaMs = 0.0;
  std::optional<double> firstRuntimeFrameStartMs;
  std::optional<double> firstRuntimeFrameCompleteMs;
  std::vector<FrameSample> samples;
};

TelemetryState g_state;

thread_local RuntimeTelemetry::UploadSource g_uploadSource = RuntimeTelemetry::UploadSource::PerFrame;
thread_local bool g_uploadActive = false;
thread_local uint64_t g_uploadIssueToken = 0;
thread_local size_t g_uploadSlot = 0;
thread_local RuntimeTelemetry::ScopeId g_passDraws = RuntimeTelemetry::InvalidId;
thread_local RuntimeTelemetry::ScopeId g_passIndices = RuntimeTelemetry::InvalidId;
struct LocalState {
  std::shared_ptr<ThreadState> thread;
  uint64_t session = 0;
  unsigned scopeDepth = 0;
  bool frameThread = false;
  void Publish() {
    if (thread && thread->current) {
      thread->current->state.store(Published, std::memory_order_release);
      thread->current = nullptr;
    }
  }
  ~LocalState() { Publish(); }
};
thread_local LocalState g_local;

ThreadBuffer* BufferFor(uint64_t token) {
  if (!token || !g_state.enabledAtomic.load(std::memory_order_acquire) ||
      token < g_state.firstToken.load(std::memory_order_acquire)) return nullptr;
  const auto session = g_state.session.load(std::memory_order_acquire);
  if (!g_local.thread || g_local.session != session) {
    g_state.slowPaths.fetch_add(1, std::memory_order_relaxed);
    g_local.Publish();
    auto thread = std::make_shared<ThreadState>();
    std::lock_guard lock(g_state.mutex);
    if (session != g_state.session.load() || g_state.threads.size() >= 128) {
      g_state.dropped.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }
    g_state.threads.push_back(thread);
    g_local.thread = std::move(thread);
    g_local.session = session;
    g_local.frameThread = std::this_thread::get_id() == g_state.frameThread;
  }
  if (g_local.thread->current && g_local.thread->current->token != token) g_local.Publish();
  if (g_local.thread->current) return g_local.thread->current;
  for (auto& buffer : g_local.thread->buffers) {
    unsigned expected = Free;
    if (!buffer.state.compare_exchange_strong(expected, Writing, std::memory_order_acquire)) continue;
    buffer.token = token;
    buffer.ringOverflows = 0;
    buffer.metrics.fill({});
    buffer.uploads.fill({});
    g_local.thread->current = &buffer;
    return &buffer;
  }
  g_state.dropped.fetch_add(1, std::memory_order_relaxed);
  return nullptr;
}

void MergeUpload(RuntimeTelemetry::UploadStats& total, const RuntimeTelemetry::UploadStats& value) {
  total.logicalBytes += value.logicalBytes;
  total.stagingBytes += value.stagingBytes;
  total.calls += value.calls;
  total.nanoseconds += value.nanoseconds;
  total.reallocations += value.reallocations;
  total.largestBytes = (std::max)(total.largestBytes, value.largestBytes);
}

FrameSample* FindFrame(uint64_t token) {
  if (const auto found = g_state.frameSlots.find(token); found != g_state.frameSlots.end())
    return &g_state.samples[found->second];
  if (const auto found = g_state.budgetFrames.find(token); found != g_state.budgetFrames.end())
    return &found->second;
  return nullptr;
}

void DrainLocked() {
  for (auto& thread : g_state.threads) {
    for (auto& buffer : thread->buffers) {
      unsigned expected = Published;
      if (!buffer.state.compare_exchange_strong(expected, Reading, std::memory_order_acquire)) continue;
      auto* destination = FindFrame(buffer.token);
      if (!destination) g_state.dropped.fetch_add(1, std::memory_order_relaxed);
      else {
        auto& frame = *destination;
        if (frame.complete) ++frame.latePublications;
        for (size_t index = 0; index < g_state.registry.size(); ++index) {
          const auto& entry = g_state.registry[index];
          const auto& metric = buffer.metrics[index];
          if (entry.counter && metric.hasCounter) {
            auto foundCounter = std::find_if(frame.counters.begin(), frame.counters.end(), [&](const auto& item) { return item.first == entry.name; });
            if (foundCounter == frame.counters.end()) frame.counters.emplace_back(entry.name, metric.counter);
            else if (metric.setCounter || !std::isfinite(foundCounter->second)) foundCounter->second = metric.counter;
            else foundCounter->second += metric.counter;
          } else if (!entry.counter && metric.scope.count) {
            auto foundScope = std::find_if(frame.scopes.begin(), frame.scopes.end(), [&](const auto& item) { return item.first == entry.name; });
            if (foundScope == frame.scopes.end()) frame.scopes.emplace_back(entry.name, metric.scope);
            else {
              foundScope->second.totalMs += metric.scope.totalMs;
              foundScope->second.maxMs = (std::max)(foundScope->second.maxMs, metric.scope.maxMs);
              foundScope->second.count += metric.scope.count;
              foundScope->second.accumulatedWork = foundScope->second.accumulatedWork || metric.scope.accumulatedWork;
            }
          }
        }
        for (size_t slot = 0; slot < buffer.uploads.size(); ++slot) MergeUpload(frame.uploads[slot], buffer.uploads[slot]);
        if (buffer.ringOverflows) {
          const auto counter = std::find_if(frame.counters.begin(), frame.counters.end(), [](const auto& item) {
            return item.first == "gpu.ring.overflows";
          });
          if (counter == frame.counters.end()) frame.counters.emplace_back("gpu.ring.overflows", static_cast<double>(buffer.ringOverflows));
          else counter->second = (std::isfinite(counter->second) ? counter->second : 0) + buffer.ringOverflows;
        }
        uint64_t dynamicBytes = 0;
        size_t dominant = 0;
        for (size_t slot = 0; slot < frame.uploads.size(); ++slot) {
          if (slot % 3 == 2) continue;
          dynamicBytes += frame.uploads[slot].logicalBytes;
          if (frame.uploads[slot].logicalBytes > frame.uploads[dominant].logicalBytes) dominant = slot;
        }
        const bool overflow = std::any_of(frame.counters.begin(), frame.counters.end(), [](const auto& counter) {
          return counter.first == "gpu.ring.overflows" && counter.second > 0;
        });
        if (!frame.uploadBudgetExceeded && ((g_state.uploadBudgetBytes && dynamicBytes > g_state.uploadBudgetBytes) || overflow)) {
          frame.uploadBudgetExceeded = true;
          constexpr const char* resources[] = {"Vertex", "Index", "Uniform", "Texture"};
          constexpr const char* sources[] = {"PerFrame", "Streaming", "AssetLoad"};
          T8_LOG_INFO("[UploadBudget] WARNING frame=%llu bytes=%llu dominant=%s/%s ringOverflow=%d",
            static_cast<unsigned long long>(frame.frameIndex), static_cast<unsigned long long>(dynamicBytes),
            resources[dominant / 3], sources[dominant % 3], overflow);
        }
        if (!frame.detailed && frame.uploadBudgetExceeded && !g_state.frameSlots.contains(buffer.token)) {
          if (g_state.samples.size() < 8192) {
            g_state.frameSlots.emplace(buffer.token, g_state.samples.size());
            g_state.samples.push_back(std::move(frame));
            g_state.budgetFrames.erase(buffer.token);
          } else g_state.dropped.fetch_add(1, std::memory_order_relaxed);
        }
      }
      buffer.state.store(Free, std::memory_order_release);
    }
  }
}

RuntimeTelemetry::ScopeId Register(std::string_view name, bool counter) {
  if (name.empty() || !T850_ENABLE_PROFILING) return RuntimeTelemetry::InvalidId;
  g_state.slowPaths.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard lock(g_state.mutex);
  const std::string key = std::string(counter ? "c:" : "s:") + std::string(name);
  if (const auto found = g_state.names.find(key); found != g_state.names.end()) return found->second;
  if (g_state.registry.size() >= RuntimeTelemetry::MaxMetrics) {
    g_state.dropped.fetch_add(1, std::memory_order_relaxed);
    return RuntimeTelemetry::InvalidId;
  }
  const auto id = static_cast<RuntimeTelemetry::ScopeId>(g_state.registry.size());
  g_state.registry.push_back({std::string(name), counter});
  g_state.names.emplace(key, id);
  return id;
}

void StartSampleLocked(uint64_t frameIndex, double deltaMs, bool detailed = true) {
  if (detailed && g_state.samples.size() == 8192) {
    g_state.dropped.fetch_add(1, std::memory_order_relaxed);
    g_state.frameActive = false;
    detailed = false;
  }
  FrameSample sample;
  sample.frameIndex = frameIndex;
  sample.deltaMs = deltaMs;
  sample.detailed = detailed;
  for (const char* name : {"gpu.ring.peak_bytes", "gpu.ring.capacity_bytes", "gpu.ring.overflows",
                           "gpu.pool.hits", "gpu.pool.misses", "gpu.pool.evictions", "gpu.present", "gpu.submit", "gpu.gpu_wait"})
    sample.counters.emplace_back(name, std::numeric_limits<double>::quiet_NaN());
  sample.timeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_state.startTime).count();
  const auto token = ++g_state.nextToken;
  if (detailed) {
    g_state.frameSlots.emplace(token, g_state.samples.size());
    g_state.samples.push_back(std::move(sample));
  } else {
    if (g_state.budgetFrames.size() >= 64) g_state.budgetFrames.erase(g_state.budgetFrames.begin());
    g_state.budgetFrames.emplace(token, std::move(sample));
  }
  g_state.uploadToken.store(token, std::memory_order_release);
  g_state.activeToken.store(detailed ? token : 0, std::memory_order_release);
  g_state.frameActiveAtomic.store(detailed, std::memory_order_release);
}

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
  out << "  \"version\": 2,\n";
  if (g_pBaseDriver) {
    out << "  \"api\":\"" << g_pBaseDriver->ApiTag() << "\",\n"
        << "  \"provider\":\"" << g_pBaseDriver->ProviderTag() << "\",\n"
        << "  \"backend\":\"" << g_pBaseDriver->UnderlyingBackendTag() << "\",\n"
        << "  \"adapterId\":\"" << g_pBaseDriver->ProfilingAdapterId() << "\",\n";
  }
  out << "  \"shaderFlow\":\"" << JsonEscape(g_config.webgpuShaderFlow) << "\",\n"
      << "  \"gpuTiming\":null,\n  \"scopeTiming\":\"inclusive CPU wall time\",\n";
  out << "  \"startupTiming\":{\"epoch\":\"telemetry initialization\",\"firstRuntimeFrameStartMs\":";
  if (g_state.firstRuntimeFrameStartMs) out << *g_state.firstRuntimeFrameStartMs; else out << "null";
  out << ",\"firstRuntimeFrameCompleteMs\":";
  if (g_state.firstRuntimeFrameCompleteMs) out << *g_state.firstRuntimeFrameCompleteMs; else out << "null";
  out << "},\n";
  out << "  \"droppedRecords\": " << g_state.dropped.load() << ",\n";
  unsigned unfinishedWriters = 0;
  for (const auto& thread : g_state.threads)
    for (const auto& buffer : thread->buffers)
      if (buffer.state.load(std::memory_order_acquire) != Free) ++unfinishedWriters;
  out << "  \"unfinishedWriters\": " << unfinishedWriters << ",\n";
  out << "  \"frequencyFrames\": " << g_state.frequencyFrames << ",\n";
  out << "  \"requestedOutputPath\": \"" << JsonEscape(requestedOutputPath.string()) << "\",\n";
  out << "  \"outputPath\": \"" << JsonEscape(outputPath.string()) << "\",\n";
  out << "  \"sampleCount\": " << g_state.samples.size() << ",\n";
  out << "  \"frames\": [\n";
  for (std::size_t i = 0; i < g_state.samples.size(); ++i) {
    const FrameSample& frame = g_state.samples[i];
    out << "    {\n";
    out << "      \"frame\": " << frame.frameIndex << ",\n";
    out << "      \"startup\": " << (frame.frameIndex == UINT64_MAX ? "true" : "false") << ",\n";
    out << "      \"detailed\": " << (frame.detailed ? "true" : "false") << ",\n";
    out << "      \"latePublications\": " << frame.latePublications << ",\n";
    out << "      \"uploadBudgetExceeded\": " << (frame.uploadBudgetExceeded ? "true" : "false") << ",\n";
    out << "      \"timeMs\": " << frame.timeMs << ",\n";
    out << "      \"deltaMs\": " << frame.deltaMs << ",\n";
    out << "      \"cpuFrameMs\": " << frame.cpuFrameMs << ",\n";
    out << "      \"scopes\": [\n";
    for (std::size_t s = 0; s < frame.scopes.size(); ++s) {
      const auto& scope = frame.scopes[s];
      out << "        {\"name\": \"" << JsonEscape(scope.first) << "\", \"totalMs\": " << scope.second.totalMs
          << ", \"count\": " << scope.second.count << ", \"maxMs\": " << scope.second.maxMs
          << ", \"accumulatedWork\": " << (scope.second.accumulatedWork ? "true" : "false") << "}";
      out << (s + 1 < frame.scopes.size() ? ",\n" : "\n");
    }
    out << "      ],\n";
    out << "      \"counters\": [\n";
    for (std::size_t c = 0; c < frame.counters.size(); ++c) {
      const auto& counter = frame.counters[c];
      out << "        {\"name\": \"" << JsonEscape(counter.first) << "\", \"value\": ";
      if (std::isfinite(counter.second)) out << counter.second; else out << "null";
      out << "}";
      out << (c + 1 < frame.counters.size() ? ",\n" : "\n");
    }
    out << "      ],\n      \"uploads\": [\n";
    constexpr const char* resources[] = {"Vertex", "Index", "Uniform", "Texture"};
    constexpr const char* sources[] = {"PerFrame", "Streaming", "AssetLoad"};
    for (size_t slot = 0; slot < frame.uploads.size(); ++slot) {
      const auto& upload = frame.uploads[slot];
      out << "        {\"resource\":\"" << resources[slot / 3] << "\",\"source\":\"" << sources[slot % 3]
          << "\",\"logicalBytes\":" << upload.logicalBytes << ",\"stagingBytes\":" << upload.stagingBytes
          << ",\"calls\":" << upload.calls << ",\"nanoseconds\":" << upload.nanoseconds
          << ",\"reallocations\":" << upload.reallocations << ",\"largestBytes\":" << upload.largestBytes << "}";
      out << (slot + 1 < frame.uploads.size() ? ",\n" : "\n");
    }
    out << "      ],\n      \"uploadTotals\": [\n";
    const auto writeTotal = [&](const char* axis, const char* name, const UploadStats& total, bool comma) {
      out << "        {\"axis\":\"" << axis << "\",\"name\":\"" << name << "\",\"logicalBytes\":" << total.logicalBytes
          << ",\"stagingBytes\":" << total.stagingBytes << ",\"calls\":" << total.calls << ",\"nanoseconds\":" << total.nanoseconds
          << ",\"reallocations\":" << total.reallocations << ",\"largestBytes\":" << total.largestBytes << "}" << (comma ? ",\n" : "\n");
    };
    for (size_t resource = 0; resource < 4; ++resource) {
      UploadStats total;
      for (size_t source = 0; source < 3; ++source) MergeUpload(total, frame.uploads[resource * 3 + source]);
      writeTotal("resource", resources[resource], total, true);
    }
    for (size_t source = 0; source < 3; ++source) {
      UploadStats total;
      for (size_t resource = 0; resource < 4; ++resource) MergeUpload(total, frame.uploads[resource * 3 + source]);
      writeTotal("source", sources[source], total, source != 2);
    }
    out << "      ]\n";
    out << "    }" << (i + 1 < g_state.samples.size() ? "," : "") << "\n";
  }
  out << "  ]\n";
  out << "}\n";
  out.close();
#ifdef __EMSCRIPTEN__
  std::ifstream reportFile(outputPath);
  const std::string report((std::istreambuf_iterator<char>(reportFile)), std::istreambuf_iterator<char>());
  MAIN_THREAD_EM_ASM({
    if (globalThis.t850) globalThis.t850.telemetry = JSON.parse(UTF8ToString($0));
  }, report.c_str());
#endif

  T8_LOG_INFO("[RuntimeTelemetry] Wrote %zu sampled frames to '%s'",
              g_state.samples.size(), outputPath.string().c_str());
}

} // namespace

RuntimeTelemetry::ScopedTimer::ScopedTimer(ScopeId id, bool accumulatedWork) : m_id(id), m_accumulatedWork(accumulatedWork) {
  if (!T850_ENABLE_PROFILING || id >= MaxMetrics) return;
  m_token = g_state.activeToken.load(std::memory_order_acquire);
  if (!m_token) return;
  if (!BufferFor(m_token)) { m_token = 0; return; }
  ++g_local.scopeDepth;
  m_start = std::chrono::steady_clock::now();
  m_active = true;
}

RuntimeTelemetry::ScopedTimer::ScopedTimer(const char* name)
  : ScopedTimer(IsFrameActive() && name ? RegisterScope(name) : InvalidId) {}
RuntimeTelemetry::ScopedTimer::ScopedTimer(std::string name)
  : ScopedTimer(IsFrameActive() ? RegisterScope(name) : InvalidId) {}

RuntimeTelemetry::ScopedTimer::~ScopedTimer() {
  if (!m_active) return;
  const auto end = std::chrono::steady_clock::now();
  const double elapsedMs = std::chrono::duration<double, std::milli>(end - m_start).count();
  if (m_accumulatedWork) {
    if (auto* buffer = BufferFor(m_token)) {
      auto& stats = buffer->metrics[m_id].scope;
      stats.totalMs += elapsedMs;
      stats.maxMs = (std::max)(stats.maxMs, elapsedMs);
      stats.count = 1;
      stats.accumulatedWork = true;
    }
  } else RuntimeTelemetry::RecordScope(m_id, elapsedMs, m_token);
  if (--g_local.scopeDepth == 0 && !g_local.frameThread) PublishThread();
}

void RuntimeTelemetry::InitializeFromConfig(const Config& cfg) {
  g_state.enabledAtomic.store(false, std::memory_order_release);
  g_state.activeToken.store(0, std::memory_order_release);
  g_state.uploadToken.store(0, std::memory_order_release);
  PublishThread();
  std::lock_guard<std::mutex> lock(g_state.mutex);
  g_state.initialized = true;
  g_state.enabled = T850_ENABLE_PROFILING && cfg.flags.runtimeTelemetry;
  g_state.frameActive = false;
  g_state.enabledAtomic.store(g_state.enabled, std::memory_order_relaxed);
  g_state.frameActiveAtomic.store(false, std::memory_order_relaxed);
  g_state.frequencyFrames = (std::max)(cfg.runtimeTelemetryFrequencyFrames, 0);
  g_state.uploadBudgetBytes = static_cast<uint64_t>((std::max)(0, cfg.telemetryUploadBudgetMB)) * 1024 * 1024;
  g_state.outputPath = cfg.runtimeTelemetryOutputPath.empty()
    ? std::string("logs/perf_telemetry.json")
    : cfg.runtimeTelemetryOutputPath;
  g_state.outputTimestamp = MakeTimestampString();
  g_state.startTime = std::chrono::steady_clock::now();
  g_state.currentFrame = 0;
  g_state.currentDeltaMs = 0.0;
  g_state.firstRuntimeFrameStartMs.reset();
  g_state.firstRuntimeFrameCompleteMs.reset();
  g_state.threads.clear();
  g_state.frameSlots.clear();
  g_state.budgetFrames.clear();
  g_state.frameThread = std::this_thread::get_id();
  g_state.firstToken.store(g_state.nextToken + 1, std::memory_order_release);
  g_state.session.fetch_add(1, std::memory_order_release);
  g_state.dropped.store(0);
  g_state.samples.clear();

  if (g_state.enabled) {
    StartSampleLocked(UINT64_MAX, 0);
    T8_LOG_INFO("[RuntimeTelemetry] Enabled: frequencyFrames=%d output='%s'",
                g_state.frequencyFrames, g_state.outputPath.c_str());
  }
}

void RuntimeTelemetry::Shutdown() {
  EndFrame();
  g_state.enabledAtomic.store(false, std::memory_order_release);
  std::lock_guard<std::mutex> lock(g_state.mutex);
  if (!g_state.initialized) return;
  DrainLocked();
  DumpLocked();
  g_state.initialized = false;
  g_state.enabled = false;
  g_state.enabledAtomic.store(false, std::memory_order_relaxed);
  g_state.frameActiveAtomic.store(false, std::memory_order_relaxed);
}

bool RuntimeTelemetry::IsEnabled() {
  return T850_ENABLE_PROFILING && g_state.enabledAtomic.load(std::memory_order_acquire);
}

bool RuntimeTelemetry::IsFrameActive() {
  return T850_ENABLE_PROFILING && g_state.activeToken.load(std::memory_order_acquire) != 0;
}

void RuntimeTelemetry::BeginFrame(uint64_t frameIndex, double deltaSeconds) {
  if (!IsEnabled()) return;
  const auto previous = g_state.uploadToken.exchange(0, std::memory_order_acq_rel);
  g_state.activeToken.store(0, std::memory_order_release);
  g_state.frameActiveAtomic.store(false, std::memory_order_release);
  PublishThread();
  std::lock_guard<std::mutex> lock(g_state.mutex);
  DrainLocked();
  const double elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_state.startTime).count();
  if (auto* frame = FindFrame(previous); frame && !frame->complete) {
    frame->cpuFrameMs = elapsedMs - frame->timeMs;
    frame->complete = true;
  }
  if (!g_state.firstRuntimeFrameStartMs) g_state.firstRuntimeFrameStartMs = elapsedMs;
  g_state.currentFrame = frameIndex;
  g_state.currentDeltaMs = deltaSeconds * 1000.0;
  g_state.frameActive =
    g_state.frequencyFrames == 0 ||
    (g_state.frequencyFrames > 0 && (frameIndex % static_cast<uint64_t>(g_state.frequencyFrames)) == 0);
  StartSampleLocked(frameIndex, deltaSeconds * 1000.0, g_state.frameActive);
}

void RuntimeTelemetry::EndFrame() {
  if (!IsEnabled()) return;
  const auto token = g_state.uploadToken.exchange(0, std::memory_order_acq_rel);
  g_state.activeToken.store(0, std::memory_order_release);
  PublishThread();
  std::lock_guard<std::mutex> lock(g_state.mutex);
  DrainLocked();
  if (auto* frame = FindFrame(token); frame && !frame->complete) {
    const double elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_state.startTime).count();
    frame->cpuFrameMs = elapsedMs - frame->timeMs;
    if (frame->frameIndex != UINT64_MAX && !g_state.firstRuntimeFrameCompleteMs)
      g_state.firstRuntimeFrameCompleteMs = elapsedMs;
    frame->complete = true;
  }
  g_state.frameActive = false;
  g_state.frameActiveAtomic.store(false, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordScope(const std::string& name, double elapsedMs) {
  if (IsFrameActive()) RecordScope(RegisterScope(name), elapsedMs);
}

void RuntimeTelemetry::AddCounter(const char* name, double value) {
  if (IsFrameActive() && name) AddCounter(RegisterCounter(name), value);
}

void RuntimeTelemetry::SetCounter(const char* name, double value) {
  if (IsFrameActive() && name) SetCounter(RegisterCounter(name), value);
}

RuntimeTelemetry::ScopeId RuntimeTelemetry::RegisterScope(std::string_view name) { return Register(name, false); }
RuntimeTelemetry::ScopeId RuntimeTelemetry::RegisterCounter(std::string_view name) { return Register(name, true); }
std::string RuntimeTelemetry::ScopeName(ScopeId id) {
  std::lock_guard lock(g_state.mutex);
  return id < g_state.registry.size() ? g_state.registry[id].name : std::string{};
}
void RuntimeTelemetry::PublishThread() { g_local.Publish(); }
uint64_t RuntimeTelemetry::DroppedRecords() { return g_state.dropped.load(); }
uint64_t RuntimeTelemetry::SlowPathEntries() { return g_state.slowPaths.load(); }
void RuntimeTelemetry::RecordScope(ScopeId id, double elapsedMs, uint64_t token) {
  if (!T850_ENABLE_PROFILING || id >= MaxMetrics) return;
  if (!token) token = g_state.activeToken.load(std::memory_order_acquire);
  if (auto* buffer = BufferFor(token)) {
    auto& stats = buffer->metrics[id].scope;
    stats.totalMs += elapsedMs;
    stats.maxMs = (std::max)(stats.maxMs, elapsedMs);
    ++stats.count;
  }
}
void RuntimeTelemetry::AddCounter(ScopeId id, double value) {
  if (!T850_ENABLE_PROFILING || id >= MaxMetrics) return;
  if (auto* buffer = BufferFor(g_state.activeToken.load(std::memory_order_acquire))) {
    auto& metric = buffer->metrics[id];
    metric.counter += value;
    metric.hasCounter = true;
  }
}
void RuntimeTelemetry::SetCounter(ScopeId id, double value) {
  if (!T850_ENABLE_PROFILING || id >= MaxMetrics) return;
  if (auto* buffer = BufferFor(g_state.activeToken.load(std::memory_order_acquire))) {
    auto& metric = buffer->metrics[id];
    metric.counter = value;
    metric.hasCounter = metric.setCounter = true;
  }
}
std::vector<RuntimeTelemetry::FrameSample> RuntimeTelemetry::Snapshot() {
  PublishThread();
  std::lock_guard lock(g_state.mutex);
  DrainLocked();
  return g_state.samples;
}
RuntimeTelemetry::UploadSourceGuard::UploadSourceGuard(UploadSource source) : m_previous(g_uploadSource) { g_uploadSource = source; }
RuntimeTelemetry::UploadSourceGuard::~UploadSourceGuard() { g_uploadSource = m_previous; }
RuntimeTelemetry::UploadSource RuntimeTelemetry::CurrentUploadSource() { return g_uploadSource; }
uint64_t RuntimeTelemetry::TextureUploadBytes(unsigned width, unsigned height, unsigned levels, unsigned layers, unsigned bytesPerPixel) {
  if (!width || !height || !layers || !bytesPerPixel || levels > 32) return 0;
  uint64_t total = 0;
  for (unsigned level = 0; level < (std::max)(1u, levels); ++level) {
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    const uint64_t stride = static_cast<uint64_t>(layers) * bytesPerPixel;
    if (pixels > (UINT64_MAX - total) / stride) return 0;
    total += pixels * stride;
    width = (std::max)(1u, width / 2);
    height = (std::max)(1u, height / 2);
  }
  return total;
}
RuntimeTelemetry::UploadGuard::UploadGuard(UploadResource resource, uint64_t logicalBytes, uint64_t stagingBytes) {
  if (!T850_ENABLE_PROFILING || g_uploadActive || !logicalBytes || resource >= UploadResource::Count) return;
  m_token = g_state.uploadToken.load(std::memory_order_acquire);
  if (!m_token) return;
  if (!BufferFor(m_token)) { m_token = 0; return; }
  g_uploadActive = true;
  ++g_local.scopeDepth;
  m_slot = static_cast<size_t>(resource) * 3 + static_cast<size_t>(g_uploadSource);
  g_uploadIssueToken = m_token;
  g_uploadSlot = m_slot;
  m_stats.logicalBytes = m_stats.largestBytes = logicalBytes;
  m_stats.stagingBytes = stagingBytes;
  m_stats.calls = 1;
  m_start = std::chrono::steady_clock::now();
}
RuntimeTelemetry::UploadGuard::~UploadGuard() {
  if (!m_token || m_slot >= 12) return;
  m_stats.nanoseconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m_start).count());
  if (auto* buffer = BufferFor(m_token)) MergeUpload(buffer->uploads[m_slot], m_stats);
  g_uploadActive = false;
  g_uploadIssueToken = 0;
  if (--g_local.scopeDepth == 0 && !g_local.frameThread) PublishThread();
}

void RuntimeTelemetry::RecordStaging(UploadResource resource, uint64_t bytes, uint64_t reallocations) {
  if (!T850_ENABLE_PROFILING) return;
  const bool nested = g_uploadActive && g_uploadSlot / 3 == static_cast<size_t>(resource);
  const size_t slot = nested ? g_uploadSlot : static_cast<size_t>(resource) * 3 + static_cast<size_t>(g_uploadSource);
  if (slot >= 12) return;
  if (auto* buffer = BufferFor(nested ? g_uploadIssueToken : g_state.uploadToken.load(std::memory_order_acquire))) {
    buffer->uploads[slot].stagingBytes += bytes;
    buffer->uploads[slot].reallocations += reallocations;
  }
}

void RuntimeTelemetry::RecordRingOverflow() {
  if (!T850_ENABLE_PROFILING) return;
  if (auto* buffer = BufferFor(g_state.uploadToken.load(std::memory_order_acquire))) ++buffer->ringOverflows;
}

void RuntimeTelemetry::RecordActiveStaging(uint64_t bytes, uint64_t reallocations) {
  if (g_uploadActive) RecordStaging(static_cast<UploadResource>(g_uploadSlot / 3), bytes, reallocations);
}

RuntimeTelemetry::DrawCounterScope::DrawCounterScope(ScopeId draws, ScopeId indices)
  : m_previousDraws(g_passDraws), m_previousIndices(g_passIndices) {
  g_passDraws = draws;
  g_passIndices = indices;
}
RuntimeTelemetry::DrawCounterScope::~DrawCounterScope() {
  g_passDraws = m_previousDraws;
  g_passIndices = m_previousIndices;
}
void RuntimeTelemetry::RecordDraw(uint32_t indices) {
  T8_TELEMETRY_ADD("gpu.draws", 1);
  T8_TELEMETRY_ADD("gpu.indices", indices);
  if (g_passDraws != InvalidId) {
    AddCounter(g_passDraws, 1);
    AddCounter(g_passIndices, indices);
    T8_TELEMETRY_ADD("render.draws", 1);
    T8_TELEMETRY_ADD("render.indices", indices);
  } else {
    T8_TELEMETRY_ADD("gpu.auxiliary_draws", 1);
    T8_TELEMETRY_ADD("gpu.auxiliary_indices", indices);
  }
}

} // namespace t850
