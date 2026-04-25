#pragma once
#include <vector>
#include <scene/PrimitiveBase.h>
#include <utils/Spline.h>
#include <video/BaseDriver.h>
namespace t850 {
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
    void Load(const char *) {};
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