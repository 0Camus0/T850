#include <pch.h>
#include <scene/MutableMeshData.h>
#include <utils/XDataBase.h>

#include <algorithm>
#include <bit>

namespace t850 {

std::unique_ptr<xF::XDataBase> BuildMeshDatabase(const MutableMeshSnapshot& snapshot, std::string* error) {
  if (!ValidateMutableMeshSnapshot(snapshot, error) || snapshot.Empty()) return nullptr;
  std::vector<bool> assigned(snapshot.indices.size() / 3, false);
  for (const auto& section : snapshot.sections) {
    if (section.firstIndex % 3 != 0) {
      if (error) *error = "Static mesh sections must start on a triangle boundary";
      return nullptr;
    }
    for (size_t triangle = section.firstIndex / 3; triangle < (section.firstIndex + section.indexCount) / 3; ++triangle) {
      if (assigned[triangle]) {
        if (error) *error = "Static mesh sections must not overlap";
        return nullptr;
      }
      assigned[triangle] = true;
    }
  }
  if (std::find(assigned.begin(), assigned.end(), false) != assigned.end()) {
    if (error) *error = "Static mesh sections must cover every triangle";
    return nullptr;
  }
  auto database = std::make_unique<xF::XDataBase>();
  auto container = std::make_unique<xF::xMeshContainer>();
  container->Geometry.resize(1);
  auto& geometry = container->Geometry.front();
  geometry.RelativeMatrix.Identity();
  geometry.VertexAttributes = xF::xMeshGeometry::HAS_POSITION | xF::xMeshGeometry::HAS_NORMAL |
      xF::xMeshGeometry::HAS_TEXCOORD0;
  geometry.VertexSize = 10 * sizeof(float);
  geometry.NumChannelsTexCoords = 1;
  geometry.NumVertices = static_cast<uint32_t>(snapshot.vertices.size());
  geometry.NumIndices = static_cast<uint32_t>(snapshot.indices.size());
  geometry.NumTriangles = geometry.NumIndices / 3;
  geometry.Indices32Bit = true;
  geometry.Triangles32.assign(snapshot.indices.begin(), snapshot.indices.end());
  geometry.MaterialList.FaceIndices.resize(geometry.NumTriangles);
  database->MeshInfo.resize(1);
  auto& finalGeometry = database->MeshInfo.front();
  finalGeometry.VertexSize = geometry.VertexSize;
  finalGeometry.NumVertex = geometry.NumVertices;
  finalGeometry.pData = new float[snapshot.vertices.size() * 10];
  finalGeometry.pDataDest = new float[snapshot.vertices.size() * 10];
  uint64_t hash = 14695981039346656037ull;
  auto hashValue = [&](uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      hash ^= (value >> shift) & 255;
      hash *= 1099511628211ull;
    }
  };
  for (size_t index = 0; index < snapshot.vertices.size(); ++index) {
    const auto& vertex = snapshot.vertices[index];
    geometry.Positions.push_back(vertex.position);
    geometry.Normals.push_back(vertex.normal);
    geometry.TexCoordinates[0].push_back(XVECTOR2(vertex.u, vertex.v));
    const float values[] = {vertex.position.x, vertex.position.y, vertex.position.z, 1.0f,
        vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0f, vertex.u, vertex.v};
    std::copy(std::begin(values), std::end(values), finalGeometry.pData + index * 10);
    std::copy(std::begin(values), std::end(values), finalGeometry.pDataDest + index * 10);
    for (float value : values) hashValue(std::bit_cast<uint32_t>(value));
  }
  for (uint32_t index : snapshot.indices) hashValue(index);
  for (const auto& section : snapshot.sections) {
    std::fill_n(geometry.MaterialList.FaceIndices.begin() + section.firstIndex / 3,
        section.indexCount / 3, section.materialIndex);
    hashValue(section.firstIndex);
    hashValue(section.indexCount);
    hashValue(section.materialIndex);
  }
  for (size_t index = 0; index < snapshot.materials.size(); ++index) {
    const auto& source = snapshot.materials[index];
    if (source.usesBaseColorTexture && source.baseColorTexture.empty()) {
      if (error) *error = "Generated static mesh texture materials require a resource path";
      return nullptr;
    }
    xF::xMaterial material;
    material.bEffects = true;
    if (!source.baseColorTexture.empty()) {
      xF::xEffectDefault texture;
      texture.NameParam = "diffuseMap";
      texture.Type = xF::xEFFECTENUM::STDX_STRINGS;
      texture.CaseString = source.baseColorTexture;
      for (unsigned char character : texture.CaseString) hashValue(character);
      hashValue(0);
      material.EffectInstance.pDefaults.push_back(std::move(texture));
    }
    auto addFloats = [&](const char* name, std::initializer_list<float> values) {
      xF::xEffectDefault effect;
      effect.NameParam = name;
      effect.Type = xF::xEFFECTENUM::STDX_FLOATS;
      effect.CaseFloat.assign(values);
      for (float value : values) hashValue(std::bit_cast<uint32_t>(value));
      material.EffectInstance.pDefaults.push_back(std::move(effect));
    };
    addFloats("diffuseColor", {source.baseColor.x, source.baseColor.y, source.baseColor.z, source.baseColor.w});
    addFloats("emissiveColor", {source.emissiveColor.x, source.emissiveColor.y, source.emissiveColor.z});
    addFloats("pbrMetallic", {source.metallic});
    addFloats("pbrRoughness", {source.roughness});
    addFloats("alphaCutoff", {source.alphaCutoff});
    auto addInteger = [&](const char* name, uint32_t value) {
      xF::xEffectDefault effect;
      effect.NameParam = name;
      effect.Type = xF::xEFFECTENUM::STDX_DWORDS;
      effect.CaseDWORD = value;
      material.EffectInstance.pDefaults.push_back(std::move(effect));
      hashValue(value);
    };
    addInteger("alphaMode", static_cast<uint32_t>(source.alphaMode));
    addInteger("doubleSided", source.doubleSided ? 1 : 0);
    material.EffectInstance.NumDefaults = static_cast<uint32_t>(material.EffectInstance.pDefaults.size());
    geometry.MaterialList.Materials.push_back(std::move(material));
    xF::xSubsetInfo subset{};
    subset.NumTris = static_cast<uint32_t>(std::count(geometry.MaterialList.FaceIndices.begin(),
        geometry.MaterialList.FaceIndices.end(), static_cast<uint32_t>(index)));
    subset.NumVertex = subset.NumTris * 3;
    subset.VertexSize = geometry.VertexSize;
    subset.VertexAttrib = geometry.VertexAttributes;
    subset.bAlignedVertex = true;
    finalGeometry.Subsets.push_back(subset);
  }
  geometry.MaterialList.NumMatProcess = static_cast<uint32_t>(snapshot.materials.size());
  database->m_name = "generated/mesh/" + std::to_string(hash);
  container->FileName = database->m_name;
  database->XMeshDataBase.push_back(container.release());
  return database;
}

}