/*********************************************************
 * T850 Engine — Animation Controller
 *
 * Drives keyframe-based skeletal animation playback.
 * Reads from engine xAnimationInfo / xSkeleton structures
 * and updates bone matrices each frame.
 *********************************************************/

#ifndef T800_ANIMATION_CONTROLLER_H
#define T800_ANIMATION_CONTROLLER_H

#include <Config.h>
#include <utils/xDefs.h>
#include <utils/xMaths.h>

namespace t800 {

static constexpr int kMaxBones = 256;

class AnimationController {
public:
  AnimationController();
  ~AnimationController() = default;

  void Init(xF::xAnimationInfo* animInfo,
            xF::xSkeleton* skeletonBind,
            xF::xSkeleton* skeletonAnimated);

  void Update(float deltaTime);

  void NextAnimationSet();
  void PrevAnimationSet();
  void ResetAnimationSet();

  void SetLooping(bool loop) { m_looping = loop; }
  bool IsLooping() const     { return m_looping; }

  void SetUseSlerp(bool slerp) { m_useSlerp = slerp; }
  bool GetUseSlerp() const     { return m_useSlerp; }

  void SetSpeed(float speed) { m_speed = speed; }
  float GetSpeed() const     { return m_speed; }

  int  GetCurrentSet() const { return m_currentSet; }
  int  GetNumSets() const;

  // After Update(), these contain the final bone matrices
  // (InverseBindMatrix * CombinedWorldMatrix) ready for the shader.
  const XMATRIX44* GetBoneMatrices() const { return m_finalBoneMatrices; }
  int GetNumBones() const { return m_numBones; }

  // Access the animated skeleton (for debug bone visualization)
  const xF::xSkeleton* GetAnimSkeleton() const { return m_pSkeletonAnim; }

  // Dump all bone matrices to a text file for debugging
  void DumpMatrices(const char* filename) const;

  // The skin weights needed to compute final matrices
  void SetSkinWeights(const std::vector<xF::xSkinWeights>& weights) {
    m_pSkinWeights = &weights;
  }

private:
  void InterpolateKeys(float dt);
  void ComputeHierarchy();
  void ComputeFinalMatrices();
  void ComputeBindPose();       // compute bind-pose combined + own IBM
  void ResetLocals();

  // Invert a 4x4 affine matrix (rotation + translation)
  static XMATRIX44 InvertAffine(const XMATRIX44& m);

  // Quaternion SLERP
  static XQUATERNION Slerp(const XQUATERNION& a, const XQUATERNION& b, float t);
  // Quaternion NLERP (normalized LERP — faster, adequate for small angles)
  static XQUATERNION Nlerp(const XQUATERNION& a, const XQUATERNION& b, float t);
  // Quaternion to rotation matrix (row-major)
  static XMATRIX44 QuaternionToMatrix(const XQUATERNION& q);

  xF::xAnimationInfo*  m_pAnimInfo      = nullptr;
  xF::xSkeleton*       m_pSkeletonBind  = nullptr;
  xF::xSkeleton*       m_pSkeletonAnim  = nullptr;
  const std::vector<xF::xSkinWeights>* m_pSkinWeights = nullptr;

  XMATRIX44 m_finalBoneMatrices[kMaxBones];
  XMATRIX44 m_invBindPose[kMaxBones]; // our own IBM (computed, not from glTF)

  float m_localTime     = 0.0f;
  float m_speed         = 1.0f;
  float m_ticksPerSecond = 4800.0f;

  int   m_currentSet    = 0;
  int   m_numBones      = 0;
  bool  m_looping       = true;
  bool  m_useSlerp     = true;
  bool  m_initialized   = false;
};

} // namespace t800

#endif // T800_ANIMATION_CONTROLLER_H
