#include <pch.h>
/*********************************************************
 * T850 Engine — Line Renderer. See header for overview.
 *********************************************************/

#include <scene/LineRenderer.h>

#include <Config.h>
#include <video/BaseDriver.h>
#include <utils/Utils.h>
#include <utils/Log.h>

#include <cstdlib>
#include <string>

namespace t850 {
  extern Device*        T8Device;
  extern DeviceContext* T8DeviceContext;

LineRenderer::LineRenderer() = default;

LineRenderer::~LineRenderer() {
  Destroy();
}

bool LineRenderer::Create() {
  if (m_shaderDepth && m_shaderFlat && m_cb) return true;
  if (!g_pBaseDriver) {
    T8_LOG_ERROR("[LineRenderer] Create called before driver init");
    return false;
  }

  const bool useGL = (g_pBaseDriver->m_currentAPI == GraphicsApi::OPENGL);

  // Load vertex shader (shared by both variants)
  char* vsSrc = file2string(useGL ? "Shaders/VS_EditorLine.glsl"
                                  : "Shaders/VS_EditorLine.hlsl");
  // Depth-tested fragment shader
  char* fsDepthSrc = file2string(useGL ? "Shaders/FS_EditorLine.glsl"
                                       : "Shaders/FS_EditorLine.hlsl");
  // Flat (always-visible) fragment shader
  char* fsFlatSrc = file2string(useGL ? "Shaders/FS_LineFlat.glsl"
                                      : "Shaders/FS_LineFlat.hlsl");

  if (!vsSrc || !fsDepthSrc || !fsFlatSrc) {
    T8_LOG_ERROR("[LineRenderer] shader file load failed");
    if (vsSrc) free(vsSrc);
    if (fsDepthSrc) free(fsDepthSrc);
    if (fsFlatSrc) free(fsFlatSrc);
    return false;
  }

  std::string vstr(vsSrc);
  std::string fsDepthStr(fsDepthSrc);
  std::string fsFlatStr(fsFlatSrc);
  free(vsSrc); free(fsDepthSrc); free(fsFlatSrc);

  if (useGL) {
    std::string defines;
#if defined(USING_OPENGL)
    // Match WireframeSphere/WireframeArrow: use #version 130 with
    // precision-qualifier macros blanked so the non-ES path is taken.
    defines += "#version 130\n\n";
    defines += "#define lowp \n\n";
    defines += "#define mediump \n\n";
    defines += "#define highp \n\n";
#elif defined(USING_OPENGL_ES30) || defined(USING_OPENGL_ES31)
    defines += "#version 300 es\n\n";
    defines += "#define ES_30\n\n";
#endif
    vstr = defines + vstr;
    fsDepthStr = defines + fsDepthStr;
    fsFlatStr  = defines + fsFlatStr;
  }

  // Compile depth-tested shader variant
  int depthId = g_pBaseDriver->CreateShader(vstr, fsDepthStr);
  m_shaderDepth = g_pBaseDriver->GetShaderIdx(depthId);
  if (!m_shaderDepth) {
    T8_LOG_ERROR("[LineRenderer] depth shader compile failed");
    return false;
  }

  // Compile flat (always-visible) shader variant
  int flatId = g_pBaseDriver->CreateShader(vstr, fsFlatStr);
  m_shaderFlat = g_pBaseDriver->GetShaderIdx(flatId);
  if (!m_shaderFlat) {
    T8_LOG_ERROR("[LineRenderer] flat shader compile failed");
    return false;
  }

  BufferDesc bd;
  bd.byteWidth = sizeof(CBuffer);
  bd.usage     = BufferUsage::DEFAULT;
  m_cb = (ConstantBuffer*)T8Device->CreateBuffer(BufferType::CONSTANT, bd);
  if (!m_cb) {
    T8_LOG_ERROR("[LineRenderer] CB create failed");
    return false;
  }

  T8_LOG_INFO("[LineRenderer] ready (depth + flat shaders)");
  return true;
}

void LineRenderer::Destroy() {
  m_shaderDepth = nullptr; // owned by the driver's shader cache
  m_shaderFlat  = nullptr;
  if (m_cb) m_cb->release();
  m_cb = nullptr;
}

void LineRenderer::DrawLines(const XMATRIX44& world,
                             const XMATRIX44& vp,
                             const XVECTOR3&  rgba,
                             VertexBuffer* vb,
                             IndexBuffer*  ib,
                             unsigned indexCount,
                             unsigned vertexStride,
                             IndexBufferFormat::E ibFormat) {
  if (!m_shaderDepth || !m_cb || !vb || !ib || indexCount == 0) return;

  ShaderBase* shader = m_depthTest ? m_shaderDepth : m_shaderFlat;

  CBuffer cb;
  cb.WVP       = world * vp;
  cb.LineColor = rgba;
  cb.DepthParams = XVECTOR3(
    (m_viewW > 0) ? 1.0f / (float)m_viewW : 1.0f / 1280.0f,
    (m_viewH > 0) ? 1.0f / (float)m_viewH : 1.0f / 720.0f,
    m_farPlane,
    0.005f);  // proportional depth bias (wireDepth *= 1 - bias)

  ib->Set(*T8DeviceContext, 0, ibFormat);
  vb->Set(*T8DeviceContext, vertexStride, 0);

  // Set topology BEFORE shader (Vulkan bakes topology into the pipeline at Set time)
  T8DeviceContext->SetPrimitiveTopology(Topology::LINE_LIST);

  shader->Set(*T8DeviceContext);
  m_cb->UpdateFromBuffer(*T8DeviceContext, &cb);
  m_cb->Set(*T8DeviceContext);

  // Bind depth texture AFTER shader is set (D3D12 needs active root signature)
  if (m_depthTest && m_depthTex)
    m_depthTex->Set(*T8DeviceContext, 0, "depthTex");

  T8DeviceContext->DrawIndexed(indexCount, 0, 0);

  // Reset topology back to triangle list for subsequent draws
  T8DeviceContext->SetPrimitiveTopology(Topology::TRIANLE_LIST);
}

VertexBuffer* LineRenderer::CreatePositionVB(const float* positionsXYZW,
                                             unsigned numVertices,
                                             BufferUsage::E usage) {
  if (!T8Device || !positionsXYZW || numVertices == 0) return nullptr;
  BufferDesc bd;
  bd.byteWidth = static_cast<int>(sizeof(float) * 4 * numVertices);
  bd.usage     = usage;
  return (VertexBuffer*)T8Device->CreateBuffer(
      BufferType::VERTEX, bd, const_cast<float*>(positionsXYZW));
}

IndexBuffer* LineRenderer::CreateIndexBuffer16(const unsigned short* indices,
                                               unsigned numIndices) {
  if (!T8Device || !indices || numIndices == 0) return nullptr;
  BufferDesc bd;
  bd.byteWidth = static_cast<int>(sizeof(unsigned short) * numIndices);
  bd.usage     = BufferUsage::DEFAULT;
  return (IndexBuffer*)T8Device->CreateBuffer(
      BufferType::INDEX, bd, const_cast<unsigned short*>(indices));
}

IndexBuffer* LineRenderer::CreateIndexBuffer32(const unsigned int* indices,
                                               unsigned numIndices) {
  if (!T8Device || !indices || numIndices == 0) return nullptr;
  BufferDesc bd;
  bd.byteWidth = static_cast<int>(sizeof(unsigned int) * numIndices);
  bd.usage     = BufferUsage::DEFAULT;
  return (IndexBuffer*)T8Device->CreateBuffer(
      BufferType::INDEX, bd, const_cast<unsigned int*>(indices));
}

} // namespace t850
