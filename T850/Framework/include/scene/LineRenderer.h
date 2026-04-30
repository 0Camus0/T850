/*********************************************************
 * T850 Engine — Line Renderer
 *
 * General-purpose line-list renderer for wireframe overlays,
 * skeleton debug visualization, and editor chrome. Wraps the
 * VS_EditorLine shader with two fragment-shader variants:
 *   - Depth-tested (FS_EditorLine): compares against GBuffer depth
 *   - Flat (FS_LineFlat): always visible, no depth test
 *
 * Call SetDepthTestEnabled(false) before DrawLines() to use
 * the flat shader (e.g. for skeleton bones that should draw
 * on top of everything).
 *********************************************************/

#ifndef T800_LINE_RENDERER_H
#define T800_LINE_RENDERER_H

#include <video/BaseDriver.h>
#include <Descriptors.h>
#include <utils/xMaths.h>

namespace t850 {

class LineRenderer {
public:
  // Constant buffer layout shared with VS_EditorLine.{hlsl,glsl}.
  // Field order is significant — the GL backend reflects uniforms positionally.
  // The GL GLSL parser adds attribute byte sizes before uniform offsets,
  // so the CB must include padding to match GL's reflected layout.
  struct CBuffer {
    XMATRIX44 WVP;
    XVECTOR3  LineColor;    // .x .y .z .w
    XVECTOR3  DepthParams;  // .x=1/viewW, .y=1/viewH, .z=farPlane, .w=depthBias
    XVECTOR3  _glPad[2];   // GL parser may offset uniforms past sizeof(base fields)
  };

  LineRenderer();
  ~LineRenderer();

  // Compile shaders + create the shared CB. Safe to call once per app.
  bool Create();
  void Destroy();

  bool IsReady() const { return m_shaderDepth != nullptr && m_cb != nullptr; }

  // Toggle depth testing. When true, uses FS_EditorLine (depth-tested).
  // When false, uses FS_LineFlat (always visible).
  void SetDepthTestEnabled(bool enabled) { m_depthTest = enabled; }

  // Set viewport dimensions for depth comparison (call once per frame or on resize)
  void SetViewport(int width, int height) { m_viewW = width; m_viewH = height; }

  // Set the GBuffer depth texture for depth-tested wireframe.
  void SetDepthTexture(Texture* depthTex) { m_depthTex = depthTex; }

  // Set the camera far plane for depth-tested overlays.
  void SetFarPlane(float farPlane) { m_farPlane = farPlane; }

  // Issue one indexed line-list draw using the supplied VB/IB.
  void DrawLines(const XMATRIX44& world,
                 const XMATRIX44& vp,
                 const XVECTOR3&  rgba,
                 VertexBuffer* vb,
                 IndexBuffer*  ib,
                 unsigned indexCount,
                 unsigned vertexStride,
                 IndexBufferFormat::E ibFormat = IndexBufferFormat::R16);

  // Helper for callers that want to build a VB of float4 line endpoints (xyzw, w=1).
  // Use BufferUsage::DINAMIC for buffers updated every frame.
  static VertexBuffer* CreatePositionVB(const float* positionsXYZW,
                                        unsigned numVertices,
                                        BufferUsage::E usage = BufferUsage::DEFAULT);
  static IndexBuffer*  CreateIndexBuffer16(const unsigned short* indices,
                                           unsigned numIndices);
  static IndexBuffer*  CreateIndexBuffer32(const unsigned int* indices,
                                           unsigned numIndices);

private:
  ShaderBase*     m_shaderDepth = nullptr;  // FS_EditorLine (depth-tested)
  ShaderBase*     m_shaderFlat  = nullptr;  // FS_LineFlat   (always visible)
  ConstantBuffer* m_cb          = nullptr;
  Texture*        m_depthTex    = nullptr;
  int             m_viewW       = 1280;
  int             m_viewH       = 720;
  float           m_farPlane    = 1000.0f;
  bool            m_depthTest   = true;
};

} // namespace t850

#endif // T800_LINE_RENDERER_H
