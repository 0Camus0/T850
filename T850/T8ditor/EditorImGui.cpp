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
#  include <video/windows/D3D11Texture.h>
#  include <core/windows/Win32Framework.h>
#endif
#include <imgui_impl_opengl3.h>

#include <cmath>
#include <mutex>
#include <vector>

#include <ImGuizmo.h>

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

// ── Log capture ring buffer ───────────────────────────
static const int              kMaxLogLines = 500;
struct LogLine {
  t800::Log::Level level;
  std::string      text;
};
static std::vector<LogLine>   s_logLines;
static std::mutex             s_logMutex;
static bool                   s_logAutoScroll = true;

static void EditorLogCallback(t800::Log::Level level, const char* msg) {
  std::lock_guard<std::mutex> lock(s_logMutex);
  if (s_logLines.size() >= (size_t)kMaxLogLines)
    s_logLines.erase(s_logLines.begin());
  s_logLines.push_back({ level, std::string(msg) });
}

void ImGuiLogCaptureStart() {
  t800::Log::SetCallback(EditorLogCallback);
}

void ImGuiLogCaptureStop() {
  t800::Log::SetCallback(nullptr);
  std::lock_guard<std::mutex> lock(s_logMutex);
  s_logLines.clear();
}

// ── SDL3 event watcher ────────────────────────────────
static bool sdlEventWatcher(void* /*userdata*/, SDL_Event* event) {
  ImGui_ImplSDL3_ProcessEvent(event);
  if (event->type == SDL_EVENT_MOUSE_WHEEL)
    s_wheelAccum += event->wheel.y;
  return true;
}

// ── Init ──────────────────────────────────────────────
bool ImGuiInit(t800::RootFramework* fw) {
  if (s_inited) return true;
  if (!fw || !fw->pVideoDriver) return false;

#ifdef OS_WINDOWS
  auto* w32 = static_cast<t800::Win32Framework*>(fw);
  s_sdlWindow = w32->m_pWindow;
#else
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

  // Save/load layout to imgui_layout.ini in the working directory
  io.IniFilename = "imgui_layout.ini";

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

  // Create a dockspace over the entire viewport so panels can dock to edges
  ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
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
MenuAction ImGuiDrawMenuBar(PanelVisibility& panels) {
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
      ImGui::MenuItem("Hierarchy", nullptr, &panels.showHierarchy);
      ImGui::MenuItem("Inspector", nullptr, &panels.showInspector);
      ImGui::MenuItem("Console",   nullptr, &panels.showConsole);
      ImGui::Separator();
      ImGui::MenuItem("Wireframe Overlay", nullptr, &panels.showWireframe);
      ImGui::MenuItem("Show Skybox",       nullptr, &panels.showSkybox);
      ImGui::Separator();
      ImGui::MenuItem("RT Debug",          nullptr, &panels.showRTDebug);
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

// ── Toolbar ───────────────────────────────────────────
int ImGuiDrawToolbar(int currentMode, int& addCamera, int& addLight) {
  addCamera = -1;
  addLight  = -1;
  if (!s_inited) return currentMode;

  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0), ImGuiCond_Always);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                         | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoScrollbar
                         | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoDocking;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

  if (ImGui::Begin("##Toolbar", nullptr, flags)) {
    ImVec4 activeCol = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    ImVec2 btnSize(70, 0);

    auto ToolButton = [&](const char* label, int mode) {
      bool isActive = (currentMode == mode);
      if (isActive)
        ImGui::PushStyleColor(ImGuiCol_Button, activeCol);
      if (ImGui::Button(label, btnSize))
        currentMode = mode;
      if (isActive)
        ImGui::PopStyleColor();
    };

    ToolButton("Move (W)",   0);  ImGui::SameLine();
    ToolButton("Rotate (E)", 1);  ImGui::SameLine();
    ToolButton("Scale (R)",  2);

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // Camera add button with popup
    if (ImGui::Button("+ Camera", ImVec2(75, 0)))
      ImGui::OpenPopup("AddCameraPopup");
    if (ImGui::BeginPopup("AddCameraPopup")) {
      if (ImGui::MenuItem("Perspective"))  addCamera = 0;
      if (ImGui::MenuItem("Orthographic")) addCamera = 1;
      ImGui::EndPopup();
    }

    ImGui::SameLine();

    // Light add button with popup
    if (ImGui::Button("+ Light", ImVec2(65, 0)))
      ImGui::OpenPopup("AddLightPopup");
    if (ImGui::BeginPopup("AddLightPopup")) {
      if (ImGui::MenuItem("Directional")) addLight = 0;
      if (ImGui::MenuItem("Omni"))        addLight = 1;
      ImGui::EndPopup();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);

  return currentMode;
}

// ── Hierarchy panel ───────────────────────────────────
bool ImGuiDrawHierarchyPanel(const char* meshName, bool hasMesh, bool& selected) {
  bool selectionChanged = false;

  ImGui::SetNextWindowSize(ImVec2(250, 300), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Hierarchy")) {
    ImGui::End();
    return false;
  }

  ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow
                               | ImGuiTreeNodeFlags_DefaultOpen;

  if (ImGui::TreeNodeEx("Scene Root", rootFlags)) {
    if (hasMesh && meshName) {
      ImGuiTreeNodeFlags leafFlags = ImGuiTreeNodeFlags_Leaf
                                   | ImGuiTreeNodeFlags_SpanAvailWidth;
      if (selected)
        leafFlags |= ImGuiTreeNodeFlags_Selected;

      bool nodeOpen = ImGui::TreeNodeEx(meshName, leafFlags);
      if (ImGui::IsItemClicked()) {
        selected = !selected;
        selectionChanged = true;
      }
      if (nodeOpen)
        ImGui::TreePop();
    }
    ImGui::TreePop();
  }

  ImGui::End();
  return selectionChanged;
}

// ── Inspector panel ───────────────────────────────────
void ImGuiDrawInspectorPanel(XVECTOR3& pos, XVECTOR3& eulerDeg,
                             XVECTOR3& scale, bool hasMesh) {
  ImGui::SetNextWindowSize(ImVec2(300, 350), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Inspector")) {
    ImGui::End();
    return;
  }

  if (!hasMesh) {
    ImGui::TextDisabled("No mesh loaded.");
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("Transform");

  float p[3] = { pos.x, pos.y, pos.z };
  if (ImGui::DragFloat3("Position", p, 0.1f)) {
    pos.x = p[0]; pos.y = p[1]; pos.z = p[2];
  }

  float r[3] = { eulerDeg.x, eulerDeg.y, eulerDeg.z };
  if (ImGui::DragFloat3("Rotation", r, 0.5f)) {
    eulerDeg.x = r[0]; eulerDeg.y = r[1]; eulerDeg.z = r[2];
  }

  float s[3] = { scale.x, scale.y, scale.z };
  if (ImGui::DragFloat3("Scale", s, 0.01f, 0.01f, 100.0f)) {
    scale.x = s[0]; scale.y = s[1]; scale.z = s[2];
  }

  ImGui::End();
}

// ── Console panel ─────────────────────────────────────
static ImVec4 LogLevelColor(t800::Log::Level lvl) {
  switch (lvl) {
    case t800::Log::LVL_ERROR:   return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    case t800::Log::LVL_INFO:    return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    case t800::Log::LVL_DEBUG:   return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
    case t800::Log::LVL_VERBOSE: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    case t800::Log::LVL_TRACE:   return ImVec4(0.5f, 0.5f, 1.0f, 1.0f);
    default:                     return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  }
}

void ImGuiDrawConsolePanel() {
  ImGui::SetNextWindowSize(ImVec2(600, 200), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Console")) {
    ImGui::End();
    return;
  }

  // Clear button
  if (ImGui::SmallButton("Clear")) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_logLines.clear();
  }
  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &s_logAutoScroll);
  ImGui::Separator();

  // Scrollable log region
  ImGui::BeginChild("LogScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar);

  {
    std::lock_guard<std::mutex> lock(s_logMutex);
    for (const auto& line : s_logLines) {
      ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(line.level));
      ImGui::TextUnformatted(line.text.c_str());
      ImGui::PopStyleColor();
    }
  }

  if (s_logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    ImGui::SetScrollHereY(1.0f);

  ImGui::EndChild();
  ImGui::End();
}

// ── RT Debug panel ────────────────────────────────────

int ImGuiDrawRTDebugPanel(int selectedRT) {
  if (!s_inited) return selectedRT;

  ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Render Targets")) {
    ImGui::End();
    return selectedRT;
  }

  // "BackBuffer" entry (selectedRT = -1)
  {
    bool isSel = (selectedRT < 0);
    if (isSel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
    if (ImGui::Selectable("BackBuffer (default)", isSel))
      selectedRT = -1;
    if (isSel) ImGui::PopStyleColor();
  }
  ImGui::Separator();

  t800::BaseDriver* drv = t800::g_pBaseDriver;
  if (!drv) { ImGui::End(); return selectedRT; }

  // Helper to get SRV for ImGui::Image (D3D11 only)
  auto GetSRV = [&](t800::Texture* tex) -> ImTextureID {
#ifdef OS_WINDOWS
    if (s_api == t800::GRAPHICS_API::D3D11 && tex) {
      // D3DXTexture has pSRVTex as a public ComPtr
      auto* d3dTex = static_cast<t800::D3DXTexture*>(tex);
      return (ImTextureID)d3dTex->pSRVTex.Get();
    }
#endif
    return (ImTextureID)nullptr;
  };

  int globalIdx = 0;
  for (int rtIdx = 0; rtIdx < (int)drv->RTs.size(); ++rtIdx) {
    t800::BaseRT* rt = drv->RTs[rtIdx];
    if (!rt) continue;

    ImGui::PushID(rtIdx);

    for (int ci = 0; ci < (int)rt->vColorTextures.size(); ++ci) {
      t800::Texture* tex = rt->vColorTextures[ci];
      if (!tex) continue;

      char label[128];
      snprintf(label, sizeof(label), "RT%d : Color%d (%ux%u)", rtIdx, ci, tex->x, tex->y);

      bool isSel = (selectedRT == globalIdx);
      if (isSel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));

      ImTextureID srv = GetSRV(tex);
      if (srv) {
        ImGui::Image(srv, ImVec2(140, 79));
        if (ImGui::IsItemClicked()) selectedRT = globalIdx;
      }

      if (ImGui::Selectable(label, isSel))
        selectedRT = globalIdx;

      if (isSel) ImGui::PopStyleColor();
      globalIdx++;
    }

    if (rt->pDepthTexture) {
      char label[128];
      snprintf(label, sizeof(label), "RT%d : Depth (%ux%u)", rtIdx, rt->pDepthTexture->x, rt->pDepthTexture->y);
      bool isSel = (selectedRT == globalIdx);
      if (isSel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));

      ImTextureID srv = GetSRV(rt->pDepthTexture);
      if (srv) {
        ImGui::Image(srv, ImVec2(140, 79));
        if (ImGui::IsItemClicked()) selectedRT = globalIdx;
      }

      if (ImGui::Selectable(label, isSel))
        selectedRT = globalIdx;

      if (isSel) ImGui::PopStyleColor();
      globalIdx++;
    }

    ImGui::Separator();
    ImGui::PopID();
  }

  ImGui::End();
  return selectedRT;
}

// ── ImGuizmo ──────────────────────────────────────────

void ImGuizmoBeginFrame(int vpX, int vpY, int vpW, int vpH, bool ortho) {
  ImGuizmo::BeginFrame();
  ImGuizmo::SetOrthographic(ortho);
  ImGuizmo::SetRect((float)vpX, (float)vpY, (float)vpW, (float)vpH);
}

bool ImGuizmoManipulate(const float* view, const float* proj,
                        int operation, float* worldMatrix) {
  ImGuizmo::OPERATION op;
  switch (operation) {
    case 0:  op = ImGuizmo::TRANSLATE; break;
    case 1:  op = ImGuizmo::ROTATE;    break;
    case 2:  op = ImGuizmo::SCALE;     break;
    default: op = ImGuizmo::TRANSLATE; break;
  }

  return ImGuizmo::Manipulate(view, proj, op, ImGuizmo::WORLD,
                              worldMatrix, nullptr, nullptr);
}

// ── Mouse wheel ───────────────────────────────────────
float ImGuiConsumeWheelDelta() {
  float d = s_wheelAccum;
  s_wheelAccum = 0.0f;
  return d;
}

// ── File dialog (Win32) ───────────────────────────────
#ifdef OS_WINDOWS
#include <commdlg.h>
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