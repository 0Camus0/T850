/*********************************************************
* T8ditor — line renderer for editor overlays.
*
* Shared infrastructure used by EditorGrid, EditorGizmo, and EditorMesh's
* wireframe drawing. Wraps the editor-only VS_EditorLine / FS_EditorLine
* shader pair and exposes a "DrawLines(vp, world, color, vb, ib, count)"
* primitive. Each draw uploads its own WVP+color into the shared CB.
*
* This lives in T8ditor (not Framework) because it exists only to render
* editor chrome. The VB/IB it draws are owned by callers (grid, gizmo,
* mesh) so the renderer itself is a stateless helper.
*********************************************************/

#ifndef T8DITOR_EDITOR_LINE_RENDERER_H
#define T8DITOR_EDITOR_LINE_RENDERER_H

#include <video/BaseDriver.h>
#include <utils/xMaths.h>

namespace t8ditor {

class EditorLineRenderer {
public:
  // Constant buffer layout shared with VS_EditorLine.{hlsl,glsl}. Field
  // order is significant — the GL backend reflects uniforms positionally.
  struct CBuffer {
    XMATRIX44 WVP;
    XVECTOR3  LineColor;    // .x .y .z .w
    XVECTOR3  DepthParams;  // .x=1/viewW, .y=1/viewH, .z=farPlane, .w=depthBias
    XVECTOR3  _glPad[2];   // GL uniform byte position padding
  };

  EditorLineRenderer();
  ~EditorLineRenderer();

  // Compile shaders + create the shared CB. Safe to call once per app.
  bool Create();
  void Destroy();

  bool IsReady() const { return m_shader != nullptr && m_cb != nullptr; }

  // Set viewport dimensions for depth comparison (call once per frame or on resize)
  void SetViewport(int width, int height) { m_viewW = width; m_viewH = height; }

  // Set the scene depth textures for depth-tested wireframe. Pass nullptr to disable.
  void SetDepthTexture(t850::Texture* depthTex) { m_depthTex = depthTex; }
  void SetSecondaryDepthTexture(t850::Texture* depthTex) { m_depthTex2 = depthTex; }

  // Set the camera far plane for depth-tested overlays.
  void SetFarPlane(float farPlane) { m_farPlane = farPlane; }

  // Issue one indexed line-list draw using the supplied VB/IB. The caller
  // owns the buffers and is responsible for their lifetime.
  void DrawLines(const XMATRIX44& world,
                 const XMATRIX44& vp,
                 const XVECTOR3&  rgba,
                 t850::VertexBuffer* vb,
                 t850::IndexBuffer*  ib,
                 unsigned indexCount,
                 unsigned vertexStride,
                 t850::IndexBufferFormat::E ibFormat = t850::IndexBufferFormat::R16);

  // Helper for callers that want to build a VB of a list of float4 line
  // endpoints (xyzw, w=1). Returns nullptr on failure. Ownership transfers
  // to the caller (must Destroy/release).
  static t850::VertexBuffer* CreatePositionVB(const float* positionsXYZW,
                                              unsigned numVertices);
  static t850::IndexBuffer*  CreateIndexBuffer16(const unsigned short* indices,
                                                 unsigned numIndices);
  static t850::IndexBuffer*  CreateIndexBuffer32(const unsigned int* indices,
                                                 unsigned numIndices);

private:
  t850::ShaderBase*     m_shader   = nullptr;
  t850::ConstantBuffer* m_cb       = nullptr;
  t850::Texture*        m_depthTex = nullptr;
  t850::Texture*        m_depthTex2 = nullptr;
  int                   m_viewW    = 1280;
  int                   m_viewH    = 720;
  float                 m_farPlane = 1000.0f;
};

} // namespace t8ditor

#endif // T8DITOR_EDITOR_LINE_RENDERER_H
