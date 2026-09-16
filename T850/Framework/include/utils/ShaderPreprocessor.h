#pragma once

#include <string>

namespace t850 {
bool PreprocessShader(const std::string& source, const std::string& name,
                      const std::string& defines, std::string& output, std::string& diagnostic);
}