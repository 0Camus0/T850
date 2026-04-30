#include <pch.h>
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
#include <scene/RenderGraph.h>
#include <scene/RenderSkinnedMesh.h>
#include <utils/Log.h>
#include <core/Core.h>
#include <cstring>
#include <cmath>
#include <algorithm>

extern t850::AppBase *pApp;

namespace t850 {
  extern Device*        T8Device;
  extern DeviceContext*  T8DeviceContext;

  static constexpr unsigned MaterialSamplerSlot = 0;
  static constexpr unsigned ClampSamplerSlot = 1;

  namespace {
    bool IsForwardOnlySubset(const RenderMesh::SubSetInfo& subInfo) {
      return subInfo.AlphaMode == 2 || subInfo.TransmissionFactor > 0.0f;
    }

    bool ShouldDrawSubsetInPass(const RenderMesh::SubSetInfo& subInfo, uint8_t pass) {
      const bool forwardOnly = IsForwardOnlySubset(subInfo);
      if (pass == PassType::GBUFFER || pass == PassType::SHADOW_MAP || pass == PassType::RADIAL_DEPTH) {
        return !forwardOnly;
      }
      if (pass == PassType::FORWARD) {
        return forwardOnly;
      }
      return true;
    }

    float SubsetDistanceSqToCamera(const RenderMesh::SubSetInfo& subInfo, const XMATRIX44& world, const XVECTOR3& eye) {
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

    int ForwardSubsetGroup(const RenderMesh::SubSetInfo& subInfo) {
      return subInfo.TransmissionFactor > 0.0f ? 0 : 1;
    }

    int NonForwardSubsetGroup(const RenderMesh::SubSetInfo& subInfo) {
      return subInfo.AlphaMode == 1 ? 1 : 0;
    }

    int GeometryNonForwardGroup(const RenderMesh::MeshInfo& meshInfo, uint8_t pass) {
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

    int GeometryForwardGroup(const RenderMesh::MeshInfo& meshInfo) {
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

    float GeometryForwardDistanceSq(const RenderMesh::MeshInfo& meshInfo, const XMATRIX44& world, const XVECTOR3& eye) {
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
  }

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

    // Use texture-based bone matrix skinning
    uint64_t skinBit = ShaderKey::HAS_SKINNING_TEX;

    for (auto& meshInfo : Info) {
      for (auto& subset : meshInfo.SubSets) {
        subset.key.bits |= skinBit;
      }
    }

    // Create bone texture (RGBA32F, each bone = 4 texels for 4 matrix rows)
    {
      int numBones = static_cast<int>(mc->SkeletonAnimated.Bones.size());
      if (numBones > kMaxBones) numBones = kMaxBones;
      int numTexels = numBones * 4; // 4 texels per bone
      m_boneTexWidth = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(numTexels))));
      if (m_boneTexWidth < 1) m_boneTexWidth = 1;
      m_boneTexData.resize(m_boneTexWidth * m_boneTexWidth * 4, 0.0f); // RGBA per texel
      m_boneTexture = T8Device->CreateFloatTexture(m_boneTexWidth, m_boneTexWidth,
                                                    m_boneTexData.data());
      T8_LOG_INFO("[SkinnedMesh] Bone texture: %dx%d (%d bones, %d texels)",
                  m_boneTexWidth, m_boneTexWidth, numBones, numTexels);
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

    // Compile wireframe shader (VS_Mesh + FS_WireMesh with skinning)
    {
      char *vsWireP, *fsWireP;
      std::string vsWireName, fsWireName;
      if (g_pBaseDriver->UsesGLSL()) {
        vsWireP = file2string("Shaders/VS_Mesh.glsl");
        fsWireP = file2string("Shaders/FS_WireMesh.glsl");
        vsWireName = "VS_Mesh.glsl"; fsWireName = "FS_WireMesh.glsl";
      } else {
        vsWireP = file2string("Shaders/VS_Mesh.hlsl");
        fsWireP = file2string("Shaders/FS_WireMesh.hlsl");
        vsWireName = "VS_Mesh.hlsl"; fsWireName = "FS_WireMesh.hlsl";
      }
      std::string vsWStr(vsWireP), fsWStr(fsWireP);
      free(vsWireP); free(fsWireP);

      ShaderKey wireKey(0);
      wireKey.bits |= skinBit;
      if (!Info.empty() && !Info[0].SubSets.empty())
        wireKey.bits |= (Info[0].SubSets[0].key.bits & 0x1Full);
      wireKey.setPass(32); // unused pass type — avoids collision with mesh shaders
      g_pBaseDriver->CreateShader(vsWStr, fsWStr, wireKey, vsWireName, fsWireName);
      m_wireShader = g_pBaseDriver->GetShader(wireKey);
    }

    // Allocate constant buffers — base CBuffer only (bones go via texture now)
    m_skinnedCBuffers.resize(Info.size());
    m_skinnedQTBuffers.resize(Info.size());
    for (std::size_t i = 0; i < Info.size(); i++) {
      if (Info[i].CB) {
        Info[i].CB->release();
      }
      BufferDesc bdesc;
      bdesc.byteWidth = sizeof(RenderMesh::CBuffer);
      bdesc.usage = BufferUsage::DEFAULT;
      Info[i].CB = (t850::ConstantBuffer*)T8Device->CreateBuffer(
          BufferType::CONSTANT, bdesc, nullptr);
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

    // Compile skeleton line shader using the exact WireframeSphere approach
    // (VS_W + FS_W, #version 130 with blanked precision qualifiers).
    // This is known to work on all GL backends.
    {
      char* vsP = file2string(g_pBaseDriver->UsesGLSL() ? "Shaders/VS_W.glsl" : "Shaders/VS_W.hlsl");
      char* fsP = file2string(g_pBaseDriver->UsesGLSL() ? "Shaders/FS_W.glsl" : "Shaders/FS_W.hlsl");
      if (vsP && fsP) {
        std::string vs(vsP), fs(fsP);
        free(vsP); free(fsP);
        if (g_pBaseDriver->UsesGLSL()) {
          std::string defs;
#if defined(USING_OPENGL)
          defs += "#version 130\n\n";
          defs += "#define lowp \n\n";
          defs += "#define mediump \n\n";
          defs += "#define highp \n\n";
#elif defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
          defs += "#version 300 es\n\n";
          defs += "#define ES_30\n\n";
#endif
          vs = defs + vs;
          fs = defs + fs;
        }
        int sid = g_pBaseDriver->CreateShader(vs, fs);
        m_skelShader = g_pBaseDriver->GetShaderIdx(sid);
      } else {
        if (vsP) free(vsP);
        if (fsP) free(fsP);
      }
      if (m_skelShader) {
        BufferDesc bd;
        bd.byteWidth = sizeof(XMATRIX44);  // 64 bytes — just WVP, matching VS_W
        bd.usage = BufferUsage::DEFAULT;
        m_skelCB = (ConstantBuffer*)T8Device->CreateBuffer(BufferType::CONSTANT, bd);
      }
    }
  }

  // ── Wireframe buffer construction (per-geometry line-list IBs) ─

  void RenderSkinnedMesh::BuildWireframeBuffers() {
    if (!xFile || xFile->XMeshDataBase.empty()) return;
    xF::xMeshContainer* mc = xFile->XMeshDataBase[0];

    m_wireGeo.resize(mc->Geometry.size());

    for (std::size_t gi = 0; gi < mc->Geometry.size(); gi++) {
      auto& geom = mc->Geometry[gi];
      std::vector<unsigned int> lineIdx;

      if (geom.Indices32Bit && !geom.Triangles32.empty()) {
        const auto& tris = geom.Triangles32;
        for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
          unsigned int a = tris[t + 0], b = tris[t + 1], c = tris[t + 2];
          lineIdx.push_back(a); lineIdx.push_back(b);
          lineIdx.push_back(b); lineIdx.push_back(c);
          lineIdx.push_back(c); lineIdx.push_back(a);
        }
      } else {
        const auto& tris = geom.Triangles;
        for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
          unsigned int a = tris[t + 0], b = tris[t + 1], c = tris[t + 2];
          lineIdx.push_back(a); lineIdx.push_back(b);
          lineIdx.push_back(b); lineIdx.push_back(c);
          lineIdx.push_back(c); lineIdx.push_back(a);
        }
      }

      if (lineIdx.empty()) continue;

      unsigned maxVert = (unsigned)geom.Positions.size();
      if (maxVert <= 65535) {
        std::vector<unsigned short> idx16(lineIdx.size());
        for (std::size_t j = 0; j < lineIdx.size(); j++)
          idx16[j] = (unsigned short)lineIdx[j];
        m_wireGeo[gi].IB = LineRenderer::CreateIndexBuffer16(idx16.data(), (unsigned)idx16.size());
        m_wireGeo[gi].use32Bit = false;
      } else {
        m_wireGeo[gi].IB = LineRenderer::CreateIndexBuffer32(lineIdx.data(), (unsigned)lineIdx.size());
        m_wireGeo[gi].use32Bit = true;
      }
      m_wireGeo[gi].indexCount = (unsigned)lineIdx.size();
    }
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
                                              BufferUsage::DINAMIC);
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

  // ── Debug wireframe draw (GPU-skinned) ─────────────────

  void RenderSkinnedMesh::DrawWireframe() {
    if (!m_hasSkin || !m_wireShader || m_wireGeo.empty()) return;
    if (!pScProp || pScProp->pCameras.empty()) return;

    Camera* cam = pScProp->pCameras[0];
    XMATRIX44 WVP = transform * cam->VP;
    XMATRIX44 WorldView = transform * cam->View;
    // CameraInfo: .x=near, .y=far, .z=viewportW, .w=viewportH
    XVECTOR3 infoCam = XVECTOR3(cam->NPlane, cam->FPlane,
                                 (float)m_wireViewW, (float)m_wireViewH);
    XVECTOR3 wireColor(0.0f, 1.0f, 0.0f, 1.0f);

    // Base CBuffer (no bone data — bones come from texture)
    RenderMesh::CBuffer wireCB;
    wireCB.WVP = WVP;
    wireCB.World = transform;
    wireCB.WorldView = WorldView;
    wireCB.CameraInfo = infoCam;
    wireCB.DiffuseColor = wireColor;

    for (std::size_t i = 0; i < Info.size() && i < m_wireGeo.size(); i++) {
      if (!m_wireGeo[i].IB || m_wireGeo[i].indexCount == 0) continue;

      MeshInfo* mi = &Info[i];
      mi->VB->Set(*T8DeviceContext, mi->VertexSize, 0);

      auto ibFmt = m_wireGeo[i].use32Bit ? IndexBufferFormat::R32 : IndexBufferFormat::R16;
      m_wireGeo[i].IB->Set(*T8DeviceContext, 0, ibFmt);

      T8DeviceContext->SetPrimitiveTopology(Topology::LINE_LIST);

      m_wireShader->Set(*T8DeviceContext);
      mi->CB->UpdateFromBuffer(*T8DeviceContext, &wireCB.WVP[0]);
      mi->CB->Set(*T8DeviceContext);

      // Bind bone texture to VS slot 7
      if (m_boneTexture)
        m_boneTexture->SetVS(*T8DeviceContext, 7, "u_BoneTex");

      // Bind GBuffer depth texture for manual depth comparison in FS
      if (m_wireDepthTex)
        m_wireDepthTex->Set(*T8DeviceContext, 0, "depthTex");

      T8DeviceContext->DrawIndexed(m_wireGeo[i].indexCount, 0, 0);

      T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
    }
  }

  void RenderSkinnedMesh::DrawSkeleton() {
    if (!m_hasSkin || !m_skelIB || !m_skelVB || !m_skelShader || !m_skelCB
        || m_skelIndexCount == 0)
      return;

    if (!pScProp || pScProp->pCameras.empty()) return;
    Camera* cam = pScProp->pCameras[0];

    UpdateSkeletonPositions();
    m_skelVB->UpdateFromBuffer(*T8DeviceContext, m_skelPositions.data());

    // WVP = mesh world transform * VP (skeleton positions are in model space)
    XMATRIX44 wvp = transform * cam->VP;

    // Draw using the exact WireframeSphere pattern (known to work on all APIs)
    m_skelIB->Set(*T8DeviceContext, 0, IndexBufferFormat::R16);
    m_skelVB->Set(*T8DeviceContext, 16, 0);
    // Topology BEFORE shader — Vulkan bakes topology into pipeline at Set() time
    T8DeviceContext->SetPrimitiveTopology(Topology::LINE_LIST);
    m_skelShader->Set(*T8DeviceContext);
    m_skelCB->UpdateFromBuffer(*T8DeviceContext, &wvp[0]);
    m_skelCB->Set(*T8DeviceContext);
    T8DeviceContext->DrawIndexed(m_skelIndexCount, 0, 0);
    T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
  }

  // ── Animation update + bone texture upload (call BEFORE render passes) ──

  void RenderSkinnedMesh::UpdateAnimationAndBones() {
    if (!m_hasSkin || !m_boneTexture) return;

    // Dump matrices on first frame for debugging
    static bool sDumped = false;
    if (!sDumped) {
      m_animController.Update(0.0f);
      m_animController.DumpMatrices("anim_debug_bindpose.txt");
      sDumped = true;
    }

    // Update animation
    if (m_playing && !m_animController.GetKeyframeMode()) {
      m_animController.SetUseSlerp(m_useSlerp);
      float deltaTime = pScProp ? pScProp->FrameDeltaSec : (1.0f / 60.0f);
      m_animController.Update(deltaTime);
    }

    // Upload bone matrices to texture
    int numBones = m_animController.GetNumBones();
    int count = (numBones < kMaxBones) ? numBones : kMaxBones;
    const XMATRIX44* bones = m_animController.GetBoneMatrices();
    for (int b = 0; b < count; b++) {
      int texelBase = b * 4 * 4;
      memcpy(&m_boneTexData[texelBase],      &bones[b].m[0][0], 16);
      memcpy(&m_boneTexData[texelBase + 4],  &bones[b].m[1][0], 16);
      memcpy(&m_boneTexData[texelBase + 8],  &bones[b].m[2][0], 16);
      memcpy(&m_boneTexData[texelBase + 12], &bones[b].m[3][0], 16);
    }
    m_boneTexture->UpdateFloatData(*T8DeviceContext, m_boneTexWidth, m_boneTexWidth,
                                    m_boneTexData.data());
  }

  // ── Main draw ──────────────────────────────────────────

  void RenderSkinnedMesh::Draw(float *t, float *vp) {
    if (t) transform = t;  // Accept world transform from PrimitiveInstance

    if (!m_hasSkin) {
      // Fall back to static rendering
      RenderMesh::Draw(t, vp);
      return;
    }

    // Now do the actual draw using base CBuffer + bone texture
    // (animation update + texture upload already done in UpdateAnimationAndBones)
    Camera *pActualCamera = pScProp->pCameras[0];
    XMATRIX44 VP = pActualCamera->VP;

    m_totalSubsets = m_drawnSubsets = m_culledMeshes = 0;

    XVECTOR3 frustumPlanes[6];
    ExtractFrustumPlanes(VP, frustumPlanes);

    uint8_t currentPass = gKey.getPass();
    std::vector<std::size_t> geometryOrder(Info.size());
    for (std::size_t i = 0; i < Info.size(); i++) geometryOrder[i] = i;
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

    for (std::size_t oi = 0; oi < geometryOrder.size(); oi++) {
      std::size_t i = geometryOrder[oi];
      MeshInfo *it_MeshInfo = &Info[i];

      if (!AABBInsideFrustum(it_MeshInfo->bounds, transform, frustumPlanes)) {
        m_culledMeshes++;
        continue;
      }

      // Fill base CBuffer (no bone data — that's in the texture)
      RenderMesh::CBuffer baseCB;
      XMATRIX44 WVP = transform * VP;
      XMATRIX44 WorldView = transform * pActualCamera->View;
      XVECTOR3 infoCam = XVECTOR3(pActualCamera->NPlane, pActualCamera->FPlane, pActualCamera->Fov, 1.0f);

      baseCB.WVP = WVP;
      baseCB.World = transform;
      baseCB.WorldView = WorldView;
      if (pScProp) {
        baseCB.Light0Pos = pScProp->Lights[0].Position;
        baseCB.Light0Col = pScProp->Lights[0].Color;
        baseCB.CameraPos = pActualCamera->Eye;
        unsigned int numLights = static_cast<unsigned int>(pScProp->ActiveLights);
        if (numLights > pScProp->Lights.size())
          numLights = static_cast<unsigned int>(pScProp->Lights.size());
        if (numLights > 128u) numLights = 128u;
        infoCam.w = static_cast<float>(numLights);
        baseCB.CameraInfo = infoCam;
        baseCB.Light0Dir = pScProp->Lights[0].Direction;
        for (int li = 0; li < 128; li++) {
          baseCB.LightPositions[li] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
          baseCB.LightColors[li] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
        }
        for (int ri = 0; ri < 32; ri++) {
          baseCB.LightRadius[ri] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
        }
        for (unsigned int li = 0; li < numLights; li++) {
          Light& light = pScProp->Lights[li];
          if (light.Type == LIGHT_DIRECTIONAL) {
            baseCB.LightPositions[li] = XVECTOR3(light.Direction.x, light.Direction.y, light.Direction.z, 0.0f);
          } else {
            baseCB.LightPositions[li] = XVECTOR3(light.Position.x, light.Position.y, light.Position.z, 1.0f);
          }
          baseCB.LightColors[li] = XVECTOR3(light.Color.x, light.Color.y, light.Color.z, light.Intensity);
          XVECTOR3& radiusPack = baseCB.LightRadius[li >> 2];
          if ((li & 3u) == 0u) radiusPack.x = light.radius;
          else if ((li & 3u) == 1u) radiusPack.y = light.radius;
          else if ((li & 3u) == 2u) radiusPack.z = light.radius;
          else radiusPack.w = light.radius;
        }
      }
      baseCB.ParallaxSettings = XVECTOR3(m_fParallaxLowSamples, m_fParallaxHighSamples, m_fParallaxHeight);
      baseCB.ParallaxSettings.w = m_fParallaxEnabled;
      baseCB.ParallaxShadowSettings = XVECTOR3(m_fParallaxShadowMinLayers, m_fParallaxShadowMaxLayers, m_fParallaxShadowSoftness);
      baseCB.ParallaxShadowSettings.w = m_fParallaxShadowStrength;

      unsigned int stride = it_MeshInfo->VertexSize;
      unsigned int offset = 0;

      ShaderBase *s = nullptr;
      it_MeshInfo->VB->Set(*T8DeviceContext, stride, offset);

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
        m_totalSubsets++;
        SubSetInfo *sub_info = &it_MeshInfo->SubSets[k];

        if (!ShouldDrawSubsetInPass(*sub_info, currentPass))
          continue;

        if (!AABBInsideFrustum(sub_info->bounds, transform, frustumPlanes))
          continue;

        baseCB.AmbientColor = sub_info->AmbientColor;
        baseCB.DiffuseColor = sub_info->DiffuseColor;
        baseCB.SpecularColor = sub_info->SpecularColor;
        baseCB.PBRParams = sub_info->PBRParams;
        baseCB.Intensities = sub_info->Intensities;
        baseCB.Intensities.w = (float)sub_info->MatID;
        baseCB.EmissiveColor = sub_info->EmissiveColor;
        baseCB.AlphaParams = XVECTOR3((float)sub_info->AlphaMode, sub_info->AlphaCutoff, sub_info->DoubleSided ? 1.0f : 0.0f, sub_info->TransmissionFactor);
        baseCB.ForwardParams = XVECTOR3((float)g_pBaseDriver->width, (float)g_pBaseDriver->height, Textures[7] ? 1.0f : 0.0f, sub_info->IOR);
        baseCB.TexCoordSets = XVECTOR3((float)sub_info->DiffuseTexCoord, (float)sub_info->NormalTexCoord, (float)sub_info->MetallicTexCoord, (float)sub_info->EmissiveTexCoord);
        float emissiveMul = pScProp ? pScProp->MaterialEmissiveIntensity : 1.0f;
        float transmissionMul = pScProp ? pScProp->MaterialTransmissionMultiplier : 1.0f;
        float refractionStrength = pScProp ? pScProp->MaterialRefractionStrength : 0.03f;
        float iblFactor = pScProp ? pScProp->IBLFactor : 1.0f;
        float iblMipCount = pScProp ? pScProp->IBLMipCount : 4.0f;
        float iblDiffuseMipLevel = pScProp ? pScProp->IBLDiffuseMipLevel : 4.0f;
        float iblBrdfLutEnabled = pScProp ? pScProp->IBLBRDFLUTEnabled : 0.0f;
        baseCB.MaterialParams = XVECTOR3(sub_info->ClearcoatFactor, sub_info->ClearcoatRoughness, sub_info->Unlit ? 1.0f : 0.0f, emissiveMul);
        baseCB.MaterialParams2 = XVECTOR3(transmissionMul, refractionStrength, Textures[9] ? 1.0f : 0.0f, iblFactor);
        baseCB.MaterialParams3 = XVECTOR3(iblMipCount, iblBrdfLutEnabled, iblDiffuseMipLevel, 0.0f);
        baseCB.MaterialParams4 = XVECTOR3(sub_info->SheenColor.x, sub_info->SheenColor.y, sub_info->SheenColor.z, sub_info->SheenRoughness);
        baseCB.MaterialParams5 = XVECTOR3(sub_info->SheenColorTex ? 1.0f : 0.0f, sub_info->SheenRoughnessTex ? 1.0f : 0.0f, (float)sub_info->SheenColorTexCoord, (float)sub_info->SheenRoughnessTexCoord);
        baseCB.MaterialParams6 = XVECTOR3(sub_info->ClearcoatTex ? 1.0f : 0.0f, sub_info->ClearcoatRoughnessTex ? 1.0f : 0.0f, (float)sub_info->ClearcoatTexCoord, (float)sub_info->ClearcoatRoughnessTexCoord);
        baseCB.MaterialParams7 = XVECTOR3(sub_info->OcclusionTex ? 1.0f : 0.0f, sub_info->OcclusionStrength, (float)sub_info->OcclusionTexCoord, sub_info->TransmissionTex ? 1.0f : 0.0f);
        baseCB.MaterialParams8 = XVECTOR3((float)sub_info->TransmissionTexCoord, sub_info->SpecularFactorTex ? 1.0f : 0.0f, (float)sub_info->SpecularFactorTexCoord, sub_info->SpecularColorTex ? 1.0f : 0.0f);
        baseCB.MaterialParams9 = XVECTOR3((float)sub_info->SpecularColorTexCoord, 0.0f, 0.0f, 0.0f);
        baseCB.BaseColorUVTransform0 = sub_info->BaseColorUVTransform0;
        baseCB.BaseColorUVTransform1 = sub_info->BaseColorUVTransform1;
        baseCB.NormalUVTransform0 = sub_info->NormalUVTransform0;
        baseCB.NormalUVTransform1 = sub_info->NormalUVTransform1;
        baseCB.MetallicUVTransform0 = sub_info->MetallicUVTransform0;
        baseCB.MetallicUVTransform1 = sub_info->MetallicUVTransform1;
        baseCB.EmissiveUVTransform0 = sub_info->EmissiveUVTransform0;
        baseCB.EmissiveUVTransform1 = sub_info->EmissiveUVTransform1;
        baseCB.SheenColorUVTransform0 = sub_info->SheenColorUVTransform0;
        baseCB.SheenColorUVTransform1 = sub_info->SheenColorUVTransform1;
        baseCB.SheenRoughnessUVTransform0 = sub_info->SheenRoughnessUVTransform0;
        baseCB.SheenRoughnessUVTransform1 = sub_info->SheenRoughnessUVTransform1;
        baseCB.ClearcoatUVTransform0 = sub_info->ClearcoatUVTransform0;
        baseCB.ClearcoatUVTransform1 = sub_info->ClearcoatUVTransform1;
        baseCB.ClearcoatRoughnessUVTransform0 = sub_info->ClearcoatRoughnessUVTransform0;
        baseCB.ClearcoatRoughnessUVTransform1 = sub_info->ClearcoatRoughnessUVTransform1;
        baseCB.OcclusionUVTransform0 = sub_info->OcclusionUVTransform0;
        baseCB.OcclusionUVTransform1 = sub_info->OcclusionUVTransform1;
        baseCB.SpecularFactorUVTransform0 = sub_info->SpecularFactorUVTransform0;
        baseCB.SpecularFactorUVTransform1 = sub_info->SpecularFactorUVTransform1;
        baseCB.SpecularColorUVTransform0 = sub_info->SpecularColorUVTransform0;
        baseCB.SpecularColorUVTransform1 = sub_info->SpecularColorUVTransform1;
        baseCB.TransmissionUVTransform0 = sub_info->TransmissionUVTransform0;
        baseCB.TransmissionUVTransform1 = sub_info->TransmissionUVTransform1;

        sub_info->IB->Set(*T8DeviceContext, 0,
                          sub_info->IB32Bit ? IndexBufferFormat::R32
                                            : IndexBufferFormat::R16);

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

        BaseDriver::FaceCulling prevCull = g_pBaseDriver->m_FaceCulling;
        bool changedCull = sub_info->DoubleSided && prevCull != BaseDriver::FRONT_AND_BACK;
        if (changedCull) {
          g_pBaseDriver->SetCullFace(BaseDriver::FRONT_AND_BACK);
        }

        s->Set(*T8DeviceContext);
        it_MeshInfo->CB->UpdateFromBuffer(*T8DeviceContext, &baseCB.WVP[0]);
        it_MeshInfo->CB->Set(*T8DeviceContext);

        // Bind bone texture to VS slot 7
        if (m_boneTexture)
          m_boneTexture->SetVS(*T8DeviceContext, 7, "u_BoneTex");

        if (s->key.has(ShaderKey::DIFFUSE_MAP) && sub_info->DiffuseTex) {
          sub_info->DiffuseTex->Set(*T8DeviceContext, 0, "DiffuseTex");
          sub_info->DiffuseTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SPECULAR_MAP) && sub_info->SpecularTex) {
          sub_info->SpecularTex->Set(*T8DeviceContext, 1, "SpecularTex");
          sub_info->SpecularTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::GLOSS_MAP) && sub_info->GlossfTex) {
          sub_info->GlossfTex->Set(*T8DeviceContext, 2, "GlossTex");
          sub_info->GlossfTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::NORMAL_MAP) && sub_info->NormalTex) {
          sub_info->NormalTex->Set(*T8DeviceContext, 3, "NormalTex");
          sub_info->NormalTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (EnvMap) {
          EnvMap->Set(*T8DeviceContext, 4, "texEnv");
          EnvMap->SetSampler(*T8DeviceContext, ClampSamplerSlot);
        }
        if (s->key.has(ShaderKey::HEIGHT_MAP) && sub_info->ParalaxTex) {
          sub_info->ParalaxTex->Set(*T8DeviceContext, 5, "HeightTex");
          sub_info->ParalaxTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::METALLIC_MAP) && sub_info->MetallicTex) {
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
        if (s->key.has(ShaderKey::SHEEN_COLOR_MAP) && sub_info->SheenColorTex) {
          sub_info->SheenColorTex->Set(*T8DeviceContext, MaterialTextureSlot::SheenColor, "SheenColorTex");
          sub_info->SheenColorTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SHEEN_ROUGHNESS_MAP) && sub_info->SheenRoughnessTex) {
          sub_info->SheenRoughnessTex->Set(*T8DeviceContext, MaterialTextureSlot::SheenRoughness, "SheenRoughnessTex");
          sub_info->SheenRoughnessTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::CLEARCOAT_MAP) && sub_info->ClearcoatTex) {
          sub_info->ClearcoatTex->Set(*T8DeviceContext, MaterialTextureSlot::Clearcoat, "ClearcoatTex");
          sub_info->ClearcoatTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::CLEARCOAT_ROUGHNESS_MAP) && sub_info->ClearcoatRoughnessTex) {
          sub_info->ClearcoatRoughnessTex->Set(*T8DeviceContext, MaterialTextureSlot::ClearcoatRoughness, "ClearcoatRoughnessTex");
          sub_info->ClearcoatRoughnessTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::OCCLUSION_MAP) && sub_info->OcclusionTex) {
          sub_info->OcclusionTex->Set(*T8DeviceContext, MaterialTextureSlot::Occlusion, "OcclusionTex");
          sub_info->OcclusionTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SPECULAR_FACTOR_MAP) && sub_info->SpecularFactorTex) {
          sub_info->SpecularFactorTex->Set(*T8DeviceContext, MaterialTextureSlot::SpecularFactor, "SpecularFactorTex");
          sub_info->SpecularFactorTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SPECULAR_COLOR_MAP) && sub_info->SpecularColorTex) {
          sub_info->SpecularColorTex->Set(*T8DeviceContext, MaterialTextureSlot::SpecularColor, "SpecularColorTex");
          sub_info->SpecularColorTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::TRANSMISSION_MAP) && sub_info->TransmissionTex) {
          sub_info->TransmissionTex->Set(*T8DeviceContext, MaterialTextureSlot::Transmission, "TransmissionTex");
          sub_info->TransmissionTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }

        T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
        T8DeviceContext->DrawIndexed(sub_info->NumVertex, 0, 0);
        if (changedCull) {
          g_pBaseDriver->SetCullFace(prevCull);
        }
        m_drawnSubsets++;
      }
    }
  }

  void RenderSkinnedMesh::Destroy() {
    m_lineRenderer.Destroy();
    for (auto& wg : m_wireGeo) {
      if (wg.IB) { wg.IB->release(); wg.IB = nullptr; }
    }
    m_wireGeo.clear();
    m_wireShader = nullptr;
    if (m_skelVB) { m_skelVB->release(); m_skelVB = nullptr; }
    if (m_skelIB) { m_skelIB->release(); m_skelIB = nullptr; }
    m_skelPositions.clear();

    RenderMesh::Destroy();
    m_skinnedCBuffers.clear();
  }

} // namespace t850
