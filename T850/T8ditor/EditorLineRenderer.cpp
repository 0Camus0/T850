/*********************************************************
* T8ditor — line renderer for editor overlays. See header.
*********************************************************/

#include "EditorLineRenderer.h"

#include <Config.h>
#include <scene/RenderQueue.h>
#include <video/BaseDriver.h>
#include <utils/Utils.h>
#include <utils/Log.h>

#include <cstdlib>
#include <string>

namespace t850 {
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
  if (!t850::g_pBaseDriver) {
    T8_LOG_ERROR("[T8ditor] EditorLineRenderer::Create called before driver init");
    return false;
  }

  // Load source. The Assets/Shaders dir is junctioned into the editor's
  // working directory by the same post-build step DayScene uses.
  const bool useGL = t850::g_pBaseDriver->UsesGLSL();
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
    std::string defines;
#if defined(USING_OPENGL)
    // Match WireframeSphere: #version 130 with precision blanked
    defines += "#version 130\n\n";
    defines += "#define lowp \n\n";
    defines += "#define mediump \n\n";
    defines += "#define highp \n\n";
#elif defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    defines += "#version 300 es\n\n";
    defines += "#define ES_30\n\n";
#endif
    vstr = defines + vstr;
    fstr = defines + fstr;
  }

  int shaderID = t850::g_pBaseDriver->CreateShader(vstr, fstr);
  m_shader = t850::g_pBaseDriver->GetShaderIdx(shaderID);
  if (!m_shader) {
    T8_LOG_ERROR("[T8ditor] EditorLineRenderer: shader compile failed");
    return false;
  }

  t850::BufferDesc bd;
  bd.byteWidth = sizeof(CBuffer);
  bd.usage     = t850::BufferUsage::DEFAULT;
  m_cb = (t850::ConstantBuffer*)t850::T8Device->CreateBuffer(t850::BufferType::CONSTANT, bd);
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
                                   t850::VertexBuffer* vb,
                                   t850::IndexBuffer*  ib,
                                   unsigned indexCount,
                                   unsigned vertexStride,
                                   t850::IndexBufferFormat::E ibFormat) {
  if (!m_shader || !m_cb || !vb || !ib || indexCount == 0) return;

  CBuffer cb;
  cb.WVP       = world * vp;
  cb.LineColor = rgba;
  cb.DepthParams = XVECTOR3(
    (m_viewW > 0) ? 1.0f / (float)m_viewW : 1.0f / 1280.0f,
    (m_viewH > 0) ? 1.0f / (float)m_viewH : 1.0f / 720.0f,
    m_farPlane,
    0.005f);  // proportional depth bias (wireDepth *= 1 - bias)

  t850::MeshDrawStateTracker& tracker = t850::MeshDrawStateTracker::Get();
  tracker.BindIndexedGeometry(*t850::T8DeviceContext,
                              vb,
                              vertexStride,
                              0,
                              ib,
                              ibFormat,
                              t850::Topology::LINE_LIST);

  m_shader->Set(*t850::T8DeviceContext);
  tracker.OnShaderChanged(m_shader);
  tracker.UpdateAndBindConstantBuffer(*t850::T8DeviceContext, m_cb, 0, &cb, sizeof(cb));
#if defined(USING_VULKAN) || defined(USING_VULKAN_ONLY)
  // Vulkan reflection can map VS/FS cbuffers to different bindings when the
  // fragment shader also samples depth. Populate both logical slots.
  tracker.UpdateAndBindConstantBuffer(*t850::T8DeviceContext, m_cb, 1, &cb, sizeof(cb));
#endif

  // Bind depth texture AFTER shader is set (D3D12 needs active root signature)
  if (m_depthTex || m_depthTex2) {
    t850::Texture* primaryDepth = m_depthTex ? m_depthTex : m_depthTex2;
    t850::Texture* secondaryDepth = m_depthTex2 ? m_depthTex2 : primaryDepth;
    primaryDepth->Set(*t850::T8DeviceContext, 0, "depthTex");
    secondaryDepth->Set(*t850::T8DeviceContext, 1, "depthTex2");
  }

  t850::T8DeviceContext->DrawIndexed(indexCount, 0, 0);
}

t850::VertexBuffer* EditorLineRenderer::CreatePositionVB(const float* positionsXYZW,
                                                         unsigned numVertices,
                                                         t850::BufferUsage::E usage) {
  if (!t850::T8Device || !positionsXYZW || numVertices == 0) return nullptr;
  t850::BufferDesc bd;
  bd.byteWidth = static_cast<int>(sizeof(float) * 4 * numVertices);
  bd.usage     = usage;
  return (t850::VertexBuffer*)t850::T8Device->CreateBuffer(
      t850::BufferType::VERTEX, bd, const_cast<float*>(positionsXYZW));
}

t850::IndexBuffer* EditorLineRenderer::CreateIndexBuffer16(const unsigned short* indices,
                                                           unsigned numIndices) {
  if (!t850::T8Device || !indices || numIndices == 0) return nullptr;
  t850::BufferDesc bd;
  bd.byteWidth = static_cast<int>(sizeof(unsigned short) * numIndices);
  bd.usage     = t850::BufferUsage::DEFAULT;
  return (t850::IndexBuffer*)t850::T8Device->CreateBuffer(
      t850::BufferType::INDEX, bd, const_cast<unsigned short*>(indices));
}

t850::IndexBuffer* EditorLineRenderer::CreateIndexBuffer32(const unsigned int* indices,
                                                           unsigned numIndices) {
  if (!t850::T8Device || !indices || numIndices == 0) return nullptr;
  t850::BufferDesc bd;
  bd.byteWidth = static_cast<int>(sizeof(unsigned int) * numIndices);
  bd.usage     = t850::BufferUsage::DEFAULT;
  return (t850::IndexBuffer*)t850::T8Device->CreateBuffer(
      t850::BufferType::INDEX, bd, const_cast<unsigned int*>(indices));
}

} // namespace t8ditor
