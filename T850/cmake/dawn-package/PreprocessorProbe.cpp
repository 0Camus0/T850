#include <utils/ShaderPreprocessor.h>

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
  try {
    const std::string source = R"(
#ifdef OUTER
#if defined(INNER) && COUNT > 1
let selected = 1u;
#else
let selected = 2u;
#endif
#else
let selected = 3u;
#endif
@group(0) @binding(1) var tex: texture_2d<f32>;
fn value() -> vec3<f32> { return vec3<f32>(1.0, -2.0, 1e-3); }
)";
    std::string output;
    std::string diagnostic;
    for (const int mode : {1, 2, 3}) {
      const auto defines = mode == 1 ? "#define OUTER\n#define INNER\n#define COUNT 2\n"
        : mode == 2 ? "#define OUTER\n" : "#define INNER\n";
      if (!t850::PreprocessShader(source, "nested", defines, output, diagnostic)) throw std::runtime_error(diagnostic);
      if (output.find("selected = " + std::to_string(mode) + "u") == std::string::npos || output.find('#') != std::string::npos)
        throw std::runtime_error("Nested condition or WGSL token output failed");
      if (output.find("1e-3") == std::string::npos || output.find("@ group") == std::string::npos)
        throw std::runtime_error("WGSL token preservation failed");
    }
    for (const std::string invalid : {"#ifdef OUTER\n", "#endif\n", "#if 1\n#else\n#else\n#endif\n",
         "#include \"missing\"\n", "#pragma once\n", "#error intentional\n", "__DATE__\n"}) {
      if (t850::PreprocessShader(invalid, "invalid", "", output, diagnostic) || diagnostic.empty())
        throw std::runtime_error("Invalid template was accepted");
    }
    std::cout << "Shader preprocessor PASS: nested conditions, expressions, WGSL tokens, invalid input\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}