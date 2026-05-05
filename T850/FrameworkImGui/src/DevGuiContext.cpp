#include <imgui/DevGuiContext.h>

#include <algorithm>

namespace t850 {

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
  const char* label = desc.label.empty() ? desc.name.c_str() : desc.label.c_str();
  float clamped = (std::max)(desc.min_val, (std::min)(desc.max_val, value));
  bool changed = ImGui::SliderFloat(label, &clamped, desc.min_val, desc.max_val, "%.3f");
  if (changed) value = clamped;
  return changed;
}

bool DevGuiContext::Checkbox(const CheckboxDesc& desc, bool& value) {
  const char* label = desc.label.empty() ? desc.name.c_str() : desc.label.c_str();
  return ImGui::Checkbox(label, &value);
}

bool DevGuiContext::Combo(const SelectorDesc& desc, int& selectedIndex, const std::vector<std::string>* overrideOptions) {
  const std::vector<std::string>& options = overrideOptions ? *overrideOptions : desc.options;
  if (options.empty()) return false;

  selectedIndex = (std::max)(0, (std::min)(selectedIndex, (int)options.size() - 1));
  const char* label = desc.label.empty() ? desc.name.c_str() : desc.label.c_str();
  bool changed = false;
  if (ImGui::BeginCombo(label, options[selectedIndex].c_str())) {
    for (int i = 0; i < (int)options.size(); ++i) {
      bool selected = (i == selectedIndex);
      if (ImGui::Selectable(options[i].c_str(), selected)) {
        selectedIndex = i;
        changed = true;
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