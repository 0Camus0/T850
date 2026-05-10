#include <pch.h>
/*********************************************************
 * T850 Engine — Animation Controller
 *
 * Keyframe-based skeletal animation playback. Ported and
 * cleaned up from the legacy Resources/xOGLESMesh.cpp
 * AnimationController with fixes:
 *   - Proper SLERP for rotation interpolation
 *   - Fixed scale key index bug (was incrementing rot index)
 *   - Uses delta-time instead of absolute timer
 *   - Cleaner hierarchy traversal
 *********************************************************/

#include <scene/AnimationController.h>
#include <utils/Log.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace t850 {

AnimationController::AnimationController() {
  for (int i = 0; i < kMaxBones; i++) {
    m_finalBoneMatrices[i].Identity();
    m_invBindPose[i].Identity();
    m_finalBoneQuats[i] = XQUATERNION(0.0f, 0.0f, 0.0f, 1.0f);
    m_finalBoneTrans[i] = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  }
}

void AnimationController::Init(xF::xAnimationInfo* animInfo,
                               xF::xSkeleton* skeletonBind,
                               xF::xSkeleton* skeletonAnim) {
  m_pAnimInfo     = animInfo;
  m_pSkeletonBind = skeletonBind;
  m_pSkeletonAnim = skeletonAnim;
  m_currentSet    = 0;
  m_localTime     = 0.0f;
  m_initialized   = false;

  if (!m_pAnimInfo || !m_pSkeletonBind || !m_pSkeletonAnim) return;
  if (m_pSkeletonAnim->Bones.empty()) return;

  m_numBones = static_cast<int>(m_pSkeletonAnim->Bones.size());
  if (m_numBones > kMaxBones) {
    T8_LOG_ERROR("[AnimCtrl] Skeleton has %d bones, clamping to %d", m_numBones, kMaxBones);
    m_numBones = kMaxBones;
  }

  m_ticksPerSecond = (m_pAnimInfo->TicksPerSecond > 0)
    ? static_cast<float>(m_pAnimInfo->TicksPerSecond)
    : 4800.0f;

  ResetLocals();
  m_initialized = true;

  // Ensure every animation set has m_MaxTimeOnTicks computed
  for (auto& aset : m_pAnimInfo->Animations) {
    if (aset.m_MaxTimeOnTicks <= 0) {
      long maxT = 0;
      for (auto& bone : aset.BonesRef) {
        for (auto& pk : bone.PositionKeys) {
          long t = static_cast<long>(pk.t.i_atTime);
          if (t > maxT) maxT = t;
        }
        for (auto& rk : bone.RotationKeys) {
          long t = static_cast<long>(rk.t.i_atTime);
          if (t > maxT) maxT = t;
        }
        for (auto& sk : bone.ScaleKeys) {
          long t = static_cast<long>(sk.t.i_atTime);
          if (t > maxT) maxT = t;
        }
      }
      aset.m_MaxTimeOnTicks = maxT;
    }
    T8_LOG_INFO("[AnimCtrl] Anim '%s': duration=%ld ticks (%.2f sec)",
                aset.Name.c_str(), aset.m_MaxTimeOnTicks,
                aset.m_MaxTimeOnTicks / (double)m_ticksPerSecond);
  }

  // Compute bind-pose combined matrices and our own IBM from them.
  // This guarantees IBM * BindCombined = Identity exactly, avoiding
  // accumulated numerical drift from glTF's pre-baked IBM values.
  ComputeBindPose();

  T8_LOG_INFO("[AnimCtrl] Initialized: %d bones, %zu animation sets, %.0f tps",
              m_numBones, m_pAnimInfo->Animations.size(), m_ticksPerSecond);
}

int AnimationController::GetNumSets() const {
  return m_pAnimInfo ? static_cast<int>(m_pAnimInfo->Animations.size()) : 0;
}

void AnimationController::NextAnimationSet() {
  m_currentSet = (m_currentSet + 1) % GetNumSets();
  ResetLocals();
}

void AnimationController::PrevAnimationSet() {
  int n = GetNumSets();
  m_currentSet = (m_currentSet - 1 + n) % n;
  ResetLocals();
}

void AnimationController::ResetAnimationSet() {
  m_currentKeyframe = 0;
  ResetLocals();
}

int AnimationController::GetTotalKeyframes() const {
  if (!m_pAnimInfo || m_pAnimInfo->Animations.empty()) return 0;
  const xF::xAnimationSet& aset = m_pAnimInfo->Animations[m_currentSet];
  // Use the max number of rotation keyframes across all bones as the
  // "total keyframes" count (rotation is the most common channel).
  unsigned int maxKeys = 0;
  for (const auto& bone : aset.BonesRef) {
    if (bone.RotationKeys.size() > maxKeys)
      maxKeys = static_cast<unsigned int>(bone.RotationKeys.size());
    if (bone.PositionKeys.size() > maxKeys)
      maxKeys = static_cast<unsigned int>(bone.PositionKeys.size());
  }
  return static_cast<int>(maxKeys);
}

void AnimationController::StepKeyframe(int delta) {
  if (!m_pAnimInfo || m_pAnimInfo->Animations.empty()) return;
  const xF::xAnimationSet& aset = m_pAnimInfo->Animations[m_currentSet];

  int total = GetTotalKeyframes();
  if (total <= 0) return;

  m_currentKeyframe += delta;
  if (m_currentKeyframe < 0) m_currentKeyframe = total - 1;
  if (m_currentKeyframe >= total) m_currentKeyframe = 0;

  // Find the tick time for this keyframe index from the first bone
  // channel that has enough keys. All channels share the same time axis.
  float tickTime = 0.0f;
  for (const auto& bone : aset.BonesRef) {
    if (m_currentKeyframe < static_cast<int>(bone.RotationKeys.size())) {
      tickTime = static_cast<float>(bone.RotationKeys[m_currentKeyframe].t.i_atTime);
      break;
    }
    if (m_currentKeyframe < static_cast<int>(bone.PositionKeys.size())) {
      tickTime = static_cast<float>(bone.PositionKeys[m_currentKeyframe].t.i_atTime);
      break;
    }
  }

  m_localTime = tickTime / m_ticksPerSecond;

  // Apply this keyframe (no interpolation — snap each bone to its nearest key)
  ApplyKeyframeSnap(tickTime);
  ComputeHierarchy();
  ComputeFinalMatrices();
}

void AnimationController::ResetLocals() {
  m_localTime = 0.0f;
  if (!m_pAnimInfo || m_pAnimInfo->Animations.empty()) return;

  for (auto& aset : m_pAnimInfo->Animations) {
    for (auto& bone : aset.BonesRef) {
      auto& k = bone.ActualKey;
      k.StatePos = xF::xAnimationSingleKey::START;
      k.StateRot = xF::xAnimationSingleKey::START;
      k.StateSc  = xF::xAnimationSingleKey::START;
      k.LocalIndexPos = k.LocalIndexRot = k.LocalIndexSc = 0;
      k.LocaltimePos = k.LocaltimeRot = k.LocaltimeSc = 0.0f;
      k.LocaltimePosLerp = k.LocaltimeRotLerp = k.LocaltimeScLerp = 0.0f;
      k.MaxIndexPos = static_cast<unsigned int>(bone.PositionKeys.size());
      k.MaxIndexRot = static_cast<unsigned int>(bone.RotationKeys.size());
      k.MaxIndexSc  = static_cast<unsigned int>(bone.ScaleKeys.size());
    }
  }
}

void AnimationController::Update(float deltaTime) {
  if (!m_initialized || !m_pAnimInfo) return;
  if (m_pAnimInfo->Animations.empty()) return;

  float dt = deltaTime * m_speed;
  m_localTime += dt;

  xF::xAnimationSet* pAS = &m_pAnimInfo->Animations[m_currentSet];
  float maxTick = static_cast<float>(pAS->m_MaxTimeOnTicks);
  if (maxTick <= 0.0f) maxTick = 1.0f;

  float tickTime = m_ticksPerSecond * m_localTime;

  // Wrap time for looping animations
  if (m_looping && tickTime > maxTick) {
    tickTime = std::fmod(tickTime, maxTick);
    m_localTime = tickTime / m_ticksPerSecond;
  } else if (!m_looping && tickTime > maxTick) {
    tickTime = maxTick;
  }

  InterpolateKeys(tickTime);
  ComputeHierarchy();
  ComputeFinalMatrices();
}

// ── Keyframe interpolation ──────────────────────────────

void AnimationController::InterpolateKeys(float tickTime) {
  xF::xAnimationSet* pAS = &m_pAnimInfo->Animations[m_currentSet];

  for (auto& bone : pAS->BonesRef) {
    auto& k = bone.ActualKey;

    // ── Position ──
    if (k.MaxIndexPos == 0) {
      k.PositionKey.Position = XVECTOR3(0.0f, 0.0f, 0.0f);
    } else if (k.MaxIndexPos == 1) {
      k.PositionKey.Position = bone.PositionKeys[0].Position;
    } else {
      // Find the two surrounding keyframes for tickTime
      unsigned int idx = 0;
      for (unsigned int j = 0; j + 1 < k.MaxIndexPos; j++) {
        if (tickTime < static_cast<float>(bone.PositionKeys[j + 1].t.i_atTime)) { idx = j; break; }
        idx = j;
      }
      unsigned int nextIdx = idx + 1;
      if (nextIdx >= k.MaxIndexPos) nextIdx = 0; // wrap for loop interpolation
      float curTick  = static_cast<float>(bone.PositionKeys[idx].t.i_atTime);
      float nextTick = (nextIdx > idx)
        ? static_cast<float>(bone.PositionKeys[nextIdx].t.i_atTime)
        : static_cast<float>(pAS->m_MaxTimeOnTicks); // wrap to end
      float span = nextTick - curTick;
      float t = (span > 0.0f) ? (tickTime - curTick) / span : 0.0f;
      t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
      auto& cur  = bone.PositionKeys[idx];
      auto& next = bone.PositionKeys[nextIdx];
      k.PositionKey.Position.x = cur.Position.x + (next.Position.x - cur.Position.x) * t;
      k.PositionKey.Position.y = cur.Position.y + (next.Position.y - cur.Position.y) * t;
      k.PositionKey.Position.z = cur.Position.z + (next.Position.z - cur.Position.z) * t;
    }

    // ── Rotation (with SLERP) ──
    if (k.MaxIndexRot == 0) {
      k.RotationKey.Rot = XQUATERNION(0.0f, 0.0f, 0.0f, 1.0f);
    } else if (k.MaxIndexRot == 1) {
      k.RotationKey.Rot = bone.RotationKeys[0].Rot;
    } else {
      unsigned int idx = 0;
      for (unsigned int j = 0; j + 1 < k.MaxIndexRot; j++) {
        if (tickTime < static_cast<float>(bone.RotationKeys[j + 1].t.i_atTime)) { idx = j; break; }
        idx = j;
      }
      unsigned int nextIdx = idx + 1;
      if (nextIdx >= k.MaxIndexRot) nextIdx = 0;
      float curTick  = static_cast<float>(bone.RotationKeys[idx].t.i_atTime);
      float nextTick = (nextIdx > idx)
        ? static_cast<float>(bone.RotationKeys[nextIdx].t.i_atTime)
        : static_cast<float>(pAS->m_MaxTimeOnTicks);
      float span = nextTick - curTick;
      float t = (span > 0.0f) ? (tickTime - curTick) / span : 0.0f;
      t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
      k.RotationKey.Rot = m_useSlerp
        ? Slerp(bone.RotationKeys[idx].Rot, bone.RotationKeys[nextIdx].Rot, t)
        : Nlerp(bone.RotationKeys[idx].Rot, bone.RotationKeys[nextIdx].Rot, t);
    }

    // ── Scale ──
    if (k.MaxIndexSc == 0) {
      k.ScaleKey.Scale = XVECTOR3(1.0f, 1.0f, 1.0f);
    } else if (k.MaxIndexSc == 1) {
      k.ScaleKey.Scale = bone.ScaleKeys[0].Scale;
    } else {
      unsigned int idx = 0;
      for (unsigned int j = 0; j + 1 < k.MaxIndexSc; j++) {
        if (tickTime < static_cast<float>(bone.ScaleKeys[j + 1].t.i_atTime)) { idx = j; break; }
        idx = j;
      }
      unsigned int nextIdx = idx + 1;
      if (nextIdx >= k.MaxIndexSc) nextIdx = 0;
      float curTick  = static_cast<float>(bone.ScaleKeys[idx].t.i_atTime);
      float nextTick = (nextIdx > idx)
        ? static_cast<float>(bone.ScaleKeys[nextIdx].t.i_atTime)
        : static_cast<float>(pAS->m_MaxTimeOnTicks);
      float span = nextTick - curTick;
      float t = (span > 0.0f) ? (tickTime - curTick) / span : 0.0f;
      t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
      auto& cur  = bone.ScaleKeys[idx];
      auto& next = bone.ScaleKeys[nextIdx];
      k.ScaleKey.Scale.x = cur.Scale.x + (next.Scale.x - cur.Scale.x) * t;
      k.ScaleKey.Scale.y = cur.Scale.y + (next.Scale.y - cur.Scale.y) * t;
      k.ScaleKey.Scale.z = cur.Scale.z + (next.Scale.z - cur.Scale.z) * t;
    }

    // Build local transform from interpolated S * R * T (row-vector convention)
    XMATRIX44 S, R, T;
    XMatScaling(S, k.ScaleKey.Scale.x, k.ScaleKey.Scale.y, k.ScaleKey.Scale.z);
    R = QuaternionToMatrix(k.RotationKey.Rot);
    XMatTranslation(T, k.PositionKey.Position.x,
                       k.PositionKey.Position.y,
                       k.PositionKey.Position.z);
    bone.MatrixfromKeys = S * R * T;

    // Write to animated skeleton
    if (bone.BoneID < static_cast<unsigned int>(m_numBones)) {
      m_pSkeletonAnim->Bones[bone.BoneID].Bone = bone.MatrixfromKeys;
    }
  }
}

// ── Keyframe snap: set each bone to the nearest keyframe (no interpolation) ──

void AnimationController::ApplyKeyframeSnap(float tickTime) {
  xF::xAnimationSet* pAS = &m_pAnimInfo->Animations[m_currentSet];

  for (auto& bone : pAS->BonesRef) {
    auto& k = bone.ActualKey;

    // Position: find nearest key <= tickTime
    if (!bone.PositionKeys.empty()) {
      unsigned int idx = 0;
      for (unsigned int j = 0; j < static_cast<unsigned int>(bone.PositionKeys.size()); j++) {
        if (static_cast<float>(bone.PositionKeys[j].t.i_atTime) <= tickTime) idx = j;
        else break;
      }
      k.PositionKey.Position = bone.PositionKeys[idx].Position;
    }

    // Rotation: find nearest key <= tickTime
    if (!bone.RotationKeys.empty()) {
      unsigned int idx = 0;
      for (unsigned int j = 0; j < static_cast<unsigned int>(bone.RotationKeys.size()); j++) {
        if (static_cast<float>(bone.RotationKeys[j].t.i_atTime) <= tickTime) idx = j;
        else break;
      }
      k.RotationKey.Rot = bone.RotationKeys[idx].Rot;
    }

    // Scale: find nearest key <= tickTime
    if (!bone.ScaleKeys.empty()) {
      unsigned int idx = 0;
      for (unsigned int j = 0; j < static_cast<unsigned int>(bone.ScaleKeys.size()); j++) {
        if (static_cast<float>(bone.ScaleKeys[j].t.i_atTime) <= tickTime) idx = j;
        else break;
      }
      k.ScaleKey.Scale = bone.ScaleKeys[idx].Scale;
    }

    XMATRIX44 S, R, T;
    XMatScaling(S, k.ScaleKey.Scale.x, k.ScaleKey.Scale.y, k.ScaleKey.Scale.z);
    R = QuaternionToMatrix(k.RotationKey.Rot);
    XMatTranslation(T, k.PositionKey.Position.x,
                       k.PositionKey.Position.y,
                       k.PositionKey.Position.z);
    bone.MatrixfromKeys = S * R * T;

    if (bone.BoneID < static_cast<unsigned int>(m_numBones)) {
      m_pSkeletonAnim->Bones[bone.BoneID].Bone = bone.MatrixfromKeys;
    }
  }
}

// ── Skeleton hierarchy: compute combined matrices ──────

void AnimationController::ComputeHierarchy() {
  if (!m_pSkeletonAnim) return;

  auto& bones = m_pSkeletonAnim->Bones;
  int n = (m_numBones < static_cast<int>(bones.size()))
        ? m_numBones : static_cast<int>(bones.size());

  // First pass: root bones (Dad == 0 and index != 0 means parent is bone 0,
  // but if index == 0 and Dad == 0 then it's the true root)
  // Actually the convention is: Dad is the parent index.
  // We iterate in order, assuming parents come before children (common for
  // glTF which stores joints in topological order).
  //
  // Root bones include the non-skeleton ancestor world transform
  // (RootParentWorld) so that the combined matrix matches the IBM.
  const XMATRIX44& rootWorld = m_pSkeletonAnim->RootParentWorld;

  for (int i = 0; i < n; i++) {
    // Apply intermediate non-joint transforms between this joint and its parent joint
    // Combined = (Bone * Intermediate) * parent.Combined
    XMATRIX44 localWithIntermediate = bones[i].Bone * bones[i].IntermediateTransform;

    if (i == 0 || bones[i].Dad == static_cast<unsigned short>(i)) {
      // Root bone: combined = local * ancestorWorld
      bones[i].Combined = localWithIntermediate * rootWorld;
    } else {
      // Child: combined = local * parent.combined (row-vector convention)
      unsigned short dad = bones[i].Dad;
      if (dad < n) {
        bones[i].Combined = localWithIntermediate * bones[dad].Combined;
      } else {
        bones[i].Combined = localWithIntermediate;
      }
    }
  }
}

namespace {
  bool InvertAffineFull(const XMATRIX44& matrix, XMATRIX44& out) {
    const float a00 = matrix.m11, a01 = matrix.m12, a02 = matrix.m13;
    const float a10 = matrix.m21, a11 = matrix.m22, a12 = matrix.m23;
    const float a20 = matrix.m31, a21 = matrix.m32, a22 = matrix.m33;

    const float det =
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);
    if (std::fabs(det) <= 0.000001f) {
      return false;
    }

    const float invDet = 1.0f / det;
    out.m11 =  (a11 * a22 - a12 * a21) * invDet;
    out.m12 =  (a02 * a21 - a01 * a22) * invDet;
    out.m13 =  (a01 * a12 - a02 * a11) * invDet;
    out.m14 = 0.0f;
    out.m21 =  (a12 * a20 - a10 * a22) * invDet;
    out.m22 =  (a00 * a22 - a02 * a20) * invDet;
    out.m23 =  (a02 * a10 - a00 * a12) * invDet;
    out.m24 = 0.0f;
    out.m31 =  (a10 * a21 - a11 * a20) * invDet;
    out.m32 =  (a01 * a20 - a00 * a21) * invDet;
    out.m33 =  (a00 * a11 - a01 * a10) * invDet;
    out.m34 = 0.0f;

    const float tx = matrix.m41;
    const float ty = matrix.m42;
    const float tz = matrix.m43;
    out.m41 = -(tx * out.m11 + ty * out.m21 + tz * out.m31);
    out.m42 = -(tx * out.m12 + ty * out.m22 + tz * out.m32);
    out.m43 = -(tx * out.m13 + ty * out.m23 + tz * out.m33);
    out.m44 = 1.0f;
    return true;
  }
}

bool AnimationController::ApplyCombinedPoseOverrides(const std::vector<int>& boneIndices,
                                                     const std::vector<XMATRIX44>& combinedMatrices) {
  if (!m_initialized || !m_pSkeletonAnim || boneIndices.size() != combinedMatrices.size()) {
    return false;
  }

  auto& bones = m_pSkeletonAnim->Bones;
  const int n = (m_numBones < static_cast<int>(bones.size()))
      ? m_numBones
      : static_cast<int>(bones.size());
  if (n <= 0) {
    return false;
  }

  std::vector<unsigned char> hasOverride(static_cast<std::size_t>(n), 0);
  std::vector<XMATRIX44> overrideCombined(static_cast<std::size_t>(n));
  for (std::size_t i = 0; i < boneIndices.size(); ++i) {
    const int boneIndex = boneIndices[i];
    if (boneIndex < 0 || boneIndex >= n) {
      continue;
    }
    hasOverride[static_cast<std::size_t>(boneIndex)] = 1;
    overrideCombined[static_cast<std::size_t>(boneIndex)] = combinedMatrices[i];
  }

  bool appliedAny = false;
  const XMATRIX44& rootWorld = m_pSkeletonAnim->RootParentWorld;
  for (int i = 0; i < n; ++i) {
    if (hasOverride[static_cast<std::size_t>(i)]) {
      const XMATRIX44& parentCombined =
          (i == 0 || bones[i].Dad == static_cast<unsigned short>(i) || bones[i].Dad >= n)
              ? rootWorld
              : bones[bones[i].Dad].Combined;

      XMATRIX44 inverseParent;
      XMATRIX44 inverseIntermediate;
      if (InvertAffineFull(parentCombined, inverseParent) &&
          InvertAffineFull(bones[i].IntermediateTransform, inverseIntermediate)) {
        const XMATRIX44 localWithIntermediate = overrideCombined[static_cast<std::size_t>(i)] * inverseParent;
        bones[i].Bone = localWithIntermediate * inverseIntermediate;
        appliedAny = true;
      }
    }

    const XMATRIX44 localWithIntermediate = bones[i].Bone * bones[i].IntermediateTransform;
    if (i == 0 || bones[i].Dad == static_cast<unsigned short>(i)) {
      bones[i].Combined = localWithIntermediate * rootWorld;
    } else {
      const unsigned short dad = bones[i].Dad;
      bones[i].Combined = dad < n
          ? localWithIntermediate * bones[dad].Combined
          : localWithIntermediate;
    }
  }

  if (appliedAny) {
    ComputeFinalMatrices();
  }
  return appliedAny;
}

// ── Invert an affine 4x4 matrix (rotation + translation) ──

XMATRIX44 AnimationController::InvertAffine(const XMATRIX44& m) {
  // For an affine matrix [R|0; T|1] in row-vector convention:
  // Inverse = [R^T|0; -T*R^T|1]
  XMATRIX44 inv;
  // Transpose the 3x3 rotation part
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
      inv.m[r][c] = m.m[c][r];
  inv.m[0][3] = 0; inv.m[1][3] = 0; inv.m[2][3] = 0;
  // Compute -T * R^T (row-vector: new translation = -oldTrans * transposedRot)
  float tx = m.m[3][0], ty = m.m[3][1], tz = m.m[3][2];
  inv.m[3][0] = -(tx*inv.m[0][0] + ty*inv.m[1][0] + tz*inv.m[2][0]);
  inv.m[3][1] = -(tx*inv.m[0][1] + ty*inv.m[1][1] + tz*inv.m[2][1]);
  inv.m[3][2] = -(tx*inv.m[0][2] + ty*inv.m[1][2] + tz*inv.m[2][2]);
  inv.m[3][3] = 1.0f;
  return inv;
}

// ── Compute bind-pose hierarchy and our own IBM ──────────

void AnimationController::ComputeBindPose() {
  if (!m_pSkeletonBind || !m_pSkeletonAnim) return;

  auto& bindBones = m_pSkeletonBind->Bones;
  int n = (m_numBones < static_cast<int>(bindBones.size()))
        ? m_numBones : static_cast<int>(bindBones.size());

  const XMATRIX44& rootWorld = m_pSkeletonBind->RootParentWorld;

  // Compute bind-pose combined (world) matrices from the ORIGINAL node TRS
  for (int i = 0; i < n; i++) {
    XMATRIX44 localWithIntermediate = bindBones[i].Bone * bindBones[i].IntermediateTransform;

    if (i == 0 || bindBones[i].Dad == static_cast<unsigned short>(i)) {
      bindBones[i].Combined = localWithIntermediate * rootWorld;
    } else {
      unsigned short dad = bindBones[i].Dad;
      if (dad < n)
        bindBones[i].Combined = localWithIntermediate * bindBones[dad].Combined;
      else
        bindBones[i].Combined = localWithIntermediate;
    }
    // Compute our own IBM as the exact inverse of the bind-pose combined
    m_invBindPose[i] = InvertAffine(bindBones[i].Combined);
  }

  T8_LOG_INFO("[AnimCtrl] Bind pose computed: %d bones, own IBMs generated", n);
}

// ── Compute final bone matrices for shader ─────────────
// Skinning operates in RH space (IBM and skeleton are in RH row-vector).
// The Z-flip (RH→LH) is applied once to the final product so it matches
// the LH vertex positions in the vertex buffer.

static XMATRIX44 FlipMatrixZ(const XMATRIX44& m) {
  XMATRIX44 r = m;
  for (int i = 0; i < 4; i++) {
    r.m[2][i] = -r.m[2][i];
    r.m[i][2] = -r.m[i][2];
  }
  r.m[2][2] = m.m[2][2]; // double-negated → restore
  return r;
}

void AnimationController::ComputeFinalMatrices() {
  if (!m_pSkeletonAnim) return;

  auto& bones = m_pSkeletonAnim->Bones;
  int n = m_numBones;

  // Use glTF's inverseBindMatrices when available (matches reference viewer),
  // fall back to our own computed IBM otherwise.
  bool useGltfIBM = (m_pSkinWeights != nullptr &&
                     !m_pSkinWeights->empty());

  for (int i = 0; i < n && i < static_cast<int>(bones.size()); i++) {
    XMATRIX44 rhResult;
    if (useGltfIBM && i < static_cast<int>(m_pSkinWeights->size())) {
      rhResult = (*m_pSkinWeights)[i].MatrixOffset * bones[i].Combined;
    } else {
      rhResult = m_invBindPose[i] * bones[i].Combined;
    }
    XMATRIX44 lh = FlipMatrixZ(rhResult);
    m_finalBoneMatrices[i] = lh;

    // Extract quaternion+translation from the final matrix.
    // The 3x3 upper-left is a rotation (possibly with minor shear from blending).
    // Extract translation from row 3 (row-vector convention).
    const XMATRIX44& fm = m_finalBoneMatrices[i];
    m_finalBoneTrans[i] = XVECTOR3(fm.m[3][0], fm.m[3][1], fm.m[3][2], 0.0f);

    // Extract quaternion from 3x3 rotation using Shepperd's method.
    float tr = fm.m[0][0] + fm.m[1][1] + fm.m[2][2];
    if (tr > 0.0f) {
      float s = std::sqrt(tr + 1.0f) * 2.0f;
      m_finalBoneQuats[i] = XQUATERNION(
        (fm.m[1][2] - fm.m[2][1]) / s,
        (fm.m[2][0] - fm.m[0][2]) / s,
        (fm.m[0][1] - fm.m[1][0]) / s,
        0.25f * s);
    } else if (fm.m[0][0] > fm.m[1][1] && fm.m[0][0] > fm.m[2][2]) {
      float s = std::sqrt(1.0f + fm.m[0][0] - fm.m[1][1] - fm.m[2][2]) * 2.0f;
      m_finalBoneQuats[i] = XQUATERNION(
        0.25f * s,
        (fm.m[0][1] + fm.m[1][0]) / s,
        (fm.m[2][0] + fm.m[0][2]) / s,
        (fm.m[1][2] - fm.m[2][1]) / s);
    } else if (fm.m[1][1] > fm.m[2][2]) {
      float s = std::sqrt(1.0f + fm.m[1][1] - fm.m[0][0] - fm.m[2][2]) * 2.0f;
      m_finalBoneQuats[i] = XQUATERNION(
        (fm.m[0][1] + fm.m[1][0]) / s,
        0.25f * s,
        (fm.m[1][2] + fm.m[2][1]) / s,
        (fm.m[2][0] - fm.m[0][2]) / s);
    } else {
      float s = std::sqrt(1.0f + fm.m[2][2] - fm.m[0][0] - fm.m[1][1]) * 2.0f;
      m_finalBoneQuats[i] = XQUATERNION(
        (fm.m[2][0] + fm.m[0][2]) / s,
        (fm.m[1][2] + fm.m[2][1]) / s,
        0.25f * s,
        (fm.m[0][1] - fm.m[1][0]) / s);
    }
  }
  // Remaining slots stay identity (quat=(0,0,0,1), trans=(0,0,0))
}

// ── SLERP ───────────────────────────────────────────────

XQUATERNION AnimationController::Slerp(const XQUATERNION& a,
                                        const XQUATERNION& b, float t) {
  float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;

  XQUATERNION b2 = b;
  if (dot < 0.0f) {
    dot = -dot;
    b2.x = -b2.x; b2.y = -b2.y; b2.z = -b2.z; b2.w = -b2.w;
  }

  if (dot > 0.9995f) {
    return Nlerp(a, b2, t);
  }

  float theta = std::acos(dot);
  float sinTheta = std::sin(theta);
  float wa = std::sin((1.0f - t) * theta) / sinTheta;
  float wb = std::sin(t * theta) / sinTheta;

  return XQUATERNION(
    wa * a.x + wb * b2.x,
    wa * a.y + wb * b2.y,
    wa * a.z + wb * b2.z,
    wa * a.w + wb * b2.w);
}

// ── NLERP (Normalized LERP) ─────────────────────────────

XQUATERNION AnimationController::Nlerp(const XQUATERNION& a,
                                        const XQUATERNION& b, float t) {
  float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
  XQUATERNION b2 = b;
  if (dot < 0.0f) {
    b2.x = -b2.x; b2.y = -b2.y; b2.z = -b2.z; b2.w = -b2.w;
  }
  XQUATERNION r(
    a.x + t * (b2.x - a.x),
    a.y + t * (b2.y - a.y),
    a.z + t * (b2.z - a.z),
    a.w + t * (b2.w - a.w));
  float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
  if (len > 1e-8f) { r.x/=len; r.y/=len; r.z/=len; r.w/=len; }
  return r;
}

// ── Quaternion → ROW-VECTOR rotation matrix ─────────────
// Must match MakeTRS in GLTFMesh.cpp: r01=2*(xy+wz), r10=2*(xy-wz)

XMATRIX44 AnimationController::QuaternionToMatrix(const XQUATERNION& q) {
  float x2 = q.x * q.x, y2 = q.y * q.y, z2 = q.z * q.z;
  float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

  XMATRIX44 m;
  m.m[0][0] = 1.0f - 2.0f*(y2+z2); m.m[0][1] = 2.0f*(xy+wz);        m.m[0][2] = 2.0f*(xz-wy);        m.m[0][3] = 0.0f;
  m.m[1][0] = 2.0f*(xy-wz);        m.m[1][1] = 1.0f - 2.0f*(x2+z2); m.m[1][2] = 2.0f*(yz+wx);        m.m[1][3] = 0.0f;
  m.m[2][0] = 2.0f*(xz+wy);        m.m[2][1] = 2.0f*(yz-wx);        m.m[2][2] = 1.0f - 2.0f*(x2+y2); m.m[2][3] = 0.0f;
  m.m[3][0] = 0.0f;                m.m[3][1] = 0.0f;                m.m[3][2] = 0.0f;                m.m[3][3] = 1.0f;
  return m;
}

// ── Debug matrix dump ───────────────────────────────────

void AnimationController::DumpMatrices(const char* filename) const {
  if (!m_initialized) return;
  FILE* f = fopen(filename, "w");
  if (!f) { T8_LOG_ERROR("[AnimCtrl] DumpMatrices: cannot open '%s'", filename); return; }

  fprintf(f, "=== AnimationController Dump ===\n");
  fprintf(f, "NumBones: %d  TicksPerSecond: %.0f  CurrentSet: %d/%d\n",
          m_numBones, m_ticksPerSecond, m_currentSet,
          m_pAnimInfo ? (int)m_pAnimInfo->Animations.size() : 0);

  if (m_pSkeletonAnim) {
    fprintf(f, "\n=== Skeleton (Bind Pose + Current) ===\n");
    fprintf(f, "RootParentWorld diag=(%.4f,%.4f,%.4f) trans=(%.4f,%.4f,%.4f)\n",
            m_pSkeletonAnim->RootParentWorld.m[0][0], m_pSkeletonAnim->RootParentWorld.m[1][1],
            m_pSkeletonAnim->RootParentWorld.m[2][2],
            m_pSkeletonAnim->RootParentWorld.m[3][0], m_pSkeletonAnim->RootParentWorld.m[3][1],
            m_pSkeletonAnim->RootParentWorld.m[3][2]);

    int n = m_numBones < (int)m_pSkeletonAnim->Bones.size()
          ? m_numBones : (int)m_pSkeletonAnim->Bones.size();
    for (int i = 0; i < n; i++) {
      const auto& b = m_pSkeletonAnim->Bones[i];
      fprintf(f, "\nBone[%d] '%s' Dad=%d isRoot=%s\n", i, b.Name.c_str(), (int)b.Dad,
              (b.Dad == (unsigned short)i) ? "YES" : "no");
      fprintf(f, "  Local (Bone):\n");
      for (int r = 0; r < 4; r++)
        fprintf(f, "    [%.6f, %.6f, %.6f, %.6f]\n", b.Bone.m[r][0], b.Bone.m[r][1], b.Bone.m[r][2], b.Bone.m[r][3]);
      fprintf(f, "  Combined (World):\n");
      for (int r = 0; r < 4; r++)
        fprintf(f, "    [%.6f, %.6f, %.6f, %.6f]\n", b.Combined.m[r][0], b.Combined.m[r][1], b.Combined.m[r][2], b.Combined.m[r][3]);
    }
  }

  if (m_pSkinWeights) {
    fprintf(f, "\n=== Inverse Bind Matrices ===\n");
    int n = m_numBones < (int)m_pSkinWeights->size()
          ? m_numBones : (int)m_pSkinWeights->size();
    for (int i = 0; i < n; i++) {
      const auto& sw = (*m_pSkinWeights)[i];
      fprintf(f, "\nIBM[%d] '%s':\n", i, sw.NodeName.c_str());
      for (int r = 0; r < 4; r++)
        fprintf(f, "    [%.6f, %.6f, %.6f, %.6f]\n", sw.MatrixOffset.m[r][0], sw.MatrixOffset.m[r][1], sw.MatrixOffset.m[r][2], sw.MatrixOffset.m[r][3]);
    }
  }

  fprintf(f, "\n=== Final Bone Matrices (sent to GPU) ===\n");
  for (int i = 0; i < m_numBones; i++) {
    const auto& fm = m_finalBoneMatrices[i];
    // Check if near identity
    float diagSum = fm.m[0][0] + fm.m[1][1] + fm.m[2][2];
    float offDiag = std::abs(fm.m[0][1]) + std::abs(fm.m[0][2]) + std::abs(fm.m[1][0])
                  + std::abs(fm.m[1][2]) + std::abs(fm.m[2][0]) + std::abs(fm.m[2][1]);
    bool nearIdentity = (std::abs(diagSum - 3.0f) < 0.01f && offDiag < 0.01f
                        && std::abs(fm.m[3][0]) < 0.01f && std::abs(fm.m[3][1]) < 0.01f
                        && std::abs(fm.m[3][2]) < 0.01f);
    fprintf(f, "\nFinal[%d]%s:\n", i, nearIdentity ? " ~IDENTITY" : "");
    for (int r = 0; r < 4; r++)
      fprintf(f, "    [%.6f, %.6f, %.6f, %.6f]\n", fm.m[r][0], fm.m[r][1], fm.m[r][2], fm.m[r][3]);
  }

  fclose(f);
  T8_LOG_INFO("[AnimCtrl] Dumped matrices to '%s'", filename);
}

} // namespace t850
