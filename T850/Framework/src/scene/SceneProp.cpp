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


#include <scene/SceneProp.h>

#include <cstdio>
#include <algorithm>

#include <utils/Log.h>

#if defined(USING_GL_COMMON)
#include <video/gl/GLShader.h>
#include <video/gl/GLDriver.h>
#endif
#if defined(OS_WINDOWS)
#include <video/d3d11/D3D11Shader.h>
#include <video/d3d11/D3D11Driver.h>
#endif

namespace t850 {
	extern Device*            T8Device;
	extern DeviceContext*     T8DeviceContext;
}

void	SceneProps::AddLight(XVECTOR3 Pos, XVECTOR3 Color, float radius, float intensity, LightType type, bool enabled){
	Light l;
	l.Position=Pos;
	l.Direction = XVECTOR3(0, -1, 0);
	l.Color=Color;
	l.Enabled=(int)enabled;
	l.radius = radius;
	l.Intensity = intensity;
	l.Type = type;
	Lights.push_back(l);
}

void	SceneProps::AddDirectionalLight(XVECTOR3 Dir, XVECTOR3 Color, float intensity, bool enabled){
	Light l;
	l.Position = XVECTOR3(0, 0, 0);
	// Normalize direction
	float len = sqrtf(Dir.x*Dir.x + Dir.y*Dir.y + Dir.z*Dir.z);
	if (len > 0.0001f) { Dir.x /= len; Dir.y /= len; Dir.z /= len; }
	l.Direction = Dir;
	l.Color = Color;
	l.Enabled = (int)enabled;
	l.radius = 0.0f;
	l.Intensity = intensity;
	l.Type = LIGHT_DIRECTIONAL;
	Lights.push_back(l);
}

void	SceneProps::RemoveLight(unsigned int index){
	if(index < 0 || index >= Lights.size())
		return;

	Lights.erase(Lights.begin() + index);
}

void	SceneProps::SetLightPos(unsigned int index, XVECTOR3 pos){
	if (index < 0 || index >= Lights.size())
		return;

	Lights[index].Position = pos;
}

void	SceneProps::AddCamera(Camera* cam){
	pCameras.push_back(cam);
}

void SceneProps::SetPrimaryCamera(Camera* cam) {
	if (pCameras.empty()) {
		pCameras.push_back(cam);
	} else {
		pCameras[0] = cam;
	}
	ActiveCamera = 0;
}

Camera* SceneProps::GetPrimaryCamera() const {
	return pCameras.empty() ? nullptr : pCameras[0];
}

ScopedPrimaryCameraOverride::ScopedPrimaryCameraOverride(SceneProps& props)
	: m_props(props),
	  m_previousCamera(props.GetPrimaryCamera()),
	  m_hadPrimaryCamera(!props.pCameras.empty()) {
}

ScopedPrimaryCameraOverride::~ScopedPrimaryCameraOverride() {
	if (m_hadPrimaryCamera) {
		m_props.SetPrimaryCamera(m_previousCamera);
	} else if (!m_props.pCameras.empty()) {
		m_props.pCameras.erase(m_props.pCameras.begin());
	}
}

void	SceneProps::RemoveCamera(unsigned int index){
	if (index < 0 || index >= pCameras.size())
		return;

	pCameras.erase(pCameras.begin() + index);
}

void	SceneProps::AddLightCamera(Camera* cam) {
	pLightCameras.push_back(cam);
}

void	SceneProps::RemoveLightCamera(unsigned int index) {
	if (index < 0 || index >= pLightCameras.size())
		return;

	pLightCameras.erase(pLightCameras.begin() + index);
}

template<typename T>
T RandRange(T m, T M) {
	T r = static_cast <T> (rand()) / static_cast <T> (RAND_MAX);
	return m + r * (M - m);
}


float t850_lerp(float a, float b, float f)
{
	return a + f * (b - a);
}

void SSAOFilter::InitTexture() {
	Destroy();
	unsigned char *pChar = &Noise[0];
	for (int i = 0; i < SSAO_NOISE_SIZE*SSAO_NOISE_SIZE; i++) {
			*pChar = (unsigned char)RandRange(0.0f,255.0f); pChar++;
			*pChar = (unsigned char)RandRange(0.0f,255.0f); pChar++;
			*pChar = 0; pChar++;
			*pChar = 255; pChar++;

	}
	std::string dummy;
	NoiseTex = t850::T8Device->CreateTextureFromMemory(Noise, SSAO_NOISE_SIZE, SSAO_NOISE_SIZE, 4, dummy);
	NoiseTex->params |= t850::TextBasicParams::TILED | t850::TextBasicParams::NEAREST_FILTER;
	NoiseTex->SetTextureParams();
	std::string noiseRaw;
	noiseRaw.reserve(SSAO_NOISE_SIZE * SSAO_NOISE_SIZE * 5);
	char sample[8] = {};
	for (int i = 0; i < SSAO_NOISE_SIZE * SSAO_NOISE_SIZE; i++) {
		std::snprintf(sample, sizeof(sample), " %02X%02X", Noise[i * 4], Noise[i * 4 + 1]);
		noiseRaw += sample;
	}
	T8_LOG_INFO("NOISE_RAW(%dx%d):%s", SSAO_NOISE_SIZE, SSAO_NOISE_SIZE, noiseRaw.c_str());
}

void SSAOFilter::Destroy() {
	if (NoiseTex) { NoiseTex->release(); NoiseTex = nullptr; }
}

void SSAOFilter::Update() {
	vSSAOKernel.clear();
	for (int i = 0; i < KernelSize; i++) {
		XVECTOR3 vec = XVECTOR3(
			RandRange(-1.0f, 1.0f),
			RandRange(-1.0f, 1.0f),
			RandRange( 0.0f, 1.0f)
		);
		vec.Normalize();
		vec.x *= RandRange(0.0f, 1.0f);
		vec.y *= RandRange(0.0f, 1.0f);
		vec.z *= RandRange(0.0f, 1.0f);

		float scale = float(i) / float(KernelSize);
		scale = t850_lerp(0.1f, 1.0f, scale * scale);
		vec.x *= scale;
		vec.y *= scale;
		vec.z *= scale;
		vSSAOKernel.push_back(vec);
	}
}

void	GaussFilter::Update(){
	if ((kernelSize - 1) % 2 != 0)
		kernelSize--;

	std::vector<sample_> allSamples = UpdateKernel(sigma, (float)kernelSize, 1000.0f);
	const int firstTap = 1;
	const int lastTap = (int)allSamples.size() - 1;
	const int shaderSampleCount = (std::max)(0, lastTap - firstTap);
	XVECTOR3 KernelSize = XVECTOR3((float)shaderSampleCount, radius, 0.0f, 0.0f);

	vGaussKernel.clear();
	vGaussKernel.push_back(KernelSize);
	for (int i = firstTap; i < lastTap; i++) {
		vGaussKernel.push_back(XVECTOR3(allSamples[i].weight, 0.0f, 0.0f, 0.0f));
	}
}

void SceneProps::AddGaussKernel(GaussFilter* pGF){
	pGaussKernels.push_back(pGF);
}
