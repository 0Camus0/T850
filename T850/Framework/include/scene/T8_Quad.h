#pragma once
#include <video/BaseDriver.h>
namespace t800 {
  struct Quad {
    struct Vertex {
      float x, y,z,w, s, t;
    };
    void Init();
    void Destroy();
    void Set();

    IndexBuffer*		m_IB;
    VertexBuffer*		m_VB;
    Vertex m_vertex[4];
    unsigned short	m_index[6];
  };


}