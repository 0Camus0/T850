#pragma once

#include <scene/SceneDescriptor.h>

#include <imgui.h>

#include <string>
#include <vector>

namespace t850 {

  class DevGuiContext {
  public:
    bool BeginPanel(const char* title, bool* open = nullptr, ImGuiWindowFlags flags = 0);
    void EndPanel();

    bool BeginSection(const char* label, bool defaultOpen = true);
    void Separator();
    void Text(const char* text);

    bool Slider(const SliderDesc& desc, float& value);
    bool Checkbox(const CheckboxDesc& desc, bool& value);
    bool Combo(const SelectorDesc& desc, int& selectedIndex, const std::vector<std::string>* overrideOptions = nullptr);

    void DrawFrameStatsOverlay(const char* text);
  };

} // namespace t850