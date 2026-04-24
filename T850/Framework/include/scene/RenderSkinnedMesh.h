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

  void SetUseQuatSkinning(bool qt) { m_useQuatSkinning = qt; }
  bool GetUseQuatSkinning() const  { return m_useQuatSkinning; }

  // ── Debug wireframe / skeleton visualization ──
  // Draw mesh wireframe using GPU skinning pipeline (green, LINE_LIST)
  void DrawWireframe();
  // Draw skeleton bones without depth testing (magenta)
  void DrawSkeleton();

private:
  // Extended CBuffer with bone matrices appended (matrix skinning path)
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

  // Extended CBuffer with quaternion+translation (QT skinning path — half the size)
  struct CBufferSkinnedQT {
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
    XVECTOR3  BoneQuats[kMaxBones];  // quaternion (x,y,z,w) — uses XVECTOR3 which has 4 floats
    XVECTOR3  BoneTrans[kMaxBones];  // translation (x,y,z,0)
  };

  // ── Wireframe helpers ──
  void BuildWireframeBuffers();
  void BuildSkeletonBuffers();
  void UpdateSkeletonPositions();

  AnimationController m_animController;
  std::vector<CBufferSkinned> m_skinnedCBuffers;   // matrix path (one per geometry)
  std::vector<CBufferSkinnedQT> m_skinnedQTBuffers; // quat+trans path
  bool m_hasSkin = false;
  bool m_playing = true;
  bool m_useSlerp = true;
  bool m_useQuatSkinning = true; // default to QT path (smaller CB, fewer ops)

  // ── Wireframe state (GPU-skinned) ──
  struct WireGeo {
    IndexBuffer* IB = nullptr;
    unsigned indexCount = 0;
    bool use32Bit = false;
  };
  std::vector<WireGeo> m_wireGeo;      // per-geometry line-list IBs
  ShaderBase*          m_wireShader = nullptr;

  // Skeleton bone lines (CPU-updated each frame)
  // Uses WireframeSphere-style shader (VS_W + FS_W) which is known to
  // work on all GL backends, bypassing the LineRenderer.
  ShaderBase*          m_skelShader     = nullptr;
  ConstantBuffer*      m_skelCB         = nullptr;
  VertexBuffer*        m_skelVB         = nullptr;
  IndexBuffer*         m_skelIB         = nullptr;
  unsigned             m_skelIndexCount = 0;
  std::vector<float>   m_skelPositions;   // xyzw per bone endpoint

  // LineRenderer kept for depth-tested wireframe overlays
  LineRenderer         m_lineRenderer;
};

} // namespace t800

#endif // T800_RENDER_SKINNED_MESH_H
