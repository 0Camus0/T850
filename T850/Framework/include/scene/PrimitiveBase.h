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

namespace t800 {
#ifndef BUFFER_OFFSET
#define BUFFER_OFFSET(i) ((char *)NULL + (i))
#endif

  class PrimitiveBase {
  public:
    PrimitiveBase() : pScProp(0), gSig(0) {
      for (int i = 0; i < 8; i++) {
        Textures[i] = 0;
      }
      EnvMap = 0;
    }
    virtual ~PrimitiveBase() {}
    virtual void Load(char *) = 0;
    virtual void Create() = 0;
    virtual void Transform(float *t) = 0;
    virtual void Draw(float *t, float *vp) = 0;
    virtual void Destroy() = 0;
    friend class PrimitiveInst;

    void SetSceneProps(SceneProps *p) { pScProp = p; }
    SceneProps				*pScProp;
  protected:
    void SetGlobalSignature(unsigned long long f) { gSig = f; }
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
    Texture*				 Textures[8];
    Texture*			     EnvMap;
    unsigned long long gSig;
    float m_brightness;
	float m_fParallaxLowSamples;
	float m_fParallaxHighSamples;
	float m_fParallaxHeight;
  };
}


#endif
