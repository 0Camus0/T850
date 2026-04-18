#include "gui/GUIElement.h"
#include <scene/T8_TextRenderer.h>
#include <scene/T8_Quad.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace t800 {

extern Device*        T8Device;
extern DeviceContext* T8DeviceContext;

// ═══════════════════════════════════════════════════════════
//  GUIDrawContext helpers
// ═══════════════════════════════════════════════════════════

void GUIDrawContext::DrawSolidQuad(float px, float py, float w, float h,
                                    const XVECTOR3& color, float alpha) {
  if (!whiteTex || !quad || !shader || !cb) return;
  DrawTexturedQuad(px, py, w, h, whiteTex, color);
}

void GUIDrawContext::DrawTexturedQuad(float px, float py, float w, float h,
                                      Texture* tex, const XVECTOR3& tint) {
  if (!tex || !quad || !shader || !cb) return;

  XVECTOR3 t = tint;
  cb->UpdateFromBuffer(*T8DeviceContext, &t.x);
  cb->Set(*T8DeviceContext);

  float x0 = (px / screenW) * 2.0f - 1.0f;
  float y0 = 1.0f - ((py + h) / screenH) * 2.0f;
  float x1 = ((px + w) / screenW) * 2.0f - 1.0f;
  float y1 = 1.0f - (py / screenH) * 2.0f;

  // GL samples textures with v=0 at the bottom row of pixel data, while the
  // texture loader uploads image data top-first (row 0 == top of image).
  // Under D3D11, texel (0,0) is top-left so the UVs below render correctly.
  // Under GL, we flip V so the top of the quad samples the top of the image
  // (otherwise asymmetric GUI textures — e.g. the checkmark arrow — appear
  // upside down). This affects every GUI quad drawn through this helper.
  const bool gl = (g_pBaseDriver && g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL);
  const float vTop = gl ? 1.0f : 0.0f;
  const float vBot = gl ? 0.0f : 1.0f;

  Quad::Vertex verts[4] = {
    {x0, y1, 0.0f, 1.0f, 0.0f, vTop},
    {x0, y0, 0.0f, 1.0f, 0.0f, vBot},
    {x1, y0, 0.0f, 1.0f, 1.0f, vBot},
    {x1, y1, 0.0f, 1.0f, 1.0f, vTop},
  };
  quad->m_VB->UpdateFromBuffer(*T8DeviceContext, verts);
  tex->Set(*T8DeviceContext, 0, "tex0");
  T8DeviceContext->DrawIndexed(6, 0, 0);
}

// ═══════════════════════════════════════════════════════════
//  GUIElement (base)
// ═══════════════════════════════════════════════════════════

bool GUIElement::HitTest(float px, float py) const {
  return visible && px >= x && px <= x + w && py >= y && py <= y + h;
}

bool GUIElement::HitTestScaleHandle(float px, float py) const {
  float hx = x + w - kScaleHandleSize;
  float hy = y + h - kScaleHandleSize;
  return px >= hx && px <= hx + kScaleHandleSize &&
         py >= hy && py <= hy + kScaleHandleSize;
}

void GUIElement::SnapToGrid(float gridW, float gridH) {
  if (gridW > 0.0f) x = std::round(x / gridW) * gridW;
  if (gridH > 0.0f) y = std::round(y / gridH) * gridH;
}

void GUIElement::DrawEditOverlay(GUIDrawContext& ctx) {
  if (!visible) return;
  float lineW = 1.0f;
  XVECTOR3 bboxColor = selected ? XVECTOR3(1.0f, 1.0f, 0.0f) : XVECTOR3(1.0f, 0.0f, 0.0f);

  // Top edge
  ctx.DrawSolidQuad(x, y, w, lineW, bboxColor);
  // Bottom edge
  ctx.DrawSolidQuad(x, y + h - lineW, w, lineW, bboxColor);
  // Left edge
  ctx.DrawSolidQuad(x, y, lineW, h, bboxColor);
  // Right edge
  ctx.DrawSolidQuad(x + w - lineW, y, lineW, h, bboxColor);

  // Scale handle: small filled square in bottom‑right corner
  float hs = kScaleHandleSize;
  XVECTOR3 handleColor(0.0f, 1.0f, 0.0f);
  ctx.DrawSolidQuad(x + w - hs, y + h - hs, hs, hs, handleColor);
}

void GUIElement::DrawGroupHighlight(GUIDrawContext& ctx) {
  if (!visible || !groupHighlighted) return;
  float lineW = 2.0f;
  XVECTOR3 hlColor(0.0f, 0.7f, 1.0f);
  ctx.DrawSolidQuad(x, y, w, lineW, hlColor);
  ctx.DrawSolidQuad(x, y + h - lineW, w, lineW, hlColor);
  ctx.DrawSolidQuad(x, y, lineW, h, hlColor);
  ctx.DrawSolidQuad(x + w - lineW, y, lineW, h, hlColor);
}

// ═══════════════════════════════════════════════════════════
//  GUILabel
// ═══════════════════════════════════════════════════════════

void GUILabel::Draw(GUIDrawContext& ctx) {
  if (!visible || !ctx.text) return;

  int sw = (int)ctx.screenW;
  int sh = (int)ctx.screenH;

  // Uniform scale: element height vs natural text height
  float charH = ctx.text->m_fontSize * ctx.screenH / (float)ctx.text->m_textureSize;
  float scale = (charH > 0.0f && h > 0.0f) ? h / charH : 1.0f;

  // Keep bbox width in sync with scaled text
  float natW = ctx.text->MeasurePixel(text, sw, sh);
  w = natW * scale;

  if (ctx.text->m_batchActive)
    ctx.text->DrawPixelScaledBatched(x, y, scale, scale, sw, sh, color, text);
  else
    ctx.text->DrawPixelScaled(x, y, scale, scale, sw, sh, color, text);
}

void GUILabel::OnResizeEnd() {
  // Could re-bake font in the future. For now, nothing needed.
}

void GUILabel::FitToText(GUIDrawContext& ctx) {
  if (!ctx.text) return;
  int sw = (int)ctx.screenW;
  int sh = (int)ctx.screenH;
  float tw = ctx.text->MeasurePixel(text, sw, sh);
  float fontSize = ctx.text->m_fontSize;
  float texSize  = (float)ctx.text->m_textureSize;
  float charH    = fontSize * ctx.screenH / texSize;
  w = tw;
  h = charH;
}

// ═══════════════════════════════════════════════════════════
//  GUISliderBar
// ═══════════════════════════════════════════════════════════

void GUISliderBar::SetValue(float v) {
  float clamped = (std::max)(minVal, (std::min)(maxVal, v));
  if (step > 0.0f) {
    float steps = std::round((clamped - minVal) / step);
    clamped = minVal + steps * step;
    clamped = (std::min)(clamped, maxVal);
  }
  value = clamped;
  float range = maxVal - minVal;
  normValue = (range > 0.0f) ? (value - minVal) / range : 0.0f;
}

float GUISliderBar::GetKnobX() const {
  // Knob size matches bar height; constrain center within bar bounds
  float kSize = h;
  float range = (std::max)(0.0f, w - kSize);
  return x + kSize * 0.5f + normValue * range;
}

void GUISliderBar::Draw(GUIDrawContext& ctx) {
  if (!visible) return;

  // Bar
  XVECTOR3 barTint(1.0f, 1.0f, 1.0f);
  ctx.DrawTexturedQuad(x, y, w, h, ctx.barTex, barTint);

  // Knob: tunable offset/scale relative to bar height.
  float baseW = h;
  float baseH = h;
  float baseY = y;
  float kW = (std::max)(4.0f, baseW * knobScaleX);
  float kH = (std::max)(4.0f, baseH * knobScaleY);
  float kx = 0.0f;
  if (knobRangeCalibrated) {
    // min/max are knob CENTER as fraction of bar width.
    float t = (std::max)(0.0f, (std::min)(1.0f, normValue));
    float centerNorm = knobMinNorm + (knobMaxNorm - knobMinNorm) * t;
    kx = x + centerNorm * w - kW * 0.5f;
  } else {
    float baseX = x + normValue * (std::max)(0.0f, w - baseW);
    kx = baseX + knobOffsetX * h + (baseW - kW) * 0.5f;
  }
  float ky = baseY + knobOffsetY * h + (baseH - kH) * 0.5f;
  XVECTOR3 knobTint = (knobHover || knobDragging)
    ? XVECTOR3(1.5f, 1.5f, 1.5f)
    : XVECTOR3(1.0f, 1.0f, 1.0f);
  ctx.DrawTexturedQuad(kx, ky, kW, kH, ctx.knobTex, knobTint);
}

void GUISliderBar::UpdateInteraction(float mx, float my, bool mouseDown) {
  float baseW = h;
  float baseH = h;
  float baseY = y;
  float kW = (std::max)(4.0f, baseW * knobScaleX);
  float kH = (std::max)(4.0f, baseH * knobScaleY);
  float kx = 0.0f;
  if (knobRangeCalibrated) {
    float t = (std::max)(0.0f, (std::min)(1.0f, normValue));
    float centerNorm = knobMinNorm + (knobMaxNorm - knobMinNorm) * t;
    kx = x + centerNorm * w - kW * 0.5f;
  } else {
    float baseX = x + normValue * (std::max)(0.0f, w - baseW);
    kx = baseX + knobOffsetX * h + (baseW - kW) * 0.5f;
  }
  float ky = baseY + knobOffsetY * h + (baseH - kH) * 0.5f;
  float knobCenterX = kx + kW * 0.5f;

  knobHover = (mx >= kx && mx <= kx + kW &&
               my >= ky && my <= ky + kH);

  bool barHover = (mx >= x && mx <= x + w && my >= y && my <= y + h);

  if (mouseDown && !knobDragging && (knobHover || barHover)) {
    knobDragging = true;
    if (knobHover) {
      // Clicked on knob: remember offset so it doesn't jump
      knobDragAnchorOff = mx - knobCenterX;
    } else {
      // Clicked on bar: snap knob center to mouse
      knobDragAnchorOff = 0.0f;
    }
  }
  if (knobDragging) {
    if (mouseDown) {
      float desiredCenter = mx - knobDragAnchorOff;
      float norm = 0.0f;
      if (knobRangeCalibrated) {
        float minCX = x + knobMinNorm * w;
        float maxCX = x + knobMaxNorm * w;
        float span = maxCX - minCX;
        norm = (std::abs(span) > 0.0001f) ? (desiredCenter - minCX) / span : 0.0f;
      } else {
        float range = (std::max)(0.0f, w - baseW);
        float kHalf = baseW * 0.5f;
        norm = (range > 0.0f) ? (desiredCenter - x - kHalf) / range : 0.0f;
      }
      norm = (std::max)(0.0f, (std::min)(1.0f, norm));
      SetValue(minVal + norm * (maxVal - minVal));
    } else {
      knobDragging = false;
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  GUICheckbox
// ═══════════════════════════════════════════════════════════

void GUICheckbox::Draw(GUIDrawContext& ctx) {
  if (!visible) return;

  // Draw box texture (always)
  XVECTOR3 tint(1.0f, 1.0f, 1.0f);
  if (ctx.checkBoxTex)
    ctx.DrawTexturedQuad(x, y, h, h, ctx.checkBoxTex, tint);

  // Draw check mark overlay when checked
  if (checked && ctx.checkMarkTex) {
    float baseW = h;
    float baseH = h;
    float markW = (std::max)(4.0f, baseW * markScaleX);
    float markH = (std::max)(4.0f, baseH * markScaleY);
    float markX = x + markOffsetX * h + (baseW - markW) * 0.5f;
    float markY = y + markOffsetY * h + (baseH - markH) * 0.5f;
    ctx.DrawTexturedQuad(markX, markY, markW, markH, ctx.checkMarkTex, tint);
  }
}

void GUICheckbox::UpdateInteraction(float mx, float my, bool mouseDown) {
  justToggled = false;
  // Toggle on click (press-then-release inside the box area)
  bool over = (mx >= x && mx <= x + h && my >= y && my <= y + h);
  if (!mouseDown && wasMouseDown && over) {
    checked = !checked;
    justToggled = true;
  }
  wasMouseDown = mouseDown;
}

// ═══════════════════════════════════════════════════════════
//  GUISelector
// ═══════════════════════════════════════════════════════════

static const std::string s_emptyOption = "---";

const std::string& GUISelector::CurrentOption() const {
  if (options.empty()) return s_emptyOption;
  return options[selectedIndex];
}

void GUISelector::SelectNext() {
  if (options.empty()) return;
  selectedIndex = (selectedIndex + 1) % (int)options.size();
  justChanged = true;
}

void GUISelector::SelectPrev() {
  if (options.empty()) return;
  selectedIndex = (selectedIndex - 1 + (int)options.size()) % (int)options.size();
  justChanged = true;
}

void GUISelector::Draw(GUIDrawContext& ctx) {
  DrawQuadsOnly(ctx);
  // Draw current option text centered on bar (bar = full element bounds)
  if (ctx.text) {
    int sw = (int)ctx.screenW;
    int sh = (int)ctx.screenH;
    float charH = ctx.text->m_fontSize * ctx.screenH / (float)ctx.text->m_textureSize;
    float scale = (charH > 0.0f && h > 0.0f) ? h / charH : 1.0f;

    const std::string& opt = CurrentOption();
    float textW = ctx.text->MeasurePixel(opt, sw, sh) * scale;
    float textX = x + (w - textW) * 0.5f;
    float textY = y;
    XVECTOR3 textColor(0.9f, 0.85f, 0.8f);
    ctx.text->DrawPixelScaled(textX, textY, scale, scale, sw, sh, textColor, opt);
  }
}

void GUISelector::DrawQuadsOnly(GUIDrawContext& ctx) {
  if (!visible) return;

  XVECTOR3 tint(1.0f, 1.0f, 1.0f);

  // Bar drawn at full element bounds
  if (ctx.selectorBarTex)
    ctx.DrawTexturedQuad(x, y, w, h, ctx.selectorBarTex, tint);

  // Left button: base at element left edge
  float leftW = (std::max)(4.0f, btnSize * leftScaleX);
  float leftH = (std::max)(4.0f, h * leftScaleY);
  float leftX = x + leftOffsetX * h + (btnSize - leftW) * 0.5f;
  float leftY = y + leftOffsetY * h + (h - leftH) * 0.5f;

  // Right button: base at element right edge
  float rightW = (std::max)(4.0f, btnSize * rightScaleX);
  float rightH = (std::max)(4.0f, h * rightScaleY);
  float rightX = (x + w - btnSize) + rightOffsetX * h + (btnSize - rightW) * 0.5f;
  float rightY = y + rightOffsetY * h + (h - rightH) * 0.5f;

  // Left button
  Texture* leftTex = leftPressed ? ctx.selectorBtnLeftPressTex : ctx.selectorBtnLeftTex;
  if (leftTex)
    ctx.DrawTexturedQuad(leftX, leftY, leftW, leftH, leftTex, tint);

  // Right button
  Texture* rightTex = rightPressed ? ctx.selectorBtnRightPressTex : ctx.selectorBtnRightTex;
  if (rightTex)
    ctx.DrawTexturedQuad(rightX, rightY, rightW, rightH, rightTex, tint);
}

void GUISelector::DrawTextBatched(GUIDrawContext& ctx) {
  if (!visible || !ctx.text) return;

  int sw = (int)ctx.screenW;
  int sh = (int)ctx.screenH;
  float charH = ctx.text->m_fontSize * ctx.screenH / (float)ctx.text->m_textureSize;
  float scale = (charH > 0.0f && h > 0.0f) ? h / charH : 1.0f;

  // Text centered on bar (bar = full element bounds)
  const std::string& opt = CurrentOption();
  float textW = ctx.text->MeasurePixel(opt, sw, sh) * scale;
  float textX = x + (w - textW) * 0.5f;
  float textY = y;
  XVECTOR3 textColor(0.9f, 0.85f, 0.8f);
  ctx.text->DrawPixelScaledBatched(textX, textY, scale, scale, sw, sh, textColor, opt);
}

void GUISelector::UpdateInteraction(float mx, float my, bool mouseDown) {
  justChanged = false;

  // Hit test against tuned button rectangles.
  float leftW = (std::max)(4.0f, btnSize * leftScaleX);
  float leftH = (std::max)(4.0f, h * leftScaleY);
  float leftX = x + leftOffsetX * h + (btnSize - leftW) * 0.5f;
  float leftY = y + leftOffsetY * h + (h - leftH) * 0.5f;

  float rightW = (std::max)(4.0f, btnSize * rightScaleX);
  float rightH = (std::max)(4.0f, h * rightScaleY);
  float rightX = (x + w - btnSize) + rightOffsetX * h + (btnSize - rightW) * 0.5f;
  float rightY = y + rightOffsetY * h + (h - rightH) * 0.5f;

  leftHover  = (mx >= leftX && mx <= leftX + leftW && my >= leftY && my <= leftY + leftH);
  rightHover = (mx >= rightX && mx <= rightX + rightW && my >= rightY && my <= rightY + rightH);

  bool justPressed  = mouseDown && !wasMouseDown;

  leftPressed  = leftHover && mouseDown;
  rightPressed = rightHover && mouseDown;

  if (justPressed && leftHover) {
    SelectPrev();
  }
  if (justPressed && rightHover) {
    SelectNext();
  }

  wasMouseDown = mouseDown;
}

// ═══════════════════════════════════════════════════════════
//  GUIButton
// ═══════════════════════════════════════════════════════════

void GUIButton::Draw(GUIDrawContext& ctx) {
  if (!visible || !texNormal) return;
  Texture* tex = (pressed && texPressed) ? texPressed : texNormal;
  if (!tex) return;
  // Guard against drawing with uninitialized/default size
  if (w <= 0.0f || h <= 0.0f) return;
  T8_LOG_TRACE("[GUIButton] Draw at (%.1f,%.1f) size %.1fx%.1f visible=%d", x, y, w, h, (int)visible);
  XVECTOR3 tint(1.0f, 1.0f, 1.0f);
  ctx.DrawTexturedQuad(x, y, w, h, tex, tint);
}

void GUIButton::UpdateInteraction(float mx, float my, bool mouseDown) {
  justClicked = false;
  if (!visible) return;

  hover = (mx >= x && mx <= x + w && my >= y && my <= y + h);
  pressed = hover && mouseDown;

  bool justReleased = !mouseDown && wasMouseDown;
  if (justReleased && hover) {
    justClicked = true;
  }
  wasMouseDown = mouseDown;
}

} // namespace t800
