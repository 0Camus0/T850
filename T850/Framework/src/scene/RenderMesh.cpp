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
#include <utils/ThreadPool.h>
#include <video/GLShader.h>
#include <video/GLDriver.h>

#if defined(OS_WINDOWS)
#include <video/windows/D3D11Shader.h>
#include <video/windows/D3D11Driver.h>
#endif
#include "core/Core.h"
#include <utils/Log.h>

#define CHANGE_TO_RH 0
#define DEBUG_MODEL 0
extern t800::AppBase		  *pApp;
namespace t800 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;

  
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

      t800::BufferDesc bdesc;
      bdesc.byteWidth = sizeof(RenderMesh::CBuffer);
      bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
      it_MeshInfo->CB = (t800::ConstantBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::CONSTANT, bdesc);

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
				  it_subsetinfo->DiffuseColor.w = 1.0f;
			  }

			  if (mDef->NameParam == "specularColor") {
				  it_subsetinfo->SpecularColor.x = mDef->CaseFloat[0];
				  it_subsetinfo->SpecularColor.y = mDef->CaseFloat[1];
				  it_subsetinfo->SpecularColor.z = mDef->CaseFloat[2];
				  it_subsetinfo->SpecularColor.w = 1.0f;
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

          t800::BufferDesc bdesc;
          bdesc.byteWidth = it_subsetinfo->NumTris * 3 * sizeof(unsigned short);
          bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
          it_subsetinfo->IB = (t800::IndexBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::INDEX, bdesc, tmpIndexex);

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

          t800::BufferDesc bdesc;
          bdesc.byteWidth = it_subsetinfo->NumTris * 3 * sizeof(unsigned int);
          bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
          it_subsetinfo->IB = (t800::IndexBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::INDEX, bdesc, tmpIndexex);

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

      t800::BufferDesc buffdesc;
      buffdesc.byteWidth = pActual->NumVertices*it->VertexSize;
      buffdesc.usage = T8_BUFFER_USAGE::DEFAULT;
      it_MeshInfo->VB = (t800::VertexBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::VERTEX, buffdesc, &it->pData[0]);

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
        buffdesc.usage = T8_BUFFER_USAGE::DEFAULT;
        it_MeshInfo->IB = (t800::IndexBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::INDEX, buffdesc, &pActual->Triangles[0]);
      } else {
        buffdesc.byteWidth = static_cast<int>(pActual->Triangles32.size() * sizeof(unsigned int));
        buffdesc.usage = T8_BUFFER_USAGE::DEFAULT;
        it_MeshInfo->IB = (t800::IndexBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::INDEX, buffdesc, &pActual->Triangles32[0]);
      }
    }

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

    unsigned int params = TEXT_BASIC_PARAMS::MIPMAPS;

    if (tiled)
      params |= TEXT_BASIC_PARAMS::TILED;
    else
      params |= TEXT_BASIC_PARAMS::CLAMP_TO_EDGE;

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

    for (std::size_t i = 0; i < numGeometries; i++) {
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
      it_MeshInfo->CnstBuffer.CameraInfo = infoCam;
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
      uint8_t currentPass = gKey.getPass();
      std::stable_sort(drawOrder.begin(), drawOrder.end(),
        [&](std::size_t a, std::size_t b) {
          ShaderKey ka(it_MeshInfo->SubSets[a].key.bits); ka.setPass(currentPass);
          ShaderKey kb(it_MeshInfo->SubSets[b].key.bits); kb.setPass(currentPass);
          return ka.bits < kb.bits;
        });

      for (std::size_t ki = 0; ki < numSubsets; ki++) {
        std::size_t k = drawOrder[ki];
        bool update = false;
        SubSetInfo *sub_info = &it_MeshInfo->SubSets[k];

        // Per-subset frustum cull
        if (!AABBInsideFrustum(sub_info->bounds, transform, frustumPlanes))
          continue;

		it_MeshInfo->CnstBuffer.AmbientColor = sub_info->AmbientColor;
		it_MeshInfo->CnstBuffer.DiffuseColor = sub_info->DiffuseColor;
		it_MeshInfo->CnstBuffer.SpecularColor = sub_info->SpecularColor;
		it_MeshInfo->CnstBuffer.PBRParams = sub_info->PBRParams;
		it_MeshInfo->CnstBuffer.Intensities = sub_info->Intensities;
		it_MeshInfo->CnstBuffer.Intensities.w = (float)sub_info->MatID;

        sub_info->IB->Set(*T8DeviceContext, 0,
                          sub_info->IB32Bit ? T8_IB_FORMAR::R32
                                            : T8_IB_FORMAR::R16);

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

        if (update) {
          s->Set(*T8DeviceContext);  

          it_MeshInfo->CB->UpdateFromBuffer(*T8DeviceContext, &it_MeshInfo->CnstBuffer.WVP[0]);
          it_MeshInfo->CB->Set(*T8DeviceContext);
        }
        if (s->key.has(ShaderKey::DIFFUSE_MAP)) {
          sub_info->DiffuseTex->Set(*T8DeviceContext, 0, "DiffuseTex");
        }
        if (s->key.has(ShaderKey::SPECULAR_MAP)) {
          sub_info->SpecularTex->Set(*T8DeviceContext, 1, "SpecularTex");
        }

        if (s->key.has(ShaderKey::GLOSS_MAP)) {
          sub_info->GlossfTex->Set(*T8DeviceContext, 2, "GlossTex");
        }

        if (s->key.has(ShaderKey::NORMAL_MAP)) {
          sub_info->NormalTex->Set(*T8DeviceContext, 3, "NormalTex");
        }
        if (EnvMap) {
          EnvMap->Set(*T8DeviceContext, 4, "texEnv");
        }
        if (s->key.has(ShaderKey::HEIGHT_MAP)) {
          sub_info->ParalaxTex->Set(*T8DeviceContext, 5, "HeightTex");
        }
        if (s->key.has(ShaderKey::METALLIC_MAP)) {
          sub_info->MetallicTex->Set(*T8DeviceContext, 6, "MetallicTex");
        }
        if (s->key.has(ShaderKey::DIFFUSE_MAP)) {
          sub_info->DiffuseTex->SetSampler(*T8DeviceContext);
        }

        T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
        T8DeviceContext->DrawIndexed(sub_info->NumVertex, 0, 0);
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

