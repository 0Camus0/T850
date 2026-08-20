#include <pch.h>

#include <game/GamePhysicsService.h>

#include <debug/RuntimeTelemetry.h>
#include <game/GameObjectRegistry.h>
#include <physics/JoltPhysicsSystem.h>

#include <cmath>

namespace t850::game {
namespace {

RuntimeGameObjectId FindRuntimeObjectByPrimitiveId(
    const GameObjectRegistry* registry, uint32_t primitiveEntityId) {
  if (!registry) return kInvalidRuntimeGameObjectId;
  for (const GameObject& object : registry->Objects()) {
    if (object.links.primitiveEntityId == primitiveEntityId) return object.runtimeId;
  }
  return kInvalidRuntimeGameObjectId;
}

bool PassesGameFilter(
    const GameObjectRegistry* registry, RuntimeGameObjectId id, const GameQueryFilter& filter) {
  if (id == kInvalidRuntimeGameObjectId) return filter.team < 0 && filter.ignore == kInvalidRuntimeGameObjectId;
  if (id == filter.ignore) return false;
  const GameObject* object = registry ? registry->Get(id) : nullptr;
  return filter.team < 0 || (object && object->team == filter.team);
}

} // namespace

void GamePhysicsService::Bind(t850::JoltPhysicsSystem* physics, GameObjectRegistry* registry) {
  physics_ = physics;
  registry_ = registry;
  Clear();
}

bool GamePhysicsService::LineOfSight(
    const XVECTOR3& from,
    const XVECTOR3& to,
    const GameQueryFilter& filter,
    GameHit& out) const {
  T8_TELEMETRY_SCOPE("game.spatial_queries");
  out = GameHit{};
  if (!Available()) return false;

  const XVECTOR3 displacement = to - from;
  const float distance = std::sqrt(
      displacement.x * displacement.x +
      displacement.y * displacement.y +
      displacement.z * displacement.z);
  if (distance <= 0.000001f) return false;

  t850::PhysicsCapsuleCastDesc cast;
  cast.startCenter = from;
  cast.displacement = displacement;
  cast.radius = 0.01f;
  cast.halfHeight = 0.01f;
  cast.includeLayers = filter.includeLayers;
  cast.excludeLayers = filter.excludeLayers;
  if (filter.ignore != kInvalidRuntimeGameObjectId) {
    const GameObject* ignored = registry_->Get(filter.ignore);
    if (ignored && ignored->links.primitiveEntityId != 0) {
      cast.ignoredEntityIds.push_back(ignored->links.primitiveEntityId);
    }
  }

  t850::PhysicsCastHit hit;
  t850::RuntimeTelemetry::AddCounter("game.physics.queries", 1.0);
  if (!physics_->CastCapsule(cast, hit)) return false;
  const RuntimeGameObjectId runtimeId = FindRuntimeObjectByPrimitiveId(registry_, hit.entityId);
  if (!PassesGameFilter(registry_, runtimeId, filter)) return false;

  out.entity = runtimeId;
  out.point = hit.position;
  out.normal = hit.normal;
  out.distance = distance * hit.fraction;
  return true;
}

int GamePhysicsService::OverlapSphere(
    const XVECTOR3& center,
    float radius,
    const GameQueryFilter& filter,
    std::vector<GameHit>& out) const {
  T8_TELEMETRY_SCOPE("game.spatial_queries");
  out.clear();
  if (!Available()) return 0;

  std::vector<t850::PhysicsOverlapHit> physicsHits;
  t850::RuntimeTelemetry::AddCounter("game.physics.queries", 1.0);
  physics_->OverlapSphere(
      center, radius, filter.includeLayers, filter.excludeLayers, physicsHits);
  for (const t850::PhysicsOverlapHit& physicsHit : physicsHits) {
    const RuntimeGameObjectId runtimeId = FindRuntimeObjectByPrimitiveId(registry_, physicsHit.entityId);
    if (!PassesGameFilter(registry_, runtimeId, filter)) continue;
    GameHit hit;
    hit.entity = runtimeId;
    hit.point = physicsHit.position;
    hit.distance = (physicsHit.position - center).Length();
    out.push_back(hit);
  }
  return static_cast<int>(out.size());
}

void GamePhysicsService::EnqueueSetVelocity(
    RuntimeGameObjectId id, const XVECTOR3& linear, const XVECTOR3& angular) {
  velocityCommands_.push_back(VelocityCommand{id, linear, angular});
}

void GamePhysicsService::EnqueueKinematicMove(
    RuntimeGameObjectId id, const XMATRIX44& worldTransform) {
  kinematicCommands_.push_back(KinematicCommand{id, worldTransform});
}

void GamePhysicsService::Flush(float fixedDt) {
  if (Available() && registry_) {
    for (const VelocityCommand& command : velocityCommands_) {
      GameObject* object = registry_->Get(command.id);
      if (object && object->links.primaryBody.IsValid()) {
        physics_->SetBodyVelocity(object->links.primaryBody, command.linear, command.angular);
      }
    }
    for (const KinematicCommand& command : kinematicCommands_) {
      GameObject* object = registry_->Get(command.id);
      if (!object || !object->links.primaryBody.IsValid()) continue;
      if (!physics_->DriveBodyKinematic(object->links.primaryBody, command.worldTransform, fixedDt)) {
        physics_->SetBodyTransform(object->links.primaryBody, command.worldTransform, true);
      }
    }
  }
  Clear();
}

bool GamePhysicsService::Available() const {
  return physics_ && physics_->IsInitialized();
}

void GamePhysicsService::Clear() {
  velocityCommands_.clear();
  kinematicCommands_.clear();
}

} // namespace t850::game