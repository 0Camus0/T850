#pragma once

#include <navigation/NavigationSystem.h>
#include <scene/LineRenderer.h>

#include <vector>

namespace t850 {
namespace navigation {

enum class NavMeshDebugShapeMode {
  Geometry = 0,
  Nodes = 1
};

class NavMeshDebugRenderer {
public:
  bool Create();
  void Destroy();
  bool IsReady() const { return m_lineRenderer.IsReady(); }

  void Invalidate();
  void SetDepthTexture(Texture* depthTexture) { m_depthTexture = depthTexture; }
  void SetViewport(int width, int height) { m_viewWidth = width; m_viewHeight = height; }
  void SetFarPlane(float farPlane) { m_farPlane = farPlane; }
  void SetVerticalOffset(float offset) { m_verticalOffset = offset; }
  void SetGraphVerticalOffset(float offset) { m_graphVerticalOffset = offset; }
  void SetShapeMode(NavMeshDebugShapeMode mode) { m_shapeMode = mode; }

  void Draw(const NavMesh& navMesh, const XMATRIX44& viewProjection);

private:
  void ReleaseGeometryBuffers();
  bool UploadGeometry(const NavMesh& navMesh);

  LineRenderer m_lineRenderer;
  VertexBuffer* m_vertexBuffer = nullptr;
  IndexBuffer* m_indexBuffer = nullptr;
  VertexBuffer* m_graphVertexBuffer = nullptr;
  IndexBuffer* m_graphIndexBuffer = nullptr;
  VertexBuffer* m_dropLinkVertexBuffer = nullptr;
  IndexBuffer* m_dropLinkIndexBuffer = nullptr;
  VertexBuffer* m_jumpLinkVertexBuffer = nullptr;
  IndexBuffer* m_jumpLinkIndexBuffer = nullptr;
  VertexBuffer* m_jumpPadLinkVertexBuffer = nullptr;
  IndexBuffer* m_jumpPadLinkIndexBuffer = nullptr;
  unsigned m_vertexCapacity = 0;
  unsigned m_indexCapacity = 0;
  unsigned m_graphVertexCapacity = 0;
  unsigned m_graphIndexCapacity = 0;
  unsigned m_dropLinkVertexCapacity = 0;
  unsigned m_dropLinkIndexCapacity = 0;
  unsigned m_jumpLinkVertexCapacity = 0;
  unsigned m_jumpLinkIndexCapacity = 0;
  unsigned m_jumpPadLinkVertexCapacity = 0;
  unsigned m_jumpPadLinkIndexCapacity = 0;
  unsigned m_indexCount = 0;
  unsigned m_graphIndexCount = 0;
  unsigned m_dropLinkIndexCount = 0;
  unsigned m_jumpLinkIndexCount = 0;
  unsigned m_jumpPadLinkIndexCount = 0;
  Texture* m_depthTexture = nullptr;
  int m_viewWidth = 1280;
  int m_viewHeight = 720;
  float m_farPlane = 1000.0f;
  float m_verticalOffset = 0.01f;
  float m_graphVerticalOffset = 0.015f;
  const NavMesh* m_uploadedNavMesh = nullptr;
  int m_uploadedPolygonCount = 0;
  int m_uploadedDetailTriangleCount = 0;
  int m_uploadedOffMeshLinkCount = 0;
  float m_uploadedVerticalOffset = -1.0f;
  float m_uploadedGraphVerticalOffset = -1.0f;
  NavMeshDebugShapeMode m_shapeMode = NavMeshDebugShapeMode::Geometry;
  NavMeshDebugShapeMode m_uploadedShapeMode = NavMeshDebugShapeMode::Geometry;

  std::vector<XVECTOR3> m_points;
  std::vector<unsigned int> m_indices;
  std::vector<float> m_vertices;
  std::vector<XVECTOR3> m_graphPoints;
  std::vector<unsigned int> m_graphIndices;
  std::vector<float> m_graphVertices;
  std::vector<XVECTOR3> m_dropLinkPoints;
  std::vector<unsigned int> m_dropLinkIndices;
  std::vector<float> m_dropLinkVertices;
  std::vector<XVECTOR3> m_jumpLinkPoints;
  std::vector<unsigned int> m_jumpLinkIndices;
  std::vector<float> m_jumpLinkVertices;
  std::vector<XVECTOR3> m_jumpPadLinkPoints;
  std::vector<unsigned int> m_jumpPadLinkIndices;
  std::vector<float> m_jumpPadLinkVertices;
};

} // namespace navigation
} // namespace t850
