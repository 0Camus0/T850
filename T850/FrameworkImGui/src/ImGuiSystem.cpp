#include <imgui/ImGuiSystem.h>
#include <imgui/ImGuiRendererBackend.h>

#include <Config.h>
#include <Descriptors.h>
#include <core/Core.h>
#include <debug/LoadingProgress.h>
#if defined(OS_WINDOWS) || defined(OS_LINUX)
#include <utils/HandheldControllerOverlay.h>
#endif
#include <utils/Log.h>
#include <video/BaseDriver.h>

#include <imgui.h>
#ifndef OS_ANDROID
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#endif

#ifdef OS_WINDOWS
#  include <core/windows/Win32Framework.h>
#endif
#ifdef OS_LINUX
#  include <core/LinuxFramework.h>
#endif
#ifdef OS_ANDROID
#  include <android/native_window.h>
#  include <core/android/AndroidFramework.h>
#endif
#include <algorithm>
#include <string>

namespace {
#ifndef OS_ANDROID
  const char* SdlWindowEventName(Uint32 eventType) {
    switch (eventType) {
    case SDL_EVENT_WINDOW_FOCUS_LOST: return "FOCUS_LOST";
    case SDL_EVENT_WINDOW_MINIMIZED: return "MINIMIZED";
    case SDL_EVENT_WINDOW_HIDDEN: return "HIDDEN";
    case SDL_EVENT_WINDOW_FOCUS_GAINED: return "FOCUS_GAINED";
    case SDL_EVENT_WINDOW_RESTORED: return "RESTORED";
    case SDL_EVENT_WINDOW_SHOWN: return "SHOWN";
    case SDL_EVENT_WINDOW_MAXIMIZED: return "MAXIMIZED";
    case SDL_EVENT_WINDOW_RESIZED: return "RESIZED";
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: return "PIXEL_SIZE_CHANGED";
    case SDL_EVENT_WINDOW_MOUSE_ENTER: return "MOUSE_ENTER";
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: return "MOUSE_LEAVE";
    default: return "WINDOW_EVENT";
    }
  }

  static bool sdlEventWatcher(void* userdata, SDL_Event* event) {
    ImGui_ImplSDL3_ProcessEvent(event);
    auto* system = static_cast<t850::ImGuiSystem*>(userdata);
    if (system && event->type == SDL_EVENT_MOUSE_WHEEL) {
      system->AddWheelDelta(event->wheel.y);
    }
    if (system &&
        event->type >= SDL_EVENT_WINDOW_FIRST &&
        event->type <= SDL_EVENT_WINDOW_LAST) {
      system->NoteWindowEvent(SdlWindowEventName(event->type), event->window.data1, event->window.data2);
    }
    return true;
  }

  void SyncImGuiMouseFromSDL(SDL_Window* window, bool platformWindowsEnabled, int traceFrames, const std::string& lastEventName, int eventData1, int eventData2) {
    if (!window) {
      return;
    }

    ImGuiIO& io = ImGui::GetIO();
    float x = 0.0f;
    float y = 0.0f;
    SDL_MouseButtonFlags buttons = 0;
    if (platformWindowsEnabled) {
      buttons = SDL_GetGlobalMouseState(&x, &y);
    } else {
      buttons = SDL_GetMouseState(&x, &y);
    }

    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, (buttons & SDL_BUTTON_LMASK) != 0);
    io.AddMouseButtonEvent(1, (buttons & SDL_BUTTON_RMASK) != 0);
    io.AddMouseButtonEvent(2, (buttons & SDL_BUTTON_MMASK) != 0);
    if (traceFrames > 0) {
      int winX = 0, winY = 0, winW = 0, winH = 0, pixW = 0, pixH = 0;
      SDL_GetWindowPosition(window, &winX, &winY);
      SDL_GetWindowSize(window, &winW, &winH);
      SDL_GetWindowSizeInPixels(window, &pixW, &pixH);
      ImGuiViewport* viewport = ImGui::GetMainViewport();
      T8_LOG_TRACE("[ImGuiMouseSync] after=%s(%d,%d) mode=%s mouse=(%.1f,%.1f) buttons=0x%08X io.MousePos=(%.1f,%.1f) display=(%.1f,%.1f) winPos=(%d,%d) winSize=(%d,%d) pix=(%d,%d) viewportPos=(%.1f,%.1f) viewportSize=(%.1f,%.1f) captureMouse=%d",
                  lastEventName.c_str(),
                  eventData1,
                  eventData2,
                  platformWindowsEnabled ? "global" : "window",
                  x,
                  y,
                  (unsigned int)buttons,
                  io.MousePos.x,
                  io.MousePos.y,
                  io.DisplaySize.x,
                  io.DisplaySize.y,
                  winX,
                  winY,
                  winW,
                  winH,
                  pixW,
                  pixH,
                  viewport ? viewport->Pos.x : 0.0f,
                  viewport ? viewport->Pos.y : 0.0f,
                  viewport ? viewport->Size.x : 0.0f,
                  viewport ? viewport->Size.y : 0.0f,
                  io.WantCaptureMouse ? 1 : 0);
    }
  }
#endif

  std::string TrimLoadingText(std::string text, std::size_t maxChars) {
    if (text.size() <= maxChars) return text;
    if (maxChars <= 3) return text.substr(0, maxChars);
    return "..." + text.substr(text.size() - (maxChars - 3));
  }
}

namespace t850 {

ImGuiSystem::~ImGuiSystem() {
  Shutdown();
}

bool ImGuiSystem::Init(RootFramework* framework, const char* iniFileName, bool enableDocking, bool enablePlatformWindows) {
  if (m_inited) return true;
  if (!framework || !framework->pVideoDriver) return false;

  m_framework = framework;
  void* nativeWindow = nullptr;
#ifdef OS_ANDROID
  auto* androidFramework = static_cast<AndroidFramework*>(framework);
  nativeWindow = androidFramework ? androidFramework->GetNativeWindow() : nullptr;
  m_sdlWindow = nullptr;
#elif defined(OS_WINDOWS)
  auto* w32 = static_cast<Win32Framework*>(framework);
  m_sdlWindow = w32 ? w32->m_pWindow : nullptr;
  nativeWindow = m_sdlWindow;
#elif defined(OS_LINUX)
  auto* linuxFramework = static_cast<LinuxFramework*>(framework);
  m_sdlWindow = linuxFramework ? linuxFramework->m_pWindow : nullptr;
  nativeWindow = m_sdlWindow;
#else
  m_sdlWindow = nullptr;
#endif

  if (!nativeWindow) {
    T8_LOG_ERROR("[ImGuiSystem] Init failed: no native window");
    return false;
  }

  m_api = framework->pVideoDriver->m_currentAPI;
  m_dockingEnabled = enableDocking;
  m_platformWindowsEnabled = enablePlatformWindows;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  if (m_dockingEnabled) {
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  }
#ifndef OS_ANDROID
  if (m_platformWindowsEnabled) {
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  }
#endif
  io.IniFilename = iniFileName;

  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.FrameRounding = 2.0f;
  style.GrabRounding = 2.0f;
  style.ScrollbarRounding = 3.0f;
  if (m_platformWindowsEnabled) {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  m_rendererBackend = CreateImGuiRendererBackend(m_api);
  if (!m_rendererBackend || !m_rendererBackend->Init(framework, nativeWindow)) {
    T8_LOG_ERROR("[ImGuiSystem] Renderer backend init failed (api=%d)", (int)m_api);
    m_rendererBackend.reset();
    ImGui::DestroyContext();
    m_framework = nullptr;
    m_sdlWindow = nullptr;
    m_platformWindowsEnabled = false;
    return false;
  }

#if defined(OS_WINDOWS) || defined(OS_LINUX)
  ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual);
#endif

#ifndef OS_ANDROID
  SDL_AddEventWatch(sdlEventWatcher, this);
#endif
  m_wheelAccum = 0.0f;
  m_inited = true;
  T8_LOG_INFO("[ImGuiSystem] Initialized (api=%d)", (int)m_api);
  return true;
}

void ImGuiSystem::Shutdown() {
  if (!m_inited) return;

  ClearLoadingProgressRenderer();

  ImGuiIO& io = ImGui::GetIO();
  if (io.IniFilename && io.IniFilename[0] != '\0') {
    ImGui::SaveIniSettingsToDisk(io.IniFilename);
  }

#ifndef OS_ANDROID
  SDL_RemoveEventWatch(sdlEventWatcher, this);
#endif
  m_rendererBackend->Shutdown();
  m_rendererBackend.reset();
  ImGui::DestroyContext();

  m_framework = nullptr;
  m_sdlWindow = nullptr;
  m_wheelAccum = 0.0f;
  m_gamepadNavigationState = GamepadInputState{};
  m_gamepadNavigationGuiVisible = false;
  m_gamepadNavigationTouchCursor = false;
  m_inited = false;
  T8_LOG_INFO("[ImGuiSystem] Shutdown complete");
}

void ImGuiSystem::NoteWindowEvent(const char* eventName, int data1, int data2) {
  m_lastWindowEventName = eventName ? eventName : "<null>";
  m_lastWindowEventData1 = data1;
  m_lastWindowEventData2 = data2;
  m_windowEventTraceFrames = 12;
}

void ImGuiSystem::SetGamepadNavigationInput(const GamepadInputState& gamepad, bool guiVisible, bool touchCursorVisible) {
  m_gamepadNavigationState = gamepad;
  m_gamepadNavigationGuiVisible = guiVisible;
  m_gamepadNavigationTouchCursor = touchCursorVisible;
}

bool ImGuiSystem::NewFrame(bool createDockspace) {
  if (!m_inited) return false;

#ifdef OS_ANDROID
  auto* androidFramework = static_cast<AndroidFramework*>(m_framework);
  ANativeWindow* currentWindow = androidFramework ? androidFramework->GetNativeWindow() : nullptr;
  if (!SetAndroidNativeWindow(currentWindow)) return false;
#endif

  m_rendererBackend->NewFrame();

#ifndef OS_ANDROID
  SyncImGuiMouseFromSDL(
      m_sdlWindow,
      m_platformWindowsEnabled,
      m_windowEventTraceFrames,
      m_lastWindowEventName,
      m_lastWindowEventData1,
      m_lastWindowEventData2);
#endif
#ifdef OS_WINDOWS
  SubmitGamepadGuiNavigation(m_gamepadNavigationState, m_gamepadNavigationGuiVisible);
  ImGui::GetIO().MouseDrawCursor = m_gamepadNavigationGuiVisible && m_gamepadNavigationTouchCursor;
#elif defined(OS_LINUX)
  SubmitGamepadGuiDirectionalNavigation(m_gamepadNavigationState, m_gamepadNavigationGuiVisible);
#endif
  ImGui::NewFrame();
  if (m_windowEventTraceFrames > 0) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    T8_LOG_TRACE("[ImGuiNewFrameTrace] after=%s framesLeft=%d io.MousePos=(%.1f,%.1f) display=(%.1f,%.1f) viewportPos=(%.1f,%.1f) viewportSize=(%.1f,%.1f) wantMouse=%d wantKeyboard=%d hoveredAny=%d",
                m_lastWindowEventName.c_str(),
                m_windowEventTraceFrames,
                io.MousePos.x,
                io.MousePos.y,
                io.DisplaySize.x,
                io.DisplaySize.y,
                viewport ? viewport->Pos.x : 0.0f,
                viewport ? viewport->Pos.y : 0.0f,
                viewport ? viewport->Size.x : 0.0f,
                viewport ? viewport->Size.y : 0.0f,
                io.WantCaptureMouse ? 1 : 0,
                io.WantCaptureKeyboard ? 1 : 0,
                ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ? 1 : 0);
    --m_windowEventTraceFrames;
  }

  if (m_dockingEnabled && createDockspace) {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  }
  return true;
}

void ImGuiSystem::Render() {
  if (!m_inited) return;

  BuildDrawData();
  RenderDrawData();

#ifndef OS_ANDROID
  ImGuiIO& io = ImGui::GetIO();
  if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
    if (m_rendererBackend->ShouldDeferPlatformWindowsUpdate()) {
      return;
    }
#if defined(USING_GL_COMMON)
    SDL_Window* backupWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
#endif
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
#if defined(USING_GL_COMMON)
    if (backupWindow && backupContext) {
      SDL_GL_MakeCurrent(backupWindow, backupContext);
    }
#endif
  }
#endif
}

void ImGuiSystem::InstallLoadingProgressRenderer() {
  LoadingProgress::SetFrameCallback([this]() {
    RenderLoadingFrame();
  });
}

void ImGuiSystem::ClearLoadingProgressRenderer() {
  LoadingProgress::ClearFrameCallback();
}

void ImGuiSystem::RenderLoadingFrame() {
  if (m_loadingFrameActive || !m_inited || !m_framework || !m_framework->pVideoDriver) return;

  m_loadingFrameActive = true;

#if defined(OS_WINDOWS) || defined(OS_LINUX)
  m_framework->ProcessInput();
#endif

  auto* driver = m_framework->pVideoDriver;
  driver->ClearBackbufferWithColor(0.0f, 0.0f, 0.0f, 1.0f);

  if (NewFrame(false)) {
    const LoadingProgress::Snapshot snapshot = LoadingProgress::GetSnapshot();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;
    const float scale = (std::max)(0.75f, (std::min)(displaySize.x / 1920.0f, displaySize.y / 1080.0f));

    ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::Begin("##T850LoadingScreen", nullptr, flags);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);
    ImFont* font = ImGui::GetFont();

    const char* title = "T850";
    const float titleSize = ImGui::GetFontSize() * 3.4f * scale;
    const ImVec2 titleTextSize = font->CalcTextSizeA(titleSize, 10000.0f, 0.0f, title);
    draw->AddText(font,
                  titleSize,
                  ImVec2(center.x - titleTextSize.x * 0.5f, center.y - 130.0f * scale),
                  IM_COL32(255, 255, 255, 255),
                  title);

    const std::string phase = snapshot.phase.empty() ? "Loading" : snapshot.phase;
    const float phaseSize = ImGui::GetFontSize() * 1.15f * scale;
    const ImVec2 phaseTextSize = font->CalcTextSizeA(phaseSize, 10000.0f, 0.0f, phase.c_str());
    draw->AddText(font,
                  phaseSize,
                  ImVec2(center.x - phaseTextSize.x * 0.5f, center.y - 40.0f * scale),
                  IM_COL32(230, 230, 230, 255),
                  phase.c_str());

    const float barWidth = (std::min)(displaySize.x * 0.58f, 640.0f * scale);
    const float barHeight = 14.0f * scale;
    const ImVec2 barMin(center.x - barWidth * 0.5f, center.y + 8.0f * scale);
    const ImVec2 barMax(barMin.x + barWidth, barMin.y + barHeight);
    const float fillWidth = barWidth * std::clamp(snapshot.percent / 100.0f, 0.0f, 1.0f);

    draw->AddRectFilled(barMin, barMax, IM_COL32(28, 28, 28, 255), barHeight * 0.5f);
    draw->AddRectFilled(barMin, ImVec2(barMin.x + fillWidth, barMax.y), IM_COL32(56, 168, 255, 255), barHeight * 0.5f);
    draw->AddRect(barMin, barMax, IM_COL32(95, 95, 95, 255), barHeight * 0.5f);

    const int percentInt = static_cast<int>(snapshot.percent + 0.5f);
    const std::string percent = std::to_string(percentInt) + "%";
    const float percentSize = ImGui::GetFontSize() * 0.95f * scale;
    const ImVec2 percentTextSize = font->CalcTextSizeA(percentSize, 10000.0f, 0.0f, percent.c_str());
    draw->AddText(font,
                  percentSize,
                  ImVec2(center.x - percentTextSize.x * 0.5f, barMax.y + 12.0f * scale),
                  IM_COL32(210, 210, 210, 255),
                  percent.c_str());

    std::string item = TrimLoadingText(snapshot.item.empty() ? std::string("Preparing...") : snapshot.item, 92);
    const float itemSize = ImGui::GetFontSize() * 0.85f * scale;
    const ImVec2 itemTextSize = font->CalcTextSizeA(itemSize, 10000.0f, 0.0f, item.c_str());
    draw->AddText(font,
                  itemSize,
                  ImVec2(center.x - itemTextSize.x * 0.5f, barMax.y + 42.0f * scale),
                  IM_COL32(160, 160, 160, 255),
                  item.c_str());

    if (!snapshot.detail.empty()) {
      std::string detail = TrimLoadingText(snapshot.detail, 104);
      const float detailSize = ImGui::GetFontSize() * 0.78f * scale;
      const ImVec2 detailTextSize = font->CalcTextSizeA(detailSize, 10000.0f, 0.0f, detail.c_str());
      draw->AddText(font,
                    detailSize,
                    ImVec2(center.x - detailTextSize.x * 0.5f, barMax.y + 66.0f * scale),
                    IM_COL32(120, 120, 120, 255),
                    detail.c_str());
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    Render();
  }

  driver->CompleteFrame(BaseDriver::FrameCompletionMode::Present);
  m_loadingFrameActive = false;
}

void ImGuiSystem::BuildDrawData() {
  if (!m_inited) return;

  ImGui::Render();
}

void ImGuiSystem::RenderDrawData() {
  if (!m_inited) return;
  m_rendererBackend->RenderDrawData(ImGui::GetDrawData());
}

ImTextureID ImGuiSystem::GetTextureID(Texture* texture, ImGuiTextureMode mode) {
  return m_inited && m_rendererBackend && texture
    ? m_rendererBackend->GetTextureID(texture, mode)
    : (ImTextureID)nullptr;
}

void ImGuiSystem::PruneTextureIDs(const std::unordered_set<Texture*>& liveTextures) {
  if (m_rendererBackend) m_rendererBackend->PruneTextureIDs(liveTextures);
}

void ImGuiSystem::ReleaseTextureIDs() {
  if (m_rendererBackend) m_rendererBackend->ReleaseTextureIDs();
}

bool ImGuiSystem::RequiresOpaquePreviewBlend() const {
  return m_rendererBackend && m_rendererBackend->RequiresOpaquePreviewBlend();
}

bool ImGuiSystem::WantsKeyboard() const {
  if (!m_inited) return false;
  return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiSystem::WantsTextInput() const {
  if (!m_inited) return false;
  return ImGui::GetIO().WantTextInput;
}

bool ImGuiSystem::WantsMouse() const {
  if (!m_inited) return false;
  return ImGui::GetIO().WantCaptureMouse;
}

float ImGuiSystem::ConsumeWheelDelta() {
  float delta = m_wheelAccum;
  m_wheelAccum = 0.0f;
  return delta;
}

#ifdef OS_ANDROID
bool ImGuiSystem::SetAndroidNativeWindow(ANativeWindow* window) {
  return m_rendererBackend && m_rendererBackend->SetNativeWindow(window);
}

bool ImGuiSystem::HandleAndroidInputEvent(AInputEvent* event) {
  return m_inited && m_rendererBackend && event
    ? m_rendererBackend->HandlePlatformInput(event)
    : false;
}
#endif

} // namespace t850
