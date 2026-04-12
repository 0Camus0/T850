#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <utils/xMaths.h>

namespace t800 {

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
    float           screenW    = 1280.0f;
    float           screenH    = 720.0f;
    bool            editMode   = false;
    bool            snapToGrid = false;
    float           gridCellW  = 40.0f;
    float           gridCellH  = 40.0f;

    // Helper: draw a solid‑colour quad (uses whiteTex + tint)
    void DrawSolidQuad(float px, float py, float w, float h,
                       const XVECTOR3& color, float alpha = 1.0f);
    // Helper: draw a textured quad with tint
    void DrawTexturedQuad(float px, float py, float w, float h,
                          Texture* tex, const XVECTOR3& tint);
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

    static constexpr float kScaleHandleSize = 8.0f;

    virtual ~GUIElement() = default;

    // Core interface
    virtual void Draw(GUIDrawContext& ctx) = 0;
    virtual void OnResizeEnd() {}           // called after scale handle released

    // Edit overlay: bounding box + scale handle
    void DrawEditOverlay(GUIDrawContext& ctx);

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

    // Interaction state (normal mode)
    bool  knobHover    = false;
    bool  knobDragging = false;

    void SetValue(float v);
    float GetKnobX() const;

    void Draw(GUIDrawContext& ctx) override;
    void UpdateInteraction(float mx, float my, bool mouseDown);
  };

} // namespace t800
