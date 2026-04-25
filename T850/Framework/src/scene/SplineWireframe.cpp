#include <pch.h>
#include <scene/SplineWireframe.h>
#include <utils/Utils.h>
namespace t850 {
  extern Device*            T8Device;
  extern DeviceContext*     T8DeviceContext;
void SplineWireframe::Create()
{
  char *vsSourceP;
  char *fsSourceP;
  if (g_pBaseDriver->UsesGLSL()) {
    vsSourceP = file2string("Shaders/VS_W.glsl");
    fsSourceP = file2string("Shaders/FS_W.glsl");
  }
  else {
    vsSourceP = file2string("Shaders/VS_W.hlsl");
    fsSourceP = file2string("Shaders/FS_W.hlsl");
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

  Camera *pActualCamera = pScProp->pCameras[0];
  constantBuff.WVP = pActualCamera->VP;
  IB->Set(*T8DeviceContext, 0, IndexBufferFormat::R16);
  VB->Set(*T8DeviceContext,sizeof(Vert),0);
  s->Set(*T8DeviceContext);
  CB->UpdateFromBuffer(*T8DeviceContext, &constantBuff.WVP[0]);
  CB->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(Topology::LINE_STRIP);
  T8DeviceContext->DrawIndexed(static_cast<unsigned>(vertices.size()), 0, 0);
}
void SplineWireframe::Destroy()
{
  if (VB) { VB->release(); VB = nullptr; }
  if (IB) { IB->release(); IB = nullptr; }
  if (CB) { CB->release(); CB = nullptr; }
}
}