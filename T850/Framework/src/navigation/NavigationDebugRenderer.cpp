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
  m_uploadedVerticalOffset = -1.0f;
  m_uploadedGraphVerticalOffset = -1.0f;
  m_uploadedShapeMode = m_shapeMode;
  m_indexCount = 0;
  m_graphIndexCount = 0;
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
  m_vertexCapacity = 0;
  m_indexCapacity = 0;
  m_graphVertexCapacity = 0;
  m_graphIndexCapacity = 0;
  Invalidate();
}

bool NavMeshDebugRenderer::UploadGeometry(const NavMesh& navMesh) {
  const NavMeshBuildStats& stats = navMesh.GetStats();
  if (m_vertexBuffer && m_indexBuffer &&
      m_uploadedNavMesh == &navMesh &&
      m_uploadedPolygonCount == stats.polygonCount &&
      m_uploadedDetailTriangleCount == stats.detailTriangleCount &&
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
  m_graphVertices.clear();
  m_graphVertices.reserve(m_graphPoints.size() * 4u);
  for (const XVECTOR3& point : m_graphPoints) {
    m_graphVertices.push_back(point.x);
    m_graphVertices.push_back(point.y);
    m_graphVertices.push_back(point.z);
    m_graphVertices.push_back(1.0f);
  }

  const unsigned graphVertexCount = static_cast<unsigned>(m_graphVertices.size() / 4u);
  const unsigned graphIndexCount = static_cast<unsigned>(m_graphIndices.size());
  if (graphVertexCount == 0 || graphIndexCount == 0) {
    m_graphIndexCount = 0;
  } else if (!m_graphVertexBuffer || !m_graphIndexBuffer ||
             graphVertexCount > m_graphVertexCapacity ||
             graphIndexCount > m_graphIndexCapacity) {
    if (m_graphVertexBuffer) {
      m_graphVertexBuffer->release();
      m_graphVertexBuffer = nullptr;
    }
    if (m_graphIndexBuffer) {
      m_graphIndexBuffer->release();
      m_graphIndexBuffer = nullptr;
    }
    m_graphVertexCapacity = 0;
    m_graphIndexCapacity = 0;

    m_graphVertexBuffer = LineRenderer::CreatePositionVB(m_graphVertices.data(), graphVertexCount, BufferUsage::DINAMIC);

    BufferDesc graphIndexDesc;
    graphIndexDesc.byteWidth = static_cast<int>(sizeof(unsigned int) * graphIndexCount);
    graphIndexDesc.usage = BufferUsage::DINAMIC;
    m_graphIndexBuffer = T8Device ? static_cast<IndexBuffer*>(
        T8Device->CreateBuffer(BufferType::INDEX, graphIndexDesc, m_graphIndices.data())) : nullptr;

    if (!m_graphVertexBuffer || !m_graphIndexBuffer) {
      T8_LOG_ERROR("[NavigationDebugRenderer] Failed to create navmesh graph edge buffers");
      if (m_graphVertexBuffer) {
        m_graphVertexBuffer->release();
        m_graphVertexBuffer = nullptr;
      }
      if (m_graphIndexBuffer) {
        m_graphIndexBuffer->release();
        m_graphIndexBuffer = nullptr;
      }
      m_graphIndexCount = 0;
    } else {
      m_graphVertexCapacity = graphVertexCount;
      m_graphIndexCapacity = graphIndexCount;
      m_graphIndexCount = graphIndexCount;
    }
  } else {
    if (!T8DeviceContext) {
      return false;
    }
    m_graphVertices.resize(static_cast<std::size_t>(m_graphVertexCapacity) * 4u, 0.0f);
    m_graphIndices.resize(m_graphIndexCapacity, 0u);
    m_graphVertexBuffer->UpdateFromBuffer(*T8DeviceContext, m_graphVertices.data());
    m_graphIndexBuffer->UpdateFromBuffer(*T8DeviceContext, m_graphIndices.data());
    m_graphIndexCount = graphIndexCount;
  }

  m_uploadedNavMesh = &navMesh;
  m_uploadedPolygonCount = stats.polygonCount;
  m_uploadedDetailTriangleCount = stats.detailTriangleCount;
  m_uploadedVerticalOffset = m_verticalOffset;
  m_uploadedGraphVerticalOffset = m_graphVerticalOffset;
  m_uploadedShapeMode = m_shapeMode;
  m_indexCount = indexCount;
  T8_LOG_INFO("[NavigationDebugRenderer] Uploaded navmesh debug mode=%d magentaLines=%u graphEdges=%u verts=%u bounds=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) offsets=(%.2f,%.2f)",
              static_cast<int>(m_shapeMode),
              indexCount / 2u, m_graphIndexCount / 2u, vertexCount,
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
}

} // namespace navigation
} // namespace t850
