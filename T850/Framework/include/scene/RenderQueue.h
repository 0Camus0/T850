/*********************************************************
 * DrawItem / RenderQueue / RenderEntity — Phase C scaffolding.
 *
 * The shape of a flat, sortable draw list (one item per
 * submesh per frame), and the per-pass queue that batches
 * across entities.
 *
 * Phase C step 1 (this commit): types only. Not wired into
 * the render path yet — RenderMesh::Draw still does its own
 * per-entity walk.
 *
 * Phase C step 2 will refactor RenderMesh::Draw to populate
 * a RenderQueue and call its Execute, preserving today's
 * order. State tracking in Execute will skip redundant
 * VB/IB/PSO/material rebinds.
 *
 * Phase C step 3 will lift the queue to scene scope so
 * multiple RenderEntity instances can share one queue per
 * pass — the cross-entity batching that motivates this
 * refactor.
 *
 * For sort key bit layout, see RENDER_ARCHITECTURE.md §5.
 *********************************************************/

#ifndef T850_RENDER_QUEUE_H
#define T850_RENDER_QUEUE_H

#include <Config.h>
#include <Descriptors.h>
#include <utils/xMaths.h>
#include <utils/Picking.h>           // canonical t850::AABB
#include <scene/MeshAsset.h>
#include <scene/MeshAssetCache.h>
#include <scene/MaterialAsset.h>

#include <cstdint>
#include <vector>

namespace t850 {
  class VertexBuffer;
  class IndexBuffer;
  class ConstantBuffer;
  class Texture;
  class ShaderBase;

  // A single dispatchable draw within a pass. POD-ish (has a
  // ShaderKey but no destructors that matter for trivially-copyable
  // semantics). Sized to fit comfortably in a cache line × 2.
  struct DrawItem {
    // ── Sort key ─────────────────────────────────────────────────
    // 64-bit packed identity used by std::sort. Layout (Phase C step
    // 3+; today this is left at zero by the producer if no sort is
    // requested):
    //   opaque:     pass(6) layer(4) psoId(16) materialId(16) vbPool(8) ibPool(1) depthBucket(13)
    //   transparent: pass(6) layer(4) depthBucket(24) psoId(16) materialIdLow(14)
    uint64_t        sortKey       = 0;

    // ── Shader / PSO ─────────────────────────────────────────────
    // Final pass-merged ShaderKey. The executor calls
    // BaseDriver::GetShader(finalKey) once per change. Cached
    // dense psoId for sort packing arrives in step 3.
    ShaderKey       finalKey;

    // ── Geometry (Tier 1 pool refs) ──────────────────────────────
    VertexPool*     vbPool        = nullptr;     // null → vbFallback
    VertexBuffer*   vbFallback    = nullptr;     // legacy per-asset VB
    uint32_t        vbStride      = 0;
    uint32_t        baseVertex    = 0;           // pool offset (elems)

    IndexPool*      ibPool        = nullptr;     // null → ibFallback
    IndexBuffer*    ibFallback    = nullptr;
    bool            ib32Bit       = false;
    uint32_t        indexStart    = 0;           // pool offset (elems)
    uint32_t        indexCount    = 0;

    // ── Material ─────────────────────────────────────────────────
    const MaterialAsset* material = nullptr;

    // ── Per-instance ─────────────────────────────────────────────
    ConstantBuffer*       cb           = nullptr;  // per-instance/per-mesh CB (borrowed)
    uint32_t              matId        = 0;        // copied to Intensities.w
    bool                  doubleSided  = false;    // raster state hint

    // For diagnostics / future depth bucketing
    float           viewDepth     = 0.0f;
  };

  // Per-instance scene presence. Today RenderMesh fuses asset and
  // instance state; this struct pulls out the per-instance facets
  // that Phase C step 3 will live on. Step 1 defines the type and
  // its relationship to MeshAsset/MaterialAsset; step 2 makes Scene
  // walk a vector of these to extract DrawItems.
  struct RenderEntity {
    uint32_t                    id              = 0;
    MeshAsset*                  mesh            = nullptr;
    std::vector<MaterialAsset*> materialOverrides;       // size==mesh->submeshes.size(); null = use Submesh's matAsset
    XMATRIX44                   worldFromLocal;
    AABB                        worldAABB;
    uint8_t                     layerMask       = 0xFF;
    bool                        visible         = true;
  };

  // Per-pass flat draw list. Reset/Push at extract, Sort, Execute.
  // Phase C step 1: only stores a vector + reset. The Execute(...)
  // signature will take a SceneFrameContext in step 2 — left as a
  // forward declaration here.
  class SceneFrameContext;   // step 2

  class RenderQueue {
  public:
    void Reset()                              { m_items.clear(); }
    void Push(const DrawItem& item)           { m_items.push_back(item); }
    void Reserve(std::size_t n)               { m_items.reserve(n); }
    std::size_t Size() const                  { return m_items.size(); }
    bool        Empty() const                 { return m_items.empty(); }

    // Sort by sortKey ascending. No-op if Push order is desired.
    // The producer is responsible for assigning sortKey before Sort.
    void Sort();

    // Phase C step 2: defined out-of-line in RenderQueue.cpp once
    // SceneFrameContext is fleshed out. For step 1 the queue exists
    // but does not drive rendering.
    // void Execute(SceneFrameContext& ctx);

    const std::vector<DrawItem>& Items() const { return m_items; }
          std::vector<DrawItem>& Items()       { return m_items; }

  private:
    std::vector<DrawItem> m_items;
  };
}

#endif // T850_RENDER_QUEUE_H
