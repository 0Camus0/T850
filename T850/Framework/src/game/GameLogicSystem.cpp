#include <pch.h>

#include <game/GameLogicSystem.h>

#include <debug/RuntimeTelemetry.h>
#include <game/MovementComponent.h>
#include <game/StateMachine.h>
#include <scene/PrimitiveInstance.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace t850::game {

GameObjectLinks GameSceneRuntimeLinks::Resolve(
    const t850::scene::SceneGameEntityDesc& descriptor) const {
  GameObjectLinks links;
  if (resolveMeshSlot && !descriptor.mesh_object.empty()) {
    links.meshSlot = resolveMeshSlot(descriptor.mesh_object);
  }
  if (primitiveForSlot && links.meshSlot >= 0) {
    links.primitive = primitiveForSlot(links.meshSlot);
    if (links.primitive) links.primitiveEntityId = links.primitive->GetEntityId();
  }
  if (navigationAgentForSlot && links.meshSlot >= 0) {
    links.navigationAgent = navigationAgentForSlot(links.meshSlot);
  }
  if (resolveBody && !descriptor.primary_physics_entity.empty()) {
    links.primaryBody = resolveBody(descriptor.primary_physics_entity);
  }
  if (resolveBody) {
    links.bodies.reserve(descriptor.physics_entities.size());
    for (const std::string& physicsEntity : descriptor.physics_entities) {
      const t850::PhysicsBodyHandle body = resolveBody(physicsEntity);
      if (body.IsValid()) links.bodies.push_back(body);
    }
  }
  if (resolveCamera && !descriptor.camera.empty()) {
    links.cameraIndex = resolveCamera(descriptor.camera);
  }
  return links;
}

GameLogicSystem::GameLogicSystem() = default;
GameLogicSystem::~GameLogicSystem() {
  Shutdown();
}

void GameLogicSystem::Initialize(t850::EngineContext& context, const GameLogicSettings& settings) {
  Shutdown();
  context_ = &context;
  settings_ = settings;
  if (!std::isfinite(settings_.fixedDeltaSeconds) || settings_.fixedDeltaSeconds <= 0.0f) {
    settings_.fixedDeltaSeconds = 1.0f / 60.0f;
  }
  if (!std::isfinite(settings_.maxFrameDeltaSeconds) || settings_.maxFrameDeltaSeconds < 0.0f) {
    settings_.maxFrameDeltaSeconds = 0.25f;
  }
  if (settings_.maxStepsPerFrame < 1) settings_.maxStepsPerFrame = 1;
  factories_.Register("movement", CreateMovementComponent,
                      ComponentTypeInfo{.type = "movement", .allowMultiple = false});
  physics_.Bind(context.physics, &registry_);
  RefreshStats(0);
}

bool GameLogicSystem::LoadFromScene(
    const t850::scene::EditorSceneFile& scene,
    const GameSceneRuntimeLinks& links,
    t850::scene::SceneValidationReport* report) {
  navigation_.Bind(links.navMesh, context_ ? context_->threadPool : nullptr);
  t850::scene::SceneValidationReport validation = t850::scene::ValidateEditorSceneGameLogic(scene);
  validationErrors_ = 0;
  validationWarnings_ = 0;
  for (const t850::scene::SceneValidationIssue& issue : validation.issues) {
    if (issue.severity == t850::scene::SceneValidationSeverity::Error) ++validationErrors_;
    else if (issue.severity == t850::scene::SceneValidationSeverity::Warning) ++validationWarnings_;
  }
  if (report) *report = validation;
  if (validation.HasErrors()) return false;

  for (GameObject& object : registry_.Objects()) DestroyComponents(object);
  registry_.Clear();
  for (const std::unique_ptr<IController>& controller : controllers_) {
    if (controller) controller->OnUnpossess();
  }
  controllers_.clear();
  pendingComponentAdds_.clear();
  pendingComponentRemoves_.clear();
  pendingObjectDestroys_.clear();

  for (const t850::scene::SceneGameEntityDesc& descriptor : scene.game_entities) {
    GameObject* object = registry_.Create(descriptor, links.Resolve(descriptor));
    if (!object) continue;

    for (const t850::scene::SceneComponentDesc& componentDescriptor : descriptor.components) {
      ComponentLoadContext loadContext{object, this, report};
      std::unique_ptr<Component> component = factories_.Create(componentDescriptor, loadContext);
      if (!component) continue;
      component->id_ = componentDescriptor.id;
      component->enabled_ = componentDescriptor.enabled;
      component->OnAttach(*object, *this);
      component->OnCreate();
      object->components.push_back(std::move(component));
    }

    if (descriptor.control.mode == "player") {
      auto controller = std::make_unique<PlayerController>(
          descriptor.control.player_slot, descriptor.control.controller);
      controller->OnPossess(*object);
      object->controller = controller.get();
      controllers_.push_back(std::move(controller));
    } else if (descriptor.control.mode == "ai") {
      auto controller = std::make_unique<AIController>(descriptor.control.controller);
      controller->OnPossess(*object);
      object->controller = controller.get();
      controllers_.push_back(std::move(controller));
    }
    if (descriptor.behavior.has_value()) {
      auto behavior = std::make_unique<StateMachine>();
      if (behavior->Compile(*descriptor.behavior, report)) {
        behavior->SetInitialState();
        object->behavior = std::move(behavior);
      }
    }
  }

  if (groupSystem_) groupSystem_->LoadGroups(scene.game_groups, registry_, *this);

  RefreshStats(0);
  return true;
}

void GameLogicSystem::Update(float deltaSeconds) {
  t850::RuntimeTelemetry::SetCounter("game.components.updated", 0.0);
  t850::RuntimeTelemetry::SetCounter("game.state_machines.transitions", 0.0);
  t850::RuntimeTelemetry::SetCounter("game.physics.queries", 0.0);
  t850::RuntimeTelemetry::SetCounter("game.nav.requests", 0.0);
  t850::RuntimeTelemetry::SetCounter("game.nav.completed", 0.0);
  if (!context_ || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
    RefreshStats(0);
    return;
  }
  if (paused_) {
    RefreshStats(0);
    return;
  }

  accumulator_ += (std::min)(deltaSeconds, settings_.maxFrameDeltaSeconds);
  int steps = 0;
  while (accumulator_ >= settings_.fixedDeltaSeconds && steps < settings_.maxStepsPerFrame) {
    Tick(settings_.fixedDeltaSeconds);
    accumulator_ -= settings_.fixedDeltaSeconds;
    ++steps;
  }
  presentationAlpha_ = accumulator_ / settings_.fixedDeltaSeconds;
  RefreshStats(steps);
}

void GameLogicSystem::SetPaused(bool paused) {
  paused_ = paused;
}

void GameLogicSystem::Shutdown() {
  if (groupSystem_) groupSystem_->Clear();
  for (GameObject& object : registry_.Objects()) DestroyComponents(object);
  for (const std::unique_ptr<IController>& controller : controllers_) {
    if (controller) controller->OnUnpossess();
  }
  controllers_.clear();
  intents_.clear();
  inputFrame_ = InputFrame{};
  pendingComponentAdds_.clear();
  pendingComponentRemoves_.clear();
  pendingObjectDestroys_.clear();
  registry_.Clear();
  events_.Clear();
  physics_.Bind(nullptr, nullptr);
  navigation_.Reset();
  accumulator_ = 0.0f;
  presentationAlpha_ = 0.0f;
  tickIndex_ = 0;
  paused_ = false;
  validationErrors_ = 0;
  validationWarnings_ = 0;
  context_ = nullptr;
  groupSystem_ = nullptr;
  RefreshStats(0);
}

GameObjectRegistry& GameLogicSystem::Registry() { return registry_; }
const GameObjectRegistry& GameLogicSystem::Registry() const { return registry_; }
EventBus& GameLogicSystem::Events() { return events_; }
const EventBus& GameLogicSystem::Events() const { return events_; }
ComponentFactoryRegistry& GameLogicSystem::Factories() { return factories_; }
const ComponentFactoryRegistry& GameLogicSystem::Factories() const { return factories_; }
GamePhysicsService& GameLogicSystem::Physics() { return physics_; }
const GamePhysicsService& GameLogicSystem::Physics() const { return physics_; }
GameNavigationService& GameLogicSystem::Navigation() { return navigation_; }
const GameNavigationService& GameLogicSystem::Navigation() const { return navigation_; }

void GameLogicSystem::RequestAddComponent(
    RuntimeGameObjectId id, t850::scene::SceneComponentDesc descriptor) {
  pendingComponentAdds_.push_back(PendingComponentAdd{id, std::move(descriptor)});
}

void GameLogicSystem::RequestRemoveComponent(RuntimeGameObjectId id, std::string componentId) {
  pendingComponentRemoves_.push_back(PendingComponentRemove{id, std::move(componentId)});
}

void GameLogicSystem::RequestDestroy(RuntimeGameObjectId id) {
  if (id == kInvalidRuntimeGameObjectId) return;
  if (std::find(pendingObjectDestroys_.begin(), pendingObjectDestroys_.end(), id) == pendingObjectDestroys_.end()) {
    pendingObjectDestroys_.push_back(id);
  }
}

void GameLogicSystem::SetInputFrame(InputFrame inputFrame) {
  inputFrame_ = std::move(inputFrame);
}

void GameLogicSystem::SetGroupSystem(IGameGroupSystem* groupSystem) {
  if (groupSystem_ == groupSystem) return;
  if (groupSystem_) groupSystem_->Clear();
  groupSystem_ = groupSystem;
}

const MovementIntent& GameLogicSystem::IntentFor(RuntimeGameObjectId id) const {
  static const MovementIntent emptyIntent{};
  const auto found = intents_.find(id);
  return found == intents_.end() ? emptyIntent : found->second;
}

bool GameLogicSystem::SetAINavigationGoal(RuntimeGameObjectId id, const XVECTOR3& goal) {
  GameObject* object = registry_.Get(id);
  if (!object || !object->controller || object->controller->Kind() != ControllerKind::AI) return false;
  static_cast<AIController*>(object->controller)->SetNavigationGoal(goal);
  return true;
}

const GameLogicStats& GameLogicSystem::Stats() const { return stats_; }

void GameLogicSystem::Tick(float fixedDt) {
  T8_TELEMETRY_SCOPE("game.update");
  ApplyDeferredCreates();
  {
    T8_TELEMETRY_SCOPE("game.events.dispatch");
    events_.DispatchQueued(tickIndex_);
  }
  intents_.clear();
  for (GameObject& object : registry_.Objects()) {
    if (object.enabled && object.controller) {
      intents_[object.runtimeId] = object.controller->SampleIntent(inputFrame_, fixedDt);
    }
  }
  {
    T8_TELEMETRY_SCOPE("game.components.pre_physics");
    UpdateComponents(ComponentUpdatePhase::PrePhysics, fixedDt);
  }
  physics_.Flush(fixedDt);
  UpdateComponents(ComponentUpdatePhase::PostPhysics, fixedDt);
  navigation_.ResolveCompleted();
  {
    T8_TELEMETRY_SCOPE("game.components.logic");
    UpdateComponents(ComponentUpdatePhase::Logic, fixedDt);
  }
  {
    T8_TELEMETRY_SCOPE("game.state_machines");
    for (GameObject& object : registry_.Objects()) {
      if (object.enabled && object.behavior) object.behavior->Evaluate(object, *this, fixedDt);
    }
  }
  if (groupSystem_) {
    T8_TELEMETRY_SCOPE("game.groups");
    groupSystem_->Update(fixedDt);
  }
  UpdateComponents(ComponentUpdatePhase::Late, fixedDt);
  ApplyDeferredDestroys();
  ++tickIndex_;
}

void GameLogicSystem::UpdateComponents(ComponentUpdatePhase phase, float fixedDt) {
  for (GameObject& object : registry_.Objects()) {
    if (!object.enabled) continue;
    for (const std::unique_ptr<Component>& component : object.components) {
      if (component && component->Enabled() && component->Phase() == phase) {
        component->Update(fixedDt);
        t850::RuntimeTelemetry::AddCounter("game.components.updated", 1.0);
      }
    }
  }
}

void GameLogicSystem::ApplyDeferredCreates() {
  std::vector<PendingComponentAdd> additions;
  additions.swap(pendingComponentAdds_);
  for (PendingComponentAdd& addition : additions) {
    GameObject* object = registry_.Get(addition.id);
    if (!object) continue;
    ComponentLoadContext loadContext{object, this, nullptr};
    std::unique_ptr<Component> component = factories_.Create(addition.descriptor, loadContext);
    if (!component) continue;
    component->id_ = addition.descriptor.id;
    component->enabled_ = addition.descriptor.enabled;
    component->OnAttach(*object, *this);
    component->OnCreate();
    object->components.push_back(std::move(component));
  }
}

void GameLogicSystem::ApplyDeferredDestroys() {
  std::vector<PendingComponentRemove> removals;
  removals.swap(pendingComponentRemoves_);
  for (const PendingComponentRemove& removal : removals) {
    GameObject* object = registry_.Get(removal.id);
    if (!object) continue;
    auto found = std::find_if(object->components.begin(), object->components.end(), [&](const auto& component) {
      return component && component->Id() == removal.componentId;
    });
    if (found != object->components.end()) {
      (*found)->OnDestroy();
      (*found)->OnDetach();
      object->components.erase(found);
    }
  }

  for (RuntimeGameObjectId id : pendingObjectDestroys_) {
    if (GameObject* object = registry_.Get(id)) {
      if (object->controller) object->controller->OnUnpossess();
      intents_.erase(id);
      DestroyComponents(*object);
    }
    registry_.RequestDestroy(id);
  }
  pendingObjectDestroys_.clear();
  registry_.ApplyDeferredDestroys();
}

void GameLogicSystem::DestroyComponents(GameObject& object) {
  for (const std::unique_ptr<Component>& component : object.components) {
    if (!component) continue;
    component->OnDestroy();
    component->OnDetach();
  }
  object.components.clear();
}

void GameLogicSystem::RefreshStats(int lastFrameSteps) {
  stats_.tickIndex = tickIndex_;
  stats_.lastFrameSteps = lastFrameSteps;
  stats_.objectCount = registry_.Count();
  stats_.componentCount = 0;
  for (const GameObject& object : registry_.Objects()) {
    stats_.componentCount += object.components.size();
  }
  stats_.eventsQueued = events_.PendingCount();
  stats_.eventsDispatched = events_.LastDispatchCount();
  stats_.validationErrors = validationErrors_;
  stats_.validationWarnings = validationWarnings_;
  stats_.presentationAlpha = presentationAlpha_;
  std::size_t activeObjects = 0;
  for (const GameObject& object : registry_.Objects()) {
    if (object.enabled) ++activeObjects;
  }
  t850::RuntimeTelemetry::SetCounter("game.entities.total", static_cast<double>(stats_.objectCount));
  t850::RuntimeTelemetry::SetCounter("game.entities.active", static_cast<double>(activeObjects));
  t850::RuntimeTelemetry::SetCounter("game.components.total", static_cast<double>(stats_.componentCount));
  t850::RuntimeTelemetry::SetCounter("game.events.queued", static_cast<double>(stats_.eventsQueued));
  t850::RuntimeTelemetry::SetCounter("game.events.dispatched", static_cast<double>(stats_.eventsDispatched));
  t850::RuntimeTelemetry::SetCounter("game.validation.errors", static_cast<double>(stats_.validationErrors));
  t850::RuntimeTelemetry::SetCounter("game.validation.warnings", static_cast<double>(stats_.validationWarnings));
}

} // namespace t850::game