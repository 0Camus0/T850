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

#ifndef T800_MESH_D3D_H
#define T800_MESH_D3D_H

#include <Config.h>

#ifdef USING_OPENGL_ES20
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <video/GLTexture.h>
#elif defined(USING_OPENGL_ES30)
#include <GLES3/gl3.h>
#include <video/GLTexture.h>
#elif defined(USING_OPENGL_ES31)
#include <GLES3/gl31.h>
#include <video/GLTexture.h>
#elif defined(USING_OPENGL)
#include <GL/glew.h>
#include <video/GLTexture.h>
#endif

#if defined(OS_WINDOWS)
#include <video/windows/D3DXTexture.h>
#include <D3Dcompiler.h>
#endif




#include <video/BaseDriver.h>

#include <utils/Utils.h>

#include <utils/xMaths.h>
#include <utils/XDataBase.h>
#include <scene/PrimitiveBase.h>



#include <vector>
#include <memory>
using namespace xF;
namespace t800 {
  class RenderMesh : public PrimitiveBase {
  public:
    RenderMesh() {
      d3dxEnvMap = 0;
      EnvMap = 0;
    }

    struct CBuffer {
      XMATRIX44 WVP;
      XMATRIX44 World;
      XMATRIX44 WorldView;
      XVECTOR3  Light0Pos;
      XVECTOR3  Light0Col;
      XVECTOR3  CameraPos;
      XVECTOR3  CameraInfo;
      XVECTOR3  AmbientColor;
	  XVECTOR3  DiffuseColor;
	  XVECTOR3  SpecularColor;
	  XVECTOR3  PBRParams;       // .x=metallic .y=roughness (fallbacks)
	  XVECTOR3  Intensities;     // .w=MatID
	  XVECTOR3  ParallaxSettings;
    };


    struct SubSetInfo {
		SubSetInfo() {
			AmbientColor = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
			DiffuseColor = XVECTOR3(0.5f, 0.5f, 0.5f, 1.0f);
			SpecularColor = XVECTOR3(0.04f, 0.04f, 0.04f, 1.0f);
			PBRParams = XVECTOR3(0.0f, 0.8f, 0.0f, 0.0f);  // metallic=0, roughness=0.8
			Intensities = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
			MetallicTex = nullptr;
			MetallicId = -1;
			bUseFresnel = false;
			MatID = 0;
		}
      ShaderKey		key;

      t800::IndexBuffer*  	IB;
      Texture*					DiffuseTex;
      Texture*					SpecularTex;
      Texture*					GlossfTex;
      Texture*					NormalTex;
      Texture*					ReflectTex;
      Texture*					ParalaxTex;
      Texture*					MetallicTex;

	  XVECTOR3		  AmbientColor;
	  XVECTOR3	      DiffuseColor;
	  XVECTOR3		  SpecularColor;
	  XVECTOR3		  PBRParams;       // .x=metallic .y=roughness
	  XVECTOR3        Intensities;

      int					DiffuseId;
      int					SpecularId;
      int					GlossfId;
      int					NormalId;
      int					ReflectId;
      int					ParalaxId;
      int					MetallicId;
	  
	  int					MatID;

      unsigned int		VertexStart;
      unsigned int		NumVertex;
      unsigned int		TriStart;
      unsigned int		NumTris;
      unsigned int		VertexSize;
      bool				bAlignedVertex;
	  bool				bUseFresnel;
    };


    struct MeshInfo {
      unsigned int			 VertexSize;
      unsigned int			 NumVertex;

      t800::IndexBuffer*  	IB;
      t800::VertexBuffer*  	VB;
      t800::ConstantBuffer* CB;
      RenderMesh::CBuffer			CnstBuffer;

      std::vector<SubSetInfo>	SubSets;
    };

    void Load(const char *);
    void Create();
    void Transform(float *t);
    void Draw(float *t, float *vp);
    void Destroy();

    void GatherInfo();
    int  LoadTex(std::string p, xF::xMaterial *mat, Texture** tex);

    Texture*	d3dxEnvMap;

    XMATRIX44	transform;
    XDataBase*	xFile;
    std::vector<MeshInfo> Info;
  };
}

#endif


