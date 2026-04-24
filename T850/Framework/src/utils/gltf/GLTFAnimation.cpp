#include "pch.h"
/*********************************************************
 * glTF 2.0 — skin / animation conversion.
 *
 * Converts glTF skins and animations into the engine's
 * xSkeleton / xAnimationInfo / xSkinWeights structures.
 *
 * Handles:
 *   - Building skeleton hierarchy from doc.skins[*].joints
 *   - Reading inverseBindMatrices into xSkinWeights.MatrixOffset
 *   - Converting AnimationChannels into xAnimationBone keys
 *   - LINEAR interpolation (STEP/CUBICSPLINE baked to LINEAR)
 *   - RH→LH coordinate conversion
 *********************************************************/

#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFTypes.h>
#include <utils/gltf/GLTFAccessor.h>
#include <utils/XDataBase.h>
#include <utils/Log.h>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace t800 {
namespace gltf {

static constexpr bool kFlipToLeftHanded = true;
static constexpr float kTicksPerSecond = 4800.0f;

// ── Helper: glTF column-major mat4 → engine row-major XMATRIX44 ─────
// glTF uses column-vector convention (v' = M * v), stored column-major.
// Engine uses row-vector convention (v' = v * M), stored row-major.
// Convert glTF column-major mat4 → engine row-major XMATRIX44.
// Direct copy: matches FromColumnMajor16 in GLTFMesh.cpp which is
// used by the working static mesh path.
static XMATRIX44 ColMajorToRowMajor(const float* cm) {
  XMATRIX44 m;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      m.m[r][c] = cm[r * 4 + c];  // direct copy (matches FromColumnMajor16)
  return m;
}

// ── Helper: build a local TRS matrix from a glTF node ───────────────
static XMATRIX44 NodeLocalMatrix(const Node& n) {
  if (!n.matrix.empty() && n.matrix.size() == 16) {
    return ColMajorToRowMajor(n.matrix.data());
  }
  XMATRIX44 S, R, T;
  // Scale
  if (n.scale.size() == 3) {
    XMatScaling(S, n.scale[0], n.scale[1], n.scale[2]);
  } else {
    S.Identity();
  }
  // Rotation (quaternion xyzw → ROW-VECTOR rotation matrix)
  // Must match MakeTRS in GLTFMesh.cpp: r01=2*(xy+wz), r10=2*(xy-wz)
  if (n.rotation.size() == 4) {
    float qx = n.rotation[0], qy = n.rotation[1];
    float qz = n.rotation[2], qw = n.rotation[3];
    float x2=qx*qx, y2=qy*qy, z2=qz*qz;
    float xy=qx*qy, xz=qx*qz, yz=qy*qz;
    float wx=qw*qx, wy=qw*qy, wz=qw*qz;
    R.m[0][0]=1-2*(y2+z2); R.m[0][1]=2*(xy+wz);   R.m[0][2]=2*(xz-wy);   R.m[0][3]=0;
    R.m[1][0]=2*(xy-wz);   R.m[1][1]=1-2*(x2+z2); R.m[1][2]=2*(yz+wx);   R.m[1][3]=0;
    R.m[2][0]=2*(xz+wy);   R.m[2][1]=2*(yz-wx);   R.m[2][2]=1-2*(x2+y2); R.m[2][3]=0;
    R.m[3][0]=0;            R.m[3][1]=0;            R.m[3][2]=0;            R.m[3][3]=1;
  } else {
    R.Identity();
  }
  // Translation
  if (n.translation.size() == 3) {
    XMatTranslation(T, n.translation[0], n.translation[1], n.translation[2]);
  } else {
    T.Identity();
  }
  return S * R * T;  // row-vector convention: Scale * Rotation * Translation
}

// ── Helper: negate Z in a translation for RH→LH flip ────────────────
static XVECTOR3 FlipPositionZ(const XVECTOR3& p) {
  return XVECTOR3(p.x, p.y, kFlipToLeftHanded ? -p.z : p.z);
}

// ── Helper: flip quaternion for RH→LH (negate x and y components) ───
static XQUATERNION FlipQuaternionZ(const XQUATERNION& q) {
  if (kFlipToLeftHanded)
    return XQUATERNION(-q.x, -q.y, q.z, q.w);
  return q;
}

// ── Helper: negate Z column/row in a 4x4 matrix for RH→LH flip ─────
static XMATRIX44 FlipMatrixZ(const XMATRIX44& m) {
  if (!kFlipToLeftHanded) return m;
  XMATRIX44 result = m;
  // Negate row 2 and column 2 (Z axis), then un-negate [2][2]
  for (int i = 0; i < 4; i++) {
    result.m[2][i] = -result.m[2][i];
    result.m[i][2] = -result.m[i][2];
  }
  result.m[2][2] = m.m[2][2]; // double-negated → restore
  return result;
}

void BuildSkinsAndAnimations(const Document& doc,
                             xF::XDataBase& out) {
  if (out.XMeshDataBase.empty()) return;
  xF::xMeshContainer* mc = out.XMeshDataBase[0];

  // ═══════════════════════════════════════════════════════
  //  SKINS → xSkeleton + xSkinWeights
  // ═══════════════════════════════════════════════════════

  if (!doc.skins.empty()) {
    const Skin& skin = doc.skins[0];  // v1: single skin per asset
    int numJoints = static_cast<int>(skin.joints.size());

    if (numJoints == 0) {
      T8_LOG_ERROR("[glTF] Skin has no joints");
    } else {
      T8_LOG_INFO("[glTF] Building skeleton: %d joints", numJoints);

      // Build node→joint index mapping
      std::unordered_map<int, int> nodeToJoint;
      for (int j = 0; j < numJoints; j++) {
        nodeToJoint[skin.joints[j]] = j;
      }

      // Build parent map from scene graph
      std::unordered_map<int, int> nodeParent;
      for (int ni = 0; ni < static_cast<int>(doc.nodes.size()); ni++) {
        for (int child : doc.nodes[ni].children) {
          nodeParent[child] = ni;
        }
      }

      // Populate xSkeleton (bind pose)
      mc->Skeleton.NumBones = numJoints;
      mc->Skeleton.Bones.resize(numJoints);

      // Also populate animated skeleton (will be updated at runtime)
      mc->SkeletonAnimated.NumBones = numJoints;
      mc->SkeletonAnimated.Bones.resize(numJoints);

      // Compute the world transform of all non-skeleton ancestors above
      // the skeleton root. The IBM includes the FULL world transform
      // (including nodes outside the joint list), so ComputeHierarchy
      // must also account for these ancestor transforms.
      XMATRIX44 skeletonRootWorld;
      skeletonRootWorld.Identity();
      {
        int skelRootNode = skin.skeleton.value_or(skin.joints[0]);
        // Walk up from skeleton root's parent to scene root
        int cur = skelRootNode;
        std::vector<int> ancestors;
        while (nodeParent.count(cur)) {
          int p = nodeParent[cur];
          // Only include ancestors that are NOT joints
          if (nodeToJoint.find(p) == nodeToJoint.end()) {
            ancestors.push_back(p);
          }
          cur = p;
        }
        // Multiply ancestor matrices from root down
        for (int ai = static_cast<int>(ancestors.size()) - 1; ai >= 0; ai--) {
          skeletonRootWorld = NodeLocalMatrix(doc.nodes[ancestors[ai]]) * skeletonRootWorld;
        }
        T8_LOG_INFO("[glTF] Skeleton root world: diag=(%.3f,%.3f,%.3f) trans=(%.3f,%.3f,%.3f)",
          skeletonRootWorld.m[0][0], skeletonRootWorld.m[1][1], skeletonRootWorld.m[2][2],
          skeletonRootWorld.m[3][0], skeletonRootWorld.m[3][1], skeletonRootWorld.m[3][2]);
      }

      mc->Skeleton.RootParentWorld = skeletonRootWorld;
      mc->SkeletonAnimated.RootParentWorld = skeletonRootWorld;

      for (int j = 0; j < numJoints; j++) {
        int nodeIdx = skin.joints[j];
        xF::xBone& bone = mc->Skeleton.Bones[j];
        xF::xBone& boneAnim = mc->SkeletonAnimated.Bones[j];

        // Node name
        if (nodeIdx >= 0 && nodeIdx < static_cast<int>(doc.nodes.size())) {
          bone.Name = doc.nodes[nodeIdx].name;
          boneAnim.Name = bone.Name;
        }

        // Find parent joint — walk up the node ancestry chain until we find
        // a node that is in the joint list. This handles non-joint intermediate
        // nodes between joints in the scene graph (the reference viewer
        // naturally handles this because it traverses nodes, not joints).
        // Also accumulate intermediate non-joint transforms so they're baked
        // into this joint's local matrix.
        bone.Dad = static_cast<unsigned short>(j);  // self = root by default
        boneAnim.Dad = bone.Dad;
        XMATRIX44 intermediateTransform;
        intermediateTransform.Identity();
        {
          int cur = nodeIdx;
          std::vector<int> intermediates;
          while (nodeParent.count(cur)) {
            int p = nodeParent[cur];
            auto jointIt = nodeToJoint.find(p);
            if (jointIt != nodeToJoint.end()) {
              bone.Dad = static_cast<unsigned short>(jointIt->second);
              boneAnim.Dad = bone.Dad;
              break;
            }
            intermediates.push_back(p);
            cur = p;  // keep walking up
          }
          // Multiply intermediate transforms from closest to farthest ancestor
          for (int ii = 0; ii < static_cast<int>(intermediates.size()); ii++) {
            int ni = intermediates[ii];
            if (ni >= 0 && ni < static_cast<int>(doc.nodes.size()))
              intermediateTransform = NodeLocalMatrix(doc.nodes[ni]) * intermediateTransform;
          }
        }

        // Store intermediate transform separately — applied in ComputeHierarchy.
        // NOT baked into Bone because animation keyframes overwrite Bone.
        bone.IntermediateTransform = intermediateTransform;
        boneAnim.IntermediateTransform = intermediateTransform;

        // Local TRS matrix (bind pose) — kept in RH space.
        if (nodeIdx >= 0 && nodeIdx < static_cast<int>(doc.nodes.size())) {
          bone.Bone = NodeLocalMatrix(doc.nodes[nodeIdx]);
          boneAnim.Bone = bone.Bone;
        }

        // Build Sons list
        if (nodeIdx >= 0 && nodeIdx < static_cast<int>(doc.nodes.size())) {
          for (int child : doc.nodes[nodeIdx].children) {
            auto childJointIt = nodeToJoint.find(child);
            if (childJointIt != nodeToJoint.end()) {
              bone.Sons.push_back(childJointIt->second);
              boneAnim.Sons.push_back(childJointIt->second);
            }
          }
        }
      }

      // Read inverse bind matrices
      std::vector<float> ibmData;
      int ibmElem = 0;
      bool hasIBM = skin.inverseBindMatrices.has_value()
                    && ReadAccessorFloats(doc, *skin.inverseBindMatrices, ibmData, &ibmElem)
                    && ibmElem == 16;

      // Build xSkinInfo on the first geometry with skin data
      for (auto& geom : mc->Geometry) {
        if (!(geom.VertexAttributes & xF::xMeshGeometry::HAS_SKINWEIGHTS0))
          continue;

        geom.Info.SkinMeshHeader.NumBones = static_cast<xF::xWORD>(numJoints);
        geom.Info.SkinMeshHeader.MaxNumWeightPerVertex = 4;
        geom.Info.SkinWeights.resize(numJoints);

        for (int j = 0; j < numJoints; j++) {
          xF::xSkinWeights& sw = geom.Info.SkinWeights[j];
          sw.NodeName = mc->Skeleton.Bones[j].Name;

          // Inverse bind matrix — kept in RH row-vector space.
          // The Z-flip is applied once to the final bone matrix product.
          if (hasIBM && static_cast<int>(ibmData.size()) >= (j + 1) * 16) {
            sw.MatrixOffset = ColMajorToRowMajor(&ibmData[j * 16]);
          } else {
            sw.MatrixOffset.Identity();
          }

          sw.MatrixCombined = &mc->SkeletonAnimated.Bones[j].Combined;
          sw.MatrixCombinedAnimation = &mc->SkeletonAnimated.Bones[j].Combined;
        }

        T8_LOG_INFO("[glTF] Skin weights applied to geometry '%s': %d bones",
                    geom.Name.c_str(), numJoints);
        break;  // v1: apply skin to first matching geometry
      }
    }
  }

  // ═══════════════════════════════════════════════════════
  //  ANIMATIONS → xAnimationInfo
  // ═══════════════════════════════════════════════════════

  if (doc.animations.empty()) {
    T8_LOG_INFO("[glTF] No animations to convert");
    return;
  }

  mc->Animation.isAnimInfo = true;
  mc->Animation.TicksPerSecond = static_cast<unsigned int>(kTicksPerSecond);
  mc->Animation.Animations.resize(doc.animations.size());

  // Build node→joint mapping (reuse if skins exist)
  std::unordered_map<int, int> nodeToJoint;
  if (!doc.skins.empty()) {
    for (int j = 0; j < static_cast<int>(doc.skins[0].joints.size()); j++) {
      nodeToJoint[doc.skins[0].joints[j]] = j;
    }
  }

  for (std::size_t ai = 0; ai < doc.animations.size(); ai++) {
    const Animation& anim = doc.animations[ai];
    xF::xAnimationSet& animSet = mc->Animation.Animations[ai];
    animSet.Name = anim.name.empty() ? ("Animation_" + std::to_string(ai)) : anim.name;

    // Collect which bones are animated by this animation
    std::unordered_map<int, int> boneIndexToRef;  // boneID → BonesRef index

    for (const auto& channel : anim.channels) {
      if (!channel.target.node.has_value()) continue;
      int nodeIdx = *channel.target.node;

      // Map node to bone index
      int boneIdx = -1;
      auto it = nodeToJoint.find(nodeIdx);
      if (it != nodeToJoint.end()) {
        boneIdx = it->second;
      } else {
        // If no skin, use node index directly as bone index
        boneIdx = nodeIdx;
      }

      // Ensure this bone has a BonesRef entry
      if (boneIndexToRef.find(boneIdx) == boneIndexToRef.end()) {
        int refIdx = static_cast<int>(animSet.BonesRef.size());
        boneIndexToRef[boneIdx] = refIdx;
        xF::xAnimationBone ab;
        ab.BoneID = static_cast<unsigned int>(boneIdx);
        if (nodeIdx >= 0 && nodeIdx < static_cast<int>(doc.nodes.size()))
          ab.BoneName = doc.nodes[nodeIdx].name;
        animSet.BonesRef.push_back(ab);
      }

      int refIdx = boneIndexToRef[boneIdx];
      xF::xAnimationBone& ab = animSet.BonesRef[refIdx];

      // Read sampler data
      if (channel.sampler < 0 || channel.sampler >= static_cast<int>(anim.samplers.size()))
        continue;
      const AnimationSampler& sampler = anim.samplers[channel.sampler];

      // Read timestamps
      std::vector<float> times;
      int timesElem = 0;
      if (!ReadAccessorFloats(doc, sampler.input, times, &timesElem) || timesElem != 1)
        continue;

      // Read output values
      std::vector<float> values;
      int valElem = 0;
      if (!ReadAccessorFloats(doc, sampler.output, values, &valElem))
        continue;

      int numKeys = static_cast<int>(times.size());

      if (channel.target.path == "translation" && valElem == 3) {
        ab.PositionKeys.resize(numKeys);
        for (int k = 0; k < numKeys; k++) {
          ab.PositionKeys[k].t.i_atTime = static_cast<unsigned int>(times[k] * kTicksPerSecond);
          ab.PositionKeys[k].Position = XVECTOR3(values[k*3+0], values[k*3+1], values[k*3+2]);
        }
      }
      else if (channel.target.path == "rotation" && valElem == 4) {
        ab.RotationKeys.resize(numKeys);
        for (int k = 0; k < numKeys; k++) {
          ab.RotationKeys[k].t.i_atTime = static_cast<unsigned int>(times[k] * kTicksPerSecond);
          // glTF quaternion: (x, y, z, w) — kept in RH space
          ab.RotationKeys[k].Rot = XQUATERNION(values[k*4+0], values[k*4+1], values[k*4+2], values[k*4+3]);
        }
      }
      else if (channel.target.path == "scale" && valElem == 3) {
        ab.ScaleKeys.resize(numKeys);
        for (int k = 0; k < numKeys; k++) {
          ab.ScaleKeys[k].t.i_atTime = static_cast<unsigned int>(times[k] * kTicksPerSecond);
          ab.ScaleKeys[k].Scale = XVECTOR3(values[k*3+0], values[k*3+1], values[k*3+2]);
        }
      }
    }

    // Synthesize default keys for bones missing channels
    for (auto& ab : animSet.BonesRef) {
      int nodeIdx = -1;
      if (!doc.skins.empty() && ab.BoneID < doc.skins[0].joints.size()) {
        nodeIdx = doc.skins[0].joints[ab.BoneID];
      }

      if (ab.PositionKeys.empty()) {
        xF::xPositionKey pk;
        pk.t.i_atTime = 0;
        if (nodeIdx >= 0 && nodeIdx < static_cast<int>(doc.nodes.size())
            && doc.nodes[nodeIdx].translation.size() == 3) {
          pk.Position = XVECTOR3(doc.nodes[nodeIdx].translation[0],
                                 doc.nodes[nodeIdx].translation[1],
                                 doc.nodes[nodeIdx].translation[2]);
        } else {
          pk.Position = XVECTOR3(0.0f, 0.0f, 0.0f);
        }
        ab.PositionKeys.push_back(pk);
      }
      if (ab.RotationKeys.empty()) {
        xF::xRotationKey rk;
        rk.t.i_atTime = 0;
        if (nodeIdx >= 0 && nodeIdx < static_cast<int>(doc.nodes.size())
            && doc.nodes[nodeIdx].rotation.size() == 4) {
          rk.Rot = XQUATERNION(doc.nodes[nodeIdx].rotation[0],
                                doc.nodes[nodeIdx].rotation[1],
                                doc.nodes[nodeIdx].rotation[2],
                                doc.nodes[nodeIdx].rotation[3]);
        } else {
          rk.Rot = XQUATERNION(0.0f, 0.0f, 0.0f, 1.0f);
        }
        ab.RotationKeys.push_back(rk);
      }
      if (ab.ScaleKeys.empty()) {
        xF::xScaleKey sk;
        sk.t.i_atTime = 0;
        if (nodeIdx >= 0 && nodeIdx < static_cast<int>(doc.nodes.size())
            && doc.nodes[nodeIdx].scale.size() == 3) {
          sk.Scale = XVECTOR3(doc.nodes[nodeIdx].scale[0],
                              doc.nodes[nodeIdx].scale[1],
                              doc.nodes[nodeIdx].scale[2]);
        } else {
          sk.Scale = XVECTOR3(1.0f, 1.0f, 1.0f);
        }
        ab.ScaleKeys.push_back(sk);
      }
    }

    // Compute animation duration from the maximum keyframe tick across all channels
    long maxTick = 0;
    for (auto& ab : animSet.BonesRef) {
      if (!ab.PositionKeys.empty()) {
        long t = static_cast<long>(ab.PositionKeys.back().t.i_atTime);
        if (t > maxTick) maxTick = t;
      }
      if (!ab.RotationKeys.empty()) {
        long t = static_cast<long>(ab.RotationKeys.back().t.i_atTime);
        if (t > maxTick) maxTick = t;
      }
      if (!ab.ScaleKeys.empty()) {
        long t = static_cast<long>(ab.ScaleKeys.back().t.i_atTime);
        if (t > maxTick) maxTick = t;
      }
    }
    animSet.m_MaxTimeOnTicks = maxTick;

    T8_LOG_INFO("[glTF] Animation '%s': %zu bone channels",
                animSet.Name.c_str(), animSet.BonesRef.size());
  }

  T8_LOG_INFO("[glTF] Converted %zu animations", doc.animations.size());
}

} // namespace gltf
} // namespace t800
