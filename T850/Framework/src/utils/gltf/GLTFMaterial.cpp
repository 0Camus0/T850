#include "pch.h"
/*********************************************************
 * glTF 2.0 — material → xF::xMaterial conversion.
 *
 * Mapping (Phase 1 — covers what RenderMesh's ShaderKey already
 * supports). Each glTF material becomes one xMaterial whose
 * EffectInstance.pDefaults entries carry the data:
 *
 *   pbrMetallicRoughness.baseColorFactor      → diffuseColor (FLOATS)
 *   pbrMetallicRoughness.baseColorTexture     → diffuseMap   (STRINGS)
 *   pbrMetallicRoughness.metallicFactor       → pbrMetallic  (FLOATS)
 *   pbrMetallicRoughness.roughnessFactor      → pbrRoughness (FLOATS)
 *   pbrMetallicRoughness.metallicRoughnessTex → metallicMap  (STRINGS)
 *   normalTexture                             → normalMap    (STRINGS)
 *   occlusionTexture                          → occlusionMap (STRINGS, future shader)
 *   emissiveTexture                           → emissiveMap  (STRINGS, future shader)
 *   emissiveFactor                            → emissiveColor(FLOATS, future shader)
 *   alphaMode                                 → alphaMode    (DWORDS: 0 OPAQUE / 1 MASK / 2 BLEND)
 *   alphaCutoff                               → alphaCutoff  (FLOATS)
 *   doubleSided                               → doubleSided  (DWORDS)
 *
 * Note (per plan §6.5): glTF packs metallic in the B channel of the
 * combined metallic-roughness texture and roughness in G. The current
 * FS_Mesh shader reads MetallicTex.r → metallic; this is documented as
 * a follow-up phase so we still bind the texture but the visual
 * mapping may be off until the shader is updated.
 *********************************************************/

#include <utils/gltf/GLTFMaterial.h>
#include <utils/gltf/GLTFImage.h>
#include <utils/gltf/GLTFTypes.h>
#include <utils/XDataBase.h>
#include <utils/Log.h>

#include <string>

namespace t800 {
namespace gltf {

namespace {

void AddString(xF::xMaterial& mat, const std::string& key, const std::string& val) {
  xF::xEffectDefault d;
  d.Type = xF::xEFFECTENUM::STDX_STRINGS;
  d.NameParam = key;
  d.CaseString = val;
  mat.EffectInstance.pDefaults.push_back(d);
}

void AddFloats(xF::xMaterial& mat, const std::string& key,
               std::initializer_list<float> values) {
  xF::xEffectDefault d;
  d.Type = xF::xEFFECTENUM::STDX_FLOATS;
  d.NameParam = key;
  d.CaseFloat.assign(values);
  mat.EffectInstance.pDefaults.push_back(d);
}

void AddDword(xF::xMaterial& mat, const std::string& key, uint32_t v) {
  xF::xEffectDefault d;
  d.Type = xF::xEFFECTENUM::STDX_DWORDS;
  d.NameParam = key;
  d.CaseDWORD = v;
  mat.EffectInstance.pDefaults.push_back(d);
}

// Resolve `textures[texIdx].source → images[imgIdx]` to a name string
// that we put into the EffectDefault. Returns "" on failure.
std::string ResolveTextureName(const Document& doc, int texIdx) {
  if (texIdx < 0 || texIdx >= static_cast<int>(doc.textures.size())) return {};
  const Texture& tex = doc.textures[texIdx];
  if (!tex.source) return {};
  std::string name; int slot = -1;
  if (!ResolveImage(doc, *tex.source, name, slot)) return {};
  return name;
}

// glTF wrap mode constants (per spec §5.29)
constexpr int WRAP_REPEAT          = 10497;
constexpr int WRAP_CLAMP_TO_EDGE   = 33071;
constexpr int WRAP_MIRRORED_REPEAT = 33648;

// Returns true if the sampler associated with `texIdx` uses tiling
// (REPEAT or MIRRORED_REPEAT) on either axis. glTF defaults to REPEAT
// when no sampler is specified.
bool IsTextureWrapped(const Document& doc, int texIdx) {
  if (texIdx < 0 || texIdx >= static_cast<int>(doc.textures.size())) return true;
  const Texture& tex = doc.textures[texIdx];
  if (!tex.sampler) return true; // no sampler → glTF default is REPEAT
  int samplerIdx = *tex.sampler;
  if (samplerIdx < 0 || samplerIdx >= static_cast<int>(doc.samplers.size())) return true;
  const Sampler& s = doc.samplers[samplerIdx];
  return s.wrapS == WRAP_REPEAT || s.wrapS == WRAP_MIRRORED_REPEAT
      || s.wrapT == WRAP_REPEAT || s.wrapT == WRAP_MIRRORED_REPEAT;
}

} // namespace

void ConvertMaterial(const Document& doc, int materialIndex,
                     xF::xMaterial& outMat) {
  outMat = xF::xMaterial{};
  outMat.bEffects = true;

  if (materialIndex < 0 || materialIndex >= static_cast<int>(doc.materials.size())) {
    // glTF allows primitives without a material — supply sane defaults.
    AddFloats(outMat, "diffuseColor",  {0.8f, 0.8f, 0.8f});
    AddFloats(outMat, "pbrMetallic",   {0.0f});
    AddFloats(outMat, "pbrRoughness",  {0.8f});
    return;
  }
  const Material& m = doc.materials[materialIndex];
  outMat.Name = m.name;

  if (m.pbrMetallicRoughness) {
    const auto& pbr = *m.pbrMetallicRoughness;

    if (pbr.baseColorFactor.size() >= 3) {
      const auto& c = pbr.baseColorFactor;
      AddFloats(outMat, "diffuseColor", {c[0], c[1], c[2]});
      outMat.FaceColor.r = c[0];
      outMat.FaceColor.g = c[1];
      outMat.FaceColor.b = c[2];
      outMat.FaceColor.a = c.size() >= 4 ? c[3] : 1.0f;
    } else {
      AddFloats(outMat, "diffuseColor", {1.0f, 1.0f, 1.0f});
    }

    if (pbr.baseColorTexture) {
      std::string n = ResolveTextureName(doc, pbr.baseColorTexture->index);
      if (!n.empty()) AddString(outMat, "diffuseMap", n);
      if (pbr.baseColorTexture->texCoord != 0)
        T8_LOG_ERROR("[glTF] material '%s': baseColorTexture.texCoord=%d "
                     "(only UV0 supported, texture will use wrong UV set)",
                     m.name.c_str(), pbr.baseColorTexture->texCoord);
    }

    AddFloats(outMat, "pbrMetallic",  {pbr.metallicFactor});
    AddFloats(outMat, "pbrRoughness", {pbr.roughnessFactor});

    if (pbr.metallicRoughnessTexture) {
      std::string n = ResolveTextureName(doc, pbr.metallicRoughnessTexture->index);
      if (!n.empty()) AddString(outMat, "metallicMap", n);
      if (pbr.metallicRoughnessTexture->texCoord != 0)
        T8_LOG_ERROR("[glTF] material '%s': metallicRoughnessTexture.texCoord=%d "
                     "(only UV0 supported)", m.name.c_str(),
                     pbr.metallicRoughnessTexture->texCoord);
    }
  } else {
    // No PBR block — fall back to defaults.
    AddFloats(outMat, "diffuseColor", {1.0f, 1.0f, 1.0f});
    AddFloats(outMat, "pbrMetallic",  {0.0f});
    AddFloats(outMat, "pbrRoughness", {0.8f});
  }

  if (m.normalTexture) {
    std::string n = ResolveTextureName(doc, m.normalTexture->index);
    if (!n.empty()) AddString(outMat, "normalMap", n);
    if (m.normalTexture->texCoord != 0)
      T8_LOG_ERROR("[glTF] material '%s': normalTexture.texCoord=%d "
                   "(only UV0 supported)", m.name.c_str(),
                   m.normalTexture->texCoord);
  }
  if (m.occlusionTexture) {
    std::string n = ResolveTextureName(doc, m.occlusionTexture->index);
    if (!n.empty()) AddString(outMat, "occlusionMap", n);
    if (m.occlusionTexture->texCoord != 0)
      T8_LOG_ERROR("[glTF] material '%s': occlusionTexture.texCoord=%d "
                   "(only UV0 supported)", m.name.c_str(),
                   m.occlusionTexture->texCoord);
  }
  if (m.emissiveTexture) {
    std::string n = ResolveTextureName(doc, m.emissiveTexture->index);
    if (!n.empty()) AddString(outMat, "emissiveMap", n);
    if (m.emissiveTexture->texCoord != 0)
      T8_LOG_ERROR("[glTF] material '%s': emissiveTexture.texCoord=%d "
                   "(only UV0 supported)", m.name.c_str(),
                   m.emissiveTexture->texCoord);
  }
  if (m.emissiveFactor.size() >= 3) {
    const auto& e = m.emissiveFactor;
    AddFloats(outMat, "emissiveColor", {e[0], e[1], e[2]});
    outMat.Emissive.r = e[0];
    outMat.Emissive.g = e[1];
    outMat.Emissive.b = e[2];
    outMat.Emissive.a = 1.0f;
  }

  uint32_t alphaMode =
      m.alphaMode == "MASK"  ? 1u :
      m.alphaMode == "BLEND" ? 2u :
                               0u;
  AddDword(outMat, "alphaMode", alphaMode);
  if (alphaMode == 1u) AddFloats(outMat, "alphaCutoff", {m.alphaCutoff});
  AddDword(outMat, "doubleSided", m.doubleSided ? 1u : 0u);
  AddDword(outMat, "gltfTangentSpace", 1u);

  // Determine wrapping mode from glTF sampler (default is REPEAT).
  // The engine's "Tiled" flag controls GL_REPEAT vs GL_CLAMP_TO_EDGE.
  // Check all texture references; any REPEAT/MIRRORED_REPEAT → tiled.
  bool tiled = false;
  auto checkTex = [&](int texIdx) {
    if (IsTextureWrapped(doc, texIdx)) tiled = true;
  };
  if (m.pbrMetallicRoughness) {
    if (m.pbrMetallicRoughness->baseColorTexture)
      checkTex(m.pbrMetallicRoughness->baseColorTexture->index);
    if (m.pbrMetallicRoughness->metallicRoughnessTexture)
      checkTex(m.pbrMetallicRoughness->metallicRoughnessTexture->index);
  }
  if (m.normalTexture)    checkTex(m.normalTexture->index);
  if (m.occlusionTexture) checkTex(m.occlusionTexture->index);
  if (m.emissiveTexture)  checkTex(m.emissiveTexture->index);
  AddDword(outMat, "Tiled", tiled ? 1u : 0u);
}

} // namespace gltf
} // namespace t800
