#pragma once
#include <vector>
#include "scene/RenderQuad.h"
#include "utils/xMaths.h"
#include "scene/PrimitiveManager.h"
#include "scene/PrimitiveInstance.h"
namespace t800 {
class LensFlare {
public:
  float m_sunSize;
  float m_spacing;
  XVECTOR3 m_sunWorldPos;
  XMATRIX44 m_proj;
  XMATRIX44 * pVP;
  const XVECTOR2 CENTER_SCREEN = XVECTOR2(0.0,0.0);
  std::vector<PrimitiveInst> m_quads;
  std::vector<int> m_flareTextureID;
  void Init(const PrimitiveManager& mngr);
  void Draw();
  void Update();

  float aspectRatio;
};
}