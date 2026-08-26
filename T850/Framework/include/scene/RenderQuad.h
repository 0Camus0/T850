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

#ifndef T800_QUAD_D3DX_H
#define T800_QUAD_D3DX_H

#include <Config.h>

#include <video/BaseDriver.h>
#include <utils/xMaths.h>
#include <scene/PrimitiveBase.h>
#include <vector>


#if defined(USING_GL_COMMON)
#include <video/gl/GLTexture.h>
#endif
#if defined(OS_WINDOWS)
#include <video/d3d11/D3D11Texture.h>
#endif

#include <scene/Quad.h>
namespace t850 {
  class RenderQuad : public PrimitiveBase {
  public:
    struct CBuffer {
      XMATRIX44 WVP;
      XMATRIX44 World;
      XMATRIX44 WorldView;
      XMATRIX44 WVPInverse;
      XMATRIX44 WVPLight;
	  XMATRIX44	Projection;
      XVECTOR3  LightPositions[128];
      XVECTOR3  LightColors[128];
      float  LightRadius[128];
      XVECTOR3  CameraPos;
      XVECTOR3  CameraInfo;
      XVECTOR3  LightCameraPos;
      XVECTOR3  LightCameraInfo;
      XVECTOR3 brightness;
	  XVECTOR3 toogles;
      XMATRIX44 ShadowViewProjection[6];
      XVECTOR3  ShadowSplitDepths[2];
      XVECTOR3  ShadowAtlasScaleBias[6];
      XVECTOR3  ShadowParams0;
      XVECTOR3  ShadowParams1;
      CBuffer() {
        brightness.x = 1;
      }
    };

    struct FrameCBuffer {
      XMATRIX44 WVP;
      XMATRIX44 World;
      XMATRIX44 WorldView;
      XMATRIX44 WVPInverse;
      XMATRIX44 WVPLight;
      XMATRIX44 Projection;
      XVECTOR3  CameraPos;
      XVECTOR3  CameraInfo;
      XVECTOR3  LightCameraPos;
      XVECTOR3  LightCameraInfo;
    };

    struct PassCBuffer {
      XVECTOR3  LightPositions[128];
      XVECTOR3  LightColors[128];
      float     LightRadius[128];
      XVECTOR3  brightness;
      XVECTOR3  toogles;
    };

    // Fixed max-six shadow sampling payload (slot b2). XVECTOR3 carries 4 floats.
    struct ShadowSamplingCBuffer {
      XMATRIX44 ViewProjection[6];
      XVECTOR3  SplitDepths[2];
      XVECTOR3  AtlasScaleBias[6];
      XVECTOR3  Params0; // x=viewCount, y=atlasWidth, z=atlasHeight, w=technique
      XVECTOR3  Params1; // x=farDistance, y=blendFraction, z=shadowBias, w=shadowMin
    };
    static_assert(sizeof(XMATRIX44) == 64, "XMATRIX44 must be 64 bytes");
    static_assert(sizeof(XVECTOR3) == 16, "XVECTOR3 must be 16 bytes");
    static_assert(sizeof(ShadowSamplingCBuffer) == 544, "ShadowSamplingCBuffer must be 544 bytes");

    RenderQuad() {
    }
    void Load(const char *) {};
    void Create();
    void Transform(float *t);
    void Draw(float *t, float *vp);
    void Destroy();
    void UploadShadowSamplingCB(const SceneProps& props);

    ShaderKey	sigBase;
    ConstantBuffer* pd3dConstantBuffer = nullptr;
    ConstantBuffer* FrameCBGPU = nullptr;
    ConstantBuffer* PassCBGPU = nullptr;
    ConstantBuffer* ShadowSamplingCBGPU = nullptr;
    ShadowSamplingCBuffer ShadowSamplingCB;
    int m_tiledLightHeaderTex = -1;
    int m_tiledLightIndexTex = -1;
    int m_tiledLightTilesX = 0;
    int m_tiledLightTilesY = 0;
    std::vector<float> m_tiledLightHeaderData;
    std::vector<float> m_tiledLightIndexData;
    std::vector<unsigned int> m_tiledLightCounts;
    //ID3D11SamplerState*  pSampler;
    Quad m_quad;
    CBuffer			CnstBuffer;
    FrameCBuffer FrameCB;
    PassCBuffer PassCB;
    XMATRIX44		transform;

  };
}

#endif

