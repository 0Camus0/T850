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
#include <scene/MeshAsset.h>
#include <scene/MeshAssetCache.h>
#include <scene/MaterialAsset.h>



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
    XVECTOR3  MaterialParams9; // .x=specular color uv .y=normal scale
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

    struct MeshInstanceCBuffer {
      XMATRIX44 WVP;
      XMATRIX44 World;
      XMATRIX44 WorldView;
    };

    struct MeshFrameCBuffer {
      XVECTOR3  Light0Pos;
      XVECTOR3  Light0Col;
      XVECTOR3  CameraPos;
      XVECTOR3  CameraInfo;
      XVECTOR3  ParallaxSettings;
      XVECTOR3  ParallaxShadowSettings;
      XVECTOR3  Light0Dir;
      XVECTOR3  LightPositions[128];
      XVECTOR3  LightColors[128];
      XVECTOR3  LightRadius[32];
    };

    struct MeshMaterialCBuffer {
      XVECTOR3  AmbientColor;
      XVECTOR3  DiffuseColor;
      XVECTOR3  SpecularColor;
      XVECTOR3  PBRParams;
      XVECTOR3  Intensities;
      XVECTOR3  EmissiveColor;
      XVECTOR3  AlphaParams;
      XVECTOR3  ForwardParams;
      XVECTOR3  TexCoordSets;
      XVECTOR3  MaterialParams;
      XVECTOR3  MaterialParams2;
      XVECTOR3  MaterialParams3;
      XVECTOR3  MaterialParams4;
      XVECTOR3  MaterialParams5;
      XVECTOR3  MaterialParams6;
      XVECTOR3  MaterialParams7;
      XVECTOR3  MaterialParams8;
      XVECTOR3  MaterialParams9;
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
        // ── Phase B note ────────────────────────────────────────────
        //
        // The material-related fields below (texture pointers,
        // colors, factors, UV transforms, texCoord set selectors,
        // alphaMode/cutoff/doubleSided, etc.) are scratch storage
        // populated by the Create() pDefaults parse loop and copied
        // into a MaterialAsset prototype to acquire `matAsset` from
        // MaterialAssetCache. After Create() returns, the **draw
        // path reads exclusively via `matAsset->params` / `matAsset->
        // textures[]`**. These per-subset fields are kept as legacy
        // duplicates so the parse loop remains compact (~300 lines)
        // and unbreaks; rewriting the parse to populate the proto
        // directly is a deferred cleanup with no functional gain.
        //
        // Per-instance fields (key, MatID, NumTris/NumVertex/IB32Bit,
        // bounds, vbPoolAlloc/ibPoolAlloc, matAsset) are not in
        // MaterialAsset and remain authoritative here.
        // ────────────────────────────────────────────────────────────
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
      NormalScale = 1.0f;
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
      float             NormalScale;
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

      // Phase A.5 step 2: shared IB suballocation. Points into a
      // MeshAssetCache::IndexPool. Used by Draw() to skip per-subset
      // IB binds and to compute startIndex offsets.
      PoolAlloc ibPoolAlloc;

      uint32_t meshAssetSubmeshIndex = UINT32_MAX;

      // Phase B step 1: deduplicated material asset for this subset.
      // Borrowed pointer; lifetime managed by MaterialAssetCache via
      // RenderMesh::Destroy. Today the legacy material fields above
      // remain populated and drive the draw path; step 2 retires
      // them.
      MaterialAsset* matAsset = nullptr;

      MeshMaterialCBuffer MaterialCB;
    };

    struct MeshInfo {
      unsigned int			 VertexSize;
      unsigned int			 NumVertex;

      t850::IndexBuffer*  	IB = nullptr;
      t850::VertexBuffer*  	VB = nullptr;
      t850::ConstantBuffer* CB = nullptr;
      t850::ConstantBuffer* FrameCBGPU = nullptr;
      t850::ConstantBuffer* InstanceCBGPU = nullptr;
      t850::ConstantBuffer* MaterialCBGPU = nullptr;
      RenderMesh::CBuffer			CnstBuffer;
      MeshInstanceCBuffer   InstanceCB;
      MeshFrameCBuffer      FrameCB;

      std::vector<SubSetInfo>	SubSets;

      AABB bounds;

      // Phase A.5 step 2: shared VB suballocation. Points into a
      // MeshAssetCache::VertexPool. Used by Draw() instead of binding
      // the per-asset MeshInfo::VB.
      PoolAlloc vbPoolAlloc;
    };

    void Load(const char *);
    void Create();
    void Transform(float *t);
    void Draw(float *t, float *vp);
    void Destroy();

    void GatherInfo();
    int  LoadTex(std::string p, xF::xMaterial *mat, Texture** tex);

    enum class FrustumResult {
      Outside = 0,
      Intersecting = 1,
      Inside = 2
    };

    // Frustum culling: extract 6 planes from row-vector VP matrix
    static void ExtractFrustumPlanes(const XMATRIX44& vp, XVECTOR3 planes[6]);
    static FrustumResult ClassifyAABBFrustum(const AABB& box, const XMATRIX44& world, const XVECTOR3 planes[6]);
    static bool AABBInsideFrustum(const AABB& box, const XMATRIX44& world, const XVECTOR3 planes[6]);

    // Culling stats (per draw pass)
    mutable int m_totalMeshes = 0;
    mutable int m_visibleMeshes = 0;
    mutable int m_culledMeshes = 0;
    mutable int m_totalSubsets = 0;
    mutable int m_visibleSubsets = 0;
    mutable int m_culledSubsets = 0;
    mutable int m_drawnSubsets = 0;
    mutable int m_totalClusters = 0;
    mutable int m_visibleClusters = 0;
    mutable int m_culledClusters = 0;
    mutable int m_drawnClusters = 0;
    mutable unsigned long long m_totalIndices = 0;
    mutable unsigned long long m_drawnIndices = 0;
    mutable unsigned long long m_culledIndices = 0;
    mutable unsigned long long m_cullingMeshTests = 0;
    mutable unsigned long long m_cullingSubsetTests = 0;
    mutable unsigned long long m_cullingClusterTests = 0;
    mutable unsigned long long m_drawCalls = 0;
    mutable unsigned long long m_renderStateChanges = 0;
    mutable double m_cullingCpuMs = 0.0;

    Texture*	d3dxEnvMap;

    XMATRIX44	transform;
    XDataBase*	xFile;
    std::vector<MeshInfo> Info;

    // Phase A: shared geometry asset (path-deduplicated through
    // MeshAssetCache). Borrowed pointer — cache owns the asset and its
    // lifetime. Populated in Create(); released in Destroy().
    MeshAsset*  m_asset = nullptr;
    std::string m_sourcePath;

    std::vector<uint8_t> m_visibilityScratch;
    std::vector<std::size_t> m_geometryOrderScratch;
    std::vector<std::size_t> m_drawOrderScratch;
  };
}

#endif


