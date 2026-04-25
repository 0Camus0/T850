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

namespace t850 {

class RenderSkinnedMesh : public RenderMesh {
public:
  RenderSkinnedMesh() = default;
  ~RenderSkinnedMesh() override = default;

  void Create() override;
  void Draw(float *t, float *vp) override;
  void Destroy() override;

  // Call BEFORE the render graph — updates animation and uploads bone texture.
  // Must happen outside any render pass (Vulkan copy commands require it).
  void UpdateAnimationAndBones();

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

  // Keyframe stepping mode
  void SetKeyframeMode(bool enabled) { m_animController.SetKeyframeMode(enabled); }
  bool GetKeyframeMode() const { return m_animController.GetKeyframeMode(); }
  void StepKeyframe(int delta) { m_animController.StepKeyframe(delta); }
  int  GetCurrentKeyframe() const { return m_animController.GetCurrentKeyframe(); }
  int  GetTotalKeyframes() const { return m_animController.GetTotalKeyframes(); }

  // ── Debug wireframe / skeleton visualization ──
  // Draw mesh wireframe using GPU skinning pipeline (green, LINE_LIST)
  void DrawWireframe();
  // Draw skeleton bones without depth testing (magenta)
  void DrawSkeleton();

  // Set the GBuffer depth texture for wireframe depth-tested occlusion
  void SetWireframeDepthTex(Texture* depthTex) { m_wireDepthTex = depthTex; }
  void SetWireframeViewport(int w, int h) { m_wireViewW = w; m_wireViewH = h; }

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
  bool m_useQuatSkinning = false;

  // ── Bone texture (RGBA32F, updated per-frame) ──
  Texture*             m_boneTexture    = nullptr;
  int                  m_boneTexWidth   = 0;
  std::vector<float>   m_boneTexData;       // RGBA32F pixel data

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

  // Wireframe depth occlusion (GBuffer depth texture)
  Texture*             m_wireDepthTex   = nullptr;
  int                  m_wireViewW      = 1280;
  int                  m_wireViewH      = 720;
};

} // namespace t850

#endif // T800_RENDER_SKINNED_MESH_H
