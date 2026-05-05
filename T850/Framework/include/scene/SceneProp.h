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

#ifndef T800_SCENEPROPS_H
#define T800_SCENEPROPS_H

#include <Config.h>
#include <video/BaseDriver.h>
#include <utils/xMaths.h>
#include <utils/Camera.h>
#include <vector>

enum LightType {
	LIGHT_DIRECTIONAL = 0,
	LIGHT_POINT = 1,
};

struct Light{
	XVECTOR3 Position;
	XVECTOR3 Direction = XVECTOR3(0.0f, -1.0f, 0.0f);  // normalized, for directional lights
	XVECTOR3 Color;
	LightType Type;
	int		 Enabled;
	float    radius;
	float    Intensity;
};

struct GaussFilter{
	float sigma;
	float radius;
	int kernelSize;
	std::vector<XVECTOR3> vGaussKernel;
	void Update();
};

#define SSAO_NOISE_SIZE 4

struct SSAOFilter {
	SSAOFilter() : KernelSize(10), Radius(1.0f), NoiseSize((float)SSAO_NOISE_SIZE){}
	int   KernelSize;
	float Radius;
	float NoiseSize;
	unsigned char  Noise[SSAO_NOISE_SIZE * SSAO_NOISE_SIZE * 4];
	t850::Texture* NoiseTex;
	std::vector<XVECTOR3> vSSAOKernel;
	void InitTexture();
	void Destroy();
	void Update();
};
struct SceneProps{
SceneProps() : ActiveCamera(0) , ActiveLights(1), ActiveLightCamera(0), ActiveGaussKernel(0), Exposure(0.0f) , BloomFactor(0.35f), BloomThreshold(2.0f), ToneMapWhiteLevel(4.0f), LuminanceTau(1.1f), FrameDeltaSec(1.0f / 60.0f), ToogleShadow(1), ToogleSSAO(1), DebugMode(0){}

	void	AddLight(XVECTOR3 Pos, XVECTOR3 Color, float radius, float intensity, LightType type, bool enabled);
	void	AddDirectionalLight(XVECTOR3 Dir, XVECTOR3 Color, float intensity, bool enabled);
	void	RemoveLight(unsigned int index);
	void	SetLightPos(unsigned int index, XVECTOR3);

	void	AddCamera(Camera*);
	void	RemoveCamera(unsigned int index);

	void	AddLightCamera(Camera*);
	void	RemoveLightCamera(unsigned int index);

	void	AddGaussKernel(GaussFilter*);

	std::vector<Light>	   Lights;
	std::vector<Camera*> pCameras;
	Camera* pCullingCamera = nullptr;
	bool FrustumCullingEnabled = true;
	bool FrustumCullingToggleAllowed = true;
	bool ShowCullingDebug = false;

	std::vector<Camera*> pLightCameras;

	std::vector<GaussFilter*> pGaussKernels;
	SSAOFilter				  SSAOKernel;

	XVECTOR3			AmbientColor;

	int ActiveCamera;
	int	ActiveLights;
	int ActiveLightCamera;
	int ActiveGaussKernel;

	int ToogleShadow;
	int ToogleSSAO;
	int ToogleDOF = 1;
	int ToogleParallax = 1;
	int ToogleParallaxShadow = 1;
	int ToogleGodRays = 1;
	int DebugMode;

	float ShadowBias = 0.000005f;
	float ShadowMin = 0.25f;
	float EnvFactor = 1.0f;
	float IBLFactor = 1.0f;
	float IBLMipCount = 4.0f;
	float IBLDiffuseMipLevel = 4.0f;
	float IBLBRDFLUTEnabled = 0.0f;
	float GodRaysFactor = 1.0f;
	float MaterialEmissiveIntensity = 1.0f;
	float MaterialTransmissionMultiplier = 1.0f;
	float MaterialRefractionStrength = 0.03f;

	float ShadowMapResolution = 1024.0f;
  float GoodRaysResolution = 0.0f;
	float PCFScale = 1.7f;
	float PCFSamples = 1.0f;

	float ParallaxLowSamples = 0.0f;
	float ParallaxHighSamples = 0.0f;
	float ParallaxHeight = 0.0f;
	float ParallaxShadowMinLayers = 8.0f;
	float ParallaxShadowMaxLayers = 32.0f;
	float ParallaxShadowSoftness = 0.5f;
	float ParallaxShadowStrength = 1.0f;

	float LightVolumeSteps = 0.0f;
	// HDR
	float	Exposure = 0.0f;
	float	BloomFactor = 0.35f;
	float	BloomThreshold = 2.0f;
	float ToneMapWhiteLevel = 4.0f;
	float LuminanceTau = 1.1f;
	float FrameDeltaSec = 1.0f / 60.0f;

  //DOF
  float Aperture = 0.0f;
  float FocalLength = 0.0f;
  float FocusDepth = 0.0f;
  float MaxCoc = 0.0f;
  float DOF_Near_Samples_squared = 0.0f;
  float DOF_Far_Samples_squared = 0.0f;
  bool AutoFocus = false;
};

#endif
