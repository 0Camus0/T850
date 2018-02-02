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
    unsigned char* ttf_buffer = new unsigned char [1 << 25];
    unsigned char* temp_bitmap = new unsigned char[m_textureSize * m_textureSize];

    
    fread(ttf_buffer, 1, 1 << 20, fopen(path.c_str(), "rb"));
    stbtt_BakeFontBitmap(ttf_buffer, 0, fontSize, temp_bitmap, m_textureSize, m_textureSize, 32, 96, cdata);

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

	
#ifdef USING_OPENGL
	std::string Defines = "";
	Defines += "#version 130\n\n";
	Defines += "#define lowp \n\n";
	Defines += "#define mediump \n\n";
	Defines += "#define highp \n\n";
	
	vstr = Defines + vstr;
	fstr = Defines + fstr;
#endif

    free(vsSourceP);
    free(fsSourceP);

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
  void TextRenderer::Destroy()
  {
    m_quad.Destroy();
    m_CB->release();
  }
}