#include <pch.h>
#include <debug/T8_Profiler.h>

#ifdef T8_ENABLE_PROFILER

#include <video/BaseDriver.h>
#include <utils/Log.h>

#include <algorithm>
#include <cstring>
#include <cstdio>

#ifdef OS_WINDOWS
#include <video/d3d12/D3D12Driver.h>
#include <video/d3d11/D3D11Driver.h>
#include <video/vulkan/VulkanDriver.h>
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

struct VulkanProfileState {
  VkQueryPool queryPool = VK_NULL_HANDLE;
  VkBuffer    readbackBuffer = VK_NULL_HANDLE;
  VmaAllocation readbackAllocation = VK_NULL_HANDLE;
  uint64_t*   mappedData = nullptr;
  float       timestampPeriod = 0.0f;  // nanoseconds per tick
  int         maxQueries = 0;          // 2 per scope (begin + end)
  int         writeFrame = 0;
  bool        needsReset = false;      // deferred reset flag
  bool        hasCalibratedTimestamps = false;
  static const int kFrameDelay = 3;    // match kBackBufferCount
  struct FrameRecord {
    int activeCount = 0;
    int scopeIndices[64] = {};
    bool cpuOnly[64] = {};  // true if scope had no GPU timestamp
  };
  FrameRecord frameRecords[kFrameDelay];
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
  } else if (driver->m_currentAPI == GRAPHICS_API::VULKAN) {
    m_apiType = 4;
    InitGPU_Vulkan();
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

void T8Profiler::InitGPU_Vulkan() {
#ifdef OS_WINDOWS
  auto* state = new VulkanProfileState();
  state->maxQueries = m_maxScopes * 2;  // begin + end per scope

  auto* vkDrv = static_cast<VulkanDriver*>(m_driver);
  VkDevice device = vkDrv->GetDevice();
  VkPhysicalDevice physDevice = vkDrv->GetPhysicalDevice();

  // Query timestamp period (nanoseconds per tick)
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physDevice, &props);
  state->timestampPeriod = props.limits.timestampPeriod;

  // Create query pool: maxQueries * kFrameDelay slots
  int totalSlots = state->maxQueries * VulkanProfileState::kFrameDelay;
  VkQueryPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
  poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
  poolInfo.queryCount = totalSlots;
  vkCreateQueryPool(device, &poolInfo, nullptr, &state->queryPool);

  // Reset all queries at init (device-level reset via command buffer)
  // Queries will be reset per-frame in BeginFrame.

  T8_LOG_INFO("[Profiler] Vulkan GPU timestamp period: %.3f ns/tick (%d query slots)",
              state->timestampPeriod, totalSlots);

  // Check calibrated timestamp support
  auto vkGetCalibratedTimestampsEXT = (PFN_vkGetCalibratedTimestampsEXT)
      vkGetDeviceProcAddr(device, "vkGetCalibratedTimestampsEXT");
  if (vkGetCalibratedTimestampsEXT) {
    auto vkGetPhysicalDeviceCalibrateableTimeDomainsEXT = (PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT)
        vkGetInstanceProcAddr(vkDrv->GetInstance(), "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT");
    if (vkGetPhysicalDeviceCalibrateableTimeDomainsEXT) {
      uint32_t domainCount = 0;
      vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(vkDrv->GetPhysicalDevice(), &domainCount, nullptr);
      std::vector<VkTimeDomainEXT> domains(domainCount);
      vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(vkDrv->GetPhysicalDevice(), &domainCount, domains.data());

      bool hasDevice = false, hasQPC = false;
      for (auto d : domains) {
        if (d == VK_TIME_DOMAIN_DEVICE_EXT) hasDevice = true;
        if (d == VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT) hasQPC = true;
      }

      if (hasDevice && hasQPC) {
        VkCalibratedTimestampInfoEXT infos[2] = {};
        infos[0].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
        infos[0].timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;
        infos[1].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
        infos[1].timeDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;

        uint64_t timestamps[2];
        uint64_t maxDeviation;
        vkGetCalibratedTimestampsEXT(device, 2, infos, timestamps, &maxDeviation);

        T8_LOG_INFO("[Profiler] Calibrated timestamps: GPU=%llu QPC=%llu deviation=%llu ns",
                    timestamps[0], timestamps[1], maxDeviation);
        T8_LOG_INFO("[Profiler] Max deviation: %.3f us", maxDeviation * state->timestampPeriod / 1000.0);
      }
    }
    state->hasCalibratedTimestamps = true;
  } else {
    T8_LOG_INFO("[Profiler] vkGetCalibratedTimestampsEXT not available");
  }

  m_gpuState = state;
#endif
}

void T8Profiler::DestroyGPU() {
#ifdef OS_WINDOWS
  if (m_apiType == 1) delete static_cast<D3D12ProfileState*>(m_gpuState);
  if (m_apiType == 2) delete static_cast<D3D11ProfileState*>(m_gpuState);
  if (m_apiType == 4) {
    auto* state = static_cast<VulkanProfileState*>(m_gpuState);
    auto* vkDrv = static_cast<VulkanDriver*>(m_driver);
    VkDevice device = vkDrv->GetDevice();
    if (state->queryPool) vkDestroyQueryPool(device, state->queryPool, nullptr);
    delete state;
  }
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
  else if (m_apiType == 4) {
    // Vulkan: mark that query pool needs reset (flushed from VulkanDriver::BeginFrame)
    auto* state = static_cast<VulkanProfileState*>(m_gpuState);
    state->needsReset = true;
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
  else if (m_apiType == 4) {
    // Vulkan: save scope mappings for this frame
    auto* state = static_cast<VulkanProfileState*>(m_gpuState);
    int frameSlot = state->writeFrame % VulkanProfileState::kFrameDelay;

    auto& rec = state->frameRecords[frameSlot];
    rec.activeCount = m_activeQueryCount;
    for (int i = 0; i < m_activeQueryCount && i < 64; i++) {
      rec.scopeIndices[i] = m_frameQueries[i].scopeIndex;
      rec.cpuOnly[i] = m_frameQueries[i].cpuOnly;
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
  m_frameQueries[queryIdx].cpuOnly = false;

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

// CPU-only scope (no GPU timestamp — safe to call outside command list recording)
void T8Profiler::BeginCPUScope(const char* name) {
  if (!m_initialized) return;
  if (m_activeQueryCount >= m_maxScopes) return;

  int queryIdx = m_activeQueryCount;
  int scopeIdx = FindOrCreateScope(name);

  m_frameQueries[queryIdx].scopeIndex = scopeIdx;
  m_frameQueries[queryIdx].cpuOnly = true;

#ifdef OS_WINDOWS
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  m_frameQueries[queryIdx].cpuBegin = now.QuadPart;
#endif

  m_activeQueryCount++;
}

void T8Profiler::EndCPUScope() {
  if (!m_initialized) return;
  if (m_activeQueryCount <= 0) return;

  int queryIdx = m_activeQueryCount - 1;

#ifdef OS_WINDOWS
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  m_frameQueries[queryIdx].cpuEnd = now.QuadPart;
#endif

  auto& fq = m_frameQueries[queryIdx];
  if (fq.scopeIndex >= 0 && fq.scopeIndex < (int)m_scopes.size()) {
    double cpuMs = (double)(fq.cpuEnd - fq.cpuBegin) * 1000.0 / (double)m_cpuFreq;
    m_scopes[fq.scopeIndex].cpuTotalMs += cpuMs;
    m_scopes[fq.scopeIndex].sampleCount++;
  }
}

void T8Profiler::AddDrawCall(int vertexCount) {
  if (!m_initialized || m_activeQueryCount <= 0) return;
  int queryIdx = m_activeQueryCount - 1;
  auto& fq = m_frameQueries[queryIdx];
  if (fq.scopeIndex >= 0 && fq.scopeIndex < (int)m_scopes.size()) {
    m_scopes[fq.scopeIndex].drawCalls++;
    m_scopes[fq.scopeIndex].triangles += vertexCount / 3;
  }
}

void T8Profiler::FlushVulkanQueryReset(void* commandBuffer) {
#ifdef OS_WINDOWS
  if (m_apiType == 4 && m_gpuState) {
    auto* state = static_cast<VulkanProfileState*>(m_gpuState);
    if (state->needsReset) {
      VkCommandBuffer cmd = static_cast<VkCommandBuffer>(commandBuffer);
      int frameSlot = state->writeFrame % VulkanProfileState::kFrameDelay;
      int baseQuery = frameSlot * state->maxQueries;
      vkCmdResetQueryPool(cmd, state->queryPool, baseQuery, state->maxQueries);
      state->needsReset = false;
    }
  }
#endif
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
  else if (m_apiType == 4) {
    auto* state = static_cast<VulkanProfileState*>(m_gpuState);
    auto* vkCtx = static_cast<VulkanDeviceContext*>(T8DeviceContext);
    VkCommandBuffer cmd = vkCtx->GetCommandBuffer();
    int frameSlot = state->writeFrame % VulkanProfileState::kFrameDelay;
    int slot = frameSlot * state->maxQueries + queryIndex * 2;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, state->queryPool, slot);
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
  else if (m_apiType == 4) {
    auto* state = static_cast<VulkanProfileState*>(m_gpuState);
    auto* vkCtx = static_cast<VulkanDeviceContext*>(T8DeviceContext);
    VkCommandBuffer cmd = vkCtx->GetCommandBuffer();
    int frameSlot = state->writeFrame % VulkanProfileState::kFrameDelay;
    int slot = frameSlot * state->maxQueries + queryIndex * 2 + 1;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, state->queryPool, slot);
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
  else if (m_apiType == 4 && m_gpuState) {
    auto* state = static_cast<VulkanProfileState*>(m_gpuState);
    int readFrame = state->writeFrame - VulkanProfileState::kFrameDelay;
    if (readFrame < 0) return;

    int frameSlot = readFrame % VulkanProfileState::kFrameDelay;
    auto& rec = state->frameRecords[frameSlot];
    if (rec.activeCount <= 0) return;

    VkDevice device = static_cast<VulkanDriver*>(m_driver)->GetDevice();
    int baseQuery = frameSlot * state->maxQueries;

    // Read per-scope, skipping CPU-only scopes (no GPU timestamps written)
    for (int i = 0; i < rec.activeCount && i < 64; i++) {
      if (rec.cpuOnly[i]) continue;
      int scopeIdx = rec.scopeIndices[i];
      if (scopeIdx < 0 || scopeIdx >= (int)m_scopes.size()) continue;

      uint64_t timestamps[2] = {};
      int queryStart = baseQuery + i * 2;
      VkResult res = vkGetQueryPoolResults(device, state->queryPool,
        queryStart, 2,
        sizeof(timestamps), timestamps,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

      if (res == VK_SUCCESS) {
        double gpuMs = (double)(timestamps[1] - timestamps[0]) * (double)state->timestampPeriod / 1000000.0;
        m_scopes[scopeIdx].gpuTotalMs += gpuMs;
        m_scopes[scopeIdx].sampleCount++;
      }
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
  T8_LOG_INFO("╔══════════════════════════════════════════════════════════════════════════════╗");
  T8_LOG_INFO("║  PROFILER REPORT  (%d frames)                                               ║", m_frameCount);
  T8_LOG_INFO("╠═══════════════════════════════╦═══════════╦═══════════╦═══════╦════════╦═════╣");
  T8_LOG_INFO("║ Scope                         ║  GPU avg  ║  CPU avg  ║ Draws ║  Tris  ║  N  ║");
  T8_LOG_INFO("╠═══════════════════════════════╬═══════════╬═══════════╬═══════╬════════╬═════╣");

  // Sort by GPU time descending
  std::vector<int> order(m_scopes.size());
  for (int i = 0; i < (int)order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return m_scopes[a].GpuAvgMs() > m_scopes[b].GpuAvgMs();
  });

  double totalGpu = 0, totalCpu = 0;
  int totalDraws = 0, totalTris = 0;
  int count = topN > 0 ? (std::min)(topN, (int)order.size()) : (int)order.size();
  for (int i = 0; i < count; i++) {
    auto& s = m_scopes[order[i]];
    T8_LOG_INFO("║ %-29s ║ %7.3fms ║ %7.3fms ║ %5d ║ %6d ║ %3d ║",
      s.name.c_str(), s.GpuAvgMs(), s.CpuAvgMs(), s.drawCalls, s.triangles, s.sampleCount);
    totalGpu += s.GpuAvgMs();
    totalCpu += s.CpuAvgMs();
    totalDraws += s.drawCalls;
    totalTris += s.triangles;
  }

  T8_LOG_INFO("╠═══════════════════════════════╬═══════════╬═══════════╬═══════╬════════╬═════╣");
  T8_LOG_INFO("║ TOTAL                         ║ %7.3fms ║ %7.3fms ║ %5d ║ %6d ║     ║", totalGpu, totalCpu, totalDraws, totalTris);
  T8_LOG_INFO("╚═══════════════════════════════╩═══════════╩═══════════╩═══════╩════════╩═════╝");
}

void T8Profiler::Reset() {
  m_scopes.clear();
  m_frameCount = 0;
}

} // namespace t800

#endif // T8_ENABLE_PROFILER
