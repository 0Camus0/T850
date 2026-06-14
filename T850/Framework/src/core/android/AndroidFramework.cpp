#include <pch.h>

#include <Config.h>

#ifdef OS_ANDROID

#include <core/android/AndroidFramework.h>
#include <core/EngineContext.h>
#include <core/Config.h>
#include <video/vulkan/VulkanDriver.h>
#include <utils/Log.h>
#include <utils/ThreadPool.h>
#include <debug/RuntimeTelemetry.h>
#include <navigation/NavigationSystem.h>

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <algorithm>
#include <cmath>

namespace t850 {

  namespace {
    constexpr float kPinchScrollPixelsPerStep = 120.0f;
    constexpr const char* kLauncherPrefsName = "t850_launcher";
    constexpr const char* kReturnToNativeKey = "returnToNative";

    struct ScopedJniEnv {
      JavaVM* vm = nullptr;
      JNIEnv* env = nullptr;
      bool attached = false;

      explicit ScopedJniEnv(ANativeActivity* activity) {
        if (!activity || !activity->vm) return;
        vm = activity->vm;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return;
        if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
          attached = true;
        } else {
          env = nullptr;
        }
      }

      ~ScopedJniEnv() {
        if (attached && vm) vm->DetachCurrentThread();
      }
    };

    float GetPointerDistance(AInputEvent* event, size_t firstIndex, size_t secondIndex) {
      const float firstX = AMotionEvent_getX(event, firstIndex);
      const float firstY = AMotionEvent_getY(event, firstIndex);
      const float secondX = AMotionEvent_getX(event, secondIndex);
      const float secondY = AMotionEvent_getY(event, secondIndex);
      const float dx = firstX - secondX;
      const float dy = firstY - secondY;
      return std::sqrt((dx * dx) + (dy * dy));
    }

    void SetMousePositionFromPointer(InputManager& input, AInputEvent* event, size_t pointerIndex) {
      input.mouseX = static_cast<int>(AMotionEvent_getX(event, pointerIndex));
      input.mouseY = static_cast<int>(AMotionEvent_getY(event, pointerIndex));
    }

    void SetMousePositionFromTwoPointers(InputManager& input, AInputEvent* event,
                                         size_t firstIndex, size_t secondIndex) {
      const float x = (AMotionEvent_getX(event, firstIndex) + AMotionEvent_getX(event, secondIndex)) * 0.5f;
      const float y = (AMotionEvent_getY(event, firstIndex) + AMotionEvent_getY(event, secondIndex)) * 0.5f;
      input.mouseX = static_cast<int>(x);
      input.mouseY = static_cast<int>(y);
    }
  }

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
    RuntimeTelemetry::InitializeFromConfig(g_config);
    const navigation::NavigationBackendInfo navInfo = navigation::GetNavigationBackendInfo();
    T8_LOG_INFO("[Navigation] Recast=%d Detour=%d Crowd=%d TileCache=%d version=%s validation=%s",
                navInfo.recastAvailable ? 1 : 0,
                navInfo.detourAvailable ? 1 : 0,
                navInfo.detourCrowdAvailable ? 1 : 0,
                navInfo.detourTileCacheAvailable ? 1 : 0,
                navInfo.recastVersion.c_str(),
                navigation::ValidateNavigationBackend() ? "ok" : "failed");
    pBaseApp->InitVars();
    m_inited = true;
  }

  void AndroidFramework::OnDestroyApplication() {
    DestroyVulkanRuntime();
    RuntimeTelemetry::Shutdown();
    ShutdownGlobalThreadPool();
    m_inited = false;
  }

  void AndroidFramework::OnInterruptApplication() {
    m_paused = true;
    ClearTouchState();
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
      while (ALooper_pollOnce((m_closing || m_paused || !m_hasRuntime || !m_surfaceActive) ? -1 : 0, nullptr, &events,
                              reinterpret_cast<void**>(&source)) >= 0) {
        if (source) source->process(m_app, source);
        if (m_closeRequested) {
          m_closeRequested = false;
          RequestCloseApplication();
          break;
        }
        if (m_app && m_app->destroyRequested) {
          m_alive = false;
          break;
        }
      }

      if (!m_alive) break;
      if (!m_closing && !m_paused && m_hasRuntime && m_surfaceActive && pBaseApp) {
        ProcessInput();
        pBaseApp->OnUpdate();
        ResetTransientInput();
      }
    }
  }

  void AndroidFramework::ProcessInput() {
  }

  void AndroidFramework::ResetApplication() {}

  void AndroidFramework::ChangeAPI(GraphicsApi::E api) {
    if (api != GraphicsApi::VULKAN) {
      T8_LOG_INFO("[AndroidFramework] Android backend is Vulkan-only; forcing Vulkan");
    }
    aplicationDescriptor.api = GraphicsApi::VULKAN;
    if (m_window) {
      DestroyVulkanRuntime();
      CreateVulkanRuntime();
    }
  }

  void AndroidFramework::OnNativeWindowCreated(ANativeWindow* window) {
    m_window = window;
    UpdateWindowSize();
    if (pBaseApp) pBaseApp->OnAndroidNativeWindowChanged(m_window);
    if (!m_inited) return;
    if (m_hasRuntime) {
      ResumeVulkanWindow();
    } else {
      CreateVulkanRuntime();
    }
  }

  void AndroidFramework::OnNativeWindowDestroyed() {
    ClearTouchState();
    if (pBaseApp) pBaseApp->OnAndroidNativeWindowChanged(nullptr);
    SuspendVulkanWindow();
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
        if (pVideoDriver && m_hasRuntime && m_surfaceActive) {
          pVideoDriver->ResizeSwapchain(aplicationDescriptor.width, aplicationDescriptor.height);
        }
        break;
      default:
        break;
    }
  }

  int32_t AndroidFramework::OnInputEvent(AInputEvent* event) {
    if (!pBaseApp || !event) return 0;
    const bool appHandled = pBaseApp->HandleAndroidInputEvent(event);
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
      if (appHandled) {
        ClearTouchState();
        return 1;
      }

      const int32_t rawAction = AMotionEvent_getAction(event);
      const int32_t action = rawAction & AMOTION_EVENT_ACTION_MASK;
      const int32_t actionPointerIndex =
        (rawAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
      const size_t pointerCount = AMotionEvent_getPointerCount(event);
      if (pointerCount == 0) return 0;

      InputManager& input = pBaseApp->IManager;

      if (action == AMOTION_EVENT_ACTION_DOWN) {
        SetMousePositionFromPointer(input, event, 0);
        m_lastTouchX = AMotionEvent_getX(event, 0);
        m_lastTouchY = AMotionEvent_getY(event, 0);
        m_touchActive = true;
        m_pinchActive = false;
        m_lastPinchDistance = 0.0f;
        input.xDelta = 0;
        input.yDelta = 0;
        input.MouseButtonStates[0][0] = true;
        return 1;
      }

      if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        ClearTouchState();
        return 1;
      }

      if (action == AMOTION_EVENT_ACTION_POINTER_DOWN && pointerCount >= 2) {
        SetMousePositionFromTwoPointers(input, event, 0, 1);
        m_lastPinchDistance = GetPointerDistance(event, 0, 1);
        m_pinchActive = true;
        m_touchActive = false;
        input.xDelta = 0;
        input.yDelta = 0;
        input.MouseButtonStates[0][0] = false;
        input.MouseButtonStates[1][0] = false;
        return 1;
      }

      if (action == AMOTION_EVENT_ACTION_POINTER_UP) {
        const size_t remainingCount = pointerCount - 1;
        if (remainingCount == 0) {
          ClearTouchState();
          return 1;
        }

        size_t firstRemaining = pointerCount;
        size_t secondRemaining = pointerCount;
        for (size_t pointerIndex = 0; pointerIndex < pointerCount; ++pointerIndex) {
          if (static_cast<int32_t>(pointerIndex) == actionPointerIndex) continue;
          if (firstRemaining == pointerCount) {
            firstRemaining = pointerIndex;
          } else {
            secondRemaining = pointerIndex;
            break;
          }
        }

        input.xDelta = 0;
        input.yDelta = 0;
        if (remainingCount >= 2 && secondRemaining < pointerCount) {
          SetMousePositionFromTwoPointers(input, event, firstRemaining, secondRemaining);
          m_lastPinchDistance = GetPointerDistance(event, firstRemaining, secondRemaining);
          m_pinchActive = true;
          m_touchActive = false;
          input.MouseButtonStates[0][0] = false;
          input.MouseButtonStates[1][0] = false;
        } else if (firstRemaining < pointerCount) {
          SetMousePositionFromPointer(input, event, firstRemaining);
          m_lastTouchX = AMotionEvent_getX(event, firstRemaining);
          m_lastTouchY = AMotionEvent_getY(event, firstRemaining);
          m_touchActive = true;
          m_pinchActive = false;
          m_lastPinchDistance = 0.0f;
          input.MouseButtonStates[0][0] = true;
        }
        return 1;
      }

      if (action == AMOTION_EVENT_ACTION_MOVE) {
        if (pointerCount >= 2) {
          SetMousePositionFromTwoPointers(input, event, 0, 1);
          const float pinchDistance = GetPointerDistance(event, 0, 1);
          if (m_pinchActive) {
            input.scrollDelta += (pinchDistance - m_lastPinchDistance) / kPinchScrollPixelsPerStep;
          }
          m_lastPinchDistance = pinchDistance;
          m_pinchActive = true;
          m_touchActive = false;
          input.xDelta = 0;
          input.yDelta = 0;
          input.MouseButtonStates[0][0] = false;
          input.MouseButtonStates[1][0] = false;
          return 1;
        }

        const float x = AMotionEvent_getX(event, 0);
        const float y = AMotionEvent_getY(event, 0);
        SetMousePositionFromPointer(input, event, 0);
        if (m_touchActive && !m_pinchActive) {
          input.xDelta += static_cast<int>(std::lround(x - m_lastTouchX));
          input.yDelta += static_cast<int>(std::lround(y - m_lastTouchY));
        } else {
          input.xDelta = 0;
          input.yDelta = 0;
        }
        m_lastTouchX = x;
        m_lastTouchY = y;
        m_touchActive = true;
        m_pinchActive = false;
        m_lastPinchDistance = 0.0f;
        input.MouseButtonStates[0][0] = true;
        return 1;
      }
      return 1;
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
      const int32_t key = AKeyEvent_getKeyCode(event);
      const int32_t action = AKeyEvent_getAction(event);
      if (key == AKEYCODE_BACK) {
        if (appHandled) return 1;
        if (action == AKEY_EVENT_ACTION_DOWN) {
          pBaseApp->IManager.KeyStates[0][T800K_ESCAPE] = true;
        } else if (action == AKEY_EVENT_ACTION_UP) {
          pBaseApp->IManager.KeyStates[0][T800K_ESCAPE] = false;
          pBaseApp->IManager.KeyStates[1][T800K_ESCAPE] = false;
          if (!pBaseApp->IsModalActive()) {
            m_closeRequested = true;
          }
        }
        return 1;
      }
    }
    return 0;
  }

  void AndroidFramework::CreateVulkanRuntime() {
    if (!m_window) return;
    if (m_hasRuntime) {
      ResumeVulkanWindow();
      return;
    }
    DestroyVulkanRuntime();
    UpdateWindowSize();

    pVideoDriver = new VulkanDriver;
    g_pBaseDriver = pVideoDriver;
    pVideoDriver->SetDimensions(aplicationDescriptor.width, aplicationDescriptor.height);
    pVideoDriver->SetWindowHandle(WindowHandle::FromAndroidNativeWindow(m_window));
    pVideoDriver->InitDriver();
    RefreshEngineContextFromGlobals();
    pBaseApp->CreateAssets();
    pVideoDriver->BuildPipelineObjects();
    m_hasRuntime = true;
    m_surfaceActive = true;
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
    ClearEngineContext();
    m_hasRuntime = false;
    m_surfaceActive = false;
  }

  void AndroidFramework::SuspendVulkanWindow() {
    if (!m_surfaceActive || !pVideoDriver) return;
    static_cast<VulkanDriver*>(pVideoDriver)->SuspendWindowSurface();
    m_surfaceActive = false;
  }

  void AndroidFramework::ResumeVulkanWindow() {
    if (!m_hasRuntime || !pVideoDriver || !m_window) return;
    UpdateWindowSize();
    pVideoDriver->SetDimensions(aplicationDescriptor.width, aplicationDescriptor.height);
    pVideoDriver->SetWindowHandle(WindowHandle::FromAndroidNativeWindow(m_window));
    if (static_cast<VulkanDriver*>(pVideoDriver)->ResumeWindowSurface(
          m_window, aplicationDescriptor.width, aplicationDescriptor.height)) {
      pVideoDriver->BuildPipelineObjects();
      m_surfaceActive = true;
      T8_LOG_INFO("[AndroidFramework] Vulkan window resumed (%dx%d)",
                  aplicationDescriptor.width, aplicationDescriptor.height);
    } else {
      T8_LOG_ERROR("[AndroidFramework] Failed to resume Vulkan window surface");
    }
  }

  void AndroidFramework::UpdateWindowSize() {
    if (!m_window) return;
    if (g_config.flags.benchmark && g_config.width > 0 && g_config.height > 0) {
      ANativeWindow_setBuffersGeometry(m_window, g_config.width, g_config.height, 0);
    }
    int32_t w = ANativeWindow_getWidth(m_window);
    int32_t h = ANativeWindow_getHeight(m_window);
    if (w > 0 && h > 0 && h > w) {
      std::swap(w, h);
    }
    if (w > 0) aplicationDescriptor.width = w;
    if (h > 0) aplicationDescriptor.height = h;
  }

  void AndroidFramework::ResetTransientInput() {
    if (!pBaseApp) return;
    pBaseApp->IManager.xDelta = 0;
    pBaseApp->IManager.yDelta = 0;
    pBaseApp->IManager.scrollDelta = 0.0f;
  }

  void AndroidFramework::ClearTouchState() {
    m_touchActive = false;
    m_pinchActive = false;
    m_lastPinchDistance = 0.0f;
    if (!pBaseApp) return;
    pBaseApp->IManager.xDelta = 0;
    pBaseApp->IManager.yDelta = 0;
    pBaseApp->IManager.MouseButtonStates[0][0] = false;
    pBaseApp->IManager.MouseButtonStates[1][0] = false;
  }

  void AndroidFramework::RequestCloseApplication() {
    if (m_closing) return;
    m_closing = true;
    ClearTouchState();
    ClearReturnToNativePreference();
    m_paused = true;
    if (m_app && m_app->activity) {
      T8_LOG_INFO("[AndroidFramework] Back pressed; finishing NativeActivity");
      ANativeActivity_finish(m_app->activity);
    }
  }

  void AndroidFramework::ClearReturnToNativePreference() {
    if (!m_app || !m_app->activity || !m_app->activity->clazz) return;

    ScopedJniEnv jni(m_app->activity);
    JNIEnv* env = jni.env;
    if (!env) return;

    jobject activity = m_app->activity->clazz;
    jclass activityClass = env->GetObjectClass(activity);
    if (!activityClass) return;

    jmethodID getSharedPreferences = env->GetMethodID(
      activityClass, "getSharedPreferences",
      "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");
    env->DeleteLocalRef(activityClass);
    if (!getSharedPreferences || env->ExceptionCheck()) {
      env->ExceptionClear();
      return;
    }

    jstring prefsName = env->NewStringUTF(kLauncherPrefsName);
    if (!prefsName) return;
    jobject prefs = env->CallObjectMethod(activity, getSharedPreferences, prefsName, 0);
    env->DeleteLocalRef(prefsName);
    if (!prefs || env->ExceptionCheck()) {
      env->ExceptionClear();
      return;
    }

    jclass prefsClass = env->GetObjectClass(prefs);
    if (!prefsClass) {
      env->DeleteLocalRef(prefs);
      return;
    }
    jmethodID edit = env->GetMethodID(prefsClass, "edit", "()Landroid/content/SharedPreferences$Editor;");
    env->DeleteLocalRef(prefsClass);
    if (!edit || env->ExceptionCheck()) {
      env->ExceptionClear();
      env->DeleteLocalRef(prefs);
      return;
    }

    jobject editor = env->CallObjectMethod(prefs, edit);
    env->DeleteLocalRef(prefs);
    if (!editor || env->ExceptionCheck()) {
      env->ExceptionClear();
      return;
    }

    jclass editorClass = env->GetObjectClass(editor);
    if (!editorClass) {
      env->DeleteLocalRef(editor);
      return;
    }
    jmethodID putBoolean = env->GetMethodID(
      editorClass, "putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;");
    jmethodID apply = env->GetMethodID(editorClass, "apply", "()V");
    env->DeleteLocalRef(editorClass);
    if (!putBoolean || !apply || env->ExceptionCheck()) {
      env->ExceptionClear();
      env->DeleteLocalRef(editor);
      return;
    }

    jstring key = env->NewStringUTF(kReturnToNativeKey);
    if (!key) {
      env->DeleteLocalRef(editor);
      return;
    }
    jobject updatedEditor = env->CallObjectMethod(editor, putBoolean, key, JNI_FALSE);
    env->DeleteLocalRef(key);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      env->DeleteLocalRef(editor);
      return;
    }

    jobject editorToApply = updatedEditor ? updatedEditor : editor;
    env->CallVoidMethod(editorToApply, apply);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    }
    if (updatedEditor) env->DeleteLocalRef(updatedEditor);
    env->DeleteLocalRef(editor);
  }

} // namespace t850

#endif // OS_ANDROID
