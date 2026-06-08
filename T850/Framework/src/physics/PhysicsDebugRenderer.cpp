#include <pch.h>

#include <physics/PhysicsDebugRenderer.h>

#include <utils/Log.h>

#include <algorithm>
#include <cmath>

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;

namespace {

constexpr float kPhysicsDebugPi = 3.14159265358979323846f;
constexpr int kCapsuleSegments = 16;

XVECTOR3 TransformPoint(const XVECTOR3& point, const XMATRIX44& matrix) {
  return XVECTOR3(
      point.x * matrix.m11 + point.y * matrix.m21 + point.z * matrix.m31 + matrix.m41,
      point.x * matrix.m12 + point.y * matrix.m22 + point.z * matrix.m32 + matrix.m42,
      point.x * matrix.m13 + point.y * matrix.m23 + point.z * matrix.m33 + matrix.m43,
      1.0f);
}

void AppendLine(std::vector<float>& vertices,
                std::vector<unsigned int>& indices,
                const XVECTOR3& start,
                const XVECTOR3& end) {
  const unsigned base = static_cast<unsigned>(vertices.size() / 4);
  vertices.push_back(start.x);
  vertices.push_back(start.y);
  vertices.push_back(start.z);
  vertices.push_back(1.0f);
  vertices.push_back(end.x);
  vertices.push_back(end.y);
  vertices.push_back(end.z);
  vertices.push_back(1.0f);
  indices.push_back(base);
  indices.push_back(base + 1);
}

XVECTOR3 LocalPoint(float x, float y, float z, const XMATRIX44& world) {
  return TransformPoint(XVECTOR3(x, y, z, 1.0f), world);
}

void AppendBox(std::vector<float>& vertices,
               std::vector<unsigned int>& indices,
               const PhysicsShapeDesc& shape,
               const XMATRIX44& world) {
  const float x = shape.halfExtents.x;
  const float y = shape.halfExtents.y;
  const float z = shape.halfExtents.z;

  XVECTOR3 corners[8] = {
      LocalPoint(-x, -y, -z, world),
      LocalPoint( x, -y, -z, world),
      LocalPoint( x,  y, -z, world),
      LocalPoint(-x,  y, -z, world),
      LocalPoint(-x, -y,  z, world),
      LocalPoint( x, -y,  z, world),
      LocalPoint( x,  y,  z, world),
      LocalPoint(-x,  y,  z, world),
  };

  static constexpr int edges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };

  for (const auto& edge : edges) {
    AppendLine(vertices, indices, corners[edge[0]], corners[edge[1]]);
  }
}

void AppendCircleXZ(std::vector<float>& vertices,
                    std::vector<unsigned int>& indices,
                    const XMATRIX44& world,
                    float y,
                    float radius) {
  for (int i = 0; i < kCapsuleSegments; ++i) {
    const float a0 = (2.0f * kPhysicsDebugPi * static_cast<float>(i)) / static_cast<float>(kCapsuleSegments);
    const float a1 = (2.0f * kPhysicsDebugPi * static_cast<float>(i + 1)) / static_cast<float>(kCapsuleSegments);
    AppendLine(vertices, indices,
               LocalPoint(std::cos(a0) * radius, y, std::sin(a0) * radius, world),
               LocalPoint(std::cos(a1) * radius, y, std::sin(a1) * radius, world));
  }
}

void AppendCapsuleArc(std::vector<float>& vertices,
                      std::vector<unsigned int>& indices,
                      const XMATRIX44& world,
                      bool yzPlane,
                      float centerY,
                      float startAngle,
                      float endAngle,
                      float radius) {
  for (int i = 0; i < kCapsuleSegments / 2; ++i) {
    const float t0 = static_cast<float>(i) / static_cast<float>(kCapsuleSegments / 2);
    const float t1 = static_cast<float>(i + 1) / static_cast<float>(kCapsuleSegments / 2);
    const float a0 = startAngle + (endAngle - startAngle) * t0;
    const float a1 = startAngle + (endAngle - startAngle) * t1;
    const float c0 = std::cos(a0) * radius;
    const float s0 = std::sin(a0) * radius;
    const float c1 = std::cos(a1) * radius;
    const float s1 = std::sin(a1) * radius;

    if (yzPlane) {
      AppendLine(vertices, indices,
                 LocalPoint(0.0f, centerY + s0, c0, world),
                 LocalPoint(0.0f, centerY + s1, c1, world));
    } else {
      AppendLine(vertices, indices,
                 LocalPoint(c0, centerY + s0, 0.0f, world),
                 LocalPoint(c1, centerY + s1, 0.0f, world));
    }
  }
}

void AppendCapsule(std::vector<float>& vertices,
                   std::vector<unsigned int>& indices,
                   const PhysicsShapeDesc& shape,
                   const XMATRIX44& world) {
  const float radius = (std::max)(0.001f, shape.radius);
  const float halfHeight = (std::max)(0.001f, shape.halfHeight);

  AppendCircleXZ(vertices, indices, world, halfHeight, radius);
  AppendCircleXZ(vertices, indices, world, -halfHeight, radius);

  AppendLine(vertices, indices, LocalPoint( radius, -halfHeight, 0.0f, world), LocalPoint( radius, halfHeight, 0.0f, world));
  AppendLine(vertices, indices, LocalPoint(-radius, -halfHeight, 0.0f, world), LocalPoint(-radius, halfHeight, 0.0f, world));
  AppendLine(vertices, indices, LocalPoint(0.0f, -halfHeight,  radius, world), LocalPoint(0.0f, halfHeight,  radius, world));
  AppendLine(vertices, indices, LocalPoint(0.0f, -halfHeight, -radius, world), LocalPoint(0.0f, halfHeight, -radius, world));

  AppendCapsuleArc(vertices, indices, world, false, halfHeight, 0.0f, kPhysicsDebugPi, radius);
  AppendCapsuleArc(vertices, indices, world, false, -halfHeight, kPhysicsDebugPi, 2.0f * kPhysicsDebugPi, radius);
  AppendCapsuleArc(vertices, indices, world, true, halfHeight, 0.0f, kPhysicsDebugPi, radius);
  AppendCapsuleArc(vertices, indices, world, true, -halfHeight, kPhysicsDebugPi, 2.0f * kPhysicsDebugPi, radius);
}

void AppendSphere(std::vector<float>& vertices,
                  std::vector<unsigned int>& indices,
                  const PhysicsShapeDesc& shape,
                  const XMATRIX44& world) {
  const float radius = (std::max)(0.001f, shape.radius);
  AppendCircleXZ(vertices, indices, world, 0.0f, radius);
  for (int i = 0; i < kCapsuleSegments; ++i) {
    const float a0 = (2.0f * kPhysicsDebugPi * static_cast<float>(i)) / static_cast<float>(kCapsuleSegments);
    const float a1 = (2.0f * kPhysicsDebugPi * static_cast<float>(i + 1)) / static_cast<float>(kCapsuleSegments);
    AppendLine(vertices, indices,
               LocalPoint(std::cos(a0) * radius, std::sin(a0) * radius, 0.0f, world),
               LocalPoint(std::cos(a1) * radius, std::sin(a1) * radius, 0.0f, world));
    AppendLine(vertices, indices,
               LocalPoint(0.0f, std::sin(a0) * radius, std::cos(a0) * radius, world),
               LocalPoint(0.0f, std::sin(a1) * radius, std::cos(a1) * radius, world));
  }
}

void AppendCylinder(std::vector<float>& vertices,
                    std::vector<unsigned int>& indices,
                    const PhysicsShapeDesc& shape,
                    const XMATRIX44& world) {
  const float radius = (std::max)(0.001f, shape.radius);
  const float halfHeight = (std::max)(0.001f, shape.halfHeight);
  AppendCircleXZ(vertices, indices, world, halfHeight, radius);
  AppendCircleXZ(vertices, indices, world, -halfHeight, radius);
  for (int i = 0; i < kCapsuleSegments; i += 4) {
    const float a = (2.0f * kPhysicsDebugPi * static_cast<float>(i)) / static_cast<float>(kCapsuleSegments);
    const float x = std::cos(a) * radius;
    const float z = std::sin(a) * radius;
    AppendLine(vertices, indices,
               LocalPoint(x, -halfHeight, z, world),
               LocalPoint(x, halfHeight, z, world));
  }
}

void AppendIndexedLineMesh(std::vector<float>& vertices,
                           std::vector<unsigned int>& indices,
                           const std::vector<XVECTOR3>& meshVertices,
                           const std::vector<uint32_t>& lineIndices,
                           const XMATRIX44& world) {
  const unsigned baseVertex = static_cast<unsigned>(vertices.size() / 4);
  vertices.reserve(vertices.size() + meshVertices.size() * 4u);
  for (const XVECTOR3& meshVertex : meshVertices) {
    const XVECTOR3 worldVertex = TransformPoint(meshVertex, world);
    vertices.push_back(worldVertex.x);
    vertices.push_back(worldVertex.y);
    vertices.push_back(worldVertex.z);
    vertices.push_back(1.0f);
  }

  indices.reserve(indices.size() + lineIndices.size());
  for (std::size_t i = 0; i + 1 < lineIndices.size(); i += 2) {
    const uint32_t startIndex = lineIndices[i + 0];
    const uint32_t endIndex = lineIndices[i + 1];
    if (startIndex >= meshVertices.size() || endIndex >= meshVertices.size()) {
      continue;
    }

    indices.push_back(baseVertex + startIndex);
    indices.push_back(baseVertex + endIndex);
  }
}

} // namespace

bool PhysicsDebugRenderer::Create() {
  return m_lineRenderer.Create();
}

void PhysicsDebugRenderer::Destroy() {
  ReleaseGeometryBuffers();
  m_lineRenderer.Destroy();
}

void PhysicsDebugRenderer::ReleaseGeometryBuffers() {
  if (m_vertexBuffer) {
    m_vertexBuffer->release();
    m_vertexBuffer = nullptr;
  }
  if (m_indexBuffer) {
    m_indexBuffer->release();
    m_indexBuffer = nullptr;
  }
  m_vertexCapacity = 0;
  m_indexCapacity = 0;
  m_indexCount = 0;
}

bool PhysicsDebugRenderer::UploadGeometry(const std::vector<PhysicsDebugBody>& bodies) {
  m_vertices.clear();
  m_indices.clear();

  for (const PhysicsDebugBody& body : bodies) {
    if (body.shape.type == PhysicsShapeType::TriangleMesh &&
        body.debugVertices && body.debugLineIndices &&
        !body.debugVertices->empty() && !body.debugLineIndices->empty()) {
      AppendIndexedLineMesh(m_vertices, m_indices, *body.debugVertices, *body.debugLineIndices, body.state.worldTransform);
    } else if (body.shape.type == PhysicsShapeType::Capsule) {
      AppendCapsule(m_vertices, m_indices, body.shape, body.state.worldTransform);
    } else if (body.shape.type == PhysicsShapeType::Sphere) {
      AppendSphere(m_vertices, m_indices, body.shape, body.state.worldTransform);
    } else if (body.shape.type == PhysicsShapeType::Cylinder) {
      AppendCylinder(m_vertices, m_indices, body.shape, body.state.worldTransform);
    } else {
      AppendBox(m_vertices, m_indices, body.shape, body.state.worldTransform);
    }
  }

  const unsigned vertexCount = static_cast<unsigned>(m_vertices.size() / 4);
  const unsigned indexCount = static_cast<unsigned>(m_indices.size());
  if (vertexCount == 0 || indexCount == 0) {
    m_indexCount = 0;
    return false;
  }

  if (!m_vertexBuffer || !m_indexBuffer ||
      vertexCount > m_vertexCapacity ||
      indexCount > m_indexCapacity) {
    ReleaseGeometryBuffers();

    m_vertexBuffer = LineRenderer::CreatePositionVB(m_vertices.data(), vertexCount, BufferUsage::DINAMIC);

    BufferDesc indexDesc;
    indexDesc.byteWidth = static_cast<int>(sizeof(unsigned int) * indexCount);
    indexDesc.usage = BufferUsage::DINAMIC;
    m_indexBuffer = T8Device ? static_cast<IndexBuffer*>(T8Device->CreateBuffer(BufferType::INDEX, indexDesc, m_indices.data())) : nullptr;

    if (!m_vertexBuffer || !m_indexBuffer) {
      T8_LOG_ERROR("[PhysicsDebugRenderer] Failed to create line buffers");
      ReleaseGeometryBuffers();
      return false;
    }

    m_vertexCapacity = vertexCount;
    m_indexCapacity = indexCount;
    m_indexCount = indexCount;
    return true;
  }

  if (!T8DeviceContext) {
    return false;
  }

  m_vertices.resize(static_cast<std::size_t>(m_vertexCapacity) * 4, 0.0f);
  m_indices.resize(m_indexCapacity, 0u);
  m_vertexBuffer->UpdateFromBuffer(*T8DeviceContext, m_vertices.data());
  m_indexBuffer->UpdateFromBuffer(*T8DeviceContext, m_indices.data());
  m_indexCount = indexCount;
  return true;
}

void PhysicsDebugRenderer::Draw(const JoltPhysicsSystem& physics, const XMATRIX44& viewProjection) {
  if (!m_lineRenderer.IsReady() || !physics.IsInitialized()) {
    return;
  }

  if (!physics.GetDebugBodies(m_bodies)) {
    return;
  }
  DrawBodies(m_bodies, viewProjection);
}

void PhysicsDebugRenderer::DrawBodies(const std::vector<PhysicsDebugBody>& bodies,
                                      const XMATRIX44& viewProjection,
                                      const XVECTOR3& color) {
  if (!m_lineRenderer.IsReady()) {
    return;
  }

  if (!UploadGeometry(bodies)) {
    return;
  }

  XMATRIX44 identity;
  identity.Identity();
  m_lineRenderer.SetDepthTestEnabled(m_depthTest && m_depthTexture != nullptr);
  m_lineRenderer.SetDepthTexture(m_depthTexture);
  m_lineRenderer.SetViewport(m_viewWidth, m_viewHeight);
  m_lineRenderer.SetFarPlane(m_farPlane);
  m_lineRenderer.DrawLines(identity,
                           viewProjection,
                           color,
                           m_vertexBuffer,
                           m_indexBuffer,
                           m_indexCount,
                           sizeof(float) * 4,
                           IndexBufferFormat::R32);
}

} // namespace t850
