#pragma once

#include <scene/MutableMeshData.h>
#include <scene/PrimitiveBase.h>
#include <scene/RenderMesh.h>

#include <string>

namespace t850 {

class MutableMesh final : public PrimitiveBase {
public:
  MutableMesh();
  ~MutableMesh() override;

  MutableMesh(const MutableMesh&) = delete;
  MutableMesh& operator=(const MutableMesh&) = delete;

  bool ReplaceSnapshot(MutableMeshSnapshot snapshot, std::string* error = nullptr);
  bool Ready() const { return m_vertexBuffer && m_indexBuffer && !m_snapshot.Empty(); }
  uint64_t Version() const { return m_snapshot.version; }
  std::size_t VertexCount() const { return m_snapshot.vertices.size(); }
  std::size_t IndexCount() const { return m_snapshot.indices.size(); }
  const AABB& LocalBounds() const { return m_snapshot.localBounds; }
  const MutableMeshSnapshot& Snapshot() const { return m_snapshot; }

  void Load(const char* path) override;
  void Create() override;
  void Transform(float* transform) override;
  void Draw(float* transform, float* viewProjection) override;
  void Destroy() override;

private:
  bool CompileShaders();
  void RetireGeometryBuffers();
  void FillFrameConstants(const Camera& camera, RenderMesh::MeshFrameCBuffer& frame) const;
  void FillMaterialConstants(const MutableMeshMaterial& material,
                             RenderMesh::MeshMaterialCBuffer& constants) const;

  MutableMeshSnapshot m_snapshot;
  VertexBuffer* m_vertexBuffer = nullptr;
  IndexBuffer* m_indexBuffer = nullptr;
  ConstantBuffer* m_combinedCB = nullptr;
  ConstantBuffer* m_frameCB = nullptr;
  ConstantBuffer* m_instanceCB = nullptr;
  ConstantBuffer* m_materialCB = nullptr;
  XMATRIX44 m_transform;
  std::string m_vertexShaderSource;
  std::string m_fragmentShaderSource;
  std::string m_vertexShaderName;
  std::string m_fragmentShaderName;
  bool m_created = false;
  bool m_shadersCompiled = false;
};

} // namespace t850
