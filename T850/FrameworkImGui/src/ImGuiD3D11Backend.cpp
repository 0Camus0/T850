#include <pch.h>

#include <imgui/ImGuiRendererBackend.h>

#ifdef OS_WINDOWS
#include <core/Core.h>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <video/BaseDriver.h>
#include <video/d3d11/D3D11Texture.h>
#endif

namespace t850 {

#ifdef OS_WINDOWS
extern Device* T8Device;
extern DeviceContext* T8DeviceContext;

namespace {

class ImGuiD3D11Backend final : public ImGuiRendererBackend {
public:
  ~ImGuiD3D11Backend() override { Shutdown(); }

  bool Init(RootFramework* framework, void* nativeWindow) override {
    auto* window = static_cast<SDL_Window*>(nativeWindow);
    if (!framework || !framework->pVideoDriver ||
        framework->pVideoDriver->m_currentAPI != GraphicsApi::D3D11 ||
        !T8Device || !T8DeviceContext || !window ||
        !ImGui_ImplSDL3_InitForD3D(window)) return false;
    m_platformInitialized = true;

    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* context =
      reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    m_rendererInitialized = ImGui_ImplDX11_Init(device, context);
    if (!m_rendererInitialized) Shutdown();
    return m_rendererInitialized;
  }

  void Shutdown() override {
    if (m_rendererInitialized) ImGui_ImplDX11_Shutdown();
    if (m_platformInitialized) ImGui_ImplSDL3_Shutdown();
    m_rendererInitialized = false;
    m_platformInitialized = false;
  }

  void NewFrame() override {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplSDL3_NewFrame();
  }

  void RenderDrawData(ImDrawData* drawData) override {
    ImGui_ImplDX11_RenderDrawData(drawData);
  }

  ImTextureID GetTextureID(Texture* texture, ImGuiTextureMode) override {
    return reinterpret_cast<ImTextureID>(static_cast<D3DXTexture*>(texture)->pSRVTex.Get());
  }

  bool RequiresOpaquePreviewBlend() const override { return true; }

private:
  bool m_platformInitialized = false;
  bool m_rendererInitialized = false;
};

} // namespace
#endif

std::unique_ptr<ImGuiRendererBackend> CreateImGuiD3D11Backend() {
#ifdef OS_WINDOWS
  return std::make_unique<ImGuiD3D11Backend>();
#else
  return nullptr;
#endif
}

} // namespace t850
