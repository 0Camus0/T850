#include <pch.h>
#include <imgui/ImGuiRendererBackend.h>

#if (defined(_WIN32) && defined(_M_X64)) || defined(__EMSCRIPTEN__)
#include <video/webgpu/WebGPUDriver.h>
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
    ImGui_ImplWGPU_InitInfo info{};
    info.Device = m_driver->NativeDevice();
    info.NumFramesInFlight = 3;
    info.RenderTargetFormat = m_driver->SurfaceFormat();
    info.DepthStencilFormat = WGPUTextureFormat_Depth32Float;
    m_rendererInitialized = ImGui_ImplWGPU_Init(&info);
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
    m_driver = nullptr;
  }
  void NewFrame() override {
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
#ifdef __EMSCRIPTEN__
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(m_driver->width), static_cast<float>(m_driver->height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
#endif
  }
  void RenderDrawData(ImDrawData* drawData) override {
    if (auto pass = m_driver->OverlayPass()) ImGui_ImplWGPU_RenderDrawData(drawData, pass);
  }
  ImTextureID GetTextureID(Texture* texture, ImGuiTextureMode) override {
    return reinterpret_cast<ImTextureID>(m_driver->TextureView(texture));
  }
private:
  WebGPUDriver* m_driver = nullptr;
  bool m_platformInitialized = false;
  bool m_rendererInitialized = false;
};
}
std::unique_ptr<ImGuiRendererBackend> CreateImGuiWebGPUBackend() { return std::make_unique<ImGuiWebGPUBackend>(); }
}
#endif