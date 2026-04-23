#include "pch.h"
/*********************************************************
 * T850 Engine — Skinned Mesh Renderer
 *
 * Extends RenderMesh with GPU skinning. Overrides Create()
 * to detect skin data and allocate larger constant buffers
 * that include bone matrices. Overrides Draw() to update
 * animation and upload bone matrices before rendering.
 *
 * Also provides debug wireframe (mesh edges with depth test)
 * and skeleton bone visualization (no depth test, magenta).
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

    // Use quaternion+translation skinning on all backends (smaller CB, fits GL limit)
    uint32_t skinBit = ShaderKey::HAS_SKINNING_QT;
    m_useQuatSkinning = true;

    for (auto& meshInfo : Info) {
      for (auto& subset : meshInfo.SubSets) {
        subset.key.bits |= skinBit;
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

    // Allocate constant buffers — size depends on skinning mode
    std::size_t cbSize = m_useQuatSkinning ? sizeof(CBufferSkinnedQT) : sizeof(CBufferSkinned);
    m_skinnedCBuffers.resize(Info.size());
    m_skinnedQTBuffers.resize(Info.size());
    for (std::size_t i = 0; i < Info.size(); i++) {
      if (Info[i].CB) {
        Info[i].CB->release();
      }
      BufferDesc bdesc;
      bdesc.byteWidth = static_cast<int>(cbSize);
      bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
      Info[i].CB = (t800::ConstantBuffer*)T8Device->CreateBuffer(
          T8_BUFFER_TYPE::CONSTANT, bdesc, nullptr);

      // Initialize bone data
      for (int b = 0; b < kMaxBones; b++) {
        m_skinnedCBuffers[i].BoneMatrices[b].Identity();
        m_skinnedQTBuffers[i].BoneQuats[b] = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
        m_skinnedQTBuffers[i].BoneTrans[b] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
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

    // Build wireframe and skeleton debug buffers
    BuildWireframeBuffers();
    BuildSkeletonBuffers();
    m_lineRenderer.Create();
  }

  // ── Wireframe buffer construction ──────────────────────

  void RenderSkinnedMesh::BuildWireframeBuffers() {
    if (!xFile || xFile->XMeshDataBase.empty()) return;
    xF::xMeshContainer* mc = xFile->XMeshDataBase[0];

    // Collect positions and triangle-edge indices across all geometries
    std::vector<unsigned int> lineIdx;
    m_wireTotalVerts = 0;

    for (auto& geom : mc->Geometry) {
      unsigned baseV = m_wireTotalVerts;
      m_wireTotalVerts += (unsigned)geom.Positions.size();

      // Extract edges from triangles
      if (geom.Indices32Bit && !geom.Triangles32.empty()) {
        const auto& tris = geom.Triangles32;
        for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
          unsigned int a = baseV + tris[t + 0];
          unsigned int b = baseV + tris[t + 1];
          unsigned int c = baseV + tris[t + 2];
          lineIdx.push_back(a); lineIdx.push_back(b);
          lineIdx.push_back(b); lineIdx.push_back(c);
          lineIdx.push_back(c); lineIdx.push_back(a);
        }
      } else {
        const auto& tris = geom.Triangles;
        for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
          unsigned int a = baseV + tris[t + 0];
          unsigned int b = baseV + tris[t + 1];
          unsigned int c = baseV + tris[t + 2];
          lineIdx.push_back(a); lineIdx.push_back(b);
          lineIdx.push_back(b); lineIdx.push_back(c);
          lineIdx.push_back(c); lineIdx.push_back(a);
        }
      }
    }

    if (lineIdx.empty() || m_wireTotalVerts == 0) return;

    // Create IB (16-bit when possible)
    if (m_wireTotalVerts <= 65535) {
      std::vector<unsigned short> idx16(lineIdx.size());
      for (std::size_t i = 0; i < lineIdx.size(); i++)
        idx16[i] = (unsigned short)lineIdx[i];
      m_wireIB = LineRenderer::CreateIndexBuffer16(idx16.data(), (unsigned)idx16.size());
      m_wireUse32Bit = false;
    } else {
      m_wireIB = LineRenderer::CreateIndexBuffer32(lineIdx.data(), (unsigned)lineIdx.size());
      m_wireUse32Bit = true;
    }
    m_wireIndexCount = (unsigned)lineIdx.size();

    // Pre-allocate CPU position buffer (filled each frame by UpdateSkinnedPositions)
    m_wirePositions.resize(m_wireTotalVerts * 4, 0.0f);

    // Pre-allocate wireframe VB (DYNAMIC — updated each frame via UpdateFromBuffer)
    m_wireVB = LineRenderer::CreatePositionVB(m_wirePositions.data(), m_wireTotalVerts,
                                              T8_BUFFER_USAGE::DINAMIC);
  }

  void RenderSkinnedMesh::BuildSkeletonBuffers() {
    if (!xFile || xFile->XMeshDataBase.empty()) return;
    xF::xMeshContainer* mc = xFile->XMeshDataBase[0];
    const auto& bones = mc->SkeletonAnimated.Bones;
    int n = (int)bones.size();
    if (n == 0) return;

    // One line segment (2 vertices, 2 indices) per bone that has a parent
    std::vector<unsigned short> lineIdx;
    unsigned vertCount = 0;
    for (int i = 0; i < n; i++) {
      if (bones[i].Dad != (unsigned short)i && bones[i].Dad < n) {
        // line from parent position to child position
        lineIdx.push_back((unsigned short)vertCount);
        lineIdx.push_back((unsigned short)(vertCount + 1));
        vertCount += 2;
      }
    }

    if (lineIdx.empty()) return;

    m_skelIB = LineRenderer::CreateIndexBuffer16(lineIdx.data(), (unsigned)lineIdx.size());
    m_skelIndexCount = (unsigned)lineIdx.size();
    m_skelPositions.resize(vertCount * 4, 0.0f);

    // Pre-allocate skeleton VB (DYNAMIC — updated each frame)
    m_skelVB = LineRenderer::CreatePositionVB(m_skelPositions.data(), vertCount,
                                              T8_BUFFER_USAGE::DINAMIC);
  }

  // ── CPU skinning for wireframe positions ───────────────

  void RenderSkinnedMesh::UpdateSkinnedPositions() {
    if (!xFile || xFile->XMeshDataBase.empty()) return;
    xF::xMeshContainer* mc = xFile->XMeshDataBase[0];

    const XMATRIX44* bones = m_animController.GetBoneMatrices();
    int numBones = m_animController.GetNumBones();
    unsigned vOff = 0;

    for (auto& geom : mc->Geometry) {
      bool hasSkin = !geom.SkinWeights.empty() && !geom.SkinIndices.empty();
      unsigned nv = (unsigned)geom.Positions.size();

      for (unsigned v = 0; v < nv; v++) {
        float px = geom.Positions[v].x;
        float py = geom.Positions[v].y;
        float pz = geom.Positions[v].z;
        float ox = px, oy = py, oz = pz;

        if (hasSkin && v < (unsigned)geom.SkinIndices.size()) {
          // Accumulate blended bone matrix
          XMATRIX44 skinMat;
          memset(&skinMat, 0, sizeof(skinMat));

          for (int b = 0; b < 4; b++) {
            int joint = (int)geom.SkinIndices[v].v[b];
            float weight = geom.SkinWeights[v].v[b];
            if (weight <= 0.0f) continue;
            if (joint < 0 || joint >= numBones) continue;

            const XMATRIX44& bm = bones[joint];
            for (int r = 0; r < 4; r++)
              for (int c = 0; c < 4; c++)
                skinMat.m[r][c] += bm.m[r][c] * weight;
          }

          // Row-vector multiply: pos * skinMat
          ox = px * skinMat.m[0][0] + py * skinMat.m[1][0] + pz * skinMat.m[2][0] + skinMat.m[3][0];
          oy = px * skinMat.m[0][1] + py * skinMat.m[1][1] + pz * skinMat.m[2][1] + skinMat.m[3][1];
          oz = px * skinMat.m[0][2] + py * skinMat.m[1][2] + pz * skinMat.m[2][2] + skinMat.m[3][2];
        }

        unsigned idx = (vOff + v) * 4;
        m_wirePositions[idx + 0] = ox;
        m_wirePositions[idx + 1] = oy;
        m_wirePositions[idx + 2] = oz;
        m_wirePositions[idx + 3] = 1.0f;
      }
      vOff += nv;
    }
  }

  void RenderSkinnedMesh::UpdateSkeletonPositions() {
    const xF::xSkeleton* skel = m_animController.GetAnimSkeleton();
    if (!skel) return;
    const auto& bones = skel->Bones;
    int n = (int)bones.size();

    // Extract LH world position from each bone's Combined matrix.
    // Combined is in RH space. LH position = (m[3][0], m[3][1], -m[3][2]).
    unsigned vOff = 0;
    for (int i = 0; i < n; i++) {
      if (bones[i].Dad != (unsigned short)i && bones[i].Dad < n) {
        unsigned dadIdx = bones[i].Dad;
        // Parent position
        m_skelPositions[vOff * 4 + 0] = bones[dadIdx].Combined.m[3][0];
        m_skelPositions[vOff * 4 + 1] = bones[dadIdx].Combined.m[3][1];
        m_skelPositions[vOff * 4 + 2] = -bones[dadIdx].Combined.m[3][2];
        m_skelPositions[vOff * 4 + 3] = 1.0f;
        vOff++;
        // Child position
        m_skelPositions[vOff * 4 + 0] = bones[i].Combined.m[3][0];
        m_skelPositions[vOff * 4 + 1] = bones[i].Combined.m[3][1];
        m_skelPositions[vOff * 4 + 2] = -bones[i].Combined.m[3][2];
        m_skelPositions[vOff * 4 + 3] = 1.0f;
        vOff++;
      }
    }
  }

  // ── Debug wireframe draw ───────────────────────────────

  void RenderSkinnedMesh::DrawWireframe(Texture* depthTex, int viewW, int viewH, float farPlane) {
    if (!m_hasSkin || !m_wireIB || !m_wireVB || m_wireIndexCount == 0 || !m_lineRenderer.IsReady())
      return;

    if (!pScProp || pScProp->pCameras.empty()) return;
    Camera* cam = pScProp->pCameras[0];

    UpdateSkinnedPositions();

    // Update existing VB with CPU-skinned positions
    m_wireVB->UpdateFromBuffer(*T8DeviceContext, m_wirePositions.data());

    m_lineRenderer.SetDepthTestEnabled(true);
    m_lineRenderer.SetDepthTexture(depthTex);
    m_lineRenderer.SetViewport(viewW, viewH);
    m_lineRenderer.SetFarPlane(farPlane);

    XMATRIX44 identity;
    identity.Identity();
    XVECTOR3 wireColor(0.0f, 1.0f, 0.0f, 1.0f);  // green
    auto ibFmt = m_wireUse32Bit ? T8_IB_FORMAR::R32 : T8_IB_FORMAR::R16;

    m_lineRenderer.DrawLines(identity, cam->VP, wireColor,
                             m_wireVB, m_wireIB, m_wireIndexCount, 16, ibFmt);
  }

  void RenderSkinnedMesh::DrawSkeleton() {
    if (!m_hasSkin || !m_skelIB || !m_skelVB || m_skelIndexCount == 0 || !m_lineRenderer.IsReady())
      return;

    if (!pScProp || pScProp->pCameras.empty()) return;
    Camera* cam = pScProp->pCameras[0];

    UpdateSkeletonPositions();

    // Update existing VB with current bone positions
    m_skelVB->UpdateFromBuffer(*T8DeviceContext, m_skelPositions.data());

    m_lineRenderer.SetDepthTestEnabled(false);

    XMATRIX44 identity;
    identity.Identity();
    XVECTOR3 skelColor(1.0f, 0.0f, 1.0f, 1.0f);  // magenta

    m_lineRenderer.DrawLines(identity, cam->VP, skelColor,
                             m_skelVB, m_skelIB, m_skelIndexCount, 16, T8_IB_FORMAR::R16);
  }

  // ── Main draw ──────────────────────────────────────────

  void RenderSkinnedMesh::Draw(float *t, float *vp) {
    if (!m_hasSkin) {
      // Fall back to static rendering
      RenderMesh::Draw(t, vp);
      return;
    }

    // Dump matrices on first frame for debugging — compute bind-pose first
    static bool sDumped = false;
    if (!sDumped) {
      // Compute hierarchy and finals at bind pose (no animation applied yet)
      // This tells us if IBM * BindWorldTransform = Identity
      m_animController.Update(0.0f);  // zero dt = compute hierarchy without advancing time
      m_animController.DumpMatrices("anim_debug_bindpose.txt");
      sDumped = true;
    }

    // Update animation using scene delta time
    if (m_playing) {
      m_animController.SetUseSlerp(m_useSlerp);
      float deltaTime = pScProp ? pScProp->FrameDeltaSec : (1.0f / 60.0f);
      m_animController.Update(deltaTime);
    }

    // Copy bone data into skinned cbuffers based on mode
    int numBones = m_animController.GetNumBones();
    int count = (numBones < kMaxBones) ? numBones : kMaxBones;

    if (m_useQuatSkinning) {
      const XQUATERNION* quats = m_animController.GetBoneQuats();
      const XVECTOR3*    trans = m_animController.GetBoneTrans();
      for (std::size_t i = 0; i < m_skinnedQTBuffers.size() && i < Info.size(); i++) {
        memcpy(&m_skinnedQTBuffers[i].WVP, &Info[i].CnstBuffer.WVP,
               sizeof(RenderMesh::CBuffer));
        for (int b = 0; b < count; b++) {
          m_skinnedQTBuffers[i].BoneQuats[b] = XVECTOR3(quats[b].x, quats[b].y, quats[b].z, quats[b].w);
          m_skinnedQTBuffers[i].BoneTrans[b] = trans[b];
        }
      }
    } else {
      const XMATRIX44* bones = m_animController.GetBoneMatrices();
      for (std::size_t i = 0; i < m_skinnedCBuffers.size() && i < Info.size(); i++) {
        memcpy(&m_skinnedCBuffers[i].WVP, &Info[i].CnstBuffer.WVP,
               sizeof(RenderMesh::CBuffer));
        for (int b = 0; b < count; b++) {
          m_skinnedCBuffers[i].BoneMatrices[b] = bones[b];
        }
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

      // Update matrices in the skinned cbuffer (base portion is same layout for both modes)
      XMATRIX44 WVP = transform * VP;
      XMATRIX44 WorldView = transform * pActualCamera->View;
      XVECTOR3 infoCam = XVECTOR3(pActualCamera->NPlane, pActualCamera->FPlane, pActualCamera->Fov, 1.0f);

      // Get pointer to the base CBuffer portion (same offset in both structs)
      CBufferSkinned& cb = m_skinnedCBuffers[i];
      CBufferSkinnedQT& cbQT = m_skinnedQTBuffers[i];
      auto& base = m_useQuatSkinning ? (CBufferSkinned&)cbQT : cb;
      // Note: CBufferSkinnedQT has same base fields layout as CBufferSkinned
      // (same first 15 fields). We can safely cast the first portion.

      if (m_useQuatSkinning) {
        cbQT.WVP = WVP; cbQT.World = transform; cbQT.WorldView = WorldView;
        if (pScProp) {
          cbQT.Light0Pos = pScProp->Lights[0].Position;
          cbQT.Light0Col = pScProp->Lights[0].Color;
          cbQT.CameraPos = pActualCamera->Eye;
          cbQT.CameraInfo = infoCam;
          cbQT.Light0Dir = pScProp->Lights[0].Direction;
        }
        cbQT.ParallaxSettings = XVECTOR3(m_fParallaxLowSamples, m_fParallaxHighSamples, m_fParallaxHeight);
        cbQT.ParallaxSettings.w = m_fParallaxEnabled;
        cbQT.ParallaxShadowSettings = XVECTOR3(m_fParallaxShadowMinLayers, m_fParallaxShadowMaxLayers, m_fParallaxShadowSoftness);
        cbQT.ParallaxShadowSettings.w = m_fParallaxShadowStrength;
      } else {
        cb.WVP = WVP; cb.World = transform; cb.WorldView = WorldView;
        if (pScProp) {
          cb.Light0Pos = pScProp->Lights[0].Position;
          cb.Light0Col = pScProp->Lights[0].Color;
          cb.CameraPos = pActualCamera->Eye;
          cb.CameraInfo = infoCam;
          cb.Light0Dir = pScProp->Lights[0].Direction;
        }
        cb.ParallaxSettings = XVECTOR3(m_fParallaxLowSamples, m_fParallaxHighSamples, m_fParallaxHeight);
        cb.ParallaxSettings.w = m_fParallaxEnabled;
        cb.ParallaxShadowSettings = XVECTOR3(m_fParallaxShadowMinLayers, m_fParallaxShadowMaxLayers, m_fParallaxShadowSoftness);
        cb.ParallaxShadowSettings.w = m_fParallaxShadowStrength;
      }

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

        if (m_useQuatSkinning) {
          cbQT.AmbientColor = sub_info->AmbientColor;
          cbQT.DiffuseColor = sub_info->DiffuseColor;
          cbQT.SpecularColor = sub_info->SpecularColor;
          cbQT.PBRParams = sub_info->PBRParams;
          cbQT.Intensities = sub_info->Intensities;
          cbQT.Intensities.w = (float)sub_info->MatID;
        } else {
          cb.AmbientColor = sub_info->AmbientColor;
          cb.DiffuseColor = sub_info->DiffuseColor;
          cb.SpecularColor = sub_info->SpecularColor;
          cb.PBRParams = sub_info->PBRParams;
          cb.Intensities = sub_info->Intensities;
          cb.Intensities.w = (float)sub_info->MatID;
        }

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
        // Upload the skinned CB (matrix or QT variant)
        if (m_useQuatSkinning) {
          it_MeshInfo->CB->UpdateFromBuffer(*T8DeviceContext, &cbQT.WVP[0]);
        } else {
          it_MeshInfo->CB->UpdateFromBuffer(*T8DeviceContext, &cb.WVP[0]);
        }
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
    m_lineRenderer.Destroy();
    if (m_wireVB) { m_wireVB->release(); m_wireVB = nullptr; }
    if (m_wireIB) { m_wireIB->release(); m_wireIB = nullptr; }
    if (m_skelVB) { m_skelVB->release(); m_skelVB = nullptr; }
    if (m_skelIB) { m_skelIB->release(); m_skelIB = nullptr; }
    m_wirePositions.clear();
    m_skelPositions.clear();

    RenderMesh::Destroy();
    m_skinnedCBuffers.clear();
  }

} // namespace t800
