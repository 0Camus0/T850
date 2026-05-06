#include <pch.h>

#include <Config.h>

#ifdef OS_ANDROID

#include <core/android/AndroidFramework.h>
#include <video/vulkan/VulkanDriver.h>
#include <utils/Log.h>
#include <utils/ThreadPool.h>

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android_native_app_glue.h>

namespace t850 {

  AndroidFramework::AndroidFramework(AppBase* appBase, android_app* app)
    : RootFramework(appBase), m_app(app) {
    pBaseApp->SetParentFramework(this);
    m_inited = false;
  }

  void AndroidFramework::InitGlobalVars() {}

  void AndroidFramework::OnCreateApplication(ApplicationDesc desc) {
    aplicationDescriptor = desc;
    aplicationDescriptor.api = GraphicsApi::VULKAN;
    InitGlobalThreadPool();
    pBaseApp->InitVars();
    m_inited = true;
  }

  void AndroidFramework::OnDestroyApplication() {
    DestroyVulkanRuntime();
    ShutdownGlobalThreadPool();
    m_inited = false;
  }

  void AndroidFramework::OnInterruptApplication() {
    m_paused = true;
    if (pBaseApp) pBaseApp->OnPause();
  }

  void AndroidFramework::OnResumeApplication() {
    m_paused = false;
    if (pBaseApp) pBaseApp->OnResume();
  }

  void AndroidFramework::UpdateApplication() {
    while (m_alive) {
      int events = 0;
      android_poll_source* source = nullptr;
      while (ALooper_pollOnce((m_paused || !m_hasRuntime) ? -1 : 0, nullptr, &events,
                              reinterpret_cast<void**>(&source)) >= 0) {
        if (source) source->process(m_app, source);
        if (m_app && m_app->destroyRequested) {
          m_alive = false;
          break;
        }
      }

      if (!m_alive) break;
      if (!m_paused && m_hasRuntime && pBaseApp) {
        ProcessInput();
        pBaseApp->OnUpdate();
      }
    }
  }

  void AndroidFramework::ProcessInput() {
    if (!pBaseApp) return;
    pBaseApp->IManager.scrollDelta = 0.0f;
  }

  void AndroidFramework::ResetApplication() {}

  void AndroidFramework::ChangeAPI(GraphicsApi::E api) {
    if (api != GraphicsApi::VULKAN) {
      T8_LOG_INFO("[AndroidFramework] Android backend is Vulkan-only; forcing Vulkan");
    }
    aplicationDescriptor.api = GraphicsApi::VULKAN;
    if (m_window) CreateVulkanRuntime();
  }

  void AndroidFramework::OnNativeWindowCreated(ANativeWindow* window) {
    m_window = window;
    UpdateWindowSize();
    if (m_inited) CreateVulkanRuntime();
  }

  void AndroidFramework::OnNativeWindowDestroyed() {
    DestroyVulkanRuntime();
    m_window = nullptr;
  }

  void AndroidFramework::OnAppCommand(int32_t cmd) {
    switch (cmd) {
      case APP_CMD_INIT_WINDOW:
        if (m_app && m_app->window) OnNativeWindowCreated(m_app->window);
        break;
      case APP_CMD_TERM_WINDOW:
        OnNativeWindowDestroyed();
        break;
      case APP_CMD_GAINED_FOCUS:
      case APP_CMD_RESUME:
        OnResumeApplication();
        break;
      case APP_CMD_LOST_FOCUS:
      case APP_CMD_PAUSE:
        OnInterruptApplication();
        break;
      case APP_CMD_WINDOW_RESIZED:
      case APP_CMD_CONFIG_CHANGED:
        UpdateWindowSize();
        if (pVideoDriver && m_hasRuntime) {
          pVideoDriver->ResizeSwapchain(aplicationDescriptor.width, aplicationDescriptor.height);
        }
        break;
      default:
        break;
    }
  }

  int32_t AndroidFramework::OnInputEvent(AInputEvent* event) {
    if (!pBaseApp || !event) return 0;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
      const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
      const float x = AMotionEvent_getX(event, 0);
      const float y = AMotionEvent_getY(event, 0);
      pBaseApp->IManager.mouseX = static_cast<int>(x);
      pBaseApp->IManager.mouseY = static_cast<int>(y);
      pBaseApp->IManager.xDelta = static_cast<int>(x - m_lastTouchX);
      pBaseApp->IManager.yDelta = static_cast<int>(y - m_lastTouchY);
      m_lastTouchX = x;
      m_lastTouchY = y;

      if (action == AMOTION_EVENT_ACTION_DOWN) {
        m_touchActive = true;
        pBaseApp->IManager.MouseButtonStates[0][0] = true;
      } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        m_touchActive = false;
        pBaseApp->IManager.MouseButtonStates[0][0] = false;
        pBaseApp->IManager.MouseButtonStates[1][0] = false;
      }
      return 1;
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
      const int32_t key = AKeyEvent_getKeyCode(event);
      const int32_t action = AKeyEvent_getAction(event);
      if (key == AKEYCODE_BACK) {
        if (action == AKEY_EVENT_ACTION_DOWN) {
          pBaseApp->IManager.KeyStates[0][T800K_ESCAPE] = true;
        } else if (action == AKEY_EVENT_ACTION_UP) {
          pBaseApp->IManager.KeyStates[0][T800K_ESCAPE] = false;
          pBaseApp->IManager.KeyStates[1][T800K_ESCAPE] = false;
          if (!pBaseApp->IsModalActive()) m_alive = false;
        }
        return 1;
      }
    }
    return 0;
  }

  void AndroidFramework::CreateVulkanRuntime() {
    if (!m_window) return;
    DestroyVulkanRuntime();
    UpdateWindowSize();

    pVideoDriver = new VulkanDriver;
    g_pBaseDriver = pVideoDriver;
    pVideoDriver->SetDimensions(aplicationDescriptor.width, aplicationDescriptor.height);
    pVideoDriver->SetWindowHandle(WindowHandle::FromAndroidNativeWindow(m_window));
    pVideoDriver->InitDriver();
    pBaseApp->CreateAssets();
    pVideoDriver->BuildPipelineObjects();
    m_hasRuntime = true;
    T8_LOG_INFO("[AndroidFramework] Vulkan runtime created (%dx%d)",
                aplicationDescriptor.width, aplicationDescriptor.height);
  }

  void AndroidFramework::DestroyVulkanRuntime() {
    if (!m_hasRuntime || !pVideoDriver) return;
    pVideoDriver->FlushGPUResources();
    if (pBaseApp) pBaseApp->DestroyAssets();
    pVideoDriver->DestroyDriver();
    delete pVideoDriver;
    pVideoDriver = nullptr;
    g_pBaseDriver = nullptr;
    m_hasRuntime = false;
  }

  void AndroidFramework::UpdateWindowSize() {
    if (!m_window) return;
    int32_t w = ANativeWindow_getWidth(m_window);
    int32_t h = ANativeWindow_getHeight(m_window);
    if (w > 0) aplicationDescriptor.width = w;
    if (h > 0) aplicationDescriptor.height = h;
  }

} // namespace t850

#endif // OS_ANDROID
