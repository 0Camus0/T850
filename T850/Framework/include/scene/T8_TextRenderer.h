#pragma once
#include <stb_truetype.h>
#include <string>
#include "video/BaseDriver.h"
#include <scene/PrimitiveManager.h>
#include <scene/PrimitiveInstance.h>
#include "scene/T8_Quad.h"
namespace t800 {
  class TextRenderer {
  public:
    stbtt_fontinfo font;

    stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphsl
    Texture* ftex;
    void LoadFromFile(float fontSize, std::string path, float textureSize = 512);
    void Draw(float x, float y, const XVECTOR3& color, std::string);
    void Destroy();

    int m_textureSize;
    float m_fontSize;
    ShaderBase* m_shader;
    ConstantBuffer* m_CB;
    Quad m_quad;

  };
}