#include <pch.h>

#include <imgui/ImGuiRendererBackend.h>

#ifndef OS_ANDROID
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <video/BaseDriver.h>
#endif

namespace t850 {

#ifndef OS_ANDROID
namespace {

class ImGuiOpenGLBackend final : public ImGuiRendererBackend {
public:
  ~ImGuiOpenGLBackend() override { Shutdown(); }

  bool Init(RootFramework* framework, void* nativeWindow) override {
    auto* window = static_cast<SDL_Window*>(nativeWindow);
    if (!framework || !framework->pVideoDriver ||
        framework->pVideoDriver->m_currentAPI != GraphicsApi::OPENGL ||
        !window || !ImGui_ImplSDL3_InitForOpenGL(window, nullptr)) return false;
    m_platformInitialized = true;
    m_driver = framework->pVideoDriver;
    m_rendererInitialized = ImGui_ImplOpenGL3_Init("#version 300 es");
    if (!m_rendererInitialized) Shutdown();
    return m_rendererInitialized;
  }

  void Shutdown() override {
    if (m_rendererInitialized) ImGui_ImplOpenGL3_Shutdown();
    if (m_platformInitialized) ImGui_ImplSDL3_Shutdown();
    m_rendererInitialized = false;
    m_platformInitialized = false;
    m_driver = nullptr;
    m_targetReady = false;
  }

  void NewFrame() override {
    m_targetLayout = m_driver->GetRenderTargetLayout();
    m_targetReady = ValidateOverlayLayout(*m_driver, m_targetLayout);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
  }

  void RenderDrawData(ImDrawData* drawData) override {
    if (!m_targetReady || !ValidateOverlayDraw(*m_driver, m_targetLayout)) return;
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
  }

  ImTextureID GetTextureID(Texture* texture, ImGuiTextureMode) override {
    return (ImTextureID)(intptr_t)texture->id;
  }

private:
  BaseDriver* m_driver = nullptr;
  RenderTargetLayout m_targetLayout;
  bool m_targetReady = false;
  bool m_platformInitialized = false;
  bool m_rendererInitialized = false;
};

} // namespace
#endif

std::unique_ptr<ImGuiRendererBackend> CreateImGuiOpenGLBackend() {
#ifndef OS_ANDROID
  return std::make_unique<ImGuiOpenGLBackend>();
#else
  return nullptr;
#endif
}

} // namespace t850
