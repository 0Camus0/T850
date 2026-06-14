#include <Config.h>

#ifdef OS_ANDROID

#include <Application.h>
#include <core/android/AndroidFramework.h>
#include <core/Config.h>
#include <utils/ConfigRuntime.h>
#include <utils/Log.h>
#include <utils/RuntimeProfile.h>
#include <utils/AndroidAssets.h>
#include <utils/ResourceLocator.h>

#include <android_native_app_glue.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>
#include <string>

std::vector<std::string> g_args;
t850::AppBase* pApp = nullptr;

namespace {
  constexpr const char* kExtraScene = "com.t850.engine.extra.SCENE";
  constexpr const char* kExtraModel = "com.t850.engine.extra.MODEL";
  constexpr const char* kExtraSceneFile = "com.t850.engine.extra.SCENE_FILE";
  constexpr const char* kExtraLogLevel = "com.t850.engine.extra.LOG_LEVEL";
  constexpr const char* kExtraDumpFrame = "com.t850.engine.extra.DUMP_FRAME";
  constexpr const char* kExtraDumpSeconds = "com.t850.engine.extra.DUMP_SECONDS";
  constexpr const char* kExtraDebugFrames = "com.t850.engine.extra.DEBUG_FRAMES";
  constexpr const char* kExtraKeepRunning = "com.t850.engine.extra.KEEP_RUNNING";
  constexpr const char* kExtraReplaySnapshot = "com.t850.engine.extra.REPLAY_SNAPSHOT";
  constexpr const char* kExtraProfile = "com.t850.engine.extra.PROFILE";
  constexpr const char* kExtraProfileFrames = "com.t850.engine.extra.PROFILE_FRAMES";
  constexpr const char* kExtraBenchmark = "com.t850.engine.extra.BENCHMARK";
  constexpr const char* kExtraBenchmarkMatrix = "com.t850.engine.extra.BENCHMARK_MATRIX";
  constexpr const char* kExtraBenchmarkOutput = "com.t850.engine.extra.BENCHMARK_OUTPUT";
  constexpr const char* kExtraBenchmarkReport = "com.t850.engine.extra.BENCHMARK_REPORT";
  constexpr const char* kExtraBenchmarkFrames = "com.t850.engine.extra.BENCHMARK_FRAMES";
  constexpr const char* kExtraBenchmarkFixedDt = "com.t850.engine.extra.BENCHMARK_FIXED_DT";
  constexpr const char* kExtraWidth = "com.t850.engine.extra.WIDTH";
  constexpr const char* kExtraHeight = "com.t850.engine.extra.HEIGHT";
  constexpr const char* kExtraOffscreen = "com.t850.engine.extra.OFFSCREEN";
  constexpr const char* kExtraTelemetry = "com.t850.engine.extra.TELEMETRY";
  constexpr const char* kExtraTelemetryFrequencyFrames = "com.t850.engine.extra.TELEMETRY_FREQUENCY_FRAMES";
  constexpr const char* kExtraTelemetryOutput = "com.t850.engine.extra.TELEMETRY_OUTPUT";
  constexpr const char* kExtraAutoStartRagdoll = "com.t850.engine.extra.AUTO_START_RAGDOLL";
  constexpr const char* kExtraRagdollSpeedIndex = "com.t850.engine.extra.RAGDOLL_SPEED_INDEX";
  constexpr const char* kExtraSceneProfile = "com.t850.engine.extra.SCENE_PROFILE";

  struct AndroidJniEnv {
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    bool attached = false;

    explicit AndroidJniEnv(ANativeActivity* activity) {
      if (!activity || !activity->vm) return;
      vm = activity->vm;
      if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return;
      if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        attached = true;
      } else {
        env = nullptr;
      }
    }

    ~AndroidJniEnv() {
      if (attached && vm) {
        vm->DetachCurrentThread();
      }
    }
  };

  int GetIntentIntExtra(JNIEnv* env, jobject intent, jmethodID getIntExtra, const char* name, int fallback) {
    jstring key = env->NewStringUTF(name);
    if (!key) return fallback;
    jint value = env->CallIntMethod(intent, getIntExtra, key, static_cast<jint>(fallback));
    env->DeleteLocalRef(key);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return fallback;
    }
    return static_cast<int>(value);
  }

  float GetIntentFloatExtra(JNIEnv* env, jobject intent, jmethodID getFloatExtra, const char* name, float fallback) {
    jstring key = env->NewStringUTF(name);
    if (!key) return fallback;
    jfloat value = env->CallFloatMethod(intent, getFloatExtra, key, static_cast<jfloat>(fallback));
    env->DeleteLocalRef(key);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return fallback;
    }
    return static_cast<float>(value);
  }

  bool GetIntentBoolExtra(JNIEnv* env, jobject intent, jmethodID getBooleanExtra, const char* name, bool fallback) {
    jstring key = env->NewStringUTF(name);
    if (!key) return fallback;
    jboolean value = env->CallBooleanMethod(intent, getBooleanExtra, key, static_cast<jboolean>(fallback));
    env->DeleteLocalRef(key);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return fallback;
    }
    return value == JNI_TRUE;
  }

  std::string GetIntentStringExtra(JNIEnv* env, jobject intent, jmethodID getStringExtra, const char* name) {
    jstring key = env->NewStringUTF(name);
    if (!key) return {};
    jstring value = static_cast<jstring>(env->CallObjectMethod(intent, getStringExtra, key));
    env->DeleteLocalRef(key);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return {};
    }
    if (!value) return {};

    std::string out;
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars) {
      out = chars;
      env->ReleaseStringUTFChars(value, chars);
    }
    env->DeleteLocalRef(value);
    return out;
  }

  std::string GetStaticStringField(JNIEnv* env, jclass cls, const char* name) {
    jfieldID field = env->GetStaticFieldID(cls, name, "Ljava/lang/String;");
    if (!field || env->ExceptionCheck()) {
      env->ExceptionClear();
      return {};
    }
    jstring value = static_cast<jstring>(env->GetStaticObjectField(cls, field));
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return {};
    }
    if (!value) return {};

    std::string out;
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars) {
      out = chars;
      env->ReleaseStringUTFChars(value, chars);
    }
    env->DeleteLocalRef(value);
    return out;
  }

  void CaptureAndroidBuildInfo(android_app* state) {
    if (!state || !state->activity) return;
    AndroidJniEnv jni(state->activity);
    JNIEnv* env = jni.env;
    if (!env) return;
    jclass buildClass = env->FindClass("android/os/Build");
    if (!buildClass || env->ExceptionCheck()) {
      env->ExceptionClear();
      return;
    }
    t850::SetAndroidBuildInfo(
      GetStaticStringField(env, buildClass, "MANUFACTURER"),
      GetStaticStringField(env, buildClass, "MODEL"),
      GetStaticStringField(env, buildClass, "HARDWARE"),
      GetStaticStringField(env, buildClass, "BOARD"),
      GetStaticStringField(env, buildClass, "SOC_MODEL"));
    env->DeleteLocalRef(buildClass);
  }

  void ApplyLauncherIntent(android_app* state) {
    if (!state || !state->activity || !state->activity->clazz) return;

    AndroidJniEnv jni(state->activity);
    JNIEnv* env = jni.env;
    if (!env) return;

    jobject activity = state->activity->clazz;
    jclass activityClass = env->GetObjectClass(activity);
    if (!activityClass) return;

    jmethodID getIntent = env->GetMethodID(activityClass, "getIntent", "()Landroid/content/Intent;");
    if (!getIntent) {
      env->DeleteLocalRef(activityClass);
      env->ExceptionClear();
      return;
    }

    jobject intent = env->CallObjectMethod(activity, getIntent);
    env->DeleteLocalRef(activityClass);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return;
    }
    if (!intent) return;

    jclass intentClass = env->GetObjectClass(intent);
    if (!intentClass) {
      env->DeleteLocalRef(intent);
      return;
    }

    jmethodID getIntExtra = env->GetMethodID(intentClass, "getIntExtra", "(Ljava/lang/String;I)I");
    jmethodID getFloatExtra = env->GetMethodID(intentClass, "getFloatExtra", "(Ljava/lang/String;F)F");
    jmethodID getBooleanExtra = env->GetMethodID(intentClass, "getBooleanExtra", "(Ljava/lang/String;Z)Z");
    jmethodID getStringExtra = env->GetMethodID(intentClass, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    }

    if (getIntExtra) {
      int scene = GetIntentIntExtra(env, intent, getIntExtra, kExtraScene, t850::g_config.startScene);
      if (scene >= 0 && scene <= 4) {
        t850::g_config.startScene = scene;
      }

      int logLevel = GetIntentIntExtra(env, intent, getIntExtra, kExtraLogLevel, t850::g_config.logLevel);
      if (logLevel >= t850::Log::LVL_ERROR && logLevel <= t850::Log::LVL_TRACE) {
        t850::g_config.logLevel = logLevel;
      }

      int dumpFrame = GetIntentIntExtra(env, intent, getIntExtra, kExtraDumpFrame, -1);
      if (dumpFrame >= 0) {
        t850::g_config.flags.dumpEnabled = true;
        t850::g_config.flags.dumpByFrame = true;
        t850::g_config.dumpFrame = dumpFrame;
      }

      int profileFrames = GetIntentIntExtra(env, intent, getIntExtra, kExtraProfileFrames, t850::g_config.profileFrames);
      if (profileFrames > 0) {
        t850::g_config.profileFrames = profileFrames;
      }
      int width = GetIntentIntExtra(env, intent, getIntExtra, kExtraWidth, t850::g_config.width);
      if (width > 0) {
        t850::g_config.width = width;
      }
      int height = GetIntentIntExtra(env, intent, getIntExtra, kExtraHeight, t850::g_config.height);
      if (height > 0) {
        t850::g_config.height = height;
      }
      int telemetryFrequency = GetIntentIntExtra(env, intent, getIntExtra, kExtraTelemetryFrequencyFrames, t850::g_config.runtimeTelemetryFrequencyFrames);
      if (telemetryFrequency >= 0) {
        t850::g_config.runtimeTelemetryFrequencyFrames = telemetryFrequency;
      }
      int benchmarkFrames = GetIntentIntExtra(env, intent, getIntExtra, kExtraBenchmarkFrames, t850::g_config.benchmarkFrameLimit);
      if (benchmarkFrames > 0) {
        t850::g_config.benchmarkFrameLimit = benchmarkFrames;
      }
      t850::g_config.ragdollSimulationSpeedIndex =
        GetIntentIntExtra(env, intent, getIntExtra, kExtraRagdollSpeedIndex, t850::g_config.ragdollSimulationSpeedIndex);
    }

    if (getFloatExtra && t850::g_config.dumpFrame < 0) {
      float dumpSeconds = GetIntentFloatExtra(env, intent, getFloatExtra, kExtraDumpSeconds, -1.0f);
      if (dumpSeconds >= 0.0f) {
        t850::g_config.flags.dumpEnabled = true;
        t850::g_config.flags.dumpByFrame = false;
        t850::g_config.dumpSeconds = dumpSeconds;
      }
      float benchmarkFixedDt = GetIntentFloatExtra(env, intent, getFloatExtra, kExtraBenchmarkFixedDt, t850::g_config.benchmarkFixedDt);
      if (benchmarkFixedDt > 0.0f) {
        t850::g_config.benchmarkFixedDt = benchmarkFixedDt;
      }
    }

    if (getBooleanExtra) {
      if (GetIntentBoolExtra(env, intent, getBooleanExtra, kExtraDebugFrames, false)) {
        t850::g_config.flags.debugFrames = true;
        t850::g_config.flags.dumpEnabled = true;
      }
      t850::g_config.flags.keepRunning =
        GetIntentBoolExtra(env, intent, getBooleanExtra, kExtraKeepRunning, t850::g_config.flags.keepRunning);
      t850::g_config.flags.profile =
        GetIntentBoolExtra(env, intent, getBooleanExtra, kExtraProfile, t850::g_config.flags.profile);
      t850::g_config.flags.benchmark =
        GetIntentBoolExtra(env, intent, getBooleanExtra, kExtraBenchmark, t850::g_config.flags.benchmark);
      t850::g_config.flags.benchmarkMatrix =
        GetIntentBoolExtra(env, intent, getBooleanExtra, kExtraBenchmarkMatrix, t850::g_config.flags.benchmarkMatrix);
      if (t850::g_config.flags.benchmarkMatrix) {
        t850::g_config.flags.benchmark = true;
      }
      t850::g_config.flags.offscreen =
        GetIntentBoolExtra(env, intent, getBooleanExtra, kExtraOffscreen, t850::g_config.flags.offscreen);
      t850::g_config.flags.runtimeTelemetry =
        GetIntentBoolExtra(env, intent, getBooleanExtra, kExtraTelemetry, t850::g_config.flags.runtimeTelemetry);
      t850::g_config.flags.autoStartRagdoll =
        GetIntentBoolExtra(env, intent, getBooleanExtra, kExtraAutoStartRagdoll, t850::g_config.flags.autoStartRagdoll);
    }

    if (getStringExtra) {
      std::string model = GetIntentStringExtra(env, intent, getStringExtra, kExtraModel);
      if (!model.empty()) {
        t850::g_config.modelPath = model;
        t850::g_config.sceneFilePath.clear();
      }

      std::string sceneFile = GetIntentStringExtra(env, intent, getStringExtra, kExtraSceneFile);
      if (!sceneFile.empty()) {
        t850::g_config.sceneFilePath = sceneFile;
        t850::g_config.modelPath.clear();
      }

      std::string replaySnapshot = GetIntentStringExtra(env, intent, getStringExtra, kExtraReplaySnapshot);
      if (!replaySnapshot.empty()) {
        t850::g_config.replaySnapshotPath = replaySnapshot;
      }

      std::string sceneProfile = GetIntentStringExtra(env, intent, getStringExtra, kExtraSceneProfile);
      if (!sceneProfile.empty()) {
        t850::g_config.sceneProfile = sceneProfile;
      }

      std::string benchmarkOutput = GetIntentStringExtra(env, intent, getStringExtra, kExtraBenchmarkOutput);
      if (!benchmarkOutput.empty()) {
        t850::g_config.benchmarkOutputPath = benchmarkOutput;
      }
      std::string benchmarkReport = GetIntentStringExtra(env, intent, getStringExtra, kExtraBenchmarkReport);
      if (!benchmarkReport.empty()) {
        t850::g_config.benchmarkReportPath = benchmarkReport;
      }

      std::string telemetryOutput = GetIntentStringExtra(env, intent, getStringExtra, kExtraTelemetryOutput);
      if (!telemetryOutput.empty()) {
        t850::g_config.runtimeTelemetryOutputPath = telemetryOutput;
      }
    }

    env->DeleteLocalRef(intentClass);
    env->DeleteLocalRef(intent);
  }

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
  t850::Config defaultConfig;
  t850::g_config = defaultConfig;
  t850::g_config.api = "vulkan";
  t850::g_config.flags.fullscreen = true;
  t850::g_config.width = 1280;
  t850::g_config.height = 720;
  t850::g_config.startScene = 0;
  t850::g_config.logLevel = t850::Log::LVL_DEBUG;
  ApplyLauncherIntent(state);
  if (t850::g_config.flags.benchmarkMatrix) {
    t850::g_config.api = "vulkan";
    t850::g_config.width = 1920;
    t850::g_config.height = 1080;
    t850::g_config.flags.offscreen = false;
    t850::g_config.startScene = 1;
  }
  CaptureAndroidBuildInfo(state);

  t850::Log::Init(static_cast<t850::Log::Level>(t850::g_config.logLevel),
                  t850::Log::T8_LOG_BACKEND_ANDROID_LOGCAT,
                  nullptr);
  t850::Log::SetSessionTag("android-vulkan");
  if (state && state->activity) {
    t850::SetAndroidAssetManager(state->activity->assetManager);
    const char* dataPath = state->activity->externalDataPath
      ? state->activity->externalDataPath
      : state->activity->internalDataPath;
    if (dataPath) {
      mkdir(dataPath, 0775);
      t850::ResourceLocator::Instance().SetBasePath(dataPath);
      t850::ResourceLocator::Instance().SetCachePath(dataPath);
      if (chdir(dataPath) == 0) {
        T8_LOG_INFO("[Android] Working directory: %s", dataPath);
      }
    }
  }
  T8_LOG_INFO("[Android] T850 NativeActivity starting");
  T8_LOG_INFO("[Android] Launch config: scene=%d model='%s' sceneFile='%s' sceneProfile='%s' size=%dx%d logLevel=%d api=vulkan benchmark=%d benchmarkMatrix=%d benchmarkOutput='%s' benchmarkReport='%s' offscreen=%d dumpEnabled=%d dumpByFrame=%d dumpFrame=%d dumpSeconds=%.3f keepRunning=%d profile=%d profileFrames=%d telemetry=%d telemetryFrequencyFrames=%d telemetryOutput='%s' replay='%s'",
              t850::g_config.startScene, t850::g_config.modelPath.c_str(), t850::g_config.sceneFilePath.c_str(), t850::g_config.sceneProfile.c_str(),
              t850::g_config.width,
              t850::g_config.height,
              t850::g_config.logLevel,
              t850::g_config.flags.benchmark ? 1 : 0,
              t850::g_config.flags.benchmarkMatrix ? 1 : 0,
              t850::g_config.benchmarkOutputPath.c_str(),
              t850::g_config.benchmarkReportPath.c_str(),
              t850::g_config.flags.offscreen ? 1 : 0,
              t850::g_config.flags.dumpEnabled ? 1 : 0,
              t850::g_config.flags.dumpByFrame ? 1 : 0,
              t850::g_config.dumpFrame,
              t850::g_config.dumpSeconds,
              t850::g_config.flags.keepRunning ? 1 : 0,
              t850::g_config.flags.profile ? 1 : 0,
              t850::g_config.profileFrames,
              t850::g_config.flags.runtimeTelemetry ? 1 : 0,
              t850::g_config.runtimeTelemetryFrequencyFrames,
              t850::g_config.runtimeTelemetryOutputPath.c_str(),
              t850::g_config.replaySnapshotPath.c_str());

  t850::ApplicationDesc desc;
  t850::config::ConfigureApplicationDesc(t850::g_config, desc);
  desc.api = t850::GraphicsApi::VULKAN;
  desc.videoMode = t850::VideoMode::FULLSCREEN;

  App app;
  pApp = &app;
  t850::AndroidFramework framework(&app, state);
  state->userData = &framework;
  state->onAppCmd = HandleCommand;
  state->onInputEvent = HandleInput;

  framework.InitGlobalVars();
  framework.OnCreateApplication(desc);
  framework.UpdateApplication();
  framework.OnDestroyApplication();

  state->userData = nullptr;
  pApp = nullptr;
  T8_LOG_INFO("[Android] T850 NativeActivity stopped");
  t850::Log::Shutdown();
}

#endif // OS_ANDROID
