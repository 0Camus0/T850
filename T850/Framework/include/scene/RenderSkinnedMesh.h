/*********************************************************
 * T850 Engine — Skinned Mesh Renderer
 *
 * Extends RenderMesh with skeletal animation support.
 * Manages bone matrix upload and animation playback.
 * Provides debug wireframe and skeleton visualization.
 *********************************************************/

#ifndef T800_RENDER_SKINNED_MESH_H
#define T800_RENDER_SKINNED_MESH_H

#include <Config.h>
#include <scene/RenderMesh.h>
#include <scene/AnimationController.h>
#include <scene/LineRenderer.h>

#include <vector>

namespace t800 {

class RenderSkinnedMesh : public RenderMesh {
public:
  RenderSkinnedMesh() = default;
  ~RenderSkinnedMesh() override = default;

  void Create() override;
  void Draw(float *t, float *vp) override;
  void Destroy() override;

  // ── Animation playback API ──
  AnimationController& GetAnimController() { return m_animController; }

  void PlayAnimation()  { m_playing = true; }
  void PauseAnimation() { m_playing = false; }
  bool IsPlaying() const { return m_playing; }

  void NextAnimation()  { m_animController.NextAnimationSet(); }
  void PrevAnimation()  { m_animController.PrevAnimationSet(); }
  void ResetAnimation() { m_animController.ResetAnimationSet(); }

  void SetAnimSpeed(float speed) { m_animController.SetSpeed(speed); }
  float GetAnimSpeed() const     { return m_animController.GetSpeed(); }

  void SetLooping(bool loop) { m_animController.SetLooping(loop); }
  bool IsLooping() const     { return m_animController.IsLooping(); }

  void SetUseSlerp(bool slerp) { m_useSlerp = slerp; }
  bool GetUseSlerp() const     { return m_useSlerp; }

  int  GetCurrentAnimSet() const { return m_animController.GetCurrentSet(); }
  int  GetNumAnimSets() const    { return m_animController.GetNumSets(); }
  int  GetNumBones() const       { return m_animController.GetNumBones(); }

  bool HasSkinData() const { return m_hasSkin; }

  // ── Debug wireframe / skeleton visualization ──
  // Draw mesh wireframe with depth testing (green)
  void DrawWireframe(Texture* depthTex, int viewW, int viewH, float farPlane);
  // Draw skeleton bones without depth testing (magenta)
  void DrawSkeleton();

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

  // ── Wireframe helpers ──
  void BuildWireframeBuffers();
  void BuildSkeletonBuffers();
  void UpdateSkinnedPositions();
  void UpdateSkeletonPositions();

  AnimationController m_animController;
  std::vector<CBufferSkinned> m_skinnedCBuffers; // one per geometry
  bool m_hasSkin = false;
  bool m_playing = true;
  bool m_useSlerp = true;

  // ── Wireframe state ──
  LineRenderer         m_lineRenderer;
  // Mesh wireframe (edges from triangles, CPU-skinned each frame)
  VertexBuffer*        m_wireVB         = nullptr;
  IndexBuffer*         m_wireIB         = nullptr;
  unsigned             m_wireIndexCount = 0;
  bool                 m_wireUse32Bit   = false;
  unsigned             m_wireTotalVerts = 0;
  std::vector<float>   m_wirePositions;   // xyzw per vertex, updated each frame
  // Skeleton bone lines (rebuilt each frame from Combined matrices)
  VertexBuffer*        m_skelVB         = nullptr;
  IndexBuffer*         m_skelIB         = nullptr;
  unsigned             m_skelIndexCount = 0;
  std::vector<float>   m_skelPositions;   // xyzw per bone endpoint
};

} // namespace t800

#endif // T800_RENDER_SKINNED_MESH_H
