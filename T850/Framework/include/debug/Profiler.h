#pragma once
// ─── T8 Profiler: Cross-API GPU + CPU Frame Profiling ────
//
// Usage:
//   1. Call Init() after driver creation
//   2. Call BeginFrame() / EndFrame() around each frame
//   3. Call BeginScope("name") / EndScope() around sections to measure
//   4. After N frames, call Report() to print timing breakdown
//
// GPU timestamps are collected asynchronously (results from frame N-2).
// CPU timestamps use QueryPerformanceCounter.
//
// Enable at runtime via --profile or the Android PROFILE launch extra.

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <debug/RuntimeTelemetry.h>

#ifdef OS_WINDOWS
#include <windows.h>
#endif

namespace t850 {

  class BaseDriver;

  // ── Per-scope accumulated timing ──
  struct ProfileScope {
    std::string name;
    int parentIndex = -1;
    uint64_t generation = 0;
    double gpuTotalMs = 0.0;   // accumulated GPU time
    double cpuTotalMs = 0.0;   // accumulated CPU time
    int cpuSampleCount = 0;
    int gpuSampleCount = 0;
    uint64_t drawCalls = 0;
    uint64_t triangles = 0;

    double GpuAvgMs() const { return gpuSampleCount > 0 ? gpuTotalMs / gpuSampleCount : 0.0; }
    double CpuAvgMs() const { return cpuSampleCount > 0 ? cpuTotalMs / cpuSampleCount : 0.0; }
  };

  struct ProfileFrameQuery {
    int scopeIndex = -1;
    uint64_t generation = 0;
    int64_t cpuBegin = 0;
    int64_t cpuEnd = 0;
    bool cpuOnly = false;
  };

  class ProfilerGpuBackend {
  public:
    virtual ~ProfilerGpuBackend() = default;
    virtual bool Init(BaseDriver* driver, int maxScopes) = 0;
    virtual const char* Name() const = 0;
    virtual void Resolve(std::vector<ProfileScope>& scopes) = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame(int activeQueryCount,
                          const std::vector<ProfileFrameQuery>& frameQueries) = 0;
    virtual void BeginScope(int queryIndex) = 0;
    virtual void EndScope(int queryIndex) = 0;
    virtual void FlushQueryReset(void* commandBuffer) { (void)commandBuffer; }
  };

  std::unique_ptr<ProfilerGpuBackend> CreateProfilerGpuBackend(BaseDriver* driver);

  // ── Profiler interface ──
  class Profiler {
  public:
    Profiler() = default;
    ~Profiler();

    // Initialize the profiler for the current graphics API.
    // maxScopes: maximum number of named scopes per frame.
    void Init(BaseDriver* driver, int maxScopes = 64);
    void Init(BaseDriver* driver, int maxScopes, std::unique_ptr<ProfilerGpuBackend> backend,
          int64_t (*clock)() = nullptr, int64_t clockFrequency = 0);
    void Destroy();

    bool IsInitialized() const { return m_initialized; }

    // Frame boundary
    void BeginFrame();
    void EndFrame();

    // Scopes are inclusive and strictly nested on the frame thread.
    using ScopeToken = uint64_t;
    using ScopeId = RuntimeTelemetry::ScopeId;
    static ScopeId RegisterScope(const char* name) { return name ? RuntimeTelemetry::RegisterScope(name) : RuntimeTelemetry::InvalidId; }
    ScopeToken BeginScope(ScopeId id);
    ScopeToken BeginScope(const char* name);
    void EndScope();
    void EndScope(ScopeToken token);

    // CPU-only scope (no GPU timestamp, just QPC)
    ScopeToken BeginCPUScope(const char* name);
    ScopeToken BeginCPUScope(ScopeId id);
    void EndCPUScope();
    void EndCPUScope(ScopeToken token);

    // Draw call tracking (called from DrawIndexed)
    void AddDrawCall(int vertexCount);

    // Flush deferred query reset after command recording starts, before a render pass.
    void FlushDeferredQueryReset(void* commandBuffer);

    // Reporting
    int  GetFrameCount() const { return m_frameCount; }
    uint64_t GetWarningCount() const { return m_warningCount; }
    const std::vector<ProfileScope>& GetScopes() const { return m_scopes; }
    void Report(int topN = 0) const;  // print to log; 0 = all scopes
    void Reset();                      // clear accumulated data

  private:
    int FindOrCreateScope(ScopeId id, int parentIndex);
    ScopeToken Begin(ScopeId id, bool cpuOnly);
    void End(ScopeToken token, bool cpuOnly);
    void CloseScope(bool recordSample);
    void DiscardOpenScopes();

    struct ActiveScope {
      ScopeToken token;
      int queryIndex;
      bool cpuOnly;
    };

    // ── State ──
    bool         m_initialized = false;
    BaseDriver*  m_driver      = nullptr;
    int          m_maxScopes   = 64;
    int          m_frameCount  = 0;
    bool         m_frameActive = false;
    uint64_t     m_generation = 0;
    uint64_t     m_warningCount = 0;
    ScopeToken   m_nextToken = 1;
    ScopeToken   m_firstValidToken = 1;

    // CPU timing
    int64_t      m_cpuFreq     = 0;
    int64_t      (*m_clock)() = nullptr;

    // Per-frame active queries
    int          m_activeQueryCount = 0;
    std::vector<ProfileFrameQuery> m_frameQueries;  // [maxScopes] per frame
    std::vector<ActiveScope> m_scopeStack;

    // Accumulated results
    std::vector<ProfileScope> m_scopes;
    std::vector<int> m_scopeLookup;

    std::unique_ptr<ProfilerGpuBackend> m_gpuBackend;
  };

  // ── RAII scoped timer (GPU + CPU) ──
  struct ProfileScopeGuard {
    Profiler* profiler;
    Profiler::ScopeToken token = 0;
    ProfileScopeGuard(Profiler* p, const char* name) : profiler(p) {
      if (profiler) token = profiler->BeginScope(name);
    }
    ProfileScopeGuard(Profiler* owner, Profiler::ScopeId id) : profiler(owner) {
      if (profiler) token = profiler->BeginScope(id);
    }
    ProfileScopeGuard(const ProfileScopeGuard&) = delete;
    ProfileScopeGuard& operator=(const ProfileScopeGuard&) = delete;
    ~ProfileScopeGuard() {
      if (profiler) profiler->EndScope(token);
    }
  };

  // ── RAII CPU-only scoped timer (no GPU timestamp) ──
  struct CPUProfileScopeGuard {
    Profiler* profiler;
    Profiler::ScopeToken token = 0;
    CPUProfileScopeGuard(Profiler* p, const char* name) : profiler(p) {
      if (profiler) token = profiler->BeginCPUScope(name);
    }
    CPUProfileScopeGuard(Profiler* owner, Profiler::ScopeId id) : profiler(owner) {
      if (profiler) token = profiler->BeginCPUScope(id);
    }
    CPUProfileScopeGuard(const CPUProfileScopeGuard&) = delete;
    CPUProfileScopeGuard& operator=(const CPUProfileScopeGuard&) = delete;
    ~CPUProfileScopeGuard() {
      if (profiler) profiler->EndCPUScope(token);
    }
  };

  // Global profiler instance (set by framework)
  extern Profiler* g_profiler;

} // namespace t850

// ── Active macros ──
#define T8_PROFILE_JOIN_IMPL(left, right) left##right
#define T8_PROFILE_JOIN(left, right) T8_PROFILE_JOIN_IMPL(left, right)
#if T850_ENABLE_PROFILING
#define T8_PROFILE_SCOPE(profiler, name) \
  static const auto T8_PROFILE_JOIN(_t8ScopeId, __LINE__) = t850::Profiler::RegisterScope(name); \
  t850::ProfileScopeGuard T8_PROFILE_JOIN(_t8prof, __LINE__)((profiler), T8_PROFILE_JOIN(_t8ScopeId, __LINE__))
#define T8_PROFILE_CPU_SCOPE(profiler, name) \
  static const auto T8_PROFILE_JOIN(_t8CpuScopeId, __LINE__) = t850::Profiler::RegisterScope(name); \
  t850::CPUProfileScopeGuard T8_PROFILE_JOIN(_t8cpuprof, __LINE__)((profiler), T8_PROFILE_JOIN(_t8CpuScopeId, __LINE__))
#else
#define T8_PROFILE_SCOPE(profiler, name) ((void)0)
#define T8_PROFILE_CPU_SCOPE(profiler, name) ((void)0)
#endif
