#include "gui/T8_GUI.h"
#include "gui/SliderBarData.h"
#include "gui/SliderKnobData.h"

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
  printf("[GUIManager] Initialized  scale=%.2f  fontSize=%.1f  visible=%d\n",
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
  m_dragTarget = nullptr;
  m_resizeTarget = nullptr;
}

GUISliderBar* GUIManager::FindSlider(const std::string& name) {
  for (auto& sp : m_sliderPairs) {
    if (sp.slider->name == name) return sp.slider;
  }
  return nullptr;
}

// ─── Layout ─────────────────────────────────────────────────
void GUIManager::LayoutSliders(int screenW, int screenH) {
  m_layout.Compute(screenW, screenH);

  float startX = (float)screenW - m_layout.marginRight - m_layout.sliderW;
  float startY = m_layout.marginTop;

  // Build a temporary draw context for text measurement
  GUIDrawContext tmpCtx;
  tmpCtx.text    = &m_textRenderer;
  tmpCtx.screenW = (float)screenW;
  tmpCtx.screenH = (float)screenH;

  for (size_t i = 0; i < m_sliderPairs.size(); i++) {
    auto& sp = m_sliderPairs[i];
    float rowY = startY + (float)i * m_layout.spacingY;

    // Slider bar position & size
    sp.slider->x        = startX;
    sp.slider->y        = rowY;
    sp.slider->w        = m_layout.sliderW;
    sp.slider->h        = m_layout.sliderH;
    sp.slider->knobSize = m_layout.knobSize;

    // Build label text
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: %.2f", sp.slider->label.c_str(), sp.slider->value);
    sp.label->text = buf;

    // Fit label to text and position to the left of the bar
    sp.label->FitToText(tmpCtx);
    sp.label->x = sp.slider->x - m_layout.labelGap - sp.label->w;
    // Vertically centre label with bar
    float charH = m_layout.fontSize * (float)screenH / m_layout.textureSize;
    sp.label->y = rowY + sp.slider->h * 0.5f - charH * 0.5f;
    sp.label->h = charH;
  }

  // Clamp all elements inside the screen
  float screenWf = (float)screenW;
  float screenHf = (float)screenH;
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
      sp.slider->UpdateInteraction(mx, my, mouseDown);
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
      m_resizeTarget = nullptr;
      RebakeFontIfNeeded();
    }
    if (m_dragTarget) {
      m_dragTarget->dragging = false;
      if (m_snapToGrid) {
        m_dragTarget->SnapToGrid(m_gridCellW, m_gridCellH);
      }
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

  // Update label texts with current values and draw all slider pairs
  for (auto& sp : m_sliderPairs) {
    if (!sp.slider->visible) continue;

    // Update label text
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: %.2f", sp.slider->label.c_str(), sp.slider->value);
    sp.label->text = buf;

    // Draw slider bar + knob
    sp.slider->Draw(m_ctx);

    // Draw label (switches to text shader internally)
    sp.label->Draw(m_ctx);

    // Restore GUI render state after text draw
    m_quad.Set();
    m_shader->Set(*T8DeviceContext);
    T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::NONE);
  }

  // Draw edit overlays on top
  if (m_editMode) {
    DrawEditOverlays();
  }

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

  // Only rebake if the largest label exceeds the baked glyph height
  if (maxH > charH * 1.05f) {
    float newFontSize = maxH * (float)m_textRenderer.m_textureSize / screenH;
    m_textRenderer.Rebake(newFontSize);
  }
}

// ═══════════════════════════════════════════════════════════
//  Layout JSON save / load  (via glaze)
// ═══════════════════════════════════════════════════════════

struct ElementLayoutEntry {
  std::string id;
  float x = 0, y = 0, w = 0, h = 0;
};

struct GUILayoutFile {
  std::vector<ElementLayoutEntry> elements;
  float gridCellW = 40.0f;
  float gridCellH = 40.0f;
};

bool GUIManager::SaveLayout(const std::string& path) {
  GUILayoutFile lf;
  lf.gridCellW = m_gridCellW;
  lf.gridCellH = m_gridCellH;
  for (auto* e : m_elements) {
    lf.elements.push_back({e->id, e->x, e->y, e->w, e->h});
  }
  auto result = glz::write<glz::opts{.prettify = true}>(lf);
  if (!result) {
    printf("[GUIManager] ERROR: failed to serialize layout\n");
    return false;
  }
  std::ofstream file(path);
  if (!file.is_open()) {
    printf("[GUIManager] ERROR: cannot write '%s'\n", path.c_str());
    return false;
  }
  file << result.value();
  printf("[GUIManager] Layout saved to '%s' (%zu elements)\n", path.c_str(), m_elements.size());
  return true;
}

bool GUIManager::LoadLayout(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    printf("[GUIManager] No layout file '%s' — using defaults\n", path.c_str());
    return false;
  }
  std::stringstream ss;
  ss << file.rdbuf();
  std::string json = ss.str();

  GUILayoutFile lf;
  auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(lf, json);
  if (ec) {
    std::string err = glz::format_error(ec, json);
    printf("[GUIManager] ERROR parsing '%s': %s\n", path.c_str(), err.c_str());
    return false;
  }

  m_gridCellW = lf.gridCellW;
  m_gridCellH = lf.gridCellH;

  // Apply positions/sizes by matching element IDs
  for (auto& entry : lf.elements) {
    for (auto* e : m_elements) {
      if (e->id == entry.id) {
        e->x = entry.x;
        e->y = entry.y;
        e->w = entry.w;
        e->h = entry.h;
        break;
      }
    }
  }
  printf("[GUIManager] Layout loaded from '%s' (%zu entries)\n", path.c_str(), lf.elements.size());
  RebakeFontIfNeeded();
  return true;
}

} // namespace t800
