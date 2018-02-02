#include "scene/LensFlare.h"

namespace t800 {
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
    m_quads[0].CreateInstance(mngr.GetPrimitive(PrimitiveManager::QUAD), &m_proj);
    m_quads[0].SetTexture(g_pBaseDriver->GetTexture(g_pBaseDriver->CreateTexture("lens5.png")), 0);
    m_quads[0].SetGlobalSignature(Signature::FSQUAD_1_TEX);
    m_quads[0].ScaleAbsolute(aspectRatio*m_sunSize, 1 * m_sunSize, 1);
    m_quads[0].Update();

    for (int i = 1; i < 10; i++) {
      std::string path = "lens" + std::to_string(i);
      path += +".png";
      m_flareTextureID.push_back(g_pBaseDriver->CreateTexture(path));
    }
    for (int i = 1; i < 10; i++) {
      m_quads[i].CreateInstance(mngr.GetPrimitive(PrimitiveManager::QUAD), &m_proj);
      m_quads[i].SetGlobalSignature(Signature::FSQUAD_1_TEX);
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
    if (pos.z < 0.0f ) {
      return;
    }
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ);
    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::ADDITIVE);
    m_quads[0].TranslateAbsolute(pos.x, pos.y, 0);
    m_quads[0].Update();
    XVECTOR2 sunToCenter = CENTER_SCREEN - XVECTOR2(pos.x, pos.y);
    float scL = sunToCenter.Length();
    float brightness = 1 - (scL);
    sunToCenter.Normalize();
    if (brightness > 0) {
      XVECTOR2 lastPos(pos.x, pos.y);
      for (std::size_t j = 1; j < m_quads.size(); j++) {
        sunToCenter =  sunToCenter *scL*m_spacing;
        lastPos += sunToCenter;
        XVECTOR2 flarePos = lastPos;
        m_quads[j].TranslateAbsolute(flarePos.x, flarePos.y, 0);
        //m_quads[j].ScaleAbsolute(aspectRatio*m_sunSize , 1 * m_sunSize, 3.2f);
       // m_quads[j].ScaleRelative(1.0/(0.8 + sunToCenter.Length()));
        m_quads[j].Update();
      }
      
      for (int j = m_quads.size()-1; j > 0; j--) {
        m_quads[j].SetBrightness(brightness);
        m_quads[j].Draw();
      }
    }
    m_quads[0].Draw();
    g_pBaseDriver->SetDepthStencilState(BaseDriver::DEPTH_STENCIL_STATES::READ_WRITE);
    g_pBaseDriver->SetBlendState(BaseDriver::BLEND_STATES::BLEND_DEFAULT);
  }
  void LensFlare::Update()
  {
  }
}