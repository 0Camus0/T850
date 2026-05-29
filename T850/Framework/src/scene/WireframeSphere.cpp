#include <pch.h>
#include <scene/WireframeSphere.h>
#include <utils/Log.h>
#include <utils/Utils.h>
#include <cmath>

namespace t850 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;

void WireframeSphere::Create(int rings, int segments) {
  Destroy();
  vertices.clear();
  indices.clear();
  indexCount = 0;

  char* vsSourceP = nullptr;
  char* fsSourceP = nullptr;
  const char* vsName = nullptr;
  const char* fsName = nullptr;
  if (g_pBaseDriver->UsesGLSL()) {
    vsName = "Shaders/VS_W.glsl";
    fsName = "Shaders/FS_W.glsl";
  }
  else {
    vsName = "Shaders/VS_W.hlsl";
    fsName = "Shaders/FS_W.hlsl";
  }
  vsSourceP = file2string(vsName);
  fsSourceP = file2string(fsName);

  if (!vsSourceP || !fsSourceP) {
    T8_LOG_ERROR("[WireframeSphere] Create skipped: failed loading shader source(s) %s, %s", vsName, fsName);
    free(vsSourceP);
    free(fsSourceP);
    return;
  }

  std::string vstr = std::string(vsSourceP);
  std::string fstr = std::string(fsSourceP);

  if (g_pBaseDriver->UsesGLSL()) {
#if defined(USING_OPENGL)
    std::string Defines = "";
    Defines += "#version 130\n\n";
    Defines += "#define lowp \n\n";
    Defines += "#define mediump \n\n";
    Defines += "#define highp \n\n";
    vstr = Defines + vstr;
    fstr = Defines + fstr;
#elif defined(USING_GL_COMMON)
    std::string Defines = "";
    Defines += "#version 300 es\n\n";
    Defines += "#define ES_30\n\n";
    vstr = Defines + vstr;
    fstr = Defines + fstr;
#endif
    if (g_pBaseDriver->m_currentAPI == GraphicsApi::VULKAN) {
      std::string Defines;
      Defines += "#version 450\n\n";
      Defines += "#define ES_30\n\n";
      vstr = Defines + vstr;
      fstr = Defines + fstr;
    }
  }

  free(vsSourceP);
  free(fsSourceP);

  int shaderID = g_pBaseDriver->CreateShader(vstr, fstr);
  s = g_pBaseDriver->GetShaderIdx(shaderID);

  const float PI = 3.14159265358979323846f;

  // Generate unit sphere vertices
  for (int r = 0; r <= rings; r++) {
    float phi = PI * (float)r / (float)rings;
    float sp = sinf(phi);
    float cp = cosf(phi);
    for (int seg = 0; seg <= segments; seg++) {
      float theta = 2.0f * PI * (float)seg / (float)segments;
      float st = sinf(theta);
      float ct = cosf(theta);
      float x = sp * ct;
      float y = cp;
      float z = sp * st;
      vertices.push_back(Vert{ x, y, z, 1.0f });
    }
  }

  // Generate line indices
  // Horizontal rings (latitude lines)
  for (int r = 0; r <= rings; r++) {
    for (int seg = 0; seg < segments; seg++) {
      unsigned short a = (unsigned short)(r * (segments + 1) + seg);
      unsigned short b = (unsigned short)(r * (segments + 1) + seg + 1);
      indices.push_back(a);
      indices.push_back(b);
    }
  }
  // Vertical segments (longitude lines)
  for (int seg = 0; seg <= segments; seg++) {
    for (int r = 0; r < rings; r++) {
      unsigned short a = (unsigned short)(r * (segments + 1) + seg);
      unsigned short b = (unsigned short)((r + 1) * (segments + 1) + seg);
      indices.push_back(a);
      indices.push_back(b);
    }
  }
  indexCount = (int)indices.size();

  BufferDesc bdesc;
  bdesc.byteWidth = sizeof(CBuffer);
  bdesc.usage = BufferUsage::DEFAULT;
  CB = (ConstantBuffer*)T8Device->CreateBuffer(BufferType::CONSTANT, bdesc);

  bdesc.byteWidth = static_cast<int>(sizeof(Vert) * vertices.size());
  bdesc.usage = BufferUsage::DEFAULT;
  VB = (VertexBuffer*)T8Device->CreateBuffer(BufferType::VERTEX, bdesc, &vertices[0]);

  bdesc.byteWidth = static_cast<int>(indices.size() * sizeof(unsigned short));
  bdesc.usage = BufferUsage::DEFAULT;
  IB = (IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, bdesc, &indices[0]);
}

void WireframeSphere::Draw(const XMATRIX44& vp, const XVECTOR3& center, float radius) {
  Draw(vp, center, radius, XVECTOR3(1.0f, 0.0f, 1.0f, 1.0f));
}

void WireframeSphere::Draw(const XMATRIX44& vp, const XVECTOR3& center, float radius, const XVECTOR3& color) {
  if (!s || !IB || !VB || !CB || !T8DeviceContext)
    return;

  XMATRIX44 scale, translate, world;
  XVECTOR3 pos = center;
  XMatScaling(scale, radius, radius, radius);
  XMatTranslation(translate, pos);
  world = scale * translate;
  constantBuff.WVP = world * vp;
  constantBuff.LineColor = color;

  IB->Set(*T8DeviceContext, 0, IndexBufferFormat::R16);
  VB->Set(*T8DeviceContext, sizeof(Vert), 0);
  T8DeviceContext->SetPrimitiveTopology(Topology::LINE_LIST);
  s->Set(*T8DeviceContext);
  CB->UpdateFromBuffer(*T8DeviceContext, &constantBuff);
  CB->Set(*T8DeviceContext);
  T8DeviceContext->DrawIndexed(static_cast<unsigned>(indexCount), 0, 0);
  T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
}

void WireframeSphere::Destroy() {
  if (VB) { VB->release(); VB = nullptr; }
  if (IB) { IB->release(); IB = nullptr; }
  if (CB) { CB->release(); CB = nullptr; }
  s = nullptr;
  vertices.clear();
  indices.clear();
  indexCount = 0;
}

}
