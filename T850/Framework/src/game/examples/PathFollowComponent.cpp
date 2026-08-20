#include <pch.h>

#include <game/examples/PathFollowComponent.h>

#include <game/ComponentFactory.h>
#include <game/Controller.h>
#include <game/GameLogicSystem.h>
#include <game/GameObject.h>
#include <scene/PrimitiveInstance.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace t850::game::examples {
namespace {

constexpr float kGoalChangeEpsilonSquared = 0.0001f;
constexpr float kWaypointDistance = 0.35f;
constexpr float kFailedPathRetrySeconds = 0.5f;

float PlanarDistanceSquared(const XVECTOR3& left, const XVECTOR3& right) {
  const float deltaX = left.x - right.x;
  const float deltaZ = left.z - right.z;
  return deltaX * deltaX + deltaZ * deltaZ;
}

XVECTOR3 PrimitivePosition(const t850::PrimitiveInst& primitive) {
  return XVECTOR3(
      primitive.Position.m[3][0],
      primitive.Position.m[3][1],
      primitive.Position.m[3][2],
      1.0f);
}

} // namespace

PathFollowComponent::PathFollowComponent(t850::scene::SceneComponentDesc descriptor)
    : descriptor_(std::move(descriptor)) {}

void PathFollowComponent::OnCreate() {
  if (!owner_) return;
  const GameNavigationAgentSettings& settings = owner_->links.navigationAgent;
  followDistance_ = (std::max)(0.0f, settings.followDistance);
  sideOffset_ = settings.sideOffset;
  formationDepthStep_ = settings.formationDepthStep;
  formationSlot_ = settings.formationSlot;
}

void PathFollowComponent::Update(float fixedDt) {
  if (!owner_ || !system_ || !owner_->links.primitive ||
      !owner_->controller || owner_->controller->Kind() != ControllerKind::AI) {
    return;
  }

  DrainSupersededResults();
  retryRemainingSeconds_ = (std::max)(0.0f, retryRemainingSeconds_ - fixedDt);
  auto& controller = static_cast<AIController&>(*owner_->controller);
  const MovementIntent& intent = system_->IntentFor(owner_->runtimeId);
  const std::optional<XVECTOR3> observedGoal =
      intent.hasNavGoal && intent.navGoal.has_value() ? intent.navGoal : std::nullopt;

  if (observedGoal.has_value()) {
    const bool changedFromIssued = issuedSteeringGoal_.has_value() &&
        PlanarDistanceSquared(*observedGoal, *issuedSteeringGoal_) > kGoalChangeEpsilonSquared;
    const bool newDestination = !destination_.has_value() ||
        (!issuedSteeringGoal_.has_value() &&
         PlanarDistanceSquared(*observedGoal, *destination_) > kGoalChangeEpsilonSquared);
    if (changedFromIssued || newDestination) BeginDestination(*observedGoal);
  } else if (issuedSteeringGoal_.has_value()) {
    BeginDestination(XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f));
    destination_.reset();
    issuedSteeringGoal_.reset();
    return;
  }

  if (!destination_.has_value()) return;

  GameNavigationService& navigation = system_->Navigation();
  if (requestId_ != GameNavigationService::kInvalidRequestId) {
    t850::navigation::NavPathResult result;
    if (!navigation.TryGetResult(requestId_, result)) return;
    requestId_ = GameNavigationService::kInvalidRequestId;
    pathPoints_.clear();
    waypointIndex_ = 0;
    if (result.success && !result.points.empty()) {
      pathPoints_ = std::move(result.points);
      waypointIndex_ = pathPoints_.size() > 1 ? 1 : 0;
    } else {
      retryRemainingSeconds_ = kFailedPathRetrySeconds;
      IssueSteeringGoal(controller, *destination_);
    }
  }

  const XVECTOR3 position = PrimitivePosition(*owner_->links.primitive);
  if (!pathPoints_.empty()) {
    while (waypointIndex_ < pathPoints_.size()) {
      const bool finalWaypoint = waypointIndex_ + 1 == pathPoints_.size();
      const float reachDistance = finalWaypoint
          ? (std::max)(kWaypointDistance, followDistance_)
          : kWaypointDistance;
      if (PlanarDistanceSquared(position, pathPoints_[waypointIndex_]) >
          reachDistance * reachDistance) {
        break;
      }
      ++waypointIndex_;
    }

    if (waypointIndex_ >= pathPoints_.size()) {
      CompleteDestination(controller);
      return;
    }
    IssueSteeringGoal(controller, pathPoints_[waypointIndex_]);
    return;
  }

  IssueSteeringGoal(controller, *destination_);
  if (retryRemainingSeconds_ > 0.0f || !navigation.Available()) return;
  requestId_ = navigation.RequestPath(owner_->runtimeId, position, *destination_);
  if (requestId_ == GameNavigationService::kInvalidRequestId) {
    retryRemainingSeconds_ = kFailedPathRetrySeconds;
  }
}

bool PathFollowComponent::TryGetFloat(std::string_view name, float& value) const {
  if (name == "nav_agent_follow_distance") value = followDistance_;
  else if (name == "nav_agent_side_offset") value = sideOffset_;
  else if (name == "nav_agent_formation_depth_step") value = formationDepthStep_;
  else if (name == "nav_agent_slot") value = static_cast<float>(formationSlot_);
  else return false;
  return true;
}

void PathFollowComponent::BeginDestination(const XVECTOR3& goal) {
  if (requestId_ != GameNavigationService::kInvalidRequestId) {
    supersededRequestIds_.push_back(requestId_);
  }
  requestId_ = GameNavigationService::kInvalidRequestId;
  destination_ = goal;
  issuedSteeringGoal_ = goal;
  pathPoints_.clear();
  waypointIndex_ = 0;
  retryRemainingSeconds_ = 0.0f;
}

void PathFollowComponent::IssueSteeringGoal(AIController& controller, const XVECTOR3& goal) {
  controller.SetNavigationGoal(goal);
  issuedSteeringGoal_ = goal;
}

void PathFollowComponent::CompleteDestination(AIController& controller) {
  controller.ClearNavigationGoal();
  destination_.reset();
  issuedSteeringGoal_.reset();
  pathPoints_.clear();
  waypointIndex_ = 0;
}

void PathFollowComponent::DrainSupersededResults() {
  if (!system_) return;
  t850::navigation::NavPathResult ignored;
  for (auto request = supersededRequestIds_.begin(); request != supersededRequestIds_.end();) {
    if (system_->Navigation().TryGetResult(*request, ignored)) {
      request = supersededRequestIds_.erase(request);
    } else {
      ++request;
    }
  }
}

std::unique_ptr<Component> CreatePathFollowComponent(
    const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context) {
  (void)context;
  return std::make_unique<PathFollowComponent>(descriptor);
}

void RegisterPathFollowComponent(ComponentFactoryRegistry& registry) {
  registry.Register("path_follow", CreatePathFollowComponent,
                    ComponentTypeInfo{.type = "path_follow", .allowMultiple = false});
}

} // namespace t850::game::examples