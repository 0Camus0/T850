#include "gui/T8_GUI.h"
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
#include <chrono>

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
  // Buttons start hidden until LayoutSliders positions them
  m_guiButton.visible = false;
  m_backButton.visible = false;
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
  auto loadGuiTexture = [&](const char* name) -> Texture* {
    // Load from Assets/Layouts/Textures at runtime.
    // The texture loader roots at "Textures/", so "../Layouts/..." resolves to Assets/Layouts.
    std::string path = std::string("../Layouts/Textures/") + name;
    Texture* t = T8Device->CreateTexture(path);
    if (!t) {
      T8_LOG_ERROR("[GUIManager] Failed to load GUI texture '%s'", path.c_str());
      return nullptr;
    }
    // High quality UI sampling: trilinear/aniso with mipmaps, clamped edges.
    t->params = TEXT_BASIC_PARAMS::CLAMP_TO_EDGE | TEXT_BASIC_PARAMS::MIPMAPS;
    t->SetTextureParams();
    return t;
  };

  m_barTexture = loadGuiTexture("SliderBar.png");
  m_knobTexture = loadGuiTexture("SliderKnob.png");
  // 1x1 white pixel for solid-colour quads
  {
    unsigned char white[4] = {255, 255, 255, 255};
    m_whiteTexture = T8Device->CreateTextureFromMemory(white, 1, 1, 4, "gui_white_1x1");
  }
  m_checkBoxTexture = loadGuiTexture("GUI_CheckBox_Box.png");
  m_checkMarkTexture = loadGuiTexture("GUI_Checkbox_Check.png");

  m_selectorBarTexture = loadGuiTexture("GUI_DropBar.png");
  m_selectorBtnLeftTexture = loadGuiTexture("GUI_DropNonPressedLeft.png");
  m_selectorBtnRightTexture = loadGuiTexture("GUI_DropNonPressedRight.png");
  m_selectorBtnLeftPressTexture = loadGuiTexture("GUI_DropPressedLeft.png");
  m_selectorBtnRightPressTexture = loadGuiTexture("GUI_DropPressedRight.png");

  // Line-edit popup
  m_popupBgTexture            = loadGuiTexture("PopupBackground.png");
  m_popupOkTexture            = loadGuiTexture("PopUpOKNonPressed.png");
  m_popupOkPressedTexture     = loadGuiTexture("PopUpOKPressed.png");
  m_popupCancelTexture        = loadGuiTexture("PopUpCancelNonPressed.png");
  m_popupCancelPressedTexture = loadGuiTexture("PopUpCancelPressed.png");

  // GUI / Back buttons (loaded from Textures/UI/)
  auto loadUITexture = [&](const char* name) -> Texture* {
    std::string path = std::string("UI/") + name;
    Texture* t = T8Device->CreateTexture(path);
    if (!t) {
      T8_LOG_ERROR("[GUIManager] Failed to load UI texture '%s'", path.c_str());
      return nullptr;
    }
    t->params = TEXT_BASIC_PARAMS::CLAMP_TO_EDGE | TEXT_BASIC_PARAMS::MIPMAPS;
    t->SetTextureParams();
    return t;
  };

  m_guiBtnNormalTex  = loadUITexture("GUINonPressed.png");
  m_guiBtnPressedTex = loadUITexture("GUIPressed.png");
  m_backBtnNormalTex  = loadUITexture("BackNonPressed.png");
  m_backBtnPressedTex = loadUITexture("BackPressed.png");

  // Wire button textures
  m_guiButton.texNormal  = m_guiBtnNormalTex;
  m_guiButton.texPressed = m_guiBtnPressedTex;
  m_backButton.texNormal  = m_backBtnNormalTex;
  m_backButton.texPressed = m_backBtnPressedTex;
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
  m_controlEditRect = {};
  m_controlDragActive = false;
  m_controlResizeActive = false;

  // Reset group system
  m_groups.clear();
  m_groups.push_back({"Global", {}});
  m_activeGroupIndex = 0;
  m_groupEditMode = false;
  m_popupForGroupName = false;
  m_groupEditSelectedIds.clear();
  m_groupSelector.id = "group_selector";
  m_groupSelector.name = "group_selector";
  m_groupSelector.options = {"Global"};
  m_groupSelector.selectedIndex = 0;
  m_groupSelector.justChanged = false;
  m_groupSelectorLabel.id = "group_selector_label";
  m_groupSelectorLabel.text = "Group:";
  m_groupSelectorLabel.color = XVECTOR3(0.9f, 0.85f, 0.8f);
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

  // Position group selector at top-left (above main grid)
  {
    m_groupSelectorLabel.text = "Group:";
    m_groupSelectorLabel.FitToText(tmpCtx);
    m_groupSelectorLabel.x = m_gridCellW;
    m_groupSelectorLabel.y = 2.0f;
    m_groupSelectorLabel.h = charH;

    float selX = m_groupSelectorLabel.x + m_groupSelectorLabel.w + m_layout.labelGap;
    m_groupSelector.x       = selX;
    m_groupSelector.y       = 2.0f;
    m_groupSelector.w       = m_layout.sliderW;
    m_groupSelector.h       = m_layout.sliderH;
    m_groupSelector.btnSize = m_layout.sliderH;
  }

  // Position GUI button: top-right, double FPS label height
  {
    float btnH = charH * 2.0f;
    // Maintain aspect ratio from texture
    float aspect = 1.0f;
    if (m_guiBtnNormalTex && m_guiBtnNormalTex->y > 0)
      aspect = (float)m_guiBtnNormalTex->x / (float)m_guiBtnNormalTex->y;
    float btnW = btnH * aspect;
    m_guiButton.x = screenWf - btnW - 4.0f;
    m_guiButton.y = 4.0f;
    m_guiButton.w = btnW;
    m_guiButton.h = btnH;
  }

  // Position Back button: bottom-center, double FPS label height
  {
    float btnH = charH * 2.0f;
    float aspect = 1.0f;
    if (m_backBtnNormalTex && m_backBtnNormalTex->y > 0)
      aspect = (float)m_backBtnNormalTex->x / (float)m_backBtnNormalTex->y;
    float btnW = btnH * aspect;
    m_backButton.x = (screenWf - btnW) * 0.5f;
    m_backButton.y = screenHf - btnH - 4.0f;
    m_backButton.w = btnW;
    m_backButton.h = btnH;
  }

  // Buttons are now positioned — make them visible
  m_guiButton.visible = true;
  m_backButton.visible = true;

  // Clamp all elements inside the screen
  for (auto* e : m_elements) {
    if (e->x < 0.0f)               e->x = 0.0f;
    if (e->y < 0.0f)               e->y = 0.0f;
    if (e->x + e->w > screenWf)    e->x = screenWf - e->w;
    if (e->y + e->h > screenHf)    e->y = screenHf - e->h;
  }

  ApplyControlLayoutToElements();
}

// ─── Update ─────────────────────────────────────────────────
void GUIManager::UpdateButtons(InputManager& input) {
  float mx = (float)input.mouseX;
  float my = (float)input.mouseY;
  bool mouseDown = input.MouseButtonStates[0][0];

  if (!m_visible) {
    // GUI is hidden: only the GUI button is active
    m_guiButton.UpdateInteraction(mx, my, mouseDown);
    if (m_guiButton.justClicked) {
      SetVisible(true);
    }
  } else {
    // GUI is visible: only the Back button is active (not in edit modes)
    m_guiButton.justClicked = false;
    if (!m_editMode && !m_controlEditMode && !m_groupEditMode && !m_popupActive) {
      m_backButton.UpdateInteraction(mx, my, mouseDown);
      if (m_backButton.justClicked) {
        SetVisible(false);
      }
    }
  }
}

void GUIManager::Update(InputManager& input, int screenW, int screenH) {
  if (!m_visible) return;

  float mx = (float)input.mouseX;
  float my = (float)input.mouseY;
  bool mouseDown = input.MouseButtonStates[0][0];

  // Popup (line-edit) takes priority over all other modes
  if (m_popupActive) {
    UpdatePopup(input, mx, my, mouseDown);
    m_wasMouseDown = mouseDown;
    // Don't let text input leak into next frame even if popup not active
    input.textInput.clear();
    return;
  }
  // Drop any text input accumulated while popup is not active
  input.textInput.clear();

  if (m_groupEditMode) {
    UpdateGroupEditMode(input, mx, my, mouseDown);
  } else if (m_editMode) {
    UpdateEditMode(mx, my, mouseDown);
  } else if (m_controlEditMode) {
    UpdateControlEditMode(mx, my, mouseDown);
  } else {
    // Update group selector
    m_groupSelector.UpdateInteraction(mx, my, mouseDown);
    if (m_groupSelector.justChanged) {
      SwitchToGroup(m_groupSelector.selectedIndex);
    }
    // Double-click on any label opens the line-edit popup (normal flow).
    bool justPressed = mouseDown && !m_wasMouseDown;
    if (justPressed) {
      for (auto* e : m_elements) {
        if (!e->visible) continue;
        auto* lbl = dynamic_cast<GUILabel*>(e);
        if (!lbl || lbl->isFPS) continue;
        if (!lbl->HitTest(mx, my)) continue;
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch()).count();
        const uint64_t kDblClickMs = 400;
        const float kDblClickPx = 8.0f;
        bool isDbl = (m_lastClickElement == lbl) &&
                     ((uint64_t)nowMs - m_lastClickTimeMs <= kDblClickMs) &&
                     (std::abs(mx - m_lastClickX) <= kDblClickPx) &&
                     (std::abs(my - m_lastClickY) <= kDblClickPx);
        m_lastClickTimeMs = (uint64_t)nowMs;
        m_lastClickElement = lbl;
        m_lastClickX = mx;
        m_lastClickY = my;
        if (isDbl) {
          OpenPopupFor(lbl);
          m_wasMouseDown = mouseDown;
          return;
        }
        break; // Consumed the click for dbl-click tracking
      }
    }
    // Normal slider interaction
    for (auto& sp : m_sliderPairs) {
      if (!sp.slider->visible) continue;
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
      if (!cp.checkbox->visible) continue;
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
      if (!sp.selector->visible) continue;
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
    // Build combined list: scene elements + group selector pair
    std::vector<GUIElement*> allEditable;
    allEditable.reserve(m_elements.size() + 2);
    allEditable.insert(allEditable.end(), m_elements.begin(), m_elements.end());
    allEditable.push_back(&m_groupSelectorLabel);
    allEditable.push_back(&m_groupSelector);

    // Deselect all first
    for (auto* e : allEditable) e->selected = false;

    // Check scale handles first (higher priority)
    for (int i = (int)allEditable.size() - 1; i >= 0; i--) {
      auto* e = allEditable[i];
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
    for (int i = (int)allEditable.size() - 1; i >= 0; i--) {
      auto* e = allEditable[i];
      if (!e->visible) continue;
      if (e->HitTest(mx, my)) {
        // Double-click on a label opens the text-edit popup.
        if (auto* lbl = dynamic_cast<GUILabel*>(e)) {
          auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count();
          const uint64_t kDblClickMs = 400;
          const float kDblClickPx = 8.0f;
          bool isDbl = (m_lastClickElement == e) &&
                       ((uint64_t)nowMs - m_lastClickTimeMs <= kDblClickMs) &&
                       (std::abs(mx - m_lastClickX) <= kDblClickPx) &&
                       (std::abs(my - m_lastClickY) <= kDblClickPx);
          m_lastClickTimeMs = (uint64_t)nowMs;
          m_lastClickElement = e;
          m_lastClickX = mx;
          m_lastClickY = my;
          if (isDbl) {
            OpenPopupFor(lbl);
            return;
          }
        } else {
          // Clicked a non-label: reset double-click tracking
          m_lastClickElement = nullptr;
        }
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
  T8_LOG_TRACE("[GUIManager::Draw] BEGIN (visible=%d)", (int)m_visible);

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
  m_ctx.popupBgTex             = m_popupBgTexture;
  m_ctx.popupOkTex             = m_popupOkTexture;
  m_ctx.popupOkPressedTex      = m_popupOkPressedTexture;
  m_ctx.popupCancelTex         = m_popupCancelTexture;
  m_ctx.popupCancelPressedTex  = m_popupCancelPressedTexture;
  m_ctx.screenW    = screenW;
  m_ctx.screenH    = screenH;
  m_ctx.editMode   = m_editMode;
  m_ctx.snapToGrid = m_snapToGrid;
  m_ctx.gridCellW  = m_gridCellW;
  m_ctx.gridCellH  = m_gridCellH;
  m_ctx.sliderKnobScaleX = m_controlLayout.sliderKnobScaleX;
  m_ctx.sliderKnobScaleY = m_controlLayout.sliderKnobScaleY;
  m_ctx.sliderKnobOffsetX = m_controlLayout.sliderKnobOffsetX;
  m_ctx.sliderKnobOffsetY = m_controlLayout.sliderKnobOffsetY;
  m_ctx.selectorLeftScaleX = m_controlLayout.selectorLeftScaleX;
  m_ctx.selectorLeftScaleY = m_controlLayout.selectorLeftScaleY;
  m_ctx.selectorLeftOffsetX = m_controlLayout.selectorLeftOffsetX;
  m_ctx.selectorLeftOffsetY = m_controlLayout.selectorLeftOffsetY;
  m_ctx.selectorRightScaleX = m_controlLayout.selectorRightScaleX;
  m_ctx.selectorRightScaleY = m_controlLayout.selectorRightScaleY;
  m_ctx.selectorRightOffsetX = m_controlLayout.selectorRightOffsetX;
  m_ctx.selectorRightOffsetY = m_controlLayout.selectorRightOffsetY;
  m_ctx.checkboxMarkScaleX = m_controlLayout.checkboxMarkScaleX;
  m_ctx.checkboxMarkScaleY = m_controlLayout.checkboxMarkScaleY;
  m_ctx.checkboxMarkOffsetX = m_controlLayout.checkboxMarkOffsetX;
  m_ctx.checkboxMarkOffsetY = m_controlLayout.checkboxMarkOffsetY;
  m_ctx.popupBgScaleX    = m_controlLayout.popupBgScaleX;
  m_ctx.popupBgScaleY    = m_controlLayout.popupBgScaleY;
  m_ctx.popupOkScaleX    = m_controlLayout.popupOkScaleX;
  m_ctx.popupOkScaleY    = m_controlLayout.popupOkScaleY;
  m_ctx.popupOkOffsetX   = m_controlLayout.popupOkOffsetX;
  m_ctx.popupOkOffsetY   = m_controlLayout.popupOkOffsetY;
  m_ctx.popupCancelScaleX  = m_controlLayout.popupCancelScaleX;
  m_ctx.popupCancelScaleY  = m_controlLayout.popupCancelScaleY;
  m_ctx.popupCancelOffsetX = m_controlLayout.popupCancelOffsetX;
  m_ctx.popupCancelOffsetY = m_controlLayout.popupCancelOffsetY;
  m_ctx.popupTextScaleX  = m_controlLayout.popupTextScaleX;
  m_ctx.popupTextScaleY  = m_controlLayout.popupTextScaleY;

  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::NONE);

  if (m_controlEditMode) {
    DrawControlEditPreview();
    DrawControlEditOverlay();
    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::BLEND_DEFAULT);
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::DEPTH_DEFAULT);
    return;
  }

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

  // Draw group selector quads (always visible)
  m_groupSelector.DrawQuadsOnly(m_ctx);

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

  // Group selector text (always visible)
  m_groupSelector.DrawTextBatched(m_ctx);
  m_groupSelectorLabel.Draw(m_ctx);

  m_textRenderer.EndBatch();

  // Back button (visible when GUI is shown, not in edit modes)
  if (!m_editMode && !m_controlEditMode && !m_groupEditMode) {
    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::NONE);
    m_quad.Set();
    m_shader->Set(*T8DeviceContext);
    T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
    m_backButton.Draw(m_ctx);
  }

  // Draw edit overlays on top
  if (m_editMode) {
    DrawEditOverlays();
  }

  // Draw group-edit highlights on top
  if (m_groupEditMode) {
    DrawGroupEditHighlights();
  }

  // Line-edit popup draws on top of everything
  if (m_popupActive) {
    DrawPopup();
  }

  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::BLEND_DEFAULT);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::DEPTH_DEFAULT);
}

void GUIManager::DrawFPSOnly() {
  if (!m_initialized || !m_fpsLabel || !m_fpsLabel->visible) return;
  T8_LOG_TRACE("[GUIManager::DrawFPSOnly] BEGIN");

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

  // GUI button (visible when GUI overlay is hidden)
  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::NONE);
  m_quad.Set();
  m_shader->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
  m_guiButton.Draw(m_ctx);

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

void GUIManager::DrawControlEditPreview() {
  // Ensure alpha blend is enabled so textures with transparent regions composite correctly.
  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::NONE);

  m_quad.Set();
  m_shader->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);

  float screenW = m_ctx.screenW;
  float screenH = m_ctx.screenH;

  // Draw a neutral full-screen parent bounds reference first.
  XVECTOR3 neutral(0.12f, 0.12f, 0.12f);
  m_ctx.DrawSolidQuad(0.0f, 0.0f, screenW, screenH, neutral);

  // Grid over neutral background for alignment in control-edit mode.
  DrawGrid();

  auto fitRect = [&](float srcW, float srcH) {
    float safeW = (std::max)(1.0f, srcW);
    float safeH = (std::max)(1.0f, srcH);
    float targetW = screenW * m_controlPreviewVisualScale;
    float targetH = screenH * m_controlPreviewVisualScale;
    float s = (std::min)(targetW / safeW, targetH / safeH);
    float w = safeW * s;
    float h = safeH * s;
    float x = (screenW - w) * 0.5f;
    float y = (screenH - h) * 0.5f;
    return ControlEditRect{ x, y, w, h, true };
  };

  auto mapRectToPreview = [&](const ControlEditRect& r, const ControlEditRect& p) {
    ControlEditRect out;
    out.valid = r.valid && p.valid;
    if (!out.valid) return out;
    out.x = p.x + (r.x / screenW) * p.w;
    out.y = p.y + (r.y / screenH) * p.h;
    out.w = (r.w / screenW) * p.w;
    out.h = (r.h / screenH) * p.h;
    return out;
  };

  switch (m_controlEditTarget) {
    case GUIControlEditTarget::SliderKnob: {
      // Parent bar in original texture aspect ratio.
      float barW = m_barTexture ? (float)m_barTexture->x : 256.0f;
      float barH = m_barTexture ? (float)m_barTexture->y : 32.0f;
      ControlEditRect parent = fitRect(barW, barH);
      if (m_barTexture) {
        m_ctx.DrawTexturedQuad(parent.x, parent.y, parent.w, parent.h, m_barTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
      }
      if (m_knobTexture && m_controlEditRect.valid) {
        m_ctx.DrawTexturedQuad(m_controlEditRect.x, m_controlEditRect.y, m_controlEditRect.w, m_controlEditRect.h,
                               m_knobTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
      }
      break;
    }
    case GUIControlEditTarget::SelectorControl: {
      // Parent selector in original aspect ratio.  Bar = full parent rect.
      float sbw = m_selectorBarTexture ? (float)m_selectorBarTexture->x : 256.0f;
      float sbh = m_selectorBarTexture ? (float)m_selectorBarTexture->y : 32.0f;
      float blw = m_selectorBtnLeftTexture ? (float)m_selectorBtnLeftTexture->x : 32.0f;
      float brw = m_selectorBtnRightTexture ? (float)m_selectorBtnRightTexture->x : 32.0f;
      float sw = sbw + blw + brw;
      float sh = sbh;
      ControlEditRect parent = fitRect(sw, sh);

      // Bar fills the full parent rect
      if (m_selectorBarTexture) {
        m_ctx.DrawTexturedQuad(parent.x, parent.y, parent.w, parent.h, m_selectorBarTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
      }

      if (m_selectorBtnLeftTexture && m_controlEditRect.valid) {
        if (m_controlActiveSubpart == GUIControlSubpart::SelectorLeft) {
          m_ctx.DrawTexturedQuad(m_controlEditRect.x, m_controlEditRect.y, m_controlEditRect.w, m_controlEditRect.h,
                                 m_selectorBtnLeftTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
        } else {
          m_ctx.DrawTexturedQuad(m_controlEditRectSecondary.x, m_controlEditRectSecondary.y,
                                 m_controlEditRectSecondary.w, m_controlEditRectSecondary.h,
                                 m_selectorBtnLeftTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
        }
      }
      if (m_selectorBtnRightTexture && m_controlEditRect.valid) {
        if (m_controlActiveSubpart == GUIControlSubpart::SelectorRight) {
          m_ctx.DrawTexturedQuad(m_controlEditRect.x, m_controlEditRect.y, m_controlEditRect.w, m_controlEditRect.h,
                                 m_selectorBtnRightTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
        } else {
          m_ctx.DrawTexturedQuad(m_controlEditRectSecondary.x, m_controlEditRectSecondary.y,
                                 m_controlEditRectSecondary.w, m_controlEditRectSecondary.h,
                                 m_selectorBtnRightTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
        }
      }
      break;
    }
    case GUIControlEditTarget::CheckboxMark: {
      // Parent checkbox box in original aspect ratio.
      float cbW = m_checkBoxTexture ? (float)m_checkBoxTexture->x : 64.0f;
      float cbH = m_checkBoxTexture ? (float)m_checkBoxTexture->y : 64.0f;
      ControlEditRect parent = fitRect(cbW, cbH);
      if (m_checkBoxTexture) {
        m_ctx.DrawTexturedQuad(parent.x, parent.y, parent.w, parent.h, m_checkBoxTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
      }
      if (m_checkMarkTexture && m_controlEditRect.valid) {
        m_ctx.DrawTexturedQuad(m_controlEditRect.x, m_controlEditRect.y, m_controlEditRect.w, m_controlEditRect.h,
                               m_checkMarkTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
      }
      break;
    }
    case GUIControlEditTarget::LineEditControl: {
      // Render the popup preview with current layout. The active subpart rect is drawn via DrawControlEditOverlay.
      float bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH;
      GetPopupRects(bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH);
      if (m_popupBgTexture)
        m_ctx.DrawTexturedQuad(bgX, bgY, bgW, bgH, m_popupBgTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
      if (m_popupOkTexture)
        m_ctx.DrawTexturedQuad(okX, okY, okW, okH, m_popupOkTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
      if (m_popupCancelTexture)
        m_ctx.DrawTexturedQuad(caX, caY, caW, caH, m_popupCancelTexture, XVECTOR3(1.0f, 1.0f, 1.0f));
      // Example text centered on bg (matches runtime text placement).
      if (m_ctx.text) {
        int sw = (int)m_ctx.screenW;
        int sh = (int)m_ctx.screenH;
        float charH = m_textRenderer.m_fontSize * m_ctx.screenH / (float)m_textRenderer.m_textureSize;
        float targetH = bgH * m_controlLayout.popupTextScaleY * 0.25f;
        float scaleY = (charH > 0.0f) ? targetH / charH : 1.0f;
        float scaleX = scaleY * m_controlLayout.popupTextScaleX / (std::max)(0.0001f, m_controlLayout.popupTextScaleY);
        std::string exemplar = "Example";
        float textW = m_textRenderer.MeasurePixel(exemplar, sw, sh) * scaleX;
        float textX = bgX + (bgW - textW) * 0.5f;
        float textY = bgY + (bgH - targetH) * 0.5f - bgH * 0.15f;
        m_textRenderer.DrawPixelScaled(textX, textY, scaleX, scaleY, sw, sh, XVECTOR3(1.0f, 1.0f, 1.0f), exemplar);
      }
      break;
    }
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
  // Group selector pair
  m_groupSelectorLabel.DrawEditOverlay(m_ctx);
  m_groupSelector.DrawEditOverlay(m_ctx);
}

void GUIManager::SetControlEditMode(bool e) {
  m_controlEditMode = e;
  if (m_controlEditMode) {
    m_editMode = false;
    m_controlDragActive = false;
    m_controlResizeActive = false;
    m_controlActiveSubpart = GUIControlSubpart::Primary;
    m_sliderCalibrationStep = 0;
  }
}

void GUIManager::SetEditMode(bool e) {
  m_editMode = e;
  if (m_editMode) {
    m_controlEditMode = false;
    m_controlDragActive = false;
    m_controlResizeActive = false;
  }
}

bool GUIManager::SetControlEditTargetByName(const std::string& targetName) {
  if (targetName == "slider_knob") {
    m_controlEditTarget = GUIControlEditTarget::SliderKnob;
    m_controlActiveSubpart = GUIControlSubpart::Primary;
    m_sliderCalibrationStep = 0;
    return true;
  }
  if (targetName == "selector_control" || targetName == "selector_left" || targetName == "selector_right") {
    m_controlEditTarget = GUIControlEditTarget::SelectorControl;
    m_controlActiveSubpart = (targetName == "selector_right") ? GUIControlSubpart::SelectorRight : GUIControlSubpart::SelectorLeft;
    m_sliderCalibrationStep = 0;
    return true;
  }
  if (targetName == "checkbox_mark") {
    m_controlEditTarget = GUIControlEditTarget::CheckboxMark;
    m_controlActiveSubpart = GUIControlSubpart::Primary;
    m_sliderCalibrationStep = 0;
    return true;
  }
  if (targetName == "linedit_popup" || targetName == "linedit_control" || targetName == "linedit_ok" ||
      targetName == "linedit_cancel" || targetName == "linedit_text") {
    m_controlEditTarget = GUIControlEditTarget::LineEditControl;
    // All popup parts share one edit session; start on background.
    m_controlActiveSubpart = GUIControlSubpart::Primary;
    m_sliderCalibrationStep = 0;
    return true;
  }
  return false;
}

const char* GUIManager::GetControlEditTargetName() const {
  switch (m_controlEditTarget) {
    case GUIControlEditTarget::SliderKnob: return "slider_knob";
    case GUIControlEditTarget::SelectorControl: return "selector_control";
    case GUIControlEditTarget::CheckboxMark: return "checkbox_mark";
    case GUIControlEditTarget::LineEditControl: return "linedit_popup";
    default: return "slider_knob";
  }
}

void GUIManager::ApplyControlLayoutToElements() {
  for (auto& sp : m_sliderPairs) {
    sp.slider->knobScaleX = m_controlLayout.sliderKnobScaleX;
    sp.slider->knobScaleY = m_controlLayout.sliderKnobScaleY;
    sp.slider->knobOffsetX = m_controlLayout.sliderKnobOffsetX;
    sp.slider->knobOffsetY = m_controlLayout.sliderKnobOffsetY;
    sp.slider->knobRangeCalibrated = m_controlLayout.sliderKnobRangeCalibrated;
    sp.slider->knobMinNorm = m_controlLayout.sliderKnobMinNorm;
    sp.slider->knobMaxNorm = m_controlLayout.sliderKnobMaxNorm;
  }
  for (auto& cp : m_checkboxPairs) {
    cp.checkbox->markScaleX = m_controlLayout.checkboxMarkScaleX;
    cp.checkbox->markScaleY = m_controlLayout.checkboxMarkScaleY;
    cp.checkbox->markOffsetX = m_controlLayout.checkboxMarkOffsetX;
    cp.checkbox->markOffsetY = m_controlLayout.checkboxMarkOffsetY;
  }
  for (auto& sp : m_selectorPairs) {
    sp.selector->leftScaleX = m_controlLayout.selectorLeftScaleX;
    sp.selector->leftScaleY = m_controlLayout.selectorLeftScaleY;
    sp.selector->leftOffsetX = m_controlLayout.selectorLeftOffsetX;
    sp.selector->leftOffsetY = m_controlLayout.selectorLeftOffsetY;
    sp.selector->rightScaleX = m_controlLayout.selectorRightScaleX;
    sp.selector->rightScaleY = m_controlLayout.selectorRightScaleY;
    sp.selector->rightOffsetX = m_controlLayout.selectorRightOffsetX;
    sp.selector->rightOffsetY = m_controlLayout.selectorRightOffsetY;
  }
  // Also apply to the group selector
  m_groupSelector.leftScaleX  = m_controlLayout.selectorLeftScaleX;
  m_groupSelector.leftScaleY  = m_controlLayout.selectorLeftScaleY;
  m_groupSelector.leftOffsetX = m_controlLayout.selectorLeftOffsetX;
  m_groupSelector.leftOffsetY = m_controlLayout.selectorLeftOffsetY;
  m_groupSelector.rightScaleX  = m_controlLayout.selectorRightScaleX;
  m_groupSelector.rightScaleY  = m_controlLayout.selectorRightScaleY;
  m_groupSelector.rightOffsetX = m_controlLayout.selectorRightOffsetX;
  m_groupSelector.rightOffsetY = m_controlLayout.selectorRightOffsetY;
}

void GUIManager::UpdateControlEditMode(float mx, float my, bool mouseDown) {
  m_controlEditRect.valid = false;
  m_controlEditRectSecondary.valid = false;
  float screenW = m_ctx.screenW > 0.0f ? m_ctx.screenW : (float)g_pBaseDriver->width;
  float screenH = m_ctx.screenH > 0.0f ? m_ctx.screenH : (float)g_pBaseDriver->height;

  auto fitRect = [&](float srcW, float srcH) {
    float safeW = (std::max)(1.0f, srcW);
    float safeH = (std::max)(1.0f, srcH);
    float targetW = screenW * m_controlPreviewVisualScale;
    float targetH = screenH * m_controlPreviewVisualScale;
    float s = (std::min)(targetW / safeW, targetH / safeH);
    float w = safeW * s;
    float h = safeH * s;
    float x = (screenW - w) * 0.5f;
    float y = (screenH - h) * 0.5f;
    return ControlEditRect{ x, y, w, h, true };
  };

  auto updateFromRect = [&]() {
    if (!m_controlEditRect.valid) return;
    float partW = (std::max)(1.0f, m_controlEditRect.w);
    float partH = (std::max)(1.0f, m_controlEditRect.h);
    float cx = (screenW - partW) * 0.5f;
    float cy = (screenH - partH) * 0.5f;

    switch (m_controlEditTarget) {
      case GUIControlEditTarget::SliderKnob: {
        float barW = m_barTexture ? (float)m_barTexture->x : 256.0f;
        float barH = m_barTexture ? (float)m_barTexture->y : 32.0f;
        ControlEditRect parent = fitRect(barW, barH);
        float baseW = parent.h;
        float baseX = parent.x + 0.5f * (std::max)(0.0f, parent.w - baseW);
        m_controlLayout.sliderKnobScaleX = partW / (std::max)(1.0f, baseW);
        m_controlLayout.sliderKnobScaleY = partH / (std::max)(1.0f, parent.h);
        m_controlLayout.sliderKnobOffsetX = (m_controlEditRect.x - baseX - (baseW - partW) * 0.5f) / (std::max)(1.0f, parent.h);
        m_controlLayout.sliderKnobOffsetY = (m_controlEditRect.y - parent.y - (parent.h - partH) * 0.5f) / (std::max)(1.0f, parent.h);
        break;
      }
      case GUIControlEditTarget::SelectorControl: {
        float sbw = m_selectorBarTexture ? (float)m_selectorBarTexture->x : 256.0f;
        float sbh = m_selectorBarTexture ? (float)m_selectorBarTexture->y : 32.0f;
        float blw = m_selectorBtnLeftTexture ? (float)m_selectorBtnLeftTexture->x : 32.0f;
        float brw = m_selectorBtnRightTexture ? (float)m_selectorBtnRightTexture->x : 32.0f;
        ControlEditRect parent = fitRect(sbw + blw + brw, sbh);
        float baseW = parent.h;
        float baseY = parent.y;
        if (m_controlActiveSubpart == GUIControlSubpart::SelectorRight) {
          float baseX = parent.x + parent.w - baseW;
          m_controlLayout.selectorRightScaleX = partW / (std::max)(1.0f, baseW);
          m_controlLayout.selectorRightScaleY = partH / (std::max)(1.0f, parent.h);
          m_controlLayout.selectorRightOffsetX = (m_controlEditRect.x - baseX - (baseW - partW) * 0.5f) / (std::max)(1.0f, parent.h);
          m_controlLayout.selectorRightOffsetY = (m_controlEditRect.y - baseY - (parent.h - partH) * 0.5f) / (std::max)(1.0f, parent.h);
        } else {
          float baseX = parent.x;
          m_controlLayout.selectorLeftScaleX = partW / (std::max)(1.0f, baseW);
          m_controlLayout.selectorLeftScaleY = partH / (std::max)(1.0f, parent.h);
          m_controlLayout.selectorLeftOffsetX = (m_controlEditRect.x - baseX - (baseW - partW) * 0.5f) / (std::max)(1.0f, parent.h);
          m_controlLayout.selectorLeftOffsetY = (m_controlEditRect.y - baseY - (parent.h - partH) * 0.5f) / (std::max)(1.0f, parent.h);
        }
        break;
      }
      case GUIControlEditTarget::CheckboxMark: {
        float cbW = m_checkBoxTexture ? (float)m_checkBoxTexture->x : 64.0f;
        float cbH = m_checkBoxTexture ? (float)m_checkBoxTexture->y : 64.0f;
        ControlEditRect parent = fitRect(cbW, cbH);
        m_controlLayout.checkboxMarkScaleX = partW / (std::max)(1.0f, parent.w);
        m_controlLayout.checkboxMarkScaleY = partH / (std::max)(1.0f, parent.h);
        m_controlLayout.checkboxMarkOffsetX = (m_controlEditRect.x - parent.x - (parent.w - partW) * 0.5f) / (std::max)(1.0f, parent.h);
        m_controlLayout.checkboxMarkOffsetY = (m_controlEditRect.y - parent.y - (parent.h - partH) * 0.5f) / (std::max)(1.0f, parent.h);
        break;
      }
      case GUIControlEditTarget::LineEditControl: {
        // Need current popup geometry to back out normalized values
        float bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH;
        GetPopupRects(bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH);
        float bgCX = bgX + bgW * 0.5f;
        float bgCY = bgY + bgH * 0.5f;
        switch (m_controlActiveSubpart) {
          case GUIControlSubpart::PopupOk: {
            // Keep the OK button's native aspect ratio when scaling.
            float baseH = bgH * 0.25f; // baseline
            m_controlLayout.popupOkScaleY = partH / (std::max)(1.0f, baseH);
            float srcW = m_popupOkTexture ? (float)m_popupOkTexture->x : 100.0f;
            float srcH = m_popupOkTexture ? (float)m_popupOkTexture->y : 40.0f;
            float implWFromY = (srcH > 0.0f ? (srcW / srcH) : 2.5f) * partH;
            m_controlLayout.popupOkScaleX = m_controlLayout.popupOkScaleY * (partW / (std::max)(1.0f, implWFromY));
            float centerX = m_controlEditRect.x + partW * 0.5f;
            float centerY = m_controlEditRect.y + partH * 0.5f;
            m_controlLayout.popupOkOffsetX = (centerX - bgCX) / (std::max)(1.0f, bgH);
            m_controlLayout.popupOkOffsetY = (centerY - bgCY) / (std::max)(1.0f, bgH);
            break;
          }
          case GUIControlSubpart::PopupCancel: {
            float baseH = bgH * 0.25f;
            m_controlLayout.popupCancelScaleY = partH / (std::max)(1.0f, baseH);
            float srcW = m_popupCancelTexture ? (float)m_popupCancelTexture->x : 100.0f;
            float srcH = m_popupCancelTexture ? (float)m_popupCancelTexture->y : 40.0f;
            float implWFromY = (srcH > 0.0f ? (srcW / srcH) : 2.5f) * partH;
            m_controlLayout.popupCancelScaleX = m_controlLayout.popupCancelScaleY * (partW / (std::max)(1.0f, implWFromY));
            float centerX = m_controlEditRect.x + partW * 0.5f;
            float centerY = m_controlEditRect.y + partH * 0.5f;
            m_controlLayout.popupCancelOffsetX = (centerX - bgCX) / (std::max)(1.0f, bgH);
            m_controlLayout.popupCancelOffsetY = (centerY - bgCY) / (std::max)(1.0f, bgH);
            break;
          }
          case GUIControlSubpart::PopupText: {
            float baseH = bgH * 0.25f;
            m_controlLayout.popupTextScaleY = partH / (std::max)(1.0f, baseH);
            // Match X scale to rect width, using the CURRENT scaleY (partH/charH) so that
            // width/height can be adjusted independently without cross-coupling.
            float charH = m_textRenderer.m_fontSize * m_ctx.screenH / (float)m_textRenderer.m_textureSize;
            float scaleY = (charH > 0.0f) ? partH / charH : 1.0f;
            float natW = m_textRenderer.MeasurePixel("Example", (int)m_ctx.screenW, (int)m_ctx.screenH) * scaleY;
            m_controlLayout.popupTextScaleX = m_controlLayout.popupTextScaleY * (partW / (std::max)(1.0f, natW));
            break;
          }
          default: {
            // Primary: edit background size.
            float baseH = (m_ctx.screenH > 0.0f ? m_ctx.screenH : (float)g_pBaseDriver->height) * 0.30f;
            m_controlLayout.popupBgScaleY = partH / (std::max)(1.0f, baseH);
            float srcW = m_popupBgTexture ? (float)m_popupBgTexture->x : 400.0f;
            float srcH = m_popupBgTexture ? (float)m_popupBgTexture->y : 160.0f;
            float implWFromY = (srcH > 0.0f ? (srcW / srcH) : 2.5f) * partH;
            m_controlLayout.popupBgScaleX = m_controlLayout.popupBgScaleY * (partW / (std::max)(1.0f, implWFromY));
            break;
          }
        }
        break;
      }
    }
    ApplyControlLayoutToElements();
  };

  auto computeRect = [&]() {
    switch (m_controlEditTarget) {
      case GUIControlEditTarget::SliderKnob: {
        float barW = m_barTexture ? (float)m_barTexture->x : 256.0f;
        float barH = m_barTexture ? (float)m_barTexture->y : 32.0f;
        ControlEditRect parent = fitRect(barW, barH);
        float baseW = parent.h;
        float baseX = parent.x + 0.5f * (std::max)(0.0f, parent.w - baseW);
        float kW = (std::max)(4.0f, baseW * m_controlLayout.sliderKnobScaleX);
        float kH = (std::max)(4.0f, parent.h * m_controlLayout.sliderKnobScaleY);
        m_controlEditRect.x = baseX + m_controlLayout.sliderKnobOffsetX * parent.h + (baseW - kW) * 0.5f;
        m_controlEditRect.y = parent.y + m_controlLayout.sliderKnobOffsetY * parent.h + (parent.h - kH) * 0.5f;
        m_controlEditRect.w = kW;
        m_controlEditRect.h = kH;
        m_controlEditRect.valid = true;
        return;
      }
      case GUIControlEditTarget::SelectorControl: {
        float sbw = m_selectorBarTexture ? (float)m_selectorBarTexture->x : 256.0f;
        float sbh = m_selectorBarTexture ? (float)m_selectorBarTexture->y : 32.0f;
        float blw = m_selectorBtnLeftTexture ? (float)m_selectorBtnLeftTexture->x : 32.0f;
        float brw = m_selectorBtnRightTexture ? (float)m_selectorBtnRightTexture->x : 32.0f;
        ControlEditRect parent = fitRect(sbw + blw + brw, sbh);
        float baseW = parent.h;
        float baseY = parent.y;

        float lW = (std::max)(4.0f, baseW * m_controlLayout.selectorLeftScaleX);
        float lH = (std::max)(4.0f, parent.h * m_controlLayout.selectorLeftScaleY);
        float lX = parent.x + m_controlLayout.selectorLeftOffsetX * parent.h + (baseW - lW) * 0.5f;
        float lY = baseY + m_controlLayout.selectorLeftOffsetY * parent.h + (parent.h - lH) * 0.5f;

        float rW = (std::max)(4.0f, baseW * m_controlLayout.selectorRightScaleX);
        float rH = (std::max)(4.0f, parent.h * m_controlLayout.selectorRightScaleY);
        float rX = (parent.x + parent.w - baseW) + m_controlLayout.selectorRightOffsetX * parent.h + (baseW - rW) * 0.5f;
        float rY = baseY + m_controlLayout.selectorRightOffsetY * parent.h + (parent.h - rH) * 0.5f;

        if (m_controlActiveSubpart == GUIControlSubpart::SelectorRight) {
          m_controlEditRect = { rX, rY, rW, rH, true };
          m_controlEditRectSecondary = { lX, lY, lW, lH, true };
        } else {
          m_controlEditRect = { lX, lY, lW, lH, true };
          m_controlEditRectSecondary = { rX, rY, rW, rH, true };
        }
        return;
      }
      case GUIControlEditTarget::CheckboxMark: {
        float cbW = m_checkBoxTexture ? (float)m_checkBoxTexture->x : 64.0f;
        float cbH = m_checkBoxTexture ? (float)m_checkBoxTexture->y : 64.0f;
        ControlEditRect parent = fitRect(cbW, cbH);
        float mW = (std::max)(4.0f, parent.w * m_controlLayout.checkboxMarkScaleX);
        float mH = (std::max)(4.0f, parent.h * m_controlLayout.checkboxMarkScaleY);
        m_controlEditRect.x = parent.x + m_controlLayout.checkboxMarkOffsetX * parent.h + (parent.w - mW) * 0.5f;
        m_controlEditRect.y = parent.y + m_controlLayout.checkboxMarkOffsetY * parent.h + (parent.h - mH) * 0.5f;
        m_controlEditRect.w = mW;
        m_controlEditRect.h = mH;
        m_controlEditRect.valid = true;
        return;
      }
      case GUIControlEditTarget::LineEditControl: {
        float bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH;
        GetPopupRects(bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH);
        switch (m_controlActiveSubpart) {
          case GUIControlSubpart::PopupOk:
            m_controlEditRect = { okX, okY, okW, okH, true };
            break;
          case GUIControlSubpart::PopupCancel:
            m_controlEditRect = { caX, caY, caW, caH, true };
            break;
          case GUIControlSubpart::PopupText: {
            // Represent text rect as the baseline example text area
            int sw = (int)m_ctx.screenW;
            int sh = (int)m_ctx.screenH;
            float charH = m_textRenderer.m_fontSize * m_ctx.screenH / (float)m_textRenderer.m_textureSize;
            float targetH = bgH * m_controlLayout.popupTextScaleY * 0.25f;
            float scaleY = (charH > 0.0f) ? targetH / charH : 1.0f;
            float scaleX = scaleY * m_controlLayout.popupTextScaleX / (std::max)(0.0001f, m_controlLayout.popupTextScaleY);
            float natW = m_textRenderer.MeasurePixel("Example", sw, sh) * scaleX;
            float tW = (std::max)(8.0f, natW);
            float tH = (std::max)(4.0f, targetH);
            m_controlEditRect = { bgX + (bgW - tW) * 0.5f,
                                  bgY + (bgH - tH) * 0.5f - bgH * 0.15f,
                                  tW, tH, true };
            break;
          }
          default:
            m_controlEditRect = { bgX, bgY, bgW, bgH, true };
            break;
        }
        return;
      }
    }
  };

  computeRect();
  if (!m_controlEditRect.valid) {
    return;
  }

  bool justPressed = mouseDown && !m_wasMouseDown;
  bool justReleased = !mouseDown && m_wasMouseDown;

  float hs = GUIElement::kScaleHandleSize;
  float hx = m_controlEditRect.x + m_controlEditRect.w - hs;
  float hy = m_controlEditRect.y + m_controlEditRect.h - hs;
  bool overHandle = (mx >= hx && mx <= hx + hs && my >= hy && my <= hy + hs);
  bool overBody = (mx >= m_controlEditRect.x && mx <= m_controlEditRect.x + m_controlEditRect.w &&
                   my >= m_controlEditRect.y && my <= m_controlEditRect.y + m_controlEditRect.h);
  bool overSecondary = m_controlEditRectSecondary.valid &&
                       (mx >= m_controlEditRectSecondary.x && mx <= m_controlEditRectSecondary.x + m_controlEditRectSecondary.w &&
                        my >= m_controlEditRectSecondary.y && my <= m_controlEditRectSecondary.y + m_controlEditRectSecondary.h);

  if (justReleased) {
    if (m_snapToGrid) {
      m_controlEditRect.x = std::round(m_controlEditRect.x / m_gridCellW) * m_gridCellW;
      m_controlEditRect.y = std::round(m_controlEditRect.y / m_gridCellH) * m_gridCellH;
      updateFromRect();
    }
    m_controlDragActive = false;
    m_controlResizeActive = false;
    // Popup text can grow arbitrarily; ensure the font atlas is baked at sufficient resolution.
    if (m_controlEditTarget == GUIControlEditTarget::LineEditControl &&
        m_controlActiveSubpart == GUIControlSubpart::PopupText) {
      RebakeFontIfNeeded();
    }
    return;
  }

  if (mouseDown && m_controlResizeActive) {
    float dw = mx - m_controlResizeOrigMX;
    float dh = my - m_controlResizeOrigMY;
    m_controlEditRect.w = (std::max)(4.0f, m_controlResizeOrigW + dw);
    m_controlEditRect.h = (std::max)(4.0f, m_controlResizeOrigH + dh);
    m_controlEditRect.x = m_controlResizeOrigX;
    m_controlEditRect.y = m_controlResizeOrigY;
    updateFromRect();
    return;
  }

  if (mouseDown && m_controlDragActive) {
    m_controlEditRect.x = mx - m_controlDragOffX;
    m_controlEditRect.y = my - m_controlDragOffY;
    if (m_snapToGrid) {
      m_controlEditRect.x = std::round(m_controlEditRect.x / m_gridCellW) * m_gridCellW;
      m_controlEditRect.y = std::round(m_controlEditRect.y / m_gridCellH) * m_gridCellH;
    }
    updateFromRect();
    return;
  }

  if (justPressed) {
    if (m_controlEditTarget == GUIControlEditTarget::SelectorControl && overSecondary) {
      m_controlActiveSubpart = (m_controlActiveSubpart == GUIControlSubpart::SelectorLeft)
                                 ? GUIControlSubpart::SelectorRight
                                 : GUIControlSubpart::SelectorLeft;
      computeRect();
      overHandle = false;
      overBody = true;
    }

    // Line-edit popup: pick whichever subpart was clicked (bg/ok/cancel/text).
    if (m_controlEditTarget == GUIControlEditTarget::LineEditControl) {
      float bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH;
      GetPopupRects(bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH);

      int sw = (int)m_ctx.screenW;
      int sh = (int)m_ctx.screenH;
      float charH = m_textRenderer.m_fontSize * m_ctx.screenH / (float)m_textRenderer.m_textureSize;
      float targetH = bgH * m_controlLayout.popupTextScaleY * 0.25f;
      float scaleY = (charH > 0.0f) ? targetH / charH : 1.0f;
      float scaleX = scaleY * m_controlLayout.popupTextScaleX / (std::max)(0.0001f, m_controlLayout.popupTextScaleY);
      float natW = m_textRenderer.MeasurePixel("Example", sw, sh) * scaleX;
      float tW = (std::max)(8.0f, natW);
      float tH = (std::max)(4.0f, targetH);
      float tX = bgX + (bgW - tW) * 0.5f;
      float tY = bgY + (bgH - tH) * 0.5f - bgH * 0.15f;

      auto hit = [&](float x, float y, float w, float h) {
        return (mx >= x && mx <= x + w && my >= y && my <= y + h);
      };

      // Priority: smaller rects first so they win over the background they sit on.
      GUIControlSubpart picked = m_controlActiveSubpart;
      if      (hit(okX, okY, okW, okH))                 picked = GUIControlSubpart::PopupOk;
      else if (hit(caX, caY, caW, caH))                 picked = GUIControlSubpart::PopupCancel;
      else if (hit(tX, tY, tW, tH))                     picked = GUIControlSubpart::PopupText;
      else if (hit(bgX, bgY, bgW, bgH))                 picked = GUIControlSubpart::Primary;

      if (picked != m_controlActiveSubpart) {
        m_controlActiveSubpart = picked;
        computeRect();
        // Recompute handle/body for the new active rect.
        hx = m_controlEditRect.x + m_controlEditRect.w - hs;
        hy = m_controlEditRect.y + m_controlEditRect.h - hs;
        overHandle = (mx >= hx && mx <= hx + hs && my >= hy && my <= hy + hs);
        overBody = (mx >= m_controlEditRect.x && mx <= m_controlEditRect.x + m_controlEditRect.w &&
                    my >= m_controlEditRect.y && my <= m_controlEditRect.y + m_controlEditRect.h);
      }
    }

    if (overHandle) {
      m_controlResizeActive = true;
      m_controlResizeOrigW = m_controlEditRect.w;
      m_controlResizeOrigH = m_controlEditRect.h;
      m_controlResizeOrigMX = mx;
      m_controlResizeOrigMY = my;
      m_controlResizeOrigX = m_controlEditRect.x;
      m_controlResizeOrigY = m_controlEditRect.y;
      return;
    }
    if (overBody) {
      m_controlDragActive = true;
      m_controlDragOffX = mx - m_controlEditRect.x;
      m_controlDragOffY = my - m_controlEditRect.y;
      return;
    }
  }
}

void GUIManager::DrawControlEditRectOverlay(float x, float y, float w, float h, const XVECTOR3& color, bool drawHandle) {
  const float lineW = 1.0f;
  m_ctx.DrawSolidQuad(x, y, w, lineW, color);
  m_ctx.DrawSolidQuad(x, y + h - lineW, w, lineW, color);
  m_ctx.DrawSolidQuad(x, y, lineW, h, color);
  m_ctx.DrawSolidQuad(x + w - lineW, y, lineW, h, color);

  if (drawHandle) {
    float hs = GUIElement::kScaleHandleSize;
    XVECTOR3 handleColor(0.1f, 1.0f, 0.2f);
    m_ctx.DrawSolidQuad(x + w - hs, y + h - hs, hs, hs, handleColor);
  }
}

void GUIManager::DrawControlEditOverlay() {
  if (!m_controlEditRect.valid) return;

  m_quad.Set();
  m_shader->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
  XVECTOR3 activeColor(1.0f, 0.6f, 0.1f);
  XVECTOR3 secondaryColor(0.2f, 0.8f, 1.0f);

  // For the popup, draw all four subpart outlines; only the active one gets the handle.
  if (m_controlEditTarget == GUIControlEditTarget::LineEditControl) {
    float bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH;
    GetPopupRects(bgX, bgY, bgW, bgH, okX, okY, okW, okH, caX, caY, caW, caH);

    int sw = (int)m_ctx.screenW;
    int sh = (int)m_ctx.screenH;
    float charH = m_textRenderer.m_fontSize * m_ctx.screenH / (float)m_textRenderer.m_textureSize;
    float targetH = bgH * m_controlLayout.popupTextScaleY * 0.25f;
    float scaleY = (charH > 0.0f) ? targetH / charH : 1.0f;
    float scaleX = scaleY * m_controlLayout.popupTextScaleX / (std::max)(0.0001f, m_controlLayout.popupTextScaleY);
    float natW = m_textRenderer.MeasurePixel("Example", sw, sh) * scaleX;
    float tW = (std::max)(8.0f, natW);
    float tH = (std::max)(4.0f, targetH);
    float tX = bgX + (bgW - tW) * 0.5f;
    float tY = bgY + (bgH - tH) * 0.5f - bgH * 0.15f;

    auto drawSub = [&](GUIControlSubpart kind, float x, float y, float w, float h) {
      bool isActive = (m_controlActiveSubpart == kind);
      const XVECTOR3& c = isActive ? activeColor : secondaryColor;
      DrawControlEditRectOverlay(x, y, w, h, c, isActive);
    };
    drawSub(GUIControlSubpart::Primary,     bgX, bgY, bgW, bgH);
    drawSub(GUIControlSubpart::PopupOk,     okX, okY, okW, okH);
    drawSub(GUIControlSubpart::PopupCancel, caX, caY, caW, caH);
    drawSub(GUIControlSubpart::PopupText,   tX, tY, tW, tH);
    return;
  }

  DrawControlEditRectOverlay(m_controlEditRect.x, m_controlEditRect.y, m_controlEditRect.w, m_controlEditRect.h, activeColor, true);
  if (m_controlEditTarget == GUIControlEditTarget::SelectorControl && m_controlEditRectSecondary.valid) {
    DrawControlEditRectOverlay(m_controlEditRectSecondary.x, m_controlEditRectSecondary.y,
                               m_controlEditRectSecondary.w, m_controlEditRectSecondary.h,
                               secondaryColor, false);
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
  for (auto& cp : m_checkboxPairs) {
    if (cp.label->h > maxH)
      maxH = cp.label->h;
  }
  for (auto& sp : m_selectorPairs) {
    if (sp.label->h > maxH)
      maxH = sp.label->h;
  }
  if (m_fpsLabel && m_fpsLabel->h > maxH)
    maxH = m_fpsLabel->h;

  // Also account for the popup text target height so popup text stays crisp when scaled up.
  {
    float sh = (m_ctx.screenH > 0.0f) ? m_ctx.screenH : screenH;
    float popupBgH = sh * 0.30f * m_controlLayout.popupBgScaleY;
    float popupTextH = popupBgH * m_controlLayout.popupTextScaleY * 0.25f;
    if (popupTextH > maxH) maxH = popupTextH;
  }

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

void GUIManager::AdjustControlPreviewScale(float delta) {
  m_controlPreviewVisualScale = (std::max)(0.10f, (std::min)(0.95f, m_controlPreviewVisualScale + delta));
  T8_LOG_DEBUG("[GUI Control Edit] Preview scale: %.2f", m_controlPreviewVisualScale);
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
    for (auto& cp : m_checkboxPairs) {
      cp.label->h = newH;
    }
    for (auto& sp : m_selectorPairs) {
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
  std::string label;                 // optional: custom label text for slider/checkbox/selector
};

struct GroupLayoutFileEntry {
  std::string name;
  std::vector<std::string> element_ids;      // widget IDs in this group; empty = all (Global)
  std::vector<ElementLayoutEntry> elements;  // per-group element positions
};

struct GroupSelectorLayoutEntry {
  float label_x = 0, label_y = 0, label_w = 0, label_h = 0;
  float sel_x = 0, sel_y = 0, sel_w = 0, sel_h = 0;
};

struct GUILayoutFile {
  float ref_width  = 1920.0f;   // screen width when layout was authored
  float ref_height = 1080.0f;   // screen height when layout was authored
  std::vector<ElementLayoutEntry> elements; // backward-compat (Global positions)
  float gridCellW = 40.0f;      // normalized to ref_height
  float gridCellH = 40.0f;      // normalized to ref_height
  std::vector<GroupLayoutFileEntry> groups;  // group definitions & per-group layouts
  GroupSelectorLayoutEntry group_selector;   // group selector position
};

struct GUIControlLayoutFile {
  float sliderKnobScaleX = 1.0f;
  float sliderKnobScaleY = 1.0f;
  float sliderKnobOffsetX = 0.0f;
  float sliderKnobOffsetY = 0.0f;
  bool sliderKnobRangeCalibrated = false;
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

  float popupBgScaleX = 1.0f;
  float popupBgScaleY = 1.0f;
  float popupOkScaleX = 1.0f;
  float popupOkScaleY = 1.0f;
  float popupOkOffsetX = -0.30f;
  float popupOkOffsetY = 0.35f;
  float popupCancelScaleX = 1.0f;
  float popupCancelScaleY = 1.0f;
  float popupCancelOffsetX = 0.30f;
  float popupCancelOffsetY = 0.35f;
  float popupTextScaleX = 0.6f;
  float popupTextScaleY = 0.6f;
};

bool GUIManager::SaveLayout(const std::string& path) {
  float screenW = (float)g_pBaseDriver->width;
  float screenH = (float)g_pBaseDriver->height;

  // Capture current group's live positions into its saved layout
  CaptureGroupLayout(m_activeGroupIndex);

  // Always capture FPS into Global so its position is saved uniformly.
  if (m_activeGroupIndex != 0) {
    SyncFPSToGlobalLayout();
  }

  GUILayoutFile lf;
  lf.ref_width  = screenW;
  lf.ref_height = screenH;
  // Normalize grid cells by reference height (uniform scale)
  lf.gridCellW = m_gridCellW / screenH;
  lf.gridCellH = m_gridCellH / screenH;

  // Legacy "elements" field: Global group positions for backward compat
  if (!m_groups.empty()) {
    for (auto& el : m_groups[0].layout) {
      lf.elements.push_back({el.id, el.x, el.y, el.w, el.h, el.label});
    }
  }

  // Save all groups
  for (auto& g : m_groups) {
    GroupLayoutFileEntry ge;
    ge.name = g.name;
    ge.element_ids = g.elementIds;
    for (auto& el : g.layout) {
      ge.elements.push_back({el.id, el.x, el.y, el.w, el.h, el.label});
    }
    lf.groups.push_back(ge);
  }

  // Save group selector position (normalized)
  lf.group_selector.label_x = m_groupSelectorLabel.x / screenW;
  lf.group_selector.label_y = m_groupSelectorLabel.y / screenH;
  lf.group_selector.label_w = m_groupSelectorLabel.w / screenH;
  lf.group_selector.label_h = m_groupSelectorLabel.h / screenH;
  lf.group_selector.sel_x   = m_groupSelector.x / screenW;
  lf.group_selector.sel_y   = m_groupSelector.y / screenH;
  lf.group_selector.sel_w   = m_groupSelector.w / screenH;
  lf.group_selector.sel_h   = m_groupSelector.h / screenH;

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
  T8_LOG_INFO("[GUIManager] Layout saved to '%s' (%zu elements, %zu groups)",
         path.c_str(), m_elements.size(), m_groups.size());
  m_layoutDirty = false;
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

  auto applyEntriesToElements = [&](const std::vector<ElementLayoutEntry>& entries) {
    for (auto& entry : entries) {
      for (auto* e : m_elements) {
        if (e->id == entry.id) {
          e->x = entry.x * curW;
          e->y = entry.y * curH;
          e->w = entry.w * curH;
          e->h = entry.h * curH;
          if (auto* sl = dynamic_cast<GUISliderBar*>(e)) {
            sl->knobSize = e->h;
            if (!entry.label.empty()) sl->label = entry.label;
          } else if (auto* sel = dynamic_cast<GUISelector*>(e)) {
            sel->btnSize = e->h;
            if (!entry.label.empty()) sel->label = entry.label;
          } else if (auto* cb = dynamic_cast<GUICheckbox*>(e)) {
            if (!entry.label.empty()) cb->label = entry.label;
          }
          break;
        }
      }
    }
  };

  if (!lf.groups.empty()) {
    // Load groups
    m_groups.clear();
    m_groupSelector.options.clear();
    for (auto& ge : lf.groups) {
      GUIGroup g;
      g.name = ge.name;
      g.elementIds = ge.element_ids;
      for (auto& el : ge.elements) {
        g.layout.push_back({el.id, el.x, el.y, el.w, el.h, el.label});
      }
      m_groups.push_back(g);
      m_groupSelector.options.push_back(ge.name);
    }
    m_activeGroupIndex = 0;
    m_groupSelector.selectedIndex = 0;
    // Apply Global group layout and visibility
    applyEntriesToElements(lf.groups[0].elements);
    ApplyGroupVisibility();
    T8_LOG_INFO("[GUIManager] Layout loaded from '%s' (%zu groups, ref %.0fx%.0f -> %.0fx%.0f)",
           path.c_str(), m_groups.size(), lf.ref_width, lf.ref_height, curW, curH);
  } else {
    // Legacy format: treat "elements" as Global group
    m_groups.clear();
    GUIGroup globalGroup;
    globalGroup.name = "Global";
    for (auto& entry : lf.elements) {
      globalGroup.layout.push_back({entry.id, entry.x, entry.y, entry.w, entry.h, entry.label});
    }
    m_groups.push_back(globalGroup);
    m_groupSelector.options = {"Global"};
    m_activeGroupIndex = 0;
    m_groupSelector.selectedIndex = 0;
    applyEntriesToElements(lf.elements);
    T8_LOG_INFO("[GUIManager] Layout loaded from '%s' (%zu entries, ref %.0fx%.0f -> %.0fx%.0f)",
           path.c_str(), lf.elements.size(), lf.ref_width, lf.ref_height, curW, curH);
  }
  // Mirror restored labels into their paired GUILabel->text so they show immediately.
  for (auto& cp : m_checkboxPairs) {
    if (cp.label && cp.checkbox) cp.label->text = cp.checkbox->label;
  }
  for (auto& sp : m_selectorPairs) {
    if (sp.label && sp.selector) sp.label->text = sp.selector->label;
  }
  // Slider paired labels are regenerated each frame in Draw (label: value).
  RebakeFontIfNeeded();
  ApplyControlLayoutToElements();

  // Restore group selector position (if saved)
  auto& gs = lf.group_selector;
  if (gs.sel_w > 0.0f && gs.sel_h > 0.0f) {
    m_groupSelectorLabel.x = gs.label_x * curW;
    m_groupSelectorLabel.y = gs.label_y * curH;
    m_groupSelectorLabel.w = gs.label_w * curH;
    m_groupSelectorLabel.h = gs.label_h * curH;
    m_groupSelector.x      = gs.sel_x * curW;
    m_groupSelector.y      = gs.sel_y * curH;
    m_groupSelector.w      = gs.sel_w * curH;
    m_groupSelector.h      = gs.sel_h * curH;
    m_groupSelector.btnSize = m_groupSelector.h;
  }

  return true;
}

bool GUIManager::SaveControlLayout(const std::string& path) {
  GUIControlLayoutFile f;
  f.sliderKnobScaleX = m_controlLayout.sliderKnobScaleX;
  f.sliderKnobScaleY = m_controlLayout.sliderKnobScaleY;
  f.sliderKnobOffsetX = m_controlLayout.sliderKnobOffsetX;
  f.sliderKnobOffsetY = m_controlLayout.sliderKnobOffsetY;
  f.sliderKnobRangeCalibrated = m_controlLayout.sliderKnobRangeCalibrated;
  f.sliderKnobMinNorm = m_controlLayout.sliderKnobMinNorm;
  f.sliderKnobMaxNorm = m_controlLayout.sliderKnobMaxNorm;
  f.selectorLeftScaleX = m_controlLayout.selectorLeftScaleX;
  f.selectorLeftScaleY = m_controlLayout.selectorLeftScaleY;
  f.selectorLeftOffsetX = m_controlLayout.selectorLeftOffsetX;
  f.selectorLeftOffsetY = m_controlLayout.selectorLeftOffsetY;
  f.selectorRightScaleX = m_controlLayout.selectorRightScaleX;
  f.selectorRightScaleY = m_controlLayout.selectorRightScaleY;
  f.selectorRightOffsetX = m_controlLayout.selectorRightOffsetX;
  f.selectorRightOffsetY = m_controlLayout.selectorRightOffsetY;
  f.checkboxMarkScaleX = m_controlLayout.checkboxMarkScaleX;
  f.checkboxMarkScaleY = m_controlLayout.checkboxMarkScaleY;
  f.checkboxMarkOffsetX = m_controlLayout.checkboxMarkOffsetX;
  f.checkboxMarkOffsetY = m_controlLayout.checkboxMarkOffsetY;
  f.popupBgScaleX = m_controlLayout.popupBgScaleX;
  f.popupBgScaleY = m_controlLayout.popupBgScaleY;
  f.popupOkScaleX = m_controlLayout.popupOkScaleX;
  f.popupOkScaleY = m_controlLayout.popupOkScaleY;
  f.popupOkOffsetX = m_controlLayout.popupOkOffsetX;
  f.popupOkOffsetY = m_controlLayout.popupOkOffsetY;
  f.popupCancelScaleX = m_controlLayout.popupCancelScaleX;
  f.popupCancelScaleY = m_controlLayout.popupCancelScaleY;
  f.popupCancelOffsetX = m_controlLayout.popupCancelOffsetX;
  f.popupCancelOffsetY = m_controlLayout.popupCancelOffsetY;
  f.popupTextScaleX = m_controlLayout.popupTextScaleX;
  f.popupTextScaleY = m_controlLayout.popupTextScaleY;

  auto result = glz::write<glz::opts{.prettify = true}>(f);
  if (!result) {
    T8_LOG_ERROR("[GUIManager] Failed to serialize control layout");
    return false;
  }
  std::ofstream file(path);
  if (!file.is_open()) {
    T8_LOG_ERROR("[GUIManager] Cannot write '%s'", path.c_str());
    return false;
  }
  file << result.value();
  T8_LOG_INFO("[GUIManager] Control layout saved to '%s' (target=%s)", path.c_str(), GetControlEditTargetName());
  return true;
}

bool GUIManager::LoadControlLayout(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    T8_LOG_INFO("[GUIManager] No control layout file '%s' -- using defaults", path.c_str());
    ApplyControlLayoutToElements();
    return false;
  }
  std::stringstream ss;
  ss << file.rdbuf();
  std::string json = ss.str();

  GUIControlLayoutFile f;
  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(f, json);
  if (ec) {
    std::string err = glz::format_error(ec, json);
    T8_LOG_ERROR("[GUIManager] Control layout parse error '%s': %s", path.c_str(), err.c_str());
    return false;
  }

  m_controlLayout.sliderKnobScaleX = f.sliderKnobScaleX;
  m_controlLayout.sliderKnobScaleY = f.sliderKnobScaleY;
  m_controlLayout.sliderKnobOffsetX = f.sliderKnobOffsetX;
  m_controlLayout.sliderKnobOffsetY = f.sliderKnobOffsetY;
  m_controlLayout.sliderKnobRangeCalibrated = f.sliderKnobRangeCalibrated;
  m_controlLayout.sliderKnobMinNorm = f.sliderKnobMinNorm;
  m_controlLayout.sliderKnobMaxNorm = f.sliderKnobMaxNorm;
  m_controlLayout.selectorLeftScaleX = f.selectorLeftScaleX;
  m_controlLayout.selectorLeftScaleY = f.selectorLeftScaleY;
  m_controlLayout.selectorLeftOffsetX = f.selectorLeftOffsetX;
  m_controlLayout.selectorLeftOffsetY = f.selectorLeftOffsetY;
  m_controlLayout.selectorRightScaleX = f.selectorRightScaleX;
  m_controlLayout.selectorRightScaleY = f.selectorRightScaleY;
  m_controlLayout.selectorRightOffsetX = f.selectorRightOffsetX;
  m_controlLayout.selectorRightOffsetY = f.selectorRightOffsetY;
  m_controlLayout.checkboxMarkScaleX = f.checkboxMarkScaleX;
  m_controlLayout.checkboxMarkScaleY = f.checkboxMarkScaleY;
  m_controlLayout.checkboxMarkOffsetX = f.checkboxMarkOffsetX;
  m_controlLayout.checkboxMarkOffsetY = f.checkboxMarkOffsetY;
  m_controlLayout.popupBgScaleX = f.popupBgScaleX;
  m_controlLayout.popupBgScaleY = f.popupBgScaleY;
  m_controlLayout.popupOkScaleX = f.popupOkScaleX;
  m_controlLayout.popupOkScaleY = f.popupOkScaleY;
  m_controlLayout.popupOkOffsetX = f.popupOkOffsetX;
  m_controlLayout.popupOkOffsetY = f.popupOkOffsetY;
  m_controlLayout.popupCancelScaleX = f.popupCancelScaleX;
  m_controlLayout.popupCancelScaleY = f.popupCancelScaleY;
  m_controlLayout.popupCancelOffsetX = f.popupCancelOffsetX;
  m_controlLayout.popupCancelOffsetY = f.popupCancelOffsetY;
  m_controlLayout.popupTextScaleX = f.popupTextScaleX;
  m_controlLayout.popupTextScaleY = f.popupTextScaleY;

  ApplyControlLayoutToElements();
  RebakeFontIfNeeded();
  T8_LOG_INFO("[GUIManager] Control layout loaded from '%s'", path.c_str());
  return true;
}

bool GUIManager::HandleControlEditTab(const std::string& path) {
  if (!m_controlEditMode) {
    return false;
  }

  if (m_controlEditTarget != GUIControlEditTarget::SliderKnob || !m_controlEditRect.valid) {
    return SaveControlLayout(path);
  }

  float screenW = m_ctx.screenW > 0.0f ? m_ctx.screenW : (float)g_pBaseDriver->width;
  float screenH = m_ctx.screenH > 0.0f ? m_ctx.screenH : (float)g_pBaseDriver->height;
  float barW = m_barTexture ? (float)m_barTexture->x : 256.0f;
  float barH = m_barTexture ? (float)m_barTexture->y : 32.0f;
  float safeW = (std::max)(1.0f, barW);
  float safeH = (std::max)(1.0f, barH);
  float targetW = screenW * m_controlPreviewVisualScale;
  float targetH = screenH * m_controlPreviewVisualScale;
  float s = (std::min)(targetW / safeW, targetH / safeH);
  float parentW = safeW * s;
  float parentX = (screenW - parentW) * 0.5f;

  // Capture knob CENTER as fraction of preview bar width
  float knobCenterX = m_controlEditRect.x + m_controlEditRect.w * 0.5f;
  float centerNorm = (knobCenterX - parentX) / (std::max)(1.0f, parentW);
  centerNorm = (std::max)(0.0f, (std::min)(1.0f, centerNorm));

  if (m_sliderCalibrationStep == 0) {
    m_controlLayout.sliderKnobMinNorm = centerNorm;
    m_sliderCalibrationStep = 1;
    T8_LOG_INFO("[GUI Control Edit] Slider calibration: min captured (%.4f). Move knob to MAX and press TAB again.", centerNorm);
    return true;
  }

  m_controlLayout.sliderKnobMaxNorm = centerNorm;
  if (m_controlLayout.sliderKnobMaxNorm < m_controlLayout.sliderKnobMinNorm) {
    std::swap(m_controlLayout.sliderKnobMinNorm, m_controlLayout.sliderKnobMaxNorm);
  }

  m_controlLayout.sliderKnobRangeCalibrated =
    std::abs(m_controlLayout.sliderKnobMaxNorm - m_controlLayout.sliderKnobMinNorm) > 0.0001f;

  ApplyControlLayoutToElements();
  m_sliderCalibrationStep = 0;
  T8_LOG_INFO("[GUI Control Edit] Slider calibration: max captured (%.4f), calibrated=%d, range=[%.4f, %.4f]",
              centerNorm,
              (int)m_controlLayout.sliderKnobRangeCalibrated,
              m_controlLayout.sliderKnobMinNorm,
              m_controlLayout.sliderKnobMaxNorm);
  return SaveControlLayout(path);
}

// ═══════════════════════════════════════════════════════════
//  Line-edit popup
// ═══════════════════════════════════════════════════════════

void GUIManager::OpenPopupFor(GUILabel* label) {
  if (!label) return;
  if (label->isFPS) return; // FPS text is updated each frame; not user-editable.

  // Resolve what field the popup edits so the user sees the editable part
  m_popupTargetLabel    = label;
  m_popupTargetSlider   = nullptr;
  m_popupTargetCheckbox = nullptr;
  m_popupTargetSelector = nullptr;

  for (auto& sp : m_sliderPairs) {
    if (sp.label == label) { m_popupTargetSlider = sp.slider; break; }
  }
  if (!m_popupTargetSlider) {
    for (auto& cp : m_checkboxPairs) {
      if (cp.label == label) { m_popupTargetCheckbox = cp.checkbox; break; }
    }
  }
  if (!m_popupTargetSlider && !m_popupTargetCheckbox) {
    for (auto& sp : m_selectorPairs) {
      if (sp.label == label) { m_popupTargetSelector = sp.selector; break; }
    }
  }

  if (m_popupTargetSlider)        m_popupText = m_popupTargetSlider->label;
  else if (m_popupTargetCheckbox) m_popupText = m_popupTargetCheckbox->label;
  else if (m_popupTargetSelector) m_popupText = m_popupTargetSelector->label;
  else                            m_popupText = label->text;

  m_popupCaret = (int)m_popupText.size();
  m_popupBlink = 0.0f;
  m_popupActive = true;
  m_popupWasMouseDown = true; // user is still holding mouse from the double-click
  m_popupOkPressed = false;
  m_popupCancelPressed = false;
  T8_LOG_INFO("[GUI Popup] Opened for label '%s' (text='%s')",
              label->id.c_str(), m_popupText.c_str());
}

void GUIManager::ClosePopupAndCommit(bool commit) {
  if (!m_popupActive) return;

  if (m_popupForGroupName) {
    if (commit && !m_popupText.empty()) {
      CreateGroupFromSelection(m_popupText);
      m_groupEditMode = false;
      ClearGroupHighlights();
      m_layoutDirty = true;
      T8_LOG_INFO("[GUI Popup] Group '%s' created with %zu elements",
                  m_popupText.c_str(), m_groupEditSelectedIds.size());
    } else {
      T8_LOG_INFO("[GUI Popup] Group naming cancelled, staying in group edit mode");
    }
    m_popupForGroupName = false;
    m_popupActive = false;
    m_popupTargetLabel    = nullptr;
    m_popupTargetSlider   = nullptr;
    m_popupTargetCheckbox = nullptr;
    m_popupTargetSelector = nullptr;
    m_popupText.clear();
    m_popupCaret = 0;
    return;
  }

  if (commit && m_popupTargetLabel) {
    if (m_popupTargetSlider) {
      m_popupTargetSlider->label = m_popupText;
    } else if (m_popupTargetCheckbox) {
      m_popupTargetCheckbox->label = m_popupText;
      m_popupTargetLabel->text = m_popupText;
    } else if (m_popupTargetSelector) {
      m_popupTargetSelector->label = m_popupText;
      m_popupTargetLabel->text = m_popupText;
    } else {
      m_popupTargetLabel->text = m_popupText;
    }
    m_layoutDirty = true;
    T8_LOG_INFO("[GUI Popup] Committed text '%s' to label '%s' (layout dirty)",
                m_popupText.c_str(), m_popupTargetLabel->id.c_str());
  } else {
    T8_LOG_INFO("[GUI Popup] Cancelled");
  }
  m_popupActive = false;
  m_popupTargetLabel    = nullptr;
  m_popupTargetSlider   = nullptr;
  m_popupTargetCheckbox = nullptr;
  m_popupTargetSelector = nullptr;
  m_popupText.clear();
  m_popupCaret = 0;
}

void GUIManager::GetPopupRects(float& bgX, float& bgY, float& bgW, float& bgH,
                                float& okX, float& okY, float& okW, float& okH,
                                float& cancelX, float& cancelY, float& cancelW, float& cancelH) const {
  float screenW = m_ctx.screenW > 0.0f ? m_ctx.screenW : (float)g_pBaseDriver->width;
  float screenH = m_ctx.screenH > 0.0f ? m_ctx.screenH : (float)g_pBaseDriver->height;

  // Popup background size: driven by texture aspect & bg scale, centered on screen.
  float srcW = m_popupBgTexture ? (float)m_popupBgTexture->x : 400.0f;
  float srcH = m_popupBgTexture ? (float)m_popupBgTexture->y : 160.0f;
  float baseH = screenH * 0.30f; // baseline height (30% of screen)
  bgH = baseH * m_controlLayout.popupBgScaleY;
  bgW = (srcH > 0.0f ? (srcW / srcH) : 2.5f) * bgH * m_controlLayout.popupBgScaleX / (std::max)(0.0001f, m_controlLayout.popupBgScaleY);
  bgX = (screenW - bgW) * 0.5f;
  bgY = (screenH - bgH) * 0.5f;

  // Buttons: scaled to native aspect relative to bg height, positioned via offsets from bg center.
  float okSrcW = m_popupOkTexture ? (float)m_popupOkTexture->x : 100.0f;
  float okSrcH = m_popupOkTexture ? (float)m_popupOkTexture->y : 40.0f;
  okH = bgH * m_controlLayout.popupOkScaleY * 0.25f; // 25% of bg height baseline
  okW = (okSrcH > 0.0f ? (okSrcW / okSrcH) : 2.5f) * okH * m_controlLayout.popupOkScaleX / (std::max)(0.0001f, m_controlLayout.popupOkScaleY);
  float bgCX = bgX + bgW * 0.5f;
  float bgCY = bgY + bgH * 0.5f;
  okX = bgCX + m_controlLayout.popupOkOffsetX * bgH - okW * 0.5f;
  okY = bgCY + m_controlLayout.popupOkOffsetY * bgH - okH * 0.5f;

  float caSrcW = m_popupCancelTexture ? (float)m_popupCancelTexture->x : 100.0f;
  float caSrcH = m_popupCancelTexture ? (float)m_popupCancelTexture->y : 40.0f;
  cancelH = bgH * m_controlLayout.popupCancelScaleY * 0.25f;
  cancelW = (caSrcH > 0.0f ? (caSrcW / caSrcH) : 2.5f) * cancelH * m_controlLayout.popupCancelScaleX / (std::max)(0.0001f, m_controlLayout.popupCancelScaleY);
  cancelX = bgCX + m_controlLayout.popupCancelOffsetX * bgH - cancelW * 0.5f;
  cancelY = bgCY + m_controlLayout.popupCancelOffsetY * bgH - cancelH * 0.5f;
}

void GUIManager::UpdatePopup(InputManager& input, float mx, float my, bool mouseDown) {
  // Keyboard handling (edge-triggered)
  auto pressed = [&](int key) -> bool {
    // PressedOnceKey uses edge latch; we need a local check because we may call multiple times
    if (key < 0 || key >= MAXKEYS) return false;
    bool now = input.KeyStates[0][key];
    bool latched = input.KeyStates[1][key];
    if (now && !latched) { input.KeyStates[1][key] = true; return true; }
    return false;
  };

  if (pressed(T800K_ESCAPE)) {
    ClosePopupAndCommit(false);
    return;
  }
  if (pressed(T800K_RETURN) || pressed(T800K_KP_ENTER)) {
    ClosePopupAndCommit(true);
    return;
  }
  if (pressed(T800K_BACKSPACE)) {
    if (m_popupCaret > 0) {
      m_popupText.erase(m_popupCaret - 1, 1);
      m_popupCaret--;
    }
  }
  if (pressed(T800K_DELETE)) {
    if (m_popupCaret < (int)m_popupText.size()) {
      m_popupText.erase(m_popupCaret, 1);
    }
  }
  if (pressed(T800K_LEFT)) {
    if (m_popupCaret > 0) m_popupCaret--;
  }
  if (pressed(T800K_RIGHT)) {
    if (m_popupCaret < (int)m_popupText.size()) m_popupCaret++;
  }
  if (pressed(T800K_HOME)) {
    m_popupCaret = 0;
  }
  if (pressed(T800K_END)) {
    m_popupCaret = (int)m_popupText.size();
  }

  // Consume text input buffer (UTF-8 chars)
  if (!input.textInput.empty()) {
    m_popupText.insert(m_popupCaret, input.textInput);
    m_popupCaret += (int)input.textInput.size();
    input.textInput.clear();
  }

  // Blink timer (approx 60fps)
  m_popupBlink += 1.0f / 60.0f;
  if (m_popupBlink > 1.0f) m_popupBlink -= 1.0f;

  // Mouse on buttons
  float bgX, bgY, bgW, bgH, okX, okY, okW, okH, cancelX, cancelY, cancelW, cancelH;
  GetPopupRects(bgX, bgY, bgW, bgH, okX, okY, okW, okH, cancelX, cancelY, cancelW, cancelH);
  bool overOk     = (mx >= okX && mx <= okX + okW && my >= okY && my <= okY + okH);
  bool overCancel = (mx >= cancelX && mx <= cancelX + cancelW && my >= cancelY && my <= cancelY + cancelH);
  m_popupOkPressed     = overOk && mouseDown;
  m_popupCancelPressed = overCancel && mouseDown;
  bool justReleased = !mouseDown && m_popupWasMouseDown;
  if (justReleased) {
    if (overOk)     { ClosePopupAndCommit(true);  m_popupWasMouseDown = mouseDown; return; }
    if (overCancel) { ClosePopupAndCommit(false); m_popupWasMouseDown = mouseDown; return; }
  }
  m_popupWasMouseDown = mouseDown;
}

void GUIManager::DrawPopup() {
  if (!m_popupActive) return;

  // The text batch pass that runs before us ends with BLEND_DEFAULT (no blending).
  // Transparent regions of the OK/Cancel PNGs would otherwise render as opaque,
  // which under GL hides the buttons completely.
  g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
  g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::NONE);

  m_quad.Set();
  m_shader->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);

  // No full-screen dim; popup is modal via input routing, not via visual overlay.
  float bgX, bgY, bgW, bgH, okX, okY, okW, okH, cancelX, cancelY, cancelW, cancelH;
  GetPopupRects(bgX, bgY, bgW, bgH, okX, okY, okW, okH, cancelX, cancelY, cancelW, cancelH);

  XVECTOR3 tint(1.0f, 1.0f, 1.0f);
  if (m_popupBgTexture)
    m_ctx.DrawTexturedQuad(bgX, bgY, bgW, bgH, m_popupBgTexture, tint);

  Texture* okTex = m_popupOkPressed ? m_popupOkPressedTexture : m_popupOkTexture;
  if (okTex) m_ctx.DrawTexturedQuad(okX, okY, okW, okH, okTex, tint);
  Texture* caTex = m_popupCancelPressed ? m_popupCancelPressedTexture : m_popupCancelTexture;
  if (caTex) m_ctx.DrawTexturedQuad(cancelX, cancelY, cancelW, cancelH, caTex, tint);

  // Draw text centered on background
  if (m_ctx.text) {
    int sw = (int)m_ctx.screenW;
    int sh = (int)m_ctx.screenH;
    float charH = m_textRenderer.m_fontSize * m_ctx.screenH / (float)m_textRenderer.m_textureSize;
    float targetH = bgH * m_controlLayout.popupTextScaleY * 0.25f;
    float scaleY = (charH > 0.0f) ? targetH / charH : 1.0f;
    float scaleX = scaleY * m_controlLayout.popupTextScaleX / (std::max)(0.0001f, m_controlLayout.popupTextScaleY);
    float textW = m_textRenderer.MeasurePixel(m_popupText, sw, sh) * scaleX;
    float textX = bgX + (bgW - textW) * 0.5f;
    float textY = bgY + (bgH - targetH) * 0.5f - bgH * 0.15f; // above buttons
    XVECTOR3 color(1.0f, 1.0f, 1.0f);
    m_textRenderer.DrawPixelScaled(textX, textY, scaleX, scaleY, sw, sh, color, m_popupText);

    // DrawPixelScaled switches to the text shader and resets blend/depth to
    // defaults.  Restore the GUI shader + alpha blend before drawing the caret,
    // otherwise the caret quad renders through the text shader and produces a
    // white flash (especially visible when popup text is empty).
    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::NONE);
    m_quad.Set();
    m_shader->Set(*T8DeviceContext);
    T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);

    // Caret
    if (m_popupBlink < 0.5f) {
      std::string pre = m_popupText.substr(0, (std::max)(0, (std::min)((int)m_popupText.size(), m_popupCaret)));
      float preW = m_textRenderer.MeasurePixel(pre, sw, sh) * scaleX;
      float caretX = textX + preW;
      float caretW = (std::max)(1.0f, scaleX * 2.0f);
      m_ctx.DrawSolidQuad(caretX, textY, caretW, targetH, color);
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  Group system
// ═══════════════════════════════════════════════════════════

void GUIManager::EnterGroupEditMode() {
  if (m_groupEditMode) return;
  m_groupEditMode = true;
  m_editMode = false;
  m_controlEditMode = false;
  m_groupEditSelectedIds.clear();
  ClearGroupHighlights();
  T8_LOG_INFO("[GUI] Entered group edit mode. Click controls to select, Enter to name group.");
}

void GUIManager::ExitGroupEditMode() {
  m_groupEditMode = false;
  ClearGroupHighlights();
  m_groupEditSelectedIds.clear();
  T8_LOG_INFO("[GUI] Exited group edit mode.");
}

void GUIManager::OpenGroupNamePopup() {
  if (m_groupEditSelectedIds.empty()) {
    T8_LOG_INFO("[GUI] No controls selected for group.");
    return;
  }
  m_popupForGroupName = true;
  m_popupActive = true;
  m_popupText.clear();
  m_popupCaret = 0;
  m_popupBlink = 0.0f;
  m_popupWasMouseDown = true;
  m_popupOkPressed = false;
  m_popupCancelPressed = false;
  m_popupTargetLabel = nullptr;
  m_popupTargetSlider = nullptr;
  m_popupTargetCheckbox = nullptr;
  m_popupTargetSelector = nullptr;
  T8_LOG_INFO("[GUI] Group name popup opened (%zu controls selected).",
              m_groupEditSelectedIds.size());
}

void GUIManager::DeleteAllCustomGroups() {
  if (m_groups.size() <= 1) {
    T8_LOG_INFO("[GUI] No custom groups to delete.");
    return;
  }
  // Capture current layout before switching
  CaptureGroupLayout(m_activeGroupIndex);

  // Keep only Global (index 0)
  m_groups.resize(1);
  m_groupSelector.options.resize(1);
  m_activeGroupIndex = 0;
  m_groupSelector.selectedIndex = 0;
  ApplyGroupVisibility();
  ApplyGroupLayout(0);
  m_layoutDirty = true;
  T8_LOG_INFO("[GUI] All custom groups deleted. Reverted to Global.");
}

void GUIManager::SwitchToGroup(int index) {
  if (index < 0 || index >= (int)m_groups.size()) return;
  if (index == m_activeGroupIndex) return;

  // Capture the current group's live positions
  CaptureGroupLayout(m_activeGroupIndex);

  // Always keep FPS position in sync with Global.
  if (m_activeGroupIndex != 0) {
    SyncFPSToGlobalLayout();
  }

  m_activeGroupIndex = index;
  m_groupSelector.selectedIndex = index;

  ApplyGroupVisibility();
  ApplyGroupLayout(index);
  T8_LOG_INFO("[GUI] Switched to group '%s' (index %d).",
              m_groups[index].name.c_str(), index);
}

void GUIManager::SwitchToPrevGroup() {
  if (m_groups.size() <= 1) return;
  int prev = (m_activeGroupIndex - 1 + (int)m_groups.size()) % (int)m_groups.size();
  SwitchToGroup(prev);
}

void GUIManager::SwitchToNextGroup() {
  if (m_groups.size() <= 1) return;
  int next = (m_activeGroupIndex + 1) % (int)m_groups.size();
  SwitchToGroup(next);
}

void GUIManager::ApplyGroupVisibility() {
  if (m_activeGroupIndex < 0 || m_activeGroupIndex >= (int)m_groups.size()) return;
  const auto& group = m_groups[m_activeGroupIndex];

  if (group.elementIds.empty()) {
    // Global: all visible
    for (auto* e : m_elements) {
      e->visible = true;
    }
  } else {
    // Custom group: only selected widget pairs visible
    for (auto* e : m_elements) {
      // FPS label always visible
      if (e == m_fpsLabel) { e->visible = true; continue; }

      std::string wid = FindPairedWidgetId(e);
      if (wid.empty()) {
        e->visible = false;
        continue;
      }
      e->visible = std::find(group.elementIds.begin(), group.elementIds.end(), wid) != group.elementIds.end();
    }
  }
}

void GUIManager::CaptureGroupLayout(int groupIndex) {
  if (groupIndex < 0 || groupIndex >= (int)m_groups.size()) return;
  auto& group = m_groups[groupIndex];

  float screenW = (float)g_pBaseDriver->width;
  float screenH = (float)g_pBaseDriver->height;

  group.layout.clear();

  for (auto* e : m_elements) {
    // FPS is global-only: only capture it in the Global group (index 0).
    if (e == m_fpsLabel && groupIndex != 0) continue;

    // For Global, save all. For custom, save only elements in the group.
    if (!group.elementIds.empty()) {
      if (e != m_fpsLabel) {
        std::string wid = FindPairedWidgetId(e);
        if (wid.empty()) continue;
        if (std::find(group.elementIds.begin(), group.elementIds.end(), wid) == group.elementIds.end())
          continue;
      }
    }

    GUIGroup::ElemLayout el;
    el.id = e->id;
    el.x = e->x / screenW;
    el.y = e->y / screenH;
    el.w = e->w / screenH;
    el.h = e->h / screenH;
    if (auto* sl = dynamic_cast<GUISliderBar*>(e))     el.label = sl->label;
    else if (auto* cb = dynamic_cast<GUICheckbox*>(e)) el.label = cb->label;
    else if (auto* se = dynamic_cast<GUISelector*>(e)) el.label = se->label;
    group.layout.push_back(el);
  }
}

void GUIManager::ApplyGroupLayout(int groupIndex) {
  if (groupIndex < 0 || groupIndex >= (int)m_groups.size()) return;
  const auto& group = m_groups[groupIndex];

  if (group.layout.empty()) return; // no saved layout, keep current positions

  float curW = (float)g_pBaseDriver->width;
  float curH = (float)g_pBaseDriver->height;

  for (auto& el : group.layout) {
    for (auto* e : m_elements) {
      if (e->id == el.id) {
        // FPS position is global — never overwrite it per-group.
        if (e == m_fpsLabel) break;

        e->x = el.x * curW;
        e->y = el.y * curH;
        e->w = el.w * curH;
        e->h = el.h * curH;
        if (auto* sl = dynamic_cast<GUISliderBar*>(e)) {
          sl->knobSize = e->h;
          if (!el.label.empty()) sl->label = el.label;
        } else if (auto* sel = dynamic_cast<GUISelector*>(e)) {
          sel->btnSize = e->h;
          if (!el.label.empty()) sel->label = el.label;
        } else if (auto* cb = dynamic_cast<GUICheckbox*>(e)) {
          if (!el.label.empty()) cb->label = el.label;
        }
        break;
      }
    }
  }
}

void GUIManager::CreateGroupFromSelection(const std::string& name) {
  GUIGroup group;
  group.name = name;
  group.elementIds = m_groupEditSelectedIds;

  // Capture current positions for the selected elements
  float screenW = (float)g_pBaseDriver->width;
  float screenH = (float)g_pBaseDriver->height;

  for (auto* e : m_elements) {
    std::string wid = FindPairedWidgetId(e);
    if (wid.empty()) continue;
    if (std::find(group.elementIds.begin(), group.elementIds.end(), wid) == group.elementIds.end())
      continue;

    GUIGroup::ElemLayout el;
    el.id = e->id;
    el.x = e->x / screenW;
    el.y = e->y / screenH;
    el.w = e->w / screenH;
    el.h = e->h / screenH;
    if (auto* sl = dynamic_cast<GUISliderBar*>(e))     el.label = sl->label;
    else if (auto* cb = dynamic_cast<GUICheckbox*>(e)) el.label = cb->label;
    else if (auto* se = dynamic_cast<GUISelector*>(e)) el.label = se->label;
    group.layout.push_back(el);
  }

  m_groups.push_back(group);
  m_groupSelector.options.push_back(name);

  // Auto-switch to the new group
  SwitchToGroup((int)m_groups.size() - 1);
}

std::string GUIManager::FindPairedWidgetId(GUIElement* element) const {
  for (auto& sp : m_sliderPairs) {
    if (sp.label == element || sp.slider == element) return sp.slider->id;
  }
  for (auto& cp : m_checkboxPairs) {
    if (cp.label == element || cp.checkbox == element) return cp.checkbox->id;
  }
  for (auto& sp : m_selectorPairs) {
    if (sp.label == element || sp.selector == element) return sp.selector->id;
  }
  return "";
}

void GUIManager::ClearGroupHighlights() {
  for (auto* e : m_elements) {
    e->groupHighlighted = false;
  }
}

void GUIManager::SyncFPSToGlobalLayout() {
  if (m_groups.empty() || !m_fpsLabel) return;
  float screenW = (float)g_pBaseDriver->width;
  float screenH = (float)g_pBaseDriver->height;

  GUIGroup::ElemLayout el;
  el.id = m_fpsLabel->id;
  el.x = m_fpsLabel->x / screenW;
  el.y = m_fpsLabel->y / screenH;
  el.w = m_fpsLabel->w / screenH;
  el.h = m_fpsLabel->h / screenH;

  // Update existing FPS entry in Global or append it
  auto& globalLayout = m_groups[0].layout;
  for (auto& entry : globalLayout) {
    if (entry.id == el.id) {
      entry.x = el.x;
      entry.y = el.y;
      entry.w = el.w;
      entry.h = el.h;
      return;
    }
  }
  globalLayout.push_back(el);
}

void GUIManager::UpdateGroupEditMode(InputManager& input, float mx, float my, bool mouseDown) {
  bool justPressed = mouseDown && !m_wasMouseDown;

  if (justPressed) {
    // Find which element was clicked
    for (int i = (int)m_elements.size() - 1; i >= 0; i--) {
      auto* e = m_elements[i];
      if (!e->visible) continue;
      if (e == m_fpsLabel) continue; // Can't select FPS
      if (!e->HitTest(mx, my)) continue;

      std::string wid = FindPairedWidgetId(e);
      if (wid.empty()) continue; // Not part of a known pair

      // Toggle selection
      auto it = std::find(m_groupEditSelectedIds.begin(), m_groupEditSelectedIds.end(), wid);
      if (it != m_groupEditSelectedIds.end()) {
        m_groupEditSelectedIds.erase(it);
      } else {
        m_groupEditSelectedIds.push_back(wid);
      }

      // Update highlights for the pair
      for (auto* el : m_elements) {
        std::string elWid = FindPairedWidgetId(el);
        if (elWid == wid) {
          bool selected = std::find(m_groupEditSelectedIds.begin(), m_groupEditSelectedIds.end(), wid) != m_groupEditSelectedIds.end();
          el->groupHighlighted = selected;
        }
      }
      break; // Only process topmost element
    }
  }
}

void GUIManager::DrawGroupEditHighlights() {
  m_quad.Set();
  m_shader->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);

  for (auto* e : m_elements) {
    e->DrawGroupHighlight(m_ctx);
  }
}

void GUIManager::DrawGroupSelector() {
  // Group selector is drawn as part of the main Draw pass (quads + text batch)
}

} // namespace t800
