#include <imgui/DevGuiContext.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace t850 {

namespace {

std::string MakeImGuiLabel(const std::string& name, const std::string& label) {
  const std::string& visible = label.empty() ? name : label;
  if (name.empty() || visible.find("##") != std::string::npos) return visible;
  return visible + "##" + name;
}

std::string MakePanelLabel(const char* title, const std::string& suffix) {
  if (!title || suffix.empty()) return title ? title : "";
  const std::string original(title);
  const std::size_t idMarker = original.find("##");
  const std::string visible = idMarker == std::string::npos ? original : original.substr(0, idMarker);
  return visible + "##" + suffix + "/" + original;
}

} // namespace

bool DevGuiContext::BeginPanel(const char* title, bool* open, ImGuiWindowFlags flags) {
  if (m_embedPanels) {
    const std::string scopedTitle = MakePanelLabel(title, m_idSuffix);
    ImGui::PushID(scopedTitle.c_str());
    if (open && !*open) {
      m_embeddedPanelStack.push_back(false);
      return false;
    }
    const bool begun = ImGui::CollapsingHeader(title ? title : "", ImGuiTreeNodeFlags_DefaultOpen);
    if (begun) {
      ImGui::Indent();
    }
    m_embeddedPanelStack.push_back(begun);
    return begun;
  }
  const std::string scopedTitle = MakePanelLabel(title, m_idSuffix);
  if (m_windowClassId != 0) {
    ImGuiWindowClass windowClass{};
    windowClass.ClassId = m_windowClassId;
    windowClass.DockingAllowUnclassed = false;
    ImGui::SetNextWindowClass(&windowClass);
  }
  if (m_viewportId != 0) {
    ImGui::SetNextWindowViewport(m_viewportId);
  }
  if (m_dockId != 0) {
    ImGui::SetNextWindowDockID(m_dockId, ImGuiCond_FirstUseEver);
  }
  return ImGui::Begin(scopedTitle.c_str(), open, flags);
}

void DevGuiContext::EndPanel() {
  if (m_embedPanels) {
    const bool begun = !m_embeddedPanelStack.empty() && m_embeddedPanelStack.back();
    if (begun) {
      ImGui::Unindent();
    }
    if (!m_embeddedPanelStack.empty()) {
      m_embeddedPanelStack.pop_back();
    }
    ImGui::PopID();
    return;
  }
  ImGui::End();
}

void DevGuiContext::SetIdSuffix(std::string suffix) {
  m_idSuffix = std::move(suffix);
}

bool DevGuiContext::BeginSection(const char* label, bool defaultOpen) {
  ImGuiTreeNodeFlags flags = defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0;
  return ImGui::CollapsingHeader(label, flags);
}

void DevGuiContext::Separator() {
  ImGui::Separator();
}

void DevGuiContext::Text(const char* text) {
  ImGui::TextUnformatted(text);
}

bool DevGuiContext::Slider(const SliderDesc& desc, float& value) {
  const std::string label = MakeImGuiLabel(desc.name, desc.label);
  float clamped = (std::max)(desc.min_val, (std::min)(desc.max_val, value));
  const float range = desc.max_val - desc.min_val;
  const char* format = (std::abs(range) <= 0.001f || std::abs(desc.step) < 0.0001f) ? "%.7f" : "%.3f";
  bool changed = ImGui::SliderFloat(label.c_str(), &clamped, desc.min_val, desc.max_val, format);
  if (changed) value = clamped;
  return changed;
}

bool DevGuiContext::Checkbox(const CheckboxDesc& desc, bool& value) {
  const std::string label = MakeImGuiLabel(desc.name, desc.label);
  if (!desc.enabled) ImGui::BeginDisabled();
  bool changed = ImGui::Checkbox(label.c_str(), &value);
  if (!desc.enabled) ImGui::EndDisabled();
  return desc.enabled && changed;
}

bool DevGuiContext::Combo(const SelectorDesc& desc, int& selectedIndex, const std::vector<std::string>* overrideOptions) {
  const std::vector<std::string>& options = overrideOptions ? *overrideOptions : desc.options;
  if (options.empty()) return false;

  selectedIndex = (std::max)(0, (std::min)(selectedIndex, (int)options.size() - 1));
  const std::string label = MakeImGuiLabel(desc.name, desc.label);
  bool changed = false;
  if (ImGui::BeginCombo(label.c_str(), options[selectedIndex].c_str(), ImGuiComboFlags_HeightLarge)) {
#ifdef OS_ANDROID
    ImGuiIO& io = ImGui::GetIO();
    const float dragThreshold = io.MouseDragThreshold * io.MouseDragThreshold;
    const bool touchDragged = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] > dragThreshold;
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
      ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
    }
#endif
    for (int i = 0; i < (int)options.size(); ++i) {
      bool selected = (i == selectedIndex);
      ImGuiSelectableFlags selectableFlags = 0;
#ifdef OS_ANDROID
      selectableFlags |= ImGuiSelectableFlags_NoAutoClosePopups;
#endif
      if (ImGui::Selectable(options[i].c_str(), selected, selectableFlags)) {
#ifdef OS_ANDROID
        if (touchDragged) continue;
#endif
        selectedIndex = i;
        changed = true;
#ifdef OS_ANDROID
        ImGui::CloseCurrentPopup();
#endif
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

bool DevGuiContext::Button(const char* label, bool enabled) {
  if (!enabled) ImGui::BeginDisabled();
  bool clicked = ImGui::Button(label);
  if (!enabled) ImGui::EndDisabled();
  return enabled && clicked;
}

void DevGuiContext::DrawFrameStatsOverlay(const char* text) {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 pos(viewport->WorkPos.x + 10.0f, viewport->WorkPos.y + 10.0f);
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.35f);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("Frame Stats", nullptr, flags)) {
    ImGui::TextUnformatted(text ? text : "");
  }
  ImGui::End();
}

} // namespace t850
