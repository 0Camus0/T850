#include "gui/GUIElement.h"
#include <scene/T8_TextRenderer.h>
#include <scene/T8_Quad.h>
#include <video/BaseDriver.h>
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

  Quad::Vertex verts[4] = {
    {x0, y1, 0.0f, 1.0f, 0.0f, 0.0f},
    {x0, y0, 0.0f, 1.0f, 0.0f, 1.0f},
    {x1, y0, 0.0f, 1.0f, 1.0f, 1.0f},
    {x1, y1, 0.0f, 1.0f, 1.0f, 0.0f},
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

  // Knob: square side = bar height, centered on bar
  float kSize = h;
  float kx = GetKnobX();
  float kHalf = kSize * 0.5f;
  float ky = y + h * 0.5f - kHalf;    // vertically centred on bar
  XVECTOR3 knobTint = (knobHover || knobDragging)
    ? XVECTOR3(1.5f, 1.5f, 1.5f)
    : XVECTOR3(1.0f, 1.0f, 1.0f);
  ctx.DrawTexturedQuad(kx - kHalf, ky, kSize, kSize, ctx.knobTex, knobTint);
}

void GUISliderBar::UpdateInteraction(float mx, float my, bool mouseDown) {
  float kSize = h;                               // knob square = bar height
  float kHalf = kSize * 0.5f;
  float kx    = GetKnobX();
  float ky0   = y + h * 0.5f - kHalf;            // = y

  knobHover = (mx >= kx - kHalf && mx <= kx + kHalf &&
               my >= ky0 && my <= ky0 + kSize);

  bool barHover = (mx >= x && mx <= x + w && my >= y && my <= y + h);

  float range = (std::max)(0.0f, w - kSize);

  if (mouseDown && !knobDragging && (knobHover || barHover)) {
    knobDragging = true;
    float norm = (range > 0.0f) ? (mx - x - kHalf) / range : 0.0f;
    norm = (std::max)(0.0f, (std::min)(1.0f, norm));
    SetValue(minVal + norm * (maxVal - minVal));
  }
  if (knobDragging) {
    if (mouseDown) {
      float norm = (range > 0.0f) ? (mx - x - kHalf) / range : 0.0f;
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
  if (checked && ctx.checkMarkTex)
    ctx.DrawTexturedQuad(x, y, h, h, ctx.checkMarkTex, tint);
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
  // Draw current option text centered on bar
  if (ctx.text) {
    int sw = (int)ctx.screenW;
    int sh = (int)ctx.screenH;
    float charH = ctx.text->m_fontSize * ctx.screenH / (float)ctx.text->m_textureSize;
    float scale = (charH > 0.0f && h > 0.0f) ? h / charH : 1.0f;

    const std::string& opt = CurrentOption();
    float textW = ctx.text->MeasurePixel(opt, sw, sh) * scale;
    float barX = x + btnSize;
    float barW = w - btnSize * 2.0f;
    float textX = barX + (barW - textW) * 0.5f;
    float textY = y;
    XVECTOR3 textColor(0.9f, 0.85f, 0.8f);
    ctx.text->DrawPixelScaled(textX, textY, scale, scale, sw, sh, textColor, opt);
  }
}

void GUISelector::DrawQuadsOnly(GUIDrawContext& ctx) {
  if (!visible) return;

  float barX = x + btnSize;
  float barW = w - btnSize * 2.0f;
  XVECTOR3 tint(1.0f, 1.0f, 1.0f);

  // Draw bar background
  if (ctx.selectorBarTex)
    ctx.DrawTexturedQuad(barX, y, barW, h, ctx.selectorBarTex, tint);

  // Left button
  Texture* leftTex = leftPressed ? ctx.selectorBtnLeftPressTex : ctx.selectorBtnLeftTex;
  if (leftTex)
    ctx.DrawTexturedQuad(x, y, btnSize, h, leftTex, tint);

  // Right button
  Texture* rightTex = rightPressed ? ctx.selectorBtnRightPressTex : ctx.selectorBtnRightTex;
  if (rightTex)
    ctx.DrawTexturedQuad(x + w - btnSize, y, btnSize, h, rightTex, tint);
}

void GUISelector::DrawTextBatched(GUIDrawContext& ctx) {
  if (!visible || !ctx.text) return;

  int sw = (int)ctx.screenW;
  int sh = (int)ctx.screenH;
  float charH = ctx.text->m_fontSize * ctx.screenH / (float)ctx.text->m_textureSize;
  float scale = (charH > 0.0f && h > 0.0f) ? h / charH : 1.0f;

  float barX = x + btnSize;
  float barW = w - btnSize * 2.0f;
  const std::string& opt = CurrentOption();
  float textW = ctx.text->MeasurePixel(opt, sw, sh) * scale;
  float textX = barX + (barW - textW) * 0.5f;
  float textY = y;
  XVECTOR3 textColor(0.9f, 0.85f, 0.8f);
  ctx.text->DrawPixelScaledBatched(textX, textY, scale, scale, sw, sh, textColor, opt);
}

void GUISelector::UpdateInteraction(float mx, float my, bool mouseDown) {
  justChanged = false;

  // Left button hit area
  leftHover  = (mx >= x && mx <= x + btnSize && my >= y && my <= y + h);
  // Right button hit area
  rightHover = (mx >= x + w - btnSize && mx <= x + w && my >= y && my <= y + h);

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

} // namespace t800
