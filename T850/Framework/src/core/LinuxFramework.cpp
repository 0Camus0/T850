#include <pch.h>

#include <core/LinuxFramework.h>
#include <core/EngineContext.h>
#include <core/Config.h>
#include <video/vulkan/VulkanDriver.h>
#include <utils/Log.h>
#include <utils/ThreadPool.h>
#include <utils/ConfigRuntime.h>
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
    std::string ReadFirstLine(const char* path) {
      std::ifstream file(path);
      std::string value;
      std::getline(file, value);
      return value;
    }

    std::string DetectSteamDeckReason() {
      const std::string product = ReadFirstLine("/sys/devices/virtual/dmi/id/product_name");
      const std::string board = ReadFirstLine("/sys/devices/virtual/dmi/id/board_name");
      const std::string vendor = ReadFirstLine("/sys/devices/virtual/dmi/id/sys_vendor");
      const std::string probe = product + " " + board + " " + vendor;
      std::string lower = probe;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
      });
      if (lower.find("steam deck") != std::string::npos || lower.find("jupiter") != std::string::npos) {
        return "Steam Deck (" + probe + ")";
      }
      return {};
    }

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

    // On Wayland (e.g. GNOME) SDL defaults to the Wayland backend, whose
    // windows have no X11-style title bar (can't grab/move/minimize/close).
    // Prefer the X11 backend (Xwayland) so the window manager decorates the
    // window normally. Respect an explicit user override (SDL_VIDEODRIVER).
    if (!std::getenv("SDL_VIDEODRIVER")) {
      setenv("SDL_VIDEODRIVER", "x11", 0);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
      T8_LOG_ERROR("[LinuxFramework] SDL initialization failed: %s", SDL_GetError());
      m_alive = false;
      return;
    }

    pBaseApp->InitVars();
    InitializeGamepads();
    ChangeAPI(GraphicsApi::VULKAN);
    m_inited = pVideoDriver != nullptr;
    if (m_inited) {
      UpdateApplication();
    }
    OnDestroyApplication();
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
      // Apply any window resize coalesced during event processing at the frame
      // boundary, before the app (and thus the GPU) does any work this frame.
      // Doing this here — rather than inside the SDL event pump — avoids
      // destroying/recreating the swapchain mid-frame, which crashed on Linux.
      ApplyPendingResize();
      pBaseApp->OnUpdate();
    }
  }

  void LinuxFramework::ApplyPendingResize() {
    if (m_pendingResizeW <= 0 || m_pendingResizeH <= 0) {
      return;
    }
    // Coalesce repeated resize events: only resize when the size actually
    // changed since the last applied resize.
    if (pVideoDriver &&
        (static_cast<int>(aplicationDescriptor.width) != m_pendingResizeW ||
         static_cast<int>(aplicationDescriptor.height) != m_pendingResizeH)) {
      aplicationDescriptor.width = static_cast<unsigned int>(m_pendingResizeW);
      aplicationDescriptor.height = static_cast<unsigned int>(m_pendingResizeH);
      pVideoDriver->ResizeSwapchain(m_pendingResizeW, m_pendingResizeH);
    }
    m_pendingResizeW = 0;
    m_pendingResizeH = 0;
  }

  void LinuxFramework::ProcessInput() {
    pBaseApp->IManager.scrollDelta = 0.0f;
    pBaseApp->IManager.xDelta = 0;
    pBaseApp->IManager.yDelta = 0;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_QUIT:
        m_alive = false;
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
        pBaseApp->IManager.xDelta += static_cast<int>(event.motion.xrel);
        pBaseApp->IManager.yDelta += static_cast<int>(event.motion.yrel);
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

      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        // Record the requested pixel size but do NOT resize the swapchain here.
        // Resizing (device-wait + destroy/recreate of the swapchain, depth and
        // framebuffers) inside the event pump happens mid-frame and corrupts the
        // driver's in-flight frame state, which crashes on Linux. Instead we
        // coalesce the pending size and apply it once per frame at the frame
        // boundary in UpdateApplication (before any GPU work), the same way
        // Windows keeps the resize event separate from the swapchain resize.
        int width = 0;
        int height = 0;
        if (m_pWindow && SDL_GetWindowSizeInPixels(m_pWindow, &width, &height)) {
          m_pendingResizeW = (std::max)(1, width);
          m_pendingResizeH = (std::max)(1, height);
          ResetInputAfterWindowStateChange();
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

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    pBaseApp->IManager.mouseX = static_cast<int>(mouseX);
    pBaseApp->IManager.mouseY = static_cast<int>(mouseY);

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

    m_pWindow = SDL_CreateWindow(title.c_str(),
                                 static_cast<int>(aplicationDescriptor.width),
                                 static_cast<int>(aplicationDescriptor.height),
                                 flags);
    if (!m_pWindow) {
      T8_LOG_ERROR("[LinuxFramework] SDL window creation failed: %s", SDL_GetError());
      return;
    }
    SDL_SetWindowPosition(m_pWindow, 0, 0);
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

  void LinuxFramework::ResetInputAfterWindowStateChange() {
    pBaseApp->IManager.xDelta = 0;
    pBaseApp->IManager.yDelta = 0;
    pBaseApp->IManager.scrollDelta = 0.0f;
    pBaseApp->IManager.textInput.clear();
    for (int i = 0; i < MAXMOUSEBUTTONS; ++i) {
      pBaseApp->IManager.MouseButtonStates[0][i] = false;
      pBaseApp->IManager.MouseButtonStates[1][i] = false;
    }
    SDL_PumpEvents();
  }

  void LinuxFramework::InitializeGamepads() {
    m_handheldReason = DetectSteamDeckReason();
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
