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


#include <video/GLTexture.h>
#if defined(OS_WINDOWS)
#include <video/windows/D3DXTexture.h>
#endif

#include "scene/T8_Quad.h"
namespace t800 {
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
      CBuffer() {
        brightness.x = 1;
      }
    };

    RenderQuad() {
    }
    void Load(char *) {};
    void Create();
    void Transform(float *t);
    void Draw(float *t, float *vp);
    void Destroy();

    unsigned int	SigBase;
    ConstantBuffer* pd3dConstantBuffer;
    //ID3D11SamplerState*  pSampler;
    Quad m_quad;
    CBuffer			CnstBuffer;
    XMATRIX44		transform;

  };
}

#endif


