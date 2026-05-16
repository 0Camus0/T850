#include <pch.h>
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
#include <utils/gltf/GLTFSkinMap.h>
#include <utils/XDataBase.h>
#include <utils/Log.h>
#include <utils/ThreadPool.h>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace t850 {
namespace gltf {

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

static bool IsValidAccessorIndex(const Document& doc, int accessorIndex, const char* label) {
  if (accessorIndex >= 0 && accessorIndex < static_cast<int>(doc.accessors.size()))
    return true;
  T8_LOG_ERROR("[glTF] animation %s accessor index %d OOR", label, accessorIndex);
  return false;
}

static int AnimationOutputRecordsPerKey(const AnimationSampler& sampler) {
  return sampler.interpolation == "CUBICSPLINE" ? 3 : 1;
}

void BuildSkinsAndAnimations(const Document& doc,
                             xF::XDataBase& out) {
  if (out.XMeshDataBase.empty()) return;
  xF::xMeshContainer* mc = out.XMeshDataBase[0];

  // ═══════════════════════════════════════════════════════
  //  SKINS → xSkeleton + xSkinWeights
  // ═══════════════════════════════════════════════════════

  SkinJointMap jointMap = BuildSkinJointMap(doc);

  if (!doc.skins.empty()) {
    const Skin& firstSkin = doc.skins[0];
    int numJoints = static_cast<int>(jointMap.jointNodes.size());

    if (numJoints == 0) {
      T8_LOG_ERROR("[glTF] Skin has no joints");
    } else {
      T8_LOG_INFO("[glTF] Building skeleton: %d joints from %zu skin(s)",
                  numJoints, doc.skins.size());
      const auto& nodeToJoint = jointMap.nodeToJoint;

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
      if (doc.skins.size() == 1 && !firstSkin.joints.empty()) {
        int skelRootNode = firstSkin.skeleton.value_or(firstSkin.joints[0]);
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
      } else {
        T8_LOG_INFO("[glTF] Multi-skin skeleton uses per-joint ancestor transforms");
      }

      mc->Skeleton.RootParentWorld = skeletonRootWorld;
      mc->SkeletonAnimated.RootParentWorld = skeletonRootWorld;

      for (int j = 0; j < numJoints; j++) {
        int nodeIdx = jointMap.jointNodes[j];
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

      // Read inverse bind matrices from every skin and store them in global
      // joint order. Duplicate joint nodes across skins are expected to share
      // identical IBMs; keep the first valid copy.
      std::vector<XMATRIX44> inverseBindMatrices(numJoints);
      std::vector<bool> hasInverseBind(numJoints, false);
      for (int j = 0; j < numJoints; ++j) inverseBindMatrices[j].Identity();
      for (int si = 0; si < static_cast<int>(doc.skins.size()); ++si) {
        const Skin& skin = doc.skins[si];
        std::vector<float> ibmData;
        int ibmElem = 0;
        bool hasIBM = skin.inverseBindMatrices.has_value()
                      && ReadAccessorFloats(doc, *skin.inverseBindMatrices, ibmData, &ibmElem)
                      && ibmElem == 16;
        if (!hasIBM) continue;
        for (int li = 0; li < static_cast<int>(skin.joints.size()); ++li) {
          if (li >= static_cast<int>(jointMap.localToGlobal[si].size())) continue;
          int globalJoint = jointMap.localToGlobal[si][li];
          if (globalJoint < 0 || globalJoint >= numJoints || hasInverseBind[globalJoint]) continue;
          if (static_cast<int>(ibmData.size()) >= (li + 1) * 16) {
            inverseBindMatrices[globalJoint] = ColMajorToRowMajor(&ibmData[li * 16]);
            hasInverseBind[globalJoint] = true;
          }
        }
      }

      // Build xSkinInfo on every skinned geometry. Vertex JOINTS_0 values were
      // already remapped from local skin indices to this global joint order.
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
          if (hasInverseBind[j]) {
            sw.MatrixOffset = inverseBindMatrices[j];
          } else {
            sw.MatrixOffset.Identity();
          }

          sw.MatrixCombined = &mc->SkeletonAnimated.Bones[j].Combined;
          sw.MatrixCombinedAnimation = &mc->SkeletonAnimated.Bones[j].Combined;
        }

        T8_LOG_INFO("[glTF] Skin weights applied to geometry '%s': %d bones",
                    geom.Name.c_str(), numJoints);
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

  // Build node→joint mapping (reuse the global skin order when skins exist).
  const auto& nodeToJoint = jointMap.nodeToJoint;

  std::vector<std::size_t> animationBoneCounts(doc.animations.size(), 0);

  auto convertAnimation = [&](int animationIndex) {
    const std::size_t ai = static_cast<std::size_t>(animationIndex);
    const Animation& anim = doc.animations[ai];
    xF::xAnimationSet& animSet = mc->Animation.Animations[ai];
    animSet.Name = anim.name.empty() ? ("Animation_" + std::to_string(ai)) : anim.name;

    // Collect which bones are animated by this animation
    std::unordered_map<int, int> boneIndexToRef;  // boneID → BonesRef index

    for (const auto& channel : anim.channels) {
      if (!channel.target.node.has_value()) continue;
      int nodeIdx = *channel.target.node;
      if (nodeIdx < 0 || nodeIdx >= static_cast<int>(doc.nodes.size())) {
        T8_LOG_ERROR("[glTF] animation '%s': target node %d OOR", animSet.Name.c_str(), nodeIdx);
        continue;
      }

      const bool supportedPath = channel.target.path == "translation"
        || channel.target.path == "rotation"
        || channel.target.path == "scale";
      if (!supportedPath) {
        T8_LOG_INFO("[glTF] animation '%s': skipping unsupported channel path '%s'",
                    animSet.Name.c_str(), channel.target.path.c_str());
        continue;
      }

      // Map node to bone index
      int boneIdx = -1;
      auto it = nodeToJoint.find(nodeIdx);
      if (it != nodeToJoint.end()) {
        boneIdx = it->second;
      } else if (doc.skins.empty()) {
        // If no skin, use node index directly as bone index.
        boneIdx = nodeIdx;
      } else {
        continue;
      }
      if (boneIdx < 0) continue;

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
      if (channel.sampler < 0 || channel.sampler >= static_cast<int>(anim.samplers.size())) {
        T8_LOG_ERROR("[glTF] animation '%s': sampler index %d OOR", animSet.Name.c_str(), channel.sampler);
        continue;
      }
      const AnimationSampler& sampler = anim.samplers[channel.sampler];
      if (!IsValidAccessorIndex(doc, sampler.input, "input")
          || !IsValidAccessorIndex(doc, sampler.output, "output")) {
        continue;
      }

      // Read timestamps
      std::vector<float> times;
      int timesElem = 0;
      if (!ReadAccessorFloats(doc, sampler.input, times, &timesElem) || timesElem != 1 || times.empty())
        continue;

      // Read output values
      std::vector<float> values;
      int valElem = 0;
      if (!ReadAccessorFloats(doc, sampler.output, values, &valElem))
        continue;

      const int recordsPerKey = AnimationOutputRecordsPerKey(sampler);
      const std::size_t outputRecordCount = valElem > 0 ? values.size() / static_cast<std::size_t>(valElem) : 0;
      const std::size_t outputKeyCount = recordsPerKey > 0 ? outputRecordCount / static_cast<std::size_t>(recordsPerKey) : 0;
      int numKeys = static_cast<int>((std::min)(times.size(), outputKeyCount));
      if (numKeys <= 0) {
        T8_LOG_ERROR("[glTF] animation '%s': channel '%s' has no usable keys",
                     animSet.Name.c_str(), channel.target.path.c_str());
        continue;
      }
      if (times.size() != outputKeyCount) {
        T8_LOG_INFO("[glTF] animation '%s': channel '%s' key count mismatch (input=%zu output=%zu, interpolation=%s); clamping to %d",
                    animSet.Name.c_str(), channel.target.path.c_str(),
                    times.size(), outputKeyCount, sampler.interpolation.c_str(), numKeys);
      }
      auto valueOffset = [&](int keyIndex) {
        return static_cast<std::size_t>(keyIndex * recordsPerKey + (recordsPerKey == 3 ? 1 : 0))
          * static_cast<std::size_t>(valElem);
      };

      if (channel.target.path == "translation" && valElem == 3) {
        ab.PositionKeys.resize(numKeys);
        for (int k = 0; k < numKeys; k++) {
          const std::size_t base = valueOffset(k);
          ab.PositionKeys[k].t.i_atTime = static_cast<unsigned int>(times[k] * kTicksPerSecond);
          ab.PositionKeys[k].Position = XVECTOR3(values[base + 0], values[base + 1], values[base + 2]);
        }
      }
      else if (channel.target.path == "rotation" && valElem == 4) {
        ab.RotationKeys.resize(numKeys);
        for (int k = 0; k < numKeys; k++) {
          const std::size_t base = valueOffset(k);
          ab.RotationKeys[k].t.i_atTime = static_cast<unsigned int>(times[k] * kTicksPerSecond);
          // glTF quaternion: (x, y, z, w) — kept in RH space
          ab.RotationKeys[k].Rot = XQUATERNION(values[base + 0], values[base + 1], values[base + 2], values[base + 3]);
        }
      }
      else if (channel.target.path == "scale" && valElem == 3) {
        ab.ScaleKeys.resize(numKeys);
        for (int k = 0; k < numKeys; k++) {
          const std::size_t base = valueOffset(k);
          ab.ScaleKeys[k].t.i_atTime = static_cast<unsigned int>(times[k] * kTicksPerSecond);
          ab.ScaleKeys[k].Scale = XVECTOR3(values[base + 0], values[base + 1], values[base + 2]);
        }
      }
    }

    // Synthesize default keys for bones missing channels
    for (auto& ab : animSet.BonesRef) {
      int nodeIdx = -1;
      if (!doc.skins.empty() && ab.BoneID < jointMap.jointNodes.size()) {
        nodeIdx = jointMap.jointNodes[ab.BoneID];
      } else if (doc.skins.empty() && ab.BoneID < doc.nodes.size()) {
        nodeIdx = static_cast<int>(ab.BoneID);
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

    animationBoneCounts[ai] = animSet.BonesRef.size();
  };

  if (g_threadPool && doc.animations.size() > 1) {
    T8_LOG_INFO("[glTF] Converting %zu animations with %u global worker threads",
                doc.animations.size(), g_threadPool->NumWorkers());
    g_threadPool->ParallelForHeavy(0, static_cast<int>(doc.animations.size()), convertAnimation);
  } else {
    for (int ai = 0; ai < static_cast<int>(doc.animations.size()); ++ai) {
      convertAnimation(ai);
    }
  }

  for (std::size_t ai = 0; ai < doc.animations.size(); ++ai) {
    const xF::xAnimationSet& animSet = mc->Animation.Animations[ai];
    T8_LOG_INFO("[glTF] Animation '%s': %zu bone channels",
                animSet.Name.c_str(), animationBoneCounts[ai]);
  }

  T8_LOG_INFO("[glTF] Converted %zu animations", doc.animations.size());
}

} // namespace gltf
} // namespace t850
