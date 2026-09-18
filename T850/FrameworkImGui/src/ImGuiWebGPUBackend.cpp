#include <pch.h>
#include <imgui/ImGuiRendererBackend.h>

#if (defined(_WIN32) && defined(_M_X64)) || defined(__EMSCRIPTEN__)
#include <video/webgpu/WebGPUDriver.h>
#include <utils/Log.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>
#include <SDL3/SDL.h>

namespace t850 {
namespace {
class ImGuiWebGPUBackend final : public ImGuiRendererBackend {
public:
  ~ImGuiWebGPUBackend() override { Shutdown(); }
  bool Init(RootFramework* framework, void* nativeWindow) override {
    if (!framework || !nativeWindow) return false;
    m_driver = dynamic_cast<WebGPUDriver*>(framework->pVideoDriver);
    if (!m_driver || !ImGui_ImplSDL3_InitForOther(static_cast<SDL_Window*>(nativeWindow))) return false;
    m_platformInitialized = true;
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    m_rendererInitialized = InitRenderer(m_driver->GetRenderTargetLayout());
    if (!m_rendererInitialized) Shutdown();
    return m_rendererInitialized;
  }
  void Shutdown() override {
    if (m_rendererInitialized) {
      m_driver->WaitForGPU();
      ImGui_ImplWGPU_Shutdown();
    }
    if (m_platformInitialized) ImGui_ImplSDL3_Shutdown();
    m_rendererInitialized = m_platformInitialized = false;
    m_targetLayout = {};
    m_driver = nullptr;
  }
  void NewFrame() override {
    const auto layout = m_driver->GetRenderTargetLayout();
    m_targetReady = ValidateOverlayLayout(*m_driver, layout);
    if (m_targetReady && !layout.HasCompatibleAttachments(m_targetLayout)) {
      m_driver->WaitForGPU();
      if (m_rendererInitialized) ImGui_ImplWGPU_Shutdown();
      m_rendererInitialized = InitRenderer(layout);
      if (!m_rendererInitialized) T8_LOG_ERROR("[ImGui] Failed to configure WebGPU overlay target");
    }
    if (m_targetReady && m_rendererInitialized) m_targetLayout = layout;
    if (m_rendererInitialized) ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
#ifdef __EMSCRIPTEN__
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(m_driver->width), static_cast<float>(m_driver->height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
#endif
  }
  void RenderDrawData(ImDrawData* drawData) override {
    if (!m_rendererInitialized || !m_targetReady || !ValidateOverlayDraw(*m_driver, m_targetLayout)) return;
    if (auto pass = m_driver->OverlayPass()) ImGui_ImplWGPU_RenderDrawData(drawData, pass);
  }
  ImTextureID GetTextureID(Texture* texture, ImGuiTextureMode) override {
    return reinterpret_cast<ImTextureID>(m_driver->TextureView(texture));
  }
private:
  bool InitRenderer(const RenderTargetLayout& layout) {
    if (!ValidateOverlayLayout(*m_driver, layout)) return false;
    ImGui_ImplWGPU_InitInfo info{};
    info.Device = m_driver->NativeDevice();
    info.NumFramesInFlight = 3;
    switch (layout.colorFormats[0]) {
    case BaseRT::RGBA8: info.RenderTargetFormat = WGPUTextureFormat_RGBA8Unorm; break;
    case BaseRT::BGRA8: info.RenderTargetFormat = WGPUTextureFormat_BGRA8Unorm; break;
    case BaseRT::RGBA16F: info.RenderTargetFormat = WGPUTextureFormat_RGBA16Float; break;
    case BaseRT::RGBA32F: info.RenderTargetFormat = WGPUTextureFormat_RGBA32Float; break;
    case BaseRT::R8: info.RenderTargetFormat = WGPUTextureFormat_R8Unorm; break;
    case BaseRT::F16: info.RenderTargetFormat = WGPUTextureFormat_R16Float; break;
    case BaseRT::F32: info.RenderTargetFormat = WGPUTextureFormat_R32Float; break;
    default: return false;
    }
    info.DepthStencilFormat = layout.depthFormat == BaseRT::NOTHING
      ? WGPUTextureFormat_Undefined : WGPUTextureFormat_Depth32Float;
    info.PipelineMultisampleState.count = layout.sampleCount;
    if (!ImGui_ImplWGPU_Init(&info)) return false;
    m_targetLayout = layout;
    return true;
  }
  WebGPUDriver* m_driver = nullptr;
  RenderTargetLayout m_targetLayout;
  bool m_targetReady = false;
  bool m_platformInitialized = false;
  bool m_rendererInitialized = false;
};
}
std::unique_ptr<ImGuiRendererBackend> CreateImGuiWebGPUBackend() { return std::make_unique<ImGuiWebGPUBackend>(); }
}
#endif