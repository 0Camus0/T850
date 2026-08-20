#pragma once

#include <cstdint>
#include <string_view>

namespace t850 {

enum class GameplayLayer : uint16_t {
  WorldStatic,
  WorldDynamic,
  Player,
  NPC,
  Projectile,
  Trigger,
  Ragdoll,
  Debris,
  CameraBlocker,
  Count
};

constexpr uint32_t GameplayLayerBit(GameplayLayer layer) {
  return 1u << static_cast<uint32_t>(layer);
}

GameplayLayer GameplayLayerFromString(std::string_view value, GameplayLayer fallback);

} // namespace t850