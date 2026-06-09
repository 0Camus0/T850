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

  ReleaseLineBuffers(vertexBuffer, indexBuffer, vertexCapacity, indexCapacity, indexCount);

  vertexBuffer = LineRenderer::CreatePositionVB(vertices.data(), vertexCount, BufferUsage::DEFAULT);

  BufferDesc indexDesc;
  indexDesc.byteWidth = static_cast<int>(sizeof(unsigned int) * newIndexCount);
  indexDesc.usage = BufferUsage::DEFAULT;
  indexBuffer = T8Device ? static_cast<IndexBuffer*>(
      T8Device->CreateBuffer(BufferType::INDEX, indexDesc, indices.data())) : nullptr;

  if (!vertexBuffer || !indexBuffer) {
    T8_LOG_ERROR("[NavigationDebugRenderer] Failed to create %s line buffers", debugName);
    ReleaseLineBuffers(vertexBuffer, indexBuffer, vertexCapacity, indexCapacity, indexCount);
    return false;
  }

  vertexCapacity = vertexCount;
  indexCapacity = newIndexCount;
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
  m_uploadedAuxiliaryGeometryEnabled = m_auxiliaryGeometryEnabled;
  m_uploadedShapeMode = m_shapeMode;
  m_indexCount = 0;
  m_nodeIndexCount = 0;
  m_graphIndexCount = 0;
  m_dropLinkIndexCount = 0;
  m_jumpLinkIndexCount = 0;
  m_jumpPadLinkIndexCount = 0;
}

void NavMeshDebugRenderer::ReleaseCachedGeometry() {
  ReleaseGeometryBuffers();
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
  if (m_nodeVertexBuffer) {
    m_nodeVertexBuffer->release();
    m_nodeVertexBuffer = nullptr;
  }
  if (m_nodeIndexBuffer) {
    m_nodeIndexBuffer->release();
    m_nodeIndexBuffer = nullptr;
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
  m_nodeVertexCapacity = 0;
  m_nodeIndexCapacity = 0;
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
      m_uploadedAuxiliaryGeometryEnabled == m_auxiliaryGeometryEnabled &&
      m_uploadedShapeMode == m_shapeMode) {
    return m_shapeMode == NavMeshDebugShapeMode::Nodes ? m_nodeIndexCount > 0 : m_indexCount > 0;
  }

  const bool hasGeometry = navMesh.GetDebugWireframe(m_points, m_indices, m_verticalOffset);
  if (!hasGeometry ||
      !UploadLineBuffers("navmesh geometry",
                         m_points,
                         m_indices,
                         m_vertices,
                         m_vertexBuffer,
                         m_indexBuffer,
                         m_vertexCapacity,
                         m_indexCapacity,
                         m_indexCount)) {
    return false;
  }

  navMesh.GetDebugNodeMarkers(m_nodePoints, m_nodeIndices, m_verticalOffset);
  if (!UploadLineBuffers("navmesh nodes",
                         m_nodePoints,
                         m_nodeIndices,
                         m_nodeVertices,
                         m_nodeVertexBuffer,
                         m_nodeIndexBuffer,
                         m_nodeVertexCapacity,
                         m_nodeIndexCapacity,
                         m_nodeIndexCount)) {
      return false;
  }

  if (m_auxiliaryGeometryEnabled) {
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
  } else {
    m_graphIndexCount = 0;
    m_dropLinkIndexCount = 0;
    m_jumpLinkIndexCount = 0;
    m_jumpPadLinkIndexCount = 0;
  }

  m_uploadedNavMesh = &navMesh;
  m_uploadedPolygonCount = stats.polygonCount;
  m_uploadedDetailTriangleCount = stats.detailTriangleCount;
  m_uploadedOffMeshLinkCount = stats.offMeshLinkCount;
  m_uploadedVerticalOffset = m_verticalOffset;
  m_uploadedGraphVerticalOffset = m_graphVerticalOffset;
  m_uploadedAuxiliaryGeometryEnabled = m_auxiliaryGeometryEnabled;
  m_uploadedShapeMode = m_shapeMode;
  return true;
}

void NavMeshDebugRenderer::Draw(const NavMesh& navMesh, const XMATRIX44& viewProjection) {
  if (!m_lineRenderer.IsReady() || !navMesh.IsReady()) {
    return;
  }
  if (!UploadGeometry(navMesh)) {
    return;
  }

  if (!m_depthTexture && !m_depthTexture2) {
    return;
  }

  XMATRIX44 identity;
  identity.Identity();
  m_lineRenderer.SetDepthTestEnabled(true);
  m_lineRenderer.SetDepthTexture(m_depthTexture);
  m_lineRenderer.SetSecondaryDepthTexture(m_depthTexture2);
  m_lineRenderer.SetViewport(m_viewWidth, m_viewHeight);
  m_lineRenderer.SetFarPlane(m_farPlane);
  m_lineRenderer.SetDepthBias(0.025f);
  const XVECTOR3 navMeshColor = m_shapeMode == NavMeshDebugShapeMode::Nodes
      ? XVECTOR3(0.5f, 1.0f, 0.0f, 1.0f)
      : XVECTOR3(1.0f, 0.0f, 1.0f, 1.0f);
  const XVECTOR3 linkColor(1.0f, 1.0f, 0.0f, 1.0f);
  if (m_shapeMode == NavMeshDebugShapeMode::Nodes) {
    if (m_nodeVertexBuffer && m_nodeIndexBuffer && m_nodeIndexCount > 0) {
      m_lineRenderer.DrawLines(identity,
                               viewProjection,
                               navMeshColor,
                               m_nodeVertexBuffer,
                               m_nodeIndexBuffer,
                               m_nodeIndexCount,
                               sizeof(float) * 4,
                               IndexBufferFormat::R32);
    }
  } else {
    m_lineRenderer.DrawLines(identity,
                             viewProjection,
                             navMeshColor,
                             m_vertexBuffer,
                             m_indexBuffer,
                             m_indexCount,
                             sizeof(float) * 4,
                             IndexBufferFormat::R32);
  }
  if (m_auxiliaryGeometryEnabled && m_graphVertexBuffer && m_graphIndexBuffer && m_graphIndexCount > 0) {
    m_lineRenderer.DrawLines(identity,
                             viewProjection,
                             navMeshColor,
                             m_graphVertexBuffer,
                             m_graphIndexBuffer,
                             m_graphIndexCount,
                             sizeof(float) * 4,
                             IndexBufferFormat::R32);
  }
  if (m_auxiliaryGeometryEnabled && m_dropLinkVertexBuffer && m_dropLinkIndexBuffer && m_dropLinkIndexCount > 0) {
    m_lineRenderer.DrawLines(identity,
                             viewProjection,
                             linkColor,
                             m_dropLinkVertexBuffer,
                             m_dropLinkIndexBuffer,
                             m_dropLinkIndexCount,
                             sizeof(float) * 4,
                             IndexBufferFormat::R32);
  }
  if (m_auxiliaryGeometryEnabled && m_jumpLinkVertexBuffer && m_jumpLinkIndexBuffer && m_jumpLinkIndexCount > 0) {
    m_lineRenderer.DrawLines(identity,
                             viewProjection,
                             linkColor,
                             m_jumpLinkVertexBuffer,
                             m_jumpLinkIndexBuffer,
                             m_jumpLinkIndexCount,
                             sizeof(float) * 4,
                             IndexBufferFormat::R32);
  }
  if (m_auxiliaryGeometryEnabled && m_jumpPadLinkVertexBuffer && m_jumpPadLinkIndexBuffer && m_jumpPadLinkIndexCount > 0) {
    m_lineRenderer.DrawLines(identity,
                             viewProjection,
                             linkColor,
                             m_jumpPadLinkVertexBuffer,
                             m_jumpPadLinkIndexBuffer,
                             m_jumpPadLinkIndexCount,
                             sizeof(float) * 4,
                             IndexBufferFormat::R32);
  }
}

} // namespace navigation
} // namespace t850
