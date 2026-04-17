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

#include <scene/RenderMesh.h>
#include <video/GLShader.h>
#include <video/GLDriver.h>

#if defined(OS_WINDOWS)
#include <video/windows/D3DXShader.h>
#include <video/windows/D3DXDriver.h>
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

        delete[] tmpIndexex;
      }

      it_MeshInfo->VertexSize = it->VertexSize;

      t800::BufferDesc buffdesc;
      buffdesc.byteWidth = pActual->NumVertices*it->VertexSize;
      buffdesc.usage = T8_BUFFER_USAGE::DEFAULT;
      it_MeshInfo->VB = (t800::VertexBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::VERTEX, buffdesc, &it->pData[0]);

      T8_LOG_DEBUG("  Geometry %zu: VB=%u bytes (stride=%u, %d verts), IB=%zu tris",
                   i, buffdesc.byteWidth, it->VertexSize, pActual->NumVertices, pActual->Triangles.size()/3);

#if CHANGE_TO_RH
      for (std::size_t a = 0; a < pActual->Triangles.size(); a += 3) {
        unsigned short i0 = pActual->Triangles[a + 0];
        unsigned short i2 = pActual->Triangles[a + 2];
        pActual->Triangles[a + 0] = i2;
        pActual->Triangles[a + 2] = i0;
      }
#endif

      buffdesc.byteWidth = static_cast<int>(pActual->Triangles.size() * sizeof(unsigned short));
      buffdesc.usage = T8_BUFFER_USAGE::DEFAULT;
      it_MeshInfo->IB = (t800::IndexBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::INDEX, buffdesc, &pActual->Triangles[0]);
    }

    XMatIdentity(transform);
  }

  void RenderMesh::GatherInfo() {

    char *vsSourceP;
    char *fsSourceP;
    std::string vsName, fsName;
    if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::OPENGL) {
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

  void RenderMesh::Draw(float *t, float *vp) {
    if (t)
      transform = t;

    Camera *pActualCamera = pScProp->pCameras[0];

    for (std::size_t i = 0; i < xFile->MeshInfo.size(); i++) {
      MeshInfo  *it_MeshInfo = &Info[i];
      xMeshGeometry *pActual = &xFile->XMeshDataBase[0]->Geometry[i];

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

      for (std::size_t k = 0; k < it_MeshInfo->SubSets.size(); k++) {
        bool update = false;
        SubSetInfo *sub_info = &it_MeshInfo->SubSets[k];

		it_MeshInfo->CnstBuffer.AmbientColor = sub_info->AmbientColor;
		it_MeshInfo->CnstBuffer.DiffuseColor = sub_info->DiffuseColor;
		it_MeshInfo->CnstBuffer.SpecularColor = sub_info->SpecularColor;
		it_MeshInfo->CnstBuffer.PBRParams = sub_info->PBRParams;
		it_MeshInfo->CnstBuffer.Intensities = sub_info->Intensities;
		it_MeshInfo->CnstBuffer.Intensities.w = (float)sub_info->MatID;

        sub_info->IB->Set(*T8DeviceContext, 0, T8_IB_FORMAR::R16);

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

