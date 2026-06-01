#include <pch.h>

#include <navigation/NavigationDebugRenderer.h>

#include <utils/Log.h>

#include <algorithm>

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;
}

namespace t850 {
namespace navigation {

namespace {

void ReleaseLineBuffers(VertexBuffer*& vertexBuffer,
                        IndexBuffer*& indexBuffer,
                        unsigned& vertexCapacity,
                        unsigned& indexCapacity,
                        unsigned& indexCount) {
  if (vertexBuffer) {
    vertexBuffer->release();
    vertexBuffer = nullptr;
  }
  if (indexBuffer) {
    indexBuffer->release();
    indexBuffer = nullptr;
  }
  vertexCapacity = 0;
  indexCapacity = 0;
  indexCount = 0;
}

bool UploadLineBuffers(const char* debugName,
                       const std::vector<XVECTOR3>& points,
                       std::vector<unsigned int>& indices,
                       std::vector<float>& vertices,
                       VertexBuffer*& vertexBuffer,
                       IndexBuffer*& indexBuffer,
                       unsigned& vertexCapacity,
                       unsigned& indexCapacity,
                       unsigned& indexCount) {
  vertices.clear();
  vertices.reserve(points.size() * 4u);
  for (const XVECTOR3& point : points) {
    vertices.push_back(point.x);
    vertices.push_back(point.y);
    vertices.push_back(point.z);
    vertices.push_back(1.0f);
  }

  const unsigned vertexCount = static_cast<unsigned>(vertices.size() / 4u);
  const unsigned newIndexCount = static_cast<unsigned>(indices.size());
  if (vertexCount == 0 || newIndexCount == 0) {
    indexCount = 0;
    return true;
  }

  if (!vertexBuffer || !indexBuffer ||
      vertexCount > vertexCapacity ||
      newIndexCount > indexCapacity) {
    ReleaseLineBuffers(vertexBuffer, indexBuffer, vertexCapacity, indexCapacity, indexCount);

    vertexBuffer = LineRenderer::CreatePositionVB(vertices.data(), vertexCount, BufferUsage::DINAMIC);

    BufferDesc indexDesc;
    indexDesc.byteWidth = static_cast<int>(sizeof(unsigned int) * newIndexCount);
    indexDesc.usage = BufferUsage::DINAMIC;
    indexBuffer = T8Device ? static_cast<IndexBuffer*>(
        T8Device->CreateBuffer(BufferType::INDEX, indexDesc, indices.data())) : nullptr;

    if (!vertexBuffer || !indexBuffer) {
      T8_LOG_ERROR("[NavigationDebugRenderer] Failed to create %s line buffers", debugName);
      ReleaseLineBuffers(vertexBuffer, indexBuffer, vertexCapacity, indexCapacity, indexCount);
      return false;
    }

    vertexCapacity = vertexCount;
    indexCapacity = newIndexCount;
  } else {
    if (!T8DeviceContext) {
      return false;
    }
    vertices.resize(static_cast<std::size_t>(vertexCapacity) * 4u, 0.0f);
    std::vector<unsigned int> paddedIndices = indices;
    paddedIndices.resize(indexCapacity, 0u);
    vertexBuffer->UpdateFromBuffer(*T8DeviceContext, vertices.data());
    indexBuffer->UpdateFromBuffer(*T8DeviceContext, paddedIndices.data());
  }

  indexCount = newIndexCount;
  return true;
}

} // namespace

bool NavMeshDebugRenderer::Create() {
  return m_lineRenderer.Create();
}

void NavMeshDebugRenderer::Destroy() {
  ReleaseGeometryBuffers();
  m_lineRenderer.Destroy();
}

void NavMeshDebugRenderer::Invalidate() {
  m_uploadedNavMesh = nullptr;
  m_uploadedPolygonCount = 0;
  m_uploadedDetailTriangleCount = 0;
  m_uploadedOffMeshLinkCount = 0;
  m_uploadedVerticalOffset = -1.0f;
  m_uploadedGraphVerticalOffset = -1.0f;
  m_uploadedShapeMode = m_shapeMode;
  m_indexCount = 0;
  m_graphIndexCount = 0;
  m_dropLinkIndexCount = 0;
  m_jumpLinkIndexCount = 0;
  m_jumpPadLinkIndexCount = 0;
}

void NavMeshDebugRenderer::ReleaseGeometryBuffers() {
  if (m_vertexBuffer) {
    m_vertexBuffer->release();
    m_vertexBuffer = nullptr;
  }
  if (m_indexBuffer) {
    m_indexBuffer->release();
    m_indexBuffer = nullptr;
  }
  if (m_graphVertexBuffer) {
    m_graphVertexBuffer->release();
    m_graphVertexBuffer = nullptr;
  }
  if (m_graphIndexBuffer) {
    m_graphIndexBuffer->release();
    m_graphIndexBuffer = nullptr;
  }
  if (m_dropLinkVertexBuffer) {
    m_dropLinkVertexBuffer->release();
    m_dropLinkVertexBuffer = nullptr;
  }
  if (m_dropLinkIndexBuffer) {
    m_dropLinkIndexBuffer->release();
    m_dropLinkIndexBuffer = nullptr;
  }
  if (m_jumpLinkVertexBuffer) {
    m_jumpLinkVertexBuffer->release();
    m_jumpLinkVertexBuffer = nullptr;
  }
  if (m_jumpLinkIndexBuffer) {
    m_jumpLinkIndexBuffer->release();
    m_jumpLinkIndexBuffer = nullptr;
  }
  if (m_jumpPadLinkVertexBuffer) {
    m_jumpPadLinkVertexBuffer->release();
    m_jumpPadLinkVertexBuffer = nullptr;
  }
  if (m_jumpPadLinkIndexBuffer) {
    m_jumpPadLinkIndexBuffer->release();
    m_jumpPadLinkIndexBuffer = nullptr;
  }
  m_vertexCapacity = 0;
  m_indexCapacity = 0;
  m_graphVertexCapacity = 0;
  m_graphIndexCapacity = 0;
  m_dropLinkVertexCapacity = 0;
  m_dropLinkIndexCapacity = 0;
  m_jumpLinkVertexCapacity = 0;
  m_jumpLinkIndexCapacity = 0;
  m_jumpPadLinkVertexCapacity = 0;
  m_jumpPadLinkIndexCapacity = 0;
  Invalidate();
}

bool NavMeshDebugRenderer::UploadGeometry(const NavMesh& navMesh) {
  const NavMeshBuildStats& stats = navMesh.GetStats();
  if (m_vertexBuffer && m_indexBuffer &&
      m_uploadedNavMesh == &navMesh &&
      m_uploadedPolygonCount == stats.polygonCount &&
      m_uploadedDetailTriangleCount == stats.detailTriangleCount &&
      m_uploadedOffMeshLinkCount == stats.offMeshLinkCount &&
      m_uploadedVerticalOffset == m_verticalOffset &&
      m_uploadedGraphVerticalOffset == m_graphVerticalOffset &&
      m_uploadedShapeMode == m_shapeMode) {
    return m_indexCount > 0;
  }

  const bool hasShape = (m_shapeMode == NavMeshDebugShapeMode::Nodes)
      ? navMesh.GetDebugNodeMarkers(m_points, m_indices, m_verticalOffset)
      : navMesh.GetDebugWireframe(m_points, m_indices, m_verticalOffset);
  if (!hasShape) {
    m_indexCount = 0;
    return false;
  }

  XVECTOR3 minPoint( 1.0e30f,  1.0e30f,  1.0e30f, 1.0f);
  XVECTOR3 maxPoint(-1.0e30f, -1.0e30f, -1.0e30f, 1.0f);
  for (const XVECTOR3& point : m_points) {
    minPoint.x = (std::min)(minPoint.x, point.x);
    minPoint.y = (std::min)(minPoint.y, point.y);
    minPoint.z = (std::min)(minPoint.z, point.z);
    maxPoint.x = (std::max)(maxPoint.x, point.x);
    maxPoint.y = (std::max)(maxPoint.y, point.y);
    maxPoint.z = (std::max)(maxPoint.z, point.z);
  }

  m_vertices.clear();
  m_vertices.reserve(m_points.size() * 4u);
  for (const XVECTOR3& point : m_points) {
    m_vertices.push_back(point.x);
    m_vertices.push_back(point.y);
    m_vertices.push_back(point.z);
    m_vertices.push_back(1.0f);
  }

  const unsigned vertexCount = static_cast<unsigned>(m_vertices.size() / 4u);
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
    m_indexBuffer = T8Device ? static_cast<IndexBuffer*>(
        T8Device->CreateBuffer(BufferType::INDEX, indexDesc, m_indices.data())) : nullptr;

    if (!m_vertexBuffer || !m_indexBuffer) {
      T8_LOG_ERROR("[NavigationDebugRenderer] Failed to create navmesh line buffers");
      ReleaseGeometryBuffers();
      return false;
    }

    m_vertexCapacity = vertexCount;
    m_indexCapacity = indexCount;
  } else {
    if (!T8DeviceContext) {
      return false;
    }
    m_vertices.resize(static_cast<std::size_t>(m_vertexCapacity) * 4u, 0.0f);
    m_indices.resize(m_indexCapacity, 0u);
    m_vertexBuffer->UpdateFromBuffer(*T8DeviceContext, m_vertices.data());
    m_indexBuffer->UpdateFromBuffer(*T8DeviceContext, m_indices.data());
  }

  navMesh.GetDebugGraphEdges(m_graphPoints, m_graphIndices, m_graphVerticalOffset);
  if (!UploadLineBuffers("navmesh graph edge",
                         m_graphPoints,
                         m_graphIndices,
                         m_graphVertices,
                         m_graphVertexBuffer,
                         m_graphIndexBuffer,
                         m_graphVertexCapacity,
                         m_graphIndexCapacity,
                         m_graphIndexCount)) {
    return false;
  }

  navMesh.GetDebugOffMeshLinks(NavTraversalType::Drop, m_dropLinkPoints, m_dropLinkIndices, m_graphVerticalOffset + 0.010f);
  if (!UploadLineBuffers("navmesh drop link",
                         m_dropLinkPoints,
                         m_dropLinkIndices,
                         m_dropLinkVertices,
                         m_dropLinkVertexBuffer,
                         m_dropLinkIndexBuffer,
                         m_dropLinkVertexCapacity,
                         m_dropLinkIndexCapacity,
                         m_dropLinkIndexCount)) {
    return false;
  }

  navMesh.GetDebugOffMeshLinks(NavTraversalType::Jump, m_jumpLinkPoints, m_jumpLinkIndices, m_graphVerticalOffset + 0.020f);
  if (!UploadLineBuffers("navmesh jump link",
                         m_jumpLinkPoints,
                         m_jumpLinkIndices,
                         m_jumpLinkVertices,
                         m_jumpLinkVertexBuffer,
                         m_jumpLinkIndexBuffer,
                         m_jumpLinkVertexCapacity,
                         m_jumpLinkIndexCapacity,
                         m_jumpLinkIndexCount)) {
    return false;
  }

  navMesh.GetDebugOffMeshLinks(NavTraversalType::JumpPad, m_jumpPadLinkPoints, m_jumpPadLinkIndices, m_graphVerticalOffset + 0.030f);
  if (!UploadLineBuffers("navmesh jump pad link",
                         m_jumpPadLinkPoints,
                         m_jumpPadLinkIndices,
                         m_jumpPadLinkVertices,
                         m_jumpPadLinkVertexBuffer,
                         m_jumpPadLinkIndexBuffer,
                         m_jumpPadLinkVertexCapacity,
                         m_jumpPadLinkIndexCapacity,
                         m_jumpPadLinkIndexCount)) {
    return false;
  }

  m_uploadedNavMesh = &navMesh;
  m_uploadedPolygonCount = stats.polygonCount;
  m_uploadedDetailTriangleCount = stats.detailTriangleCount;
  m_uploadedOffMeshLinkCount = stats.offMeshLinkCount;
  m_uploadedVerticalOffset = m_verticalOffset;
  m_uploadedGraphVerticalOffset = m_graphVerticalOffset;
  m_uploadedShapeMode = m_shapeMode;
  m_indexCount = indexCount;
  T8_LOG_INFO("[NavigationDebugRenderer] Uploaded navmesh debug mode=%d magentaLines=%u graphEdges=%u dropLinks=%u jumpLinks=%u jumpPadLinks=%u verts=%u bounds=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) offsets=(%.2f,%.2f)",
              static_cast<int>(m_shapeMode),
              indexCount / 2u, m_graphIndexCount / 2u,
              m_dropLinkIndexCount / 2u, m_jumpLinkIndexCount / 2u, m_jumpPadLinkIndexCount / 2u, vertexCount,
              minPoint.x, minPoint.y, minPoint.z,
              maxPoint.x, maxPoint.y, maxPoint.z,
              m_verticalOffset, m_graphVerticalOffset);
  return true;
}

void NavMeshDebugRenderer::Draw(const NavMesh& navMesh, const XMATRIX44& viewProjection) {
  if (!m_lineRenderer.IsReady() || !navMesh.IsReady()) {
    return;
  }
  if (!UploadGeometry(navMesh)) {
    return;
  }

  if (!m_depthTexture) {
    return;
  }

  XMATRIX44 identity;
  identity.Identity();
  m_lineRenderer.SetDepthTestEnabled(true);
  m_lineRenderer.SetDepthTexture(m_depthTexture);
  m_lineRenderer.SetViewport(m_viewWidth, m_viewHeight);
  m_lineRenderer.SetFarPlane(m_farPlane);
  m_lineRenderer.SetDepthBias(0.025f);
  m_lineRenderer.DrawLines(identity,
                           viewProjection,
                           XVECTOR3(1.0f, 0.0f, 1.0f, 1.0f),
                           m_vertexBuffer,
                           m_indexBuffer,
                           m_indexCount,
                           sizeof(float) * 4,
                           IndexBufferFormat::R32);
  if (m_graphVertexBuffer && m_graphIndexBuffer && m_graphIndexCount > 0) {
    m_lineRenderer.DrawLines(identity,
                             viewProjection,
                             XVECTOR3(0.0f, 1.0f, 0.05f, 1.0f),
                             m_graphVertexBuffer,
                             m_graphIndexBuffer,
                             m_graphIndexCount,
                             sizeof(float) * 4,
                             IndexBufferFormat::R32);
  }
  if (m_dropLinkVertexBuffer && m_dropLinkIndexBuffer && m_dropLinkIndexCount > 0) {
    m_lineRenderer.DrawLines(identity,
                             viewProjection,
                             XVECTOR3(1.0f, 0.55f, 0.0f, 1.0f),
                             m_dropLinkVertexBuffer,
                             m_dropLinkIndexBuffer,
                             m_dropLinkIndexCount,
                             sizeof(float) * 4,
                             IndexBufferFormat::R32);
  }
  if (m_jumpLinkVertexBuffer && m_jumpLinkIndexBuffer && m_jumpLinkIndexCount > 0) {
    m_lineRenderer.DrawLines(identity,
                             viewProjection,
                             XVECTOR3(1.0f, 1.0f, 0.0f, 1.0f),
                             m_jumpLinkVertexBuffer,
                             m_jumpLinkIndexBuffer,
                             m_jumpLinkIndexCount,
                             sizeof(float) * 4,
                             IndexBufferFormat::R32);
  }
  if (m_jumpPadLinkVertexBuffer && m_jumpPadLinkIndexBuffer && m_jumpPadLinkIndexCount > 0) {
    m_lineRenderer.DrawLines(identity,
                             viewProjection,
                             XVECTOR3(0.0f, 0.75f, 1.0f, 1.0f),
                             m_jumpPadLinkVertexBuffer,
                             m_jumpPadLinkIndexBuffer,
                             m_jumpPadLinkIndexCount,
                             sizeof(float) * 4,
                             IndexBufferFormat::R32);
  }
}

} // namespace navigation
} // namespace t850
