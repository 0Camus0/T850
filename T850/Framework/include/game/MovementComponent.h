#pragma once

#include <game/Component.h>
#include <scene/EditorSceneFile.h>
#include <utils/xMaths.h>

#include <string_view>

namespace t850::game {

class MovementComponent final : public Component {
public:
  explicit MovementComponent(t850::scene::SceneComponentDesc descriptor);

  std::string_view Type() const override { return "movement"; }
  ComponentUpdatePhase Phase() const override { return ComponentUpdatePhase::PrePhysics; }
  void OnCreate() override;
  void Update(float fixedDt) override;

private:
  enum class Mode {
    Kinematic,
    Dynamic,
    TransformOnly
  };

  t850::scene::SceneComponentDesc descriptor_;
  Mode mode_ = Mode::Kinematic;
  float maxSpeed_ = 6.0f;
  float accel_ = 40.0f;
  float turnRateRadPerSec_ = 8.0f;
  XVECTOR3 velocity_ = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
};

std::unique_ptr<Component> CreateMovementComponent(
    const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context);

} // namespace t850::game