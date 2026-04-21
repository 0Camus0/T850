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

  enum class GUIControlEditTarget {
    SliderKnob,
    SelectorControl,
    CheckboxMark,
    LineEditControl
  };

  enum class GUIControlSubpart {
    Primary,
    SelectorLeft,
    SelectorRight,
    PopupOk,
    PopupCancel,
    PopupText
  };

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

  // A named group that controls element visibility and has its own layout
  struct GUIGroup {
    std::string name;
    std::vector<std::string> elementIds; // widget IDs; empty = all (Global)
    struct ElemLayout {
      std::string id;
      float x = 0, y = 0, w = 0, h = 0; // normalized
      std::string label;
    };
    std::vector<ElemLayout> layout; // per-group element positions (normalized)
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

    void Update(InputManager& input, int screenW, int screenH);
    void UpdateButtons(InputManager& input);
    void Draw();

    // Draw only the FPS label (for use when GUI overlay is hidden)
    void DrawFPSOnly();

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
    void SetEditMode(bool e);
    bool IsEditMode() const { return m_editMode; }
    void SetSnapToGrid(bool s) { m_snapToGrid = s; }
    bool IsSnapToGrid() const { return m_snapToGrid; }

    // Control-shape edit mode (edits internals like slider knob / selector buttons)
    void SetControlEditMode(bool e);
    bool IsControlEditMode() const { return m_controlEditMode; }
    bool SetControlEditTargetByName(const std::string& targetName);
    const char* GetControlEditTargetName() const;

    // Grid resize (+/-)
    void GrowGrid(float delta);
    void AdjustControlPreviewScale(float delta);

    // Apply the last-edited element's scale to all elements of the same kind
    void ApplyUniformScale();

    // Layout save/load
    bool SaveLayout(const std::string& path);
    bool LoadLayout(const std::string& path);
    bool SaveControlLayout(const std::string& path);
    bool LoadControlLayout(const std::string& path);
    bool HandleControlEditTab(const std::string& path);

    // Dirty flag for regular-flow label edits via popup; set on commit, cleared on save.
    bool IsLayoutDirty() const { return m_layoutDirty; }

    // Popup status (used by DevLayer/Framework to implement modality).
    bool IsPopupActive() const { return m_popupActive; }

    // All elements (for iteration)
    std::vector<GUIElement*>& GetElements() { return m_elements; }

    // Group system
    void EnterGroupEditMode();
    void ExitGroupEditMode();
    bool IsGroupEditMode() const { return m_groupEditMode; }
    void DeleteAllCustomGroups();
    void SwitchToGroup(int index);
    void SwitchToPrevGroup();
    void SwitchToNextGroup();
    void OpenGroupNamePopup();
    int  GetActiveGroupIndex() const { return m_activeGroupIndex; }
    int  GetGroupCount() const { return (int)m_groups.size(); }

  private:
    void InitTextures();
    void InitShader();
    bool TryLoadAtlas();  // attempt atlas-based texture init

    // Edit‑mode helpers
    void UpdateEditMode(float mx, float my, bool mouseDown);
    void UpdateControlEditMode(float mx, float my, bool mouseDown);
    void DrawGrid();
    void DrawEditOverlays();
    void DrawControlEditPreview();
    void DrawControlEditOverlay();
    void DrawControlEditRectOverlay(float x, float y, float w, float h, const XVECTOR3& color, bool drawHandle);
    void RebakeFontIfNeeded();
    void ApplyControlLayoutToElements();

    // Popup helpers
    void OpenPopupFor(GUILabel* label);
    void ClosePopupAndCommit(bool commit);
    void DrawPopup();
    void UpdatePopup(InputManager& input, float mx, float my, bool mouseDown);
    void GetPopupRects(float& bgX, float& bgY, float& bgW, float& bgH,
                       float& okX, float& okY, float& okW, float& okH,
                       float& cancelX, float& cancelY, float& cancelW, float& cancelH) const;

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

    // Line-edit popup textures
    Texture* m_popupBgTexture            = nullptr;
    Texture* m_popupOkTexture            = nullptr;
    Texture* m_popupOkPressedTexture     = nullptr;
    Texture* m_popupCancelTexture        = nullptr;
    Texture* m_popupCancelPressedTexture = nullptr;

    // GUI / Back button textures
    Texture* m_guiBtnNormalTex  = nullptr;
    Texture* m_guiBtnPressedTex = nullptr;
    Texture* m_backBtnNormalTex  = nullptr;
    Texture* m_backBtnPressedTex = nullptr;

    // ── Atlas system ──
    Texture* m_atlasTexture = nullptr;
    bool     m_useAtlas     = false;
    // Per-sprite atlas regions (valid when m_useAtlas == true)
    AtlasRegion m_atlasBarRegion;
    AtlasRegion m_atlasKnobRegion;
    AtlasRegion m_atlasCheckBoxRegion;
    AtlasRegion m_atlasCheckMarkRegion;
    AtlasRegion m_atlasSelectorBarRegion;
    AtlasRegion m_atlasSelectorBtnLeftRegion;
    AtlasRegion m_atlasSelectorBtnRightRegion;
    AtlasRegion m_atlasSelectorBtnLeftPressRegion;
    AtlasRegion m_atlasSelectorBtnRightPressRegion;
    AtlasRegion m_atlasPopupBgRegion;
    AtlasRegion m_atlasPopupOkRegion;
    AtlasRegion m_atlasPopupOkPressedRegion;
    AtlasRegion m_atlasPopupCancelRegion;
    AtlasRegion m_atlasPopupCancelPressedRegion;
    // Source dims for aspect-ratio code
    float m_atlasBarSrcW = 256.0f, m_atlasBarSrcH = 32.0f;
    float m_atlasSelectorBarSrcW = 256.0f, m_atlasSelectorBarSrcH = 32.0f;
    float m_atlasSelectorBtnLeftSrcW = 32.0f;
    float m_atlasSelectorBtnRightSrcW = 32.0f;
    float m_atlasCheckBoxSrcW = 64.0f, m_atlasCheckBoxSrcH = 64.0f;
    float m_atlasPopupBgSrcW = 400.0f, m_atlasPopupBgSrcH = 160.0f;
    float m_atlasPopupOkSrcW = 100.0f, m_atlasPopupOkSrcH = 40.0f;
    float m_atlasPopupCancelSrcW = 100.0f, m_atlasPopupCancelSrcH = 40.0f;

    Quad          m_quad;
    ShaderBase*   m_shader = nullptr;
    ConstantBuffer* m_CB   = nullptr;

    bool m_visible      = false;
    bool m_initialized  = false;
    bool m_editMode     = false;
    bool m_snapToGrid   = false;
    bool m_controlEditMode = false;
    float m_gridCellW   = 40.0f;
    float m_gridCellH   = 40.0f;

    GUIControlEditTarget m_controlEditTarget = GUIControlEditTarget::SliderKnob;
    GUIControlSubpart m_controlActiveSubpart = GUIControlSubpart::Primary;
    float m_controlPreviewVisualScale = 0.65f; // Visual-only zoom for control edit preview.

    struct ControlLayoutState {
      float sliderKnobScaleX = 1.0f;
      float sliderKnobScaleY = 1.0f;
      float sliderKnobOffsetX = 0.0f;
      float sliderKnobOffsetY = 0.0f;
      bool  sliderKnobRangeCalibrated = false;
      float sliderKnobMinNorm = 0.0f;
      float sliderKnobMaxNorm = 1.0f;

      float selectorLeftScaleX = 1.0f;
      float selectorLeftScaleY = 1.0f;
      float selectorLeftOffsetX = 0.0f;
      float selectorLeftOffsetY = 0.0f;

      float selectorRightScaleX = 1.0f;
      float selectorRightScaleY = 1.0f;
      float selectorRightOffsetX = 0.0f;
      float selectorRightOffsetY = 0.0f;

      float checkboxMarkScaleX = 1.0f;
      float checkboxMarkScaleY = 1.0f;
      float checkboxMarkOffsetX = 0.0f;
      float checkboxMarkOffsetY = 0.0f;

      // Line-edit popup
      float popupBgScaleX    = 1.0f;
      float popupBgScaleY    = 1.0f;
      float popupOkScaleX    = 1.0f;
      float popupOkScaleY    = 1.0f;
      float popupOkOffsetX   = -0.30f; // relative to bg-height, from bg center-X
      float popupOkOffsetY   = 0.35f;  // relative to bg-height, from bg center-Y
      float popupCancelScaleX  = 1.0f;
      float popupCancelScaleY  = 1.0f;
      float popupCancelOffsetX = 0.30f;
      float popupCancelOffsetY = 0.35f;
      float popupTextScaleX  = 0.6f;   // text height as fraction of bg height
      float popupTextScaleY  = 0.6f;
    };

    ControlLayoutState m_controlLayout;

    struct ControlEditRect {
      float x = 0.0f;
      float y = 0.0f;
      float w = 10.0f;
      float h = 10.0f;
      bool valid = false;
    } m_controlEditRect;

    ControlEditRect m_controlEditRectSecondary;

    bool  m_controlDragActive = false;
    bool  m_controlResizeActive = false;
    float m_controlDragOffX = 0.0f;
    float m_controlDragOffY = 0.0f;
    float m_controlResizeOrigW = 0.0f;
    float m_controlResizeOrigH = 0.0f;
    float m_controlResizeOrigMX = 0.0f;
    float m_controlResizeOrigMY = 0.0f;
    float m_controlResizeOrigX = 0.0f;
    float m_controlResizeOrigY = 0.0f;

    int   m_sliderCalibrationStep = 0; // 0: waiting min, 1: waiting max

    // ── Line-edit popup state ──
    bool        m_popupActive = false;
    GUILabel*   m_popupTargetLabel = nullptr;  // label element being edited
    GUISliderBar*  m_popupTargetSlider   = nullptr; // if non-null, label belongs to this slider; edit slider->label
    GUICheckbox*   m_popupTargetCheckbox = nullptr; // if non-null, edit checkbox->label (mirrored into label->text)
    GUISelector*   m_popupTargetSelector = nullptr; // if non-null, edit selector->label
    std::string m_popupText;       // current edit buffer
    int         m_popupCaret = 0;  // caret position (char index in utf-8 bytes)
    float       m_popupBlink = 0.0f;
    bool        m_popupWasMouseDown = false;
    bool        m_popupOkPressed = false;
    bool        m_popupCancelPressed = false;
    // Double-click detection (on labels)
    uint64_t    m_lastClickTimeMs = 0;
    GUIElement* m_lastClickElement = nullptr;
    float       m_lastClickX = 0.0f;
    float       m_lastClickY = 0.0f;

    // Edit‑mode tracking
    GUIElement* m_dragTarget   = nullptr;
    GUIElement* m_resizeTarget = nullptr;
    GUIElement* m_lastEdited   = nullptr;  // last element that was dragged/resized
    GUILabel*   m_fpsLabel     = nullptr;  // FPS label element (owned via m_elements)
    bool m_wasMouseDown = false;
    bool m_layoutDirty  = false;           // Set when popup commits a label change in regular flow

    // ── Group system ──
    GUISelector  m_groupSelector;
    GUILabel     m_groupSelectorLabel;
    std::vector<GUIGroup> m_groups;         // index 0 = "Global"
    int  m_activeGroupIndex = 0;
    bool m_groupEditMode    = false;
    bool m_popupForGroupName = false;
    std::vector<std::string> m_groupEditSelectedIds;

    void UpdateGroupEditMode(InputManager& input, float mx, float my, bool mouseDown);
    void DrawGroupEditHighlights();
    void DrawGroupSelector();
    void ApplyGroupVisibility();
    void CaptureGroupLayout(int groupIndex);
    void ApplyGroupLayout(int groupIndex);
    void CreateGroupFromSelection(const std::string& name);
    std::string FindPairedWidgetId(GUIElement* element) const;
    void ClearGroupHighlights();
    void SyncFPSToGlobalLayout();

    // GUI / Back buttons (standalone, not in m_elements)
    GUIButton m_guiButton;
    GUIButton m_backButton;
  };

} // namespace t800
