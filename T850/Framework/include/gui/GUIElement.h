#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <utils/xMaths.h>
#include <utils/GUIAtlasGenerator.h>  // AtlasRegion

namespace t850 {

  class TextRenderer;
  struct Quad;
  class ShaderBase;
  class ConstantBuffer;
  class Texture;

  // ─── Shared render resources passed to every element ───
  struct GUIDrawContext {
    TextRenderer*   text       = nullptr;
    Quad*           quad       = nullptr;
    ShaderBase*     shader     = nullptr;
    ConstantBuffer* cb         = nullptr;
    Texture*        barTex     = nullptr;
    Texture*        knobTex    = nullptr;
    Texture*        whiteTex   = nullptr;   // 1x1 white pixel for solid quads
    // Checkbox textures
    Texture*        checkBoxTex  = nullptr;
    Texture*        checkMarkTex = nullptr;
    // Selector textures
    Texture*        selectorBarTex       = nullptr;
    Texture*        selectorBtnLeftTex   = nullptr;
    Texture*        selectorBtnRightTex  = nullptr;
    Texture*        selectorBtnLeftPressTex  = nullptr;
    Texture*        selectorBtnRightPressTex = nullptr;
    // Line-edit popup textures
    Texture*        popupBgTex            = nullptr;
    Texture*        popupOkTex            = nullptr;
    Texture*        popupOkPressedTex     = nullptr;
    Texture*        popupCancelTex        = nullptr;
    Texture*        popupCancelPressedTex = nullptr;

    float           screenW    = 1280.0f;
    float           screenH    = 720.0f;
    bool            editMode   = false;
    bool            snapToGrid = false;
    float           gridCellW  = 40.0f;
    float           gridCellH  = 40.0f;

    // Control-shape layout tuning (shared across all instances of each control kind)
    float sliderKnobScaleX   = 1.0f;
    float sliderKnobScaleY   = 1.0f;
    float sliderKnobOffsetX  = 0.0f; // in bar-height units
    float sliderKnobOffsetY  = 0.0f; // in bar-height units

    float selectorLeftScaleX   = 1.0f;
    float selectorLeftScaleY   = 1.0f;
    float selectorLeftOffsetX  = 0.0f; // in selector-height units
    float selectorLeftOffsetY  = 0.0f; // in selector-height units

    float selectorRightScaleX  = 1.0f;
    float selectorRightScaleY  = 1.0f;
    float selectorRightOffsetX = 0.0f; // in selector-height units
    float selectorRightOffsetY = 0.0f; // in selector-height units

    float checkboxMarkScaleX   = 1.0f;
    float checkboxMarkScaleY   = 1.0f;
    float checkboxMarkOffsetX  = 0.0f; // in checkbox-height units
    float checkboxMarkOffsetY  = 0.0f; // in checkbox-height units

    // Line-edit popup layout (all scale/offset in popup-background-height units).
    float popupBgScaleX    = 1.0f;
    float popupBgScaleY    = 1.0f;
    float popupOkScaleX    = 1.0f;
    float popupOkScaleY    = 1.0f;
    float popupOkOffsetX   = 0.0f;
    float popupOkOffsetY   = 0.0f;
    float popupCancelScaleX  = 1.0f;
    float popupCancelScaleY  = 1.0f;
    float popupCancelOffsetX = 0.0f;
    float popupCancelOffsetY = 0.0f;
    float popupTextScaleX  = 1.0f;  // scale of popup text relative to bg height
    float popupTextScaleY  = 1.0f;

    // Helper: draw a solid‑colour quad (uses whiteTex + tint)
    void DrawSolidQuad(float px, float py, float w, float h,
                       const XVECTOR3& color, float alpha = 1.0f);
    // Helper: draw a textured quad with tint (full UV range)
    void DrawTexturedQuad(float px, float py, float w, float h,
                          Texture* tex, const XVECTOR3& tint);
    // Helper: draw a textured quad with tint using atlas sub-region UVs
    void DrawTexturedQuad(float px, float py, float w, float h,
                          Texture* tex, const XVECTOR3& tint,
                          const AtlasRegion& region);

    // ── Atlas regions (populated when atlas is loaded) ──
    AtlasRegion barRegion;
    AtlasRegion knobRegion;
    AtlasRegion checkBoxRegion;
    AtlasRegion checkMarkRegion;
    AtlasRegion selectorBarRegion;
    AtlasRegion selectorBtnLeftRegion;
    AtlasRegion selectorBtnRightRegion;
    AtlasRegion selectorBtnLeftPressRegion;
    AtlasRegion selectorBtnRightPressRegion;
    AtlasRegion popupBgRegion;
    AtlasRegion popupOkRegion;
    AtlasRegion popupOkPressedRegion;
    AtlasRegion popupCancelRegion;
    AtlasRegion popupCancelPressedRegion;

    // ── Source dimensions for aspect-ratio code (atlas sprites) ──
    float barSrcW = 256.0f, barSrcH = 32.0f;
    float selectorBarSrcW = 256.0f, selectorBarSrcH = 32.0f;
    float selectorBtnLeftSrcW = 32.0f;
    float selectorBtnRightSrcW = 32.0f;
    float checkBoxSrcW = 64.0f, checkBoxSrcH = 64.0f;
    float popupBgSrcW = 400.0f, popupBgSrcH = 160.0f;
    float popupOkSrcW = 100.0f, popupOkSrcH = 40.0f;
    float popupCancelSrcW = 100.0f, popupCancelSrcH = 40.0f;
  };

  // ─── Base class for every GUI widget ───
  class GUIElement {
  public:
    std::string id;              // unique id for serialization (e.g. "label_Exposure")
    float x     = 0.0f;         // screen pixels – top‑left
    float y     = 0.0f;
    float w     = 100.0f;       // size in screen pixels
    float h     = 25.0f;
    bool  visible = true;

    // ── Edit‑mode state ──
    bool  selected  = false;
    bool  dragging  = false;
    bool  resizing  = false;
    float dragOffX  = 0.0f;
    float dragOffY  = 0.0f;
    float resizeOrigW = 0.0f;
    float resizeOrigH = 0.0f;
    float resizeOrigMX = 0.0f;
    float resizeOrigMY = 0.0f;

    // ── Group‑edit‑mode state ──
    bool  groupHighlighted = false;

    static constexpr float kScaleHandleSize = 8.0f;

    virtual ~GUIElement() = default;

    // Core interface
    virtual void Draw(GUIDrawContext& ctx) = 0;
    virtual void OnResizeEnd() {}           // called after scale handle released

    // Edit overlay: bounding box + scale handle
    void DrawEditOverlay(GUIDrawContext& ctx);

    // Group‑edit highlight overlay
    void DrawGroupHighlight(GUIDrawContext& ctx);

    // Hit testing
    bool HitTest(float px, float py) const;
    bool HitTestScaleHandle(float px, float py) const;

    // Snap position to nearest grid intersection
    void SnapToGrid(float gridW, float gridH);
  };

  // ─── Text label element ───
  class GUILabel : public GUIElement {
  public:
    std::string text;
    XVECTOR3    color = XVECTOR3(0.9f, 0.85f, 0.8f);
    bool        isFPS = false;  // marks the FPS label (standalone, not part of a slider pair)

    void Draw(GUIDrawContext& ctx) override;
    void OnResizeEnd() override;

    // Recompute bounding box from actual text metrics
    void FitToText(GUIDrawContext& ctx);
  };

  // ─── Slider bar + knob as a single widget ───
  class GUISliderBar : public GUIElement {
  public:
    // Slider data
    std::string name;           // matches SliderDesc::name for linking
    std::string label;          // display label
    float minVal     = 0.0f;
    float maxVal     = 1.0f;
    float step       = 0.1f;
    float value      = 0.5f;
    float normValue  = 0.5f;
    int   settingIndex = -1;

    float knobSize   = 25.0f;   // knob square side
    float knobScaleX = 1.0f;
    float knobScaleY = 1.0f;
    float knobOffsetX = 0.0f; // in bar-height units
    float knobOffsetY = 0.0f; // in bar-height units

    // Optional calibrated travel range (knob CENTER as fraction of bar width).
    bool  knobRangeCalibrated = false;
    float knobMinNorm = 0.0f; // knob center at value=0 as fraction of bar width
    float knobMaxNorm = 1.0f; // knob center at value=1 as fraction of bar width

    // Interaction state (normal mode)
    bool  knobHover    = false;
    bool  knobDragging = false;
    float knobDragAnchorOff = 0.0f; // mouse offset from knob center at drag start

    void SetValue(float v);
    float GetKnobX() const;

    void Draw(GUIDrawContext& ctx) override;
    void UpdateInteraction(float mx, float my, bool mouseDown);
  };

  // ─── Checkbox: box + check overlay, toggle on click ───
  class GUICheckbox : public GUIElement {
  public:
    std::string name;           // matches CheckboxDesc::name
    std::string label;
    bool  checked      = false;
    int   settingIndex = -1;

    // Interaction state
    bool  wasMouseDown = false;
    bool  justToggled  = false;   // true on the frame a toggle happened

    float markScaleX = 1.0f;
    float markScaleY = 1.0f;
    float markOffsetX = 0.0f; // in checkbox-height units
    float markOffsetY = 0.0f; // in checkbox-height units

    void Draw(GUIDrawContext& ctx) override;
    void UpdateInteraction(float mx, float my, bool mouseDown);
  };

  // ─── Selector: bar + label + left/right arrow buttons ───
  class GUISelector : public GUIElement {
  public:
    std::string name;
    std::string label;
    std::vector<std::string> options;
    int   selectedIndex = 0;
    int   settingIndex  = -1;

    float btnSize       = 25.0f;  // square button side

    float leftScaleX = 1.0f;
    float leftScaleY = 1.0f;
    float leftOffsetX = 0.0f; // in selector-height units
    float leftOffsetY = 0.0f; // in selector-height units

    float rightScaleX = 1.0f;
    float rightScaleY = 1.0f;
    float rightOffsetX = 0.0f; // in selector-height units
    float rightOffsetY = 0.0f; // in selector-height units

    // Button interaction state
    bool  leftHover     = false;
    bool  rightHover    = false;
    bool  leftPressed   = false;
    bool  rightPressed  = false;
    bool  wasMouseDown  = false;
    bool  justChanged   = false;  // true on the frame index changed

    const std::string& CurrentOption() const;
    void SelectNext();
    void SelectPrev();

    void Draw(GUIDrawContext& ctx) override;
    void DrawQuadsOnly(GUIDrawContext& ctx);   // bar + buttons, no text
    void DrawTextBatched(GUIDrawContext& ctx);  // option text only (batch must be active)
    void UpdateInteraction(float mx, float my, bool mouseDown);
  };

  // ─── Simple textured button with pressed/non-pressed states ───
  class GUIButton {
  public:
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool  visible = false;
    bool  pressed = false;
    bool  justClicked = false;  // true on the frame of release-inside

    Texture* texNormal  = nullptr;
    Texture* texPressed = nullptr;

    void Draw(GUIDrawContext& ctx);
    void UpdateInteraction(float mx, float my, bool mouseDown);

  private:
    bool  wasMouseDown = false;
    bool  hover = false;
  };

} // namespace t850
