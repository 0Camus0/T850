/*********************************************************
 * MaterialAsset / MaterialAssetCache — Phase B of the
 * geometry/material refactor. Hash-keyed dedup of material
 * data so that two SubSetInfo records (or future
 * RenderEntity material-overrides) collapse into one shared
 * GPU-bound material when their content is identical.
 *
 * Phase B step 1: the cache is populated alongside the
 * legacy SubSetInfo fields. The draw path still reads from
 * the SubSetInfo. Step 2 retires the duplicate fields.
 *********************************************************/

#ifndef T850_MATERIAL_ASSET_H
#define T850_MATERIAL_ASSET_H

#include <Config.h>
#include <Descriptors.h>
#include <utils/xMaths.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace t850 {
  class Texture;

  // POD bag of every per-material parameter the shader CB consumes.
  // Mirrors the MaterialParams* layout in RenderMesh::CBuffer plus
  // every per-textureInfo UV transform. Hash and equality are
  // defined over the bit pattern of this struct (binary equal →
  // hash equal), so std::memcmp is the equality oracle.
  //
  // IMPORTANT: vector-like fields are raw `float[4]` (POD), NOT
  // XVECTOR3. T850's XVECTOR3 has a copy constructor that forces
  // `w = 1.0f` regardless of source — that quirk would silently
  // corrupt cached entries (their stored .w would diverge from the
  // hash that was computed before the copy). Storing as plain floats
  // avoids the issue and makes the struct trivially copyable too.
  struct MaterialParams {
    // Colors / scalars
    float    ambientColor[4]    = {0.0f, 0.0f, 0.0f, 1.0f};
    float    diffuseColor[4]    = {0.5f, 0.5f, 0.5f, 1.0f};
    float    specularColor[4]   = {0.04f, 0.04f, 0.04f, 1.0f};
    float    pbrParams[4]       = {0.0f, 0.8f, 0.0f, 0.0f};
    float    intensities[4]     = {0.0f, 0.0f, 0.0f, 1.0f};
    float    emissiveColor[4]   = {0.0f, 0.0f, 0.0f, 1.0f};
    float    sheenColor[4]      = {0.0f, 0.0f, 0.0f, 0.0f};

    float    sheenRoughness     = 0.0f;
    float    clearcoatFactor    = 0.0f;
    float    clearcoatRoughness = 0.0f;
    float    transmissionFactor = 0.0f;
    float    ior                = 1.5f;
    float    occlusionStrength  = 1.0f;
    float    normalScale        = 1.0f;
    float    alphaCutoff        = 0.5f;

    // UV transforms — 12 textureInfos × 2 vec4 each (24 vec4 total).
    float    baseColorUV0[4]        = {1,0,0,0}, baseColorUV1[4]        = {0,1,0,0};
    float    normalUV0[4]           = {1,0,0,0}, normalUV1[4]           = {0,1,0,0};
    float    metallicUV0[4]         = {1,0,0,0}, metallicUV1[4]         = {0,1,0,0};
    float    emissiveUV0[4]         = {1,0,0,0}, emissiveUV1[4]         = {0,1,0,0};
    float    sheenColorUV0[4]       = {1,0,0,0}, sheenColorUV1[4]       = {0,1,0,0};
    float    sheenRoughUV0[4]       = {1,0,0,0}, sheenRoughUV1[4]       = {0,1,0,0};
    float    clearcoatUV0[4]        = {1,0,0,0}, clearcoatUV1[4]        = {0,1,0,0};
    float    clearcoatRoughUV0[4]   = {1,0,0,0}, clearcoatRoughUV1[4]   = {0,1,0,0};
    float    occlusionUV0[4]        = {1,0,0,0}, occlusionUV1[4]        = {0,1,0,0};
    float    specFactorUV0[4]       = {1,0,0,0}, specFactorUV1[4]       = {0,1,0,0};
    float    specColorUV0[4]        = {1,0,0,0}, specColorUV1[4]        = {0,1,0,0};
    float    transmissionUV0[4]     = {1,0,0,0}, transmissionUV1[4]     = {0,1,0,0};

    // Per-textureInfo texCoord set selector (0..3). Packed as bytes
    // to keep the struct small; convert to float in the CB upload.
    uint8_t  diffuseTexCoord    = 0;
    uint8_t  normalTexCoord     = 0;
    uint8_t  metallicTexCoord   = 0;
    uint8_t  emissiveTexCoord   = 0;
    uint8_t  sheenColorTexCoord = 0;
    uint8_t  sheenRoughTexCoord = 0;
    uint8_t  clearcoatTexCoord  = 0;
    uint8_t  clearcoatRoughTexCoord = 0;
    uint8_t  occlusionTexCoord  = 0;
    uint8_t  specFactorTexCoord = 0;
    uint8_t  specColorTexCoord  = 0;
    uint8_t  transmissionTexCoord = 0;

    // Mode flags
    uint8_t  alphaMode          = 0;
    uint8_t  doubleSided        = 0;
    uint8_t  unlit              = 0;
    uint8_t  bUseFresnel        = 0;
    uint8_t  _pad[4]            = {0,0,0,0};
  };
  static_assert(std::is_trivially_copyable<MaterialParams>::value,
                "MaterialParams must be trivially copyable for memcmp dedup");

  // Slot indices into MaterialAsset::textures[]. Mirrors the legacy
  // texture pointer set on SubSetInfo so we can route through this
  // array without renaming everything at once.
  enum class MatTexSlot : uint8_t {
    BaseColor          = 0,
    Specular           = 1,
    Gloss              = 2,
    Normal             = 3,
    Reflect            = 4,
    Parallax           = 5,
    Metallic           = 6,
    Emissive           = 7,
    SheenColor         = 8,
    SheenRoughness     = 9,
    Clearcoat          = 10,
    ClearcoatRoughness = 11,
    Occlusion          = 12,
    SpecularFactor     = 13,
    SpecularColor      = 14,
    Transmission       = 15,
    Count              = 16,
  };

  struct MaterialAsset {
    std::string     name;
    uint64_t        contentHash    = 0;             // dedup key in MaterialAssetCache
    ShaderKey       featureKey;                     // material feature bits only (no vertex attribs, no pass)
    Texture*        textures[(int)MatTexSlot::Count] = {};
    int             textureIds[(int)MatTexSlot::Count] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    MaterialParams  params;
    uint32_t        refCount       = 0;
  };

  class MaterialAssetCache {
  public:
    static MaterialAssetCache& Get();

    // Compute the content hash of (textureIds + featureBits + params)
    // for a fully-populated MaterialAsset. Stable across runs since it
    // hashes paths/ids and binary parameter blocks (no pointers).
    static uint64_t ComputeHash(const MaterialAsset& mat);

    // Acquire a cached asset matching `prototype`. If no entry with
    // the same hash + content exists, the cache adopts a copy of the
    // prototype (refCount=1). On cache hit, returns the existing
    // asset and bumps refCount. `outCreated` reports which path.
    MaterialAsset* Acquire(const MaterialAsset& prototype, bool* outCreated = nullptr);

    void Release(MaterialAsset* asset);

    std::size_t Size() const;
    void DumpToLog() const;
    void Clear();

  private:
    MaterialAssetCache() = default;
    ~MaterialAssetCache() = default;

    static bool ContentEqual(const MaterialAsset& a, const MaterialAsset& b);

    mutable std::mutex                                              m_mutex;
    // Multi-bucket map: hash -> list of assets with that hash. We
    // double-check ContentEqual on lookup to handle collisions.
    std::unordered_map<uint64_t, std::vector<std::unique_ptr<MaterialAsset>>> m_byHash;
    std::size_t                                                     m_total = 0;
  };

  // Helper to fill a RenderMesh::CBuffer-shaped struct from a
  // MaterialAsset's MaterialParams. Templated so MaterialAsset.h
  // doesn't need to include RenderMesh.h. Caller still has to set
  // per-instance fields (WVP/World/CameraPos/Lights/MatID).
  template <typename CBufferT>
  inline void FillCBufferFromMaterial(CBufferT& cb, const MaterialParams& mp) {
    auto v3 = [](const float a[4]) { return XVECTOR3(a[0], a[1], a[2], a[3]); };
    cb.AmbientColor   = v3(mp.ambientColor);
    cb.DiffuseColor   = v3(mp.diffuseColor);
    cb.SpecularColor  = v3(mp.specularColor);
    cb.PBRParams      = v3(mp.pbrParams);
    cb.Intensities    = v3(mp.intensities);
    cb.EmissiveColor  = v3(mp.emissiveColor);
    cb.AlphaParams    = XVECTOR3(static_cast<float>(mp.alphaMode),
                                 mp.alphaCutoff,
                                 mp.doubleSided ? 1.0f : 0.0f,
                                 mp.transmissionFactor);
    cb.TexCoordSets   = XVECTOR3(static_cast<float>(mp.diffuseTexCoord),
                                 static_cast<float>(mp.normalTexCoord),
                                 static_cast<float>(mp.metallicTexCoord),
                                 static_cast<float>(mp.emissiveTexCoord));
    // MaterialParams .w slots filled by caller (emissiveMul, etc.).
    cb.MaterialParams4 = XVECTOR3(mp.sheenColor[0], mp.sheenColor[1], mp.sheenColor[2], mp.sheenRoughness);
    cb.MaterialParams9 = XVECTOR3(static_cast<float>(mp.specColorTexCoord), mp.normalScale, 0.0f, 0.0f);
    cb.BaseColorUVTransform0 = v3(mp.baseColorUV0); cb.BaseColorUVTransform1 = v3(mp.baseColorUV1);
    cb.NormalUVTransform0    = v3(mp.normalUV0);    cb.NormalUVTransform1    = v3(mp.normalUV1);
    cb.MetallicUVTransform0  = v3(mp.metallicUV0);  cb.MetallicUVTransform1  = v3(mp.metallicUV1);
    cb.EmissiveUVTransform0  = v3(mp.emissiveUV0);  cb.EmissiveUVTransform1  = v3(mp.emissiveUV1);
    cb.SheenColorUVTransform0 = v3(mp.sheenColorUV0); cb.SheenColorUVTransform1 = v3(mp.sheenColorUV1);
    cb.SheenRoughnessUVTransform0 = v3(mp.sheenRoughUV0); cb.SheenRoughnessUVTransform1 = v3(mp.sheenRoughUV1);
    cb.ClearcoatUVTransform0      = v3(mp.clearcoatUV0);   cb.ClearcoatUVTransform1      = v3(mp.clearcoatUV1);
    cb.ClearcoatRoughnessUVTransform0 = v3(mp.clearcoatRoughUV0); cb.ClearcoatRoughnessUVTransform1 = v3(mp.clearcoatRoughUV1);
    cb.OcclusionUVTransform0   = v3(mp.occlusionUV0);   cb.OcclusionUVTransform1   = v3(mp.occlusionUV1);
    cb.SpecularFactorUVTransform0 = v3(mp.specFactorUV0); cb.SpecularFactorUVTransform1 = v3(mp.specFactorUV1);
    cb.SpecularColorUVTransform0  = v3(mp.specColorUV0);  cb.SpecularColorUVTransform1  = v3(mp.specColorUV1);
    cb.TransmissionUVTransform0   = v3(mp.transmissionUV0); cb.TransmissionUVTransform1 = v3(mp.transmissionUV1);
  }
}

#endif // T850_MATERIAL_ASSET_H
