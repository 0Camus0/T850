#pragma once
#include <vector>
#include <video/BaseDriver.h>
#include <utils/xMaths.h>

namespace t850 {
  class WireframeSphere {
  public:
    struct CBuffer {
      XMATRIX44 WVP;
    };
    struct Vert {
      float x, y, z, w;
    };

    WireframeSphere() : s(nullptr), IB(nullptr), VB(nullptr), CB(nullptr) {}
    void Create(int rings = 12, int segments = 24);
    void Draw(const XMATRIX44& vp, const XVECTOR3& center, float radius);
    void Destroy();

  private:
    ShaderBase* s;
    IndexBuffer* IB;
    VertexBuffer* VB;
    ConstantBuffer* CB;
    CBuffer constantBuff;
    std::vector<Vert> vertices;
    std::vector<unsigned short> indices;
    int indexCount;
  };
}
