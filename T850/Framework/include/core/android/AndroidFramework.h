/*********************************************************
* T850 Engine — Android NativeActivity framework
*********************************************************/

#ifndef T850_ANDROID_FRAMEWORK_H
#define T850_ANDROID_FRAMEWORK_H

#include <Config.h>

#ifdef OS_ANDROID

#include <core/Core.h>
#include <video/BaseDriver.h>

struct android_app;
struct AInputEvent;
struct ANativeWindow;

namespace t850 {

  class AndroidFramework : public RootFramework {
  public:
    AndroidFramework(AppBase* pBaseApp, android_app* app);
    ~AndroidFramework() {}

    void InitGlobalVars() override;
    void OnCreateApplication(ApplicationDesc desc) override;
    void OnDestroyApplication() override;
    void OnInterruptApplication() override;
    void OnResumeApplication() override;
    void UpdateApplication() override;
    void ProcessInput() override;
    void ResetApplication() override;
    void ChangeAPI(GraphicsApi::E api) override;

    void OnNativeWindowCreated(ANativeWindow* window);
    void OnNativeWindowDestroyed();
    void OnAppCommand(int32_t cmd);
    int32_t OnInputEvent(AInputEvent* event);

    bool IsAlive() const { return m_alive; }

  private:
    void CreateVulkanRuntime();
    void DestroyVulkanRuntime();
    void UpdateWindowSize();

    android_app* m_app = nullptr;
    ANativeWindow* m_window = nullptr;
    bool m_alive = true;
    bool m_paused = false;
    bool m_hasRuntime = false;
    float m_lastTouchX = 0.0f;
    float m_lastTouchY = 0.0f;
    bool m_touchActive = false;
  };

} // namespace t850

#endif // OS_ANDROID
#endif // T850_ANDROID_FRAMEWORK_H
