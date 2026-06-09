#pragma once

#include <physics/JoltPhysicsSystem.h>
#include <scene/LineRenderer.h>

#include <vector>

namespace t850 {

class PhysicsDebugRenderer {
public:
  bool Create();
  void Destroy();
  bool IsReady() const { return m_lineRenderer.IsReady(); }

  void SetDepthTestEnabled(bool enabled) { m_depthTest = enabled; }
  void SetDepthTexture(Texture* depthTexture) { m_depthTexture = depthTexture; }
  void SetSecondaryDepthTexture(Texture* depthTexture) { m_depthTexture2 = depthTexture; }
  void SetViewport(int width, int height) { m_viewWidth = width; m_viewHeight = height; }
  void SetFarPlane(float farPlane) { m_farPlane = farPlane; }

  void Draw(const JoltPhysicsSystem& physics, const XMATRIX44& viewProjection);
  void DrawBodies(const std::vector<PhysicsDebugBody>& bodies,
                  const XMATRIX44& viewProjection,
                  const XVECTOR3& color = XVECTOR3(0.0f, 1.0f, 0.0f, 1.0f));

private:
  void ReleaseGeometryBuffers();
  bool UploadGeometry(const std::vector<PhysicsDebugBody>& bodies);

  LineRenderer m_lineRenderer;
  VertexBuffer* m_vertexBuffer = nullptr;
  IndexBuffer* m_indexBuffer = nullptr;
  unsigned m_vertexCapacity = 0;
  unsigned m_indexCapacity = 0;
  unsigned m_indexCount = 0;
  Texture* m_depthTexture = nullptr;
  Texture* m_depthTexture2 = nullptr;
  int m_viewWidth = 1280;
  int m_viewHeight = 720;
  float m_farPlane = 1000.0f;
  bool m_depthTest = true;

  std::vector<float> m_vertices;
  std::vector<unsigned int> m_indices;
  std::vector<PhysicsDebugBody> m_bodies;
};

} // namespace t850
