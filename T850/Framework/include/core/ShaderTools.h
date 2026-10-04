#pragma once

#include <optional>

namespace t850 {

class Config;

std::optional<int> RunShaderPrecompileCommand(const Config& config);
void BeginShaderPermutationRecording(const Config& config);

}