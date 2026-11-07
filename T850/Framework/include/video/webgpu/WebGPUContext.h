#pragma once

#if (defined(_WIN32) && (defined(_M_X64) || defined(_M_ARM64))) || defined(__EMSCRIPTEN__)
#include <webgpu/webgpu_cpp.h>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <map>

namespace t850::webgpu {
void ConfigureDeviceLossTestFrame(uint64_t frame);
void ConfigureDeviceLossTestSequence(uint64_t firstFrame, uint32_t count, uint64_t intervalFrames);

class WebGPUContext {
public:
  WebGPUContext();
  ~WebGPUContext();
  WebGPUContext(const WebGPUContext&) = delete;
  WebGPUContext& operator=(const WebGPUContext&) = delete;

  void Initialize(void* hwnd, uint32_t width, uint32_t height);
  void Resize(uint32_t width, uint32_t height);
  bool BeginFrame(bool swapchain);
  void Submit(bool present);
  void SubmitCommands(const wgpu::CommandBuffer& command);
  wgpu::Buffer AcquireBuffer(const wgpu::BufferDescriptor& descriptor);
  wgpu::Buffer UploadUniform(const void* data, uint64_t size, uint64_t& offset);
  // Per-dispatch storage data (the solver's body list), in shared pages bound with dynamic offsets.
  wgpu::Buffer UploadStorage(const void* data, uint64_t size, uint64_t& offset);
  // Bumped when a pooled buffer is destroyed; a cached bind group may reference it.
  uint64_t ReleaseGeneration() const { return m_releaseGeneration; }
  // A headless compute loop never reaches the frame end that clears the uniform ring, so the
  // fluid solver would grow it until the process runs out of memory. `force` clears it now;
  // otherwise it clears itself once the ring is worth recycling.
  void RecycleComputeUniforms(bool force = false);
  uint64_t UniformEpoch() const { return m_uniformEpoch; }
  void Retire(wgpu::Buffer buffer, uint64_t size, wgpu::BufferUsage usage);
  void Retire(wgpu::Texture texture);
  void WaitForGPU();
  bool GetHealthError(std::string& diagnostic) const;
  void CheckHealth() const;
  void Shutdown();

  wgpu::Instance instance;
  wgpu::Adapter adapter;
  wgpu::Device device;
  wgpu::Queue queue;
  wgpu::Surface surface;
  wgpu::SurfaceConfiguration configuration{};
  wgpu::CommandEncoder commands;
  wgpu::Texture backbuffer;
  wgpu::Texture depth;
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t adapterLuid = 0;

private:
  struct Health;
#ifndef __EMSCRIPTEN__
  struct PersistentCache;
  std::unique_ptr<PersistentCache> m_persistentCache;
#endif
  std::shared_ptr<Health> m_health;
  bool m_configured = false;
  bool m_directReadback = false;
  using BufferKey = std::pair<uint64_t, wgpu::BufferUsage>;
  struct RetiredBuffer { wgpu::Buffer buffer; BufferKey key; };
  struct Submission { wgpu::Future completion; std::vector<RetiredBuffer> buffers; uint64_t frame = 0; };
  void CollectCompletedBuffers(bool waitForOldest = false);
  std::deque<Submission> m_submissions;
  uint64_t m_frameSerial = 0;
  std::map<BufferKey, std::vector<wgpu::Buffer>> m_freeBuffers;
  static constexpr uint64_t maximumPoolBytes = 32 * 1024 * 1024;
  uint64_t m_freeBufferBytes = 0;
  struct UniformUpload { wgpu::Buffer buffer; std::vector<char> data; uint64_t capacity; };
  wgpu::Buffer UploadPaged(std::vector<UniformUpload>& pages, wgpu::BufferUsage usage, const void* data, uint64_t size, uint64_t& offset);
  std::vector<UniformUpload> m_uniformUploads;
  std::vector<UniformUpload> m_storageUploads;
  uint64_t m_releaseGeneration = 0;
  uint64_t m_uniformEpoch = 1;
  wgpu::Texture m_captureTarget;
  wgpu::Texture m_presentTarget;
  wgpu::RenderPipeline m_presentPipeline;
  std::vector<RetiredBuffer> m_retiredBuffers;
  std::vector<wgpu::Texture> m_retiredTextures;
};
}
#endif