#include <pch.h>
#include <debug/GpuTimestampProfiler.h>
#include <video/BaseDriver.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>

#if T850_ENABLE_GPU_PROFILING && defined(OS_WINDOWS)
#include <d3d12.h>
#include <video/d3d12/D3D12Driver.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

#if T850_ENABLE_GPU_PROFILING && (defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX))
#include <video/vulkan/VulkanDriver.h>
#endif

#if T850_ENABLE_GPU_PROFILING && ((defined(_WIN32) && (defined(_M_X64) || defined(_M_ARM64))) || defined(__EMSCRIPTEN__))
#include <video/webgpu/WebGPUDriver.h>
#endif

namespace t850 {

GpuTimestampProfiler* g_gpuTimestampProfiler = nullptr;
extern Device* T8Device;

GpuTimestampBatchRing::GpuTimestampBatchRing(std::size_t capacity)
    : m_batches((std::max)(capacity, std::size_t{1})) {
  for (std::size_t slot = 0; slot < m_batches.size(); ++slot)
    m_batches[slot].slot = slot;
}

std::optional<std::size_t> GpuTimestampBatchRing::Acquire(uint64_t frame,
                                                          uint64_t generation) {
  if (generation != m_generation) {
    ++m_stats.staleCallbacks;
    return std::nullopt;
  }
  for (std::size_t offset = 0; offset < m_batches.size(); ++offset) {
    const std::size_t slot = (m_nextSlot + offset) % m_batches.size();
    GpuTimestampBatch& batch = m_batches[slot];
    if (batch.state != GpuTimestampBatchState::Free) continue;
    batch.state = GpuTimestampBatchState::Recording;
    batch.frame = frame;
    batch.generation = generation;
    batch.submissionToken = 0;
    batch.queryCount = 0;
    m_nextSlot = (slot + 1) % m_batches.size();
    ++m_stats.acquired;
    return slot;
  }
  ++m_stats.dropped;
  return std::nullopt;
}

bool GpuTimestampBatchRing::Matches(std::size_t slot, uint64_t generation) {
  if (slot >= m_batches.size() || generation != m_generation ||
      m_batches[slot].generation != generation ||
      m_batches[slot].state == GpuTimestampBatchState::Free) {
    ++m_stats.staleCallbacks;
    return false;
  }
  return true;
}

bool GpuTimestampBatchRing::MarkSubmitted(std::size_t slot, uint64_t generation,
                                           uint64_t submissionToken,
                                           uint32_t queryCount) {
  if (!Matches(slot, generation)) return false;
  GpuTimestampBatch& batch = m_batches[slot];
  if (batch.state != GpuTimestampBatchState::Recording || queryCount == 0) return false;
  batch.state = GpuTimestampBatchState::Submitted;
  batch.submissionToken = submissionToken;
  batch.queryCount = queryCount;
  return true;
}

bool GpuTimestampBatchRing::MarkResultsPending(std::size_t slot,
                                                uint64_t generation) {
  if (!Matches(slot, generation)) return false;
  GpuTimestampBatch& batch = m_batches[slot];
  if (batch.state != GpuTimestampBatchState::Submitted) return false;
  batch.state = GpuTimestampBatchState::ResultsPending;
  return true;
}

bool GpuTimestampBatchRing::MarkReady(std::size_t slot, uint64_t generation) {
  if (!Matches(slot, generation)) return false;
  GpuTimestampBatch& batch = m_batches[slot];
  if (batch.state != GpuTimestampBatchState::Submitted &&
      batch.state != GpuTimestampBatchState::ResultsPending)
    return false;
  batch.state = GpuTimestampBatchState::Ready;
  return true;
}

bool GpuTimestampBatchRing::MarkFailed(std::size_t slot, uint64_t generation) {
  if (!Matches(slot, generation)) return false;
  GpuTimestampBatch& batch = m_batches[slot];
  if (batch.state == GpuTimestampBatchState::Ready ||
      batch.state == GpuTimestampBatchState::Failed)
    return false;
  batch.state = GpuTimestampBatchState::Failed;
  return true;
}

void GpuTimestampBatchRing::Release(GpuTimestampBatch& batch) {
  const std::size_t slot = batch.slot;
  batch = {};
  batch.slot = slot;
}

std::vector<GpuTimestampBatch> GpuTimestampBatchRing::ConsumeTerminal(
    uint64_t generation) {
  if (generation != m_generation) {
    ++m_stats.staleCallbacks;
    return {};
  }
  std::vector<GpuTimestampBatch> terminal;
  for (GpuTimestampBatch& batch : m_batches) {
    if (batch.state != GpuTimestampBatchState::Ready &&
        batch.state != GpuTimestampBatchState::Failed)
      continue;
    terminal.push_back(batch);
    if (batch.state == GpuTimestampBatchState::Ready) ++m_stats.completed;
    else ++m_stats.failed;
    Release(batch);
  }
  return terminal;
}

void GpuTimestampBatchRing::Reset(uint64_t generation) {
  for (GpuTimestampBatch& batch : m_batches) {
    if (batch.state != GpuTimestampBatchState::Free) ++m_stats.cancelled;
    Release(batch);
  }
  m_generation = generation;
  m_nextSlot = 0;
}

std::size_t GpuTimestampBatchRing::PendingCount() const {
  return static_cast<std::size_t>(std::count_if(
      m_batches.begin(), m_batches.end(), [](const GpuTimestampBatch& batch) {
        return batch.state != GpuTimestampBatchState::Free;
      }));
}

namespace {

struct GpuTimestampRegionRange {
  std::string name;
  uint32_t beginQuery = 0;
  uint32_t endQuery = 0;
};

struct GpuTimestampQueryLayout {
  void BeginFrame() {
    regions.clear();
    activeRegion.reset();
    frameEndQuery = 0;
    queryCount = 1;
  }

  std::optional<uint32_t> BeginRegion(std::string_view name) {
    if (activeRegion || queryCount + 2 > kGpuTimestampQueriesPerBatch) return std::nullopt;
    regions.push_back({std::string(name), queryCount++, 0});
    activeRegion = regions.size() - 1;
    return regions.back().beginQuery;
  }

  std::optional<uint32_t> EndRegion() {
    if (!activeRegion || queryCount >= kGpuTimestampQueriesPerBatch) return std::nullopt;
    GpuTimestampRegionRange& region = regions[*activeRegion];
    region.endQuery = queryCount++;
    activeRegion.reset();
    return region.endQuery;
  }

  std::optional<uint32_t> EndFrame() {
    if (activeRegion || queryCount >= kGpuTimestampQueriesPerBatch) return std::nullopt;
    frameEndQuery = queryCount++;
    return frameEndQuery;
  }

  uint32_t queryCount = 0;
  uint32_t frameEndQuery = 0;
  std::optional<std::size_t> activeRegion;
  std::vector<GpuTimestampRegionRange> regions;
};

template <typename ConvertDelta>
bool AppendTimestampSamples(const GpuTimestampBatch& batch,
                            const GpuTimestampQueryLayout& layout,
                            const uint64_t* values,
                            uint64_t currentFrame,
                            ConvertDelta convertDelta,
                            std::vector<GpuTimestampSample>& samples) {
  if (!values || layout.frameEndQuery == 0 ||
      values[layout.frameEndQuery] < values[0]) return false;
  const std::size_t sampleStart = samples.size();
  const uint64_t latency = currentFrame >= batch.frame ? currentFrame - batch.frame : 0;
  samples.push_back({batch.frame, "gpu.frame",
                     convertDelta(values[layout.frameEndQuery], values[0]), latency, true});
  for (const GpuTimestampRegionRange& region : layout.regions) {
    if (region.endQuery <= region.beginQuery || values[region.endQuery] < values[region.beginQuery]) {
      samples.resize(sampleStart);
      return false;
    }
    samples.push_back({batch.frame, region.name,
                       convertDelta(values[region.endQuery], values[region.beginQuery]), latency, true});
  }
  return true;
}

} // namespace

#if T850_ENABLE_GPU_PROFILING && defined(OS_WINDOWS)
namespace {

class D3D12GpuTimestampBackend final : public GpuTimestampBackend {
public:
  const char* Name() const override { return "d3d12"; }

  bool Init(BaseDriver* driver, std::size_t capacity) override {
    if (!driver || driver->m_currentAPI != GraphicsApi::D3D12) return false;
    m_driver = static_cast<D3D12Driver*>(driver);
    m_ring = std::make_unique<GpuTimestampBatchRing>(capacity);
    m_layouts.resize(capacity);
    auto* device = T8Device ? reinterpret_cast<ID3D12Device*>(T8Device->GetAPIObject()) : nullptr;
    if (!device || FAILED(m_driver->GetCmdQueue()->GetTimestampFrequency(&m_frequency))) return false;

    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count = static_cast<UINT>(capacity * kGpuTimestampQueriesPerBatch);
    if (FAILED(device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&m_queries)))) return false;

    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = sizeof(uint64_t) * queryDesc.Count;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(device->CreateCommittedResource(
      &properties, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, IID_PPV_ARGS(&m_readback)));
  }

  bool BeginFrame(uint64_t frame) override {
    if (m_activeSlot || !m_ring) return false;
    const auto slot = m_ring->Acquire(frame, m_ring->Generation());
    if (!slot) return false;
    m_activeSlot = *slot;
    m_layouts[*slot].BeginFrame();
    m_driver->GetCmdList()->EndQuery(
      m_queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
      static_cast<UINT>(*slot * kGpuTimestampQueriesPerBatch));
    return true;
  }

  bool BeginRegion(std::string_view name) override {
    if (!m_activeSlot) return false;
    const auto query = m_layouts[*m_activeSlot].BeginRegion(name);
    if (!query) return false;
    m_driver->GetCmdList()->EndQuery(
      m_queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
      static_cast<UINT>(*m_activeSlot * kGpuTimestampQueriesPerBatch + *query));
    return true;
  }

  void EndRegion() override {
    if (!m_activeSlot) return;
    const auto query = m_layouts[*m_activeSlot].EndRegion();
    if (!query) return;
    m_driver->GetCmdList()->EndQuery(
      m_queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
      static_cast<UINT>(*m_activeSlot * kGpuTimestampQueriesPerBatch + *query));
  }

  void EndFrame() override {
    if (!m_activeSlot) return;
    GpuTimestampQueryLayout& layout = m_layouts[*m_activeSlot];
    const auto frameEnd = layout.EndFrame();
    if (!frameEnd) return;
    const UINT query = static_cast<UINT>(*m_activeSlot * kGpuTimestampQueriesPerBatch);
    auto* commands = m_driver->GetCmdList();
    commands->EndQuery(m_queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + *frameEnd);
    commands->ResolveQueryData(m_queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                               query, layout.queryCount, m_readback.Get(), query * sizeof(uint64_t));
  }

  void OnSubmitted(uint64_t submissionToken) override {
    if (!m_activeSlot) return;
    const uint32_t queryCount = m_layouts[*m_activeSlot].queryCount;
    m_ring->MarkSubmitted(*m_activeSlot, m_ring->Generation(), submissionToken, queryCount);
    m_activeSlot.reset();
  }

  void Poll(uint64_t currentFrame, std::vector<GpuTimestampSample>& samples) override {
    if (!m_ring || !m_readback) return;
    const uint64_t completed = m_driver->GetCompletedFenceValue();
    for (std::size_t slot = 0; slot < m_ring->Capacity(); ++slot) {
      const GpuTimestampBatch batch = m_ring->Batch(slot);
      if (batch.state != GpuTimestampBatchState::Submitted ||
          batch.submissionToken > completed)
        continue;
      const SIZE_T offset = slot * kGpuTimestampQueriesPerBatch * sizeof(uint64_t);
      D3D12_RANGE range{offset, offset + batch.queryCount * sizeof(uint64_t)};
      uint64_t* values = nullptr;
      if (FAILED(m_readback->Map(0, &range, reinterpret_cast<void**>(&values)))) {
        m_ring->MarkFailed(slot, batch.generation);
        continue;
      }
      const uint64_t* batchValues = values + slot * kGpuTimestampQueriesPerBatch;
      const bool valid = m_frequency != 0 && AppendTimestampSamples(
        batch, m_layouts[slot], batchValues, currentFrame,
        [this](uint64_t end, uint64_t begin) {
          return static_cast<double>(end - begin) * 1000.0 / static_cast<double>(m_frequency);
        }, samples);
      D3D12_RANGE written{0, 0};
      m_readback->Unmap(0, &written);
      if (!valid) {
        m_ring->MarkFailed(slot, batch.generation);
        continue;
      }
      m_ring->MarkReady(slot, batch.generation);
    }
    m_ring->ConsumeTerminal(m_ring->Generation());
  }

  const GpuTimestampBatchStats& Stats() const override { return m_ring->Stats(); }

private:
  D3D12Driver* m_driver = nullptr;
  std::unique_ptr<GpuTimestampBatchRing> m_ring;
  std::vector<GpuTimestampQueryLayout> m_layouts;
  std::optional<std::size_t> m_activeSlot;
  ComPtr<ID3D12QueryHeap> m_queries;
  ComPtr<ID3D12Resource> m_readback;
  uint64_t m_frequency = 0;
};

} // namespace
#endif

#if T850_ENABLE_GPU_PROFILING && ((defined(_WIN32) && (defined(_M_X64) || defined(_M_ARM64))) || defined(__EMSCRIPTEN__))
namespace {

class WebGpuTimestampBackend final : public GpuTimestampBackend {
public:
  const char* Name() const override { return "webgpu"; }

  bool Init(BaseDriver* driver, std::size_t capacity) override {
    if (!driver || driver->m_currentAPI != GraphicsApi::WEBGPU) return false;
    m_driver = static_cast<WebGPUDriver*>(driver);
    auto& context = m_driver->TimestampContext();
    if (!context.device || !context.device.HasFeature(wgpu::FeatureName::TimestampQuery)) return false;
    m_ring = std::make_unique<GpuTimestampBatchRing>(capacity);
    wgpu::QuerySetDescriptor queryDesc{};
    queryDesc.type = wgpu::QueryType::Timestamp;
    queryDesc.count = static_cast<uint32_t>(capacity * kGpuTimestampQueriesPerBatch);
    queryDesc.label = "T850 GPU timestamps";
    m_queries = context.device.CreateQuerySet(&queryDesc);
    if (!m_queries) return false;
    m_slots.resize(capacity);
    for (Slot& slot : m_slots) {
      wgpu::BufferDescriptor resolve{};
      resolve.size = kGpuTimestampQueriesPerBatch * sizeof(uint64_t);
      resolve.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
      resolve.label = "T850 timestamp resolve";
      slot.resolve = context.device.CreateBuffer(&resolve);
      wgpu::BufferDescriptor staging{};
      staging.size = resolve.size;
      staging.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
      staging.label = "T850 timestamp staging";
      slot.staging = context.device.CreateBuffer(&staging);
      if (!slot.resolve || !slot.staging) return false;
    }
    return true;
  }

  bool BeginFrame(uint64_t frame) override {
    if (m_activeSlot || !m_ring) return false;
    const auto slot = m_ring->Acquire(frame, m_ring->Generation());
    if (!slot) return false;
    m_activeSlot = *slot;
    m_slots[*slot].layout.BeginFrame();
    m_driver->TimestampContext().commands.WriteTimestamp(
      m_queries, static_cast<uint32_t>(*slot * kGpuTimestampQueriesPerBatch));
    return true;
  }

  bool BeginRegion(std::string_view name) override {
    if (!m_activeSlot) return false;
    m_driver->EndFrame();
    const auto query = m_slots[*m_activeSlot].layout.BeginRegion(name);
    if (!query) return false;
    m_driver->TimestampContext().commands.WriteTimestamp(
      m_queries, static_cast<uint32_t>(*m_activeSlot * kGpuTimestampQueriesPerBatch + *query));
    return true;
  }

  void EndRegion() override {
    if (!m_activeSlot) return;
    m_driver->EndFrame();
    const auto query = m_slots[*m_activeSlot].layout.EndRegion();
    if (!query) return;
    m_driver->TimestampContext().commands.WriteTimestamp(
      m_queries, static_cast<uint32_t>(*m_activeSlot * kGpuTimestampQueriesPerBatch + *query));
  }

  void EndFrame() override {
    if (!m_activeSlot) return;
    auto& context = m_driver->TimestampContext();
    Slot& slot = m_slots[*m_activeSlot];
    const auto frameEnd = slot.layout.EndFrame();
    if (!frameEnd) return;
    const uint32_t query = static_cast<uint32_t>(*m_activeSlot * kGpuTimestampQueriesPerBatch);
    context.commands.WriteTimestamp(m_queries, query + *frameEnd);
    context.commands.ResolveQuerySet(m_queries, query, slot.layout.queryCount, slot.resolve, 0);
    context.commands.CopyBufferToBuffer(slot.resolve, 0, slot.staging, 0,
                                        slot.layout.queryCount * sizeof(uint64_t));
  }

  void OnSubmitted(uint64_t submissionToken) override {
    if (!m_activeSlot) return;
    const std::size_t slotIndex = *m_activeSlot;
    const uint64_t generation = m_ring->Generation();
    Slot& slot = m_slots[slotIndex];
    const uint32_t queryCount = slot.layout.queryCount;
    if (!m_ring->MarkSubmitted(slotIndex, generation, submissionToken, queryCount) ||
        !m_ring->MarkResultsPending(slotIndex, generation)) {
      m_activeSlot.reset();
      return;
    }
    slot.callback = std::make_shared<CallbackState>();
    auto callback = slot.callback;
    auto& context = m_driver->TimestampContext();
    context.queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowProcessEvents,
      [callback](wgpu::QueueWorkDoneStatus status, wgpu::StringView) {
        callback->workSuccess.store(status == wgpu::QueueWorkDoneStatus::Success,
                                    std::memory_order_release);
        callback->workDone.store(true, std::memory_order_release);
      });
    slot.staging.MapAsync(wgpu::MapMode::Read, 0, queryCount * sizeof(uint64_t),
      wgpu::CallbackMode::AllowProcessEvents,
      [callback](wgpu::MapAsyncStatus status, wgpu::StringView) {
        callback->mapSuccess.store(status == wgpu::MapAsyncStatus::Success,
                                   std::memory_order_release);
        callback->mapDone.store(true, std::memory_order_release);
      });
    m_activeSlot.reset();
  }

  void Poll(uint64_t currentFrame, std::vector<GpuTimestampSample>& samples) override {
    if (!m_ring) return;
    auto& context = m_driver->TimestampContext();
    context.instance.ProcessEvents();
    for (std::size_t slotIndex = 0; slotIndex < m_ring->Capacity(); ++slotIndex) {
      const GpuTimestampBatch batch = m_ring->Batch(slotIndex);
      if (batch.state != GpuTimestampBatchState::ResultsPending) continue;
      Slot& slot = m_slots[slotIndex];
      const auto callback = slot.callback;
      if (!callback || !callback->workDone.load(std::memory_order_acquire) ||
          !callback->mapDone.load(std::memory_order_acquire))
        continue;
      if (!callback->workSuccess.load(std::memory_order_acquire) ||
          !callback->mapSuccess.load(std::memory_order_acquire)) {
        m_ring->MarkFailed(slotIndex, batch.generation);
        slot.callback.reset();
        continue;
      }
      const auto* values = static_cast<const uint64_t*>(
        slot.staging.GetConstMappedRange(0, batch.queryCount * sizeof(uint64_t)));
      const bool valid = AppendTimestampSamples(
        batch, slot.layout, values, currentFrame,
        [](uint64_t end, uint64_t begin) {
          return static_cast<double>(end - begin) / 1000000.0;
        }, samples);
      if (!valid) {
        slot.staging.Unmap();
        m_ring->MarkFailed(slotIndex, batch.generation);
        slot.callback.reset();
        continue;
      }
      slot.staging.Unmap();
      m_ring->MarkReady(slotIndex, batch.generation);
      slot.callback.reset();
    }
    m_ring->ConsumeTerminal(m_ring->Generation());
  }

  const GpuTimestampBatchStats& Stats() const override { return m_ring->Stats(); }

private:
  struct CallbackState {
    std::atomic<bool> workDone{false};
    std::atomic<bool> workSuccess{false};
    std::atomic<bool> mapDone{false};
    std::atomic<bool> mapSuccess{false};
  };
  struct Slot {
    wgpu::Buffer resolve;
    wgpu::Buffer staging;
    std::shared_ptr<CallbackState> callback;
    GpuTimestampQueryLayout layout;
  };

  WebGPUDriver* m_driver = nullptr;
  std::unique_ptr<GpuTimestampBatchRing> m_ring;
  std::optional<std::size_t> m_activeSlot;
  wgpu::QuerySet m_queries;
  std::vector<Slot> m_slots;
};

} // namespace
#endif

#if T850_ENABLE_GPU_PROFILING && (defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX))
namespace {

class VulkanGpuTimestampBackend final : public GpuTimestampBackend {
public:
  ~VulkanGpuTimestampBackend() override {
    if (m_driver && m_queries) vkDestroyQueryPool(m_driver->GetDevice(), m_queries, nullptr);
  }

  const char* Name() const override { return "vulkan"; }

  bool Init(BaseDriver* driver, std::size_t capacity) override {
    if (!driver || driver->m_currentAPI != GraphicsApi::VULKAN) return false;
    m_driver = static_cast<VulkanDriver*>(driver);
    m_ring = std::make_unique<GpuTimestampBatchRing>(capacity);
    m_layouts.resize(capacity);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_driver->GetPhysicalDevice(), &properties);
    m_timestampPeriod = properties.limits.timestampPeriod;
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_driver->GetPhysicalDevice(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_driver->GetPhysicalDevice(), &familyCount, families.data());
    const uint32_t family = m_driver->GetGraphicsQueueFamily();
    if (family >= families.size() || families[family].timestampValidBits == 0 || m_timestampPeriod <= 0.0f)
      return false;
    m_validBits = families[family].timestampValidBits;
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = static_cast<uint32_t>(capacity * kGpuTimestampQueriesPerBatch);
    return vkCreateQueryPool(m_driver->GetDevice(), &info, nullptr, &m_queries) == VK_SUCCESS;
  }

  bool BeginFrame(uint64_t frame) override {
    if (m_activeSlot || !m_ring) return false;
    const auto slot = m_ring->Acquire(frame, m_ring->Generation());
    if (!slot) return false;
    m_activeSlot = *slot;
    m_layouts[*slot].BeginFrame();
    const uint32_t query = static_cast<uint32_t>(*slot * kGpuTimestampQueriesPerBatch);
    const VkCommandBuffer commands = m_driver->GetCurrentCommandBuffer();
    vkCmdResetQueryPool(commands, m_queries, query, kGpuTimestampQueriesPerBatch);
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queries, query);
    return true;
  }

  bool BeginRegion(std::string_view name) override {
    if (!m_activeSlot) return false;
    const auto query = m_layouts[*m_activeSlot].BeginRegion(name);
    if (!query) return false;
    vkCmdWriteTimestamp(m_driver->GetCurrentCommandBuffer(),
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queries,
                        static_cast<uint32_t>(*m_activeSlot * kGpuTimestampQueriesPerBatch + *query));
    return true;
  }

  void EndRegion() override {
    if (!m_activeSlot) return;
    const auto query = m_layouts[*m_activeSlot].EndRegion();
    if (!query) return;
    vkCmdWriteTimestamp(m_driver->GetCurrentCommandBuffer(),
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queries,
                        static_cast<uint32_t>(*m_activeSlot * kGpuTimestampQueriesPerBatch + *query));
  }

  void EndFrame() override {
    if (!m_activeSlot) return;
    const auto query = m_layouts[*m_activeSlot].EndFrame();
    if (!query) return;
    vkCmdWriteTimestamp(m_driver->GetCurrentCommandBuffer(),
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queries,
                        static_cast<uint32_t>(*m_activeSlot * kGpuTimestampQueriesPerBatch + *query));
  }

  void OnSubmitted(uint64_t submissionToken) override {
    if (!m_activeSlot) return;
    m_ring->MarkSubmitted(*m_activeSlot, m_ring->Generation(), submissionToken,
                m_layouts[*m_activeSlot].queryCount);
    m_activeSlot.reset();
  }

  void Poll(uint64_t currentFrame, std::vector<GpuTimestampSample>& samples) override {
    if (!m_ring) return;
    const uint64_t completed = m_driver->GetCompletedGpuSubmissionSerial();
    for (std::size_t slot = 0; slot < m_ring->Capacity(); ++slot) {
      const GpuTimestampBatch batch = m_ring->Batch(slot);
      if (batch.state != GpuTimestampBatchState::Submitted || batch.submissionToken > completed)
        continue;
      std::vector<uint64_t> values(batch.queryCount);
      const VkResult result = vkGetQueryPoolResults(
        m_driver->GetDevice(), m_queries,
        static_cast<uint32_t>(slot * kGpuTimestampQueriesPerBatch), batch.queryCount,
        values.size() * sizeof(uint64_t), values.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
      if (result == VK_NOT_READY) continue;
      if (result != VK_SUCCESS) {
        m_ring->MarkFailed(slot, batch.generation);
        continue;
      }
      const bool valid = AppendTimestampSamples(
        batch, m_layouts[slot], values.data(), currentFrame,
        [this](uint64_t end, uint64_t begin) {
          uint64_t delta = end - begin;
          if (m_validBits < 64) {
            const uint64_t mask = (uint64_t{1} << m_validBits) - 1;
            delta &= mask;
          }
          return static_cast<double>(delta) * static_cast<double>(m_timestampPeriod) / 1000000.0;
        }, samples);
      if (!valid) {
        m_ring->MarkFailed(slot, batch.generation);
        continue;
      }
      m_ring->MarkReady(slot, batch.generation);
    }
    m_ring->ConsumeTerminal(m_ring->Generation());
  }

  const GpuTimestampBatchStats& Stats() const override { return m_ring->Stats(); }

private:
  VulkanDriver* m_driver = nullptr;
  std::unique_ptr<GpuTimestampBatchRing> m_ring;
  std::vector<GpuTimestampQueryLayout> m_layouts;
  std::optional<std::size_t> m_activeSlot;
  VkQueryPool m_queries = VK_NULL_HANDLE;
  float m_timestampPeriod = 0.0f;
  uint32_t m_validBits = 0;
};

} // namespace
#endif

std::unique_ptr<GpuTimestampBackend> CreateGpuTimestampBackend(BaseDriver* driver) {
#if T850_ENABLE_GPU_PROFILING && defined(OS_WINDOWS)
  if (driver && driver->m_currentAPI == GraphicsApi::D3D12)
    return std::make_unique<D3D12GpuTimestampBackend>();
#endif
#if T850_ENABLE_GPU_PROFILING && (defined(OS_WINDOWS) || defined(OS_ANDROID) || defined(OS_LINUX))
  if (driver && driver->m_currentAPI == GraphicsApi::VULKAN)
    return std::make_unique<VulkanGpuTimestampBackend>();
#endif
#if T850_ENABLE_GPU_PROFILING && ((defined(_WIN32) && (defined(_M_X64) || defined(_M_ARM64))) || defined(__EMSCRIPTEN__))
  if (driver && driver->m_currentAPI == GraphicsApi::WEBGPU)
    return std::make_unique<WebGpuTimestampBackend>();
#endif
  (void)driver;
  return {};
}

bool GpuTimestampProfiler::Init(BaseDriver* driver, uint64_t targetFrames,
                                std::string outputPath, std::size_t pendingCapacity,
                                bool captureRegions) {
#if !T850_ENABLE_GPU_PROFILING
  (void)driver; (void)targetFrames; (void)outputPath; (void)pendingCapacity;
  (void)captureRegions;
  return false;
#else
  if (!driver || targetFrames == 0 || outputPath.empty()) return false;
  m_backend = CreateGpuTimestampBackend(driver);
  if (!m_backend || !m_backend->Init(driver, pendingCapacity)) {
    m_backend.reset();
    return false;
  }
  m_driver = driver;
  m_targetFrames = targetFrames;
  m_outputPath = std::move(outputPath);
  m_samples.clear();
  m_nextFrame = 0;
  m_submittedFrames = 0;
  m_frameActive = false;
  m_regionActive = false;
  m_captureRegions = captureRegions;
  m_finished = false;
  return true;
#endif
}

void GpuTimestampProfiler::BeginFrame() {
  if (!m_backend || m_finished || m_frameActive || m_submittedFrames >= m_targetFrames) return;
  m_frameActive = m_backend->BeginFrame(m_nextFrame++);
}

bool GpuTimestampProfiler::BeginRegion(std::string_view name) {
  if (!m_backend || !m_captureRegions || !m_frameActive || m_regionActive || name.empty())
    return false;
  m_regionActive = m_backend->BeginRegion(name);
  return m_regionActive;
}

void GpuTimestampProfiler::EndRegion() {
  if (!m_backend || !m_regionActive) return;
  m_backend->EndRegion();
  m_regionActive = false;
}

void GpuTimestampProfiler::EndFrame() {
  if (!m_backend || !m_frameActive) return;
  EndRegion();
  m_backend->EndFrame();
}

void GpuTimestampProfiler::OnSubmitted(uint64_t submissionToken) {
  if (!m_backend || !m_frameActive) return;
  m_backend->OnSubmitted(submissionToken);
  ++m_submittedFrames;
  m_frameActive = false;
  m_regionActive = false;
}

void GpuTimestampProfiler::Poll() {
  if (m_backend && !m_finished) m_backend->Poll(m_nextFrame, m_samples);
}

bool GpuTimestampProfiler::IsComplete() const {
  return m_backend && m_submittedFrames >= m_targetFrames &&
         m_backend->Stats().completed + m_backend->Stats().failed >= m_targetFrames;
}

bool GpuTimestampProfiler::Finish() {
  if (!m_backend || m_finished) return m_finished;
  m_driver->WaitForGPU();
  m_backend->Poll(m_nextFrame, m_samples);
  m_finished = WriteReport();
  return m_finished;
}

bool GpuTimestampProfiler::WriteReport() const {
  std::filesystem::path path(m_outputPath);
  std::error_code error;
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
  if (error) return false;
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out) return false;
  const auto& stats = m_backend->Stats();
  out << std::fixed << std::setprecision(6)
      << "{\n  \"schema\":1,\n  \"mode\":\"gpu-timestamps\",\n"
      << "  \"api\":\"" << m_driver->ApiTag() << "\",\n"
      << "  \"provider\":\"" << m_driver->ProviderTag() << "\",\n"
      << "  \"backend\":\"" << m_driver->UnderlyingBackendTag() << "\",\n"
      << "  \"granularity\":\"" << (m_captureRegions ? "render-graph" : "whole-frame") << "\",\n"
      << "  \"framesRequested\":" << m_targetFrames << ",\n"
      << "  \"framesSubmitted\":" << m_submittedFrames << ",\n"
      << "  \"framesValid\":" << stats.completed << ",\n"
      << "  \"framesDropped\":" << stats.dropped << ",\n"
      << "  \"framesFailed\":" << stats.failed << ",\n"
      << "  \"samples\":[\n";
  for (std::size_t index = 0; index < m_samples.size(); ++index) {
    const auto& sample = m_samples[index];
    out << "    {\"frame\":" << sample.frame << ",\"region\":\"" << sample.region << "\",\"gpuMs\":"
        << sample.gpuMs << ",\"completionLatencyFrames\":" << sample.completionLatencyFrames
        << ",\"valid\":" << (sample.valid ? "true" : "false") << "}"
        << (index + 1 < m_samples.size() ? "," : "") << "\n";
  }
  out << "  ]\n}\n";
  return static_cast<bool>(out);
}

} // namespace t850
