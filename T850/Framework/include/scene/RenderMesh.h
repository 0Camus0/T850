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
    XVECTOR3  MaterialParams;  // .x=clearcoat .y=clearcoat roughness .z=unlit .w=emissive multiplier
    XVECTOR3  MaterialParams2; // .x=transmission multiplier .y=refraction strength .z=scene color bound .w=IBL factor
    XVECTOR3  MaterialParams3; // .x=IBL max mip .y=BRDF LUT enabled .z=diffuse IBL mip
    XVECTOR3  MaterialParams4; // .rgb=sheen color .w=sheen roughness
    XVECTOR3  MaterialParams5; // .x=sheen color map .y=sheen roughness map .z=color uv .w=roughness uv
    XVECTOR3  MaterialParams6; // .x=clearcoat map .y=clearcoat roughness map .z=factor uv .w=roughness uv
    XVECTOR3  MaterialParams7; // .x=occlusion map .y=occlusion strength .z=occlusion uv .w=transmission map
    XVECTOR3  MaterialParams8; // .x=transmission uv .y=specular factor map .z=specular factor uv .w=specular color map
    XVECTOR3  MaterialParams9; // .x=specular color uv
    XVECTOR3  BaseColorUVTransform0;
    XVECTOR3  BaseColorUVTransform1;
    XVECTOR3  NormalUVTransform0;
    XVECTOR3  NormalUVTransform1;
    XVECTOR3  MetallicUVTransform0;
    XVECTOR3  MetallicUVTransform1;
    XVECTOR3  EmissiveUVTransform0;
    XVECTOR3  EmissiveUVTransform1;
    XVECTOR3  SheenColorUVTransform0;
    XVECTOR3  SheenColorUVTransform1;
    XVECTOR3  SheenRoughnessUVTransform0;
    XVECTOR3  SheenRoughnessUVTransform1;
    XVECTOR3  ClearcoatUVTransform0;
    XVECTOR3  ClearcoatUVTransform1;
    XVECTOR3  ClearcoatRoughnessUVTransform0;
    XVECTOR3  ClearcoatRoughnessUVTransform1;
    XVECTOR3  OcclusionUVTransform0;
    XVECTOR3  OcclusionUVTransform1;
    XVECTOR3  SpecularFactorUVTransform0;
    XVECTOR3  SpecularFactorUVTransform1;
    XVECTOR3  SpecularColorUVTransform0;
    XVECTOR3  SpecularColorUVTransform1;
    XVECTOR3  TransmissionUVTransform0;
    XVECTOR3  TransmissionUVTransform1;
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
      ClearcoatFactor = 0.0f;
      ClearcoatRoughness = 0.0f;
      OcclusionStrength = 1.0f;
      SheenColor = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      SheenRoughness = 0.0f;
      Unlit = false;
      DiffuseTexCoord = 0;
      NormalTexCoord = 0;
      MetallicTexCoord = 0;
      EmissiveTexCoord = 0;
      SheenColorTexCoord = 0;
      SheenRoughnessTexCoord = 0;
      ClearcoatTexCoord = 0;
      ClearcoatRoughnessTexCoord = 0;
      OcclusionTexCoord = 0;
      SpecularFactorTexCoord = 0;
      SpecularColorTexCoord = 0;
      TransmissionTexCoord = 0;
      BaseColorUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      BaseColorUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      NormalUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      NormalUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      MetallicUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      MetallicUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      EmissiveUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      EmissiveUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      SheenColorUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      SheenColorUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      SheenRoughnessUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      SheenRoughnessUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      ClearcoatUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      ClearcoatUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      ClearcoatRoughnessUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      ClearcoatRoughnessUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      OcclusionUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      OcclusionUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      SpecularFactorUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      SpecularFactorUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      SpecularColorUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      SpecularColorUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
      TransmissionUVTransform0 = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
      TransmissionUVTransform1 = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
			MetallicTex = nullptr;
      EmissiveTex = nullptr;
			SheenColorTex = nullptr;
      SheenRoughnessTex = nullptr;
      ClearcoatTex = nullptr;
      ClearcoatRoughnessTex = nullptr;
      OcclusionTex = nullptr;
      SpecularFactorTex = nullptr;
      SpecularColorTex = nullptr;
      TransmissionTex = nullptr;
			MetallicId = -1;
      EmissiveId = -1;
			SheenColorId = -1;
      SheenRoughnessId = -1;
      ClearcoatId = -1;
      ClearcoatRoughnessId = -1;
      OcclusionId = -1;
      SpecularFactorId = -1;
      SpecularColorId = -1;
      TransmissionId = -1;
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
      Texture*					SheenColorTex;
      Texture*					SheenRoughnessTex;
      Texture*					ClearcoatTex;
      Texture*					ClearcoatRoughnessTex;
      Texture*					OcclusionTex;
      Texture*					SpecularFactorTex;
      Texture*					SpecularColorTex;
      Texture*					TransmissionTex;

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
      int					SheenColorId;
      int					SheenRoughnessId;
      int					ClearcoatId;
      int					ClearcoatRoughnessId;
      int					OcclusionId;
      int					SpecularFactorId;
      int					SpecularColorId;
      int					TransmissionId;

	  int					MatID;
      unsigned int      AlphaMode;
      float             AlphaCutoff;
      bool              DoubleSided;
      float             TransmissionFactor;
      float             IOR;
      float             ClearcoatFactor;
      float             ClearcoatRoughness;
      float             OcclusionStrength;
      XVECTOR3          SheenColor;
      float             SheenRoughness;
      bool              Unlit;
      unsigned int      DiffuseTexCoord;
      unsigned int      NormalTexCoord;
      unsigned int      MetallicTexCoord;
      unsigned int      EmissiveTexCoord;
      unsigned int      SheenColorTexCoord;
      unsigned int      SheenRoughnessTexCoord;
      unsigned int      ClearcoatTexCoord;
      unsigned int      ClearcoatRoughnessTexCoord;
      unsigned int      OcclusionTexCoord;
      unsigned int      SpecularFactorTexCoord;
      unsigned int      SpecularColorTexCoord;
      unsigned int      TransmissionTexCoord;
      XVECTOR3          BaseColorUVTransform0;
      XVECTOR3          BaseColorUVTransform1;
      XVECTOR3          NormalUVTransform0;
      XVECTOR3          NormalUVTransform1;
      XVECTOR3          MetallicUVTransform0;
      XVECTOR3          MetallicUVTransform1;
      XVECTOR3          EmissiveUVTransform0;
      XVECTOR3          EmissiveUVTransform1;
      XVECTOR3          SheenColorUVTransform0;
      XVECTOR3          SheenColorUVTransform1;
      XVECTOR3          SheenRoughnessUVTransform0;
      XVECTOR3          SheenRoughnessUVTransform1;
      XVECTOR3          ClearcoatUVTransform0;
      XVECTOR3          ClearcoatUVTransform1;
      XVECTOR3          ClearcoatRoughnessUVTransform0;
      XVECTOR3          ClearcoatRoughnessUVTransform1;
      XVECTOR3          OcclusionUVTransform0;
      XVECTOR3          OcclusionUVTransform1;
      XVECTOR3          SpecularFactorUVTransform0;
      XVECTOR3          SpecularFactorUVTransform1;
      XVECTOR3          SpecularColorUVTransform0;
      XVECTOR3          SpecularColorUVTransform1;
      XVECTOR3          TransmissionUVTransform0;
      XVECTOR3          TransmissionUVTransform1;

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


