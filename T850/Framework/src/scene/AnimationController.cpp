#include "pch.h"
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

namespace t800 {

AnimationController::AnimationController() {
  for (int i = 0; i < kMaxBones; i++) {
    m_finalBoneMatrices[i].Identity();
    m_invBindPose[i].Identity();
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
  ResetLocals();
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
  InterpolateKeys(dt);
  ComputeHierarchy();
  ComputeFinalMatrices();
}

// ── Keyframe interpolation ──────────────────────────────

void AnimationController::InterpolateKeys(float dt) {
  xF::xAnimationSet* pAS = &m_pAnimInfo->Animations[m_currentSet];

  bool allFinished = true;

  for (auto& bone : pAS->BonesRef) {
    auto& k = bone.ActualKey;
    k.LocaltimePos += dt;
    k.LocaltimeRot += dt;
    k.LocaltimeSc  += dt;

    // ── Position ──
    if (k.MaxIndexPos == 0) {
      k.PositionKey.Position = XVECTOR3(0.0f, 0.0f, 0.0f);
      k.StatePos = xF::xAnimationSingleKey::FINISHED;
    } else if (k.MaxIndexPos == 1) {
      k.PositionKey.Position = bone.PositionKeys[0].Position;
      k.StatePos = xF::xAnimationSingleKey::FINISHED;
    } else {
      if (k.LocalIndexPos + 1 < k.MaxIndexPos) {
        k.StatePos = xF::xAnimationSingleKey::RUNNING;
        auto& cur  = bone.PositionKeys[k.LocalIndexPos];
        auto& next = bone.PositionKeys[k.LocalIndexPos + 1];
        float curTick  = static_cast<float>(cur.t.i_atTime);
        float nextTick = static_cast<float>(next.t.i_atTime);
        float elapsed  = m_ticksPerSecond * k.LocaltimePos;

        if (elapsed >= nextTick) {
          k.LocalIndexPos++;
          k.PositionKey.Position = next.Position;
        } else {
          float span = nextTick - curTick;
          float t = (span > 0.0f) ? (elapsed - curTick) / span : 0.0f;
          t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
          k.PositionKey.Position.x = cur.Position.x + (next.Position.x - cur.Position.x) * t;
          k.PositionKey.Position.y = cur.Position.y + (next.Position.y - cur.Position.y) * t;
          k.PositionKey.Position.z = cur.Position.z + (next.Position.z - cur.Position.z) * t;
        }
      } else {
        k.StatePos = xF::xAnimationSingleKey::FINISHED;
        k.PositionKey.Position = bone.PositionKeys[k.MaxIndexPos - 1].Position;
      }
    }

    // ── Rotation (with SLERP) ──
    if (k.MaxIndexRot == 0) {
      k.RotationKey.Rot = XQUATERNION(0.0f, 0.0f, 0.0f, 1.0f);
      k.StateRot = xF::xAnimationSingleKey::FINISHED;
    } else if (k.MaxIndexRot == 1) {
      k.RotationKey.Rot = bone.RotationKeys[0].Rot;
      k.StateRot = xF::xAnimationSingleKey::FINISHED;
    } else {
      if (k.LocalIndexRot + 1 < k.MaxIndexRot) {
        k.StateRot = xF::xAnimationSingleKey::RUNNING;
        auto& cur  = bone.RotationKeys[k.LocalIndexRot];
        auto& next = bone.RotationKeys[k.LocalIndexRot + 1];
        float curTick  = static_cast<float>(cur.t.i_atTime);
        float nextTick = static_cast<float>(next.t.i_atTime);
        float elapsed  = m_ticksPerSecond * k.LocaltimeRot;

        if (elapsed >= nextTick) {
          k.LocalIndexRot++;
          k.RotationKey.Rot = next.Rot;
        } else {
          float span = nextTick - curTick;
          float t = (span > 0.0f) ? (elapsed - curTick) / span : 0.0f;
          t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
          k.RotationKey.Rot = m_useSlerp ? Slerp(cur.Rot, next.Rot, t) : Nlerp(cur.Rot, next.Rot, t);
        }
      } else {
        k.StateRot = xF::xAnimationSingleKey::FINISHED;
        k.RotationKey.Rot = bone.RotationKeys[k.MaxIndexRot - 1].Rot;
      }
    }

    // ── Scale ──
    if (k.MaxIndexSc == 0) {
      k.ScaleKey.Scale = XVECTOR3(1.0f, 1.0f, 1.0f);
      k.StateSc = xF::xAnimationSingleKey::FINISHED;
    } else if (k.MaxIndexSc == 1) {
      k.ScaleKey.Scale = bone.ScaleKeys[0].Scale;
      k.StateSc = xF::xAnimationSingleKey::FINISHED;
    } else {
      if (k.LocalIndexSc + 1 < k.MaxIndexSc) {
        k.StateSc = xF::xAnimationSingleKey::RUNNING;
        auto& cur  = bone.ScaleKeys[k.LocalIndexSc];
        auto& next = bone.ScaleKeys[k.LocalIndexSc + 1];
        float curTick  = static_cast<float>(cur.t.i_atTime);
        float nextTick = static_cast<float>(next.t.i_atTime);
        float elapsed  = m_ticksPerSecond * k.LocaltimeSc;

        if (elapsed >= nextTick) {
          k.LocalIndexSc++;  // Fixed: was LocalIndexRot++ in legacy code
          k.ScaleKey.Scale = next.Scale;
        } else {
          float span = nextTick - curTick;
          float t = (span > 0.0f) ? (elapsed - curTick) / span : 0.0f;
          t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
          k.ScaleKey.Scale.x = cur.Scale.x + (next.Scale.x - cur.Scale.x) * t;
          k.ScaleKey.Scale.y = cur.Scale.y + (next.Scale.y - cur.Scale.y) * t;
          k.ScaleKey.Scale.z = cur.Scale.z + (next.Scale.z - cur.Scale.z) * t;
        }
      } else {
        k.StateSc = xF::xAnimationSingleKey::FINISHED;
        k.ScaleKey.Scale = bone.ScaleKeys[k.MaxIndexSc - 1].Scale;
      }
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

    if (k.StatePos != xF::xAnimationSingleKey::FINISHED ||
        k.StateRot != xF::xAnimationSingleKey::FINISHED ||
        k.StateSc  != xF::xAnimationSingleKey::FINISHED) {
      allFinished = false;
    }
  }

  if (allFinished && m_looping) {
    ResetLocals();
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
    if (i == 0 || bones[i].Dad == static_cast<unsigned short>(i)) {
      // Root bone: combined = local * ancestorWorld
      bones[i].Combined = bones[i].Bone * rootWorld;
    } else {
      // Child: combined = local * parent.combined (row-vector convention)
      unsigned short dad = bones[i].Dad;
      if (dad < n) {
        bones[i].Combined = bones[i].Bone * bones[dad].Combined;
      } else {
        bones[i].Combined = bones[i].Bone;
      }
    }
  }
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
    if (i == 0 || bindBones[i].Dad == static_cast<unsigned short>(i)) {
      bindBones[i].Combined = bindBones[i].Bone * rootWorld;
    } else {
      unsigned short dad = bindBones[i].Dad;
      if (dad < n)
        bindBones[i].Combined = bindBones[i].Bone * bindBones[dad].Combined;
      else
        bindBones[i].Combined = bindBones[i].Bone;
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
  int n = m_numBones < static_cast<int>(bones.size())
        ? m_numBones : static_cast<int>(bones.size());

  for (int i = 0; i < n; i++) {
    // FinalBoneMatrix = FlipZ( OurIBM[i] * AnimCombined[i] )
    // Using our own IBM (exact inverse of bind-pose combined) instead of
    // the glTF file's IBM avoids accumulated numerical drift in long chains.
    XMATRIX44 rhResult = m_invBindPose[i] * bones[i].Combined;
    m_finalBoneMatrices[i] = FlipMatrixZ(rhResult);
  }
  // Remaining slots stay identity
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

// ── Quaternion → row-major rotation matrix ──────────────

XMATRIX44 AnimationController::QuaternionToMatrix(const XQUATERNION& q) {
  float x2 = q.x * q.x, y2 = q.y * q.y, z2 = q.z * q.z;
  float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

  XMATRIX44 m;
  m.m[0][0] = 1.0f - 2.0f*(y2+z2); m.m[0][1] = 2.0f*(xy-wz);        m.m[0][2] = 2.0f*(xz+wy);        m.m[0][3] = 0.0f;
  m.m[1][0] = 2.0f*(xy+wz);        m.m[1][1] = 1.0f - 2.0f*(x2+z2); m.m[1][2] = 2.0f*(yz-wx);        m.m[1][3] = 0.0f;
  m.m[2][0] = 2.0f*(xz-wy);        m.m[2][1] = 2.0f*(yz+wx);        m.m[2][2] = 1.0f - 2.0f*(x2+y2); m.m[2][3] = 0.0f;
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

} // namespace t800
