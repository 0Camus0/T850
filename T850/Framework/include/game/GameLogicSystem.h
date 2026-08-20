#pragma once

#include <core/EngineContext.h>
#include <game/ComponentFactory.h>
#include <game/EventBus.h>
#include <game/GameNavigationService.h>
#include <game/GameObjectRegistry.h>
#include <game/GamePhysicsService.h>
#include <game/GameValidation.h>
#include <game/InputFrame.h>
#include <game/Controller.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace t850::game {

class IController;
class GameLogicSystem;

class IGameGroupSystem {
public:
  virtual ~IGameGroupSystem() = default;
  virtual void LoadGroups(const std::vector<t850::scene::SceneGroupDesc>& groups,
                          GameObjectRegistry& registry,
                          GameLogicSystem& system) = 0;
  virtual void Update(float fixedDt) = 0;
  virtual void Clear() = 0;
};

struct GameLogicSettings {
  float fixedDeltaSeconds = 1.0f / 60.0f;
  float maxFrameDeltaSeconds = 0.25f;
  int maxStepsPerFrame = 4;
};

struct GameSceneRuntimeLinks {
  std::function<int(std::string_view meshObject)> resolveMeshSlot;
  std::function<t850::PrimitiveInst*(int slot)> primitiveForSlot;
  std::function<t850::PhysicsBodyHandle(std::string_view physicsEntity)> resolveBody;
  std::function<int(std::string_view camera)> resolveCamera;
  std::function<GameNavigationAgentSettings(int slot)> navigationAgentForSlot;
  t850::navigation::NavMesh* navMesh = nullptr;

  GameObjectLinks Resolve(const t850::scene::SceneGameEntityDesc& descriptor) const;
};

struct GameLogicStats {
  uint64_t tickIndex = 0;
  int lastFrameSteps = 0;
  std::size_t objectCount = 0;
  std::size_t componentCount = 0;
  std::size_t eventsQueued = 0;
  std::size_t eventsDispatched = 0;
  std::size_t validationErrors = 0;
  std::size_t validationWarnings = 0;
  float presentationAlpha = 0.0f;
};

class GameLogicSystem {
public:
  GameLogicSystem();
  ~GameLogicSystem();
  GameLogicSystem(const GameLogicSystem&) = delete;
  GameLogicSystem& operator=(const GameLogicSystem&) = delete;

  void Initialize(t850::EngineContext& context, const GameLogicSettings& settings);
  bool LoadFromScene(const t850::scene::EditorSceneFile& scene,
                     const GameSceneRuntimeLinks& links,
                     t850::scene::SceneValidationReport* report = nullptr);
  void Update(float deltaSeconds);
  void SetPaused(bool paused);
  bool Paused() const { return paused_; }
  void Shutdown();

  GameObjectRegistry& Registry();
  const GameObjectRegistry& Registry() const;
  EventBus& Events();
  const EventBus& Events() const;
  ComponentFactoryRegistry& Factories();
  const ComponentFactoryRegistry& Factories() const;
  GamePhysicsService& Physics();
  const GamePhysicsService& Physics() const;
  GameNavigationService& Navigation();
  const GameNavigationService& Navigation() const;

  void RequestAddComponent(RuntimeGameObjectId id, t850::scene::SceneComponentDesc descriptor);
  void RequestRemoveComponent(RuntimeGameObjectId id, std::string componentId);
  void RequestDestroy(RuntimeGameObjectId id);
  void SetInputFrame(InputFrame inputFrame);
  void SetGroupSystem(IGameGroupSystem* groupSystem);
  const MovementIntent& IntentFor(RuntimeGameObjectId id) const;
  bool SetAINavigationGoal(RuntimeGameObjectId id, const XVECTOR3& goal);
  uint64_t TickIndex() const { return tickIndex_; }
  const GameLogicStats& Stats() const;

private:
  struct PendingComponentAdd {
    RuntimeGameObjectId id = kInvalidRuntimeGameObjectId;
    t850::scene::SceneComponentDesc descriptor;
  };

  struct PendingComponentRemove {
    RuntimeGameObjectId id = kInvalidRuntimeGameObjectId;
    std::string componentId;
  };

  void Tick(float fixedDt);
  void UpdateComponents(ComponentUpdatePhase phase, float fixedDt);
  void ApplyDeferredCreates();
  void ApplyDeferredDestroys();
  void DestroyComponents(GameObject& object);
  void RefreshStats(int lastFrameSteps);

  t850::EngineContext* context_ = nullptr;
  GameLogicSettings settings_;
  float accumulator_ = 0.0f;
  float presentationAlpha_ = 0.0f;
  uint64_t tickIndex_ = 0;
  bool paused_ = false;
  std::size_t validationErrors_ = 0;
  std::size_t validationWarnings_ = 0;
  GameObjectRegistry registry_;
  EventBus events_;
  ComponentFactoryRegistry factories_;
  GamePhysicsService physics_;
  GameNavigationService navigation_;
  IGameGroupSystem* groupSystem_ = nullptr;
  std::vector<std::unique_ptr<IController>> controllers_;
  InputFrame inputFrame_;
  std::unordered_map<RuntimeGameObjectId, MovementIntent> intents_;
  std::vector<PendingComponentAdd> pendingComponentAdds_;
  std::vector<PendingComponentRemove> pendingComponentRemoves_;
  std::vector<RuntimeGameObjectId> pendingObjectDestroys_;
  GameLogicStats stats_;
};

} // namespace t850::game