#include "pch.h"
/*********************************************************
 * T850 Engine — Skinned Mesh Renderer
 *
 * Extends RenderMesh with GPU skinning. Overrides Create()
 * to detect skin data and allocate larger constant buffers
 * that include bone matrices. Overrides Draw() to update
 * animation and upload bone matrices before rendering.
 *********************************************************/

#include <video/BaseDriver.h>
#include <scene/RenderSkinnedMesh.h>
#include <utils/Log.h>
#include <core/Core.h>
#include <cstring>

extern t800::AppBase *pApp;

namespace t800 {
  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  void RenderSkinnedMesh::Create() {
    // Call base Create() to set up geometry, materials, shaders, VB/IB
    RenderMesh::Create();

    // Check if the model has skin data
    if (!xFile || xFile->XMeshDataBase.empty()) return;
    xF::xMeshContainer* mc = xFile->XMeshDataBase[0];

    // Detect skin: check if any geometry has skin vertex attributes
    m_hasSkin = false;
    for (auto& geom : mc->Geometry) {
      if ((geom.VertexAttributes & xF::xMeshGeometry::HAS_SKINWEIGHTS0) &&
          (geom.VertexAttributes & xF::xMeshGeometry::HAS_SKININDEXES0)) {
        m_hasSkin = true;
        break;
      }
    }

    if (!m_hasSkin) {
      T8_LOG_INFO("[SkinnedMesh] No skin data found, will render as static");
      return;
    }

    // Set up HAS_SKINNING in all subset keys and recompile shader variants
    for (auto& meshInfo : Info) {
      for (auto& subset : meshInfo.SubSets) {
        subset.key.bits |= ShaderKey::HAS_SKINNING;
      }
    }

    // Recompile shaders with skinning enabled
    {
      char *vsSourceP, *fsSourceP;
      std::string vsName, fsName;
      if (g_pBaseDriver->UsesGLSL()) {
        vsSourceP = file2string("Shaders/VS_Mesh.glsl");
        fsSourceP = file2string("Shaders/FS_Mesh.glsl");
        vsName = "VS_Mesh.glsl"; fsName = "FS_Mesh.glsl";
      } else {
        vsSourceP = file2string("Shaders/VS_Mesh.hlsl");
        fsSourceP = file2string("Shaders/FS_Mesh.hlsl");
        vsName = "VS_Mesh.hlsl"; fsName = "FS_Mesh.hlsl";
      }
      std::string vstr(vsSourceP), fstr(fsSourceP);
      free(vsSourceP); free(fsSourceP);

      for (auto& meshInfo : Info) {
        for (auto& subset : meshInfo.SubSets) {
          ShaderKey matKey = subset.key;
          g_pBaseDriver->CreateShader(vstr, fstr, matKey, vsName, fsName);
          static const uint8_t passes[] = {
            PassType::FORWARD, PassType::GBUFFER,
            PassType::SHADOW_MAP, PassType::RADIAL_DEPTH
          };
          for (uint8_t pass : passes) {
            ShaderKey k(matKey.bits);
            k.setPass(pass);
            g_pBaseDriver->CreateShader(vstr, fstr, k, vsName, fsName);
          }
        }
      }
    }

    // Allocate skinned constant buffers (larger, with bone matrices)
    m_skinnedCBuffers.resize(Info.size());
    for (std::size_t i = 0; i < Info.size(); i++) {
      // Release the old CB and create a larger one
      if (Info[i].CB) {
        Info[i].CB->release();
      }
      BufferDesc bdesc;
      bdesc.byteWidth = sizeof(CBufferSkinned);
      bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
      Info[i].CB = (t800::ConstantBuffer*)T8Device->CreateBuffer(
          T8_BUFFER_TYPE::CONSTANT, bdesc, nullptr);

      // Initialize bone matrices to identity
      for (int b = 0; b < kMaxBones; b++) {
        m_skinnedCBuffers[i].BoneMatrices[b].Identity();
      }
    }

    // Initialize animation controller
    if (mc->Animation.isAnimInfo && !mc->Animation.Animations.empty()) {
      m_animController.Init(&mc->Animation, &mc->Skeleton, &mc->SkeletonAnimated);

      // Set skin weights from the first geometry's skin info
      for (auto& geom : mc->Geometry) {
        if (!geom.Info.SkinWeights.empty()) {
          m_animController.SetSkinWeights(geom.Info.SkinWeights);
          break;
        }
      }
    }

    T8_LOG_INFO("[SkinnedMesh] Created with %d bones, %zu animation sets",
                m_animController.GetNumBones(),
                mc->Animation.Animations.size());
  }

  void RenderSkinnedMesh::Draw(float *t, float *vp) {
    if (!m_hasSkin) {
      // Fall back to static rendering
      RenderMesh::Draw(t, vp);
      return;
    }

    // Update animation using scene delta time
    if (m_playing) {
      m_animController.SetUseSlerp(m_useSlerp);
      float deltaTime = pScProp ? pScProp->FrameDeltaSec : (1.0f / 60.0f);
      m_animController.Update(deltaTime);
    }

    // Copy bone matrices into all skinned cbuffers
    const XMATRIX44* bones = m_animController.GetBoneMatrices();
    int numBones = m_animController.GetNumBones();
    for (std::size_t i = 0; i < m_skinnedCBuffers.size() && i < Info.size(); i++) {
      // Copy base CBuffer data
      memcpy(&m_skinnedCBuffers[i].WVP, &Info[i].CnstBuffer.WVP,
             sizeof(RenderMesh::CBuffer));
      // Copy bone matrices
      int count = (numBones < kMaxBones) ? numBones : kMaxBones;
      for (int b = 0; b < count; b++) {
        m_skinnedCBuffers[i].BoneMatrices[b] = bones[b];
      }
    }

    // Now do the actual draw, similar to RenderMesh::Draw but using skinned CB
    Camera *pActualCamera = pScProp->pCameras[0];
    XMATRIX44 VP = pActualCamera->VP;

    m_totalSubsets = m_drawnSubsets = m_culledMeshes = 0;

    XVECTOR3 frustumPlanes[6];
    ExtractFrustumPlanes(VP, frustumPlanes);

    for (std::size_t i = 0; i < Info.size(); i++) {
      MeshInfo *it_MeshInfo = &Info[i];

      if (!AABBInsideFrustum(it_MeshInfo->bounds, transform, frustumPlanes)) {
        m_culledMeshes++;
        continue;
      }

      // Update matrices in the skinned cbuffer
      XMATRIX44 WVP = transform * VP;
      XMATRIX44 WorldView = transform * pActualCamera->View;
      XVECTOR3 infoCam = XVECTOR3(pActualCamera->NPlane, pActualCamera->FPlane, pActualCamera->Fov, 1.0f);

      m_skinnedCBuffers[i].WVP = WVP;
      m_skinnedCBuffers[i].World = transform;
      m_skinnedCBuffers[i].WorldView = WorldView;

      if (pScProp) {
        m_skinnedCBuffers[i].Light0Pos = pScProp->Lights[0].Position;
        m_skinnedCBuffers[i].Light0Col = pScProp->Lights[0].Color;
        m_skinnedCBuffers[i].CameraPos = pActualCamera->Eye;
        m_skinnedCBuffers[i].CameraInfo = infoCam;
        m_skinnedCBuffers[i].Light0Dir = pScProp->Lights[0].Direction;
      }

      m_skinnedCBuffers[i].ParallaxSettings = XVECTOR3(
          m_fParallaxLowSamples, m_fParallaxHighSamples, m_fParallaxHeight);
      m_skinnedCBuffers[i].ParallaxSettings.w = m_fParallaxEnabled;
      m_skinnedCBuffers[i].ParallaxShadowSettings = XVECTOR3(
          m_fParallaxShadowMinLayers, m_fParallaxShadowMaxLayers,
          m_fParallaxShadowSoftness);
      m_skinnedCBuffers[i].ParallaxShadowSettings.w = m_fParallaxShadowStrength;

      unsigned int stride = it_MeshInfo->VertexSize;
      unsigned int offset = 0;

      ShaderBase *s = nullptr;
      it_MeshInfo->VB->Set(*T8DeviceContext, stride, offset);

      std::size_t numSubsets = it_MeshInfo->SubSets.size();
      for (std::size_t k = 0; k < numSubsets; k++) {
        m_totalSubsets++;
        SubSetInfo *sub_info = &it_MeshInfo->SubSets[k];

        if (!AABBInsideFrustum(sub_info->bounds, transform, frustumPlanes))
          continue;

        m_skinnedCBuffers[i].AmbientColor = sub_info->AmbientColor;
        m_skinnedCBuffers[i].DiffuseColor = sub_info->DiffuseColor;
        m_skinnedCBuffers[i].SpecularColor = sub_info->SpecularColor;
        m_skinnedCBuffers[i].PBRParams = sub_info->PBRParams;
        m_skinnedCBuffers[i].Intensities = sub_info->Intensities;
        m_skinnedCBuffers[i].Intensities.w = (float)sub_info->MatID;

        sub_info->IB->Set(*T8DeviceContext, 0,
                          sub_info->IB32Bit ? T8_IB_FORMAR::R32
                                            : T8_IB_FORMAR::R16);

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

        s->Set(*T8DeviceContext);
        // Upload the larger skinned CB
        it_MeshInfo->CB->UpdateFromBuffer(*T8DeviceContext,
                                           &m_skinnedCBuffers[i].WVP[0]);
        it_MeshInfo->CB->Set(*T8DeviceContext);

        if (s->key.has(ShaderKey::DIFFUSE_MAP) && sub_info->DiffuseTex)
          sub_info->DiffuseTex->Set(*T8DeviceContext, 0, "DiffuseTex");
        if (s->key.has(ShaderKey::SPECULAR_MAP) && sub_info->SpecularTex)
          sub_info->SpecularTex->Set(*T8DeviceContext, 1, "SpecularTex");
        if (s->key.has(ShaderKey::GLOSS_MAP) && sub_info->GlossfTex)
          sub_info->GlossfTex->Set(*T8DeviceContext, 2, "GlossTex");
        if (s->key.has(ShaderKey::NORMAL_MAP) && sub_info->NormalTex)
          sub_info->NormalTex->Set(*T8DeviceContext, 3, "NormalTex");
        if (EnvMap)
          EnvMap->Set(*T8DeviceContext, 4, "texEnv");
        if (s->key.has(ShaderKey::HEIGHT_MAP) && sub_info->ParalaxTex)
          sub_info->ParalaxTex->Set(*T8DeviceContext, 5, "HeightTex");
        if (s->key.has(ShaderKey::METALLIC_MAP) && sub_info->MetallicTex)
          sub_info->MetallicTex->Set(*T8DeviceContext, 6, "MetallicTex");
        if (s->key.has(ShaderKey::DIFFUSE_MAP) && sub_info->DiffuseTex)
          sub_info->DiffuseTex->SetSampler(*T8DeviceContext);

        T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::TRIANLE_LIST);
        T8DeviceContext->DrawIndexed(sub_info->NumVertex, 0, 0);
        m_drawnSubsets++;
      }
    }
  }

  void RenderSkinnedMesh::Destroy() {
    RenderMesh::Destroy();
    m_skinnedCBuffers.clear();
  }

} // namespace t800
