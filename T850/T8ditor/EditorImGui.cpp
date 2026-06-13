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
#include <imgui_internal.h>

#ifndef OS_ANDROID
#include <imgui_impl_vulkan.h>
#endif

#ifdef OS_WINDOWS
#  include <video/d3d11/D3D11Texture.h>
#  include <video/d3d12/D3D12Texture.h>
#  include <video/vulkan/VulkanTexture.h>
#endif

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <ImGuizmo.h>
#ifndef OS_ANDROID
#include <SDL3/SDL.h>
#endif

namespace t8ditor {

// ── Module state ──────────────────────────────────────
static t850::ImGuiSystem      s_imguiSystem;
static bool                   s_inited = false;
static t850::GraphicsApi::E   s_api = t850::GraphicsApi::D3D11;
static std::unordered_map<void*, uint64_t> s_imguiVkTextureDescriptors;
static bool                   s_showAppearanceDialog = false;
static int                    s_editorTheme = 0;
static float                  s_editorFontScale = 1.04f;
static bool                   s_allowCustomSceneLayout = false;
static std::string            s_globalLayoutPath;

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

static void MigrateLegacyRelativeLayout(const std::filesystem::path& globalLayoutPath) {
  std::error_code ec;
  if (std::filesystem::exists(globalLayoutPath, ec)) {
    return;
  }

  std::filesystem::path currentDirectory = std::filesystem::current_path(ec);
  if (ec) {
    return;
  }

  const std::filesystem::path legacyPath = currentDirectory / "imgui_layout.ini";
  if (legacyPath.lexically_normal() == globalLayoutPath.lexically_normal() ||
      !std::filesystem::exists(legacyPath, ec)) {
    return;
  }

  std::filesystem::create_directories(globalLayoutPath.parent_path(), ec);
  ec.clear();
  std::filesystem::copy_file(legacyPath, globalLayoutPath, std::filesystem::copy_options::skip_existing, ec);
}

static std::string BuildGlobalLayoutPath() {
#ifndef OS_ANDROID
  if (char* prefPath = SDL_GetPrefPath("T850", "T8ditor")) {
    std::filesystem::path directory(prefPath);
    SDL_free(prefPath);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const std::filesystem::path layoutPath = directory / "imgui_layout.ini";
    MigrateLegacyRelativeLayout(layoutPath);
    return layoutPath.string();
  }
#endif

  std::error_code ec;
  std::filesystem::path directory = std::filesystem::current_path(ec);
  if (ec) {
    directory = ".";
  }
  const std::filesystem::path layoutPath = directory / "imgui_layout.ini";
  MigrateLegacyRelativeLayout(layoutPath);
  return layoutPath.string();
}

static void SetGlobalLayoutIniTarget() {
  if (!s_inited || s_globalLayoutPath.empty()) {
    return;
  }
  ImGui::GetIO().IniFilename = s_globalLayoutPath.c_str();
}

static void DisableLayoutIniTarget() {
  if (!s_inited) {
    return;
  }
  ImGui::GetIO().IniFilename = nullptr;
}

static void LoadGlobalLayoutFromDisk() {
  if (!s_inited || s_globalLayoutPath.empty()) {
    return;
  }
  std::error_code ec;
  if (std::filesystem::exists(s_globalLayoutPath, ec)) {
    ImGui::LoadIniSettingsFromDisk(s_globalLayoutPath.c_str());
  }
}

void ImGuiSaveGlobalLayout() {
  if (!s_inited || s_globalLayoutPath.empty()) {
    return;
  }
  ImGui::SaveIniSettingsToDisk(s_globalLayoutPath.c_str());
}

static void SaveGlobalLayoutIfDirty() {
  if (!s_inited || s_allowCustomSceneLayout || s_globalLayoutPath.empty()) {
    return;
  }

  ImGuiContext* context = ImGui::GetCurrentContext();
  if (!context || context->SettingsDirtyTimer <= 0.0f) {
    return;
  }

  ImGui::SaveIniSettingsToDisk(s_globalLayoutPath.c_str());
  context->SettingsDirtyTimer = 0.0f;
}

bool ImGuiAllowCustomSceneLayout() {
  return s_allowCustomSceneLayout;
}

void ImGuiSetAllowCustomSceneLayout(bool allow) {
  if (s_allowCustomSceneLayout == allow) {
    return;
  }

  if (allow) {
    ImGuiSaveGlobalLayout();
    s_allowCustomSceneLayout = true;
    DisableLayoutIniTarget();
  } else {
    s_allowCustomSceneLayout = false;
    SetGlobalLayoutIniTarget();
    LoadGlobalLayoutFromDisk();
  }
}

void ImGuiApplySceneLayout(bool allowCustomLayout, const std::string& sceneLayoutIni) {
  if (!s_inited) {
    s_allowCustomSceneLayout = allowCustomLayout;
    return;
  }

  if (!allowCustomLayout) {
    ImGuiSetAllowCustomSceneLayout(false);
    return;
  }

  if (!s_allowCustomSceneLayout) {
    ImGuiSaveGlobalLayout();
  }
  s_allowCustomSceneLayout = true;
  DisableLayoutIniTarget();
  LoadGlobalLayoutFromDisk();
  if (!sceneLayoutIni.empty()) {
    ImGui::LoadIniSettingsFromMemory(sceneLayoutIni.c_str(), sceneLayoutIni.size());
  }
}

std::string ImGuiCaptureCurrentLayout() {
  if (!s_inited) {
    return {};
  }
  size_t dataSize = 0;
  const char* data = ImGui::SaveIniSettingsToMemory(&dataSize);
  if (!data || dataSize == 0) {
    return {};
  }
  return std::string(data, dataSize);
}

static void ApplyArtistEditorStyle() {
  if (s_editorTheme == 1) {
    ImGui::StyleColorsLight();
  } else {
    ImGui::StyleColorsDark();
  }

  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = ImVec2(14.0f, 12.0f);
  style.FramePadding = ImVec2(10.0f, 6.0f);
  style.CellPadding = ImVec2(8.0f, 5.0f);
  style.ItemSpacing = ImVec2(9.0f, 7.0f);
  style.ItemInnerSpacing = ImVec2(7.0f, 5.0f);
  style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
  style.IndentSpacing = 18.0f;
  style.ScrollbarSize = 14.0f;
  style.GrabMinSize = 12.0f;
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBorderSize = 0.0f;
  style.WindowRounding = 8.0f;
  style.ChildRounding = 6.0f;
  style.FrameRounding = 5.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 8.0f;
  style.GrabRounding = 5.0f;
  style.TabRounding = 6.0f;
  style.WindowMenuButtonPosition = ImGuiDir_Right;

  ImVec4* colors = style.Colors;
  if (s_editorTheme == 1) {
    const ImVec4 text(0.090f, 0.105f, 0.130f, 1.0f);
    const ImVec4 muted(0.420f, 0.455f, 0.510f, 1.0f);
    const ImVec4 panel(0.930f, 0.940f, 0.955f, 0.98f);
    const ImVec4 panel2(0.860f, 0.880f, 0.910f, 1.0f);
    const ImVec4 panel3(0.780f, 0.815f, 0.870f, 1.0f);
    const ImVec4 accent(0.150f, 0.360f, 0.760f, 1.0f);
    const ImVec4 accentHover(0.230f, 0.450f, 0.890f, 1.0f);
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = muted;
    colors[ImGuiCol_WindowBg] = panel;
    colors[ImGuiCol_ChildBg] = ImVec4(0.965f, 0.970f, 0.980f, 0.96f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.980f, 0.985f, 0.995f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.560f, 0.600f, 0.670f, 0.55f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = panel2;
    colors[ImGuiCol_FrameBgHovered] = panel3;
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.700f, 0.755f, 0.830f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.760f, 0.795f, 0.860f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.670f, 0.730f, 0.840f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.810f, 0.835f, 0.880f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.825f, 0.850f, 0.895f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.900f, 0.915f, 0.940f, 0.65f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.620f, 0.660f, 0.730f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.520f, 0.580f, 0.680f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentHover;
    colors[ImGuiCol_Button] = ImVec4(0.790f, 0.825f, 0.890f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.700f, 0.765f, 0.870f, 1.0f);
    colors[ImGuiCol_ButtonActive] = accent;
    colors[ImGuiCol_Header] = ImVec4(0.760f, 0.815f, 0.905f, 0.95f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.660f, 0.745f, 0.885f, 1.0f);
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Separator] = ImVec4(0.620f, 0.660f, 0.730f, 0.60f);
    colors[ImGuiCol_SeparatorHovered] = accentHover;
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.150f, 0.360f, 0.760f, 0.18f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.150f, 0.360f, 0.760f, 0.50f);
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_Tab] = panel2;
    colors[ImGuiCol_TabHovered] = ImVec4(0.650f, 0.735f, 0.880f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.710f, 0.775f, 0.890f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.880f, 0.900f, 0.935f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = panel2;
    colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.960f, 0.965f, 0.975f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.870f, 0.480f, 0.120f, 1.0f);
    colors[ImGuiCol_TableHeaderBg] = panel2;
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.620f, 0.660f, 0.730f, 1.0f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.720f, 0.750f, 0.810f, 1.0f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.150f, 0.360f, 0.760f, 0.26f);
    colors[ImGuiCol_NavHighlight] = accentHover;
    ImGui::GetIO().FontGlobalScale = s_editorFontScale;
    return;
  }

  if (s_editorTheme == 2) {
    const ImVec4 bg(0.015f, 0.017f, 0.022f, 1.0f);
    const ImVec4 panel(0.030f, 0.035f, 0.045f, 0.99f);
    const ImVec4 panel2(0.075f, 0.085f, 0.105f, 1.0f);
    const ImVec4 panel3(0.125f, 0.145f, 0.180f, 1.0f);
    const ImVec4 accent(0.980f, 0.700f, 0.180f, 1.0f);
    const ImVec4 accentHover(1.000f, 0.820f, 0.320f, 1.0f);
    colors[ImGuiCol_Text] = ImVec4(0.960f, 0.965f, 0.975f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.520f, 0.550f, 0.600f, 1.0f);
    colors[ImGuiCol_WindowBg] = panel;
    colors[ImGuiCol_ChildBg] = bg;
    colors[ImGuiCol_PopupBg] = panel;
    colors[ImGuiCol_Border] = ImVec4(0.330f, 0.350f, 0.390f, 0.70f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = panel2;
    colors[ImGuiCol_FrameBgHovered] = panel3;
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.180f, 0.200f, 0.245f, 1.0f);
    colors[ImGuiCol_TitleBg] = bg;
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.065f, 0.075f, 0.095f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = bg;
    colors[ImGuiCol_MenuBarBg] = bg;
    colors[ImGuiCol_Button] = panel2;
    colors[ImGuiCol_ButtonHovered] = panel3;
    colors[ImGuiCol_ButtonActive] = accent;
    colors[ImGuiCol_Header] = panel2;
    colors[ImGuiCol_HeaderHovered] = panel3;
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_CheckMark] = accentHover;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentHover;
    colors[ImGuiCol_Separator] = ImVec4(0.330f, 0.350f, 0.390f, 0.70f);
    colors[ImGuiCol_SeparatorHovered] = accentHover;
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_Tab] = panel2;
    colors[ImGuiCol_TabHovered] = panel3;
    colors[ImGuiCol_TabActive] = ImVec4(0.155f, 0.175f, 0.220f, 1.0f);
    colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.980f, 0.700f, 0.180f, 0.30f);
    colors[ImGuiCol_NavHighlight] = accentHover;
    ImGui::GetIO().FontGlobalScale = s_editorFontScale;
    return;
  }

  const ImVec4 bg(0.070f, 0.078f, 0.095f, 1.0f);
  const ImVec4 panel(0.105f, 0.118f, 0.145f, 0.98f);
  const ImVec4 panel2(0.135f, 0.153f, 0.188f, 1.0f);
  const ImVec4 panel3(0.175f, 0.198f, 0.245f, 1.0f);
  const ImVec4 text(0.890f, 0.910f, 0.940f, 1.0f);
  const ImVec4 muted(0.560f, 0.610f, 0.680f, 1.0f);
  const ImVec4 accent(0.340f, 0.520f, 0.960f, 1.0f);
  const ImVec4 accentHover(0.430f, 0.610f, 1.000f, 1.0f);
  const ImVec4 warm(0.950f, 0.680f, 0.230f, 1.0f);

  colors[ImGuiCol_Text] = text;
  colors[ImGuiCol_TextDisabled] = muted;
  colors[ImGuiCol_WindowBg] = panel;
  colors[ImGuiCol_ChildBg] = ImVec4(0.080f, 0.090f, 0.112f, 0.96f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.085f, 0.105f, 0.98f);
  colors[ImGuiCol_Border] = ImVec4(0.255f, 0.295f, 0.365f, 0.65f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_FrameBg] = panel2;
  colors[ImGuiCol_FrameBgHovered] = panel3;
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.230f, 0.310f, 0.430f, 1.0f);
  colors[ImGuiCol_TitleBg] = bg;
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.105f, 0.135f, 0.185f, 1.0f);
  colors[ImGuiCol_TitleBgCollapsed] = bg;
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.050f, 0.058f, 0.075f, 1.0f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.050f, 0.058f, 0.075f, 0.65f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.265f, 0.315f, 0.390f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.350f, 0.410f, 0.500f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabActive] = accent;
  colors[ImGuiCol_CheckMark] = accentHover;
  colors[ImGuiCol_SliderGrab] = accent;
  colors[ImGuiCol_SliderGrabActive] = accentHover;
  colors[ImGuiCol_Button] = ImVec4(0.185f, 0.220f, 0.285f, 1.0f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.255f, 0.315f, 0.400f, 1.0f);
  colors[ImGuiCol_ButtonActive] = accent;
  colors[ImGuiCol_Header] = ImVec4(0.180f, 0.230f, 0.325f, 0.95f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.245f, 0.315f, 0.435f, 1.0f);
  colors[ImGuiCol_HeaderActive] = accent;
  colors[ImGuiCol_Separator] = ImVec4(0.290f, 0.340f, 0.420f, 0.65f);
  colors[ImGuiCol_SeparatorHovered] = accentHover;
  colors[ImGuiCol_SeparatorActive] = accent;
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.340f, 0.520f, 0.960f, 0.22f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.340f, 0.520f, 0.960f, 0.55f);
  colors[ImGuiCol_ResizeGripActive] = accent;
  colors[ImGuiCol_Tab] = panel2;
  colors[ImGuiCol_TabHovered] = ImVec4(0.245f, 0.315f, 0.435f, 1.0f);
  colors[ImGuiCol_TabActive] = ImVec4(0.200f, 0.270f, 0.380f, 1.0f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.080f, 0.090f, 0.112f, 1.0f);
  colors[ImGuiCol_TabUnfocusedActive] = panel2;
  colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
  colors[ImGuiCol_DockingEmptyBg] = bg;
  colors[ImGuiCol_PlotHistogram] = warm;
  colors[ImGuiCol_TableHeaderBg] = panel2;
  colors[ImGuiCol_TableBorderStrong] = ImVec4(0.300f, 0.350f, 0.430f, 1.0f);
  colors[ImGuiCol_TableBorderLight] = ImVec4(0.220f, 0.260f, 0.320f, 1.0f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(0.340f, 0.520f, 0.960f, 0.35f);
  colors[ImGuiCol_NavHighlight] = accentHover;

  ImGuiIO& io = ImGui::GetIO();
  io.FontGlobalScale = s_editorFontScale;
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
bool ImGuiInit(t850::RootFramework* fw, bool enablePlatformWindows) {
  if (s_inited) return true;
  if (!fw || !fw->pVideoDriver) return false;
  s_api = fw->pVideoDriver->m_currentAPI;
  s_globalLayoutPath = BuildGlobalLayoutPath();
  s_inited = s_imguiSystem.Init(fw, s_globalLayoutPath.c_str(), true, enablePlatformWindows);
  if (s_inited) {
    s_allowCustomSceneLayout = false;
    ApplyArtistEditorStyle();
  }
  return s_inited;
}

// ── Shutdown ──────────────────────────────────────────
void ImGuiShutdown() {
  if (!s_inited) return;
  s_imguiSystem.Shutdown();
  s_imguiVkTextureDescriptors.clear();
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
  SaveGlobalLayoutIfDirty();
}

void ImGuiSetNextNativeEditorWindow(float offsetX, float offsetY, float width, float height) {
  if (!s_inited) return;

  ImGuiWindowClass windowClass{};
  windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
  windowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon;
  ImGui::SetNextWindowClass(&windowClass);

  ImGuiViewport* mainViewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(mainViewport->Pos.x + offsetX, mainViewport->Pos.y + offsetY),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
}

ImTextureID ImGuiTextureID(t850::BaseDriver* driver, t850::Texture* texture) {
  if (!driver || !texture) {
    return (ImTextureID)nullptr;
  }

#ifdef OS_WINDOWS
  if (driver->m_currentAPI == t850::GraphicsApi::D3D11) {
    auto* d3dTexture = static_cast<t850::D3DXTexture*>(texture);
    return (ImTextureID)d3dTexture->pSRVTex.Get();
  }
  if (driver->m_currentAPI == t850::GraphicsApi::D3D12) {
    auto* d3dTexture = static_cast<t850::D3D12Texture*>(texture);
    return (ImTextureID)d3dTexture->srvGPU.ptr;
  }
  if (driver->m_currentAPI == t850::GraphicsApi::VULKAN) {
    auto* vkTexture = static_cast<t850::VulkanTexture*>(texture);
    if (!vkTexture->m_sampler || !vkTexture->m_imageView) {
      return (ImTextureID)nullptr;
    }
    auto found = s_imguiVkTextureDescriptors.find(texture);
    if (found != s_imguiVkTextureDescriptors.end()) {
#if defined(VK_USE_64_BIT_PTR_DEFINES) && VK_USE_64_BIT_PTR_DEFINES
      return (ImTextureID)reinterpret_cast<VkDescriptorSet>((uintptr_t)found->second);
#else
      return (ImTextureID)(VkDescriptorSet)found->second;
#endif
    }
    VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
        vkTexture->m_sampler,
        vkTexture->m_imageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#if defined(VK_USE_64_BIT_PTR_DEFINES) && VK_USE_64_BIT_PTR_DEFINES
    s_imguiVkTextureDescriptors[texture] = (uint64_t)(uintptr_t)descriptor;
#else
    s_imguiVkTextureDescriptors[texture] = (uint64_t)descriptor;
#endif
    return (ImTextureID)descriptor;
  }
#endif

  if (driver->m_currentAPI == t850::GraphicsApi::OPENGL) {
    return (ImTextureID)(intptr_t)texture->id;
  }

  return (ImTextureID)nullptr;
}

static float ToolbarButtonWidth(const char* label, float minWidth) {
  return (std::max)(minWidth, ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f);
}

static void DrawAppearanceDialog() {
  if (!s_showAppearanceDialog) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(420.0f, 360.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Appearance", &s_showAppearanceDialog, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  bool changed = false;
  const char* themes[] = { "Dark Studio", "Light Studio", "Graphite" };
  ImGui::SeparatorText("Theme");
  changed |= ImGui::Combo("Color Theme", &s_editorTheme, themes, static_cast<int>(sizeof(themes) / sizeof(themes[0])));

  ImGui::SeparatorText("Text Size");
  changed |= ImGui::SliderFloat("Font Size", &s_editorFontScale, 0.85f, 1.45f, "%.2fx");
  ImGui::SameLine();
  if (ImGui::SmallButton("Reset")) {
    s_editorFontScale = 1.04f;
    changed = true;
  }
  ImGui::TextDisabled("Applies immediately to all editor panels.");

  if (changed) {
    ApplyArtistEditorStyle();
  }

  ImGui::SeparatorText("Layout");
  bool allowCustomLayout = ImGuiAllowCustomSceneLayout();
  if (ImGui::Checkbox("Allow Custom Layout", &allowCustomLayout)) {
    ImGuiSetAllowCustomSceneLayout(allowCustomLayout);
  }
  ImGui::TextWrapped("%s",
      allowCustomLayout
          ? "Panel docking and window positions are saved into the current scene when you save it."
          : "Panel docking and window positions are saved globally and shared by all scenes.");

  ImGui::SeparatorText("Preview");
  ImGui::BeginChild("AppearancePreview", ImVec2(0.0f, 120.0f), true);
  ImGui::TextUnformatted("Scene Hierarchy");
  ImGui::TextDisabled("Game Entities / Scene objects");
  ImGui::Button("Primary Action");
  ImGui::SameLine();
  ImGui::Button("Secondary");
  float previewValue = 0.42f;
  ImGui::SliderFloat("Exposure", &previewValue, 0.0f, 1.0f, "%.2f");
  bool previewCheck = true;
  ImGui::Checkbox("Show orientation arrows", &previewCheck);
  ImGui::EndChild();

  if (ImGui::Button("Close", ImVec2(ToolbarButtonWidth("Close", 90.0f), 0.0f))) {
    s_showAppearanceDialog = false;
  }

  ImGui::End();
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
      if (ImGui::MenuItem("Appearance..."))
        s_showAppearanceDialog = true;
      ImGui::Separator();
      ImGui::MenuItem("Hierarchy", nullptr, &panels.showHierarchy);
      ImGui::MenuItem("Inspector", nullptr, &panels.showInspector);
      ImGui::MenuItem("Console",   nullptr, &panels.showConsole);
      ImGui::MenuItem("Rendering", nullptr, &panels.showRendering);
      ImGui::Separator();
      ImGui::MenuItem("Wireframe Overlay", nullptr, &panels.showWireframe);
      ImGui::MenuItem("Show Skybox",       nullptr, &panels.showSkybox);
      ImGui::Separator();
      ImGui::MenuItem("RT Debug",          nullptr, &panels.showRTDebug);
      ImGui::Separator();
      if (ImGui::MenuItem("Reset Artist Layout"))
        action.wantsResetLayout = true;
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

  DrawAppearanceDialog();
  return action;
}

void ImGuiClampCurrentWindowToEditorWorkArea() {
  ImGuiViewport* mainViewport = ImGui::GetMainViewport();
  if (!mainViewport || ImGui::IsWindowDocked()) {
    return;
  }
  ImVec2 pos = ImGui::GetWindowPos();
  if (pos.y < mainViewport->WorkPos.y) {
    ImGui::SetWindowPos(ImVec2(pos.x, mainViewport->WorkPos.y), ImGuiCond_Always);
  }
}

// ── Toolbar ───────────────────────────────────────────
int ImGuiDrawToolbar(int currentMode, int& addCamera, int& addLight,
                     bool& wantsClone, bool& wantsGroup, bool& wantsUngroup,
                     bool& wantsPlayScene,
                     bool hasSelection, bool hasMultiSelect,
                     int& cameraMode) {
  addCamera = -1;
  addLight  = -1;
  wantsClone = false;
  wantsGroup = false;
  wantsUngroup = false;
  wantsPlayScene = false;
  if (!s_inited) return currentMode;

  ImGuiViewport* vp = ImGui::GetMainViewport();
  if (!vp) return currentMode;

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                         | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoScrollbar
                         | ImGuiWindowFlags_NoScrollWithMouse
                         | ImGuiWindowFlags_NoSavedSettings
                         | ImGuiWindowFlags_NoDocking;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

  const float toolbarHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
  if (ImGui::BeginViewportSideBar("##T8ditorToolbar", vp, ImGuiDir_Up, toolbarHeight, flags)) {
    ImVec4 activeCol = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    ImVec4 mutedText = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

    auto ToolButton = [&](const char* label, int mode) {
      bool isActive = (currentMode == mode);
      if (isActive)
        ImGui::PushStyleColor(ImGuiCol_Button, activeCol);
      if (ImGui::Button(label, ImVec2(ToolbarButtonWidth(label, 82.0f), 0)))
        currentMode = mode;
      if (isActive)
        ImGui::PopStyleColor();
    };

    ImGui::TextDisabled("TOOLS");
    ImGui::SameLine();
    ToolButton("Select  Q", -1); ImGui::SameLine();
    ToolButton("Move  W",   0);  ImGui::SameLine();
    ToolButton("Rotate  E", 1);  ImGui::SameLine();
    ToolButton("Scale  R",  2);

    ImGui::SameLine();
    ImGui::TextColored(mutedText, "  |  ");
    ImGui::SameLine();

    if (ImGui::Button("Play Scene", ImVec2(ToolbarButtonWidth("Play Scene", 98.0f), 0)))
      wantsPlayScene = true;

    ImGui::SameLine();
    ImGui::TextColored(mutedText, "  |  ");
    ImGui::SameLine();

    auto CameraModeButton = [&](const char* label, int mode) {
      const bool isActive = cameraMode == mode;
      if (isActive)
        ImGui::PushStyleColor(ImGuiCol_Button, activeCol);
      if (ImGui::Button(label, ImVec2(ToolbarButtonWidth(label, 84.0f), 0)))
        cameraMode = mode;
      if (isActive)
        ImGui::PopStyleColor();
    };

    ImGui::TextDisabled("CAMERA");
    ImGui::SameLine();
    CameraModeButton("Orbit", 0); ImGui::SameLine();
    CameraModeButton("Fly", 1);

    ImGui::SameLine();
    ImGui::TextColored(mutedText, "  |  ");
    ImGui::SameLine();

    // Camera add button with popup
    if (ImGui::Button("Add Camera", ImVec2(ToolbarButtonWidth("Add Camera", 104.0f), 0)))
      ImGui::OpenPopup("AddCameraPopup");
    if (ImGui::BeginPopup("AddCameraPopup")) {
      if (ImGui::MenuItem("Perspective"))  addCamera = 0;
      if (ImGui::MenuItem("Orthographic")) addCamera = 1;
      ImGui::EndPopup();
    }

    ImGui::SameLine();

    // Light add button with popup
    if (ImGui::Button("Add Light", ImVec2(ToolbarButtonWidth("Add Light", 94.0f), 0)))
      ImGui::OpenPopup("AddLightPopup");
    if (ImGui::BeginPopup("AddLightPopup")) {
      if (ImGui::MenuItem("Directional")) addLight = 0;
      if (ImGui::MenuItem("Omni"))        addLight = 1;
      ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextColored(mutedText, "  |  ");
    ImGui::SameLine();

    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::Button("Clone", ImVec2(ToolbarButtonWidth("Clone", 70.0f), 0)))
      wantsClone = true;
    if (!hasSelection) ImGui::EndDisabled();

    ImGui::SameLine();

    // Group / Ungroup buttons
    if (!hasMultiSelect) ImGui::BeginDisabled();
    if (ImGui::Button("Group", ImVec2(ToolbarButtonWidth("Group", 72.0f), 0)))
      wantsGroup = true;
    if (!hasMultiSelect) ImGui::EndDisabled();

    ImGui::SameLine();
    if (!hasMultiSelect) ImGui::BeginDisabled();
    if (ImGui::Button("Ungroup", ImVec2(ToolbarButtonWidth("Ungroup", 88.0f), 0)))
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
  if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
    const float margin = 12.0f;
    const float width = (std::max)(560.0f, viewport->WorkSize.x - 480.0f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + viewport->WorkSize.y - 230.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(width, 218.0f), ImGuiCond_FirstUseEver);
  }
  if (!ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  ImGuiClampCurrentWindowToEditorWorkArea();

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

  if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 360.0f, viewport->WorkPos.y + 12.0f), ImGuiCond_FirstUseEver);
  }
  ImGui::SetNextWindowSize(ImVec2(348, 540), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Render Targets", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return selectedRT;
  }
  ImGuiClampCurrentWindowToEditorWorkArea();

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
  ImGuiViewport* mainViewport = ImGui::GetMainViewport();
  if (mainViewport) {
    // Platform viewports use OS/global coordinates; a (0,0) rect makes gizmos drift from the model.
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList(mainViewport));
    ImGuizmo::SetRect(mainViewport->Pos.x + (float)vpX,
                      mainViewport->Pos.y + (float)vpY,
                      (float)vpW,
                      (float)vpH);
  } else {
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect((float)vpX, (float)vpY, (float)vpW, (float)vpH);
  }
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
