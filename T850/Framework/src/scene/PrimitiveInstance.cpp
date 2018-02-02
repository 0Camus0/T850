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
namespace t800 {
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
    if (!Visible)
      return;
    pBase->SetEnvironmentMap(EnvMap);
    pBase->SetGlobalSignature(gSig);
    pBase->SetTexture(Textures[0], 0);
    pBase->SetTexture(Textures[1], 1);
    pBase->SetTexture(Textures[2], 2);
    pBase->SetTexture(Textures[3], 3);
    pBase->SetTexture(Textures[4], 4);
    pBase->SetTexture(Textures[5], 5);
    pBase->SetTexture(Textures[6], 6);
    pBase->SetTexture(Textures[7], 7);
    pBase->SetBrightness(m_brightness);
	pBase->SetParallaxSettings(m_fParallaxLowSamples, m_fParallaxHighSamples, m_fParallaxHeight);
    pBase->Draw(&Final.m[0][0], &(*pViewProj).m[0][0]);
  }
}
