#pragma once

#include <game/GameIds.h>
#include <physics/GameplayLayers.h>
#include <utils/xMaths.h>

#include <cstdint>
#include <vector>

namespace t850 {
class JoltPhysicsSystem;
}

namespace t850::game {

class GameObjectRegistry;

struct GameQueryFilter {
  uint32_t includeLayers = 0xFFFFFFFFu;
  uint32_t excludeLayers = 0;
  int team = -1;
  RuntimeGameObjectId ignore = kInvalidRuntimeGameObjectId;
};

struct GameHit {
  RuntimeGameObjectId entity = kInvalidRuntimeGameObjectId;
  XVECTOR3 point = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 normal = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
  float distance = 0.0f;
};

class GamePhysicsService {
public:
  void Bind(t850::JoltPhysicsSystem* physics, GameObjectRegistry* registry);
  bool LineOfSight(const XVECTOR3& from,
                   const XVECTOR3& to,
                   const GameQueryFilter& filter,
                   GameHit& out) const;
  int OverlapSphere(const XVECTOR3& center,
                    float radius,
                    const GameQueryFilter& filter,
                    std::vector<GameHit>& out) const;
  void EnqueueSetVelocity(RuntimeGameObjectId id,
                          const XVECTOR3& linear,
                          const XVECTOR3& angular);
  void EnqueueKinematicMove(RuntimeGameObjectId id, const XMATRIX44& worldTransform);
  void Flush(float fixedDt);
  bool Available() const;
  void Clear();

private:
  struct VelocityCommand {
    RuntimeGameObjectId id = kInvalidRuntimeGameObjectId;
    XVECTOR3 linear;
    XVECTOR3 angular;
  };

  struct KinematicCommand {
    RuntimeGameObjectId id = kInvalidRuntimeGameObjectId;
    XMATRIX44 worldTransform;
  };

  t850::JoltPhysicsSystem* physics_ = nullptr;
  GameObjectRegistry* registry_ = nullptr;
  std::vector<VelocityCommand> velocityCommands_;
  std::vector<KinematicCommand> kinematicCommands_;
};

} // namespace t850::game