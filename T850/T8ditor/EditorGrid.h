/*********************************************************
* T8ditor — XZ-plane reference grid.
*
* Draws a grid of lines on the Y=0 plane, like the default scene in
* Blender / 3dsMax. Two passes:
*   1. minor lines at unit spacing in a neutral gray,
*   2. axis lines at the world X (red) and Z (blue) directions, brighter.
*
* All geometry is built on Create() once and reused every frame. Color
* comes from EditorLineRenderer's CB so we don't need a separate shader.
*********************************************************/

#ifndef T8DITOR_EDITOR_GRID_H
#define T8DITOR_EDITOR_GRID_H

#include "EditorLineRenderer.h"

namespace t8ditor {

class EditorGrid {
public:
  EditorGrid()  = default;
  ~EditorGrid() { Destroy(); }

  // halfExtent: number of cells from origin to edge. spacing: world units
  // between grid lines. So a 10/1.0 grid spans 20×20 world units.
  bool Create(int halfExtent = 10, float spacing = 1.0f);
  void Destroy();

  // Draw the grid + axes. EditorLineRenderer must already be Created.
  void Draw(EditorLineRenderer& lines, const XMATRIX44& vp);

  // Cosmetics — tweakable from EditorApp.
  XVECTOR3 MinorColor = XVECTOR3(0.35f, 0.35f, 0.35f, 1.0f);
  XVECTOR3 XAxisColor = XVECTOR3(0.85f, 0.20f, 0.20f, 1.0f);
  XVECTOR3 ZAxisColor = XVECTOR3(0.20f, 0.40f, 0.85f, 1.0f);

private:
  // Two separate VB/IB pairs so we can colour them independently.
  t800::VertexBuffer* m_minorVB = nullptr;
  t800::IndexBuffer*  m_minorIB = nullptr;
  unsigned m_minorIndexCount = 0;

  t800::VertexBuffer* m_axesVB  = nullptr;
  t800::IndexBuffer*  m_xAxisIB = nullptr;
  t800::IndexBuffer*  m_zAxisIB = nullptr;
};

} // namespace t8ditor

#endif // T8DITOR_EDITOR_GRID_H
