#pragma once

#include <utils/xMaths.h>

#include <cstdint>

namespace t850 {

struct CharacterCollisionSweep {
  XVECTOR3 startCenter = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 displacement = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  float radius = 0.35f;
  float halfHeight = 0.55f;
};

struct CharacterBoxSweep {
  XVECTOR3 startCenter = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 displacement = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  XVECTOR3 halfExtents = XVECTOR3(0.35f, 0.90f, 0.35f, 0.0f);
};

struct CharacterCollisionHit {
  bool hit = false;
  float fraction = 1.0f;
  XVECTOR3 position = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 normal = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
  uint32_t entityId = 0;
};

struct CharacterTriggerQuery {
  XVECTOR3 center = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 halfExtents = XVECTOR3(0.35f, 0.90f, 0.35f, 0.0f);
};

struct CharacterTriggerTouch {
  enum class Type : uint8_t {
    JumpPad
  };

  Type type = Type::JumpPad;
  uint32_t entityId = 0;
  XVECTOR3 velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
};

class CharacterCollisionWorld {
public:
  virtual ~CharacterCollisionWorld() = default;
  virtual bool SweepCapsule(const CharacterCollisionSweep& sweep, CharacterCollisionHit& outHit) const = 0;
  virtual bool SweepBox(const CharacterBoxSweep& sweep, CharacterCollisionHit& outHit) const { return false; }
  virtual bool QueryTriggerTouch(const CharacterTriggerQuery& query, CharacterTriggerTouch& outTouch) const { return false; }
};

struct CharacterControllerContext {
  const CharacterCollisionWorld* collisionWorld = nullptr;
};

struct KinematicCharacterSettings {
  enum class CollisionShape : uint8_t {
    Capsule,
    Box
  };

  CollisionShape collisionShape = CollisionShape::Capsule;
  float walkSpeed = 8.0f;
  float sprintSpeed = 10.0f;
  float groundAcceleration = 30.0f;
  float airAcceleration = 3.0f;
  float friction = 8.0f;
  // Horizontal drag applied while airborne (0 = no drag, Quake-style). Minecraft
  // stops almost instantly even in the air, so it sets this > 0.
  float airFriction = 0.0f;
  float stopSpeed = 2.0f;
  float gravity = 18.0f;
  float jumpSpeed = 5.0f;
  float mouseSensitivity = 0.005f;
  float capsuleRadius = 0.35f;
  float capsuleHalfHeight = 0.55f;
  float eyeHeight = 1.60f;
  float groundProbeDistance = 0.25f;
  float stepHeight = 0.35f;
  float minWalkNormalY = 0.70f;
  bool allowSprint = true;
  bool airControl = true;
};

struct KinematicCharacterInput {
  bool moveForward = false;
  bool moveBackward = false;
  bool moveLeft = false;
  bool moveRight = false;
  float moveForwardAmount = 0.0f;
  float moveRightAmount = 0.0f;
  bool jump = false;
  bool sprint = false;
  XVECTOR3 forward = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  XVECTOR3 right = XVECTOR3(1.0f, 0.0f, 0.0f, 0.0f);
};

KinematicCharacterSettings MakeQuake3CharacterSettings();

class KinematicCharacterController {
public:
  explicit KinematicCharacterController(const KinematicCharacterSettings& settings = KinematicCharacterSettings{});

  void SetSettings(const KinematicCharacterSettings& settings);
  const KinematicCharacterSettings& GetSettings() const { return m_settings; }

  void Reset();
  void SetPosition(const XVECTOR3& capsuleCenter);
  const XVECTOR3& GetPosition() const { return m_position; }
  void SetEyePosition(const XVECTOR3& eyePosition);
  XVECTOR3 GetEyePosition() const;

  void SetVelocity(const XVECTOR3& velocity);
  const XVECTOR3& GetVelocity() const { return m_velocity; }
  bool IsGrounded() const { return m_grounded; }

  void UpdateFps(float deltaSeconds, const KinematicCharacterInput& input, const CharacterControllerContext& context);
  void UpdateQuake3(float deltaSeconds, const KinematicCharacterInput& input, const CharacterControllerContext& context);

private:
  float CapsuleCenterOffsetFromEye() const;

  KinematicCharacterSettings m_settings;
  XVECTOR3 m_position = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 m_velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  bool m_grounded = false;
  bool m_jumpHeld = false;
  bool m_q3EscapeDiagnosticActive = false;
  uint32_t m_lastTriggerEntityId = 0;
};

} // namespace t850
