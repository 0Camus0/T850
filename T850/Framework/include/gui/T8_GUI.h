#pragma once
#include <video/BaseDriver.h>
#include <scene/T8_Quad.h>
#include <scene/T8_TextRenderer.h>
#include <scene/SceneDescriptor.h>
#include <gui/GUIElement.h>
#include <utils/InputManager.h>
#include <string>
#include <vector>
#include <memory>

namespace t800 {

  // ─── Responsive layout computed from screen dimensions ───
  struct GUILayout {
    float scale       = 1.0f;
    float sliderW     = 100.0f;
    float sliderH     = 25.0f;
    float knobSize    = 25.0f;
    float spacingY    = 40.0f;
    float marginRight = 20.0f;
    float marginTop   = 10.0f;
    float labelGap    = 8.0f;
    float valueGap    = 8.0f;
    float fontSize    = 16.0f;
    float textureSize = 1024.0f;

    void Compute(int screenW, int screenH);
  };

  // Pairs a label element with its slider bar element
  struct GUISliderPair {
    GUILabel*     label   = nullptr;  // owned via m_elements
    GUISliderBar* slider  = nullptr;  // owned via m_elements
  };

  // Pairs a label element with its checkbox element
  struct GUICheckboxPair {
    GUILabel*    label    = nullptr;
    GUICheckbox* checkbox = nullptr;
  };

  // Pairs a label element with its selector element
  struct GUISelectorPair {
    GUILabel*    label    = nullptr;
    GUISelector* selector = nullptr;
  };

  class GUIManager {
  public:
    void Init(int screenW, int screenH);
    void Destroy();

    void AddSlider(const SliderDesc& desc, int settingIndex);
    void AddCheckbox(const CheckboxDesc& desc, int settingIndex);
    void AddSelector(const SelectorDesc& desc, int settingIndex);
    void AddFPSLabel();
    void SetFPSText(const std::string& text, const XVECTOR3& color);
    void ClearSliders();

    void Update(const InputManager& input, int screenW, int screenH);
    void Draw();

    void LayoutSliders(int screenW, int screenH);

    // Access slider pairs for scene sync
    std::vector<GUISliderPair>& GetSliderPairs() { return m_sliderPairs; }
    GUISliderBar* FindSlider(const std::string& name);

    // Access checkbox pairs for scene sync
    std::vector<GUICheckboxPair>& GetCheckboxPairs() { return m_checkboxPairs; }

    // Access selector pairs for scene sync
    std::vector<GUISelectorPair>& GetSelectorPairs() { return m_selectorPairs; }

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool v) { m_visible = v; }
    void ToggleVisible() { m_visible = !m_visible; }

    // Edit mode
    void SetEditMode(bool e) { m_editMode = e; }
    bool IsEditMode() const { return m_editMode; }
    void SetSnapToGrid(bool s) { m_snapToGrid = s; }
    bool IsSnapToGrid() const { return m_snapToGrid; }

    // Grid resize (+/-)
    void GrowGrid(float delta);

    // Apply the last-edited element's scale to all elements of the same kind
    void ApplyUniformScale();

    // Layout save/load
    bool SaveLayout(const std::string& path);
    bool LoadLayout(const std::string& path);

    // All elements (for iteration)
    std::vector<GUIElement*>& GetElements() { return m_elements; }

  private:
    void InitTextures();
    void InitShader();

    // Edit‑mode helpers
    void UpdateEditMode(float mx, float my, bool mouseDown);
    void DrawGrid();
    void DrawEditOverlays();
    void RebakeFontIfNeeded();

    // ── State ──
    std::vector<GUIElement*>       m_elements;      // owns all elements
    std::vector<GUISliderPair>     m_sliderPairs;   // indexed links
    std::vector<GUICheckboxPair>   m_checkboxPairs;
    std::vector<GUISelectorPair>   m_selectorPairs;

    TextRenderer  m_textRenderer;
    GUILayout     m_layout;
    GUIDrawContext m_ctx;                          // rebuilt each frame

    Texture* m_barTexture   = nullptr;
    Texture* m_knobTexture  = nullptr;
    Texture* m_whiteTexture = nullptr;

    // Checkbox textures
    Texture* m_checkBoxTexture  = nullptr;
    Texture* m_checkMarkTexture = nullptr;

    // Selector textures
    Texture* m_selectorBarTexture       = nullptr;
    Texture* m_selectorBtnLeftTexture   = nullptr;
    Texture* m_selectorBtnRightTexture  = nullptr;
    Texture* m_selectorBtnLeftPressTexture  = nullptr;
    Texture* m_selectorBtnRightPressTexture = nullptr;

    Quad          m_quad;
    ShaderBase*   m_shader = nullptr;
    ConstantBuffer* m_CB   = nullptr;

    bool m_visible      = false;
    bool m_initialized  = false;
    bool m_editMode     = false;
    bool m_snapToGrid   = false;
    float m_gridCellW   = 40.0f;
    float m_gridCellH   = 40.0f;

    // Edit‑mode tracking
    GUIElement* m_dragTarget   = nullptr;
    GUIElement* m_resizeTarget = nullptr;
    GUIElement* m_lastEdited   = nullptr;  // last element that was dragged/resized
    GUILabel*   m_fpsLabel     = nullptr;  // FPS label element (owned via m_elements)
    bool m_wasMouseDown = false;
  };

} // namespace t800
