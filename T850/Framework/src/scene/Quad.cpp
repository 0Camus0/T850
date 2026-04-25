#include <pch.h>
#include <scene/Quad.h>
#include <utils/Utils.h>
namespace t850 {

  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;
  void Quad::Init()
  {
    m_vertex[0] = { -1.0f,  1.0f, 0.0f, 1.0f,  0.0f, 0.0f };
    m_vertex[1] = { -1.0f, -1.0f, 0.0f, 1.0f,   0.0f, 1.0f };
    m_vertex[2] = { 1.0f, -1.0f, 0.0f, 1.0f,    1.0f,   1.0f };
    m_vertex[3] = { 1.0f,  1.0f, 0.0f, 1.0f,    1.0f,   0.0f };

    m_index[0] = 2;
    m_index[1] = 1;
    m_index[2] = 0;
    m_index[3] = 3;
    m_index[4] = 2;
    m_index[5] = 0;

    BufferDesc bdesc;
    bdesc.byteWidth = sizeof(Vertex) * 4;
    bdesc.usage = BufferUsage::DEFAULT;
    m_VB = (t850::VertexBuffer*)T8Device->CreateBuffer(BufferType::VERTEX, bdesc, m_vertex);


    bdesc.byteWidth = 6 * sizeof(unsigned short);
    bdesc.usage = BufferUsage::DEFAULT;
    m_IB = (t850::IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, bdesc, m_index);
  }

  void Quad::Destroy()
  {
    m_IB->release();
    m_VB->release();
  }

  void Quad::Set()
  {
    unsigned int offset = 0;
    unsigned int stride = sizeof(Vertex);
    m_VB->Set(*T8DeviceContext, stride, offset);
    m_IB->Set(*T8DeviceContext, 0, IndexBufferFormat::R16);
  }
}