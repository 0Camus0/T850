#include "scene/WireframeArrow.h"
#include "utils/Utils.h"
#include <cmath>

namespace t800 {
  extern Device* T8Device;
  extern DeviceContext* T8DeviceContext;

void WireframeArrow::Create(int circleSegments, int numRays) {
  char* vsSourceP;
  char* fsSourceP;
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
    if (g_pBaseDriver->m_currentAPI == GRAPHICS_API::VULKAN) {
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

  // Build geometry in local space:
  // Circle (XZ plane at origin, radius=1), plus rays pointing along -Y (the "direction" axis).
  // When drawn, we orient -Y toward the light direction.
  const float PI = 3.14159265358979323846f;
  std::vector<Vert> vertices;
  std::vector<unsigned short> indices;

  // Circle vertices (indices 0..circleSegments-1)
  for (int i = 0; i < circleSegments; i++) {
    float theta = 2.0f * PI * (float)i / (float)circleSegments;
    float x = cosf(theta);
    float z = sinf(theta);
    vertices.push_back(Vert{ x, 0.0f, z, 1.0f });
  }
  // Circle line segments
  for (int i = 0; i < circleSegments; i++) {
    indices.push_back((unsigned short)i);
    indices.push_back((unsigned short)((i + 1) % circleSegments));
  }

  // Ray lines: from circle points evenly spaced, extending 2 units along -Y
  // Plus arrowhead chevrons at the tip
  float rayLength = 2.0f;
  float arrowSize = 0.3f;
  for (int i = 0; i < numRays; i++) {
    float theta = 2.0f * PI * (float)i / (float)numRays;
    float cx = cosf(theta);
    float cz = sinf(theta);

    // Ray start on circle
    unsigned short baseIdx = (unsigned short)vertices.size();
    vertices.push_back(Vert{ cx, 0.0f, cz, 1.0f });
    // Ray tip
    vertices.push_back(Vert{ cx, -rayLength, cz, 1.0f });
    // Arrowhead left
    vertices.push_back(Vert{ cx - arrowSize * cz, -rayLength + arrowSize, cz + arrowSize * cx, 1.0f });
    // Arrowhead right
    vertices.push_back(Vert{ cx + arrowSize * cz, -rayLength + arrowSize, cz - arrowSize * cx, 1.0f });

    // Ray shaft
    indices.push_back(baseIdx);
    indices.push_back((unsigned short)(baseIdx + 1));
    // Arrowhead left
    indices.push_back((unsigned short)(baseIdx + 1));
    indices.push_back((unsigned short)(baseIdx + 2));
    // Arrowhead right
    indices.push_back((unsigned short)(baseIdx + 1));
    indices.push_back((unsigned short)(baseIdx + 3));
  }

  indexCount = (int)indices.size();

  BufferDesc bdesc;
  bdesc.byteWidth = sizeof(CBuffer);
  bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
  CB = (ConstantBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::CONSTANT, bdesc);

  bdesc.byteWidth = static_cast<int>(sizeof(Vert) * vertices.size());
  bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
  VB = (VertexBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::VERTEX, bdesc, &vertices[0]);

  bdesc.byteWidth = static_cast<int>(indices.size() * sizeof(unsigned short));
  bdesc.usage = T8_BUFFER_USAGE::DEFAULT;
  IB = (IndexBuffer*)T8Device->CreateBuffer(T8_BUFFER_TYPE::INDEX, bdesc, &indices[0]);
}

void WireframeArrow::Draw(const XMATRIX44& vp, const XVECTOR3& position, const XVECTOR3& direction, float size) {
  // Build orientation matrix: align local -Y axis to the light direction
  // direction = normalized light direction (where light points)
  float dx = direction.x, dy = direction.y, dz = direction.z;
  float len = sqrtf(dx*dx + dy*dy + dz*dz);
  if (len < 0.0001f) return;
  dx /= len; dy /= len; dz /= len;

  // We want local -Y to map to (dx,dy,dz)
  // So local Y maps to (-dx,-dy,-dz)
  float ux, uy, uz; // local Y = -direction
  ux = -dx; uy = -dy; uz = -dz;

  // Pick a right vector not parallel to Y
  float rx, ry, rz;
  if (fabsf(uy) < 0.99f) {
    // Cross(up=(0,1,0), Y_axis)
    rx = uz;
    ry = 0.0f;
    rz = -ux;
  } else {
    // Cross(forward=(0,0,1), Y_axis)
    rx = -uy;
    ry = ux;
    rz = 0.0f;
  }
  float rlen = sqrtf(rx*rx + ry*ry + rz*rz);
  if (rlen < 0.0001f) return;
  rx /= rlen; ry /= rlen; rz /= rlen;

  // Forward = cross(Y_axis, Right)
  float fx = uy * rz - uz * ry;
  float fy = uz * rx - ux * rz;
  float fz = ux * ry - uy * rx;

  // Build world = scale * rotation * translation
  // rotation columns: X=right, Y=up(-dir), Z=forward
  XMATRIX44 world;
  world.m[0][0] = rx * size; world.m[0][1] = ux * size; world.m[0][2] = fx * size; world.m[0][3] = 0.0f;
  world.m[1][0] = ry * size; world.m[1][1] = uy * size; world.m[1][2] = fy * size; world.m[1][3] = 0.0f;
  world.m[2][0] = rz * size; world.m[2][1] = uz * size; world.m[2][2] = fz * size; world.m[2][3] = 0.0f;
  world.m[3][0] = position.x; world.m[3][1] = position.y; world.m[3][2] = position.z; world.m[3][3] = 1.0f;

  constantBuff.WVP = world * vp;

  IB->Set(*T8DeviceContext, 0, T8_IB_FORMAR::R16);
  VB->Set(*T8DeviceContext, sizeof(Vert), 0);
  s->Set(*T8DeviceContext);
  CB->UpdateFromBuffer(*T8DeviceContext, &constantBuff.WVP[0]);
  CB->Set(*T8DeviceContext);
  T8DeviceContext->SetPrimitiveTopology(T8_TOPOLOGY::LINE_LIST);
  T8DeviceContext->DrawIndexed(static_cast<unsigned>(indexCount), 0, 0);
}

void WireframeArrow::Destroy() {
  if (VB) { VB->release(); VB = nullptr; }
  if (IB) { IB->release(); IB = nullptr; }
}

} // namespace t800
