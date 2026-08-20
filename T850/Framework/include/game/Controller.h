#pragma once

#include <game/InputFrame.h>

#include <cstdint>
#include <optional>
#include <string>

namespace t850::game {

struct GameObject;

enum class ControllerKind {
  None,
  Player,
  AI
};

struct MovementIntent {
  XVECTOR3 moveDir = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  float speedScale = 1.0f;
  XVECTOR3 lookDir = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
  uint32_t actionBits = 0;
  std::optional<XVECTOR3> navGoal;
  bool hasNavGoal = false;
};

class IController {
public:
  virtual ~IController() = default;
  virtual ControllerKind Kind() const = 0;
  virtual void OnPossess(GameObject& pawn) { pawn_ = &pawn; }
  virtual void OnUnpossess() { pawn_ = nullptr; }
  virtual MovementIntent SampleIntent(const InputFrame& input, float fixedDt) = 0;

protected:
  GameObject* pawn_ = nullptr;
};

class PlayerController final : public IController {
public:
  explicit PlayerController(int playerSlot = 0, std::string profile = {});

  ControllerKind Kind() const override { return ControllerKind::Player; }
  MovementIntent SampleIntent(const InputFrame& input, float fixedDt) override;
  int PlayerSlot() const { return playerSlot_; }
  std::string_view Profile() const { return profile_; }

private:
  int playerSlot_ = 0;
  std::string profile_;
  XVECTOR3 lookDirection_ = XVECTOR3(0.0f, 0.0f, 1.0f, 0.0f);
};

class AIController final : public IController {
public:
  explicit AIController(std::string profile = {});

  ControllerKind Kind() const override { return ControllerKind::AI; }
  MovementIntent SampleIntent(const InputFrame& input, float fixedDt) override;
  void SetNavigationGoal(const XVECTOR3& goal);
  void ClearNavigationGoal();
  std::string_view Profile() const { return profile_; }

private:
  std::string profile_;
  std::optional<XVECTOR3> navigationGoal_;
};

} // namespace t850::game