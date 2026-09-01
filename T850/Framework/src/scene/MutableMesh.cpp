#include <pch.h>

#include <scene/MutableMesh.h>

#include <core/EngineContext.h>
#include <debug/RuntimeTelemetry.h>
#include <scene/RenderQueue.h>
#include <utils/Log.h>
#include <utils/Utils.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace t850 {
namespace {

void SetIdentityUVTransforms(RenderMesh::MeshMaterialCBuffer& constants) {
  const XVECTOR3 row0(1.0f, 0.0f, 0.0f, 0.0f);
  const XVECTOR3 row1(0.0f, 1.0f, 0.0f, 0.0f);
  constants.BaseColorUVTransform0 = row0;
  constants.BaseColorUVTransform1 = row1;
  constants.NormalUVTransform0 = row0;
  constants.NormalUVTransform1 = row1;
  constants.MetallicUVTransform0 = row0;
  constants.MetallicUVTransform1 = row1;
  constants.EmissiveUVTransform0 = row0;
  constants.EmissiveUVTransform1 = row1;
  constants.SheenColorUVTransform0 = row0;
  constants.SheenColorUVTransform1 = row1;
  constants.SheenRoughnessUVTransform0 = row0;
  constants.SheenRoughnessUVTransform1 = row1;
  constants.ClearcoatUVTransform0 = row0;
  constants.ClearcoatUVTransform1 = row1;
  constants.ClearcoatRoughnessUVTransform0 = row0;
  constants.ClearcoatRoughnessUVTransform1 = row1;
  constants.OcclusionUVTransform0 = row0;
  constants.OcclusionUVTransform1 = row1;
  constants.SpecularFactorUVTransform0 = row0;
  constants.SpecularFactorUVTransform1 = row1;
  constants.SpecularColorUVTransform0 = row0;
  constants.SpecularColorUVTransform1 = row1;
  constants.TransmissionUVTransform0 = row0;
  constants.TransmissionUVTransform1 = row1;
  constants.LightmapUVTransform0 = row0;
  constants.LightmapUVTransform1 = row1;
}

void CopyInstance(RenderMesh::CBuffer& combined, const RenderMesh::MeshInstanceCBuffer& instance) {
  combined.WVP = instance.WVP;
  combined.World = instance.World;
  combined.WorldView = instance.WorldView;
}

void CopyFrame(RenderMesh::CBuffer& combined, const RenderMesh::MeshFrameCBuffer& frame) {
  combined.Light0Pos = frame.Light0Pos;
  combined.Light0Col = frame.Light0Col;
  combined.CameraPos = frame.CameraPos;
  combined.CameraInfo = frame.CameraInfo;
  combined.ParallaxSettings = frame.ParallaxSettings;
  combined.ParallaxShadowSettings = frame.ParallaxShadowSettings;
  combined.Light0Dir = frame.Light0Dir;
  for (int index = 0; index < 128; ++index) {
    combined.LightPositions[index] = frame.LightPositions[index];
    combined.LightColors[index] = frame.LightColors[index];
  }
  for (int index = 0; index < 32; ++index) combined.LightRadius[index] = frame.LightRadius[index];
}

void CopyMaterial(RenderMesh::CBuffer& combined, const RenderMesh::MeshMaterialCBuffer& material) {
  combined.AmbientColor = material.AmbientColor;
  combined.DiffuseColor = material.DiffuseColor;
  combined.SpecularColor = material.SpecularColor;
  combined.PBRParams = material.PBRParams;
  combined.Intensities = material.Intensities;
  combined.EmissiveColor = material.EmissiveColor;
  combined.AlphaParams = material.AlphaParams;
  combined.ForwardParams = material.ForwardParams;
  combined.TexCoordSets = material.TexCoordSets;
  combined.MaterialParams = material.MaterialParams;
  combined.MaterialParams2 = material.MaterialParams2;
  combined.MaterialParams3 = material.MaterialParams3;
  combined.MaterialParams4 = material.MaterialParams4;
  combined.MaterialParams5 = material.MaterialParams5;
  combined.MaterialParams6 = material.MaterialParams6;
  combined.MaterialParams7 = material.MaterialParams7;
  combined.MaterialParams8 = material.MaterialParams8;
  combined.MaterialParams9 = material.MaterialParams9;
  combined.BaseColorUVTransform0 = material.BaseColorUVTransform0;
  combined.BaseColorUVTransform1 = material.BaseColorUVTransform1;
  combined.NormalUVTransform0 = material.NormalUVTransform0;
  combined.NormalUVTransform1 = material.NormalUVTransform1;
  combined.MetallicUVTransform0 = material.MetallicUVTransform0;
  combined.MetallicUVTransform1 = material.MetallicUVTransform1;
  combined.EmissiveUVTransform0 = material.EmissiveUVTransform0;
  combined.EmissiveUVTransform1 = material.EmissiveUVTransform1;
  combined.SheenColorUVTransform0 = material.SheenColorUVTransform0;
  combined.SheenColorUVTransform1 = material.SheenColorUVTransform1;
  combined.SheenRoughnessUVTransform0 = material.SheenRoughnessUVTransform0;
  combined.SheenRoughnessUVTransform1 = material.SheenRoughnessUVTransform1;
  combined.ClearcoatUVTransform0 = material.ClearcoatUVTransform0;
  combined.ClearcoatUVTransform1 = material.ClearcoatUVTransform1;
  combined.ClearcoatRoughnessUVTransform0 = material.ClearcoatRoughnessUVTransform0;
  combined.ClearcoatRoughnessUVTransform1 = material.ClearcoatRoughnessUVTransform1;
  combined.OcclusionUVTransform0 = material.OcclusionUVTransform0;
  combined.OcclusionUVTransform1 = material.OcclusionUVTransform1;
  combined.SpecularFactorUVTransform0 = material.SpecularFactorUVTransform0;
  combined.SpecularFactorUVTransform1 = material.SpecularFactorUVTransform1;
  combined.SpecularColorUVTransform0 = material.SpecularColorUVTransform0;
  combined.SpecularColorUVTransform1 = material.SpecularColorUVTransform1;
  combined.TransmissionUVTransform0 = material.TransmissionUVTransform0;
  combined.TransmissionUVTransform1 = material.TransmissionUVTransform1;
  combined.LightmapUVTransform0 = material.LightmapUVTransform0;
  combined.LightmapUVTransform1 = material.LightmapUVTransform1;
}

bool DrawsInPass(MutableMeshAlphaMode alphaMode, uint8_t pass) {
  if (pass == PassType::FORWARD || pass == PassType::NONE) return alphaMode == MutableMeshAlphaMode::Blend;
  if (pass == PassType::GBUFFER || pass == PassType::SHADOW_MAP || pass == PassType::RADIAL_DEPTH) {
    return alphaMode != MutableMeshAlphaMode::Blend;
  }
  return false;
}

} // namespace

MutableMesh::MutableMesh() {
  m_transform.Identity();
}

MutableMesh::~MutableMesh() {
  Destroy();
}

void MutableMesh::Load(const char* path) {
  T8_LOG_ERROR("[MutableMesh] File loading is unsupported%s%s", path ? ": " : "", path ? path : "");
}

void MutableMesh::Create() {
  if (m_created) return;
  EngineContext& context = pEngineContext ? *pEngineContext : t850::GetEngineContext();
  if (!context.device || !context.driver) {
    T8_LOG_ERROR("[MutableMesh] Create requires an initialized engine context");
    return;
  }

  BufferDesc desc;
  desc.usage = BufferUsage::DEFAULT;
  desc.byteWidth = sizeof(RenderMesh::CBuffer);
  m_combinedCB = static_cast<ConstantBuffer*>(context.device->CreateBuffer(BufferType::CONSTANT, desc));
  desc.byteWidth = sizeof(RenderMesh::MeshFrameCBuffer);
  m_frameCB = static_cast<ConstantBuffer*>(context.device->CreateBuffer(BufferType::CONSTANT, desc));
  desc.byteWidth = sizeof(RenderMesh::MeshInstanceCBuffer);
  m_instanceCB = static_cast<ConstantBuffer*>(context.device->CreateBuffer(BufferType::CONSTANT, desc));
  desc.byteWidth = sizeof(RenderMesh::MeshMaterialCBuffer);
  m_materialCB = static_cast<ConstantBuffer*>(context.device->CreateBuffer(BufferType::CONSTANT, desc));

  char* vertexSource = nullptr;
  char* fragmentSource = nullptr;
  if (context.driver->UsesGLSL()) {
    vertexSource = file2string("Shaders/VS_Mesh.glsl");
    fragmentSource = file2string("Shaders/FS_Mesh.glsl");
    m_vertexShaderName = "VS_Mesh.glsl";
    m_fragmentShaderName = "FS_Mesh.glsl";
  } else {
    vertexSource = file2string("Shaders/VS_Mesh.hlsl");
    fragmentSource = file2string("Shaders/FS_Mesh.hlsl");
    m_vertexShaderName = "VS_Mesh.hlsl";
    m_fragmentShaderName = "FS_Mesh.hlsl";
  }
  if (vertexSource && fragmentSource) {
    m_vertexShaderSource = vertexSource;
    m_fragmentShaderSource = fragmentSource;
  }
  free(vertexSource);
  free(fragmentSource);

  m_created = m_combinedCB && m_frameCB && m_instanceCB && m_materialCB &&
      !m_vertexShaderSource.empty() && !m_fragmentShaderSource.empty();
  if (!m_created) T8_LOG_ERROR("[MutableMesh] Failed to create constant buffers or load shaders");
}

bool MutableMesh::CompileShaders() {
  if (m_shadersCompiled) return true;
  EngineContext& context = pEngineContext ? *pEngineContext : t850::GetEngineContext();
  if (!m_created || !context.driver) return false;

  const uint64_t layouts[] = {
      ShaderKey::HAS_NORMALS | ShaderKey::HAS_TEXCOORD0,
      ShaderKey::HAS_NORMALS | ShaderKey::HAS_TEXCOORD0 | ShaderKey::DIFFUSE_MAP};
  const uint8_t passes[] = {PassType::FORWARD, PassType::GBUFFER, PassType::SHADOW_MAP, PassType::RADIAL_DEPTH};
  for (uint64_t layout : layouts) {
    for (uint8_t pass : passes) {
      ShaderKey key(layout);
      key.setPass(pass);
      context.driver->CreateShader(
          m_vertexShaderSource, m_fragmentShaderSource, key, m_vertexShaderName, m_fragmentShaderName);
      if (!context.driver->GetShader(key)) return false;
    }
  }
  m_shadersCompiled = true;
  return true;
}

bool MutableMesh::ReplaceSnapshot(MutableMeshSnapshot snapshot, std::string* error,
                                  bool retainCpuGeometry) {
  if (!m_created) Create();
  if (!m_created || !CompileShaders()) {
    if (error) *error = "mutable mesh GPU resources are unavailable";
    return false;
  }
  if (!ValidateMutableMeshSnapshot(snapshot, error)) return false;
  if (Ready() && snapshot.version < m_snapshot.version) {
    if (error) *error = "mutable mesh snapshot is older than the committed version";
    return false;
  }

  if (snapshot.Empty()) {
    RetireGeometryBuffers();
    m_snapshot = std::move(snapshot);
    m_vertexCount = 0;
    m_indexCount = 0;
    return true;
  }

  EngineContext& context = pEngineContext ? *pEngineContext : t850::GetEngineContext();
  context.driver->BeginResourceUploadBatch();
  BufferDesc vertexDesc;
  vertexDesc.byteWidth = static_cast<int>(snapshot.vertices.size() * sizeof(MutableMeshVertex));
  vertexDesc.usage = BufferUsage::DEFAULT;
  VertexBuffer* newVertexBuffer = static_cast<VertexBuffer*>(context.device->CreateBuffer(
      BufferType::VERTEX, vertexDesc, snapshot.vertices.data()));
  BufferDesc indexDesc;
  indexDesc.byteWidth = static_cast<int>(snapshot.indices.size() * sizeof(uint32_t));
  indexDesc.usage = BufferUsage::DEFAULT;
  IndexBuffer* newIndexBuffer = static_cast<IndexBuffer*>(context.device->CreateBuffer(
      BufferType::INDEX, indexDesc, snapshot.indices.data()));
  context.driver->EndResourceUploadBatch();
  if (!newVertexBuffer || !newIndexBuffer) {
    if (newVertexBuffer) context.driver->RetireBuffer(newVertexBuffer);
    if (newIndexBuffer) context.driver->RetireBuffer(newIndexBuffer);
    if (error) *error = "mutable mesh buffer creation failed";
    return false;
  }

  RetireGeometryBuffers();
  m_vertexBuffer = newVertexBuffer;
  m_indexBuffer = newIndexBuffer;
  m_vertexCount = snapshot.vertices.size();
  m_indexCount = snapshot.indices.size();
  m_snapshot = std::move(snapshot);
  if (!retainCpuGeometry) {
    std::vector<MutableMeshVertex>().swap(m_snapshot.vertices);
    std::vector<uint32_t>().swap(m_snapshot.indices);
    std::vector<char>().swap(m_vertexBuffer->sysMemCpy);
    std::vector<char>().swap(m_indexBuffer->sysMemCpy);
  }
  RuntimeTelemetry::AddCounter("render.mutable_mesh.commits", 1.0);
  RuntimeTelemetry::AddCounter("render.mutable_mesh.upload_bytes",
      static_cast<double>(vertexDesc.byteWidth + indexDesc.byteWidth));
  return true;
}

void MutableMesh::Transform(float* transform) {
  if (transform) std::memcpy(&m_transform.m[0][0], transform, sizeof(m_transform.m));
}

void MutableMesh::FillFrameConstants(const Camera& camera, RenderMesh::MeshFrameCBuffer& frame) const {
  frame = RenderMesh::MeshFrameCBuffer{};
  frame.CameraPos = camera.Eye;
  frame.CameraInfo = XVECTOR3(camera.NPlane, camera.FPlane, camera.Fov, 0.0f);
  if (!pScProp || pScProp->Lights.empty()) return;

  frame.Light0Pos = pScProp->Lights[0].Position;
  frame.Light0Col = pScProp->Lights[0].Color;
  frame.Light0Dir = pScProp->Lights[0].Direction;
  const unsigned int activeLights = static_cast<unsigned int>((std::max)(0, pScProp->ActiveLights));
  const unsigned int availableLights = static_cast<unsigned int>((std::min<std::size_t>)(128, pScProp->Lights.size()));
  const unsigned int lightCount = (std::min)(activeLights, availableLights);
  unsigned int packed = 0;
  for (unsigned int index = 0; index < lightCount; ++index) {
    const Light& light = pScProp->Lights[index];
    if (!light.Enabled) continue;
    frame.LightPositions[packed] = light.Type == LIGHT_DIRECTIONAL
        ? XVECTOR3(light.Direction.x, light.Direction.y, light.Direction.z, 0.0f)
        : XVECTOR3(light.Position.x, light.Position.y, light.Position.z, 1.0f);
    frame.LightColors[packed] = XVECTOR3(
        light.Color.x, light.Color.y, light.Color.z,
        light.Intensity * (std::max)(0.0f, pScProp->LightIntensityScale));
    XVECTOR3& radii = frame.LightRadius[packed >> 2];
    const float radius = light.radius * (std::max)(0.0f, pScProp->LightRadiusScale);
    if ((packed & 3u) == 0u) radii.x = radius;
    else if ((packed & 3u) == 1u) radii.y = radius;
    else if ((packed & 3u) == 2u) radii.z = radius;
    else radii.w = radius;
    ++packed;
  }
  frame.CameraInfo.w = static_cast<float>(packed);
}

void MutableMesh::FillMaterialConstants(
    const MutableMeshMaterial& material, RenderMesh::MeshMaterialCBuffer& constants) const {
  constants = RenderMesh::MeshMaterialCBuffer{};
  constants.AmbientColor = pScProp ? pScProp->AmbientColor : XVECTOR3(0.1f, 0.1f, 0.1f, 1.0f);
  constants.DiffuseColor = material.baseColor;
  constants.SpecularColor = XVECTOR3(0.04f, 0.04f, 0.04f, 1.0f);
  constants.PBRParams = XVECTOR3(material.metallic, material.roughness, 0.0f, 0.0f);
  constants.Intensities = XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f);
  constants.EmissiveColor = material.emissiveColor;
  constants.AlphaParams = XVECTOR3(
      static_cast<float>(material.alphaMode), material.alphaCutoff,
      material.doubleSided ? 1.0f : 0.0f, 0.0f);
  EngineContext& context = pEngineContext ? *pEngineContext : t850::GetEngineContext();
  constants.ForwardParams = XVECTOR3(
      context.driver ? static_cast<float>(context.driver->width) : 1.0f,
      context.driver ? static_cast<float>(context.driver->height) : 1.0f,
      Textures[7] ? 1.0f : 0.0f, 1.5f);
  constants.MaterialParams = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  constants.MaterialParams2 = XVECTOR3(0.0f, 0.0f, Textures[9] ? 1.0f : 0.0f, 0.0f);
  constants.MaterialParams9 = XVECTOR3(0.0f, 1.0f, 0.0f, 1.0f);
  SetIdentityUVTransforms(constants);
}

void MutableMesh::Draw(float* transform, float* viewProjection) {
  (void)viewProjection;
  T8_TELEMETRY_SCOPE("render.mutable_mesh.draw");
  if (transform) Transform(transform);
  if (!Ready() || !pScProp) return;
  Camera* camera = pScProp->GetPrimaryCamera();
  if (!camera) return;

  if (pScProp->FrustumCullingEnabled) {
    RenderMesh::AABB bounds;
    bounds.min = m_snapshot.localBounds.vMin;
    bounds.max = m_snapshot.localBounds.vMax;
    XVECTOR3 planes[6];
    RenderMesh::ExtractFrustumPlanes(camera->VP, planes);
    if (RenderMesh::ClassifyAABBFrustum(bounds, m_transform, planes) == RenderMesh::FrustumResult::Outside) {
      RuntimeTelemetry::AddCounter("render.mutable_mesh.culled", 1.0);
      return;
    }
  }

  EngineContext& context = pEngineContext ? *pEngineContext : t850::GetEngineContext();
  if (!context.driver || !context.deviceContext) return;
  const uint8_t pass = gKey.getPass();
  RenderMesh::MeshInstanceCBuffer instance;
  instance.World = m_transform;
  instance.WVP = m_transform * camera->VP;
  instance.WorldView = m_transform * camera->View;
  RenderMesh::MeshFrameCBuffer frame;
  FillFrameConstants(*camera, frame);

  MeshDrawStateTracker& tracker = MeshDrawStateTracker::Get();
  const bool ownsScope = !tracker.InScope();
  if (ownsScope) tracker.Begin();
  tracker.BindIndexedGeometry(
      *context.deviceContext, m_vertexBuffer, sizeof(MutableMeshVertex), 0,
      m_indexBuffer, IndexBufferFormat::R32, Topology::TRIANLE_LIST);

  for (const MutableMeshSection& section : m_snapshot.sections) {
    const MutableMeshMaterial& material = m_snapshot.materials[section.materialIndex];
    if (!DrawsInPass(material.alphaMode, pass)) continue;
    ShaderKey key(ShaderKey::HAS_NORMALS | ShaderKey::HAS_TEXCOORD0);
    if (material.usesBaseColorTexture && Textures[0]) key.bits |= ShaderKey::DIFFUSE_MAP;
    key.setPass(pass == PassType::NONE ? PassType::FORWARD : pass);
    ShaderBase* shader = context.driver->GetShader(key);
    if (!shader) continue;
    const BaseDriver::FaceCulling previousCull = context.driver->m_FaceCulling;
    const bool changedCull = material.doubleSided && previousCull != BaseDriver::FRONT_AND_BACK;
    if (changedCull) context.driver->SetCullFace(BaseDriver::FRONT_AND_BACK);
    shader->Set(*context.deviceContext);
    tracker.OnShaderChanged(shader);

    RenderMesh::MeshMaterialCBuffer materialConstants;
    FillMaterialConstants(material, materialConstants);
    if (context.driver->UsesGLSL()) {
      RenderMesh::CBuffer combined{};
      CopyInstance(combined, instance);
      CopyFrame(combined, frame);
      CopyMaterial(combined, materialConstants);
      tracker.UpdateAndBindConstantBuffer(
          *context.deviceContext, m_combinedCB, 0, &combined, sizeof(combined));
    } else {
      tracker.UpdateAndBindConstantBuffer(
          *context.deviceContext, m_frameCB, 0, &frame, sizeof(frame));
      tracker.UpdateAndBindConstantBuffer(
          *context.deviceContext, m_instanceCB, 1, &instance, sizeof(instance));
      tracker.UpdateAndBindConstantBuffer(
          *context.deviceContext, m_materialCB, 2, &materialConstants, sizeof(materialConstants));
    }
    if (key.has(ShaderKey::DIFFUSE_MAP)) {
      if (tracker.ShouldBindTexture(0, Textures[0])) Textures[0]->Set(*context.deviceContext, 0, "DiffuseTex");
      Textures[0]->SetSampler(*context.deviceContext, 0);
    }
    context.deviceContext->DrawIndexed(section.indexCount, section.firstIndex, 0);
    if (changedCull) context.driver->SetCullFace(previousCull);
    RuntimeTelemetry::AddCounter("render.mutable_mesh.draw_calls", 1.0);
  }
  if (ownsScope) tracker.End();
}

void MutableMesh::RetireGeometryBuffers() {
  EngineContext& context = pEngineContext ? *pEngineContext : t850::GetEngineContext();
  if (m_vertexBuffer) {
    if (context.driver) context.driver->RetireBuffer(m_vertexBuffer);
    else m_vertexBuffer->release();
  }
  if (m_indexBuffer) {
    if (context.driver) context.driver->RetireBuffer(m_indexBuffer);
    else m_indexBuffer->release();
  }
  m_vertexBuffer = nullptr;
  m_indexBuffer = nullptr;
}

void MutableMesh::Destroy() {
  if (!m_created && !m_vertexBuffer && !m_indexBuffer) return;
  EngineContext& context = pEngineContext ? *pEngineContext : t850::GetEngineContext();
  RetireGeometryBuffers();
  auto retire = [&](Buffer*& buffer) {
    if (!buffer) return;
    if (context.driver) context.driver->RetireBuffer(buffer);
    else buffer->release();
    buffer = nullptr;
  };
  Buffer* combined = m_combinedCB;
  Buffer* frame = m_frameCB;
  Buffer* instance = m_instanceCB;
  Buffer* material = m_materialCB;
  retire(combined);
  retire(frame);
  retire(instance);
  retire(material);
  m_combinedCB = nullptr;
  m_frameCB = nullptr;
  m_instanceCB = nullptr;
  m_materialCB = nullptr;
  m_snapshot = MutableMeshSnapshot{};
  m_vertexCount = 0;
  m_indexCount = 0;
  m_vertexShaderSource.clear();
  m_fragmentShaderSource.clear();
  m_created = false;
  m_shadersCompiled = false;
}

} // namespace t850
