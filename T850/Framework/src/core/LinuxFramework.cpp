#include <pch.h>

#include <core/LinuxFramework.h>
#include <core/EngineContext.h>
#include <core/Config.h>
#include <video/vulkan/VulkanDriver.h>
#include <utils/Log.h>
#include <utils/ThreadPool.h>
#include <utils/ConfigRuntime.h>
#include <utils/DeviceInfo.h>
#include <debug/RuntimeTelemetry.h>
#include <navigation/NavigationSystem.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

extern std::vector<std::string> g_args;

namespace t850 {
  namespace {
    float NormalizeGamepadAxis(Sint16 value, Sint16 deadzone = 8000) {
      if (std::abs(value) <= deadzone) {
        return 0.0f;
      }
      const float sign = value < 0 ? -1.0f : 1.0f;
      const float magnitude = (std::abs(static_cast<int>(value)) - deadzone) / static_cast<float>(32767 - deadzone);
      return sign * (std::min)(1.0f, (std::max)(0.0f, magnitude));
    }

    float NormalizeGamepadTrigger(Sint16 value) {
      constexpr Sint16 deadzone = 2000;
      if (value <= deadzone) {
        return 0.0f;
      }
      return (std::min)(1.0f, (value - deadzone) / static_cast<float>(32767 - deadzone));
    }
  }

  LinuxFramework* LinuxFramework::thiz = nullptr;

  LinuxFramework::LinuxFramework(AppBase* appBase)
    : RootFramework(appBase), m_alive(true), m_pWindow(nullptr) {
    pBaseApp->SetParentFramework(this);
    LinuxFramework::thiz = this;
  }

  LinuxFramework::~LinuxFramework() {
    OnDestroyApplication();
  }

  void LinuxFramework::InitGlobalVars() {}

  void LinuxFramework::OnCreateApplication(ApplicationDesc desc) {
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

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
      T8_LOG_ERROR("[LinuxFramework] SDL initialization failed: %s", SDL_GetError());
      m_alive = false;
      return;
    }
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    T8_LOG_INFO("[Input] SDL video driver: %s (relative mouse mode %s)",
                videoDriver ? videoDriver : "unknown",
                SDL_GetCurrentVideoDriver() && std::string(SDL_GetCurrentVideoDriver()) == "wayland"
                    ? "uses the pointer-constraints protocol" : "uses pointer warping");

    pBaseApp->InitVars();
    InitializeGamepads();
    ChangeAPI(GraphicsApi::VULKAN);
    m_inited = pVideoDriver != nullptr;
    // As on Windows, main() runs the loop (or a self-test) and then OnDestroyApplication.
    if (!m_inited) m_alive = false;
  }

  void LinuxFramework::OnDestroyApplication() {
    if (!m_inited && !pVideoDriver && !m_pWindow && !m_gamepad) {
      return;
    }

    ShutdownGamepads();
    if (pVideoDriver) {
      pVideoDriver->FlushGPUResources();
      if (pBaseApp) {
        pBaseApp->DestroyAssets();
      }
      pVideoDriver->DestroyDriver();
      delete pVideoDriver;
      pVideoDriver = nullptr;
      g_pBaseDriver = nullptr;
    }

    RuntimeTelemetry::Shutdown();
    ShutdownGlobalThreadPool();
    ClearEngineContext();

    if (m_pWindow) {
      SDL_DestroyWindow(m_pWindow);
      m_pWindow = nullptr;
    }
    SDL_Quit();
    m_inited = false;
  }

  void LinuxFramework::OnInterruptApplication() {}
  void LinuxFramework::OnResumeApplication() {}

  void LinuxFramework::UpdateApplication() {
    while (m_alive) {
      ProcessInput();
      if (!m_alive) {
        break;
      }
      pBaseApp->OnUpdate();
    }
  }

  void LinuxFramework::ProcessInput() {
    // Diagnostic harness: inject synthetic SDL mouse events instead of touching the real
    // cursor. Enable with T850_CAMERA_INPUT_TEST=1 and run with --unattended so relative
    // mode is off. The camera itself logs [CameraMouse] at --logLevel verbose.
    static const bool s_cameraInputTest = std::getenv("T850_CAMERA_INPUT_TEST") != nullptr;
    static int s_cameraTestFrame = 0;

    pBaseApp->IManager.scrollDelta = 0.0f;
    pBaseApp->IManager.xDelta = 0;
    pBaseApp->IManager.yDelta = 0;

    if (s_cameraInputTest) {
      if (s_cameraTestFrame == 10) {
        SDL_Event down{};
        down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        down.button.button = SDL_BUTTON_LEFT;
        down.button.down = true;
        down.button.clicks = 1;
        down.button.which = 0;
        SDL_PushEvent(&down);
      }
      if (s_cameraTestFrame >= 10 && s_cameraTestFrame < 30) {
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.which = 0;
        motion.motion.state = SDL_BUTTON_LMASK;
        motion.motion.xrel = 2.0f;
        motion.motion.yrel = 1.0f;
        motion.motion.x = static_cast<float>(100 + (s_cameraTestFrame - 10) * 2);
        motion.motion.y = static_cast<float>(100 + (s_cameraTestFrame - 10));
        SDL_PushEvent(&motion);
      }
      if (s_cameraTestFrame == 30) {
        SDL_Event up{};
        up.type = SDL_EVENT_MOUSE_BUTTON_UP;
        up.button.button = SDL_BUTTON_LEFT;
        up.button.down = false;
        up.button.clicks = 1;
        up.button.which = 0;
        SDL_PushEvent(&up);
      }
      ++s_cameraTestFrame;
    }

    float eventMouseX = 0.0f;
    float eventMouseY = 0.0f;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_QUIT:
        ReleaseMouseMode();
        m_alive = false;
        break;

      case SDL_EVENT_WINDOW_FOCUS_LOST:
      case SDL_EVENT_WINDOW_MINIMIZED:
      case SDL_EVENT_WINDOW_HIDDEN:
        // Do not keep the pointer grabbed or hidden while another window owns focus.
        ReleaseMouseMode();
        break;

      case SDL_EVENT_WINDOW_FOCUS_GAINED:
      case SDL_EVENT_WINDOW_RESTORED:
      case SDL_EVENT_WINDOW_SHOWN:
        ResetInputAfterWindowStateChange();
        break;

      case SDL_EVENT_KEY_DOWN: {
        const int t800key = SDL3KeyToSTDKEY(static_cast<unsigned int>(event.key.key));
        if (t800key == T800K_ESCAPE) {
          if (!pBaseApp || !pBaseApp->IsModalActive()) {
            m_alive = false;
          }
        }
        if (t800key >= 0 && t800key < MAXKEYS) {
          pBaseApp->IManager.KeyStates[0][t800key] = true;
        }
      } break;

      case SDL_EVENT_KEY_UP: {
        const int t800key = SDL3KeyToSTDKEY(static_cast<unsigned int>(event.key.key));
        if (t800key >= 0 && t800key < MAXKEYS) {
          pBaseApp->IManager.KeyStates[0][t800key] = false;
          pBaseApp->IManager.KeyStates[1][t800key] = false;
        }
      } break;

      case SDL_EVENT_TEXT_INPUT:
        if (event.text.text) {
          pBaseApp->IManager.textInput.append(event.text.text);
        }
        break;

      case SDL_EVENT_MOUSE_MOTION:
        // Keep the fractional part: high-rate mice emit sub-pixel xrel values, and an
        // (int) cast per event silently loses up to one pixel per event.
        eventMouseX += event.motion.xrel;
        eventMouseY += event.motion.yrel;
        pBaseApp->IManager.mouseX = static_cast<int>(event.motion.x);
        pBaseApp->IManager.mouseY = static_cast<int>(event.motion.y);
        break;

      case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const int btn = event.button.button - 1;
        if (btn >= 0 && btn < MAXMOUSEBUTTONS) {
          pBaseApp->IManager.MouseButtonStates[0][btn] = true;
        }
      } break;

      case SDL_EVENT_MOUSE_BUTTON_UP: {
        const int btn = event.button.button - 1;
        if (btn >= 0 && btn < MAXMOUSEBUTTONS) {
          pBaseApp->IManager.MouseButtonStates[0][btn] = false;
          pBaseApp->IManager.MouseButtonStates[1][btn] = false;
        }
      } break;

      case SDL_EVENT_MOUSE_WHEEL: {
        float wheelY = event.wheel.y;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
          wheelY = -wheelY;
        }
        pBaseApp->IManager.scrollDelta += wheelY;
      } break;

      case SDL_EVENT_WINDOW_MAXIMIZED:
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        int width = 0;
        int height = 0;
        if (m_pWindow && SDL_GetWindowSizeInPixels(m_pWindow, &width, &height)) {
          const unsigned int newWidth = static_cast<unsigned int>((std::max)(1, width));
          const unsigned int newHeight = static_cast<unsigned int>((std::max)(1, height));
          // Wayland can emit PIXEL_SIZE_CHANGED without an actual size change (and can
          // emit it more than once for one resize). Only reset the delta baseline when
          // the size really changed, otherwise a held look-drag would stutter or stop.
          if (newWidth == aplicationDescriptor.width &&
              newHeight == aplicationDescriptor.height) {
            break;
          }
          aplicationDescriptor.width = newWidth;
          aplicationDescriptor.height = newHeight;
          // A resize is not a focus change: keep held buttons and only drop the stale
          // delta. Clearing MouseButtonStates here released a held look-drag mid-move.
          ResetMouseDeltaBaseline();
          if (pVideoDriver) {
            pVideoDriver->ResizeSwapchain(static_cast<int>(aplicationDescriptor.width),
                                          static_cast<int>(aplicationDescriptor.height));
          }
        }
      } break;

      case SDL_EVENT_GAMEPAD_ADDED:
        OpenGamepad(static_cast<int>(event.gdevice.which));
        break;

      case SDL_EVENT_GAMEPAD_REMOVED:
        CloseGamepad(static_cast<int>(event.gdevice.which));
        break;

      default:
        break;
      }
    }

    // Match Win32Framework: relative mode is the only way the camera can keep turning
    // after the pointer reaches a screen/window edge. The event loop already accumulated
    // xrel/yrel for the absolute path; in relative mode SDL's relative state is the
    // authoritative one (and must be consumed exactly once per frame).
    UpdateMouseMode();
    const bool useRelativeMouse =
        m_relativeMouseMode && m_pWindow && SDL_GetWindowRelativeMouseMode(m_pWindow);
    float rawMouseX = eventMouseX;
    float rawMouseY = eventMouseY;
    if (useRelativeMouse) {
      float relativeX = 0.0f;
      float relativeY = 0.0f;
      SDL_GetRelativeMouseState(&relativeX, &relativeY);
      rawMouseX = relativeX;
      rawMouseY = relativeY;
    }

    // One round per frame, keeping the sub-pixel remainder for the next frame.
    m_pendingMouseX += rawMouseX;
    m_pendingMouseY += rawMouseY;
    const int frameMouseX = static_cast<int>(std::lround(m_pendingMouseX));
    const int frameMouseY = static_cast<int>(std::lround(m_pendingMouseY));
    m_pendingMouseX -= static_cast<float>(frameMouseX);
    m_pendingMouseY -= static_cast<float>(frameMouseY);
    pBaseApp->IManager.xDelta = frameMouseX;
    pBaseApp->IManager.yDelta = frameMouseY;
    if (useRelativeMouse) {
      T8_LOG_VERBOSE("[MouseInput] mode=relative raw=(%.3f,%.3f) pending=(%.3f,%.3f) delta=(%d,%d)",
                     rawMouseX, rawMouseY, m_pendingMouseX, m_pendingMouseY,
                     pBaseApp->IManager.xDelta, pBaseApp->IManager.yDelta);
    }

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    pBaseApp->IManager.mouseX = static_cast<int>(mouseX);
    pBaseApp->IManager.mouseY = static_cast<int>(mouseY);

    if (s_cameraInputTest && s_cameraTestFrame <= 80) {
      T8_LOG_INFO("[CameraInputTest] frame=%d xDelta=%d yDelta=%d mouse=(%d,%d) buttons=%d%d%d",
                  s_cameraTestFrame - 1,
                  pBaseApp->IManager.xDelta,
                  pBaseApp->IManager.yDelta,
                  pBaseApp->IManager.mouseX,
                  pBaseApp->IManager.mouseY,
                  pBaseApp->IManager.MouseButtonStates[0][0] ? 1 : 0,
                  pBaseApp->IManager.MouseButtonStates[0][1] ? 1 : 0,
                  pBaseApp->IManager.MouseButtonStates[0][2] ? 1 : 0);
    }

    RefreshGamepadState();
    if (pBaseApp->IManager.Gamepad.backPressed) {
      T8_LOG_INFO("[Input] Gamepad View/Back pressed");
    }
  }

  void LinuxFramework::ResetApplication() {}

  void LinuxFramework::ChangeAPI(GraphicsApi::E api) {
    if (api != GraphicsApi::VULKAN) {
      T8_LOG_INFO("[LinuxFramework] Steam Deck/Linux backend is Vulkan-only; forcing Vulkan");
    }

    if (m_inited && pVideoDriver) {
      pVideoDriver->FlushGPUResources();
      pBaseApp->DestroyAssets();
      pVideoDriver->DestroyDriver();
      delete pVideoDriver;
      pVideoDriver = nullptr;
      g_pBaseDriver = nullptr;
      ClearEngineContext();
    }

    if (m_pWindow) {
      SDL_DestroyWindow(m_pWindow);
      m_pWindow = nullptr;
    }

    aplicationDescriptor.api = GraphicsApi::VULKAN;
    std::string title = aplicationDescriptor.title.empty() ? "T850" : aplicationDescriptor.title;
    title += "   Vulkan Steam Deck";

    Uint64 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
    if (aplicationDescriptor.videoMode == VideoMode::FULLSCREEN) {
      flags |= SDL_WINDOW_FULLSCREEN;
    }

    // Centred on the primary display rather than at the desktop origin, which is the leftmost
    // monitor. Wayland compositors ignore the position and place the window themselves.
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_PropertiesID windowProps = SDL_CreateProperties();
    SDL_SetStringProperty(windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title.c_str());
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, aplicationDescriptor.width);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, aplicationDescriptor.height);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
    m_pWindow = SDL_CreateWindowWithProperties(windowProps);
    SDL_DestroyProperties(windowProps);
    if (!m_pWindow) {
      T8_LOG_ERROR("[LinuxFramework] SDL window creation failed: %s", SDL_GetError());
      return;
    }
    const char* displayName = SDL_GetDisplayName(display);
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    T8_LOG_INFO("[LinuxFramework] Window centred on primary display '%s' (video driver %s)",
                displayName ? displayName : "unknown", videoDriver ? videoDriver : "unknown");
    SDL_StartTextInput(m_pWindow);

    int pixelWidth = 0;
    int pixelHeight = 0;
    if (SDL_GetWindowSizeInPixels(m_pWindow, &pixelWidth, &pixelHeight)) {
      aplicationDescriptor.width = static_cast<unsigned int>((std::max)(1, pixelWidth));
      aplicationDescriptor.height = static_cast<unsigned int>((std::max)(1, pixelHeight));
    }

    pVideoDriver = new VulkanDriver;
    pVideoDriver->SetDimensions(static_cast<int>(aplicationDescriptor.width),
                                static_cast<int>(aplicationDescriptor.height));
    g_pBaseDriver = pVideoDriver;
    Log::SetSessionTag(config::ApiTag(pVideoDriver->m_currentAPI));
    pVideoDriver->SetWindowHandle(WindowHandle::FromSDL(m_pWindow));
    pVideoDriver->InitDriver();
    RefreshEngineContextFromGlobals();
    pBaseApp->CreateAssets();
    pVideoDriver->BuildPipelineObjects();

    T8_LOG_INFO("[LinuxFramework] Vulkan runtime ready (%ux%u)",
                aplicationDescriptor.width,
                aplicationDescriptor.height);
  }

  void LinuxFramework::ReleaseMouseMode() {
    if (m_pWindow && m_relativeMouseMode) {
      SDL_SetWindowRelativeMouseMode(m_pWindow, false);
    }
    SDL_ShowCursor();
    m_relativeMouseMode = false;
    m_pendingMouseX = 0.0f;
    m_pendingMouseY = 0.0f;
  }

  void LinuxFramework::UpdateMouseMode() {
    if (!m_pWindow || !pBaseApp || !pBaseApp->AllowsMouseCapture()) {
      ReleaseMouseMode();
      return;
    }

    const SDL_WindowFlags flags = SDL_GetWindowFlags(m_pWindow);
    if (!(flags & SDL_WINDOW_INPUT_FOCUS) || (flags & SDL_WINDOW_MINIMIZED) ||
        (flags & SDL_WINDOW_HIDDEN)) {
      ReleaseMouseMode();
      return;
    }

    const bool wantsRelativeMouse = pBaseApp->WantsRelativeMouseMode();
    if (wantsRelativeMouse) {
      if (!m_relativeMouseMode) {
        // Relative mode warps the pointer once; discard that first delta or the camera
        // jumps on the frame the GUI closes.
        if (!SDL_SetWindowRelativeMouseMode(m_pWindow, true)) {
          static bool s_relativeFailureLogged = false;
          if (!s_relativeFailureLogged) {
            s_relativeFailureLogged = true;
            T8_LOG_ERROR("[Input] SDL_SetWindowRelativeMouseMode failed: %s (the camera "
                         "will stop at the pointer's screen edge)", SDL_GetError());
          }
          ReleaseMouseMode();
          return;
        }
        float discardX = 0.0f;
        float discardY = 0.0f;
        SDL_GetRelativeMouseState(&discardX, &discardY);
        m_pendingMouseX = 0.0f;
        m_pendingMouseY = 0.0f;
        T8_LOG_VERBOSE("[MouseInput] relative mouse mode enabled");
      }
      SDL_HideCursor();
      m_relativeMouseMode = true;
      return;
    }

    if (m_relativeMouseMode) {
      SDL_SetWindowRelativeMouseMode(m_pWindow, false);
      float discardX = 0.0f;
      float discardY = 0.0f;
      SDL_GetRelativeMouseState(&discardX, &discardY);
      m_pendingMouseX = 0.0f;
      m_pendingMouseY = 0.0f;
      T8_LOG_VERBOSE("[MouseInput] relative mouse mode disabled");
    }
    m_relativeMouseMode = false;
    SDL_ShowCursor();
  }

  void LinuxFramework::ResetMouseDeltaBaseline() {
    m_pendingMouseX = 0.0f;
    m_pendingMouseY = 0.0f;
    if (pBaseApp) {
      pBaseApp->IManager.xDelta = 0;
      pBaseApp->IManager.yDelta = 0;
      pBaseApp->IManager.scrollDelta = 0.0f;
    }
    if (m_relativeMouseMode) {
      float discardX = 0.0f;
      float discardY = 0.0f;
      SDL_GetRelativeMouseState(&discardX, &discardY);
    }
  }

  void LinuxFramework::ResetInputAfterWindowStateChange() {
    pBaseApp->IManager.xDelta = 0;
    pBaseApp->IManager.yDelta = 0;
    pBaseApp->IManager.scrollDelta = 0.0f;
    pBaseApp->IManager.textInput.clear();
    for (int i = 0; i < MAXMOUSEBUTTONS; ++i) {
      pBaseApp->IManager.MouseButtonStates[0][i] = false;
      pBaseApp->IManager.MouseButtonStates[1][i] = false;
    }
    if (m_relativeMouseMode) {
      float discardX = 0.0f;
      float discardY = 0.0f;
      SDL_GetRelativeMouseState(&discardX, &discardY);
    }
    SDL_PumpEvents();
  }

  void LinuxFramework::InitializeGamepads() {
    m_handheldReason = GetDeviceInfo().handheldReason;
    m_handheldDetected = !m_handheldReason.empty();
    pBaseApp->IManager.Gamepad.handheldDevice = m_handheldDetected;
    pBaseApp->IManager.Gamepad.handheldReason = m_handheldReason;
    T8_LOG_INFO("[Input] Handheld detection: %s%s",
                m_handheldDetected ? "yes " : "no",
                m_handheldDetected ? m_handheldReason.c_str() : "");

    int gamepadCount = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepadCount);
    for (int i = 0; gamepads && i < gamepadCount && !m_gamepad; ++i) {
      OpenGamepad(static_cast<int>(gamepads[i]));
    }
    if (gamepads) {
      SDL_free(gamepads);
    }
  }

  void LinuxFramework::ShutdownGamepads() {
    if (m_gamepad) {
      SDL_CloseGamepad(static_cast<SDL_Gamepad*>(m_gamepad));
      m_gamepad = nullptr;
      m_gamepadInstanceId = 0;
    }
    if (pBaseApp) {
      pBaseApp->IManager.Gamepad = GamepadInputState{};
    }
  }

  void LinuxFramework::OpenGamepad(int instanceId) {
    if (m_gamepad) {
      return;
    }
    SDL_Gamepad* gamepad = SDL_OpenGamepad(static_cast<SDL_JoystickID>(instanceId));
    if (!gamepad) {
      T8_LOG_INFO("[Input] Could not open SDL gamepad id=%d: %s", instanceId, SDL_GetError());
      return;
    }

    m_gamepad = gamepad;
    m_gamepadInstanceId = instanceId;
    GamepadInputState& state = pBaseApp->IManager.Gamepad;
    state.connected = true;
    state.enabled = true;
    state.handheldDevice = m_handheldDetected;
    state.handheldReason = m_handheldReason;
    const char* name = SDL_GetGamepadName(gamepad);
    state.name = name ? name : "SDL gamepad";
    T8_LOG_INFO("[Input] Gamepad opened id=%d name='%s' handheld=%d reason='%s'",
                instanceId,
                state.name.c_str(),
                state.handheldDevice ? 1 : 0,
                state.handheldReason.c_str());
  }

  void LinuxFramework::CloseGamepad(int instanceId) {
    if (!m_gamepad || m_gamepadInstanceId != instanceId) {
      return;
    }
    T8_LOG_INFO("[Input] Gamepad removed id=%d name='%s'", instanceId, pBaseApp->IManager.Gamepad.name.c_str());
    SDL_CloseGamepad(static_cast<SDL_Gamepad*>(m_gamepad));
    m_gamepad = nullptr;
    m_gamepadInstanceId = 0;
    pBaseApp->IManager.Gamepad = GamepadInputState{};
    pBaseApp->IManager.Gamepad.handheldDevice = m_handheldDetected;
    pBaseApp->IManager.Gamepad.handheldReason = m_handheldReason;
  }

  void LinuxFramework::RefreshGamepadState() {
    GamepadInputState& state = pBaseApp->IManager.Gamepad;
    const GamepadInputState previous = state;
    state.handheldDevice = m_handheldDetected;
    state.handheldReason = m_handheldReason;
    if (!m_gamepad) {
      state.connected = false;
      state.enabled = false;
      return;
    }

    SDL_Gamepad* gamepad = static_cast<SDL_Gamepad*>(m_gamepad);
    state.connected = true;
    state.enabled = true;
    state.leftX = NormalizeGamepadAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX));
    state.leftY = NormalizeGamepadAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
    state.rightX = NormalizeGamepadAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
    state.rightY = NormalizeGamepadAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
    state.leftTrigger = NormalizeGamepadTrigger(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    state.rightTrigger = NormalizeGamepadTrigger(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));

    state.buttonSouth = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    state.buttonEast = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
    state.buttonWest = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST);
    state.buttonNorth = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH);
    state.back = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);
    state.guide = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_GUIDE);
    state.start = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);
    state.leftStick = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    state.rightStick = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    state.leftShoulder = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    state.rightShoulder = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    state.dpadUp = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    state.dpadDown = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    state.dpadLeft = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    state.dpadRight = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

    state.buttonSouthPressed = state.buttonSouth && !previous.buttonSouth;
    state.buttonEastPressed = state.buttonEast && !previous.buttonEast;
    state.buttonWestPressed = state.buttonWest && !previous.buttonWest;
    state.buttonNorthPressed = state.buttonNorth && !previous.buttonNorth;
    state.backPressed = state.back && !previous.back;
    state.guidePressed = state.guide && !previous.guide;
    state.startPressed = state.start && !previous.start;
    state.leftStickPressed = state.leftStick && !previous.leftStick;
    state.rightStickPressed = state.rightStick && !previous.rightStick;
    state.leftShoulderPressed = state.leftShoulder && !previous.leftShoulder;
    state.rightShoulderPressed = state.rightShoulder && !previous.rightShoulder;
    state.dpadUpPressed = state.dpadUp && !previous.dpadUp;
    state.dpadDownPressed = state.dpadDown && !previous.dpadDown;
    state.dpadLeftPressed = state.dpadLeft && !previous.dpadLeft;
    state.dpadRightPressed = state.dpadRight && !previous.dpadRight;
  }
}
