#include <imgui/DevGuiContext.h>

#include <algorithm>
#include <string>

namespace t850 {

namespace {

std::string MakeImGuiLabel(const std::string& name, const std::string& label) {
  const std::string& visible = label.empty() ? name : label;
  if (name.empty() || visible.find("##") != std::string::npos) return visible;
  return visible + "##" + name;
}

} // namespace

bool DevGuiContext::BeginPanel(const char* title, bool* open, ImGuiWindowFlags flags) {
  return ImGui::Begin(title, open, flags);
}

void DevGuiContext::EndPanel() {
  ImGui::End();
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
  bool changed = ImGui::SliderFloat(label.c_str(), &clamped, desc.min_val, desc.max_val, "%.3f");
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
