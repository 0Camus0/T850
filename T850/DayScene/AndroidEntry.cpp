#include <Config.h>

#ifdef OS_ANDROID

#include <Application.h>
#include <core/android/AndroidFramework.h>
#include <core/Config.h>
#include <utils/ConfigRuntime.h>
#include <utils/Log.h>
#include <utils/AndroidAssets.h>

#include <android_native_app_glue.h>

#include <vector>
#include <string>

std::vector<std::string> g_args;

namespace {
  t850::AndroidFramework* GetFramework(android_app* app) {
    return static_cast<t850::AndroidFramework*>(app ? app->userData : nullptr);
  }

  void HandleCommand(android_app* app, int32_t cmd) {
    if (auto* framework = GetFramework(app)) {
      framework->OnAppCommand(cmd);
    }
  }

  int32_t HandleInput(android_app* app, AInputEvent* event) {
    if (auto* framework = GetFramework(app)) {
      return framework->OnInputEvent(event);
    }
    return 0;
  }
}

void android_main(android_app* state) {
  app_dummy();

  t850::Config defaultConfig;
  t850::g_config = defaultConfig;
  t850::g_config.api = "vulkan";
  t850::g_config.flags.fullscreen = true;
  t850::g_config.width = 1280;
  t850::g_config.height = 720;
  t850::g_config.startScene = 0;
  t850::g_config.logLevel = t850::Log::LVL_DEBUG;

  t850::Log::Init(static_cast<t850::Log::Level>(t850::g_config.logLevel),
                  t850::Log::T8_LOG_BACKEND_CONSOLE,
                  nullptr);
  t850::Log::SetSessionTag("android-vulkan");
  if (state && state->activity) {
    t850::SetAndroidAssetManager(state->activity->assetManager);
  }
  T8_LOG_INFO("[Android] T850 NativeActivity starting");

  t850::ApplicationDesc desc;
  t850::config::ConfigureApplicationDesc(t850::g_config, desc);
  desc.api = t850::GraphicsApi::VULKAN;
  desc.videoMode = t850::VideoMode::FULLSCREEN;

  App app;
  t850::AndroidFramework framework(&app, state);
  state->userData = &framework;
  state->onAppCmd = HandleCommand;
  state->onInputEvent = HandleInput;

  framework.InitGlobalVars();
  framework.OnCreateApplication(desc);
  framework.UpdateApplication();
  framework.OnDestroyApplication();

  state->userData = nullptr;
  T8_LOG_INFO("[Android] T850 NativeActivity stopped");
  t850::Log::Shutdown();
}

#endif // OS_ANDROID
