#include <pch.h>
#include <core/WebFramework.h>

#ifdef __EMSCRIPTEN__
#include <core/Config.h>
#include <core/EngineContext.h>
#include <debug/RuntimeTelemetry.h>
#include <utils/Log.h>
#include <utils/ResourceLocator.h>
#include <utils/ThreadPool.h>
#include <video/webgpu/WebGPUDriver.h>
#include <SDL3/SDL.h>
#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/heap.h>
#include <glaze/glaze.hpp>
#include <stdexcept>

namespace t850 {
WebFramework::WebFramework(AppBase* app) : RootFramework(app) {
  pBaseApp->SetParentFramework(this);
}

void WebFramework::InitGlobalVars() {
  if (g_config.webAssetBaseUrl.empty())
    throw std::invalid_argument("Browser startup requires --webAssetBaseUrl");
  void* data = nullptr;
  int length = 0;
  int error = 0;
  emscripten_wget_data((g_config.webAssetBaseUrl + "/index.json").c_str(), &data, &length, &error);
  std::unique_ptr<void, decltype(&std::free)> owned(data, &std::free);
  if (error) throw std::runtime_error("Cannot download browser asset catalog");
  std::vector<std::string> paths;
  if (glz::read_json(paths, std::string_view(static_cast<const char*>(data), length)))
    throw std::runtime_error("Invalid browser asset catalog");
  auto& resources = ResourceLocator::Instance();
  resources.SetBasePath("/assets");
  resources.SetCachePath("/persistent");
  resources.SetWebAssets(g_config.webAssetBaseUrl, paths);
}

void WebFramework::OnCreateApplication(ApplicationDesc desc) {
  aplicationDescriptor = desc;
  if (desc.api != GraphicsApi::WEBGPU)
    throw std::invalid_argument("The browser target requires WebGPU");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    throw std::runtime_error(SDL_GetError());
  m_window = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, SDL_WINDOW_RESIZABLE);
  if (!m_window) throw std::runtime_error(SDL_GetError());
  int pixelWidth = 0;
  int pixelHeight = 0;
  if (!SDL_GetWindowSizeInPixels(m_window, &pixelWidth, &pixelHeight) || pixelWidth <= 0 || pixelHeight <= 0)
    throw std::runtime_error("Browser canvas has no drawable size");
  aplicationDescriptor.width = g_config.width = pixelWidth;
  aplicationDescriptor.height = g_config.height = pixelHeight;
  SDL_StartTextInput(m_window);
  InitGlobalThreadPool();
  RuntimeTelemetry::InitializeFromConfig(g_config);
  pBaseApp->InitVars();
  MAIN_THREAD_EM_ASM({
    if (globalThis.t850Touch) globalThis.t850Touch.attach(new Int32Array(HEAP32.buffer, $0, 8));
  }, m_touchInput.data());
  ChangeAPI(desc.api);
}

void WebFramework::ChangeAPI(GraphicsApi::E api) {
  if (api != GraphicsApi::WEBGPU)
    throw std::invalid_argument("Only WebGPU is available in the browser");
  if (pVideoDriver) return;
  auto driver = std::make_unique<WebGPUDriver>();
  webgpu::ShaderFlow flow;
  if (!webgpu::ParseShaderFlow(g_config.webgpuShaderFlow, flow))
    throw std::invalid_argument("Invalid browser shader flow");
  driver->SetShaderFlow(flow);
  driver->SetDimensions(aplicationDescriptor.width, aplicationDescriptor.height);
  driver->SetWindowHandle(WindowHandle::FromSDL(m_window));
  driver->InitDriver();
  pVideoDriver = driver.release();
  g_pBaseDriver = pVideoDriver;
  RefreshEngineContextFromGlobals();
  m_inited = true;
  pBaseApp->CreateAssets();
  pVideoDriver->BuildPipelineObjects();
  T8_LOG_INFO("[WebFramework] Runtime ready");
  MAIN_THREAD_ASYNC_EM_ASM({ globalThis.dispatchEvent(new Event('t850-runtime-ready')); });
}

void WebFramework::UpdateApplication() {
  emscripten_set_main_loop_arg(Tick, this, 120, false);
  EM_ASM({
    const channel = new MessageChannel();
    const callbacks = [];
    let burstFrames = 0;
    channel.port1.onmessage = () => callbacks.shift()();
    MainLoop.setImmediate = callback => {
      if (++burstFrames == 8) {
        burstFrames = 0;
        setTimeout(callback, 0);
      } else {
        callbacks.push(callback);
        channel.port2.postMessage(0);
      }
    };
  });
  emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 1);
  emscripten_unwind_to_js_event_loop();
}

void WebFramework::Tick(void* context) {
  auto& framework = *static_cast<WebFramework*>(context);
  const double started = emscripten_get_now();
  if (framework.m_lastTick) framework.m_intervalTotal += started - framework.m_lastTick;
  framework.m_lastTick = started;
  try {
    framework.ProcessInput();
    if (framework.m_inited && !framework.pBaseApp->bPaused) {
      framework.pBaseApp->OnUpdate();
      framework.m_workTotal += emscripten_get_now() - started;
      ++framework.m_frames;
      if (framework.m_frames % 10 == 0) {
        MAIN_THREAD_ASYNC_EM_ASM({
          if (globalThis.t850) {
            globalThis.t850.frames = $0;
            globalThis.t850.renderSize = Array.of($1, $2);
            globalThis.t850.draws = $3;
            globalThis.t850.memoryBytes = $4;
            globalThis.t850.workerTiming = Object.assign({}, { intervalMs: $5, workMs: $6 });
            globalThis.t850.input = Object.assign({}, { keys: $7, mouse: $8, focus: $9, relative: !!$10, forward: !!$11 });
          }
        }, framework.m_frames, g_config.width, g_config.height,
          static_cast<WebGPUDriver*>(framework.pVideoDriver)->DrawCount(), emscripten_get_heap_size(),
          framework.m_intervalTotal / 10, framework.m_workTotal / 10,
          framework.m_keyEvents, framework.m_mouseEvents, framework.m_focusEvents, SDL_GetWindowRelativeMouseMode(framework.m_window),
          framework.pBaseApp->IManager.PressedKey(T800K_w));
        MAIN_THREAD_ASYNC_EM_ASM({
          if (globalThis.t850) globalThis.t850.touch = Object.assign({}, { active: !!$0, moveX: $1, moveY: $2, lookX: $3, lookY: $4, jump: !!$5, sprint: !!$6, breakBlock: $7, placeBlock: $8 });
        },
          framework.m_touchInput[0].load(), framework.pBaseApp->IManager.Gamepad.leftX, framework.pBaseApp->IManager.Gamepad.leftY,
          framework.pBaseApp->IManager.Gamepad.rightX, framework.pBaseApp->IManager.Gamepad.rightY,
          framework.pBaseApp->IManager.Gamepad.buttonSouth, framework.pBaseApp->IManager.Gamepad.leftStick,
          framework.pBaseApp->IManager.Gamepad.rightTrigger, framework.pBaseApp->IManager.Gamepad.leftTrigger);
        framework.m_intervalTotal = framework.m_workTotal = 0;
      }
    }
    const bool idle = !framework.m_inited || framework.pBaseApp->bPaused;
    if (idle != framework.m_idleLoop) {
      framework.m_idleLoop = idle;
      emscripten_set_main_loop_timing(idle ? EM_TIMING_SETTIMEOUT : EM_TIMING_SETIMMEDIATE, idle ? 100 : 1);
      framework.m_lastTick = framework.m_intervalTotal = framework.m_workTotal = 0;
    }
  } catch (const std::exception& error) {
    T8_LOG_ERROR("[WebFramework] Runtime failure: %s", error.what());
    emscripten_cancel_main_loop();
  }
}

void WebFramework::ClearInput() {
  auto& input = pBaseApp->IManager;
  for (auto& states : input.KeyStates) for (auto& state : states) state = false;
  for (auto& states : input.MouseButtonStates) for (auto& state : states) state = false;
  input.xDelta = input.yDelta = 0;
  input.scrollDelta = 0;
  input.textInput.clear();
}

void WebFramework::ProcessInput() {
  auto& input = pBaseApp->IManager;
  input.xDelta = input.yDelta = 0;
  input.scrollDelta = 0;
  const bool touchActive = m_touchInput[0].load() != 0;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (m_inputEventHandler) m_inputEventHandler(m_inputEventUserdata, &event);
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
      ++m_keyEvents;
      const int key = SDL3KeyToSTDKEY(static_cast<unsigned int>(event.key.key));
      if (key >= 0 && key < MAXKEYS) {
        input.KeyStates[0][key] = event.type == SDL_EVENT_KEY_DOWN;
        if (event.type == SDL_EVENT_KEY_UP) input.KeyStates[1][key] = false;
      }
      break;
    }
    case SDL_EVENT_TEXT_INPUT:
      if (event.text.text) input.textInput.append(event.text.text);
      break;
    case SDL_EVENT_MOUSE_MOTION:
      ++m_mouseEvents;
      input.xDelta += static_cast<int>(event.motion.xrel);
      input.yDelta += static_cast<int>(event.motion.yrel);
      input.mouseX = static_cast<int>(event.motion.x);
      input.mouseY = static_cast<int>(event.motion.y);
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !touchActive && pBaseApp->WantsRelativeMouseMode() &&
          !SDL_GetWindowRelativeMouseMode(m_window)) {
        SDL_SetWindowRelativeMouseMode(m_window, true);
        break;
      }
      const int button = event.button.button - 1;
      if (button >= 0 && button < MAXMOUSEBUTTONS) {
        input.MouseButtonStates[0][button] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) input.MouseButtonStates[1][button] = false;
      }
      break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
      input.scrollDelta += event.wheel.y;
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      ++m_focusEvents;
      ClearInput();
      break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      ++m_focusEvents;
      break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      if (pVideoDriver && event.window.data1 > 0 && event.window.data2 > 0) {
        g_config.width = event.window.data1;
        g_config.height = event.window.data2;
        pVideoDriver->ResizeSwapchain(g_config.width, g_config.height);
      }
      break;
    default: break;
    }
  }
  if ((touchActive || !pBaseApp->WantsRelativeMouseMode()) && SDL_GetWindowRelativeMouseMode(m_window))
    SDL_SetWindowRelativeMouseMode(m_window, false);
  ReadTouchInput();
}

void WebFramework::ReadTouchInput() {
  const int commands = m_touchInput[7].exchange(0);
  pBaseApp->IManager.toggleCameraView = (commands & 1) != 0;
  pBaseApp->IManager.toggleInvertY = (commands & 2) != 0;
  auto& gamepad = pBaseApp->IManager.Gamepad;
  const bool active = m_touchInput[0].load() != 0;
  const int pressed = m_touchInput[6].exchange(0);
  if (!active) {
    if (gamepad.name == "Browser touch") gamepad = {};
    return;
  }
  if (gamepad.name != "Browser touch") {
    gamepad = {};
    gamepad.connected = gamepad.enabled = true;
    gamepad.name = "Browser touch";
  }
  const auto axis = [this](size_t index) { return std::clamp(m_touchInput[index].load() / 1000.0f, -1.0f, 1.0f); };
  const int held = m_touchInput[5].load() | pressed;
  gamepad.leftX = axis(1);
  gamepad.leftY = axis(2);
  gamepad.rightX = axis(3);
  gamepad.rightY = axis(4);
  gamepad.buttonSouth = (held & 1) != 0;
  gamepad.leftStick = (held & 2) != 0;
  gamepad.rightTrigger = (held & 4) ? 1.0f : 0.0f;
  gamepad.leftTrigger = (held & 8) ? 1.0f : 0.0f;
  gamepad.dpadLeftPressed = (pressed & 16) != 0;
  gamepad.dpadRightPressed = (pressed & 32) != 0;
}

void WebFramework::OnInterruptApplication() { ClearInput(); pBaseApp->OnPause(); }
void WebFramework::OnResumeApplication() { ClearInput(); pBaseApp->OnResume(); }
void WebFramework::ResetApplication() { pBaseApp->OnReset(); }

void WebFramework::OnDestroyApplication() {
  emscripten_cancel_main_loop();
  MAIN_THREAD_EM_ASM({ if (globalThis.t850Touch) globalThis.t850Touch.detach(); });
  if (pVideoDriver) {
    pVideoDriver->FlushGPUResources();
    pBaseApp->DestroyAssets();
    delete pVideoDriver;
    pVideoDriver = g_pBaseDriver = nullptr;
  }
  RuntimeTelemetry::Shutdown();
  ShutdownGlobalThreadPool();
  ClearEngineContext();
  if (m_window) SDL_DestroyWindow(m_window);
  m_window = nullptr;
  SDL_Quit();
  m_inited = false;
}
}
#endif