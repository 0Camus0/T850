/*********************************************************
 * T8ditor — ImGui integration layer.  See header.
 *********************************************************/

#include "EditorImGui.h"

#include <Config.h>
#include <imgui/ImGuiSystem.h>
#include <video/BaseDriver.h>
#include <core/Core.h>
#include <utils/Log.h>
#include <Descriptors.h>

// ImGui core
#include <imgui.h>

#ifdef OS_WINDOWS
#  include <video/d3d11/D3D11Texture.h>
#endif

#include <cmath>
#include <mutex>
#include <vector>

#include <ImGuizmo.h>

namespace t8ditor {

// ── Module state ──────────────────────────────────────
static t850::ImGuiSystem      s_imguiSystem;
static bool                   s_inited = false;
static t850::GraphicsApi::E   s_api = t850::GraphicsApi::D3D11;

// ── Log capture ring buffer ───────────────────────────
static const int              kMaxLogLines = 500;
struct LogLine {
  t850::Log::Level level;
  std::string      text;
};
static std::vector<LogLine>   s_logLines;
static std::mutex             s_logMutex;
static bool                   s_logAutoScroll = true;

static void EditorLogCallback(t850::Log::Level level, const char* msg) {
  std::lock_guard<std::mutex> lock(s_logMutex);
  if (s_logLines.size() >= (size_t)kMaxLogLines)
    s_logLines.erase(s_logLines.begin());
  s_logLines.push_back({ level, std::string(msg) });
}

void ImGuiLogCaptureStart() {
  t850::Log::SetCallback(EditorLogCallback);
}

void ImGuiLogCaptureStop() {
  t850::Log::SetCallback(nullptr);
  std::lock_guard<std::mutex> lock(s_logMutex);
  s_logLines.clear();
}

// ── Init ──────────────────────────────────────────────
bool ImGuiInit(t850::RootFramework* fw) {
  if (s_inited) return true;
  if (!fw || !fw->pVideoDriver) return false;
  s_api = fw->pVideoDriver->m_currentAPI;
  s_inited = s_imguiSystem.Init(fw, "imgui_layout.ini", true);
  return s_inited;
}

// ── Shutdown ──────────────────────────────────────────
void ImGuiShutdown() {
  if (!s_inited) return;
  s_imguiSystem.Shutdown();
  s_inited = false;
}

// ── NewFrame ──────────────────────────────────────────
void ImGuiNewFrame() {
  if (!s_inited) return;
  s_imguiSystem.NewFrame(true);
}

// ── Render ────────────────────────────────────────────
void ImGuiRender() {
  if (!s_inited) return;
  s_imguiSystem.Render();
}

// ── Menu bar ──────────────────────────────────────────
MenuAction ImGuiDrawMenuBar(PanelVisibility& panels) {
  MenuAction action;
  if (!s_inited) return action;

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Import Mesh ...", "Ctrl+I"))
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
int ImGuiDrawToolbar(int currentMode, int& addCamera, int& addLight,
                     bool& wantsClone, bool& wantsGroup, bool& wantsUngroup,
                     bool hasSelection, bool hasMultiSelect) {
  addCamera = -1;
  addLight  = -1;
  wantsClone = false;
  wantsGroup = false;
  wantsUngroup = false;
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

    ToolButton("Select (Q)", -1); ImGui::SameLine();
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

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::Button("Clone", ImVec2(55, 0)))
      wantsClone = true;
    if (!hasSelection) ImGui::EndDisabled();

    ImGui::SameLine();

    // Group / Ungroup buttons
    if (!hasMultiSelect) ImGui::BeginDisabled();
    if (ImGui::Button("Group", ImVec2(55, 0)))
      wantsGroup = true;
    if (!hasMultiSelect) ImGui::EndDisabled();

    ImGui::SameLine();
    if (!hasMultiSelect) ImGui::BeginDisabled();
    if (ImGui::Button("Ungroup", ImVec2(65, 0)))
      wantsUngroup = true;
    if (!hasMultiSelect) ImGui::EndDisabled();
  }
  ImGui::End();
  ImGui::PopStyleVar(2);

  return currentMode;
}

// ── Context menu (right-click viewport) ───────────────

ContextAction ImGuiDrawContextMenu(bool hasSelection, bool hasMultiSelect, bool hasGroup) {
  ContextAction action;
  if (!s_inited) return action;

  // Suppress context menu if right-click was used for camera rotation (drag).
  // Track whether the right mouse button was dragged during this press.
  static bool s_rightWasDragged = false;
  if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 3.0f))
    s_rightWasDragged = true;
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && s_rightWasDragged) {
    s_rightWasDragged = false;
    return action;  // skip popup this frame — user was rotating
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
    s_rightWasDragged = false;

  // Open on right-click in viewport (not over ImGui windows)
  if (ImGui::BeginPopupContextVoid("##ViewportContext", ImGuiPopupFlags_MouseButtonRight)) {

    // Transform modes
    ImGui::SeparatorText("Transform");
    if (ImGui::MenuItem("Select (Q)"))     action.setMode = -1;
    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::MenuItem("Move (W)"))       action.setMode = 0;
    if (ImGui::MenuItem("Rotate (E)"))     action.setMode = 1;
    if (ImGui::MenuItem("Scale (R)"))      action.setMode = 2;
    if (!hasSelection) ImGui::EndDisabled();

    ImGui::Separator();

    // Selection actions
    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::MenuItem("Frame View (Z)")) action.wantsFrameView = true;
    if (ImGui::MenuItem("Clone"))          action.wantsClone = true;
    if (ImGui::MenuItem("Delete"))         action.wantsDelete = true;
    if (!hasSelection) ImGui::EndDisabled();

    ImGui::Separator();

    // Group actions
    if (!hasMultiSelect) ImGui::BeginDisabled();
    if (ImGui::MenuItem("Group"))          action.wantsGroup = true;
    if (!hasMultiSelect) ImGui::EndDisabled();

    if (!hasGroup) ImGui::BeginDisabled();
    if (ImGui::MenuItem("Ungroup"))        action.wantsUngroup = true;
    if (!hasGroup) ImGui::EndDisabled();

    ImGui::Separator();

    // Add entities
    ImGui::SeparatorText("Add");
    if (ImGui::BeginMenu("Camera")) {
      if (ImGui::MenuItem("Perspective"))  action.addCamera = 0;
      if (ImGui::MenuItem("Orthographic")) action.addCamera = 1;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Light")) {
      if (ImGui::MenuItem("Directional"))  action.addLight = 0;
      if (ImGui::MenuItem("Omni"))         action.addLight = 1;
      ImGui::EndMenu();
    }

    ImGui::EndPopup();
  }

  return action;
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
static ImVec4 LogLevelColor(t850::Log::Level lvl) {
  switch (lvl) {
    case t850::Log::LVL_ERROR:   return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    case t850::Log::LVL_INFO:    return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    case t850::Log::LVL_DEBUG:   return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
    case t850::Log::LVL_VERBOSE: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    case t850::Log::LVL_TRACE:   return ImVec4(0.5f, 0.5f, 1.0f, 1.0f);
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

  // Scrollable log region with selectable/copyable text
  ImGui::BeginChild("LogScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar);

  {
    std::lock_guard<std::mutex> lock(s_logMutex);
    for (const auto& line : s_logLines) {
      ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(line.level));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
      // Selectable text: click to copy the line to clipboard
      if (ImGui::Selectable(line.text.c_str(), false)) {
        ImGui::SetClipboardText(line.text.c_str());
      }
      ImGui::PopStyleColor(2);
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

  t850::BaseDriver* drv = t850::g_pBaseDriver;
  if (!drv) { ImGui::End(); return selectedRT; }

  // Helper to get SRV for ImGui::Image (D3D11 only)
  auto GetSRV = [&](t850::Texture* tex) -> ImTextureID {
#ifdef OS_WINDOWS
    if (s_api == t850::GraphicsApi::D3D11 && tex) {
      // D3DXTexture has pSRVTex as a public ComPtr
      auto* d3dTex = static_cast<t850::D3DXTexture*>(tex);
      return (ImTextureID)d3dTex->pSRVTex.Get();
    }
#endif
    return (ImTextureID)nullptr;
  };

  int globalIdx = 0;
  for (int rtIdx = 0; rtIdx < (int)drv->RTs.size(); ++rtIdx) {
    t850::BaseRT* rt = drv->RTs[rtIdx];
    if (!rt) continue;

    ImGui::PushID(rtIdx);

    for (int ci = 0; ci < (int)rt->vColorTextures.size(); ++ci) {
      t850::Texture* tex = rt->vColorTextures[ci];
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
  return s_imguiSystem.ConsumeWheelDelta();
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
