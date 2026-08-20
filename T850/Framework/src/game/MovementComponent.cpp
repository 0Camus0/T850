#include <pch.h>

#include <game/MovementComponent.h>

#include <game/GameLogicSystem.h>
#include <game/GameObject.h>
#include <scene/PrimitiveInstance.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace t850::game {
namespace {

float ParseFloat(const std::map<std::string, std::string>& params,
                 std::string_view key,
                 float fallback) {
  const auto found = params.find(std::string(key));
  if (found == params.end()) return fallback;
  char* end = nullptr;
  const float parsed = std::strtof(found->second.c_str(), &end);
  return end == found->second.c_str() + found->second.size() && std::isfinite(parsed)
      ? parsed
      : fallback;
}

float Approach(float current, float target, float maximumDelta) {
  if (current < target) return (std::min)(current + maximumDelta, target);
  return (std::max)(current - maximumDelta, target);
}

} // namespace

MovementComponent::MovementComponent(t850::scene::SceneComponentDesc descriptor)
    : descriptor_(std::move(descriptor)) {}

void MovementComponent::OnCreate() {
  maxSpeed_ = (std::max)(0.0f, ParseFloat(descriptor_.params, "maxSpeed", maxSpeed_));
  accel_ = (std::max)(0.0f, ParseFloat(descriptor_.params, "accel", accel_));
  turnRateRadPerSec_ = (std::max)(0.0f, ParseFloat(descriptor_.params, "turnRate", turnRateRadPerSec_));
  const auto mode = descriptor_.params.find("mode");
  if (mode != descriptor_.params.end()) {
    if (mode->second == "dynamic") mode_ = Mode::Dynamic;
    else if (mode->second == "transform" || mode->second == "transform_only") mode_ = Mode::TransformOnly;
    else mode_ = Mode::Kinematic;
  }
}

void MovementComponent::Update(float fixedDt) {
  if (!owner_ || !system_ || !owner_->links.primitive || fixedDt <= 0.0f) return;
  const MovementIntent& intent = system_->IntentFor(owner_->runtimeId);
  const XVECTOR3 targetVelocity = intent.moveDir * (maxSpeed_ * intent.speedScale);
  const float maximumDelta = accel_ * fixedDt;
  velocity_.x = Approach(velocity_.x, targetVelocity.x, maximumDelta);
  velocity_.y = Approach(velocity_.y, targetVelocity.y, maximumDelta);
  velocity_.z = Approach(velocity_.z, targetVelocity.z, maximumDelta);

  const float velocitySquared =
      velocity_.x * velocity_.x + velocity_.y * velocity_.y + velocity_.z * velocity_.z;
  if (velocitySquared <= 0.00000001f) {
    velocity_ = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
    return;
  }

  t850::PrimitiveInst& primitive = *owner_->links.primitive;
  const XVECTOR3 position(
      primitive.Position.m[3][0],
      primitive.Position.m[3][1],
      primitive.Position.m[3][2],
      1.0f);
  const XVECTOR3 next = position + velocity_ * fixedDt;

  primitive.TranslateAbsolute(next.x, next.y, next.z);
  if (intent.lookDir.x * intent.lookDir.x + intent.lookDir.z * intent.lookDir.z > 0.000001f &&
      turnRateRadPerSec_ > 0.0f) {
    const float yawDegrees = Rad2Deg(std::atan2(intent.lookDir.x, intent.lookDir.z));
    primitive.RotateYAbsolute(yawDegrees);
  }
  primitive.Update();

  if (owner_->links.primaryBody.IsValid() && mode_ != Mode::TransformOnly) {
    if (mode_ == Mode::Dynamic) {
      system_->Physics().EnqueueSetVelocity(
          owner_->runtimeId, velocity_, XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
    } else {
      system_->Physics().EnqueueKinematicMove(owner_->runtimeId, primitive.Final);
    }
  }
}

std::unique_ptr<Component> CreateMovementComponent(
    const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context) {
  (void)context;
  return std::make_unique<MovementComponent>(descriptor);
}

} // namespace t850::game