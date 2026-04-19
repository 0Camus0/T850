#include <debug/T8_Profiler.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>

#include <algorithm>
#include <cstring>
#include <cstdio>

#ifdef OS_WINDOWS
#include <video/d3d12/D3D12Driver.h>
#include <video/windows/D3D11Driver.h>
#include <d3d12.h>
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#if defined(USING_OPENGL)
#include <GL/glew.h>
#elif defined(USING_GL_COMMON)
#include <GLES3/gl3.h>
#endif

namespace t800 {

T8Profiler* g_profiler = nullptr;

extern BaseDriver* g_pBaseDriver;
extern Device*        T8Device;
extern DeviceContext* T8DeviceContext;

// ═════════════════════════════════════════════════════════════
//  GPU Backend State (per-API)
// ═════════════════════════════════════════════════════════════

#ifdef OS_WINDOWS
struct D3D12ProfileState {
  ComPtr<ID3D12QueryHeap> queryHeap;
  ComPtr<ID3D12Resource>  readbackBuffer;
  int maxQueries = 0;       // total query slots (2 per scope: begin + end)
  uint64_t gpuFrequency = 0;
  int writeFrame = 0;
  static const int kFrameDelay = 3;  // match kBackBufferCount
  // Per-frame ring buffer of scope mappings
  struct FrameRecord {
    int activeCount = 0;
    int scopeIndices[64] = {};  // scope index for each query slot
  };
  FrameRecord frameRecords[kFrameDelay];
};

struct D3D11ProfileState {
  struct QueryPair {
    ComPtr<ID3D11Query> begin;
    ComPtr<ID3D11Query> end;
    ComPtr<ID3D11Query> disjoint;
    bool pending = false;
  };
  // Double-buffer: frame N writes set A, frame N+1 reads set A
  std::vector<QueryPair> querySets[2];
  int writeSet = 0;
  int maxQueries = 0;
};
#endif

struct GLProfileState {
  struct QueryPair {
    unsigned int beginQuery = 0;
    unsigned int endQuery = 0;
  };
  std::vector<QueryPair> querySets[2];
  int writeSet = 0;
  int maxQueries = 0;
};

// ═════════════════════════════════════════════════════════════
//  Init / Destroy
// ═════════════════════════════════════════════════════════════

void T8Profiler::Init(BaseDriver* driver, int maxScopes) {
  m_driver = driver;
  m_maxScopes = maxScopes;
  m_frameQueries.resize(maxScopes);

  // CPU frequency
#ifdef OS_WINDOWS
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  m_cpuFreq = freq.QuadPart;
#endif

  // Detect API
  if (driver->m_currentAPI == GRAPHICS_API::D3D12) {
    m_apiType = 1;
    InitGPU_D3D12();
  } else if (driver->m_currentAPI == GRAPHICS_API::D3D11) {
    m_apiType = 2;
    InitGPU_D3D11();
  } else if (driver->m_currentAPI == GRAPHICS_API::OPENGL) {
    m_apiType = 3;
    InitGPU_GL();
  }

  m_initialized = true;
  T8_LOG_INFO("[Profiler] Initialized (API=%d, maxScopes=%d)", m_apiType, maxScopes);
}

T8Profiler::~T8Profiler() {
  Destroy();
}

void T8Profiler::Destroy() {
  if (!m_initialized) return;
  DestroyGPU();
  m_initialized = false;
}

// ═════════════════════════════════════════════════════════════
//  D3D12 GPU Backend
// ═════════════════════════════════════════════════════════════

void T8Profiler::InitGPU_D3D12() {
#ifdef OS_WINDOWS
  auto* state = new D3D12ProfileState();
  state->maxQueries = m_maxScopes * 2;  // begin + end per scope

  auto* d3d12Drv = static_cast<D3D12Driver*>(m_driver);
  auto* d3dDev   = reinterpret_cast<ID3D12Device*>(
                     T8Device->GetAPIObject());
  auto* cmdQueue = d3d12Drv->GetCmdQueue();

  // Get GPU timestamp frequency
  cmdQueue->GetTimestampFrequency(&state->gpuFrequency);

  // Create query heap (enough for kFrameDelay frames)
  D3D12_QUERY_HEAP_DESC heapDesc = {};
  heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  heapDesc.Count = state->maxQueries * D3D12ProfileState::kFrameDelay;
  heapDesc.NodeMask = 0;
  d3dDev->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&state->queryHeap));

  // Create readback buffer for resolved timestamps
  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC bufDesc = {};
  bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufDesc.Width = sizeof(uint64_t) * heapDesc.Count;
  bufDesc.Height = 1;
  bufDesc.DepthOrArraySize = 1;
  bufDesc.MipLevels = 1;
  bufDesc.SampleDesc.Count = 1;
  bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  d3dDev->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
    &bufDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
    IID_PPV_ARGS(&state->readbackBuffer));

  T8_LOG_INFO("[Profiler] D3D12 GPU timestamp freq: %llu Hz", state->gpuFrequency);
  m_gpuState = state;
#endif
}

void T8Profiler::InitGPU_D3D11() {
#ifdef OS_WINDOWS
  auto* state = new D3D11ProfileState();
  state->maxQueries = m_maxScopes;

  auto* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());

  for (int s = 0; s < 2; s++) {
    state->querySets[s].resize(m_maxScopes);
    for (int i = 0; i < m_maxScopes; i++) {
      D3D11_QUERY_DESC qd = {};
      qd.Query = D3D11_QUERY_TIMESTAMP;
      device->CreateQuery(&qd, &state->querySets[s][i].begin);
      device->CreateQuery(&qd, &state->querySets[s][i].end);

      D3D11_QUERY_DESC dd = {};
      dd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
      device->CreateQuery(&dd, &state->querySets[s][i].disjoint);
    }
  }

  T8_LOG_INFO("[Profiler] D3D11 GPU timestamp queries created (%d pairs x2)", m_maxScopes);
  m_gpuState = state;
#endif
}

void T8Profiler::InitGPU_GL() {
  auto* state = new GLProfileState();
  state->maxQueries = m_maxScopes;

#if defined(USING_OPENGL)
  for (int s = 0; s < 2; s++) {
    state->querySets[s].resize(m_maxScopes);
    for (int i = 0; i < m_maxScopes; i++) {
      glGenQueries(1, &state->querySets[s][i].beginQuery);
      glGenQueries(1, &state->querySets[s][i].endQuery);
    }
  }
  T8_LOG_INFO("[Profiler] GL GPU timestamp queries created (%d pairs x2)", m_maxScopes);
#endif
  m_gpuState = state;
}

void T8Profiler::DestroyGPU() {
#ifdef OS_WINDOWS
  if (m_apiType == 1) delete static_cast<D3D12ProfileState*>(m_gpuState);
  if (m_apiType == 2) delete static_cast<D3D11ProfileState*>(m_gpuState);
#endif
  if (m_apiType == 3) {
    auto* state = static_cast<GLProfileState*>(m_gpuState);
#if defined(USING_OPENGL)
    for (int s = 0; s < 2; s++)
      for (auto& qp : state->querySets[s]) {
        if (qp.beginQuery) glDeleteQueries(1, &qp.beginQuery);
        if (qp.endQuery)   glDeleteQueries(1, &qp.endQuery);
      }
#endif
    delete state;
  }
  m_gpuState = nullptr;
}

// ═════════════════════════════════════════════════════════════
//  Frame Boundary
// ═════════════════════════════════════════════════════════════

void T8Profiler::BeginFrame() {
  if (!m_initialized) return;

  // Resolve GPU results from previous frame(s)
  ResolveGPUFrame();

  m_activeQueryCount = 0;

#ifdef OS_WINDOWS
  if (m_apiType == 2) {
    // D3D11: flip write set
    auto* state = static_cast<D3D11ProfileState*>(m_gpuState);
    state->writeSet = 1 - state->writeSet;
  }
#endif
  if (m_apiType == 3) {
    auto* state = static_cast<GLProfileState*>(m_gpuState);
    state->writeSet = 1 - state->writeSet;
  }
}

void T8Profiler::EndFrame() {
  if (!m_initialized) return;

#ifdef OS_WINDOWS
  if (m_apiType == 1) {
    // D3D12: save scope mappings for this frame, then resolve timestamps
    auto* state = static_cast<D3D12ProfileState*>(m_gpuState);
    int frameSlot = state->writeFrame % D3D12ProfileState::kFrameDelay;

    // Store scope indices for later readback
    auto& rec = state->frameRecords[frameSlot];
    rec.activeCount = m_activeQueryCount;
    for (int i = 0; i < m_activeQueryCount && i < 64; i++)
      rec.scopeIndices[i] = m_frameQueries[i].scopeIndex;

    auto* cmdList = static_cast<D3D12Driver*>(m_driver)->GetCmdList();
    int baseQuery = frameSlot * state->maxQueries;
    int queryCount = m_activeQueryCount * 2;
    if (queryCount > 0) {
      cmdList->ResolveQueryData(state->queryHeap.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        baseQuery, queryCount,
        state->readbackBuffer.Get(),
        baseQuery * sizeof(uint64_t));
    }
    state->writeFrame++;
  }
#endif

  m_frameCount++;
}

// ═════════════════════════════════════════════════════════════
//  Scope Begin / End
// ═════════════════════════════════════════════════════════════

int T8Profiler::FindOrCreateScope(const char* name) {
  for (int i = 0; i < (int)m_scopes.size(); i++) {
    if (m_scopes[i].name == name) return i;
  }
  ProfileScope s;
  s.name = name;
  m_scopes.push_back(s);
  return (int)m_scopes.size() - 1;
}

void T8Profiler::BeginScope(const char* name) {
  if (!m_initialized) return;
  if (m_activeQueryCount >= m_maxScopes) return;

  int queryIdx = m_activeQueryCount;
  int scopeIdx = FindOrCreateScope(name);

  m_frameQueries[queryIdx].scopeIndex = scopeIdx;

  // CPU timestamp
#ifdef OS_WINDOWS
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  m_frameQueries[queryIdx].cpuBegin = now.QuadPart;
#endif

  // GPU timestamp
  BeginGPUScope(queryIdx);

  m_activeQueryCount++;
}

void T8Profiler::EndScope() {
  if (!m_initialized) return;
  if (m_activeQueryCount <= 0) return;

  int queryIdx = m_activeQueryCount - 1;

  // CPU timestamp
#ifdef OS_WINDOWS
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  m_frameQueries[queryIdx].cpuEnd = now.QuadPart;
#endif

  // GPU timestamp
  EndGPUScope(queryIdx);

  // Accumulate CPU time immediately
  auto& fq = m_frameQueries[queryIdx];
  if (fq.scopeIndex >= 0 && fq.scopeIndex < (int)m_scopes.size()) {
    double cpuMs = (double)(fq.cpuEnd - fq.cpuBegin) * 1000.0 / (double)m_cpuFreq;
    m_scopes[fq.scopeIndex].cpuTotalMs += cpuMs;
    // sampleCount incremented when GPU result arrives (or if no GPU)
    if (m_apiType == 0) {
      m_scopes[fq.scopeIndex].sampleCount++;
    }
  }
}

// ═════════════════════════════════════════════════════════════
//  GPU Scope Implementation
// ═════════════════════════════════════════════════════════════

void T8Profiler::BeginGPUScope(int queryIndex) {
#ifdef OS_WINDOWS
  if (m_apiType == 1) {
    auto* state = static_cast<D3D12ProfileState*>(m_gpuState);
    auto* cmdList = static_cast<D3D12Driver*>(m_driver)->GetCmdList();
    int frameSlot = state->writeFrame % D3D12ProfileState::kFrameDelay;
    int slot = frameSlot * state->maxQueries + queryIndex * 2;
    cmdList->EndQuery(state->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot);
  }
  else if (m_apiType == 2) {
    auto* state = static_cast<D3D11ProfileState*>(m_gpuState);
    auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(
        T8DeviceContext->GetAPIObject());
    auto& qp = state->querySets[state->writeSet][queryIndex];
    ctx->Begin(qp.disjoint.Get());
    ctx->End(qp.begin.Get());
  }
#endif
#if defined(USING_OPENGL)
  if (m_apiType == 3) {
    auto* state = static_cast<GLProfileState*>(m_gpuState);
    glQueryCounter(state->querySets[state->writeSet][queryIndex].beginQuery, GL_TIMESTAMP);
  }
#endif
}

void T8Profiler::EndGPUScope(int queryIndex) {
#ifdef OS_WINDOWS
  if (m_apiType == 1) {
    auto* state = static_cast<D3D12ProfileState*>(m_gpuState);
    auto* cmdList = static_cast<D3D12Driver*>(m_driver)->GetCmdList();
    int frameSlot = state->writeFrame % D3D12ProfileState::kFrameDelay;
    int slot = frameSlot * state->maxQueries + queryIndex * 2 + 1;
    cmdList->EndQuery(state->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot);
  }
  else if (m_apiType == 2) {
    auto* state = static_cast<D3D11ProfileState*>(m_gpuState);
    auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(
        T8DeviceContext->GetAPIObject());
    auto& qp = state->querySets[state->writeSet][queryIndex];
    ctx->End(qp.end.Get());
    ctx->End(qp.disjoint.Get());
    qp.pending = true;
  }
#endif
#if defined(USING_OPENGL)
  if (m_apiType == 3) {
    auto* state = static_cast<GLProfileState*>(m_gpuState);
    glQueryCounter(state->querySets[state->writeSet][queryIndex].endQuery, GL_TIMESTAMP);
  }
#endif
}

// ═════════════════════════════════════════════════════════════
//  Resolve GPU Results
// ═════════════════════════════════════════════════════════════

void T8Profiler::ResolveGPUFrame() {
#ifdef OS_WINDOWS
  if (m_apiType == 1 && m_gpuState) {
    auto* state = static_cast<D3D12ProfileState*>(m_gpuState);
    // Read results from kFrameDelay frames ago
    int readFrame = state->writeFrame - D3D12ProfileState::kFrameDelay;
    if (readFrame < 0) return;

    int frameSlot = readFrame % D3D12ProfileState::kFrameDelay;
    auto& rec = state->frameRecords[frameSlot];
    int baseOffset = frameSlot * state->maxQueries;

    // Map readback buffer
    uint64_t* data = nullptr;
    D3D12_RANGE range = { (SIZE_T)(baseOffset * sizeof(uint64_t)),
                          (SIZE_T)((baseOffset + rec.activeCount * 2) * sizeof(uint64_t)) };
    if (SUCCEEDED(state->readbackBuffer->Map(0, &range, (void**)&data))) {
      for (int i = 0; i < rec.activeCount && i < 64; i++) {
        int scopeIdx = rec.scopeIndices[i];
        if (scopeIdx < 0 || scopeIdx >= (int)m_scopes.size()) continue;
        uint64_t begin = data[baseOffset + i * 2];
        uint64_t end   = data[baseOffset + i * 2 + 1];
        double gpuMs = (double)(end - begin) * 1000.0 / (double)state->gpuFrequency;
        m_scopes[scopeIdx].gpuTotalMs += gpuMs;
        m_scopes[scopeIdx].sampleCount++;
      }
      D3D12_RANGE written = {0, 0};
      state->readbackBuffer->Unmap(0, &written);
    }
  }
  else if (m_apiType == 2 && m_gpuState) {
    auto* state = static_cast<D3D11ProfileState*>(m_gpuState);
    auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(
        T8DeviceContext->GetAPIObject());
    int readSet = 1 - state->writeSet;

    for (int i = 0; i < state->maxQueries; i++) {
      auto& qp = state->querySets[readSet][i];
      if (!qp.pending) continue;

      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
      if (ctx->GetData(qp.disjoint.Get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
        continue;
      if (disjointData.Disjoint) { qp.pending = false; continue; }

      UINT64 tsBegin = 0, tsEnd = 0;
      if (ctx->GetData(qp.begin.Get(), &tsBegin, sizeof(tsBegin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
      if (ctx->GetData(qp.end.Get(), &tsEnd, sizeof(tsEnd), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;

      if (i < (int)m_frameQueries.size() && m_frameQueries[i].scopeIndex >= 0) {
        double gpuMs = (double)(tsEnd - tsBegin) * 1000.0 / (double)disjointData.Frequency;
        m_scopes[m_frameQueries[i].scopeIndex].gpuTotalMs += gpuMs;
        m_scopes[m_frameQueries[i].scopeIndex].sampleCount++;
      }
      qp.pending = false;
    }
  }
#endif

#if defined(USING_OPENGL)
  if (m_apiType == 3 && m_gpuState) {
    auto* state = static_cast<GLProfileState*>(m_gpuState);
    int readSet = 1 - state->writeSet;

    for (int i = 0; i < m_activeQueryCount && i < state->maxQueries; i++) {
      auto& qp = state->querySets[readSet][i];
      GLuint64 tsBegin = 0, tsEnd = 0;
      GLint available = 0;
      glGetQueryObjectiv(qp.endQuery, GL_QUERY_RESULT_AVAILABLE, &available);
      if (!available) continue;
      glGetQueryObjectui64v(qp.beginQuery, GL_QUERY_RESULT, &tsBegin);
      glGetQueryObjectui64v(qp.endQuery, GL_QUERY_RESULT, &tsEnd);

      if (i < (int)m_frameQueries.size() && m_frameQueries[i].scopeIndex >= 0) {
        double gpuMs = (double)(tsEnd - tsBegin) / 1000000.0; // ns -> ms
        m_scopes[m_frameQueries[i].scopeIndex].gpuTotalMs += gpuMs;
        m_scopes[m_frameQueries[i].scopeIndex].sampleCount++;
      }
    }
  }
#endif
}

// ═════════════════════════════════════════════════════════════
//  Reporting
// ═════════════════════════════════════════════════════════════

void T8Profiler::Report(int topN) const {
  T8_LOG_INFO("╔══════════════════════════════════════════════════════════════╗");
  T8_LOG_INFO("║  PROFILER REPORT  (%d frames)                               ║", m_frameCount);
  T8_LOG_INFO("╠═══════════════════════════════╦═══════════╦═══════════╦═════╣");
  T8_LOG_INFO("║ Scope                         ║  GPU avg  ║  CPU avg  ║  N  ║");
  T8_LOG_INFO("╠═══════════════════════════════╬═══════════╬═══════════╬═════╣");

  // Sort by GPU time descending
  std::vector<int> order(m_scopes.size());
  for (int i = 0; i < (int)order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return m_scopes[a].GpuAvgMs() > m_scopes[b].GpuAvgMs();
  });

  double totalGpu = 0, totalCpu = 0;
  int count = topN > 0 ? (std::min)(topN, (int)order.size()) : (int)order.size();
  for (int i = 0; i < count; i++) {
    auto& s = m_scopes[order[i]];
    T8_LOG_INFO("║ %-29s ║ %7.3fms ║ %7.3fms ║ %3d ║",
      s.name.c_str(), s.GpuAvgMs(), s.CpuAvgMs(), s.sampleCount);
    totalGpu += s.GpuAvgMs();
    totalCpu += s.CpuAvgMs();
  }

  T8_LOG_INFO("╠═══════════════════════════════╬═══════════╬═══════════╬═════╣");
  T8_LOG_INFO("║ TOTAL                         ║ %7.3fms ║ %7.3fms ║     ║", totalGpu, totalCpu);
  T8_LOG_INFO("╚═══════════════════════════════╩═══════════╩═══════════╩═════╝");
}

void T8Profiler::Reset() {
  m_scopes.clear();
  m_frameCount = 0;
}

} // namespace t800
