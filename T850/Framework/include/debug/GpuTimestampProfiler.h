#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef T850_ENABLE_GPU_PROFILING
#define T850_ENABLE_GPU_PROFILING 0
#endif

namespace t850 {

constexpr uint32_t kGpuTimestampMaxRegions = 128;
constexpr uint32_t kGpuTimestampQueriesPerBatch = 2 + kGpuTimestampMaxRegions * 2;

enum class GpuTimestampBatchState : uint8_t {
  Free,
  Recording,
  Submitted,
  ResultsPending,
  Ready,
  Failed
};

struct GpuTimestampBatch {
  std::size_t slot = 0;
  GpuTimestampBatchState state = GpuTimestampBatchState::Free;
  uint64_t frame = 0;
  uint64_t generation = 0;
  uint64_t submissionToken = 0;
  uint32_t queryCount = 0;
};

struct GpuTimestampBatchStats {
  uint64_t acquired = 0;
  uint64_t completed = 0;
  uint64_t failed = 0;
  uint64_t dropped = 0;
  uint64_t staleCallbacks = 0;
  uint64_t cancelled = 0;
};

class GpuTimestampBatchRing {
public:
  explicit GpuTimestampBatchRing(std::size_t capacity = 8);

  std::optional<std::size_t> Acquire(uint64_t frame, uint64_t generation);
  bool MarkSubmitted(std::size_t slot, uint64_t generation,
                     uint64_t submissionToken, uint32_t queryCount);
  bool MarkResultsPending(std::size_t slot, uint64_t generation);
  bool MarkReady(std::size_t slot, uint64_t generation);
  bool MarkFailed(std::size_t slot, uint64_t generation);
  std::vector<GpuTimestampBatch> ConsumeTerminal(uint64_t generation);
  void Reset(uint64_t generation);

  std::size_t Capacity() const { return m_batches.size(); }
  std::size_t PendingCount() const;
  uint64_t Generation() const { return m_generation; }
  const GpuTimestampBatchStats& Stats() const { return m_stats; }
  const GpuTimestampBatch& Batch(std::size_t slot) const { return m_batches.at(slot); }

private:
  bool Matches(std::size_t slot, uint64_t generation);
  void Release(GpuTimestampBatch& batch);

  std::vector<GpuTimestampBatch> m_batches;
  std::size_t m_nextSlot = 0;
  uint64_t m_generation = 0;
  GpuTimestampBatchStats m_stats;
};

struct GpuTimestampSample {
  uint64_t frame = 0;
  std::string region;
  double gpuMs = 0.0;
  uint64_t completionLatencyFrames = 0;
  bool valid = false;
};

std::optional<uint64_t> ComputeGpuTimestampDelta(uint64_t begin, uint64_t end,
                                                  uint32_t validBits);
std::string EscapeGpuTimestampJson(std::string_view value);

class BaseDriver;

class GpuTimestampBackend {
public:
  virtual ~GpuTimestampBackend() = default;
  virtual const char* Name() const = 0;
  virtual bool Init(BaseDriver* driver, std::size_t capacity) = 0;
  virtual bool BeginFrame(uint64_t frame) = 0;
  virtual bool BeginRegion(std::string_view name) = 0;
  virtual void EndRegion() = 0;
  virtual void EndFrame() = 0;
  virtual void OnSubmitted(uint64_t submissionToken) = 0;
  virtual void Poll(uint64_t currentFrame, std::vector<GpuTimestampSample>& samples) = 0;
  virtual const GpuTimestampBatchStats& Stats() const = 0;
};

std::unique_ptr<GpuTimestampBackend> CreateGpuTimestampBackend(BaseDriver* driver);

class GpuTimestampProfiler {
public:
  bool Init(BaseDriver* driver, uint64_t targetFrames, std::string outputPath,
            std::size_t pendingCapacity = 8, bool captureRegions = false);
  void BeginFrame();
  bool BeginRegion(std::string_view name);
  void EndRegion();
  void EndFrame();
  void OnSubmitted(uint64_t submissionToken);
  void Poll();
  bool IsComplete() const;
  bool Finish();

  uint64_t TargetFrames() const { return m_targetFrames; }
  uint64_t SubmittedFrames() const { return m_submittedFrames; }
  const std::vector<GpuTimestampSample>& Samples() const { return m_samples; }

private:
  bool WriteReport() const;

  BaseDriver* m_driver = nullptr;
  std::unique_ptr<GpuTimestampBackend> m_backend;
  std::vector<GpuTimestampSample> m_samples;
  std::string m_outputPath;
  uint64_t m_targetFrames = 0;
  uint64_t m_nextFrame = 0;
  uint64_t m_submittedFrames = 0;
  bool m_frameActive = false;
  bool m_regionActive = false;
  bool m_captureRegions = false;
  bool m_finished = false;
};

class GpuTimestampRegionGuard {
public:
  GpuTimestampRegionGuard(GpuTimestampProfiler* profiler, std::string_view name)
      : m_profiler(profiler) {
    m_active = m_profiler && m_profiler->BeginRegion(name);
  }
  ~GpuTimestampRegionGuard() {
    if (m_active) m_profiler->EndRegion();
  }

  GpuTimestampRegionGuard(const GpuTimestampRegionGuard&) = delete;
  GpuTimestampRegionGuard& operator=(const GpuTimestampRegionGuard&) = delete;

private:
  GpuTimestampProfiler* m_profiler = nullptr;
  bool m_active = false;
};

extern GpuTimestampProfiler* g_gpuTimestampProfiler;

} // namespace t850
