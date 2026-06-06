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
    void SetIdSuffix(std::string suffix);
    void SetViewportId(ImGuiID viewportId) { m_viewportId = viewportId; }
    void SetDockId(ImGuiID dockId) { m_dockId = dockId; }
    void SetWindowClassId(ImGuiID classId) { m_windowClassId = classId; }
    void SetEmbedPanels(bool embed) { m_embedPanels = embed; }
    bool EmbedPanels() const { return m_embedPanels; }

    bool BeginSection(const char* label, bool defaultOpen = true);
    void Separator();
    void Text(const char* text);

    bool Slider(const SliderDesc& desc, float& value);
    bool Checkbox(const CheckboxDesc& desc, bool& value);
    bool Combo(const SelectorDesc& desc, int& selectedIndex, const std::vector<std::string>* overrideOptions = nullptr);
    bool Button(const char* label, bool enabled = true);

    void DrawFrameStatsOverlay(const char* text);

  private:
    std::string m_idSuffix;
    ImGuiID m_viewportId = 0;
    ImGuiID m_dockId = 0;
    ImGuiID m_windowClassId = 0;
    bool m_embedPanels = false;
    std::vector<bool> m_embeddedPanelStack;
  };

} // namespace t850