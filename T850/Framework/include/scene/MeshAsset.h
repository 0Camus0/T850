/*********************************************************
 * MeshAsset / Submesh — Phase A of the entity/geometry
 * refactor. These are immutable, ref-counted GPU+CPU asset
 * objects shared across all RenderEntity instances of the
 * same source file.
 *
 * In Phase A step 1 these types coexist with RenderMesh
 * without being wired in. RenderMesh keeps owning its own
 * VB/IB; MeshAssetCache::Load is implemented but unused.
 * Subsequent steps will move RenderMesh to dereference a
 * MeshAsset* and finally let multiple RenderMesh share one
 * MeshAsset.
 *********************************************************/

#ifndef T850_MESH_ASSET_H
#define T850_MESH_ASSET_H

#include <Config.h>
#include <Descriptors.h>
#include <utils/xMaths.h>
#include <utils/Picking.h>   // canonical t850::AABB (vMin/vMax)

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace t850 {
  class VertexBuffer;
  class IndexBuffer;

  // Suballocation handle into a VertexPool / IndexPool. Offsets are in
  // ELEMENTS (vertices or indices), matching the API of DrawIndexed.
  struct PoolAlloc {
    uint32_t poolId      = UINT32_MAX;   // index into MeshAssetCache::m_vertexPools or m_indexPools
    uint32_t offsetElems = 0;            // start vertex / start index
    uint32_t count       = 0;            // verts or indices
    bool IsValid() const { return poolId != UINT32_MAX && count > 0; }
  };

  struct SubmeshCluster {
    uint32_t submeshIndex = 0;
    uint32_t indexOffset  = 0;  // relative to Submesh::ibAlloc.offsetElems
    uint32_t indexCount   = 0;
    AABB     localAABB;
  };

  // One drawable index range within a MeshAsset. Mirrors the geometry
  // portion of RenderMesh::SubSetInfo (vertexStart/numVertex/triStart/
  // numTris/IB32Bit/bounds + the vertex-attrib bits of the ShaderKey).
  // Material data does NOT live here — that becomes MaterialAsset in
  // Phase B.
  struct Submesh {
    uint32_t vertexStart   = 0;
    uint32_t vertexCount   = 0;
    uint32_t indexStart    = 0;   // in indices, not triangles
    uint32_t triangleCount = 0;
    uint32_t materialSlot  = 0;   // index into RenderEntity::materials in Phase C
    bool     ib32Bit       = false;
    AABB     localAABB;
    ShaderKey vertexAttribKey;    // only HAS_NORMALS/TANGENTS/BINORMALS/TEXCOORDn bits set
    uint32_t firstCluster    = 0;
    uint32_t clusterCount    = 0;

    // Phase A.5: shared VB/IB pool offsets. In step 1 these are
    // populated alongside the legacy per-asset GPU buffers but not
    // used by the draw path. Step 2 will swap the draw to use them.
    PoolAlloc vbAlloc;
    PoolAlloc ibAlloc;

    // Dense pool indices for sort-key packing. Same value as
    // vbAlloc.poolId / ibAlloc.poolId but stored as uint16_t so they
    // pack cleanly into the per-DrawItem 64-bit sort key without a
    // PoolAlloc dereference. See RENDER_ARCHITECTURE.md §"Identity
    // vs Sort key" for the bit-packing rationale.
    uint16_t vbPoolId      = 0xFFFFu;
    uint16_t ibPoolId      = 0xFFFFu;
  };

  // Shared geometry asset. One instance per distinct source path.
  // Geometry GPU memory lives in MeshAssetCache::m_vertexPools and
  // m_indexPools (Tier 1 storage). MeshAsset itself owns only CPU
  // metadata.
  struct MeshAsset {
    std::string             sourcePath;       // dedup key in MeshAssetCache
    uint64_t                vertexAttribMask  = 0;   // union of all submesh attrib bits
    uint32_t                vertexStride      = 0;
    uint32_t                vertexCount       = 0;
    uint32_t                indexCount        = 0;
    AABB                    rootAABB;                // union of submesh AABBs
    std::vector<Submesh>    submeshes;               // flattened across all geometries
    std::vector<SubmeshCluster> clusters;             // contiguous index ranges inside submeshes
    uint32_t                refCount          = 0;   // managed by MeshAssetCache
  };
}

#endif // T850_MESH_ASSET_H
