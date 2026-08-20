#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace t850::game {

using RuntimeGameObjectId = uint32_t;
constexpr RuntimeGameObjectId kInvalidRuntimeGameObjectId = 0;

std::string MakeStableId(std::string_view prefix);

} // namespace t850::game