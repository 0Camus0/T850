#include <pch.h>

#include <physics/CharacterController.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace t850 {
namespace {

constexpr float kCollisionSkin = 0.001f;
constexpr float kInitialHitEpsilon = 0.0005f;

float ClampFloat(float value, float minValue, float maxValue) {
  return (std::max)(minValue, (std::min)(value, maxValue));
}

float Dot3(const XVECTOR3& a, const XVECTOR3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float LengthSq3(const XVECTOR3& value) {
  return Dot3(value, value);
}

float Length3(const XVECTOR3& value) {
  return std::sqrt(LengthSq3(value));
}

XVECTOR3 NormalizeOr(XVECTOR3 value, const XVECTOR3& fallback) {
  const float length = Length3(value);
  if (length <= 0.000001f) {
    return fallback;
  }
  value.x /= length;
  value.y /= length;
  value.z /= length;
  return value;
}

XVECTOR3 FlattenHorizontal(const XVECTOR3& value, const XVECTOR3& fallback) {
  return NormalizeOr(XVECTOR3(value.x, 0.0f, value.z, 0.0f), fallback);
}

float CombineMoveAxis(float analogAmount, bool positive, bool negative) {
  float value = ClampFloat(analogAmount, -1.0f, 1.0f);
  if (positive) {
    value += 1.0f;
  }
  if (negative) {
    value -= 1.0f;
  }
  return ClampFloat(value, -1.0f, 1.0f);
}

float ForwardMoveAmount(const KinematicCharacterInput& input) {
  return CombineMoveAxis(input.moveForwardAmount, input.moveForward, input.moveBackward);
}

float RightMoveAmount(const KinematicCharacterInput& input) {
  return CombineMoveAxis(input.moveRightAmount, input.moveRight, input.moveLeft);
}

float MoveCommandMagnitude(float forwardMove, float rightMove) {
  return ClampFloat(std::sqrt(forwardMove * forwardMove + rightMove * rightMove), 0.0f, 1.0f);
}

int Q3MoveCommand(float amount) {
  return static_cast<int>(std::round(ClampFloat(amount, -1.0f, 1.0f) * 127.0f));
}

bool SweepCapsule(const CharacterControllerContext& context,
                  const XVECTOR3& startCenter,
                  const XVECTOR3& displacement,
                  float radius,
                  float halfHeight,
                  CharacterCollisionHit& outHit) {
  if (!context.collisionWorld || LengthSq3(displacement) <= 0.00000001f) {
    return false;
  }

  CharacterCollisionSweep sweep;
  sweep.startCenter = startCenter;
  sweep.displacement = displacement;
  sweep.radius = radius;
  sweep.halfHeight = halfHeight;
  return context.collisionWorld->SweepCapsule(sweep, outHit) && outHit.hit;
}

XVECTOR3 BoxHalfExtentsFromSettings(const KinematicCharacterSettings& settings) {
  return XVECTOR3(
      settings.capsuleRadius,
      settings.capsuleHalfHeight + settings.capsuleRadius,
      settings.capsuleRadius,
      0.0f);
}

bool ApplyCharacterTriggerTouch(const CharacterControllerContext& context,
                                const KinematicCharacterSettings& settings,
                                const XVECTOR3& position,
                                XVECTOR3& velocity,
                                bool& grounded,
                                uint32_t& lastTriggerEntityId) {
  if (!context.collisionWorld) {
    lastTriggerEntityId = 0;
    return false;
  }

  CharacterTriggerQuery query;
  query.center = position;
  query.halfExtents = BoxHalfExtentsFromSettings(settings);

  CharacterTriggerTouch touch;
  if (!context.collisionWorld->QueryTriggerTouch(query, touch)) {
    lastTriggerEntityId = 0;
    return false;
  }

  if (touch.type == CharacterTriggerTouch::Type::JumpPad) {
    velocity = touch.velocity;
    velocity.w = 0.0f;
    grounded = false;
    if (touch.entityId != 0 && touch.entityId != lastTriggerEntityId) {
      T8_LOG_INFO(
          "[CharacterController] Jump pad %u velocity=(%.3f, %.3f, %.3f)",
          touch.entityId,
          velocity.x,
          velocity.y,
          velocity.z);
    }
    lastTriggerEntityId = touch.entityId;
    return true;
  }

  return false;
}

bool SweepCharacterShape(const CharacterControllerContext& context,
                         const XVECTOR3& startCenter,
                         const XVECTOR3& displacement,
                         const KinematicCharacterSettings& settings,
                         CharacterCollisionHit& outHit) {
  if (!context.collisionWorld || LengthSq3(displacement) <= 0.00000001f) {
    return false;
  }

  if (settings.collisionShape == KinematicCharacterSettings::CollisionShape::Box) {
    CharacterBoxSweep sweep;
    sweep.startCenter = startCenter;
    sweep.displacement = displacement;
    sweep.halfExtents = BoxHalfExtentsFromSettings(settings);
    return context.collisionWorld->SweepBox(sweep, outHit) && outHit.hit;
  }

  return SweepCapsule(
      context,
      startCenter,
      displacement,
      settings.capsuleRadius,
      settings.capsuleHalfHeight,
      outHit);
}

bool SweepCharacterShapePastInitialTouch(const CharacterControllerContext& context,
                                         const XVECTOR3& startCenter,
                                         const XVECTOR3& displacement,
                                         const KinematicCharacterSettings& settings,
                                         CharacterCollisionHit& outHit) {
  const float totalLength = Length3(displacement);
  if (totalLength <= 0.000001f) {
    return false;
  }

  XVECTOR3 currentStart = startCenter;
  XVECTOR3 remaining = displacement;
  float consumedLength = 0.0f;

  for (int attempt = 0; attempt < 3; ++attempt) {
    CharacterCollisionHit hit;
    if (!SweepCharacterShape(context, currentStart, remaining, settings, hit)) {
      return false;
    }

    const XVECTOR3 normal = NormalizeOr(hit.normal, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    if (hit.fraction > kInitialHitEpsilon || Dot3(remaining, normal) < -0.00001f) {
      const float remainingLength = Length3(remaining);
      hit.fraction = ClampFloat((consumedLength + hit.fraction * remainingLength) / totalLength, 0.0f, 1.0f);
      outHit = hit;
      return true;
    }

    const float remainingLength = Length3(remaining);
    const float nudgeLength = (std::min)(
        remainingLength,
        (std::max)(kCollisionSkin * 4.0f, remainingLength * 0.01f));
    if (nudgeLength <= 0.000001f || remainingLength <= nudgeLength + 0.000001f) {
      return false;
    }

    const XVECTOR3 direction = remaining * (1.0f / remainingLength);
    currentStart += direction * nudgeLength;
    currentStart.w = 1.0f;
    consumedLength += nudgeLength;
    remaining = displacement - direction * consumedLength;
    remaining.w = 0.0f;
  }

  return false;
}

XVECTOR3 ClipVelocity(const XVECTOR3& velocity, const XVECTOR3& normal, float overclip = 1.001f) {
  XVECTOR3 out = velocity;
  float backoff = Dot3(out, normal);
  if (backoff < 0.0f) {
    backoff *= overclip;
  } else {
    backoff /= overclip;
  }
  out.x -= normal.x * backoff;
  out.y -= normal.y * backoff;
  out.z -= normal.z * backoff;
  out.w = 0.0f;
  return out;
}

XVECTOR3 SlideCapsule(const CharacterControllerContext& context,
                      const XVECTOR3& startCenter,
                      const XVECTOR3& displacement,
                      float radius,
                      float halfHeight,
                      XVECTOR3* inOutVelocity = nullptr) {
  XVECTOR3 position = startCenter;
  XVECTOR3 remaining = displacement;

  for (int bump = 0; bump < 4; ++bump) {
    if (LengthSq3(remaining) <= 0.00000001f) {
      break;
    }

    CharacterCollisionHit hit;
    if (!SweepCapsule(context, position, remaining, radius, halfHeight, hit)) {
      position += remaining;
      break;
    }

    const XVECTOR3 normal = NormalizeOr(hit.normal, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    const float normalMotion = Dot3(remaining, normal);
    if (hit.fraction <= kInitialHitEpsilon && normalMotion >= -0.00001f) {
      position += remaining;
      break;
    }

    const float travelFraction = ClampFloat(hit.fraction - kCollisionSkin, 0.0f, 1.0f);
    position += remaining * travelFraction;
    if (hit.fraction <= kInitialHitEpsilon) {
      const float depenetration = (std::max)(kCollisionSkin * 4.0f, (std::min)(radius * 0.05f, 0.03f));
      position += normal * depenetration;
    }

    remaining = ClipVelocity(remaining * (1.0f - travelFraction), normal);
    if (inOutVelocity) {
      *inOutVelocity = ClipVelocity(*inOutVelocity, normal);
    }
  }

  position.w = 1.0f;
  return position;
}

XVECTOR3 StepSlideCapsule(const CharacterControllerContext& context,
                          const XVECTOR3& startCenter,
                          const XVECTOR3& displacement,
                          const KinematicCharacterSettings& settings,
                          XVECTOR3* inOutVelocity) {
  const XVECTOR3 slidePos = SlideCapsule(
      context, startCenter, displacement, settings.capsuleRadius, settings.capsuleHalfHeight, inOutVelocity);

  const XVECTOR3 horizontalDelta(displacement.x, 0.0f, displacement.z, 0.0f);
  if (!context.collisionWorld || LengthSq3(horizontalDelta) <= 0.00000001f || settings.stepHeight <= 0.0f) {
    return slidePos;
  }

  const XVECTOR3 up(0.0f, settings.stepHeight, 0.0f, 0.0f);
  CharacterCollisionHit upHit;
  XVECTOR3 upCenter = startCenter;
  if (SweepCapsule(context, startCenter, up, settings.capsuleRadius, settings.capsuleHalfHeight, upHit)) {
    upCenter += up * ClampFloat(upHit.fraction - kCollisionSkin, 0.0f, 1.0f);
  } else {
    upCenter += up;
  }

  XVECTOR3 stepVelocity = inOutVelocity ? *inOutVelocity : XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 stepPos = SlideCapsule(context, upCenter, displacement, settings.capsuleRadius, settings.capsuleHalfHeight, &stepVelocity);

  const XVECTOR3 down(0.0f, -settings.stepHeight, 0.0f, 0.0f);
  CharacterCollisionHit downHit;
  if (SweepCapsule(context, stepPos, down, settings.capsuleRadius, settings.capsuleHalfHeight, downHit)) {
    stepPos += down * ClampFloat(downHit.fraction, 0.0f, 1.0f);
  } else {
    return slidePos;
  }

  const XVECTOR3 slideHorizontal(slidePos.x - startCenter.x, 0.0f, slidePos.z - startCenter.z, 0.0f);
  const XVECTOR3 stepHorizontal(stepPos.x - startCenter.x, 0.0f, stepPos.z - startCenter.z, 0.0f);
  if (LengthSq3(stepHorizontal) > LengthSq3(slideHorizontal) + 0.000001f) {
    if (inOutVelocity) {
      *inOutVelocity = stepVelocity;
    }
    stepPos.w = 1.0f;
    return stepPos;
  }

  return slidePos;
}

void Accelerate(XVECTOR3& velocity, const XVECTOR3& wishDir, float wishSpeed, float accel, float deltaSeconds) {
  const float currentSpeed = Dot3(velocity, wishDir);
  float addSpeed = wishSpeed - currentSpeed;
  if (addSpeed <= 0.0f) {
    return;
  }

  float accelSpeed = accel * deltaSeconds * wishSpeed;
  if (accelSpeed > addSpeed) {
    accelSpeed = addSpeed;
  }

  velocity.x += accelSpeed * wishDir.x;
  velocity.y += accelSpeed * wishDir.y;
  velocity.z += accelSpeed * wishDir.z;
  velocity.w = 0.0f;
}

void ApplyGroundFriction(XVECTOR3& velocity, float friction, float stopSpeed, float deltaSeconds) {
  XVECTOR3 horizontal(velocity.x, 0.0f, velocity.z, 0.0f);
  const float speed = Length3(horizontal);
  if (speed < 0.0001f) {
    velocity.x = 0.0f;
    velocity.z = 0.0f;
    return;
  }

  const float control = speed < stopSpeed ? stopSpeed : speed;
  const float drop = control * friction * deltaSeconds;
  const float newSpeed = (std::max)(0.0f, speed - drop) / speed;
  velocity.x *= newSpeed;
  velocity.z *= newSpeed;
}

XVECTOR3 Cross3(const XVECTOR3& a, const XVECTOR3& b) {
  return XVECTOR3(
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
      0.0f);
}

float HorizontalLengthSq(const XVECTOR3& value) {
  return value.x * value.x + value.z * value.z;
}

float CmdScale(int forwardMove, int rightMove, int upMove, float speed) {
  const int maxMove = (std::max)((std::max)(std::abs(forwardMove), std::abs(rightMove)), std::abs(upMove));
  if (maxMove <= 0) {
    return 0.0f;
  }

  const float total = std::sqrt(
      static_cast<float>(forwardMove * forwardMove + rightMove * rightMove + upMove * upMove));
  if (total <= 0.000001f) {
    return 0.0f;
  }

  return speed * static_cast<float>(maxMove) / (127.0f * total);
}

struct GroundTraceState {
  bool groundPlane = false;
  bool walking = false;
  XVECTOR3 normal = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
};

GroundTraceState TraceGround(const CharacterControllerContext& context,
                             const XVECTOR3& center,
                             const XVECTOR3& velocity,
                             const KinematicCharacterSettings& settings) {
  GroundTraceState state;
  if (!context.collisionWorld) {
    state.groundPlane = true;
    state.walking = true;
    return state;
  }

  CharacterCollisionHit hit;
  const XVECTOR3 probe(0.0f, -settings.groundProbeDistance, 0.0f, 0.0f);
  if (!SweepCharacterShapePastInitialTouch(context, center, probe, settings, hit)) {
    T8_LOG_VERBOSE(
        "[Q3Ground] miss center=(%.4f, %.4f, %.4f) velocity=(%.4f, %.4f, %.4f) probe=%.4f",
        center.x,
        center.y,
        center.z,
        velocity.x,
        velocity.y,
        velocity.z,
        settings.groundProbeDistance);
    return state;
  }

  state.normal = NormalizeOr(hit.normal, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  if (velocity.y > 0.0f && Dot3(velocity, state.normal) > 10.0f / 32.0f) {
    return state;
  }

  state.groundPlane = true;
  state.walking = state.normal.y >= settings.minWalkNormalY;
  T8_LOG_VERBOSE(
      "[Q3Ground] hit center=(%.4f, %.4f, %.4f) fraction=%.5f normal=(%.4f, %.4f, %.4f) walking=%d velocity=(%.4f, %.4f, %.4f)",
      center.x,
      center.y,
      center.z,
      hit.fraction,
      state.normal.x,
      state.normal.y,
      state.normal.z,
      state.walking ? 1 : 0,
      velocity.x,
      velocity.y,
      velocity.z);
  return state;
}

bool Q3SlideMove(const CharacterControllerContext& context,
                 XVECTOR3& position,
                 XVECTOR3& velocity,
                 float deltaSeconds,
                 bool applyGravity,
                 const GroundTraceState& ground,
                 const KinematicCharacterSettings& settings) {
  constexpr int maxClipPlanes = 5;
  constexpr float planeInteractEpsilon = 0.1f;

  bool clipped = false;
  XVECTOR3 endVelocity = velocity;

  if (applyGravity) {
    endVelocity.y -= settings.gravity * deltaSeconds;
    velocity.y = (velocity.y + endVelocity.y) * 0.5f;
    if (ground.groundPlane) {
      velocity = ClipVelocity(velocity, ground.normal);
    }
  }

  T8_LOG_VERBOSE(
      "[Q3Slide] begin dt=%.5f gravity=%d pos=(%.4f, %.4f, %.4f) vel=(%.4f, %.4f, %.4f) groundPlane=%d walking=%d groundNormal=(%.4f, %.4f, %.4f)",
      deltaSeconds,
      applyGravity ? 1 : 0,
      position.x,
      position.y,
      position.z,
      velocity.x,
      velocity.y,
      velocity.z,
      ground.groundPlane ? 1 : 0,
      ground.walking ? 1 : 0,
      ground.normal.x,
      ground.normal.y,
      ground.normal.z);

  float timeLeft = deltaSeconds;
  std::array<XVECTOR3, maxClipPlanes> planes;
  int planeCount = 0;

  if (ground.groundPlane) {
    planes[planeCount++] = ground.normal;
  }

  if (LengthSq3(velocity) > 0.00000001f && planeCount < maxClipPlanes) {
    planes[planeCount++] = NormalizeOr(velocity, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
  }

  for (int bump = 0; bump < 4; ++bump) {
    if (LengthSq3(velocity) <= 0.00000001f || timeLeft <= 0.0f) {
      break;
    }

    const XVECTOR3 displacement = velocity * timeLeft;
    CharacterCollisionHit hit;
    if (!SweepCharacterShapePastInitialTouch(context, position, displacement, settings, hit)) {
      T8_LOG_VERBOSE(
          "[Q3Slide] clear bump=%d timeLeft=%.5f pos=(%.4f, %.4f, %.4f) disp=(%.4f, %.4f, %.4f) next=(%.4f, %.4f, %.4f)",
          bump,
          timeLeft,
          position.x,
          position.y,
          position.z,
          displacement.x,
          displacement.y,
          displacement.z,
          position.x + displacement.x,
          position.y + displacement.y,
          position.z + displacement.z);
      position += displacement;
      break;
    }

    const XVECTOR3 normal = NormalizeOr(hit.normal, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    if (hit.fraction <= kInitialHitEpsilon && Dot3(velocity, normal) >= -0.00001f) {
      T8_LOG_VERBOSE(
          "[Q3Slide] initial-touch-nonblocking bump=%d fraction=%.5f dot=%.6f normal=(%.4f, %.4f, %.4f) pos=(%.4f, %.4f, %.4f) disp=(%.4f, %.4f, %.4f)",
          bump,
          hit.fraction,
          Dot3(velocity, normal),
          normal.x,
          normal.y,
          normal.z,
          position.x,
          position.y,
          position.z,
          displacement.x,
          displacement.y,
          displacement.z);
      position += displacement;
      break;
    }

    T8_LOG_VERBOSE(
        "[Q3Slide] hit bump=%d timeLeft=%.5f fraction=%.5f normal=(%.4f, %.4f, %.4f) pos=(%.4f, %.4f, %.4f) disp=(%.4f, %.4f, %.4f)",
        bump,
        timeLeft,
        hit.fraction,
        normal.x,
        normal.y,
        normal.z,
        position.x,
        position.y,
        position.z,
        displacement.x,
        displacement.y,
        displacement.z);

    if (hit.fraction > 0.0f) {
      position += displacement * ClampFloat(hit.fraction, 0.0f, 1.0f);
    }

    if (hit.fraction >= 1.0f) {
      break;
    }

    clipped = true;
    timeLeft -= timeLeft * ClampFloat(hit.fraction, 0.0f, 1.0f);

    if (planeCount >= maxClipPlanes) {
      velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
      if (applyGravity) {
        endVelocity = velocity;
      }
      T8_LOG_VERBOSE("[Q3Slide] max clip planes reached; stopping at pos=(%.4f, %.4f, %.4f)",
                     position.x,
                     position.y,
                     position.z);
      return true;
    }

    bool duplicatePlane = false;
    for (int i = 0; i < planeCount; ++i) {
      if (Dot3(normal, planes[i]) > 0.99f) {
        velocity += normal;
        if (applyGravity) {
          endVelocity += normal;
        }
        duplicatePlane = true;
        break;
      }
    }
    if (duplicatePlane) {
      continue;
    }

    planes[planeCount++] = normal;

    bool resolved = false;
    for (int i = 0; i < planeCount; ++i) {
      if (Dot3(velocity, planes[i]) >= planeInteractEpsilon) {
        continue;
      }

      XVECTOR3 clipVelocity = ClipVelocity(velocity, planes[i]);
      XVECTOR3 endClipVelocity = applyGravity ? ClipVelocity(endVelocity, planes[i]) : endVelocity;

      for (int j = 0; j < planeCount; ++j) {
        if (j == i || Dot3(clipVelocity, planes[j]) >= planeInteractEpsilon) {
          continue;
        }

        clipVelocity = ClipVelocity(clipVelocity, planes[j]);
        if (applyGravity) {
          endClipVelocity = ClipVelocity(endClipVelocity, planes[j]);
        }

        if (Dot3(clipVelocity, planes[i]) >= 0.0f) {
          continue;
        }

        XVECTOR3 crease = NormalizeOr(Cross3(planes[i], planes[j]), XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
        const float velocityOnCrease = Dot3(crease, velocity);
        clipVelocity = crease * velocityOnCrease;
        if (applyGravity) {
          const float endVelocityOnCrease = Dot3(crease, endVelocity);
          endClipVelocity = crease * endVelocityOnCrease;
        }

        for (int k = 0; k < planeCount; ++k) {
          if (k == i || k == j || Dot3(clipVelocity, planes[k]) >= planeInteractEpsilon) {
            continue;
          }

          velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
          if (applyGravity) {
            endVelocity = velocity;
          }
          return true;
        }
      }

      velocity = clipVelocity;
      if (applyGravity) {
        endVelocity = endClipVelocity;
      }
      resolved = true;
      break;
    }

    if (!resolved) {
      T8_LOG_VERBOSE("[Q3Slide] unresolved clip planes=%d pos=(%.4f, %.4f, %.4f) vel=(%.4f, %.4f, %.4f)",
                     planeCount,
                     position.x,
                     position.y,
                     position.z,
                     velocity.x,
                     velocity.y,
                     velocity.z);
      break;
    }
  }

  if (applyGravity) {
    velocity = endVelocity;
  }
  velocity.w = 0.0f;
  position.w = 1.0f;
  T8_LOG_VERBOSE(
      "[Q3Slide] end clipped=%d pos=(%.4f, %.4f, %.4f) vel=(%.4f, %.4f, %.4f)",
      clipped ? 1 : 0,
      position.x,
      position.y,
      position.z,
      velocity.x,
      velocity.y,
      velocity.z);
  return clipped;
}

void Q3StepSlideMove(const CharacterControllerContext& context,
                     XVECTOR3& position,
                     XVECTOR3& velocity,
                     float deltaSeconds,
                     bool applyGravity,
                     const GroundTraceState& ground,
                     const KinematicCharacterSettings& settings) {
  const XVECTOR3 startPosition = position;
  const XVECTOR3 startVelocity = velocity;
  const float stepClearance = (std::max)(kCollisionSkin * 4.0f, settings.groundProbeDistance * 0.05f);
  const float stepLift = settings.stepHeight + stepClearance;

  if (!Q3SlideMove(context, position, velocity, deltaSeconds, applyGravity, ground, settings)) {
    T8_LOG_VERBOSE("[Q3Step] no slide collision; step skipped pos=(%.4f, %.4f, %.4f)",
                   position.x,
                   position.y,
                   position.z);
    return;
  }

  const XVECTOR3 slidePosition = position;
  const XVECTOR3 slideVelocity = velocity;
  auto restoreSlide = [&]() {
    position = slidePosition;
    velocity = slideVelocity;
    position.w = 1.0f;
    velocity.w = 0.0f;
  };

  CharacterCollisionHit downHit;
  const XVECTOR3 downFromStart(0.0f, -stepLift, 0.0f, 0.0f);
  const bool groundBelow = SweepCharacterShapePastInitialTouch(context, startPosition, downFromStart, settings, downHit);
  const bool walkableSupport = ground.walking ||
                               (groundBelow && downHit.normal.y >= settings.minWalkNormalY);
  if (velocity.y > 0.0f && !walkableSupport) {
    T8_LOG_VERBOSE("[Q3Step] rejected: upward velocity with no walkable ground below");
    return;
  }

  CharacterCollisionHit upHit;
  XVECTOR3 upPosition = startPosition;
  const XVECTOR3 up(0.0f, stepLift, 0.0f, 0.0f);
  if (SweepCharacterShapePastInitialTouch(context, startPosition, up, settings, upHit)) {
    if (upHit.fraction <= kInitialHitEpsilon) {
      T8_LOG_VERBOSE("[Q3Step] rejected: blocked at step-up fraction=%.5f normal=(%.4f, %.4f, %.4f)",
                     upHit.fraction,
                     upHit.normal.x,
                     upHit.normal.y,
                     upHit.normal.z);
      return;
    }
    upPosition += up * ClampFloat(upHit.fraction, 0.0f, 1.0f);
  } else {
    upPosition += up;
  }

  const float stepSize = upPosition.y - startPosition.y;
  if (stepSize <= 0.0001f) {
    T8_LOG_VERBOSE("[Q3Step] rejected: stepSize=%.5f", stepSize);
    return;
  }

  position = upPosition;
  velocity = startVelocity;
  Q3SlideMove(context, position, velocity, deltaSeconds, applyGravity, ground, settings);

  const XVECTOR3 down(0.0f, -stepSize, 0.0f, 0.0f);
  CharacterCollisionHit stepDownHit;
  if (SweepCharacterShapePastInitialTouch(context, position, down, settings, stepDownHit)) {
    const XVECTOR3 stepDownNormal = NormalizeOr(stepDownHit.normal, XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f));
    if (stepDownNormal.y < settings.minWalkNormalY) {
      T8_LOG_VERBOSE("[Q3Step] rejected: step-down not walkable fraction=%.5f normal=(%.4f, %.4f, %.4f)",
                     stepDownHit.fraction,
                     stepDownNormal.x,
                     stepDownNormal.y,
                     stepDownNormal.z);
      restoreSlide();
      return;
    }

    position += down * ClampFloat(stepDownHit.fraction, 0.0f, 1.0f);
    if (stepDownHit.fraction < 1.0f) {
      velocity = ClipVelocity(velocity, stepDownNormal);
    }
    T8_LOG_VERBOSE("[Q3Step] accepted stepSize=%.5f downFraction=%.5f pos=(%.4f, %.4f, %.4f)",
                   stepSize,
                   stepDownHit.fraction,
                   position.x,
                   position.y,
                   position.z);
  } else {
    if (!walkableSupport) {
      T8_LOG_VERBOSE("[Q3Step] rejected: no step-down support while airborne");
      restoreSlide();
      return;
    }
    position += down;
    T8_LOG_VERBOSE("[Q3Step] accepted without down hit stepSize=%.5f pos=(%.4f, %.4f, %.4f)",
                   stepSize,
                   position.x,
                   position.y,
                   position.z);
  }

  position.w = 1.0f;
  velocity.w = 0.0f;
}

} // namespace

KinematicCharacterSettings MakeQuake3CharacterSettings() {
  constexpr float q3ToEngine = 1.0f / 32.0f;
  KinematicCharacterSettings settings;
  settings.collisionShape = KinematicCharacterSettings::CollisionShape::Box;
  settings.walkSpeed = 320.0f * q3ToEngine;
  settings.sprintSpeed = settings.walkSpeed;
  settings.groundAcceleration = 10.0f;
  settings.airAcceleration = 1.0f;
  settings.friction = 6.0f;
  settings.stopSpeed = 100.0f * q3ToEngine;
  settings.gravity = 800.0f * q3ToEngine;
  settings.jumpSpeed = 270.0f * q3ToEngine;
  settings.capsuleRadius = 15.0f * q3ToEngine;
  settings.capsuleHalfHeight = 13.0f * q3ToEngine;
  settings.eyeHeight = 50.0f * q3ToEngine;
  settings.groundProbeDistance = 4.0f * q3ToEngine;
  settings.stepHeight = 18.0f * q3ToEngine;
  settings.allowSprint = false;
  settings.airControl = true;
  return settings;
}

KinematicCharacterController::KinematicCharacterController(const KinematicCharacterSettings& settings)
    : m_settings(settings) {}

void KinematicCharacterController::SetSettings(const KinematicCharacterSettings& settings) {
  m_settings = settings;
}

void KinematicCharacterController::Reset() {
  m_velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  m_grounded = false;
  m_jumpHeld = false;
  m_q3EscapeDiagnosticActive = false;
  m_lastTriggerEntityId = 0;
}

void KinematicCharacterController::SetPosition(const XVECTOR3& capsuleCenter) {
  m_position = capsuleCenter;
  m_position.w = 1.0f;
}

void KinematicCharacterController::SetEyePosition(const XVECTOR3& eyePosition) {
  m_position = eyePosition + XVECTOR3(0.0f, CapsuleCenterOffsetFromEye(), 0.0f, 0.0f);
  m_position.w = 1.0f;
}

XVECTOR3 KinematicCharacterController::GetEyePosition() const {
  XVECTOR3 eye = m_position - XVECTOR3(0.0f, CapsuleCenterOffsetFromEye(), 0.0f, 0.0f);
  eye.w = 1.0f;
  return eye;
}

void KinematicCharacterController::SetVelocity(const XVECTOR3& velocity) {
  m_velocity = velocity;
  m_velocity.w = 0.0f;
}

float KinematicCharacterController::CapsuleCenterOffsetFromEye() const {
  return (m_settings.capsuleHalfHeight + m_settings.capsuleRadius) - m_settings.eyeHeight;
}

void KinematicCharacterController::UpdateFps(float deltaSeconds,
                                             const KinematicCharacterInput& input,
                                             const CharacterControllerContext& context) {
  deltaSeconds = ClampFloat(deltaSeconds, 0.0f, 0.1f);

  if (context.collisionWorld) {
    CharacterCollisionHit groundHit;
    const XVECTOR3 probe(0.0f, -m_settings.groundProbeDistance, 0.0f, 0.0f);
    m_grounded = SweepCapsule(
        context, m_position, probe, m_settings.capsuleRadius, m_settings.capsuleHalfHeight, groundHit) &&
        groundHit.normal.y >= m_settings.minWalkNormalY;
  } else {
    m_grounded = true;
    m_velocity.y = 0.0f;
  }

  if (m_grounded && m_velocity.y < 0.0f) {
    m_velocity.y = 0.0f;
  }

  const XVECTOR3 forward = FlattenHorizontal(input.forward, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
  const XVECTOR3 right = FlattenHorizontal(input.right, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
  const float forwardMove = ForwardMoveAmount(input);
  const float rightMove = RightMoveAmount(input);
  XVECTOR3 wish(0.0f, 0.0f, 0.0f, 0.0f);
  wish += forward * forwardMove;
  wish += right * rightMove;

  float wishSpeed = 0.0f;
  if (LengthSq3(wish) > 0.000001f) {
    wish = NormalizeOr(wish, XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
    const float commandMagnitude = MoveCommandMagnitude(forwardMove, rightMove);
    const float maxSpeed = (input.sprint && m_settings.allowSprint) ? m_settings.sprintSpeed : m_settings.walkSpeed;
    wishSpeed = maxSpeed * commandMagnitude;
  }

  if (m_grounded) {
    ApplyGroundFriction(m_velocity, m_settings.friction, m_settings.stopSpeed, deltaSeconds);
    Accelerate(m_velocity, wish, wishSpeed, m_settings.groundAcceleration, deltaSeconds);
  } else if (m_settings.airControl) {
    Accelerate(m_velocity, wish, wishSpeed, m_settings.airAcceleration, deltaSeconds);
  }

  if (input.jump && !m_jumpHeld && m_grounded) {
    m_velocity.y = m_settings.jumpSpeed;
    m_grounded = false;
  }
  m_jumpHeld = input.jump;

  if (context.collisionWorld) {
    m_velocity.y -= m_settings.gravity * deltaSeconds;
  }

  XVECTOR3 displacement = m_velocity * deltaSeconds;
  displacement.w = 0.0f;
  if (context.collisionWorld) {
    if (m_grounded) {
      m_position = StepSlideCapsule(context, m_position, displacement, m_settings, &m_velocity);
    } else {
      m_position = SlideCapsule(
          context, m_position, displacement, m_settings.capsuleRadius, m_settings.capsuleHalfHeight, &m_velocity);
    }

    CharacterCollisionHit groundHit;
    const XVECTOR3 probe(0.0f, -m_settings.groundProbeDistance, 0.0f, 0.0f);
    if (SweepCapsule(context, m_position, probe, m_settings.capsuleRadius, m_settings.capsuleHalfHeight, groundHit) &&
        groundHit.normal.y >= m_settings.minWalkNormalY) {
      m_position += probe * ClampFloat(groundHit.fraction, 0.0f, 1.0f);
      m_grounded = true;
      if (m_velocity.y < 0.0f) {
        m_velocity.y = 0.0f;
      }
    } else {
      m_grounded = false;
    }
  } else {
    m_position += displacement;
    m_position.w = 1.0f;
  }
}

void KinematicCharacterController::UpdateQuake3(float deltaSeconds,
                                                const KinematicCharacterInput& input,
                                                const CharacterControllerContext& context) {
  deltaSeconds = ClampFloat(deltaSeconds, 0.0f, 0.2f);

  if (!input.jump) {
    m_jumpHeld = false;
  }

  const int forwardMove = Q3MoveCommand(ForwardMoveAmount(input));
  const int rightMove = Q3MoveCommand(RightMoveAmount(input));
  int upMove = input.jump ? 127 : 0;
  const float playerSpeed = (input.sprint && m_settings.allowSprint) ? m_settings.sprintSpeed : m_settings.walkSpeed;

  auto runAirMove = [&](float frameSeconds, const GroundTraceState& ground) {
    const float scale = CmdScale(forwardMove, rightMove, upMove, playerSpeed);
    XVECTOR3 forward = FlattenHorizontal(input.forward, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    XVECTOR3 right = FlattenHorizontal(input.right, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));

    XVECTOR3 wishVelocity = forward * static_cast<float>(forwardMove) + right * static_cast<float>(rightMove);
    wishVelocity.y = 0.0f;
    XVECTOR3 wishDir = wishVelocity;
    float wishSpeed = Length3(wishDir);
    if (wishSpeed > 0.000001f) {
      wishDir = wishDir * (1.0f / wishSpeed);
      wishSpeed *= scale;
      Accelerate(m_velocity, wishDir, wishSpeed, m_settings.airAcceleration, frameSeconds);
    }

    if (ground.groundPlane && !ground.walking) {
      m_velocity = ClipVelocity(m_velocity, ground.normal);
    }

    Q3StepSlideMove(context, m_position, m_velocity, frameSeconds, true, ground, m_settings);
  };

  auto runWalkMove = [&](float frameSeconds, const GroundTraceState& ground) {
    if (input.jump && !m_jumpHeld) {
      m_jumpHeld = true;
      m_grounded = false;
      m_velocity.y = m_settings.jumpSpeed;
      runAirMove(frameSeconds, GroundTraceState{});
      return;
    }

    if (input.jump && m_jumpHeld) {
      upMove = 0;
    }

    ApplyGroundFriction(m_velocity, m_settings.friction, m_settings.stopSpeed, frameSeconds);

    const float scale = CmdScale(forwardMove, rightMove, upMove, playerSpeed);
    XVECTOR3 forward = FlattenHorizontal(input.forward, XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f));
    XVECTOR3 right = FlattenHorizontal(input.right, XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f));
    forward = NormalizeOr(ClipVelocity(forward, ground.normal), forward);
    right = NormalizeOr(ClipVelocity(right, ground.normal), right);

    XVECTOR3 wishVelocity = forward * static_cast<float>(forwardMove) + right * static_cast<float>(rightMove);
    XVECTOR3 wishDir = wishVelocity;
    float wishSpeed = Length3(wishDir);
    if (wishSpeed > 0.000001f) {
      wishDir = wishDir * (1.0f / wishSpeed);
      wishSpeed *= scale;
      Accelerate(m_velocity, wishDir, wishSpeed, m_settings.groundAcceleration, frameSeconds);
    }

    const float velocityLength = Length3(m_velocity);
    m_velocity = ClipVelocity(m_velocity, ground.normal);
    const float clippedLength = Length3(m_velocity);
    if (clippedLength > 0.000001f) {
      m_velocity = m_velocity * (velocityLength / clippedLength);
    }

    if (HorizontalLengthSq(m_velocity) <= 0.00000001f) {
      m_velocity.x = 0.0f;
      m_velocity.z = 0.0f;
      return;
    }

    Q3StepSlideMove(context, m_position, m_velocity, frameSeconds, false, ground, m_settings);
  };

  float remaining = deltaSeconds;
  while (remaining > 0.000001f) {
    const float frameSeconds = (std::min)(remaining, 0.066f);
    remaining -= frameSeconds;

    GroundTraceState ground = TraceGround(context, m_position, m_velocity, m_settings);
    m_grounded = ground.walking;

    if (ground.walking) {
      runWalkMove(frameSeconds, ground);
    } else {
      runAirMove(frameSeconds, ground);
    }

    ground = TraceGround(context, m_position, m_velocity, m_settings);
    m_grounded = ground.walking;
    ApplyCharacterTriggerTouch(context, m_settings, m_position, m_velocity, m_grounded, m_lastTriggerEntityId);
    if (m_jumpHeld) {
      upMove = 20;
    }
    T8_LOG_VERBOSE(
        "[Q3Update] substep=%.5f center=(%.4f, %.4f, %.4f) eye=(%.4f, %.4f, %.4f) velocity=(%.4f, %.4f, %.4f) grounded=%d hasCollision=%d",
        frameSeconds,
        m_position.x,
        m_position.y,
        m_position.z,
        GetEyePosition().x,
        GetEyePosition().y,
        GetEyePosition().z,
        m_velocity.x,
        m_velocity.y,
        m_velocity.z,
        m_grounded ? 1 : 0,
        context.collisionWorld ? 1 : 0);

    const bool escaped = m_position.y < -64.0f ||
                         std::fabs(m_position.x) > 128.0f ||
                         std::fabs(m_position.z) > 128.0f;
    if (escaped && !m_q3EscapeDiagnosticActive) {
      const XVECTOR3 eye = GetEyePosition();
      T8_LOG_INFO(
          "[Q3Escape] character outside expected q3dm6 bounds center=(%.4f, %.4f, %.4f) eye=(%.4f, %.4f, %.4f) velocity=(%.4f, %.4f, %.4f) grounded=%d hasCollision=%d",
          m_position.x,
          m_position.y,
          m_position.z,
          eye.x,
          eye.y,
          eye.z,
          m_velocity.x,
          m_velocity.y,
          m_velocity.z,
          m_grounded ? 1 : 0,
          context.collisionWorld ? 1 : 0);
      m_q3EscapeDiagnosticActive = true;
    } else if (!escaped && m_q3EscapeDiagnosticActive) {
      T8_LOG_INFO(
          "[Q3Escape] character returned inside expected bounds center=(%.4f, %.4f, %.4f) velocity=(%.4f, %.4f, %.4f)",
          m_position.x,
          m_position.y,
          m_position.z,
          m_velocity.x,
          m_velocity.y,
          m_velocity.z);
      m_q3EscapeDiagnosticActive = false;
    }
  }
}

} // namespace t850
