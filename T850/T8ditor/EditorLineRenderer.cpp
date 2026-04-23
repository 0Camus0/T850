/*********************************************************
* T8ditor — line renderer for editor overlays. See header.
*********************************************************/

#include "EditorLineRenderer.h"

#include <Config.h>
#include <video/BaseDriver.h>
#include <utils/Utils.h>
#include <utils/Log.h>

#include <cstdlib>
#include <string>

namespace t800 {
  extern Device*        T8Device;
  extern DeviceContext* T8DeviceContext;
}

namespace t8ditor {

EditorLineRenderer::EditorLineRenderer() = default;

EditorLineRenderer::~EditorLineRenderer() {
  Destroy();
}

bool EditorLineRenderer::Create() {
  if (m_shader && m_cb) return true;
  if (!t800::g_pBaseDriver) {
    T8_LOG_ERROR("[T8ditor] EditorLineRenderer::Create called before driver init");
    return false;
  }

  // Load source. The Assets/Shaders dir is junctioned into the editor's
  // working directory by the same post-build step DayScene uses.
  const bool useGL = (t800::g_pBaseDriver->m_currentAPI == t800::GRAPHICS_API::OPENGL);
  char* vsSrc = file2string(useGL ? "Shaders/VS_EditorLine.glsl"
                                  : "Shaders/VS_EditorLine.hlsl");
  char* fsSrc = file2string(useGL ? "Shaders/FS_EditorLine.glsl"
                                  : "Shaders/FS_EditorLine.hlsl");
  if (!vsSrc || !fsSrc) {
    T8_LOG_ERROR("[T8ditor] EditorLineRenderer: shader file load failed");
    if (vsSrc) free(vsSrc);
    if (fsSrc) free(fsSrc);
    return false;
  }

  std::string vstr(vsSrc);
  std::string fstr(fsSrc);
  free(vsSrc); free(fsSrc);

  if (useGL) {
#if defined(USING_OPENGL)
    std::string defines;
    defines += "#version 130\n\n";
    defines += "#define lowp \n\n";
    defines += "#define mediump \n\n";
    defines += "#define highp \n\n";
    vstr = defines + vstr;
    fstr = defines + fstr;
#elif defined(USING_GL_COMMON)
    std::string defines;
    defines += "#version 300 es\n\n";
    defines += "#define ES_30\n\n";
    vstr = defines + vstr;
    fstr = defines + fstr;
#endif
  }

  int shaderID = t800::g_pBaseDriver->CreateShader(vstr, fstr);
  m_shader = t800::g_pBaseDriver->GetShaderIdx(shaderID);
  if (!m_shader) {
    T8_LOG_ERROR("[T8ditor] EditorLineRenderer: shader compile failed");
    return false;
  }

  t800::BufferDesc bd;
  bd.byteWidth = sizeof(CBuffer);
  bd.usage     = t800::T8_BUFFER_USAGE::DEFAULT;
  m_cb = (t800::ConstantBuffer*)t800::T8Device->CreateBuffer(t800::T8_BUFFER_TYPE::CONSTANT, bd);
  if (!m_cb) {
    T8_LOG_ERROR("[T8ditor] EditorLineRenderer: CB create failed");
    return false;
  }

  T8_LOG_INFO("[T8ditor] EditorLineRenderer ready");
  return true;
}

void EditorLineRenderer::Destroy() {
  m_shader = nullptr; // owned by the driver's shader cache
  if (m_cb) m_cb->release();
  m_cb = nullptr;
}

void EditorLineRenderer::DrawLines(const XMATRIX44& world,
                                   const XMATRIX44& vp,
                                   const XVECTOR3&  rgba,
                                   t800::VertexBuffer* vb,
                                   t800::IndexBuffer*  ib,
                                   unsigned indexCount,
                                   unsigned vertexStride,
                                   t800::T8_IB_FORMAR::E ibFormat) {
  if (!m_shader || !m_cb || !vb || !ib || indexCount == 0) return;

  CBuffer cb;
  cb.WVP       = world * vp;
  cb.LineColor = rgba;
  cb.DepthParams = XVECTOR3(
    (m_viewW > 0) ? 1.0f / (float)m_viewW : 1.0f / 1280.0f,
    (m_viewH > 0) ? 1.0f / (float)m_viewH : 1.0f / 720.0f,
    m_farPlane,
    0.005f);  // proportional depth bias (wireDepth *= 1 - bias)

  ib->Set(*t800::T8DeviceContext, 0, ibFormat);
  vb->Set(*t800::T8DeviceContext, vertexStride, 0);

  // Set topology BEFORE shader (Vulkan bakes topology into the pipeline at Set time)
  t800::T8DeviceContext->SetPrimitiveTopology(t800::T8_TOPOLOGY::LINE_LIST);

  m_shader->Set(*t800::T8DeviceContext);
  m_cb->UpdateFromBuffer(*t800::T8DeviceContext, &cb);
  m_cb->Set(*t800::T8DeviceContext);

  // Bind depth texture AFTER shader is set (D3D12 needs active root signature)
  if (m_depthTex)
    m_depthTex->Set(*t800::T8DeviceContext, 0, "depthTex");

  t800::T8DeviceContext->DrawIndexed(indexCount, 0, 0);

  // Reset topology back to triangle list for subsequent draws (meshes, ImGui, etc.)
  t800::T8DeviceContext->SetPrimitiveTopology(t800::T8_TOPOLOGY::TRIANLE_LIST);
}

t800::VertexBuffer* EditorLineRenderer::CreatePositionVB(const float* positionsXYZW,
                                                         unsigned numVertices) {
  if (!t800::T8Device || !positionsXYZW || numVertices == 0) return nullptr;
  t800::BufferDesc bd;
  bd.byteWidth = static_cast<int>(sizeof(float) * 4 * numVertices);
  bd.usage     = t800::T8_BUFFER_USAGE::DEFAULT;
  return (t800::VertexBuffer*)t800::T8Device->CreateBuffer(
      t800::T8_BUFFER_TYPE::VERTEX, bd, const_cast<float*>(positionsXYZW));
}

t800::IndexBuffer* EditorLineRenderer::CreateIndexBuffer16(const unsigned short* indices,
                                                           unsigned numIndices) {
  if (!t800::T8Device || !indices || numIndices == 0) return nullptr;
  t800::BufferDesc bd;
  bd.byteWidth = static_cast<int>(sizeof(unsigned short) * numIndices);
  bd.usage     = t800::T8_BUFFER_USAGE::DEFAULT;
  return (t800::IndexBuffer*)t800::T8Device->CreateBuffer(
      t800::T8_BUFFER_TYPE::INDEX, bd, const_cast<unsigned short*>(indices));
}

t800::IndexBuffer* EditorLineRenderer::CreateIndexBuffer32(const unsigned int* indices,
                                                           unsigned numIndices) {
  if (!t800::T8Device || !indices || numIndices == 0) return nullptr;
  t800::BufferDesc bd;
  bd.byteWidth = static_cast<int>(sizeof(unsigned int) * numIndices);
  bd.usage     = t800::T8_BUFFER_USAGE::DEFAULT;
  return (t800::IndexBuffer*)t800::T8Device->CreateBuffer(
      t800::T8_BUFFER_TYPE::INDEX, bd, const_cast<unsigned int*>(indices));
}

} // namespace t8ditor
