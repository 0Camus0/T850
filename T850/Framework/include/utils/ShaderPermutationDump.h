#pragma once

#include <Descriptors.h>

#include <string>

namespace t850::ShaderPermutationDump {

void Begin(const std::string& outputPath);
bool IsEnabled();
void Record(const ShaderKey& key,
            const std::string& vertexShader,
            const std::string& fragmentShader,
            const std::string& defines);
bool Flush();

} // namespace t850::ShaderPermutationDump
