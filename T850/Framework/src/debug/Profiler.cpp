#include <pch.h>
#include <debug/Profiler.h>
#include <core/Config.h>

#include <utils/Log.h>
#include <video/BaseDriver.h>

#include <algorithm>
#include <chrono>

namespace t850 {

Profiler* g_profiler = nullptr;

namespace {

int64_t GetProfilerTicks() {
#ifdef OS_WINDOWS
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return now.QuadPart;
#else
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

} // namespace

Profiler::~Profiler() {
  Destroy();
}

void Profiler::Init(BaseDriver* driver, int maxScopes) {
  Init(driver, maxScopes, g_config.profileCpuOnly || !T850_ENABLE_PROFILING
    ? nullptr : CreateProfilerGpuBackend(driver));
}

void Profiler::Init(BaseDriver* driver, int maxScopes,
                    std::unique_ptr<ProfilerGpuBackend> backend,
                    int64_t (*clock)(), int64_t clockFrequency) {
  Destroy();
  m_driver = driver;
  m_maxScopes = (std::max)(maxScopes, 1);
  m_frameQueries.assign(m_maxScopes, ProfileFrameQuery{});
  m_scopeStack.reserve(m_maxScopes);
  m_scopes.reserve(RuntimeTelemetry::MaxMetrics);
  m_scopeLookup.assign((RuntimeTelemetry::MaxMetrics + 1) * RuntimeTelemetry::MaxMetrics, -1);
  m_clock = GetProfilerTicks;
#ifdef OS_WINDOWS
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  m_cpuFreq = frequency.QuadPart;
#else
  m_cpuFreq = 1000000000LL;
#endif

  if (clock && clockFrequency > 0) {
    m_clock = clock;
    m_cpuFreq = clockFrequency;
  }
  m_gpuBackend = std::move(backend);
  if (m_gpuBackend && !m_gpuBackend->Init(driver, m_maxScopes)) {
    T8_LOG_ERROR("[Profiler] Failed to initialize %s GPU backend", m_gpuBackend->Name());
    m_gpuBackend.reset();
  }
  m_initialized = true;
  T8_LOG_INFO("[Profiler] Initialized (API=%s, GPU=%s, maxScopes=%d)",
              driver ? driver->ApiTag() : "none", m_gpuBackend ? m_gpuBackend->Name() : "disabled", m_maxScopes);
}

void Profiler::Destroy() {
  m_gpuBackend.reset();
  m_driver = nullptr;
  m_initialized = false;
  m_frameActive = false;
  m_activeQueryCount = 0;
  m_scopeStack.clear();
  m_scopes.clear();
  m_frameCount = 0;
  m_warningCount = 0;
  ++m_generation;
  m_firstValidToken = m_nextToken;
}

void Profiler::BeginFrame() {
  if (!m_initialized) return;
  if (m_frameActive) {
    ++m_warningCount;
    T8_LOG_INFO("[Profiler] WARNING: BeginFrame before EndFrame; discarding unfinished scopes");
    EndFrame();
  }
  if (m_gpuBackend) m_gpuBackend->Resolve(m_scopes);
  m_activeQueryCount = 0;
  m_firstValidToken = m_nextToken;
  m_frameActive = true;
  if (m_gpuBackend) m_gpuBackend->BeginFrame();
}

void Profiler::EndFrame() {
  if (!m_initialized || !m_frameActive) return;
  DiscardOpenScopes();
  if (m_gpuBackend) m_gpuBackend->EndFrame(m_activeQueryCount, m_frameQueries);
  m_frameActive = false;
  m_firstValidToken = m_nextToken;
  ++m_frameCount;
}

int Profiler::FindOrCreateScope(ScopeId id, int parentIndex) {
  auto& index = m_scopeLookup[(static_cast<size_t>(parentIndex) + 1) * RuntimeTelemetry::MaxMetrics + id];
  if (index >= 0) return index;
  if (m_scopes.size() >= RuntimeTelemetry::MaxMetrics) return -1;
  ProfileScope scope;
  scope.name = RuntimeTelemetry::ScopeName(id);
  scope.parentIndex = parentIndex;
  scope.generation = m_generation;
  m_scopes.push_back(scope);
  index = static_cast<int>(m_scopes.size()) - 1;
  return index;
}

Profiler::ScopeToken Profiler::BeginScope(const char* name) {
  return Begin(RegisterScope(name), false);
}
Profiler::ScopeToken Profiler::BeginScope(ScopeId id) { return Begin(id, false); }

void Profiler::EndScope() {
  End(m_scopeStack.empty() ? m_nextToken : m_scopeStack.back().token, false);
}

void Profiler::EndScope(ScopeToken token) { End(token, false); }

Profiler::ScopeToken Profiler::BeginCPUScope(const char* name) {
  return Begin(RegisterScope(name), true);
}
Profiler::ScopeToken Profiler::BeginCPUScope(ScopeId id) { return Begin(id, true); }

void Profiler::EndCPUScope() {
  End(m_scopeStack.empty() ? m_nextToken : m_scopeStack.back().token, true);
}

void Profiler::EndCPUScope(ScopeToken token) { End(token, true); }

Profiler::ScopeToken Profiler::Begin(ScopeId id, bool cpuOnly) {
  if (!m_initialized || !m_frameActive) return 0;
  const ScopeToken token = m_nextToken++;
  if (id >= RuntimeTelemetry::MaxMetrics || m_activeQueryCount >= m_maxScopes) {
    ++m_warningCount;
    T8_LOG_INFO("[Profiler] WARNING: Scope ID %u not recorded: invalid ID or query capacity (%d)",
          static_cast<unsigned>(id), m_maxScopes);
    m_scopeStack.push_back({token, -1, cpuOnly});
    return token;
  }
  const int parentIndex = m_scopeStack.empty() || m_scopeStack.back().queryIndex < 0
    ? -1 : m_frameQueries[m_scopeStack.back().queryIndex].scopeIndex;
  const int queryIndex = m_activeQueryCount++;
  ProfileFrameQuery& query = m_frameQueries[queryIndex];
  query = {};
  query.scopeIndex = FindOrCreateScope(id, parentIndex);
  if (query.scopeIndex < 0) {
    --m_activeQueryCount;
    ++m_warningCount;
    m_scopeStack.push_back({token, -1, cpuOnly});
    return token;
  }
  query.generation = m_generation;
  query.cpuOnly = cpuOnly;
  query.cpuBegin = m_clock();
  if (m_gpuBackend && !cpuOnly) m_gpuBackend->BeginScope(queryIndex);
  m_scopeStack.push_back({token, queryIndex, cpuOnly});
  return token;
}

void Profiler::End(ScopeToken token, bool cpuOnly) {
  if (!m_initialized || !m_frameActive || token == 0 || token < m_firstValidToken) return;
  if (m_scopeStack.empty() || m_scopeStack.back().token != token ||
      m_scopeStack.back().cpuOnly != cpuOnly) {
    ++m_warningCount;
    T8_LOG_INFO("[Profiler] WARNING: Unmatched or out-of-order %s scope end", cpuOnly ? "CPU" : "GPU/CPU");
    return;
  }
  CloseScope(true);
}

void Profiler::CloseScope(bool recordSample) {
  const ActiveScope active = m_scopeStack.back();
  m_scopeStack.pop_back();
  if (active.queryIndex < 0) return;
  ProfileFrameQuery& query = m_frameQueries[active.queryIndex];
  query.cpuEnd = m_clock();
  if (m_gpuBackend && !query.cpuOnly) m_gpuBackend->EndScope(active.queryIndex);
  if (recordSample && query.generation == m_generation && query.scopeIndex >= 0) {
    ProfileScope& scope = m_scopes[query.scopeIndex];
    scope.cpuTotalMs += static_cast<double>(query.cpuEnd - query.cpuBegin) * 1000.0 /
                        static_cast<double>(m_cpuFreq);
    ++scope.cpuSampleCount;
  } else {
    query.scopeIndex = -1;
  }
}

void Profiler::DiscardOpenScopes() {
  while (!m_scopeStack.empty()) {
    const int queryIndex = m_scopeStack.back().queryIndex;
    const int scopeIndex = queryIndex < 0 ? -1 : m_frameQueries[queryIndex].scopeIndex;
    ++m_warningCount;
    T8_LOG_INFO("[Profiler] WARNING: Unbalanced scope '%s' at frame boundary; sample discarded",
                scopeIndex < 0 ? "<unrecorded>" : m_scopes[scopeIndex].name.c_str());
    CloseScope(false);
  }
}

void Profiler::AddDrawCall(int vertexCount) {
  if (!m_initialized || !m_frameActive || m_scopeStack.empty() ||
      m_scopeStack.back().queryIndex < 0) return;
  const ProfileFrameQuery& query = m_frameQueries[m_scopeStack.back().queryIndex];
  if (query.scopeIndex >= 0 && query.scopeIndex < static_cast<int>(m_scopes.size())) {
    ++m_scopes[query.scopeIndex].drawCalls;
    m_scopes[query.scopeIndex].triangles += vertexCount / 3;
  }
}

void Profiler::FlushDeferredQueryReset(void* commandBuffer) {
  if (m_gpuBackend) m_gpuBackend->FlushQueryReset(commandBuffer);
}

void Profiler::Report(int topN) const {
  T8_LOG_INFO("[Profiler] REPORT (%d frames): inclusive scope tree; times are not additive", m_frameCount);
  int reported = 0;
  const auto reportChildren = [&](auto&& self, int parent, int depth) -> void {
    for (int index = 0; index < static_cast<int>(m_scopes.size()); ++index) {
      const ProfileScope& scope = m_scopes[index];
      if (scope.parentIndex != parent || (topN > 0 && reported >= topN)) continue;
      const std::string gpu = scope.gpuSampleCount > 0 ? std::to_string(scope.GpuAvgMs()) + "ms" : "unavailable";
      T8_LOG_INFO("[Profiler] %*s%s CPU=%.6fms GPU=%s CPU_N=%d GPU_N=%d Draws=%llu Tris=%llu",
                  depth * 2, "", scope.name.c_str(), scope.CpuAvgMs(), gpu.c_str(),
                  scope.cpuSampleCount, scope.gpuSampleCount,
                  static_cast<unsigned long long>(scope.drawCalls), static_cast<unsigned long long>(scope.triangles));
      ++reported;
      self(self, index, depth + 1);
    }
  };
  reportChildren(reportChildren, -1, 0);
}

void Profiler::Reset() {
  while (!m_scopeStack.empty()) CloseScope(false);
  ++m_generation;
  m_firstValidToken = m_nextToken;
  m_scopes.clear();
  std::fill(m_scopeLookup.begin(), m_scopeLookup.end(), -1);
  m_frameCount = 0;
}

} // namespace t850
