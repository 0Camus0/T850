#pragma once

#include <physics/JoltPhysicsSystem.h>

#include <vector>

namespace t850 {

class PrimitiveInst;
class RenderMesh;
class RenderSkinnedMesh;

bool BuildMeshBoxBodyDesc(const RenderMesh& mesh,
                          const XMATRIX44& worldFromMesh,
                          uint32_t entityId,
                          PhysicsBodyMotion motion,
                          PhysicsBodyDesc& outDesc);
bool AttachMeshBoxBody(JoltPhysicsSystem& physics,
                       PrimitiveInst& instance,
                       const RenderMesh& mesh,
                       PhysicsBodyMotion motion);
bool BuildStaticTriangleMeshBodyDesc(const RenderMesh& mesh,
                                     const XMATRIX44& worldFromMesh,
                                     uint32_t entityId,
                                     const PhysicsTriangleMeshCookSettings& settings,
                                     PhysicsTriangleMeshBodyDesc& outDesc,
                                     PhysicsCookStats* outStats = nullptr);
bool AttachStaticTriangleMeshBody(JoltPhysicsSystem& physics,
                                  PrimitiveInst& instance,
                                  const RenderMesh& mesh,
                                  const PhysicsTriangleMeshCookSettings& settings,
                                  PhysicsCookStats* outStats = nullptr);

bool BuildRagdollDescFromSkeleton(const RenderSkinnedMesh& mesh,
                                  const XMATRIX44& worldFromMesh,
                                  uint32_t entityId,
                                  const PhysicsRagdollBuildSettings& settings,
                                  PhysicsRagdollDesc& outDesc);
bool BuildRagdollAnimationBinding(const RenderSkinnedMesh& mesh,
                                  const XMATRIX44& worldFromMesh,
                                  const PhysicsRagdollDesc& referencePose,
                                  PhysicsRagdollAnimationBinding& outBinding);
bool BuildRagdollPoseFromAnimation(const RenderSkinnedMesh& mesh,
                                   const XMATRIX44& worldFromMesh,
                                   const PhysicsRagdollAnimationBinding& binding,
                                   PhysicsRagdollDesc& outPose);
bool BuildSkeletonPoseFromRagdollState(const RenderSkinnedMesh& mesh,
                                       const XMATRIX44& worldFromMesh,
                                       const PhysicsRagdollAnimationBinding& binding,
                                       const std::vector<PhysicsBodyState>& states,
                                       std::vector<int>& outBoneIndices,
                                       std::vector<XMATRIX44>& outCombinedMatrices);
bool AttachSkeletonRagdoll(JoltPhysicsSystem& physics,
                           PrimitiveInst& instance,
                           const RenderSkinnedMesh& mesh,
                           const PhysicsRagdollBuildSettings& settings,
                           PhysicsBodyMotion initialMotion,
                           PhysicsRagdollDesc* outDesc = nullptr);

} // namespace t850
