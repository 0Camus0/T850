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
      XVECTOR3  LineColor;   // matches FS_W's `float4 LineColor` after WVP
    };
    struct Vert {
      float x, y, z, w;
    };

    WireframeArrow() : s(nullptr), IB(nullptr), VB(nullptr), CB(nullptr) {}
    void Create(int circleSegments = 24, int numRays = 6);
    // `color` fills FS_W's LineColor. It is part of the constant buffer because D3D and WebGPU bind
    // the FS's whole 80-byte block, and WebGPU rejects the draw outright when the buffer is the
    // 64-byte WVP-only one ("required=80 supplied=64"); GL silently skips the uniform and draws the
    // arrow in whatever colour happened to be bound.
    // `size` is the ring radius (the geometry is 1 unit across the circle and 2 units along the
    // shaft). `lengthScale` stretches ONLY the shaft, so a caller can vary the arrow's length
    // without inflating its tail: size 0.2 + lengthScale 5 is a 0.4 m ring on a 2 m shaft.
    void Draw(const XMATRIX44& vp, const XVECTOR3& position, const XVECTOR3& direction, float size,
              const XVECTOR3& color = XVECTOR3(1.0f, 1.0f, 1.0f, 1.0f), float lengthScale = 1.0f);
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
