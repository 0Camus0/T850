/*********************************************************
 * T850 Engine — Wireframe geometry resource
 *********************************************************/

#ifndef T850_WIREFRAME_GEOMETRY_H
#define T850_WIREFRAME_GEOMETRY_H

#include <video/BaseDriver.h>

#include <vector>

namespace t850 {

class WireframeGeometry {
public:
  WireframeGeometry() = default;
  ~WireframeGeometry();

  WireframeGeometry(const WireframeGeometry&) = delete;
  WireframeGeometry& operator=(const WireframeGeometry&) = delete;

  WireframeGeometry(WireframeGeometry&& other) noexcept;
  WireframeGeometry& operator=(WireframeGeometry&& other) noexcept;

  void Destroy();

  bool CreatePositionBuffer(const float* positionsXYZW,
                            unsigned vertexCount,
                            BufferUsage::E usage = BufferUsage::DEFAULT);
  bool CreateLineIndexBuffer(const unsigned int* lineIndices,
                             unsigned indexCount,
                             unsigned vertexCount);
  bool CreateLineIndexBuffer(const std::vector<unsigned int>& lineIndices,
                             unsigned vertexCount);
  bool CreateFromTriangles(const float* positionsXYZW,
                           unsigned vertexCount,
                           const unsigned int* triangleIndices,
                           unsigned triangleIndexCount,
                           BufferUsage::E usage = BufferUsage::DEFAULT);

  bool HasVertexBuffer() const { return m_vb != nullptr; }
  bool HasIndexBuffer() const { return m_ib != nullptr && m_indexCount > 0; }
  bool IsReady() const { return HasVertexBuffer() && HasIndexBuffer(); }

  VertexBuffer* GetVertexBuffer() const { return m_vb; }
  IndexBuffer* GetIndexBuffer() const { return m_ib; }
  unsigned GetIndexCount() const { return m_indexCount; }
  IndexBufferFormat::E GetIndexFormat() const { return m_indexFormat; }

private:
  void ReleaseVertexBuffer();
  void ReleaseIndexBuffer();
  void MoveFrom(WireframeGeometry& other) noexcept;

  VertexBuffer* m_vb = nullptr;
  IndexBuffer* m_ib = nullptr;
  unsigned m_indexCount = 0;
  IndexBufferFormat::E m_indexFormat = IndexBufferFormat::R16;
};

} // namespace t850

#endif // T850_WIREFRAME_GEOMETRY_H
