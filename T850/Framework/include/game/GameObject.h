#pragma once

#include <game/GameIds.h>
#include <physics/PhysicsTypes.h>

#include <memory>
#include <string>
#include <vector>

namespace t850 {
class PrimitiveInst;
}

namespace t850::game {

class Component;
class IController;
class StateMachine;

struct GameNavigationAgentSettings {
  std::string targetMode = "direct";
  float followDistance = 0.0f;
  float sideOffset = 0.0f;
  float formationDepthStep = 0.0f;
  int formationSlot = -1;
};

struct GameObjectLinks {
  int meshSlot = -1;
  t850::PrimitiveInst* primitive = nullptr;
  uint32_t primitiveEntityId = 0;
  t850::PhysicsBodyHandle primaryBody;
  std::vector<t850::PhysicsBodyHandle> bodies;
  int cameraIndex = -1;
  int ragdollIndex = -1;
  GameNavigationAgentSettings navigationAgent;
};

struct GameObject {
  GameObject();
  ~GameObject();
  GameObject(GameObject&&) noexcept;
  GameObject& operator=(GameObject&&) noexcept;
  GameObject(const GameObject&) = delete;
  GameObject& operator=(const GameObject&) = delete;

  RuntimeGameObjectId runtimeId = kInvalidRuntimeGameObjectId;
  std::string sceneId;
  std::string name;
  std::string kind = "generic";
  int team = -1;
  bool enabled = true;
  GameObjectLinks links;
  std::vector<std::unique_ptr<Component>> components;
  std::unique_ptr<StateMachine> behavior;
  IController* controller = nullptr;
};

} // namespace t850::game