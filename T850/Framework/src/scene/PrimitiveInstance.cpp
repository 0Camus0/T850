#include <pch.h>
/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include <scene/PrimitiveInstance.h>
#include <scene/RenderSkinnedMesh.h>
#include <scene/RenderMesh.h>
namespace t850 {
  namespace {
    uint32_t NextPrimitiveEntityId() {
      static uint32_t nextEntityId = 1;
      return nextEntityId++;
    }
  }

  PrimitiveInst::PrimitiveInst() {
    gKey.bits = 0;
    for (int i = 0; i < MaxPrimitiveTextures; i++) {
      Textures[i] = 0;
    }
    EnvMap = 0;
    pBase = nullptr;
    pViewProj = nullptr;
    XMatIdentity(Position);
    XMatIdentity(Scale);
    XMatIdentity(RotationX);
    XMatIdentity(RotationY);
    XMatIdentity(RotationZ);
    XMatIdentity(Final);
    Visible = false;
    m_brightness = 1.0f;
    m_fParallaxLowSamples = 0.0f;
    m_fParallaxHighSamples = 0.0f;
    m_fParallaxHeight = 0.0f;
    EntityId = 0;
    ClearPhysicsLinks();
  }

  void PrimitiveInst::CreateInstance(PrimitiveBase *pPrim, XMATRIX44 *pVP) {
    gKey.bits = 0;
    for (int i = 0; i < MaxPrimitiveTextures; i++) {
      Textures[i] = 0;
    }
    EnvMap = 0;

    pBase = pPrim;
    pViewProj = pVP;
    XMatIdentity(Position);
    XMatIdentity(Scale);
    XMatIdentity(RotationX);
    XMatIdentity(RotationY);
    XMatIdentity(RotationZ);
    XMatIdentity(Final);
    Visible = true;
    m_brightness = 1.0f;
    m_fParallaxLowSamples = 0.0f;
    m_fParallaxHighSamples = 0.0f;
    m_fParallaxHeight = 0.0f;
    EntityId = NextPrimitiveEntityId();
    ClearPhysicsLinks();
  }

  void PrimitiveInst::TranslateAbsolute(float x, float y, float z) {
    XMatTranslation(Position, x, y, z);
  }

  void PrimitiveInst::RotateXAbsolute(float ang) {
    XMatRotationX(RotationX, Deg2Rad(ang));
  }

  void PrimitiveInst::RotateYAbsolute(float ang) {
    XMatRotationY(RotationY, Deg2Rad(ang));
  }

  void PrimitiveInst::RotateZAbsolute(float ang) {
    XMatRotationZ(RotationZ, Deg2Rad(ang));
  }

  void PrimitiveInst::ScaleAbsolute(float sc) {
    XMatScaling(Scale, sc, sc, sc);
  }

  void PrimitiveInst::ScaleAbsolute(float scX, float scY, float scZ)
  {
    XMatScaling(Scale, scX, scY, scZ);
  }

  void PrimitiveInst::TranslateRelative(float x, float y, float z) {
    XMATRIX44 tmp;
    XMatTranslation(tmp, x, y, z);
    Position *= tmp;
  }

  void PrimitiveInst::RotateXRelative(float ang) {
    XMATRIX44 tmp;
    XMatRotationX(tmp, Deg2Rad(ang));
    RotationX *= tmp;
  }

  void PrimitiveInst::RotateYRelative(float ang) {
    XMATRIX44 tmp;
    XMatRotationY(tmp, Deg2Rad(ang));
    RotationY *= tmp;
  }

  void PrimitiveInst::RotateZRelative(float ang) {
    XMATRIX44 tmp;
    XMatRotationZ(tmp, Deg2Rad(ang));
    RotationZ *= tmp;
  }

  void PrimitiveInst::ScaleRelative(float sc) {
    XMATRIX44 tmp;
    XMatScaling(tmp, sc, sc, sc);
    Scale *= tmp;
  }

  void PrimitiveInst::Update() {
    Final = Scale*RotationX*RotationY*RotationZ*Position;
  }

  void PrimitiveInst::Draw() {
    if (!Visible || !pBase)
      return;
    Texture* previousTextures[MaxPrimitiveTextures];
    for (int i = 0; i < MaxPrimitiveTextures; i++) {
      previousTextures[i] = pBase->Textures[i];
    }
    Texture* previousEnvMap = pBase->EnvMap;
    ShaderKey previousKey = pBase->gKey;
    const float previousBrightness = pBase->m_brightness;
    const float previousParallaxLowSamples = pBase->m_fParallaxLowSamples;
    const float previousParallaxHighSamples = pBase->m_fParallaxHighSamples;
    const float previousParallaxHeight = pBase->m_fParallaxHeight;
    const float previousParallaxEnabled = pBase->m_fParallaxEnabled;
    const float previousParallaxShadowMinLayers = pBase->m_fParallaxShadowMinLayers;
    const float previousParallaxShadowMaxLayers = pBase->m_fParallaxShadowMaxLayers;
    const float previousParallaxShadowSoftness = pBase->m_fParallaxShadowSoftness;
    const float previousParallaxShadowStrength = pBase->m_fParallaxShadowStrength;
    const float previousParallaxShadowEnabled = pBase->m_fParallaxShadowEnabled;

    pBase->SetEnvironmentMap(EnvMap);
    pBase->SetGlobalKey(gKey);
    for (int i = 0; i < MaxPrimitiveTextures; i++) {
      pBase->SetTexture(Textures[i], i);
    }
    pBase->SetBrightness(m_brightness);
	pBase->SetParallaxSettings(m_fParallaxLowSamples, m_fParallaxHighSamples, m_fParallaxHeight);
    pBase->Draw(&Final.m[0][0], &(*pViewProj).m[0][0]);

    for (int i = 0; i < MaxPrimitiveTextures; i++) {
      pBase->Textures[i] = previousTextures[i];
    }
    pBase->EnvMap = previousEnvMap;
    pBase->gKey = previousKey;
    pBase->m_brightness = previousBrightness;
    pBase->m_fParallaxLowSamples = previousParallaxLowSamples;
    pBase->m_fParallaxHighSamples = previousParallaxHighSamples;
    pBase->m_fParallaxHeight = previousParallaxHeight;
    pBase->m_fParallaxEnabled = previousParallaxEnabled;
    pBase->m_fParallaxShadowMinLayers = previousParallaxShadowMinLayers;
    pBase->m_fParallaxShadowMaxLayers = previousParallaxShadowMaxLayers;
    pBase->m_fParallaxShadowSoftness = previousParallaxShadowSoftness;
    pBase->m_fParallaxShadowStrength = previousParallaxShadowStrength;
    pBase->m_fParallaxShadowEnabled = previousParallaxShadowEnabled;
  }

  RenderEntity PrimitiveInst::ToRenderEntity() const {
    RenderEntity entity;
    entity.id = EntityId;
    entity.worldFromLocal = Final;
    entity.visible = Visible;
    if (auto* mesh = dynamic_cast<RenderMesh*>(pBase)) {
      entity.mesh = mesh->m_asset;
      if (entity.mesh && entity.mesh->rootAABB.IsValid()) {
        entity.worldAABB = entity.mesh->rootAABB.Transformed(Final);
      }
    }
    return entity;
  }

  RenderSkinnedMesh* PrimitiveInst::GetSkinnedMesh() const {
    if (!pBase) {
      return nullptr;
    }
    return dynamic_cast<RenderSkinnedMesh*>(pBase);
  }

  bool PrimitiveInst::IsSkinnedMesh() const {
    return GetSkinnedMesh() != nullptr;
  }

  void PrimitiveInst::ClearPhysicsLinks() {
    BodyHandle.Reset();
    RagdollHandle.Reset();
  }
}
