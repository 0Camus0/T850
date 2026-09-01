#include <pch.h>
#include <scene/SplineWireframe.h>
#include <scene/RenderQueue.h>
#include <utils/Log.h>
#include <utils/Utils.h>
namespace t850 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;
void SplineWireframe::Create()
{
  char *vsSourceP = nullptr;
  char *fsSourceP = nullptr;
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
    T8_LOG_ERROR("[SplineWireframe] Create skipped: failed loading shader source(s) %s, %s", vsName, fsName);
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
  }

  free(vsSourceP);
  free(fsSourceP);

  shaderID = g_pBaseDriver->CreateShader(vstr, fstr);
  s = g_pBaseDriver->GetShaderIdx(shaderID);


  for (float i = 0; i < (float)m_spline->m_points.size() - 3.0f; i+= m_spline->STEP_SIZE) {
    XVECTOR3 v = m_spline->GetPoint(i);
    vertices.push_back(Vert{ v.x,v.y,v.z,1.0f });
  }
  for (std::size_t i = 0; i < vertices.size(); i++) {
    indices.push_back((unsigned short)i);
  }

  t850::BufferDesc bdesc;
  bdesc.byteWidth = sizeof(CBuffer);
  bdesc.usage = BufferUsage::DEFAULT;
  CB = (t850::ConstantBuffer*)T8Device->CreateBuffer(BufferType::CONSTANT, bdesc);

  bdesc.byteWidth = static_cast<int>(sizeof(Vert) * vertices.size());
  bdesc.usage = BufferUsage::DEFAULT;
  VB = (t850::VertexBuffer*)T8Device->CreateBuffer(BufferType::VERTEX, bdesc, &vertices[0]);


  bdesc.byteWidth = static_cast<int>(indices.size() * sizeof(unsigned short));
  bdesc.usage = BufferUsage::DEFAULT;
  IB = (t850::IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, bdesc, &indices[0]);

}
void SplineWireframe::Transform(float * t)
{
}
void SplineWireframe::Draw(float * t, float * vp)
{

  Camera *pActualCamera = pScProp->GetPrimaryCamera();
  constantBuff.WVP = pActualCamera->VP;
  MeshDrawStateTracker& tracker = MeshDrawStateTracker::Get();
  tracker.BindIndexedGeometry(*T8DeviceContext,
                              VB,
                              sizeof(Vert),
                              0,
                              IB,
                              IndexBufferFormat::R16,
                              Topology::LINE_STRIP);
  s->Set(*T8DeviceContext);
  tracker.OnShaderChanged(s);
  CB->UpdateFromBuffer(*T8DeviceContext, &constantBuff.WVP[0]);
  CB->Set(*T8DeviceContext);
  T8DeviceContext->DrawIndexed(static_cast<unsigned>(vertices.size()), 0, 0);
}
void SplineWireframe::Destroy()
{
  if (VB) { VB->release(); VB = nullptr; }
  if (IB) { IB->release(); IB = nullptr; }
  if (CB) { CB->release(); CB = nullptr; }
}
}