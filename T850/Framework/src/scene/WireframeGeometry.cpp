#include <pch.h>

#include <scene/WireframeGeometry.h>

#include <video/BaseDriver.h>
#include <utils/Log.h>

#include <algorithm>

namespace t850 {
  extern Device* T8Device;

  WireframeGeometry::~WireframeGeometry() {
    Destroy();
  }

  WireframeGeometry::WireframeGeometry(WireframeGeometry&& other) noexcept {
    MoveFrom(other);
  }

  WireframeGeometry& WireframeGeometry::operator=(WireframeGeometry&& other) noexcept {
    if (this != &other) {
      Destroy();
      MoveFrom(other);
    }
    return *this;
  }

  void WireframeGeometry::MoveFrom(WireframeGeometry& other) noexcept {
    m_vb = other.m_vb;
    m_ib = other.m_ib;
    m_indexCount = other.m_indexCount;
    m_indexFormat = other.m_indexFormat;

    other.m_vb = nullptr;
    other.m_ib = nullptr;
    other.m_indexCount = 0;
    other.m_indexFormat = IndexBufferFormat::R16;
  }

  void WireframeGeometry::ReleaseVertexBuffer() {
    if (m_vb) {
      if (g_pBaseDriver) g_pBaseDriver->RetireBuffer(m_vb);
      else m_vb->release();
      m_vb = nullptr;
    }
  }

  void WireframeGeometry::ReleaseIndexBuffer() {
    if (m_ib) {
      if (g_pBaseDriver) g_pBaseDriver->RetireBuffer(m_ib);
      else m_ib->release();
      m_ib = nullptr;
    }
    m_indexCount = 0;
    m_indexFormat = IndexBufferFormat::R16;
  }

  void WireframeGeometry::Destroy() {
    ReleaseIndexBuffer();
    ReleaseVertexBuffer();
  }

  bool WireframeGeometry::CreatePositionBuffer(const float* positionsXYZW,
                                               unsigned vertexCount,
                                               BufferUsage::E usage) {
    ReleaseVertexBuffer();
    if (!T8Device || !positionsXYZW || vertexCount == 0) {
      T8_LOG_ERROR("[WireframeGeometry] position buffer creation failed: invalid input");
      return false;
    }

    BufferDesc bd;
    bd.byteWidth = static_cast<int>(sizeof(float) * 4u * vertexCount);
    bd.usage = usage;
    m_vb = static_cast<VertexBuffer*>(
        T8Device->CreateBuffer(BufferType::VERTEX, bd, const_cast<float*>(positionsXYZW)));
    if (!m_vb) {
      T8_LOG_ERROR("[WireframeGeometry] position buffer creation failed");
      return false;
    }
    return true;
  }

  bool WireframeGeometry::CreateLineIndexBuffer(const std::vector<unsigned int>& lineIndices,
                                                unsigned vertexCount) {
    return CreateLineIndexBuffer(lineIndices.data(),
                                 static_cast<unsigned>(lineIndices.size()),
                                 vertexCount);
  }

  bool WireframeGeometry::CreateLineIndexBuffer(const unsigned int* lineIndices,
                                                unsigned indexCount,
                                                unsigned vertexCount) {
    ReleaseIndexBuffer();
    if (!T8Device || !lineIndices || indexCount == 0 || vertexCount == 0) {
      T8_LOG_ERROR("[WireframeGeometry] index buffer creation failed: invalid input");
      return false;
    }

    BufferDesc bd;
    bd.usage = BufferUsage::DEFAULT;
    if (vertexCount <= 65535u) {
      std::vector<unsigned short> indices16(indexCount);
      for (unsigned i = 0; i < indexCount; ++i) {
        if (lineIndices[i] >= vertexCount) {
          T8_LOG_ERROR("[WireframeGeometry] index buffer creation failed: index %u out of %u vertices",
                       lineIndices[i], vertexCount);
          return false;
        }
        indices16[i] = static_cast<unsigned short>(lineIndices[i]);
      }
      bd.byteWidth = static_cast<int>(sizeof(unsigned short) * indices16.size());
      m_ib = static_cast<IndexBuffer*>(
          T8Device->CreateBuffer(BufferType::INDEX, bd, indices16.data()));
      m_indexFormat = IndexBufferFormat::R16;
    } else {
      for (unsigned i = 0; i < indexCount; ++i) {
        if (lineIndices[i] >= vertexCount) {
          T8_LOG_ERROR("[WireframeGeometry] index buffer creation failed: index %u out of %u vertices",
                       lineIndices[i], vertexCount);
          return false;
        }
      }
      bd.byteWidth = static_cast<int>(sizeof(unsigned int) * indexCount);
      m_ib = static_cast<IndexBuffer*>(
          T8Device->CreateBuffer(BufferType::INDEX, bd, const_cast<unsigned int*>(lineIndices)));
      m_indexFormat = IndexBufferFormat::R32;
    }

    if (!m_ib) {
      T8_LOG_ERROR("[WireframeGeometry] index buffer creation failed");
      m_indexCount = 0;
      m_indexFormat = IndexBufferFormat::R16;
      return false;
    }
    m_indexCount = indexCount;
    return true;
  }

  bool WireframeGeometry::CreateFromTriangles(const float* positionsXYZW,
                                              unsigned vertexCount,
                                              const unsigned int* triangleIndices,
                                              unsigned triangleIndexCount,
                                              BufferUsage::E usage) {
    Destroy();
    if (!positionsXYZW || vertexCount == 0 ||
        !triangleIndices || triangleIndexCount < 3) {
      T8_LOG_ERROR("[WireframeGeometry] triangle wireframe creation failed: invalid input");
      return false;
    }

    std::vector<unsigned int> lineIndices;
    lineIndices.reserve((triangleIndexCount / 3u) * 6u);
    for (unsigned t = 0; t + 2 < triangleIndexCount; t += 3) {
      const unsigned int a = triangleIndices[t + 0];
      const unsigned int b = triangleIndices[t + 1];
      const unsigned int c = triangleIndices[t + 2];
      if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
        continue;
      lineIndices.push_back(a); lineIndices.push_back(b);
      lineIndices.push_back(b); lineIndices.push_back(c);
      lineIndices.push_back(c); lineIndices.push_back(a);
    }

    if (lineIndices.empty()) {
      T8_LOG_ERROR("[WireframeGeometry] triangle wireframe creation failed: no valid triangles");
      return false;
    }

    if (!CreatePositionBuffer(positionsXYZW, vertexCount, usage)) {
      Destroy();
      return false;
    }
    if (!CreateLineIndexBuffer(lineIndices, vertexCount)) {
      Destroy();
      return false;
    }
    return true;
  }
}
