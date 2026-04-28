/*********************************************************
 * glTF 2.0 — POD spec types
 *
 * Pure data structures mirroring the glTF 2.0 schema:
 *   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
 *
 * No engine types are referenced here — the conversion to the engine's
 * xF::XDataBase happens in src/utils/gltf/GLTFMesh|Material|Animation.cpp.
 *
 * Field naming matches the spec exactly so that glaze's reflection-based
 * JSON parser can populate these structs with no explicit glz::meta
 * specialisations (members are deserialised by name).
 *
 * Fields not used in Phase 1 are still declared so unknown_keys=false
 * does not break — but only the ones consulted by the loader are filled.
 *********************************************************/

#ifndef T800_GLTF_TYPES_H
#define T800_GLTF_TYPES_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace t850 {
namespace gltf {

// ── Component / element type enumerants (per spec) ──
enum ComponentType : int {
  CT_BYTE           = 5120,
  CT_UNSIGNED_BYTE  = 5121,
  CT_SHORT          = 5122,
  CT_UNSIGNED_SHORT = 5123,
  CT_UNSIGNED_INT   = 5125,
  CT_FLOAT          = 5126,
};

// String "type" on accessor: SCALAR, VEC2, VEC3, VEC4, MAT2, MAT3, MAT4
inline int ElementCount(const std::string& t) {
  if (t == "SCALAR") return 1;
  if (t == "VEC2")   return 2;
  if (t == "VEC3")   return 3;
  if (t == "VEC4")   return 4;
  if (t == "MAT2")   return 4;
  if (t == "MAT3")   return 9;
  if (t == "MAT4")   return 16;
  return 0;
}

inline int ComponentSize(int ct) {
  switch (ct) {
    case CT_BYTE:
    case CT_UNSIGNED_BYTE:  return 1;
    case CT_SHORT:
    case CT_UNSIGNED_SHORT: return 2;
    case CT_UNSIGNED_INT:
    case CT_FLOAT:          return 4;
  }
  return 0;
}

// Primitive mode (mesh.primitive.mode). Default is TRIANGLES (4).
enum PrimitiveMode : int {
  PM_POINTS         = 0,
  PM_LINES          = 1,
  PM_LINE_LOOP      = 2,
  PM_LINE_STRIP     = 3,
  PM_TRIANGLES      = 4,
  PM_TRIANGLE_STRIP = 5,
  PM_TRIANGLE_FAN   = 6,
};

// ── Asset metadata ──
struct Asset {
  std::string version;     // required by spec
  std::string generator;
  std::string copyright;
  std::string minVersion;
};

// ── Buffer / BufferView / Accessor ──
struct Buffer {
  std::optional<std::string> uri;  // absent => GLB BIN chunk (buffer 0)
  std::size_t byteLength = 0;
  std::string name;
  // Resolved bytes (filled by loader, never serialised back).
  // Not parsed from JSON.
};

struct BufferView {
  int          buffer = 0;
  std::size_t  byteOffset = 0;
  std::size_t  byteLength = 0;
  std::optional<std::size_t> byteStride;
  std::optional<int> target;   // 34962 ARRAY_BUFFER / 34963 ELEMENT_ARRAY_BUFFER
  std::string  name;
};

struct AccessorSparseIndices {
  int          bufferView = 0;
  std::size_t  byteOffset = 0;
  int          componentType = CT_UNSIGNED_SHORT;
};

struct AccessorSparseValues {
  int         bufferView = 0;
  std::size_t byteOffset = 0;
};

struct AccessorSparse {
  std::size_t           count = 0;
  AccessorSparseIndices indices;
  AccessorSparseValues  values;
};

struct Accessor {
  std::optional<int> bufferView;        // absent => initialised to zero
  std::size_t        byteOffset = 0;
  int                componentType = CT_FLOAT;
  bool               normalized = false;
  std::size_t        count = 0;
  std::string        type;              // SCALAR/VEC2/VEC3/...
  std::vector<double> min;
  std::vector<double> max;
  std::optional<AccessorSparse> sparse;
  std::string        name;
};

// ── Texture / Image / Sampler ──
struct Image {
  std::optional<std::string> uri;        // either uri OR bufferView
  std::optional<int>         bufferView;
  std::optional<std::string> mimeType;   // "image/png" / "image/jpeg"
  std::string                name;
};

struct Sampler {
  std::optional<int> magFilter;
  std::optional<int> minFilter;
  int                wrapS = 10497; // REPEAT
  int                wrapT = 10497;
  std::string        name;
};

struct Texture {
  std::optional<int> sampler;
  std::optional<int> source; // index into images[]
  std::string        name;
};

// ── Material ──
struct TextureInfo {
  int         index = -1;
  int         texCoord = 0;
};

struct NormalTextureInfo {
  int   index = -1;
  int   texCoord = 0;
  float scale = 1.0f;
};

struct OcclusionTextureInfo {
  int   index = -1;
  int   texCoord = 0;
  float strength = 1.0f;
};

struct PBRMetallicRoughness {
  std::vector<float>         baseColorFactor;          // size 4 if present
  std::optional<TextureInfo> baseColorTexture;
  float                      metallicFactor = 1.0f;
  float                      roughnessFactor = 1.0f;
  std::optional<TextureInfo> metallicRoughnessTexture;
};

struct Material {
  std::string                          name;
  std::optional<PBRMetallicRoughness>  pbrMetallicRoughness;
  std::optional<NormalTextureInfo>     normalTexture;
  std::optional<OcclusionTextureInfo>  occlusionTexture;
  std::optional<TextureInfo>           emissiveTexture;
  std::vector<float>                   emissiveFactor;  // size 3 if present
  std::string                          alphaMode = "OPAQUE";
  float                                alphaCutoff = 0.5f;
  bool                                 doubleSided = false;

  struct Extensions {
    struct KHRMaterialsTransmission {
      float transmissionFactor = 0.0f;
      std::optional<TextureInfo> transmissionTexture;
    };
    struct KHRMaterialsEmissiveStrength {
      float emissiveStrength = 1.0f;
    };
    struct KHRMaterialsIOR {
      float ior = 1.5f;
    };
    struct KHRMaterialsVolume {
      float thicknessFactor = 0.0f;
      std::optional<TextureInfo> thicknessTexture;
      float attenuationDistance = 0.0f;
      std::vector<float> attenuationColor;
    };
    struct KHRMaterialsClearcoat {
      float clearcoatFactor = 0.0f;
      std::optional<TextureInfo> clearcoatTexture;
      float clearcoatRoughnessFactor = 0.0f;
      std::optional<TextureInfo> clearcoatRoughnessTexture;
      std::optional<NormalTextureInfo> clearcoatNormalTexture;
    };
    struct KHRMaterialsSpecular {
      float specularFactor = 1.0f;
      std::vector<float> specularColorFactor;
      std::optional<TextureInfo> specularTexture;
      std::optional<TextureInfo> specularColorTexture;
    };
    struct KHRMaterialsSheen {
      std::vector<float> sheenColorFactor;
      std::optional<TextureInfo> sheenColorTexture;
      float sheenRoughnessFactor = 0.0f;
      std::optional<TextureInfo> sheenRoughnessTexture;
    };
    struct KHRMaterialsDiffuseTransmission {
      float diffuseTransmissionFactor = 0.0f;
      std::vector<float> diffuseTransmissionColorFactor;
      std::optional<TextureInfo> diffuseTransmissionTexture;
      std::optional<TextureInfo> diffuseTransmissionColorTexture;
    };
    struct KHRMaterialsDispersion { float dispersion = 0.0f; };
    struct KHRMaterialsIridescence {
      float iridescenceFactor = 0.0f;
      float iridescenceIor = 1.3f;
      float iridescenceThicknessMinimum = 100.0f;
      float iridescenceThicknessMaximum = 400.0f;
      std::optional<TextureInfo> iridescenceTexture;
      std::optional<TextureInfo> iridescenceThicknessTexture;
    };
    struct KHRMaterialsAnisotropy {
      float anisotropyStrength = 0.0f;
      float anisotropyRotation = 0.0f;
      std::optional<TextureInfo> anisotropyTexture;
    };
    struct KHRMaterialsVolumeScatter {
      std::vector<float> multiscatterColor;
      float scatterAnisotropy = 0.0f;
    };
    struct KHRMaterialsUnlit {};
    struct KHRMaterialsPBRSpecularGlossiness {
      std::vector<float> diffuseFactor;
      std::optional<TextureInfo> diffuseTexture;
      std::vector<float> specularFactor;
      float glossinessFactor = 1.0f;
      std::optional<TextureInfo> specularGlossinessTexture;
    };

    std::optional<KHRMaterialsTransmission> KHR_materials_transmission;
    std::optional<KHRMaterialsEmissiveStrength> KHR_materials_emissive_strength;
    std::optional<KHRMaterialsIOR> KHR_materials_ior;
    std::optional<KHRMaterialsVolume> KHR_materials_volume;
    std::optional<KHRMaterialsClearcoat> KHR_materials_clearcoat;
    std::optional<KHRMaterialsSpecular> KHR_materials_specular;
    std::optional<KHRMaterialsSheen> KHR_materials_sheen;
    std::optional<KHRMaterialsDiffuseTransmission> KHR_materials_diffuse_transmission;
    std::optional<KHRMaterialsDispersion> KHR_materials_dispersion;
    std::optional<KHRMaterialsIridescence> KHR_materials_iridescence;
    std::optional<KHRMaterialsAnisotropy> KHR_materials_anisotropy;
    std::optional<KHRMaterialsVolumeScatter> KHR_materials_volume_scatter;
    std::optional<KHRMaterialsUnlit> KHR_materials_unlit;
    std::optional<KHRMaterialsPBRSpecularGlossiness> KHR_materials_pbrSpecularGlossiness;
  };
  std::optional<Extensions> extensions;
};

// ── Mesh / Primitive ──
// Attribute -> accessor index mapping (POSITION, NORMAL, TANGENT,
// TEXCOORD_0, TEXCOORD_1, COLOR_0, JOINTS_0, WEIGHTS_0, ...).
struct PrimitiveAttributes {
  int POSITION   = -1;
  int NORMAL     = -1;
  int TANGENT    = -1;
  int TEXCOORD_0 = -1;
  int TEXCOORD_1 = -1;
  int COLOR_0    = -1;
  int JOINTS_0   = -1;
  int WEIGHTS_0  = -1;
};

struct DracoMeshCompression {
  int bufferView = -1;
  PrimitiveAttributes attributes;
};

struct PrimitiveExtensions {
  std::optional<DracoMeshCompression> KHR_draco_mesh_compression;
};

struct Primitive {
  PrimitiveAttributes attributes;
  std::optional<int>  indices;
  std::optional<int>  material;
  int                 mode = PM_TRIANGLES;
  std::optional<PrimitiveExtensions> extensions;
};

struct Mesh {
  std::vector<Primitive> primitives;
  std::vector<float>     weights;
  std::string            name;
};

// ── Node / Scene ──
struct Node {
  std::vector<int>   children;
  std::vector<float> matrix;       // 16 if present (column-major)
  std::vector<float> translation;  // 3 if present
  std::vector<float> rotation;     // 4 (xyzw) if present
  std::vector<float> scale;        // 3 if present
  std::optional<int> mesh;
  std::optional<int> skin;
  std::optional<int> camera;
  std::vector<float> weights;
  std::string        name;
};

struct Scene {
  std::vector<int> nodes;
  std::string      name;
};

// ── Skin ──
struct Skin {
  std::optional<int> inverseBindMatrices;
  std::optional<int> skeleton;
  std::vector<int>   joints;
  std::string        name;
};

// ── Animation ──
struct AnimationSampler {
  int         input = -1;
  int         output = -1;
  std::string interpolation = "LINEAR"; // LINEAR / STEP / CUBICSPLINE
};

struct AnimationChannelTarget {
  std::optional<int> node;
  std::string        path; // translation / rotation / scale / weights
};

struct AnimationChannel {
  int                    sampler = -1;
  AnimationChannelTarget target;
};

struct Animation {
  std::vector<AnimationChannel> channels;
  std::vector<AnimationSampler> samplers;
  std::string                   name;
};

// ── Top-level document ──
struct Document {
  Asset                    asset;
  std::optional<int>       scene;
  std::vector<Scene>       scenes;
  std::vector<Node>        nodes;
  std::vector<Mesh>        meshes;
  std::vector<Accessor>    accessors;
  std::vector<BufferView>  bufferViews;
  std::vector<Buffer>      buffers;
  std::vector<Material>    materials;
  std::vector<Texture>     textures;
  std::vector<Image>       images;
  std::vector<Sampler>     samplers;
  std::vector<Skin>        skins;
  std::vector<Animation>   animations;
  std::vector<std::string> extensionsUsed;
  std::vector<std::string> extensionsRequired;

  // Resolved binary buffer payloads (Phase 2-stage data, not from JSON).
  // buffers[i].byteLength bytes long; populated by GLTFLoader.cpp.
  std::vector<std::vector<unsigned char>> _bufferData;

  // Source file path (for resolving relative URIs and naming embedded
  // textures). Not from JSON.
  std::string _sourcePath;
};

} // namespace gltf
} // namespace t850

#endif // T800_GLTF_TYPES_H
