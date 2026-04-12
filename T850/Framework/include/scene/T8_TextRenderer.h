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
    // Rebake the font atlas at a new pixel size without recompiling shaders.
    void Rebake(float newFontSize);
    void Draw(float x, float y, const XVECTOR3& color, std::string);
    // Pixel-space draw: x,y in screen pixels (top-left origin).
    // Returns the X advance in pixels (width of rendered text).
    float DrawPixel(float px, float py, int screenW, int screenH,
                    const XVECTOR3& color, const std::string& text);
    // Pixel-space draw with uniform or non-uniform scale.
    // (px,py) is the top-left of the text area.  scale is applied to glyph quads.
    float DrawPixelScaled(float px, float py, float scaleX, float scaleY,
                          int screenW, int screenH,
                          const XVECTOR3& color, const std::string& text);
    // Measure text width in screen pixels without drawing.
    float MeasurePixel(const std::string& text, int screenW, int screenH);
    void Destroy();

    int m_textureSize;
    float m_fontSize;
    float m_ascent;       // ascent in baked-texture pixels (set by LoadFromFile)
    std::string m_fontPath;  // stored by LoadFromFile for Rebake
    ShaderBase* m_shader;
    ConstantBuffer* m_CB;
    Quad m_quad;

  };
}