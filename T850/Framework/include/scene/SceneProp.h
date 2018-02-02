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

struct Light{
	XVECTOR3 Position;
	XVECTOR3 Color;
	int		 Type;
	int		 Enabled;
  float radius;
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
	t800::Texture* NoiseTex;
	std::vector<XVECTOR3> vSSAOKernel;
	void InitTexture();
	void Update();
};
struct SceneProps{
	SceneProps() : ActiveCamera(0) , ActiveLights(1), ActiveLightCamera(0), ActiveGaussKernel(0), Exposure(0.3f) , BloomFactor(0.35f) {}

	void	AddLight(XVECTOR3 Pos, XVECTOR3 Color, float radius, bool enabled);
	void	RemoveLight(unsigned int index);
	void	SetLightPos(unsigned int index, XVECTOR3);

	void	AddCamera(Camera*);
	void	RemoveCamera(unsigned int index);

	void	AddLightCamera(Camera*);
	void	RemoveLightCamera(unsigned int index);

	void	AddGaussKernel(GaussFilter*);

	std::vector<Light>	   Lights;
	std::vector<Camera*> pCameras;

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

	float ShadowMapResolution;
  float  GoodRaysResolution;
	float PCFScale;
	float PCFSamples;

	float ParallaxLowSamples;
	float ParallaxHighSamples;
	float ParallaxHeight;

	float LightVolumeSteps;
	// HDR
	float	Exposure;
	float	BloomFactor;

  //DOF
  float Aperture;
  float FocalLength;
  float FocusDepth;
  float MaxCoc;
  float DOF_Near_Samples_squared;
  float DOF_Far_Samples_squared;
  bool AutoFocus;
};

#endif
