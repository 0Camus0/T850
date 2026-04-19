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
  // order is significant — the GL backend reflects uniforms positionally
  // (see Framework/src/video/GLShader.cpp).
  struct CBuffer {
    XMATRIX44 WVP;
    XVECTOR3  LineColor; // .x .y .z .w (XVECTOR3 carries .w too)
  };

  EditorLineRenderer();
  ~EditorLineRenderer();

  // Compile shaders + create the shared CB. Safe to call once per app.
  bool Create();
  void Destroy();

  bool IsReady() const { return m_shader != nullptr && m_cb != nullptr; }

  // Issue one indexed line-list draw using the supplied VB/IB. The caller
  // owns the buffers and is responsible for their lifetime.
  void DrawLines(const XMATRIX44& world,
                 const XMATRIX44& vp,
                 const XVECTOR3&  rgba,
                 t800::VertexBuffer* vb,
                 t800::IndexBuffer*  ib,
                 unsigned indexCount,
                 unsigned vertexStride,
                 t800::T8_IB_FORMAR::E ibFormat = t800::T8_IB_FORMAR::R16);

  // Helper for callers that want to build a VB of a list of float4 line
  // endpoints (xyzw, w=1). Returns nullptr on failure. Ownership transfers
  // to the caller (must Destroy/release).
  static t800::VertexBuffer* CreatePositionVB(const float* positionsXYZW,
                                              unsigned numVertices);
  static t800::IndexBuffer*  CreateIndexBuffer16(const unsigned short* indices,
                                                 unsigned numIndices);

private:
  t800::ShaderBase*     m_shader = nullptr;
  t800::ConstantBuffer* m_cb     = nullptr;
};

} // namespace t8ditor

#endif // T8DITOR_EDITOR_LINE_RENDERER_H
