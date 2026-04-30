#include <pch.h>
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
 *   occlusionTexture                          → occlusionMap (STRINGS)
 *   emissiveTexture                           → emissiveMap  (STRINGS)
 *   emissiveFactor                            → emissiveColor(FLOATS)
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

#include <cmath>
#include <string>

namespace t850 {
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

uint32_t SupportedTexCoord(const Material& mat, const char* propertyName, int texCoord) {
  if (texCoord == 0 || texCoord == 1) return static_cast<uint32_t>(texCoord);
  T8_LOG_ERROR("[glTF] material '%s': %s.texCoord=%d (only UV0/UV1 supported)",
               mat.name.c_str(), propertyName, texCoord);
  return 0;
}

template <typename TextureInfoT>
int EffectiveTexCoord(const TextureInfoT& textureInfo) {
  if (textureInfo.extensions && textureInfo.extensions->KHR_texture_transform && textureInfo.extensions->KHR_texture_transform->texCoord)
    return *textureInfo.extensions->KHR_texture_transform->texCoord;
  return textureInfo.texCoord;
}

float Vec2ValueOrDefault(const std::vector<float>& values, std::size_t index, float fallback) {
  return values.size() > index ? values[index] : fallback;
}

template <typename TextureInfoT>
void AddUVTransform(xF::xMaterial& mat, const std::string& prefix, const TextureInfoT& textureInfo) {
  if (!textureInfo.extensions || !textureInfo.extensions->KHR_texture_transform)
    return;

  const TextureTransform& transform = *textureInfo.extensions->KHR_texture_transform;
  float offsetX = Vec2ValueOrDefault(transform.offset, 0, 0.0f);
  float offsetY = Vec2ValueOrDefault(transform.offset, 1, 0.0f);
  float scaleX = Vec2ValueOrDefault(transform.scale, 0, 1.0f);
  float scaleY = Vec2ValueOrDefault(transform.scale, 1, 1.0f);
  float sinRotation = std::sin(transform.rotation);
  float cosRotation = std::cos(transform.rotation);

  AddFloats(mat, prefix + "UVTransform0", {cosRotation * scaleX, -sinRotation * scaleY, offsetX, 0.0f});
  AddFloats(mat, prefix + "UVTransform1", {sinRotation * scaleX, cosRotation * scaleY, offsetY, 0.0f});
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

float DielectricF0FromIor(float ior) {
  float safeIor = ior > 0.0f ? ior : 1.5f;
  float f = (safeIor - 1.0f) / (safeIor + 1.0f);
  return f * f;
}

float MaterialIor(const Material& mat) {
  if (mat.extensions && mat.extensions->KHR_materials_ior)
    return mat.extensions->KHR_materials_ior->ior;
  return 1.5f;
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

  const auto* pbrSpecGloss =
    (m.extensions && m.extensions->KHR_materials_pbrSpecularGlossiness)
      ? &*m.extensions->KHR_materials_pbrSpecularGlossiness
      : nullptr;

  if (pbrSpecGloss) {
    const auto& sg = *pbrSpecGloss;
    if (sg.diffuseFactor.size() >= 3) {
      const auto& c = sg.diffuseFactor;
      AddFloats(outMat, "diffuseColor", {c[0], c[1], c[2], c.size() >= 4 ? c[3] : 1.0f});
      outMat.FaceColor.r = c[0];
      outMat.FaceColor.g = c[1];
      outMat.FaceColor.b = c[2];
      outMat.FaceColor.a = c.size() >= 4 ? c[3] : 1.0f;
    } else {
      AddFloats(outMat, "diffuseColor", {1.0f, 1.0f, 1.0f, 1.0f});
    }

    if (sg.diffuseTexture) {
      std::string n = ResolveTextureName(doc, sg.diffuseTexture->index);
      if (!n.empty()) AddString(outMat, "diffuseMap", n);
      AddDword(outMat, "diffuseTexCoord", SupportedTexCoord(m, "pbrSpecularGlossiness.diffuseTexture", EffectiveTexCoord(*sg.diffuseTexture)));
      AddUVTransform(outMat, "diffuse", *sg.diffuseTexture);
    }

    AddFloats(outMat, "pbrMetallic", {0.0f});
    AddFloats(outMat, "pbrRoughness", {1.0f - sg.glossinessFactor});
    if (sg.specularFactor.size() >= 3) {
      const auto& s = sg.specularFactor;
      AddFloats(outMat, "specularColor", {s[0], s[1], s[2], 1.0f});
    }
  } else if (m.pbrMetallicRoughness) {
    const auto& pbr = *m.pbrMetallicRoughness;

    if (pbr.baseColorFactor.size() >= 3) {
      const auto& c = pbr.baseColorFactor;
      AddFloats(outMat, "diffuseColor", {c[0], c[1], c[2], c.size() >= 4 ? c[3] : 1.0f});
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
      AddDword(outMat, "diffuseTexCoord", SupportedTexCoord(m, "baseColorTexture", EffectiveTexCoord(*pbr.baseColorTexture)));
      AddUVTransform(outMat, "diffuse", *pbr.baseColorTexture);
    }

    AddFloats(outMat, "pbrMetallic",  {pbr.metallicFactor});
    AddFloats(outMat, "pbrRoughness", {pbr.roughnessFactor});

    if (pbr.metallicRoughnessTexture) {
      std::string n = ResolveTextureName(doc, pbr.metallicRoughnessTexture->index);
      if (!n.empty()) AddString(outMat, "metallicMap", n);
      AddDword(outMat, "metallicTexCoord", SupportedTexCoord(m, "metallicRoughnessTexture", EffectiveTexCoord(*pbr.metallicRoughnessTexture)));
      AddUVTransform(outMat, "metallic", *pbr.metallicRoughnessTexture);
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
    AddDword(outMat, "normalTexCoord", SupportedTexCoord(m, "normalTexture", EffectiveTexCoord(*m.normalTexture)));
    AddUVTransform(outMat, "normal", *m.normalTexture);
  }
  if (m.occlusionTexture) {
    std::string n = ResolveTextureName(doc, m.occlusionTexture->index);
    if (!n.empty()) AddString(outMat, "occlusionMap", n);
    AddDword(outMat, "occlusionTexCoord", SupportedTexCoord(m, "occlusionTexture", EffectiveTexCoord(*m.occlusionTexture)));
    AddFloats(outMat, "occlusionStrength", {m.occlusionTexture->strength});
    AddUVTransform(outMat, "occlusion", *m.occlusionTexture);
  }
  if (m.emissiveTexture) {
    std::string n = ResolveTextureName(doc, m.emissiveTexture->index);
    if (!n.empty()) AddString(outMat, "emissiveMap", n);
    AddDword(outMat, "emissiveTexCoord", SupportedTexCoord(m, "emissiveTexture", EffectiveTexCoord(*m.emissiveTexture)));
    AddUVTransform(outMat, "emissive", *m.emissiveTexture);
  }
  float emissiveStrength = 1.0f;
  if (m.extensions && m.extensions->KHR_materials_emissive_strength) {
    emissiveStrength = m.extensions->KHR_materials_emissive_strength->emissiveStrength;
  }

  if (m.emissiveFactor.size() >= 3) {
    const auto& e = m.emissiveFactor;
    AddFloats(outMat, "emissiveColor", {e[0] * emissiveStrength, e[1] * emissiveStrength, e[2] * emissiveStrength});
    outMat.Emissive.r = e[0] * emissiveStrength;
    outMat.Emissive.g = e[1] * emissiveStrength;
    outMat.Emissive.b = e[2] * emissiveStrength;
    outMat.Emissive.a = 1.0f;
  }

  if (m.extensions && m.extensions->KHR_materials_transmission) {
    const auto& t = *m.extensions->KHR_materials_transmission;
    AddDword(outMat, "transmission", 1u);
    AddFloats(outMat, "transmissionFactor", {t.transmissionFactor});
    if (t.transmissionTexture) {
      std::string n = ResolveTextureName(doc, t.transmissionTexture->index);
      if (!n.empty()) AddString(outMat, "transmissionMap", n);
      AddDword(outMat, "transmissionTexCoord", SupportedTexCoord(m, "transmissionTexture", EffectiveTexCoord(*t.transmissionTexture)));
      AddUVTransform(outMat, "transmission", *t.transmissionTexture);
    }
  }
  if (m.extensions && m.extensions->KHR_materials_diffuse_transmission) {
    const auto& t = *m.extensions->KHR_materials_diffuse_transmission;
    if (t.diffuseTransmissionFactor > 0.0f) {
      AddDword(outMat, "transmission", 1u);
      AddFloats(outMat, "transmissionFactor", {t.diffuseTransmissionFactor});
    }
  }
  if (m.extensions && m.extensions->KHR_materials_ior) {
    float ior = MaterialIor(m);
    AddFloats(outMat, "ior", {ior});
    if (!pbrSpecGloss && !(m.extensions && m.extensions->KHR_materials_specular)) {
      float dielectricF0 = DielectricF0FromIor(ior);
      AddFloats(outMat, "specularColor", {dielectricF0, dielectricF0, dielectricF0, 1.0f});
    }
  }
  if (m.extensions && m.extensions->KHR_materials_clearcoat) {
    const auto& c = *m.extensions->KHR_materials_clearcoat;
    AddFloats(outMat, "clearcoatFactor", {c.clearcoatFactor});
    AddFloats(outMat, "clearcoatRoughness", {c.clearcoatRoughnessFactor});
    if (c.clearcoatTexture) {
      std::string n = ResolveTextureName(doc, c.clearcoatTexture->index);
      if (!n.empty()) AddString(outMat, "clearcoatMap", n);
      AddDword(outMat, "clearcoatTexCoord", SupportedTexCoord(m, "clearcoatTexture", EffectiveTexCoord(*c.clearcoatTexture)));
      AddUVTransform(outMat, "clearcoat", *c.clearcoatTexture);
    }
    if (c.clearcoatRoughnessTexture) {
      std::string n = ResolveTextureName(doc, c.clearcoatRoughnessTexture->index);
      if (!n.empty()) AddString(outMat, "clearcoatRoughnessMap", n);
      AddDword(outMat, "clearcoatRoughnessTexCoord", SupportedTexCoord(m, "clearcoatRoughnessTexture", EffectiveTexCoord(*c.clearcoatRoughnessTexture)));
      AddUVTransform(outMat, "clearcoatRoughness", *c.clearcoatRoughnessTexture);
    }
  }
  if (m.extensions && m.extensions->KHR_materials_specular) {
    const auto& s = *m.extensions->KHR_materials_specular;
    float dielectricF0 = DielectricF0FromIor(MaterialIor(m));
    float r = 1.0f, g = 1.0f, b = 1.0f;
    if (s.specularColorFactor.size() >= 3) {
      r = s.specularColorFactor[0];
      g = s.specularColorFactor[1];
      b = s.specularColorFactor[2];
    }
    AddFloats(outMat, "specularColor", {dielectricF0 * r, dielectricF0 * g, dielectricF0 * b, s.specularFactor});
    if (s.specularTexture) {
      std::string n = ResolveTextureName(doc, s.specularTexture->index);
      if (!n.empty()) AddString(outMat, "specularFactorMap", n);
      AddDword(outMat, "specularFactorTexCoord", SupportedTexCoord(m, "KHR_materials_specular.specularTexture", EffectiveTexCoord(*s.specularTexture)));
      AddUVTransform(outMat, "specularFactor", *s.specularTexture);
    }
    if (s.specularColorTexture) {
      std::string n = ResolveTextureName(doc, s.specularColorTexture->index);
      if (!n.empty()) AddString(outMat, "specularColorMap", n);
      AddDword(outMat, "specularColorTexCoord", SupportedTexCoord(m, "KHR_materials_specular.specularColorTexture", EffectiveTexCoord(*s.specularColorTexture)));
      AddUVTransform(outMat, "specularColor", *s.specularColorTexture);
    }
  }
  if (m.extensions && m.extensions->KHR_materials_unlit) {
    AddDword(outMat, "unlit", 1u);
  }
  if (m.extensions && m.extensions->KHR_materials_sheen) {
    const auto& s = *m.extensions->KHR_materials_sheen;
    if (s.sheenColorFactor.size() >= 3) {
      AddFloats(outMat, "sheenColor", {s.sheenColorFactor[0], s.sheenColorFactor[1], s.sheenColorFactor[2]});
    }
    AddFloats(outMat, "sheenRoughness", {s.sheenRoughnessFactor});
    if (s.sheenColorTexture) {
      std::string n = ResolveTextureName(doc, s.sheenColorTexture->index);
      if (!n.empty()) AddString(outMat, "sheenColorMap", n);
      AddDword(outMat, "sheenColorTexCoord", SupportedTexCoord(m, "sheenColorTexture", EffectiveTexCoord(*s.sheenColorTexture)));
      AddUVTransform(outMat, "sheenColor", *s.sheenColorTexture);
    }
    if (s.sheenRoughnessTexture) {
      std::string n = ResolveTextureName(doc, s.sheenRoughnessTexture->index);
      if (!n.empty()) AddString(outMat, "sheenRoughnessMap", n);
      AddDword(outMat, "sheenRoughnessTexCoord", SupportedTexCoord(m, "sheenRoughnessTexture", EffectiveTexCoord(*s.sheenRoughnessTexture)));
      AddUVTransform(outMat, "sheenRoughness", *s.sheenRoughnessTexture);
    }
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
  if (m.extensions && m.extensions->KHR_materials_sheen) {
    if (m.extensions->KHR_materials_sheen->sheenColorTexture)
      checkTex(m.extensions->KHR_materials_sheen->sheenColorTexture->index);
    if (m.extensions->KHR_materials_sheen->sheenRoughnessTexture)
      checkTex(m.extensions->KHR_materials_sheen->sheenRoughnessTexture->index);
  }
  if (m.extensions && m.extensions->KHR_materials_clearcoat) {
    const auto& c = *m.extensions->KHR_materials_clearcoat;
    if (c.clearcoatTexture)
      checkTex(c.clearcoatTexture->index);
    if (c.clearcoatRoughnessTexture)
      checkTex(c.clearcoatRoughnessTexture->index);
    if (c.clearcoatNormalTexture)
      checkTex(c.clearcoatNormalTexture->index);
  }
  if (m.extensions && m.extensions->KHR_materials_specular) {
    const auto& s = *m.extensions->KHR_materials_specular;
    if (s.specularTexture)
      checkTex(s.specularTexture->index);
    if (s.specularColorTexture)
      checkTex(s.specularColorTexture->index);
  }
  if (m.extensions && m.extensions->KHR_materials_transmission) {
    const auto& t = *m.extensions->KHR_materials_transmission;
    if (t.transmissionTexture)
      checkTex(t.transmissionTexture->index);
  }
  AddDword(outMat, "Tiled", tiled ? 1u : 0u);
}

} // namespace gltf
} // namespace t850
