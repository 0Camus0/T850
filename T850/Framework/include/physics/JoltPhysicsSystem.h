#pragma once

#include <physics/PhysicsTypes.h>

#include <vector>

namespace t850 {

class JoltPhysicsSystem {
public:
  JoltPhysicsSystem();
  ~JoltPhysicsSystem();

  JoltPhysicsSystem(const JoltPhysicsSystem&) = delete;
  JoltPhysicsSystem& operator=(const JoltPhysicsSystem&) = delete;

  bool Initialize();
  void Shutdown();
  void Update(float deltaSeconds);
  void SetSimulationSpeedScale(float scale);
  float GetSimulationSpeedScale() const;
  void SetUseFixedSimulationDelta(bool useFixedDelta);
  bool GetUseFixedSimulationDelta() const;

  PhysicsBodyHandle CreateBody(const PhysicsBodyDesc& desc);
  PhysicsBodyHandle CreateTriangleMeshBody(const PhysicsTriangleMeshBodyDesc& desc, PhysicsCookStats* outStats = nullptr);
  PhysicsBodyHandle CreateBoxBodyFromBounds(uint32_t entityId,
                                            const AABB& localBounds,
                                            const XMATRIX44& worldFromLocal,
                                            PhysicsBodyMotion motion);
  bool DestroyBody(PhysicsBodyHandle handle);
  bool SetBodyMotion(PhysicsBodyHandle handle, PhysicsBodyMotion motion);
  bool SetBodyVelocity(PhysicsBodyHandle handle, const XVECTOR3& linearVelocity, const XVECTOR3& angularVelocity);
  bool DriveBodyKinematic(PhysicsBodyHandle handle, const XMATRIX44& worldTransform, float deltaSeconds);
  bool SetBodyTransform(PhysicsBodyHandle handle, const XMATRIX44& worldTransform, bool activate);
  bool GetBodyState(PhysicsBodyHandle handle, PhysicsBodyState& outState) const;

  PhysicsRagdollHandle CreateRagdoll(const PhysicsRagdollDesc& desc, PhysicsBodyMotion initialMotion);
  bool DestroyRagdoll(PhysicsRagdollHandle handle);
  bool SetRagdollMotion(PhysicsRagdollHandle handle, PhysicsBodyMotion motion);
  bool SetRagdollVelocity(PhysicsRagdollHandle handle, const XVECTOR3& linearVelocity, const XVECTOR3& angularVelocity);
  bool DriveRagdollFromPose(PhysicsRagdollHandle handle, const PhysicsRagdollDesc& pose, float deltaSeconds);
  bool GetRagdollState(PhysicsRagdollHandle handle, std::vector<PhysicsBodyState>& outStates) const;
  bool GetDebugBodies(std::vector<PhysicsDebugBody>& outBodies) const;

  bool IsAvailable() const;
  bool IsInitialized() const;

private:
  struct Impl;

  PhysicsBodyHandle CreateBodyInternal(const PhysicsBodyDesc& desc, const void* collisionGroup);

  Impl* m_impl;
  bool m_initialized;
};

} // namespace t850
