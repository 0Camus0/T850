#pragma once
#include <core/Core.h>

#ifdef __EMSCRIPTEN__
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
struct SDL_Window;
union SDL_Event;

namespace t850 {
class WebFramework final : public RootFramework {
public:
  explicit WebFramework(AppBase* app);
  void InitGlobalVars() override;
  void OnCreateApplication(ApplicationDesc desc) override;
  void OnDestroyApplication() override;
  void OnInterruptApplication() override;
  void OnResumeApplication() override;
  void UpdateApplication() override;
  void ProcessInput() override;
  void ResetApplication() override;
  void ChangeAPI(GraphicsApi::E api) override;
  SDL_Window* Window() const { return m_window; }
  void SetInputEventHandler(bool (*handler)(void*, SDL_Event*), void* userdata) {
    m_inputEventHandler = handler;
    m_inputEventUserdata = userdata;
  }

private:
  static void Tick(void* context);
  bool HandleGraphicsFailure(const std::exception& error);
  void ClearInput();
  void ReadTouchInput();
  std::array<std::atomic<int32_t>, 8> m_touchInput{};
  SDL_Window* m_window = nullptr;
  bool (*m_inputEventHandler)(void*, SDL_Event*) = nullptr;
  void* m_inputEventUserdata = nullptr;
  unsigned m_frames = 0;
  unsigned m_keyEvents = 0;
  unsigned m_mouseEvents = 0;
  unsigned m_focusEvents = 0;
  double m_lastTick = 0;
  double m_intervalTotal = 0;
  double m_workTotal = 0;
  bool m_idleLoop = false;
  unsigned m_deviceRecoveryAttempts = 0;
  bool m_deviceRecoveryPending = false;
};
}
#endif