#include <pch.h>

#include <debug/Profiler.h>
#include <utils/Log.h>
#include <video/BaseDriver.h>

#include <algorithm>
#include <vector>

#ifdef OS_WINDOWS
#include <d3d11.h>
#include <d3d12.h>
#include <video/d3d12/D3D12Driver.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#if defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX)
#include <video/vulkan/VulkanDeviceContext.h>
#include <video/vulkan/VulkanDriver.h>
#endif

#if defined(USING_OPENGL)
#include <GL/glew.h>
#endif

namespace t850 {

extern Device* T8Device;
extern DeviceContext* T8DeviceContext;

namespace {

#ifdef OS_WINDOWS
class D3D12ProfilerBackend final : public ProfilerGpuBackend {
public:
  bool Init(BaseDriver* driver, int maxScopes) override {
    if (!driver || driver->m_currentAPI != GraphicsApi::D3D12 || !T8Device) return false;
    m_driver = static_cast<D3D12Driver*>(driver);
    m_maxQueries = maxScopes * 2;
    m_records.resize(kFrameDelay);
    for (FrameRecord& record : m_records) {
      record.scopeIndices.resize(maxScopes, -1);
      record.cpuOnly.resize(maxScopes, false);
    }

    auto* device = reinterpret_cast<ID3D12Device*>(T8Device->GetAPIObject());
    if (FAILED(m_driver->GetCmdQueue()->GetTimestampFrequency(&m_gpuFrequency))) return false;

    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = m_maxQueries * kFrameDelay;
    if (FAILED(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_queryHeap)))) return false;

    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(uint64_t) * heapDesc.Count;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(
          &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readbackBuffer)))) return false;

    T8_LOG_INFO("[Profiler] D3D12 GPU timestamp freq: %llu Hz", m_gpuFrequency);
    return true;
  }

  const char* Name() const override { return "d3d12"; }
  void BeginFrame() override {}

  void BeginScope(int queryIndex) override {
    const int frameSlot = m_writeFrame % kFrameDelay;
    const int slot = frameSlot * m_maxQueries + queryIndex * 2;
    m_driver->GetCmdList()->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot);
  }

  void EndScope(int queryIndex) override {
    const int frameSlot = m_writeFrame % kFrameDelay;
    const int slot = frameSlot * m_maxQueries + queryIndex * 2 + 1;
    m_driver->GetCmdList()->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot);
  }

  void EndFrame(int activeQueryCount,
                const std::vector<ProfileFrameQuery>& frameQueries) override {
    const int frameSlot = m_writeFrame % kFrameDelay;
    FrameRecord& record = m_records[frameSlot];
    record.activeCount = activeQueryCount;
    for (int index = 0; index < activeQueryCount; ++index) {
      record.scopeIndices[index] = frameQueries[index].scopeIndex;
      record.cpuOnly[index] = frameQueries[index].cpuOnly;
    }

    const int queryCount = activeQueryCount * 2;
    if (queryCount > 0) {
      const int baseQuery = frameSlot * m_maxQueries;
      m_driver->GetCmdList()->ResolveQueryData(
        m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        baseQuery, queryCount, m_readbackBuffer.Get(),
        baseQuery * sizeof(uint64_t));
    }
    ++m_writeFrame;
  }

  void Resolve(std::vector<ProfileScope>& scopes) override {
    const int readFrame = m_writeFrame - kFrameDelay;
    if (readFrame < 0) return;
    const int frameSlot = readFrame % kFrameDelay;
    const FrameRecord& record = m_records[frameSlot];
    const int baseOffset = frameSlot * m_maxQueries;
    uint64_t* data = nullptr;
    D3D12_RANGE range = {
      static_cast<SIZE_T>(baseOffset * sizeof(uint64_t)),
      static_cast<SIZE_T>((baseOffset + record.activeCount * 2) * sizeof(uint64_t))};
    if (FAILED(m_readbackBuffer->Map(0, &range, reinterpret_cast<void**>(&data)))) return;

    for (int index = 0; index < record.activeCount; ++index) {
      if (record.cpuOnly[index]) continue;
      const int scopeIndex = record.scopeIndices[index];
      if (scopeIndex < 0 || scopeIndex >= static_cast<int>(scopes.size())) continue;
      const uint64_t begin = data[baseOffset + index * 2];
      const uint64_t end = data[baseOffset + index * 2 + 1];
      scopes[scopeIndex].gpuTotalMs +=
        static_cast<double>(end - begin) * 1000.0 / static_cast<double>(m_gpuFrequency);
      ++scopes[scopeIndex].sampleCount;
    }
    D3D12_RANGE written = {0, 0};
    m_readbackBuffer->Unmap(0, &written);
  }

private:
  struct FrameRecord {
    int activeCount = 0;
    std::vector<int> scopeIndices;
    std::vector<bool> cpuOnly;
  };
  static constexpr int kFrameDelay = 3;
  D3D12Driver* m_driver = nullptr;
  ComPtr<ID3D12QueryHeap> m_queryHeap;
  ComPtr<ID3D12Resource> m_readbackBuffer;
  std::vector<FrameRecord> m_records;
  uint64_t m_gpuFrequency = 0;
  int m_maxQueries = 0;
  int m_writeFrame = 0;
};

class D3D11ProfilerBackend final : public ProfilerGpuBackend {
public:
  bool Init(BaseDriver* driver, int maxScopes) override {
    if (!driver || driver->m_currentAPI != GraphicsApi::D3D11 ||
        !T8Device || !T8DeviceContext) return false;
    m_maxQueries = maxScopes;
    auto* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    for (auto& querySet : m_querySets) {
      querySet.resize(maxScopes);
      for (QueryPair& pair : querySet) {
        D3D11_QUERY_DESC timestamp = {};
        timestamp.Query = D3D11_QUERY_TIMESTAMP;
        if (FAILED(device->CreateQuery(&timestamp, &pair.begin)) ||
            FAILED(device->CreateQuery(&timestamp, &pair.end))) return false;
        D3D11_QUERY_DESC disjoint = {};
        disjoint.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        if (FAILED(device->CreateQuery(&disjoint, &pair.disjoint))) return false;
      }
    }
    T8_LOG_INFO("[Profiler] D3D11 GPU timestamp queries created (%d pairs x2)", maxScopes);
    return true;
  }

  const char* Name() const override { return "d3d11"; }
  void BeginFrame() override { m_writeSet = 1 - m_writeSet; }

  void BeginScope(int queryIndex) override {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    QueryPair& pair = m_querySets[m_writeSet][queryIndex];
    context->Begin(pair.disjoint.Get());
    context->End(pair.begin.Get());
  }

  void EndScope(int queryIndex) override {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    QueryPair& pair = m_querySets[m_writeSet][queryIndex];
    context->End(pair.end.Get());
    context->End(pair.disjoint.Get());
    pair.pending = true;
  }

  void EndFrame(int activeQueryCount,
                const std::vector<ProfileFrameQuery>& frameQueries) override {
    for (int index = 0; index < activeQueryCount; ++index) {
      QueryPair& pair = m_querySets[m_writeSet][index];
      pair.scopeIndex = frameQueries[index].scopeIndex;
      if (frameQueries[index].cpuOnly) pair.pending = false;
    }
  }

  void Resolve(std::vector<ProfileScope>& scopes) override {
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    const int readSet = 1 - m_writeSet;
    for (QueryPair& pair : m_querySets[readSet]) {
      if (!pair.pending) continue;
      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData = {};
      if (context->GetData(pair.disjoint.Get(), &disjointData, sizeof(disjointData),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
      if (disjointData.Disjoint) { pair.pending = false; continue; }
      UINT64 begin = 0;
      UINT64 end = 0;
      if (context->GetData(pair.begin.Get(), &begin, sizeof(begin),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
          context->GetData(pair.end.Get(), &end, sizeof(end),
                           D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
      if (pair.scopeIndex >= 0 && pair.scopeIndex < static_cast<int>(scopes.size())) {
        scopes[pair.scopeIndex].gpuTotalMs +=
          static_cast<double>(end - begin) * 1000.0 / static_cast<double>(disjointData.Frequency);
        ++scopes[pair.scopeIndex].sampleCount;
      }
      pair.pending = false;
    }
  }

private:
  struct QueryPair {
    ComPtr<ID3D11Query> begin;
    ComPtr<ID3D11Query> end;
    ComPtr<ID3D11Query> disjoint;
    int scopeIndex = -1;
    bool pending = false;
  };
  std::vector<QueryPair> m_querySets[2];
  int m_writeSet = 0;
  int m_maxQueries = 0;
};
#endif

#if defined(USING_OPENGL)
class OpenGLProfilerBackend final : public ProfilerGpuBackend {
public:
  ~OpenGLProfilerBackend() override {
    for (auto& querySet : m_querySets) {
      for (QueryPair& pair : querySet) {
        if (pair.beginQuery) glDeleteQueries(1, &pair.beginQuery);
        if (pair.endQuery) glDeleteQueries(1, &pair.endQuery);
      }
    }
  }

  bool Init(BaseDriver* driver, int maxScopes) override {
    if (!driver || driver->m_currentAPI != GraphicsApi::OPENGL) return false;
    m_maxQueries = maxScopes;
    for (auto& querySet : m_querySets) {
      querySet.resize(maxScopes);
      for (QueryPair& pair : querySet) {
        glGenQueries(1, &pair.beginQuery);
        glGenQueries(1, &pair.endQuery);
      }
    }
    T8_LOG_INFO("[Profiler] GL GPU timestamp queries created (%d pairs x2)", maxScopes);
    return true;
  }

  const char* Name() const override { return "gl"; }
  void BeginFrame() override { m_writeSet = 1 - m_writeSet; }
  void BeginScope(int queryIndex) override {
    glQueryCounter(m_querySets[m_writeSet][queryIndex].beginQuery, GL_TIMESTAMP);
  }
  void EndScope(int queryIndex) override {
    QueryPair& pair = m_querySets[m_writeSet][queryIndex];
    glQueryCounter(pair.endQuery, GL_TIMESTAMP);
    pair.pending = true;
  }
  void EndFrame(int activeQueryCount,
                const std::vector<ProfileFrameQuery>& frameQueries) override {
    for (int index = 0; index < activeQueryCount; ++index) {
      QueryPair& pair = m_querySets[m_writeSet][index];
      pair.scopeIndex = frameQueries[index].scopeIndex;
      if (frameQueries[index].cpuOnly) pair.pending = false;
    }
  }
  void Resolve(std::vector<ProfileScope>& scopes) override {
    const int readSet = 1 - m_writeSet;
    for (QueryPair& pair : m_querySets[readSet]) {
      if (!pair.pending) continue;
      GLint available = 0;
      glGetQueryObjectiv(pair.endQuery, GL_QUERY_RESULT_AVAILABLE, &available);
      if (!available) continue;
      GLuint64 begin = 0;
      GLuint64 end = 0;
      glGetQueryObjectui64v(pair.beginQuery, GL_QUERY_RESULT, &begin);
      glGetQueryObjectui64v(pair.endQuery, GL_QUERY_RESULT, &end);
      if (pair.scopeIndex >= 0 && pair.scopeIndex < static_cast<int>(scopes.size())) {
        scopes[pair.scopeIndex].gpuTotalMs += static_cast<double>(end - begin) / 1000000.0;
        ++scopes[pair.scopeIndex].sampleCount;
      }
      pair.pending = false;
    }
  }

private:
  struct QueryPair {
    GLuint beginQuery = 0;
    GLuint endQuery = 0;
    int scopeIndex = -1;
    bool pending = false;
  };
  std::vector<QueryPair> m_querySets[2];
  int m_writeSet = 0;
  int m_maxQueries = 0;
};
#endif

#if defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX)
class VulkanProfilerBackend final : public ProfilerGpuBackend {
public:
  ~VulkanProfilerBackend() override {
    if (m_driver && m_queryPool)
      vkDestroyQueryPool(m_driver->GetDevice(), m_queryPool, nullptr);
  }

  bool Init(BaseDriver* driver, int maxScopes) override {
    if (!driver || driver->m_currentAPI != GraphicsApi::VULKAN || !T8DeviceContext) return false;
    m_driver = static_cast<VulkanDriver*>(driver);
    m_maxQueries = maxScopes * 2;
    m_records.resize(kFrameDelay);
    for (FrameRecord& record : m_records) {
      record.scopeIndices.resize(maxScopes, -1);
      record.cpuOnly.resize(maxScopes, false);
    }

    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(m_driver->GetPhysicalDevice(), &properties);
    m_timestampPeriod = properties.limits.timestampPeriod;
    VkQueryPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = m_maxQueries * kFrameDelay;
    if (vkCreateQueryPool(m_driver->GetDevice(), &poolInfo, nullptr, &m_queryPool) != VK_SUCCESS)
      return false;

    T8_LOG_INFO("[Profiler] Vulkan GPU timestamp period: %.3f ns/tick (%d query slots)",
                m_timestampPeriod, poolInfo.queryCount);
    LogCalibratedTimestampSupport();
    return true;
  }

  const char* Name() const override { return "vulkan"; }
  void BeginFrame() override { m_needsReset = true; }

  void BeginScope(int queryIndex) override {
    auto* context = static_cast<VulkanDeviceContext*>(T8DeviceContext);
    const int frameSlot = m_writeFrame % kFrameDelay;
    const int slot = frameSlot * m_maxQueries + queryIndex * 2;
    vkCmdWriteTimestamp(context->GetCommandBuffer(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        m_queryPool, slot);
  }

  void EndScope(int queryIndex) override {
    auto* context = static_cast<VulkanDeviceContext*>(T8DeviceContext);
    const int frameSlot = m_writeFrame % kFrameDelay;
    const int slot = frameSlot * m_maxQueries + queryIndex * 2 + 1;
    vkCmdWriteTimestamp(context->GetCommandBuffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        m_queryPool, slot);
  }

  void EndFrame(int activeQueryCount,
                const std::vector<ProfileFrameQuery>& frameQueries) override {
    FrameRecord& record = m_records[m_writeFrame % kFrameDelay];
    record.activeCount = activeQueryCount;
    for (int index = 0; index < activeQueryCount; ++index) {
      record.scopeIndices[index] = frameQueries[index].scopeIndex;
      record.cpuOnly[index] = frameQueries[index].cpuOnly;
    }
    ++m_writeFrame;
  }

  void Resolve(std::vector<ProfileScope>& scopes) override {
    const int readFrame = m_writeFrame - kFrameDelay;
    if (readFrame < 0) return;
    const int frameSlot = readFrame % kFrameDelay;
    const FrameRecord& record = m_records[frameSlot];
    const int baseQuery = frameSlot * m_maxQueries;
    for (int index = 0; index < record.activeCount; ++index) {
      if (record.cpuOnly[index]) continue;
      const int scopeIndex = record.scopeIndices[index];
      if (scopeIndex < 0 || scopeIndex >= static_cast<int>(scopes.size())) continue;
      uint64_t timestamps[2] = {};
      const VkResult result = vkGetQueryPoolResults(
        m_driver->GetDevice(), m_queryPool, baseQuery + index * 2, 2,
        sizeof(timestamps), timestamps, sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
      if (result == VK_SUCCESS) {
        scopes[scopeIndex].gpuTotalMs +=
          static_cast<double>(timestamps[1] - timestamps[0]) *
          static_cast<double>(m_timestampPeriod) / 1000000.0;
        ++scopes[scopeIndex].sampleCount;
      }
    }
  }

  void FlushQueryReset(void* commandBuffer) override {
    if (!m_needsReset || !commandBuffer) return;
    const int frameSlot = m_writeFrame % kFrameDelay;
    const int baseQuery = frameSlot * m_maxQueries;
    vkCmdResetQueryPool(static_cast<VkCommandBuffer>(commandBuffer), m_queryPool,
                        baseQuery, m_maxQueries);
    m_needsReset = false;
  }

private:
  struct FrameRecord {
    int activeCount = 0;
    std::vector<int> scopeIndices;
    std::vector<bool> cpuOnly;
  };
  static constexpr int kFrameDelay = 3;

  void LogCalibratedTimestampSupport() {
    auto getCalibratedTimestamps = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(
      vkGetDeviceProcAddr(m_driver->GetDevice(), "vkGetCalibratedTimestampsEXT"));
    if (!getCalibratedTimestamps) {
      T8_LOG_INFO("[Profiler] vkGetCalibratedTimestampsEXT not available");
      return;
    }
    auto getTimeDomains = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(
      vkGetInstanceProcAddr(m_driver->GetInstance(),
                            "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"));
    if (!getTimeDomains) return;

    uint32_t domainCount = 0;
    getTimeDomains(m_driver->GetPhysicalDevice(), &domainCount, nullptr);
    std::vector<VkTimeDomainEXT> domains(domainCount);
    getTimeDomains(m_driver->GetPhysicalDevice(), &domainCount, domains.data());
#ifdef OS_WINDOWS
    constexpr VkTimeDomainEXT cpuDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
    constexpr const char* cpuDomainName = "QPC";
#else
    constexpr VkTimeDomainEXT cpuDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT;
    constexpr const char* cpuDomainName = "CLOCK_MONOTONIC";
#endif
    const bool hasDevice = std::find(domains.begin(), domains.end(), VK_TIME_DOMAIN_DEVICE_EXT) != domains.end();
    const bool hasCpu = std::find(domains.begin(), domains.end(), cpuDomain) != domains.end();
    if (!hasDevice || !hasCpu) return;

    VkCalibratedTimestampInfoEXT infos[2] = {};
    infos[0].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
    infos[0].timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;
    infos[1].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
    infos[1].timeDomain = cpuDomain;
    uint64_t timestamps[2] = {};
    uint64_t maxDeviation = 0;
    getCalibratedTimestamps(m_driver->GetDevice(), 2, infos, timestamps, &maxDeviation);
    T8_LOG_INFO("[Profiler] Calibrated timestamps: GPU=%llu %s=%llu deviation=%llu ns",
                timestamps[0], cpuDomainName, timestamps[1], maxDeviation);
  }

  VulkanDriver* m_driver = nullptr;
  VkQueryPool m_queryPool = VK_NULL_HANDLE;
  std::vector<FrameRecord> m_records;
  float m_timestampPeriod = 0.0f;
  int m_maxQueries = 0;
  int m_writeFrame = 0;
  bool m_needsReset = false;
};
#endif

} // namespace

std::unique_ptr<ProfilerGpuBackend> CreateProfilerGpuBackend(BaseDriver* driver) {
  if (!driver) return nullptr;
  switch (driver->m_currentAPI) {
#ifdef OS_WINDOWS
  case GraphicsApi::D3D12: return std::make_unique<D3D12ProfilerBackend>();
  case GraphicsApi::D3D11: return std::make_unique<D3D11ProfilerBackend>();
#endif
#if defined(USING_OPENGL)
  case GraphicsApi::OPENGL: return std::make_unique<OpenGLProfilerBackend>();
#endif
#if defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX)
  case GraphicsApi::VULKAN: return std::make_unique<VulkanProfilerBackend>();
#endif
  default: return nullptr;
  }
}

} // namespace t850
