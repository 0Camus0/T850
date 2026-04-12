#include "scene/T8_TextRenderer.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <fstream>

#include <video/GLShader.h>
#include <video/GLDriver.h>
#if defined(OS_WINDOWS)
#include <video/windows/D3DXShader.h>
#include <video/windows/D3DXDriver.h>
#endif
namespace t800 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;
  void TextRenderer::LoadFromFile(float fontSize, std::string path, float textureSize)
  {
    m_fontSize = fontSize;
    m_textureSize = (int)textureSize;
    m_fontPath = path;
    unsigned char* ttf_buffer = new unsigned char [1 << 25];
    unsigned char* temp_bitmap = new unsigned char[m_textureSize * m_textureSize];

    
    fread(ttf_buffer, 1, 1 << 20, fopen(path.c_str(), "rb"));
    stbtt_BakeFontBitmap(ttf_buffer, 0, fontSize, temp_bitmap, m_textureSize, m_textureSize, 32, 96, cdata);

    // Compute ascent for DrawPixelScaled (must happen before ttf_buffer is freed)
    stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));
    {
      int asc, desc, gap;
      stbtt_GetFontVMetrics(&font, &asc, &desc, &gap);
      float fScale = stbtt_ScaleForPixelHeight(&font, fontSize);
      m_ascent = asc * fScale;
    }

    if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) { //OpenGL is loading the texture upside down T_T
	  size_t sx = (size_t)m_textureSize*m_textureSize;
      for (size_t i = 0; i < sx / 2; i++)
      {
        char temp = temp_bitmap[i];
        temp_bitmap[i] = temp_bitmap[sx - i - 1];
        temp_bitmap[sx - i - 1] = temp;
      }
      for (size_t i = 0; i < (size_t)m_textureSize; i++)
      {
        for (size_t j = 0; j < (size_t)m_textureSize / 2; j++)
        {
          char temp = temp_bitmap[j + i*m_textureSize];
          temp_bitmap[j + i*m_textureSize] = temp_bitmap[m_textureSize - j - 1 + m_textureSize*i];
          temp_bitmap[m_textureSize - j - 1 + m_textureSize*i] = temp;
        }
      }
    }

    ftex = T8Device->CreateTextureFromMemory(temp_bitmap, m_textureSize, m_textureSize, 1, path);
    //Create Quad
    m_quad.Init();
    /*SHADERS*/
    char *vsSourceP;
    char *fsSourceP;
    if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) {
      vsSourceP = file2string("Shaders/VS_Text.glsl");
      fsSourceP = file2string("Shaders/FS_Text.glsl");
    }
    else {
      vsSourceP = file2string("Shaders/VS_Text.hlsl");
      fsSourceP = file2string("Shaders/FS_Text.hlsl");
    }
    std::string vstr = std::string(vsSourceP);
    std::string fstr = std::string(fsSourceP);

    if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) {
#if defined(USING_OPENGL)
	std::string Defines = "";
	Defines += "#version 130\n\n";
	Defines += "#define lowp \n\n";
	Defines += "#define mediump \n\n";
	Defines += "#define highp \n\n";
	vstr = Defines + vstr;
	fstr = Defines + fstr;
#elif defined(USING_GL_COMMON)
	std::string Defines = "";
	Defines += "#version 300 es\n\n";
	Defines += "#define ES_30\n\n";
	vstr = Defines + vstr;
	fstr = Defines + fstr;
#endif
    }

    int shaderID = g_pBaseDriver->CreateShader(vstr, fstr);
    m_shader = g_pBaseDriver->GetShaderIdx(shaderID);


    t800::BufferDesc bdesc;
    bdesc.byteWidth = sizeof(XVECTOR3);
    bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
    m_CB = (t800::ConstantBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::CONSTANT, bdesc);

    /*DEALLOCATE MEMORY*/
    delete[] temp_bitmap;
    delete[]ttf_buffer;
  }
  void TextRenderer::Draw(float x, float y,const XVECTOR3& color, std::string text)
  {
    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ);
    x = (x + 1)*0.5f * m_textureSize;
    y = (y - 1)*0.5f * m_textureSize;
    //y = -m_textureSize - y;
    m_quad.Set();
    m_shader->Set(*T8DeviceContext);
    m_CB->UpdateFromBuffer(*T8DeviceContext, &color.x);
    m_CB->Set(*T8DeviceContext);
    T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
    ftex->Set(*T8DeviceContext, 0, "tex0");
    //ftex->SetSampler(*T8DeviceContext);
    float tempDiv = 1.0f / (float)m_textureSize;
    char* pT = &text[0];
    while (*pT) {
      if (*pT >= 32 && *pT < 128) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(cdata, m_textureSize, m_textureSize, *pT - 32, &x, &y, &q, 1);
        float tempx0Mul = (q.x0*tempDiv) * 2 - 1;
        float tempx1Mul = (q.x1*tempDiv) * 2 - 1;
        float tempy1Mul = (-q.y0*tempDiv) * 2 - 1;
        float tempy0Mul = (-q.y1*tempDiv) * 2 - 1;

        m_quad.m_vertex[0].x = tempx0Mul ;
        m_quad.m_vertex[0].y = tempy1Mul;
        m_quad.m_vertex[0].s = q.s0 ;
        m_quad.m_vertex[0].t = q.t0 ;

        m_quad.m_vertex[1].x = tempx0Mul;
        m_quad.m_vertex[1].y = tempy0Mul;
        m_quad.m_vertex[1].s = q.s0 ;
        m_quad.m_vertex[1].t = q.t1 ;

        m_quad.m_vertex[2].x = tempx1Mul ;
        m_quad.m_vertex[2].y = tempy0Mul ;
        m_quad.m_vertex[2].s = q.s1;
        m_quad.m_vertex[2].t = q.t1 ;

        m_quad.m_vertex[3].x = tempx1Mul;
        m_quad.m_vertex[3].y = tempy1Mul ;
        m_quad.m_vertex[3].s = q.s1;
        m_quad.m_vertex[3].t = q.t0 ;

        m_quad.m_VB->UpdateFromBuffer(*T8DeviceContext, m_quad.m_vertex);


        T8DeviceContext->DrawIndexed(6, 0, 0);

      }
      pT++;
    }
    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::BLEND_DEFAULT);
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::DEPTH_DEFAULT);
  }

  float TextRenderer::MeasurePixel(const std::string& text, int screenW, int screenH) {
    // Simulate stbtt_GetBakedQuad advances without drawing.
    // The advance is in texture-space pixels, convert to screen pixels.
    float texX = 0.0f;
    float texY = 0.0f;
    float ts = (float)m_textureSize;
    for (char c : text) {
      if (c >= 32 && c < 128) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(cdata, m_textureSize, m_textureSize, c - 32,
                           &texX, &texY, &q, 1);
      }
    }
    // texX is now the total advance in texture-space pixels.
    // Map: texX / textureSize = fraction of NDC [0..1] half-width
    // screenPx = texX * screenW / textureSize
    return texX * (float)screenW / ts;
  }

  float TextRenderer::DrawPixel(float px, float py, int screenW, int screenH,
                                const XVECTOR3& color, const std::string& text) {
    // Convert pixel coords (top-left origin) to NDC for the legacy path.
    // Draw() internally negates NDC-Y, so we pass the "inverted" value
    // so the final vertex positions end up correct.
    float ndcX = (px / (float)screenW) * 2.0f - 1.0f;
    float ndcY = (py / (float)screenH) * 2.0f - 1.0f;
    Draw(ndcX, ndcY, color, text);
    return MeasurePixel(text, screenW, screenH);
  }

  float TextRenderer::DrawPixelScaled(float px, float py, float scaleX, float scaleY,
                                       int screenW, int screenH,
                                       const XVECTOR3& color, const std::string& text) {
    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ALPHA_BLEND);
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ);

    m_quad.Set();
    m_shader->Set(*T8DeviceContext);
    m_CB->UpdateFromBuffer(*T8DeviceContext, &color.x);
    m_CB->Set(*T8DeviceContext);
    T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
    ftex->Set(*T8DeviceContext, 0, "tex0");

    float sw = (float)screenW;
    float sh = (float)screenH;
    float ts = (float)m_textureSize;

    // stbtt cursor – place baseline at m_ascent so py = top of text
    float curX = 0.0f;
    float curY = m_ascent;

    const char* pT = text.c_str();
    while (*pT) {
      if (*pT >= 32 && (unsigned char)*pT < 128) {
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(cdata, m_textureSize, m_textureSize, *pT - 32,
                           &curX, &curY, &q, 1);

        // Glyph corners: texture-pixels → screen-pixels (scaled) → offset by label pos
        float gx0 = px + q.x0 * (sw / ts) * scaleX;
        float gy0 = py + q.y0 * (sh / ts) * scaleY;
        float gx1 = px + q.x1 * (sw / ts) * scaleX;
        float gy1 = py + q.y1 * (sh / ts) * scaleY;

        // Screen-pixels → NDC  (Y-down pixel → Y-up NDC)
        float nx0 = (gx0 / sw) * 2.0f - 1.0f;
        float ny0 = 1.0f - (gy0 / sh) * 2.0f;  // top  (higher NDC)
        float nx1 = (gx1 / sw) * 2.0f - 1.0f;
        float ny1 = 1.0f - (gy1 / sh) * 2.0f;  // bottom

        m_quad.m_vertex[0] = {nx0, ny0, 0.0f, 1.0f, q.s0, q.t0};
        m_quad.m_vertex[1] = {nx0, ny1, 0.0f, 1.0f, q.s0, q.t1};
        m_quad.m_vertex[2] = {nx1, ny1, 0.0f, 1.0f, q.s1, q.t1};
        m_quad.m_vertex[3] = {nx1, ny0, 0.0f, 1.0f, q.s1, q.t0};

        m_quad.m_VB->UpdateFromBuffer(*T8DeviceContext, m_quad.m_vertex);
        T8DeviceContext->DrawIndexed(6, 0, 0);
      }
      pT++;
    }

    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::BLEND_DEFAULT);
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::DEPTH_DEFAULT);

    return MeasurePixel(text, screenW, screenH) * scaleX;
  }

  void TextRenderer::Rebake(float newFontSize) {
    if (std::abs(newFontSize - m_fontSize) < 0.5f) return;
    m_fontSize = newFontSize;

    unsigned char* ttf_buffer  = new unsigned char[1 << 25];
    unsigned char* temp_bitmap = new unsigned char[m_textureSize * m_textureSize];

    fread(ttf_buffer, 1, 1 << 20, fopen(m_fontPath.c_str(), "rb"));
    stbtt_BakeFontBitmap(ttf_buffer, 0, newFontSize, temp_bitmap,
                         m_textureSize, m_textureSize, 32, 96, cdata);

    stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));
    {
      int asc, desc, gap;
      stbtt_GetFontVMetrics(&font, &asc, &desc, &gap);
      float fScale = stbtt_ScaleForPixelHeight(&font, newFontSize);
      m_ascent = asc * fScale;
    }

    if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) {
      size_t sx = (size_t)m_textureSize * m_textureSize;
      for (size_t i = 0; i < sx / 2; i++) {
        char temp = temp_bitmap[i];
        temp_bitmap[i] = temp_bitmap[sx - i - 1];
        temp_bitmap[sx - i - 1] = temp;
      }
      for (size_t i = 0; i < (size_t)m_textureSize; i++) {
        for (size_t j = 0; j < (size_t)m_textureSize / 2; j++) {
          char temp = temp_bitmap[j + i * m_textureSize];
          temp_bitmap[j + i * m_textureSize] = temp_bitmap[m_textureSize - j - 1 + m_textureSize * i];
          temp_bitmap[m_textureSize - j - 1 + m_textureSize * i] = temp;
        }
      }
    }

    if (ftex) ftex->release();
    ftex = T8Device->CreateTextureFromMemory(temp_bitmap, m_textureSize, m_textureSize, 1, m_fontPath);

    delete[] temp_bitmap;
    delete[] ttf_buffer;
    printf("[TextRenderer] Rebaked font at size %.1f\n", newFontSize);
  }

  void TextRenderer::Destroy()
  {
    m_quad.Destroy();
    m_CB->release();
  }
}