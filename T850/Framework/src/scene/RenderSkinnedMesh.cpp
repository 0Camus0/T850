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
#include <scene/RenderQueue.h>
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
  static constexpr unsigned EnvSamplerSlot = 4;
  static constexpr unsigned BoneTextureSlot = 24;

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

    void ExtractMeshInstanceCB(RenderMesh::MeshInstanceCBuffer& dst, const RenderMesh::CBuffer& src) {
      dst.WVP = src.WVP;
      dst.World = src.World;
      dst.WorldView = src.WorldView;
    }

    void ExtractMeshFrameCB(RenderMesh::MeshFrameCBuffer& dst, const RenderMesh::CBuffer& src) {
      dst.Light0Pos = src.Light0Pos;
      dst.Light0Col = src.Light0Col;
      dst.CameraPos = src.CameraPos;
      dst.CameraInfo = src.CameraInfo;
      dst.ParallaxSettings = src.ParallaxSettings;
      dst.ParallaxShadowSettings = src.ParallaxShadowSettings;
      dst.Light0Dir = src.Light0Dir;
      for (int li = 0; li < 128; li++) {
        dst.LightPositions[li] = src.LightPositions[li];
        dst.LightColors[li] = src.LightColors[li];
      }
      for (int ri = 0; ri < 32; ri++) {
        dst.LightRadius[ri] = src.LightRadius[ri];
      }
    }

    void ExtractMeshMaterialCB(RenderMesh::MeshMaterialCBuffer& dst, const RenderMesh::CBuffer& src) {
      dst.AmbientColor = src.AmbientColor;
      dst.DiffuseColor = src.DiffuseColor;
      dst.SpecularColor = src.SpecularColor;
      dst.PBRParams = src.PBRParams;
      dst.Intensities = src.Intensities;
      dst.EmissiveColor = src.EmissiveColor;
      dst.AlphaParams = src.AlphaParams;
      dst.ForwardParams = src.ForwardParams;
      dst.TexCoordSets = src.TexCoordSets;
      dst.MaterialParams = src.MaterialParams;
      dst.MaterialParams2 = src.MaterialParams2;
      dst.MaterialParams3 = src.MaterialParams3;
      dst.MaterialParams4 = src.MaterialParams4;
      dst.MaterialParams5 = src.MaterialParams5;
      dst.MaterialParams6 = src.MaterialParams6;
      dst.MaterialParams7 = src.MaterialParams7;
      dst.MaterialParams8 = src.MaterialParams8;
      dst.MaterialParams9 = src.MaterialParams9;
      dst.BaseColorUVTransform0 = src.BaseColorUVTransform0;
      dst.BaseColorUVTransform1 = src.BaseColorUVTransform1;
      dst.NormalUVTransform0 = src.NormalUVTransform0;
      dst.NormalUVTransform1 = src.NormalUVTransform1;
      dst.MetallicUVTransform0 = src.MetallicUVTransform0;
      dst.MetallicUVTransform1 = src.MetallicUVTransform1;
      dst.EmissiveUVTransform0 = src.EmissiveUVTransform0;
      dst.EmissiveUVTransform1 = src.EmissiveUVTransform1;
      dst.SheenColorUVTransform0 = src.SheenColorUVTransform0;
      dst.SheenColorUVTransform1 = src.SheenColorUVTransform1;
      dst.SheenRoughnessUVTransform0 = src.SheenRoughnessUVTransform0;
      dst.SheenRoughnessUVTransform1 = src.SheenRoughnessUVTransform1;
      dst.ClearcoatUVTransform0 = src.ClearcoatUVTransform0;
      dst.ClearcoatUVTransform1 = src.ClearcoatUVTransform1;
      dst.ClearcoatRoughnessUVTransform0 = src.ClearcoatRoughnessUVTransform0;
      dst.ClearcoatRoughnessUVTransform1 = src.ClearcoatRoughnessUVTransform1;
      dst.OcclusionUVTransform0 = src.OcclusionUVTransform0;
      dst.OcclusionUVTransform1 = src.OcclusionUVTransform1;
      dst.SpecularFactorUVTransform0 = src.SpecularFactorUVTransform0;
      dst.SpecularFactorUVTransform1 = src.SpecularFactorUVTransform1;
      dst.SpecularColorUVTransform0 = src.SpecularColorUVTransform0;
      dst.SpecularColorUVTransform1 = src.SpecularColorUVTransform1;
      dst.TransmissionUVTransform0 = src.TransmissionUVTransform0;
      dst.TransmissionUVTransform1 = src.TransmissionUVTransform1;
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
      char *vsSourceP = nullptr, *fsSourceP = nullptr;
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
      if (!vsSourceP || !fsSourceP) {
        T8_LOG_ERROR("[SkinnedMesh] Create skipped: failed loading shader source(s) %s, %s",
                     vsName.c_str(), fsName.c_str());
        free(vsSourceP);
        free(fsSourceP);
        return;
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
      char *vsWireP = nullptr, *fsWireP = nullptr;
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
      if (!vsWireP || !fsWireP) {
        T8_LOG_ERROR("[SkinnedMesh] Wireframe shader skipped: failed loading shader source(s) %s, %s",
                     vsWireName.c_str(), fsWireName.c_str());
        free(vsWireP);
        free(fsWireP);
      } else {
        std::string vsWStr(vsWireP), fsWStr(fsWireP);
        free(vsWireP); free(fsWireP);

        ShaderKey wireKey(0);
        wireKey.bits |= skinBit;
        if (!Info.empty() && !Info[0].SubSets.empty())
          wireKey.bits |= (Info[0].SubSets[0].key.bits & ShaderKey::VERTEX_ATTRIB_MASK);
        wireKey.setPass(32); // unused pass type — avoids collision with mesh shaders
        g_pBaseDriver->CreateShader(vsWStr, fsWStr, wireKey, vsWireName, fsWireName);
        m_wireShader = g_pBaseDriver->GetShader(wireKey);
      }
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
    RenderMesh::MeshInstanceCBuffer wireInstanceCB;
    ExtractMeshInstanceCB(wireInstanceCB, wireCB);

    for (std::size_t i = 0; i < Info.size() && i < m_wireGeo.size(); i++) {
      if (!m_wireGeo[i].IB || m_wireGeo[i].indexCount == 0) continue;

      MeshInfo* mi = &Info[i];
      VertexBuffer* vbToBind = mi->VB;
      unsigned int baseVertex = 0;
      if (mi->vbPoolAlloc.IsValid()) {
        if (VertexPool* vpool = MeshAssetCache::Get().GetVertexPool(mi->vbPoolAlloc.poolId)) {
          if (VertexBuffer* gpu = vpool->GetGPUBuffer()) {
            vbToBind = gpu;
            baseVertex = mi->vbPoolAlloc.offsetElems;
          }
        }
      }
      if (!vbToBind) {
        T8_LOG_ERROR("[SkinnedMesh] Wireframe skipped geometry %zu: no vertex buffer", i);
        continue;
      }
      vbToBind->Set(*T8DeviceContext, mi->VertexSize, 0);

      auto ibFmt = m_wireGeo[i].use32Bit ? IndexBufferFormat::R32 : IndexBufferFormat::R16;
      m_wireGeo[i].IB->Set(*T8DeviceContext, 0, ibFmt);

      T8DeviceContext->SetPrimitiveTopology(Topology::LINE_LIST);

      m_wireShader->Set(*T8DeviceContext);
      mi->CB->UpdateFromBuffer(*T8DeviceContext, &wireCB.WVP[0]);
      mi->CB->Set(*T8DeviceContext, 0);
      if (!g_pBaseDriver->UsesGLSL()) {
        mi->InstanceCBGPU->UpdateFromBuffer(*T8DeviceContext, &wireInstanceCB);
        mi->InstanceCBGPU->Set(*T8DeviceContext, 1);
      }

      // Bind bone texture to a slot that cannot alias mesh pixel textures.
      if (m_boneTexture)
        m_boneTexture->SetVS(*T8DeviceContext, BoneTextureSlot, "u_BoneTex");

      // Bind GBuffer depth texture for manual depth comparison in FS
      if (m_wireDepthTex)
        m_wireDepthTex->Set(*T8DeviceContext, 0, "depthTex");

      T8DeviceContext->DrawIndexed(m_wireGeo[i].indexCount, 0, baseVertex);

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
    if (!sDumped && !m_snapshotPoseActive) {
      m_animController.Update(0.0f);
      m_animController.DumpMatrices("anim_debug_bindpose.txt");
      sDumped = true;
    }

    // Update animation
    if (!m_snapshotPoseActive && m_playing && !m_animController.GetKeyframeMode()) {
      m_animController.SetUseSlerp(m_useSlerp);
      float deltaTime = pScProp ? pScProp->FrameDeltaSec : (1.0f / 60.0f);
      m_animController.Update(deltaTime);
    }

    // Upload bone matrices to texture
    int numBones = m_snapshotPoseActive
      ? static_cast<int>(m_snapshotBoneMatrices.size())
      : m_animController.GetNumBones();
    int count = (numBones < kMaxBones) ? numBones : kMaxBones;
    const XMATRIX44* bones = m_snapshotPoseActive
      ? m_snapshotBoneMatrices.data()
      : m_animController.GetBoneMatrices();
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

  void RenderSkinnedMesh::ExportBoneMatrices(std::vector<XMATRIX44>& out) const {
    out.clear();
    if (!m_hasSkin) return;
    int numBones = m_snapshotPoseActive
      ? static_cast<int>(m_snapshotBoneMatrices.size())
      : m_animController.GetNumBones();
    int count = (numBones < kMaxBones) ? numBones : kMaxBones;
    if (count <= 0) return;
    if (m_snapshotPoseActive) {
      out.assign(m_snapshotBoneMatrices.begin(), m_snapshotBoneMatrices.begin() + count);
      return;
    }
    const XMATRIX44* bones = m_animController.GetBoneMatrices();
    out.assign(bones, bones + count);
  }

  void RenderSkinnedMesh::ApplySnapshotBoneMatrices(const std::vector<XMATRIX44>& matrices) {
    m_snapshotBoneMatrices = matrices;
    if (static_cast<int>(m_snapshotBoneMatrices.size()) > kMaxBones)
      m_snapshotBoneMatrices.resize(kMaxBones);
    m_snapshotPoseActive = !m_snapshotBoneMatrices.empty();
  }

  void RenderSkinnedMesh::ClearSnapshotBoneMatrices() {
    m_snapshotPoseActive = false;
    m_snapshotBoneMatrices.clear();
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

    uint8_t currentPass = gKey.getPass();
    MeshDrawStateTracker& tracker = MeshDrawStateTracker::Get();
    const bool ownsScope = !tracker.InScope();
    if (ownsScope) tracker.Begin();

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
      ExtractMeshInstanceCB(it_MeshInfo->InstanceCB, baseCB);
      ExtractMeshFrameCB(it_MeshInfo->FrameCB, baseCB);

      unsigned int stride = it_MeshInfo->VertexSize;
      unsigned int offset = 0;

      ShaderBase *s = nullptr;

      // Phase A.5 step 3: bind shared VB pool. Falls back to legacy
      // per-asset VB if the asset wasn't pool-populated (shouldn't
      // happen post step 1 but kept defensive).
      VertexBuffer* vbToBind = it_MeshInfo->VB;
      if (it_MeshInfo->vbPoolAlloc.IsValid()) {
        if (VertexPool* vpool = MeshAssetCache::Get().GetVertexPool(it_MeshInfo->vbPoolAlloc.poolId)) {
          if (VertexBuffer* gpu = vpool->GetGPUBuffer()) {
            vbToBind = gpu;
          }
        }
      }
      if (!vbToBind) {
        T8_LOG_ERROR("[SkinnedMesh] Skipped geometry %zu: no uploaded vertex buffer", i);
        continue;
      }
      vbToBind->Set(*T8DeviceContext, stride, offset);

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

        // Bind-pose CPU AABBs are not conservative for GPU-skinned vertices.
        // Draw all skinned subsets until we have animation-aware bounds.

        // Phase B step 2: read material data via the deduplicated
        // MaterialAsset. Mirrors RenderMesh::Draw.
        const MaterialAsset* mat = sub_info->matAsset;
        const MaterialParams* mp = mat ? &mat->params : nullptr;
        if (mp) {
          FillCBufferFromMaterial(baseCB, *mp);
          baseCB.Intensities.w = (float)sub_info->MatID;
        } else {
          baseCB.AmbientColor = sub_info->AmbientColor;
          baseCB.DiffuseColor = sub_info->DiffuseColor;
          baseCB.SpecularColor = sub_info->SpecularColor;
          baseCB.PBRParams = sub_info->PBRParams;
          baseCB.Intensities = sub_info->Intensities;
          baseCB.Intensities.w = (float)sub_info->MatID;
          baseCB.EmissiveColor = sub_info->EmissiveColor;
          baseCB.AlphaParams = XVECTOR3((float)sub_info->AlphaMode, sub_info->AlphaCutoff, sub_info->DoubleSided ? 1.0f : 0.0f, sub_info->TransmissionFactor);
          baseCB.TexCoordSets = XVECTOR3((float)sub_info->DiffuseTexCoord, (float)sub_info->NormalTexCoord, (float)sub_info->MetallicTexCoord, (float)sub_info->EmissiveTexCoord);
        }
        baseCB.ForwardParams = XVECTOR3((float)g_pBaseDriver->width, (float)g_pBaseDriver->height, Textures[7] ? 1.0f : 0.0f, mp ? mp->ior : sub_info->IOR);
        float emissiveMul = pScProp ? pScProp->MaterialEmissiveIntensity : 1.0f;
        float transmissionMul = pScProp ? pScProp->MaterialTransmissionMultiplier : 1.0f;
        float refractionStrength = pScProp ? pScProp->MaterialRefractionStrength : 0.03f;
        float iblFactor = pScProp ? pScProp->IBLFactor : 1.0f;
        float iblMipCount = pScProp ? pScProp->IBLMipCount : 4.0f;
        float iblDiffuseMipLevel = pScProp ? pScProp->IBLDiffuseMipLevel : 4.0f;
        float iblBrdfLutEnabled = pScProp ? pScProp->IBLBRDFLUTEnabled : 0.0f;
        if (mp) {
          baseCB.MaterialParams  = XVECTOR3(mp->clearcoatFactor, mp->clearcoatRoughness, mp->unlit ? 1.0f : 0.0f, emissiveMul);
          baseCB.MaterialParams5 = XVECTOR3(mat->textures[(int)MatTexSlot::SheenColor]     ? 1.0f : 0.0f,
                                             mat->textures[(int)MatTexSlot::SheenRoughness] ? 1.0f : 0.0f,
                                             static_cast<float>(mp->sheenColorTexCoord),
                                             static_cast<float>(mp->sheenRoughTexCoord));
          baseCB.MaterialParams6 = XVECTOR3(mat->textures[(int)MatTexSlot::Clearcoat]          ? 1.0f : 0.0f,
                                             mat->textures[(int)MatTexSlot::ClearcoatRoughness] ? 1.0f : 0.0f,
                                             static_cast<float>(mp->clearcoatTexCoord),
                                             static_cast<float>(mp->clearcoatRoughTexCoord));
          baseCB.MaterialParams7 = XVECTOR3(mat->textures[(int)MatTexSlot::Occlusion] ? 1.0f : 0.0f,
                                             mp->occlusionStrength,
                                             static_cast<float>(mp->occlusionTexCoord),
                                             mat->textures[(int)MatTexSlot::Transmission] ? 1.0f : 0.0f);
          baseCB.MaterialParams8 = XVECTOR3(static_cast<float>(mp->transmissionTexCoord),
                                             mat->textures[(int)MatTexSlot::SpecularFactor] ? 1.0f : 0.0f,
                                             static_cast<float>(mp->specFactorTexCoord),
                                             mat->textures[(int)MatTexSlot::SpecularColor]  ? 1.0f : 0.0f);
        } else {
          baseCB.MaterialParams = XVECTOR3(sub_info->ClearcoatFactor, sub_info->ClearcoatRoughness, sub_info->Unlit ? 1.0f : 0.0f, emissiveMul);
          baseCB.MaterialParams5 = XVECTOR3(sub_info->SheenColorTex ? 1.0f : 0.0f, sub_info->SheenRoughnessTex ? 1.0f : 0.0f, (float)sub_info->SheenColorTexCoord, (float)sub_info->SheenRoughnessTexCoord);
          baseCB.MaterialParams6 = XVECTOR3(sub_info->ClearcoatTex ? 1.0f : 0.0f, sub_info->ClearcoatRoughnessTex ? 1.0f : 0.0f, (float)sub_info->ClearcoatTexCoord, (float)sub_info->ClearcoatRoughnessTexCoord);
          baseCB.MaterialParams7 = XVECTOR3(sub_info->OcclusionTex ? 1.0f : 0.0f, sub_info->OcclusionStrength, (float)sub_info->OcclusionTexCoord, sub_info->TransmissionTex ? 1.0f : 0.0f);
          baseCB.MaterialParams8 = XVECTOR3((float)sub_info->TransmissionTexCoord, sub_info->SpecularFactorTex ? 1.0f : 0.0f, (float)sub_info->SpecularFactorTexCoord, sub_info->SpecularColorTex ? 1.0f : 0.0f);
        }
        baseCB.MaterialParams2 = XVECTOR3(transmissionMul, refractionStrength, Textures[9] ? 1.0f : 0.0f, iblFactor);
        baseCB.MaterialParams3 = XVECTOR3(iblMipCount, iblBrdfLutEnabled, iblDiffuseMipLevel, 0.0f);
        ExtractMeshMaterialCB(sub_info->MaterialCB, baseCB);

        // Phase A.5 step 3: bind shared IB pool.
        IndexBuffer* ibToBind = sub_info->IB;
        if (sub_info->ibPoolAlloc.IsValid()) {
          if (IndexPool* ipool = MeshAssetCache::Get().GetIndexPool(sub_info->ibPoolAlloc.poolId)) {
            if (IndexBuffer* gpu = ipool->GetGPUBuffer()) {
              ibToBind = gpu;
            }
          }
        }
        if (!ibToBind) {
          T8_LOG_ERROR("[SkinnedMesh] Skipped subset %zu: no uploaded index buffer", k);
          continue;
        }
        ibToBind->Set(*T8DeviceContext, 0,
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
        tracker.OnShaderChanged(s);
        if (g_pBaseDriver->UsesGLSL()) {
          tracker.UpdateAndBindConstantBuffer(*T8DeviceContext, it_MeshInfo->CB, 0,
                                              &baseCB, sizeof(RenderMesh::CBuffer));
        } else {
          tracker.UpdateAndBindConstantBuffer(*T8DeviceContext, it_MeshInfo->FrameCBGPU, 0,
                                              &it_MeshInfo->FrameCB,
                                              sizeof(RenderMesh::MeshFrameCBuffer));
          tracker.UpdateAndBindConstantBuffer(*T8DeviceContext, it_MeshInfo->InstanceCBGPU, 1,
                                              &it_MeshInfo->InstanceCB,
                                              sizeof(RenderMesh::MeshInstanceCBuffer));
          tracker.UpdateAndBindConstantBuffer(*T8DeviceContext, it_MeshInfo->MaterialCBGPU, 2,
                                              &sub_info->MaterialCB,
                                              sizeof(RenderMesh::MeshMaterialCBuffer));
        }

        // Bind bone texture to a slot that cannot alias mesh pixel textures.
        if (m_boneTexture)
          m_boneTexture->SetVS(*T8DeviceContext, BoneTextureSlot, "u_BoneTex");

        if (s->key.has(ShaderKey::DIFFUSE_MAP) && sub_info->DiffuseTex) {
          sub_info->DiffuseTex->Set(*T8DeviceContext, 0, "DiffuseTex");
          sub_info->DiffuseTex->SetSampler(*T8DeviceContext, MaterialSamplerSlot);
        }
        if (s->key.has(ShaderKey::SPECULAR_MAP) && sub_info->SpecularTex) {
          sub_info->SpecularTex->Set(*T8DeviceContext, 1, "SpecularTex");
          sub_info->SpecularTex->SetSampler(*T8DeviceContext, 1);
        }
        if (s->key.has(ShaderKey::GLOSS_MAP) && sub_info->GlossfTex) {
          sub_info->GlossfTex->Set(*T8DeviceContext, 2, "GlossTex");
          sub_info->GlossfTex->SetSampler(*T8DeviceContext, 2);
        }
        if (s->key.has(ShaderKey::NORMAL_MAP) && sub_info->NormalTex) {
          sub_info->NormalTex->Set(*T8DeviceContext, 3, "NormalTex");
          sub_info->NormalTex->SetSampler(*T8DeviceContext, 3);
        }
        if (EnvMap) {
          EnvMap->Set(*T8DeviceContext, 4, "texEnv");
          EnvMap->SetSampler(*T8DeviceContext, EnvSamplerSlot);
        }
        if (s->key.has(ShaderKey::HEIGHT_MAP) && sub_info->ParalaxTex) {
          sub_info->ParalaxTex->Set(*T8DeviceContext, 5, "HeightTex");
          sub_info->ParalaxTex->SetSampler(*T8DeviceContext, 5);
        }
        if (s->key.has(ShaderKey::METALLIC_MAP) && sub_info->MetallicTex) {
          sub_info->MetallicTex->Set(*T8DeviceContext, 6, "MetallicTex");
          sub_info->MetallicTex->SetSampler(*T8DeviceContext, 6);
        }
        if (Textures[7]) {
          Textures[7]->Set(*T8DeviceContext, 7, "SceneDepthTex");
          Textures[7]->SetSampler(*T8DeviceContext, 7);
        }
        if (s->key.has(ShaderKey::EMISSIVE_MAP) && sub_info->EmissiveTex) {
          sub_info->EmissiveTex->Set(*T8DeviceContext, 8, "EmissiveTex");
          sub_info->EmissiveTex->SetSampler(*T8DeviceContext, 8);
        }
        if (Textures[9]) {
          Textures[9]->Set(*T8DeviceContext, 9, "SceneColorTex");
          Textures[9]->SetSampler(*T8DeviceContext, 9);
        }
        if (Textures[EnvironmentTextureSlot::DiffuseIBL]) {
          Textures[EnvironmentTextureSlot::DiffuseIBL]->Set(*T8DeviceContext, EnvironmentTextureSlot::DiffuseIBL, "texIBLDiffuse");
          Textures[EnvironmentTextureSlot::DiffuseIBL]->SetSampler(*T8DeviceContext, EnvironmentTextureSlot::DiffuseIBL);
        }
        if (Textures[EnvironmentTextureSlot::SpecularIBL]) {
          Textures[EnvironmentTextureSlot::SpecularIBL]->Set(*T8DeviceContext, EnvironmentTextureSlot::SpecularIBL, "texIBLSpecular");
          Textures[EnvironmentTextureSlot::SpecularIBL]->SetSampler(*T8DeviceContext, EnvironmentTextureSlot::SpecularIBL);
        }
        if (Textures[EnvironmentTextureSlot::BrdfLUT]) {
          Textures[EnvironmentTextureSlot::BrdfLUT]->Set(*T8DeviceContext, EnvironmentTextureSlot::BrdfLUT, "texIBLBRDF");
          Textures[EnvironmentTextureSlot::BrdfLUT]->SetSampler(*T8DeviceContext, EnvironmentTextureSlot::BrdfLUT);
        }
        if (Textures[EnvironmentTextureSlot::CharlieIBL]) {
          Textures[EnvironmentTextureSlot::CharlieIBL]->Set(*T8DeviceContext, EnvironmentTextureSlot::CharlieIBL, "texIBLCharlie");
          Textures[EnvironmentTextureSlot::CharlieIBL]->SetSampler(*T8DeviceContext, EnvironmentTextureSlot::CharlieIBL);
        }
        if (Textures[EnvironmentTextureSlot::CharlieLUT]) {
          Textures[EnvironmentTextureSlot::CharlieLUT]->Set(*T8DeviceContext, EnvironmentTextureSlot::CharlieLUT, "texIBLCharlieLUT");
          Textures[EnvironmentTextureSlot::CharlieLUT]->SetSampler(*T8DeviceContext, EnvironmentTextureSlot::CharlieLUT);
        }
        if (Textures[EnvironmentTextureSlot::SheenELUT]) {
          Textures[EnvironmentTextureSlot::SheenELUT]->Set(*T8DeviceContext, EnvironmentTextureSlot::SheenELUT, "texIBLSheenELUT");
          Textures[EnvironmentTextureSlot::SheenELUT]->SetSampler(*T8DeviceContext, EnvironmentTextureSlot::SheenELUT);
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
        // Phase A.5 step 3: pool offsets steer the draw to this
        // submesh's allocation.
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
      }
    }

    if (ownsScope) tracker.End();
  }

  void RenderSkinnedMesh::Destroy() {
    if (m_boneTexture) { m_boneTexture->release(); m_boneTexture = nullptr; }
    m_boneTexData.clear();
    m_boneTexWidth = 0;
    m_snapshotBoneMatrices.clear();
    m_snapshotPoseActive = false;

    m_lineRenderer.Destroy();
    for (auto& wg : m_wireGeo) {
      if (wg.IB) { wg.IB->release(); wg.IB = nullptr; }
    }
    m_wireGeo.clear();
    m_wireShader = nullptr;
    if (m_skelCB) { m_skelCB->release(); m_skelCB = nullptr; }
    if (m_skelVB) { m_skelVB->release(); m_skelVB = nullptr; }
    if (m_skelIB) { m_skelIB->release(); m_skelIB = nullptr; }
    m_skelShader = nullptr;
    m_skelIndexCount = 0;
    m_skelPositions.clear();
    m_wireDepthTex = nullptr;

    RenderMesh::Destroy();
    m_skinnedCBuffers.clear();
    m_skinnedQTBuffers.clear();
    m_hasSkin = false;
  }

} // namespace t850
