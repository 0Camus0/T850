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

#include <scene/RenderQuad.h>
#include <utils/Utils.h>

#include <video/GLShader.h>
#include <video/GLDriver.h>
#if defined(OS_WINDOWS)
#include <video/windows/D3D11Shader.h>
#include <video/windows/D3D11Driver.h>
#endif
#include <utils/Log.h>
namespace t800 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  void RenderQuad::Create() {
    m_quad.Init();
    sigBase.bits = ShaderKey::HAS_TEXCOORD0;

    char *vsSourceP;
    char *fsSourceP;
    std::string vsName, fsName;
    if (g_pBaseDriver->UsesGLSL()) {
      vsSourceP = file2string("Shaders/VS_Quad.glsl");
      fsSourceP = file2string("Shaders/FS_Quad.glsl");
      vsName = "VS_Quad.glsl";
      fsName = "FS_Quad.glsl";
    }
    else {
      vsSourceP = file2string("Shaders/VS_Quad.hlsl");
      fsSourceP = file2string("Shaders/FS_Quad.hlsl");
      vsName = "VS_Quad.hlsl";
      fsName = "FS_Quad.hlsl";
    }


    std::string vstr = std::string(vsSourceP);
    std::string fstr = std::string(fsSourceP);

    free(vsSourceP);
    free(fsSourceP);

    int shaderID = g_pBaseDriver->CreateShader(vstr, fstr, sigBase, vsName, fsName);

    // Simple pass variants (no toggle combinations)
    static const uint8_t simplePasses[] = {
      PassType::DEFERRED, PassType::DEFERRED_LDR, PassType::FSQUAD_1_TEX, PassType::FSQUAD_2_TEX,
      PassType::FSQUAD_3_TEX, PassType::VERTICAL_BLUR, PassType::HORIZONTAL_BLUR,
      PassType::ONE_PASS_BLUR, PassType::BRIGHT, PassType::HDR_COMP,
      PassType::LUMINANCE_MAP, PassType::ADAPT_LUMINANCE, PassType::COMBINE_COC,
      PassType::DOF, PassType::DOF_2, PassType::BACKBUFFER,
      PassType::GOD_RAY_CALCULATION, PassType::GOD_RAY_BLEND,
      PassType::SSAO, PassType::RAY_MARCH, PassType::LIGHT_ADD, PassType::FADE
    };
    for (uint8_t p : simplePasses) {
      ShaderKey k(sigBase.bits);
      k.setPass(p);
      g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
    }

    // SHADOW_COMP: 4 toggle variants (+-SHADOWS +-SSAO), also with OMNI_SHADOWS
    for (int sh = 0; sh <= 1; sh++) {
      for (int ao = 0; ao <= 1; ao++) {
        ShaderKey k(sigBase.bits);
        k.setPass(PassType::SHADOW_COMP);
        if (sh) k.bits |= ShaderKey::SHADOWS;
        if (ao) k.bits |= ShaderKey::SSAO;
        g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
        ShaderKey ko(k.bits);
        ko.bits |= ShaderKey::OMNI_SHADOWS;
        g_pBaseDriver->CreateShader(vstr, fstr, ko, vsName, fsName);
      }
    }

    // COC: 2 variants (+-AUTO_FOCUS)
    for (int af = 0; af <= 1; af++) {
      ShaderKey k(sigBase.bits);
      k.setPass(PassType::COC);
      if (af) k.bits |= ShaderKey::AUTO_FOCUS;
      g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
    }

    // LIGHT_RAY_MARCHING: 2 variants (+-GOD_RAYS)
    for (int gr = 0; gr <= 1; gr++) {
      ShaderKey k(sigBase.bits);
      k.setPass(PassType::LIGHT_RAY_MARCHING);
      if (gr) k.bits |= ShaderKey::GOD_RAYS;
      g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
    }
    
    

    

    ShaderBase* s = g_pBaseDriver->GetShaderIdx(shaderID);
    T8_LOG_INFO("RenderQuad created: %zu shader variants compiled", g_pBaseDriver->m_shaderCache.size());


    t800::BufferDesc bdesc;
    bdesc.byteWidth = sizeof(CBuffer);
    bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
    pd3dConstantBuffer = (t800::ConstantBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::CONSTANT, bdesc);

    /*D3D11_SAMPLER_DESC sdesc;
    sdesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdesc.MinLOD = 0;
    sdesc.MaxLOD = D3D11_FLOAT32_MAX;
    sdesc.MipLODBias = 0.0f;
    sdesc.MaxAnisotropy = 1;
    sdesc.BorderColor[0] = sdesc.BorderColor[1] = sdesc.BorderColor[2] = sdesc.BorderColor[3] = 0;
    reinterpret_cast<ID3D11Device*>(T8Device->GetAPIObject())->CreateSamplerState(&sdesc, &pSampler);*/

    XMatIdentity(transform);
  }

  void RenderQuad::Transform(float *t) {
    transform = t;
  }

  void RenderQuad::Draw(float *t, float *vp) {
    static float time= 0;
    time += 1 / 60.0f;
    if (t)
      transform = t;

    // Build final shader key: base features + global pass + toggles
    ShaderKey finalKey(sigBase.bits);
    finalKey.setPass(gKey.getPass());
    constexpr uint32_t featureMask = (1u << ShaderKey::PASS_SHIFT) - 1;
    finalKey.bits |= (gKey.bits & featureMask);

    uint8_t pass = finalKey.getPass();

    // Add toggle bits based on pass and scene properties
    if (pass == PassType::SHADOW_COMP) {
      if (pScProp->ToogleShadow) finalKey.bits |= ShaderKey::SHADOWS;
      if (pScProp->ToogleSSAO)   finalKey.bits |= ShaderKey::SSAO;
    }
    else if (pass == PassType::COC) {
      if (pScProp->AutoFocus)     finalKey.bits |= ShaderKey::AUTO_FOCUS;
    }
    else if (pass == PassType::LIGHT_RAY_MARCHING) {
      if (pScProp->ToogleGodRays) finalKey.bits |= ShaderKey::GOD_RAYS;
    }

    ShaderBase * s = g_pBaseDriver->GetShader(finalKey);
    if (!s) return;

    Camera *pActualCamera = pScProp->pCameras[0];
    XMATRIX44 VP = pActualCamera->VP;
    XMATRIX44 WV = pActualCamera->View;
    VP.Inverse(&CnstBuffer.WVPInverse);
    CnstBuffer.WVP = transform;
    CnstBuffer.World = transform;
    CnstBuffer.WorldView = WV;
	  CnstBuffer.Projection = pActualCamera->VP;
    CnstBuffer.CameraPos = pActualCamera->Eye;
    CnstBuffer.brightness.x = m_brightness;
	CnstBuffer.brightness.y = pScProp->ShadowMapResolution;
	CnstBuffer.brightness.z = pScProp->PCFScale;

    if (pScProp->pLightCameras.size() > 0) {
      int selected = pScProp->ActiveLightCamera;
      CnstBuffer.WVPLight = pScProp->pLightCameras[selected]->VP;
      CnstBuffer.LightCameraPos = pScProp->pLightCameras[selected]->Eye;
      CnstBuffer.LightCameraInfo = XVECTOR3(pScProp->pLightCameras[selected]->NPlane, pScProp->pLightCameras[selected]->FPlane, pScProp->pLightCameras[selected]->Fov, 1.0f);
    }

    if (pass == PassType::DEFERRED || pass == PassType::DEFERRED_LDR) {
      unsigned int numLights = pScProp->ActiveLights;
      if (numLights >= static_cast<unsigned int>(pScProp->Lights.size()))
        numLights = static_cast<unsigned int>(pScProp->Lights.size());

      CnstBuffer.CameraInfo = XVECTOR3(pActualCamera->NPlane, pActualCamera->FPlane, pActualCamera->Fov, float(numLights));
      CnstBuffer.toogles.x = pScProp->EnvFactor;

      for (unsigned int i = 0; i < numLights; i++) {
        Light& light = pScProp->Lights[i];
        if (light.Type == LIGHT_DIRECTIONAL) {
          CnstBuffer.LightPositions[i] = XVECTOR3(light.Direction.x, light.Direction.y, light.Direction.z, 0.0f);
        } else {
          CnstBuffer.LightPositions[i] = XVECTOR3(light.Position.x, light.Position.y, light.Position.z, 1.0f);
        }
        CnstBuffer.LightColors[i] = XVECTOR3(light.Color.x, light.Color.y, light.Color.z, light.Intensity);
        CnstBuffer.LightRadius[i] = light.radius;
      }
    }
	else if (pass == PassType::SHADOW_COMP) {
		CnstBuffer.LightPositions[0].x = (float)pScProp->SSAOKernel.KernelSize;
		CnstBuffer.LightPositions[0].y = pScProp->SSAOKernel.Radius;
		CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
		CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
		CnstBuffer.brightness.x = pScProp->PCFSamples;
		CnstBuffer.brightness.w = pScProp->SSAOKernel.NoiseSize;
    CnstBuffer.toogles.x = pScProp->ShadowMin;
    CnstBuffer.toogles.z = (float)pScProp->DebugMode;
    CnstBuffer.toogles.w = pScProp->ShadowBias;
		for (unsigned int i = 1; i < pScProp->SSAOKernel.vSSAOKernel.size(); i++) {
			CnstBuffer.LightPositions[i] = pScProp->SSAOKernel.vSSAOKernel[i-1];
		}
	}
    else if (pass == PassType::ONE_PASS_BLUR) {
      CnstBuffer.LightPositions[0].x = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].x;
      CnstBuffer.LightPositions[0].y = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].y;
      CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
      CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
      for (unsigned int i = 1; i < pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel.size(); i++) {
        CnstBuffer.LightPositions[i] = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[i];
      }
    }
    else if (pass == PassType::VERTICAL_BLUR || pass == PassType::HORIZONTAL_BLUR) {
      CnstBuffer.LightPositions[0].x = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].x;
      CnstBuffer.LightPositions[0].y = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].y;
      CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
      CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
      for (unsigned int i = 1; i < pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel.size(); i++) {
        CnstBuffer.LightPositions[i].x = roundTo(pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[i].x, 6.0f);
      }
    }
    else if (pass == PassType::HDR_COMP || pass == PassType::BRIGHT || pass == PassType::FSQUAD_3_TEX || pass == PassType::LUMINANCE_MAP || pass == PassType::ADAPT_LUMINANCE) {
      Texture* mipSource = Textures[0];
      if ((pass == PassType::ADAPT_LUMINANCE) && Textures[1]) {
        mipSource = Textures[1];
      }

      if (mipSource) {
        unsigned int maxDim = mipSource->x > mipSource->y ? mipSource->x : mipSource->y;
        int mipLevels = 1;
        while (maxDim > 1) { maxDim >>= 1; mipLevels++; }
        CnstBuffer.CameraPos.w = (float)(mipLevels - 1);
      }

      if (pass == PassType::BRIGHT) {
        CnstBuffer.LightPositions[0].x = pScProp->BloomThreshold;
        CnstBuffer.LightPositions[0].y = pScProp->Exposure;
        CnstBuffer.LightPositions[0].z = pScProp->ToneMapWhiteLevel;
      }

      if (pass == PassType::HDR_COMP) {
        CnstBuffer.LightPositions[0].x = pScProp->BloomFactor;
        CnstBuffer.LightPositions[0].y = pScProp->Exposure;
        CnstBuffer.LightPositions[0].z = pScProp->ToneMapWhiteLevel;
      }

      if (pass == PassType::ADAPT_LUMINANCE) {
        CnstBuffer.LightPositions[1].x = pScProp->LuminanceTau;
        CnstBuffer.LightPositions[1].y = pScProp->FrameDeltaSec;
      }
    }
    else if (pass == PassType::COC) {
      CnstBuffer.LightPositions[0].x = pScProp->Aperture;
      CnstBuffer.LightPositions[0].y = pScProp->FocalLength;
      CnstBuffer.LightPositions[0].z = pScProp->FocusDepth;
      CnstBuffer.LightPositions[0].w = pScProp->MaxCoc;
    }
	else if (pass == PassType::DOF || pass == PassType::DOF_2) {
	  CnstBuffer.LightPositions[0].x = pScProp->DOF_Near_Samples_squared;
	  CnstBuffer.LightPositions[0].y = pScProp->DOF_Far_Samples_squared;
	  CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
	  CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
	}
    else if (pass == PassType::RAY_MARCH) {
      CnstBuffer.LightPositions[0].x = time;
      CnstBuffer.toogles = pScProp->pLightCameras[1]->Eye;
	}
	else if (pass == PassType::LIGHT_RAY_MARCHING) {
	  CnstBuffer.LightPositions[0].y = pScProp->LightVolumeSteps;
    CnstBuffer.toogles.x = pScProp->GodRaysFactor;
	  CnstBuffer.toogles.z = (float)pScProp->DebugMode;
	}

    m_quad.Set();
    s->Set(*T8DeviceContext);

    pd3dConstantBuffer->UpdateFromBuffer(*T8DeviceContext, &CnstBuffer);
    pd3dConstantBuffer->Set(*T8DeviceContext);
    if (Textures[0])
      Textures[0]->Set(*T8DeviceContext, 0, "tex0");
    if (Textures[1])
      Textures[1]->Set(*T8DeviceContext, 1, "tex1");
    if (Textures[2])
      Textures[2]->Set(*T8DeviceContext, 2, "tex2");
    if (Textures[3])
      Textures[3]->Set(*T8DeviceContext, 3, "tex3");
    if (Textures[4])
      Textures[4]->Set(*T8DeviceContext, 4, "tex4");
    if (Textures[5])
      Textures[5]->Set(*T8DeviceContext, 5, "tex5");
    if (EnvMap) {
      EnvMap->Set(*T8DeviceContext, 6, "texEnv");
    }

    if (Textures[0])
      Textures[0]->SetSampler(*T8DeviceContext, 0);
    if (Textures[1])
      Textures[1]->SetSampler(*T8DeviceContext, 1);
    if (Textures[2])
      Textures[2]->SetSampler(*T8DeviceContext, 2);
    if (Textures[3])
      Textures[3]->SetSampler(*T8DeviceContext, 3);
    if (Textures[4])
      Textures[4]->SetSampler(*T8DeviceContext, 4);
    if (Textures[5])
      Textures[5]->SetSampler(*T8DeviceContext, 5);

    T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
    T8DeviceContext->DrawIndexed(6, 0, 0);
  }

  void RenderQuad::Destroy() {
    m_quad.Destroy();
    pd3dConstantBuffer->release();
  }
}


