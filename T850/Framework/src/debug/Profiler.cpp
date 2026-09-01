#include <pch.h>
#include <debug/Profiler.h>

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
  Destroy();
  m_driver = driver;
  m_maxScopes = maxScopes;
  m_frameQueries.assign(maxScopes, ProfileFrameQuery{});
#ifdef OS_WINDOWS
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  m_cpuFreq = frequency.QuadPart;
#else
  m_cpuFreq = 1000000000LL;
#endif

  m_gpuBackend = CreateProfilerGpuBackend(driver);
  if (m_gpuBackend && !m_gpuBackend->Init(driver, maxScopes)) {
    T8_LOG_ERROR("[Profiler] Failed to initialize %s GPU backend", driver->ApiTag());
    m_gpuBackend.reset();
  }
  m_initialized = true;
  T8_LOG_INFO("[Profiler] Initialized (API=%s, GPU=%s, maxScopes=%d)",
              driver->ApiTag(), m_gpuBackend ? m_gpuBackend->Name() : "disabled", maxScopes);
}

void Profiler::Destroy() {
  m_gpuBackend.reset();
  m_driver = nullptr;
  m_initialized = false;
}

void Profiler::BeginFrame() {
  if (!m_initialized) return;
  if (m_gpuBackend) m_gpuBackend->Resolve(m_scopes);
  m_activeQueryCount = 0;
  if (m_gpuBackend) m_gpuBackend->BeginFrame();
}

void Profiler::EndFrame() {
  if (!m_initialized) return;
  if (m_gpuBackend) m_gpuBackend->EndFrame(m_activeQueryCount, m_frameQueries);
  ++m_frameCount;
}

int Profiler::FindOrCreateScope(const char* name) {
  for (int index = 0; index < static_cast<int>(m_scopes.size()); ++index) {
    if (m_scopes[index].name == name) return index;
  }
  ProfileScope scope;
  scope.name = name;
  m_scopes.push_back(scope);
  return static_cast<int>(m_scopes.size()) - 1;
}

void Profiler::BeginScope(const char* name) {
  if (!m_initialized || m_activeQueryCount >= m_maxScopes) return;
  const int queryIndex = m_activeQueryCount;
  ProfileFrameQuery& query = m_frameQueries[queryIndex];
  query.scopeIndex = FindOrCreateScope(name);
  query.cpuOnly = false;
  query.cpuBegin = GetProfilerTicks();
  if (m_gpuBackend) m_gpuBackend->BeginScope(queryIndex);
  ++m_activeQueryCount;
}

void Profiler::EndScope() {
  if (!m_initialized || m_activeQueryCount <= 0) return;
  const int queryIndex = m_activeQueryCount - 1;
  ProfileFrameQuery& query = m_frameQueries[queryIndex];
  query.cpuEnd = GetProfilerTicks();
  if (m_gpuBackend) m_gpuBackend->EndScope(queryIndex);
  if (query.scopeIndex >= 0 && query.scopeIndex < static_cast<int>(m_scopes.size())) {
    m_scopes[query.scopeIndex].cpuTotalMs +=
      static_cast<double>(query.cpuEnd - query.cpuBegin) * 1000.0 /
      static_cast<double>(m_cpuFreq);
    if (!m_gpuBackend) ++m_scopes[query.scopeIndex].sampleCount;
  }
}

void Profiler::BeginCPUScope(const char* name) {
  if (!m_initialized || m_activeQueryCount >= m_maxScopes) return;
  ProfileFrameQuery& query = m_frameQueries[m_activeQueryCount];
  query.scopeIndex = FindOrCreateScope(name);
  query.cpuOnly = true;
  query.cpuBegin = GetProfilerTicks();
  ++m_activeQueryCount;
}

void Profiler::EndCPUScope() {
  if (!m_initialized || m_activeQueryCount <= 0) return;
  ProfileFrameQuery& query = m_frameQueries[m_activeQueryCount - 1];
  query.cpuEnd = GetProfilerTicks();
  if (query.scopeIndex >= 0 && query.scopeIndex < static_cast<int>(m_scopes.size())) {
    m_scopes[query.scopeIndex].cpuTotalMs +=
      static_cast<double>(query.cpuEnd - query.cpuBegin) * 1000.0 /
      static_cast<double>(m_cpuFreq);
    ++m_scopes[query.scopeIndex].sampleCount;
  }
}

void Profiler::AddDrawCall(int vertexCount) {
  if (!m_initialized || m_activeQueryCount <= 0) return;
  const ProfileFrameQuery& query = m_frameQueries[m_activeQueryCount - 1];
  if (query.scopeIndex >= 0 && query.scopeIndex < static_cast<int>(m_scopes.size())) {
    ++m_scopes[query.scopeIndex].drawCalls;
    m_scopes[query.scopeIndex].triangles += vertexCount / 3;
  }
}

void Profiler::FlushVulkanQueryReset(void* commandBuffer) {
  if (m_gpuBackend) m_gpuBackend->FlushQueryReset(commandBuffer);
}

void Profiler::Report(int topN) const {
  T8_LOG_INFO("╔══════════════════════════════════════════════════════════════════════════════╗");
  T8_LOG_INFO("║  PROFILER REPORT  (%d frames)                                               ║", m_frameCount);
  T8_LOG_INFO("╠═══════════════════════════════╦═══════════╦═══════════╦═══════╦════════╦═════╣");
  T8_LOG_INFO("║ Scope                         ║  GPU avg  ║  CPU avg  ║ Draws ║  Tris  ║  N  ║");
  T8_LOG_INFO("╠═══════════════════════════════╬═══════════╬═══════════╬═══════╬════════╬═════╣");

  std::vector<int> order(m_scopes.size());
  for (int index = 0; index < static_cast<int>(order.size()); ++index) order[index] = index;
  std::sort(order.begin(), order.end(), [&](int left, int right) {
    return m_scopes[left].GpuAvgMs() > m_scopes[right].GpuAvgMs();
  });

  double totalGpu = 0.0;
  double totalCpu = 0.0;
  int totalDraws = 0;
  int totalTriangles = 0;
  const int count = topN > 0 ? (std::min)(topN, static_cast<int>(order.size()))
                             : static_cast<int>(order.size());
  for (int index = 0; index < count; ++index) {
    const ProfileScope& scope = m_scopes[order[index]];
    T8_LOG_INFO("║ %-29s ║ %7.3fms ║ %7.3fms ║ %5d ║ %6d ║ %3d ║",
                scope.name.c_str(), scope.GpuAvgMs(), scope.CpuAvgMs(),
                scope.drawCalls, scope.triangles, scope.sampleCount);
    totalGpu += scope.GpuAvgMs();
    totalCpu += scope.CpuAvgMs();
    totalDraws += scope.drawCalls;
    totalTriangles += scope.triangles;
  }

  T8_LOG_INFO("╠═══════════════════════════════╬═══════════╬═══════════╬═══════╬════════╬═════╣");
  T8_LOG_INFO("║ TOTAL                         ║ %7.3fms ║ %7.3fms ║ %5d ║ %6d ║     ║",
              totalGpu, totalCpu, totalDraws, totalTriangles);
  T8_LOG_INFO("╚═══════════════════════════════╩═══════════╩═══════════╩═══════╩════════╩═════╝");
}

void Profiler::Reset() {
  m_scopes.clear();
  m_frameCount = 0;
}

} // namespace t850
