#pragma once

#if defined(_WIN32) && defined(_M_X64)
#include <webgpu/webgpu_cpp.h>
#include <memory>

namespace t850::webgpu {
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
  void WaitForGPU();
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
};
}
#endif