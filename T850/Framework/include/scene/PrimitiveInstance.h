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

#ifndef T800_PRIMITIVE_INSTANCE_H
#define T800_PRIMITIVE_INSTANCE_H

#include <Config.h>

#include <scene/PrimitiveBase.h>
#include <scene/RenderQueue.h>
#include <physics/PhysicsTypes.h>
#include <video/BaseDriver.h>
#include <utils/xMaths.h>

namespace t850 {
  class PrimitiveInst {
  public:
    PrimitiveInst();

    void	CreateInstance(PrimitiveBase *pPrim, XMATRIX44 *pVP);

    void	TranslateAbsolute(float x, float y, float z);
    void	RotateXAbsolute(float ang);
    void	RotateYAbsolute(float ang);
    void	RotateZAbsolute(float ang);
    void	ScaleAbsolute(float scX);
    void	ScaleAbsolute(float scX, float scY, float scZ);

    void	TranslateRelative(float x, float y, float z);
    void	RotateXRelative(float ang);
    void	RotateYRelative(float ang);
    void	RotateZRelative(float ang);
    void	ScaleRelative(float sc);


    void	Update();
    void	Draw();
    RenderEntity ToRenderEntity() const;

    void ToogleVisible() { Visible = !Visible; }
    void SetVisible(bool f) { Visible = f; }

    //
    void SetGlobalKey(ShaderKey k) { gKey = k; }
    void SetTexture(Texture* tex, int index) {
      Textures[index] = tex;
    }

    void SetEnvironmentMap(Texture* tex) {
      EnvMap = tex;
    }
    void SetBrightness(float brightness) {
      m_brightness = brightness;
    }
	void SetParallaxSettings(float lsamples, float hsamples, float height) {
		m_fParallaxLowSamples = lsamples;
		m_fParallaxHighSamples = hsamples;
		m_fParallaxHeight = height;
	}
	void SetParallaxEnabled(bool enabled) {
		if (pBase) pBase->SetParallaxEnabled(enabled);
	}
	void SetParallaxShadowSettings(float minLayers, float maxLayers, float softness, float strength) {
		if (pBase) pBase->SetParallaxShadowSettings(minLayers, maxLayers, softness, strength);
	}
	void SetParallaxShadowEnabled(bool enabled) {
		if (pBase) pBase->SetParallaxShadowEnabled(enabled);
	}

	// Animation control — delegates to RenderSkinnedMesh if present
	class RenderSkinnedMesh* GetSkinnedMesh() const;
	bool IsSkinnedMesh() const;

    uint32_t GetEntityId() const { return EntityId; }
    void AttachPhysicsBody(PhysicsBodyHandle handle) { BodyHandle = handle; }
    void AttachPhysicsRagdoll(PhysicsRagdollHandle handle) { RagdollHandle = handle; }
    PhysicsBodyHandle GetPhysicsBody() const { return BodyHandle; }
    PhysicsRagdollHandle GetPhysicsRagdoll() const { return RagdollHandle; }
    bool HasPhysicsBody() const { return BodyHandle.IsValid(); }
    bool HasPhysicsRagdoll() const { return RagdollHandle.IsValid(); }
    void ClearPhysicsLinks();

    Texture*				 Textures[MaxPrimitiveTextures];
    Texture*			     EnvMap;
    ShaderKey gKey;
    float m_brightness;
    //

    bool Visible;

    XMATRIX44		Position;
    XMATRIX44		Scale;
    XMATRIX44		RotationX;
    XMATRIX44		RotationY;
    XMATRIX44		RotationZ;
    XMATRIX44		Final;

	float m_fParallaxLowSamples;
	float m_fParallaxHighSamples;
	float m_fParallaxHeight;

    XMATRIX44		*pViewProj;
    PrimitiveBase	*pBase;
    uint32_t EntityId = 0;
    PhysicsBodyHandle BodyHandle;
    PhysicsRagdollHandle RagdollHandle;
  };
}

#endif
