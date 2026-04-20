#pragma once
// ─── T8 Profiler: Cross-API GPU + CPU Frame Profiling ────
//
// Guarded by T8_ENABLE_PROFILER.  When undefined, all profiler
// calls compile to no-ops with zero runtime cost.
//
// To enable: add T8_ENABLE_PROFILER to preprocessor defines
// (the build script does this for profiling builds).
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
// Enable via --profile CLI flag + T8_ENABLE_PROFILER define.

#ifdef T8_ENABLE_PROFILER

#include <string>
#include <vector>
#include <cstdint>

#ifdef OS_WINDOWS
#include <windows.h>
#endif

namespace t800 {

  class BaseDriver;

  // ── Per-scope accumulated timing ──
  struct ProfileScope {
    std::string name;
    double gpuTotalMs = 0.0;   // accumulated GPU time
    double cpuTotalMs = 0.0;   // accumulated CPU time
    int    sampleCount = 0;

    double GpuAvgMs() const { return sampleCount > 0 ? gpuTotalMs / sampleCount : 0.0; }
    double CpuAvgMs() const { return sampleCount > 0 ? cpuTotalMs / sampleCount : 0.0; }
  };

  // ── Profiler interface ──
  class T8Profiler {
  public:
    T8Profiler() = default;
    ~T8Profiler();

    // Initialize the profiler for the current graphics API.
    // maxScopes: maximum number of named scopes per frame.
    void Init(BaseDriver* driver, int maxScopes = 64);
    void Destroy();

    bool IsInitialized() const { return m_initialized; }

    // Frame boundary
    void BeginFrame();
    void EndFrame();

    // Scope measurement (nest-safe but scopes should not overlap)
    void BeginScope(const char* name);
    void EndScope();

    // CPU-only scope (no GPU timestamp, just QPC)
    void BeginCPUScope(const char* name);
    void EndCPUScope();

    // Vulkan: flush deferred query pool reset (call after vkBeginCommandBuffer, before render pass)
    void FlushVulkanQueryReset(void* commandBuffer);

    // Reporting
    int  GetFrameCount() const { return m_frameCount; }
    const std::vector<ProfileScope>& GetScopes() const { return m_scopes; }
    void Report(int topN = 0) const;  // print to log; 0 = all scopes
    void Reset();                      // clear accumulated data

  private:
    // ── Per-frame query data ──
    struct FrameQuery {
      int    scopeIndex = -1;
      int64_t cpuBegin = 0;
      int64_t cpuEnd   = 0;
      bool   cpuOnly   = false;  // true = no GPU timestamp for this scope
    };

    int FindOrCreateScope(const char* name);

    // ── API-specific GPU timestamp backend ──
    void InitGPU_D3D12();
    void InitGPU_D3D11();
    void InitGPU_GL();
    void InitGPU_Vulkan();
    void DestroyGPU();

    void BeginGPUScope(int queryIndex);
    void EndGPUScope(int queryIndex);
    void ResolveGPUFrame();  // collect results from completed frame

    // ── State ──
    bool         m_initialized = false;
    BaseDriver*  m_driver      = nullptr;
    int          m_maxScopes   = 64;
    int          m_frameCount  = 0;
    int          m_apiType     = 0;  // 0=unknown, 1=D3D12, 2=D3D11, 3=GL, 4=Vulkan

    // CPU timing
    int64_t      m_cpuFreq     = 0;

    // Per-frame active queries
    int          m_activeQueryCount = 0;
    std::vector<FrameQuery> m_frameQueries;  // [maxScopes] per frame

    // Accumulated results
    std::vector<ProfileScope> m_scopes;

    // GPU backend opaque state (cast per API)
    void*        m_gpuState    = nullptr;
  };

  // ── RAII scoped timer (GPU + CPU) ──
  struct ProfileScopeGuard {
    T8Profiler* profiler;
    ProfileScopeGuard(T8Profiler* p, const char* name) : profiler(p) {
      if (profiler) profiler->BeginScope(name);
    }
    ~ProfileScopeGuard() {
      if (profiler) profiler->EndScope();
    }
  };

  // ── RAII CPU-only scoped timer (no GPU timestamp) ──
  struct CPUProfileScopeGuard {
    T8Profiler* profiler;
    CPUProfileScopeGuard(T8Profiler* p, const char* name) : profiler(p) {
      if (profiler) profiler->BeginCPUScope(name);
    }
    ~CPUProfileScopeGuard() {
      if (profiler) profiler->EndCPUScope();
    }
  };

  // Global profiler instance (set by framework)
  extern T8Profiler* g_profiler;

} // namespace t800

// ── Active macros ──
#define T8_PROFILE_SCOPE(profiler, name) \
  t800::ProfileScopeGuard _t8prof##__LINE__((profiler), (name))

#define T8_PROFILE_CPU_SCOPE(profiler, name) \
  t800::CPUProfileScopeGuard _t8cpuprof##__LINE__((profiler), (name))

#else // T8_ENABLE_PROFILER not defined — everything compiles away

namespace t800 {
  class T8Profiler;
  inline T8Profiler* g_profiler = nullptr;
}

#define T8_PROFILE_SCOPE(profiler, name)     ((void)0)
#define T8_PROFILE_CPU_SCOPE(profiler, name) ((void)0)

#endif // T8_ENABLE_PROFILER
