#pragma once

#include <game/Component.h>
#include <game/GameNavigationService.h>
#include <scene/EditorSceneFile.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace t850::game {
class AIController;
class ComponentFactoryRegistry;
}

namespace t850::game::examples {

class PathFollowComponent final : public Component {
public:
  explicit PathFollowComponent(t850::scene::SceneComponentDesc descriptor);

  std::string_view Type() const override { return "path_follow"; }
  ComponentUpdatePhase Phase() const override { return ComponentUpdatePhase::Logic; }
  void OnCreate() override;
  void Update(float fixedDt) override;
  bool TryGetFloat(std::string_view name, float& value) const override;

private:
  void BeginDestination(const XVECTOR3& goal);
  void IssueSteeringGoal(AIController& controller, const XVECTOR3& goal);
  void CompleteDestination(AIController& controller);
  void DrainSupersededResults();

  t850::scene::SceneComponentDesc descriptor_;
  std::optional<XVECTOR3> destination_;
  std::optional<XVECTOR3> issuedSteeringGoal_;
  std::vector<XVECTOR3> pathPoints_;
  std::vector<uint64_t> supersededRequestIds_;
  std::size_t waypointIndex_ = 0;
  uint64_t requestId_ = GameNavigationService::kInvalidRequestId;
  float retryRemainingSeconds_ = 0.0f;
  float followDistance_ = 0.0f;
  float sideOffset_ = 0.0f;
  float formationDepthStep_ = 0.0f;
  int formationSlot_ = -1;
};

std::unique_ptr<Component> CreatePathFollowComponent(
    const t850::scene::SceneComponentDesc& descriptor, ComponentLoadContext& context);
void RegisterPathFollowComponent(ComponentFactoryRegistry& registry);

} // namespace t850::game::examples