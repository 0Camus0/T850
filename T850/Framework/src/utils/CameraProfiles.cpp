#include <pch.h>

#include <utils/CameraProfiles.h>
#include <utils/Log.h>

#include <algorithm>
#include <cmath>

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

float CombineAxis(float analogAmount, bool positive, bool negative) {
  float value = ClampFloat(analogAmount, -1.0f, 1.0f);
  if (positive) {
    value += 1.0f;
  }
  if (negative) {
    value -= 1.0f;
  }
  return ClampFloat(value, -1.0f, 1.0f);
}

float ClampCommandLength(float x, float y, float z = 0.0f) {
  return ClampFloat(std::sqrt(x * x + y * y + z * z), 0.0f, 1.0f);
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

void RefreshCamera(Camera& camera) {
  camera.Velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  camera.Update(0.0f);
}

void ApplyMouseLook(Camera& camera, const CameraInputState& input, float sensitivity) {
  if (!input.mouseLook) {
    return;
  }
  const float yawStep = input.mouseDeltaX * sensitivity;
  const float pitchStep = input.mouseDeltaY * sensitivity;
  T8_LOG_VERBOSE("[CameraMouse] delta=(%.3f,%.3f) sensitivity=%.6f yawStep=%.6f pitchStep=%.6f before=(yaw=%.6f,pitch=%.6f,roll=%.6f)",
                 input.mouseDeltaX,
                 input.mouseDeltaY,
                 sensitivity,
                 yawStep,
                 pitchStep,
                 camera.Yaw,
                 camera.Pitch,
                 camera.Roll);
  camera.MoveYaw(yawStep);
  camera.MovePitch(pitchStep);
  T8_LOG_VERBOSE("[CameraMouse] after=(yaw=%.6f,pitch=%.6f,roll=%.6f)",
                 camera.Yaw,
                 camera.Pitch,
                 camera.Roll);
}

KinematicCharacterInput BuildCharacterInput(const Camera& camera, const CameraInputState& input) {
  KinematicCharacterInput characterInput;
  characterInput.moveForward = input.moveForward;
  characterInput.moveBackward = input.moveBackward;
  characterInput.moveLeft = input.moveLeft;
  characterInput.moveRight = input.moveRight;
  characterInput.moveForwardAmount = ClampFloat(input.moveForwardAmount, -1.0f, 1.0f);
  characterInput.moveRightAmount = ClampFloat(input.moveRightAmount, -1.0f, 1.0f);
  characterInput.jump = input.jump;
  characterInput.sprint = input.sprint;
  characterInput.forward = camera.Look;
  characterInput.forward.w = 0.0f;
  characterInput.right = camera.Right;
  characterInput.right.w = 0.0f;
  return characterInput;
}

bool SweepCapsule(const CameraUpdateContext& context,
                  const XVECTOR3& startCenter,
                  const XVECTOR3& displacement,
                  float radius,
                  float halfHeight,
                  CameraCollisionHit& outHit) {
  if (!context.collisionWorld || LengthSq3(displacement) <= 0.00000001f) {
    return false;
  }

  CameraCollisionSweep sweep;
  sweep.startCenter = startCenter;
  sweep.displacement = displacement;
  sweep.radius = radius;
  sweep.halfHeight = halfHeight;
  return context.collisionWorld->SweepCapsule(sweep, outHit) && outHit.hit;
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

XVECTOR3 SlideCapsule(const CameraUpdateContext& context,
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

    CameraCollisionHit hit;
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

} // namespace

const char* CameraProfileName(CameraProfileType type) {
  switch (type) {
  case CameraProfileType::Orbit: return "Orbit";
  case CameraProfileType::FreeFly: return "Free Fly";
  case CameraProfileType::CollidingFly: return "Colliding Fly";
  case CameraProfileType::GroundedFps: return "Grounded FPS";
  case CameraProfileType::CodFps: return "COD FPS";
  case CameraProfileType::Quake3Fps: return "Quake 3 FPS";
  default: return "Unknown";
  }
}

CameraProfileType CameraProfileTypeFromIndex(int index) {
  const int count = static_cast<int>(CameraProfileType::Count);
  if (index < 0 || index >= count) {
    return CameraProfileType::Orbit;
  }
  return static_cast<CameraProfileType>(index);
}

int CameraProfileIndex(CameraProfileType type) {
  const int index = static_cast<int>(type);
  const int count = static_cast<int>(CameraProfileType::Count);
  return index >= 0 && index < count ? index : 0;
}

std::vector<std::string> CameraProfileNames() {
  std::vector<std::string> names;
  names.reserve(static_cast<std::size_t>(CameraProfileType::Count));
  for (int i = 0; i < static_cast<int>(CameraProfileType::Count); ++i) {
    names.push_back(CameraProfileName(CameraProfileTypeFromIndex(i)));
  }
  return names;
}

void ApplyGamepadToCameraInput(CameraInputState& state,
                               const InputManager& input,
                               float deltaSeconds,
                               bool allowLook) {
  const GamepadInputState& gamepad = input.Gamepad;
  if (!gamepad.connected || !gamepad.enabled) {
    return;
  }

  constexpr float kMoveThreshold = 0.12f;
  constexpr float kLookThreshold = 0.08f;
  constexpr float kLookYawMouseDeltaPerSecond = 520.0f;
  constexpr float kLookPitchMouseDeltaPerSecond = 440.0f;

  state.moveForwardAmount = ClampFloat(state.moveForwardAmount - gamepad.leftY, -1.0f, 1.0f);
  state.moveRightAmount = ClampFloat(state.moveRightAmount + gamepad.leftX, -1.0f, 1.0f);
  state.moveForward = state.moveForward || gamepad.leftY < -kMoveThreshold;
  state.moveBackward = state.moveBackward || gamepad.leftY > kMoveThreshold;
  state.moveLeft = state.moveLeft || gamepad.leftX < -kMoveThreshold;
  state.moveRight = state.moveRight || gamepad.leftX > kMoveThreshold;

  state.jump = state.jump || gamepad.buttonSouth;
  state.crouch = state.crouch || gamepad.buttonEast;
  state.sprint = state.sprint || gamepad.leftStick;

  if (allowLook &&
      (std::fabs(gamepad.rightX) > kLookThreshold || std::fabs(gamepad.rightY) > kLookThreshold)) {
    state.mouseLook = true;
    state.mouseDeltaX += gamepad.rightX * kLookYawMouseDeltaPerSecond * deltaSeconds;
    state.mouseDeltaY += gamepad.rightY * kLookPitchMouseDeltaPerSecond * deltaSeconds;
  }
}

void CameraProfile::OnActivated(Camera& camera) {
  RefreshCamera(camera);
}

void OrbitCameraProfile::OnActivated(Camera& camera) {
  CameraProfile::OnActivated(camera);
  Update(camera, 0.0f, CameraUpdateContext{});
}

void OrbitCameraProfile::SetState(const OrbitCameraState& state) {
  m_state = state;
  m_state.distance = (std::max)(0.001f, m_state.distance);
  m_state.modelRadius = (std::max)(0.001f, m_state.modelRadius);
}

void OrbitCameraProfile::HandleInput(Camera&, const CameraInputState& input) {
  m_input = input;
}

void OrbitCameraProfile::Update(Camera& camera, float, const CameraUpdateContext&) {
  if (m_input.orbitRotate) {
    m_state.yaw += m_input.mouseDeltaX * 0.005f;
    m_state.pitch += m_input.mouseDeltaY * 0.005f;
    const float maxPitch = Deg2Rad(89.0f);
    m_state.pitch = ClampFloat(m_state.pitch, -maxPitch, maxPitch);
  }

  if (m_input.orbitZoom) {
    m_state.distance -= m_input.mouseDeltaY * 0.02f * m_state.modelRadius;
  }
  if (m_input.scrollDelta != 0.0f) {
    m_state.distance -= m_input.scrollDelta * 0.15f * m_state.modelRadius;
  }
  m_state.distance = (std::max)(m_state.modelRadius * 0.05f, m_state.distance);

  if (m_input.orbitPan) {
    const float panSpeed = m_state.distance * 0.002f;
    m_state.panOffset += camera.Right * (-m_input.mouseDeltaX * panSpeed);
    m_state.panOffset += camera.Up * (m_input.mouseDeltaY * panSpeed);
    m_state.panOffset.w = 0.0f;
  }

  const XVECTOR3 target = m_state.target + m_state.panOffset;
  const float cy = std::cos(m_state.yaw);
  const float sy = std::sin(m_state.yaw);
  const float cp = std::cos(m_state.pitch);
  const float sp = std::sin(m_state.pitch);
  const XVECTOR3 offset(sy * cp, sp, cy * cp, 0.0f);
  camera.Eye = target + offset * m_state.distance;
  camera.Eye.w = 1.0f;
  camera.Velocity = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  camera.SetLookAt(target);
}

FreeFlyCameraProfile::FreeFlyCameraProfile(CameraProfileType type)
    : m_type(type) {}

void FreeFlyCameraProfile::OnActivated(Camera& camera) {
  CameraProfile::OnActivated(camera);
}

void FreeFlyCameraProfile::HandleInput(Camera&, const CameraInputState& input) {
  m_input = input;
}

float FreeFlyCameraProfile::MoveSpeed(const CameraInputState& input) const {
  return input.sprint ? m_sprintSpeed : m_walkSpeed;
}

void FreeFlyCameraProfile::Update(Camera& camera, float deltaSeconds, const CameraUpdateContext& context) {
  ApplyMouseLook(camera, m_input, m_mouseSensitivity);
  RefreshCamera(camera);

  const float forwardAmount = CombineAxis(m_input.moveForwardAmount, m_input.moveForward, m_input.moveBackward);
  const float rightAmount = CombineAxis(m_input.moveRightAmount, m_input.moveRight, m_input.moveLeft);
  const float upAmount = (m_input.moveUp ? 1.0f : 0.0f) + (m_input.moveDown ? -1.0f : 0.0f);
  const float moveAmount = ClampCommandLength(forwardAmount, rightAmount, upAmount);
  XVECTOR3 move(0.0f, 0.0f, 0.0f, 0.0f);
  move += camera.Look * forwardAmount;
  move += camera.Right * rightAmount;
  move += camera.Up * upAmount;
  move.w = 0.0f;

  if (LengthSq3(move) > 0.000001f) {
    move = NormalizeOr(move, XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f));
    const XVECTOR3 displacement = move * (MoveSpeed(m_input) * moveAmount * deltaSeconds);
    if (UsesCollision() && context.collisionWorld) {
      camera.Eye = SlideCapsule(context, camera.Eye, displacement, m_collisionRadius, m_collisionHalfHeight);
    } else {
      camera.Eye += displacement;
    }
    camera.Eye.w = 1.0f;
  }

  RefreshCamera(camera);
}

CollidingFlyCameraProfile::CollidingFlyCameraProfile()
    : FreeFlyCameraProfile(CameraProfileType::CollidingFly) {
  m_walkSpeed = 8.0f;
  m_sprintSpeed = 16.0f;
  m_collisionRadius = 0.35f;
  m_collisionHalfHeight = 0.55f;
}

KinematicFpsCameraProfile::KinematicFpsCameraProfile(CameraProfileType type, const Settings& settings)
    : m_type(type),
      m_settings(settings),
      m_character(settings) {}

void KinematicFpsCameraProfile::OnActivated(Camera& camera) {
  CameraProfile::OnActivated(camera);
  m_character.SetSettings(m_settings);
  m_character.Reset();
  m_character.SetEyePosition(camera.Eye);
}

void KinematicFpsCameraProfile::HandleInput(Camera&, const CameraInputState& input) {
  m_input = input;
}

void KinematicFpsCameraProfile::SetSettings(const Settings& settings) {
  m_settings = settings;
  m_character.SetSettings(m_settings);
}

void KinematicFpsCameraProfile::Update(Camera& camera, float deltaSeconds, const CameraUpdateContext& context) {
  ApplyMouseLook(camera, m_input, m_settings.mouseSensitivity);
  RefreshCamera(camera);
  m_character.SetSettings(m_settings);
  m_character.SetEyePosition(camera.Eye);
  m_character.UpdateFps(deltaSeconds, BuildCharacterInput(camera, m_input), CharacterControllerContext{context.collisionWorld});
  camera.Eye = m_character.GetEyePosition();
  camera.Eye.w = 1.0f;
  RefreshCamera(camera);
}

GroundedFpsCameraProfile::GroundedFpsCameraProfile()
    : KinematicFpsCameraProfile(CameraProfileType::GroundedFps, Settings{}) {}

CodFpsCameraProfile::CodFpsCameraProfile()
    : KinematicFpsCameraProfile(CameraProfileType::CodFps, [] {
        Settings s;
        s.walkSpeed = 7.0f;
        s.sprintSpeed = 11.0f;
        s.groundAcceleration = 45.0f;
        s.airAcceleration = 1.5f;
        s.friction = 12.0f;
        s.stopSpeed = 2.5f;
        s.jumpSpeed = 4.3f;
        s.gravity = 20.0f;
        return s;
      }()) {}

Quake3FpsCameraProfile::Quake3FpsCameraProfile()
    : KinematicFpsCameraProfile(CameraProfileType::Quake3Fps, MakeQuake3CharacterSettings()) {}

void Quake3FpsCameraProfile::Update(Camera& camera, float deltaSeconds, const CameraUpdateContext& context) {
  ApplyMouseLook(camera, m_input, m_settings.mouseSensitivity);
  RefreshCamera(camera);
  m_character.SetSettings(m_settings);
  m_character.SetEyePosition(camera.Eye);
  m_character.UpdateQuake3(deltaSeconds, BuildCharacterInput(camera, m_input), CharacterControllerContext{context.collisionWorld});
  camera.Eye = m_character.GetEyePosition();
  camera.Eye.w = 1.0f;
  RefreshCamera(camera);
}

CameraController::CameraController() {
  m_profiles[CameraProfileIndex(CameraProfileType::Orbit)] = std::make_unique<OrbitCameraProfile>();
  m_profiles[CameraProfileIndex(CameraProfileType::FreeFly)] = std::make_unique<FreeFlyCameraProfile>();
  m_profiles[CameraProfileIndex(CameraProfileType::CollidingFly)] = std::make_unique<CollidingFlyCameraProfile>();
  m_profiles[CameraProfileIndex(CameraProfileType::GroundedFps)] = std::make_unique<GroundedFpsCameraProfile>();
  m_profiles[CameraProfileIndex(CameraProfileType::CodFps)] = std::make_unique<CodFpsCameraProfile>();
  m_profiles[CameraProfileIndex(CameraProfileType::Quake3Fps)] = std::make_unique<Quake3FpsCameraProfile>();
}

void CameraController::AttachCamera(Camera* camera) {
  m_camera = camera;
  if (m_camera && GetActiveProfile()) {
    GetActiveProfile()->OnActivated(*m_camera);
  }
}

bool CameraController::SetActiveProfile(CameraProfileType type) {
  if (!GetProfile(type)) {
    return false;
  }

  m_activeType = type;
  if (m_camera) {
    GetActiveProfile()->OnActivated(*m_camera);
  }
  return true;
}

bool CameraController::SetActiveProfileByIndex(int index) {
  return SetActiveProfile(CameraProfileTypeFromIndex(index));
}

bool CameraController::SetKinematicProfileSettings(CameraProfileType type, const KinematicCharacterSettings& settings) {
  CameraProfile* profile = GetProfile(type);
  auto* kinematic = dynamic_cast<KinematicFpsCameraProfile*>(profile);
  if (!kinematic) {
    return false;
  }
  kinematic->SetSettings(settings);
  if (m_camera && m_activeType == type) {
    kinematic->OnActivated(*m_camera);
  }
  return true;
}

CameraProfile* CameraController::GetActiveProfile() {
  return GetProfile(m_activeType);
}

const CameraProfile* CameraController::GetActiveProfile() const {
  return GetProfile(m_activeType);
}

CameraProfile* CameraController::GetProfile(CameraProfileType type) {
  const int index = CameraProfileIndex(type);
  return m_profiles[static_cast<std::size_t>(index)].get();
}

const CameraProfile* CameraController::GetProfile(CameraProfileType type) const {
  const int index = CameraProfileIndex(type);
  return m_profiles[static_cast<std::size_t>(index)].get();
}

OrbitCameraProfile* CameraController::GetOrbitProfile() {
  return static_cast<OrbitCameraProfile*>(GetProfile(CameraProfileType::Orbit));
}

const OrbitCameraProfile* CameraController::GetOrbitProfile() const {
  return static_cast<const OrbitCameraProfile*>(GetProfile(CameraProfileType::Orbit));
}

void CameraController::HandleInput(const CameraInputState& input) {
  if (m_camera && GetActiveProfile()) {
    GetActiveProfile()->HandleInput(*m_camera, input);
  }
}

void CameraController::ClearInput() {
  HandleInput(CameraInputState{});
}

void CameraController::Update(float deltaSeconds, const CameraUpdateContext& context) {
  if (m_camera && GetActiveProfile()) {
    GetActiveProfile()->Update(*m_camera, deltaSeconds, context);
  }
}

} // namespace t850
