/*********************************************************
 * T850 Engine — Skinned Mesh Renderer
 *
 * Extends RenderMesh with skeletal animation support.
 * Manages bone matrix upload and animation playback.
 *********************************************************/

#ifndef T800_RENDER_SKINNED_MESH_H
#define T800_RENDER_SKINNED_MESH_H

#include <Config.h>
#include <scene/RenderMesh.h>
#include <scene/AnimationController.h>

namespace t800 {

class RenderSkinnedMesh : public RenderMesh {
public:
  RenderSkinnedMesh() = default;
  ~RenderSkinnedMesh() override = default;

  void Create() override;
  void Draw(float *t, float *vp) override;
  void Destroy() override;

  AnimationController& GetAnimController() { return m_animController; }

private:
  // Extended CBuffer with bone matrices appended
  struct CBufferSkinned {
    // Same layout as RenderMesh::CBuffer
    XMATRIX44 WVP;
    XMATRIX44 World;
    XMATRIX44 WorldView;
    XVECTOR3  Light0Pos;
    XVECTOR3  Light0Col;
    XVECTOR3  CameraPos;
    XVECTOR3  CameraInfo;
    XVECTOR3  AmbientColor;
    XVECTOR3  DiffuseColor;
    XVECTOR3  SpecularColor;
    XVECTOR3  PBRParams;
    XVECTOR3  Intensities;
    XVECTOR3  ParallaxSettings;
    XVECTOR3  ParallaxShadowSettings;
    XVECTOR3  Light0Dir;
    // Bone matrices appended for skinning
    XMATRIX44 BoneMatrices[kMaxBones];
  };

  AnimationController m_animController;
  std::vector<CBufferSkinned> m_skinnedCBuffers; // one per geometry
  bool m_hasSkin = false;
};

} // namespace t800

#endif // T800_RENDER_SKINNED_MESH_H
