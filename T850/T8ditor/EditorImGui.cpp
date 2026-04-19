/*********************************************************
* T8ditor — ImGui integration layer.  See header.
*********************************************************/

#include "EditorImGui.h"

#include <Config.h>
#include <video/BaseDriver.h>
#include <core/Core.h>
#include <utils/Log.h>
#include <T8_descriptors.h>

// ImGui core
#include <imgui.h>

// Platform backend — SDL3
#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>

// Renderer backends (compile-time: all are available; runtime: one is used)
#ifdef OS_WINDOWS
#  include <imgui_impl_dx11.h>
#  include <imgui_impl_dx12.h>
#  include <d3d11.h>
#  include <video/d3d12/D3D12Driver.h>
#  include <core/windows/Win32Framework.h>
#endif
#include <imgui_impl_opengl3.h>

// Framework globals
namespace t800 {
  extern Device*        T8Device;
  extern DeviceContext* T8DeviceContext;
}

namespace t8ditor {

// ── Module state ──────────────────────────────────────
static bool                   s_inited    = false;
static t800::GRAPHICS_API::E  s_api       = t800::GRAPHICS_API::D3D11;
static SDL_Window*            s_sdlWindow = nullptr;
static float                  s_wheelAccum = 0.0f;

#ifdef OS_WINDOWS
// D3D12: we create a small SRV heap dedicated to ImGui
static ID3D12DescriptorHeap*  s_d3d12SrvHeap = nullptr;
#endif

// ── SDL3 event watcher ────────────────────────────────
// Installed via SDL_AddEventWatch so ImGui sees events
// even though Win32Framework owns the SDL_PollEvent loop.
static bool sdlEventWatcher(void* /*userdata*/, SDL_Event* event) {
  ImGui_ImplSDL3_ProcessEvent(event);
  // Capture mouse wheel for the editor camera
  if (event->type == SDL_EVENT_MOUSE_WHEEL)
    s_wheelAccum += event->wheel.y;
  return true;  // let the event propagate to the framework
}

// ── Init ──────────────────────────────────────────────
bool ImGuiInit(t800::RootFramework* fw) {
  if (s_inited) return true;
  if (!fw || !fw->pVideoDriver) return false;

  // We need the Win32Framework to get the SDL window.
  // RootFramework doesn't expose m_pWindow, but Win32Framework
  // (the only concrete framework on Windows) does.
#ifdef OS_WINDOWS
  auto* w32 = static_cast<t800::Win32Framework*>(fw);
  s_sdlWindow = w32->m_pWindow;
#else
  // Linux path — LinuxFramework has its own window member.
  s_sdlWindow = nullptr;
#endif

  if (!s_sdlWindow) {
    T8_LOG_ERROR("[T8ditor] ImGuiInit: no SDL window");
    return false;
  }

  s_api = fw->pVideoDriver->m_currentAPI;

  // ── ImGui context ──
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Dark theme
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding   = 4.0f;
  style.FrameRounding    = 2.0f;
  style.GrabRounding     = 2.0f;
  style.ScrollbarRounding = 3.0f;

  // ── Platform backend ──
  bool platformOK = false;
#ifdef OS_WINDOWS
  if (s_api == t800::GRAPHICS_API::OPENGL)
    platformOK = ImGui_ImplSDL3_InitForOpenGL(s_sdlWindow, nullptr);
  else
    platformOK = ImGui_ImplSDL3_InitForD3D(s_sdlWindow);
#else
  platformOK = ImGui_ImplSDL3_InitForOpenGL(s_sdlWindow, nullptr);
#endif
  if (!platformOK) {
    T8_LOG_ERROR("[T8ditor] ImGui SDL3 platform init failed");
    return false;
  }

  // ── Renderer backend ──
  bool rendererOK = false;

#ifdef OS_WINDOWS
  if (s_api == t800::GRAPHICS_API::D3D11) {
    ID3D11Device*        device = reinterpret_cast<ID3D11Device*>(t800::T8Device->GetAPIObject());
    ID3D11DeviceContext* ctx    = reinterpret_cast<ID3D11DeviceContext*>(t800::T8DeviceContext->GetAPIObject());
    rendererOK = ImGui_ImplDX11_Init(device, ctx);
  }
  else if (s_api == t800::GRAPHICS_API::D3D12) {
    auto* d3d12Drv = static_cast<t800::D3D12Driver*>(fw->pVideoDriver);
    ID3D12Device* device = static_cast<t800::D3D12Device*>(t800::T8Device)->GetNativeDevice();

    // Create a dedicated SRV descriptor heap for ImGui (small, shader-visible)
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 64;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&s_d3d12SrvHeap));
    if (FAILED(hr)) {
      T8_LOG_ERROR("[T8ditor] Failed to create D3D12 SRV heap for ImGui");
      return false;
    }

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device           = device;
    initInfo.CommandQueue     = d3d12Drv->GetCmdQueue();
    initInfo.NumFramesInFlight = t800::D3D12Driver::kBackBufferCount;
    initInfo.RTVFormat        = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat        = DXGI_FORMAT_D32_FLOAT;
    initInfo.SrvDescriptorHeap = s_d3d12SrvHeap;
    initInfo.LegacySingleSrvCpuDescriptor = s_d3d12SrvHeap->GetCPUDescriptorHandleForHeapStart();
    initInfo.LegacySingleSrvGpuDescriptor = s_d3d12SrvHeap->GetGPUDescriptorHandleForHeapStart();
    rendererOK = ImGui_ImplDX12_Init(&initInfo);
  }
#endif

  if (s_api == t800::GRAPHICS_API::OPENGL) {
    rendererOK = ImGui_ImplOpenGL3_Init("#version 300 es");
  }

  if (!rendererOK) {
    T8_LOG_ERROR("[T8ditor] ImGui renderer backend init failed (api=%d)", (int)s_api);
    return false;
  }

  // Install SDL event watcher so ImGui receives events
  SDL_AddEventWatch(sdlEventWatcher, nullptr);

  s_inited = true;
  T8_LOG_INFO("[T8ditor] ImGui initialised (api=%d)", (int)s_api);
  return true;
}

// ── Shutdown ──────────────────────────────────────────
void ImGuiShutdown() {
  if (!s_inited) return;

#ifdef OS_WINDOWS
  if (s_api == t800::GRAPHICS_API::D3D11) {
    ImGui_ImplDX11_Shutdown();
  }
  else if (s_api == t800::GRAPHICS_API::D3D12) {
    ImGui_ImplDX12_Shutdown();
    if (s_d3d12SrvHeap) {
      s_d3d12SrvHeap->Release();
      s_d3d12SrvHeap = nullptr;
    }
  }
#endif
  if (s_api == t800::GRAPHICS_API::OPENGL) {
    ImGui_ImplOpenGL3_Shutdown();
  }

  ImGui_ImplSDL3_Shutdown();

  SDL_RemoveEventWatch(sdlEventWatcher, nullptr);

  ImGui::DestroyContext();
  s_inited = false;
  T8_LOG_INFO("[T8ditor] ImGui shut down");
}

// ── NewFrame ──────────────────────────────────────────
void ImGuiNewFrame() {
  if (!s_inited) return;

#ifdef OS_WINDOWS
  if (s_api == t800::GRAPHICS_API::D3D11)
    ImGui_ImplDX11_NewFrame();
  else if (s_api == t800::GRAPHICS_API::D3D12)
    ImGui_ImplDX12_NewFrame();
#endif
  if (s_api == t800::GRAPHICS_API::OPENGL)
    ImGui_ImplOpenGL3_NewFrame();

  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

// ── Render ────────────────────────────────────────────
void ImGuiRender() {
  if (!s_inited) return;

  ImGui::Render();
  ImDrawData* drawData = ImGui::GetDrawData();

#ifdef OS_WINDOWS
  if (s_api == t800::GRAPHICS_API::D3D11) {
    ImGui_ImplDX11_RenderDrawData(drawData);
  }
  else if (s_api == t800::GRAPHICS_API::D3D12) {
    // Bind ImGui's SRV heap before rendering
    auto* d3d12Drv = static_cast<t800::D3D12Driver*>(t800::g_pBaseDriver);
    ID3D12GraphicsCommandList* cmdList = d3d12Drv->GetCmdList();
    ID3D12DescriptorHeap* heaps[] = { s_d3d12SrvHeap };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(drawData, cmdList);
  }
#endif
  if (s_api == t800::GRAPHICS_API::OPENGL) {
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
  }
}

// ── Menu bar ──────────────────────────────────────────
MenuAction ImGuiDrawMenuBar() {
  MenuAction action;
  if (!s_inited) return action;

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Import .x ...", "Ctrl+I"))
        action.wantsImportX = true;
      ImGui::Separator();
      if (ImGui::MenuItem("Load Scene ...", "Ctrl+O"))
        action.wantsLoadScene = true;
      if (ImGui::MenuItem("Save Scene ...", "Ctrl+S"))
        action.wantsSaveScene = true;
      ImGui::Separator();
      if (ImGui::MenuItem("Exit", "Alt+F4"))
        action.wantsExit = true;
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
      // Placeholder — panels toggle will go here
      ImGui::MenuItem("Hierarchy", nullptr, false, false);
      ImGui::MenuItem("Inspector", nullptr, false, false);
      ImGui::MenuItem("Console",   nullptr, false, false);
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About T8ditor")) {
        // TODO: about popup
      }
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }

  return action;
}

// ── Mouse wheel ───────────────────────────────────────
float ImGuiConsumeWheelDelta() {
  float d = s_wheelAccum;
  s_wheelAccum = 0.0f;
  return d;
}

// ── File dialog (Win32) ───────────────────────────────
#ifdef OS_WINDOWS
#include <commdlg.h>   // GetOpenFileNameW
#include <windows.h>
#endif

std::string OpenFileDialog(const wchar_t* filter, const wchar_t* title) {
#ifdef OS_WINDOWS
  wchar_t path[MAX_PATH] = {};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner   = nullptr;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile   = path;
  ofn.nMaxFile    = MAX_PATH;
  ofn.lpstrTitle  = title;
  ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

  if (GetOpenFileNameW(&ofn)) {
    // Convert wchar_t to UTF-8 std::string
    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
      std::string result(len - 1, '\0');
      WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), len, nullptr, nullptr);
      return result;
    }
  }
#endif
  return {};
}

} // namespace t8ditor
