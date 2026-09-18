#pragma once
#include <video/BaseDriver.h>

#if (defined(_WIN32) && defined(_M_X64)) || defined(__EMSCRIPTEN__)
#include <video/webgpu/WebGPUShaderCompiler.h>
#include <webgpu/webgpu.h>
#include <memory>

namespace t850 {
struct WebGPUDriverState;
class WebGPUDriver : public BaseDriver {
public:
  WebGPUDriver();
  ~WebGPUDriver() override;
  const char* ApiTag() const override { return "webgpu"; }
  bool SupportsDeferredRendering() const override { return true; }
  bool SupportsComputeShaders() const override { return true; }
  bool SupportsComputeTextures() const override { return true; }
  void InitDriver() override;
  std::unique_ptr<ComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc) override;
  std::unique_ptr<ComputeBuffer> CreateComputeBuffer(const ComputeBufferDesc& desc,
                                                      const void* initialData = nullptr) override;
  bool DispatchCompute(ComputePipeline& pipeline, const std::vector<ComputeBindingDesc>& bindings,
                       uint32_t groupX, uint32_t groupY, uint32_t groupZ) override;
  bool ReadComputeBuffer(ComputeBuffer& buffer, void* destination, size_t bytes) override;
  void CreateSurfaces() override;
  void DestroySurfaces() override;
  void Update() override;
  void DestroyDriver() override;
  void SetWindow(void* window) override;
  void SetWindowHandle(const WindowHandle& handle) override;
  void SetDimensions(int newWidth, int newHeight) override;
  bool ResizeSwapchain(int newWidth, int newHeight) override;
  bool SuspendWindowSurface() override;
  bool ResumeWindowSurface(void* window, int newWidth, int newHeight) override;
  void BeginFrame(FrameTargetMode target = FrameTargetMode::Swapchain) override;
  void EndFrame() override;
  void CompleteFrame(FrameCompletionMode mode = FrameCompletionMode::Present) override;
  void Clear() override;
  void ClearWithColor(float red, float green, float blue, float alpha) override;
  void ClearBackbufferWithColor(float red, float green, float blue, float alpha) override;
  void SwapBuffers() override;
  void WaitForGPU() override;
  void FlushGPUResources() override;
  void SetBlendState(BlendStates state) override;
  void SetDepthStencilState(DepthStencilStates state) override;
  void SetCullFace(FaceCulling state) override;
  void SetViewport(float x, float y, float width, float height) override;
  void SetScissorRect(int x, int y, int width, int height) override;
  void ClearPendingTextureBinding(int slot) override;
  void PopRT() override;
  void SaveScreenshot(std::string path) override;
  void SaveRTToFile(int target, int attachment, std::string path) override;
  bool ReadRTColorFloat(int target, int attachment, float outRGBA[4]) override;
  void SetShaderFlow(webgpu::ShaderFlow flow);
  unsigned DrawCount() const;
  uint64_t AdapterLuid() const;
  WGPUDevice NativeDevice() const;
  WGPUTextureFormat SurfaceFormat() const;
  WGPURenderPassEncoder OverlayPass();
  WGPUTextureView TextureView(Texture* texture) const;
private:
  std::unique_ptr<WebGPUDriverState> m_state;
};
}
#endif