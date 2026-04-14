#include "gui/T8_GUI.h"
#include "gui/SliderBarData.h"
#include "gui/SliderKnobData.h"
#include "gui/CheckBoxBoxData.h"
#include "gui/CheckBoxCheckData.h"
#include "gui/SelectorBarData.h"
#include "gui/SelectorBtnLeftData.h"
#include "gui/SelectorBtnRightData.h"
#include "gui/SelectorBtnLeftPressedData.h"
#include "gui/SelectorBtnRightPressedData.h"
#include <utils/Log.h>

#include <video/GLShader.h>
#include <video/GLDriver.h>
#if defined(OS_WINDOWS)
#include <video/windows/D3DXShader.h>
#include <video/windows/D3DXDriver.h>
#endif
#include <utils/Utils.h>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#pragma warning(push)
#pragma warning(disable: 4267 4244)
#include <glaze/glaze.hpp>
#pragma warning(pop)

#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>

namespace t800 {

extern Device*        T8Device;
extern DeviceContext* T8DeviceContext;

// ─── GUILayout ──────────────────────────────────────────────
void GUILayout::Compute(int screenW, int screenH) {
  scale       = (float)screenH / 720.0f;
  sliderW     = 100.0f * scale;
  sliderH     =  25.0f * scale;
  knobSize    = sliderH;
  spacingY    =  40.0f * scale;
  marginRight =  20.0f * scale;
  marginTop   =  10.0f * scale;
  labelGap    =   8.0f * scale;
  valueGap    =   8.0f * scale;
  textureSize = 1024.0f;
  float desiredPx = 11.0f * scale;
  fontSize = desiredPx * textureSize / (float)screenH;
}

// ─── GUIManager Init / Destroy ──────────────────────────────
void GUIManager::Init(int screenW, int screenH) {
  m_layout.Compute(screenW, screenH);

  m_textRenderer.LoadFromFile(m_layout.fontSize, "Fonts/Martius-LV9L4.ttf",
                              m_layout.textureSize);

  InitTextures();
  m_quad.Init();
  InitShader();

  BufferDesc bdesc;
  bdesc.byteWidth = sizeof(XVECTOR3);
  bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
  m_CB = (ConstantBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::CONSTANT, bdesc);

  m_initialized = true;
  m_visible = false;
  T8_LOG_INFO("[GUIManager] Initialized  scale=%.2f  fontSize=%.1f  visible=%d",
         m_layout.scale, m_layout.fontSize, (int)m_visible);
}

void GUIManager::InitShader() {
  char* vsSourceP;
  char* fsSourceP;
  if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) {
    vsSourceP = file2string("Shaders/VS_GUI.glsl");
    fsSourceP = file2string("Shaders/FS_GUI.glsl");
  } else {
    vsSourceP = file2string("Shaders/VS_GUI.hlsl");
    fsSourceP = file2string("Shaders/FS_GUI.hlsl");
  }

  std::string vstr(vsSourceP);
  std::string fstr(fsSourceP);

  if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) {
#if defined(USING_OPENGL)
    std::string Defines;
    Defines += "#version 130\n\n";
    Defines += "#define lowp \n\n";
    Defines += "#define mediump \n\n";
    Defines += "#define highp \n\n";
    vstr = Defines + vstr;
    fstr = Defines + fstr;
#elif defined(USING_GL_COMMON)
    std::string Defines;
    Defines += "#version 300 es\n\n";
    Defines += "#define ES_30\n\n";
    vstr = Defines + vstr;
    fstr = Defines + fstr;
#endif
  }

  free(vsSourceP);
  free(fsSourceP);

  int shaderID = g_pBaseDriver->CreateShader(vstr, fstr);
  m_shader = g_pBaseDriver->GetShaderIdx(shaderID);
}

void GUIManager::InitTextures() {
  // Slider bar
  {
    int total = GUI_SLIDER_BAR_WIDTH * GUI_SLIDER_BAR_HEIGHT;
    std::vector<unsigned char> rgba(total * 4);
    for (int i = 0; i < total; i++) {
      uint32_t px = GUI_SLIDER_BAR_DATA[i];
      rgba[i * 4 + 0] = (unsigned char)(px & 0xFF);
      rgba[i * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
      rgba[i * 4 + 2] = (unsigned char)((px >> 16) & 0xFF);
      rgba[i * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
    }
    m_barTexture = T8Device->CreateTextureFromMemory(
      rgba.data(), GUI_SLIDER_BAR_WIDTH, GUI_SLIDER_BAR_HEIGHT, 4, "gui_slider_bar");
  }
  // Slider knob
  {
    int total = GUI_SLIDER_KNOB_WIDTH * GUI_SLIDER_KNOB_HEIGHT;
    std::vector<unsigned char> rgba(total * 4);
    for (int i = 0; i < total; i++) {
      uint32_t px = GUI_SLIDER_KNOB_DATA[i];
      rgba[i * 4 + 0] = (unsigned char)(px & 0xFF);
      rgba[i * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
      rgba[i * 4 + 2] = (unsigned char)((px >> 16) & 0xFF);
      rgba[i * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
    }
    m_knobTexture = T8Device->CreateTextureFromMemory(
      rgba.data(), GUI_SLIDER_KNOB_WIDTH, GUI_SLIDER_KNOB_HEIGHT, 4, "gui_slider_knob");
  }
  // 1x1 white pixel for solid-colour quads
  {
    unsigned char white[4] = {255, 255, 255, 255};
    m_whiteTexture = T8Device->CreateTextureFromMemory(white, 1, 1, 4, "gui_white_1x1");
  }
  // Checkbox box
  {
    int total = GUI_CHECKBOX_BOX_WIDTH * GUI_CHECKBOX_BOX_HEIGHT;
    std::vector<unsigned char> rgba(total * 4);
    for (int i = 0; i < total; i++) {
      uint32_t px = GUI_CHECKBOX_BOX_DATA[i];
      rgba[i * 4 + 0] = (unsigned char)(px & 0xFF);
      rgba[i * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
      rgba[i * 4 + 2] = (unsigned char)((px >> 16) & 0xFF);
      rgba[i * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
    }
    m_checkBoxTexture = T8Device->CreateTextureFromMemory(
      rgba.data(), GUI_CHECKBOX_BOX_WIDTH, GUI_CHECKBOX_BOX_HEIGHT, 4, "gui_checkbox_box");
  }
  // Checkbox check mark
  {
    int total = GUI_CHECKBOX_CHECK_WIDTH * GUI_CHECKBOX_CHECK_HEIGHT;
    std::vector<unsigned char> rgba(total * 4);
    for (int i = 0; i < total; i++) {
      uint32_t px = GUI_CHECKBOX_CHECK_DATA[i];
      rgba[i * 4 + 0] = (unsigned char)(px & 0xFF);
      rgba[i * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
      rgba[i * 4 + 2] = (unsigned char)((px >> 16) & 0xFF);
      rgba[i * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
    }
    m_checkMarkTexture = T8Device->CreateTextureFromMemory(
      rgba.data(), GUI_CHECKBOX_CHECK_WIDTH, GUI_CHECKBOX_CHECK_HEIGHT, 4, "gui_checkbox_check");
  }
  // Selector bar
  {
    int total = GUI_SELECTOR_BAR_WIDTH * GUI_SELECTOR_BAR_HEIGHT;
    std::vector<unsigned char> rgba(total * 4);
    for (int i = 0; i < total; i++) {
      uint32_t px = GUI_SELECTOR_BAR_DATA[i];
      rgba[i * 4 + 0] = (unsigned char)(px & 0xFF);
      rgba[i * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
      rgba[i * 4 + 2] = (unsigned char)((px >> 16) & 0xFF);
      rgba[i * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
    }
    m_selectorBarTexture = T8Device->CreateTextureFromMemory(
      rgba.data(), GUI_SELECTOR_BAR_WIDTH, GUI_SELECTOR_BAR_HEIGHT, 4, "gui_selector_bar");
  }
  // Selector button left (non-pressed)
  {
    int total = GUI_SELECTOR_BTN_LEFT_WIDTH * GUI_SELECTOR_BTN_LEFT_HEIGHT;
    std::vector<unsigned char> rgba(total * 4);
    for (int i = 0; i < total; i++) {
      uint32_t px = GUI_SELECTOR_BTN_LEFT_DATA[i];
      rgba[i * 4 + 0] = (unsigned char)(px & 0xFF);
      rgba[i * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
      rgba[i * 4 + 2] = (unsigned char)((px >> 16) & 0xFF);
      rgba[i * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
    }
    m_selectorBtnLeftTexture = T8Device->CreateTextureFromMemory(
      rgba.data(), GUI_SELECTOR_BTN_LEFT_WIDTH, GUI_SELECTOR_BTN_LEFT_HEIGHT, 4, "gui_selector_btn_left");
  }
  // Selector button right (non-pressed)
  {
    int total = GUI_SELECTOR_BTN_RIGHT_WIDTH * GUI_SELECTOR_BTN_RIGHT_HEIGHT;
    std::vector<unsigned char> rgba(total * 4);
    for (int i = 0; i < total; i++) {
      uint32_t px = GUI_SELECTOR_BTN_RIGHT_DATA[i];
      rgba[i * 4 + 0] = (unsigned char)(px & 0xFF);
      rgba[i * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
      rgba[i * 4 + 2] = (unsigned char)((px >> 16) & 0xFF);
      rgba[i * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
    }
    m_selectorBtnRightTexture = T8Device->CreateTextureFromMemory(
      rgba.data(), GUI_SELECTOR_BTN_RIGHT_WIDTH, GUI_SELECTOR_BTN_RIGHT_HEIGHT, 4, "gui_selector_btn_right");
  }
  // Selector button left (pressed)
  {
    int total = GUI_SELECTOR_BTN_LEFT_PRESSED_WIDTH * GUI_SELECTOR_BTN_LEFT_PRESSED_HEIGHT;
    std::vector<unsigned char> rgba(total * 4);
    for (int i = 0; i < total; i++) {
      uint32_t px = GUI_SELECTOR_BTN_LEFT_PRESSED_DATA[i];
      rgba[i * 4 + 0] = (unsigned char)(px & 0xFF);
      rgba[i * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
      rgba[i * 4 + 2] = (unsigned char)((px >> 16) & 0xFF);
      rgba[i * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
    }
    m_selectorBtnLeftPressTexture = T8Device->CreateTextureFromMemory(
      rgba.data(), GUI_SELECTOR_BTN_LEFT_PRESSED_WIDTH, GUI_SELECTOR_BTN_LEFT_PRESSED_HEIGHT, 4, "gui_selector_btn_left_press");
  }
  // Selector button right (pressed)
  {
    int total = GUI_SELECTOR_BTN_RIGHT_PRESSED_WIDTH * GUI_SELECTOR_BTN_RIGHT_PRESSED_HEIGHT;
    std::vector<unsigned char> rgba(total * 4);
    for (int i = 0; i < total; i++) {
      uint32_t px = GUI_SELECTOR_BTN_RIGHT_PRESSED_DATA[i];
      rgba[i * 4 + 0] = (unsigned char)(px & 0xFF);
      rgba[i * 4 + 1] = (unsigned char)((px >> 8) & 0xFF);
      rgba[i * 4 + 2] = (unsigned char)((px >> 16) & 0xFF);
      rgba[i * 4 + 3] = (unsigned char)((px >> 24) & 0xFF);
    }
    m_selectorBtnRightPressTexture = T8Device->CreateTextureFromMemory(
      rgba.data(), GUI_SELECTOR_BTN_RIGHT_PRESSED_WIDTH, GUI_SELECTOR_BTN_RIGHT_PRESSED_HEIGHT, 4, "gui_selector_btn_right_press");
  }
}

void GUIManager::Destroy() {
  if (m_initialized) {
    m_quad.Destroy();
    m_CB->release();
    m_textRenderer.Destroy();
  }
  for (auto* e : m_elements) delete e;
  m_elements.clear();
  m_sliderPairs.clear();
  m_initialized = false;
}

// ─── Slider management ─────────────────────────────────────
void GUIManager::AddSlider(const SliderDesc& desc, int settingIndex) {
  // Create label element
  auto* lbl = new GUILabel();
  lbl->id    = "label_" + desc.name;
  lbl->color = XVECTOR3(0.9f, 0.85f, 0.8f);
  // text will be set in Draw/Layout

  // Create slider bar element
  auto* bar = new GUISliderBar();
  bar->id            = "slider_" + desc.name;
  bar->name          = desc.name;
  bar->label         = desc.label;
  bar->minVal        = desc.min_val;
  bar->maxVal        = desc.max_val;
  bar->step          = desc.step;
  bar->settingIndex  = settingIndex;
  bar->SetValue(desc.default_val);

  m_elements.push_back(lbl);
  m_elements.push_back(bar);
  m_sliderPairs.push_back({lbl, bar});
}

void GUIManager::ClearSliders() {
  for (auto* e : m_elements) delete e;
  m_elements.clear();
  m_sliderPairs.clear();
  m_checkboxPairs.clear();
  m_selectorPairs.clear();
  m_fpsLabel   = nullptr;
  m_dragTarget = nullptr;
  m_resizeTarget = nullptr;
  m_lastEdited = nullptr;
}

GUISliderBar* GUIManager::FindSlider(const std::string& name) {
  for (auto& sp : m_sliderPairs) {
    if (sp.slider->name == name) return sp.slider;
  }
  return nullptr;
}

// ─── Checkbox management ────────────────────────────────────
void GUIManager::AddCheckbox(const CheckboxDesc& desc, int settingIndex) {
  auto* lbl = new GUILabel();
  lbl->id    = "label_" + desc.name;
  lbl->text  = desc.label;
  lbl->color = XVECTOR3(0.9f, 0.85f, 0.8f);

  auto* cb = new GUICheckbox();
  cb->id           = "checkbox_" + desc.name;
  cb->name         = desc.name;
  cb->label        = desc.label;
  cb->checked      = desc.default_val;
  cb->settingIndex = settingIndex;

  m_elements.push_back(lbl);
  m_elements.push_back(cb);
  m_checkboxPairs.push_back({lbl, cb});
}

// ─── Selector management ────────────────────────────────────
void GUIManager::AddSelector(const SelectorDesc& desc, int settingIndex) {
  auto* lbl = new GUILabel();
  lbl->id    = "label_" + desc.name;
  lbl->text  = desc.label;
  lbl->color = XVECTOR3(0.9f, 0.85f, 0.8f);

  auto* sel = new GUISelector();
  sel->id            = "selector_" + desc.name;
  sel->name          = desc.name;
  sel->label         = desc.label;
  sel->options       = desc.options;
  sel->selectedIndex = desc.default_index;
  sel->settingIndex  = settingIndex;

  m_elements.push_back(lbl);
  m_elements.push_back(sel);
  m_selectorPairs.push_back({lbl, sel});
}

// ─── FPS label ───────────────────────────────────────────────────────
void GUIManager::AddFPSLabel() {
  auto* lbl = new GUILabel();
  lbl->id    = "label_fps";
  lbl->text  = "FPS ---";
  lbl->color = XVECTOR3(0.2f, 0.8f, 0.2f);
  lbl->isFPS = true;
  m_elements.push_back(lbl);
  m_fpsLabel = lbl;
}

void GUIManager::SetFPSText(const std::string& text, const XVECTOR3& color) {
  if (m_fpsLabel) {
    m_fpsLabel->text  = text;
    m_fpsLabel->color = color;
  }
}

// ─── Layout ─────────────────────────────────────────────────
void GUIManager::LayoutSliders(int screenW, int screenH) {
  m_layout.Compute(screenW, screenH);

  float screenWf = (float)screenW;
  float screenHf = (float)screenH;

  // Build a temporary draw context for text measurement
  GUIDrawContext tmpCtx;
  tmpCtx.text    = &m_textRenderer;
  tmpCtx.screenW = screenWf;
  tmpCtx.screenH = screenHf;

  // Compute natural text height
  float charH = m_layout.fontSize * screenHf / m_layout.textureSize;

  // Pre-measure maximum label width across ALL control types for uniform columns
  float maxLabelW = 0.0f;
  for (auto& sp : m_sliderPairs) {
    char buf[128];
    char valueBuf[32];
    if (sp.slider->name == "shadow_bias") {
      snprintf(valueBuf, sizeof(valueBuf), "%.2e", sp.slider->maxVal);
    } else {
      snprintf(valueBuf, sizeof(valueBuf), "%.2f", sp.slider->maxVal);
    }
    snprintf(buf, sizeof(buf), "%s: %s", sp.slider->label.c_str(), valueBuf);
    sp.label->text = buf;
    sp.label->FitToText(tmpCtx);
    if (sp.label->w > maxLabelW) maxLabelW = sp.label->w;
  }
  for (auto& cp : m_checkboxPairs) {
    cp.label->FitToText(tmpCtx);
    if (cp.label->w > maxLabelW) maxLabelW = cp.label->w;
  }
  for (auto& sp : m_selectorPairs) {
    sp.label->FitToText(tmpCtx);
    if (sp.label->w > maxLabelW) maxLabelW = sp.label->w;
  }

  float groupW = maxLabelW + m_layout.labelGap + m_layout.sliderW;
  float groupSpacingX = groupW + m_layout.marginRight;

  // Snap starting position to grid
  float startX = m_gridCellW;
  float startY = m_gridCellH;

  // Determine how many columns fit on screen
  int cols = (std::max)(1, (int)((screenWf - startX) / groupSpacingX));

  // Layout all controls in a continuous grid: sliders first, then checkboxes, then selectors
  int itemIdx = 0;

  for (size_t i = 0; i < m_sliderPairs.size(); i++, itemIdx++) {
    auto& sp = m_sliderPairs[i];
    int col = itemIdx % cols;
    int row = itemIdx / cols;

    float cellX = startX + col * groupSpacingX;
    float rowY  = startY + row * m_layout.spacingY;

    float labelX = std::round(cellX / m_gridCellW) * m_gridCellW;
    float labelY = std::round(rowY  / m_gridCellH) * m_gridCellH;

    char buf[128];
    char valueBuf[32];
    if (sp.slider->name == "shadow_bias") {
      snprintf(valueBuf, sizeof(valueBuf), "%.2e", sp.slider->value);
    } else {
      snprintf(valueBuf, sizeof(valueBuf), "%.2f", sp.slider->value);
    }
    snprintf(buf, sizeof(buf), "%s: %s", sp.slider->label.c_str(), valueBuf);
    sp.label->text = buf;
    sp.label->FitToText(tmpCtx);
    sp.label->x = labelX;
    sp.label->y = labelY;
    sp.label->h = charH;

    float barX = labelX + maxLabelW + m_layout.labelGap;
    barX = std::round(barX / m_gridCellW) * m_gridCellW;
    sp.slider->x        = barX;
    sp.slider->y        = labelY;
    sp.slider->w        = m_layout.sliderW;
    sp.slider->h        = m_layout.sliderH;
    sp.slider->knobSize = m_layout.knobSize;
  }

  for (size_t i = 0; i < m_checkboxPairs.size(); i++, itemIdx++) {
    auto& cp = m_checkboxPairs[i];
    int col = itemIdx % cols;
    int row = itemIdx / cols;

    float cellX = startX + col * groupSpacingX;
    float rowY  = startY + row * m_layout.spacingY;

    float labelX = std::round(cellX / m_gridCellW) * m_gridCellW;
    float labelY = std::round(rowY  / m_gridCellH) * m_gridCellH;

    cp.label->FitToText(tmpCtx);
    cp.label->x = labelX;
    cp.label->y = labelY;
    cp.label->h = charH;

    float cbX = labelX + maxLabelW + m_layout.labelGap;
    cbX = std::round(cbX / m_gridCellW) * m_gridCellW;
    cp.checkbox->x = cbX;
    cp.checkbox->y = labelY;
    cp.checkbox->w = m_layout.sliderH;  // square checkbox
    cp.checkbox->h = m_layout.sliderH;
  }

  for (size_t i = 0; i < m_selectorPairs.size(); i++, itemIdx++) {
    auto& sp = m_selectorPairs[i];
    int col = itemIdx % cols;
    int row = itemIdx / cols;

    float cellX = startX + col * groupSpacingX;
    float rowY  = startY + row * m_layout.spacingY;

    float labelX = std::round(cellX / m_gridCellW) * m_gridCellW;
    float labelY = std::round(rowY  / m_gridCellH) * m_gridCellH;

    sp.label->FitToText(tmpCtx);
    sp.label->x = labelX;
    sp.label->y = labelY;
    sp.label->h = charH;

    float selX = labelX + maxLabelW + m_layout.labelGap;
    selX = std::round(selX / m_gridCellW) * m_gridCellW;
    sp.selector->x       = selX;
    sp.selector->y       = labelY;
    sp.selector->w       = m_layout.sliderW;
    sp.selector->h       = m_layout.sliderH;
    sp.selector->btnSize = m_layout.sliderH;
  }

  // Position FPS label at bottom-left, grid-aligned
  if (m_fpsLabel) {
    m_fpsLabel->FitToText(tmpCtx);
    m_fpsLabel->x = m_gridCellW;
    m_fpsLabel->y = std::round((screenHf - m_gridCellH - charH) / m_gridCellH) * m_gridCellH;
    m_fpsLabel->h = charH;
  }

  // Clamp all elements inside the screen
  for (auto* e : m_elements) {
    if (e->x < 0.0f)               e->x = 0.0f;
    if (e->y < 0.0f)               e->y = 0.0f;
    if (e->x + e->w > screenWf)    e->x = screenWf - e->w;
    if (e->y + e->h > screenHf)    e->y = screenHf - e->h;
  }
}

// ─── Update ─────────────────────────────────────────────────
void GUIManager::Update(const InputManager& input, int screenW, int screenH) {
  if (!m_visible) return;

  float mx = (float)input.mouseX;
  float my = (float)input.mouseY;
  bool mouseDown = input.MouseButtonStates[0][0];

  if (m_editMode) {
    UpdateEditMode(mx, my, mouseDown);
  } else {
    // Normal slider interaction
    for (auto& sp : m_sliderPairs) {
      float before = sp.slider->value;
      sp.slider->UpdateInteraction(mx, my, mouseDown);
      float after = sp.slider->value;
      if (before != after) {
        const char* sliderName = sp.slider->label.empty() ? sp.slider->name.c_str() : sp.slider->label.c_str();
        T8_LOG_INFO("[GUI] Slider '%s' changed: %.4f -> %.4f", sliderName, before, after);
      }
    }
    // Checkbox interaction
    for (auto& cp : m_checkboxPairs) {
      bool before = cp.checkbox->checked;
      cp.checkbox->UpdateInteraction(mx, my, mouseDown);
      bool after = cp.checkbox->checked;
      if (before != after) {
        const char* checkboxName = cp.checkbox->label.empty() ? cp.checkbox->name.c_str() : cp.checkbox->label.c_str();
        T8_LOG_INFO("[GUI] Checkbox '%s' changed: %s -> %s", checkboxName, before ? "true" : "false", after ? "true" : "false");
      }
    }
    // Selector interaction
    for (auto& sp : m_selectorPairs) {
      int beforeIdx = sp.selector->selectedIndex;
      sp.selector->UpdateInteraction(mx, my, mouseDown);
      int afterIdx = sp.selector->selectedIndex;
      if (beforeIdx != afterIdx) {
        const char* selectorName = sp.selector->label.empty() ? sp.selector->name.c_str() : sp.selector->label.c_str();
        T8_LOG_INFO("[GUI] Selector '%s' changed: [%d] %s -> [%d] %s",
                    selectorName,
                    beforeIdx,
                    (beforeIdx >= 0 && beforeIdx < (int)sp.selector->options.size()) ? sp.selector->options[beforeIdx].c_str() : "<invalid>",
                    afterIdx,
                    (afterIdx >= 0 && afterIdx < (int)sp.selector->options.size()) ? sp.selector->options[afterIdx].c_str() : "<invalid>");
      }
    }
  }

  m_wasMouseDown = mouseDown;
}

void GUIManager::UpdateEditMode(float mx, float my, bool mouseDown) {
  bool justPressed  = mouseDown && !m_wasMouseDown;
  bool justReleased = !mouseDown && m_wasMouseDown;

  // ── Handle releases ──
  if (justReleased) {
    if (m_resizeTarget) {
      m_resizeTarget->resizing = false;
      m_resizeTarget->OnResizeEnd();
      m_lastEdited = m_resizeTarget;
      m_resizeTarget = nullptr;
      RebakeFontIfNeeded();
    }
    if (m_dragTarget) {
      m_dragTarget->dragging = false;
      if (m_snapToGrid) {
        m_dragTarget->SnapToGrid(m_gridCellW, m_gridCellH);
      }
      m_lastEdited = m_dragTarget;
      m_dragTarget = nullptr;
    }
    return;
  }

  // ── Handle ongoing drag/resize ──
  if (mouseDown && m_resizeTarget) {
    float dw = mx - m_resizeTarget->resizeOrigMX;
    float dh = my - m_resizeTarget->resizeOrigMY;
    m_resizeTarget->w = (std::max)(10.0f, m_resizeTarget->resizeOrigW + dw);
    m_resizeTarget->h = (std::max)(10.0f, m_resizeTarget->resizeOrigH + dh);
    return;
  }
  if (mouseDown && m_dragTarget) {
    m_dragTarget->x = mx - m_dragTarget->dragOffX;
    m_dragTarget->y = my - m_dragTarget->dragOffY;
    return;
  }

  // ── Handle new press ──
  if (justPressed) {
    // Deselect all first
    for (auto* e : m_elements) e->selected = false;

    // Check scale handles first (higher priority)
    for (int i = (int)m_elements.size() - 1; i >= 0; i--) {
      auto* e = m_elements[i];
      if (!e->visible) continue;
      if (e->HitTestScaleHandle(mx, my)) {
        e->selected   = true;
        e->resizing   = true;
        e->resizeOrigW  = e->w;
        e->resizeOrigH  = e->h;
        e->resizeOrigMX = mx;
        e->resizeOrigMY = my;
        m_resizeTarget = e;
        T8_LOG_DEBUG("[GUI Edit] Selected: %s", e->id.c_str());
        return;
      }
    }
    // Check drag (body hit test)
    for (int i = (int)m_elements.size() - 1; i >= 0; i--) {
      auto* e = m_elements[i];
      if (!e->visible) continue;
      if (e->HitTest(mx, my)) {
        e->selected  = true;
        e->dragging  = true;
        e->dragOffX  = mx - e->x;
        e->dragOffY  = my - e->y;
        m_dragTarget = e;
        T8_LOG_DEBUG("[GUI Edit] Selected: %s", e->id.c_str());
        return;
      }
    }
  }
}

// ─── Draw ───────────────────────────────────────────────────
void GUIManager::Draw() {
  if (!m_visible || !m_initialized) return;

  float screenW = (float)g_pBaseDriver->width;
  float screenH = (float)g_pBaseDriver->height;

  // Prepare draw context
  m_ctx.text       = &m_textRenderer;
  m_ctx.quad       = &m_quad;
  m_ctx.shader     = m_shader;
  m_ctx.cb         = m_CB;
  m_ctx.barTex     = m_barTexture;
  m_ctx.knobTex    = m_knobTexture;
  m_ctx.whiteTex   = m_whiteTexture;
  m_ctx.checkBoxTex  = m_checkBoxTexture;
  m_ctx.checkMarkTex = m_checkMarkTexture;
  m_ctx.selectorBarTex       = m_selectorBarTexture;
  m_ctx.selectorBtnLeftTex   = m_selectorBtnLeftTexture;
  m_ctx.selectorBtnRightTex  = m_selectorBtnRightTexture;
  m_ctx.selectorBtnLeftPressTex  = m_selectorBtnLeftPressTexture;
  m_ctx.selectorBtnRightPressTex = m_selectorBtnRightPressTexture;
  m_ctx.screenW    = screenW;
  m_ctx.screenH    = screenH;
  m_ctx.editMode   = m_editMode;
  m_ctx.snapToGrid = m_snapToGrid;
  m_ctx.gridCellW  = m_gridCellW;
  m_ctx.gridCellH  = m_gridCellH;

  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::NONE);

  // Draw grid first (behind everything) in edit mode
  if (m_editMode) {
    DrawGrid();
  }

  // Set GUI render state
  m_quad.Set();
  m_shader->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);

  // ── Pass 1: all quads (slider bars/knobs, checkboxes, selector bars/buttons) ──

  // Update label texts with current values and draw slider quads
  for (auto& sp : m_sliderPairs) {
    if (!sp.slider->visible) continue;

    char buf[128];
    snprintf(buf, sizeof(buf), "%s: %.2f", sp.slider->label.c_str(), sp.slider->value);
    sp.label->text = buf;

    sp.slider->Draw(m_ctx);
  }

  // Draw checkbox quads
  for (auto& cp : m_checkboxPairs) {
    if (!cp.checkbox->visible) continue;
    cp.checkbox->Draw(m_ctx);
  }

  // Draw selector quads (bar + buttons only, no text)
  for (auto& sp : m_selectorPairs) {
    if (!sp.selector->visible) continue;
    sp.selector->DrawQuadsOnly(m_ctx);
  }

  // ── Pass 2: all text in one batch (single shader switch, one draw call per string) ──

  m_textRenderer.BeginBatch();

  for (auto& sp : m_sliderPairs) {
    if (!sp.slider->visible) continue;
    sp.label->Draw(m_ctx);
  }

  for (auto& cp : m_checkboxPairs) {
    if (!cp.checkbox->visible) continue;
    cp.label->Draw(m_ctx);
  }

  for (auto& sp : m_selectorPairs) {
    if (!sp.selector->visible) continue;
    sp.selector->DrawTextBatched(m_ctx);
    sp.label->Draw(m_ctx);
  }

  if (m_fpsLabel && m_fpsLabel->visible) {
    m_fpsLabel->Draw(m_ctx);
  }

  m_textRenderer.EndBatch();

  // Draw edit overlays on top
  if (m_editMode) {
    DrawEditOverlays();
  }

  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::BLEND_DEFAULT);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::DEPTH_DEFAULT);
}

void GUIManager::DrawFPSOnly() {
  if (!m_initialized || !m_fpsLabel || !m_fpsLabel->visible) return;

  float screenW = (float)g_pBaseDriver->width;
  float screenH = (float)g_pBaseDriver->height;

  m_ctx.text       = &m_textRenderer;
  m_ctx.quad       = &m_quad;
  m_ctx.shader     = m_shader;
  m_ctx.cb         = m_CB;
  m_ctx.screenW    = screenW;
  m_ctx.screenH    = screenH;
  m_ctx.editMode   = false;
  m_ctx.snapToGrid = false;

  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::NONE);

  m_textRenderer.BeginBatch();
  m_fpsLabel->Draw(m_ctx);
  m_textRenderer.EndBatch();

  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::BLEND_DEFAULT);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::DEPTH_DEFAULT);
}

void GUIManager::DrawGrid() {
  m_quad.Set();
  m_shader->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);

  XVECTOR3 gridColor(1.0f, 1.0f, 1.0f);
  float lineW = 1.0f;

  // Vertical lines
  for (float gx = 0.0f; gx < m_ctx.screenW; gx += m_gridCellW) {
    m_ctx.DrawSolidQuad(gx, 0.0f, lineW, m_ctx.screenH, gridColor);
  }
  // Horizontal lines
  for (float gy = 0.0f; gy < m_ctx.screenH; gy += m_gridCellH) {
    m_ctx.DrawSolidQuad(0.0f, gy, m_ctx.screenW, lineW, gridColor);
  }
}

void GUIManager::DrawEditOverlays() {
  // Ensure GUI shader is active for the solid-colour quads
  m_quad.Set();
  m_shader->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);

  for (auto* e : m_elements) {
    e->DrawEditOverlay(m_ctx);
  }
}

// ─── Font rebake after label resize ─────────────────────────
void GUIManager::RebakeFontIfNeeded() {
  float screenH = (float)g_pBaseDriver->height;
  float charH = m_textRenderer.m_fontSize * screenH / (float)m_textRenderer.m_textureSize;

  // Find the maximum label height — this determines the effective font size needed
  float maxH = charH;
  for (auto& sp : m_sliderPairs) {
    if (sp.label->h > maxH)
      maxH = sp.label->h;
  }
  if (m_fpsLabel && m_fpsLabel->h > maxH)
    maxH = m_fpsLabel->h;

  // Only rebake if the largest label exceeds the baked glyph height
  if (maxH > charH * 1.05f) {
    float newFontSize = maxH * (float)m_textRenderer.m_textureSize / screenH;
    m_textRenderer.Rebake(newFontSize);
  }
}

// ─── Grid resize (+/-) ──────────────────────────────────────
void GUIManager::GrowGrid(float delta) {
  m_gridCellW = (std::max)(10.0f, m_gridCellW + delta);
  m_gridCellH = (std::max)(10.0f, m_gridCellH + delta);
  T8_LOG_DEBUG("[GUI Edit] Grid cell size: %.0f x %.0f", m_gridCellW, m_gridCellH);
}

// ─── Apply uniform scale ────────────────────────────────────
void GUIManager::ApplyUniformScale() {
  if (!m_lastEdited) {
    T8_LOG_DEBUG("[GUI Edit] No element was edited yet");
    return;
  }

  float newW = m_lastEdited->w;
  float newH = m_lastEdited->h;

  // Determine element type by trying dynamic_cast
  GUISliderBar* asSlider   = dynamic_cast<GUISliderBar*>(m_lastEdited);
  GUILabel*     asLabel    = dynamic_cast<GUILabel*>(m_lastEdited);
  GUICheckbox*  asCheckbox = dynamic_cast<GUICheckbox*>(m_lastEdited);
  GUISelector*  asSelector = dynamic_cast<GUISelector*>(m_lastEdited);

  if (asSlider) {
    // Apply to all slider bars
    for (auto& sp : m_sliderPairs) {
      sp.slider->w = newW;
      sp.slider->h = newH;
      sp.slider->knobSize = newH;
    }
    T8_LOG_DEBUG("[GUI Edit] Applied slider scale (%.0f x %.0f) to all sliders", newW, newH);
  } else if (asCheckbox) {
    for (auto& cp : m_checkboxPairs) {
      cp.checkbox->w = newH;  // keep square
      cp.checkbox->h = newH;
    }
    T8_LOG_DEBUG("[GUI Edit] Applied checkbox scale (%.0f) to all checkboxes", newH);
  } else if (asSelector) {
    for (auto& sp : m_selectorPairs) {
      sp.selector->w = newW;
      sp.selector->h = newH;
      sp.selector->btnSize = newH;
    }
    T8_LOG_DEBUG("[GUI Edit] Applied selector scale (%.0f x %.0f) to all selectors", newW, newH);
  } else if (asLabel) {
    // Apply height (scale) to all labels
    for (auto& sp : m_sliderPairs) {
      sp.label->h = newH;
    }
    if (m_fpsLabel) {
      m_fpsLabel->h = newH;
    }
    T8_LOG_DEBUG("[GUI Edit] Applied label height (%.0f) to all labels", newH);
    RebakeFontIfNeeded();
  }
}

// ═══════════════════════════════════════════════════════════
//  Layout JSON save / load  (via glaze)
// ═══════════════════════════════════════════════════════════

struct ElementLayoutEntry {
  std::string id;
  float x = 0, y = 0, w = 0, h = 0;  // normalized: x,w in [0,1] of refW; y,h in [0,1] of refH
};

struct GUILayoutFile {
  float ref_width  = 1920.0f;   // screen width when layout was authored
  float ref_height = 1080.0f;   // screen height when layout was authored
  std::vector<ElementLayoutEntry> elements;
  float gridCellW = 40.0f;      // normalized to ref_height
  float gridCellH = 40.0f;      // normalized to ref_height
};

bool GUIManager::SaveLayout(const std::string& path) {
  float screenW = (float)g_pBaseDriver->width;
  float screenH = (float)g_pBaseDriver->height;

  GUILayoutFile lf;
  lf.ref_width  = screenW;
  lf.ref_height = screenH;
  // Normalize grid cells by reference height (uniform scale)
  lf.gridCellW = m_gridCellW / screenH;
  lf.gridCellH = m_gridCellH / screenH;
  // Normalize element coords: positions by respective axis, sizes by height
  for (auto* e : m_elements) {
    lf.elements.push_back({
      e->id,
      e->x / screenW,   // X position: fraction of width
      e->y / screenH,   // Y position: fraction of height
      e->w / screenH,   // width:  fraction of height (uniform)
      e->h / screenH    // height: fraction of height (uniform)
    });
  }
  auto result = glz::write<glz::opts{.prettify = true}>(lf);
  if (!result) {
    T8_LOG_ERROR("[GUIManager] Failed to serialize layout");
    return false;
  }
  std::ofstream file(path);
  if (!file.is_open()) {
    T8_LOG_ERROR("[GUIManager] Cannot write '%s'", path.c_str());
    return false;
  }
  file << result.value();
  T8_LOG_INFO("[GUIManager] Layout saved to '%s' (%zu elements)", path.c_str(), m_elements.size());
  return true;
}

bool GUIManager::LoadLayout(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    T8_LOG_INFO("[GUIManager] No layout file '%s' -- using defaults", path.c_str());
    return false;
  }
  std::stringstream ss;
  ss << file.rdbuf();
  std::string json = ss.str();

  GUILayoutFile lf;
  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(lf, json);
  if (ec) {
    std::string err = glz::format_error(ec, json);
    T8_LOG_ERROR("[GUIManager] Parse error '%s': %s", path.c_str(), err.c_str());
    return false;
  }

  float curW = (float)g_pBaseDriver->width;
  float curH = (float)g_pBaseDriver->height;

  // Denormalize grid cells (stored as fraction of ref height)
  m_gridCellW = lf.gridCellW * curH;
  m_gridCellH = lf.gridCellH * curH;

  // Denormalize element coords:
  //   positions: X by current width, Y by current height
  //   sizes:     both by current height (uniform — preserves proportions)
  for (auto& entry : lf.elements) {
    for (auto* e : m_elements) {
      if (e->id == entry.id) {
        e->x = entry.x * curW;
        e->y = entry.y * curH;
        e->w = entry.w * curH;
        e->h = entry.h * curH;
        break;
      }
    }
  }
  T8_LOG_INFO("[GUIManager] Layout loaded from '%s' (%zu entries, ref %.0fx%.0f -> %.0fx%.0f)",
         path.c_str(), lf.elements.size(), lf.ref_width, lf.ref_height, curW, curH);
  RebakeFontIfNeeded();
  return true;
}

} // namespace t800
