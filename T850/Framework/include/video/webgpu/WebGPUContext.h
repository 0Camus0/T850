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
  std::shared_ptr<Health> m_health;
  bool m_configured = false;
  bool m_directReadback = false;
  using BufferKey = std::pair<uint64_t, wgpu::BufferUsage>;
  struct RetiredBuffer { wgpu::Buffer buffer; BufferKey key; };
  struct Submission { wgpu::Future completion; std::vector<RetiredBuffer> buffers; };
  void CollectCompletedBuffers(bool waitForOldest = false);
  std::deque<Submission> m_submissions;
  std::map<BufferKey, std::vector<wgpu::Buffer>> m_freeBuffers;
  static constexpr uint64_t maximumPoolBytes = 32 * 1024 * 1024;
  uint64_t m_freeBufferBytes = 0;
  struct UniformUpload { wgpu::Buffer buffer; std::vector<char> data; uint64_t capacity; };
  std::vector<UniformUpload> m_uniformUploads;
  uint64_t m_uniformEpoch = 1;
  wgpu::Texture m_captureTarget;
  wgpu::Texture m_presentTarget;
  wgpu::RenderPipeline m_presentPipeline;
  std::vector<RetiredBuffer> m_retiredBuffers;
  std::vector<wgpu::Texture> m_retiredTextures;
};
}
#endif