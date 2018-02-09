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
#include <video/windows/D3DXShader.h>
#include <video/windows/D3DXDriver.h>
#endif
namespace t800 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  void RenderQuad::Create() {
    m_quad.Init();
    SigBase = Signature::HAS_TEXCOORDS0;
    unsigned long long Dest;

    char *vsSourceP;
    char *fsSourceP;
    if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) {
      vsSourceP = file2string("Shaders/VS_Quad.glsl");
      fsSourceP = file2string("Shaders/FS_Quad.glsl");
    }
    else {
      vsSourceP = file2string("Shaders/VS_Quad.hlsl");
      fsSourceP = file2string("Shaders/FS_Quad.hlsl");
    }


    std::string vstr = std::string(vsSourceP);
    std::string fstr = std::string(fsSourceP);

    free(vsSourceP);
    free(fsSourceP);

    int shaderID = g_pBaseDriver->CreateShader(vstr, fstr, SigBase);

    Dest = SigBase | Signature::DEFERRED_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::FSQUAD_1_TEX;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::FSQUAD_2_TEX;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::FSQUAD_3_TEX;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::SHADOW_COMP_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::VERTICAL_BLUR_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::HORIZONTAL_BLUR_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::ONE_PASS_BLUR;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::BRIGHT_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::HDR_COMP_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::COC_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::COMBINE_COC_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::DOF_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::DOF_PASS_2;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);


    Dest = SigBase | Signature::VIGNETTE_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::GOD_RAY_CALCULATION_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);
    

    Dest = SigBase | Signature::GOD_RAY_BLEND_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);


    Dest = SigBase | Signature::SSAO_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);
    
    Dest = SigBase | Signature::RAY_MARCH;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::DEPTH_PRE_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::LIGHT_RAY_MARCHING;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::LIGHT_ADD;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);

    Dest = SigBase | Signature::FADE_PASS;
    g_pBaseDriver->CreateShader(vstr, fstr, Dest);
    
    
    

    

    ShaderBase* s = g_pBaseDriver->GetShaderIdx(shaderID);


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

    if (t)
      transform = t;
    unsigned long long sig = SigBase;
    sig |= gSig;
    ShaderBase * s = g_pBaseDriver->GetShaderSig(sig);

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

    if (sig&Signature::DEFERRED_PASS) {
      unsigned int numLights = pScProp->ActiveLights;
      if (numLights >= pScProp->Lights.size())
        numLights = pScProp->Lights.size();

      CnstBuffer.CameraInfo = XVECTOR3(pActualCamera->NPlane, pActualCamera->FPlane, pActualCamera->Fov, float(numLights));

      for (unsigned int i = 0; i < numLights; i++) {
        CnstBuffer.LightPositions[i] = pScProp->Lights[i].Position;
        CnstBuffer.LightColors[i] = pScProp->Lights[i].Color;
        CnstBuffer.LightRadius[i] = pScProp->Lights[i].radius;
      }
    }
	else if (sig&Signature::SHADOW_COMP_PASS) {
		CnstBuffer.LightPositions[0].x = (float)pScProp->SSAOKernel.KernelSize;
		CnstBuffer.LightPositions[0].y = pScProp->SSAOKernel.Radius;
		CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
		CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
		CnstBuffer.brightness.x = pScProp->PCFSamples;
		CnstBuffer.brightness.w = pScProp->SSAOKernel.NoiseSize;
		CnstBuffer.toogles.x = (float)pScProp->ToogleShadow;
		CnstBuffer.toogles.y = (float)pScProp->ToogleSSAO;
		for (unsigned int i = 1; i < pScProp->SSAOKernel.vSSAOKernel.size(); i++) {
			CnstBuffer.LightPositions[i] = pScProp->SSAOKernel.vSSAOKernel[i-1];
		}
	}
    else if (sig&Signature::ONE_PASS_BLUR) {
      CnstBuffer.LightPositions[0].x = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].x;
      CnstBuffer.LightPositions[0].y = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].y;
      CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
      CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
      for (unsigned int i = 1; i < pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel.size(); i++) {
        CnstBuffer.LightPositions[i] = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[i];
      }
    }
    else if (sig&Signature::VERTICAL_BLUR_PASS || sig&Signature::HORIZONTAL_BLUR_PASS) {
      CnstBuffer.LightPositions[0].x = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].x;
      CnstBuffer.LightPositions[0].y = pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[0].y;
      CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
      CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
      for (unsigned int i = 1; i < pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel.size(); i++) {
        CnstBuffer.LightPositions[i].x = roundTo(pScProp->pGaussKernels[pScProp->ActiveGaussKernel]->vGaussKernel[i].x, 6.0f);
      }
    }
    else if (sig&Signature::HDR_COMP_PASS || sig&Signature::BRIGHT_PASS || sig&Signature::FSQUAD_3_TEX) {
      //D3D11_TEXTURE2D_DESC pDesc;
      //reinterpret_cast<D3DXTexture*>(d3dxTextures[0])->Tex->GetDesc(&pDesc);
      CnstBuffer.CameraPos.w = 10.0f;//(float)pDesc.MipLevels;
      CnstBuffer.LightPositions[0].x = pScProp->BloomFactor;
      CnstBuffer.LightPositions[0].y = pScProp->Exposure;
    }
    else if (sig&Signature::COC_PASS ) {
      CnstBuffer.LightPositions[0].x = pScProp->Aperture;
      CnstBuffer.LightPositions[0].y = pScProp->FocalLength;
      CnstBuffer.LightPositions[0].z = pScProp->FocusDepth;
      CnstBuffer.LightPositions[0].w = pScProp->MaxCoc;
      CnstBuffer.LightPositions[1].x = pScProp->AutoFocus ? 1.0f : 0.0f;
    }
	else if (sig&Signature::DOF_PASS || sig&Signature::DOF_PASS_2) {
	  CnstBuffer.LightPositions[0].x = pScProp->DOF_Near_Samples_squared;
	  CnstBuffer.LightPositions[0].y = pScProp->DOF_Far_Samples_squared;
	  CnstBuffer.LightPositions[0].z = (float)Textures[0]->x;
	  CnstBuffer.LightPositions[0].w = (float)Textures[0]->y;
	}
    else if (sig&Signature::RAY_MARCH) {
      //CnstBuffer.LightPositions[0].x = time;
	}
	else if (sig&Signature::LIGHT_RAY_MARCHING) {
	  CnstBuffer.LightPositions[0].y = pScProp->LightVolumeSteps;
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
    if (EnvMap)
      EnvMap->Set(*T8DeviceContext, 6, "texEnv");

    //reinterpret_cast<ID3D11DeviceContext*>(T8DeviceContext->GetAPIObject())->PSSetSamplers(0, 1, &pSampler);

    T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
    T8DeviceContext->DrawIndexed(6, 0, 0);
  }

  void RenderQuad::Destroy() {
    m_quad.Destroy();
    pd3dConstantBuffer->release();
  }
}


