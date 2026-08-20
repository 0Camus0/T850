#include <pch.h>

#include <game/Controller.h>

#include <game/GameObject.h>
#include <scene/PrimitiveInstance.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace t850::game {
namespace {

XVECTOR3 NormalizePlanar(XVECTOR3 value) {
  value.y = 0.0f;
  value.w = 0.0f;
  const float length = std::sqrt(value.x * value.x + value.z * value.z);
  if (length <= 0.000001f) return XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  return value / length;
}

XVECTOR3 PrimitivePosition(const GameObject* object) {
  if (!object || !object->links.primitive) return XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  const XMATRIX44& position = object->links.primitive->Position;
  return XVECTOR3(position.m[3][0], position.m[3][1], position.m[3][2], 1.0f);
}

} // namespace

PlayerController::PlayerController(int playerSlot, std::string profile)
    : playerSlot_(playerSlot), profile_(std::move(profile)) {}

MovementIntent PlayerController::SampleIntent(const InputFrame& input, float fixedDt) {
  (void)fixedDt;
  MovementIntent intent;
  const float inputLength = std::sqrt(
      input.moveAxis.x * input.moveAxis.x + input.moveAxis.z * input.moveAxis.z);
  intent.moveDir = NormalizePlanar(input.moveAxis);
  intent.speedScale = (std::min)(1.0f, inputLength);
  intent.actionBits = input.buttonsDown | input.buttonsPressed;

  lookDirection_.x += input.lookDelta.x;
  lookDirection_.y += input.lookDelta.y;
  if (lookDirection_.Length() > 0.000001f) lookDirection_.Normalize();
  intent.lookDir = lookDirection_;
  return intent;
}

AIController::AIController(std::string profile) : profile_(std::move(profile)) {}

MovementIntent AIController::SampleIntent(const InputFrame& input, float fixedDt) {
  (void)input;
  (void)fixedDt;
  MovementIntent intent;
  if (!navigationGoal_.has_value()) {
    intent.speedScale = 0.0f;
    return intent;
  }

  const XVECTOR3 position = PrimitivePosition(pawn_);
  const XVECTOR3 toGoal = *navigationGoal_ - position;
  intent.moveDir = NormalizePlanar(toGoal);
  intent.lookDir = intent.moveDir.Length() > 0.000001f
      ? intent.moveDir
      : XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  intent.navGoal = navigationGoal_;
  intent.hasNavGoal = true;
  intent.speedScale = intent.moveDir.Length() > 0.000001f ? 1.0f : 0.0f;
  return intent;
}

void AIController::SetNavigationGoal(const XVECTOR3& goal) {
  navigationGoal_ = goal;
}

void AIController::ClearNavigationGoal() {
  navigationGoal_.reset();
}

} // namespace t850::game