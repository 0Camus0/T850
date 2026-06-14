#include <imgui/ImGuiSystem.h>

#include <Config.h>
#include <Descriptors.h>
#include <core/Core.h>
#include <debug/LoadingProgress.h>
#include <utils/Log.h>
#include <video/BaseDriver.h>

#include <imgui.h>
#ifndef OS_ANDROID
#include <imgui_impl_opengl3.h>
#endif
#include <imgui_impl_vulkan.h>

#ifdef OS_WINDOWS
#  include <core/windows/Win32Framework.h>
#  include <d3d11.h>
#  include <imgui_impl_dx11.h>
#  include <imgui_impl_sdl3.h>
#  include <SDL3/SDL.h>
#  include <video/d3d11/D3D11Texture.h>
#  if __has_include(<imgui_impl_dx12.h>)
#    define T850_IMGUI_HAS_DX12 1
#    include <imgui_impl_dx12.h>
#    include <video/d3d12/D3D12Driver.h>
#  else
#    define T850_IMGUI_HAS_DX12 0
#  endif
#endif
#ifdef OS_ANDROID
#  include <android/input.h>
#  include <android/native_window.h>
#  include <core/android/AndroidFramework.h>
#  include <imgui_impl_android.h>
#endif

#include <video/vulkan/VulkanDriver.h>

#include <cstring>
#include <algorithm>
#include <string>

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;
}

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
#ifdef OS_ANDROID
  auto* androidFramework = static_cast<AndroidFramework*>(framework);
  m_androidWindow = androidFramework ? androidFramework->GetNativeWindow() : nullptr;
  m_sdlWindow = nullptr;
#elif defined(OS_WINDOWS)
  auto* w32 = static_cast<Win32Framework*>(framework);
  m_sdlWindow = w32 ? w32->m_pWindow : nullptr;
#else
  m_sdlWindow = nullptr;
#endif

#ifdef OS_ANDROID
  if (!m_androidWindow) {
    T8_LOG_ERROR("[ImGuiSystem] Init failed: no Android native window");
    return false;
  }
#else
  if (!m_sdlWindow) {
    T8_LOG_ERROR("[ImGuiSystem] Init failed: no SDL window");
    return false;
  }
#endif

  m_api = framework->pVideoDriver->m_currentAPI;
  m_dockingEnabled = enableDocking;
  m_platformWindowsEnabled = enablePlatformWindows;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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

  bool platformOK = false;
#ifdef OS_ANDROID
  platformOK = ImGui_ImplAndroid_Init(m_androidWindow);
#elif defined(OS_WINDOWS)
  if (m_api == GraphicsApi::OPENGL) {
    platformOK = ImGui_ImplSDL3_InitForOpenGL(m_sdlWindow, nullptr);
  } else if (m_api == GraphicsApi::VULKAN) {
    platformOK = ImGui_ImplSDL3_InitForVulkan(m_sdlWindow);
  } else {
    platformOK = ImGui_ImplSDL3_InitForD3D(m_sdlWindow);
  }
#else
  platformOK = ImGui_ImplSDL3_InitForOpenGL(m_sdlWindow, nullptr);
#endif

  if (!platformOK) {
    T8_LOG_ERROR("[ImGuiSystem] Platform backend init failed");
    ImGui::DestroyContext();
    m_framework = nullptr;
    return false;
  }
#ifdef OS_ANDROID
  m_androidPlatformInited = true;
#endif

  bool rendererOK = false;

#ifdef OS_WINDOWS
  if (m_api == GraphicsApi::D3D11) {
    ID3D11Device* device = reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject());
    ID3D11DeviceContext* ctx = reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject());
    rendererOK = ImGui_ImplDX11_Init(device, ctx);
  }
#if T850_IMGUI_HAS_DX12
  else if (m_api == GraphicsApi::D3D12) {
    auto* d3d12Driver = static_cast<D3D12Driver*>(framework->pVideoDriver);
    ID3D12Device* device = static_cast<D3D12Device*>(T8Device)->GetNativeDevice();
    auto& srvHeap = d3d12Driver->GetHeap(D3D12Heap::CBV_SRV_UAV_VISIBLE);
    m_d3d12SrvHeap = srvHeap.GetHeap();

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device;
    initInfo.CommandQueue = d3d12Driver->GetCmdQueue();
    initInfo.NumFramesInFlight = D3D12Driver::kBackBufferCount;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    initInfo.SrvDescriptorHeap = m_d3d12SrvHeap;
    initInfo.UserData = &srvHeap;
    initInfo.SrvDescriptorAllocFn =
        [](ImGui_ImplDX12_InitInfo* info,
           D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
           D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
          auto* heap = static_cast<D3D12Heap*>(info ? info->UserData : nullptr);
          if (!heap || !outCpu || !outGpu) {
            if (outCpu) *outCpu = D3D12_CPU_DESCRIPTOR_HANDLE{0};
            if (outGpu) *outGpu = D3D12_GPU_DESCRIPTOR_HANDLE{0};
            return;
          }
          *outCpu = heap->AllocateCPU();
          *outGpu = heap->AllocateGPU();
        };
    initInfo.SrvDescriptorFreeFn =
        [](ImGui_ImplDX12_InitInfo*,
           D3D12_CPU_DESCRIPTOR_HANDLE,
           D3D12_GPU_DESCRIPTOR_HANDLE) {
        };
    rendererOK = ImGui_ImplDX12_Init(&initInfo);
  }
#endif
#endif

#ifndef OS_ANDROID
  if (m_api == GraphicsApi::OPENGL) {
    rendererOK = ImGui_ImplOpenGL3_Init("#version 300 es");
  }
#endif

  if (m_api == GraphicsApi::VULKAN) {
    auto* vkDriver = static_cast<VulkanDriver*>(framework->pVideoDriver);
    ImGui_ImplVulkan_InitInfo vkInit = {};
    vkInit.ApiVersion = VK_API_VERSION_1_0;
    vkInit.Instance = vkDriver->GetInstance();
    vkInit.PhysicalDevice = vkDriver->GetPhysicalDevice();
    vkInit.Device = vkDriver->GetDevice();
    vkInit.QueueFamily = vkDriver->GetGraphicsQueueFamily();
    vkInit.Queue = vkDriver->GetGraphicsQueue();
    vkInit.DescriptorPoolSize = 64;
    vkInit.MinImageCount = VulkanDriver::kBackBufferCount;
    vkInit.ImageCount = VulkanDriver::kBackBufferCount;
    vkInit.PipelineInfoMain.RenderPass = vkDriver->GetBackbufferRenderPass();
    rendererOK = ImGui_ImplVulkan_Init(&vkInit);
  }

  if (!rendererOK) {
    T8_LOG_ERROR("[ImGuiSystem] Renderer backend init failed (api=%d)", (int)m_api);
#ifdef OS_WINDOWS
#if T850_IMGUI_HAS_DX12
    if (m_d3d12SrvHeap) {
      m_d3d12SrvHeap = nullptr;
    }
#endif
#endif
#ifdef OS_ANDROID
    if (m_androidPlatformInited) {
      ImGui_ImplAndroid_Shutdown();
      m_androidPlatformInited = false;
    }
#else
    ImGui_ImplSDL3_Shutdown();
#endif
    ImGui::DestroyContext();
    m_framework = nullptr;
    m_sdlWindow = nullptr;
    m_platformWindowsEnabled = false;
#ifdef OS_ANDROID
    m_androidWindow = nullptr;
#endif
    return false;
  }

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

#ifdef OS_WINDOWS
  if (m_api == GraphicsApi::D3D11) {
    ImGui_ImplDX11_Shutdown();
  }
#if T850_IMGUI_HAS_DX12
  else if (m_api == GraphicsApi::D3D12) {
    ImGui_ImplDX12_Shutdown();
    m_d3d12SrvHeap = nullptr;
  }
#endif
#endif

#ifndef OS_ANDROID
  if (m_api == GraphicsApi::OPENGL) {
    ImGui_ImplOpenGL3_Shutdown();
  }
#endif
  if (m_api == GraphicsApi::VULKAN) {
    ImGui_ImplVulkan_Shutdown();
  }

#ifdef OS_ANDROID
  if (m_androidPlatformInited) {
    ImGui_ImplAndroid_Shutdown();
    m_androidPlatformInited = false;
  }
#else
  ImGui_ImplSDL3_Shutdown();
  SDL_RemoveEventWatch(sdlEventWatcher, this);
#endif
  ImGui::DestroyContext();

  m_framework = nullptr;
  m_sdlWindow = nullptr;
#ifdef OS_ANDROID
  m_androidWindow = nullptr;
#endif
  m_wheelAccum = 0.0f;
  m_inited = false;
  T8_LOG_INFO("[ImGuiSystem] Shutdown complete");
}

void ImGuiSystem::NoteWindowEvent(const char* eventName, int data1, int data2) {
  m_lastWindowEventName = eventName ? eventName : "<null>";
  m_lastWindowEventData1 = data1;
  m_lastWindowEventData2 = data2;
  m_windowEventTraceFrames = 12;
}

bool ImGuiSystem::NewFrame(bool createDockspace) {
  if (!m_inited) return false;

#ifdef OS_ANDROID
  auto* androidFramework = static_cast<AndroidFramework*>(m_framework);
  ANativeWindow* currentWindow = androidFramework ? androidFramework->GetNativeWindow() : nullptr;
  if (!SetAndroidNativeWindow(currentWindow)) return false;
#endif

#ifdef OS_WINDOWS
  if (m_api == GraphicsApi::D3D11) {
    ImGui_ImplDX11_NewFrame();
  }
#if T850_IMGUI_HAS_DX12
  else if (m_api == GraphicsApi::D3D12) {
    ImGui_ImplDX12_NewFrame();
  }
#endif
#endif

#ifndef OS_ANDROID
  if (m_api == GraphicsApi::OPENGL) {
    ImGui_ImplOpenGL3_NewFrame();
  }
#endif
  if (m_api == GraphicsApi::VULKAN) {
    ImGui_ImplVulkan_NewFrame();
  }

#ifdef OS_ANDROID
  ImGui_ImplAndroid_NewFrame();
#else
  ImGui_ImplSDL3_NewFrame();
  SyncImGuiMouseFromSDL(
      m_sdlWindow,
      m_platformWindowsEnabled,
      m_windowEventTraceFrames,
      m_lastWindowEventName,
      m_lastWindowEventData1,
      m_lastWindowEventData2);
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
    bool deferPlatformResize = false;
    if (m_api == GraphicsApi::VULKAN && io.MouseDown[0]) {
      ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
      for (ImGuiViewport* viewport : platformIO.Viewports) {
        if (viewport && viewport->PlatformRequestResize) {
          deferPlatformResize = true;
          break;
        }
      }
    }
    if (deferPlatformResize) {
      return;
    }
    SDL_Window* backupWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    if (backupWindow && backupContext) {
      SDL_GL_MakeCurrent(backupWindow, backupContext);
    }
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

#if defined(OS_WINDOWS)
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

  ImDrawData* drawData = ImGui::GetDrawData();

#ifdef OS_WINDOWS
  if (m_api == GraphicsApi::D3D11) {
    ImGui_ImplDX11_RenderDrawData(drawData);
  }
#if T850_IMGUI_HAS_DX12
  else if (m_api == GraphicsApi::D3D12) {
    auto* d3d12Driver = static_cast<D3D12Driver*>(g_pBaseDriver);
    ID3D12GraphicsCommandList* cmdList = d3d12Driver->GetCmdList();
    ID3D12DescriptorHeap* heaps[] = { m_d3d12SrvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(drawData, cmdList);
  }
#endif
#endif

#ifndef OS_ANDROID
  if (m_api == GraphicsApi::OPENGL) {
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
  }
#endif
  if (m_api == GraphicsApi::VULKAN) {
    auto* vkDriver = static_cast<VulkanDriver*>(g_pBaseDriver);
    std::memset(vkDriver->m_pendingTextures, 0, sizeof(vkDriver->m_pendingTextures));
    vkDriver->EnsureBackbufferRenderPass();
    VkCommandBuffer cmd = static_cast<VulkanDeviceContext*>(T8DeviceContext)->GetCommandBuffer();
    if (cmd && drawData) {
      ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    } else {
      T8_LOG_ERROR("[ImGuiSystem] Vulkan render skipped: cmd=%p drawData=%p", (void*)cmd, (void*)drawData);
    }
  }
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
  if (!m_inited) {
    m_androidWindow = window;
    return window != nullptr;
  }

  if (m_androidPlatformInited && m_androidWindow == window && window) {
    return true;
  }

  if (m_androidPlatformInited) {
    ImGui_ImplAndroid_Shutdown();
    m_androidPlatformInited = false;
  }

  m_androidWindow = nullptr;
  if (!window) return false;

  if (!ImGui_ImplAndroid_Init(window)) {
    T8_LOG_ERROR("[ImGuiSystem] Android platform backend reinit failed");
    return false;
  }

  m_androidWindow = window;
  m_androidPlatformInited = true;
  T8_LOG_INFO("[ImGuiSystem] Android native window rebound");
  return true;
}

bool ImGuiSystem::HandleAndroidInputEvent(AInputEvent* event) {
  if (!m_inited || !m_androidPlatformInited || !event) return false;
  const bool handled = ImGui_ImplAndroid_HandleInputEvent(event) != 0;
  if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
    const int32_t rawAction = AMotionEvent_getAction(event);
    const int32_t action = rawAction & AMOTION_EVENT_ACTION_MASK;
    int32_t pointerIndex = (rawAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    const size_t pointerCount = AMotionEvent_getPointerCount(event);
    if (pointerCount == 0) return handled;
    if (pointerIndex < 0 || pointerIndex >= static_cast<int32_t>(pointerCount)) pointerIndex = 0;
    const int32_t toolType = AMotionEvent_getToolType(event, pointerIndex);
    if (toolType == AMOTION_EVENT_TOOL_TYPE_STYLUS || toolType == AMOTION_EVENT_TOOL_TYPE_ERASER) {
      ImGuiIO& io = ImGui::GetIO();
      io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
      switch (action) {
      case AMOTION_EVENT_ACTION_DOWN:
      case AMOTION_EVENT_ACTION_POINTER_DOWN:
        io.AddMousePosEvent(AMotionEvent_getX(event, pointerIndex), AMotionEvent_getY(event, pointerIndex));
        io.AddMouseButtonEvent(0, true);
        return true;
      case AMOTION_EVENT_ACTION_UP:
      case AMOTION_EVENT_ACTION_POINTER_UP:
      case AMOTION_EVENT_ACTION_CANCEL:
        io.AddMousePosEvent(AMotionEvent_getX(event, pointerIndex), AMotionEvent_getY(event, pointerIndex));
        io.AddMouseButtonEvent(0, false);
        return true;
      case AMOTION_EVENT_ACTION_MOVE:
        io.AddMousePosEvent(AMotionEvent_getX(event, pointerIndex), AMotionEvent_getY(event, pointerIndex));
        return true;
      default:
        break;
      }
    }
  }
  return handled;
}
#endif

} // namespace t850
