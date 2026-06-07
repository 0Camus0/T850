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
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <chrono>

#include <scene/RenderMesh.h>
#include <scene/RenderGraph.h>
#include <utils/ThreadPool.h>
#ifndef OS_ANDROID
#include <video/gl/GLShader.h>
#include <video/gl/GLDriver.h>
#endif

#if defined(OS_WINDOWS)
#include <video/d3d11/D3D11Shader.h>
#include <video/d3d11/D3D11Driver.h>
#endif
#include <core/Config.h>
#include <core/Core.h>
#include <core/EngineContext.h>
#include <utils/Log.h>
#include <debug/RuntimeTelemetry.h>

#define CHANGE_TO_RH 0
#define DEBUG_MODEL 0
extern t850::AppBase		  *pApp;
namespace t850 {
  static constexpr unsigned MaterialSamplerSlot = 0;
  static constexpr unsigned LightmapSamplerSlot = 7;
  static constexpr unsigned EnvSamplerSlot = 4;

  const EngineContext& RenderMesh::Context() const {
    return pEngineContext ? *pEngineContext : t850::GetEngineContext();
  }

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

    bool SphereIntersectsFrustum(const XVECTOR3 planes[6], const XVECTOR3& center, float radius) {
      for (int i = 0; i < 6; ++i) {
        const float distance = planes[i].x * center.x +
                               planes[i].y * center.y +
                               planes[i].z * center.z +
                               planes[i].w;
        if (distance < -radius)
          return false;
      }
      return true;
    }

    constexpr uint32_t kMinTrianglesForClustering = 256;
    constexpr uint32_t kTargetTrianglesPerCluster = 128;

    bool CanSplitSubsetIntoClusters(const RenderMesh::SubSetInfo& subInfo) {
      return subInfo.AlphaMode == 0 && subInfo.TransmissionFactor <= 0.0f;
    }

    template <typename IndexT>
    void BuildContiguousClustersForSubset(const IndexT* indices,
                                          uint32_t indexCount,
                                          const float* vertexData,
                                          uint32_t vertexStrideBytes,
                                          bool splitIntoChunks,
                                          uint32_t submeshIndex,
                                          std::vector<SubmeshCluster>& clusters) {
      if (!indices || indexCount == 0 || !vertexData || vertexStrideBytes < sizeof(float) * 3u)
        return;

      const uint32_t triangleCount = indexCount / 3u;
      if (triangleCount == 0)
        return;

      const uint32_t vertexStrideFloats = vertexStrideBytes / sizeof(float);
      const uint32_t targetTriangles = (splitIntoChunks && triangleCount > kMinTrianglesForClustering)
        ? kTargetTrianglesPerCluster
        : triangleCount;

      for (uint32_t firstTriangle = 0; firstTriangle < triangleCount; firstTriangle += targetTriangles) {
        const uint32_t chunkTriangles = (std::min)(targetTriangles, triangleCount - firstTriangle);
        SubmeshCluster cluster;
        cluster.submeshIndex = submeshIndex;
        cluster.indexOffset = firstTriangle * 3u;
        cluster.indexCount = chunkTriangles * 3u;

        for (uint32_t localIndex = 0; localIndex < cluster.indexCount; ++localIndex) {
          const uint32_t vertexIndex = static_cast<uint32_t>(indices[cluster.indexOffset + localIndex]);
          const float* vertex = vertexData + vertexIndex * vertexStrideFloats;
          cluster.localAABB.ExpandToInclude(vertex[0], vertex[1], vertex[2]);
        }

        if (cluster.localAABB.IsValid())
          clusters.push_back(cluster);
      }
    }

    const Submesh* FindSubmeshForSubset(const MeshAsset* asset, const RenderMesh::SubSetInfo& subInfo) {
      if (!asset || subInfo.meshAssetSubmeshIndex == UINT32_MAX)
        return nullptr;
      if (subInfo.meshAssetSubmeshIndex >= asset->submeshes.size())
        return nullptr;
      return &asset->submeshes[subInfo.meshAssetSubmeshIndex];
    }

    uint32_t ClusterCountForSubset(const MeshAsset* asset, const RenderMesh::SubSetInfo& subInfo) {
      const Submesh* submesh = FindSubmeshForSubset(asset, subInfo);
      return submesh ? submesh->clusterCount : 0u;
    }

    uint32_t DrawIndexCountForSubset(const RenderMesh::SubSetInfo& subInfo) {
      return subInfo.ibPoolAlloc.IsValid() ? subInfo.ibPoolAlloc.count : subInfo.NumVertex;
    }

    void ApplyCachedBounds(RenderMesh::SubSetInfo& subInfo, const AABB& bounds) {
      subInfo.bounds.min = XVECTOR3(bounds.vMin.x, bounds.vMin.y, bounds.vMin.z, 0.0f);
      subInfo.bounds.max = XVECTOR3(bounds.vMax.x, bounds.vMax.y, bounds.vMax.z, 0.0f);
    }

    void ExpandMeshBounds(RenderMesh::MeshInfo& meshInfo, const AABB& bounds) {
      meshInfo.bounds.Expand(bounds.vMin.x, bounds.vMin.y, bounds.vMin.z);
      meshInfo.bounds.Expand(bounds.vMax.x, bounds.vMax.y, bounds.vMax.z);
    }

    RenderMesh::AABB ComputeGeometryBounds(const xFinalGeometry& geometry, const xMeshGeometry& sourceGeometry) {
      RenderMesh::AABB bounds;
      bounds.Reset();
      const unsigned int stride = geometry.VertexSize / sizeof(float);
      if (stride < 3 || !geometry.pData)
        return bounds;
      for (unsigned int v = 0; v < sourceGeometry.NumVertices; ++v) {
        const float px = geometry.pData[v * stride + 0];
        const float py = geometry.pData[v * stride + 1];
        const float pz = geometry.pData[v * stride + 2];
        bounds.Expand(px, py, pz);
      }
      return bounds;
    }

    bool IsMaterialTiled(const xMaterial* material) {
      if (!material)
        return false;
      for (const xEffectDefault& effectDefault : material->EffectInstance.pDefaults) {
        if (effectDefault.Type == xF::xEFFECTENUM::STDX_DWORDS && effectDefault.NameParam == "Tiled")
          return effectDefault.CaseDWORD == 1;
      }
      return false;
    }

    uint64_t HashPreprocessValue(uint64_t hash, uint64_t value) {
      for (int i = 0; i < 8; ++i) {
        hash ^= static_cast<uint8_t>((value >> (i * 8)) & 0xFFu);
        hash *= 0x100000001b3ull;
      }
      return hash;
    }

    uint64_t BuildPreprocessTopologyHash(const XDataBase* xFile,
                                         const std::vector<RenderMesh::MeshInfo>& meshInfos,
                                         uint64_t vertexAttribMask,
                                         uint32_t vertexStride,
                                         uint32_t vertexCount,
                                         uint32_t indexCount) {
      uint64_t hash = 0xcbf29ce484222325ull;
      hash = HashPreprocessValue(hash, vertexAttribMask);
      hash = HashPreprocessValue(hash, vertexStride);
      hash = HashPreprocessValue(hash, vertexCount);
      hash = HashPreprocessValue(hash, indexCount);

      const xMeshContainer* meshContainer = xFile->XMeshDataBase[0];
      std::size_t submeshCount = 0;
      for (const RenderMesh::MeshInfo& meshInfo : meshInfos)
        submeshCount += meshInfo.SubSets.size();
      hash = HashPreprocessValue(hash, static_cast<uint64_t>(submeshCount));

      for (std::size_t i = 0; i < xFile->MeshInfo.size() && i < meshInfos.size(); ++i) {
        const xMeshGeometry& sourceGeometry = meshContainer->Geometry[i];
        const xFinalGeometry& finalGeometry = xFile->MeshInfo[i];
        const RenderMesh::MeshInfo& meshInfo = meshInfos[i];
        for (std::size_t j = 0; j < meshInfo.SubSets.size() && j < finalGeometry.Subsets.size(); ++j) {
          const xSubsetInfo& sourceSubset = finalGeometry.Subsets[j];
          const RenderMesh::SubSetInfo& renderSubset = meshInfo.SubSets[j];
          hash = HashPreprocessValue(hash, sourceSubset.NumVertex);
          hash = HashPreprocessValue(hash, sourceSubset.NumTris);
          hash = HashPreprocessValue(hash, sourceGeometry.Indices32Bit ? 1u : 0u);
          hash = HashPreprocessValue(hash, renderSubset.key.bits & ShaderKey::VERTEX_ATTRIB_MASK);
        }
      }
      return hash;
    }

    void ApplyCachedCullingMetadata(std::vector<RenderMesh::MeshInfo>& meshInfos,
                                    MeshAsset* asset,
                                    const MeshPreprocessCacheData& cache) {
      if (!asset)
        return;
      asset->clusters = cache.clusters;
      asset->rootAABB = cache.rootAABB;

      std::size_t flatSubmesh = 0;
      for (std::size_t i = 0; i < meshInfos.size(); ++i) {
        RenderMesh::MeshInfo& meshInfo = meshInfos[i];
        meshInfo.bounds.Reset();
        for (std::size_t j = 0; j < meshInfo.SubSets.size(); ++j, ++flatSubmesh) {
          if (flatSubmesh >= cache.submeshes.size() || flatSubmesh >= asset->submeshes.size())
            return;
          const Submesh& cachedSubmesh = cache.submeshes[flatSubmesh];
          Submesh& assetSubmesh = asset->submeshes[flatSubmesh];
          assetSubmesh.localAABB = cachedSubmesh.localAABB;
          assetSubmesh.firstCluster = cachedSubmesh.firstCluster;
          assetSubmesh.clusterCount = cachedSubmesh.clusterCount;
          ApplyCachedBounds(meshInfo.SubSets[j], cachedSubmesh.localAABB);
          ExpandMeshBounds(meshInfo, cachedSubmesh.localAABB);
        }
      }
      asset->cullingMetadataReady = true;
    }

    bool ValidatePreprocessCacheTopology(const XDataBase* xFile,
                                         const std::vector<RenderMesh::MeshInfo>& meshInfos,
                                         const MeshPreprocessCacheData& cache) {
      if (!xFile || xFile->XMeshDataBase.empty() || cache.submeshes.empty())
        return false;

      const xMeshContainer* meshContainer = xFile->XMeshDataBase[0];
      std::size_t flatSubmesh = 0;
      std::size_t totalVerts = 0;
      std::size_t totalIndices = 0;
      uint32_t maxVertexStride = 0;
      uint64_t vertexAttribMask = 0;

      if (meshInfos.size() != xFile->MeshInfo.size() || meshContainer->Geometry.size() < xFile->MeshInfo.size())
        return false;

      for (std::size_t i = 0; i < xFile->MeshInfo.size(); ++i) {
        const xFinalGeometry& finalGeometry = xFile->MeshInfo[i];
        const xMeshGeometry& sourceGeometry = meshContainer->Geometry[i];
        const RenderMesh::MeshInfo& meshInfo = meshInfos[i];
        if (meshInfo.SubSets.size() != finalGeometry.Subsets.size())
          return false;

        totalVerts += sourceGeometry.NumVertices;
        totalIndices += static_cast<std::size_t>(sourceGeometry.NumTriangles) * 3u;
        if (finalGeometry.VertexSize > maxVertexStride)
          maxVertexStride = finalGeometry.VertexSize;

        for (std::size_t j = 0; j < meshInfo.SubSets.size(); ++j, ++flatSubmesh) {
          if (flatSubmesh >= cache.submeshes.size())
            return false;

          const xSubsetInfo& sourceSubset = finalGeometry.Subsets[j];
          const RenderMesh::SubSetInfo& renderSubset = meshInfo.SubSets[j];
          const Submesh& cachedSubmesh = cache.submeshes[flatSubmesh];
          const uint64_t cachedAttribs = cachedSubmesh.vertexAttribKey.bits & ShaderKey::VERTEX_ATTRIB_MASK;
          const uint64_t renderAttribs = renderSubset.key.bits & ShaderKey::VERTEX_ATTRIB_MASK;

          if (cachedSubmesh.vertexCount != sourceSubset.NumVertex
              || cachedSubmesh.triangleCount != sourceSubset.NumTris
              || cachedSubmesh.ib32Bit != sourceGeometry.Indices32Bit
              || cachedAttribs != renderAttribs) {
            return false;
          }

          vertexAttribMask |= renderAttribs;
        }
      }

      const bool topologyMatches = flatSubmesh == cache.submeshes.size()
        && cache.vertexCount == static_cast<uint32_t>(totalVerts)
        && cache.indexCount == static_cast<uint32_t>(totalIndices)
        && cache.vertexStride == maxVertexStride
        && cache.vertexAttribMask == vertexAttribMask;
      if (!topologyMatches)
        return false;

      const uint64_t currentTopologyHash = BuildPreprocessTopologyHash(xFile,
                                                                       meshInfos,
                                                                       vertexAttribMask,
                                                                       maxVertexStride,
                                                                       static_cast<uint32_t>(totalVerts),
                                                                       static_cast<uint32_t>(totalIndices));
      if (cache.topologyHash != 0 && cache.topologyHash != currentTopologyHash) {
        T8_LOG_INFO("[MeshAssetCache] Mesh preprocess cache topology hash mismatch for '%s' (cache=0x%016llX current=0x%016llX)",
                    xFile->m_name.c_str(),
                    static_cast<unsigned long long>(cache.topologyHash),
                    static_cast<unsigned long long>(currentTopologyHash));
        return false;
      }
      return true;
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
    if (!xFile) {
      m_asset = nullptr;
      T8_LOG_ERROR("[RenderMesh] Load failed for '%s'", m_sourcePath.c_str());
      return;
    }

    bool created = false;
    m_asset = MeshAssetCache::Get().Acquire(m_sourcePath, &created);
    T8_LOG_INFO("[MeshAssetCache] %s '%s' (refs=%u, total assets=%zu)",
                created ? "MISS — first acquisition" : "HIT  — reused",
                m_sourcePath.c_str(),
                m_asset ? m_asset->refCount : 0u,
                MeshAssetCache::Get().Size());
  }

  void RenderMesh::Create() {
    const EngineContext& context = Context();
    Device* device = context.device;
    const Config* config = context.config;
    if (!device) {
      T8_LOG_ERROR("[RenderMesh] Create skipped for '%s': no device in engine context", m_sourcePath.c_str());
      return;
    }
    if (!xFile || xFile->MeshInfo.empty() || xFile->XMeshDataBase.empty() || !xFile->XMeshDataBase[0]) {
      T8_LOG_ERROR("[RenderMesh] Create skipped for '%s': mesh data is not loaded", m_sourcePath.c_str());
      return;
    }

    GatherInfo();
    if (Info.empty()) {
      T8_LOG_ERROR("[RenderMesh] Create skipped for '%s': no drawable geometry was gathered", m_sourcePath.c_str());
      return;
    }
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
    const MeshPreprocessCacheSettings preprocessCacheSettings = {
      kMinTrianglesForClustering,
      kTargetTrianglesPerCluster,
      CHANGE_TO_RH ? 1u : 0u
    };
    const bool buildCullingMetadataOnLoad = config && config->cullingLoadMode == Config::CullingLoadMode::FullOnLoad;
    MeshPreprocessCacheData preprocessCache;
    bool usePreprocessCache = false;
    if (populatePools && buildCullingMetadataOnLoad) {
      usePreprocessCache = MeshAssetCache::Get().LoadPreprocessCache(m_sourcePath, preprocessCacheSettings, preprocessCache);
      if (usePreprocessCache && !ValidatePreprocessCacheTopology(xFile, Info, preprocessCache)) {
        T8_LOG_INFO("[MeshAssetCache] Ignoring mesh preprocess cache for '%s': topology changed",
                    m_sourcePath.c_str());
        preprocessCache = MeshPreprocessCacheData{};
        usePreprocessCache = false;
      }
    }

    struct VBAllocSide { uint32_t poolId = UINT32_MAX; uint32_t offsetVerts = 0; uint32_t count = 0; };
    struct IBAllocSide { uint32_t poolId = UINT32_MAX; uint32_t offsetIdx   = 0; uint32_t count = 0; };
    std::vector<VBAllocSide>             poolVBAllocs(xFile->MeshInfo.size());
    std::vector<std::vector<IBAllocSide>> poolIBAllocs(xFile->MeshInfo.size());
    std::vector<std::vector<std::vector<SubmeshCluster>>> clusterBuildData(xFile->MeshInfo.size());
    uint32_t nextMeshAssetSubmeshIndex = 0;

    for (std::size_t i = 0; i < xFile->MeshInfo.size(); i++) {
      xFinalGeometry *it = &xFile->MeshInfo[i];
      xMeshGeometry *pActual = &xFile->XMeshDataBase[0]->Geometry[i];
      MeshInfo  *it_MeshInfo = &Info[i];
      const RenderMesh::AABB geometryBounds = ComputeGeometryBounds(*it, *pActual);
      if (populatePools) {
        poolIBAllocs[i].resize(it_MeshInfo->SubSets.size());
        clusterBuildData[i].resize(it_MeshInfo->SubSets.size());
      }

      t850::BufferDesc bdesc;
      bdesc.byteWidth = sizeof(RenderMesh::CBuffer);
      bdesc.usage = BufferUsage::DEFAULT;
      it_MeshInfo->CB = (t850::ConstantBuffer*)device->CreateBuffer(BufferType::CONSTANT, bdesc);
      bdesc.byteWidth = sizeof(RenderMesh::MeshFrameCBuffer);
      it_MeshInfo->FrameCBGPU = (t850::ConstantBuffer*)device->CreateBuffer(BufferType::CONSTANT, bdesc);
      bdesc.byteWidth = sizeof(RenderMesh::MeshInstanceCBuffer);
      it_MeshInfo->InstanceCBGPU = (t850::ConstantBuffer*)device->CreateBuffer(BufferType::CONSTANT, bdesc);
      bdesc.byteWidth = sizeof(RenderMesh::MeshMaterialCBuffer);
      it_MeshInfo->MaterialCBGPU = (t850::ConstantBuffer*)device->CreateBuffer(BufferType::CONSTANT, bdesc);

      int NumMaterials = static_cast<int>(pActual->MaterialList.Materials.size());
      int NumFaceIndices = static_cast<int>(pActual->MaterialList.FaceIndices.size());
      const bool kUse32 = pActual->Indices32Bit;
      const uint32_t geometryFirstSubmeshIndex = nextMeshAssetSubmeshIndex;
      std::vector<unsigned short> indexScratch16;
      std::vector<unsigned int> indexScratch32;

      for (int j = 0; j < NumMaterials; j++) {
        xSubsetInfo *subinfo = &it->Subsets[j];
        xMaterial *material = &pActual->MaterialList.Materials[j];
        SubSetInfo *it_subsetinfo = &it_MeshInfo->SubSets[j];
        const bool materialTiled = IsMaterialTiled(material);

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

        if (mDef->NameParam == "lightmapIntensity") {
          it_subsetinfo->LightmapIntensity = mDef->CaseFloat[0];
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
        assignUVTransform("lightmapUVTransform0", it_subsetinfo->LightmapUVTransform0);
        assignUVTransform("lightmapUVTransform1", it_subsetinfo->LightmapUVTransform1);

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
            T8_LOG_DEBUG("[%s]", mDef->NameParam.c_str());
#endif
            auto loadTextureWithTiling = [&](const char* key, int& id, Texture** tex, bool tiled) -> bool {
              if (mDef->NameParam != key)
                return false;
              std::string path = RemovePath(mDef->CaseString);
#if DEBUG_MODEL
              T8_LOG_DEBUG("path[%s]", path.c_str());
#endif
              id = LoadTex(path, tex, tiled);
              return true;
            };
            auto loadTexture = [&](const char* key, int& id, Texture** tex) -> bool {
              return loadTextureWithTiling(key, id, tex, materialTiled);
            };

            loadTexture("diffuseMap", it_subsetinfo->DiffuseId, &it_subsetinfo->DiffuseTex)
              || loadTexture("specularMap", it_subsetinfo->SpecularId, &it_subsetinfo->SpecularTex)
              || loadTexture("glossMap", it_subsetinfo->GlossfId, &it_subsetinfo->GlossfTex)
              || loadTexture("normalMap", it_subsetinfo->NormalId, &it_subsetinfo->NormalTex)
              || loadTexture("heightMap", it_subsetinfo->ParalaxId, &it_subsetinfo->ParalaxTex)
              || loadTexture("metallicMap", it_subsetinfo->MetallicId, &it_subsetinfo->MetallicTex)
              || loadTexture("emissiveMap", it_subsetinfo->EmissiveId, &it_subsetinfo->EmissiveTex)
              || loadTexture("sheenColorMap", it_subsetinfo->SheenColorId, &it_subsetinfo->SheenColorTex)
              || loadTexture("sheenRoughnessMap", it_subsetinfo->SheenRoughnessId, &it_subsetinfo->SheenRoughnessTex)
              || loadTexture("clearcoatMap", it_subsetinfo->ClearcoatId, &it_subsetinfo->ClearcoatTex)
              || loadTexture("clearcoatRoughnessMap", it_subsetinfo->ClearcoatRoughnessId, &it_subsetinfo->ClearcoatRoughnessTex)
              || loadTexture("occlusionMap", it_subsetinfo->OcclusionId, &it_subsetinfo->OcclusionTex)
              || loadTexture("specularFactorMap", it_subsetinfo->SpecularFactorId, &it_subsetinfo->SpecularFactorTex)
              || loadTexture("specularColorMap", it_subsetinfo->SpecularColorId, &it_subsetinfo->SpecularColorTex)
              || loadTexture("transmissionMap", it_subsetinfo->TransmissionId, &it_subsetinfo->TransmissionTex)
              || loadTextureWithTiling("lightmapMap", it_subsetinfo->LightmapId, &it_subsetinfo->LightmapTex, false);
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
            if (mDef->NameParam == "lightmapTexCoord") {
              it_subsetinfo->LightmapTexCoord = mDef->CaseDWORD;
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
          proto.textures[(int)MatTexSlot::Lightmap]           = it_subsetinfo->LightmapTex;         proto.textureIds[(int)MatTexSlot::Lightmap]           = it_subsetinfo->LightmapId;
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
          p.lightmapIntensity  = it_subsetinfo->LightmapIntensity;
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
          copy4(p.lightmapUV0,       it_subsetinfo->LightmapUVTransform0);      copy4(p.lightmapUV1,       it_subsetinfo->LightmapUVTransform1);
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
          p.lightmapTexCoord   = (uint8_t)it_subsetinfo->LightmapTexCoord;
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
        const uint32_t meshAssetSubmeshIndex = nextMeshAssetSubmeshIndex++;
        const bool splitIntoClusters = CanSplitSubsetIntoClusters(*it_subsetinfo);
        const Submesh* cachedSubmesh = (usePreprocessCache && meshAssetSubmeshIndex < preprocessCache.submeshes.size())
          ? &preprocessCache.submeshes[meshAssetSubmeshIndex]
          : nullptr;
        // Allocate temp index storage matching the source width. Both
        // branches build the same {first vertex of each face} order,
        // mirroring the legacy 16-bit path. For glTF >65 535-vertex
        // primitives the loader sets `Indices32Bit` and populates
        // `Triangles32`; the legacy `.x` loader keeps the 16-bit path.
        if (!kUse32) {
          indexScratch16.resize(static_cast<std::size_t>(it_subsetinfo->NumTris) * 3u);
          unsigned short *tmpIndexex = indexScratch16.data();
          int counter = 0;
          bool first = false;
          for (int k = 0; k < NumFaceIndices; k++) {
            if (pActual->MaterialList.FaceIndices[k] == static_cast<unsigned long>(j)) {
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

          if (cachedSubmesh) {
            ApplyCachedBounds(*it_subsetinfo, cachedSubmesh->localAABB);
          } else if (buildCullingMetadataOnLoad) {
            it_subsetinfo->bounds.Reset();
            unsigned int stride16 = it->VertexSize / sizeof(float);
            for (int vi = 0; vi < counter; vi++) {
              unsigned int idx = tmpIndexex[vi];
              it_subsetinfo->bounds.Expand(it->pData[idx*stride16], it->pData[idx*stride16+1], it->pData[idx*stride16+2]);
            }
          } else {
            it_subsetinfo->bounds = geometryBounds;
          }

          if (populatePools && buildCullingMetadataOnLoad && !usePreprocessCache) {
            BuildContiguousClustersForSubset(tmpIndexex,
                                             static_cast<uint32_t>(counter),
                                             &it->pData[0],
                                             it->VertexSize,
                                             splitIntoClusters,
                                             meshAssetSubmeshIndex,
                                             clusterBuildData[i][j]);
          }

        } else {
          indexScratch32.resize(static_cast<std::size_t>(it_subsetinfo->NumTris) * 3u);
          unsigned int *tmpIndexex = indexScratch32.data();
          int counter = 0;
          bool first = false;
          for (int k = 0; k < NumFaceIndices; k++) {
            if (pActual->MaterialList.FaceIndices[k] == static_cast<unsigned long>(j)) {
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

          if (cachedSubmesh) {
            ApplyCachedBounds(*it_subsetinfo, cachedSubmesh->localAABB);
          } else if (buildCullingMetadataOnLoad) {
            it_subsetinfo->bounds.Reset();
            unsigned int stride32 = it->VertexSize / sizeof(float);
            for (int vi = 0; vi < counter; vi++) {
              unsigned int idx = tmpIndexex[vi];
              it_subsetinfo->bounds.Expand(it->pData[idx*stride32], it->pData[idx*stride32+1], it->pData[idx*stride32+2]);
            }
          } else {
            it_subsetinfo->bounds = geometryBounds;
          }

          if (populatePools && buildCullingMetadataOnLoad && !usePreprocessCache) {
            BuildContiguousClustersForSubset(tmpIndexex,
                                             static_cast<uint32_t>(counter),
                                             &it->pData[0],
                                             it->VertexSize,
                                             splitIntoClusters,
                                             meshAssetSubmeshIndex,
                                             clusterBuildData[i][j]);
          }
        }
      }

      it_MeshInfo->VertexSize = it->VertexSize;
      it_MeshInfo->NumVertex = pActual->NumVertices;

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
        poolVBAllocs[i] = { poolId, off, static_cast<uint32_t>(pActual->NumVertices) };
      }

      it_MeshInfo->bounds.Reset();
      if (usePreprocessCache) {
        for (uint32_t submeshIndex = geometryFirstSubmeshIndex; submeshIndex < nextMeshAssetSubmeshIndex; ++submeshIndex) {
          if (submeshIndex < preprocessCache.submeshes.size())
            ExpandMeshBounds(*it_MeshInfo, preprocessCache.submeshes[submeshIndex].localAABB);
        }
      } else {
        it_MeshInfo->bounds = geometryBounds;
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
      if (usePreprocessCache)
        m_asset->clusters = preprocessCache.clusters;
      else
        m_asset->clusters.clear();

      std::size_t totalVerts = 0;
      std::size_t totalIdx   = 0;
      for (std::size_t i = 0; i < Info.size(); ++i) {
        const MeshInfo& mi = Info[i];
        if (mi.VertexSize > m_asset->vertexStride) m_asset->vertexStride = mi.VertexSize;
        totalVerts += mi.NumVertex;
        for (std::size_t j = 0; j < mi.SubSets.size(); ++j) {
          const SubSetInfo& s = mi.SubSets[j];
          const uint32_t submeshIndex = static_cast<uint32_t>(m_asset->submeshes.size());
          Submesh sub;
          if (usePreprocessCache && submeshIndex < preprocessCache.submeshes.size()) {
            sub = preprocessCache.submeshes[submeshIndex];
          } else {
            sub.vertexStart   = s.VertexStart;
            sub.vertexCount   = s.NumVertex;
            sub.indexStart    = s.TriStart * 3u;
            sub.triangleCount = s.NumTris;
            sub.materialSlot  = submeshIndex;
            sub.ib32Bit       = s.IB32Bit;
            sub.localAABB.vMin = XVECTOR3(s.bounds.min.x, s.bounds.min.y, s.bounds.min.z, 0.0f);
            sub.localAABB.vMax = XVECTOR3(s.bounds.max.x, s.bounds.max.y, s.bounds.max.z, 0.0f);
            sub.vertexAttribKey.bits = s.key.bits & ShaderKey::VERTEX_ATTRIB_MASK;
          }
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
          if (!usePreprocessCache && buildCullingMetadataOnLoad) {
            sub.firstCluster = static_cast<uint32_t>(m_asset->clusters.size());
            if (i < clusterBuildData.size() && j < clusterBuildData[i].size()) {
              for (SubmeshCluster cluster : clusterBuildData[i][j]) {
                cluster.submeshIndex = submeshIndex;
                m_asset->clusters.push_back(cluster);
              }
            }
            if (m_asset->clusters.size() == sub.firstCluster) {
              const uint32_t indexCount = sub.ibAlloc.IsValid() ? sub.ibAlloc.count : s.NumTris * 3u;
              if (indexCount > 0) {
                SubmeshCluster cluster;
                cluster.submeshIndex = submeshIndex;
                cluster.indexOffset = 0;
                cluster.indexCount = indexCount;
                cluster.localAABB = sub.localAABB;
                m_asset->clusters.push_back(cluster);
              }
            }
            sub.clusterCount = static_cast<uint32_t>(m_asset->clusters.size()) - sub.firstCluster;
          } else if (!usePreprocessCache) {
            sub.firstCluster = 0;
            sub.clusterCount = 0;
          }
          m_asset->submeshes.push_back(sub);
        }
        m_asset->rootAABB.ExpandToInclude(mi.bounds.min.x, mi.bounds.min.y, mi.bounds.min.z);
        m_asset->rootAABB.ExpandToInclude(mi.bounds.max.x, mi.bounds.max.y, mi.bounds.max.z);
      }
      m_asset->vertexCount = static_cast<uint32_t>(totalVerts);
      m_asset->indexCount  = static_cast<uint32_t>(totalIdx);

      T8_LOG_INFO("[MeshAssetCache] Populated '%s': %zu submesh(es), %zu cluster(s), %u verts, %u indices, stride=%u, attribMask=0x%016llX",
                  m_asset->sourcePath.c_str(),
                  m_asset->submeshes.size(),
                  m_asset->clusters.size(),
                  m_asset->vertexCount, m_asset->indexCount, m_asset->vertexStride,
                  static_cast<unsigned long long>(m_asset->vertexAttribMask));
      MeshAssetCache::Get().DumpToLog();
      MaterialAssetCache::Get().DumpToLog();
      m_asset->cullingMetadataReady = buildCullingMetadataOnLoad || usePreprocessCache;
      if (buildCullingMetadataOnLoad && !usePreprocessCache)
        MeshAssetCache::Get().SavePreprocessCache(m_sourcePath, preprocessCacheSettings, *m_asset);
    } else if (m_asset) {
      T8_LOG_INFO("[MeshAssetCache] Reusing populated '%s' (%zu submesh(es), refs=%u)",
                  m_asset->sourcePath.c_str(), m_asset->submeshes.size(), m_asset->refCount);
    }

    if (populatePools && m_asset)
      MeshAssetCache::Get().UploadDirtyPools();
    m_cullingMetadataReady = m_asset ? m_asset->cullingMetadataReady : false;

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
          mi.SubSets[j].meshAssetSubmeshIndex = static_cast<uint32_t>(flatIdx);
          mi.SubSets[j].ibPoolAlloc = m_asset->submeshes[flatIdx].ibAlloc;
        }
      }
    }

    CreateWireframeShader();
    BuildWireframeBuffers();
  }

  bool RenderMesh::ApplyCullingPreprocessCache(const MeshPreprocessCacheData& cache) {
    if (!m_asset || cache.submeshes.empty())
      return false;
    ApplyCachedCullingMetadata(Info, m_asset, cache);
    m_cullingMetadataReady = m_asset->cullingMetadataReady;
    return m_cullingMetadataReady;
  }

  bool RenderMesh::BuildCullingMetadata() {
    if (!xFile || xFile->XMeshDataBase.empty() || !m_asset || m_asset->submeshes.empty())
      return false;

    std::vector<SubmeshCluster> rebuiltClusters;
    t850::AABB rebuiltRoot;
    rebuiltRoot = t850::AABB{};

    std::size_t flatSubmesh = 0;
    for (std::size_t i = 0; i < xFile->MeshInfo.size() && i < Info.size(); ++i) {
      xFinalGeometry* finalGeometry = &xFile->MeshInfo[i];
      xMeshGeometry* sourceGeometry = &xFile->XMeshDataBase[0]->Geometry[i];
      MeshInfo& meshInfo = Info[i];
      meshInfo.bounds.Reset();

      for (std::size_t j = 0; j < meshInfo.SubSets.size(); ++j, ++flatSubmesh) {
        if (flatSubmesh >= m_asset->submeshes.size())
          return false;

        SubSetInfo& subset = meshInfo.SubSets[j];
        Submesh& submesh = m_asset->submeshes[flatSubmesh];
        subset.bounds.Reset();
        submesh.firstCluster = static_cast<uint32_t>(rebuiltClusters.size());

        const bool splitIntoClusters = CanSplitSubsetIntoClusters(subset);
        const int numFaceIndices = sourceGeometry->NumTriangles;
        if (!sourceGeometry->Indices32Bit) {
          std::vector<unsigned short> indices;
          indices.reserve(subset.NumVertex);
          for (int k = 0; k < numFaceIndices; ++k) {
            if (sourceGeometry->MaterialList.FaceIndices[k] != static_cast<unsigned long>(j))
              continue;
            const unsigned int index = k * 3u;
#if CHANGE_TO_RH
            indices.push_back(sourceGeometry->Triangles[index + 2]);
            indices.push_back(sourceGeometry->Triangles[index + 1]);
            indices.push_back(sourceGeometry->Triangles[index + 0]);
#else
            indices.push_back(sourceGeometry->Triangles[index + 0]);
            indices.push_back(sourceGeometry->Triangles[index + 1]);
            indices.push_back(sourceGeometry->Triangles[index + 2]);
#endif
          }

          const unsigned int stride = finalGeometry->VertexSize / sizeof(float);
          for (unsigned short vertexIndex : indices) {
            subset.bounds.Expand(finalGeometry->pData[vertexIndex * stride + 0],
                                 finalGeometry->pData[vertexIndex * stride + 1],
                                 finalGeometry->pData[vertexIndex * stride + 2]);
          }

          BuildContiguousClustersForSubset(indices.data(),
                                           static_cast<uint32_t>(indices.size()),
                                           finalGeometry->pData,
                                           finalGeometry->VertexSize,
                                           splitIntoClusters,
                                           static_cast<uint32_t>(flatSubmesh),
                                           rebuiltClusters);
        } else {
          std::vector<unsigned int> indices;
          indices.reserve(subset.NumVertex);
          for (int k = 0; k < numFaceIndices; ++k) {
            if (sourceGeometry->MaterialList.FaceIndices[k] != static_cast<unsigned long>(j))
              continue;
            const unsigned int index = k * 3u;
#if CHANGE_TO_RH
            indices.push_back(sourceGeometry->Triangles32[index + 2]);
            indices.push_back(sourceGeometry->Triangles32[index + 1]);
            indices.push_back(sourceGeometry->Triangles32[index + 0]);
#else
            indices.push_back(sourceGeometry->Triangles32[index + 0]);
            indices.push_back(sourceGeometry->Triangles32[index + 1]);
            indices.push_back(sourceGeometry->Triangles32[index + 2]);
#endif
          }

          const unsigned int stride = finalGeometry->VertexSize / sizeof(float);
          for (unsigned int vertexIndex : indices) {
            subset.bounds.Expand(finalGeometry->pData[vertexIndex * stride + 0],
                                 finalGeometry->pData[vertexIndex * stride + 1],
                                 finalGeometry->pData[vertexIndex * stride + 2]);
          }

          BuildContiguousClustersForSubset(indices.data(),
                                           static_cast<uint32_t>(indices.size()),
                                           finalGeometry->pData,
                                           finalGeometry->VertexSize,
                                           splitIntoClusters,
                                           static_cast<uint32_t>(flatSubmesh),
                                           rebuiltClusters);
        }

        if (rebuiltClusters.size() == submesh.firstCluster) {
          const uint32_t indexCount = submesh.ibAlloc.IsValid() ? submesh.ibAlloc.count : subset.NumTris * 3u;
          if (indexCount > 0) {
            SubmeshCluster cluster;
            cluster.submeshIndex = static_cast<uint32_t>(flatSubmesh);
            cluster.indexOffset = 0;
            cluster.indexCount = indexCount;
            cluster.localAABB.vMin = XVECTOR3(subset.bounds.min.x, subset.bounds.min.y, subset.bounds.min.z, 0.0f);
            cluster.localAABB.vMax = XVECTOR3(subset.bounds.max.x, subset.bounds.max.y, subset.bounds.max.z, 0.0f);
            rebuiltClusters.push_back(cluster);
          }
        }

        submesh.localAABB.vMin = XVECTOR3(subset.bounds.min.x, subset.bounds.min.y, subset.bounds.min.z, 0.0f);
        submesh.localAABB.vMax = XVECTOR3(subset.bounds.max.x, subset.bounds.max.y, subset.bounds.max.z, 0.0f);
        submesh.clusterCount = static_cast<uint32_t>(rebuiltClusters.size()) - submesh.firstCluster;
        ExpandMeshBounds(meshInfo, submesh.localAABB);
      }

      rebuiltRoot.ExpandToInclude(meshInfo.bounds.min.x, meshInfo.bounds.min.y, meshInfo.bounds.min.z);
      rebuiltRoot.ExpandToInclude(meshInfo.bounds.max.x, meshInfo.bounds.max.y, meshInfo.bounds.max.z);
    }

    m_asset->clusters = std::move(rebuiltClusters);
    m_asset->rootAABB = rebuiltRoot;
    m_asset->cullingMetadataReady = true;
    m_cullingMetadataReady = true;
    return true;
  }

  bool RenderMesh::EnsureCullingMetadata() {
    const Config* config = Context().config;
    if (config && config->cullingLoadMode == Config::CullingLoadMode::Disabled)
      return false;
    if (m_cullingMetadataReady)
      return true;
    if (!m_asset)
      return false;

    const MeshPreprocessCacheSettings preprocessCacheSettings = {
      kMinTrianglesForClustering,
      kTargetTrianglesPerCluster,
      CHANGE_TO_RH ? 1u : 0u
    };

    MeshPreprocessCacheData preprocessCache;
    if (MeshAssetCache::Get().LoadPreprocessCache(m_sourcePath, preprocessCacheSettings, preprocessCache)) {
      if (ValidatePreprocessCacheTopology(xFile, Info, preprocessCache) && ApplyCullingPreprocessCache(preprocessCache)) {
        T8_LOG_INFO("[CULLING] Loaded culling metadata for '%s' on demand", m_sourcePath.c_str());
        return true;
      }
      T8_LOG_INFO("[CULLING] Ignoring on-demand culling cache for '%s': topology changed", m_sourcePath.c_str());
    }

    if (!BuildCullingMetadata())
      return false;

    MeshAssetCache::Get().SavePreprocessCache(m_sourcePath, preprocessCacheSettings, *m_asset);
    T8_LOG_INFO("[CULLING] Built culling metadata for '%s' on demand (%zu cluster(s))",
                m_sourcePath.c_str(), m_asset->clusters.size());
    return true;
  }

  void RenderMesh::CreateWireframeShader() {
    if (!Context().driver) return;
    m_lineRenderer.Create();
  }

  void RenderMesh::BuildWireframeBuffers() {
    if (!xFile || xFile->XMeshDataBase.empty()) return;
    xF::xMeshContainer* mc = xFile->XMeshDataBase[0];

    m_wireGeo.resize(mc->Geometry.size());

    for (std::size_t gi = 0; gi < mc->Geometry.size(); gi++) {
      auto& geom = mc->Geometry[gi];
      std::vector<unsigned int> lineIdx;
      auto includeMaterial = [&](int materialIndex) {
        if (gi >= Info.size() || materialIndex < 0 || materialIndex >= (int)Info[gi].SubSets.size())
          return true;
        const SubSetInfo& subset = Info[gi].SubSets[materialIndex];
        if (const MaterialAsset* mat = subset.matAsset) {
          const MaterialParams& params = mat->params;
          return params.alphaMode != 2 && params.transmissionFactor <= 0.0f;
        }
        return subset.AlphaMode != 2 && subset.TransmissionFactor <= 0.0f;
      };

      auto pushTriangleLines = [&](unsigned int a, unsigned int b, unsigned int c) {
        lineIdx.push_back(a); lineIdx.push_back(b);
        lineIdx.push_back(b); lineIdx.push_back(c);
        lineIdx.push_back(c); lineIdx.push_back(a);
      };

      if (geom.Indices32Bit && !geom.Triangles32.empty()) {
        const auto& tris = geom.Triangles32;
        for (std::size_t t = 0, face = 0; t + 2 < tris.size(); t += 3, ++face) {
          int materialIndex = (face < geom.MaterialList.FaceIndices.size()) ? geom.MaterialList.FaceIndices[face] : 0;
          if (!includeMaterial(materialIndex)) continue;
          pushTriangleLines(tris[t + 0], tris[t + 1], tris[t + 2]);
        }
      } else {
        const auto& tris = geom.Triangles;
        for (std::size_t t = 0, face = 0; t + 2 < tris.size(); t += 3, ++face) {
          int materialIndex = (face < geom.MaterialList.FaceIndices.size()) ? geom.MaterialList.FaceIndices[face] : 0;
          if (!includeMaterial(materialIndex)) continue;
          pushTriangleLines(tris[t + 0], tris[t + 1], tris[t + 2]);
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

  void RenderMesh::DrawWireframe() {
    if (!m_lineRenderer.IsReady() || m_wireGeo.empty()) return;
    if (!pScProp || !pScProp->GetPrimaryCamera()) return;

    Camera* cam = pScProp->GetPrimaryCamera();
    XVECTOR3 wireColor(0.0f, 1.0f, 0.0f, 1.0f);
    m_lineRenderer.SetDepthTestEnabled(m_wireDepthTex != nullptr || m_wireDepthTex2 != nullptr);
    m_lineRenderer.SetDepthTexture(m_wireDepthTex);
    m_lineRenderer.SetSecondaryDepthTexture(m_wireDepthTex2);
    m_lineRenderer.SetViewport(m_wireViewW, m_wireViewH);
    m_lineRenderer.SetFarPlane(cam->FPlane);

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
        T8_LOG_ERROR("[RenderMesh] Wireframe skipped geometry %zu: no vertex buffer", i);
        continue;
      }

      auto ibFmt = m_wireGeo[i].use32Bit ? IndexBufferFormat::R32 : IndexBufferFormat::R16;
      m_lineRenderer.DrawLines(transform, cam->VP, wireColor, vbToBind, m_wireGeo[i].IB,
                               m_wireGeo[i].indexCount, mi->VertexSize, ibFmt, baseVertex);
    }
  }

  void RenderMesh::GatherInfo() {
    BaseDriver* driver = Context().driver;
    if (!driver) {
      T8_LOG_ERROR("[RenderMesh] GatherInfo skipped for '%s': no driver in engine context", m_sourcePath.c_str());
      return;
    }

    if (!xFile || xFile->MeshInfo.empty() || xFile->XMeshDataBase.empty() || !xFile->XMeshDataBase[0]) {
      T8_LOG_ERROR("[RenderMesh] GatherInfo skipped for '%s': mesh data is not loaded", m_sourcePath.c_str());
      return;
    }

    char *vsSourceP = nullptr;
    char *fsSourceP = nullptr;
    std::string vsName, fsName;
    if (driver->UsesGLSL()) {
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

    if (!vsSourceP || !fsSourceP) {
      T8_LOG_ERROR("[RenderMesh] GatherInfo skipped for '%s': failed loading shader source(s) %s, %s",
                   m_sourcePath.c_str(), vsName.c_str(), fsName.c_str());
      free(vsSourceP);
      free(fsSourceP);
      return;
    }

    std::string vstr(vsSourceP);
    std::string fstr(fsSourceP);

    free(vsSourceP);
    free(fsSourceP);

    for (std::size_t i = 0; i < xFile->MeshInfo.size(); i++) {
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
            if (mDef->NameParam == "lightmapMap")
              matKey.bits |= ShaderKey::LIGHTMAP_MAP;
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
        driver->CreateShader(vstr, fstr, matKey, vsName, fsName);

        static const uint8_t passes[] = {
          PassType::FORWARD, PassType::GBUFFER,
          PassType::SHADOW_MAP, PassType::RADIAL_DEPTH
        };
        for (uint8_t pass : passes) {
          ShaderKey k(matKey.bits);
          k.setPass(pass);
          driver->CreateShader(vstr, fstr, k, vsName, fsName);
          if (hasHeight && (pass == PassType::GBUFFER || pass == PassType::FORWARD)) {
            ShaderKey kp(k.bits);
            kp.bits |= ShaderKey::PARALLAX;
            driver->CreateShader(vstr, fstr, kp, vsName, fsName);
          }
        }

        tmp.SubSets.push_back(stmp);
      }

      Info.push_back(tmp);
    }
  }

  int	 RenderMesh::LoadTex(const std::string& p, Texture** tex, bool tiled) {
    BaseDriver* driver = Context().driver;
    if (!driver) {
      T8_LOG_ERROR("Texture [%s] not loaded: no driver in engine context", p.c_str());
      *tex = nullptr;
      return -1;
    }

    int id = driver->CreateTexture(p);
    *tex = driver->GetTexture(id);

    unsigned int params = TextBasicParams::MIPMAPS;

    if (tiled)
      params |= TextBasicParams::TILED;
    else
      params |= TextBasicParams::CLAMP_TO_EDGE;

    (*tex)->params = params;
    (*tex)->SetTextureParams();

    if (id != -1) {
#if DEBUG_MODEL
      T8_LOG_DEBUG("Texture loaded index %d", id);
#endif
    }
    else {
      T8_LOG_ERROR("Texture [%s] not found", p.c_str());
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

  static RenderMesh::FrustumResult ClassifyAABBFrustumBounds(float minX, float minY, float minZ,
                                                             float maxX, float maxY, float maxZ,
                                                             const XMATRIX44& world,
                                                             const XVECTOR3 planes[6]) {
    const float cx = (minX + maxX) * 0.5f;
    const float cy = (minY + maxY) * 0.5f;
    const float cz = (minZ + maxZ) * 0.5f;
    const float ex = (maxX - minX) * 0.5f;
    const float ey = (maxY - minY) * 0.5f;
    const float ez = (maxZ - minZ) * 0.5f;

    const float wcx = cx*world.m11 + cy*world.m21 + cz*world.m31 + world.m41;
    const float wcy = cx*world.m12 + cy*world.m22 + cz*world.m32 + world.m42;
    const float wcz = cx*world.m13 + cy*world.m23 + cz*world.m33 + world.m43;

    const float wex = std::fabs(world.m11)*ex + std::fabs(world.m21)*ey + std::fabs(world.m31)*ez;
    const float wey = std::fabs(world.m12)*ex + std::fabs(world.m22)*ey + std::fabs(world.m32)*ez;
    const float wez = std::fabs(world.m13)*ex + std::fabs(world.m23)*ey + std::fabs(world.m33)*ez;

    bool intersects = false;
    for (int p = 0; p < 6; p++) {
      const float dist = planes[p].x*wcx + planes[p].y*wcy + planes[p].z*wcz + planes[p].w;
      const float radius = std::fabs(planes[p].x)*wex + std::fabs(planes[p].y)*wey + std::fabs(planes[p].z)*wez;
      if (dist + radius < 0.0f)
        return RenderMesh::FrustumResult::Outside;
      if (dist - radius < 0.0f)
        intersects = true;
    }
    return intersects ? RenderMesh::FrustumResult::Intersecting : RenderMesh::FrustumResult::Inside;
  }

  static RenderMesh::FrustumResult ClassifyCanonicalAABBFrustum(const t850::AABB& box, const XMATRIX44& world, const XVECTOR3 planes[6]) {
    return ClassifyAABBFrustumBounds(box.vMin.x, box.vMin.y, box.vMin.z,
                                     box.vMax.x, box.vMax.y, box.vMax.z,
                                     world, planes);
  }

  // Test AABB (in local space) transformed by world matrix against frustum planes.
  RenderMesh::FrustumResult RenderMesh::ClassifyAABBFrustum(const AABB& box, const XMATRIX44& world, const XVECTOR3 planes[6]) {
    return ClassifyAABBFrustumBounds(box.min.x, box.min.y, box.min.z,
                                     box.max.x, box.max.y, box.max.z,
                                     world, planes);
  }

  // Returns true if AABB is at least partially inside the frustum.
  bool RenderMesh::AABBInsideFrustum(const AABB& box, const XMATRIX44& world, const XVECTOR3 planes[6]) {
    return ClassifyAABBFrustum(box, world, planes) != FrustumResult::Outside;
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

  static float SubsetViewDepth(const RenderMesh::SubSetInfo& subInfo, const XMATRIX44& world, const XVECTOR3& eye, const XVECTOR3& look) {
    float lx = (subInfo.bounds.min.x + subInfo.bounds.max.x) * 0.5f;
    float ly = (subInfo.bounds.min.y + subInfo.bounds.max.y) * 0.5f;
    float lz = (subInfo.bounds.min.z + subInfo.bounds.max.z) * 0.5f;
    float wx = lx*world.m11 + ly*world.m21 + lz*world.m31 + world.m41;
    float wy = lx*world.m12 + ly*world.m22 + lz*world.m32 + world.m42;
    float wz = lx*world.m13 + ly*world.m23 + lz*world.m33 + world.m43;
    float dx = wx - eye.x;
    float dy = wy - eye.y;
    float dz = wz - eye.z;
    return dx*look.x + dy*look.y + dz*look.z;
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

  static float GeometryForwardDepth(const RenderMesh::MeshInfo& meshInfo, const XMATRIX44& world, const XVECTOR3& eye, const XVECTOR3& look) {
    float depth = -FLT_MAX;
    for (const auto& subInfo : meshInfo.SubSets) {
      if (IsForwardOnlySubset(subInfo)) {
        float subsetDepth = SubsetViewDepth(subInfo, world, eye, look);
        if (subsetDepth > depth)
          depth = subsetDepth;
      }
    }
    return depth;
  }

  static void ApplyMeshInstanceCB(RenderMesh::CBuffer& dst, const RenderMesh::MeshInstanceCBuffer& src) {
    dst.WVP = src.WVP;
    dst.World = src.World;
    dst.WorldView = src.WorldView;
  }

  static void ApplyMeshFrameCB(RenderMesh::CBuffer& dst, const RenderMesh::MeshFrameCBuffer& src) {
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

  static void ApplyMeshMaterialCB(RenderMesh::CBuffer& dst, const RenderMesh::MeshMaterialCBuffer& src) {
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
    dst.LightmapUVTransform0 = src.LightmapUVTransform0;
    dst.LightmapUVTransform1 = src.LightmapUVTransform1;
  }

  static void FillMaterialCBFromSubset(RenderMesh::MeshMaterialCBuffer& cb, const RenderMesh::SubSetInfo& subInfo) {
    cb.AmbientColor = subInfo.AmbientColor;
    cb.DiffuseColor = subInfo.DiffuseColor;
    cb.SpecularColor = subInfo.SpecularColor;
    cb.PBRParams = subInfo.PBRParams;
    cb.Intensities = subInfo.Intensities;
    cb.Intensities.w = (float)subInfo.MatID;
    cb.EmissiveColor = subInfo.EmissiveColor;
    cb.AlphaParams = XVECTOR3((float)subInfo.AlphaMode, subInfo.AlphaCutoff, subInfo.DoubleSided ? 1.0f : 0.0f, subInfo.TransmissionFactor);
    cb.TexCoordSets = XVECTOR3((float)subInfo.DiffuseTexCoord, (float)subInfo.NormalTexCoord, (float)subInfo.MetallicTexCoord, (float)subInfo.EmissiveTexCoord);
    cb.MaterialParams4 = XVECTOR3(subInfo.SheenColor.x, subInfo.SheenColor.y, subInfo.SheenColor.z, subInfo.SheenRoughness);
    cb.MaterialParams9 = XVECTOR3((float)subInfo.SpecularColorTexCoord, subInfo.NormalScale, (float)subInfo.LightmapTexCoord, subInfo.LightmapIntensity);
    cb.BaseColorUVTransform0 = subInfo.BaseColorUVTransform0;
    cb.BaseColorUVTransform1 = subInfo.BaseColorUVTransform1;
    cb.NormalUVTransform0 = subInfo.NormalUVTransform0;
    cb.NormalUVTransform1 = subInfo.NormalUVTransform1;
    cb.MetallicUVTransform0 = subInfo.MetallicUVTransform0;
    cb.MetallicUVTransform1 = subInfo.MetallicUVTransform1;
    cb.EmissiveUVTransform0 = subInfo.EmissiveUVTransform0;
    cb.EmissiveUVTransform1 = subInfo.EmissiveUVTransform1;
    cb.SheenColorUVTransform0 = subInfo.SheenColorUVTransform0;
    cb.SheenColorUVTransform1 = subInfo.SheenColorUVTransform1;
    cb.SheenRoughnessUVTransform0 = subInfo.SheenRoughnessUVTransform0;
    cb.SheenRoughnessUVTransform1 = subInfo.SheenRoughnessUVTransform1;
    cb.ClearcoatUVTransform0 = subInfo.ClearcoatUVTransform0;
    cb.ClearcoatUVTransform1 = subInfo.ClearcoatUVTransform1;
    cb.ClearcoatRoughnessUVTransform0 = subInfo.ClearcoatRoughnessUVTransform0;
    cb.ClearcoatRoughnessUVTransform1 = subInfo.ClearcoatRoughnessUVTransform1;
    cb.OcclusionUVTransform0 = subInfo.OcclusionUVTransform0;
    cb.OcclusionUVTransform1 = subInfo.OcclusionUVTransform1;
    cb.SpecularFactorUVTransform0 = subInfo.SpecularFactorUVTransform0;
    cb.SpecularFactorUVTransform1 = subInfo.SpecularFactorUVTransform1;
    cb.SpecularColorUVTransform0 = subInfo.SpecularColorUVTransform0;
    cb.SpecularColorUVTransform1 = subInfo.SpecularColorUVTransform1;
    cb.TransmissionUVTransform0 = subInfo.TransmissionUVTransform0;
    cb.TransmissionUVTransform1 = subInfo.TransmissionUVTransform1;
    cb.LightmapUVTransform0 = subInfo.LightmapUVTransform0;
    cb.LightmapUVTransform1 = subInfo.LightmapUVTransform1;
  }

  void RenderMesh::Draw(float *t, float *vp) {
    T8_TELEMETRY_SCOPE("render.mesh.draw");
    if (t)
      transform = t;

    const EngineContext& context = Context();
    BaseDriver* driver = context.driver;
    DeviceContext* deviceContext = context.deviceContext;
    ThreadPool* threadPool = context.threadPool;
    const Config* config = context.config;
    if (!driver || !deviceContext)
      return;

    uint8_t currentPass = gKey.getPass();
    Camera* pRenderCamera = pScProp ? pScProp->GetPrimaryCamera() : nullptr;
    if (!pRenderCamera)
      return;
    Camera* pCullCamera = pRenderCamera;
    if ((currentPass == PassType::GBUFFER || currentPass == PassType::FORWARD) && pScProp->pCullingCamera)
      pCullCamera = pScProp->pCullingCamera;
    const bool frustumCullingEnabled = !pScProp || pScProp->FrustumCullingEnabled;

    // Extract frustum planes once per draw call
    XVECTOR3 frustumPlanes[6];
    ExtractFrustumPlanes(pCullCamera->VP, frustumPlanes);

    std::size_t numGeometries = xFile->MeshInfo.size();
    const bool trackCullStats = currentPass == PassType::GBUFFER;
    const bool timeCullStats = trackCullStats && config && config->flags.benchmark;
    using CullingClock = std::chrono::steady_clock;
    long long cullingCpuNs = 0;
    if (trackCullStats) {
      m_totalMeshes = static_cast<int>(numGeometries);
      m_visibleMeshes = 0;
      m_culledMeshes = 0;
      m_totalSubsets = 0;
      m_visibleSubsets = 0;
      m_culledSubsets = 0;
      m_drawnSubsets = 0;
      m_totalClusters = 0;
      m_visibleClusters = 0;
      m_culledClusters = 0;
      m_drawnClusters = 0;
      m_totalIndices = 0;
      m_drawnIndices = 0;
      m_culledIndices = 0;
      m_cullingMeshTests = 0;
      m_cullingSubsetTests = 0;
      m_cullingClusterTests = 0;
      m_drawCalls = 0;
      m_renderStateChanges = 0;
      m_cullingCpuMs = 0.0;
    }

    auto timeCullWork = [&](auto&& work) {
      if (!timeCullStats)
        return work();
      const auto start = CullingClock::now();
      auto result = work();
      cullingCpuNs += std::chrono::duration_cast<std::chrono::nanoseconds>(CullingClock::now() - start).count();
      return result;
    };

    // Build visibility mask — parallel for large meshes, serial for small
    std::vector<uint8_t>& visible = m_visibilityScratch;
    visible.resize(numGeometries);
    constexpr uint8_t frustumOutside = static_cast<uint8_t>(FrustumResult::Outside);
    constexpr uint8_t frustumInside = static_cast<uint8_t>(FrustumResult::Inside);
    static constexpr int kParallelCullThreshold = 256;

    if (!frustumCullingEnabled) {
      std::fill(visible.begin(), visible.end(), frustumInside);
    } else if (static_cast<int>(numGeometries) >= kParallelCullThreshold && threadPool) {
      XMATRIX44 worldCopy = transform;
      if (trackCullStats)
        m_cullingMeshTests += static_cast<unsigned long long>(numGeometries);
      timeCullWork([&]() -> bool {
        T8_TELEMETRY_SCOPE("render.mesh.culling");
        threadPool->ParallelFor(0, static_cast<int>(numGeometries), [&](int i) {
          visible[i] = static_cast<uint8_t>(ClassifyAABBFrustum(Info[i].bounds, worldCopy, frustumPlanes));
        });
        return true;
      });
    } else {
      if (trackCullStats)
        m_cullingMeshTests += static_cast<unsigned long long>(numGeometries);
      timeCullWork([&]() -> bool {
        T8_TELEMETRY_SCOPE("render.mesh.culling");
        for (std::size_t i = 0; i < numGeometries; i++) {
          visible[i] = static_cast<uint8_t>(ClassifyAABBFrustum(Info[i].bounds, transform, frustumPlanes));
        }
        return true;
      });
    }

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
        if (trackCullStats) m_renderStateChanges++;
        t->Set(*deviceContext, slot, name);
      }
      // Sampler set every time (cheap); the per-shader sampler slot
      // map is consulted inside SetSampler.
      t->SetSampler(*deviceContext, samplerSlot);
    };

    std::vector<std::size_t>& geometryOrder = m_geometryOrderScratch;
    geometryOrder.resize(numGeometries);
    for (std::size_t i = 0; i < numGeometries; i++) geometryOrder[i] = i;
    if (currentPass == PassType::FORWARD) {
      T8_TELEMETRY_SCOPE("render.mesh.geometry_sort");
      std::stable_sort(geometryOrder.begin(), geometryOrder.end(),
        [&](std::size_t a, std::size_t b) {
          int groupA = GeometryForwardGroup(Info[a]);
          int groupB = GeometryForwardGroup(Info[b]);
          if (groupA != groupB)
            return groupA < groupB;
          float da = GeometryForwardDepth(Info[a], transform, pRenderCamera->Eye, pRenderCamera->Look);
          float db = GeometryForwardDepth(Info[b], transform, pRenderCamera->Eye, pRenderCamera->Look);
          return da > db;
        });
      } else if (currentPass == PassType::GBUFFER || currentPass == PassType::SHADOW_MAP || currentPass == PassType::RADIAL_DEPTH) {
        T8_TELEMETRY_SCOPE("render.mesh.geometry_sort");
        std::stable_sort(geometryOrder.begin(), geometryOrder.end(),
          [&](std::size_t a, std::size_t b) {
            return GeometryNonForwardGroup(Info[a], currentPass) < GeometryNonForwardGroup(Info[b], currentPass);
          });
    }

    for (std::size_t oi = 0; oi < numGeometries; oi++) {
      std::size_t i = geometryOrder[oi];
      MeshInfo  *it_MeshInfo = &Info[i];
      int drawableSubsetCount = 0;
      int drawableClusterCount = 0;
      unsigned long long drawableIndexCount = 0;
      for (const SubSetInfo& subInfo : it_MeshInfo->SubSets) {
        if (!ShouldDrawSubsetInPass(subInfo, currentPass))
          continue;
        drawableSubsetCount++;
        if (trackCullStats) {
          drawableClusterCount += static_cast<int>(ClusterCountForSubset(m_asset, subInfo));
          drawableIndexCount += DrawIndexCountForSubset(subInfo);
        }
      }
      if (trackCullStats) {
        m_totalSubsets += drawableSubsetCount;
        m_totalClusters += drawableClusterCount;
        m_totalIndices += drawableIndexCount;
      }

      const uint8_t meshFrustumResult = visible[i];
      if (meshFrustumResult == frustumOutside) {
        if (trackCullStats) {
          m_culledMeshes++;
          m_culledSubsets += drawableSubsetCount;
          m_culledClusters += drawableClusterCount;
        }
        continue;
      }
      if (trackCullStats)
        m_visibleMeshes++;

      XMATRIX44 VP = pRenderCamera->VP;
      XMATRIX44 WVP = transform*VP;
      XMATRIX44 WorldView = transform*pRenderCamera->View;
      XVECTOR3 infoCam = XVECTOR3(pRenderCamera->NPlane, pRenderCamera->FPlane, pRenderCamera->Fov, 1.0f);

      RenderMesh::MeshInstanceCBuffer& instanceCB = it_MeshInfo->InstanceCB;
      instanceCB.WVP = WVP;
      instanceCB.World = transform;
      instanceCB.WorldView = WorldView;

      RenderMesh::MeshFrameCBuffer& frameCB = it_MeshInfo->FrameCB;
      frameCB.Light0Pos = pScProp->Lights[0].Position;
      frameCB.Light0Col = pScProp->Lights[0].Color;
      frameCB.CameraPos = pRenderCamera->Eye;
      unsigned int numLights = pScProp ? static_cast<unsigned int>(pScProp->ActiveLights) : 1u;
      if (pScProp && numLights > pScProp->Lights.size())
        numLights = static_cast<unsigned int>(pScProp->Lights.size());
      if (numLights > 128u) numLights = 128u;

      for (int li = 0; li < 128; li++) {
        frameCB.LightPositions[li] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
        frameCB.LightColors[li] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      }
      for (int ri = 0; ri < 32; ri++) {
        frameCB.LightRadius[ri] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      }

      XVECTOR3 lightFrustumPlanes[6];
      RenderMesh::ExtractFrustumPlanes(pRenderCamera->VP, lightFrustumPlanes);
      unsigned int packedLights = 0;
      {
        T8_TELEMETRY_SCOPE("render.mesh.light_pack");
        for (unsigned int li = 0; li < numLights; li++) {
          Light& light = pScProp->Lights[li];
          if (!light.Enabled)
            continue;
          if (light.Type == LIGHT_POINT && !pScProp->PointLightsEnabled)
            continue;

          const float effectiveRadius = light.Type == LIGHT_POINT ? light.radius * (std::max)(0.0f, pScProp->LightRadiusScale) : light.radius;
          const float effectiveIntensity = light.Intensity * (std::max)(0.0f, pScProp->LightIntensityScale);
          if (effectiveIntensity <= 0.0f)
            continue;

          const unsigned int packedIndex = packedLights++;
          if (light.Type == LIGHT_DIRECTIONAL) {
            frameCB.LightPositions[packedIndex] = XVECTOR3(light.Direction.x, light.Direction.y, light.Direction.z, 0.0f);
          } else {
            const float shaderRange = effectiveRadius * 2.0f;
            if (shaderRange <= 0.0f || !SphereIntersectsFrustum(lightFrustumPlanes, light.Position, shaderRange)) {
              --packedLights;
              continue;
            }
            frameCB.LightPositions[packedIndex] = XVECTOR3(light.Position.x, light.Position.y, light.Position.z, 1.0f);
          }
          frameCB.LightColors[packedIndex] = XVECTOR3(light.Color.x, light.Color.y, light.Color.z, effectiveIntensity);
          XVECTOR3& radiusPack = frameCB.LightRadius[packedIndex >> 2];
          if ((packedIndex & 3u) == 0u) radiusPack.x = effectiveRadius;
          else if ((packedIndex & 3u) == 1u) radiusPack.y = effectiveRadius;
          else if ((packedIndex & 3u) == 2u) radiusPack.z = effectiveRadius;
          else radiusPack.w = effectiveRadius;
        }
      }
      RuntimeTelemetry::AddCounter("render.mesh.packedLights", static_cast<double>(packedLights));
      infoCam.w = static_cast<float>(packedLights);
      frameCB.CameraInfo = infoCam;
      frameCB.ParallaxSettings = XVECTOR3(m_fParallaxLowSamples, m_fParallaxHighSamples, m_fParallaxHeight);
      frameCB.ParallaxSettings.w = m_fParallaxEnabled;
      frameCB.ParallaxShadowSettings = XVECTOR3(m_fParallaxShadowMinLayers, m_fParallaxShadowMaxLayers, m_fParallaxShadowSoftness);
      frameCB.ParallaxShadowSettings.w = m_fParallaxShadowStrength;
      frameCB.Light0Dir = pScProp->Lights[0].Direction;

      ApplyMeshInstanceCB(it_MeshInfo->CnstBuffer, instanceCB);
      ApplyMeshFrameCB(it_MeshInfo->CnstBuffer, frameCB);

      unsigned int stride = it_MeshInfo->VertexSize;
      unsigned int offset = 0;

      ShaderBase *s = 0;
      // Phase A.5 step 2: bind shared VB pool if available, otherwise
      // fall back to the per-asset VB. Pool uploads are explicit at
      // the end of mesh creation; GetGPUBuffer() is a pure accessor.
      VertexBuffer* vbToBind = it_MeshInfo->VB;
      if (it_MeshInfo->vbPoolAlloc.IsValid()) {
        if (VertexPool* vpool = MeshAssetCache::Get().GetVertexPool(it_MeshInfo->vbPoolAlloc.poolId)) {
          if (VertexBuffer* gpu = vpool->GetGPUBuffer()) {
            vbToBind = gpu;
          }
        }
      }
      if (!vbToBind) {
        T8_LOG_ERROR("[RenderMesh] Skipped geometry %zu: no uploaded vertex buffer", i);
        continue;
      }
      vbToBind->Set(*deviceContext, stride, offset);

      // Build sorted draw order by shader key to minimize PSO switches
      std::size_t numSubsets = it_MeshInfo->SubSets.size();
      std::vector<std::size_t>& drawOrder = m_drawOrderScratch;
      drawOrder.resize(numSubsets);
      for (std::size_t k = 0; k < numSubsets; k++) drawOrder[k] = k;
      {
        T8_TELEMETRY_SCOPE("render.mesh.subset_sort");
        std::stable_sort(drawOrder.begin(), drawOrder.end(),
          [&](std::size_t a, std::size_t b) {
            if (currentPass == PassType::FORWARD) {
              int groupA = ForwardSubsetGroup(it_MeshInfo->SubSets[a]);
              int groupB = ForwardSubsetGroup(it_MeshInfo->SubSets[b]);
              if (groupA != groupB)
                return groupA < groupB;
              float da = SubsetViewDepth(it_MeshInfo->SubSets[a], transform, pRenderCamera->Eye, pRenderCamera->Look);
              float db = SubsetViewDepth(it_MeshInfo->SubSets[b], transform, pRenderCamera->Eye, pRenderCamera->Look);
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
          }
        );
      }

      for (std::size_t ki = 0; ki < numSubsets; ki++) {
        std::size_t k = drawOrder[ki];
        bool update = false;
        SubSetInfo *sub_info = &it_MeshInfo->SubSets[k];

        if (!ShouldDrawSubsetInPass(*sub_info, currentPass))
          continue;

        // Per-subset frustum cull
        uint8_t subsetFrustumResult = frustumInside;
        if (frustumCullingEnabled && meshFrustumResult != frustumInside) {
          if (trackCullStats)
            m_cullingSubsetTests++;
          subsetFrustumResult = timeCullWork([&]() -> uint8_t {
            return static_cast<uint8_t>(ClassifyAABBFrustum(sub_info->bounds, transform, frustumPlanes));
          });
        }
        if (subsetFrustumResult == frustumOutside) {
          if (trackCullStats) {
            m_culledSubsets++;
            m_culledClusters += static_cast<int>(ClusterCountForSubset(m_asset, *sub_info));
          }
          continue;
        }
        if (trackCullStats)
          m_visibleSubsets++;

        // Phase B step 2: read material data via the deduplicated
        // MaterialAsset. SubSetInfo material fields are still
        // populated for now (step 3 retires them) but are no longer
        // the source of truth for rendering. Per-instance fields like
        // MatID stay on SubSetInfo.
        const MaterialAsset* mat = sub_info->matAsset;
        const MaterialParams* mp = mat ? &mat->params : nullptr;
        RenderMesh::MeshMaterialCBuffer& materialCB = sub_info->MaterialCB;
        if (mp) {
          FillCBufferFromMaterial(materialCB, *mp);
          // Per-instance MatID overrides the alpha slot used by
          // FillCBufferFromMaterial (it filled .w with intensities[3]
          // but the engine reuses Intensities.w for MatID).
          materialCB.Intensities.w = (float)sub_info->MatID;
        } else {
          // Defensive fallback (shouldn't happen if Create() ran).
          FillMaterialCBFromSubset(materialCB, *sub_info);
        }
        materialCB.ForwardParams = XVECTOR3((float)driver->width, (float)driver->height, Textures[7] ? 1.0f : 0.0f, mp ? mp->ior : sub_info->IOR);
        float emissiveMul = pScProp ? pScProp->MaterialEmissiveIntensity : 1.0f;
        float transmissionMul = pScProp ? pScProp->MaterialTransmissionMultiplier : 1.0f;
        float refractionStrength = pScProp ? pScProp->MaterialRefractionStrength : 0.03f;
        float lightmapMul = pScProp ? (std::max)(0.0f, pScProp->LightmapIntensity) : 1.0f;
        float iblFactor = pScProp ? pScProp->IBLFactor : 1.0f;
        float iblMipCount = pScProp ? pScProp->IBLMipCount : 4.0f;
        float iblDiffuseMipLevel = pScProp ? pScProp->IBLDiffuseMipLevel : 4.0f;
        float iblBrdfLutEnabled = pScProp ? pScProp->IBLBRDFLUTEnabled : 0.0f;
        if (mp) {
          // Material-driven slots: clearcoat factors + unlit flag (.x..z),
          // plus per-frame multipliers in the .w slots.
          materialCB.MaterialParams  = XVECTOR3(mp->clearcoatFactor, mp->clearcoatRoughness, mp->unlit ? 1.0f : 0.0f, emissiveMul);
          materialCB.MaterialParams5 = XVECTOR3(mat->textures[(int)MatTexSlot::SheenColor]     ? 1.0f : 0.0f,
                                                              mat->textures[(int)MatTexSlot::SheenRoughness] ? 1.0f : 0.0f,
                                                              static_cast<float>(mp->sheenColorTexCoord),
                                                              static_cast<float>(mp->sheenRoughTexCoord));
          materialCB.MaterialParams6 = XVECTOR3(mat->textures[(int)MatTexSlot::Clearcoat]          ? 1.0f : 0.0f,
                                                              mat->textures[(int)MatTexSlot::ClearcoatRoughness] ? 1.0f : 0.0f,
                                                              static_cast<float>(mp->clearcoatTexCoord),
                                                              static_cast<float>(mp->clearcoatRoughTexCoord));
          materialCB.MaterialParams7 = XVECTOR3(mat->textures[(int)MatTexSlot::Occlusion] ? 1.0f : 0.0f,
                                                              mp->occlusionStrength,
                                                              static_cast<float>(mp->occlusionTexCoord),
                                                              mat->textures[(int)MatTexSlot::Transmission] ? 1.0f : 0.0f);
          materialCB.MaterialParams8 = XVECTOR3(static_cast<float>(mp->transmissionTexCoord),
                                                              mat->textures[(int)MatTexSlot::SpecularFactor] ? 1.0f : 0.0f,
                                                              static_cast<float>(mp->specFactorTexCoord),
                                                              mat->textures[(int)MatTexSlot::SpecularColor]  ? 1.0f : 0.0f);
        } else {
          materialCB.MaterialParams  = XVECTOR3(sub_info->ClearcoatFactor, sub_info->ClearcoatRoughness, sub_info->Unlit ? 1.0f : 0.0f, emissiveMul);
          materialCB.MaterialParams5 = XVECTOR3(sub_info->SheenColorTex ? 1.0f : 0.0f, sub_info->SheenRoughnessTex ? 1.0f : 0.0f, (float)sub_info->SheenColorTexCoord, (float)sub_info->SheenRoughnessTexCoord);
          materialCB.MaterialParams6 = XVECTOR3(sub_info->ClearcoatTex ? 1.0f : 0.0f, sub_info->ClearcoatRoughnessTex ? 1.0f : 0.0f, (float)sub_info->ClearcoatTexCoord, (float)sub_info->ClearcoatRoughnessTexCoord);
          materialCB.MaterialParams7 = XVECTOR3(sub_info->OcclusionTex ? 1.0f : 0.0f, sub_info->OcclusionStrength, (float)sub_info->OcclusionTexCoord, sub_info->TransmissionTex ? 1.0f : 0.0f);
          materialCB.MaterialParams8 = XVECTOR3((float)sub_info->TransmissionTexCoord, sub_info->SpecularFactorTex ? 1.0f : 0.0f, (float)sub_info->SpecularFactorTexCoord, sub_info->SpecularColorTex ? 1.0f : 0.0f);
        }
        materialCB.MaterialParams9.w *= lightmapMul;
        materialCB.MaterialParams2 = XVECTOR3(transmissionMul, refractionStrength, Textures[9] ? 1.0f : 0.0f, iblFactor);
        materialCB.MaterialParams3 = XVECTOR3(iblMipCount, iblBrdfLutEnabled, iblDiffuseMipLevel, 0.0f);
        ApplyMeshMaterialCB(it_MeshInfo->CnstBuffer, materialCB);

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
        if (!ibToBind) {
          T8_LOG_ERROR("[RenderMesh] Skipped subset %zu: no uploaded index buffer", k);
          continue;
        }
        IndexBufferFormat::E ibFmt = sub_info->IB32Bit ? IndexBufferFormat::R32 : IndexBufferFormat::R16;
        // Phase C step 3: IB-bind dedup via process-wide tracker.
        if (tracker.ShouldBindIB(ibToBind, ibFmt)) {
          if (trackCullStats) m_renderStateChanges++;
          ibToBind->Set(*deviceContext, 0, ibFmt);
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

        s = driver->GetShader(finalKey);
        if (!s) continue;

     //   if (s != last)
          update = true;

        BaseDriver::FaceCulling prevCull = driver->m_FaceCulling;
        const bool subsetDoubleSided = mp ? (mp->doubleSided != 0) : sub_info->DoubleSided;
        bool changedCull = subsetDoubleSided && prevCull != BaseDriver::FRONT_AND_BACK;
        if (changedCull) {
          if (trackCullStats) m_renderStateChanges++;
          driver->SetCullFace(BaseDriver::FRONT_AND_BACK);
        }

        if (update) {
          // D3D12 invariant: PSO is keyed by (shader, blend, depth,
          // cull, RT formats) and re-derived at Set time. Always call
          // s->Set; the driver dedupes via m_lastPSO. Tracker only
          // notes the shader change to invalidate texture cache
          // (per-shader rootParam map).
          if (trackCullStats) m_renderStateChanges++;
          s->Set(*deviceContext);
          tracker.OnShaderChanged(s);

          if (driver->m_currentAPI == GraphicsApi::OPENGL) {
            if (tracker.UpdateAndBindConstantBuffer(*deviceContext, it_MeshInfo->CB, 0,
                                                    &it_MeshInfo->CnstBuffer,
                                                    sizeof(RenderMesh::CBuffer)) && trackCullStats)
              m_renderStateChanges++;
          } else {
            if (tracker.UpdateAndBindConstantBuffer(*deviceContext, it_MeshInfo->FrameCBGPU, 0,
                                                    &it_MeshInfo->FrameCB,
                                                    sizeof(RenderMesh::MeshFrameCBuffer)) && trackCullStats)
              m_renderStateChanges++;
            if (tracker.UpdateAndBindConstantBuffer(*deviceContext, it_MeshInfo->InstanceCBGPU, 1,
                                                    &it_MeshInfo->InstanceCB,
                                                    sizeof(RenderMesh::MeshInstanceCBuffer)) && trackCullStats)
              m_renderStateChanges++;
            if (tracker.UpdateAndBindConstantBuffer(*deviceContext, it_MeshInfo->MaterialCBGPU, 2,
                                                    &sub_info->MaterialCB,
                                                    sizeof(RenderMesh::MeshMaterialCBuffer)) && trackCullStats)
              m_renderStateChanges++;
          }
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
          bindTextureOnce(t, 1, "SpecularTex", 1);
        }

        if (s->key.has(ShaderKey::GLOSS_MAP)) {
          Texture* t = matTex(MatTexSlot::Gloss); if (!t) t = sub_info->GlossfTex;
          bindTextureOnce(t, 2, "GlossTex", 2);
        }

        if (s->key.has(ShaderKey::NORMAL_MAP)) {
          Texture* t = matTex(MatTexSlot::Normal); if (!t) t = sub_info->NormalTex;
          bindTextureOnce(t, 3, "NormalTex", 3);
        }
        if (EnvMap) {
          // EnvMap goes to slot 4 with its own dedicated tracker (so
          // it's not confused with material-driven slot 4 textures
          // from a different shader path).
          if (tracker.ShouldBindEnvMap(EnvMap)) {
            if (trackCullStats) m_renderStateChanges++;
            EnvMap->Set(*deviceContext, 4, "texEnv");
          }
          EnvMap->SetSampler(*deviceContext, EnvSamplerSlot);
        }
        if (s->key.has(ShaderKey::HEIGHT_MAP)) {
          Texture* t = matTex(MatTexSlot::Parallax); if (!t) t = sub_info->ParalaxTex;
          bindTextureOnce(t, 5, "HeightTex", 5);
        }
        if (s->key.has(ShaderKey::METALLIC_MAP)) {
          Texture* t = matTex(MatTexSlot::Metallic); if (!t) t = sub_info->MetallicTex;
          bindTextureOnce(t, 6, "MetallicTex", 6);
        }
        bindTextureOnce(Textures[7], 7, "SceneDepthTex", 7);
        if (s->key.has(ShaderKey::EMISSIVE_MAP)) {
          Texture* t = matTex(MatTexSlot::Emissive); if (!t) t = sub_info->EmissiveTex;
          bindTextureOnce(t, 8, "EmissiveTex", 8);
        }
        bindTextureOnce(Textures[9], 9, "SceneColorTex", 9);
        bindTextureOnce(Textures[EnvironmentTextureSlot::DiffuseIBL],  EnvironmentTextureSlot::DiffuseIBL,  "texIBLDiffuse",   EnvironmentTextureSlot::DiffuseIBL);
        bindTextureOnce(Textures[EnvironmentTextureSlot::SpecularIBL], EnvironmentTextureSlot::SpecularIBL, "texIBLSpecular",  EnvironmentTextureSlot::SpecularIBL);
        bindTextureOnce(Textures[EnvironmentTextureSlot::BrdfLUT],     EnvironmentTextureSlot::BrdfLUT,     "texIBLBRDF",      EnvironmentTextureSlot::BrdfLUT);
        bindTextureOnce(Textures[EnvironmentTextureSlot::CharlieIBL],  EnvironmentTextureSlot::CharlieIBL,  "texIBLCharlie",   EnvironmentTextureSlot::CharlieIBL);
        bindTextureOnce(Textures[EnvironmentTextureSlot::CharlieLUT],  EnvironmentTextureSlot::CharlieLUT,  "texIBLCharlieLUT",EnvironmentTextureSlot::CharlieLUT);
        bindTextureOnce(Textures[EnvironmentTextureSlot::SheenELUT],   EnvironmentTextureSlot::SheenELUT,   "texIBLSheenELUT", EnvironmentTextureSlot::SheenELUT);
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
        if (s->key.has(ShaderKey::LIGHTMAP_MAP)) {
          Texture* t = matTex(MatTexSlot::Lightmap); if (!t) t = sub_info->LightmapTex;
          bindTextureOnce(t, MaterialTextureSlot::Lightmap, "LightmapTex", LightmapSamplerSlot);
        }

        if (trackCullStats) m_renderStateChanges++;
        deviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
        // Phase A.5 step 2: when using shared pools the index/vertex
        // offsets steer the draw to this submesh's allocation. Falls
        // back to (count, 0, 0) on the legacy per-subset IB path.
        bool drewSubset = false;
        const Submesh* submesh = FindSubmeshForSubset(m_asset, *sub_info);
        const bool useClusterPath = frustumCullingEnabled &&
                  m_cullingMetadataReady &&
                  currentPass == PassType::GBUFFER &&
                                    submesh && submesh->clusterCount > 0 &&
                                    sub_info->ibPoolAlloc.IsValid() &&
                                    it_MeshInfo->vbPoolAlloc.IsValid();
        if (useClusterPath) {
          const bool clusterParentFullyInside = subsetFrustumResult == frustumInside;
          for (uint32_t clusterOffset = 0; clusterOffset < submesh->clusterCount; ++clusterOffset) {
            const uint32_t clusterIndex = submesh->firstCluster + clusterOffset;
            if (!m_asset || clusterIndex >= m_asset->clusters.size())
              continue;
            const SubmeshCluster& cluster = m_asset->clusters[clusterIndex];
            bool clusterInsideFrustum = true;
            if (!frustumCullingEnabled || clusterParentFullyInside) {
              clusterInsideFrustum = true;
            } else {
              if (trackCullStats)
                m_cullingClusterTests++;
              clusterInsideFrustum = timeCullWork([&]() -> bool {
                return ClassifyCanonicalAABBFrustum(cluster.localAABB, transform, frustumPlanes) != FrustumResult::Outside;
              });
            }
            if (!clusterInsideFrustum) {
              if (trackCullStats)
                m_culledClusters++;
              continue;
            }
            deviceContext->DrawIndexed(cluster.indexCount,
                                         sub_info->ibPoolAlloc.offsetElems + cluster.indexOffset,
                                         it_MeshInfo->vbPoolAlloc.offsetElems);
            if (trackCullStats) {
              m_drawCalls++;
              m_visibleClusters++;
              m_drawnClusters++;
              m_drawnIndices += cluster.indexCount;
            }
            drewSubset = true;
          }
        } else if (sub_info->ibPoolAlloc.IsValid() && it_MeshInfo->vbPoolAlloc.IsValid()) {
          deviceContext->DrawIndexed(sub_info->ibPoolAlloc.count,
                                       sub_info->ibPoolAlloc.offsetElems,
                                       it_MeshInfo->vbPoolAlloc.offsetElems);
          if (trackCullStats) {
            m_drawCalls++;
            m_drawnIndices += sub_info->ibPoolAlloc.count;
          }
          drewSubset = true;
        } else {
          deviceContext->DrawIndexed(sub_info->NumVertex, 0, 0);
          if (trackCullStats) {
            m_drawCalls++;
            m_drawnIndices += sub_info->NumVertex;
          }
          drewSubset = true;
        }
        if (changedCull) {
          if (trackCullStats) m_renderStateChanges++;
          driver->SetCullFace(prevCull);
        }
        if (trackCullStats && drewSubset)
          m_drawnSubsets++;
      }
    }
    if (ownsScope) tracker.End();
    if (trackCullStats) {
      m_culledIndices = m_totalIndices > m_drawnIndices ? (m_totalIndices - m_drawnIndices) : 0;
      m_cullingCpuMs = static_cast<double>(cullingCpuNs) / 1000000.0;
      RuntimeTelemetry::AddCounter("render.mesh.totalMeshes", static_cast<double>(m_totalMeshes));
      RuntimeTelemetry::AddCounter("render.mesh.visibleMeshes", static_cast<double>(m_visibleMeshes));
      RuntimeTelemetry::AddCounter("render.mesh.culledMeshes", static_cast<double>(m_culledMeshes));
      RuntimeTelemetry::AddCounter("render.mesh.totalSubsets", static_cast<double>(m_totalSubsets));
      RuntimeTelemetry::AddCounter("render.mesh.drawnSubsets", static_cast<double>(m_drawnSubsets));
      RuntimeTelemetry::AddCounter("render.mesh.drawCalls", static_cast<double>(m_drawCalls));
      RuntimeTelemetry::AddCounter("render.mesh.drawnIndices", static_cast<double>(m_drawnIndices));
      RuntimeTelemetry::AddCounter("render.mesh.cullingCpuMs", m_cullingCpuMs);
    }
  }

  void RenderMesh::Destroy() {
    // Phase A.5 step 3 + B step 1: VB/IB are owned by MeshAssetCache
    // pools. Material data is shared via MaterialAssetCache. Only the
    // mesh CBs are released here; assets are dereferenced
    // through their respective caches.
    for (auto &mIt : Info) {
      if (mIt.CB) mIt.CB->release();
      mIt.CB = nullptr;
      if (mIt.FrameCBGPU) mIt.FrameCBGPU->release();
      mIt.FrameCBGPU = nullptr;
      if (mIt.InstanceCBGPU) mIt.InstanceCBGPU->release();
      mIt.InstanceCBGPU = nullptr;
      if (mIt.MaterialCBGPU) mIt.MaterialCBGPU->release();
      mIt.MaterialCBGPU = nullptr;
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

    for (auto& wg : m_wireGeo) {
      if (wg.IB) { wg.IB->release(); wg.IB = nullptr; }
    }
    m_wireGeo.clear();
    m_wireShader = nullptr;
    m_lineRenderer.Destroy();
    m_wireDepthTex = nullptr;
    m_wireDepthTex2 = nullptr;
    m_cullingMetadataReady = false;
  }
}
