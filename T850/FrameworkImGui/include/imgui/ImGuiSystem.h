#pragma once

#include <core/Core.h>

struct SDL_Window;

#ifdef OS_WINDOWS
struct ID3D12DescriptorHeap;
#endif

namespace t850 {

  class RootFramework;

  class ImGuiSystem {
  public:
    ImGuiSystem() = default;
    ~ImGuiSystem();

    bool Init(RootFramework* framework, const char* iniFileName, bool enableDocking);
    void Shutdown();

    void NewFrame(bool createDockspace);
    void Render();

    bool IsReady() const { return m_inited; }
    bool WantsKeyboard() const;
    bool WantsMouse() const;
    float ConsumeWheelDelta();
    void AddWheelDelta(float delta) { m_wheelAccum += delta; }

  private:
    bool m_inited = false;
    bool m_dockingEnabled = false;
    GraphicsApi::E m_api = GraphicsApi::D3D11;
    SDL_Window* m_sdlWindow = nullptr;
    float m_wheelAccum = 0.0f;

#ifdef OS_WINDOWS
    ID3D12DescriptorHeap* m_d3d12SrvHeap = nullptr;
#endif
  };

} // namespace t850