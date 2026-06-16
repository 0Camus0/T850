#pragma once

#include <core/Core.h>

struct SDL_Window;

#ifdef OS_WINDOWS
struct ID3D12DescriptorHeap;
#endif
#ifdef OS_ANDROID
struct AInputEvent;
struct ANativeWindow;
#endif

namespace t850 {

  class RootFramework;

  class ImGuiSystem {
  public:
    ImGuiSystem() = default;
    ~ImGuiSystem();

    bool Init(RootFramework* framework, const char* iniFileName, bool enableDocking, bool enablePlatformWindows = false);
    void Shutdown();

    bool NewFrame(bool createDockspace);
    void Render();
    void BuildDrawData();
    void RenderDrawData();
    void InstallLoadingProgressRenderer();
    void ClearLoadingProgressRenderer();
    void RenderLoadingFrame();

    bool IsReady() const { return m_inited; }
    bool WantsKeyboard() const;
    bool WantsTextInput() const;
    bool WantsMouse() const;
    float ConsumeWheelDelta();
    void AddWheelDelta(float delta) { m_wheelAccum += delta; }
    void SetGamepadNavigationInput(const GamepadInputState& gamepad, bool guiVisible, bool touchCursorVisible);
    void NoteWindowEvent(const char* eventName, int data1, int data2);
#ifdef OS_ANDROID
    bool HandleAndroidInputEvent(AInputEvent* event);
    bool SetAndroidNativeWindow(ANativeWindow* window);
#endif

  private:
    RootFramework* m_framework = nullptr;
    bool m_inited = false;
    bool m_dockingEnabled = false;
    bool m_platformWindowsEnabled = false;
    bool m_loadingFrameActive = false;
    GraphicsApi::E m_api = GraphicsApi::D3D11;
    SDL_Window* m_sdlWindow = nullptr;
    float m_wheelAccum = 0.0f;
    GamepadInputState m_gamepadNavigationState;
    bool m_gamepadNavigationGuiVisible = false;
    bool m_gamepadNavigationTouchCursor = false;
    int m_windowEventTraceFrames = 0;
    std::string m_lastWindowEventName;
    int m_lastWindowEventData1 = 0;
    int m_lastWindowEventData2 = 0;

#ifdef OS_WINDOWS
    ID3D12DescriptorHeap* m_d3d12SrvHeap = nullptr;
#endif
#ifdef OS_ANDROID
    ANativeWindow* m_androidWindow = nullptr;
    bool m_androidPlatformInited = false;
#endif
  };

} // namespace t850
