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

#include <video/BaseDriver.h>
#include <scene/RenderQueue.h>   // MeshDrawStateTracker
#include <iostream>
#include <cmath>
#include <algorithm>

#include <scene/RenderMesh.h>
#include <scene/RenderGraph.h>
#include <utils/ThreadPool.h>
#include <video/gl/GLShader.h>
#include <video/gl/GLDriver.h>

#if defined(OS_WINDOWS)
#include <video/d3d11/D3D11Shader.h>
#include <video/d3d11/D3D11Driver.h>
#endif
#include <core/Core.h>
#include <utils/Log.h>

#define CHANGE_TO_RH 0
#define DEBUG_MODEL 0
extern t850::AppBase		  *pApp;
namespace t850 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  static constexpr unsigned MaterialSamplerSlot = 0;
  static constexpr unsigned ClampSamplerSlot = 1;

  namespace {
    void AssignEffectFloat4(XVECTOR3& target, const std::vector<float>& values) {
      if (values.size() >= 4)
        target = XVECTOR3(values[0], values[1], values[2], values[3]);
    }

    const char* AlphaModeName(unsigned int mode) {
      switch (mode) {
      case 1: return "MASK";
      case 2: return "BLEND";
      default: return "OPAQUE";
      }
    }

    void LogLoadedMeshDetails(const XDataBase* xFile, const std::vector<RenderMesh::MeshInfo>& meshInfos) {
      if (!xFile || xFile->XMeshDataBase.empty())
        return;

      const xMeshContainer* meshContainer = xFile->XMeshDataBase[0];
      const char* meshName = xFile->m_name.empty() ? "<unnamed>" : xFile->m_name.c_str();
      unsigned long long totalTriangles = 0;
      std::size_t totalMaterials = 0;
      std::size_t totalSubsets = 0;

      for (std::size_t i = 0; i < meshContainer->Geometry.size(); ++i) {
        const xMeshGeometry& geometry = meshContainer->Geometry[i];
        totalTriangles += static_cast<unsigned long long>(geometry.NumTriangles);
        totalMaterials += geometry.MaterialList.Materials.size();
        if (i < meshInfos.size())
          totalSubsets += meshInfos[i].SubSets.size();
      }

      T8_LOG_INFO("[MeshInfo] Loaded mesh '%s': geometries=%zu materials=%zu subsets=%zu triangles=%llu",
                  meshName, meshContainer->Geometry.size(), totalMaterials, totalSubsets, totalTriangles);

      for (std::size_t i = 0; i < meshContainer->Geometry.size(); ++i) {
        const xMeshGeometry& geometry = meshContainer->Geometry[i];
        const xFinalGeometry* finalGeometry = i < xFile->MeshInfo.size() ? &xFile->MeshInfo[i] : nullptr;
        const RenderMesh::MeshInfo* meshInfo = i < meshInfos.size() ? &meshInfos[i] : nullptr;
        const char* geometryName = geometry.Name.empty() ? "<unnamed>" : geometry.Name.c_str();
        std::size_t subsetCount = meshInfo ? meshInfo->SubSets.size() : 0;
        std::size_t sourceSubsetCount = finalGeometry ? finalGeometry->Subsets.size() : 0;
        std::size_t rowCount = std::max(geometry.MaterialList.Materials.size(), subsetCount);

        T8_LOG_INFO("[MeshInfo]   Geometry[%zu] '%s': vertices=%u triangles=%u indices=%u vertexSize=%u attrs=0x%08X materials=%zu subsets=%zu sourceSubsets=%zu index=%s",
                    i, geometryName, geometry.NumVertices, geometry.NumTriangles, geometry.NumIndices,
                    geometry.VertexSize, geometry.VertexAttributes, geometry.MaterialList.Materials.size(),
                    subsetCount, sourceSubsetCount, geometry.Indices32Bit ? "R32" : "R16");

        for (std::size_t j = 0; j < rowCount; ++j) {
          const xMaterial* material = j < geometry.MaterialList.Materials.size() ? &geometry.MaterialList.Materials[j] : nullptr;
          const xSubsetInfo* sourceSubset = finalGeometry && j < finalGeometry->Subsets.size() ? &finalGeometry->Subsets[j] : nullptr;
          const RenderMesh::SubSetInfo* subset = meshInfo && j < meshInfo->SubSets.size() ? &meshInfo->SubSets[j] : nullptr;
          const char* materialName = material && !material->Name.empty() ? material->Name.c_str() : "<unnamed>";

          T8_LOG_INFO("[MeshInfo]     Material[%zu] '%s': defaults=%zu subsetTris=%u subsetVertices=%u triStart=%u vertexStart=%u shaderKey=0x%016llX matID=%d alpha=%s cutoff=%.3f doubleSided=%d unlit=%d fresnel=%d",
                      j, materialName, material ? material->EffectInstance.pDefaults.size() : 0,
                      subset ? subset->NumTris : (sourceSubset ? sourceSubset->NumTris : 0),
                      subset ? subset->NumVertex : (sourceSubset ? sourceSubset->NumVertex : 0),
                      subset ? subset->TriStart : (sourceSubset ? sourceSubset->TriStart : 0),
                      subset ? subset->VertexStart : (sourceSubset ? sourceSubset->VertexStart : 0),
                      static_cast<unsigned long long>(subset ? subset->key.bits : 0ull),
                      subset ? subset->MatID : 0,
                      subset ? AlphaModeName(subset->AlphaMode) : "UNKNOWN",
                      subset ? subset->AlphaCutoff : 0.0f,
                      subset ? (int)subset->DoubleSided : 0,
                      subset ? (int)subset->Unlit : 0,
                      subset ? (int)subset->bUseFresnel : 0);

          if (subset) {
            T8_LOG_INFO("[MeshInfo]       PBR: base=(%.3f, %.3f, %.3f, %.3f) spec=(%.3f, %.3f, %.3f, %.3f) metallic=%.3f roughness=%.3f emissive=(%.3f, %.3f, %.3f) transmission=%.3f ior=%.3f clearcoat=(%.3f, %.3f) sheen=(%.3f, %.3f, %.3f, %.3f)",
                        subset->DiffuseColor.x, subset->DiffuseColor.y, subset->DiffuseColor.z, subset->DiffuseColor.w,
                        subset->SpecularColor.x, subset->SpecularColor.y, subset->SpecularColor.z, subset->SpecularColor.w,
                        subset->PBRParams.x, subset->PBRParams.y,
                        subset->EmissiveColor.x, subset->EmissiveColor.y, subset->EmissiveColor.z,
                        subset->TransmissionFactor, subset->IOR,
                        subset->ClearcoatFactor, subset->ClearcoatRoughness,
                        subset->SheenColor.x, subset->SheenColor.y, subset->SheenColor.z, subset->SheenRoughness);

            T8_LOG_INFO("[MeshInfo]       KeyFlags: normals=%d tangents=%d binormals=%d uv0=%d uv1=%d uv2=%d uv3=%d baseColorMap=%d specularMap=%d roughnessMap=%d normalMap=%d heightMap=%d metallicMap=%d emissiveMap=%d clearcoatMap=%d clearcoatRoughnessMap=%d sheenColorMap=%d sheenRoughnessMap=%d occlusionMap=%d specularFactorMap=%d specularColorMap=%d transmissionMap=%d gltfTangentSpace=%d",
                        subset->key.has(ShaderKey::HAS_NORMALS) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_TANGENTS) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_BINORMALS) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_TEXCOORD0) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_TEXCOORD1) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_TEXCOORD2) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_TEXCOORD3) ? 1 : 0,
                        subset->key.has(ShaderKey::DIFFUSE_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::SPECULAR_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::GLOSS_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::NORMAL_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::HEIGHT_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::METALLIC_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::EMISSIVE_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::CLEARCOAT_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::CLEARCOAT_ROUGHNESS_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::SHEEN_COLOR_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::SHEEN_ROUGHNESS_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::OCCLUSION_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::SPECULAR_FACTOR_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::SPECULAR_COLOR_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::TRANSMISSION_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::GLTF_TANGENT_SPACE) ? 1 : 0);
          }
        }
      }
    }
  }


  void RenderMesh::Load(const char *filename)
  {
    xFile = pApp->resourceManager.Load(filename);

    // Phase A: register this mesh with the shared asset cache. We keep
    // a borrowed pointer — population happens at the end of Create()
    // once GPU buffers exist; release happens in Destroy().
    m_sourcePath = filename ? filename : "";
    bool created = false;
    m_asset = MeshAssetCache::Get().Acquire(m_sourcePath, &created);
    T8_LOG_INFO("[MeshAssetCache] %s '%s' (refs=%u, total assets=%zu)",
                created ? "MISS — first acquisition" : "HIT  — reused",
                m_sourcePath.c_str(),
                m_asset ? m_asset->refCount : 0u,
                MeshAssetCache::Get().Size());
  }

  void RenderMesh::Create() {
    GatherInfo();
    T8_LOG_INFO("Mesh Create: %zu geometries, building GPU buffers", xFile->MeshInfo.size());

    // Phase A step 3: detect whether the cached asset already owns its
    // GPU resources (second+ acquisition of the same source path). If
    // yes, skip every CreateBuffer(VERTEX/INDEX) call below and alias
    // MeshInfo::VB/IB and SubSetInfo::IB to the cached pointers; the
    // CB stays per-instance because it carries per-frame transform.
    // Phase A.5 step 1: shadow per-geometry VB and per-subset IB
    // suballocations into the cache's pools. These are populated only
    // on FIRST acquisition of an asset (refCount==1, geometries empty
    // before this Create). Step 1 keeps the legacy GPU buffers and
    // does NOT use these in Draw — purely diagnostic for now.
    // Phase A.5 step 3: VB and per-subset IBs are now owned by the
    // shared pools (MeshAssetCache::m_vertexPools / m_indexPools).
    // The legacy MeshInfo::VB / SubSetInfo::IB / MeshInfo::IB pointers
    // remain as defensive nullable fallbacks but are no longer
    // created here; the draw path picks pool buffers via the
    // *PoolAlloc fields and never dereferences the legacy pointers
    // unless the alloc is invalid (which shouldn't happen in normal
    // load).
    const bool populatePools = (m_asset && m_asset->submeshes.empty());
    struct VBAllocSide { uint32_t poolId = UINT32_MAX; uint32_t offsetVerts = 0; uint32_t count = 0; };
    struct IBAllocSide { uint32_t poolId = UINT32_MAX; uint32_t offsetIdx   = 0; uint32_t count = 0; };
    std::vector<VBAllocSide>             poolVBAllocs(xFile->MeshInfo.size());
    std::vector<std::vector<IBAllocSide>> poolIBAllocs(xFile->MeshInfo.size());

    for (std::size_t i = 0; i < xFile->MeshInfo.size(); i++) {
      xFinalGeometry *it = &xFile->MeshInfo[i];
      xMeshGeometry *pActual = &xFile->XMeshDataBase[0]->Geometry[i];
      MeshInfo  *it_MeshInfo = &Info[i];
      if (populatePools) {
        poolIBAllocs[i].resize(it_MeshInfo->SubSets.size());
      }

      t850::BufferDesc bdesc;
      bdesc.byteWidth = sizeof(RenderMesh::CBuffer);
      bdesc.usage = BufferUsage::DEFAULT;
      it_MeshInfo->CB = (t850::ConstantBuffer*)T8Device->CreateBuffer(BufferType::CONSTANT, bdesc);

      int NumMaterials = static_cast<int>(pActual->MaterialList.Materials.size());
      int NumFaceIndices = static_cast<int>(pActual->MaterialList.FaceIndices.size());
      const bool kUse32 = pActual->Indices32Bit;

      for (int j = 0; j < NumMaterials; j++) {
        xSubsetInfo *subinfo = &it->Subsets[j];
        xMaterial *material = &pActual->MaterialList.Materials[j];
        SubSetInfo *it_subsetinfo = &it_MeshInfo->SubSets[j];

        for (unsigned int k = 0; k < material->EffectInstance.pDefaults.size(); k++) {
          xEffectDefault *mDef = &material->EffectInstance.pDefaults[k];

		  if (mDef->Type == xF::xEFFECTENUM::STDX_FLOATS) {
        auto assignUVTransform = [&](const char* key, XVECTOR3& target) {
          if (mDef->NameParam == key)
            AssignEffectFloat4(target, mDef->CaseFloat);
        };

			  if (mDef->NameParam == "ambientcolor") {
				  it_subsetinfo->AmbientColor.x = mDef->CaseFloat[0];
				  it_subsetinfo->AmbientColor.y = mDef->CaseFloat[1];
				  it_subsetinfo->AmbientColor.z = mDef->CaseFloat[2];
				  it_subsetinfo->AmbientColor.w = 1.0f;
			  }

			  if (mDef->NameParam == "diffuseColor") {
				  it_subsetinfo->DiffuseColor.x = mDef->CaseFloat[0];
				  it_subsetinfo->DiffuseColor.y = mDef->CaseFloat[1];
				  it_subsetinfo->DiffuseColor.z = mDef->CaseFloat[2];
          it_subsetinfo->DiffuseColor.w = mDef->CaseFloat.size() > 3 ? mDef->CaseFloat[3] : 1.0f;
			  }

			  if (mDef->NameParam == "specularColor") {
				  it_subsetinfo->SpecularColor.x = mDef->CaseFloat[0];
				  it_subsetinfo->SpecularColor.y = mDef->CaseFloat[1];
				  it_subsetinfo->SpecularColor.z = mDef->CaseFloat[2];
          it_subsetinfo->SpecularColor.w = mDef->CaseFloat.size() > 3 ? mDef->CaseFloat[3] : 1.0f;
			  }

			  if (mDef->NameParam == "FresnelColor") {
				  // Legacy: ignored in PBR
			  }

			  if (mDef->NameParam == "pbrMetallic") {
				  it_subsetinfo->PBRParams.x = mDef->CaseFloat[0];
			  }

			  if (mDef->NameParam == "pbrRoughness") {
				  it_subsetinfo->PBRParams.y = mDef->CaseFloat[0];
			  }

        if (mDef->NameParam == "emissiveColor") {
          it_subsetinfo->EmissiveColor.x = mDef->CaseFloat[0];
          it_subsetinfo->EmissiveColor.y = mDef->CaseFloat[1];
          it_subsetinfo->EmissiveColor.z = mDef->CaseFloat[2];
          it_subsetinfo->EmissiveColor.w = 1.0f;
        }

        if (mDef->NameParam == "alphaCutoff") {
          it_subsetinfo->AlphaCutoff = mDef->CaseFloat[0];
        }

        if (mDef->NameParam == "transmissionFactor") {
          it_subsetinfo->TransmissionFactor = mDef->CaseFloat[0];
        }

        if (mDef->NameParam == "ior") {
          it_subsetinfo->IOR = mDef->CaseFloat[0];
        }

        if (mDef->NameParam == "clearcoatFactor") {
          it_subsetinfo->ClearcoatFactor = mDef->CaseFloat[0];
        }

        if (mDef->NameParam == "clearcoatRoughness") {
          it_subsetinfo->ClearcoatRoughness = mDef->CaseFloat[0];
        }

        if (mDef->NameParam == "occlusionStrength") {
          it_subsetinfo->OcclusionStrength = mDef->CaseFloat[0];
        }

        if (mDef->NameParam == "normalScale") {
          it_subsetinfo->NormalScale = mDef->CaseFloat[0];
        }

        if (mDef->NameParam == "sheenColor") {
          it_subsetinfo->SheenColor.x = mDef->CaseFloat[0];
          it_subsetinfo->SheenColor.y = mDef->CaseFloat[1];
          it_subsetinfo->SheenColor.z = mDef->CaseFloat[2];
          it_subsetinfo->SheenColor.w = 0.0f;
        }

        if (mDef->NameParam == "sheenRoughness") {
          it_subsetinfo->SheenRoughness = mDef->CaseFloat[0];
        }

        assignUVTransform("diffuseUVTransform0", it_subsetinfo->BaseColorUVTransform0);
        assignUVTransform("diffuseUVTransform1", it_subsetinfo->BaseColorUVTransform1);
        assignUVTransform("normalUVTransform0", it_subsetinfo->NormalUVTransform0);
        assignUVTransform("normalUVTransform1", it_subsetinfo->NormalUVTransform1);
        assignUVTransform("metallicUVTransform0", it_subsetinfo->MetallicUVTransform0);
        assignUVTransform("metallicUVTransform1", it_subsetinfo->MetallicUVTransform1);
        assignUVTransform("emissiveUVTransform0", it_subsetinfo->EmissiveUVTransform0);
        assignUVTransform("emissiveUVTransform1", it_subsetinfo->EmissiveUVTransform1);
        assignUVTransform("sheenColorUVTransform0", it_subsetinfo->SheenColorUVTransform0);
        assignUVTransform("sheenColorUVTransform1", it_subsetinfo->SheenColorUVTransform1);
        assignUVTransform("sheenRoughnessUVTransform0", it_subsetinfo->SheenRoughnessUVTransform0);
        assignUVTransform("sheenRoughnessUVTransform1", it_subsetinfo->SheenRoughnessUVTransform1);
        assignUVTransform("clearcoatUVTransform0", it_subsetinfo->ClearcoatUVTransform0);
        assignUVTransform("clearcoatUVTransform1", it_subsetinfo->ClearcoatUVTransform1);
        assignUVTransform("clearcoatRoughnessUVTransform0", it_subsetinfo->ClearcoatRoughnessUVTransform0);
        assignUVTransform("clearcoatRoughnessUVTransform1", it_subsetinfo->ClearcoatRoughnessUVTransform1);
        assignUVTransform("occlusionUVTransform0", it_subsetinfo->OcclusionUVTransform0);
        assignUVTransform("occlusionUVTransform1", it_subsetinfo->OcclusionUVTransform1);
        assignUVTransform("specularFactorUVTransform0", it_subsetinfo->SpecularFactorUVTransform0);
        assignUVTransform("specularFactorUVTransform1", it_subsetinfo->SpecularFactorUVTransform1);
        assignUVTransform("specularColorUVTransform0", it_subsetinfo->SpecularColorUVTransform0);
        assignUVTransform("specularColorUVTransform1", it_subsetinfo->SpecularColorUVTransform1);
        assignUVTransform("transmissionUVTransform0", it_subsetinfo->TransmissionUVTransform0);
        assignUVTransform("transmissionUVTransform1", it_subsetinfo->TransmissionUVTransform1);

			  if (mDef->NameParam == "speclevel") {
				  // Legacy: ignored in PBR
			  }

			  if (mDef->NameParam == "glossiness") {
				  // Legacy: ignored in PBR
			  }

			  if (mDef->NameParam == "FresnelMult") {
				  // Legacy: ignored in PBR
			  }
		  }

          if (mDef->Type == xF::xEFFECTENUM::STDX_STRINGS) {
#if DEBUG_MODEL
            std::cout << "[" << mDef->NameParam << "]" << std::endl;
#endif
            if (mDef->NameParam == "diffuseMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif

              it_subsetinfo->DiffuseId = LoadTex(path, material, &it_subsetinfo->DiffuseTex);

            }

            if (mDef->NameParam == "specularMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->SpecularId = LoadTex(path, material, &it_subsetinfo->SpecularTex);
            }

            if (mDef->NameParam == "glossMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->GlossfId = LoadTex(path, material, &it_subsetinfo->GlossfTex);
            }

            if (mDef->NameParam == "normalMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->NormalId = LoadTex(path, material, &it_subsetinfo->NormalTex);;
            }

            if (mDef->NameParam == "heightMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->ParalaxId = LoadTex(path, material, &it_subsetinfo->ParalaxTex);;
            }

            if (mDef->NameParam == "metallicMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->MetallicId = LoadTex(path, material, &it_subsetinfo->MetallicTex);
            }

            if (mDef->NameParam == "emissiveMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->EmissiveId = LoadTex(path, material, &it_subsetinfo->EmissiveTex);
            }

            if (mDef->NameParam == "sheenColorMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->SheenColorId = LoadTex(path, material, &it_subsetinfo->SheenColorTex);
            }

            if (mDef->NameParam == "sheenRoughnessMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->SheenRoughnessId = LoadTex(path, material, &it_subsetinfo->SheenRoughnessTex);
            }

            if (mDef->NameParam == "clearcoatMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->ClearcoatId = LoadTex(path, material, &it_subsetinfo->ClearcoatTex);
            }

            if (mDef->NameParam == "clearcoatRoughnessMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->ClearcoatRoughnessId = LoadTex(path, material, &it_subsetinfo->ClearcoatRoughnessTex);
            }

            if (mDef->NameParam == "occlusionMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->OcclusionId = LoadTex(path, material, &it_subsetinfo->OcclusionTex);
            }

            if (mDef->NameParam == "specularFactorMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->SpecularFactorId = LoadTex(path, material, &it_subsetinfo->SpecularFactorTex);
            }

            if (mDef->NameParam == "specularColorMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->SpecularColorId = LoadTex(path, material, &it_subsetinfo->SpecularColorTex);
            }

            if (mDef->NameParam == "transmissionMap") {
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              std::cout << "path[" << path << "]" << std::endl;
#endif
              it_subsetinfo->TransmissionId = LoadTex(path, material, &it_subsetinfo->TransmissionTex);
            }
          }

          if (mDef->Type == xF::xEFFECTENUM::STDX_DWORDS) {
            if (mDef->NameParam == "alphaMode") {
              it_subsetinfo->AlphaMode = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "doubleSided") {
              it_subsetinfo->DoubleSided = (mDef->CaseDWORD != 0);
            }
            if (mDef->NameParam == "transmission") {
              if (mDef->CaseDWORD != 0 && it_subsetinfo->TransmissionFactor <= 0.0f)
                it_subsetinfo->TransmissionFactor = 1.0f;
            }
            if (mDef->NameParam == "unlit") {
              it_subsetinfo->Unlit = (mDef->CaseDWORD != 0);
            }
            if (mDef->NameParam == "diffuseTexCoord") {
              it_subsetinfo->DiffuseTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "normalTexCoord") {
              it_subsetinfo->NormalTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "metallicTexCoord") {
              it_subsetinfo->MetallicTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "emissiveTexCoord") {
              it_subsetinfo->EmissiveTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "sheenColorTexCoord") {
              it_subsetinfo->SheenColorTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "sheenRoughnessTexCoord") {
              it_subsetinfo->SheenRoughnessTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "clearcoatTexCoord") {
              it_subsetinfo->ClearcoatTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "clearcoatRoughnessTexCoord") {
              it_subsetinfo->ClearcoatRoughnessTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "occlusionTexCoord") {
              it_subsetinfo->OcclusionTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "specularFactorTexCoord") {
              it_subsetinfo->SpecularFactorTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "specularColorTexCoord") {
              it_subsetinfo->SpecularColorTexCoord = mDef->CaseDWORD;
            }
            if (mDef->NameParam == "transmissionTexCoord") {
              it_subsetinfo->TransmissionTexCoord = mDef->CaseDWORD;
            }
          }
        }

        // Phase B step 1: build a MaterialAsset prototype from the
        // now-populated it_subsetinfo and acquire it from the dedup
        // cache. The cache stores a copy; SubSetInfo keeps a borrowed
        // pointer so the future Phase B step 2 draw path can read
        // texture/param data via the cached asset instead of the
        // bloated SubSetInfo. Note: this runs in Create() (not
        // GatherInfo) because GatherInfo populates `stmp` with mostly
        // defaults; only the post-pDefaults `it_subsetinfo` carries
        // the real material data.
        {
          MaterialAsset proto;
          std::memset(&proto.params, 0, sizeof(MaterialParams));
          proto.name = std::string(material->Name.empty() ? std::string() : std::string(material->Name.c_str()));
          // featureKey = full ShaderKey minus vertex attribs and pass.
          // it_subsetinfo->key was set in GatherInfo to matKey for this subset.
          proto.featureKey.bits = it_subsetinfo->key.bits & ~(ShaderKey::VERTEX_ATTRIB_MASK | ShaderKey::PASS_MASK);
          proto.textures[(int)MatTexSlot::BaseColor]          = it_subsetinfo->DiffuseTex;          proto.textureIds[(int)MatTexSlot::BaseColor]          = it_subsetinfo->DiffuseId;
          proto.textures[(int)MatTexSlot::Specular]           = it_subsetinfo->SpecularTex;         proto.textureIds[(int)MatTexSlot::Specular]           = it_subsetinfo->SpecularId;
          proto.textures[(int)MatTexSlot::Gloss]              = it_subsetinfo->GlossfTex;           proto.textureIds[(int)MatTexSlot::Gloss]              = it_subsetinfo->GlossfId;
          proto.textures[(int)MatTexSlot::Normal]             = it_subsetinfo->NormalTex;           proto.textureIds[(int)MatTexSlot::Normal]             = it_subsetinfo->NormalId;
          proto.textures[(int)MatTexSlot::Reflect]            = it_subsetinfo->ReflectTex;          proto.textureIds[(int)MatTexSlot::Reflect]            = it_subsetinfo->ReflectId;
          proto.textures[(int)MatTexSlot::Parallax]           = it_subsetinfo->ParalaxTex;          proto.textureIds[(int)MatTexSlot::Parallax]           = it_subsetinfo->ParalaxId;
          proto.textures[(int)MatTexSlot::Metallic]           = it_subsetinfo->MetallicTex;         proto.textureIds[(int)MatTexSlot::Metallic]           = it_subsetinfo->MetallicId;
          proto.textures[(int)MatTexSlot::Emissive]           = it_subsetinfo->EmissiveTex;         proto.textureIds[(int)MatTexSlot::Emissive]           = it_subsetinfo->EmissiveId;
          proto.textures[(int)MatTexSlot::SheenColor]         = it_subsetinfo->SheenColorTex;       proto.textureIds[(int)MatTexSlot::SheenColor]         = it_subsetinfo->SheenColorId;
          proto.textures[(int)MatTexSlot::SheenRoughness]     = it_subsetinfo->SheenRoughnessTex;   proto.textureIds[(int)MatTexSlot::SheenRoughness]     = it_subsetinfo->SheenRoughnessId;
          proto.textures[(int)MatTexSlot::Clearcoat]          = it_subsetinfo->ClearcoatTex;        proto.textureIds[(int)MatTexSlot::Clearcoat]          = it_subsetinfo->ClearcoatId;
          proto.textures[(int)MatTexSlot::ClearcoatRoughness] = it_subsetinfo->ClearcoatRoughnessTex;proto.textureIds[(int)MatTexSlot::ClearcoatRoughness] = it_subsetinfo->ClearcoatRoughnessId;
          proto.textures[(int)MatTexSlot::Occlusion]          = it_subsetinfo->OcclusionTex;        proto.textureIds[(int)MatTexSlot::Occlusion]          = it_subsetinfo->OcclusionId;
          proto.textures[(int)MatTexSlot::SpecularFactor]     = it_subsetinfo->SpecularFactorTex;   proto.textureIds[(int)MatTexSlot::SpecularFactor]     = it_subsetinfo->SpecularFactorId;
          proto.textures[(int)MatTexSlot::SpecularColor]      = it_subsetinfo->SpecularColorTex;    proto.textureIds[(int)MatTexSlot::SpecularColor]      = it_subsetinfo->SpecularColorId;
          proto.textures[(int)MatTexSlot::Transmission]       = it_subsetinfo->TransmissionTex;     proto.textureIds[(int)MatTexSlot::Transmission]       = it_subsetinfo->TransmissionId;
          auto copy4 = [](float dst[4], const XVECTOR3& src) { dst[0]=src.x; dst[1]=src.y; dst[2]=src.z; dst[3]=src.w; };
          MaterialParams& p = proto.params;
          copy4(p.ambientColor,   it_subsetinfo->AmbientColor);
          copy4(p.diffuseColor,   it_subsetinfo->DiffuseColor);
          copy4(p.specularColor,  it_subsetinfo->SpecularColor);
          copy4(p.pbrParams,      it_subsetinfo->PBRParams);
          copy4(p.intensities,    it_subsetinfo->Intensities);
          copy4(p.emissiveColor,  it_subsetinfo->EmissiveColor);
          copy4(p.sheenColor,     it_subsetinfo->SheenColor);
          p.sheenRoughness     = it_subsetinfo->SheenRoughness;
          p.clearcoatFactor    = it_subsetinfo->ClearcoatFactor;
          p.clearcoatRoughness = it_subsetinfo->ClearcoatRoughness;
          p.transmissionFactor = it_subsetinfo->TransmissionFactor;
          p.ior                = it_subsetinfo->IOR;
          p.occlusionStrength  = it_subsetinfo->OcclusionStrength;
          p.normalScale        = it_subsetinfo->NormalScale;
          p.alphaCutoff        = it_subsetinfo->AlphaCutoff;
          copy4(p.baseColorUV0,      it_subsetinfo->BaseColorUVTransform0);     copy4(p.baseColorUV1,      it_subsetinfo->BaseColorUVTransform1);
          copy4(p.normalUV0,         it_subsetinfo->NormalUVTransform0);        copy4(p.normalUV1,         it_subsetinfo->NormalUVTransform1);
          copy4(p.metallicUV0,       it_subsetinfo->MetallicUVTransform0);      copy4(p.metallicUV1,       it_subsetinfo->MetallicUVTransform1);
          copy4(p.emissiveUV0,       it_subsetinfo->EmissiveUVTransform0);      copy4(p.emissiveUV1,       it_subsetinfo->EmissiveUVTransform1);
          copy4(p.sheenColorUV0,     it_subsetinfo->SheenColorUVTransform0);    copy4(p.sheenColorUV1,     it_subsetinfo->SheenColorUVTransform1);
          copy4(p.sheenRoughUV0,     it_subsetinfo->SheenRoughnessUVTransform0);copy4(p.sheenRoughUV1,     it_subsetinfo->SheenRoughnessUVTransform1);
          copy4(p.clearcoatUV0,      it_subsetinfo->ClearcoatUVTransform0);     copy4(p.clearcoatUV1,      it_subsetinfo->ClearcoatUVTransform1);
          copy4(p.clearcoatRoughUV0, it_subsetinfo->ClearcoatRoughnessUVTransform0); copy4(p.clearcoatRoughUV1, it_subsetinfo->ClearcoatRoughnessUVTransform1);
          copy4(p.occlusionUV0,      it_subsetinfo->OcclusionUVTransform0);     copy4(p.occlusionUV1,      it_subsetinfo->OcclusionUVTransform1);
          copy4(p.specFactorUV0,     it_subsetinfo->SpecularFactorUVTransform0);copy4(p.specFactorUV1,     it_subsetinfo->SpecularFactorUVTransform1);
          copy4(p.specColorUV0,      it_subsetinfo->SpecularColorUVTransform0); copy4(p.specColorUV1,      it_subsetinfo->SpecularColorUVTransform1);
          copy4(p.transmissionUV0,   it_subsetinfo->TransmissionUVTransform0);  copy4(p.transmissionUV1,   it_subsetinfo->TransmissionUVTransform1);
          p.diffuseTexCoord    = (uint8_t)it_subsetinfo->DiffuseTexCoord;
          p.normalTexCoord     = (uint8_t)it_subsetinfo->NormalTexCoord;
          p.metallicTexCoord   = (uint8_t)it_subsetinfo->MetallicTexCoord;
          p.emissiveTexCoord   = (uint8_t)it_subsetinfo->EmissiveTexCoord;
          p.sheenColorTexCoord = (uint8_t)it_subsetinfo->SheenColorTexCoord;
          p.sheenRoughTexCoord = (uint8_t)it_subsetinfo->SheenRoughnessTexCoord;
          p.clearcoatTexCoord  = (uint8_t)it_subsetinfo->ClearcoatTexCoord;
          p.clearcoatRoughTexCoord = (uint8_t)it_subsetinfo->ClearcoatRoughnessTexCoord;
          p.occlusionTexCoord  = (uint8_t)it_subsetinfo->OcclusionTexCoord;
          p.specFactorTexCoord = (uint8_t)it_subsetinfo->SpecularFactorTexCoord;
          p.specColorTexCoord  = (uint8_t)it_subsetinfo->SpecularColorTexCoord;
          p.transmissionTexCoord = (uint8_t)it_subsetinfo->TransmissionTexCoord;
          p.alphaMode          = (uint8_t)it_subsetinfo->AlphaMode;
          p.doubleSided        = it_subsetinfo->DoubleSided ? 1u : 0u;
          p.unlit              = it_subsetinfo->Unlit ? 1u : 0u;
          p.bUseFresnel        = it_subsetinfo->bUseFresnel ? 1u : 0u;

          // Release any prior asset (in case Create is called twice).
          if (it_subsetinfo->matAsset) MaterialAssetCache::Get().Release(it_subsetinfo->matAsset);
          it_subsetinfo->matAsset = MaterialAssetCache::Get().Acquire(proto);
        }

        it_subsetinfo->NumTris = subinfo->NumTris;
        it_subsetinfo->NumVertex = subinfo->NumVertex;
        it_subsetinfo->IB32Bit = kUse32;
        // Allocate temp index storage matching the source width. Both
        // branches build the same {first vertex of each face} order,
        // mirroring the legacy 16-bit path. For glTF >65 535-vertex
        // primitives the loader sets `Indices32Bit` and populates
        // `Triangles32`; the legacy `.x` loader keeps the 16-bit path.
        if (!kUse32) {
          unsigned short *tmpIndexex = new unsigned short[it_subsetinfo->NumVertex];
          int counter = 0;
          bool first = false;
          for (int k = 0; k < NumFaceIndices; k++) {
            if (pActual->MaterialList.FaceIndices[k] == j) {
              unsigned int index = k * 3;
              if (!first) {
                it_subsetinfo->TriStart = k;
                it_subsetinfo->VertexStart = index;
                first = true;
              }

#if CHANGE_TO_RH
              tmpIndexex[counter++] = pActual->Triangles[index + 2];
              tmpIndexex[counter++] = pActual->Triangles[index + 1];
              tmpIndexex[counter++] = pActual->Triangles[index];
#else
              tmpIndexex[counter++] = pActual->Triangles[index];
              tmpIndexex[counter++] = pActual->Triangles[index + 1];
              tmpIndexex[counter++] = pActual->Triangles[index + 2];
#endif
            }
          }

          // Phase A.5 step 3: only suballocate into pool. Legacy
          // per-subset IB is no longer created.
          it_subsetinfo->IB = nullptr;
          if (populatePools) {
            uint32_t poolId = UINT32_MAX;
            IndexPool* ipool = MeshAssetCache::Get().GetOrCreateIndexPool(/*ib32Bit=*/false, &poolId);
            uint32_t off = ipool->Suballocate(tmpIndexex, it_subsetinfo->NumTris * 3u);
            poolIBAllocs[i][j] = { poolId, off, it_subsetinfo->NumTris * 3u };
          }

          // Compute per-subset AABB from referenced vertices
          it_subsetinfo->bounds.Reset();
          unsigned int stride16 = it->VertexSize / sizeof(float);
          for (int vi = 0; vi < counter; vi++) {
            unsigned int idx = tmpIndexex[vi];
            it_subsetinfo->bounds.Expand(it->pData[idx*stride16], it->pData[idx*stride16+1], it->pData[idx*stride16+2]);
          }

          delete[] tmpIndexex;
        } else {
          unsigned int *tmpIndexex = new unsigned int[it_subsetinfo->NumVertex];
          int counter = 0;
          bool first = false;
          for (int k = 0; k < NumFaceIndices; k++) {
            if (pActual->MaterialList.FaceIndices[k] == j) {
              unsigned int index = k * 3;
              if (!first) {
                it_subsetinfo->TriStart = k;
                it_subsetinfo->VertexStart = index;
                first = true;
              }

#if CHANGE_TO_RH
              tmpIndexex[counter++] = pActual->Triangles32[index + 2];
              tmpIndexex[counter++] = pActual->Triangles32[index + 1];
              tmpIndexex[counter++] = pActual->Triangles32[index];
#else
              tmpIndexex[counter++] = pActual->Triangles32[index];
              tmpIndexex[counter++] = pActual->Triangles32[index + 1];
              tmpIndexex[counter++] = pActual->Triangles32[index + 2];
#endif
            }
          }

          // Phase A.5 step 3: only suballocate into pool.
          it_subsetinfo->IB = nullptr;
          if (populatePools) {
            uint32_t poolId = UINT32_MAX;
            IndexPool* ipool = MeshAssetCache::Get().GetOrCreateIndexPool(/*ib32Bit=*/true, &poolId);
            uint32_t off = ipool->Suballocate(tmpIndexex, it_subsetinfo->NumTris * 3u);
            poolIBAllocs[i][j] = { poolId, off, it_subsetinfo->NumTris * 3u };
          }

          // Compute per-subset AABB from referenced vertices
          it_subsetinfo->bounds.Reset();
          unsigned int stride32 = it->VertexSize / sizeof(float);
          for (int vi = 0; vi < counter; vi++) {
            unsigned int idx = tmpIndexex[vi];
            it_subsetinfo->bounds.Expand(it->pData[idx*stride32], it->pData[idx*stride32+1], it->pData[idx*stride32+2]);
          }

          delete[] tmpIndexex;
        }
      }

      it_MeshInfo->VertexSize = it->VertexSize;

      // Phase A.5 step 3: VB lives in the shared pool only.
      it_MeshInfo->VB = nullptr;
      if (populatePools) {
        uint64_t formatHash = 0;
        if (!it_MeshInfo->SubSets.empty()) {
          formatHash = it_MeshInfo->SubSets[0].key.bits & ShaderKey::VERTEX_ATTRIB_MASK;
        }
        uint32_t poolId = UINT32_MAX;
        VertexPool* vpool = MeshAssetCache::Get().GetOrCreateVertexPool(formatHash, it->VertexSize, &poolId);
        uint32_t off = vpool->Suballocate(&it->pData[0], pActual->NumVertices * it->VertexSize);
        poolVBAllocs[i] = { poolId, off, pActual->NumVertices };
      }

      // Compute AABB from vertex positions (first 3 floats of each vertex)
      it_MeshInfo->bounds.Reset();
      unsigned int stride = it->VertexSize / sizeof(float);
      for (unsigned int v = 0; v < pActual->NumVertices; v++) {
        float px = it->pData[v * stride + 0];
        float py = it->pData[v * stride + 1];
        float pz = it->pData[v * stride + 2];
        it_MeshInfo->bounds.Expand(px, py, pz);
      }

      T8_LOG_DEBUG("  Geometry %zu: VB=%u bytes (stride=%u, %d verts), IB=%zu tris%s",
                   i, pActual->NumVertices * it->VertexSize, it->VertexSize, pActual->NumVertices,
                   (kUse32 ? pActual->Triangles32.size() : pActual->Triangles.size())/3,
                   kUse32 ? " [32-bit]" : "");

#if CHANGE_TO_RH
      if (!kUse32) {
        for (std::size_t a = 0; a < pActual->Triangles.size(); a += 3) {
          unsigned short i0 = pActual->Triangles[a + 0];
          unsigned short i2 = pActual->Triangles[a + 2];
          pActual->Triangles[a + 0] = i2;
          pActual->Triangles[a + 2] = i0;
        }
      } else {
        for (std::size_t a = 0; a < pActual->Triangles32.size(); a += 3) {
          unsigned int i0 = pActual->Triangles32[a + 0];
          unsigned int i2 = pActual->Triangles32[a + 2];
          pActual->Triangles32[a + 0] = i2;
          pActual->Triangles32[a + 2] = i0;
        }
      }
#endif

      // Phase A.5 step 3: per-mesh IB (legacy MeshInfo::IB) was used
      // by an older draw path that bound a whole-geometry IB; the
      // current pool path uses per-subset IB allocations exclusively.
      it_MeshInfo->IB = nullptr;
    }

    LogLoadedMeshDetails(xFile, Info);
    XMatIdentity(transform);

    // Phase A: populate the shared MeshAsset on first acquisition.
    // VB/IB are still owned by RenderMesh in this phase; only the
    // CPU-side description (submesh ranges, AABBs, vertex layout) is
    // mirrored into the asset so that subsequent acquisitions of the
    // same source path observe a fully-formed entry.
    if (m_asset && m_asset->submeshes.empty()) {
      m_asset->vertexAttribMask = 0;
      m_asset->vertexStride     = 0;
      m_asset->vertexCount      = 0;
      m_asset->indexCount       = 0;
      m_asset->rootAABB         = t850::AABB{};

      std::size_t totalVerts = 0;
      std::size_t totalIdx   = 0;
      for (std::size_t i = 0; i < Info.size(); ++i) {
        const MeshInfo& mi = Info[i];
        if (mi.VertexSize > m_asset->vertexStride) m_asset->vertexStride = mi.VertexSize;
        totalVerts += mi.NumVertex;
        for (std::size_t j = 0; j < mi.SubSets.size(); ++j) {
          const SubSetInfo& s = mi.SubSets[j];
          Submesh sub;
          sub.vertexStart   = s.VertexStart;
          sub.vertexCount   = s.NumVertex;
          sub.indexStart    = s.TriStart * 3u;
          sub.triangleCount = s.NumTris;
          sub.materialSlot  = static_cast<uint32_t>(m_asset->submeshes.size());
          sub.ib32Bit       = s.IB32Bit;
          sub.localAABB.vMin = XVECTOR3(s.bounds.min.x, s.bounds.min.y, s.bounds.min.z, 0.0f);
          sub.localAABB.vMax = XVECTOR3(s.bounds.max.x, s.bounds.max.y, s.bounds.max.z, 0.0f);
          sub.vertexAttribKey.bits = s.key.bits & ShaderKey::VERTEX_ATTRIB_MASK;
          m_asset->vertexAttribMask |= sub.vertexAttribKey.bits;
          totalIdx += static_cast<std::size_t>(s.NumTris) * 3u;
          // Phase A.5 pool allocations captured during the loop above.
          if (i < poolVBAllocs.size() && poolVBAllocs[i].poolId != UINT32_MAX) {
            sub.vbAlloc.poolId      = poolVBAllocs[i].poolId;
            sub.vbAlloc.offsetElems = poolVBAllocs[i].offsetVerts;
            sub.vbAlloc.count       = poolVBAllocs[i].count;
            sub.vbPoolId            = static_cast<uint16_t>(poolVBAllocs[i].poolId);
          }
          if (i < poolIBAllocs.size() && j < poolIBAllocs[i].size() && poolIBAllocs[i][j].poolId != UINT32_MAX) {
            sub.ibAlloc.poolId      = poolIBAllocs[i][j].poolId;
            sub.ibAlloc.offsetElems = poolIBAllocs[i][j].offsetIdx;
            sub.ibAlloc.count       = poolIBAllocs[i][j].count;
            sub.ibPoolId            = static_cast<uint16_t>(poolIBAllocs[i][j].poolId);
          }
          m_asset->submeshes.push_back(sub);
        }
        m_asset->rootAABB.ExpandToInclude(mi.bounds.min.x, mi.bounds.min.y, mi.bounds.min.z);
        m_asset->rootAABB.ExpandToInclude(mi.bounds.max.x, mi.bounds.max.y, mi.bounds.max.z);
      }
      m_asset->vertexCount = static_cast<uint32_t>(totalVerts);
      m_asset->indexCount  = static_cast<uint32_t>(totalIdx);

      T8_LOG_INFO("[MeshAssetCache] Populated '%s': %zu submesh(es), %u verts, %u indices, stride=%u, attribMask=0x%016llX",
                  m_asset->sourcePath.c_str(),
                  m_asset->submeshes.size(),
                  m_asset->vertexCount, m_asset->indexCount, m_asset->vertexStride,
                  static_cast<unsigned long long>(m_asset->vertexAttribMask));
      MeshAssetCache::Get().DumpToLog();
      MaterialAssetCache::Get().DumpToLog();
    } else if (m_asset) {
      T8_LOG_INFO("[MeshAssetCache] Reusing populated '%s' (%zu submesh(es), refs=%u)",
                  m_asset->sourcePath.c_str(), m_asset->submeshes.size(), m_asset->refCount);
    }

    // Phase A.5 step 2: copy pool offsets from the (now populated)
    // MeshAsset into per-instance MeshInfo / SubSetInfo so the draw
    // path can reach them in O(1). This runs for EVERY instance,
    // including reuseGPU acquisitions (which never entered the
    // populate-pools branch above).
    if (m_asset && !m_asset->submeshes.empty()) {
      std::size_t flatIdx = 0;
      for (std::size_t i = 0; i < Info.size(); ++i) {
        MeshInfo& mi = Info[i];
        // VB alloc is the same across all subsets of one geometry
        // (subsets share their parent geometry's VB).
        if (flatIdx < m_asset->submeshes.size()) {
          mi.vbPoolAlloc = m_asset->submeshes[flatIdx].vbAlloc;
        }
        for (std::size_t j = 0; j < mi.SubSets.size() && flatIdx < m_asset->submeshes.size(); ++j, ++flatIdx) {
          mi.SubSets[j].ibPoolAlloc = m_asset->submeshes[flatIdx].ibAlloc;
        }
      }
    }
  }

  void RenderMesh::GatherInfo() {

    char *vsSourceP;
    char *fsSourceP;
    std::string vsName, fsName;
    if (g_pBaseDriver->UsesGLSL()) {
      vsSourceP = file2string("Shaders/VS_Mesh.glsl");
      fsSourceP = file2string("Shaders/FS_Mesh.glsl");
      vsName = "VS_Mesh.glsl";
      fsName = "FS_Mesh.glsl";
    }
    else {
      vsSourceP = file2string("Shaders/VS_Mesh.hlsl");
      fsSourceP = file2string("Shaders/FS_Mesh.hlsl");
      vsName = "VS_Mesh.hlsl";
      fsName = "FS_Mesh.hlsl";
    }

    std::string vstr = std::string(vsSourceP);
    std::string fstr = std::string(fsSourceP);

    free(vsSourceP);
    free(fsSourceP);

    for (std::size_t i = 0; i < xFile->MeshInfo.size(); i++) {
      xFinalGeometry *it = &xFile->MeshInfo[i];
      xMeshGeometry *pActual = &xFile->XMeshDataBase[0]->Geometry[i];
      ShaderKey baseKey(0);

      T8_LOG_VERBOSE("Mesh geometry %zu: attrs=0x%X, %d materials",
                     i, pActual->VertexAttributes, (int)pActual->MaterialList.Materials.size());

      if (pActual->VertexAttributes&xMeshGeometry::HAS_NORMAL)
        baseKey.bits |= ShaderKey::HAS_NORMALS;
      if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD0)
        baseKey.bits |= ShaderKey::HAS_TEXCOORD0;
      if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD1)
        baseKey.bits |= ShaderKey::HAS_TEXCOORD1;
      if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD2)
        baseKey.bits |= ShaderKey::HAS_TEXCOORD2;
      if (pActual->VertexAttributes&xMeshGeometry::HAS_TEXCOORD3)
        baseKey.bits |= ShaderKey::HAS_TEXCOORD3;
      if (pActual->VertexAttributes&xMeshGeometry::HAS_TANGENT)
        baseKey.bits |= ShaderKey::HAS_TANGENTS;
      if (pActual->VertexAttributes&xMeshGeometry::HAS_BINORMAL)
        baseKey.bits |= ShaderKey::HAS_BINORMALS;

      MeshInfo tmp;
      int NumMaterials = static_cast<int>(pActual->MaterialList.Materials.size());
      for (int j = 0; j < NumMaterials; j++) {
        ShaderKey matKey(baseKey.bits);
        xSubsetInfo *subinfo = &it->Subsets[j];
        xMaterial *material = &pActual->MaterialList.Materials[j];
        SubSetInfo stmp;

        bool bNoLight = false;
        bool bUseFresnel = false;

        for (unsigned int k = 0; k < material->EffectInstance.pDefaults.size(); k++) {
          xEffectDefault *mDef = &material->EffectInstance.pDefaults[k];

          if (mDef->Type == xF::xEFFECTENUM::STDX_STRINGS) {
            if (mDef->NameParam == "diffuseMap")
              matKey.bits |= ShaderKey::DIFFUSE_MAP;
            if (mDef->NameParam == "specularMap")
              matKey.bits |= ShaderKey::SPECULAR_MAP;
            if (mDef->NameParam == "glossMap")
              matKey.bits |= ShaderKey::GLOSS_MAP;
            if (mDef->NameParam == "normalMap")
              matKey.bits |= ShaderKey::NORMAL_MAP;
            if (mDef->NameParam == "heightMap")
              matKey.bits |= ShaderKey::HEIGHT_MAP;
            if (mDef->NameParam == "metallicMap")
              matKey.bits |= ShaderKey::METALLIC_MAP;
            if (mDef->NameParam == "emissiveMap")
              matKey.bits |= ShaderKey::EMISSIVE_MAP;
            if (mDef->NameParam == "sheenColorMap")
              matKey.bits |= ShaderKey::SHEEN_COLOR_MAP;
            if (mDef->NameParam == "sheenRoughnessMap")
              matKey.bits |= ShaderKey::SHEEN_ROUGHNESS_MAP;
            if (mDef->NameParam == "clearcoatMap")
              matKey.bits |= ShaderKey::CLEARCOAT_MAP;
            if (mDef->NameParam == "clearcoatRoughnessMap")
              matKey.bits |= ShaderKey::CLEARCOAT_ROUGHNESS_MAP;
            if (mDef->NameParam == "occlusionMap")
              matKey.bits |= ShaderKey::OCCLUSION_MAP;
            if (mDef->NameParam == "specularFactorMap")
              matKey.bits |= ShaderKey::SPECULAR_FACTOR_MAP;
            if (mDef->NameParam == "specularColorMap")
              matKey.bits |= ShaderKey::SPECULAR_COLOR_MAP;
            if (mDef->NameParam == "transmissionMap")
              matKey.bits |= ShaderKey::TRANSMISSION_MAP;
          }

          if (mDef->Type == xF::xEFFECTENUM::STDX_DWORDS) {
            if (mDef->NameParam == "NoLighting") {
              if (mDef->CaseDWORD == 1) {
                bNoLight = true;
              }
            }
			if (mDef->NameParam == "bUseFresnel") {
				if (mDef->CaseDWORD == 1) {
					bUseFresnel = true;
				}
			}
			if (mDef->NameParam == "gltfTangentSpace") {
				if (mDef->CaseDWORD == 1) {
					matKey.bits |= ShaderKey::GLTF_TANGENT_SPACE;
				}
			}
          }
        }

		if (bNoLight) {
			stmp.MatID = 0;
		}
		else if (!matKey.has(ShaderKey::NORMAL_MAP)) {
			stmp.MatID = 1;
		}
		else {
			stmp.MatID = 2;
		}

		stmp.bUseFresnel = bUseFresnel;
        stmp.key = matKey;

        // Phase B step 1: MaterialAsset prototype is built later in
        // RenderMesh::Create (after pDefaults populates it_subsetinfo
        // with real material data). Here `stmp` is mostly defaults so
        // it would dedup down to a handful of buckets.

        T8_LOG_VERBOSE("  Material %d: key=0x%016llX noLight=%d fresnel=%d matID=%d",
                 j, static_cast<unsigned long long>(matKey.bits), (int)bNoLight, (int)bUseFresnel, stmp.MatID);

        // Pre-compile pass variants
        bool hasHeight = matKey.has(ShaderKey::HEIGHT_MAP);
        g_pBaseDriver->CreateShader(vstr, fstr, matKey, vsName, fsName);

        static const uint8_t passes[] = {
          PassType::FORWARD, PassType::GBUFFER,
          PassType::SHADOW_MAP, PassType::RADIAL_DEPTH
        };
        for (uint8_t pass : passes) {
          ShaderKey k(matKey.bits);
          k.setPass(pass);
          g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
          if (hasHeight && (pass == PassType::GBUFFER || pass == PassType::FORWARD)) {
            ShaderKey kp(k.bits);
            kp.bits |= ShaderKey::PARALLAX;
            g_pBaseDriver->CreateShader(vstr, fstr, kp, vsName, fsName);
          }
        }

        tmp.SubSets.push_back(stmp);
      }

      Info.push_back(tmp);
    }
  }

  int	 RenderMesh::LoadTex(std::string p, xF::xMaterial *mat, Texture** tex) {
    int id = g_pBaseDriver->CreateTexture(p);
    *tex = g_pBaseDriver->GetTexture(id);
    bool tiled = false;
    for (unsigned int m = 0; m < mat->EffectInstance.pDefaults.size(); m++) {
      xEffectDefault *mDef_2 = &mat->EffectInstance.pDefaults[m];
      if (mDef_2->Type == xF::xEFFECTENUM::STDX_DWORDS) {
        if (mDef_2->NameParam == "Tiled") {
          if (mDef_2->CaseDWORD == 1) {
            tiled = true;
          }
          break;
        }
      }
    }

    unsigned int params = TextBasicParams::MIPMAPS;

    if (tiled)
      params |= TextBasicParams::TILED;
    else
      params |= TextBasicParams::CLAMP_TO_EDGE;

    (*tex)->params = params;
    (*tex)->SetTextureParams();

    if (id != -1) {
#if DEBUG_MODEL
      std::cout << "Texture Loaded index " << id << std::endl;
#endif
    }
    else {
      std::cout << "Texture [" << p << "] not Found" << std::endl;
    }

    return id;
  }

  void RenderMesh::Transform(float *t) {
    transform = t;
  }

  // Extract 6 frustum planes from a row-vector VP matrix.
  // Each plane is stored as (nx, ny, nz, d) in XVECTOR3 (using .x,.y,.z,.w).
  // Plane equation: nx*x + ny*y + nz*z + d >= 0 means inside.
  void RenderMesh::ExtractFrustumPlanes(const XMATRIX44& vp, XVECTOR3 planes[6]) {
    // Row-vector convention: point * M. Planes from columns+rows of the VP matrix.
    // Left
    planes[0] = XVECTOR3(vp.m14 + vp.m11, vp.m24 + vp.m21, vp.m34 + vp.m31, vp.m44 + vp.m41);
    // Right
    planes[1] = XVECTOR3(vp.m14 - vp.m11, vp.m24 - vp.m21, vp.m34 - vp.m31, vp.m44 - vp.m41);
    // Bottom
    planes[2] = XVECTOR3(vp.m14 + vp.m12, vp.m24 + vp.m22, vp.m34 + vp.m32, vp.m44 + vp.m42);
    // Top
    planes[3] = XVECTOR3(vp.m14 - vp.m12, vp.m24 - vp.m22, vp.m34 - vp.m32, vp.m44 - vp.m42);
    // Near
    planes[4] = XVECTOR3(vp.m13, vp.m23, vp.m33, vp.m43);
    // Far
    planes[5] = XVECTOR3(vp.m14 - vp.m13, vp.m24 - vp.m23, vp.m34 - vp.m33, vp.m44 - vp.m43);
    // Normalize each plane
    for (int i = 0; i < 6; i++) {
      float len = std::sqrt(planes[i].x*planes[i].x + planes[i].y*planes[i].y + planes[i].z*planes[i].z);
      if (len > 1e-8f) {
        planes[i].x /= len; planes[i].y /= len; planes[i].z /= len; planes[i].w /= len;
      }
    }
  }

  // Test AABB (in local space) transformed by world matrix against frustum planes.
  // Returns true if AABB is at least partially inside the frustum.
  bool RenderMesh::AABBInsideFrustum(const AABB& box, const XMATRIX44& world, const XVECTOR3 planes[6]) {
    // Transform the 8 AABB corners to world space
    float corners[8][3];
    float bmin[3] = { box.min.x, box.min.y, box.min.z };
    float bmax[3] = { box.max.x, box.max.y, box.max.z };
    for (int c = 0; c < 8; c++) {
      float lx = (c & 1) ? bmax[0] : bmin[0];
      float ly = (c & 2) ? bmax[1] : bmin[1];
      float lz = (c & 4) ? bmax[2] : bmin[2];
      // Row-vector: [lx,ly,lz,1] * world
      corners[c][0] = lx*world.m11 + ly*world.m21 + lz*world.m31 + world.m41;
      corners[c][1] = lx*world.m12 + ly*world.m22 + lz*world.m32 + world.m42;
      corners[c][2] = lx*world.m13 + ly*world.m23 + lz*world.m33 + world.m43;
    }
    // For each plane, check if all 8 corners are outside
    for (int p = 0; p < 6; p++) {
      int outside = 0;
      for (int c = 0; c < 8; c++) {
        float dist = planes[p].x*corners[c][0] + planes[p].y*corners[c][1] + planes[p].z*corners[c][2] + planes[p].w;
        if (dist < 0.0f) outside++;
      }
      if (outside == 8) return false; // all corners outside this plane
    }
    return true;
  }

  static bool IsForwardOnlySubset(const RenderMesh::SubSetInfo& subInfo) {
    // Phase B: read material classification via the shared
    // MaterialAsset; SubSetInfo.AlphaMode/TransmissionFactor are
    // legacy duplicates kept around for ABI stability.
    if (const MaterialAsset* mat = subInfo.matAsset) {
      const MaterialParams& mp = mat->params;
      return mp.alphaMode == 2 || mp.transmissionFactor > 0.0f;
    }
    return subInfo.AlphaMode == 2 || subInfo.TransmissionFactor > 0.0f;
  }

  static bool ShouldDrawSubsetInPass(const RenderMesh::SubSetInfo& subInfo, uint8_t pass) {
    const bool forwardOnly = IsForwardOnlySubset(subInfo);
    if (pass == PassType::GBUFFER || pass == PassType::SHADOW_MAP || pass == PassType::RADIAL_DEPTH) {
      return !forwardOnly;
    }
    if (pass == PassType::FORWARD) {
      return forwardOnly;
    }
    return true;
  }

  static float SubsetDistanceSqToCamera(const RenderMesh::SubSetInfo& subInfo, const XMATRIX44& world, const XVECTOR3& eye) {
    float lx = (subInfo.bounds.min.x + subInfo.bounds.max.x) * 0.5f;
    float ly = (subInfo.bounds.min.y + subInfo.bounds.max.y) * 0.5f;
    float lz = (subInfo.bounds.min.z + subInfo.bounds.max.z) * 0.5f;
    float wx = lx*world.m11 + ly*world.m21 + lz*world.m31 + world.m41;
    float wy = lx*world.m12 + ly*world.m22 + lz*world.m32 + world.m42;
    float wz = lx*world.m13 + ly*world.m23 + lz*world.m33 + world.m43;
    float dx = wx - eye.x;
    float dy = wy - eye.y;
    float dz = wz - eye.z;
    return dx*dx + dy*dy + dz*dz;
  }

  static int ForwardSubsetGroup(const RenderMesh::SubSetInfo& subInfo) {
    if (const MaterialAsset* mat = subInfo.matAsset) {
      return mat->params.transmissionFactor > 0.0f ? 0 : 1;
    }
    return subInfo.TransmissionFactor > 0.0f ? 0 : 1;
  }

  static int NonForwardSubsetGroup(const RenderMesh::SubSetInfo& subInfo) {
    if (const MaterialAsset* mat = subInfo.matAsset) {
      return mat->params.alphaMode == 1 ? 1 : 0;
    }
    return subInfo.AlphaMode == 1 ? 1 : 0;
  }

  static int GeometryNonForwardGroup(const RenderMesh::MeshInfo& meshInfo, uint8_t pass) {
    bool hasDrawableSubset = false;
    bool hasMaskedSubset = false;
    for (const auto& subInfo : meshInfo.SubSets) {
      if (!ShouldDrawSubsetInPass(subInfo, pass))
        continue;
      hasDrawableSubset = true;
      if (NonForwardSubsetGroup(subInfo) == 1)
        hasMaskedSubset = true;
    }
    if (!hasDrawableSubset)
      return 2;
    return hasMaskedSubset ? 1 : 0;
  }

  static int GeometryForwardGroup(const RenderMesh::MeshInfo& meshInfo) {
    int group = 2;
    for (const auto& subInfo : meshInfo.SubSets) {
      if (IsForwardOnlySubset(subInfo)) {
        int subsetGroup = ForwardSubsetGroup(subInfo);
        if (subsetGroup < group)
          group = subsetGroup;
      }
    }
    return group;
  }

  static float GeometryForwardDistanceSq(const RenderMesh::MeshInfo& meshInfo, const XMATRIX44& world, const XVECTOR3& eye) {
    float distanceSq = -1.0f;
    for (const auto& subInfo : meshInfo.SubSets) {
      if (IsForwardOnlySubset(subInfo)) {
        float subsetDistanceSq = SubsetDistanceSqToCamera(subInfo, world, eye);
        if (subsetDistanceSq > distanceSq)
          distanceSq = subsetDistanceSq;
      }
    }
    return distanceSq;
  }

  void RenderMesh::Draw(float *t, float *vp) {
    if (t)
      transform = t;

    Camera *pActualCamera = pScProp->pCameras[0];

    // Extract frustum planes once per draw call
    XVECTOR3 frustumPlanes[6];
    ExtractFrustumPlanes(pActualCamera->VP, frustumPlanes);

    std::size_t numGeometries = xFile->MeshInfo.size();
    m_totalSubsets = 0;
    m_drawnSubsets = 0;
    m_culledMeshes = 0;

    // Build visibility mask — parallel for large meshes, serial for small
    std::vector<uint8_t> visible(numGeometries, 0);
    static constexpr int kParallelCullThreshold = 256;

    if (static_cast<int>(numGeometries) >= kParallelCullThreshold && g_threadPool) {
      XMATRIX44 worldCopy = transform;
      g_threadPool->ParallelFor(0, static_cast<int>(numGeometries), [&](int i) {
        visible[i] = AABBInsideFrustum(Info[i].bounds, worldCopy, frustumPlanes) ? 1 : 0;
      });
    } else {
      for (std::size_t i = 0; i < numGeometries; i++) {
        visible[i] = AABBInsideFrustum(Info[i].bounds, transform, frustumPlanes) ? 1 : 0;
      }
    }

    uint8_t currentPass = gKey.getPass();

    // Phase C step 3: state tracker is process-wide (singleton) so
    // it can persist across multiple RenderMesh::Draw calls inside
    // a pass scope opened by RenderGraph (cross-entity dedup of IBL
    // textures, EnvMap, IB pool, etc.). Without an outer Begin(), we
    // open a private scope here so the tracker resets per-Draw and
    // behaves identically to step 2.
    MeshDrawStateTracker& tracker = MeshDrawStateTracker::Get();
    const bool ownsScope = !tracker.InScope();
    if (ownsScope) tracker.Begin();
    auto bindTextureOnce = [&](Texture* t, int slot, const char* name, int samplerSlot) {
      if (!t || slot < 0 || slot >= MeshDrawStateTracker::kMaxTrackedSlots) return;
      if (tracker.ShouldBindTexture(slot, t)) {
        t->Set(*T8DeviceContext, slot, name);
      }
      // Sampler set every time (cheap); the per-shader sampler slot
      // map is consulted inside SetSampler.
      t->SetSampler(*T8DeviceContext, samplerSlot);
    };

    std::vector<std::size_t> geometryOrder(numGeometries);
    for (std::size_t i = 0; i < numGeometries; i++) geometryOrder[i] = i;
    if (currentPass == PassType::FORWARD) {
      std::stable_sort(geometryOrder.begin(), geometryOrder.end(),
        [&](std::size_t a, std::size_t b) {
          int groupA = GeometryForwardGroup(Info[a]);
          int groupB = GeometryForwardGroup(Info[b]);
          if (groupA != groupB)
            return groupA < groupB;
          float da = GeometryForwardDistanceSq(Info[a], transform, pActualCamera->Eye);
          float db = GeometryForwardDistanceSq(Info[b], transform, pActualCamera->Eye);
          return da > db;
        });
      } else if (currentPass == PassType::GBUFFER || currentPass == PassType::SHADOW_MAP || currentPass == PassType::RADIAL_DEPTH) {
        std::stable_sort(geometryOrder.begin(), geometryOrder.end(),
          [&](std::size_t a, std::size_t b) {
            return GeometryNonForwardGroup(Info[a], currentPass) < GeometryNonForwardGroup(Info[b], currentPass);
          });
    }

    for (std::size_t oi = 0; oi < numGeometries; oi++) {
      std::size_t i = geometryOrder[oi];
      MeshInfo  *it_MeshInfo = &Info[i];
      xMeshGeometry *pActual = &xFile->XMeshDataBase[0]->Geometry[i];

      m_totalSubsets += static_cast<int>(it_MeshInfo->SubSets.size());

      if (!visible[i]) {
        m_culledMeshes++;
        continue;
      }

      XMATRIX44 VP = pActualCamera->VP;
      XMATRIX44 WVP = transform*VP;
      XMATRIX44 WorldView = transform*pActualCamera->View;
      XVECTOR3 infoCam = XVECTOR3(pActualCamera->NPlane, pActualCamera->FPlane, pActualCamera->Fov, 1.0f);

      it_MeshInfo->CnstBuffer.WVP = WVP;
      it_MeshInfo->CnstBuffer.World = transform;
      it_MeshInfo->CnstBuffer.WorldView = WorldView;
      it_MeshInfo->CnstBuffer.Light0Pos = pScProp->Lights[0].Position;
      it_MeshInfo->CnstBuffer.Light0Col = pScProp->Lights[0].Color;
      it_MeshInfo->CnstBuffer.CameraPos = pActualCamera->Eye;
      unsigned int numLights = pScProp ? static_cast<unsigned int>(pScProp->ActiveLights) : 1u;
      if (pScProp && numLights > pScProp->Lights.size())
        numLights = static_cast<unsigned int>(pScProp->Lights.size());
      if (numLights > 128u) numLights = 128u;
      infoCam.w = static_cast<float>(numLights);
      it_MeshInfo->CnstBuffer.CameraInfo = infoCam;

      for (int li = 0; li < 128; li++) {
        it_MeshInfo->CnstBuffer.LightPositions[li] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
        it_MeshInfo->CnstBuffer.LightColors[li] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      }
      for (int ri = 0; ri < 32; ri++) {
        it_MeshInfo->CnstBuffer.LightRadius[ri] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      }
      for (unsigned int li = 0; li < numLights; li++) {
        Light& light = pScProp->Lights[li];
        if (light.Type == LIGHT_DIRECTIONAL) {
          it_MeshInfo->CnstBuffer.LightPositions[li] = XVECTOR3(light.Direction.x, light.Direction.y, light.Direction.z, 0.0f);
        } else {
          it_MeshInfo->CnstBuffer.LightPositions[li] = XVECTOR3(light.Position.x, light.Position.y, light.Position.z, 1.0f);
        }
        it_MeshInfo->CnstBuffer.LightColors[li] = XVECTOR3(light.Color.x, light.Color.y, light.Color.z, light.Intensity);
        XVECTOR3& radiusPack = it_MeshInfo->CnstBuffer.LightRadius[li >> 2];
        if ((li & 3u) == 0u) radiusPack.x = light.radius;
        else if ((li & 3u) == 1u) radiusPack.y = light.radius;
        else if ((li & 3u) == 2u) radiusPack.z = light.radius;
        else radiusPack.w = light.radius;
      }
	  it_MeshInfo->CnstBuffer.ParallaxSettings = XVECTOR3(m_fParallaxLowSamples, m_fParallaxHighSamples, m_fParallaxHeight);
	  it_MeshInfo->CnstBuffer.ParallaxSettings.w = m_fParallaxEnabled;
	  it_MeshInfo->CnstBuffer.ParallaxShadowSettings = XVECTOR3(m_fParallaxShadowMinLayers, m_fParallaxShadowMaxLayers, m_fParallaxShadowSoftness);
	  it_MeshInfo->CnstBuffer.ParallaxShadowSettings.w = m_fParallaxShadowStrength;
	  it_MeshInfo->CnstBuffer.Light0Dir = pScProp->Lights[0].Direction;

      unsigned int stride = it_MeshInfo->VertexSize;
      unsigned int offset = 0;

      ShaderBase *s = 0;
      ShaderBase *last = (ShaderBase*)32;

      // Phase A.5 step 2: bind shared VB pool if available, otherwise
      // fall back to the per-asset VB. The pool's GPU buffer is built
      // lazily on first GetGPUBuffer() call.
      VertexBuffer* vbToBind = it_MeshInfo->VB;
      if (it_MeshInfo->vbPoolAlloc.IsValid()) {
        if (VertexPool* vpool = MeshAssetCache::Get().GetVertexPool(it_MeshInfo->vbPoolAlloc.poolId)) {
          if (VertexBuffer* gpu = vpool->GetGPUBuffer()) {
            vbToBind = gpu;
          }
        }
      }
      vbToBind->Set(*T8DeviceContext, stride, offset);

      // Build sorted draw order by shader key to minimize PSO switches
      std::size_t numSubsets = it_MeshInfo->SubSets.size();
      std::vector<std::size_t> drawOrder(numSubsets);
      for (std::size_t k = 0; k < numSubsets; k++) drawOrder[k] = k;
      std::stable_sort(drawOrder.begin(), drawOrder.end(),
        [&](std::size_t a, std::size_t b) {
          if (currentPass == PassType::FORWARD) {
            int groupA = ForwardSubsetGroup(it_MeshInfo->SubSets[a]);
            int groupB = ForwardSubsetGroup(it_MeshInfo->SubSets[b]);
            if (groupA != groupB)
              return groupA < groupB;
            float da = SubsetDistanceSqToCamera(it_MeshInfo->SubSets[a], transform, pActualCamera->Eye);
            float db = SubsetDistanceSqToCamera(it_MeshInfo->SubSets[b], transform, pActualCamera->Eye);
            return da > db;
          }
          if (currentPass == PassType::GBUFFER || currentPass == PassType::SHADOW_MAP || currentPass == PassType::RADIAL_DEPTH) {
            int groupA = NonForwardSubsetGroup(it_MeshInfo->SubSets[a]);
            int groupB = NonForwardSubsetGroup(it_MeshInfo->SubSets[b]);
            if (groupA != groupB)
              return groupA < groupB;
          }
          ShaderKey ka(it_MeshInfo->SubSets[a].key.bits); ka.setPass(currentPass);
          ShaderKey kb(it_MeshInfo->SubSets[b].key.bits); kb.setPass(currentPass);
          return ka.bits < kb.bits;
        });

      for (std::size_t ki = 0; ki < numSubsets; ki++) {
        std::size_t k = drawOrder[ki];
        bool update = false;
        SubSetInfo *sub_info = &it_MeshInfo->SubSets[k];

        if (!ShouldDrawSubsetInPass(*sub_info, currentPass))
          continue;

        // Per-subset frustum cull
        if (!AABBInsideFrustum(sub_info->bounds, transform, frustumPlanes))
          continue;

        // Phase B step 2: read material data via the deduplicated
        // MaterialAsset. SubSetInfo material fields are still
        // populated for now (step 3 retires them) but are no longer
        // the source of truth for rendering. Per-instance fields like
        // MatID stay on SubSetInfo.
        const MaterialAsset* mat = sub_info->matAsset;
        const MaterialParams* mp = mat ? &mat->params : nullptr;
        if (mp) {
          FillCBufferFromMaterial(it_MeshInfo->CnstBuffer, *mp);
          // Per-instance MatID overrides the alpha slot used by
          // FillCBufferFromMaterial (it filled .w with intensities[3]
          // but the engine reuses Intensities.w for MatID).
          it_MeshInfo->CnstBuffer.Intensities.w = (float)sub_info->MatID;
        } else {
          // Defensive fallback (shouldn't happen if Create() ran).
          it_MeshInfo->CnstBuffer.AmbientColor = sub_info->AmbientColor;
          it_MeshInfo->CnstBuffer.DiffuseColor = sub_info->DiffuseColor;
          it_MeshInfo->CnstBuffer.SpecularColor = sub_info->SpecularColor;
          it_MeshInfo->CnstBuffer.PBRParams = sub_info->PBRParams;
          it_MeshInfo->CnstBuffer.Intensities = sub_info->Intensities;
          it_MeshInfo->CnstBuffer.Intensities.w = (float)sub_info->MatID;
          it_MeshInfo->CnstBuffer.EmissiveColor = sub_info->EmissiveColor;
          it_MeshInfo->CnstBuffer.AlphaParams = XVECTOR3((float)sub_info->AlphaMode, sub_info->AlphaCutoff, sub_info->DoubleSided ? 1.0f : 0.0f, sub_info->TransmissionFactor);
          it_MeshInfo->CnstBuffer.TexCoordSets = XVECTOR3((float)sub_info->DiffuseTexCoord, (float)sub_info->NormalTexCoord, (float)sub_info->MetallicTexCoord, (float)sub_info->EmissiveTexCoord);
        }
        it_MeshInfo->CnstBuffer.ForwardParams = XVECTOR3((float)g_pBaseDriver->width, (float)g_pBaseDriver->height, Textures[7] ? 1.0f : 0.0f, mp ? mp->ior : sub_info->IOR);
        float emissiveMul = pScProp ? pScProp->MaterialEmissiveIntensity : 1.0f;
        float transmissionMul = pScProp ? pScProp->MaterialTransmissionMultiplier : 1.0f;
        float refractionStrength = pScProp ? pScProp->MaterialRefractionStrength : 0.03f;
        float iblFactor = pScProp ? pScProp->IBLFactor : 1.0f;
        float iblMipCount = pScProp ? pScProp->IBLMipCount : 4.0f;
        float iblDiffuseMipLevel = pScProp ? pScProp->IBLDiffuseMipLevel : 4.0f;
        float iblBrdfLutEnabled = pScProp ? pScProp->IBLBRDFLUTEnabled : 0.0f;
        if (mp) {
          // Material-driven slots: clearcoat factors + unlit flag (.x..z),
          // plus per-frame multipliers in the .w slots.
          it_MeshInfo->CnstBuffer.MaterialParams  = XVECTOR3(mp->clearcoatFactor, mp->clearcoatRoughness, mp->unlit ? 1.0f : 0.0f, emissiveMul);
          it_MeshInfo->CnstBuffer.MaterialParams5 = XVECTOR3(mat->textures[(int)MatTexSlot::SheenColor]     ? 1.0f : 0.0f,
                                                              mat->textures[(int)MatTexSlot::SheenRoughness] ? 1.0f : 0.0f,
                                                              static_cast<float>(mp->sheenColorTexCoord),
                                                              static_cast<float>(mp->sheenRoughTexCoord));
          it_MeshInfo->CnstBuffer.MaterialParams6 = XVECTOR3(mat->textures[(int)MatTexSlot::Clearcoat]          ? 1.0f : 0.0f,
                                                              mat->textures[(int)MatTexSlot::ClearcoatRoughness] ? 1.0f : 0.0f,
                                                              static_cast<float>(mp->clearcoatTexCoord),
                                                              static_cast<float>(mp->clearcoatRoughTexCoord));
          it_MeshInfo->CnstBuffer.MaterialParams7 = XVECTOR3(mat->textures[(int)MatTexSlot::Occlusion] ? 1.0f : 0.0f,
                                                              mp->occlusionStrength,
                                                              static_cast<float>(mp->occlusionTexCoord),
                                                              mat->textures[(int)MatTexSlot::Transmission] ? 1.0f : 0.0f);
          it_MeshInfo->CnstBuffer.MaterialParams8 = XVECTOR3(static_cast<float>(mp->transmissionTexCoord),
                                                              mat->textures[(int)MatTexSlot::SpecularFactor] ? 1.0f : 0.0f,
                                                              static_cast<float>(mp->specFactorTexCoord),
                                                              mat->textures[(int)MatTexSlot::SpecularColor]  ? 1.0f : 0.0f);
        } else {
          it_MeshInfo->CnstBuffer.MaterialParams  = XVECTOR3(sub_info->ClearcoatFactor, sub_info->ClearcoatRoughness, sub_info->Unlit ? 1.0f : 0.0f, emissiveMul);
          it_MeshInfo->CnstBuffer.MaterialParams5 = XVECTOR3(sub_info->SheenColorTex ? 1.0f : 0.0f, sub_info->SheenRoughnessTex ? 1.0f : 0.0f, (float)sub_info->SheenColorTexCoord, (float)sub_info->SheenRoughnessTexCoord);
          it_MeshInfo->CnstBuffer.MaterialParams6 = XVECTOR3(sub_info->ClearcoatTex ? 1.0f : 0.0f, sub_info->ClearcoatRoughnessTex ? 1.0f : 0.0f, (float)sub_info->ClearcoatTexCoord, (float)sub_info->ClearcoatRoughnessTexCoord);
          it_MeshInfo->CnstBuffer.MaterialParams7 = XVECTOR3(sub_info->OcclusionTex ? 1.0f : 0.0f, sub_info->OcclusionStrength, (float)sub_info->OcclusionTexCoord, sub_info->TransmissionTex ? 1.0f : 0.0f);
          it_MeshInfo->CnstBuffer.MaterialParams8 = XVECTOR3((float)sub_info->TransmissionTexCoord, sub_info->SpecularFactorTex ? 1.0f : 0.0f, (float)sub_info->SpecularFactorTexCoord, sub_info->SpecularColorTex ? 1.0f : 0.0f);
        }
        it_MeshInfo->CnstBuffer.MaterialParams2 = XVECTOR3(transmissionMul, refractionStrength, Textures[9] ? 1.0f : 0.0f, iblFactor);
        it_MeshInfo->CnstBuffer.MaterialParams3 = XVECTOR3(iblMipCount, iblBrdfLutEnabled, iblDiffuseMipLevel, 0.0f);

        // Phase A.5 step 2: bind shared IB pool if available, otherwise
        // fall back to the per-subset IB.
        IndexBuffer* ibToBind = sub_info->IB;
        if (sub_info->ibPoolAlloc.IsValid()) {
          if (IndexPool* ipool = MeshAssetCache::Get().GetIndexPool(sub_info->ibPoolAlloc.poolId)) {
            if (IndexBuffer* gpu = ipool->GetGPUBuffer()) {
              ibToBind = gpu;
            }
          }
        }
        IndexBufferFormat::E ibFmt = sub_info->IB32Bit ? IndexBufferFormat::R32 : IndexBufferFormat::R16;
        // Phase C step 3: IB-bind dedup via process-wide tracker.
        if (tracker.ShouldBindIB(ibToBind, ibFmt)) {
          ibToBind->Set(*T8DeviceContext, 0, ibFmt);
        }

        // Build final shader key: material features + global pass + toggles
        ShaderKey finalKey(sub_info->key.bits);
        finalKey.setPass(gKey.getPass());
        constexpr uint64_t featureMask = (1ull << ShaderKey::PASS_SHIFT) - 1ull;
        finalKey.bits |= (gKey.bits & featureMask);
        if (finalKey.has(ShaderKey::HEIGHT_MAP) && m_fParallaxEnabled > 0.5f) {
          uint8_t pass = finalKey.getPass();
          if (pass == PassType::GBUFFER || pass == PassType::FORWARD) {
            finalKey.bits |= ShaderKey::PARALLAX;
          }
        }

        s = g_pBaseDriver->GetShader(finalKey);
        if (!s) continue;

     //   if (s != last)
          update = true;

        BaseDriver::FaceCulling prevCull = g_pBaseDriver->m_FaceCulling;
        const bool subsetDoubleSided = mp ? (mp->doubleSided != 0) : sub_info->DoubleSided;
        bool changedCull = subsetDoubleSided && prevCull != BaseDriver::FRONT_AND_BACK;
        if (changedCull) {
          g_pBaseDriver->SetCullFace(BaseDriver::FRONT_AND_BACK);
        }

        if (update) {
          // D3D12 invariant: PSO is keyed by (shader, blend, depth,
          // cull, RT formats) and re-derived at Set time. Always call
          // s->Set; the driver dedupes via m_lastPSO. Tracker only
          // notes the shader change to invalidate texture cache
          // (per-shader rootParam map).
          s->Set(*T8DeviceContext);
          tracker.OnShaderChanged(s);

          it_MeshInfo->CB->UpdateFromBuffer(*T8DeviceContext, &it_MeshInfo->CnstBuffer.WVP[0]);
          it_MeshInfo->CB->Set(*T8DeviceContext);
        }
        // Phase B step 2 + C step 2: bind material textures via the
        // deduplicated MaterialAsset, with state-tracked dedup so that
        // consecutive subsets sharing a material skip the rebind.
        auto matTex = [&](MatTexSlot slot) -> Texture* {
          return mat ? mat->textures[(int)slot] : nullptr;
        };
        if (s->key.has(ShaderKey::DIFFUSE_MAP)) {
          Texture* t = matTex(MatTexSlot::BaseColor); if (!t) t = sub_info->DiffuseTex;
          bindTextureOnce(t, 0, "DiffuseTex", MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SPECULAR_MAP)) {
          Texture* t = matTex(MatTexSlot::Specular); if (!t) t = sub_info->SpecularTex;
          bindTextureOnce(t, 1, "SpecularTex", MaterialSamplerSlot);
        }

        if (s->key.has(ShaderKey::GLOSS_MAP)) {
          Texture* t = matTex(MatTexSlot::Gloss); if (!t) t = sub_info->GlossfTex;
          bindTextureOnce(t, 2, "GlossTex", MaterialSamplerSlot);
        }

        if (s->key.has(ShaderKey::NORMAL_MAP)) {
          Texture* t = matTex(MatTexSlot::Normal); if (!t) t = sub_info->NormalTex;
          bindTextureOnce(t, 3, "NormalTex", MaterialSamplerSlot);
        }
        if (EnvMap) {
          // EnvMap goes to slot 4 with its own dedicated tracker (so
          // it's not confused with material-driven slot 4 textures
          // from a different shader path).
          if (tracker.ShouldBindEnvMap(EnvMap)) {
            EnvMap->Set(*T8DeviceContext, 4, "texEnv");
          }
          EnvMap->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (s->key.has(ShaderKey::HEIGHT_MAP)) {
          Texture* t = matTex(MatTexSlot::Parallax); if (!t) t = sub_info->ParalaxTex;
          bindTextureOnce(t, 5, "HeightTex", MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::METALLIC_MAP)) {
          Texture* t = matTex(MatTexSlot::Metallic); if (!t) t = sub_info->MetallicTex;
          bindTextureOnce(t, 6, "MetallicTex", MaterialSamplerSlot);
        }
        bindTextureOnce(Textures[7], 7, "SceneDepthTex", ClampSamplerSlot);
        if (s->key.has(ShaderKey::EMISSIVE_MAP)) {
          Texture* t = matTex(MatTexSlot::Emissive); if (!t) t = sub_info->EmissiveTex;
          bindTextureOnce(t, 8, "EmissiveTex", MaterialSamplerSlot);
        }
        bindTextureOnce(Textures[9], 9, "SceneColorTex", ClampSamplerSlot);
        bindTextureOnce(Textures[EnvironmentTextureSlot::DiffuseIBL],  EnvironmentTextureSlot::DiffuseIBL,  "texIBLDiffuse",   ClampSamplerSlot);
        bindTextureOnce(Textures[EnvironmentTextureSlot::SpecularIBL], EnvironmentTextureSlot::SpecularIBL, "texIBLSpecular",  ClampSamplerSlot);
        bindTextureOnce(Textures[EnvironmentTextureSlot::BrdfLUT],     EnvironmentTextureSlot::BrdfLUT,     "texIBLBRDF",      ClampSamplerSlot);
        bindTextureOnce(Textures[EnvironmentTextureSlot::CharlieIBL],  EnvironmentTextureSlot::CharlieIBL,  "texIBLCharlie",   ClampSamplerSlot);
        bindTextureOnce(Textures[EnvironmentTextureSlot::CharlieLUT],  EnvironmentTextureSlot::CharlieLUT,  "texIBLCharlieLUT",ClampSamplerSlot);
        bindTextureOnce(Textures[EnvironmentTextureSlot::SheenELUT],   EnvironmentTextureSlot::SheenELUT,   "texIBLSheenELUT", ClampSamplerSlot);
        if (s->key.has(ShaderKey::SHEEN_COLOR_MAP)) {
          Texture* t = matTex(MatTexSlot::SheenColor); if (!t) t = sub_info->SheenColorTex;
          bindTextureOnce(t, MaterialTextureSlot::SheenColor, "SheenColorTex", MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SHEEN_ROUGHNESS_MAP)) {
          Texture* t = matTex(MatTexSlot::SheenRoughness); if (!t) t = sub_info->SheenRoughnessTex;
          bindTextureOnce(t, MaterialTextureSlot::SheenRoughness, "SheenRoughnessTex", MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::CLEARCOAT_MAP)) {
          Texture* t = matTex(MatTexSlot::Clearcoat); if (!t) t = sub_info->ClearcoatTex;
          bindTextureOnce(t, MaterialTextureSlot::Clearcoat, "ClearcoatTex", MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::CLEARCOAT_ROUGHNESS_MAP)) {
          Texture* t = matTex(MatTexSlot::ClearcoatRoughness); if (!t) t = sub_info->ClearcoatRoughnessTex;
          bindTextureOnce(t, MaterialTextureSlot::ClearcoatRoughness, "ClearcoatRoughnessTex", MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::OCCLUSION_MAP)) {
          Texture* t = matTex(MatTexSlot::Occlusion); if (!t) t = sub_info->OcclusionTex;
          bindTextureOnce(t, MaterialTextureSlot::Occlusion, "OcclusionTex", MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SPECULAR_FACTOR_MAP)) {
          Texture* t = matTex(MatTexSlot::SpecularFactor); if (!t) t = sub_info->SpecularFactorTex;
          bindTextureOnce(t, MaterialTextureSlot::SpecularFactor, "SpecularFactorTex", MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SPECULAR_COLOR_MAP)) {
          Texture* t = matTex(MatTexSlot::SpecularColor); if (!t) t = sub_info->SpecularColorTex;
          bindTextureOnce(t, MaterialTextureSlot::SpecularColor, "SpecularColorTex", MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::TRANSMISSION_MAP)) {
          Texture* t = matTex(MatTexSlot::Transmission); if (!t) t = sub_info->TransmissionTex;
          bindTextureOnce(t, MaterialTextureSlot::Transmission, "TransmissionTex", MaterialSamplerSlot);
        }

        T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
        // Phase A.5 step 2: when using shared pools the index/vertex
        // offsets steer the draw to this submesh's allocation. Falls
        // back to (count, 0, 0) on the legacy per-subset IB path.
        if (sub_info->ibPoolAlloc.IsValid() && it_MeshInfo->vbPoolAlloc.IsValid()) {
          T8DeviceContext->DrawIndexed(sub_info->ibPoolAlloc.count,
                                       sub_info->ibPoolAlloc.offsetElems,
                                       it_MeshInfo->vbPoolAlloc.offsetElems);
        } else {
          T8DeviceContext->DrawIndexed(sub_info->NumVertex, 0, 0);
        }
        if (changedCull) {
          g_pBaseDriver->SetCullFace(prevCull);
        }
        m_drawnSubsets++;
        last = s;
      }
    }
    if (ownsScope) tracker.End();
  }

  void RenderMesh::Destroy() {
    // Phase A.5 step 3 + B step 1: VB/IB are owned by MeshAssetCache
    // pools. Material data is shared via MaterialAssetCache. Only the
    // per-instance CB is released here; assets are dereferenced
    // through their respective caches.
    for (auto &mIt : Info) {
      if (mIt.CB) mIt.CB->release();
      mIt.CB = nullptr;
      for (auto &sIt : mIt.SubSets) {
        sIt.IB = nullptr;
        if (sIt.matAsset) {
          MaterialAssetCache::Get().Release(sIt.matAsset);
          sIt.matAsset = nullptr;
        }
      }
      mIt.IB = nullptr;
      mIt.VB = nullptr;
    }

    if (m_asset) {
      MeshAssetCache::Get().Release(m_asset);
      m_asset = nullptr;
    }
  }
}

