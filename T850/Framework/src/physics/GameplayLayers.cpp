#include <pch.h>

#include <physics/GameplayLayers.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace t850 {

GameplayLayer GameplayLayerFromString(std::string_view value, GameplayLayer fallback) {
  std::string normalized(value);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });

  if (normalized == "world" || normalized == "world_static" || normalized == "static") {
    return GameplayLayer::WorldStatic;
  }
  if (normalized == "dynamic" || normalized == "world_dynamic") return GameplayLayer::WorldDynamic;
  if (normalized == "player") return GameplayLayer::Player;
  if (normalized == "npc" || normalized == "enemy") return GameplayLayer::NPC;
  if (normalized == "projectile") return GameplayLayer::Projectile;
  if (normalized == "trigger" || normalized == "sensor") return GameplayLayer::Trigger;
  if (normalized == "ragdoll") return GameplayLayer::Ragdoll;
  if (normalized == "debris") return GameplayLayer::Debris;
  if (normalized == "camera_blocker" || normalized == "camera") return GameplayLayer::CameraBlocker;
  return fallback;
}

} // namespace t850