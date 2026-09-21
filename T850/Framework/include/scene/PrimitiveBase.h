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

#ifndef T800_PRIMITIVEBASE_H
#define T800_PRIMITIVEBASE_H

#include <Config.h>

#include <scene/SceneProp.h>
#include <video/BaseDriver.h>

#include <vector>

namespace t850 {
  struct EngineContext;

#ifndef BUFFER_OFFSET
#define BUFFER_OFFSET(i) ((char *)NULL + (i))
#endif

  constexpr int MaxPrimitiveTextures = 24;

  class PrimitiveBase {
  public:
    PrimitiveBase() : pScProp(0), pEngineContext(nullptr) {
      gKey.bits = 0;
        for (int i = 0; i < MaxPrimitiveTextures; i++) {
        Textures[i] = 0;
      }
      EnvMap = 0;
    }
    virtual ~PrimitiveBase() {}
    virtual void Load(const char *) = 0;
    virtual void Create() = 0;
    virtual void Transform(float *t) = 0;
    virtual void Draw(float *t, float *vp) = 0;
    virtual bool MayDrawInPass(uint8_t) const { return true; }
    virtual void Destroy() = 0;
    friend class PrimitiveInst;

    void SetSceneProps(SceneProps *p) { pScProp = p; }
    void SetEngineContext(EngineContext* context) { pEngineContext = context; }
    EngineContext* GetEngineContext() const { return pEngineContext; }
    void CopyRenderStateFrom(const PrimitiveBase& source) {
      pScProp = source.pScProp;
      gKey = source.gKey;
      for (int index = 0; index < MaxPrimitiveTextures; ++index) Textures[index] = source.Textures[index];
      EnvMap = source.EnvMap;
      m_brightness = source.m_brightness;
      m_fParallaxLowSamples = source.m_fParallaxLowSamples;
      m_fParallaxHighSamples = source.m_fParallaxHighSamples;
      m_fParallaxHeight = source.m_fParallaxHeight;
      m_fParallaxEnabled = source.m_fParallaxEnabled;
      m_fParallaxShadowMinLayers = source.m_fParallaxShadowMinLayers;
      m_fParallaxShadowMaxLayers = source.m_fParallaxShadowMaxLayers;
      m_fParallaxShadowSoftness = source.m_fParallaxShadowSoftness;
      m_fParallaxShadowStrength = source.m_fParallaxShadowStrength;
      m_fParallaxShadowEnabled = source.m_fParallaxShadowEnabled;
    }
    SceneProps				*pScProp;
  protected:
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
		m_fParallaxEnabled = enabled ? 1.0f : 0.0f;
	}
	void SetParallaxShadowSettings(float minLayers, float maxLayers, float softness, float strength) {
		m_fParallaxShadowMinLayers = minLayers;
		m_fParallaxShadowMaxLayers = maxLayers;
		m_fParallaxShadowSoftness = softness;
		m_fParallaxShadowStrength = strength;
	}
    Texture*				 Textures[MaxPrimitiveTextures];
    Texture*			     EnvMap;
    ShaderKey gKey;
    float m_brightness;
	float m_fParallaxLowSamples;
	float m_fParallaxHighSamples;
	float m_fParallaxHeight;
	float m_fParallaxEnabled = 1.0f;
	float m_fParallaxShadowMinLayers = 8.0f;
	float m_fParallaxShadowMaxLayers = 32.0f;
	float m_fParallaxShadowSoftness = 0.5f;
	float m_fParallaxShadowStrength = 1.0f;
	float m_fParallaxShadowEnabled = 1.0f;

	void SetParallaxShadowEnabled(bool enabled) {
		m_fParallaxShadowEnabled = enabled ? 1.0f : 0.0f;
	}
    EngineContext* pEngineContext;
  };
}


#endif
