#pragma once
#include <vector>
#include <video/BaseDriver.h>
#include <utils/xMaths.h>

namespace t850 {
  // Wireframe directional light gizmo: circle disc + arrow rays.
  class WireframeArrow {
  public:
    struct CBuffer {
      XMATRIX44 WVP;
    };
    struct Vert {
      float x, y, z, w;
    };

    WireframeArrow() : s(nullptr), IB(nullptr), VB(nullptr), CB(nullptr) {}
    void Create(int circleSegments = 24, int numRays = 6);
    void Draw(const XMATRIX44& vp, const XVECTOR3& position, const XVECTOR3& direction, float size);
    void Destroy();

  private:
    ShaderBase* s;
    IndexBuffer* IB;
    VertexBuffer* VB;
    ConstantBuffer* CB;
    CBuffer constantBuff;
    int indexCount;
  };
}
