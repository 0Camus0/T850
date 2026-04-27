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
#include <video/gl/GLTexture.h>
#elif defined(USING_OPENGL_ES30)
#include <GLES3/gl3.h>
#include <video/gl/GLTexture.h>
#elif defined(USING_OPENGL_ES31)
#include <GLES3/gl31.h>
#include <video/gl/GLTexture.h>
#elif defined(USING_OPENGL)
#include <GL/glew.h>
#include <video/gl/GLTexture.h>
#endif

#if defined(OS_WINDOWS)
#include <video/d3d11/D3D11Texture.h>
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
namespace t850 {
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
	  XVECTOR3  ParallaxShadowSettings;
	  XVECTOR3  Light0Dir;
    XVECTOR3  EmissiveColor;
    XVECTOR3  AlphaParams;     // .x=mode 0/1/2 .y=cutoff .z=doubleSided .w=transmission
    XVECTOR3  ForwardParams;   // .xy=screen size .z=scene depth bound .w=ior
    XVECTOR3  TexCoordSets;    // .x=baseColor .y=normal .z=metallicRoughness .w=emissive
    XVECTOR3  LightPositions[128];
    XVECTOR3  LightColors[128];
    XVECTOR3  LightRadius[32];
    };

    struct AABB {
      XVECTOR3 min;
      XVECTOR3 max;
      void Reset() {
        min = XVECTOR3( 1e18f,  1e18f,  1e18f);
        max = XVECTOR3(-1e18f, -1e18f, -1e18f);
      }
      void Expand(float x, float y, float z) {
        if (x < min.x) min.x = x; if (y < min.y) min.y = y; if (z < min.z) min.z = z;
        if (x > max.x) max.x = x; if (y > max.y) max.y = y; if (z > max.z) max.z = z;
      }
    };

    struct SubSetInfo {
		SubSetInfo() {
			AmbientColor = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
			DiffuseColor = XVECTOR3(0.5f, 0.5f, 0.5f, 1.0f);
			SpecularColor = XVECTOR3(0.04f, 0.04f, 0.04f, 1.0f);
			PBRParams = XVECTOR3(0.0f, 0.8f, 0.0f, 0.0f);  // metallic=0, roughness=0.8
			Intensities = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
      EmissiveColor = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
      AlphaMode = 0;
      AlphaCutoff = 0.5f;
      DoubleSided = false;
      TransmissionFactor = 0.0f;
      IOR = 1.5f;
      DiffuseTexCoord = 0;
      NormalTexCoord = 0;
      MetallicTexCoord = 0;
      EmissiveTexCoord = 0;
			MetallicTex = nullptr;
      EmissiveTex = nullptr;
			MetallicId = -1;
      EmissiveId = -1;
			bUseFresnel = false;
			MatID = 0;
		}
      ShaderKey		key;

      t850::IndexBuffer*  	IB;
      Texture*					DiffuseTex;
      Texture*					SpecularTex;
      Texture*					GlossfTex;
      Texture*					NormalTex;
      Texture*					ReflectTex;
      Texture*					ParalaxTex;
      Texture*					MetallicTex;
      Texture*					EmissiveTex;

	  XVECTOR3		  AmbientColor;
	  XVECTOR3	      DiffuseColor;
	  XVECTOR3		  SpecularColor;
	  XVECTOR3		  PBRParams;       // .x=metallic .y=roughness
	  XVECTOR3        Intensities;
      XVECTOR3        EmissiveColor;

      int					DiffuseId;
      int					SpecularId;
      int					GlossfId;
      int					NormalId;
      int					ReflectId;
      int					ParalaxId;
      int					MetallicId;
      int					EmissiveId;

	  int					MatID;
      unsigned int      AlphaMode;
      float             AlphaCutoff;
      bool              DoubleSided;
      float             TransmissionFactor;
      float             IOR;
      unsigned int      DiffuseTexCoord;
      unsigned int      NormalTexCoord;
      unsigned int      MetallicTexCoord;
      unsigned int      EmissiveTexCoord;

      unsigned int		VertexStart;
      unsigned int		NumVertex;
      unsigned int		TriStart;
      unsigned int		NumTris;
      unsigned int		VertexSize;
      bool				bAlignedVertex;
	  bool				bUseFresnel;
	  bool				IB32Bit = false;   // selects R16/R32 in Set()

      AABB bounds;  // per-subset bounding box for fine-grained culling
    };

    struct MeshInfo {
      unsigned int			 VertexSize;
      unsigned int			 NumVertex;

      t850::IndexBuffer*  	IB;
      t850::VertexBuffer*  	VB;
      t850::ConstantBuffer* CB;
      RenderMesh::CBuffer			CnstBuffer;

      std::vector<SubSetInfo>	SubSets;

      AABB bounds;
    };

    void Load(const char *);
    void Create();
    void Transform(float *t);
    void Draw(float *t, float *vp);
    void Destroy();

    void GatherInfo();
    int  LoadTex(std::string p, xF::xMaterial *mat, Texture** tex);

    // Frustum culling: extract 6 planes from row-vector VP matrix
    static void ExtractFrustumPlanes(const XMATRIX44& vp, XVECTOR3 planes[6]);
    static bool AABBInsideFrustum(const AABB& box, const XMATRIX44& world, const XVECTOR3 planes[6]);

    // Culling stats (per frame)
    mutable int m_totalSubsets = 0;
    mutable int m_drawnSubsets = 0;
    mutable int m_culledMeshes = 0;

    Texture*	d3dxEnvMap;

    XMATRIX44	transform;
    XDataBase*	xFile;
    std::vector<MeshInfo> Info;
  };
}

#endif


