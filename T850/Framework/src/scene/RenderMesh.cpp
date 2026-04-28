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

          T8_LOG_INFO("[MeshInfo]     Material[%zu] '%s': defaults=%zu subsetTris=%u subsetVertices=%u triStart=%u vertexStart=%u shaderKey=0x%08X matID=%d alpha=%s cutoff=%.3f doubleSided=%d unlit=%d fresnel=%d",
                      j, materialName, material ? material->EffectInstance.pDefaults.size() : 0,
                      subset ? subset->NumTris : (sourceSubset ? sourceSubset->NumTris : 0),
                      subset ? subset->NumVertex : (sourceSubset ? sourceSubset->NumVertex : 0),
                      subset ? subset->TriStart : (sourceSubset ? sourceSubset->TriStart : 0),
                      subset ? subset->VertexStart : (sourceSubset ? sourceSubset->VertexStart : 0),
                      subset ? subset->key.bits : 0u,
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

            T8_LOG_INFO("[MeshInfo]       KeyFlags: normals=%d tangents=%d binormals=%d uv0=%d uv1=%d baseColorMap=%d specularMap=%d roughnessMap=%d normalMap=%d heightMap=%d metallicMap=%d emissiveMap=%d clearcoatMap=%d gltfTangentSpace=%d",
                        subset->key.has(ShaderKey::HAS_NORMALS) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_TANGENTS) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_BINORMALS) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_TEXCOORD0) ? 1 : 0,
                        subset->key.has(ShaderKey::HAS_TEXCOORD1) ? 1 : 0,
                        subset->key.has(ShaderKey::DIFFUSE_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::SPECULAR_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::GLOSS_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::NORMAL_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::HEIGHT_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::METALLIC_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::EMISSIVE_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::CLEARCOAT_MAP) ? 1 : 0,
                        subset->key.has(ShaderKey::GLTF_TANGENT_SPACE) ? 1 : 0);
          }
        }
      }
    }
  }


  void RenderMesh::Load(const char *filename)
  {
    xFile = pApp->resourceManager.Load(filename);
  }

  void RenderMesh::Create() {
    GatherInfo();
    T8_LOG_INFO("Mesh Create: %zu geometries, building GPU buffers", xFile->MeshInfo.size());
    for (std::size_t i = 0; i < xFile->MeshInfo.size(); i++) {
      xFinalGeometry *it = &xFile->MeshInfo[i];
      xMeshGeometry *pActual = &xFile->XMeshDataBase[0]->Geometry[i];
      MeshInfo  *it_MeshInfo = &Info[i];

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
          }
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

          t850::BufferDesc bdesc;
          bdesc.byteWidth = it_subsetinfo->NumTris * 3 * sizeof(unsigned short);
          bdesc.usage = BufferUsage::DEFAULT;
          it_subsetinfo->IB = (t850::IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, bdesc, tmpIndexex);

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

          t850::BufferDesc bdesc;
          bdesc.byteWidth = it_subsetinfo->NumTris * 3 * sizeof(unsigned int);
          bdesc.usage = BufferUsage::DEFAULT;
          it_subsetinfo->IB = (t850::IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, bdesc, tmpIndexex);

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

      t850::BufferDesc buffdesc;
      buffdesc.byteWidth = pActual->NumVertices*it->VertexSize;
      buffdesc.usage = BufferUsage::DEFAULT;
      it_MeshInfo->VB = (t850::VertexBuffer*)T8Device->CreateBuffer(BufferType::VERTEX, buffdesc, &it->pData[0]);

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
                   i, buffdesc.byteWidth, it->VertexSize, pActual->NumVertices,
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

      if (!kUse32) {
        buffdesc.byteWidth = static_cast<int>(pActual->Triangles.size() * sizeof(unsigned short));
        buffdesc.usage = BufferUsage::DEFAULT;
        it_MeshInfo->IB = (t850::IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, buffdesc, &pActual->Triangles[0]);
      } else {
        buffdesc.byteWidth = static_cast<int>(pActual->Triangles32.size() * sizeof(unsigned int));
        buffdesc.usage = BufferUsage::DEFAULT;
        it_MeshInfo->IB = (t850::IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, buffdesc, &pActual->Triangles32[0]);
      }
    }

    LogLoadedMeshDetails(xFile, Info);
    XMatIdentity(transform);
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
            if (mDef->NameParam == "clearcoatMap" || mDef->NameParam == "clearcoatRoughnessMap")
              matKey.bits |= ShaderKey::CLEARCOAT_MAP;
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

        T8_LOG_VERBOSE("  Material %d: key=0x%08X noLight=%d fresnel=%d matID=%d",
                       j, matKey.bits, (int)bNoLight, (int)bUseFresnel, stmp.MatID);

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
    return subInfo.TransmissionFactor > 0.0f ? 0 : 1;
  }

  static int NonForwardSubsetGroup(const RenderMesh::SubSetInfo& subInfo) {
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
      it_MeshInfo->VB->Set(*T8DeviceContext, stride, offset);

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

		it_MeshInfo->CnstBuffer.AmbientColor = sub_info->AmbientColor;
		it_MeshInfo->CnstBuffer.DiffuseColor = sub_info->DiffuseColor;
		it_MeshInfo->CnstBuffer.SpecularColor = sub_info->SpecularColor;
		it_MeshInfo->CnstBuffer.PBRParams = sub_info->PBRParams;
		it_MeshInfo->CnstBuffer.Intensities = sub_info->Intensities;
		it_MeshInfo->CnstBuffer.Intensities.w = (float)sub_info->MatID;
        it_MeshInfo->CnstBuffer.EmissiveColor = sub_info->EmissiveColor;
        it_MeshInfo->CnstBuffer.AlphaParams = XVECTOR3((float)sub_info->AlphaMode, sub_info->AlphaCutoff, sub_info->DoubleSided ? 1.0f : 0.0f, sub_info->TransmissionFactor);
        it_MeshInfo->CnstBuffer.ForwardParams = XVECTOR3((float)g_pBaseDriver->width, (float)g_pBaseDriver->height, Textures[7] ? 1.0f : 0.0f, sub_info->IOR);
        it_MeshInfo->CnstBuffer.TexCoordSets = XVECTOR3((float)sub_info->DiffuseTexCoord, (float)sub_info->NormalTexCoord, (float)sub_info->MetallicTexCoord, (float)sub_info->EmissiveTexCoord);
        float emissiveMul = pScProp ? pScProp->MaterialEmissiveIntensity : 1.0f;
        float transmissionMul = pScProp ? pScProp->MaterialTransmissionMultiplier : 1.0f;
        float refractionStrength = pScProp ? pScProp->MaterialRefractionStrength : 0.03f;
        float iblFactor = pScProp ? pScProp->IBLFactor : 1.0f;
        float iblMipCount = pScProp ? pScProp->IBLMipCount : 4.0f;
        float iblDiffuseMipLevel = pScProp ? pScProp->IBLDiffuseMipLevel : 4.0f;
        float iblBrdfLutEnabled = pScProp ? pScProp->IBLBRDFLUTEnabled : 0.0f;
        it_MeshInfo->CnstBuffer.MaterialParams = XVECTOR3(sub_info->ClearcoatFactor, sub_info->ClearcoatRoughness, sub_info->Unlit ? 1.0f : 0.0f, emissiveMul);
        it_MeshInfo->CnstBuffer.MaterialParams2 = XVECTOR3(transmissionMul, refractionStrength, Textures[9] ? 1.0f : 0.0f, iblFactor);
        it_MeshInfo->CnstBuffer.MaterialParams3 = XVECTOR3(iblMipCount, iblBrdfLutEnabled, iblDiffuseMipLevel, 0.0f);
        it_MeshInfo->CnstBuffer.MaterialParams4 = XVECTOR3(sub_info->SheenColor.x, sub_info->SheenColor.y, sub_info->SheenColor.z, sub_info->SheenRoughness);
        it_MeshInfo->CnstBuffer.MaterialParams5 = XVECTOR3(sub_info->SheenColorTex ? 1.0f : 0.0f, sub_info->SheenRoughnessTex ? 1.0f : 0.0f, (float)sub_info->SheenColorTexCoord, (float)sub_info->SheenRoughnessTexCoord);
        it_MeshInfo->CnstBuffer.MaterialParams6 = XVECTOR3(sub_info->ClearcoatTex ? 1.0f : 0.0f, sub_info->ClearcoatRoughnessTex ? 1.0f : 0.0f, (float)sub_info->ClearcoatTexCoord, (float)sub_info->ClearcoatRoughnessTexCoord);
        it_MeshInfo->CnstBuffer.BaseColorUVTransform0 = sub_info->BaseColorUVTransform0;
        it_MeshInfo->CnstBuffer.BaseColorUVTransform1 = sub_info->BaseColorUVTransform1;
        it_MeshInfo->CnstBuffer.NormalUVTransform0 = sub_info->NormalUVTransform0;
        it_MeshInfo->CnstBuffer.NormalUVTransform1 = sub_info->NormalUVTransform1;
        it_MeshInfo->CnstBuffer.MetallicUVTransform0 = sub_info->MetallicUVTransform0;
        it_MeshInfo->CnstBuffer.MetallicUVTransform1 = sub_info->MetallicUVTransform1;
        it_MeshInfo->CnstBuffer.EmissiveUVTransform0 = sub_info->EmissiveUVTransform0;
        it_MeshInfo->CnstBuffer.EmissiveUVTransform1 = sub_info->EmissiveUVTransform1;
        it_MeshInfo->CnstBuffer.SheenColorUVTransform0 = sub_info->SheenColorUVTransform0;
        it_MeshInfo->CnstBuffer.SheenColorUVTransform1 = sub_info->SheenColorUVTransform1;
        it_MeshInfo->CnstBuffer.SheenRoughnessUVTransform0 = sub_info->SheenRoughnessUVTransform0;
        it_MeshInfo->CnstBuffer.SheenRoughnessUVTransform1 = sub_info->SheenRoughnessUVTransform1;
        it_MeshInfo->CnstBuffer.ClearcoatUVTransform0 = sub_info->ClearcoatUVTransform0;
        it_MeshInfo->CnstBuffer.ClearcoatUVTransform1 = sub_info->ClearcoatUVTransform1;
        it_MeshInfo->CnstBuffer.ClearcoatRoughnessUVTransform0 = sub_info->ClearcoatRoughnessUVTransform0;
        it_MeshInfo->CnstBuffer.ClearcoatRoughnessUVTransform1 = sub_info->ClearcoatRoughnessUVTransform1;

        sub_info->IB->Set(*T8DeviceContext, 0,
                          sub_info->IB32Bit ? IndexBufferFormat::R32
                                            : IndexBufferFormat::R16);

        // Build final shader key: material features + global pass + toggles
        ShaderKey finalKey(sub_info->key.bits);
        finalKey.setPass(gKey.getPass());
        constexpr uint32_t featureMask = (1u << ShaderKey::PASS_SHIFT) - 1;
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
        bool changedCull = sub_info->DoubleSided && prevCull != BaseDriver::FRONT_AND_BACK;
        if (changedCull) {
          g_pBaseDriver->SetCullFace(BaseDriver::FRONT_AND_BACK);
        }

        if (update) {
          s->Set(*T8DeviceContext);

          it_MeshInfo->CB->UpdateFromBuffer(*T8DeviceContext, &it_MeshInfo->CnstBuffer.WVP[0]);
          it_MeshInfo->CB->Set(*T8DeviceContext);
        }
        if (s->key.has(ShaderKey::DIFFUSE_MAP)) {
          sub_info->DiffuseTex->Set(*T8DeviceContext, 0, "DiffuseTex");
          sub_info->DiffuseTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SPECULAR_MAP)) {
          sub_info->SpecularTex->Set(*T8DeviceContext, 1, "SpecularTex");
          sub_info->SpecularTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }

        if (s->key.has(ShaderKey::GLOSS_MAP)) {
          sub_info->GlossfTex->Set(*T8DeviceContext, 2, "GlossTex");
          sub_info->GlossfTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }

        if (s->key.has(ShaderKey::NORMAL_MAP)) {
          sub_info->NormalTex->Set(*T8DeviceContext, 3, "NormalTex");
          sub_info->NormalTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (EnvMap) {
          EnvMap->Set(*T8DeviceContext, 4, "texEnv");
          EnvMap->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (s->key.has(ShaderKey::HEIGHT_MAP)) {
          sub_info->ParalaxTex->Set(*T8DeviceContext, 5, "HeightTex");
          sub_info->ParalaxTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::METALLIC_MAP)) {
          sub_info->MetallicTex->Set(*T8DeviceContext, 6, "MetallicTex");
          sub_info->MetallicTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (Textures[7]) {
          Textures[7]->Set(*T8DeviceContext, 7, "SceneDepthTex");
          Textures[7]->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (s->key.has(ShaderKey::EMISSIVE_MAP) && sub_info->EmissiveTex) {
          sub_info->EmissiveTex->Set(*T8DeviceContext, 8, "EmissiveTex");
          sub_info->EmissiveTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (Textures[9]) {
          Textures[9]->Set(*T8DeviceContext, 9, "SceneColorTex");
          Textures[9]->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (Textures[EnvironmentTextureSlot::DiffuseIBL]) {
          Textures[EnvironmentTextureSlot::DiffuseIBL]->Set(*T8DeviceContext, EnvironmentTextureSlot::DiffuseIBL, "texIBLDiffuse");
          Textures[EnvironmentTextureSlot::DiffuseIBL]->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (Textures[EnvironmentTextureSlot::SpecularIBL]) {
          Textures[EnvironmentTextureSlot::SpecularIBL]->Set(*T8DeviceContext, EnvironmentTextureSlot::SpecularIBL, "texIBLSpecular");
          Textures[EnvironmentTextureSlot::SpecularIBL]->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (Textures[EnvironmentTextureSlot::BrdfLUT]) {
          Textures[EnvironmentTextureSlot::BrdfLUT]->Set(*T8DeviceContext, EnvironmentTextureSlot::BrdfLUT, "texIBLBRDF");
          Textures[EnvironmentTextureSlot::BrdfLUT]->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (Textures[EnvironmentTextureSlot::CharlieIBL]) {
          Textures[EnvironmentTextureSlot::CharlieIBL]->Set(*T8DeviceContext, EnvironmentTextureSlot::CharlieIBL, "texIBLCharlie");
          Textures[EnvironmentTextureSlot::CharlieIBL]->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (Textures[EnvironmentTextureSlot::CharlieLUT]) {
          Textures[EnvironmentTextureSlot::CharlieLUT]->Set(*T8DeviceContext, EnvironmentTextureSlot::CharlieLUT, "texIBLCharlieLUT");
          Textures[EnvironmentTextureSlot::CharlieLUT]->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (Textures[EnvironmentTextureSlot::SheenELUT]) {
          Textures[EnvironmentTextureSlot::SheenELUT]->Set(*T8DeviceContext, EnvironmentTextureSlot::SheenELUT, "texIBLSheenELUT");
          Textures[EnvironmentTextureSlot::SheenELUT]->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (sub_info->SheenColorTex) {
          sub_info->SheenColorTex->Set(*T8DeviceContext, MaterialTextureSlot::SheenColor, "SheenColorTex");
          sub_info->SheenColorTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (sub_info->SheenRoughnessTex) {
          sub_info->SheenRoughnessTex->Set(*T8DeviceContext, MaterialTextureSlot::SheenRoughness, "SheenRoughnessTex");
          sub_info->SheenRoughnessTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::CLEARCOAT_MAP)) {
          Texture* clearcoatTex = sub_info->ClearcoatTex ? sub_info->ClearcoatTex : sub_info->ClearcoatRoughnessTex;
          Texture* clearcoatRoughnessTex = sub_info->ClearcoatRoughnessTex ? sub_info->ClearcoatRoughnessTex : sub_info->ClearcoatTex;
          if (clearcoatTex) {
            clearcoatTex->Set(*T8DeviceContext, MaterialTextureSlot::Clearcoat, "ClearcoatTex");
            clearcoatTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
          }
          if (clearcoatRoughnessTex) {
            clearcoatRoughnessTex->Set(*T8DeviceContext, MaterialTextureSlot::ClearcoatRoughness, "ClearcoatRoughnessTex");
            clearcoatRoughnessTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
          }
        }

        T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
        T8DeviceContext->DrawIndexed(sub_info->NumVertex, 0, 0);
        if (changedCull) {
          g_pBaseDriver->SetCullFace(prevCull);
        }
        m_drawnSubsets++;
        last = s;
      }
    }
  }

  void RenderMesh::Destroy() {
    //release resources
    for (auto &mIt : Info) {
      for (auto &sIt : mIt.SubSets) {
        sIt.IB->release();
      }
      mIt.CB->release();
      mIt.IB->release();
      mIt.VB->release();
    }
  }
}

