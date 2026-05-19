#pragma once

#include <physics/CharacterController.h>
#include <utils/Camera.h>
#include <utils/InputManager.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace t850 {

enum class CameraProfileType : uint8_t {
  Orbit = 0,
  FreeFly,
  CollidingFly,
  GroundedFps,
  CodFps,
  Quake3Fps,
  Count
};

const char* CameraProfileName(CameraProfileType type);
CameraProfileType CameraProfileTypeFromIndex(int index);
int CameraProfileIndex(CameraProfileType type);
std::vector<std::string> CameraProfileNames();

struct CameraInputState {
  bool moveForward = false;
  bool moveBackward = false;
  bool moveLeft = false;
  bool moveRight = false;
  bool moveUp = false;
  bool moveDown = false;
  bool jump = false;
  bool crouch = false;
  bool sprint = false;

  bool mouseLook = false;
  bool orbitRotate = false;
  bool orbitPan = false;
  bool orbitZoom = false;
  float mouseDeltaX = 0.0f;
  float mouseDeltaY = 0.0f;
  float scrollDelta = 0.0f;
};

using CameraCollisionSweep = CharacterCollisionSweep;
using CameraCollisionHit = CharacterCollisionHit;
using CameraCollisionWorld = CharacterCollisionWorld;

struct CameraUpdateContext {
  const CameraCollisionWorld* collisionWorld = nullptr;
};

struct OrbitCameraState {
  XVECTOR3 target = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 panOffset = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  float yaw = 0.0f;
  float pitch = 0.0f;
  float distance = 5.0f;
  float modelRadius = 1.0f;
};

class CameraProfile {
public:
  virtual ~CameraProfile() = default;

  virtual CameraProfileType GetType() const = 0;
  virtual const char* GetName() const = 0;
  virtual void OnActivated(Camera& camera);
  virtual void HandleInput(Camera& camera, const CameraInputState& input) = 0;
  virtual void Update(Camera& camera, float deltaSeconds, const CameraUpdateContext& context) = 0;
};

class OrbitCameraProfile final : public CameraProfile {
public:
  CameraProfileType GetType() const override { return CameraProfileType::Orbit; }
  const char* GetName() const override { return CameraProfileName(GetType()); }
  void OnActivated(Camera& camera) override;
  void HandleInput(Camera& camera, const CameraInputState& input) override;
  void Update(Camera& camera, float deltaSeconds, const CameraUpdateContext& context) override;

  void SetState(const OrbitCameraState& state);
  const OrbitCameraState& GetState() const { return m_state; }

private:
  OrbitCameraState m_state;
  CameraInputState m_input;
};

class FreeFlyCameraProfile : public CameraProfile {
public:
  explicit FreeFlyCameraProfile(CameraProfileType type = CameraProfileType::FreeFly);

  CameraProfileType GetType() const override { return m_type; }
  const char* GetName() const override { return CameraProfileName(GetType()); }
  void OnActivated(Camera& camera) override;
  void HandleInput(Camera& camera, const CameraInputState& input) override;
  void Update(Camera& camera, float deltaSeconds, const CameraUpdateContext& context) override;

protected:
  virtual bool UsesCollision() const { return false; }
  virtual float MoveSpeed(const CameraInputState& input) const;

  CameraProfileType m_type;
  CameraInputState m_input;
  float m_walkSpeed = 10.0f;
  float m_sprintSpeed = 20.0f;
  float m_mouseSensitivity = 0.005f;
  float m_collisionRadius = 0.30f;
  float m_collisionHalfHeight = 0.20f;
};

class CollidingFlyCameraProfile final : public FreeFlyCameraProfile {
public:
  CollidingFlyCameraProfile();

protected:
  bool UsesCollision() const override { return true; }
};

class KinematicFpsCameraProfile : public CameraProfile {
public:
  using Settings = KinematicCharacterSettings;

  KinematicFpsCameraProfile(CameraProfileType type, const Settings& settings);

  CameraProfileType GetType() const override { return m_type; }
  const char* GetName() const override { return CameraProfileName(GetType()); }
  void OnActivated(Camera& camera) override;
  void HandleInput(Camera& camera, const CameraInputState& input) override;
  void Update(Camera& camera, float deltaSeconds, const CameraUpdateContext& context) override;

protected:
  CameraProfileType m_type;
  Settings m_settings;
  KinematicCharacterController m_character;
  CameraInputState m_input;
};

class GroundedFpsCameraProfile final : public KinematicFpsCameraProfile {
public:
  GroundedFpsCameraProfile();
};

class CodFpsCameraProfile final : public KinematicFpsCameraProfile {
public:
  CodFpsCameraProfile();
};

class Quake3FpsCameraProfile final : public KinematicFpsCameraProfile {
public:
  Quake3FpsCameraProfile();
  void Update(Camera& camera, float deltaSeconds, const CameraUpdateContext& context) override;
};

class CameraController {
public:
  CameraController();

  void AttachCamera(Camera* camera);
  Camera* GetCamera() const { return m_camera; }

  bool SetActiveProfile(CameraProfileType type);
  bool SetActiveProfileByIndex(int index);
  CameraProfileType GetActiveProfileType() const { return m_activeType; }
  int GetActiveProfileIndex() const { return CameraProfileIndex(m_activeType); }
  CameraProfile* GetActiveProfile();
  const CameraProfile* GetActiveProfile() const;
  CameraProfile* GetProfile(CameraProfileType type);
  const CameraProfile* GetProfile(CameraProfileType type) const;
  OrbitCameraProfile* GetOrbitProfile();
  const OrbitCameraProfile* GetOrbitProfile() const;

  void HandleInput(const CameraInputState& input);
  void Update(float deltaSeconds, const CameraUpdateContext& context);

private:
  using ProfileArray = std::array<std::unique_ptr<CameraProfile>, static_cast<std::size_t>(CameraProfileType::Count)>;

  ProfileArray m_profiles;
  Camera* m_camera = nullptr;
  CameraProfileType m_activeType = CameraProfileType::Orbit;
};

} // namespace t850
