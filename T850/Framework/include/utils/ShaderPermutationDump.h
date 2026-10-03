#pragma once

#include <Descriptors.h>

#include <string>
#include <vector>

namespace t850::ShaderPermutationDump {

void Begin(const std::string& outputPath);
bool IsEnabled();
void Record(const ShaderKey& key,
            const std::string& vertexShader,
            const std::string& fragmentShader,
            const std::string& defines);
void RecordCompute(const std::string& computeShader,
                   const std::string& entryPoint,
                   const std::string& permutationName,
                   const std::vector<std::string>& defines);
bool Flush();

} // namespace t850::ShaderPermutationDump
