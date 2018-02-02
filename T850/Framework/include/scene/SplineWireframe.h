#pragma once
#include <vector>
#include "PrimitiveBase.h"
#include "utils/T8_Spline.h"
#include "video/BaseDriver.h"
namespace t800 {
  class SplineWireframe : public PrimitiveBase {
  public:
    struct CBuffer {
      XMATRIX44 WVP;
    };
    const int POINMTS_PER_UNIT = 50;
    struct Vert {
      float x, y, z, w;
    };
    SplineWireframe() {
    }
    void Load(char *) {};
    void Create();
    void Transform(float *t);
    void Draw(float *t, float *vp);
    void Destroy();
    int shaderID;
    CBuffer constantBuff;
    Spline* m_spline;
    ShaderBase* s;
    IndexBuffer*		IB;
    VertexBuffer*		VB;
    ConstantBuffer*		CB;
    std::vector<Vert>			vertices;
    std::vector<unsigned short>	indices;
  };
}