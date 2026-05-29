#include <pch.h>
#include <scene/LensFlare.h>

namespace t850 {
  XVECTOR3 WorldToScreenPos(const XVECTOR3& worldPos, XMATRIX44 VP) {
    XVECTOR3 ret;
    XVecTransform(ret, worldPos,VP);
    ret.x /= ret.w;
    ret.y /= ret.w;
    ret.z /= abs(ret.w);
    return ret;
  }

  void LensFlare::Init(const PrimitiveManager & mngr)
  {
    pVP = mngr.pVP;
    m_sunSize = 0.30f;
    m_spacing = 0.25f;
    m_sunWorldPos = XVECTOR3(-4000, 1000, 2500, 1);

    m_proj.Identity();
    aspectRatio = (float)g_pBaseDriver->height / (float)g_pBaseDriver->width;
    m_quads.resize(10);

    ShaderKey sunKey(0);
    sunKey.setPass(PassType::LENS_FLARE_SUN);
    sunKey.bits |= ShaderKey::HAS_TEXCOORD0;
    ShaderKey ghostKey(0);
    ghostKey.setPass(PassType::LENS_FLARE_GHOST);
    ghostKey.bits |= ShaderKey::HAS_TEXCOORD0;

    m_quads[0].CreateInstance(mngr.GetPrimitive(PrimitiveManager::QUAD), &m_proj);
    m_quads[0].SetGlobalKey(sunKey);
    m_quads[0].ScaleAbsolute(aspectRatio*m_sunSize, 1 * m_sunSize, 1);
    m_quads[0].Update();

    for (int i = 1; i < 10; i++) {
      std::string path = "lens" + std::to_string(i);
      path += ".png";
      m_flareTextureID.push_back(g_pBaseDriver->CreateTexture(path));
    }
    for (int i = 1; i < 10; i++) {
      m_quads[i].CreateInstance(mngr.GetPrimitive(PrimitiveManager::QUAD), &m_proj);
      m_quads[i].SetGlobalKey(ghostKey);
      m_quads[i].Update();
      m_quads[i].SetTexture(g_pBaseDriver->GetTexture(m_flareTextureID[i-1]), 0);
    };
    m_quads[1].SetTexture(g_pBaseDriver->GetTexture(m_flareTextureID[6]), 0);
    m_quads[2].SetTexture(g_pBaseDriver->GetTexture(m_flareTextureID[8]), 0);
    m_quads[5].SetTexture(g_pBaseDriver->GetTexture(m_flareTextureID[7]), 0);
    m_quads[6].SetTexture(g_pBaseDriver->GetTexture(m_flareTextureID[2]), 0);
    m_quads[8].SetTexture(g_pBaseDriver->GetTexture(m_flareTextureID[4]), 0);


    m_quads[1].ScaleAbsolute(aspectRatio*m_sunSize * 2.0f , 1.0f * m_sunSize * 2.0f, 1.0f);
    m_quads[2].ScaleAbsolute(aspectRatio*m_sunSize * 1.0f, 1.0f * m_sunSize * 1.0f, 1.0f);
    m_quads[3].ScaleAbsolute(aspectRatio*m_sunSize * 1.0f, 1.0f * m_sunSize * 1.0f, 1.0f);
    m_quads[4].ScaleAbsolute(aspectRatio*m_sunSize * 2.0f, 1.0f * m_sunSize * 2.0f, 1.0f);
    m_quads[5].ScaleAbsolute(aspectRatio*m_sunSize * 3.0f, 1.0f * m_sunSize * 3.0f, 1.0f);
    m_quads[6].ScaleAbsolute(aspectRatio*m_sunSize * 0.2f, 1.0f * m_sunSize * 0.2f, 1.0f);
    m_quads[7].ScaleAbsolute(aspectRatio*m_sunSize * 0.8f, 1.0f * m_sunSize * 0.8f, 1.0f);
    m_quads[8].ScaleAbsolute(aspectRatio*m_sunSize * 0.5f, 1.f * m_sunSize * 0.5f, 1.0f);
    m_quads[9].ScaleAbsolute(aspectRatio*m_sunSize * 0.2f, 1.0f * m_sunSize * 0.2f, 1.0f);
    for (int i = 1; i < 10; i++) {
      m_quads[i].Update();
    }
  }
  void LensFlare::Draw()
  {

    XVECTOR3 pos = WorldToScreenPos(m_sunWorldPos, *pVP);
    const float visibilityMargin = 0.0f;
    if (pos.z < 0.0f ||
        pos.x < -1.0f - visibilityMargin || pos.x > 1.0f + visibilityMargin ||
        pos.y < -1.0f - visibilityMargin || pos.y > 1.0f + visibilityMargin) {
      return;
    }

    XVECTOR2 sunToCenter = CENTER_SCREEN - XVECTOR2(pos.x, pos.y);
    float scL = sunToCenter.Length();
    float sunBrightness = 1.0f - scL * 0.5f;
    if (sunBrightness > 1.0f) {
      sunBrightness = 1.0f;
    } else if (sunBrightness < 0.0f) {
      sunBrightness = 0.0f;
    }
    float ghostBrightness = 1.0f - scL;
    if (ghostBrightness < 0.0f) ghostBrightness = 0.0f;

    aspectRatio = (float)g_pBaseDriver->height / (float)g_pBaseDriver->width;
    m_quads[0].ScaleAbsolute(aspectRatio*m_sunSize, 1 * m_sunSize, 1);
    m_quads[0].SetBrightness(sunBrightness);
    m_quads[0].TranslateAbsolute(pos.x, pos.y, pos.z);
    m_quads[0].Update();

    g_pBaseDriver->SetDepthStencilState(BaseDriver::DepthStencilStates::READ);
    g_pBaseDriver->SetBlendState(BaseDriver::BlendStates::ADDITIVE);
    if (ghostBrightness > 0.0f && scL > 0.0001f) {
      sunToCenter.Normalize();
      XVECTOR2 lastPos(pos.x, pos.y);
      for (std::size_t j = 1; j < m_quads.size(); j++) {
        sunToCenter = sunToCenter * scL * m_spacing;
        lastPos += sunToCenter;
        XVECTOR2 flarePos = lastPos;
        m_quads[j].TranslateAbsolute(flarePos.x, flarePos.y, pos.z);
        m_quads[j].Update();
      }

      for (int j = static_cast<int>(m_quads.size()) - 1; j > 0; j--) {
        m_quads[j].SetBrightness(ghostBrightness * 0.45f);
        m_quads[j].Draw();
      }
    }
    m_quads[0].Draw();
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DepthStencilStates::READ_WRITE);
    g_pBaseDriver->SetBlendState(BaseDriver::BlendStates::BLEND_DEFAULT);
  }
  void LensFlare::Update()
  {
  }
}